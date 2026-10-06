#pragma once
#include "Raw/MultiFrameHdr/FusionControls.h"
#include <imgui.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace Stack::Editor {
struct FusionWorkspaceUiState {
    std::string nodeId, projectId, storedControls, imageIdentity;
    Raw::Hdr::FusionControls controls;
    bool dirty = false;
    unsigned int texture = 0;
    int view = 0, source = 0, selectedMask = -1;
    float probeX = 0.5f, probeY = 0.5f;
    bool probeLocked = false;
    bool placeMask = false;
    std::shared_ptr<const Raw::Hdr::FusionPreview> burstPreview;
    std::uint64_t burstContentHash = 0;
    std::unordered_map<std::string, std::string> sourceLabels;
    ~FusionWorkspaceUiState();
};

bool DrawFusionControls(FusionWorkspaceUiState&, const Raw::Hdr::FusionPreview*);
void DrawFusionPreview(FusionWorkspaceUiState&, const Raw::Hdr::FusionPreview*, const ImVec2& available);
void DrawFusionProbe(const FusionWorkspaceUiState&, const Raw::Hdr::FusionPreview&);
} // namespace Stack::Editor
