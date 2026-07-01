#pragma once

#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"
#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

namespace Stack::Editor::DevelopCandidateGeneration {

DevelopCandidateScoring::DevelopAutoCandidateSolveResult BuildDevelopAutoCandidateSolve(
    const EditorNodeGraph::DevelopAutoGuidance& modeGuidance,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const Raw::RawMetadata& metadata,
    const nlohmann::json& previousToneJson);

} // namespace Stack::Editor::DevelopCandidateGeneration
