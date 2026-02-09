// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/tcp/executor.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <map>
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
    int fd = -1;
    std::uint32_t interested = 0;
    Waker read_waker;
    Waker write_waker;
};

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
        interest.data.fd = notify_fd;
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
        ready_queue_.push(std::move(node));
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void registerWaker(int fd, std::uint32_t events, Waker waker) {
        auto& reg = registry_[fd];
        const int op = (reg.fd == -1) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

        reg.fd = fd;
        reg.interested = events;
        if (events & EPOLLIN) {
            reg.read_waker = std::move(waker);
        } else {
            reg.write_waker = std::move(waker);
        }

        struct epoll_event ev = {.events = events, .data = {.ptr = &reg}};
        ::epoll_ctl(epfd_.get(), op, fd, &ev);
    }

    void deregister(int fd) {
        registry_.erase(fd);
        ::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, fd, nullptr);
    }

    void stop() {
        stop_ = true;
    }

    void loop() {
        std::array<struct epoll_event, 128> evs;
        while (!stop_) {
            // Prevent new tasks from being generated during the processing of the current batch.
            auto q = std::move(ready_queue_);
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
                // The fd is unlikely to overlap with other IORegistration*.
                if (evs[i].data.fd == notify_fd_.get()) {
                    eventfd_t value = 0;
                    ::eventfd_read(notify_fd_.get(), &value);
                    continue;
                }

                const std::uint32_t what = evs[i].events;
                auto* reg = reinterpret_cast<IORegistration*>(evs[i].data.ptr);

                if (what & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    reg->read_waker.wake();
                }

                if (what & (EPOLLOUT | EPOLLERR)) {
                    reg->write_waker.wake();
                }
            }
        }
    }

private:
    bool stop_ = false;
    UniqueFd epfd_;
    UniqueFd notify_fd_;
    IOExecutor* executor_;
    std::queue<RefPtr<ScheduledTaskNode>> ready_queue_;
    std::map<int, IORegistration> registry_;
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

void IOExecutor::waitForRead(int fd, Waker waker) {
    dispatcher_->registerWaker(fd, EPOLLIN | EPOLLRDHUP | EPOLLET, std::move(waker));
}

void IOExecutor::waitForWrite(int fd, Waker waker) {
    dispatcher_->registerWaker(fd, EPOLLOUT | EPOLLET, std::move(waker));
}

void IOExecutor::deregister(int fd) {
    dispatcher_->deregister(fd);
}

void IOExecutor::reschedule(RefPtr<ScheduledTaskNode> node) {
    dispatcher_->reschedule(std::move(node));
}

} // namespace tcp
} // namespace hcomm
