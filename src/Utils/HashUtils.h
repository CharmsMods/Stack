#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace StackHash {

inline void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
}

template <typename T>
inline std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

inline std::size_t HashBytes(const unsigned char* data, std::size_t size) {
    constexpr std::size_t offsetBasis =
        sizeof(std::size_t) == 8 ? 14695981039346656037ull : 2166136261u;
    constexpr std::size_t prime =
        sizeof(std::size_t) == 8 ? 1099511628211ull : 16777619u;

    std::size_t hash = offsetBasis;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::size_t>(data[index]);
        hash *= prime;
    }
    return hash;
}

inline std::size_t HashBytes(const std::vector<unsigned char>& data) {
    return data.empty() ? 0 : HashBytes(data.data(), data.size());
}

} // namespace StackHash
