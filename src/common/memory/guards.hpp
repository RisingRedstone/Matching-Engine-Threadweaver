#pragma once

namespace engine::common::memory::guards {

template <typename D, typename Unlock> class SingleResourceLockGuard {
private:
  D *data;

public:
  explicit SingleResourceLockGuard(D &data) : data(&data) {}
  D &operator*() { return *data; }
  SingleResourceLockGuard(SingleResourceLockGuard &other) = delete;
  SingleResourceLockGuard &operator=(SingleResourceLockGuard &other) = delete;
  SingleResourceLockGuard(SingleResourceLockGuard &&r_value)
      : data(r_value.data) {
    r_value.data = nullptr;
  }
  SingleResourceLockGuard &operator=(SingleResourceLockGuard &&r_value) {
    Unlock{}(*data);
    data = r_value.data;
    r_value.data = nullptr;
    return *this;
  }
  ~SingleResourceLockGuard() {
    if (data != nullptr) {
      Unlock{}(*data);
      data = nullptr;
    }
  }
};
} // namespace engine::common::memory::guards
