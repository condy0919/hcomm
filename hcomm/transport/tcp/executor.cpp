// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/tcp/executor.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <mutex>
#include <queue>
#include <utility>

#include "hcomm/base/unique_fd.hpp"

namespace hcomm {
namespace tcp {
/// Holds registration details for an I/O event on a specific file descriptor.
///
/// This struct stores the file descriptor, the events we are interested in, and the wakers to be invoked when read or
/// write events occur.
struct IORegistration {
    IORegistration() = default;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    IORegistration(int f, std::uint32_t events) : fd(f), interested(events) {}

    int fd = -1;
    std::uint32_t interested = 0;
    Waker read_waker;
    Waker write_waker;
};

/// A wrapper that transforms a `PendingTask` into a schedulable and wakable unit.
///
/// This class implements both `WakerImpl` and `Context`, allowing the task it holds to interact with the executor.
/// When the task is woken (`wake()` is called), it reschedules itself on the executor's ready queue. The `run()` method
/// executes the underlying task.
class IOExecutor::ScheduledTaskNode : public WakerImpl, public Context {
public:
    ScheduledTaskNode(IOExecutor* exec, PendingTask task) : executor_(exec), task_(std::move(task)) {}

    // WakerImpl implementation

    /// Re-enqueues the task into the executor's ready queue.
    void wake() override {
        if (executor_) {
            executor_->reschedule(static_pointer_cast<ScheduledTaskNode>(shared_from_this()));
        }
    }

    // Context implementation

    /// Returns the executor driving this task.
    Executor* executor() override {
        return executor_;
    }

    /// Returns a waker associated with this task node.
    Waker waker() override {
        return Waker(shared_from_this());
    }

    /// Executes the underlying task.
    bool run() {
        return task_(*this);
    }

private:
    IOExecutor* executor_;
    PendingTask task_;
};

/// The internal implementation of IOExecutor, managing the event loop and scheduling.
///
/// Dispatcher handles I/O multiplexing via epoll, inter-thread notification through eventfd, and timer management
/// using a priority queue.
class IOExecutor::Dispatcher {
public:
    Dispatcher(IOExecutor* exec) : executor_(exec) {
        int epfd = ::epoll_create1(EPOLL_CLOEXEC);
        if (epfd < 0) {
            throw IOExecutorException("Failed to create epoll");
        }
        epfd_.reset(epfd);

        int notify_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (notify_fd < 0) {
            throw IOExecutorException("Failed to create eventfd");
        }
        notify_fd_.reset(notify_fd);

        struct epoll_event interest = {
            .events = EPOLLIN,
            .data = {.u64 = 0},
        };
        ::epoll_ctl(epfd, EPOLL_CTL_ADD, notify_fd, &interest);
    }

    ~Dispatcher() {
        stop();
    }

    /// Wakes up the epoll loop from another thread.
    void notify() {
        eventfd_t value = 1;
        ::eventfd_write(notify_fd_.get(), value);
    }

    /// Schedules a task for execution in the next event loop iteration.
    void schedule(PendingTask task) {
        auto node = makeRef<ScheduledTaskNode>(executor_, std::move(task));
        reschedule(std::move(node));
    }

    /// Threads-safely adds a task node back to the ready queue.
    void reschedule(RefPtr<ScheduledTaskNode> node) {
        std::lock_guard lock(mtx_);
        ready_queue_.push(std::move(node));
        notify();
    }

    /// Registers a waker for readability events.
    void registerReadWaker(ResourceId id, Waker waker) {
        IORegistration* reg = registry_.get(id);
        if (reg == nullptr) {
            return;
        }

        reg->read_waker = std::move(waker);
    }

    /// Registers a waker for writability events.
    void registerWriteWaker(ResourceId id, Waker waker) {
        IORegistration* reg = registry_.get(id);
        if (reg == nullptr) {
            return;
        }

        reg->write_waker = std::move(waker);
    }

    /// Registers a file descriptor with epoll for event monitoring.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::expected<ResourceId, int> registerEvent(int fd, std::uint32_t events) {
        auto allocation = registry_.alloc(fd, events);
        if (!allocation) {
            return std::unexpected(ENOMEM);
        }

        const ResourceId id = allocation->id;
        struct epoll_event ev = {
            .events = events,
            .data = {.u64 = static_cast<std::uint64_t>(id)},
        };
        int ret = ::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, fd, &ev);
        if (ret < 0) {
            // Failed to register, rolling back and freeing the slot.
            registry_.free(id);
            return std::unexpected(errno);
        }
        return id;
    }

