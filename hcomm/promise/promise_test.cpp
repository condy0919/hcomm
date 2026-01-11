// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/promise.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using namespace std::string_literals;

namespace {
class PromiseTest : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}

protected:
    struct UnusedContext : hcomm::Context {
        hcomm::Executor* executor() override {
            return nullptr;
        }

        hcomm::Waker waker() override {
            std::unreachable();
        }
    };

    UnusedContext ctx_;
};

TEST_F(PromiseTest, Empty) {
    hcomm::Promise<int, int> promise1;
    EXPECT_FALSE(promise1);

    hcomm::Promise<int, int> promise2(nullptr);
    EXPECT_FALSE(promise2);
}

TEST_F(PromiseTest, makeResultPromise) {
    const auto r0 = hcomm::makeResultPromise<int, std::string>(hcomm::Ok(42))(ctx_);
    EXPECT_TRUE(r0.isOk());
    EXPECT_EQ(r0.value(), 42);

    const auto r1 = hcomm::makeResultPromise<int, std::string>(hcomm::Err("oops"s))(ctx_);
    EXPECT_TRUE(r1.isErr());
    EXPECT_EQ(r1.error(), "oops");

    const auto r2 = hcomm::makeResultPromise<int, std::string>(hcomm::Pending{})(ctx_);
    EXPECT_TRUE(r2.isPending());

    const auto r3 = hcomm::makeResultPromise<int, int>(hcomm::Ok(10))(ctx_);
    EXPECT_TRUE(r3.isOk());
    EXPECT_EQ(r3.value(), 10);

    const auto r4 = hcomm::makeResultPromise<int, std::string>("oops"s)(ctx_);
    EXPECT_TRUE(r4.isErr());
    EXPECT_EQ(r4.error(), "oops");
}

TEST_F(PromiseTest, makeOkPromise) {
    const auto r0 = hcomm::makeOkPromise(2026)(ctx_);
    EXPECT_TRUE(r0.isOk());
    EXPECT_EQ(r0.value(), 2026);

    const auto r1 = hcomm::makeOkPromise()(ctx_);
    EXPECT_TRUE(r1.isOk());
    EXPECT_TRUE(std::is_void_v<typename decltype(r1)::ValueType>);
    // r1.value() is disabled as T=void
}

TEST_F(PromiseTest, makeErrPromise) {
    const auto r0 = hcomm::makeErrPromise(2001)(ctx_);
    EXPECT_TRUE(r0.isErr());
    EXPECT_EQ(r0.error(), 2001);

    const auto r1 = hcomm::makeErrPromise()(ctx_);
    EXPECT_TRUE(r1.isErr());
    EXPECT_TRUE(std::is_void_v<typename decltype(r1)::ErrorType>);
    // r1.error() is disabled as E=void
}

TEST_F(PromiseTest, makePromise) {
    int cnt = 0;
    auto promise = hcomm::makePromise([&cnt]() -> hcomm::Result<> {
        if (++cnt == 2) {
            return hcomm::Ok();
        }
        return hcomm::Pending();
    });
    EXPECT_TRUE(promise);

    auto result = promise(ctx_);
    EXPECT_EQ(cnt, 1);
    EXPECT_TRUE(result.isPending());
    EXPECT_TRUE(promise);

    result = promise(ctx_);
    EXPECT_EQ(cnt, 2);
    EXPECT_TRUE(result.isOk());
    EXPECT_FALSE(promise);

    // Returns a PromiseImpl
    int cnt1 = 0, cnt2 = 0;
    auto p = hcomm::makePromise([&cnt1, &cnt2] {
        ++cnt1;
        return hcomm::makePromise([&cnt2] -> hcomm::Result<int, char> {
            if (++cnt2 == 2) {
                return hcomm::Ok(42);
            }
            return hcomm::Pending{};
        });
    });
    static_assert(std::is_same_v<decltype(p)::ValueType, int>);
    static_assert(std::is_same_v<decltype(p)::ErrorType, char>);

    auto ret = p(ctx_);
    EXPECT_EQ(cnt1, 1);
    EXPECT_EQ(cnt2, 1);
    EXPECT_TRUE(ret.isPending());
    EXPECT_TRUE(p); // p is unresolved

    ret = p(ctx_);
    EXPECT_EQ(cnt1, 1);
    EXPECT_EQ(cnt2, 2);
    EXPECT_TRUE(ret.isOk());
    EXPECT_EQ(ret.value(), 42);
    EXPECT_FALSE(p); // p is fulfilled
}

