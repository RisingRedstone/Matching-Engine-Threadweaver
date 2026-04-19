#pragma once

/**
 * @file cache_line.hpp
 * @brief Hardware-aware memory containers aligned to CPU cache lines to prevent
 * false sharing.
 * */

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

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

template <class D> struct CacheLineUint8LengthHeaderPacked {
  using header_type = uint8_t;
  using length_type = uint8_t;
  using cache_type = CacheLinePacked<D, LengthHeader>;
  static constexpr size_t max_elems = cache_type::slot_type::max_elems;
  cache_type cache_line;
  CacheLineUint8LengthHeaderPacked() : cache_line{} {}
  CacheLineUint8LengthHeaderPacked(const CacheLineUint8LengthHeaderPacked &) =
      default;
  CacheLineUint8LengthHeaderPacked(
      CacheLineUint8LengthHeaderPacked &&) noexcept = default;

  CacheLineUint8LengthHeaderPacked &
  operator=(const CacheLineUint8LengthHeaderPacked &) = default;
  CacheLineUint8LengthHeaderPacked &
  operator=(CacheLineUint8LengthHeaderPacked &&) noexcept = default;
  D &operator[](int i) { return cache_line.atom.data[i]; }
  header_type &header() { return cache_line.atom.counter.header; }
  bool is_data_present() { return cache_line.atom.counter.length > 0; }
  length_type &get_length() { return cache_line.atom.counter.length; }
};

// Testing for LSP
// template class CacheLineUint8LengthHeaderPacked<int>;

} // namespace engine::memory
