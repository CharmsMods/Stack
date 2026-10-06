#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Stack::Editor {

// A command target, never a borrowed node or editor pointer. Presentation state
// can retain this across frames while the editor checks the live document.
struct RawOperationMaskTarget {
    std::string documentId;
    std::uint64_t loadGeneration = 0;
    std::string layerId;
    std::string operationUuid;
    std::string displayLabel;
    std::string layerLabel;

    bool operator==(const RawOperationMaskTarget& other) const {
        return documentId == other.documentId && loadGeneration == other.loadGeneration &&
            layerId == other.layerId && operationUuid == other.operationUuid &&
            displayLabel == other.displayLabel && layerLabel == other.layerLabel;
    }
};

struct RawOperationMaskAvailability {
    std::optional<RawOperationMaskTarget> target;
    std::string disabledReason;
};

} // namespace Stack::Editor
