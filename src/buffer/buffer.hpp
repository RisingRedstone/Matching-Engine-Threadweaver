#pragma once
/**
 * @file buffer.hpp
 * @brief General-purpose buffer orchestrator for various producer-consumer
 * configurations.
 */
#include <concepts>
#include <optional>

/**
 * @namespace engine::buffer
 * @brief Provides classes and interfaces for buffers.
 * */
namespace engine::buffer {
/**
 * @brief Ensures that a Producer, Consumer, and Layout all share the same
 * Protocol and Memory Layout types.
 * @tparam P Producer type.
 * @tparam C Consumer type.
 * @tparam L Memory Layout type.
 */
template <typename P, typename C, typename L>
concept CompatiblePairAndLayout = requires {
  typename P::Protocol;
  typename C::Protocol;
  typename P::Layout;
  typename C::Layout;
  requires std::same_as<typename P::Protocol, typename C::Protocol>;
  requires std::same_as<typename P::Layout, typename C::Layout>;
  requires std::same_as<typename P::Layout, L>;
};

/**
 * @brief Validates that a Factory can produce valid Consumer objects for a
 * specific Layout.
 */
template <template <typename> typename F, typename L>
concept VerifyConsumerFactory = requires(F<L> f, L l) {
  typename F<L>::Consumer;
  { F(l) } -> std::same_as<F<L>>;
  { f.create() } -> std::same_as<std::optional<typename F<L>::Consumer>>;
};

/**
 * @brief Validates that a Factory can produce valid Consumer objects for a
 * specific Layout.
 */
template <template <typename> typename F, typename L>
concept VerifyProducerFactory = requires(F<L> f, L l) {
  typename F<L>::Producer;
  { F(l) } -> std::same_as<F<L>>;
  { f.create() } -> std::same_as<std::optional<typename F<L>::Producer>>;
};

// clang-format off
/**
 * @class Buffer
 * @brief A generic container that manages a memory layout and provides factories for producers and consumers.
 * * This class uses the "Policy" design pattern. The actual behavior (SCMP, MCSP, etc.) is determined 
 * by the @p ProducerFactory and @p ConsumerFactory passed as template arguments.
 * @tparam ProducerFactory Template class responsible for creating Producers.
 * @tparam ConsumerFactory Template class responsible for creating Consumers.
 * @tparam Layout The underlying memory structure (e.g., RingBuffer, LinkedQueue).
 */
// clang-format on
template <template <typename> typename ProducerFactory,
          template <typename> typename ConsumerFactory, typename Layout>
  requires CompatiblePairAndLayout<typename ProducerFactory<Layout>::Producer,
                                   typename ConsumerFactory<Layout>::Consumer,
                                   Layout> &&
           VerifyConsumerFactory<ConsumerFactory, Layout> &&
           VerifyProducerFactory<ProducerFactory, Layout>
class Buffer {
  using MemoryLayout = Layout;
  using ProducerCreator = ProducerFactory<MemoryLayout>;
  using ConsumerCreator = ConsumerFactory<MemoryLayout>;

private:
  // clang-format off
  MemoryLayout mem_layout; ///< The shared memory state (e.g., the actual data < array/atomics).
  ProducerCreator prod;    ///< Factory for generating producer accessors.
  ConsumerCreator cons;    ///< Factory for generating consumer accessors.
  // clang-format on

public:
  /** @brief Deleted copy constructor to prevent unintended duplication of the
   * buffer state. */
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&) = default;
  Buffer &operator=(Buffer &&) = default;

  /** @brief Initializes the layout and hooks the factories to that specific
   * memory instance. */
  Buffer() : mem_layout(), prod(mem_layout), cons(mem_layout) {}

  /**
   * @brief Attempts to create a new Consumer.
   * @return std::optional containing the Consumer if successful (e.g., if the
   * factory allows more).
   */
  std::optional<typename ConsumerCreator::Consumer> create_consumer() {
    return cons.create();
  }
  /**
   * @brief Attempts to create a new Producer.
   * @return std::optional containing the Producer if successful.
   */
  std::optional<typename ProducerCreator::Producer> create_producer() {
    return prod.create();
  }
  ~Buffer() = default;
};
} // namespace engine::buffer

#ifdef LSP_DIAGNOSTICS_ENABLED
#include "../allocators/one_time_static_allocator.hpp"
#include "layouts/lockless_ring_buffer_layout.hpp"
#include "scmp.hpp"
template <typename Layout>
using MyProducer = engine::buffer::scmp::Factory::ProducerFactory<
    engine::buffer::ring::three_pointer_approach::Producer, Layout>;

template <typename Layout>
using MyConsumer = engine::buffer::scmp::Factory::ConsumerFactory<
    engine::buffer::ring::three_pointer_approach::Consumer, Layout>;
template class engine::buffer::Buffer<
    MyProducer, MyConsumer,
    engine::buffer::layout::StaticLockLessRingBufferLayout<
        int, 1024, engine::allocators::OneTimeStaticSharedMemoryAllocator>>;
#endif
