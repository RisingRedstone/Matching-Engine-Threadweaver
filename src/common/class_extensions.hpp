#pragma once

/**
 * @file class_extensions.hpp
 * @brief Mixins introduced here to extend functionality of classes.
 * */

#include "../common/concepts/generic.hpp"
#include <emmintrin.h>

/**
 * @namespace engine::common::class_ext
 * @brief Provides Mixin classes to extend the functionality of Lockable
 * Layouts.
 */
namespace engine::common::class_ext {
/**
 * @brief Extension that adds a blocking read-lock to a ReadLockableIndex
 * layout.
 * * Instead of returning an optional, @ref read_lock will spin until the lock
 * is acquired, using the `_mm_pause` instruction for CPU efficiency.
 * @tparam T A class satisfying the ReadLockableIndex concept.
 */
template <class T>
  requires concepts::ReadLockableIndex<T>
class ReadLockLoopExtension : public T {
  using T::T;

  /**
   * @brief Spin-waits until a read lock is acquired for index @p i.
   * @param i The index to lock.
   * @return The RAII reader guard.
   */
  T::index_reader_guard read_lock(T::index_type i) {
    while (true) {
      if (auto res = this->try_read_lock(i)) {
        return std::move(*res);
      }
      _mm_pause();
    }
  }
};

/**
 * @brief Extension that adds a blocking write-lock to a WriteLockableIndex
 * layout.
 * * Inherits from @ref WriteLockableIndex and provides a blocking @ref
 * write_lock.
 * @tparam T A class satisfying the WriteLockableIndex concept.
 */
template <class T>
  requires concepts::WriteLockableIndex<T>
class WriteLockLoopExtension : public T {
  using T::T;

  /**
   * @brief Spin-waits until a write lock is acquired for index @p i.
   * @param i The index to lock.
   * @return The RAII writer guard.
   */
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
