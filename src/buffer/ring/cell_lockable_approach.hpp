#pragma once

/**
 * @file cell_lockable_approach.hpp
 * @brief Logic for a cell-lockable ring buffer where synchronization occurs at the individual element level.
 */

#include "../../common/generic.hpp"
#include "../generics.hpp"
#include <atomic>
#include <optional>

/**
 * @namespace engine::buffer::ring::cell_lockable_approach
 * @brief Defines the ProducerConsumer unified class for the cell lockable approach.
 */
namespace engine::buffer::ring::cell_lockable_approach {

/**
 * @brief Validates the layout for the Cell-Lockable protocol.
 * Requires atomic write and read heads, and consistent indexing types.
 */
template <typename Layout>
concept LayoutVerification = requires(Layout l) {
  typename Layout::MemLayout;
  typename Layout::data_type;
  typename Layout::index_type;
  typename Layout::MemLayout::index_type_a;

  Layout::size;
  requires Layout::size > 0;
  requires std::convertible_to<decltype(Layout::size), size_t>;

  requires std::same_as<typename Layout::MemLayout::index_type_a,
                        std::atomic<typename Layout::index_type>>;
  {
    l.get_write_head()
  } -> std::same_as<typename Layout::MemLayout::index_type_a &>;
  {
    l.get_read_head()
  } -> std::same_as<typename Layout::MemLayout::index_type_a &>;
};

/** @brief Tag type for the Cell-Lockable synchronization protocol. */
struct CellLockableApproachProtocol;

/**
 * @brief A unified Producer-Consumer logic utilizing cell-level RAII guards.
 * * This approach allows multiple producers to write simultaneously to different cells
 * without a central "commit" bottleneck.
 */
template <typename LayoutType>
  requires LayoutVerification<LayoutType> &&
           concepts::ReadLockableIndex<LayoutType> &&
           concepts::WriteLockableIndex<LayoutType>
class ProducerConsumer {
public:
  using Protocol = CellLockableApproachProtocol;
  using Layout = LayoutType;
  using ConsumerInstanceType = roles::SingleInstance;
  using ProducerInstanceType = roles::MultipleInstance;

  using data_type = Layout::data_type;
  using index_type = Layout::index_type;
  using index_type_a = Layout::MemLayout::index_type_a;
  using reader_guard = Layout::index_reader_guard;
  using writer_guard = Layout::index_writer_guard;

private:
  Layout mem_layout;
  std::optional<index_type> cache_w_h;

public:
  ProducerConsumer(Layout mem_layout)
      : mem_layout(mem_layout), cache_w_h(std::nullopt) {}
  // delete the copy constructor.
  // only the move constructo should be allowed
  ProducerConsumer(ProducerConsumer &other) = delete;
  ProducerConsumer &operator=(ProducerConsumer &other) = delete;
  ProducerConsumer(ProducerConsumer &&r_value) = default;
  ProducerConsumer &operator=(ProducerConsumer &&r_value) = default;

  /**
   * @brief Reads data from the ring buffer.
   * @details Claims the current read head, attempts to acquire the cell's read lock, 
   * and advances the head regardless of cell content (cleaning up if empty).
   */
  std::optional<data_type> read() {
    index_type_a &read_head = mem_layout.get_read_head();
    index_type_a &write_head = mem_layout.get_write_head();

    index_type r_h = read_head.load(std::memory_order_acquire);
    index_type w_h = write_head.load(std::memory_order_acquire);
    if (r_h >= w_h) {
      return {};
    }

    // Try locking now
    auto l_g_opt = mem_layout.try_read_lock(r_h);
    if (l_g_opt.has_value()) [[likely]] {
      data_type output = **l_g_opt;
      l_g_opt.reset(); // lock dropped here
      read_head.fetch_add(1, std::memory_order_acq_rel);
      return output;
    }

    read_head.fetch_add(1, std::memory_order_acq_rel);
    return std::nullopt;
  }

  template <typename U> struct ReaderDataGuard {
    index_type_a &read_head;
    void operator()(data_type &d, U prev_lock) {
      prev_lock(d);
      read_head.fetch_add(1, std::memory_order_acq_rel);
    }
  };
  using reader_data_guard =
      common::memory::guards::InstSingleResourceLockGuardWrapper<
          typename Layout::index_reader_guard, ReaderDataGuard>;

  /**
   * @brief Returns a RAII pattern Data Guard that can be used to read claimed data from the ring_buffer.
   * @details Claims the current read head, attempts to acquire the cell's read lock, 
   * and advances the head regardless of cell content (cleaning up if empty). If the cell is acquired, 
   * a Data Lock is created and then returned.
   */
  std::optional<reader_data_guard> read_lock() {
    index_type_a &read_head = mem_layout.get_read_head();
    index_type_a &write_head = mem_layout.get_write_head();

    index_type r_h = read_head.load(std::memory_order_acquire);
    index_type w_h = write_head.load(std::memory_order_acquire);
    if (r_h >= w_h) {
      return std::nullopt;
    }

    // Try locking now
    auto l_g_opt = mem_layout.try_read_lock(r_h);
    if (l_g_opt.has_value()) [[likely]] {
      reader_data_guard output(std::move(l_g_opt.value()),
                               {.read_head = read_head});
      return std::move(output);
    }

    read_head.fetch_add(1, std::memory_order_acq_rel);
    return std::nullopt;
  }

  // I could write a write_lock... but why bother?

  /**
   * @brief Writes data to the ring buffer.
   * @details Claims a write index via atomic increment. If the buffer is full, 
   * it reverts the increment. Otherwise, it spins/retries until the cell is locked.
   */
  bool write(const data_type &item) {
    index_type_a &read_head = mem_layout.get_read_head();
    index_type_a &write_head = mem_layout.get_write_head();

    while (true) {
      index_type w_h;
      if (cache_w_h.has_value()) {
        w_h = cache_w_h.value();
        cache_w_h = std::nullopt;
      } else {
        w_h = write_head.fetch_add(1, std::memory_order_acq_rel);
      }
      index_type r_h = read_head.load(std::memory_order_acquire);

      if (w_h >= Layout::size + r_h) {
        cache_w_h = w_h;
        return false;
      }

      {
        auto l_g_opt = mem_layout.try_write_lock(w_h);
        if (!l_g_opt.has_value()) {
          continue;
        }
        **l_g_opt = item;
      }

      // fetch min here for read_head
      index_type val = w_h - 1;
      index_type expected = read_head.load(std::memory_order_relaxed);
      while (val < expected && !read_head.compare_exchange_weak(
                                   expected, val, std::memory_order_release,
                                   std::memory_order_relaxed))
        ;

      return true;
    }
  }
};

} // namespace engine::buffer::ring::cell_lockable_approach
