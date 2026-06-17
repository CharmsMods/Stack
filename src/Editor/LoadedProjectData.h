#pragma once

#include "Persistence/StackBinaryFormat.h"

#include <string>
#include <vector>

struct EditorLoadedProjectData {
    std::vector<unsigned char> sourcePixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    nlohmann::json pipelineData = nlohmann::json::array();
    std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry> nodeBrowserThumbnailEntries;
    std::string projectName;
    std::string projectFileName;
};
