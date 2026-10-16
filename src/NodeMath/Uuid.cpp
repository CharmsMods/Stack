#include "NodeMath/Uuid.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>

namespace Stack::NodeMath {
std::string GenerateCanonicalUuid() {
    thread_local std::mt19937_64 engine([] {
        std::random_device random;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::seed_seq seed {
            random(), random(), static_cast<unsigned int>(now),
            static_cast<unsigned int>(now >> 32u)
        };
        return std::mt19937_64(seed);
    }());
    std::array<unsigned char, 16> bytes {};
    for (std::size_t index = 0; index < bytes.size(); index += 8u) {
        const std::uint64_t value = engine();
        for (std::size_t offset = 0; offset < 8u; ++offset) {
            bytes[index + offset] = static_cast<unsigned char>((value >> (offset * 8u)) & 0xffu);
        }
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fu) | 0x80u);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4u || index == 6u || index == 8u || index == 10u) stream << '-';
        stream << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return stream.str();
}
}
