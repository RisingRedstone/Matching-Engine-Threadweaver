#pragma once
#include "../generics.hpp"
#include <atomic>
#include <concepts>
#include <iostream>
#include <optional>

namespace engine::buffer::ring::simple_approach {

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
  { l[i] } -> std::same_as<typename Layout::data_type &>;
};

struct SimpleApproachProtocol;

template <typename LayoutType>
  requires LayoutVerification<LayoutType>
class Consumer {

public:
  using Protocol = SimpleApproachProtocol;
  using Layout = LayoutType;
  using ConsumerInstanceType = roles::SingleInstance;

  using data_type = Layout::data_type;
  using index_type = Layout::index_type;
  using index_type_a = Layout::MemLayout::index_type_a;

private:
  Layout mem_layout;
  index_type r_h_cache;

public:
  Consumer(Layout mem_layout)
      : mem_layout(mem_layout),
        r_h_cache(mem_layout.get_read_head().load(std::memory_order_acquire)) {}
  // delete the copy constructor.
  // only the move constructo should be allowed
  Consumer(Consumer &other) = delete;
  Consumer &operator=(Consumer &other) = delete;
  Consumer(Consumer &&r_value) = default;
  Consumer &operator=(Consumer &&r_value) = default;

  std::optional<data_type> read() {
    index_type_a &read_head = mem_layout.get_read_head();
    index_type_a &write_head = mem_layout.get_write_head();

    index_type w_h = write_head.load(std::memory_order_acquire);

    if (r_h_cache >= w_h) {
      return std::nullopt;
    }

    data_type output = mem_layout[r_h_cache];
    r_h_cache++;
    read_head.store(r_h_cache, std::memory_order_release);
    return output;
  }
};

template <typename LayoutType>
  requires LayoutVerification<LayoutType>
class Producer {

public:
  using Protocol = SimpleApproachProtocol;
  using Layout = LayoutType;
  using ProducerInstanceType = roles::SingleInstance;

  using data_type = Layout::data_type;
  using index_type = Layout::index_type;
  using index_type_a = Layout::MemLayout::index_type_a;

private:
  Layout mem_layout;
  index_type w_h_cache;

public:
  Producer(Layout mem_layout)
      : mem_layout(mem_layout),
        w_h_cache(mem_layout.get_write_head().load(std::memory_order_acquire)) {
  }
  // delete the copy constructor.
  // only the move constructo should be allowed
  Producer(Producer &other) = delete;
  Producer &operator=(Producer &other) = delete;
  Producer(Producer &&r_value) = default;
  Producer &operator=(Producer &&r_value) = default;

  bool write(const data_type &item) {
    index_type_a &read_head = mem_layout.get_read_head();
    index_type_a &write_head = mem_layout.get_write_head();

    index_type r_h = read_head.load(std::memory_order_acquire);
    if (w_h_cache >= Layout::size + r_h) {
      return false;
    }
    mem_layout[w_h_cache] = item;
    w_h_cache++;
    write_head.store(w_h_cache, std::memory_order_release);
    return true;
  }
};

} // namespace engine::buffer::ring::simple_approach
