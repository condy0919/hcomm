#include "hcomm/promise/thread_pool_executor.hpp"

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <vector>

#include "hcomm/base/scope_exit.hpp"
#include "hcomm/base/work_stealing_deque.hpp"
#include <boost/intrusive/list.hpp>

namespace hcomm {
namespace internal {
/// Generates a pseudo-random number using the Xorshift algorithm.
/// https://en.wikipedia.org/wiki/Xorshift
inline std::uint32_t xorshift32(std::uint32_t& state) {
    std::uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return state = x;
}
} // namespace internal

constexpr std::size_t kInvalidWorkerId = static_cast<std::size_t>(-1);
constexpr std::size_t kLocalQueueCapacity = 256;
constexpr std::size_t kMaxGlobalFetchBatch = 32;

/// A node representing a task scheduled in the thread pool.
/// Inherits from WakerImpl to allow rescheduling when a task is woken up.
class ThreadPoolExecutor::ScheduledTaskNode : public WakerImpl, public Context {
    friend class Dispatcher;

public:
    ScheduledTaskNode(ThreadPoolExecutor* exec, PendingTask task) : executor_(exec), task_(std::move(task)) {}

    void wake() override {
        if (executor_) [[likely]] {
            executor_->reschedule(static_pointer_cast<ScheduledTaskNode>(shared_from_this()));
        }
    }

    Executor* executor() override {
        return executor_;
    }

    Waker waker() override {
        return Waker(shared_from_this());
    }

    /// Executes the task. Returns true if the task is completed.
    bool run() {
        if (!task_) {
            return false;
        }

        bool done = task_(*this);
        if (done) {
            task_.reset();
        }
        return done;
    }

private:
    boost::intrusive::list_member_hook<> hook_;

    ThreadPoolExecutor* executor_;
    PendingTask task_;
};

/// The core engine of the ThreadPoolExecutor that manages worker threads and task distribution.
class ThreadPoolExecutor::Dispatcher {
    using TaskListOption = boost::intrusive::member_hook<ScheduledTaskNode, boost::intrusive::list_member_hook<>,
                                                         &ScheduledTaskNode::hook_>;
    using TaskList =
        boost::intrusive::list<ScheduledTaskNode, TaskListOption, boost::intrusive::constant_time_size<true>>;

public:
    Dispatcher(ThreadPoolExecutor* exec, std::size_t num_threads) : executor_(exec), local_queues_(num_threads) {
        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([i, this](std::stop_token stoken) {
                tls_worker_ctx_ = {
                    .id = i,
                    .rng_state = static_cast<std::uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
                };
                runInThread(std::move(stoken), i);
            });
        }
    }

    ~Dispatcher() {
        for (auto& worker : workers_) {
            worker.request_stop();
        }
        global_cv_.notify_all();
    }

    /// Schedules a new task for execution.
    void schedule(PendingTask task) {
        auto node = makeRef<ScheduledTaskNode>(executor_, std::move(task));
        reschedule(std::move(node));
    }

    /// Re-adds a previously scheduled task node back into the execution queues.
    void reschedule(RefPtr<ScheduledTaskNode> node) {
        bool pushed = false;
        const auto id = tls_worker_ctx_.id;
        if (id != kInvalidWorkerId) {
            auto& q = local_queues_[id];
            // Attempt to push into the local queue for better cache locality.
            pushed = q.push(std::move(node));
        }

        // Handle external submissions or local queue overflows by moving to the global queue.
        if (!pushed) {
            std::lock_guard lock(global_mtx_);
            global_queue_.push_back(*node.detach());
        }

        // Notify a sleeping worker if no threads are currently searching for tasks.
        std::uint32_t state = worker_state_.load();
        std::uint32_t searching = state >> 16;
        std::uint32_t sleeping = state & 0xffff;
        if (searching == 0 && sleeping > 0) {
            global_cv_.notify_one();
        }
    }

private:
    /// Attempts to retrieve a task from local, global, or peer queues.
    RefPtr<ScheduledTaskNode> tryGetTask(std::size_t worker_id) {
        auto& q = local_queues_[worker_id];

        // Try fetching from the local queue (LIFO) to maximize cache hits.
        if (!q.empty()) {
            if (auto task = q.pop()) {
                return *task;
            }
        }

        worker_state_.fetch_add(kSearchingInc);
        auto guard = ScopeExit([this]() { worker_state_.fetch_sub(kSearchingInc); });

        // Try fetching from the global queue with batching.
        {
            std::unique_lock lock(global_mtx_, std::try_to_lock);
            if (lock.owns_lock() && !global_queue_.empty()) {
                // Dynamically calculate the fetch batch size to balance throughput and contention.
                // batch_size = min(global_queue_size / num_workers + 1, kMaxGlobalFetchBatch)
                const std::size_t global_size = global_queue_.size();
                const std::size_t num_workers = local_queues_.size();
                std::size_t batch_size = std::min(global_size / num_workers + 1, kMaxGlobalFetchBatch);

                auto node = RefPtr<ScheduledTaskNode>::adopt(&global_queue_.front());
                global_queue_.pop_front();
                --batch_size;

                // Transfer remaining tasks in the batch to the local queue.
                auto& local = local_queues_[worker_id];
                while (batch_size > 0) {
                    auto batch_node = RefPtr<ScheduledTaskNode>::adopt(&global_queue_.front());
                    global_queue_.pop_front();

                    [[maybe_unused]] bool pushed = local.push(std::move(batch_node));
                    assert(pushed && "Local queue overflow during batch fetch! Capacity or logic error.");

                    --batch_size;
                }

                return node;
            }
        }

        // Try stealing tasks from other workers.
        return trySteal(worker_id);
    }

