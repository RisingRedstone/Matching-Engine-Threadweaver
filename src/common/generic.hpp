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

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
// x86 / x86_64
#if defined(_MSC_VER)
#include <intrin.h>
#define CPU_PAUSE() _mm_pause()
#else
#define CPU_PAUSE() __asm__ __volatile__("pause" ::: "memory")
#endif
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) ||           \
    defined(_M_ARM64)
// ARM / ARM64
#if defined(_MSC_VER)
#include <intrin.h>
#define CPU_PAUSE() __yield()
#else
#define CPU_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#endif
#else
#define CPU_PAUSE() ((void)0)
#endif
} // namespace engine::common
