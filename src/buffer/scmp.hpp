
#include <optional>
namespace engine::buffer::scmp::Factory {
template <template <typename> typename ConsumerType, typename Layout>
class ConsumerFactory {
public:
  using Consumer = ConsumerType<Layout>;

private:
  bool created;
  Layout mem_layout;

public:
  ConsumerFactory(Layout mem_layout) : created(false), mem_layout(mem_layout) {}
  std::optional<Consumer> create() {
    if (created)
      return std::nullopt;
    created = true;
    return Consumer(mem_layout);
  }
};
template <template <typename> typename ProducerType, typename Layout>
class ProducerFactory {
public:
  using Producer = ProducerType<Layout>;

private:
  Layout mem_layout;

public:
  ProducerFactory(Layout mem_layout) : mem_layout(mem_layout) {}
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
