#pragma once
/**
 * rex::atomic_ref: std::atomic_ref where the standard library has it (__cpp_lib_atomic_ref), and otherwise the part
 * of it the SDK and the app use (load, store, exchange, compare-exchange on lock-free sizes), on the compiler's
 * __atomic builtins. The NDK's libc++ (LLVM 19) has no std::atomic_ref.
 */

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace rex {

#if defined(__cpp_lib_atomic_ref) && !defined(REX_FORCE_ATOMIC_REF_FALLBACK)

template <class T>
using atomic_ref = std::atomic_ref<T>;

#else

template <class T>
class atomic_ref {
  static_assert(std::is_trivially_copyable_v<T>, "atomic_ref needs a trivially copyable type");

 public:
  using value_type = T;
  static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), 0);
  static constexpr std::size_t required_alignment = alignof(T) < sizeof(T) ? sizeof(T) : alignof(T);

  explicit atomic_ref(T& object) noexcept : ptr_(&object) {}
  atomic_ref(const atomic_ref&) noexcept = default;
  atomic_ref& operator=(const atomic_ref&) = delete;

  T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    T value;
    __atomic_load(ptr_, &value, Order(order));
    return value;
  }
  void store(T value, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    __atomic_store(ptr_, &value, Order(order));
  }
  T exchange(T value, std::memory_order order = std::memory_order_seq_cst) const noexcept {
    T old;
    __atomic_exchange(ptr_, &value, &old, Order(order));
    return old;
  }
  bool compare_exchange_weak(T& expected, T desired,
                             std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return __atomic_compare_exchange(ptr_, &expected, &desired, true, Order(order), FailureOrder(order));
  }
  bool compare_exchange_strong(T& expected, T desired,
                               std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return __atomic_compare_exchange(ptr_, &expected, &desired, false, Order(order), FailureOrder(order));
  }
  bool compare_exchange_weak(T& expected, T desired, std::memory_order success,
                             std::memory_order failure) const noexcept {
    return __atomic_compare_exchange(ptr_, &expected, &desired, true, Order(success), Order(failure));
  }
  bool compare_exchange_strong(T& expected, T desired, std::memory_order success,
                               std::memory_order failure) const noexcept {
    return __atomic_compare_exchange(ptr_, &expected, &desired, false, Order(success), Order(failure));
  }
  operator T() const noexcept { return load(); }

 private:
  static constexpr int Order(std::memory_order order) {
    switch (order) {
      case std::memory_order_relaxed: return __ATOMIC_RELAXED;
      case std::memory_order_consume: return __ATOMIC_CONSUME;
      case std::memory_order_acquire: return __ATOMIC_ACQUIRE;
      case std::memory_order_release: return __ATOMIC_RELEASE;
      case std::memory_order_acq_rel: return __ATOMIC_ACQ_REL;
      default: return __ATOMIC_SEQ_CST;
    }
  }
  // A compare-exchange's failure is a load: no release in it.
  static constexpr int FailureOrder(std::memory_order order) {
    switch (order) {
      case std::memory_order_acq_rel: return __ATOMIC_ACQUIRE;
      case std::memory_order_release: return __ATOMIC_RELAXED;
      default: return Order(order);
    }
  }

  T* ptr_;
};

#endif

}  // namespace rex
