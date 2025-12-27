// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/result.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using namespace std::string_literals;

namespace {
struct NoDefault {
    NoDefault(int, int) {}
};

TEST(Result, NoDefault) {
    hcomm::Result<NoDefault> x{hcomm::Ok(NoDefault(11, 22))};
    EXPECT_TRUE(x.isOk());
}

TEST(Result, const) {
    hcomm::Result<const int> res{hcomm::Ok(33)};
    EXPECT_FALSE(std::is_copy_assignable_v<decltype(res)>);
}

TEST(Result, state) {
    hcomm::Result<> good{hcomm::Ok()};
    EXPECT_EQ(good.state(), hcomm::ResultState::Ok);
    EXPECT_TRUE(good.isOk());
    EXPECT_FALSE(good.isPending());
    EXPECT_FALSE(good.isErr());

    hcomm::Result<> bad{hcomm::Err()};
    EXPECT_EQ(bad.state(), hcomm::ResultState::Error);
    EXPECT_TRUE(bad.isErr());
    EXPECT_FALSE(bad.isOk());
    EXPECT_FALSE(bad.isPending());

    hcomm::Result<> pending{hcomm::Pending()};
    EXPECT_EQ(pending.state(), hcomm::ResultState::Pending);
    EXPECT_TRUE(pending.isPending());
    EXPECT_FALSE(pending.isOk());
    EXPECT_FALSE(pending.isErr());
}

struct ExpectingDeleter {
    int expected;

    ExpectingDeleter(int exp) : expected(exp) {};

    void operator()(const int* p) {
        EXPECT_EQ(*p, expected);
        delete p;
    }
};

TEST(Result, value) {
    hcomm::Result<int> good1{hcomm::Ok(11)};
    auto& v1 = good1.value();
    EXPECT_EQ(v1, 11);

    v1 = 12;
    EXPECT_EQ(good1.value(), 12);

    hcomm::Result<const int> good2{hcomm::Ok(22)};
    auto& v2 = good2.value();
    EXPECT_EQ(v2, 22);

    // Unable to modify v2 as it's const qualified.
    // v2 = 23;

    // Calling value() on RValue moves the unique_ptr out.
    auto ptr =
        hcomm::Result<std::unique_ptr<int, ExpectingDeleter>>{
            hcomm::Ok(std::unique_ptr<int, ExpectingDeleter>(new int{42}, ExpectingDeleter{1337}))}
            .value();
    *ptr = 1337;
}

TEST(Result, takeValue) {
    hcomm::Result<int> good{hcomm::Ok(11)};

    int x = good.takeValue();
    EXPECT_EQ(x, 11);
    EXPECT_TRUE(good.isPending());
}

TEST(Result, takeError) {
    hcomm::Result<void, int> bad{hcomm::Err(22)};

    int x = bad.takeError();
    EXPECT_EQ(x, 22);
    EXPECT_TRUE(bad.isPending());
}

TEST(Result, swap) {
    hcomm::Result<int, std::string> x1, x2;

    x1 = hcomm::Ok(12);
    x2 = hcomm::Err("failed"s);

    using std::swap;
    swap(x1, x2);

    EXPECT_EQ(x1.error(), "failed");
    EXPECT_EQ(x2.value(), 12);
}

TEST(Result, map) {
    hcomm::Result<int, int> ex{hcomm::Ok(3)};

    auto ex2 = ex.map([](int x) { return x + 1; });
    EXPECT_EQ(ex2.value(), 4);
}

TEST(Result, mapErr) {
    hcomm::Result<int, int> ex{hcomm::Err(4)};

    auto ex2 = ex.mapErr([](int x) { return x + 1; });
    EXPECT_EQ(ex2.error(), 5);
}

struct MoveOnly {
    int value;

