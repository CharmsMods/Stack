#include "Library/Internal/LibraryFileRead.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>

namespace Stack::Library::StorageHelpers {

bool ReadExactStreamBytes(std::istream& input, const std::streamoff expectedBytes,
    std::vector<unsigned char>& outBytes) {
    outBytes.clear();
    try {
        if (!input.good() || expectedBytes < 0 ||
            static_cast<std::uintmax_t>(expectedBytes) > outBytes.max_size()) {
            return false;
        }
        outBytes.resize(static_cast<std::size_t>(expectedBytes));
        std::size_t offset = 0;
        while (offset < outBytes.size()) {
            const auto count = static_cast<std::streamsize>(
                std::min<std::size_t>(outBytes.size() - offset, 64 * 1024));
            input.read(reinterpret_cast<char*>(outBytes.data() + offset), count);
            if (input.gcount() != count || !input.good()) {
                outBytes.clear();
                return false;
            }
            offset += static_cast<std::size_t>(count);
        }
        // A source that grew after sizing must not silently lose its new tail.
        if (input.peek() != std::char_traits<char>::eof() || input.bad() || !input.eof()) {
            outBytes.clear();
            return false;
        }
        return true;
    } catch (...) {
        outBytes.clear();
        return false;
    }
}

bool ReadFileBytes(const std::filesystem::path& path,
    std::vector<unsigned char>& outBytes) {
    outBytes.clear();
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) return false;
        file.seekg(0, std::ios::beg);
        return ReadExactStreamBytes(file, size, outBytes);
    } catch (...) {
        outBytes.clear();
        return false;
    }
}

}