TEST_F(PromiseTest, takeContinuation) {
    auto shared = std::make_shared<int>(0);

    auto promise = hcomm::makePromise([shared] -> hcomm::Result<> {
        ++*shared;
        return hcomm::Pending{};
    });

    auto f = promise.takeContinuation();
    EXPECT_FALSE(promise); // no continuation anymore
    EXPECT_EQ(*shared, 0);

    auto result = f(ctx_);
    EXPECT_EQ(*shared, 1);
    EXPECT_TRUE(result.isPending());
}

TEST_F(PromiseTest, assign) {
    hcomm::Promise<> empty;
    EXPECT_FALSE(empty);

    int cnt = 0;
    auto promise = hcomm::makePromise([&cnt] -> hcomm::Result<> {
                       ++cnt;
                       return hcomm::Pending{};
                   }).box();
    EXPECT_TRUE(promise);

    // x = empty;
    auto x = std::move(empty);
    EXPECT_FALSE(x);

    // promise = empty
    // y holds a continuation
    auto y = std::move(promise);
    EXPECT_TRUE(y);
    EXPECT_FALSE(promise);

    y(ctx_);
    EXPECT_EQ(cnt, 1);

    // x holds a continuation, while y not.
    x.swap(y);
    EXPECT_TRUE(x);
    EXPECT_FALSE(y);

    x(ctx_);
    EXPECT_EQ(cnt, 2);

    // both x and y are empty.
    x = nullptr;
    EXPECT_FALSE(x);

    // x = empty, y holds a continuation.
    y = std::move_only_function<hcomm::Result<>(hcomm::Context&)>([&cnt](hcomm::Context&) -> hcomm::Result<> {
        cnt *= 2;
        return hcomm::Pending{};
    });
    EXPECT_TRUE(y);

    y(ctx_);
    EXPECT_EQ(cnt, 4);

    // x holds, y = empty
    x = std::move(y);
    EXPECT_TRUE(x);
    EXPECT_FALSE(y);

    x(ctx_);
    EXPECT_EQ(cnt, 8);

    // both x and y are empty
    x = std::move(y);
    EXPECT_FALSE(x);
    EXPECT_FALSE(y);
}

TEST_F(PromiseTest, OkThen) {
    int cnt = 0;
    auto p = hcomm::makeOkPromise(42).then([&](const hcomm::Result<int>& result) -> hcomm::Result<> {
        if (++cnt == 2) {
            return hcomm::Ok();
        }
        return hcomm::Pending{};
    });

    auto result = p(ctx_);
    EXPECT_TRUE(p);
    EXPECT_EQ(cnt, 1);
    EXPECT_TRUE(result.isPending());

    result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 2);
    EXPECT_TRUE(result.isOk());
}

TEST_F(PromiseTest, ErrorThen) {
    int cnt = 0;
    auto p = hcomm::makeErrPromise(42).then([&](const hcomm::Result<void, int>& result) -> hcomm::Result<> {
        if (++cnt == 2) {
            return hcomm::Ok();
        }
        return hcomm::Pending{};
    });

    auto result = p(ctx_);
    EXPECT_TRUE(p);
    EXPECT_EQ(cnt, 1);
    EXPECT_TRUE(result.isPending());

    result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 2);
    EXPECT_TRUE(result.isOk());
}

