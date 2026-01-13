// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/single_threaded_executor.hpp"

#include <semaphore>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {
class SingleThreadedExecutorTest : public testing::Test {
public:
    void SetUp() override {
        executor_.run();
    }

    void TearDown() override {
        executor_.shutdown();
    }

protected:
    std::binary_semaphore sem_{0};
    hcomm::SingleThreadedExecutor executor_;
};

TEST_F(SingleThreadedExecutorTest, SimpleSchedule) {
    bool executed = false;
    executor_.schedule(hcomm::makePromise([&] -> hcomm::Result<> {
        executed = true;
        sem_.release();
        return hcomm::Ok();
    }));
    sem_.acquire();
    EXPECT_TRUE(executed);
}

TEST_F(SingleThreadedExecutorTest, NestedSchedule) {
    int cnt[2] = {};
    executor_.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
        ++cnt[0];

        ctx.executor()->schedule(hcomm::makePromise([&]() -> hcomm::Result<> {
            ++cnt[1];
            sem_.release();
            return hcomm::Ok();
        }));

        return hcomm::Ok();
    }));

    sem_.acquire();
    EXPECT_EQ(cnt[0], 1);
    EXPECT_EQ(cnt[1], 1);
}

TEST_F(SingleThreadedExecutorTest, WakerSupport) {
    std::atomic<int> poll_count = 0;
    std::atomic<bool> done = false;
    hcomm::Waker saved_waker;
    executor_.schedule(hcomm::makePromise([&](hcomm::Context& ctx) -> hcomm::Result<> {
        if (++poll_count == 1) {
            // First time being polled: save the waker and return Pending.
            saved_waker = ctx.waker();
            sem_.release();
            return hcomm::Pending{};
        }

        // Second time being polled (after wake()): return Ok.
        done = true;
        sem_.release();
        return hcomm::Ok();
    }));

    sem_.acquire();
    EXPECT_EQ(poll_count, 1);
    EXPECT_FALSE(done);

    // Now wake it up from this thread.
    saved_waker.wake();

    sem_.acquire();
    EXPECT_EQ(poll_count, 2);
    EXPECT_TRUE(done);
}

TEST_F(SingleThreadedExecutorTest, ChainAndWakeOnOtherthread) {
    std::atomic<int> result_val = 0;
    auto promise = hcomm::makePromise([state = 0](hcomm::Context& ctx) mutable -> hcomm::Result<int> {
                       // Suspend and wait for a value.
                       if (state == 0) {
                           state = 1;
                           auto waker = ctx.waker();
                           std::thread([waker]() mutable {
                               std::this_thread::sleep_for(50ms);
                               waker.wake();
                           }).detach();
                           return hcomm::Pending{};
                       }
                       return hcomm::Ok(42);
                   }).andThen([](const int& val) -> hcomm::Result<int> {
        // This will be called after wake() and the promise is re-polled.
        return hcomm::Ok(val + 8);
    });

    executor_.schedule(std::move(promise).then([&](const hcomm::Result<int>& res) -> hcomm::Result<> {
        if (res.isOk()) {
            result_val = res.value();
        }
        sem_.release();
        return hcomm::Ok();
    }));

    sem_.acquire();
    EXPECT_EQ(result_val, 50);
}

TEST_F(SingleThreadedExecutorTest, ChainAndWakeOnThisThread) {
    int run_cnt[3] = {};
    int resume_cnt[3] = {};
    executor_.schedule(hcomm::makePromise(
                           // Schedules a task that suspends itself and immediately resumes.
                           [&](hcomm::Context& ctx) -> hcomm::Result<> {
                               if (++run_cnt[0] == 100) {
                                   return hcomm::Ok();
                               }

                               ++resume_cnt[0];
                               ctx.waker().wake();
                               return hcomm::Pending{};
                           })
                           // The continuation requires several iterations to complete, each time schedule
                           // another task to resume itself after suspension.
                           .andThen([&](hcomm::Context& ctx) -> hcomm::Result<> {
                               if (++run_cnt[1] == 100) {
                                   return hcomm::Ok();
                               }

                               ctx.executor()->schedule(hcomm::makePromise([&, waker = ctx.waker()] -> hcomm::Result<> {
                                   ++resume_cnt[1];
                                   waker.wake();
                                   return hcomm::Ok();
                               }));
                               return hcomm::Pending{};
                           })
                           // The continuation suspends itself and arranges to be resumed on other thread.
                           .andThen([&](hcomm::Context& ctx) -> hcomm::Result<> {
                               if (++run_cnt[2] == 100) {
                                   sem_.release();
                                   return hcomm::Ok();
                               }

                               // Race
                               std::thread([&, waker = ctx.waker()] mutable {
                                   ++resume_cnt[2];
                                   waker.wake();
                               }).detach();
                               return hcomm::Pending{};
                           }));

    sem_.acquire();
    EXPECT_EQ(run_cnt[0], 100);
    EXPECT_EQ(run_cnt[1], 100);
    EXPECT_EQ(run_cnt[2], 100);
    EXPECT_EQ(resume_cnt[0], 99);
    EXPECT_EQ(resume_cnt[1], 99);
    EXPECT_EQ(resume_cnt[2], 99);
}

} // namespace
