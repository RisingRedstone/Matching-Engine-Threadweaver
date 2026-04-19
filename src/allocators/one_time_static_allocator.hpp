
#include <atomic>
#include <optional>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

namespace engine::allocators {
template <typename T> struct OneTimeStaticSharedMemoryAllocatorChunk {
  std::atomic<int> arc;
  T data;
};
template <typename T> class OneTimeStaticSharedMemoryAllocator {
public:
  using value_type = T;
  using size_type = size_t;
  using pointer = T *;

private:
  using Chunk = OneTimeStaticSharedMemoryAllocatorChunk<value_type>;
  bool allocated = false;
  Chunk *chunk;

  //
  pid_t owner_pid;

public:
  // The copy only copies the pointers and maintains an ARC to the
  // memory.
  // The constructor does not create memory
  // The allocator function creates memory
  // The destructor decrememnts the ARC and deletes the mapping
  OneTimeStaticSharedMemoryAllocator()
      : allocated(false), chunk(nullptr), owner_pid(0) {}
  T *allocate(size_t) {
    if (allocated)
      return nullptr;
    void *mem_ptr = mmap(NULL, sizeof(Chunk), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANON, -1, 0);
    if (mem_ptr == MAP_FAILED)
      return nullptr;
    owner_pid = getpid(); // the owner is the current thread that called
    allocated = true;
    chunk = reinterpret_cast<Chunk *>(mem_ptr);
    chunk->arc.store(1, std::memory_order_release);
    return &(chunk->data);
  }
  void deallocate(pointer, size_type) {
    if (chunk != nullptr) {
      if (owner_pid == getpid()) {
        int arcs = chunk->arc.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (arcs <= 0) {
          munmap(chunk, sizeof(Chunk));
        }
      }
      chunk = nullptr;
      allocated = false;
    }
  }
  OneTimeStaticSharedMemoryAllocator(OneTimeStaticSharedMemoryAllocator &other)
      : allocated(other.allocated), chunk(other.chunk), owner_pid(0) {
    if (chunk != nullptr) {
      owner_pid = getpid();
      chunk->arc.fetch_add(1, std::memory_order_acq_rel);
    }
  }
  OneTimeStaticSharedMemoryAllocator &
  operator=(OneTimeStaticSharedMemoryAllocator &other) {
    // check if memory exists, if it does then remove/deallocate it and then
    // move on
    this->deallocate(nullptr, 1);
    // now copy the chunk
    this->allocated = other.allocated;
    this->chunk = other.chunk;
    // add 1 to arc
    if (other.chunk != nullptr) {
      owner_pid = getpid();
      this->chunk->arc.fetch_add(1, std::memory_order_acq_rel);
    }
    return *this;
  }
  OneTimeStaticSharedMemoryAllocator(
      OneTimeStaticSharedMemoryAllocator &&r_value)
      : allocated(r_value.allocated), chunk(r_value.chunk),
        owner_pid(r_value.owner_pid) {
    r_value.allocated = false;
    r_value.chunk = nullptr;
    r_value.owner_pid = 0;
  }
  OneTimeStaticSharedMemoryAllocator &
  operator=(OneTimeStaticSharedMemoryAllocator &&r_value) {
    this->deallocate(nullptr, 1);
    // now copy the chunk
    this->allocated = r_value.allocated;
    this->chunk = r_value.chunk;
    this->owner_pid = r_value.owner_pid;

    // nullptr the chunk in r_value
    r_value.allocated = false;
    r_value.chunk = nullptr;
    r_value.owner_pid = 0;
    return *this;
  }
  std::optional<T *> get() {
    if (chunk == nullptr)
      return {};
    return &(chunk->data);
  }
  ~OneTimeStaticSharedMemoryAllocator() { deallocate(nullptr, 1); }
};
} // namespace engine::allocators

// For lsp purposes
#ifdef LSP_ENABLED
template class engine::allocators::OneTimeStaticSharedMemoryAllocator<int>;
#endif
