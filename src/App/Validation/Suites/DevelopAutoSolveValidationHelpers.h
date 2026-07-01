#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

#include <string>

namespace Stack::Validation::Detail {

bool DevelopAutoSolveStageHasState(const nlohmann::json& stageSolves, const std::string& state);

EditorNodeGraph::DevelopAutoGuidance DevelopAutoSolveGuidanceFromToneJson(
    const nlohmann::json& toneJson,
    EditorNodeGraph::DevelopAutoGuidance fallback);

EditorNodeGraph::DevelopAutoGuidance DevelopAutoSolveGuidanceFromCandidateJson(
    const nlohmann::json& guidanceJson,
    EditorNodeGraph::DevelopAutoGuidance fallback);

bool IsDevelopAutoSolveFinishToneProbeId(const std::string& id);
bool IsDevelopAutoSolveWhiteBalanceProbeId(const std::string& id);

} // namespace Stack::Validation::Detail
