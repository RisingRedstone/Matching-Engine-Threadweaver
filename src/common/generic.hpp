#pragma once
/**
 * @file generic.hpp
 * @brief Low-level atomic primitives and general-purpose utility functions.
 */
#include <cstdint>
/**
 * @namespace engine::common
 * @brief Common utilities for atomic operations and memory management.
 */
namespace engine::common {
/**
 * @brief Explicitly "drops" a value by taking ownership and doing nothing.
 * * Useful for silencing "unused variable" warnings or explicitly ending
 * the lifetime of a temporary/moved object.
 * @tparam T The type of the object to drop.
 */
template <typename T> void drop(T &&) {}

/**
 * @brief Atomically sets a specific bit in a memory address and returns its
 * previous state.
 * * Uses the x86 `lock bts` (Bit Test and Set) instruction.
 * * This is highly efficient for implementing bit-based spinlocks or bitsets.
 * * @param addr Pointer to the byte containing the bit.
 * @param bit_pos The zero-based index of the bit to set (0-7 for a single
 * byte).
 * @return true If the bit was already set (1) before the operation.
 * @return false If the bit was clear (0) before the operation.
 * * @note This is an inline assembly implementation specific to x86/x64
 * architectures. The `"=@ccc"` constraint directly maps the CPU's Carry Flag
 * (where BTS stores the old bit) to the boolean return value.
 */
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
