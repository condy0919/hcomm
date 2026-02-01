// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_WEAKPTR_HPP_
#define HCOMM_BASE_WEAKPTR_HPP_

#include <atomic>
#include <utility>

#include "hcomm/base/refptr.hpp"

namespace hcomm {
namespace internal {
class Flag : public RefCounted<Flag> {
public:
    Flag() = default;

    bool valid() const {
        return valid_.load(std::memory_order_acquire);
    }

    void invalidate() {
        valid_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> valid_{true};
};

class WeakReference {
public:
    WeakReference() = default;

    WeakReference(RefPtr<Flag> flag) noexcept : flag_(std::move(flag)) {}

    WeakReference(WeakReference&& rhs) noexcept = default;
    WeakReference(const WeakReference& rhs) = default;

    WeakReference& operator=(WeakReference&& rhs) noexcept = default;
    WeakReference& operator=(const WeakReference& rhs) = default;

    ~WeakReference() = default;

    void reset() {
        flag_.reset();
    }

    bool valid() const {
        return flag_ && flag_->valid();
    }

private:
    RefPtr<Flag> flag_;
};

class WeakReferenceOwner {
public:
    WeakReferenceOwner() : flag_(makeRef<Flag>()) {}

    ~WeakReferenceOwner() {
        if (flag_) {
            flag_->invalidate();
        }
    }

    WeakReference getWeakRef() {
        return WeakReference(flag_);
    }

private:
    RefPtr<Flag> flag_;
};
} // namespace internal

template <typename T>
class WeakPtrFactory;

/// A `WeakPtr` is a non-owning pointer to an object that is invalidated when the corresponding `WeakPtrFactory` is
/// destroyed. This is useful for preventing use-after-free errors when an object's lifetime is managed manually.
///
/// `WeakPtr` is not thread-safe. All methods on a `WeakPtr` and its factory must be called from the same thread.
///
/// Example:
/// ```cpp
/// #include <memory>
///
/// // A class that can be tracked by `WeakPtr`.
/// class MyObject {
/// public:
///     MyObject() : weak_ptr_factory_(this) {}
///
///     // Returns a `WeakPtr` to this object.
///     WeakPtr<MyObject> getWeakPtr() {
///         return weak_ptr_factory_.weak_from_this();
///     }
///
///     void doSomething() {
///         // ...
///     }
///
/// private:
///     // The factory that creates `WeakPtr`s to this object.
///     WeakPtrFactory<MyObject> weak_ptr_factory_;
/// };
///
/// void useWeakPtr() {
///     WeakPtr<MyObject> weak_ptr;
///
///     {
///         auto obj = std::make_unique<MyObject>();
///         weak_ptr = obj->getWeakPtr();
///
///         // The `WeakPtr` is valid and can be used to access the object.
///         if (MyObject* raw_ptr = weak_ptr.get()) {
///             raw_ptr->doSomething();
///         }
///     }
///
///     // `obj` is now destroyed, and the `WeakPtr` is expired.
///     // `weak_ptr.get()` will return nullptr.
///     if (MyObject* raw_ptr = weak_ptr.get()) {
///         // This code will not be reached.
///     }
/// }
/// ```
template <typename T>
class WeakPtr final {
    friend class WeakPtrFactory<T>;

    template <typename U>
    friend class WeakPtr;

public:
    /// Default constructor. Creates a `WeakPtr` that is expired.
    WeakPtr() noexcept = default;

    /// Creates a `WeakPtr` that is expired.
    WeakPtr(std::nullptr_t) noexcept {}

    WeakPtr(const WeakPtr& other) noexcept = default;
    WeakPtr(WeakPtr&& other) noexcept = default;

    template <typename U>
        requires(std::is_convertible_v<U*, T*>)
    WeakPtr(const WeakPtr<U>& other) noexcept : ref_(other.ref_), ptr_(other.ptr_) {}

    template <typename U>
        requires(std::is_convertible_v<U*, T*>)
    WeakPtr(WeakPtr<U>&& other) noexcept : ref_(std::move(other.ref_)), ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    WeakPtr& operator=(const WeakPtr& other) noexcept = default;
    WeakPtr& operator=(WeakPtr&& other) noexcept = default;

