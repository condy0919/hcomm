// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_PROMISE_SINGLE_THREADED_EXECUTOR_HPP_
#define HCOMM_PROMISE_SINGLE_THREADED_EXECUTOR_HPP_

#include <memory>

#include "hcomm/promise/promise.hpp"

namespace hcomm {
/// A simple platform-independent single-threaded asynchronous task executor.
///
/// This executor spawns a single background thread (`std::jthread`) to process tasks.
/// It uses a thread-safe queue protected by a mutex and condition variable to manage scheduled tasks.
///
/// Usage:
/// 1. Create an instance of `SingleThreadedExecutor`.
/// 2. Call `run()` to start the background worker thread.
/// 3. Submit tasks using `schedule()`.
/// 4. The executor is stopped automatically when destroyed (RAII).
class SingleThreadedExecutor final : public Executor {
public:
    SingleThreadedExecutor();

    /// Destroys the executor.
    ///
    /// This stops the background thread and joins it. Any remaining tasks in the queue are discarded.
    ~SingleThreadedExecutor() override;

    SingleThreadedExecutor(SingleThreadedExecutor&& rhs) noexcept = delete;
    SingleThreadedExecutor& operator=(SingleThreadedExecutor&& rhs) noexcept = delete;

    /// Schedules a task for eventual execution by the executor.
    ///
    /// This method is thread-safe.
    void schedule(PendingTask task) override;

    /// Starts the execution loop.
    ///
    /// This method launches a background thread that begins processing tasks from the queue.
    void run();

    /// Stops the execution loop.
    void shutdown();

private:
    class Dispatcher;
    class ScheduledTaskNode;

    /// Internal method to reschedule a suspended task node.
    ///
    /// This is used by `ScheduledTaskNode::wake()` to put itself back into the run queue.
    void reschedule(RefPtr<ScheduledTaskNode> node);

    std::unique_ptr<Dispatcher> dispatcher_;
};
} // namespace hcomm

#endif // HCOMM_PROMISE_SINGLE_THREADED_EXECUTOR_HPP_
