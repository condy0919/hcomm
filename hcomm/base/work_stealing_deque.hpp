// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_WORK_STEALING_DEQUE_HPP_
#define HCOMM_BASE_WORK_STEALING_DEQUE_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "hcomm/base/optimization.hpp"

namespace hcomm {
/// WorkStealingDeque implements the Chase-Lev lock-free work-stealing deque.
///
/// # Algorithm Origin
///
/// This algorithm was originally proposed by David Chase and Yossi Lev in their 2005 paper "Dynamic Circular
/// Work-Stealing Deque". It was further refined for weak memory models (like ARM or PowerPC) by Nhat Minh Lê et al. in
/// their 2013 paper "Correct and Efficient Work-Stealing for Weak Memory Models".
///
/// It is a fundamental building block for modern task-based parallel runtimes (like Go, Rust's Tokio, or Intel TBB).
///
/// # Mental Model: Private Stack vs. Public Queue
///
/// This deque functions as a hybrid between a thread-local stack and a concurrent queue:
///
/// 1. **The Owner (LIFO Stack Behavior)**: The thread owning the deque interacts exclusively with the **Bottom**. It
///    pushes and pops tasks in a "Last-In-First-Out" manner. This maximizes **cache locality**, as the most recently
///    produced task (likely a sub-problem) is processed while its data is still warm in the L1/L2 cache.
/// 2. **The Stealers (FIFO Queue Behavior)**: Other threads attempt to "steal" from the **Top**. They take the oldest
///    tasks in a "First-In-First-Out" manner. In recursive task decomposition, older tasks represent the largest
///    branches of the computation tree. Stealing these "coarse-grained" tasks ensures that the stealing thread remains
///    productive for longer, minimizing future contention.
/// 3. **The Synchronization Point**: The design ensures that the Owner and Stealers operate on opposite ends of the
///    buffer, eliminating most contention. A formal "duel" (atomic CAS) only occurs when the deque is reduced to a
///    single element, ensuring that exactly one thread claims the final task.
///
/// # High-Level Principles
///
/// - **Topology**: The deque has two ends: "Top" (public/shared) and "Bottom" (private).
/// - **Wait-Free Push**: The Owner can push tasks without being blocked by concurrent Stealers, ensuring high
///   throughput for the local worker.
/// - **Lock-Free Coordination**: Most operations progress without locks. Strong memory barriers (SeqCst) are only used
///   to resolve races on the last remaining element.
///
/// # Implementation Notes
///
/// This specific implementation is a **Bounded** version of the Chase-Lev deque. Unlike the original paper which
/// describes a dynamic circular buffer, this version uses a fixed-size `std::array` for performance and simplicity in
/// embedded or high-performance networking contexts.
///
/// - **Memory Barriers**: Uses a combination of Acquire/Release semantics and `std::memory_order_seq_cst` fences to
///   ensure correctness across different CPU architectures (especially weak-memory models like ARM).
/// - **False Sharing**: `top_` and `bottom_` are padded to separate cache lines to prevent performance degradation
///   caused by cache coherence traffic between the Owner and Stealers.
template <typename T, std::size_t Capacity = 256>
class WorkStealingDeque final {
    static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2 for fast modulo arithmetic");

    static_assert(std::is_default_constructible_v<T>,
                  "T must be default constructible to use std::array backing buffer");

    static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move assignable");

public:
    WorkStealingDeque() = default;

    WorkStealingDeque(WorkStealingDeque&& rhs) noexcept = delete;
    WorkStealingDeque& operator=(WorkStealingDeque&& rhs) noexcept = delete;

    ~WorkStealingDeque() = default;

    /// Pushes a task to the bottom of the deque. Only callable by the Owner.
    ///
    /// The `memory_order_acquire` on top_ ensures the Owner sees the latest progress of Stealers, preventing buffer
    /// overflow.
    ///
    /// Returns true if successfully pushed, false if the deque is full.
    bool push(T&& x) noexcept {
        const std::size_t b = bottom_.load(std::memory_order_relaxed);
        const std::size_t t = top_.load(std::memory_order_acquire);
        if (b - t >= Capacity) {
            return false;
        }

        buffer_[b & kMask] = std::move(x);
        // The release store ensures buffer writes are visible to Stealers before they observe the incremented bottom_.
        bottom_.store(b + 1, std::memory_order_release);
        return true;
    }

