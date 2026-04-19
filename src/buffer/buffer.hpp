
#include <concepts>
#include <optional>
namespace engine::buffer {
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
template <template <typename> typename F, typename L>
concept VerifyConsumerFactory = requires(F<L> f, L l) {
  typename F<L>::Consumer;
  { F(l) } -> std::same_as<F<L>>;
  { f.create() } -> std::same_as<std::optional<typename F<L>::Consumer>>;
};
template <template <typename> typename F, typename L>
concept VerifyProducerFactory = requires(F<L> f, L l) {
  typename F<L>::Producer;
  { F(l) } -> std::same_as<F<L>>;
  { f.create() } -> std::same_as<std::optional<typename F<L>::Producer>>;
};
// Maybe check if the consumer is a consumer and the same with producer.
// Check if the consumer and producer factories give out the right response.
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
  MemoryLayout mem_layout;
  ProducerCreator prod;
  ConsumerCreator cons;

public:
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&) = default;
  Buffer &operator=(Buffer &&) = default;
  Buffer() : mem_layout(), prod(mem_layout), cons(mem_layout) {}
  std::optional<typename ConsumerCreator::Consumer> create_consumer() {
    return cons.create();
  }
  std::optional<typename ProducerCreator::Producer> create_producer() {
    return prod.create();
  }
  ~Buffer() {
    // Call destructors for prod, cons and etc.
    // They are called automatically
  }
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
