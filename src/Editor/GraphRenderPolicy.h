#pragma once

#include <cstddef>
#include <cstdint>

namespace Stack::GraphRendering {

// Revisions change for parameter edits. Context changes only when a completed
// frame would belong to a different source, output, or graph structure.
struct RequestTag {
    bool enabled = false;
    int outputNodeId = 0;
    int inspectionNodeId = 0;
    bool composite = false;
    std::uint64_t structureRevision = 0;
    std::size_t sourceIdentity = 0;
    std::uint64_t revision = 0;
};

inline bool SameContext(const RequestTag& a, const RequestTag& b) {
    return a.enabled && b.enabled && a.outputNodeId == b.outputNodeId &&
        a.inspectionNodeId == b.inspectionNodeId && a.composite == b.composite &&
        a.structureRevision == b.structureRevision && a.sourceIdentity == b.sourceIdentity;
}

inline bool MayFinishActive(const RequestTag& active, const RequestTag& latest) {
    return SameContext(active, latest);
}

inline bool MayAdopt(const RequestTag& result, const RequestTag& current,
                     std::uint64_t generation, std::uint64_t acceptedGeneration) {
    return SameContext(result, current) && result.revision <= current.revision &&
        generation > acceptedGeneration;
}

} // namespace Stack::GraphRendering
