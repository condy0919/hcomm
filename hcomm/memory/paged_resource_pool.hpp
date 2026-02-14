// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_MEMORY_PAGED_RESOURCE_POOL_HPP_
#define HCOMM_MEMORY_PAGED_RESOURCE_POOL_HPP_

#include <atomic>
#include <concepts>
#include <cstdint>
#include <mutex>
#include <new>
#include <optional>
#include <type_traits>

#include "hcomm/base/optimization.hpp"

namespace hcomm {
namespace internal {
/// A 64-bit value combining an index and a tag, used for the lock-free list head.
///
/// This is similar to `ResourceId`, but has a default constructor to allow it to be a field in a class (e.g.,
/// `PagedResourcePool::free_head_`). The tag is crucial for solving the ABA problem in the lock-free stack
/// implementation of the free list.
class TaggedIndex {
public:
    TaggedIndex() = default;

    TaggedIndex(std::uint32_t idx, std::uint32_t tag) : value_((static_cast<std::uint64_t>(tag) << 32) | idx) {}

    /// Returns the 32-bit index.
    std::uint32_t index() const {
        return static_cast<std::uint32_t>(value_ & 0xffffffff);
    }

    /// Returns the 32-bit tag (or version).
    std::uint32_t tag() const {
        return static_cast<std::uint32_t>(value_ >> 32);
    }

    explicit operator std::uint64_t() const {
        return value_;
    }

private:
    std::uint64_t value_ = 0;
};

static_assert(std::atomic<internal::TaggedIndex>::is_always_lock_free,
              "Atomic operations on TaggedIndex must be lock-free for performance.");

} // namespace internal

/// A versioned, opaque handle to a resource within a `PagedResourcePool`.
///
/// The resource pool is slot-based. A `ResourceId` combines a slot's 32-bit index with a 32-bit version number. The
/// version is incremented each time a resource is freed, which effectively invalidates old IDs for that slot. This
/// prevents the ABA problem, where an old, invalid ID could accidentally access a new resource that happens to be
/// allocated in the same slot.
class ResourceId {
public:
    /// Constructs a ResourceId from its raw parts.
    ///
    /// Users of the pool should not need to construct this directly.
    ResourceId(std::uint32_t idx, std::uint32_t ver) : value_((static_cast<std::uint64_t>(ver) << 32) | idx) {}

    /// Returns the 32-bit index of the slot in the pool.
    std::uint32_t index() const {
        return static_cast<std::uint32_t>(value_ & 0xffffffff);
    }

    /// Returns the 32-bit version of the resource slot.
    std::uint32_t version() const {
        return static_cast<std::uint32_t>(value_ >> 32);
    }

    explicit operator std::uint64_t() const {
        return value_;
    }

private:
    std::uint64_t value_;
};

/// Concept defining the requirements for a custom memory allocation policy.
///
/// A policy allows the `PagedResourcePool` to use different backend allocators, for example, to allocate from a
/// specific NUMA node or a pre-registered memory region for RDMA/UB transports.
template <typename T>
concept PagedAllocatorPolicy = requires(std::size_t sz, void* ptr) {
    /// The policy can define a `Metadata` struct. This metadata will be stored once per memory block and can be used
    /// for storing information like RDMA/UB memory region handles.
    typename T::Metadata;

    /// A static method to allocate a block of memory of a given size.
    { T::allocate(sz) } -> std::convertible_to<void*>;

    /// A static method to deallocate a block of memory.
    { T::deallocate(ptr) } -> std::same_as<void>;
};

/// A default allocation policy that uses global `::operator new` and `::operator delete`.
struct DefaultAllocator {
    /// The default metadata is an empty struct.
    struct Metadata {};

    static void* allocate(std::size_t sz) {
        return ::operator new(sz);
    }

    static void deallocate(void* ptr) {
        ::operator delete(ptr);
    }
};

/// Defines the geometry and limits of the `PagedResourcePool`.
template <typename T>
struct PagedResourcePoolConfig {
    /// The number of resource slots to allocate in a single memory block.
    /// This value should be chosen to balance memory usage and allocation overhead.
    static constexpr std::size_t kSlotsPerBlock = 256;