    /// Removes an I/O registration.
    void deregister(int fd, ResourceId id) {
        ::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, fd, nullptr);
        registry_.free(id);
    }

    /// Returns the steady clock snapshot from the current loop iteration.
    std::chrono::time_point<std::chrono::steady_clock> currentTime() const {
        return current_time_;
    }

    /// Schedules a timer and returns its unique ID.
    TimerId addTimer(std::chrono::milliseconds timeout, Waker waker) {
        auto allocation = timer_pool_.alloc(std::move(waker));
        if (!allocation) {
            // Resource pool exhausted.
            throw std::bad_alloc();
        }

        TimerHeapNode node = {
            .deadline = currentTime() + timeout,
            .id = static_cast<TimerId>(allocation->id),
        };

        try {
            timer_heap_.push(node);
        } catch (...) {
            // Roll back resource pool allocation.
            timer_pool_.free(allocation->id);
            throw;
        }

        return static_cast<TimerId>(allocation->id);
    }

    /// Marks a timer as cancelled.
    void cancelTimer(TimerId id) {
        timer_pool_.free(ResourceId(id));
    }

    /// Requests the event loop to terminate.
    void stop() {
        stop_ = true;
    }

    /// The central execution loop of the IOExecutor.
    ///
    /// The loop operates in several distinct phases to ensure fairness and efficiency:
    ///
    /// 1. **Task Execution Phase**: It first captures all currently ready tasks by swapping the `ready_queue_` into a
    ///    local queue. This batching prevents "ready-task starvation," where a task that immediately reschedules
    ///    itself could otherwise prevent the loop from ever reaching the I/O polling phase.
    ///
    /// 2. **Timer Preparation Phase**: It lazily removes any invalidated (cancelled) timers from the top of the
    ///    priority queue. This ensures that the subsequent timeout calculation for `epoll_wait` is based on the
    ///    earliest valid timer.
    ///
    /// 3. **I/O Polling Phase**: It calculates a poll timeout based on the deadline of the nearest timer. If no timers
    ///    exist, it blocks indefinitely; if a timer has already expired, it performs a non-blocking poll. The
    ///    `epoll_wait` call monitors both external I/O events and the internal `eventfd` used for cross-thread
    ///    notifications.
    ///
    /// 4. **Event Dispatch Phase**:
    ///    - If the `eventfd` is signaled, it clears the notification to allow future wakes.
    ///    - For socket events, it retrieves the `IORegistration` and invokes the appropriate `read_waker` or
    ///      `write_waker`. Waking a task moves it to the `ready_queue_` for execution in the next iteration.
    ///
    /// 5. **Timer Expiration Phase**: Finally, it updates the cached time and processes all timers whose deadlines
    ///    have passed, invoking their associated wakers.
    ///
    /// Throughout the loop, `current_time_` is strategically updated to provide a consistent and efficient time
    /// snapshot for all operations within a single iteration.
    void loop() {
        std::array<struct epoll_event, 128> evs;
        while (!stop_) {
            // Set current time.
            current_time_ = std::chrono::steady_clock::now();

            // Run ready tasks in a separate batch to avoid re-scheduling starvation.
            std::queue<RefPtr<ScheduledTaskNode>> q;
            {
                std::lock_guard lock(mtx_);
                q.swap(ready_queue_);
            }

            while (!q.empty()) {
                auto task = q.front();
                q.pop();
                task->run();
            }

            // Remove invalidated timers before calculating poll timeout.
            while (!timer_heap_.empty()) {
                if (!timer_pool_.valid(ResourceId(timer_heap_.top().id))) {
                    timer_heap_.pop();
                } else {
                    break;
                }
            }

            // Refresh current time.
            current_time_ = std::chrono::steady_clock::now();

            // Compute wait duration based on the nearest timer.
            int timeout_ms = -1;
            if (!timer_heap_.empty()) {
                std::chrono::milliseconds diff(0);
                if (timer_heap_.top().deadline > current_time_) {
                    diff = std::chrono::ceil<std::chrono::milliseconds>(timer_heap_.top().deadline - current_time_);
                }

                timeout_ms = static_cast<int>(diff.count());
            }

            const int num = ::epoll_wait(epfd_.get(), evs.data(), evs.size(), timeout_ms);
            if (num < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            for (std::size_t i = 0; i < static_cast<std::size_t>(num); ++i) {
                // Someone called `notify()` in other thread.
                if (evs[i].data.u64 == 0) {
                    eventfd_t value = 0;
                    ::eventfd_read(notify_fd_.get(), &value);
                    continue;
                }

                const std::uint32_t what = evs[i].events;
                const ResourceId id(evs[i].data.u64);

                IORegistration* reg = registry_.get(id);
                if (reg == nullptr) {
                    continue; // Invalid ID (e.g., dangling pointer or already freed).
                }

                if (what & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    // Waker consumed after waking, adhering to one-shot semantics.
                    auto waker = std::move(reg->read_waker);
                    waker.wake();
                }

                if (what & (EPOLLOUT | EPOLLERR)) {
                    // Waker consumed after waking, adhering to one-shot semantics.
                    auto waker = std::move(reg->write_waker);
                    waker.wake();
                }
            }

            // Process expired timers.
            while (!timer_heap_.empty()) {
                const auto& timer = timer_heap_.top();

                // The timer ID might have been cancelled.
                if (!timer_pool_.valid(ResourceId(timer.id))) {
                    timer_heap_.pop();
                    continue;
                }

                // Check if the timer has expired.
                if (timer.deadline > current_time_) {
                    break;
                }

                // The timer has expired.
                TimerPayload* payload = timer_pool_.get(ResourceId(timer.id));
                if (payload) [[likely]] {
                    auto waker = std::move(payload->waker);
                    TimerId id_to_free = timer.id;

                    timer_heap_.pop();
                    timer_pool_.free(ResourceId(id_to_free));

                    waker.wake();
                } else {
                    timer_heap_.pop();
                }
            }
        }
    }

private:
    struct TimerPayload {
        Waker waker;
    };
    static_assert(std::is_nothrow_constructible_v<TimerPayload, Waker>,
                  "Ensure the TimerPayload won't throw exception in ctor");

    struct TimerHeapNode {
        std::chrono::time_point<std::chrono::steady_clock> deadline;
        TimerId id;

        bool operator>(const TimerHeapNode& rhs) const {
            if (deadline != rhs.deadline) {
                return deadline > rhs.deadline;
            }
            return id > rhs.id;
        }
    };

    bool stop_ = false;
    UniqueFd epfd_;
    UniqueFd notify_fd_;
    IOExecutor* executor_;

    std::mutex mtx_;
    std::queue<RefPtr<ScheduledTaskNode>> ready_queue_;
    PagedResourcePool<IORegistration> registry_;

    std::chrono::time_point<std::chrono::steady_clock> current_time_;
    PagedResourcePool<TimerPayload> timer_pool_;
    std::priority_queue<TimerHeapNode, std::vector<TimerHeapNode>, std::greater<TimerHeapNode>> timer_heap_;
};

