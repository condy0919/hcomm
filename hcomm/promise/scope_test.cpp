// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/scope.hpp"

#include <memory>
#include <semaphore>
#include <utility>

#include <gtest/gtest.h>

#include "hcomm/promise/bridge.hpp"
#include "hcomm/promise/single_threaded_executor.hpp"

namespace {
class Accumulator {
public:
    auto add(std::uint32_t value) {
        return hcomm::makePromise([this, cycles = value](hcomm::Context& ctx) mutable -> hcomm::Result<std::uint32_t> {
                   if (cycles == 0) {
                       return hcomm::Ok(counter_);
                   }
                   ++counter_;
                   --cycles;
                   ctx.waker().wake(); // immediately available
                   return hcomm::Pending{};
               })
            .with(scope_);
    }

    std::uint32_t count() const {
        return counter_;
    }

private:
    std::uint32_t counter_ = 0;
    hcomm::Scope scope_;
};

class UnusableContext : public hcomm::Context {
public:
    hcomm::Executor* executor() override {
        return nullptr;
    }

    hcomm::Waker waker() override {
        std::unreachable();
    }
};

TEST(ScopeTest, ScopingTasks) {
    auto acc = std::make_unique<Accumulator>();
    std::binary_semaphore sem{0};
    hcomm::SingleThreadedExecutor exec;
    std::uint32_t sums[4] = {};

    // Schedule some tasks which accumulate values asynchronously.
    exec.schedule(acc->add(2).andThen([&sums](const std::uint32_t& v) { sums[0] = v; }));
    exec.schedule(acc->add(1).andThen([&sums](const std::uint32_t& v) { sums[1] = v; }));
    exec.schedule(acc->add(5).andThen([&sums](const std::uint32_t& v) { sums[2] = v; }));

    // Schedule a task which accumulates and then destroys the accumulator so that the scope is exited.
    std::uint32_t last_count = 0;
    exec.schedule(acc->add(3).andThen([&](const std::uint32_t& v) {
        sums[3] = v;
        // Schedule destruction in another task to avoid re-entrance.
        exec.schedule(hcomm::makePromise([&] {
            last_count = acc->count();
            acc.reset();
            sem.release();
        }));
    }));

    exec.run();
    sem.acquire();

    // | epoch | task queue          | counter | note                                                         |
    // |-------|---------------------|---------|--------------------------------------------------------------|
    // | 0     | a(2) b(1) c(5) d(3) | 0       |
    // | 1     | b(1) c(5) d(3) a(1) | 1       |
    // | 2     | c(5) d(3) a(1) b(0) | 2       |
    // | 3     | d(3) a(1) b(0) c(4) | 3       |
    // | 4     | a(1) b(0) c(4) d(2) | 4       |
    // | 5     | b(0) c(4) d(2) a(0) | 5       | next, accumulator assigns 5 to b                             |
    // | 6     | c(4) d(2) a(0)      | 5       | `b` is executed                                              |
    // | 7     | d(2) a(0) c(3)      | 6       |
    // | 8     | a(0) c(3) d(1)      | 7       | next, accumulator assigns 7 to a                             |
    // | 9     | c(3) d(1)           | 7       | `a` is executed                                              |
    // | 10    | d(1) c(2)           | 8       |
    // | 11    | c(2) d(0)           | 9       |
    // | 12    | d(0) c(1)           | 10      | next, accumulator assigns 11 to d                            |
    // | 13    | c(1) e              | 10      | `d` is executed, and a new task `e` is pushed back           |
    // | 14    | e c(0)              | 11      |
    // | 15    | c(0)                | 11      | `e` is executed, the scope is exited as `acc.reset()`        |
    // | 16    |                     |         | `c` is executed, it returns `Pending` due to `alive = false` |
    EXPECT_EQ(sums[0], 7);
    EXPECT_EQ(sums[1], 5);
    EXPECT_EQ(sums[2], 0);
    EXPECT_EQ(sums[3], 10);
    EXPECT_EQ(last_count, 11);
}

TEST(ScopeTest, DoubleWrap) {
    hcomm::Scope scope;
    UnusableContext ctx;

    int run_count = 0;
    auto p = hcomm::makePromise([&run_count](hcomm::Context& ctx) -> hcomm::Result<> {
                 ++run_count;
                 return hcomm::Pending{};
             })
                 .with(scope)
                 .with(scope);

    EXPECT_EQ(p(ctx).state(), hcomm::ResultState::Pending);
    EXPECT_EQ(run_count, 1);

    // Now exit the scope.
    scope.exit();
    EXPECT_TRUE(scope.exited());

    // Running the promise again should do nothing.
    EXPECT_EQ(p(ctx).state(), hcomm::ResultState::Pending);
    EXPECT_EQ(run_count, 1);
}
} // namespace
