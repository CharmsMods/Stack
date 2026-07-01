#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawImageData.h"
#include "Renderer/MaskRenderTypes.h"

#include <string>

namespace Stack::Editor::DevelopCandidate {

EditorNodeGraph::DevelopAutoGuidance ReadDevelopAuthoredGuidanceFromToneJson(
    const nlohmann::json& toneJson,
    EditorNodeGraph::DevelopAutoGuidance fallback);

void ApplyDevelopGuidanceToCandidateRenderPayload(
    RenderGraphRawDevelopPayload& payload,
    const EditorNodeGraph::DevelopAutoGuidance& currentGuidance,
    const EditorNodeGraph::DevelopAutoGuidance& candidateGuidance,
    const std::string& candidateId,
    EditorNodeGraph::DevelopAutoIntent intent,
    const Raw::WhiteBalanceMode* whiteBalanceOverride = nullptr);

} // namespace Stack::Editor::DevelopCandidate
