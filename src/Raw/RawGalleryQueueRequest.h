#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::RawGalleryQueue {

// Presentation-neutral handoff from the RAW Gallery to the render queue.
// The Gallery owns source browsing metadata; the Queue owns queued state.
struct SourceRequest {
    std::filesystem::path sourcePath;
    std::filesystem::path thumbnailPath;
};

struct Request {
    std::vector<std::filesystem::path> projectPaths;
    std::vector<SourceRequest> sources;
};

// Counts describe Queue adoption, not rendering or export completion.
struct Result {
    std::size_t added = 0;
    std::size_t alreadyPresent = 0;
    std::size_t skipped = 0;
    std::vector<std::string> errors;
};

} // namespace Stack::RawGalleryQueue
