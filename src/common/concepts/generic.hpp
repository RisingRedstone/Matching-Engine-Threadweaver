#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace engine::concepts {
template <std::size_t N>
concept PowerOfTwo = (N > 0) && (N & (N - 1)) == 0;
template <typename T>
concept IsStandardUint =
    std::same_as<T, uint8_t> || std::same_as<T, uint16_t> ||
    std::same_as<T, uint32_t> || std::same_as<T, uint64_t>;

// clang-format off
template <typename Layout>
concept ReadLockableIndex = requires(Layout l, typename Layout::index_type i, typename Layout::index_reader_guard l_r_g) {
  typename Layout::index_type;
  typename Layout::index_reader_guard;
  requires std::movable<typename Layout::index_reader_guard> &&
               !std::copyable<typename Layout::index_reader_guard>;
  { l.try_read_lock(i) } -> std::same_as<std::optional<typename Layout::index_reader_guard>>; // locks that value of array
  { *l_r_g } -> std::same_as<typename Layout::data_type &>;
};

template <typename Layout>
concept WriteLockableIndex = requires(Layout l, typename Layout::index_type i, typename Layout::index_writer_guard l_w_g) {
  typename Layout::index_type;
  typename Layout::index_writer_guard;
  requires std::movable<typename Layout::index_writer_guard> &&
               !std::copyable<typename Layout::index_writer_guard>;
  { l.try_write_lock(i) } -> std::same_as<std::optional<typename Layout::index_writer_guard>>; // locks that value of array
  { *l_w_g } -> std::same_as<typename Layout::data_type &>;
};

template <typename T>
concept LockableCell = requires(T& l, T::index_type r){
    // please use atomics for the functions.
    requires std::copyable<T> && std::movable<T>;
    typename T::data_type;
    typename T::index_type;
    {l[r]} -> std::same_as<typename T::data_type&>;
    {T::try_lock(l)} -> std::same_as<bool>;
    {T::clear_data(l)} ; // you dont have to clear the data, only to indicate that it doesn't contain data anymore
    {T::unlock(l)} ;
    {T::contains_data(l)} ->std::same_as<bool>; // returns false as default or after clear_data
};
// clang-format on
} // namespace engine::concepts
