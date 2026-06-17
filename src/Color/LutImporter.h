#pragma once

#include "Color/LutData.h"

#include <string>

namespace ColorLut {

struct LutImportResult {
    bool success = false;
    std::string message;
    LutPayload payload;
};

LutImportResult ImportLutFile(const std::string& path);

} // namespace ColorLut
