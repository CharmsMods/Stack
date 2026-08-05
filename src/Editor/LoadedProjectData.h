#pragma once

#include "Persistence/StackBinaryFormat.h"
#include "Utils/SharedPixelBuffer.h"

#include <string>
#include <vector>

struct EditorLoadedProjectData {
    std::vector<unsigned char> sourcePixels;
    SharedPixelBuffer sourcePixelsShared;
    int width = 0;
    int height = 0;
    int channels = 4;
    nlohmann::json pipelineData = nlohmann::json::array();
    nlohmann::json rawWorkspaceData = nlohmann::json::object();
    Stack::Project::ProjectStoreHandle projectStore;
    std::shared_ptr<Stack::Project::RawProjectSnapshot> rawProjectSnapshot;
    std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry> nodeBrowserThumbnailEntries;
    std::string projectKind = StackBinaryFormat::kEditorProjectKind;
    std::string projectName;
    std::string projectFileName;
};