    /// Attempts to steal a task from another worker's local queue.
    RefPtr<ScheduledTaskNode> trySteal(std::size_t stealer_id) {
        const std::size_t num_workers = local_queues_.size();

        // Random work-stealing provides an optimal theoretical upper bound for load balancing.
        const std::size_t start_offset = internal::xorshift32(tls_worker_ctx_.rng_state) % num_workers;
        for (std::size_t i = 0; i < num_workers; ++i) {
            const std::size_t victim_id = (start_offset + i) % num_workers;
            if (victim_id == stealer_id) {
                continue;
            }

            auto& victim = local_queues_[victim_id];

            // Perform a quick check before attempting a potentially expensive steal operation.
            if (!victim.empty()) {
                if (auto task = victim.steal()) {
                    return *task;
                }
            }
        }
        return nullptr;
    }

    /// The main loop for each worker thread.
    void runInThread(std::stop_token stoken, std::size_t worker_id) {
        while (true) {
            if (auto task = tryGetTask(worker_id)) {
                [[maybe_unused]] bool done = task->run();
            } else {
                // TODO spinning for a while

                // No tasks found; transition to sleeping state.
                std::unique_lock lock(global_mtx_);

                worker_state_.fetch_add(kSleepingInc);

                bool woken = global_cv_.wait(lock, stoken, [this, worker_id]() {
                    // Check if tasks are available globally or locally.
                    if (!global_queue_.empty() || !local_queues_[worker_id].empty()) {
                        return true;
                    }

                    // TODO inefficient

                    // Check if any other worker has tasks available for stealing.
                    for (std::size_t i = 0; i < local_queues_.size(); ++i) {
                        if (i != worker_id && local_queues_[i].size()) {
                            return true;
                        }
                    }
                    return false;
                });

                worker_state_.fetch_sub(kSleepingInc);

                // Exit if stop is requested and no pending tasks remain.
                if (stoken.stop_requested() && global_queue_.empty() && !woken) {
                    return;
                }
            }
        }
    }

    struct WorkerContext {
        std::size_t id;
        std::uint32_t rng_state;
    };

    ThreadPoolExecutor* executor_;

    std::vector<WorkStealingDeque<RefPtr<ScheduledTaskNode>, kLocalQueueCapacity>> local_queues_;
    std::vector<std::jthread> workers_;
    static thread_local WorkerContext tls_worker_ctx_;

    static constexpr std::uint32_t kSearchingInc = 1 << 16;
    static constexpr std::uint32_t kSleepingInc = 1;
    std::atomic<std::uint32_t> worker_state_{0};

    std::mutex global_mtx_;
    std::condition_variable_any global_cv_;
    TaskList global_queue_;
};

thread_local ThreadPoolExecutor::Dispatcher::WorkerContext ThreadPoolExecutor::Dispatcher::tls_worker_ctx_ = {
    kInvalidWorkerId,
    0,
};

/// Constructs a new ThreadPoolExecutor with the specified number of threads.
ThreadPoolExecutor::ThreadPoolExecutor(std::size_t num_threads)
    : dispatcher_(std::make_unique<Dispatcher>(this, num_threads)) {}

ThreadPoolExecutor::~ThreadPoolExecutor() {}

/// Schedules a task for execution in the thread pool.
void ThreadPoolExecutor::schedule(PendingTask task) {
    dispatcher_->schedule(std::move(task));
}

/// Reschedules a task node that was previously scheduled.
/// This is typically called when a task is woken up by a waker.
void ThreadPoolExecutor::reschedule(RefPtr<ScheduledTaskNode> node) {
    dispatcher_->reschedule(std::move(node));
}
} // namespace hcomm
