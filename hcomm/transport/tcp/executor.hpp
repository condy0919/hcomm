// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_TCP_EXECUTOR_HPP_
#define HCOMM_TRANSPORT_TCP_EXECUTOR_HPP_

#include <exception>
#include <memory>

#include "hcomm/base/refptr.hpp"
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
class IOExecutor : public Executor {
public:
    IOExecutor();

    IOExecutor(IOExecutor&& rhs) noexcept = delete;
    IOExecutor& operator=(IOExecutor&& rhs) noexcept = delete;

    ~IOExecutor() override;

    /// Schedules a task (e.g., the root of a promise chain) for execution.
    ///
    /// The task will be added to a queue and executed as soon as the executor's event loop is free. This method is the
    /// primary way to introduce new asynchronous workflows into the executor.
    void schedule(PendingTask task) override;

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

    /// Registers a waker to be notified when a file descriptor becomes readable.
    ///
    /// When the associated I/O event occurs, `waker.wake()` will be called, which typically reschedules the associated
    /// task on the executor.
    void waitForRead(int fd, Waker reader);

    /// Registers a waker to be notified when a file descriptor becomes writable.
    ///
    /// When the associated I/O event occurs, `waker.wake()` will be called.
    void waitForWrite(int fd, Waker writer);

    /// Deregisters all event notifications for a given file descriptor.
    void deregister(int fd);

private:
    class Dispatcher;
    class ScheduledTaskNode;

    void reschedule(RefPtr<ScheduledTaskNode> node);

    std::unique_ptr<Dispatcher> dispatcher_;
};

} // namespace tcp
} // namespace hcomm

#endif // HCOMM_TRANSPORT_TCP_EXECUTOR_HPP_
