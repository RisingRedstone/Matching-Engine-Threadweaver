

#include <cstddef>
namespace engine::concepts {
template <std::size_t N>
concept PowerOfTwo = (N > 0) && (N & (N - 1)) == 0;
}