    /// The absolute maximum number of blocks the pool can expand to.
    /// The total capacity of the pool is `kSlotsPerBlock * kMaxBlocks`.
    static constexpr std::size_t kMaxBlocks = 256;
};

/// A high-performance, thread-safe, paged resource pool for managing objects of type `T`.
///
/// This class is designed for extreme performance. It provides lock-free `alloc`, `free`, and `get` operations for
/// hot paths. Memory expansion is handled on a less-frequently-used "cold path" that uses a mutex to ensure thread
/// safety.
///
/// This pool manages object lifetime explicitly. It constructs objects with placement new in `alloc` and calls
/// destructors in `free`. This avoids constructing objects until they are actually allocated, making it suitable for
/// both POD and complex non-POD types.
///
/// The pool's behavior can be customized via the `AllocPolicy` and `Config` template parameters. `AllocPolicy` defines
/// how memory blocks are allocated, while `Config` defines the pool's geometry, such as slots per block.
///
/// NOTE: For correctness, a `ResourceId` obtained from `alloc()` in one thread must be `free()`d within the *same
/// thread*. This design does not protect against data races that could arise from freeing a resource from a different
/// thread.
template <typename T, PagedAllocatorPolicy AllocPolicy = DefaultAllocator, typename Config = PagedResourcePoolConfig<T>>
class PagedResourcePool final {
    static constexpr std::size_t kSlotsPerBlock = Config::kSlotsPerBlock;
    static constexpr std::size_t kMaxBlocks = Config::kMaxBlocks;

public:
    using Metadata = typename AllocPolicy::Metadata;

    /// Represents a successful allocation containing the handle and a pointer.
    struct Allocation {
        /// The opaque, versioned handle to the allocated resource.
        ResourceId id;
        /// A pointer to the newly constructed resource.
        T* resource;
    };

    /// Constructs a new, empty resource pool.
    PagedResourcePool() : num_blocks_(0) {
        // The free list is a lock-free stack, identified by its head. The head is a `TaggedIndex` to prevent the ABA
        // problem. It starts as index -1 (empty) with tag 0.
        free_head_.store(internal::TaggedIndex(static_cast<std::uint32_t>(-1), 0));
    }

    PagedResourcePool(PagedResourcePool&& rhs) noexcept = delete;
    PagedResourcePool& operator=(PagedResourcePool&& rhs) noexcept = delete;

    /// Destroys the resource pool and deallocates all memory blocks.
    ///
    /// NOTE: This does NOT call destructors on any active (in-use) resources. It is the user's responsibility to
    /// ensure all resources are returned to the pool via `free()` before the pool is destroyed to avoid resource
    /// leaks.
    ~PagedResourcePool() {
        for (std::size_t i = 0; i < num_blocks_; ++i) {
            // Use relaxed memory order because this is the destructor. No other thread can be concurrently accessing
            // the pool, so no synchronization is needed.
            if (Block* blk = block_ptrs_[i].load(std::memory_order_relaxed)) {
                AllocPolicy::deallocate(blk);
            }
        }
    }

