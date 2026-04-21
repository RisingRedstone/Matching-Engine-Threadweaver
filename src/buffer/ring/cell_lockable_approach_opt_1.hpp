#pragma once
#include "cell_lockable_approach.hpp"
#include <atomic>
#include <emmintrin.h>
namespace engine::buffer::ring::cell_lockable_approack_opt_1 {
struct CellLockableApproachOpt1Protocol;

template <typename LayoutType>
  requires cell_lockable_approach::LayoutVerification<LayoutType> &&
           concepts::ReadLockableIndex<LayoutType> &&
           concepts::WriteLockableIndex<LayoutType>
class ProducerConsumer {
public:
  using Protocol = CellLockableApproachOpt1Protocol;
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

    // // Try locking now
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
      index_type w_h = write_head.load(std::memory_order_acquire);
      index_type r_h = read_head.load(std::memory_order_acquire);
      if (w_h >= Layout::size + r_h) {
        return false;
      }
      {
        auto l_g_opt = mem_layout.try_write_lock(w_h);
        if (!l_g_opt.has_value()) {
          // index_type last_w_h = w_h;
          // while ((w_h = write_head.load(std::memory_order_acquire)) ==
          //        last_w_h) {
          //   _mm_pause();
          // }
          _mm_pause();
          continue;
        }
        write_head.fetch_add(
            1, std::memory_order_relaxed); // the unlocking is
                                           // memory_order_release so..
        **l_g_opt = item;
      }
      return true;
    }
  }
};
} // namespace engine::buffer::ring::cell_lockable_approack_opt_1
