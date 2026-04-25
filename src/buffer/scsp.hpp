#pragma once

#include "../allocators/one_time_static_allocator.hpp"
#include "generics.hpp"
#include "layouts/lockless_ring_buffer_layout.hpp"
#include "ring/simple_approach.hpp"
#include <optional>
namespace engine::buffer::scsp::Factory {
template <template <typename> typename ConsumerType, typename Layout>
  requires roles::IsSingleInstanceConsumer<ConsumerType<Layout>>
class ConsumerFactory {
public:
  using Consumer = ConsumerType<Layout>;

private:
  bool created; ///< Tracking flag to enforce singleton-style access per buffer.
  Layout mem_layout; ///< The shared memory state.

public:
  /** @brief Constructs the factory copy of the shared layout. */
  ConsumerFactory(Layout mem_layout) : created(false), mem_layout(mem_layout) {}
  std::optional<Consumer> create() {
    if (created)
      return std::nullopt;
    created = true;
    return Consumer(mem_layout);
  }
};

template <template <typename> typename ProducerType, typename Layout>
  requires roles::IsSingleInstanceProducer<ProducerType<Layout>>
class ProducerFactory {
public:
  using Producer = ProducerType<Layout>;

private:
  bool created; ///< Tracking flag to enforce singleton-style access per buffer.
  Layout mem_layout; ///< The shared memory state.

public:
  ProducerFactory(Layout mem_layout) : created(false), mem_layout(mem_layout) {}

  std::optional<Producer> create() {
    if (created)
      return std::nullopt;
    created = true;
    return Producer(mem_layout);
  }
};
} // namespace engine::buffer::scsp::Factory
