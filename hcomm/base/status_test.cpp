// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/base/status.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {
TEST(Status, CheckOk) {
    hcomm::Status status(hcomm::StatusCode::Ok);
    EXPECT_TRUE(status.ok());
}

TEST(Status, CheckCode) {
    hcomm::Status status(hcomm::StatusCode::InvalidArgument, "invalid argument");
    EXPECT_EQ(status.code(), hcomm::StatusCode::InvalidArgument);
    EXPECT_EQ(status.message(), "invalid argument");
}

TEST(StatusOr, Ok) {
    hcomm::StatusOr<int> st(0);
    EXPECT_TRUE(st.ok());
}

TEST(StatusOr, ImplicitConstruct) {
    auto f = [](int x) -> hcomm::StatusOr<std::string> {
        if (x & 1) {
            return "odd";
        } else {
            return hcomm::StatusCode::InvalidArgument;
        }
    };

    auto odd = f(1);
    EXPECT_TRUE(odd.ok());
    EXPECT_EQ(*odd, "odd");

    auto invalid = f(2);
    EXPECT_FALSE(invalid.ok());
    EXPECT_EQ(invalid.status().code(), hcomm::StatusCode::InvalidArgument);
}

hcomm::StatusOr<std::unique_ptr<int>> returnUniquePtr(int n) {
    // Uses implicit constructor from U&&
    return std::make_unique<int>(n);
}

TEST(StatusOr, MoveOnlyConstruct) {
    hcomm::StatusOr<std::unique_ptr<int>> thing = returnUniquePtr(0x11);
    EXPECT_TRUE(thing.ok());
    EXPECT_EQ(**thing, 0x11);
}

TEST(StatusOr, MoveOnlyVector) {
    std::vector<hcomm::StatusOr<std::unique_ptr<int>>> vec;
    vec.push_back(returnUniquePtr(0x22));
    vec.resize(2);

    auto copy = std::move(vec);
    EXPECT_EQ(**copy[0], 0x22);
    EXPECT_EQ(copy[1].status().code(), hcomm::StatusCode::Unknown);
}

TEST(StatusOr, TestDefaultCtor) {
    hcomm::StatusOr<int> thing;
    EXPECT_FALSE(thing.ok());
    EXPECT_EQ(thing.status().code(), hcomm::StatusCode::Unknown);
}

TEST(StatusOr, StatusCtorForwards) {
    hcomm::Status status(hcomm::StatusCode::Internal, "Some error");

    EXPECT_EQ(hcomm::StatusOr<int>(status).status().message(), "Some error");
    EXPECT_EQ(status.message(), "Some error");
}

TEST(StatusOr, BadAccess) {
    hcomm::StatusOr<int> thing;
    EXPECT_THROW(thing.value(), hcomm::BadStatusOrAccess);

    const hcomm::StatusOr<int> cthing;
    EXPECT_THROW(cthing.value(), hcomm::BadStatusOrAccess);
}

TEST(StatusOr, ValueCtor) {
    const int pi = 31415926;
    hcomm::StatusOr<int> thing(pi);
    EXPECT_TRUE(thing.ok());
    EXPECT_EQ(*thing, pi);
}

struct Inplace {
    const int value;
    explicit Inplace(int x) : value(x) {}
};

TEST(StatusOr, InplaceCtor) {
    hcomm::StatusOr<Inplace> inp(std::in_place, 20);
    EXPECT_EQ(inp->value, 20);
}

TEST(StatusOr, Emplace) {
    hcomm::StatusOr<Inplace> inp(10);
    inp.emplace(20);
    EXPECT_EQ(inp->value, 20);

    hcomm::StatusOr<Inplace> unknown;
    EXPECT_EQ(unknown.status().code(), hcomm::StatusCode::Unknown);

    unknown.emplace(20);
    EXPECT_TRUE(unknown.ok());
    EXPECT_EQ(unknown->value, 20);
}

TEST(StatusOr, TestCopyCtorStatusOk) {
    const int kI = 4;
    const hcomm::StatusOr<int> original(kI);
    const hcomm::StatusOr<int> copy(original);
    EXPECT_TRUE(copy.ok());
    EXPECT_EQ(*original, *copy);
}

TEST(StatusOr, TestCopyCtorStatusNotOk) {
    hcomm::StatusOr<int> original(hcomm::StatusCode::Cancelled);
    hcomm::StatusOr<int> copy(original);
    EXPECT_EQ(copy.status().code(), hcomm::StatusCode::Cancelled);
}

