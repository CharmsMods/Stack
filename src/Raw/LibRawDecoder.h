#pragma once

#include "RawImageData.h"

#include <string>

namespace Raw {

bool DecodeWithLibRaw(const std::string& path, RawImageData& outData);

} // namespace Raw
