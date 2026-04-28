#pragma once

#include <type_traits>
#include <utility>
namespace engine::common::memory::guards {

template <typename L>
concept HasUnlockOnDestruct = requires(L::data_type &d) {
  typename L::data_type;
  typename L::UnlockOnDestruct;
};

template <typename D, typename Unlock> class SingleResourceLockGuard {
public:
  using data_type = D;
  using UnlockOnDestruct = Unlock;

public:
  data_type *data;
  explicit SingleResourceLockGuard(data_type &data) : data(&data) {}
  data_type &operator*() { return *data; }
  SingleResourceLockGuard(SingleResourceLockGuard &other) = delete;
  SingleResourceLockGuard &operator=(SingleResourceLockGuard &other) = delete;
  SingleResourceLockGuard(SingleResourceLockGuard &&r_value)
      : data(r_value.data) {
    r_value.data = nullptr;
  }
  SingleResourceLockGuard &operator=(SingleResourceLockGuard &&r_value) {
    UnlockOnDestruct{}(*data);
    data = r_value.data;
    r_value.data = nullptr;
    return *this;
  }
  ~SingleResourceLockGuard() {
    if (data != nullptr) {
      UnlockOnDestruct{}(*data);
      data = nullptr;
    }
  }
};

template <typename D, typename Unlock> class InstSingleResourceLockGuard {
public:
  using data_type = D;
  using UnlockOnDestruct = Unlock;

public:
  data_type *data;
  Unlock unlock;
  explicit InstSingleResourceLockGuard(data_type &data, Unlock unlock)
      : data(&data), unlock(unlock) {}
  data_type &operator*() { return *data; }
  InstSingleResourceLockGuard(InstSingleResourceLockGuard &other) = delete;
  InstSingleResourceLockGuard &
  operator=(InstSingleResourceLockGuard &other) = delete;
  InstSingleResourceLockGuard(InstSingleResourceLockGuard &&r_value)
      : data(r_value.data), unlock(r_value.unlock) {
    r_value.data = nullptr;
  }
  void unlock_func() { unlock(*data); }
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
};

template <typename D, template <typename> typename Unlock>
  requires HasUnlockOnDestruct<D>
class InstSingleResourceLockGuardWrapper {
public:
  using data_type = D::data_type;
  using UnlockOnDestruct = Unlock<typename D::UnlockOnDestruct>;

public:
  data_type *data;
  struct {
    UnlockOnDestruct unlock;
    typename D::UnlockOnDestruct prev_unlock;
    void operator()(data_type &d) { unlock(d, prev_unlock); }
  } unlock;
  explicit InstSingleResourceLockGuardWrapper(D &&r_value,
                                              UnlockOnDestruct unlock)
      : data(r_value.data), unlock({.unlock = std::move(unlock),
                                    .prev_unlock = std::move(r_value.unlock)}) {
    r_value.data = nullptr;
  }
  data_type &operator*() { return *data; }
  InstSingleResourceLockGuardWrapper(
      InstSingleResourceLockGuardWrapper &other) = delete;
  InstSingleResourceLockGuardWrapper &
  operator=(InstSingleResourceLockGuardWrapper &other) = delete;
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
};
} // namespace engine::common::memory::guards
