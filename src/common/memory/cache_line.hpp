#pragma once

/**
 * @file cache_line.hpp
 * @brief Hardware-aware memory containers aligned to CPU cache lines to prevent
 * false sharing.
 * */

#include "../generic.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

/**
 * @namespace engine::memory
 * @brief Memory management utilities for high-performance engine operations.
 */
namespace engine::memory {

/**
 * @brief The target hardware cache line size.
 * * Uses C++17 hardware interference size if available; otherwise defaults to
 * 64 bytes.
 */
#ifdef __cpp_lib_hardware_interference_size
constexpr std::size_t cache_line_size =
    std::hardware_destructive_interference_size;
#else
constexpr std::size_t cache_line_size = 64; // fallback
#endif

/**
 * @brief Calculates how many elements of type D can fit into a single cache
 * line taking into account the size of the Header.
 * * Takes into account the header size and the required alignment of the data
 * type.
 * @tparam D The data type to be stored.
 * @tparam HeaderSize The size of the header/counter prefix in bytes.
 * @return constexpr size_t The number of elements that fit in the remaining
 * space.
 */
template <class D, size_t HeaderSize> consteval size_t calculate_buffer_fit() {
  return (cache_line_size -
          (alignof(D) + ((HeaderSize / alignof(D)) * alignof(D)))) /
         sizeof(D);
}

/**
 * @brief A structure that packs a header and a data array into a single cache
 * line.
 * * This struct ensures that the total size and alignment match the CPU cache
 * line to prevent false sharing and optimize fetch performance.
 * @tparam D The type of data elements to store.
 * @tparam Header The type used for the counter or metadata header. Defaults to
 * uint8_t.
 */
template <class D, class Header = uint8_t>
struct alignas(cache_line_size) CacheLineSlot {
  static_assert(sizeof(D) < cache_line_size,
                "Type D is too large for a single cache line atom.");
  static constexpr size_t max_elems = calculate_buffer_fit<D, sizeof(Header)>();
  alignas(D) Header counter;
  D data[max_elems];
};

/**
 * @brief A cache-aligned union allowing raw block access or structured slot
 * access.
 * * This structure is constrained to trivially copyable types to ensure that
 * raw memory copies (via the `raw` array) are safe and valid.
 * @tparam D The data type to be stored. Must be trivially copyable.
 * @tparam Header The header type for the slot.
 */
template <class D, class Header = uint8_t>
union alignas(cache_line_size) CacheLinePacked {
  /** @brief Raw representation of the cache line for fast copying. */
  using raw_type = std::array<uint64_t, cache_line_size / sizeof(uint64_t)>;

  /** @brief Structured representation of the cache line. */
  using slot_type = CacheLineSlot<D, Header>;

  static constexpr size_t max_elems = slot_type::max_elems;

  raw_type raw;   /**< Access as a raw array of 64-bit integers. */
  slot_type atom; /**< Access as a structured slot with header and data. */
  static_assert(sizeof(slot_type) == 64, "Alignment logic failed!");

  /** @brief Default constructor. Initializes raw memory to zero. */
  CacheLinePacked() : raw{} {}

  CacheLinePacked(const raw_type &data) noexcept : raw(data) {}
  /** @brief Copy constructor using raw memory assignment. */
  CacheLinePacked(const CacheLinePacked &other) noexcept {
    this->raw = other.raw;
  }
  /** @brief Move constructor. Performs a raw memory copy. */
  CacheLinePacked(CacheLinePacked &&r_value) noexcept {
    this->raw = r_value.raw;
  }
  /** @brief Copy assignment operator. */
  CacheLinePacked &operator=(const CacheLinePacked &other) noexcept {
    if (this != &other) {
      this->raw = other.raw;
    }
    return *this;
  }
  /** @brief Move assignment operator. */
  CacheLinePacked &operator=(CacheLinePacked &&other) noexcept {
    if (this != &other) {
      this->raw = other.raw;
    }
    return *this;
  }

  Header &get_length() { return atom.counter; }
  D &operator[](int i) { return atom.data[i]; }
};

struct LengthHeader {
  uint8_t header;
  uint8_t length;
};

template <class D>
struct alignas(cache_line_size) CacheLineUint8LengthHeaderPacked {
  using header_type = uint8_t;
  using length_type = uint8_t;
  using index_type = uint8_t;
  using data_type = D;
  using cache_type = CacheLinePacked<D, LengthHeader>;
  static constexpr size_t max_elems = cache_type::slot_type::max_elems;
  alignas(cache_line_size) cache_type cache_line;
  CacheLineUint8LengthHeaderPacked() : cache_line{} {}
  CacheLineUint8LengthHeaderPacked(
      const CacheLineUint8LengthHeaderPacked &other) {
    header_type temp = cache_line.atom.counter.header;
    cache_line = other.cache_line;
    cache_line.atom.counter.header = temp;
  }
  CacheLineUint8LengthHeaderPacked(
      CacheLineUint8LengthHeaderPacked &&) noexcept = default;

