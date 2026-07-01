#pragma once

#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"
#include "ThirdParty/json.hpp"

#include <cstdint>

namespace Stack::Editor::DevelopRenderedFeedback {

bool ApplyRenderedCandidateFeedbackToSolve(
    DevelopCandidateScoring::DevelopAutoCandidateSolveResult& result,
    const nlohmann::json& previousToneJson,
    std::uint64_t preliminaryFingerprint);

} // namespace Stack::Editor::DevelopRenderedFeedback
