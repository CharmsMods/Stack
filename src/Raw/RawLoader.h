#pragma once

#include "RawImageData.h"

#include <string>

namespace Raw {

class RawLoader {
public:
    static bool LoadFile(const std::string& path, RawImageData& outData);
    static bool LoadMetadata(const std::string& path, RawMetadata& outMetadata);
    static bool IsRawPath(const std::string& path);
};

} // namespace Raw
