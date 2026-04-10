
#pragma once

/**
 * @file static_lockless_ring_buffer.hpp
 * @brief Provides a static size implementation of a Lockless Ring Buffer.
 * */

/**
 * @mainpage Threadweaver: Static Lockless Ring Buffer
 * @section overview Overview
 * A high-performance, Single-Consumer Multi-Producer (SCMP) lockless ring
 * buffer designed for the Threadweaver matching engine. It utilizes C++20
 * concepts and atomics for memory safety and wait-free operations.
 * @section architecture Architecture
 * The system is divided into three distinct roles:
 * - **Init (@ref LockLessRingBufferInit):** The factory class. Manages handle
 * creation and ensures only one consumer exists.
 * - **Read (@ref LockLessRingBufferRead):** The consumer interface. Thread-safe
 * for a single thread.
 * - **Write (@ref LockLessRingBufferWrite):** The producer interface.
 * Thread-safe for multiple concurrent producers.
 * @section memory Memory Management
 * @warning This ADT does **not** manage memory. The caller must provide valid
 * pointers for the three atomic heads and the underlying storage array.
 * @section constraints Constraints
 * - Buffer size must be a **Power of Two** (verified via @ref PowerOfTwo).
 * - Only **one** consumer can be instantiated via the factory.
 * - The Buffer size if fixed during compile time so to define a new buffer with
 * a different size, it needs to be recompiled.
 */

#include <atomic>
#include <cstddef>
#include <optional>

/** @brief This concept restricts the size_t to be a power of 2.*/
template <size_t N>
concept PowerOfTwo = N > 0 && (N & (N - 1)) == 0;

/**
 * @defgroup Static_Lockless_RingBuffer_Module Static Lockless Ring Buffer ADT
 * @brief This module contains all the classes that are used for the @b static
 * version of <b>lockless ring buffer</b>
 * @{
 */

/**
 * @class LockLessRingBufferRead
 * @brief This class defines the @b consumer end of the lockless ring buffer.
 * @tparam Tptr The atomic pointer counter to the ring buffer index. (eg,
 * unsigned long long int)
 * @tparam Uarr The container data type. (eg, unsigned long long int, int, some
 * struct)
 * @tparam U_size The size of the lockless ring buffer.
 * @note The U_size needs to be a power of 2 as defined by @ref PowerOfTwo
 * concept.
 * */
template <class Tptr, class Uarr, size_t U_size>
  requires PowerOfTwo<U_size>
class LockLessRingBufferRead {
  using atomic_T = std::atomic<Tptr>;
  using U_ptr = Uarr *;

private:
  atomic_T *read_head;   ///< The read atomic pointer.
  atomic_T *commit_head; ///< The commit atomic pointer.
  U_ptr array; ///< The array where the elements in the ring buffer are stored
  size_t size = U_size; ///< Stores the size of the array (Although I should
                        ///< remove this as U_size is enough)

public:
  /**
   * @brief Defines the constructor.
   * @param read_head The read atomic pointer.
   * @param commit_head The commit attomic pointer.
   * @param The array pointer to the memory space where the ring buffer will be
   * stored.
   * @return The LocklessRingBufferRead class
   * */
  explicit LockLessRingBufferRead(atomic_T *read_head, atomic_T *commit_head,
                                  U_ptr array)
      : read_head(read_head), commit_head(commit_head), array(array) {}

  /**
   * @brief The default constructor is removed for this class.
   * */
  LockLessRingBufferRead() = delete;

  /**
   * @brief Move constructor.
   * @param r_buffer The r-value for the class that needs to be moved.
   */
  LockLessRingBufferRead(LockLessRingBufferRead &&r_buffer) {
    read_head = std::move(r_buffer.read_head);
    commit_head = std::move(r_buffer.commit_head);
    array = std::move(r_buffer.array);
    size = r_buffer.size;
    r_buffer.size = 0;
  }

  /**
   * @brief Defines the funtion that reads any commited values from the lockless
   * ring buffer.
   * * It checks if the @ref commit_head is ahead of the @ref read_head and if
   * it is, the value is read, and then returned. Or else it returns
   * std::nullopt.
   * @return Returns an std::optional containing the value, or std::nullopt if
   * the buffer is empty.
   */
  std::optional<Uarr> read() {
    Tptr r_h = read_head->load(std::memory_order_relaxed);
    Tptr c_h = commit_head->load(std::memory_order_acquire);
    if (c_h <= r_h)
      return {};
    Uarr output = array[r_h & (size - 1)];
    // The read_head only needs to move AFTER the output has been fetched
    read_head->fetch_add(1, std::memory_order_release);
    return output;
  }
};

/**
 * @class LockLessRingBufferWrite
 * @brief This class defines the @b producer end of the lockless ring buffer.
 * @tparam Tptr The atomic pointer counter to the ring buffer index. (eg,
 * unsigned long long int)
 * @tparam Uarr The container data type. (eg, unsigned long long int, int, some
 * struct)
 * @tparam U_size The size of the lockless ring buffer.
 * @note The U_size needs to be a power of 2 as defined by @ref PowerOfTwo
 * concept.
 * */
template <class Tptr, class Uarr, size_t U_size>
  requires PowerOfTwo<U_size>
