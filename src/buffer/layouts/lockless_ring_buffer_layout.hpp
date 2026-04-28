#pragma once

/**
 * @file lockless_ring_buffer_layout.hpp
 * @brief Cache-aligned, lock-free ring buffer memory layouts.
 */

#include "../../common/concepts/generic.hpp"
#include "../../common/generic.hpp"
#include "../../common/memory/guards.hpp"
#include <atomic>
#include <concepts>
#include <emmintrin.h>
#include <functional>
#include <iostream>
#include <memory>
/**
 * @namespace engine::buffer::layout
 * @brief Defines Layouts for the Producer and Consumer classes to use.
 * */
namespace engine::buffer::layout {

/**
 * @struct StaticLockLessRingBufferMemoryLayout
 * @brief Raw memory structure for a standard three-pointer ring buffer.
 * @details Aligned to 64 bytes to prevent false sharing between heads.
 */
template <typename Udata, size_t size, typename Uindex = unsigned long long>
struct StaticLockLessRingBufferMemoryLayout {
  using index_type_a = std::atomic<Uindex>;
  alignas(64) index_type_a write_head;
  alignas(64) index_type_a read_head;
  alignas(64) index_type_a commit_head;
  Udata array[size];
};

/**
 * @class StaticLockLessRingBufferLayout
 * @brief Orchestrator for the standard lock-free ring buffer layout.
 * @tparam Udata Defines the type of data to store in the buffer
 * @tparam Uindex Defines the array index pointer type that the write_head, read_head and commit_head will store.
 * * Default value is unsigned long long.
 * @tparam size_value Must be a Power of Two for optimized bitwise indexing.
 * @tparam Allocator Must provide an allocator where the @ref StaticLockLessRingBufferMemoryLayout will be allocated
 */
template <typename Udata, size_t size_value,
          template <typename> typename Allocator,
          typename Uindex = unsigned long long>
  requires concepts::PowerOfTwo<size_value>
class StaticLockLessRingBufferLayout {
  // require an allocator trait maybe?
public:
  static const size_t size = size_value;
  using data_type = Udata;
  using index_type = Uindex;
  using MemLayout =
      StaticLockLessRingBufferMemoryLayout<data_type, size, index_type>;
  using Alloc = Allocator<MemLayout>;
  using AllocTrait = std::allocator_traits<Alloc>;

private:
  // store the allocator
  Alloc alloc;
  MemLayout *layout;

public:
  StaticLockLessRingBufferLayout()
      : alloc(), layout(AllocTrait::allocate(alloc, 1)) {}
  // Copying shares the underlying memory (ARC logic handled by Allocator)
  StaticLockLessRingBufferLayout(StaticLockLessRingBufferLayout &other)
      : alloc(other.alloc), layout(other.layout) {}
  StaticLockLessRingBufferLayout &
  operator=(StaticLockLessRingBufferLayout &other) {
    alloc = other.alloc;
    layout = other.layout;
    return *this;
  }
  StaticLockLessRingBufferLayout(StaticLockLessRingBufferLayout &&r_value)
      : alloc(std::move(r_value.alloc)), layout(r_value.layout) {
    r_value.layout = nullptr;
  }
  StaticLockLessRingBufferLayout &
  operator=(StaticLockLessRingBufferLayout &&r_value) {
    alloc = std::move(r_value.alloc);
    layout = r_value.layout;
    r_value.layout = nullptr;
    return *this;
  }
  MemLayout::index_type_a &get_write_head() const {
    // Here if layout is nullptr that is your fault bruh
    return layout->write_head;
  }
  MemLayout::index_type_a &get_read_head() const { return layout->read_head; }
  MemLayout::index_type_a &get_commit_head() const {
    return layout->commit_head;
  }
  const data_type &operator[](const index_type &index) const {
    return layout->array[index & (size - 1)];
  }
  data_type &operator[](const index_type &index) {
    return layout->array[index & (size - 1)];
  }
  ~StaticLockLessRingBufferLayout() {
    AllocTrait::deallocate(alloc, layout, 1);
    layout = nullptr;
  }
};

/**
 * @brief Raw memory structure for a cell-lockable ring buffer.
 * @details Relies on the data cells themselves to manage fine-grained synchronization.
 */
template <typename Udata, size_t size, typename Uindex = unsigned long long>
  requires concepts::LockableCell<Udata>
struct StaticLockLessRingBufferCellLockableMemoryLayout {
  using index_type_a = std::atomic<Uindex>;
  alignas(64) index_type_a write_head;
  alignas(64) index_type_a read_head;
  Udata array[size];
};

/**
 * @brief Layout that satisfies ReadLockableIndex and WriteLockableIndex via LockableCell.
 */
template <typename Udata, size_t size_value,
          template <typename> typename Allocator,
          typename Uindex = unsigned long long>
  requires concepts::PowerOfTwo<size_value> && concepts::LockableCell<Udata>
class StaticLockLessRingBufferCellLockableLayout {
public:
  static const size_t size = size_value;
  using data_type = Udata;
  using data_header_type = Udata::header_type;
  using data_length_type = Udata::length_type;
  using index_type = Uindex;
  using MemLayout =
      StaticLockLessRingBufferCellLockableMemoryLayout<data_type, size,
                                                       index_type>;
  using Alloc = Allocator<MemLayout>;
  using AllocTrait = std::allocator_traits<Alloc>;

private:
  Alloc alloc;
  MemLayout *layout;

