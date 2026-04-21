#pragma once

#include "../common/concepts/generic.hpp"
#include <emmintrin.h>
namespace engine::common::class_ext {
template <class T>
  requires concepts::ReadLockableIndex<T>
class ReadLockLoopExtension : public T {
  T::index_reader_guard read_lock(T::index_type i) {
    while (true) {
      if (auto res = this->try_read_lock(i)) {
        return std::move(*res);
      }
      _mm_pause();
    }
  }
};

template <class T>
  requires concepts::WriteLockableIndex<T>
class WriteLockLoopExtension : public T {
  T::index_writer_guard read_lock(T::index_type i) {
    while (true) {
      if (auto res = this->try_write_lock(i)) {
        return std::move(*res);
      }
      _mm_pause();
    }
  }
};
} // namespace engine::common::class_ext