class CopyNoAssign {
public:
    explicit CopyNoAssign(int value) : foo(value) {}
    CopyNoAssign(const CopyNoAssign& other) : foo(other.foo) {}
    int foo;

private:
    const CopyNoAssign& operator=(const CopyNoAssign&);
};

TEST(StatusOr, CopyCtorNonAssignable) {
    const int kI = 4;
    CopyNoAssign value(kI);
    hcomm::StatusOr<CopyNoAssign> original(value);
    hcomm::StatusOr<CopyNoAssign> copy(original);
    EXPECT_TRUE(copy.ok());
    EXPECT_EQ(original->foo, copy->foo);
}

TEST(StatusOr, CopyCtorConverting) {
    hcomm::StatusOr<int> original(32);
    hcomm::StatusOr<double> copy(original);
    EXPECT_TRUE(copy.ok());
    EXPECT_DOUBLE_EQ(*original, *copy);
}

TEST(StatusOr, CopyCtorConvertingNotOk) {
    hcomm::StatusOr<int> original(hcomm::StatusCode::NotSupported);
    hcomm::StatusOr<double> copy(original);
    EXPECT_FALSE(copy.ok());
    EXPECT_EQ(copy.status().code(), hcomm::StatusCode::NotSupported);
}

TEST(StatusOr, AssignmentStatusOk) {
    // Copy assignmment
    {
        const auto p = std::make_shared<int>(17);
        hcomm::StatusOr<std::shared_ptr<int>> source(p);

        hcomm::StatusOr<std::shared_ptr<int>> target;
        target = source;

        ASSERT_TRUE(target.ok());
        EXPECT_EQ(p, *target);

        ASSERT_TRUE(source.ok());
        EXPECT_EQ(p, *source);
    }

    // Move assignment
    {
        const auto p = std::make_shared<int>(17);
        hcomm::StatusOr<std::shared_ptr<int>> source(p);

        hcomm::StatusOr<std::shared_ptr<int>> target;
        target = std::move(source);

        ASSERT_TRUE(target.ok());
        EXPECT_EQ(p, *target);

        ASSERT_TRUE(source.ok());
        EXPECT_EQ(nullptr, *source);
    }
}

TEST(StatusOr, AssignmentStatusNotOk) {
    // Copy assignment
    {
        const hcomm::Status expected(hcomm::StatusCode::Cancelled);
        hcomm::StatusOr<int> source(expected);

        hcomm::StatusOr<int> target;
        target = source;

        EXPECT_FALSE(target.ok());

        EXPECT_FALSE(source.ok());
    }

    // Move assignment
    {
        const hcomm::Status expected(hcomm::StatusCode::Cancelled);
        hcomm::StatusOr<int> source(expected);

        hcomm::StatusOr<int> target;
        target = std::move(source);

        EXPECT_FALSE(target.ok());

        EXPECT_FALSE(source.ok());
        EXPECT_EQ(source.status().code(), hcomm::StatusCode::Cancelled);
    }
}

TEST(StatusOr, AssignmentStatusOKConverting) {
    // Copy assignment
    {
        const int kI = 4;
        hcomm::StatusOr<int> source(kI);

        hcomm::StatusOr<double> target;
        target = source;

        ASSERT_TRUE(target.ok());
        EXPECT_DOUBLE_EQ(kI, *target);

        ASSERT_TRUE(source.ok());
        EXPECT_DOUBLE_EQ(kI, *source);
    }

    // Move assignment
    {
        hcomm::StatusOr<std::unique_ptr<int>> source(returnUniquePtr(17));

        hcomm::StatusOr<std::shared_ptr<int>> target;
        target = std::move(source);

        ASSERT_TRUE(target.ok());
        ASSERT_TRUE(source.ok());
        EXPECT_EQ(nullptr, source->get());
    }
}

struct A {
    int x;
};

struct ImplicitConstructibleFromA {
    int x;
    bool moved;
    ImplicitConstructibleFromA(const A& a) : x(a.x), moved(false) {}
    ImplicitConstructibleFromA(A&& a) : x(a.x), moved(true) {}
};

TEST(StatusOr, ImplicitConvertingConstructor) {
    hcomm::StatusOr<ImplicitConstructibleFromA> p1(hcomm::StatusOr<A>{20});
    EXPECT_TRUE(p1.ok());
    EXPECT_EQ(p1->x, 20);
    EXPECT_TRUE(p1->moved);

    hcomm::StatusOr<A> a{12};
    hcomm::StatusOr<ImplicitConstructibleFromA> p2(a);
    EXPECT_TRUE(p2.ok());
    EXPECT_EQ(p2->x, 12);
    EXPECT_FALSE(p2->moved);
}

