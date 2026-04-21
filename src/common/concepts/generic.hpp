#pragma once

/**
 * @file generic.hpp
 * @brief Provides general purpose concepts.
 * */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * @namespace engine::concepts
 * @brief Namespace containing C++20 concepts for template constraints within
 * the engine.
 */
namespace engine::concepts {

/**
 * @brief Ensures a value is a power of two at compile-time.
 * * Used primarily for memory alignment and efficient indexing (e.g., bitwise
 * modulo).
 * * @tparam N The size_t value to check.
 */
template <std::size_t N>
concept PowerOfTwo = (N > 0) && (N & (N - 1)) == 0;

/**
 * @brief Restricts a type to standard unsigned integer types.
 * * Specifically: uint8_t, uint16_t, uint32_t, or uint64_t.
 */
template <typename T>
concept IsStandardUint =
    std::same_as<T, uint8_t> || std::same_as<T, uint16_t> ||
    std::same_as<T, uint32_t> || std::same_as<T, uint64_t>;

// clang-format off
/**
 * @brief Defines a layout that supports shared-access (read) locking on specific indices.
 * * A ReadLockableIndex layout must provide:
 * - Nested @c index_type and @c index_reader_guard types.
 * - A RAII @c index_reader_guard that is movable but not copyable.
 * - A @c try_read_lock(i) method returning an @c std::optional guard.
 * * @tparam Layout The layout type to validate.
 */
template <typename Layout>
concept ReadLockableIndex = requires(Layout l, typename Layout::index_type i, typename Layout::index_reader_guard l_r_g) {
  typename Layout::index_type;
  typename Layout::index_reader_guard;
  requires std::movable<typename Layout::index_reader_guard> &&
               !std::copyable<typename Layout::index_reader_guard>;
  { l.try_read_lock(i) } -> std::same_as<std::optional<typename Layout::index_reader_guard>>; // locks that value of array
  { *l_r_g } -> std::same_as<typename Layout::data_type &>;
};

/**
 * @brief Defines a layout that supports exclusive-access (write) locking on specific indices.
 * * Similar to ReadLockableIndex, but for exclusive ownership. 
 * The guard must provide mutable access to the underlying @c data_type.
 * * @tparam Layout The layout type to validate.
 */
template <typename Layout>
concept WriteLockableIndex = requires(Layout l, typename Layout::index_type i, typename Layout::index_writer_guard l_w_g) {
  typename Layout::index_type;
  typename Layout::index_writer_guard;
  requires std::movable<typename Layout::index_writer_guard> &&
               !std::copyable<typename Layout::index_writer_guard>;
  { l.try_write_lock(i) } -> std::same_as<std::optional<typename Layout::index_writer_guard>>; // locks that value of array
  { *l_w_g } -> std::same_as<typename Layout::data_type &>;
};

/**
 * @brief Defines a single cell unit that supports atomic-like locking and state management.
 * * A LockableCell is expected to handle its own synchronization state (e.g., using atomics).
 * It manages both the presence of data and the ability to acquire a lock on the cell itself.
 * * @note Implementations should use atomic operations for @c try_lock and @c unlock.
 * * @tparam T The cell type to validate.
 */
template <typename T>
concept LockableCell = requires(T& l, T::index_type r){
    // please use atomics for the functions.
    requires std::copyable<T> && std::movable<T>;
    typename T::data_type;
    typename T::index_type;
    /** @brief Accesses the data within the cell at the given index. */
    {l[r]} -> std::same_as<typename T::data_type&>;
    /** @brief Attempts to acquire a lock; returns true if successful. */
    {T::try_lock(l)} -> std::same_as<bool>;
    /** @brief Marks the data as invalid or cleared. */
    {T::clear_data(l)} ; 
    /** @brief Releases the lock on the cell. */
    {T::unlock(l)} ;
    /** @brief Checks if the cell currently holds valid data. */
    {T::contains_data(l)} ->std::same_as<bool>;
};
// clang-format on
} // namespace engine::concepts
