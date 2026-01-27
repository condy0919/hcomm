// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_REFPTR_HPP_
#define HCOMM_BASE_REFPTR_HPP_

#include <atomic>
#include <cstddef>
#include <utility>

namespace hcomm {
namespace internal {
struct AdoptRefTag {
    explicit AdoptRefTag() = default;
};
} // namespace internal

template <typename T>
class RefPtr;

/// Tag instance to request adoption of an existing reference (i.e., do not increment ref count).
inline static constexpr internal::AdoptRefTag kAdoptRef{};

/// # Synopsis
///
/// `RefCounted` is a base class for objects that require intrusive reference counting.
/// It uses CRTP (Curiously Recurring Template Pattern) to implement the deletion logic efficiently.
///
/// # Usage
///
/// Inherit publicly from `RefCounted<YourClass>`. Ensure `YourClass` allows `RefCounted` to delete it.
///
/// @code
/// class MyObject : public hcomm::RefCounted<MyObject> {
/// public:
///     void doSomething() {}
/// };
/// @endcode
template <typename Derived>
class RefCounted {
public:
    RefCounted() noexcept : ref_count_(0) {}

    RefCounted(const RefCounted&) noexcept : ref_count_(0) {}

    RefCounted& operator=(const RefCounted&) noexcept {
        return *this;
    }

    RefPtr<Derived> shared_from_this() {
        return RefPtr<Derived>(static_cast<Derived*>(this));
    }

    /// Returns the current reference count.
    int use_count() const noexcept {
        return ref_count_.load(std::memory_order_relaxed);
    }

    /// Increments the reference count.
    void ref() noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Decrements the reference count and deletes the object if the count reaches zero.
    void unref() noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete static_cast<Derived*>(this);
        }
    }

protected:
    ~RefCounted() = default;

private:
    mutable std::atomic<int> ref_count_;
};

/// # Synopsis
///
/// `RefPtr` is a smart pointer class for intrusive reference counting. It is similar to `boost::intrusive_ptr`.
/// It assumes the pointed-to type `T` has `ref()` and `unref()` methods.
///
/// # Features
///
/// - **Zero-Overhead**: Same size as a raw pointer.
/// - **Polymorphism**: Supports derived-to-base conversions.
/// - **Safety**: Explicit constructors for raw pointers; no implicit assignment from raw pointers.
/// - **Adoption**: Can adopt existing references without incrementing the count via `kAdoptRef`.
///
/// # Thread Safety
///
/// `RefPtr` instances themselves are not thread-safe (like `std::shared_ptr`).
/// The reference counting mechanism in `RefCounted` *is* thread-safe.
template <typename T>
class RefPtr {
public:
    using Type = T;

    // --- Constructors ---

    /// Default constructor.
    constexpr RefPtr() noexcept = default;

    /// Constructor from nullptr.
    constexpr RefPtr(std::nullptr_t) noexcept {}

    /// Constructor from raw pointer.
    /// Increments reference count.
    explicit RefPtr(T* p) noexcept : ptr_(p) {
        if (ptr_) {
            ptr_->ref();
        }
    }

    /// Constructor from raw pointer (adoption).
    /// Does NOT increment reference count.
    RefPtr(T* p, internal::AdoptRefTag) noexcept : ptr_(p) {}

    /// Copy constructor.
    /// Increments reference count.
    RefPtr(const RefPtr& rhs) noexcept : ptr_(rhs.ptr_) {
        if (ptr_) {
            ptr_->ref();
        }
    }

    /// Copy constructor (polymorphic).
    /// Increments reference count.
    template <typename U>
        requires std::convertible_to<U*, T*>
    RefPtr(const RefPtr<U>& rhs) noexcept : ptr_(rhs.get()) {
        if (ptr_) {
            ptr_->ref();
        }
    }

    /// Move constructor.
    /// Does NOT increment reference count.
    RefPtr(RefPtr&& rhs) noexcept : ptr_(rhs.ptr_) {
        rhs.ptr_ = nullptr;
    }

    /// Move constructor (polymorphic).
    /// Does NOT increment reference count.
    template <typename U>
        requires std::convertible_to<U*, T*>
    RefPtr(RefPtr<U>&& rhs) noexcept : ptr_(rhs.detach()) {}

    /// Destructor.
    /// Decrements reference count if not null.
    ~RefPtr() noexcept {
        if (ptr_) {
            ptr_->unref();
        }
    }

    // --- Assignment ---

    /// Copy assignment.
    RefPtr& operator=(const RefPtr& rhs) noexcept {
        RefPtr(rhs).swap(*this);
        return *this;
    }

    /// Copy assignment (polymorphic).
    template <typename U>
        requires std::convertible_to<U*, T*>
    RefPtr& operator=(const RefPtr<U>& rhs) noexcept {
        RefPtr(rhs).swap(*this);
        return *this;
    }

