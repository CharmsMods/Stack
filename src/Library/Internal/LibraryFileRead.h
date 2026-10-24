#pragma once

#include <filesystem>
#include <iosfwd>
#include <vector>

namespace Stack::Library::StorageHelpers {

// Requires exactly expectedBytes followed by EOF. Failure always clears output.
bool ReadExactStreamBytes(std::istream& input, std::streamoff expectedBytes,
    std::vector<unsigned char>& outBytes);
bool ReadFileBytes(const std::filesystem::path& path,
    std::vector<unsigned char>& outBytes);

}