struct ExplicitConstructibleFromA {
    int x;
    bool moved;
    explicit ExplicitConstructibleFromA(const A& a) : x(a.x), moved(false) {}
    explicit ExplicitConstructibleFromA(A&& a) : x(a.x), moved(true) {}
};

TEST(StatusOr, ExplicitConvertingConstructor) {
    hcomm::StatusOr<ExplicitConstructibleFromA> p1(hcomm::StatusOr<A>{11});
    EXPECT_TRUE(p1.ok());
    EXPECT_EQ(p1->x, 11);
    EXPECT_TRUE(p1->moved);

    hcomm::StatusOr<A> a{12};
    hcomm::StatusOr<ExplicitConstructibleFromA> p2(a);
    EXPECT_TRUE(p2.ok());
    EXPECT_EQ(p2->x, 12);
    EXPECT_FALSE(p2->moved);
}

TEST(StatusOr, ConstExplicitConstruction) {
    hcomm::StatusOr<bool> b1(hcomm::StatusOr<const bool>{true});
    EXPECT_TRUE(b1.ok());
    EXPECT_TRUE(*b1);

    hcomm::StatusOr<bool> b2(hcomm::StatusOr<const bool>{false});
    EXPECT_TRUE(b2.ok());
    EXPECT_FALSE(*b2);

    hcomm::StatusOr<const bool> b3(hcomm::StatusOr<bool>{true});
    EXPECT_TRUE(b3.ok());
    EXPECT_TRUE(*b3);

    hcomm::StatusOr<const bool> b4(hcomm::StatusOr<bool>{true});
    EXPECT_TRUE(b4.ok());
    EXPECT_TRUE(*b4);
}

struct Copyable {
    Copyable() {}
    Copyable(const Copyable&) {}
    Copyable& operator=(const Copyable&) {
        return *this;
    }
};

struct MoveOnly {
    MoveOnly() {}
    MoveOnly(MoveOnly&&) noexcept {}
    MoveOnly& operator=(MoveOnly&&) noexcept {
        return *this;
    }
};

struct NonMovable {
    NonMovable() {}
    NonMovable(const NonMovable&) = delete;
    NonMovable(NonMovable&&) = delete;
    NonMovable& operator=(const NonMovable&) = delete;
    NonMovable& operator=(NonMovable&&) = delete;
};

TEST(StatusOr, CopyAndMoveAbility) {
    EXPECT_TRUE(std::is_copy_constructible<Copyable>::value);
    EXPECT_TRUE(std::is_copy_assignable<Copyable>::value);
    EXPECT_TRUE(std::is_move_constructible<Copyable>::value);
    EXPECT_TRUE(std::is_move_assignable<Copyable>::value);
    EXPECT_FALSE(std::is_copy_constructible<MoveOnly>::value);
    EXPECT_FALSE(std::is_copy_assignable<MoveOnly>::value);
    EXPECT_TRUE(std::is_move_constructible<MoveOnly>::value);
    EXPECT_TRUE(std::is_move_assignable<MoveOnly>::value);
    EXPECT_FALSE(std::is_copy_constructible<NonMovable>::value);
    EXPECT_FALSE(std::is_copy_assignable<NonMovable>::value);
    EXPECT_FALSE(std::is_move_constructible<NonMovable>::value);
    EXPECT_FALSE(std::is_move_assignable<NonMovable>::value);
}

TEST(StatusOr, ImplicitAssignment) {
    hcomm::StatusOr<std::variant<int, std::string>> status_or;

    status_or = 10;
    EXPECT_TRUE(status_or.ok());
    EXPECT_EQ(std::get<0>(*status_or), 10);
}

struct Base1 {};

struct Derived : Base1 {};

TEST(StatusOr, UniquePtrImplicitAssignment) {
    hcomm::StatusOr<std::unique_ptr<Base1>> p1;

    p1 = std::make_unique<Derived>();
    EXPECT_TRUE(p1.ok());
    EXPECT_NE(p1->get(), nullptr);
}

TEST(StatusOr, Pointer) {
    struct A {};
    struct B : public A {};
    struct C : private A {};

    EXPECT_TRUE((std::is_constructible<hcomm::StatusOr<A*>, B*>::value));
    EXPECT_TRUE((std::is_convertible<B*, hcomm::StatusOr<A*>>::value));
    EXPECT_FALSE((std::is_constructible<hcomm::StatusOr<A*>, C*>::value));
    EXPECT_FALSE((std::is_convertible<C*, hcomm::StatusOr<A*>>::value));
}

