#pragma once

#include "RawImageData.h"

#include <functional>
#include <string>

namespace Raw {

bool DecodeWithLibRaw(
    const std::string& path,
    RawImageData& outData,
    const std::function<bool()>& shouldCancel = {});

// Opens and inspects the RAW container without unpacking sensor samples or
// reading the complete file into memory. This is the compatibility/preflight
// path for multi-frame project creation.
bool ProbeMetadataWithLibRaw(
    const std::string& path,
    RawMetadata& outMetadata);

} // namespace Raw
