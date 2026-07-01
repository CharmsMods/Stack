#pragma once

#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"
#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

namespace Stack::Editor::DevelopAutoSolveDiagnostics {

void WriteDevelopAutoCandidateSolveDiagnostics(
    nlohmann::json& toneJson,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& result,
    const EditorNodeGraph::DevelopAutoGuidance& baseGuidance);

void WriteDevelopAutoStageSolveDiagnostics(
    nlohmann::json& toneJson,
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings,
    const Raw::RawDetailFusionSettings& prepSettings,
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& result,
    const DevelopDynamicRange::DevelopToneAutoStats& stats);

} // namespace Stack::Editor::DevelopAutoSolveDiagnostics