TEST_F(PromiseTest, thenAll) {
    int cnt = 0;
    auto p = hcomm::makeOkPromise(42)
                 .then([&](hcomm::Result<int>& result) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(result.value() + 1);
                 })
                 .then([&](const hcomm::Result<int>& result) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(result.value() + 1);
                 })
                 .then([&](hcomm::Context& ctx, hcomm::Result<int>& result) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(result.value() + 1);
                 })
                 .then([&](hcomm::Context& ctx, const hcomm::Result<int>& result) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(result.value() + 1);
                 });

    auto result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 4);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), 46);
}

TEST_F(PromiseTest, OkAndThen) {
    int cnt = 0;
    auto p = hcomm::makeOkPromise(42).andThen([&](const int& x) -> hcomm::Result<int> {
        ++cnt;
        if (cnt == 2) {
            return hcomm::Err();
        }
        return hcomm::Pending{};
    });

    auto result = p(ctx_);
    EXPECT_TRUE(p);
    EXPECT_EQ(cnt, 1);
    EXPECT_TRUE(result.isPending());

    result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 2);
    EXPECT_TRUE(result.isErr());
}

TEST_F(PromiseTest, ErrorAndThen) {
    int cnt = 0;
    auto p = hcomm::makeErrPromise(42).andThen([&]() -> hcomm::Result<int, int> {
        if (++cnt == 2) {
            return hcomm::Ok(43);
        }
        return hcomm::Pending{};
    });

    auto result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 0);
    EXPECT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), 42);
}

TEST_F(PromiseTest, andThenAll) {
    int cnt = 0;
    auto p = hcomm::makeOkPromise(42)
                 .andThen([&](int& x) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(x + 1);
                 })
                 .andThen([&](const int& x) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(x + 1);
                 })
                 .andThen([&](hcomm::Context& ctx, int& x) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(x + 1);
                 })
                 .andThen([&](hcomm::Context& ctx, const int& x) -> hcomm::Result<int> {
                     ++cnt;
                     return hcomm::Ok(x + 1);
                 });

    auto result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 4);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), 46);
}

TEST_F(PromiseTest, OkOrElse) {
    int cnt = 0;
    // Promise<int, int> that is Ok(42)
    auto p = hcomm::makeResultPromise<int, int>(hcomm::Ok(42)).orElse([&](const int& err) -> hcomm::Result<int, int> {
        ++cnt;
        return hcomm::Ok(0);
    });

    auto result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 0);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(PromiseTest, ErrorOrElse) {
    int cnt = 0;
    // Promise<int, int> that is Err(42)
    auto p = hcomm::makeResultPromise<int, int>(hcomm::Err(42)).orElse([&](int& err) -> hcomm::Result<int, int> {
        if (++cnt == 2) {
            return hcomm::Ok(err + 1);
        }
        return hcomm::Pending{};
    });

    auto result = p(ctx_);
    EXPECT_TRUE(p);
    EXPECT_EQ(cnt, 1);
    EXPECT_TRUE(result.isPending());

    result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 2);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), 43);
}

TEST_F(PromiseTest, orElseAll) {
    int cnt = 0;
    auto p = hcomm::makeResultPromise<int, int>(hcomm::Err(42))
                 .orElse([&](int& err) -> hcomm::Result<int, int> {
                     ++cnt;
                     return hcomm::Err(err + 1);
                 })
                 .orElse([&](const int& err) -> hcomm::Result<int, int> {
                     ++cnt;
                     return hcomm::Err(err + 1);
                 })
                 .orElse([&](hcomm::Context& ctx, int& err) -> hcomm::Result<int, int> {
                     ++cnt;
                     return hcomm::Err(err + 1);
                 })
                 .orElse([&](hcomm::Context& ctx, const int& err) -> hcomm::Result<int, int> {
                     ++cnt;
                     // Finally recover
                     return hcomm::Ok(100);
                 });

    auto result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 4);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), 100);
}

