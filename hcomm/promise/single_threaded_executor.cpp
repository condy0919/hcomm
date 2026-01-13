// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/single_threaded_executor.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

namespace hcomm {
/// A node in the executor's task queue.
///
/// This class plays three roles:
/// 1. It holds the `PendingTask` (the promise chain) that needs to be executed.
/// 2. It implements `Context`, passing itself to the promise so the promise can access the executor and waker.
/// 3. It implements `WakerImpl`, allowing the task to be rescheduled when it wakes up from suspension.
class SingleThreadedExecutor::ScheduledTaskNode : public WakerImpl, public Context {
public:
    ScheduledTaskNode(SingleThreadedExecutor* exec, PendingTask task) : executor_(exec), task_(std::move(task)) {}

    // WakerImpl implementation

    /// Wakes up the task.
    ///
    /// This method is called by a `Waker` (held by an event source) when the task is ready to make progress.
    /// It reschedules this node onto the executor's ready queue.
    void wake() override {
        if (executor_) {
            executor_->reschedule(static_pointer_cast<ScheduledTaskNode>(shared_from_this()));
        }
    }

    // Context implementation

    /// Creates a waker for this task.
    Waker waker() override {
        return Waker(shared_from_this());
    }

    /// Returns the associated executor.
    Executor* executor() override {
        return executor_;
    }

    /// Runs the task.
    ///
    /// Invokes the underlying `PendingTask`.
    /// @return `true` if the task completed, `false` if it suspended (pending).
    bool run() {
        return task_(*this);
    }

private:
    SingleThreadedExecutor* executor_;
    PendingTask task_;
};

/// The internal implementation of the SingleThreadedExecutor.
///
/// The Dispatcher manages the worker thread and the queue of ready tasks.
/// It handles thread synchronization and the lifecycle of the execution loop.
class SingleThreadedExecutor::Dispatcher {
public:
    Dispatcher(SingleThreadedExecutor* exec) : executor_(exec) {}

    ~Dispatcher() {
        shutdown();
    }

    /// Schedules a new task.
    ///
    /// Wraps the task in a `ScheduledTaskNode` and adds it to the queue.
    void schedule(PendingTask task) {
        RefPtr node(new ScheduledTaskNode(executor_, std::move(task)));
        reschedule(std::move(node));
    }

    /// Reschedules an existing task node.
    ///
    /// This is thread-safe and can be called from any thread (e.g., from an interrupt handler or another thread
    /// waking up a task).
    void reschedule(RefPtr<ScheduledTaskNode> node) {
        std::lock_guard lock(mtx_);
        ready_queue_.push(std::move(node));
        cv_.notify_one();
    }

    /// Stops the worker thread.
    void shutdown() {
        // Request the worker thread to stop.
        worker_.request_stop();

        // Wake up the worker in case it is sleeping on the condition variable.
        {
            std::lock_guard lock(mtx_);
            cv_.notify_one();
        }
    }

    /// Starts the main execution loop in a separate thread.
    void run() {
        worker_ = std::jthread([this](const std::stop_token& token) {
            while (!token.stop_requested()) {
                RefPtr<ScheduledTaskNode> node;
                {
                    std::unique_lock lock(mtx_);
                    cv_.wait(lock, [&] { return !ready_queue_.empty() || token.stop_requested(); });
                    if (token.stop_requested()) {
                        return;
                    }

                    node = std::move(ready_queue_.front());
                    ready_queue_.pop();
                }

                // Run the task.
                // If it returns true (completed), the node is destroyed (refcount drops to zero).
                // If it returns false (suspended), the node is kept alive by the Waker (held by the task/event source).
                [[maybe_unused]] bool done = node->run();
            }
        });
    }

private:
    std::jthread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<RefPtr<ScheduledTaskNode>> ready_queue_;
    SingleThreadedExecutor* executor_;
};

SingleThreadedExecutor::SingleThreadedExecutor() : dispatcher_(std::make_unique<Dispatcher>(this)) {}

SingleThreadedExecutor::~SingleThreadedExecutor() {
    dispatcher_->shutdown();
}

void SingleThreadedExecutor::schedule(PendingTask task) {
    dispatcher_->schedule(std::move(task));
}

void SingleThreadedExecutor::reschedule(RefPtr<ScheduledTaskNode> node) {
    dispatcher_->reschedule(std::move(node));
}

void SingleThreadedExecutor::run() {
    dispatcher_->run();
}

void SingleThreadedExecutor::shutdown() {
    dispatcher_->shutdown();
}
} // namespace hcomm
