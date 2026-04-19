#pragma once
#include <cstdint>
namespace engine::common {
template <typename T> void drop(T &&) {}

inline bool custom_test_and_set(uint8_t *addr, int bit_pos) {
  bool old_bit;
  asm volatile(
      "lock bts %[bit], %[mem]"
      : "=@ccc"(old_bit), [mem] "+m"(*addr) // "=@ccc" catches the Carry Flag
      : [bit] "Ir"(bit_pos)
      : "memory");
  return old_bit;
}

} // namespace engine::common
