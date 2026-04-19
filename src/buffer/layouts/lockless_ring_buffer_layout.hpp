
#include "../../common/concepts/generic.hpp"
#include <iostream>
#include <memory>
namespace engine::buffer::layout {

template <typename Udata, size_t size, typename Uindex = unsigned long long>
struct StaticLockLessRingBufferMemoryLayout {
  using index_type_a = std::atomic<Uindex>;
  alignas(64) index_type_a write_head;
  alignas(64) index_type_a read_head;
  alignas(64) index_type_a commit_head;
  Udata array[size];
};
template <typename Udata, size_t size_value,
          template <typename> typename Allocator,
          typename Uindex = unsigned long long>
  requires concepts::PowerOfTwo<size_value>
class StaticLockLessRingBufferLayout {
  // require an allocator trait maybe?
public:
  static const size_t size = size_value;
  using data_type = Udata;
  using index_type = Uindex;
  using MemLayout =
      StaticLockLessRingBufferMemoryLayout<data_type, size, index_type>;
  using Alloc = Allocator<MemLayout>;
  using AllocTrait = std::allocator_traits<Alloc>;

  // Nah you need to really think about how you wanna structure this.
  // If this class owns the memory then it will have to be destoyed when this
  // class destroys to prevent mem leaks, but that would mean that I won't be
  // able to copy the constructor. Maybe I can.. use an atomic reference counter
  // so on each copy it increments the counter and on each delete it decrements
  // it. Arc is already made so that should be simple enough to do. But I would
  // still have to solve the problem of different virt addresses.
  // The solution here is to also ahve a registry at the start of the allocated
  // pages and stuff and at that point, you're just creating a custom allocator,
  // just try to find one and stick to it. use Mimalloc, tcmalloc or jemalloc
private:
  // store the allocator
  Alloc alloc;
  MemLayout *layout;

public:
  StaticLockLessRingBufferLayout()
      : alloc(), layout(AllocTrait::allocate(alloc, 1)) {}
  StaticLockLessRingBufferLayout(StaticLockLessRingBufferLayout &other) {
    alloc = other.alloc;
    layout = other.layout;
  }
  StaticLockLessRingBufferLayout &
  operator=(StaticLockLessRingBufferLayout &other) {
    alloc = other.alloc;
    layout = other.layout;
    return *this;
  }
  StaticLockLessRingBufferLayout(StaticLockLessRingBufferLayout &&r_value)
      : alloc(std::move(r_value.alloc)), layout(r_value.layout) {
    r_value.layout = nullptr;
  }
  StaticLockLessRingBufferLayout &
  operator=(StaticLockLessRingBufferLayout &&r_value) {
    alloc = std::move(r_value.alloc);
    layout = r_value.layout;
    r_value.layout = nullptr;
    return *this;
  }
  MemLayout::index_type_a &get_write_head() const {
    // Here if layout is nullptr that is your fault bruh
    return layout->write_head;
  }
  MemLayout::index_type_a &get_read_head() const { return layout->read_head; }
  MemLayout::index_type_a &get_commit_head() const {
    return layout->commit_head;
  }
  const data_type &operator[](const index_type &index) const {
    return layout->array[index & (size - 1)];
  }
  data_type &operator[](const index_type &index) {
    return layout->array[index & (size - 1)];
  }
  ~StaticLockLessRingBufferLayout() {
    AllocTrait::deallocate(alloc, layout, 1);
    layout = nullptr;
  }
};

} // namespace engine::buffer::layout