    /// Allocates and constructs a resource from the pool.
    ///
    /// This method is lock-free and thread-safe. It forwards any provided arguments to the constructor of `T`. If the
    /// pool is empty, it will attempt to expand internally. On success, it returns an `std::optional` containing an
    /// `Allocation` struct (which holds the resource ID and a pointer). If the pool has reached its maximum capacity,
    /// it returns `std::nullopt`.
    template <typename... Args>
    std::optional<Allocation> alloc(Args&&... args) {
        while (true) {
            // Acquire semantics are needed here to synchronize with the `release` operation in `free()` or `expand()`.
            // This ensures that if we read the head of the free list, we also see all the memory writes that occurred
            // before the corresponding slot was freed or the new block was added.
            auto current_head = free_head_.load(std::memory_order_acquire);

            const std::uint32_t head_idx = current_head.index();
            if (head_idx == static_cast<std::uint32_t>(-1)) {
                // The free list is empty. Try to expand the pool on the cold path. If another thread expands it first,
                // our next loop iteration will succeed. If expansion fails (e.g., pool at max capacity), `expand()`
                // returns false.
                if (!expand()) {
                    return std::nullopt;
                }
                continue; // Retry allocation.
            }

            Slot* slot = getSlotByIndex(head_idx);
            if (slot == nullptr) [[unlikely]] {
                // This should not happen in a correct implementation, but as a safeguard, we handle the case where an
                // index points to an unallocated block.
                return std::nullopt;
            }

            // Prepare the new head for the free list. Relaxed memory order is sufficient here because we have
            // effectively acquired ownership of this slot by popping it from the `free_head_` stack. No other thread
            // should be accessing `next_free_idx` of this slot. Its value was set before the slot was pushed to the
            // free list, and its visibility is guaranteed by the acquire on `free_head_`.
            const std::uint32_t next_idx = slot->next_free_idx.load(std::memory_order_relaxed);
            const std::uint32_t next_tag = current_head.tag() + 1;
            internal::TaggedIndex new_head(next_idx, next_tag);

            // Atomically swap the head of the free list.
            //
            // Success memory order: `acquire`. If the CAS succeeds, we have acquired the slot. Acquire semantics are
            // needed to create a synchronization point with the `release` in `free()` or `expand()`. This prevents
            // reordering of this operation with the following construction of the new object.
            //
            // Failure memory order: `relaxed`. If the CAS fails, no memory was modified. We are just going to loop
            // again and reload `free_head_` with acquire semantics. No special ordering is needed on failure.
            if (free_head_.compare_exchange_weak(current_head, new_head, std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
                // Success! We now have exclusive access to this slot. A relaxed load is sufficient for the version, as
                // no other thread can modify it while we hold ownership.
                std::uint32_t ver = slot->version.load(std::memory_order_relaxed);

                try {
                    T* resource = new (&slot->storage) T(std::forward<Args>(args)...);
                    return Allocation{ResourceId(head_idx, ver), resource};
                } catch (...) {
                    // If construction fails, we must roll back the allocation to prevent leaking the slot. The slot is
                    // returned to the head of the free list. The slot version is NOT incremented, as the resource was
                    // never successfully given to the user.
                    while (true) {
                        auto current_head_for_rollback = free_head_.load(std::memory_order_relaxed);
                        slot->next_free_idx.store(current_head_for_rollback.index(), std::memory_order_relaxed);
                        internal::TaggedIndex new_head_for_rollback(head_idx, current_head_for_rollback.tag() + 1);
                        if (free_head_.compare_exchange_weak(current_head_for_rollback, new_head_for_rollback,
                                                             std::memory_order_release, std::memory_order_relaxed)) {
                            break;
                        }
                    }

                    // Re-throw the exception to the caller.
                    throw;
                }
            }
        }
    }

    /// Returns a resource to the pool, identified by its `ResourceId`.
    ///
    /// This method is lock-free and thread-safe. It validates the `ResourceId` to prevent double-frees or freeing of
    /// invalid handles. For non-trivial types, it calls the destructor on the object before making the slot available
    /// for reallocation.
    void free(ResourceId id) {
        Slot* slot = getSlotByIndex(id.index());
        if (!slot) {
            return; // Invalid index.
        }

        // This relaxed load is a fast pre-check to reject an obviously stale ID. If the version doesn't match, the
        // resource has likely already been freed. The user is responsible for not freeing the same ID from multiple
        // threads concurrently. The critical synchronization happens later.
        const std::uint32_t ver = slot->version.load(std::memory_order_relaxed);
        if (ver != id.version()) [[unlikely]] {
            return; // Stale ID, likely a double-free.
        }

        if constexpr (!std::is_trivially_destructible_v<T>) {
            std::destroy_at(addr(slot));
        }

        // Increment the slot's version. This invalidates all old `ResourceId` handles pointing to this slot. A relaxed
        // order is sufficient because the subsequent release-CAS on `free_head_` will publish this change.
        slot->version.fetch_add(1, std::memory_order_relaxed);

        // Atomically push the freed slot back onto the `free_head_` stack.
        while (true) {
            // Relaxed load is fine within the CAS loop. The actual synchronization happens in the CAS itself.
            auto current_head = free_head_.load(std::memory_order_relaxed);

            const std::uint32_t head_idx = current_head.index();
            // We have exclusive access to this slot (as per usage contract), so we can prepare its `next_free_idx` with
            // relaxed ordering. Its visibility to other threads will be guaranteed by the `release` CAS on
            // `free_head_`.
            slot->next_free_idx.store(head_idx, std::memory_order_relaxed);

            const std::uint32_t next_tag = current_head.tag() + 1;
            const internal::TaggedIndex new_head(id.index(), next_tag);
            // Atomically push the freed slot onto the `free_head_` stack.
            //
            // Success memory order: `release`. This makes our prior writes to the slot (version, next_free_idx) visible
            // to other threads that will `alloc` this slot. This is the "publish" operation.
            //
            // Failure memory order: `relaxed`. If the CAS fails, we just loop again. No memory ordering is needed.
            if (free_head_.compare_exchange_weak(current_head, new_head, std::memory_order_release,
                                                 std::memory_order_relaxed)) {
                break;
            }
        }
    }

    /// Retrieves a pointer to a resource using its `ResourceId`.
    ///
    /// This is the fastest way to access a resource. The operation is wait-free and thread-safe. It performs version
    /// checking to ensure the ID is still valid, returning a pointer to the resource on success, or `nullptr` if the
    /// ID is invalid.
    T* get(ResourceId id) {
        Slot* slot = getSlotByIndex(id.index());
        if (!slot) {
            return nullptr;
        }

        // Check the version with `acquire` semantics. This synchronizes with the `release` `fetch_add` in `free()`.
        // If the version matches, `acquire` ensures that we see the fully constructed state of the object `T` and not
        // a partially destroyed or re-allocated state. If the version does not match, it means the resource was freed
        // and potentially re-allocated, so the handle is invalid.
        if (slot->version.load(std::memory_order_acquire) != id.version()) {
            return nullptr;
        }
        return addr(slot);
    }

    /// Checks if a resource ID is still valid.
    ///
    /// This is a thread-safe way to check if a handle is still valid without retrieving the pointer to the resource.
    /// It returns `true` if the ID's version matches the slot's current version, `false` otherwise. This is useful
    /// for scenarios where you only need to validate a handle's liveness.
    bool valid(ResourceId id) {
        Slot* slot = getSlotByIndex(id.index());
        if (!slot) {
            return false;
        }

        // Acquire semantics are used for the same reason as in `get()`.
        return slot->version.load(std::memory_order_acquire) == id.version();
    }

    /// Retrieves the allocator-specific metadata for the block containing a given resource.
    ///
    /// This is primarily useful for special allocators, such as an RDMA/UB policy that stores memory registration info
    /// in the block's `Metadata`. It returns a pointer to the `Metadata` for the containing block, or `nullptr` if the
    /// resource ID is invalid.
    const Metadata* getMetadata(ResourceId id) {
        const std::uint32_t block_idx = id.index() / kSlotsPerBlock;
        if (block_idx >= kMaxBlocks) {
            return nullptr;
        }

        // Acquire load synchronizes with the release store on `block_ptrs_` in `expand()`, ensuring we see a fully
        // initialized `Block`.
        Block* blk = block_ptrs_[block_idx].load(std::memory_order_acquire);
        if (!blk) {
            return nullptr;
        }

        return &blk->meta;
    }

private:
    /// A Slot represents a single potential location for a resource. It is aligned to a cache line boundary to prevent
    /// false sharing between adjacent slots.
    struct alignas(HCOMM_CACHELINE_SIZE) Slot {
        /// Aligned storage for a single `T` object. `T` is only constructed here via placement new during `alloc`.
        alignas(T) unsigned char storage[sizeof(T)];

        /// The version of the slot, incremented on each `free` operation.
        ///
        /// The initial value is 1, not 0. This is a defensive measure to prevent a zero-initialized `ResourceId`
        /// (e.g., from `memset`) from accidentally matching and appearing valid for the 0th slot, which would have
        /// version 0 if we were not careful.
        std::atomic<std::uint32_t> version{1};

        /// The index of the next free slot in the lock-free stack.
        std::atomic<std::uint32_t> next_free_idx{static_cast<std::uint32_t>(-1)};
    };

    /// A Block is a contiguous chunk of memory containing a fixed number of Slots.
    struct Block {
        /// Optional metadata provided by the allocator policy (e.g., RDMA/UB memory handle).
        [[no_unique_address]] Metadata meta;

        /// The array of slots within this block.
        Slot slots[kSlotsPerBlock];
    };

    /// Helper to convert a pointer to a slot's storage into a T*.
    T* addr(Slot* slot) {
        return reinterpret_cast<T*>(&slot->storage);
    }

    /// Fixed-size array of pointers to the allocated memory blocks.
    std::atomic<Block*> block_ptrs_[kMaxBlocks] = {nullptr};

    /// The current number of allocated and published blocks.
    std::atomic<std::uint32_t> num_blocks_;

    /// The head of the lock-free stack of free slots, represented as a tagged index to prevent the ABA problem.
    ///
    /// What is the ABA problem?
    ///
    /// In short, during a read-modify-write operation on a shared value, if another thread modifies the value and then
    /// restores it to the original state before the first thread completes its operation, the first thread will be
    /// unaware of the intermediate change. This can lead to data structure corruption.
    ///
    /// Let's illustrate this with an example using the `alloc` operation in our resource pool.
    ///
    /// Assume we have a `free_head_` that only stores the index, and the current free list is A -> B -> C (where
    /// `free_head_` points to A, and A's `next` points to B).
    ///
    /// 1. Thread T1 starts `alloc`:
    ///   * Read: T1 reads the value of `free_head_`, which is A. It prepares to change `free_head_` from A to B.
    ///   * Suspend: The operating system suspends T1 and switches to execute T2.
    ///
    /// 2. Thread T2 begins execution:
    ///   * T2 calls `alloc()`, successfully taking A. The free list is now B -> C.
    ///   * T2 performs some operations on the resource in slot A.
    ///   * T2 calls `free(A)`, returning A to the pool. The free list becomes A -> B -> C again.
    ///   * The key point: `free_head_` now points back to A! However, the internal state of the free list might be
    ///     different from when T1 started (even if it's not obvious in this simple example).
    ///
    /// 3. Thread T1 resumes execution:
    ///   * Write (CAS): T1 resumes and executes its compare-exchange (CAS) operation. It expects `free_head_` to
    ///     be A and tries to update it to B.
    ///   * It checks the current value of `free_head_` and finds that it is indeed `A`.
    ///   * The CAS operation succeeds! The value of `free_head_` is changed to B by T1.
    ///
    /// Disaster strikes: T1 believes it successfully took A and set the list head to B. In reality, while it was
    /// suspended, A was taken and returned by T2. This sequence has corrupted the integrity of the free list. While
    /// the consequences might not be obvious in this simple case, in more complex scenarios (e.g., if T2's `free(A)`
    /// had pointed A's `next` to D), T1's successful CAS would leave the list in an inconsistent state, leading to
    /// memory leaks or crashes.
    ///
    /// How does the version (tag) solve this problem?
    ///
    /// Now, let's add the version. `free_head_` now stores `{version, index}`.
    ///
    /// 1. Thread T1 starts `alloc`:
    ///   * Read: T1 reads `free_head_`, getting `{v=10, index=A}`. It prepares to change `free_head_` from
    ///     `{v=10, index=A}` to `{v=11, index=B}`.
    ///   * Suspend: T1 is suspended.
    ///
    /// 2. Thread T2 begins execution:
    ///   * T2 `alloc(A)`, so `free_head_` becomes `{v=11, index=B}`.
    ///   * T2 `free(A)`, so `free_head_` becomes `{v=12, index=A}`.
    ///
    /// 3. Thread T1 resumes execution:
    ///   * Write (CAS): T1 resumes and expects the value of `free_head_` to be `{v=10, index=A}`.
    ///   * It checks the current value and finds it is `{v=12, index=A}`.
    ///   * The CAS operation fails! Because `{v=10, index=A}` is not equal to `{v=12, index=A}`.
    ///
    /// With the version, T1 can detect that `free_head_` was modified while it was suspended, even though the index
    /// part reverted to its original value. The failed CAS causes T1's `while(true)` loop to restart and retry the
    /// operation with the latest `free_head_` value, thus ensuring data structure correctness.
    std::atomic<internal::TaggedIndex> free_head_;

    /// A mutex to serialize expansion attempts on the cold path.
    std::mutex expand_mtx_;

    /// Gets a pointer to a Slot from its global index.
    Slot* getSlotByIndex(std::uint32_t idx) {
        const std::uint32_t block_idx = idx / kSlotsPerBlock;
        const std::uint32_t slot_off = idx % kSlotsPerBlock;

        if (block_idx >= kMaxBlocks) {
            return nullptr;
        }

        // Acquire load synchronizes with the release store on `block_ptrs_` in `expand()`. This ensures that we get a
        // pointer to a fully initialized `Block` and not a partially constructed one.
        Block* blk = block_ptrs_[block_idx].load(std::memory_order_acquire);
        if (!blk) {
            return nullptr;
        }

        return &blk->slots[slot_off];
    }

    /// Expands the pool by allocating a new block of slots (cold path). This function is protected by `expand_mtx_`.
    bool expand() {
        std::lock_guard<std::mutex> lock(expand_mtx_);

        // Double check: after acquiring the lock, check if another thread has already expanded the pool while we were
        // waiting.
        // Relaxed load is fine for this check (`std::mutex` provides release-acquire synchronization). We are holding
        // the `expand_mtx_`, so no other thread can be in `expand()`. This is just an optimization to see if another
        // thread already finished an expansion and populated the free list while we were waiting for the lock.
        if (free_head_.load(std::memory_order_relaxed).index() != static_cast<std::uint32_t>(-1)) {
            return true;
        }

        // Relaxed load is safe because we hold the mutex. No other thread can be modifying `num_blocks_`.
        const std::uint32_t block_idx = num_blocks_.load(std::memory_order_relaxed);
        if (block_idx >= kMaxBlocks) {
            return false;
        }

        // Allocate a new block using the custom policy.
        void* mem = AllocPolicy::allocate(sizeof(Block));
        Block* new_block = new (mem) Block();

        // Chain all slots in the new block into a single linked list.
        const std::uint32_t base_idx = block_idx * kSlotsPerBlock;
        for (std::size_t i = 0; i < kSlotsPerBlock - 1; ++i) {
            // Relaxed stores are sufficient here. We are initializing a new block of memory that is not yet visible to
            // any other thread. These writes will be made visible to other threads by the release operations below.
            new_block->slots[i].next_free_idx.store(base_idx + i + 1, std::memory_order_relaxed);
        }
        new_block->slots[kSlotsPerBlock - 1].next_free_idx.store(static_cast<std::uint32_t>(-1),
                                                                 std::memory_order_relaxed);

        // Publish the new block so it's visible to other threads.
        // The release semantics ensure that all prior writes to the `new_block` (initializing the slots) are visible
        // before other threads can see the pointer to it via an acquire-load.
        block_ptrs_[block_idx].store(new_block, std::memory_order_release);
        num_blocks_.store(block_idx + 1, std::memory_order_release);

        // Atomically prepend the new list of slots to the global free list.
        const std::uint32_t new_chain_head = base_idx;
        while (true) {
            // Relaxed load is fine inside the CAS loop. The CAS itself provides the necessary synchronization.
            auto current_head = free_head_.load(std::memory_order_relaxed);
            const std::uint32_t head_idx = current_head.index();

            // Link the tail of our new list to the old head of the global list.
            // Relaxed store is sufficient as this write will be published by the release-CAS below.
            new_block->slots[kSlotsPerBlock - 1].next_free_idx.store(head_idx, std::memory_order_relaxed);

            // Try to atomically swap the global head to the head of our new list.
            const std::uint32_t next_tag = current_head.tag() + 1;
            const internal::TaggedIndex new_head(new_chain_head, next_tag);
            // Success memory order: `release`. This makes the entire new block of slots available to `alloc` calls in
            // other threads. It synchronizes with the `acquire` load in `alloc`.
            //
            // Failure memory order: `relaxed`. If the CAS fails, we just loop again.
            if (free_head_.compare_exchange_weak(current_head, new_head, std::memory_order_release,
                                                 std::memory_order_relaxed)) {
                break;
            }
        }

        return true;
    }
};

} // namespace hcomm

#endif // HCOMM_MEMORY_PAGED_RESOURCE_POOL_HPP_