TEST(StatusOr, SelfAssignment) {
    // Copy-assignment, status OK
    {
        // A string long enough that it's likely to defeat any inline representation
        // optimization.
        const std::string long_str(128, 'a');

        hcomm::StatusOr<std::string> so = long_str;
        so = *&so;

        ASSERT_TRUE(so.ok());
        EXPECT_EQ(long_str, *so);
    }

    // Copy-assignment, error status
    {
        hcomm::StatusOr<int> so(hcomm::StatusCode::NotSupported);
        so = *&so;

        EXPECT_FALSE(so.ok());
        EXPECT_EQ(so.status().code(), hcomm::StatusCode::NotSupported);
    }

    // Move-assignment with copyable type, status OK
    {
        hcomm::StatusOr<int> so = 17;

        // Fool the compiler, which otherwise complains.
        auto& same = so;
        so = std::move(same);

        ASSERT_TRUE(so.ok());
        EXPECT_EQ(17, *so);
    }

    // Move-assignment with copyable type, error status
    {
        hcomm::StatusOr<int> so(hcomm::StatusCode::OutOfMemory);

        // Fool the compiler, which otherwise complains.
        auto& same = so;
        so = std::move(same);

        EXPECT_FALSE(so.ok());
        EXPECT_EQ(so.status().code(), hcomm::StatusCode::OutOfMemory);
    }

    // Move-assignment with non-copyable type, error status
    {
        hcomm::StatusOr<std::unique_ptr<int>> so(hcomm::StatusCode::OutOfRange);

        // Fool the compiler, which otherwise complains.
        auto& same = so;
        so = std::move(same);

        EXPECT_FALSE(so.ok());
        EXPECT_EQ(so.status().code(), hcomm::StatusCode::OutOfRange);
    }
}

TEST(StatusOr, OperatorArrow) {
    const hcomm::StatusOr<std::string> const_lvalue("hello");
    EXPECT_EQ(std::string("hello"), const_lvalue->c_str());

    hcomm::StatusOr<std::string> lvalue("hello");
    EXPECT_EQ(std::string("hello"), lvalue->c_str());
}

TEST(StatusOr, TestValue) {
    const int kI = 4;
    hcomm::StatusOr<int> thing(kI);
    EXPECT_EQ(kI, thing.value());
}

TEST(StatusOr, TestValueConst) {
    const int kI = 4;
    const hcomm::StatusOr<int> thing(kI);
    EXPECT_EQ(kI, thing.value());
}

TEST(StatusOr, TestPointerDefaultCtor) {
    hcomm::StatusOr<int*> thing;
    EXPECT_FALSE(thing.ok());
    EXPECT_EQ(thing.status().code(), hcomm::StatusCode::Unknown);
}

TEST(StatusOr, TestPointerValueCtor) {
    const int kI = 4;

    // Construction from a non-null pointer
    {
        hcomm::StatusOr<const int*> so(&kI);
        EXPECT_TRUE(so.ok());
        EXPECT_EQ(&kI, *so);
    }

    // Construction from a null pointer constant
    {
        hcomm::StatusOr<const int*> so(nullptr);
        EXPECT_TRUE(so.ok());
        EXPECT_EQ(nullptr, *so);
    }

    // Construction from a non-literal null pointer
    {
        const int* const p = nullptr;

        hcomm::StatusOr<const int*> so(p);
        EXPECT_TRUE(so.ok());
        EXPECT_EQ(nullptr, *so);
    }
}

TEST(StatusOr, StatusOrVectorOfUniquePointerCanReserveAndResize) {
    using EvilType = std::vector<std::unique_ptr<int>>;
    static_assert(std::is_copy_constructible<EvilType>::value, "");
    std::vector<hcomm::StatusOr<EvilType>> v(5);
    v.reserve(v.capacity() + 10);
    v.resize(v.capacity() + 10);
}

TEST(StatusOr, MapToStatusOrUniquePtr) {
    // A reduced version of a problematic type found in the wild. All of the
    // operations below should compile.
    using MapType = std::map<std::string, hcomm::StatusOr<std::unique_ptr<int>>>;

    MapType a;

    // Move-construction
    MapType b(std::move(a));

    // Move-assignment
    a = std::move(b);
}

TEST(StatusOr, ValueOrOk) {
    const hcomm::StatusOr<int> status_or = 0;
    EXPECT_EQ(status_or.valueOr(-1), 0);
}

TEST(StatusOr, ValueOrDefault) {
    const hcomm::StatusOr<int> status_or(hcomm::StatusCode::Unknown);
    EXPECT_EQ(status_or.valueOr(-1), -1);
}
} // namespace
