#pragma once

#include "../../common/concepts/generic.hpp"
#include "../../common/generic.hpp"
#include <atomic>
#include <concepts>
#include <emmintrin.h>
#include <functional>
#include <iostream>
#include <memory>
namespace engine::buffer::layout {

template <typename Udata, size_t size, typename Uindex = unsigned long long>
struct StaticLockLessRingBufferMemoryLayout {
  using index_type_a = std::atomic<Uindex>;
  alignas(64) index_type_a write_head;
  alignas(64) index_type_a read_head;
  alignas(64) index_type_a commit_head;
  Udata array[size];
};
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

  // Nah you need to really think about how you wanna structure this.
  // If this class owns the memory then it will have to be destoyed when this
  // class destroys to prevent mem leaks, but that would mean that I won't be
  // able to copy the constructor. Maybe I can.. use an atomic reference counter
  // so on each copy it increments the counter and on each delete it decrements
  // it. Arc is already made so that should be simple enough to do. But I would
  // still have to solve the problem of different virt addresses.
  // The solution here is to also ahve a registry at the start of the allocated
  // pages and stuff and at that point, you're just creating a custom allocator,
  // just try to find one and stick to it. use Mimalloc, tcmalloc or jemalloc
private:
  // store the allocator
  Alloc alloc;
  MemLayout *layout;

public:
  StaticLockLessRingBufferLayout()
      : alloc(), layout(AllocTrait::allocate(alloc, 1)) {}
  StaticLockLessRingBufferLayout(StaticLockLessRingBufferLayout &other) {
    alloc = other.alloc;
    layout = other.layout;
  }
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

// not used anymore. got replaced by concepts::LockableCell
// template <typename Udata>
// concept HasHeader = requires(Udata d) {
//   typename Udata::header_type;
//   typename Udata::length_type;
//   requires concepts::IsStandardUint<typename Udata::header_type>;
//   requires concepts::IsStandardUint<typename Udata::length_type>;
//   { d.header() } -> std::same_as<typename Udata::header_type &>;
//   { d.get_length() } -> std::same_as<typename Udata::length_type &>;
//   { d.is_data_present() } -> std::same_as<bool>;
// };
// writing a new type here.

template <typename Udata, size_t size, typename Uindex = unsigned long long>
  requires concepts::LockableCell<Udata>
struct StaticLockLessRingBufferCellLockableMemoryLayout {
  using index_type_a = std::atomic<Uindex>;
  alignas(64) index_type_a write_head;
  alignas(64) index_type_a read_head;
  Udata array[size];
};

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
  template <bool reader> class LockGuard {
  private:
    data_type *data;

  public:
    LockGuard(data_type *data) : data(data) {}
    LockGuard(LockGuard &) = delete;
    LockGuard &operator=(LockGuard &) = delete;
    void unlock() {
      if (data == nullptr)
        return;
      data_type::unlock(*data);
      if (reader)
        data_type::clear_data(*data);
    }
    LockGuard(LockGuard &&r_value) : data(r_value.data) {
      r_value.data = nullptr;
    }
    LockGuard &operator=(LockGuard &&r_value) {
      unlock();
      data = r_value.data;
      r_value.data = nullptr;
    }

    data_type &operator*() { return *data; }

    ~LockGuard() {
      // unlock here
      unlock();
    }
  };
  using index_reader_guard = LockGuard<true>;
  using index_writer_guard = LockGuard<false>;
  std::optional<index_reader_guard> try_read_lock(index_type i) {
    // try locking
    while (!data_type::try_lock(this->operator[](i))) {
      _mm_pause();
    }
    auto l_g = index_reader_guard(&(this->operator[](i)));
    if (!data_type::contains_data(this->operator[](i))) {
      return {};
    }
    return std::move(l_g);
  }
  std::optional<index_writer_guard> try_write_lock(index_type i) {
    // try locking
    while (!data_type::try_lock(this->operator[](i))) {
      _mm_pause();
    }
    auto l_g = index_writer_guard(&(this->operator[](i)));
    if (data_type::contains_data(this->operator[](i))) {
      return {};
    }
    return std::move(l_g);
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

// #ifdef LSP_ENABLED
// #include "../../allocators/one_time_static_allocator.hpp"
// #include "../../common/memory/cache_line.hpp"
// using data = engine::memory::CacheLineUint8LengthHeaderPacked<int>;
// template <typename Layout>
// using Allocator =
//     engine::allocators::OneTimeStaticSharedMemoryAllocator<Layout>;
// template class StaticLockLessRingBufferCellLockableLayout<data, 1024,
//                                                           Allocator>;
// #endif

} // namespace engine::buffer::layout
