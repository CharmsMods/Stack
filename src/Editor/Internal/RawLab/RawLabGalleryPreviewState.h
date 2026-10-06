#pragma once

#include <filesystem>
#include <string>

namespace Stack::Editor::RawLabInternal {

// Resolve textures through the thumbnail cache each frame. Keeping identities
// here lets the cache retain ownership and avoids dangling texture handles.
struct GalleryPreviewState {
    std::string workspaceKey;
    std::string sourceKey;
    std::filesystem::path projectPath;
    std::string previousSourceKey;
    std::filesystem::path previousProjectPath;
    float blend = 1.0f;
    double lastChangedAt = -1.0;
};

} // namespace Stack::Editor::RawLabInternal