  data_type &operator[](index_type i) { return layout->array[i & (size - 1)]; }

public:
  StaticLockLessRingBufferCellLockableLayout()
      : alloc(), layout(AllocTrait::allocate(alloc, 1)) {}
  StaticLockLessRingBufferCellLockableLayout(
      StaticLockLessRingBufferCellLockableLayout &other) {
    alloc = other.alloc;
    layout = other.layout;
  }
  StaticLockLessRingBufferCellLockableLayout &
  operator=(StaticLockLessRingBufferCellLockableLayout &other) {
    alloc = other.alloc;
    layout = other.layout;
    return *this;
  }
  StaticLockLessRingBufferCellLockableLayout(
      StaticLockLessRingBufferCellLockableLayout &&r_value)
      : alloc(std::move(r_value.alloc)), layout(r_value.layout) {
    r_value.layout = nullptr;
  }
  StaticLockLessRingBufferCellLockableLayout &
  operator=(StaticLockLessRingBufferCellLockableLayout &&r_value) {
    alloc = std::move(r_value.alloc);
    layout = r_value.layout;
    r_value.layout = nullptr;
    return *this;
  }
  MemLayout::index_type_a &get_write_head() const {
    // Here if layout is nullptr that is your fault bruh
    return layout->write_head;
  }
  MemLayout::index_type_a &get_read_head() const { return layout->read_head; }

  struct ReaderUnlockStruct {
    void operator()(data_type &d) {
      data_type::clear_data(d);
      data_type::unlock(d);
    }
  };
  struct WriterUnlockStruct {
    void operator()(data_type &d) { data_type::unlock(d); }
  };

  using index_reader_guard =
      memory::guards::InstSingleResourceLockGuard<data_type,
                                                  ReaderUnlockStruct>;
  using index_writer_guard =
      memory::guards::InstSingleResourceLockGuard<data_type,
                                                  WriterUnlockStruct>;
  std::optional<index_reader_guard> try_read_lock(index_type i) {
    // try locking
    while (!data_type::try_lock(this->operator[](i))) {
      CPU_PAUSE();
    }
    index_reader_guard l_g(this->operator[](i), ReaderUnlockStruct{});
    if (!data_type::contains_data(*l_g)) {
      return {};
    }
    return l_g;
  }
  std::optional<index_writer_guard> try_write_lock(index_type i) {
    // try locking
    while (!data_type::try_lock(this->operator[](i))) {
      CPU_PAUSE();
    }
    index_writer_guard l_g(this->operator[](i), WriterUnlockStruct{});
    if (data_type::contains_data(*l_g)) {
      return {};
    }
    return l_g;
  }
};

// #include "../../allocators/one_time_static_allocator.hpp"
// #include "../../common/memory/cache_line.hpp"
// using data = engine::memory::CacheLineUint8LengthHeaderPacked<int>;
// template <typename Layout>
// using Allocator =
//     engine::allocators::OneTimeStaticSharedMemoryAllocator<Layout>;
// template class StaticLockLessRingBufferCellLockableLayout<data, 1024,
//                                                           Allocator>;

} // namespace engine::buffer::layout
//
// #ifdef LSP_ENABLED
// #include "../../allocators/one_time_static_allocator.hpp"
// #include "../../common/memory/cache_line.hpp"
// using data = engine::memory::CacheLineUint8LengthHeaderPacked<int>;
// template <typename Layout>
// using Allocator =
//     engine::allocators::OneTimeStaticSharedMemoryAllocator<Layout>;
// template class engine::buffer::layout::
//     StaticLockLessRingBufferCellLockableLayout<data, 1024, Allocator>;
// #endif
