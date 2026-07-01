#pragma once

#include "RawImageData.h"

#include <functional>
#include <string>

namespace Raw {

bool DecodeWithLibRaw(
    const std::string& path,
    RawImageData& outData,
    const std::function<bool()>& shouldCancel = {});

} // namespace Raw
