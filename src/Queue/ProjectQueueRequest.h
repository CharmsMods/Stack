#pragma once

#include "Raw/RawGalleryQueueRequest.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Project { struct LoadedProjectData; }

namespace Stack::Queue {

// A deliberate capture of one open project's Output. The candidate owns its
// authored data and immutable source leases; it never borrows an Editor or tab.
// Consumers copy the candidate and its mutable RAW manifest before applying it.
struct ProjectRequest {
    std::shared_ptr<const Project::LoadedProjectData> snapshot;
    std::string displayName;
    std::vector<unsigned char> thumbnailBytes;
    std::string outputNodeUuid;
    std::uint64_t editRevision = 0;
};

} // namespace Stack::Queue
