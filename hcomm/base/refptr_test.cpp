// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/base/refptr.hpp"

#include <gtest/gtest.h>

namespace {
class TestObject : public hcomm::RefCounted<TestObject> {
public:
    explicit TestObject(int* counter = nullptr) : counter_(counter) {
        if (counter_) {
            (*counter_)++;
        }
    }

    ~TestObject() {
        if (counter_) {
            (*counter_)--;
        }
    }

    void do_something() const {}

private:
    int* counter_;
};

TEST(RefPtrTest, Construct) {
    int counter = 0;
    {
        hcomm::RefPtr<TestObject> ptr(new TestObject(&counter));
        EXPECT_EQ(counter, 1);
        EXPECT_EQ(ptr->use_count(), 1);
    }
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, CopyConstruct) {
    int counter = 0;
    {
        hcomm::RefPtr<TestObject> ptr1(new TestObject(&counter));
        hcomm::RefPtr<TestObject> ptr2 = ptr1;
        EXPECT_EQ(counter, 1);
        EXPECT_EQ(ptr1->use_count(), 2);
        EXPECT_EQ(ptr2->use_count(), 2);
        EXPECT_EQ(ptr1.get(), ptr2.get());
    }
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, MoveConstruct) {
    int counter = 0;
    {
        hcomm::RefPtr<TestObject> ptr1(new TestObject(&counter));
        hcomm::RefPtr<TestObject> ptr2 = std::move(ptr1);
        EXPECT_EQ(counter, 1);
        EXPECT_EQ(ptr2->use_count(), 1);
        EXPECT_EQ(ptr1.get(), nullptr);
    }
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, CopyAssign) {
    int counter = 0;
    {
        hcomm::RefPtr<TestObject> ptr1(new TestObject(&counter));
        hcomm::RefPtr<TestObject> ptr2;
        ptr2 = ptr1;
        EXPECT_EQ(counter, 1);
        EXPECT_EQ(ptr1->use_count(), 2);
        EXPECT_EQ(ptr2->use_count(), 2);
    }
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, MoveAssign) {
    int counter = 0;
    {
        hcomm::RefPtr<TestObject> ptr1(new TestObject(&counter));
        hcomm::RefPtr<TestObject> ptr2;
        ptr2 = std::move(ptr1);
        EXPECT_EQ(counter, 1);
        EXPECT_EQ(ptr2->use_count(), 1);
        EXPECT_EQ(ptr1.get(), nullptr);
    }
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, Reset) {
    int counter = 0;
    hcomm::RefPtr<TestObject> ptr(new TestObject(&counter));
    EXPECT_EQ(counter, 1);
    ptr.reset();
    EXPECT_EQ(counter, 0);
    EXPECT_EQ(ptr.get(), nullptr);

    // Double reset check
    ptr.reset();
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, Detach) {
    int counter = 0;
    TestObject* raw = nullptr;
    {
        hcomm::RefPtr<TestObject> ptr(new TestObject(&counter));
        raw = ptr.detach();
        EXPECT_EQ(ptr.get(), nullptr);
        EXPECT_EQ(raw->use_count(), 1); // Still referenced
    }
    EXPECT_EQ(counter, 1); // Not deleted
    raw->unref();          // Manual cleanup
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, Adoption) {
    int counter = 0;
    TestObject* raw = new TestObject(&counter);
    raw->ref(); // Manual ref
    EXPECT_EQ(raw->use_count(), 1);

    {
        // Adopt the reference
        hcomm::RefPtr<TestObject> ptr(raw, hcomm::kAdoptRef);
        EXPECT_EQ(ptr->use_count(), 1);
    }
    EXPECT_EQ(counter, 0);
}

TEST(RefPtrTest, Swap) {
    int counter1 = 0;
    int counter2 = 0;
    {
        hcomm::RefPtr<TestObject> ptr1(new TestObject(&counter1));
        hcomm::RefPtr<TestObject> ptr2(new TestObject(&counter2));
        auto* raw1 = ptr1.get();
        auto* raw2 = ptr2.get();

        ptr1.swap(ptr2);
        EXPECT_EQ(ptr1.get(), raw2);
        EXPECT_EQ(ptr2.get(), raw1);

        hcomm::swap(ptr1, ptr2);
        EXPECT_EQ(ptr1.get(), raw1);
        EXPECT_EQ(ptr2.get(), raw2);
    }
}

TEST(RefPtrTest, NullChecks) {
    hcomm::RefPtr<TestObject> ptr;
    EXPECT_FALSE(ptr);
    EXPECT_EQ(ptr, nullptr);
    EXPECT_TRUE(ptr == nullptr);
    EXPECT_FALSE(ptr != nullptr);

    ptr.reset(new TestObject());
    EXPECT_TRUE(ptr);
    EXPECT_NE(ptr, nullptr);
    EXPECT_TRUE(ptr != nullptr);
    EXPECT_FALSE(ptr == nullptr);
}

TEST(RefPtrTest, Equality) {
    auto obj = new TestObject();
    hcomm::RefPtr<TestObject> ptr1(obj);
    hcomm::RefPtr<TestObject> ptr2(ptr1);
    hcomm::RefPtr<TestObject> ptr3(new TestObject());

    EXPECT_TRUE(ptr1 == ptr2);
    EXPECT_FALSE(ptr1 != ptr2);

    EXPECT_FALSE(ptr1 == ptr3);
    EXPECT_TRUE(ptr1 != ptr3);

    EXPECT_TRUE(ptr1 == obj);
    EXPECT_FALSE(ptr1 != obj);

    EXPECT_TRUE(obj == ptr1);
    EXPECT_FALSE(obj != ptr1);
}

TEST(RefPtrTest, Accessors) {
    auto obj = new TestObject();
    hcomm::RefPtr<TestObject> ptr(obj);
    ptr->do_something();
    (*ptr).do_something();

    const hcomm::RefPtr<TestObject> cptr(ptr);
    cptr->do_something();
    (*cptr).do_something();
}

TEST(RefPtrTest, NullptrCtor) {
    hcomm::RefPtr<TestObject> ptr(nullptr);
    EXPECT_EQ(ptr.get(), nullptr);
}

TEST(RefPtrTest, AdoptStatic) {
    int counter = 0;
    TestObject* raw = new TestObject(&counter);
    raw->ref(); // Manually ref to simulate an existing reference
    EXPECT_EQ(raw->use_count(), 1);

    {
        // Adopt the reference using the static method
        auto ptr = hcomm::RefPtr<TestObject>::adopt(raw);
        EXPECT_EQ(ptr->use_count(), 1); // Should not increment ref count
    }
    // ptr goes out of scope, unref() is called, object should be deleted
    EXPECT_EQ(counter, 0);
}

} // namespace