class LockLessRingBufferWrite {
  using atomic_T = std::atomic<Tptr>;
  using U_ptr = Uarr *;

private:
  atomic_T *read_head;   ///< The read atomic pointer.
  atomic_T *write_head;  ///< The write atomic pointer.
  atomic_T *commit_head; ///< The commit atomic pointer.
  U_ptr array; ///< The array where the elements in the ring buffer are stored
  size_t size = U_size;

public:
  /**
   * @brief Defines the constructor.
   * @param read_head The read atomic pointer.
   * @param write_head The write atomic pointer.
   * @param commit_head The commit attomic pointer.
   * @param The array pointer to the memory space where the ring buffer will be
   * stored.
   * @return The LocklessRingBufferRead class
   * */
  explicit LockLessRingBufferWrite(atomic_T *read_head, atomic_T *write_head,
                                   atomic_T *commit_head, U_ptr array)
      : read_head(read_head), write_head(write_head), commit_head(commit_head),
        array(array) {}
  /**
   * @brief The default constructor is removed for this class.
   * */
  LockLessRingBufferWrite() = delete;

  /**
   * @brief Move constructor.
   * @param r_buffer The r-value for the class that needs to be moved.
   */
  LockLessRingBufferWrite(LockLessRingBufferWrite &&r_buffer) {
    read_head = std::move(r_buffer.read_head);
    write_head = std::move(r_buffer.write_head);
    commit_head = std::move(r_buffer.commit_head);
    array = std::move(r_buffer.array);
    size = r_buffer.size;
    r_buffer.size = 0;
  }

  /**
   * @brief writes to the lockless ring buffer.
   * * It checks if there is space in the buffer by doing (@ref write_head -
   * @ref read_head) < @ref size
   * * If there is, it acquires a position in the lockless ring buffer by using
   * CMPXCHNG or
   * [compare_exchange_weak](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange.html).
   * Then it writes in the array and increments the @ref commit_head after
   * successful completion.
   * * If there is no space it returns right away.
   * @param item The value to be inserted into the ring buffer.
   * @return True if the insertion was successful, False otherwise (reached max
   * size).
   */
  bool write(Uarr item) {
    Tptr w_h;
    do {
      Tptr r_h = read_head->load(std::memory_order_acquire);
      w_h = write_head->load(std::memory_order_acquire);

      if (w_h - r_h >= size)
        return false;
    } while (!write_head->compare_exchange_weak(w_h, w_h + 1,
                                                std::memory_order_release));

    array[w_h & (size - 1)] = item;
    // The commit_head needs to move only AFTER the array has been written to.
    Tptr expected = w_h;
    while (!commit_head->compare_exchange_weak(expected, w_h + 1,
                                               std::memory_order_release)) {
      expected = w_h;
    }

    return true;
    // Bruh I go why this would also create major problems..
    // Me so stupid
    // This code is stupid and not to be used at all
    // Tptr w_h = write_head->fetch_add(1, std::memory_order_release);
    // Tptr r_h = read_head->load(std::memory_order_acquire);
    //
    // if (w_h - r_h >= size) {
    //   write_head->fetch_sub(1, std::memory_order_release);
    //   return false;
    // }
    //
    // array[w_h & (size - 1)] = item;
    //
    // // commit_head->fetch_add(1, std::memory_order_release);
    // Tptr expected = w_h;
    // while (!commit_head->compare_exchange_weak(expected, w_h + 1,
    //                                            std::memory_order_release)) {
    //   expected = w_h;
    // }
    // return true;
  }
};

// clang-format off
/**
 * @class LockLessRingBufferInit
 * @brief This class defines the LocklessRingBuffer and can be used to get the
 * consumer and producer variants.
 * @tparam Tptr The atomic pointer counter to the ring buffer index. (eg,
 * unsigned long long int)
 * @tparam Uarr The container data type. (eg, unsigned long long int, int, some
 * struct)
 * @tparam U_size The size of the lockless ring buffer.
 * * @note
 * * Only one consumer can be created
 * * Neither this class, nor do any of its children (@ref LockLessRingBufferRead
 * and @ref LockLessRingBufferWrite) take the responsibility of managing the
 * underlying memory that is to be used for the atomics or the buffer. It is the
 * responsibility of the class instantiator to take care of the memory
 * allocation and freeing and to ensure that it is valid for the entire duration
 * of the program.
 * * The U_size needs to be a power of 2 as defined by @ref PowerOfTwo
 * concept.
 *
 * @details
 * ### Usage Example:
 * @code
 * //Allocate the memory
 * LockLessRingBufferInit r_buffer = 
 * LockLessRingBufferInit<unsigned long long int, int, 1024> (read_head, write_head, commit_head, array);
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
class LockLessRingBufferInit {
  using atomic_T = std::atomic<Tptr>;
  using U_ptr = Uarr *;

private:
  atomic_T *read_head;   ///< The read atomic pointer.
  atomic_T *write_head;  ///< The write atomic pointer.
  atomic_T *commit_head; ///< The commit atomic pointer.
  U_ptr array; ///< The array where the elements in the ring buffer are stored.
  size_t size = U_size;
  mutable bool reader_done; ///< Keeps track of whether a conumer was created.

public:
  /**
   * @brief Defines the constructor.
   * @param read_head The read atomic pointer.
   * @param write_head The write atomic pointer.
   * @param commit_head The commit attomic pointer.
   * @param The array pointer to the memory space where the ring buffer will be
   * stored.
   * @return The LocklessRingBufferRead class
   * */
  explicit LockLessRingBufferInit(atomic_T *read_head, atomic_T *write_head,
                                  atomic_T *commit_head, U_ptr array)
      : read_head(read_head), write_head(write_head), commit_head(commit_head),
        array(array) {}
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
  ~LockLessRingBufferInit() {}
};
/** @} */ // end of the Static_Lockless_RingBuffer_Module
