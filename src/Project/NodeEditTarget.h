#pragma once
#include <string>

namespace Stack::Project {
inline constexpr const char* kProjectGraphId = "project";
// Stable authored identity. Canvas integers are view-local and may be rebuilt.
struct NodeEditTarget {
    std::string documentId, layerId, nodeUuid;
    bool operator==(const NodeEditTarget& other) const {
        return documentId == other.documentId && layerId == other.layerId && nodeUuid == other.nodeUuid;
    }
    bool operator!=(const NodeEditTarget& other) const { return !(*this == other); }
    std::string Key() const {
        return std::to_string(documentId.size()) + ":" + documentId +
            std::to_string(layerId.size()) + ":" + layerId + nodeUuid;
    }
};
}
