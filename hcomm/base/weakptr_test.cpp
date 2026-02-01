// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/base/weakptr.hpp"

#include <memory>

#include <gtest/gtest.h>

namespace {
class TrackedObject {
public:
    explicit TrackedObject(int* counter = nullptr) : counter_(counter), weak_ptr_factory_(this) {
        if (counter_) {
            (*counter_)++;
        }
    }

    ~TrackedObject() {
        if (counter_) {
            (*counter_)--;
        }
    }

    hcomm::WeakPtr<TrackedObject> getWeakPtr() {
        return weak_ptr_factory_.weak_from_this();
    }

    void doSomething() const {}

private:
    int* counter_;
    hcomm::WeakPtrFactory<TrackedObject> weak_ptr_factory_;
};

TEST(WeakPtrTest, DefaultConstructor) {
    hcomm::WeakPtr<TrackedObject> weak_ptr;
    EXPECT_TRUE(weak_ptr.expired());
    EXPECT_EQ(weak_ptr.get(), nullptr);
}

TEST(WeakPtrTest, NullptrConstructor) {
    hcomm::WeakPtr<TrackedObject> weak_ptr(nullptr);
    EXPECT_TRUE(weak_ptr.expired());
    EXPECT_EQ(weak_ptr.get(), nullptr);
}

TEST(WeakPtrTest, Lifecycle) {
    int counter = 0;
    hcomm::WeakPtr<TrackedObject> weak_ptr;

    {
        auto obj = std::make_unique<TrackedObject>(&counter);
        EXPECT_EQ(counter, 1);

        weak_ptr = obj->getWeakPtr();
        EXPECT_FALSE(weak_ptr.expired());
        EXPECT_NE(weak_ptr.get(), nullptr);
        EXPECT_EQ(weak_ptr.get(), obj.get());
    }

    EXPECT_EQ(counter, 0);
    EXPECT_TRUE(weak_ptr.expired());
    EXPECT_EQ(weak_ptr.get(), nullptr);
}

TEST(WeakPtrTest, CopyConstruct) {
    hcomm::WeakPtr<TrackedObject> weak_ptr1;
    {
        auto obj = std::make_unique<TrackedObject>();
        weak_ptr1 = obj->getWeakPtr();

        hcomm::WeakPtr<TrackedObject> weak_ptr2(weak_ptr1);
        EXPECT_FALSE(weak_ptr1.expired());
        EXPECT_FALSE(weak_ptr2.expired());
        EXPECT_EQ(weak_ptr1.get(), weak_ptr2.get());
    }
    EXPECT_TRUE(weak_ptr1.expired());
}

TEST(WeakPtrTest, MoveConstruct) {
    hcomm::WeakPtr<TrackedObject> weak_ptr1;
    TrackedObject* raw_ptr = nullptr;

    {
        auto obj = std::make_unique<TrackedObject>();
        raw_ptr = obj.get();
        weak_ptr1 = obj->getWeakPtr();

        hcomm::WeakPtr<TrackedObject> weak_ptr2(std::move(weak_ptr1));
        EXPECT_TRUE(weak_ptr1.expired()); // Moved-from is expired
        EXPECT_FALSE(weak_ptr2.expired());
        EXPECT_EQ(weak_ptr2.get(), raw_ptr);
    }
}

TEST(WeakPtrTest, CopyAssign) {
    hcomm::WeakPtr<TrackedObject> weak_ptr1;
    hcomm::WeakPtr<TrackedObject> weak_ptr2;
    {
        auto obj = std::make_unique<TrackedObject>();
        weak_ptr1 = obj->getWeakPtr();

        weak_ptr2 = weak_ptr1;
        EXPECT_FALSE(weak_ptr1.expired());
        EXPECT_FALSE(weak_ptr2.expired());
        EXPECT_EQ(weak_ptr1.get(), weak_ptr2.get());
    }
    EXPECT_TRUE(weak_ptr1.expired());
    EXPECT_TRUE(weak_ptr2.expired());
}

TEST(WeakPtrTest, MoveAssign) {
    hcomm::WeakPtr<TrackedObject> weak_ptr1;
    hcomm::WeakPtr<TrackedObject> weak_ptr2;
    TrackedObject* raw_ptr = nullptr;

    {
        auto obj = std::make_unique<TrackedObject>();
        raw_ptr = obj.get();
        weak_ptr1 = obj->getWeakPtr();

        weak_ptr2 = std::move(weak_ptr1);
        EXPECT_TRUE(weak_ptr1.expired());
        EXPECT_FALSE(weak_ptr2.expired());
        EXPECT_EQ(weak_ptr2.get(), raw_ptr);
    }
    EXPECT_TRUE(weak_ptr2.expired());
}

TEST(WeakPtrTest, Reset) {
    hcomm::WeakPtr<TrackedObject> weak_ptr;
    {
        auto obj = std::make_unique<TrackedObject>();
        weak_ptr = obj->getWeakPtr();
        EXPECT_FALSE(weak_ptr.expired());

        weak_ptr.reset();
        EXPECT_TRUE(weak_ptr.expired());
        EXPECT_EQ(weak_ptr.get(), nullptr);
    }
}

TEST(WeakPtrTest, Swap) {
    hcomm::WeakPtr<TrackedObject> weak_ptr1;
    hcomm::WeakPtr<TrackedObject> weak_ptr2;
    TrackedObject* raw_ptr1 = nullptr;

    {
        auto obj1 = std::make_unique<TrackedObject>();
        raw_ptr1 = obj1.get();
        weak_ptr1 = obj1->getWeakPtr();

        EXPECT_FALSE(weak_ptr1.expired());
        EXPECT_TRUE(weak_ptr2.expired());

        weak_ptr1.swap(weak_ptr2);

        EXPECT_TRUE(weak_ptr1.expired());
        EXPECT_FALSE(weak_ptr2.expired());
        EXPECT_EQ(weak_ptr2.get(), raw_ptr1);

        swap(weak_ptr1, weak_ptr2);

        EXPECT_FALSE(weak_ptr1.expired());
        EXPECT_TRUE(weak_ptr2.expired());
        EXPECT_EQ(weak_ptr1.get(), raw_ptr1);
    }
}

TEST(WeakPtrTest, NullChecks) {
    hcomm::WeakPtr<TrackedObject> weak_ptr;
    EXPECT_FALSE(weak_ptr);
    EXPECT_EQ(weak_ptr, nullptr);
    EXPECT_TRUE(weak_ptr == nullptr);
    EXPECT_FALSE(weak_ptr != nullptr);

    auto obj = std::make_unique<TrackedObject>();
    weak_ptr = obj->getWeakPtr();

    EXPECT_TRUE(weak_ptr);
    EXPECT_NE(weak_ptr, nullptr);
    EXPECT_TRUE(weak_ptr != nullptr);
    EXPECT_FALSE(weak_ptr == nullptr);
}

TEST(WeakPtrTest, Equality) {
    hcomm::WeakPtr<TrackedObject> weak_ptr1;
    hcomm::WeakPtr<TrackedObject> weak_ptr2;
    hcomm::WeakPtr<TrackedObject> weak_ptr3;

    {
        auto obj1 = std::make_unique<TrackedObject>();
        auto obj2 = std::make_unique<TrackedObject>();
        weak_ptr1 = obj1->getWeakPtr();
        weak_ptr2 = weak_ptr1;
        weak_ptr3 = obj2->getWeakPtr();

        EXPECT_TRUE(weak_ptr1 == weak_ptr2);
        EXPECT_FALSE(weak_ptr1 != weak_ptr2);

        EXPECT_FALSE(weak_ptr1 == weak_ptr3);
        EXPECT_TRUE(weak_ptr1 != weak_ptr3);
    }
}

TEST(WeakPtrTest, Accessors) {
    hcomm::WeakPtr<TrackedObject> weak_ptr;
    {
        auto obj = std::make_unique<TrackedObject>();
        weak_ptr = obj->getWeakPtr();

        ASSERT_NE(weak_ptr.get(), nullptr);
        weak_ptr->doSomething();
        (*weak_ptr).doSomething();
    }
    ASSERT_EQ(weak_ptr.get(), nullptr);
}

class Base {
public:
    virtual ~Base() = default;
    hcomm::WeakPtr<Base> getWeakPtr() {
        return weak_ptr_factory_.weak_from_this();
    }

private:
    hcomm::WeakPtrFactory<Base> weak_ptr_factory_{this};
};

class Derived : public Base {
public:
    hcomm::WeakPtr<Derived> getDerivedWeakPtr() {
        return derived_weak_ptr_factory_.weak_from_this();
    }

private:
    hcomm::WeakPtrFactory<Derived> derived_weak_ptr_factory_{this};
};

TEST(WeakPtrTest, PolymorphicConversion) {
    hcomm::WeakPtr<Derived> derived_weak;
    Derived* raw_ptr = nullptr;

    {
        auto derived_obj = std::make_unique<Derived>();
        raw_ptr = derived_obj.get();
        derived_weak = derived_obj->getDerivedWeakPtr();

        // Copy construction
        hcomm::WeakPtr<Base> base_weak_copy(derived_weak);
        EXPECT_EQ(base_weak_copy.get(), raw_ptr);

        // Move construction
        hcomm::WeakPtr<Base> base_weak_move(std::move(derived_weak));
        EXPECT_EQ(base_weak_move.get(), raw_ptr);
        EXPECT_TRUE(derived_weak.expired()); // NOLINT

        // Re-arm derived_weak for assignment tests
        derived_weak = derived_obj->getDerivedWeakPtr();

        // Copy assignment
        hcomm::WeakPtr<Base> base_assign_copy;
        base_assign_copy = derived_weak;
        EXPECT_EQ(base_assign_copy.get(), raw_ptr);

        // Move assignment
        hcomm::WeakPtr<Base> base_assign_move;
        base_assign_move = std::move(derived_weak);
        EXPECT_EQ(base_assign_move.get(), raw_ptr);
        EXPECT_TRUE(derived_weak.expired());
    }
}

} // namespace
