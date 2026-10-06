#pragma once

#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawImageData.h"
#include "Utils/SharedPixelBuffer.h"

#include <string>
#include <vector>

namespace Stack::Project {

enum class ProjectSourceState {
    DecodedPixels,
    LazyAsset,
    // Source-compatible spelling retained for current managed-store callers.
    LazyManagedAsset = LazyAsset,
    Unavailable
};

struct LoadedProjectData {
    ProjectSourceState sourceState = ProjectSourceState::DecodedPixels;
    std::vector<unsigned char> sourcePixels;
    SharedPixelBuffer sourcePixelsShared;
    int width = 0;
    int height = 0;
    int channels = 4;
    nlohmann::json pipelineData = nlohmann::json::array();
    nlohmann::json rawWorkspaceData = nlohmann::json::object();
    Stack::Project::ProjectStoreHandle projectStore;
    std::shared_ptr<Stack::Project::RawProjectSnapshot> rawProjectSnapshot;
    // Generated only by Gallery double-click. This is an in-memory editing
    // preview, not a saved project using an older storage format. Its first
    // authored edit materializes the current managed project store.
    bool transientRawPreview = false;
    // Immutable RAW source decoded by the background open task. The live
    // renderer can consume it directly instead of invoking LibRaw during an
    // editor frame. Empty for normal and multi-frame projects.
    std::shared_ptr<const Raw::RawImageData> decodedRawSource;
    std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry> nodeBrowserThumbnailEntries;
    std::string projectKind = StackBinaryFormat::kEditorProjectKind;
    std::string projectId;
    std::filesystem::path adoptedFrom;
    std::string projectName;
    std::string projectFileName;
};

} // namespace Stack::Project
