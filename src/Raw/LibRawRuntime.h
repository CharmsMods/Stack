#pragma once

#include <string>

namespace Raw {

enum class LibRawRuntimeAvailability {
    Available,
    DisabledInBuild,
    MissingOrUnloaded
};

struct LibRawRuntimeStatus {
    LibRawRuntimeAvailability availability = LibRawRuntimeAvailability::DisabledInBuild;
    bool compiledWithLibRaw = false;
    bool runtimeAvailable = false;
    std::string message;
};

const LibRawRuntimeStatus& GetLibRawRuntimeStatus();
bool IsLibRawRuntimeAvailable();

} // namespace Raw
