#pragma once

/**
 * @file guards.hpp
 * @brief RAII wrappers for managed resource locking and unlocking.
 */

#include <type_traits>
#include <utility>

/**
 * @namespace engine::memory::guards
 * @brief Contains RAII primitives for resource safety and scoped ownership.
 */
namespace engine::memory::guards {

/**
 * @brief Concept that checks if a type provides the necessary internal types for resource management.
 * * Requires that the type `L` defines:
 * - @c data_type: The type of the resource being managed.
 * - @c UnlockOnDestruct: A functional type (functor) used to release the resource.
 * * @tparam L The type to validate against the concept.
 */
template <typename L>
concept HasUnlockOnDestruct = requires(L::data_type &d) {
  typename L::data_type;
  typename L::UnlockOnDestruct;
};

/**
 * @class SingleResourceLockGuard
 * @brief A basic RAII guard that uses a stateless functor to unlock a resource.
 * * This guard assumes @p Unlock can be default-constructed at the time of destruction.
 * * @tparam D The type of the resource data.
 * @tparam Unlock A stateless functor type called as `Unlock{}(data)`.
 */
template <typename D, typename Unlock> class SingleResourceLockGuard {
public:
  using data_type = D;
  using UnlockOnDestruct = Unlock;

public:
  data_type *data; ///< Pointer to the managed resource.

  /**
   * @brief Constructs a guard and takes ownership of the resource reference.
   * @param data Reference to the resource to manage.
   */
  explicit SingleResourceLockGuard(data_type &data) : data(&data) {}

  /** @brief Dereferences the guard to access the underlying resource. */
  data_type &operator*() { return *data; }

  /** @brief Move constructor. Transfers ownership from @p r_value. */
  SingleResourceLockGuard(SingleResourceLockGuard &&r_value)
      : data(r_value.data) {
    r_value.data = nullptr;
  }

  /** @brief Move assignment. Unlocks current resource before taking ownership of the new one. */
  SingleResourceLockGuard &operator=(SingleResourceLockGuard &&r_value) {
    UnlockOnDestruct{}(*data);
    data = r_value.data;
    r_value.data = nullptr;
    return *this;
  }

  /** @brief Destructor. Invokes the unlock functor if the resource is still owned. */
  ~SingleResourceLockGuard() {
    if (data != nullptr) {
      UnlockOnDestruct{}(*data);
      data = nullptr;
    }
  }

  SingleResourceLockGuard(SingleResourceLockGuard &other) = delete;
  SingleResourceLockGuard &operator=(SingleResourceLockGuard &other) = delete;
};

/**
 * @class InstSingleResourceLockGuard
 * @brief An RAII guard that stores a stateful instance of an unlock functor.
 * * Useful when the unlocking mechanism requires state (e.g., a specific memory pool or context).
 * * @tparam D The type of the resource data.
 * @tparam Unlock A functor type that may contain state.
 */
template <typename D, typename Unlock> class InstSingleResourceLockGuard {
public:
  using data_type = D;
  using UnlockOnDestruct = Unlock;

public:
  data_type *data; ///< Pointer to the managed resource.
  Unlock unlock;   ///< Stored instance of the unlock functor.

  /**
   * @brief Constructs a guard with a specific unlocker instance.
   * @param data Reference to the resource.
   * @param unlock The functor instance used for unlocking.
   */
  explicit InstSingleResourceLockGuard(data_type &data, Unlock unlock)
      : data(&data), unlock(unlock) {}

  data_type &operator*() { return *data; }
  /** @brief Executes the stored unlock functor on the resource. */
  void unlock_func() { unlock(*data); }

  InstSingleResourceLockGuard(InstSingleResourceLockGuard &&r_value)
      : data(r_value.data), unlock(r_value.unlock) {
    r_value.data = nullptr;
  }
  InstSingleResourceLockGuard &
  operator=(InstSingleResourceLockGuard &&r_value) {
    unlock_func();
    data = r_value.data;
    unlock = r_value.unlock;
    r_value.data = nullptr;
    return *this;
  }
  ~InstSingleResourceLockGuard() {
    if (data != nullptr) {
      unlock_func();
      data = nullptr;
    }
  }

  InstSingleResourceLockGuard(InstSingleResourceLockGuard &other) = delete;
  InstSingleResourceLockGuard &
  operator=(InstSingleResourceLockGuard &other) = delete;
};

/**
 * @class InstSingleResourceLockGuardWrapper
 * @brief A wrapping guard that chains or decorates an existing guard's unlock logic.
 * * This class wraps an existing guard (that satisfies @c HasUnlockOnDestruct) and
 * provides a mechanism to execute a new unlock logic alongside the previous one.
 * * @tparam D A type satisfying @c HasUnlockOnDestruct (e.g., another guard).
 * @tparam Unlock A template template parameter for the new unlock functor type.
 */
template <typename D, template <typename> typename Unlock>
  requires HasUnlockOnDestruct<D>
class InstSingleResourceLockGuardWrapper {
public:
  using data_type = D::data_type;
  using UnlockOnDestruct = Unlock<typename D::UnlockOnDestruct>;

public:
  data_type *data; ///< Pointer to the managed resource.

  /**
   * @struct CombinedUnlock
   * @brief Internal helper to store and execute the new and previous unlock logic.
   */
  struct {
    UnlockOnDestruct unlock; ///< The new wrapping unlock functor.
    typename D::UnlockOnDestruct
        prev_unlock; ///< The original unlock functor from the wrapped guard.
    /** @brief Executes the wrapper unlock logic, passing the previous unlocker as context. */
    void operator()(data_type &d) { unlock(d, prev_unlock); }
  } unlock;

  /**
   * @brief Wraps an existing guard, moving its state into this wrapper.
   * @param r_value The existing guard to be wrapped.
   * @param unlock The new unlock functor instance.
   */
  explicit InstSingleResourceLockGuardWrapper(D &&r_value,
                                              UnlockOnDestruct unlock)
      : data(r_value.data), unlock({.unlock = std::move(unlock),
                                    .prev_unlock = std::move(r_value.unlock)}) {
    r_value.data = nullptr;
  }
  data_type &operator*() { return *data; }
  InstSingleResourceLockGuardWrapper(
      InstSingleResourceLockGuardWrapper &&r_value)
      : data(r_value.data), unlock(r_value.unlock) {
    r_value.data = nullptr;
  }
  void unlock_func() { unlock(*data); }
  InstSingleResourceLockGuardWrapper &
  operator=(InstSingleResourceLockGuardWrapper &&r_value) {
    unlock_func();
    data = r_value.data;
    unlock = r_value.unlock;
    r_value.data = nullptr;
    return *this;
  }
  ~InstSingleResourceLockGuardWrapper() {
    if (data != nullptr) {
      unlock_func();
      data = nullptr;
    }
  }
  InstSingleResourceLockGuardWrapper(
      InstSingleResourceLockGuardWrapper &other) = delete;
  InstSingleResourceLockGuardWrapper &
  operator=(InstSingleResourceLockGuardWrapper &other) = delete;
};
} // namespace engine::memory::guards
