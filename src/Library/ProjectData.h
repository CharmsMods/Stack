#pragma once
#include "Async/TaskState.h"
#include <string>
#include <cstdint>
#include <vector>
#include "ThirdParty/json.hpp"

struct ProjectEntry {
    std::string fileName;
    std::string projectName;
    std::string timestamp;
    std::string projectKind;
    std::vector<unsigned char> thumbnailBytes;
    
    // Metadata
    int sourceWidth = 0;
    int sourceHeight = 0;
    
    // The actual binary image is only loaded when the project is opened
    std::vector<unsigned char> sourceImageBytes;
    nlohmann::json pipelineData;
    
    // GL Textures (runtime only)
    unsigned int thumbnailTex = 0;
    unsigned int sourcePreviewTex = 0;
    unsigned int fullPreviewTex = 0;

    // Deferred fullscreen preview generation state
    Async::TaskState previewTaskState = Async::TaskState::Idle;
    std::uint64_t previewRequestGeneration = 0;
    std::string previewStatusText;
};

struct AssetEntry {
    std::string fileName;
    std::string displayName;
    std::string timestamp;
    std::string projectFileName;
    std::string projectName;
    std::string projectKind;

    int width = 0;
    int height = 0;

    unsigned int thumbnailTex = 0;
    unsigned int fullPreviewTex = 0;

    Async::TaskState previewTaskState = Async::TaskState::Idle;
    std::uint64_t previewRequestGeneration = 0;
    std::string previewStatusText;
};
