// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_TCP_EXECUTOR_HPP_
#define HCOMM_TRANSPORT_TCP_EXECUTOR_HPP_

#include <exception>
#include <expected>
#include <memory>

#include "hcomm/base/refptr.hpp"
#include "hcomm/memory/paged_resource_pool.hpp"
#include "hcomm/promise/promise.hpp"

namespace hcomm {
namespace tcp {
class IOExecutorException : public std::exception {
public:
    IOExecutorException(const char* s) : err_msg_(s) {}

    const char* what() const noexcept override {
        return err_msg_;
    }

private:
    const char* err_msg_;
};

/// An I/O-aware executor for driving asynchronous TCP operations.
///
/// `IOExecutor` integrates with the promise framework (`hcomm::promise`) to schedule and run tasks. It is built upon a
/// high-performance I/O multiplexing mechanism (epoll) to handle non-blocking sockets efficiently.
///
/// This executor is responsible for polling for I/O events (e.g., data available for reading, buffer available for
/// writing) and waking up the corresponding tasks (promises) that are waiting for these events.
///
/// A typical usage pattern involves creating an `IOExecutor` instance and running its `loop()` in a dedicated thread.
/// Tasks are submitted via the `schedule()` method.
///
/// ```cpp
/// hcomm::tcp::IOExecutor executor;
///
/// // Schedule tasks on the executor
/// executor.schedule([](Context& ctx) {
///     // ... async operations
///     return true; // task is complete
/// });
///
/// executor.loop();
/// ```
class IOExecutor : public Executor, public TimerService {
public:
    /// Constructs an `IOExecutor`.
    ///
    /// Initializes the underlying I/O dispatcher (epoll instance) and prepares the executor for scheduling tasks.
    IOExecutor();

    IOExecutor(IOExecutor&& rhs) noexcept = delete;
    IOExecutor& operator=(IOExecutor&& rhs) noexcept = delete;

    /// Destroys the `IOExecutor`.
    ///
    /// This will stop the event loop if it is running and clean up all associated resources, including the I/O
    /// dispatcher.
    ~IOExecutor() override;

    /// Schedules a task (e.g., the root of a promise chain) for execution.
    ///
    /// The task will be added to a queue and executed as soon as the executor's event loop is free. This method is the
    /// primary way to introduce new asynchronous workflows into the executor.
    void schedule(PendingTask task) override;

    /// Returns the timer service for this executor.
    ///
    /// Because `IOExecutor` implements the `TimerService` interface directly, this returns a pointer to itself.
    TimerService* timer() override {
        return this;
    }

    /// Starts the executor's event loop.
    ///
    /// This is a blocking call that continuously processes ready tasks and polls for I/O events. It will run until
    /// `stop()` is called on the underlying dispatcher, which can be triggered by destroying the `IOExecutor` or by a
    /// custom stop mechanism.
    ///
    /// It is common to run the loop in a separate thread to keep the application responsive.
    void loop();

    /// Wakes up the event loop if it is currently blocked in a poll.
    ///
    /// This is useful for signaling the executor to process newly scheduled tasks or to shut down, without waiting for
    /// an I/O event to occur.
    void notify();

    /// Asks the executor to notify the waker when the corresponding resource becomes readable.
    ///
    /// The `reader` waker will be invoked when an I/O event indicates that the resource identified by `id` is ready
    /// for a read operation. This is part of the mechanism that bridges asynchronous I/O events with the
    /// promise-based task scheduler.
    void waitForRead(ResourceId id, Waker waker);

    /// Asks the executor to notify the waker when the corresponding resource becomes writable.
    ///
    /// The `writer` waker will be invoked when an I/O event indicates that the resource identified by `id` is ready
    /// for a write operation. This allows tasks to yield until the underlying socket has buffer space available.
    void waitForWrite(ResourceId id, Waker waker);

    /// Registers a file descriptor for I/O event monitoring with the executor.
    ///
    /// This method associates a file descriptor `fd` with the executor's underlying I/O multiplexer (epoll). The
    /// `events` mask specifies the types of I/O events to monitor (e.g., `EPOLLIN`, `EPOLLOUT`).
    ///
    /// On success, it returns a `ResourceId`, which is a handle that uniquely identifies this registration. This ID is
    /// required for subsequent operations like `waitForRead`, `waitForWrite`, and `deregister`. On failure, it returns
    /// an error code (errno).
    std::expected<ResourceId, int> registerEvent(int fd, std::uint32_t events);

    /// Deregisters all event notifications for a given file descriptor.
    void deregister(int fd, ResourceId id);

    /// Returns a cached snapshot of the current steady clock time.
    ///
    /// This time is updated at key points during the event loop (e.g., at the start of an iteration and after
    /// `epoll_wait`). Using this cached value is more efficient than calling `steady_clock::now()` repeatedly and
    /// ensures a consistent time view within a single processing step. Note that this may lag behind the actual
    /// time if a task has been executing for a significant duration.
    std::chrono::time_point<std::chrono::steady_clock> currentTime() const override;

    /// Registers a timer that will wake up the provided waker after the specified timeout.
    ///
    /// The timer is automatically cancelled if it expires or if it is manually cancelled via `cancelTimer`.
    ///
    /// Returns a unique `TimerId` that can be used to cancel the timer.
    TimerId addTimer(std::chrono::milliseconds timeout, Waker waker) override;

    /// Cancels a previously registered timer.
    ///
    /// If the timer has already expired or been cancelled, this operation does nothing.
    void cancelTimer(TimerId id) override;

private:
    class Dispatcher;
    class ScheduledTaskNode;

    void reschedule(RefPtr<ScheduledTaskNode> node);

    std::unique_ptr<Dispatcher> dispatcher_;
};

} // namespace tcp
} // namespace hcomm

#endif // HCOMM_TRANSPORT_TCP_EXECUTOR_HPP_
