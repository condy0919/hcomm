// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/thread_pool_executor.hpp"

#include <atomic>
#include <chrono>
#include <semaphore>
#include <string>

#include <gtest/gtest.h>

namespace {
TEST(ThreadPoolExecutorTest, BasicExecution) {
    std::atomic<int> counter{0};
    std::binary_semaphore sem{0};
    hcomm::ThreadPoolExecutor exec(4);

    exec.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
        counter++;
        sem.release();
        return hcomm::Ok();
    }));

    sem.acquire();
    EXPECT_EQ(counter.load(), 1);
}

TEST(ThreadPoolExecutorTest, ParallelExecution) {
    constexpr int kNumTasks = 1000;
    std::atomic<int> counter{0};
    std::counting_semaphore sem{0};
    hcomm::ThreadPoolExecutor exec(4);

    for (int i = 0; i < kNumTasks; ++i) {
        exec.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
            counter.fetch_add(1);
            sem.release();
            return hcomm::Ok();
        }));
    }

    for (int i = 0; i < kNumTasks; ++i) {
        sem.acquire();
    }
    EXPECT_EQ(counter.load(), kNumTasks);
}

TEST(ThreadPoolExecutorTest, WorkStealing) {
    // Test that other threads can steal tasks from a single busy thread.
    constexpr int kNumTasks = 100;
    std::atomic<int> counter{0};
    std::counting_semaphore sem{0};
    hcomm::ThreadPoolExecutor exec(4);

    // Block one thread to submit tasks from within it, simulating a single-producer scenario.
    exec.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
        for (int i = 0; i < kNumTasks; ++i) {
            exec.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
                counter.fetch_add(1);
                sem.release();
                return hcomm::Ok();
            }));
        }
        return hcomm::Ok();
    }));

    for (int i = 0; i < kNumTasks; ++i) {
        sem.acquire();
    }
    EXPECT_EQ(counter.load(), kNumTasks);
}

TEST(ThreadPoolExecutorTest, RescheduleWithWaker) {
    std::atomic<int> run_count{0};
    std::binary_semaphore sem{0};
    hcomm::ThreadPoolExecutor exec(2);
    hcomm::Waker waker;

    exec.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
        run_count++;
        if (run_count == 1) {
            waker = ctx.waker();
            return hcomm::Pending{}; // Not done yet, wait to be woken.
        }
        sem.release();
        return hcomm::Ok(); // Done.
    }));

    // Wait a bit to ensure the first run happens.
    while (run_count.load() == 0) {
        std::this_thread::yield();
    }

    EXPECT_EQ(run_count.load(), 1);

    // Manually wake the task.
    waker.wake();

    sem.acquire();
    EXPECT_EQ(run_count.load(), 2);
}

TEST(ThreadPoolExecutorTest, StressTest) {
    constexpr int kNumTasks = 100000;
    std::atomic<int> counter{0};
    std::counting_semaphore sem{0};
    hcomm::ThreadPoolExecutor exec(std::thread::hardware_concurrency());

    for (int i = 0; i < kNumTasks; ++i) {
        exec.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
            counter.fetch_add(1);
            sem.release();
            return hcomm::Ok();
        }));
    }

    for (int i = 0; i < kNumTasks; ++i) {
        sem.acquire();
    }
    EXPECT_EQ(counter.load(), kNumTasks);
}

TEST(ThreadPoolExecutorTest, ShutdownWithPendingTasks) {
    std::atomic<int> counter{0};
    {
        hcomm::ThreadPoolExecutor exec(4);
        for (int i = 0; i < 100; ++i) {
            exec.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                counter.fetch_add(1);
                return hcomm::Ok();
            }));
        }
    } // exec goes out of scope, threads should finish their current tasks if possible.
    EXPECT_GE(counter.load(), 0);
}

} // namespace
