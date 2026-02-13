// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/memory/paged_resource_pool.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {
class PagedResourcePoolTest : public ::testing::Test {
protected:
    struct TestObject {
        static std::atomic<int> constructor_calls;
        static std::atomic<int> destructor_calls;

        static void reset_counters() {
            constructor_calls = 0;
            destructor_calls = 0;
        }

        int value;

        TestObject() : value(0) {
            constructor_calls++;
        }

        explicit TestObject(int v) : value(v) {
            constructor_calls++;
        }

        ~TestObject() {
            destructor_calls++;
        }
    };
};

std::atomic<int> PagedResourcePoolTest::TestObject::constructor_calls{0};
std::atomic<int> PagedResourcePoolTest::TestObject::destructor_calls{0};

/// Tests basic allocation, retrieval, and deallocation of a simple POD type.
TEST_F(PagedResourcePoolTest, BasicAllocFree) {
    hcomm::PagedResourcePool<int> pool;

    // Allocate a resource
    auto allocation = pool.alloc(42);
    ASSERT_TRUE(allocation.has_value());
    EXPECT_EQ(*allocation->resource, 42);

    // Get the resource using its ID
    int* ptr = pool.get(allocation->id);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 42);

    // Free the resource
    pool.free(allocation->id);

    // After freeing, get should fail
    ptr = pool.get(allocation->id);
    EXPECT_EQ(ptr, nullptr);
}

/// Tests that constructors and destructors of non-trivial types are called correctly.
TEST_F(PagedResourcePoolTest, ConstructorDestructor) {
    TestObject::reset_counters();
    {
        hcomm::PagedResourcePool<TestObject> pool;
        EXPECT_EQ(TestObject::constructor_calls, 0);
        EXPECT_EQ(TestObject::destructor_calls, 0);

        // Allocate a resource
        auto allocation = pool.alloc(123);
        ASSERT_TRUE(allocation.has_value());
        EXPECT_EQ(allocation->resource->value, 123);
        EXPECT_EQ(TestObject::constructor_calls, 1);
        EXPECT_EQ(TestObject::destructor_calls, 0);

        // Free the resource
        pool.free(allocation->id);
        EXPECT_EQ(TestObject::constructor_calls, 1);
        EXPECT_EQ(TestObject::destructor_calls, 1);
    }
    // Pool is destroyed, but no more destructors should be called as the object was already freed.
    EXPECT_EQ(TestObject::destructor_calls, 1);
}

/// A custom configuration with small blocks to test expansion easily.
template <typename T>
struct SmallBlockConfig {
    static constexpr std::size_t kSlotsPerBlock = 4;
    static constexpr std::size_t kMaxBlocks = 4;
};

/// Tests that the pool can expand by allocating new blocks when it runs out of free slots.
TEST_F(PagedResourcePoolTest, PoolExpansion) {
    using Pool = hcomm::PagedResourcePool<int, hcomm::DefaultAllocator, SmallBlockConfig<int>>;
    Pool pool;

    std::vector<Pool::Allocation> allocations;
    // kSlotsPerBlock is 4, so the 5th allocation should trigger an expansion.
    constexpr std::size_t total_allocs = SmallBlockConfig<int>::kSlotsPerBlock + 1;

    for (std::size_t i = 0; i < total_allocs; ++i) {
        auto allocation = pool.alloc(i);
        ASSERT_TRUE(allocation.has_value()) << "Allocation failed at step " << i;
        EXPECT_EQ(*allocation->resource, i);
        allocations.push_back(*allocation);
    }

    // Verify all allocated resources are accessible
    for (std::size_t i = 0; i < total_allocs; ++i) {
        int* ptr = pool.get(allocations[i].id);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(*ptr, i);
    }

    // Let pool deallocate the five slots on destruction. No need to call dtor for POD-types.
}

/// Tests that the pool respects its maximum capacity.
TEST_F(PagedResourcePoolTest, PoolCapacity) {
    using Pool = hcomm::PagedResourcePool<int, hcomm::DefaultAllocator, SmallBlockConfig<int>>;
    Pool pool;

    constexpr size_t max_capacity = SmallBlockConfig<int>::kSlotsPerBlock * SmallBlockConfig<int>::kMaxBlocks;
    std::vector<Pool::Allocation> allocations;

    // Allocate until the pool is full
    for (size_t i = 0; i < max_capacity; ++i) {
        auto alloc = pool.alloc(i);
        ASSERT_TRUE(alloc.has_value()) << "Allocation failed at step " << i;
        allocations.push_back(*alloc);
    }

    // The next allocation should fail
    auto over_alloc = pool.alloc(999);
    EXPECT_FALSE(over_alloc.has_value());

    // Free one resource
    pool.free(allocations.back().id);
    allocations.pop_back();

    // Now allocation should succeed again
    auto after_free_alloc = pool.alloc(888);
    EXPECT_TRUE(after_free_alloc.has_value());
    EXPECT_EQ(*after_free_alloc->resource, 888);
}