TEST_F(PromiseTest, inspectWithoutContextRef) {
    auto p = hcomm::makeResultPromise<int, std::string>(hcomm::Err("foo"s))
                 .inspect([](hcomm::Result<int, std::string>& result) {
                     if (result.isOk()) {
                         result.value() = 42;
                     } else {
                         result.error() += "bar";
                     }
                 });
    auto result = p(ctx_);
    EXPECT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), "foobar");
}

TEST_F(PromiseTest, inspectWithContextRef) {
    auto p = hcomm::makeResultPromise<int, std::string>(hcomm::Ok(42))
                 .inspect([](hcomm::Context& ctx, hcomm::Result<int, std::string>& result) {
                     EXPECT_TRUE(result.isOk());
                     EXPECT_EQ(result.value(), 42);
                 });
    auto result = p(ctx_);
    EXPECT_TRUE(result.isOk());
    EXPECT_FALSE(p);
}

TEST_F(PromiseTest, discard) {
    auto p = hcomm::makeResultPromise<int, std::string>(hcomm::Err("oops"s)).discard();
    auto result = p(ctx_);
    EXPECT_TRUE(result.isOk());
    EXPECT_TRUE((std::is_same_v<decltype(result)::ValueType, void>));
    EXPECT_TRUE((std::is_same_v<decltype(result)::ErrorType, void>));
}

TEST_F(PromiseTest, joinPromise) {
    int cnt = 0;
    auto p = hcomm::joinPromises(
        hcomm::makeOkPromise(42),
        hcomm::makeErrPromise('a').orElse([](const char& err) -> hcomm::Result<void, char> { return hcomm::Err('y'); }),
        hcomm::makePromise([&]() -> hcomm::Result<std::string, void> {
            if (++cnt == 2) {
                return hcomm::Ok("oops"s);
            }
            return hcomm::Pending{};
        }));

    auto result = p(ctx_);
    EXPECT_TRUE(p);
    EXPECT_EQ(cnt, 1);
    EXPECT_TRUE(result.isPending());

    result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_EQ(cnt, 2);
    EXPECT_TRUE(result.isOk());

    auto& [r0, r1, r2] = result.value();
    EXPECT_EQ(r0.value(), 42);
    EXPECT_EQ(r1.error(), 'y');
    EXPECT_EQ(r2.value(), "oops");
}

TEST_F(PromiseTest, joinPromiseWithMoveOnlyResult) {
    auto p = hcomm::joinPromises(hcomm::makeOkPromise<std::unique_ptr<int>>(std::make_unique<int>(10)),
                                 hcomm::makeErrPromise<std::unique_ptr<int>>(std::make_unique<int>(11)))
                 .then([](hcomm::Result<
                           std::tuple<hcomm::Result<std::unique_ptr<int>>, hcomm::Result<void, std::unique_ptr<int>>>>&
                              result) -> hcomm::Result<std::unique_ptr<int>, int> {
                     auto [r0, r1] = result.takeValue();
                     if (r0.isOk() && r1.isErr()) {
                         int value = *r0.takeValue() + *r1.takeError();
                         return hcomm::Ok(std::make_unique<int>(value));
                     }
                     return hcomm::Err(-1);
                 });

    auto result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(*result.value(), 21);
}

TEST_F(PromiseTest, joinVectorPromise) {
    std::vector<hcomm::Promise<int>> promises;
    promises.emplace_back(hcomm::makeOkPromise(42));
    promises.emplace_back(hcomm::makeOkPromise(-1));

    auto p = hcomm::joinPromises(std::move(promises));
    auto result = p(ctx_);
    EXPECT_FALSE(p);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value()[0].value(), 42);
    EXPECT_EQ(result.value()[1].value(), -1);
}

} // namespace
