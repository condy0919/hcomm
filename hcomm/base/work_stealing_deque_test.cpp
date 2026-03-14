// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/base/work_stealing_deque.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {
TEST(WorkStealingDeque, BasicOps) {
    hcomm::WorkStealingDeque<int, 8> deque;
    EXPECT_EQ(deque.size(), 0);
    EXPECT_TRUE(deque.empty());

    // Push items
    EXPECT_TRUE(deque.push(1));
    EXPECT_FALSE(deque.empty());
    EXPECT_TRUE(deque.push(2));
    EXPECT_TRUE(deque.push(3));
    EXPECT_EQ(deque.size(), 3);
    EXPECT_FALSE(deque.empty());

    // Pop items (LIFO)
    EXPECT_EQ(deque.pop(), 3);
    EXPECT_EQ(deque.pop(), 2);
    EXPECT_FALSE(deque.empty());
    EXPECT_EQ(deque.pop(), 1);
    EXPECT_EQ(deque.size(), 0);
    EXPECT_TRUE(deque.empty());
    EXPECT_EQ(deque.pop(), std::nullopt);
}

TEST(WorkStealingDeque, StealOps) {
    hcomm::WorkStealingDeque<int, 8> deque;
    EXPECT_TRUE(deque.empty());

    // Push items
    deque.push(10);
    deque.push(20);
    deque.push(30);
    EXPECT_FALSE(deque.empty());

    // Steal items (FIFO)
    EXPECT_EQ(deque.steal(), 10);
    EXPECT_EQ(deque.steal(), 20);
    EXPECT_FALSE(deque.empty());
    EXPECT_EQ(deque.steal(), 30);
    EXPECT_TRUE(deque.empty());
    EXPECT_EQ(deque.steal(), std::nullopt);
}

TEST(WorkStealingDeque, Empty) {
    hcomm::WorkStealingDeque<int, 4> deque;
    EXPECT_TRUE(deque.empty());
    EXPECT_EQ(deque.size(), 0);

    deque.push(1);
    EXPECT_FALSE(deque.empty());
    EXPECT_EQ(deque.size(), 1);

    deque.pop();
    EXPECT_TRUE(deque.empty());
    EXPECT_EQ(deque.size(), 0);
}

TEST(WorkStealingDeque, LastElementDuel) {
    hcomm::WorkStealingDeque<int, 4> deque;
    deque.push(42);

    // Owner and Stealer race on the last element.
    // In a single-threaded test, we just check they don't both get it.
    auto result = deque.pop();
    EXPECT_EQ(result, 42);
    EXPECT_EQ(deque.steal(), std::nullopt);
    EXPECT_EQ(deque.pop(), std::nullopt);
}

TEST(WorkStealingDeque, ConcurrentSteal) {
    constexpr int kNumTasks = 10000;
    constexpr int kNumStealers = 4;
    hcomm::WorkStealingDeque<int, 16384> deque;

    for (int i = 0; i < kNumTasks; ++i) {
        deque.push(i);
    }

    std::atomic<int> stolen_count{0};
    std::atomic<int> popped_count{0};
    std::vector<int> results(kNumTasks, 0);
    std::mutex results_mtx;

    auto stealer_func = [&]() {
        while (true) {
            auto task = deque.steal();
            if (task) {
                stolen_count++;
                std::lock_guard<std::mutex> lock(results_mtx);
                results[*task]++;
            } else if (deque.size() == 0) {
                break;
            }
            std::this_thread::yield();
        }
    };

    std::vector<std::thread> stealers;
    stealers.reserve(kNumStealers);
    for (int i = 0; i < kNumStealers; ++i) {
        stealers.emplace_back(stealer_func);
    }

    // Owner also pops some
    while (true) {
        auto task = deque.pop();
        if (task) {
            popped_count++;
            std::lock_guard<std::mutex> lock(results_mtx);
            results[*task]++;
        } else if (deque.size() == 0) {
            break;
        }
    }

    for (auto& t : stealers) {
        t.join();
    }

    EXPECT_EQ(stolen_count + popped_count, kNumTasks);
    for (int i = 0; i < kNumTasks; ++i) {
        EXPECT_EQ(results[i], 1) << "Task " << i << " was not processed exactly once";
    }
}

TEST(WorkStealingDeque, MoveOnlyOps) {
    hcomm::WorkStealingDeque<std::unique_ptr<int>, 8> deque;
    EXPECT_EQ(deque.size(), 0);

    // Push unique_ptr
    deque.push(std::make_unique<int>(1));
    deque.push(std::make_unique<int>(2));
    deque.push(std::make_unique<int>(3));
    EXPECT_EQ(deque.size(), 3);

    // Pop LIFO
    auto p3 = deque.pop();
    ASSERT_TRUE(p3.has_value());
    EXPECT_EQ(**p3, 3);

    // Steal FIFO
    auto p1 = deque.steal();
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ(**p1, 1);

    auto p2 = deque.pop();
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(**p2, 2);

    EXPECT_EQ(deque.size(), 0);
    EXPECT_FALSE(deque.pop().has_value());
}

TEST(WorkStealingDeque, MoveOnlyConcurrent) {
    constexpr int kNumTasks = 5000;
    constexpr int kNumStealers = 4;
    hcomm::WorkStealingDeque<std::unique_ptr<int>, 8192> deque;

    for (int i = 0; i < kNumTasks; ++i) {
        deque.push(std::make_unique<int>(i));
    }

    std::atomic<int> processed_count{0};
    std::vector<int> results(kNumTasks, 0);
    std::mutex results_mtx;

    auto stealer_func = [&]() {
        while (processed_count < kNumTasks) {
            auto task = deque.steal();
            if (task) {
                ASSERT_NE(*task, nullptr);
                int val = **task;
                {
                    std::lock_guard<std::mutex> lock(results_mtx);
                    results[val]++;
                }
                processed_count++;
            } else if (deque.size() == 0) {
                std::this_thread::yield();
                if (deque.size() == 0 && processed_count < kNumTasks) {
                    // This is a heuristic break for the test, but in a real scenario
                    // we'd wait for the owner to push more or finish.
                    // Since owner is done pushing, we can break if size is 0 and others might be finishing.
                    break;
                }
            }
        }
    };

    std::vector<std::thread> stealers;
    stealers.reserve(kNumStealers);
    for (int i = 0; i < kNumStealers; ++i) {
        stealers.emplace_back(stealer_func);
    }

    // Owner pops
    while (processed_count < kNumTasks) {
        auto task = deque.pop();
        if (task) {
            ASSERT_NE(*task, nullptr);
            int val = **task;
            {
                std::lock_guard<std::mutex> lock(results_mtx);
                results[val]++;
            }
            processed_count++;
        } else if (deque.size() == 0) {
            break;
        }
    }

    for (auto& t : stealers) {
        t.join();
    }

    EXPECT_EQ(processed_count.load(), kNumTasks);
    for (int i = 0; i < kNumTasks; ++i) {
        EXPECT_EQ(results[i], 1) << "Task " << i << " was not processed exactly once";
    }
}
} // namespace
