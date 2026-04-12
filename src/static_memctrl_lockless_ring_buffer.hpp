
#pragma once

/**
 * @file static_memctrl_lockless_ring_buffer.hpp
 * @brief Provides a static size memory controlled implementation of a Lockless
 * Ring Buffer.
 * */

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "static_lockless_ring_buffer.hpp"

// clang-format off
/**
 * @defgroup Static_Memctrl_Lockless_RingBuffer_Module Static Memory Controlled Lockless Ring Buffer ADT
 * @brief This module is a memory controlled extension of @ref
 * Static_Lockless_RingBuffer_Module. Here ths memory is allocated and managed
 * by @ref LockLessRingBufferMemInit class itself
 *
 * @{
 */
// clang-format on

/**
 * @brief A helper struct to allocate memory pages and memory mapping the
 * allocated space
 * @tparam Tptr The atomic pointer counter to the ring buffer index. (eg,
 * unsigned long long int)
 * @tparam Uarr The container data type. (eg, unsigned long long int, int, some
 * struct)
 * @tparam U_size The size of the lockless ring buffer.
 * @note
 * * The U_size needs to be a power of 2 as defined by @ref PowerOfTwo
 * concept.
 * */
template <class Tptr, class Uarr, size_t U_size>
  requires PowerOfTwo<U_size>
struct RingBufferMemAllocation {
  using atomic_T = std::atomic<Tptr>;
  alignas(64) atomic_T read_head;
  alignas(64) atomic_T write_head;
  alignas(64) atomic_T commit_head;
  Uarr data[U_size];
};

// clang-format off
/**
 * @class LockLessRingBufferMemInit
 * @brief This class defines the LocklessRingBuffer and can be used to get the
 * consumer and producer variants.
 * @tparam Tptr The atomic pointer counter to the ring buffer index. (eg,
 * unsigned long long int)
 * @tparam Uarr The container data type. (eg, unsigned long long int, int, some
 * struct)
 * @tparam U_size The size of the lockless ring buffer.
 * @note
 * * Only one consumer can be created
 * * The U_size needs to be a power of 2 as defined by @ref PowerOfTwo
 * concept.
 *
 * @details
 * ### Usage Example:
 * @code
 * std::optional<LockLessRingBufferMemInit> r_buffer_opt = 
 * LockLessRingBufferMemInit<unsigned long long int, int, 1024>::create();
 * if(!r_buffer_opt.has_value()) {
 *  perror("Consumer buffer could not be created");
 *  return -1;
 * }
 * LockLessRingBufferMemInit r_buffer = r_buffer_opt.value();
 *
 * std::optional<LockLessRingBufferRead> r_buffer_consumer_opt = r_buffer.create_consumer();
 * if(!r_buffer_consumer_opt.has_value()) {
 *  perror("Consumer buffer could not be created");
 *  return -1;
 * }
 * LockLessRingBufferRead r_buffer_consumer = r_buffer_consumer_opt.value();
 * LockLessRingBufferWrite r_buffer_producer_1 = r_buffer.create_producer();
 * LockLessRingBufferWrite r_buffer_producer_2 = r_buffer.create_producer();
 * LockLessRingBufferWrite r_buffer_producer_3 = r_buffer.create_producer();
 * @endcode
 * */
// clang-format on
template <class Tptr, class Uarr, size_t U_size>
  requires PowerOfTwo<U_size>
class LockLessRingBufferMemInit {
  using atomic_T = std::atomic<Tptr>;
  using U_ptr = Uarr *;
  using R_Buff = RingBufferMemAllocation<Tptr, Uarr, U_size>;

private:
  atomic_T *read_head;   ///< The read atomic pointer.
  atomic_T *write_head;  ///< The write atomic pointer.
  atomic_T *commit_head; ///< The commit atomic pointer.
  U_ptr array; ///< The array where the elements in the ring buffer are stored.
  size_t size = U_size;
  void *mem_ptr;
  mutable bool reader_done; ///< Keeps track of whether a consumer was created.

  /**
   * @brief This is the class constructor.
   * @param read_head The atomic pointer to the read_head.
   * @param write_head The atomic pointer to the write_head.
   * @param commit_head The atomic pointer to the commit_head.
   * @param array The pointer to the ring buffer memory.
   * @param mem_ptr The entire memory page that stores the array and the atomic
   *  pointers.
   * @note This is intentionally left private as this class needs a shared
   * memory map and it is recommended to use the @ref create() function instead.
   * */
  LockLessRingBufferMemInit(atomic_T *read_head, atomic_T *write_head,
                            atomic_T *commit_head, U_ptr array, void *mem_ptr)
      : read_head(read_head), write_head(write_head), commit_head(commit_head),
        array(array), mem_ptr(mem_ptr) {
    read_head->store(0, std::memory_order_release);
    write_head->store(0, std::memory_order_release);
    commit_head->store(0, std::memory_order_release);
  }

public:
  /**
   * @brief This class cannot be copied.
   * */
  LockLessRingBufferMemInit(LockLessRingBufferMemInit &) = delete;

  /**
   * @brief Defines the move constructor.
   * */
  LockLessRingBufferMemInit(LockLessRingBufferMemInit &&r_value) {
    read_head = r_value.read_head;
    write_head = r_value.write_head;
    commit_head = r_value.commit_head;
    array = r_value.array;
    mem_ptr = r_value.mem_ptr;

    r_value.read_head = nullptr;
    r_value.write_head = nullptr;
    r_value.commit_head = nullptr;
    r_value.array = nullptr;
    r_value.mem_ptr = nullptr;
  }

  /**
   * @brief Defines the constructor. If memory is not created, this will fail.
   * @return The an std::optional containing LockLessRingBufferMemInit class on
   * success, or std::nullopt on failure.
   * */
  static std::optional<LockLessRingBufferMemInit> create() {
    void *mem_ptr = mmap(NULL, sizeof(R_Buff), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANON, -1, 0);
    if (mem_ptr == MAP_FAILED) {
      return {};
    }
    R_Buff *temp = reinterpret_cast<R_Buff *>(mem_ptr);
    LockLessRingBufferMemInit r_buffer(&(temp->read_head), &(temp->write_head),
                                       &(temp->commit_head), temp->data,
                                       mem_ptr);
    return r_buffer;
  }
  /**
   * @brief Creates the consumer class of the lockless ring buffer if there
   * doesn't already exist one.
   * @return std::optional containing the @ref LockLessRingBufferRead if @ref
   * reader_done is not set, else return std::nullopt
   * */
  std::optional<LockLessRingBufferRead<Tptr, Uarr, U_size>> create_consumer() {
    if (reader_done)
      return {};
    reader_done = true;
    return LockLessRingBufferRead<Tptr, Uarr, U_size>(read_head, commit_head,
                                                      array);
  }
  /**
   * @brief Creates the producer class of the lockless ring buffer
   * @return The @ref LockLessRingBufferWrite class.
   * */
  LockLessRingBufferWrite<Tptr, Uarr, U_size> create_producer() {
    return LockLessRingBufferWrite<Tptr, Uarr, U_size>(read_head, write_head,
                                                       commit_head, array);
  }

  /**
   * @brief Destroys the allocated memory pages at the end of its lifetime.
   * * This is why this class should not be copied.
   * */
  ~LockLessRingBufferMemInit() {
    if (mem_ptr != nullptr)
      munmap(mem_ptr, sizeof(R_Buff));
  }
};
/** @} */ // end of the Static_Memctrl_Lockless_RingBuffer_Module
