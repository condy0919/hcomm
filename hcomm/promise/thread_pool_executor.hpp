// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_PROMISE_THREAD_POOL_EXECUTOR_HPP_
#define HCOMM_PROMISE_THREAD_POOL_EXECUTOR_HPP_

#include <memory>
#include <thread>

#include "hcomm/base/refptr.hpp"
#include "hcomm/promise/promise.hpp"

namespace hcomm {
/// `ThreadPoolExecutor` provides a multi-threaded execution environment for promises, implementing a work-stealing
/// scheduler to maximize throughput and minimize latency.
///
/// # Design Rationale: Work-Stealing
///
/// High-performance asynchronous systems often suffer from load imbalance or contention on global task queues.
/// `ThreadPoolExecutor` addresses this by giving each worker thread its own local work-stealing deque.
///
/// 1. **Local Preference**: Workers prioritize their own tasks to improve cache locality.
/// 2. **Global Fallback**: If a worker's local queue is empty, it attempts to fetch tasks from a shared global queue,
///    which typically holds tasks scheduled from threads outside the pool.
/// 3. **Stealing**: If both local and global sources are empty, the worker will attempt to steal tasks from other
///    workers' queues, ensuring that all CPU cores remain productive even with unevenly distributed workloads.
/// ## Example Usage
///
/// The following example demonstrates how to use `ThreadPoolExecutor` to schedule and wait for a task. The constructor
/// accepts a `num_threads` parameter (defaulting to the number of CPU cores) and immediately spawns that many worker
/// threads.
///
/// ```cpp
/// // Create an executor with 4 threads
/// hcomm::ThreadPoolExecutor executor(4);
/// std::binary_semaphore sem{0};
/// ```
/// executor.schedule(hcomm::makePromise([&]() -> hcomm::Result<> {
///     // This task runs on one of the background threads in the pool.
///     sem.release();
///     return hcomm::Ok();
/// }));
///
/// sem.acquire(); // Wait for the background task to complete
/// ```
class ThreadPoolExecutor : public Executor {
public:
    explicit ThreadPoolExecutor(std::size_t num_threads = std::thread::hardware_concurrency());

    ThreadPoolExecutor(ThreadPoolExecutor&& rhs) noexcept = delete;
    ThreadPoolExecutor& operator=(ThreadPoolExecutor&& rhs) noexcept = delete;

    ~ThreadPoolExecutor() override;

    /// Schedules a task for execution. This method is thread-safe and can be called from any thread, including threads
    /// within the pool.
    ///
    /// If called from a thread already belonging to this pool, the task is pushed into the thread's local LIFO queue
    /// to preserve cache locality. Otherwise, the task is added to a global FIFO queue and an idle worker is notified.
    void schedule(PendingTask task) override;

private:
    class Dispatcher;
    class ScheduledTaskNode;

    void reschedule(RefPtr<ScheduledTaskNode> node);

    std::unique_ptr<Dispatcher> dispatcher_;
};

} // namespace hcomm

#endif // HCOMM_PROMISE_THREAD_POOL_EXECUTOR_HPP_
