// #pragma once

#define TRACING_ENABLED 0
#if TRACING_ENABLED == 1
#include "ring_buffer_tp.hpp"
#endif

#include "common/memory/cache_line.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <iostream>
#include <optional>
#include <ostream>
#include <sys/mman.h>
#include <type_traits>

/** @brief This concept restricts the size_t to be a power of 2.*/
template <size_t N>
concept PowerOfTwo = N > 0 && (N & (N - 1)) == 0;

#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline inline
#else
#define FORCE_INLINE inline
#endif

union RingBufferLineHeaderStructParsed {
  // I could use this one but I think this might require a couple extra clock
  // cycles to parse and stuff so lets skip this for now.
  // Maybe make a written bit. a bit set to indicate that there is some data
  // written.
  uint8_t raw_data;

  static constexpr uint8_t LOCK_BIT = 0x80;
  static constexpr uint8_t NO_VALUE = 0xFF;
  static constexpr uint8_t LENGTH_MASK = 0x7F;
  static constexpr uint8_t RESET = 0x00;

  [[nodiscard]] bool try_lock() {
    std::atomic_ref<uint8_t> atomic_raw(raw_data);
    uint8_t prev = atomic_raw.fetch_or(LOCK_BIT, std::memory_order_acq_rel);
    if (prev & LOCK_BIT)
      return false;
    if (prev > 0) {
      // There is data already present so reset lock bit and keep moving
      atomic_raw.fetch_and(LENGTH_MASK, std::memory_order_release);
      return false;
    }
    return true;
  }
  void unlock() {
    std::atomic_ref<uint8_t> atomic_raw(raw_data);
    atomic_raw.fetch_and(LENGTH_MASK, std::memory_order_release);
  }
  uint8_t get_length() const { return raw_data & LENGTH_MASK; }
  void set_length(uint8_t length) {
    raw_data = (raw_data & ~LENGTH_MASK) | (length & LENGTH_MASK);
  }
  bool get_data_lock() const { return !!(raw_data & LOCK_BIT); }
  void set_lock_bit_lock() { raw_data |= LOCK_BIT; }
  void set_lock_bit_unlock() { raw_data &= ~LOCK_BIT; }
  [[nodiscard]] FORCE_INLINE uint8_t try_read_lock() {
    std::atomic_ref<uint8_t> atomic_raw(raw_data);
    uint8_t curr = atomic_raw.load(std::memory_order_relaxed);
    if (curr & LOCK_BIT) {
      return NO_VALUE;
    }
    uint8_t val = atomic_raw.fetch_or(LOCK_BIT, std::memory_order_acq_rel);
    return (!(val & LOCK_BIT)) ? val : NO_VALUE;
  }
  void atomic_store(uint8_t r) {
    std::atomic_ref<uint8_t> atomic_raw(raw_data);
    atomic_raw.store(r, std::memory_order_release);
  }
};

// inline std::ostream &operator<<(std::ostream &out,
//                                 RingBufferLineHeaderStructParsed &r) {
//   if (r.get_data_lock()) {
//     out << r.get_length() << " Values Locked";
//   } else {
//     out << r.get_length() << " Values Unlocked";
//   }
//   return out;
// }

struct RingBufferLineHeader {
  uint8_t length;
  bool is_data_present() const { return length > 0; }
  void set_data_empty() { this->length = 0; }
  RingBufferLineHeader() = default;
  RingBufferLineHeader(const RingBufferLineHeader &Other) = default;
  const uint8_t get_length() const { return length; }
};

// I could do that complicated class that takes memory and the read/write
// implementation as separate things but I think that would be too much for
// now. I should do that after this one.

template <class Tptr, class Uarr, size_t U_size>
  requires PowerOfTwo<U_size>
struct RingBufferMemAllocation {
  using atomic_T = std::atomic<Tptr>;
  alignas(64) atomic_T read_head;
  alignas(64) atomic_T write_head;
  Uarr data[U_size];
};

template <class Tptr, class Udata, size_t U_size>
  requires PowerOfTwo<U_size>
class LockLessRingBufferLowCASRead {
  using atomic_T = std::atomic<Tptr>;
  using Uarr =
      engine::memory::CacheLinePacked<Udata, RingBufferLineHeaderStructParsed>;
  using U_ptr = Uarr *;

private:
  atomic_T *read_head;  ///< The read atomic pointer.
  atomic_T *write_head; ///< The write atomic pointer.
  U_ptr array; ///< The array where the elements in the ring buffer are stored
  size_t size = U_size; ///< Stores the size of the array (Although I should
                        ///< remove this as U_size is enough)

public:
  explicit LockLessRingBufferLowCASRead(atomic_T *read_head,
                                        atomic_T *write_head, U_ptr array)
      : read_head(read_head), write_head(write_head), array(array) {}

  LockLessRingBufferLowCASRead() = delete;