    MoveOnly(int x) : value(x) {}

    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
};

TEST(Result, MoveOnly) {
    hcomm::Result<MoveOnly> good{hcomm::Ok<MoveOnly>(42)};
    EXPECT_EQ(good.value().value, 42);

    hcomm::Result<MoveOnly> tmp{std::move(good)};
    EXPECT_TRUE(good.isPending());
    EXPECT_TRUE(tmp.isOk());
    EXPECT_EQ(tmp.value().value, 42);

    MoveOnly y = tmp.takeValue();
    EXPECT_TRUE(tmp.isPending());
    EXPECT_EQ(y.value, 42);
}

TEST(Result, void_map) {
    hcomm::Result<void, int> ex{hcomm::Ok()};

    // ex2 is Result<int, int>
    auto ex2 = ex.map([]() { return 42; });
    EXPECT_TRUE(ex2.isOk());
    EXPECT_EQ(ex2.value(), 42);

    // ex3 is Result<void, int>
    auto ex3 = ex2.map([](int x) {});
    EXPECT_TRUE(ex3.isOk());

    // ex4 is Result<void, int>
    auto ex4 = ex3.map([]() {});
    EXPECT_TRUE(ex4.isOk());
}

TEST(Result, void_mapErr) {
    hcomm::Result<int, void> ex{hcomm::Err()};

    // ex2 is Result<int, int>
    auto ex2 = ex.mapErr([]() { return 42; });
    EXPECT_TRUE(ex2.isErr());
    EXPECT_EQ(ex2.error(), 42);

    // ex3 is Result<int, void>
    auto ex3 = ex2.mapErr([](int) {});
    EXPECT_TRUE(ex3.isErr());

    // ex4 is Result<int, void>
    auto ex4 = ex3.mapErr([]() {});
    EXPECT_TRUE(ex4.isErr());
}

TEST(Result, andThen) {
    hcomm::Result<int, int> ex{hcomm::Ok(3)};

    auto ex2 = ex.andThen([](int x) -> hcomm::Result<std::string, int> { return hcomm::Ok(std::to_string(x)); });
    EXPECT_TRUE(ex2.isOk());
    EXPECT_EQ(ex2.value(), "3");

    auto ex3 = ex.andThen([](int) -> hcomm::Result<std::string, int> { return hcomm::Err(4); });
    EXPECT_TRUE(ex3.isErr());
    EXPECT_EQ(ex3.error(), 4);
}

TEST(Result, orElse) {
    hcomm::Result<int, int> ex{hcomm::Err(3)};

    auto ex2 = ex.orElse([](int x) -> hcomm::Result<int, std::string> { return hcomm::Err(std::to_string(x)); });
    EXPECT_TRUE(ex2.isErr());
    EXPECT_EQ(ex2.error(), "3");

    auto ex3 = ex.orElse([](int) -> hcomm::Result<int, std::string> { return hcomm::Ok(4); });
    EXPECT_TRUE(ex3.isOk());
    EXPECT_EQ(ex3.value(), 4);
}

TEST(Result, valueOr) {
    hcomm::Result<int, int> ex{hcomm::Ok(3)};
    EXPECT_EQ(ex.valueOr(4), 3);

    hcomm::Result<int, int> err{hcomm::Err(3)};
    EXPECT_EQ(err.valueOr(4), 4);
}

TEST(Result, equality) {
    hcomm::Result<int, int> ok1{hcomm::Ok(1)};
    hcomm::Result<int, int> ok1_copy{hcomm::Ok(1)};
    hcomm::Result<int, int> ok2{hcomm::Ok(2)};
    hcomm::Result<int, int> err1{hcomm::Err(1)};
    hcomm::Result<int, int> pending{hcomm::Pending{}};

    EXPECT_EQ(ok1, ok1_copy);
    EXPECT_NE(ok1, ok2);
    EXPECT_NE(ok1, err1);
    EXPECT_NE(ok1, pending);
    EXPECT_EQ(pending, (hcomm::Result<int, int>{hcomm::Pending()}));

    hcomm::Result<void> void_ok{hcomm::Ok()};
    hcomm::Result<void> void_ok2{hcomm::Ok()};
    EXPECT_EQ(void_ok, void_ok2);
}

} // namespace