    template <typename U>
        requires(std::is_convertible_v<U*, T*>)
    WeakPtr& operator=(const WeakPtr<U>& other) noexcept {
        ref_ = other.ref_;
        ptr_ = other.ptr_;
        return *this;
    }

    template <typename U>
        requires(std::is_convertible_v<U*, T*>)
    WeakPtr& operator=(WeakPtr<U>&& other) noexcept {
        ref_ = std::move(other.ref_);
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
        return *this;
    }

    /// Returns the raw pointer if the object is still alive, otherwise nullptr.
    ///
    /// It is not safe to dereference the returned pointer without first checking for nullptr. The object can be
    /// destroyed at any time on another thread. This implementation of `WeakPtr` does not provide a `lock()` method to
    /// create a strong reference, so extra care must be taken.
    T* get() const {
        return ref_.valid() ? ptr_ : nullptr;
    }

    /// Dereferences the stored pointer.
    ///
    /// The behavior is undefined if the `WeakPtr` is expired.
    T& operator*() const {
        return *get();
    }

    /// Dereferences the stored pointer.
    ///
    /// The behavior is undefined if the `WeakPtr` is expired.
    T* operator->() const {
        return get();
    }

    /// Checks if the `WeakPtr` holds a valid, non-expired pointer.
    explicit operator bool() const {
        return get() != nullptr;
    }

    /// Checks if the pointed-to object has been destroyed.
    bool expired() const {
        return get() == nullptr;
    }

    /// Resets the `WeakPtr`, making it expired.
    void reset() {
        ref_.reset();
        ptr_ = nullptr;
    }

    /// Swaps the contents of this `WeakPtr` with another.
    void swap(WeakPtr& other) noexcept {
        using std::swap;
        swap(ref_, other.ref_);
        swap(ptr_, other.ptr_);
    }

private:
    WeakPtr(internal::WeakReference ref, T* ptr) : ref_(std::move(ref)), ptr_(ptr) {}

    internal::WeakReference ref_;

    T* ptr_ = nullptr;
};

template <typename T, typename U>
bool operator==(const WeakPtr<T>& a, const WeakPtr<U>& b) {
    return a.get() == b.get();
}

template <typename T, typename U>
bool operator!=(const WeakPtr<T>& a, const WeakPtr<U>& b) {
    return !(a == b);
}

template <typename T>
bool operator==(const WeakPtr<T>& a, std::nullptr_t) noexcept {
    return a.get() == nullptr;
}

template <typename T>
bool operator==(std::nullptr_t, const WeakPtr<T>& b) noexcept {
    return b.get() == nullptr;
}

template <typename T>
bool operator!=(const WeakPtr<T>& a, std::nullptr_t) noexcept {
    return a.get() != nullptr;
}

template <typename T>
bool operator!=(std::nullptr_t, const WeakPtr<T>& b) noexcept {
    return b.get() != nullptr;
}

template <typename T>
void swap(WeakPtr<T>& a, WeakPtr<T>& b) noexcept {
    a.swap(b);
}

/// The `WeakPtrFactory` is used to create `WeakPtr` instances that point to an object of type `T`.
/// It manages the lifetime of the weak pointers by holding an `internal::WeakReferenceOwner`.
/// When the `WeakPtrFactory` is destroyed, all `WeakPtr`s created by it will become expired.
template <typename T>
class WeakPtrFactory final {
public:
    WeakPtrFactory() = delete;

    /// Constructs a `WeakPtrFactory` for a given object.
    /// The `ptr` must be a valid pointer to the object that `WeakPtr`s will observe.
    explicit WeakPtrFactory(T* ptr) : ptr_(ptr) {}

    WeakPtrFactory(const WeakPtrFactory& rhs) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory& rhs) = delete;

    ~WeakPtrFactory() = default;

    /// Creates a `WeakPtr` that observes the object managed by this factory.
    ///
    /// This method is typically used to obtain a `WeakPtr` to `this` from within a member function
    /// of an object that owns a `WeakPtrFactory`.
    WeakPtr<T> weak_from_this() {
        return WeakPtr<T>(ref_owner_.getWeakRef(), ptr_);
    }

private:
    internal::WeakReferenceOwner ref_owner_;
    T* ptr_;
};

} // namespace hcomm

#endif // HCOMM_BASE_WEAKPTR_HPP_
