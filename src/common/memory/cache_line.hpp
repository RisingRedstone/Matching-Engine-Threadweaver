#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace engine::memory {

constexpr std::size_t cache_line_size =
#ifdef __cpp_lib_hardware_interference_size
    std::hardware_destructive_interference_size;
#else
    64; // fallback
#endif

template <class D, size_t HeaderSize> consteval size_t calculate_buffer_fit() {
  return (cache_line_size -
          (alignof(D) + ((HeaderSize / alignof(D)) * alignof(D)))) /
         sizeof(D);
}
// This needs to be cache_line_size variable, for now lets just hardcode to 64
template <class D, class Header = uint8_t>
struct alignas(cache_line_size) CacheLineSlot {
  static_assert(sizeof(D) < cache_line_size,
                "Type D is too large for a single cache line atom.");
  static constexpr size_t max_elems = calculate_buffer_fit<D, sizeof(Header)>();
  alignas(D) Header counter;
  D data[max_elems];
};
template <class D, class Header = uint8_t>
  requires std::is_trivially_copyable_v<D>
union alignas(cache_line_size) CacheLinePacked {
  using raw_type = std::array<uint64_t, cache_line_size / sizeof(uint64_t)>;
  using slot_type = CacheLineSlot<D, Header>;
  raw_type raw;
  slot_type atom;
  static_assert(sizeof(slot_type) == 64, "Alignment logic failed!");

  CacheLinePacked(const CacheLinePacked &other) noexcept {
    this->raw = other.raw;
  }
  CacheLinePacked(CacheLinePacked &&r_value) noexcept {
    this->raw = r_value.raw;
  }
  CacheLinePacked &operator=(const CacheLinePacked &other) noexcept {
    if (this != &other) {
      this->raw = other.raw;
    }
    return *this;
  }
  CacheLinePacked &operator=(CacheLinePacked &&other) noexcept {
    if (this != &other) {
      this->raw = other.raw;
    }
    return *this;
  }
  CacheLinePacked() : raw{} {}
};

} // namespace engine::memory