  LockLessRingBufferLowCASRead(LockLessRingBufferLowCASRead &&r_buffer) {
    read_head = std::move(r_buffer.read_head);
    write_head = std::move(r_buffer.write_head);
    array = std::move(r_buffer.array);
    size = r_buffer.size;
    r_buffer.size = 0;
  }

  void prefetch_with_offset(int location) {
    __builtin_prefetch(&array[location & (size - 1)], 0, 3);
  }

  std::optional<Uarr> read() {
    int tid = pthread_self();
    // TO BE IMPLEMENTED
    // This one is may give whats in the buffer. it may not. just because there
    // is data in the buffer doesn't mean this one will fetch it I leave it to
    // the user to define if they want to loop over until data is reached or
    // not.
    Tptr r_h = read_head->load(std::memory_order_acquire);
    Tptr w_h = write_head->load(std::memory_order_acquire);
    if (w_h <= r_h) {
      // std::cout << "Reader: Slow down baby" << std::endl;
      return {};
    }

    size_t target_idx = r_h & (size - 1);
#if TRACING_ENABLED == 1
    tracepoint(ring_buffer_logic, read_attempt, target_idx, -1, tid,
               "claiming_ticket");
#endif

    // here replace the value with your empty thing.
    // First we need to see if the data is locked
    Uarr *array_loc = &array[target_idx];
    while ((*array_loc).atom.counter.try_read_lock() !=
           RingBufferLineHeaderStructParsed::NO_VALUE) {
      // UPDATE HERE: I used to just return null whenever this happened, but now
      // I feel like doing this is better.
      // return {};
      _mm_pause();
    }

    // Now we know that we locked it :)
    Uarr output = *array_loc;

    (*array_loc)
        .atom.counter.atomic_store(RingBufferLineHeaderStructParsed::RESET);
    read_head->fetch_add(1, std::memory_order_acq_rel);

    // remove the read lock from the data dawg
    // output.atom.counter.parse_data.data_lock = 0;
    if (output.atom.counter.get_length()) {
#if TRACING_ENABLED == 1
      tracepoint(ring_buffer_logic, read_attempt, target_idx,
                 array_loc->atom.data[0], tid, "reading_data");
#endif

      output.atom.counter.set_lock_bit_unlock();
      return output;
    }
    return {};
  }
};

template <class Tptr, class Udata, size_t U_size>
  requires PowerOfTwo<U_size>
class LockLessRingBufferLowCASWrite {
  using atomic_T = std::atomic<Tptr>;
  using Uarr =
      engine::memory::CacheLinePacked<Udata, RingBufferLineHeaderStructParsed>;
  using U_ptr = Uarr *;

private:
  atomic_T *read_head;  ///< The read atomic pointer.
  atomic_T *write_head; ///< The write atomic pointer.
  U_ptr array; ///< The array where the elements in the ring buffer are stored
  size_t size = U_size;

public:
  explicit LockLessRingBufferLowCASWrite(atomic_T *read_head,
                                         atomic_T *write_head, U_ptr array)
      : read_head(read_head), write_head(write_head), array(array) {}
  LockLessRingBufferLowCASWrite() = delete;

  LockLessRingBufferLowCASWrite(LockLessRingBufferLowCASWrite &&r_buffer) {
    read_head = std::move(r_buffer.read_head);
    write_head = std::move(r_buffer.write_head);
    array = std::move(r_buffer.array);
    size = r_buffer.size;
    r_buffer.size = 0;
  }

  bool write(Uarr &item) {
    int tid = (int)pthread_self();
    item.atom.counter.set_lock_bit_lock();
    while (true) {
      Tptr r_h = read_head->load(std::memory_order_acquire);
      Tptr w_h = write_head->fetch_add(1, std::memory_order_acq_rel);

      // Both w_h and r_h are unsigned so if r_h is ever bigger than w_h, then
      // w_h-r_h would always be greater than size
      if (w_h >= size + r_h) {
        write_head->fetch_sub(1, std::memory_order_release);
        return false;
      }

      // Tracing stuff
      size_t target_idx = w_h & (size - 1);
      int first_data = item.atom.data[0];
#if TRACING_ENABLED == 1
      tracepoint(ring_buffer_logic, write_attempt, target_idx, first_data, tid,
                 "claiming_ticket");
#endif

      Uarr *array_loc = &array[target_idx];
      if (!(*array_loc).atom.counter.try_lock()) {
        // BRUVVVV just removing the line below increased the efficiency by a
        // lot.... I think I might know why. it doesn't wait for the current
        // array to be read out any longer.
        // write_head->fetch_sub(1, std::memory_order_release);
#if TRACING_ENABLED == 1
        tracepoint(ring_buffer_logic, write_attempt, target_idx, first_data,
                   tid, "lock_failed");
#endif
        continue;
      }
#if TRACING_ENABLED == 1
      tracepoint(ring_buffer_logic, write_attempt, target_idx, first_data, tid,
                 "writing_data");
#endif

      // Successfully locked that bihh
      *array_loc = item;
      // (*array_loc).atom.counter.unlock();
      // item.atom.counter.parse_data.data_lock = 0;

      item.atom.counter.set_lock_bit_unlock();
      (*array_loc).atom.counter.atomic_store(item.atom.counter.raw_data);

      // I dont think fetch_min exists but I'll try anyways
      // read_head->fetch_min(w_h - 1, std::memory_order_release);
      // Well I give up, apparently there is not a function for min yet

      Tptr val = w_h - 1;
      Tptr expected = read_head->load(std::memory_order_relaxed);
      while (val < expected && !read_head->compare_exchange_weak(
                                   expected, val, std::memory_order_release,
                                   std::memory_order_relaxed))
        ;

      return true;
    }
  }
};