    bool push(const T& x) noexcept(std::is_nothrow_copy_assignable_v<T>)
        requires std::is_copy_assignable_v<T>
    {
        const std::size_t b = bottom_.load(std::memory_order_relaxed);
        const std::size_t t = top_.load(std::memory_order_acquire);
        if (b - t >= Capacity) {
            return false;
        }

        buffer_[b & kMask] = x;
        bottom_.store(b + 1, std::memory_order_release);
        return true;
    }

    /// Pops a task from the bottom of the deque in LIFO order. Only callable by the Owner.
    ///
    /// This method synchronizes with concurrent steal() calls. When the deque has only one element, the Owner competes
    /// with Stealers via a CAS on top_.
    std::optional<T> pop() noexcept {
        std::size_t b = bottom_.load(std::memory_order_relaxed);
        std::size_t t = top_.load(std::memory_order_relaxed);
        if (t >= b) {
            return std::nullopt;
        }

        --b;
        bottom_.store(b, std::memory_order_relaxed);

        // This SeqCst fence is mandatory. It prevents the CPU from reordering the store to bottom_ (announcing the
        // intent to pop) with the subsequent load of top_ (checking for Stealers). Without this, both a Stealer and
        // the Owner might think they own the same "last" element.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        t = top_.load(std::memory_order_relaxed);

        if (t < b) {
            // Safe zone: multiple elements remain in the deque, ensuring no conflict with concurrent stealers.
            return std::move(buffer_[b & kMask]);
        } else if (t == b) {
            // Last element race: use CAS to ensure only one thread (Owner or Stealer) claims the element by
            // incrementing top_.
            if (top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                // Owner Won. Restore bottom_ to indicate the deque is empty.
                bottom_.store(b + 1, std::memory_order_relaxed);
                return std::move(buffer_[b & kMask]);
            } else {
                // Stealer won. Restore bottom_ to indicate the deque is empty.
                bottom_.store(b + 1, std::memory_order_relaxed);
                return std::nullopt;
            }
        } else {
            // Element already stolen.
            bottom_.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
    }

    /// Steals a task from the top of the deque in FIFO order. Called by Stealers.
    ///
    /// Uses a SeqCst fence to coordinate with the Owner's pop() and multiple concurrent Stealers.
    ///
    /// NOTE: Susceptible to wrap-around overwrite if the stealer is preempted and the owner pushes exactly Capacity
    /// elements before the stealer reads the buffer. Practically impossible with large capacities.
    std::optional<T> steal() noexcept {
        std::size_t t = top_.load(std::memory_order_acquire);
        std::size_t b = bottom_.load(std::memory_order_acquire);
        if (t >= b) {
            return std::nullopt;
        }

        // Prevents the reordering of the top_ load with the subsequent bottom_ load. Ensures that if the Owner is
        // popping the last element, the Stealer correctly observes the state before attempting CAS.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        b = bottom_.load(std::memory_order_acquire);

        if (t < b) {
            if (top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return std::move(buffer_[t & kMask]);
            }
        }
        return std::nullopt;
    }

    /// Returns the approximate number of tasks in the deque.
    ///
    /// NOTE: This is a concurrent snapshot and should only be used for heuristics or logging.
    std::size_t size() const noexcept {
        const std::size_t b = bottom_.load(std::memory_order_relaxed);
        const std::size_t t = top_.load(std::memory_order_relaxed);
        return b >= t ? b - t : 0;
    }

    /// Returns true if the deque is approximately empty.
    ///
    /// Like size(), this provides a concurrent snapshot and should be used primarily for heuristics.
    bool empty() const noexcept {
        return size() == 0;
    }

    /// Returns the fixed capacity of the deque.
    std::size_t capacity() const noexcept {
        return Capacity;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // top_ and bottom_ are padded to different cachelines to prevent false sharing,
    // which significantly improves performance in high-contention work-stealing scenarios.
    alignas(HCOMM_CACHELINE_SIZE) std::atomic<std::size_t> top_{0};
    alignas(HCOMM_CACHELINE_SIZE) std::atomic<std::size_t> bottom_{0};

    std::array<T, Capacity> buffer_;
};

} // namespace hcomm

#endif // HCOMM_BASE_WORK_STEALING_DEQUE_HPP_
