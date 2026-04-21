#pragma once

/**
 * @file one_time_static_allocator.hpp
 * @brief A shared-memory allocator with atomic reference counting for cross-process data persistence.
 */

#include <atomic>
#include <optional>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

/**
 * @namespace engine::allocators
 * @brief Defines the Allocator classes to allocate and manage memory.
 * */
namespace engine::allocators {
/**
 * @brief The internal storage structure for the shared memory.
 * @tparam T The data type stored in the shared memory segment.
 */
template <typename T> struct OneTimeStaticSharedMemoryAllocatorChunk {
  std::atomic<int> arc;
  T data;
};

/**
 * @brief A specialized allocator that maps a single chunk of memory using mmap.
 * * This allocator is "One-Time" in that a single instance manages one allocation at a time.
 * * It supports shared ownership: copying the allocator increments the reference count 
 * (ARC) of the underlying memory mapping.
 * @tparam T The type of object to allocate.
 */
template <typename T> class OneTimeStaticSharedMemoryAllocator {
public:
  using value_type = T;
  using size_type = size_t;
  using pointer = T *;

private:
  using Chunk = OneTimeStaticSharedMemoryAllocatorChunk<value_type>;
  bool allocated = false;
  Chunk *chunk;

  pid_t owner_pid; ///< Tracks which process/thread context owns this instance.

public:
  // The copy only copies the pointers and maintains an ARC to the
  // memory.
  // The constructor does not create memory
  // The allocator function creates memory
  // The destructor decrememnts the ARC and deletes the mapping
  /** @brief Default constructor. Does not perform any allocation. */
  OneTimeStaticSharedMemoryAllocator()
      : allocated(false), chunk(nullptr), owner_pid(0) {}

  /**
   * @brief Allocates shared memory via mmap.
   * * Uses MAP_SHARED and MAP_ANON to create a memory segment visible to child processes.
   * @param n Number of elements (ignored as this is a static single-chunk allocator).
   * @return pointer to the allocated memory, or nullptr if allocation fails or already exists.
   */
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

  /**
   * @brief Decrements the ARC and unmaps memory if the count reaches zero.
   * @param p Pointer to deallocate (unused).
   * @param n Size of deallocation (unused).
   */
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

  /** @brief Copy constructor. Increments the ARC of the shared memory. */
  OneTimeStaticSharedMemoryAllocator(OneTimeStaticSharedMemoryAllocator &other)
      : allocated(other.allocated), chunk(other.chunk), owner_pid(0) {
    if (chunk != nullptr) {
      owner_pid = getpid();
      chunk->arc.fetch_add(1, std::memory_order_acq_rel);
    }
  }

  /** @brief Copy assignment. Cleans up current memory and shares the new chunk. */
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

  /** @brief Move constructor. Transfers ownership without affecting the ARC. */
  OneTimeStaticSharedMemoryAllocator(
      OneTimeStaticSharedMemoryAllocator &&r_value)
      : allocated(r_value.allocated), chunk(r_value.chunk),
        owner_pid(r_value.owner_pid) {
    r_value.allocated = false;
    r_value.chunk = nullptr;
    r_value.owner_pid = 0;
  }

  /** @brief Move assignment. */
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

  /** @brief Returns a pointer to the managed data if it exists. */
  std::optional<T *> get() {
    if (chunk == nullptr)
      return {};
    return &(chunk->data);
  }

  /** @brief Destructor ensures memory is deallocated and ARC is managed. */
  ~OneTimeStaticSharedMemoryAllocator() { deallocate(nullptr, 1); }
};
} // namespace engine::allocators

// For lsp purposes
#ifdef LSP_ENABLED
template class engine::allocators::OneTimeStaticSharedMemoryAllocator<int>;
#endif
