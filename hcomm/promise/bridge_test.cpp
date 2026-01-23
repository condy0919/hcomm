// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/bridge.hpp"

#include <string>

#include <gtest/gtest.h>

#include "hcomm/promise/inline_executor.hpp"
#include "hcomm/promise/single_threaded_executor.hpp"

namespace {
using namespace std::string_literals;

template <typename C, typename R = typename hcomm::PromiseImpl<C>::ResultType>
auto run(hcomm::PromiseImpl<C> promise) {
    hcomm::InlineExecutor exec;

    R saved_result;
    exec.schedule(promise.inspect([&saved_result](const R& result) { saved_result = result; }));
    return saved_result;
}

TEST(BridgeTest, CompleterExplicitAbandon) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    completer.abandon();
    EXPECT_FALSE(static_cast<bool>(completer));
    EXPECT_TRUE(consumer.abandoned());

    auto result = run(consumer.promiseOr(hcomm::Err("Abandoned"s)));
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), "Abandoned");
}

TEST(BridgeTest, CompleterDrops) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    {
        auto comp = std::move(completer);
    }
    EXPECT_FALSE(static_cast<bool>(completer));
    EXPECT_TRUE(consumer.abandoned());

    auto result = run(consumer.promiseOr(hcomm::Err("Abandoned"s)));
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), "Abandoned");
}

TEST(BridgeTest, CompleterCompleteOk) {
    auto [completer, consumer] = hcomm::makeBridge<void, std::string>();
    EXPECT_TRUE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    completer.completeOk();
    EXPECT_FALSE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    auto result = run(consumer.promise());
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_TRUE(result.isOk());
}

TEST(BridgeTest, CompleterCompleteOkInt) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    completer.completeOk(1122);
    EXPECT_FALSE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    auto result = run(consumer.promise());
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), 1122);
}

TEST(BridgeTest, CompleterCompleteErr) {
    auto [completer, consumer] = hcomm::makeBridge<>();
    EXPECT_TRUE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    completer.completeErr();
    EXPECT_FALSE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    auto result = run(consumer.promise());
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_TRUE(result.isErr());
}

TEST(BridgeTest, CompleterCompleteErrInt) {
    auto [completer, consumer] = hcomm::makeBridge<void, int>();
    EXPECT_TRUE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    completer.completeErr(42);
    EXPECT_FALSE(static_cast<bool>(completer));
    EXPECT_FALSE(consumer.abandoned());

    auto result = run(consumer.promise());
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), 42);
}

TEST(BridgeTest, ConsumerExplicitCancel) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    consumer.cancel();
    EXPECT_FALSE(consumer);
    EXPECT_TRUE(completer.cancelled());

    completer.completeOk(7);
    EXPECT_FALSE(static_cast<bool>(completer));
}

TEST(BridgeTest, ConsumerDrops) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    {
        auto cons = std::move(consumer);
    }
    EXPECT_FALSE(consumer);
    EXPECT_TRUE(completer.cancelled());

    completer.completeOk(7);
    EXPECT_FALSE(static_cast<bool>(completer));
}

TEST(BridgeTest, ConsumerPromiseCompleteOk) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    auto promise = consumer.promise();
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    completer.completeOk(42);
    EXPECT_FALSE(static_cast<bool>(completer));

    auto result = run(std::move(promise));
    EXPECT_EQ(result.value(), 42);
}

TEST(BridgeTest, ConsumerPromiseAbandoned) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    auto promise = consumer.promise();
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    completer.abandon();
    EXPECT_FALSE(static_cast<bool>(completer));

    auto result = run(std::move(promise));
    EXPECT_TRUE(result.isPending());
}

TEST(BridgeTest, ConsumerPromiseOrCompleteOk) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    auto promise = consumer.promiseOr(hcomm::Err("Abandoned"s));
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    completer.completeOk(42);
    EXPECT_FALSE(static_cast<bool>(completer));

    auto result = run(std::move(promise));
    EXPECT_EQ(result.value(), 42);
}

TEST(BridgeTest, ConsumerPromiseOrAbandoned) {
    auto [completer, consumer] = hcomm::makeBridge<int, std::string>();
    EXPECT_TRUE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    auto promise = consumer.promiseOr(hcomm::Err("Abandoned"s));
    EXPECT_FALSE(static_cast<bool>(consumer));
    EXPECT_FALSE(completer.cancelled());

    completer.abandon();
    EXPECT_FALSE(static_cast<bool>(completer));

    auto result = run(std::move(promise));
    EXPECT_EQ(result.error(), "Abandoned");
}

TEST(BridgeTest, scheduleFor) {
    int cnt = 0;
    std::binary_semaphore sem{0};
    hcomm::SingleThreadedExecutor exec;

    hcomm::Consumer<int> consumer =
        hcomm::scheduleFor(&exec, hcomm::makePromise([&cnt](hcomm::Context& ctx) -> hcomm::Result<int> {
            ++cnt;
            return hcomm::Ok(42);
        }));
    exec.run();

    exec.schedule(consumer.promise().then([&cnt, &sem](const hcomm::Result<int>& result) -> hcomm::Result<> {
        EXPECT_EQ(result.value(), 42);
        ++cnt;
        sem.release();
        return hcomm::Ok();
    }));
    sem.acquire();
    EXPECT_EQ(cnt, 2);
}
} // namespace
