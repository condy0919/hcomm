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

    // waker impl

    void wake() override {
        if (executor_) {
            executor_->reschedule(static_pointer_cast<ScheduledTaskNode>(shared_from_this()));
        }
    }

    // context impl

    Executor* executor() override {
        return executor_;
    }

    Waker waker() override {
        return Waker(shared_from_this());
    }

    bool run() {
        return task_(*this);
    }

private:
    IOExecutor* executor_;
    PendingTask task_;
};

/// The core component of `IOExecutor` that manages I/O events and task scheduling.
///
/// This class encapsulates the `epoll` file descriptor, a notification mechanism (`eventfd`), and a queue of
/// ready-to-run tasks. It is responsible for:
/// 1. Monitoring file descriptors for I/O events using `epoll`.
/// 2. Waking tasks when their corresponding I/O events occur.
/// 3. Managing a queue of tasks that are ready to be executed.
/// 4. Providing a mechanism to wake up the event loop from another thread (`notify`).
///
/// The `loop()` method runs the central event-processing loop.
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

        struct epoll_event interest;
        interest.events = EPOLLIN;
        interest.data.ptr = nullptr;
        ::epoll_ctl(epfd, EPOLL_CTL_ADD, notify_fd, &interest);
    }

    ~Dispatcher() {
        stop();
    }

    void notify() {
        eventfd_t value = 1;
        ::eventfd_write(notify_fd_.get(), value);
    }

    void schedule(PendingTask task) {
        auto node = makeRef<ScheduledTaskNode>(executor_, std::move(task));
        reschedule(std::move(node));
    }

    void reschedule(RefPtr<ScheduledTaskNode> node) {
        std::lock_guard lock(mtx_);
        ready_queue_.push(std::move(node));
        notify();
    }

    void registerReadWaker(ResourceId id, Waker waker) {
        IORegistration* reg = registry_.get(id);
        if (reg == nullptr) {
            return;
        }

        reg->read_waker = std::move(waker);
    }

    void registerWriteWaker(ResourceId id, Waker waker) {
        IORegistration* reg = registry_.get(id);
        if (reg == nullptr) {
            return;
        }

        reg->write_waker = std::move(waker);
    }

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
            // 注册失败，回滚释放 slot
            registry_.free(id);
            return std::unexpected(errno);
        }
        return id;
    }

    void deregister(int fd, ResourceId id) {
        ::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, fd, nullptr);
        registry_.free(id);
    }

    void stop() {
        stop_ = true;
    }

    /// Runs the main event loop, which is the heart of the `IOExecutor`.
    ///
    /// This blocking function continuously checks for and processes work. In each iteration, it first runs all tasks
    /// that are ready to execute. Then, it waits for new I/O events using `epoll_wait`. When an event occurs, it
    /// wakes the corresponding task (e.g., a read or write operation) so it can be executed in a future iteration. The
    /// loop also handles external notifications that wake it up to process newly scheduled tasks.
    void loop() {
        std::array<struct epoll_event, 128> evs;
        while (!stop_) {
            // Prevent new tasks from being generated during the processing of the current batch.
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

            const int num = ::epoll_wait(epfd_.get(), evs.data(), evs.size(), -1);
            if (num < 0) {
                if (errno == EINTR) {
                    continue;
                }

                break;
            }

            for (std::size_t i = 0; i < static_cast<std::size_t>(num); ++i) {
                // Someone called `notify()` in other thread.
                if (evs[i].data.ptr == nullptr) {
                    eventfd_t value = 0;
                    ::eventfd_read(notify_fd_.get(), &value);
                    continue;
                }

                const std::uint32_t what = evs[i].events;
                const ResourceId id(evs[i].data.u64);

                IORegistration* reg = registry_.get(id);
                if (reg == nullptr) {
                    continue; // 野指针，或者已释放的 id
                }

                if (what & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    // 唤醒后清空，遵循 oneshot 语义
                    auto waker = std::move(reg->read_waker);
                    waker.wake();
                }

                if (what & (EPOLLOUT | EPOLLERR)) {
                    // 唤醒后清空，遵循 oneshot 语义
                    auto waker = std::move(reg->write_waker);
                    waker.wake();
                }
            }
        }
    }

private:
    bool stop_ = false;
    UniqueFd epfd_;
    UniqueFd notify_fd_;
    IOExecutor* executor_;
    std::mutex mtx_;
    std::queue<RefPtr<ScheduledTaskNode>> ready_queue_;
    PagedResourcePool<IORegistration> registry_;
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

void IOExecutor::reschedule(RefPtr<ScheduledTaskNode> node) {
    dispatcher_->reschedule(std::move(node));
}

} // namespace tcp
} // namespace hcomm
