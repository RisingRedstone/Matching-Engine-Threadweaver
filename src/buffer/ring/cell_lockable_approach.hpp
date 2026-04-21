#pragma once

#include "../../common/concepts/generic.hpp"
#include <atomic>

namespace engine::buffer::ring::cell_lockable_approach {

// clang-format off
template <typename Layout>
concept LayoutVerification = requires(
        Layout l
) {
  typename Layout::MemLayout;
  typename Layout::data_type;
  typename Layout::index_type;
  typename Layout::MemLayout::index_type_a;

  Layout::size;
  requires Layout::size > 0;
  requires std::convertible_to<decltype(Layout::size), size_t>;

  requires std::same_as<typename Layout::MemLayout::index_type_a, std::atomic<typename Layout::index_type>>;
  { l.get_write_head() } -> std::same_as<typename Layout::MemLayout::index_type_a &>;
  { l.get_read_head() } -> std::same_as<typename Layout::MemLayout::index_type_a &>;
};
// clang-format on

struct CellLockableApproachProtocol;

template <typename LayoutType>
  requires LayoutVerification<LayoutType> &&
           concepts::ReadLockableIndex<LayoutType> &&
           concepts::WriteLockableIndex<LayoutType>
class ProducerConsumer {
public:
  using Protocol = CellLockableApproachProtocol;
  using Layout = LayoutType;

  using data_type = Layout::data_type;
  using index_type = Layout::index_type;
  using index_type_a = Layout::MemLayout::index_type_a;
  using reader_guard = Layout::index_reader_guard;
  using writer_guard = Layout::index_writer_guard;

private:
  Layout mem_layout;

public:
  ProducerConsumer(Layout mem_layout) : mem_layout(mem_layout) {}
  // delete the copy constructor.
  // only the move constructo should be allowed
  ProducerConsumer(ProducerConsumer &other) = delete;
  ProducerConsumer &operator=(ProducerConsumer &other) = delete;
  ProducerConsumer(ProducerConsumer &&r_value) = default;
  ProducerConsumer &operator=(ProducerConsumer &&r_value) = default;

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
    std::optional<data_type> output = std::nullopt;
    if (l_g_opt.has_value()) {
      auto l_g = std::move(*l_g_opt);
      output = *l_g;
    } // lock dropped here

    read_head.fetch_add(1, std::memory_order_acq_rel);
    return output;
  }

  bool write(const data_type &item) {
    index_type_a &read_head = mem_layout.get_read_head();
    index_type_a &write_head = mem_layout.get_write_head();

    while (true) {
      index_type w_h = write_head.fetch_add(1, std::memory_order_acq_rel);
      index_type r_h = read_head.load(std::memory_order_acquire);

      if (w_h >= Layout::size + r_h) {
        write_head.fetch_sub(1, std::memory_order_acq_rel);
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
                                   expected, val, std::memory_order_acq_rel,
                                   std::memory_order_relaxed))
        ;

      return true;
    }
  }
};

} // namespace engine::buffer::ring::cell_lockable_approach