  CacheLineUint8LengthHeaderPacked &
  operator=(const CacheLineUint8LengthHeaderPacked &other) {
    header_type temp = cache_line.atom.counter.header;
    cache_line = other.cache_line;
    cache_line.atom.counter.header = temp;
    return *this;
  }
  CacheLineUint8LengthHeaderPacked &
  operator=(CacheLineUint8LengthHeaderPacked &&) noexcept = default;
  D &operator[](int i) { return cache_line.atom.data[i]; }
  header_type &header() { return cache_line.atom.counter.header; }
  bool is_data_present() { return cache_line.atom.counter.length > 0; }
  length_type &get_length() { return cache_line.atom.counter.length; }
  static bool try_lock(CacheLineUint8LengthHeaderPacked &c) {
    return !common::custom_test_and_set(&c.header(), 0);
  }
  static void unlock(CacheLineUint8LengthHeaderPacked &c) {
    std::atomic_ref<header_type>(c.header())
        .store(0, std::memory_order_release);
  }
  static void clear_data(CacheLineUint8LengthHeaderPacked &c) {
    std::atomic_ref<length_type>(c.get_length())
        .store(0, std::memory_order_release);
  }
  static bool contains_data(CacheLineUint8LengthHeaderPacked &c) {
    return std::atomic_ref<length_type>(c.get_length())
               .load(std::memory_order_acquire) > 0;
  }
};

template <typename D> union alignas(cache_line_size) PackedCacheLine {
  using raw_type = std::array<uint64_t, cache_line_size / sizeof(uint64_t)>;
  using atom_type = std::array<D, cache_line_size / sizeof(D)>;
  using index_type = uint8_t;
  static constexpr size_t max_elems = cache_line_size / sizeof(D);
  raw_type raw;
  atom_type atom;

  PackedCacheLine() : raw{} {}
  PackedCacheLine(const PackedCacheLine &) = default;
  PackedCacheLine &operator=(const PackedCacheLine &) = default;
  PackedCacheLine(PackedCacheLine &&) = default;
  PackedCacheLine &operator=(PackedCacheLine &&) = default;
  D &operator[](const uint8_t &index) { return atom[index]; }
};

template <class D> struct alignas(cache_line_size) CacheAlignedHeaderLine {
  alignas(cache_line_size) LengthHeader header;
  PackedCacheLine<D> data;

  using index_type = PackedCacheLine<D>::index_type;
  using data_type = D;
  using header_type = uint8_t;
  using length_type = uint8_t;
  static const header_type HEADER_NOT_LOCKED = 0;
  static const header_type HEADER_LOCKED = 1;
  static constexpr size_t max_elems = PackedCacheLine<D>::max_elems;

  CacheAlignedHeaderLine() : header{}, data{} {}
  CacheAlignedHeaderLine(const CacheAlignedHeaderLine &other) {
    data = other.data;
    header.length = other.header.length;
  }
  CacheAlignedHeaderLine &operator=(const CacheAlignedHeaderLine &other) {
    data = other.data;
    header.length = other.header.length;
    return *this;
  }
  CacheAlignedHeaderLine(CacheAlignedHeaderLine &&) = default;
  CacheAlignedHeaderLine &operator=(CacheAlignedHeaderLine &&) = default;
  data_type &operator[](const index_type &i) { return data[i]; }
  length_type &get_length() { return header.length; }
  static bool try_lock(CacheAlignedHeaderLine &c) {
    // just do a cmpxchange here, apply bitset later. TODO
    auto atomic_ref = std::atomic_ref<header_type>(c.header.header);
    header_type v = HEADER_NOT_LOCKED;
    atomic_ref.compare_exchange_strong(
        v, HEADER_LOCKED, std::memory_order_acq_rel, std::memory_order_relaxed);
    return v == HEADER_NOT_LOCKED; // if it was header_locked, then the lock has
                                   // failed
  }
  static void unlock(CacheAlignedHeaderLine &c) {
    // just do a cmpxchange here, apply bitset later. TODO
    auto atomic_ref = std::atomic_ref<header_type>(c.header.header);
    atomic_ref.store(HEADER_NOT_LOCKED, std::memory_order_release);
  }
  static void clear_data(CacheAlignedHeaderLine &c) {
    auto atomic_ref = std::atomic_ref<length_type>(c.header.length);
    atomic_ref.store(0, std::memory_order_release);
  }
  static bool contains_data(CacheAlignedHeaderLine &c) {
    auto atomic_ref = std::atomic_ref<length_type>(c.header.length);
    return atomic_ref.load(std::memory_order_acquire) > 0;
  }
};

// Testing for LSP
// template class CacheLineUint8LengthHeaderPacked<int>;

} // namespace engine::memory

// #include "../concepts/generic.hpp"
// static_assert(
//     engine::concepts::LockableCell<engine::memory::CacheAlignedHeaderLine<int>>,
//     "Not true");