/// Tests the versioning mechanism of ResourceId to prevent the ABA problem.
TEST_F(PagedResourcePoolTest, VersioningPreventsStaleAccess) {
    hcomm::PagedResourcePool<int> pool;

    // 1. Allocate a resource and save its ID.
    auto alloc1 = pool.alloc(100);
    ASSERT_TRUE(alloc1.has_value());
    hcomm::ResourceId id1 = alloc1->id;
    EXPECT_EQ(*pool.get(id1), 100);

    // 2. Free the resource. The slot is now free, and its version is incremented.
    pool.free(id1);
    EXPECT_EQ(pool.get(id1), nullptr); // Cannot access via old ID.

    // 3. Allocate a new resource. The pool might reuse the same slot.
    auto alloc2 = pool.alloc(200);
    ASSERT_TRUE(alloc2.has_value());
    hcomm::ResourceId id2 = alloc2->id;

    // If the same slot was reused, indices will match but versions won't.
    if (id1.index() == id2.index()) {
        EXPECT_NE(id1.version(), id2.version());
    }

    // The old, stale ID should NOT be able to access the new resource.
    EXPECT_EQ(pool.get(id1), nullptr);
    // The new ID can access the new resource.
    EXPECT_EQ(*pool.get(id2), 200);
}

/// Concurrent Test: Multiple threads allocating and freeing resources.
TEST_F(PagedResourcePoolTest, ConcurrentAllocFree) {
    using Pool = hcomm::PagedResourcePool<TestObject, hcomm::DefaultAllocator, SmallBlockConfig<TestObject>>;
    Pool pool;
    TestObject::reset_counters();

    constexpr int num_threads = 8;
    constexpr int allocs_per_thread = 10000;
    std::atomic<int> successful_allocs{0};

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            std::vector<Pool::Allocation> allocations;
            allocations.reserve(allocs_per_thread);
            for (int j = 0; j < allocs_per_thread; ++j) {
                // Mix of alloc and free to create contention
                if (j % 4 == 0 && !allocations.empty()) {
                    pool.free(allocations.back().id);
                    allocations.pop_back();
                } else {
                    if (auto alloc = pool.alloc(j)) {
                        ++successful_allocs;
                        allocations.push_back(*alloc);
                    }
                }
            }

            // Free remaining resources
            for (const auto& alloc : allocations) {
                pool.free(alloc.id);
            }
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // After all threads are done and all objects are freed, the number of
    // constructor calls must equal the number of destructor calls.
    EXPECT_EQ(TestObject::constructor_calls.load(), successful_allocs.load());
    EXPECT_EQ(TestObject::constructor_calls.load(), TestObject::destructor_calls.load());
}

struct ConditionallyThrowable {
    static std::atomic<bool> should_throw;
    static std::atomic<int> constructor_calls;

    static void reset() {
        should_throw = false;
        constructor_calls = 0;
    }

    ConditionallyThrowable() {
        constructor_calls++;
        if (should_throw.load()) {
            throw std::runtime_error("constructor failed as requested");
        }
    }
};

std::atomic<bool> ConditionallyThrowable::should_throw{false};
std::atomic<int> ConditionallyThrowable::constructor_calls{0};

/// Tests the rollback mechanism when a resource's constructor throws during allocation.
TEST_F(PagedResourcePoolTest, AllocRollsBackOnConstructorException) {
    ConditionallyThrowable::reset();
    hcomm::PagedResourcePool<ConditionallyThrowable> pool;

    // 1. Configure the object to throw on construction.
    ConditionallyThrowable::should_throw = true;

    // 2. Attempt to allocate, expecting an exception.
    EXPECT_THROW(pool.alloc(), std::runtime_error);

    // The constructor was called once and it failed.
    EXPECT_EQ(ConditionallyThrowable::constructor_calls, 1);

    // 3. The slot should have been returned to the pool.
    //    Configure the object to NOT throw and try allocating again.
    ConditionallyThrowable::should_throw = false;
    auto allocation = pool.alloc();

    // 4. The allocation should now succeed.
    ASSERT_TRUE(allocation.has_value());
    EXPECT_EQ(ConditionallyThrowable::constructor_calls, 2); // One failed, one succeeded.

    // 5. The retrieved object should be valid.
    auto* ptr = pool.get(allocation->id);
    EXPECT_NE(ptr, nullptr);

    // Freeing it should work as normal.
    pool.free(allocation->id);
    EXPECT_EQ(pool.get(allocation->id), nullptr);
}

/// Tests the valid() method for resource ID validation.
TEST_F(PagedResourcePoolTest, ValidatesResourceId) {
    hcomm::PagedResourcePool<int> pool;

    // Allocate a resource and check its validity.
    auto alloc1 = pool.alloc(10);
    ASSERT_TRUE(alloc1.has_value());
    hcomm::ResourceId id1 = alloc1->id;
    EXPECT_TRUE(pool.valid(id1));

    // Free the resource. The old ID should now be invalid.
    pool.free(id1);
    EXPECT_FALSE(pool.valid(id1));

    // Allocate a new resource, possibly reusing the same slot.
    auto alloc2 = pool.alloc(20);
    ASSERT_TRUE(alloc2.has_value());
    hcomm::ResourceId id2 = alloc2->id;

    // The new ID should be valid.
    EXPECT_TRUE(pool.valid(id2));
    // The old ID should still be invalid, even if the index is the same.
    EXPECT_FALSE(pool.valid(id1));

    // Test with an out-of-bounds index (should always be invalid).
    hcomm::ResourceId invalid_id(100000, 1); // Large index, arbitrary version
    EXPECT_FALSE(pool.valid(invalid_id));
}

} // namespace
