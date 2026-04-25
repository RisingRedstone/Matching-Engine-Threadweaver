#pragma once

/**
 * @file three_pointer_approach.hpp
 * @brief Logic for a lock-free MPSC ring buffer using a three-pointer synchronization protocol.
 */

#include "../generics.hpp"
#include <atomic>
#include <concepts>
#include <optional>

/**
 * @namespace engine::buffer::ring::three_pointer_approach
 * @brief Defines the Consumer and Producer classes for the three pointer lock less ring buffer approach.
 */
namespace engine::buffer::ring::three_pointer_approach {
// Add a bunch of constraints for the layout so we know what values it must have

/**
 * @brief Validates that a Layout provides the necessary atomic heads and indexing for the 3-pointer protocol.
 */
template <typename Layout>
concept LayoutVerification = requires(Layout l, typename Layout::index_type i) {
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
  {
    l.get_commit_head()
  } -> std::same_as<typename Layout::MemLayout::index_type_a &>;
  { l[i] } -> std::same_as<typename Layout::data_type &>;
};

/** @brief Tag type for the Three-Pointer Approach protocol. */
struct ThreePointerApproachProtocol;

/**
 * @brief Single-threaded consumer for the 3-pointer ring buffer.
 */
template <typename LayoutType>
  requires LayoutVerification<LayoutType>
class Consumer {
public:
  using Protocol = ThreePointerApproachProtocol;
  using Layout = LayoutType;
  using ConsumerInstanceType = roles::SingleInstance;
  using ProducerInstanceType = roles::MultipleInstance;

  using data_type = Layout::data_type;
  using index_type = Layout::index_type;

private:
  Layout mem_layout;

public:
  Consumer(Layout mem_layout) : mem_layout(mem_layout) {}
  // delete the copy constructor.
  // only the move constructo should be allowed
  Consumer(Consumer &other) = delete;
  Consumer &operator=(Consumer &other) = delete;
  Consumer(Consumer &&r_value) = default;
  Consumer &operator=(Consumer &&r_value) = default;

  /**
   * @brief Attempts to read the next available item.
   * @details Synchronizes with the Producer's commit via memory_order_acquire.
   */
  std::optional<data_type> read() {
    index_type r_h = mem_layout.get_read_head().load(std::memory_order_relaxed);
    index_type c_h =
        mem_layout.get_commit_head().load(std::memory_order_acquire);
    if (c_h <= r_h)
      return std::nullopt;

    data_type output = mem_layout[r_h];
    mem_layout.get_read_head().fetch_add(1, std::memory_order_release);
    return output;
  }
};

/**
 * @brief Thread-safe Producer supporting multiple concurrent writers.
 */
template <typename LayoutType>
  requires LayoutVerification<LayoutType>
class Producer {
public:
  using Protocol = ThreePointerApproachProtocol;
  using Layout = LayoutType;

  using data_type = Layout::data_type;
  using index_type = Layout::index_type;
  using index_type_a = Layout::MemLayout::index_type_a;

private:
  Layout mem_layout;

public:
  Producer(Layout mem_layout) : mem_layout(mem_layout) {}
  // delete the copy constructor.
  // only the move constructo should be allowed
  Producer(Producer &other) = delete;
  Producer &operator=(Producer &other) = delete;
  Producer(Producer &&r_value) = default;
  Producer &operator=(Producer &&r_value) = default;
  /**
   * @brief Atomically claims a slot and commits data.
   * @return true if write succeeded, false if the buffer was full.
   */
  bool write(const data_type &item) {
    index_type_a &read_head = mem_layout.get_read_head();
    index_type_a &write_head = mem_layout.get_write_head();
    index_type_a &commit_head = mem_layout.get_commit_head();
    index_type w_h;
    do {
      index_type r_h = read_head.load(std::memory_order_acquire);
      w_h = write_head.load(std::memory_order_acquire);

      if (w_h - r_h >= mem_layout.size)
        return false;
    } while (!write_head.compare_exchange_weak(w_h, w_h + 1,
                                               std::memory_order_release));

    mem_layout[w_h] = item;
    // The commit_head needs to move only AFTER the array has been written to.
    index_type expected = w_h;
    while (!commit_head.compare_exchange_weak(expected, w_h + 1,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
      expected = w_h;
    }

    return true;
  }
};
} // namespace engine::buffer::ring::three_pointer_approach