IOExecutor::IOExecutor() : dispatcher_(std::make_unique<Dispatcher>(this)) {}

IOExecutor::~IOExecutor() {
    dispatcher_->stop();
}

void IOExecutor::schedule(PendingTask task) {
    dispatcher_->schedule(std::move(task));
}

void IOExecutor::loop() {
    dispatcher_->loop();
}

void IOExecutor::notify() {
    dispatcher_->notify();
}

void IOExecutor::waitForRead(ResourceId id, Waker waker) {
    dispatcher_->registerReadWaker(id, std::move(waker));
}

void IOExecutor::waitForWrite(ResourceId id, Waker waker) {
    dispatcher_->registerWriteWaker(id, std::move(waker));
}

std::expected<ResourceId, int> IOExecutor::registerEvent(int fd, std::uint32_t events) {
    return dispatcher_->registerEvent(fd, events);
}

void IOExecutor::deregister(int fd, ResourceId id) {
    dispatcher_->deregister(fd, id);
}

std::chrono::time_point<std::chrono::steady_clock> IOExecutor::currentTime() const {
    return dispatcher_->currentTime();
}

TimerId IOExecutor::addTimer(std::chrono::milliseconds timeout, Waker waker) {
    return dispatcher_->addTimer(timeout, std::move(waker));
}

void IOExecutor::cancelTimer(TimerId id) {
    dispatcher_->cancelTimer(id);
}

void IOExecutor::reschedule(RefPtr<ScheduledTaskNode> node) {
    dispatcher_->reschedule(std::move(node));
}

} // namespace tcp
} // namespace hcomm