template <class Tptr, class Udata, size_t U_size>
  requires PowerOfTwo<U_size>
class LockLessRingBufferLowCAS {
  using atomic_T = std::atomic<Tptr>;
  using Uarr =
      engine::memory::CacheLinePacked<Udata, RingBufferLineHeaderStructParsed>;
  using U_ptr = Uarr *;
  using R_Buff = RingBufferMemAllocation<Tptr, Uarr, U_size>;

public:
  using Uarr_t =
      engine::memory::CacheLinePacked<Udata, RingBufferLineHeaderStructParsed>;

private:
  atomic_T *read_head;  ///< The read atomic pointer.
  atomic_T *write_head; ///< The write atomic pointer.
  U_ptr array; ///< The array where the elements in the ring buffer are stored.
  size_t size = U_size;
  void *mem_ptr;
  mutable bool reader_done; ///< Keeps track of whether a consumer was created.

  LockLessRingBufferLowCAS(atomic_T *read_head, atomic_T *write_head,
                           U_ptr array, void *mem_ptr)
      : read_head(read_head), write_head(write_head), array(array),
        mem_ptr(mem_ptr), reader_done(false) {
    // Start at 1 so the first write can do a fetch_min(read_head, write_head-1
    // (which is 0 and not -1))
    read_head->store(1, std::memory_order_release);
    write_head->store(1, std::memory_order_release);
  }

public:
  LockLessRingBufferLowCAS(LockLessRingBufferLowCAS &) = delete;

  LockLessRingBufferLowCAS(LockLessRingBufferLowCAS &&r_value) {
    read_head = r_value.read_head;
    write_head = r_value.write_head;
    array = r_value.array;
    mem_ptr = r_value.mem_ptr;
    reader_done = r_value.reader_done;

    r_value.read_head = nullptr;
    r_value.write_head = nullptr;
    r_value.array = nullptr;
    r_value.mem_ptr = nullptr;
  }

  static std::optional<LockLessRingBufferLowCAS> create() {
    void *mem_ptr = mmap(NULL, sizeof(R_Buff), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANON, -1, 0);
    if (mem_ptr == MAP_FAILED) {
      return {};
    }
    R_Buff *temp = reinterpret_cast<R_Buff *>(mem_ptr);
    LockLessRingBufferLowCAS r_buffer(&(temp->read_head), &(temp->write_head),
                                      temp->data, mem_ptr);
    return r_buffer;
  }
  std::optional<LockLessRingBufferLowCASRead<Tptr, Udata, U_size>>
  create_consumer() {
    if (reader_done) {
      std::cout << "Reader already done" << std::endl;
      return {};
    }
    reader_done = true;
    return LockLessRingBufferLowCASRead<Tptr, Udata, U_size>(read_head,
                                                             write_head, array);
  }
  LockLessRingBufferLowCASWrite<Tptr, Udata, U_size> create_producer() {
    return LockLessRingBufferLowCASWrite<Tptr, Udata, U_size>(
        read_head, write_head, array);
  }
  static LockLessRingBufferLowCAS *print_instance;
  static void display_memory(int sig) {
    std::cout << "Read Head: "
              << print_instance->read_head->load(std::memory_order_acquire)
              << std::endl
              << "Write Head: "
              << print_instance->write_head->load(std::memory_order_acquire)
              << std::endl;
    // for (int i = 0; i < print_instance->size; i++) {
    //   uint8_t length = print_instance->array[i].atom.counter.get_length();
    //   if (length == 0)
    //     continue;
    //   std::cout << print_instance->array[i].atom.counter;
    //   for (int i = 0; i < length; i++) {
    //     std::cout << print_instance->array[i].atom.data[i] << ", ";
    //   }
    //   std::cout << std::endl;
    // }
  }
  ~LockLessRingBufferLowCAS() {
    if (mem_ptr != nullptr)
      munmap(mem_ptr, sizeof(R_Buff));
  }
};

template <typename T, typename RT, size_t S>
  requires PowerOfTwo<S>
LockLessRingBufferLowCAS<T, RT, S>
    *LockLessRingBufferLowCAS<T, RT, S>::print_instance = nullptr;