    /// Move assignment.
    RefPtr& operator=(RefPtr&& rhs) noexcept {
        RefPtr(std::move(rhs)).swap(*this);
        return *this;
    }

    /// Move assignment (polymorphic).
    template <typename U>
        requires std::convertible_to<U*, T*>
    RefPtr& operator=(RefPtr<U>&& rhs) noexcept {
        RefPtr(std::move(rhs)).swap(*this);
        return *this;
    }

    /// Assigns nullptr.
    RefPtr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    // --- Modifiers ---

    /// Resets the pointer to a new value (or nullptr).
    /// Decrements ref count of old object, increments ref count of new object.
    void reset(T* p = nullptr) noexcept {
        RefPtr(p).swap(*this);
    }

    /// Releases ownership of the pointer without decrementing the reference count.
    /// Useful for passing ownership to C APIs or hardware interfaces (e.g., RDMA wr_id).
    [[nodiscard]] T* detach() noexcept {
        T* temp = ptr_;
        ptr_ = nullptr;
        return temp;
    }

    /// Adopts an existing reference from a raw pointer.
    /// Creates a new `RefPtr` that takes ownership of a `raw` pointer without incrementing its reference count. This
    /// is useful when an API (e.g., ibv_poll_cq) gives you a raw pointer with a reference count already incremented.
    static RefPtr adopt(T* raw) noexcept {
        return RefPtr(raw, kAdoptRef);
    }

    /// Swaps the managed pointer with another RefPtr.
    void swap(RefPtr& rhs) noexcept {
        std::swap(ptr_, rhs.ptr_);
    }

    // --- Observers ---

    /// Returns the raw pointer.
    T* get() const noexcept {
        return ptr_;
    }

    /// Dereferences the pointer.
    T& operator*() const noexcept {
        return *ptr_;
    }

    /// Dereferences the pointer.
    T* operator->() const noexcept {
        return ptr_;
    }

    /// Checks if the pointer is not null.
    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

private:
    template <typename>
    friend class RefPtr;

    T* ptr_ = nullptr;
};

// --- Global Functions ---

/// Swaps the contents of two RefPtrs.
template <typename T>
inline void swap(RefPtr<T>& lhs, RefPtr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

// Comparison against RefPtr
template <typename T, typename U>
inline bool operator==(const RefPtr<T>& lhs, const RefPtr<U>& rhs) noexcept {
    return lhs.get() == rhs.get();
}

template <typename T, typename U>
inline bool operator!=(const RefPtr<T>& lhs, const RefPtr<U>& rhs) noexcept {
    return lhs.get() != rhs.get();
}

// Comparison against raw pointer
template <typename T>
inline bool operator==(const RefPtr<T>& lhs, const T* rhs) noexcept {
    return lhs.get() == rhs;
}

template <typename T>
inline bool operator!=(const RefPtr<T>& lhs, const T* rhs) noexcept {
    return lhs.get() != rhs;
}

template <typename T>
inline bool operator==(const T* lhs, const RefPtr<T>& rhs) noexcept {
    return lhs == rhs.get();
}

template <typename T>
inline bool operator!=(const T* lhs, const RefPtr<T>& rhs) noexcept {
    return lhs != rhs.get();
}

// Comparison against nullptr
template <typename T>
inline bool operator==(const RefPtr<T>& lhs, std::nullptr_t) noexcept {
    return lhs.get() == nullptr;
}

template <typename T>
inline bool operator!=(const RefPtr<T>& lhs, std::nullptr_t) noexcept {
    return lhs.get() != nullptr;
}

template <typename T>
inline bool operator==(std::nullptr_t, const RefPtr<T>& rhs) noexcept {
    return nullptr == rhs.get();
}

template <typename T>
inline bool operator!=(std::nullptr_t, const RefPtr<T>& rhs) noexcept {
    return nullptr != rhs.get();
}

// --- Casts ---

/// Performs a static_cast on the underlying pointer.
template <typename T, typename U>
inline RefPtr<T> static_pointer_cast(const RefPtr<U>& p) {
    return RefPtr<T>(static_cast<T*>(p.get()));
}

/// Performs a const_cast on the underlying pointer.
template <typename T, typename U>
inline RefPtr<T> const_pointer_cast(const RefPtr<U>& p) {
    return RefPtr<T>(const_cast<T*>(p.get()));
}

/// Performs a dynamic_cast on the underlying pointer.
template <typename T, typename U>
inline RefPtr<T> dynamic_pointer_cast(const RefPtr<U>& p) {
    return RefPtr<T>(dynamic_cast<T*>(p.get()));
}

// --- Helper ---

/// Creates a new RefPtr<T> that takes ownership of a new T.
template <typename T, typename... Args>
RefPtr<T> makeRef(Args&&... args) noexcept {
    return RefPtr<T>(new T(std::forward<Args>(args)...));
}

} // namespace hcomm

#endif // HCOMM_BASE_REFPTR_HPP_
