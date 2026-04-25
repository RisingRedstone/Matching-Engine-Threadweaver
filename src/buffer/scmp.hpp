#pragma once
/**
 * @file scmp.hpp
 * @brief Factory implementations for Single-Consumer Multiple-Producer (SCMP)
 * buffers.
 */
#include "generics.hpp"
#include <optional>
/**
 * @namespace engine::buffer::scmp::Factory
 * @brief Factories that enforce SCMP constraints during object creation.
 */
namespace engine::buffer::scmp::Factory {
/**
 * @brief Factory that ensures only one Consumer instance is created.
 * * This implements the "Single-Consumer" part of SCMP. Subsequent calls to
 * @ref create() after the first successful call will return @c std::nullopt.
 * * @tparam ConsumerType The template class of the Consumer to instantiate.
 * @tparam Layout The memory layout type shared between producer and consumer.
 */
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
  /**
   * @brief Attempts to create the single allowed Consumer.
   * @return std::optional<Consumer> The consumer instance on success, or nullopt if already created.
   */
  std::optional<Consumer> create() {
    if (created)
      return std::nullopt;
    created = true;
    return Consumer(mem_layout);
  }
};

/**
 * @brief Factory that allows the creation of an unlimited number of Producers.
 * * This implements the "Multiple-Producer" part of SCMP.
 * * @tparam ProducerType The template class of the Producer to instantiate.
 * @tparam Layout The memory layout type shared between producer and consumer.
 */
template <template <typename> typename ProducerType, typename Layout>
  requires roles::IsMultipleInstanceProducer<ProducerType<Layout>>
class ProducerFactory {
public:
  using Producer = ProducerType<Layout>;

private:
  Layout mem_layout; ///< The shared memory state.

public:
  /** @brief Constructs the factory with the shared layout. */
  ProducerFactory(Layout mem_layout) : mem_layout(mem_layout) {}

  /**
   * @brief Creates a new Producer instance.
   * @return std::optional<Producer> Always returns a valid Producer instance.
   */
  std::optional<Producer> create() { return Producer(mem_layout); }
};
} // namespace engine::buffer::scmp::Factory

// #include "ring/three_pointer_approach.hpp"
// template class engine::buffer::scmp::Factory::ConsumerFactory<
//     engine::buffer::ring::three_pointer_approach::Consumer,
//     engine::buffer::layout::StaticLockLessRingBufferLayout<
//         int, 1024, engine::allocators::OneTimeStaticSharedMemoryAllocator>>;
// template class engine::buffer::scmp::Factory::ProducerFactory<
//     engine::buffer::ring::three_pointer_approach::Producer,
//     engine::buffer::layout::StaticLockLessRingBufferLayout<
//         int, 1024, engine::allocators::OneTimeStaticSharedMemoryAllocator>>;
