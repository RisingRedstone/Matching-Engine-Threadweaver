

#include <concepts>
#include <cstddef>
#include <cstdint>
namespace engine::concepts {
template <std::size_t N>
concept PowerOfTwo = (N > 0) && (N & (N - 1)) == 0;
template <typename T>
concept IsStandardUint =
    std::same_as<T, uint8_t> || std::same_as<T, uint16_t> ||
    std::same_as<T, uint32_t> || std::same_as<T, uint64_t>;
} // namespace engine::concepts
