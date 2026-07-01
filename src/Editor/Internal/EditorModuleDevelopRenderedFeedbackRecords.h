#pragma once

#include "Editor/EditorRenderWorker.h"
#include "ThirdParty/json.hpp"

#include <cstdint>
#include <cstddef>
#include <string>

namespace Stack::Editor::DevelopRenderedFeedbackRecords {

std::size_t HashDevelopRenderedFeedbackJsonValue(const nlohmann::json& value);

nlohmann::json DevelopCandidateRenderMetricsToJson(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics);

void AppendDevelopCandidateRenderedFeedbackHistory(
    nlohmann::json& toneJson,
    std::uint64_t fingerprint,
    const std::string& selectedCandidateId,
    float selectedRenderScore,
    bool selectedRenderScoreValid,
    const std::string& bestCandidateId,
    float bestRenderScore,
    int successCount,
    int failureCount,
    const std::string& action,
    const std::string& stopReason,
    const std::string& refineIntent = {},
    const std::string& refineReason = {},
    const EditorRenderWorker::DevelopCandidateRenderMetrics* selectedMetrics = nullptr,
    const EditorRenderWorker::DevelopCandidateRenderMetrics* bestMetrics = nullptr);

void WriteDevelopCandidateRenderedFeedbackLoopRecord(
    nlohmann::json& toneJson,
    std::uint64_t solveFingerprint,
    std::uint64_t revision,
    const std::string& state,
    const std::string& action,
    const std::string& stopReason,
    const std::string& nextStep,
    bool requiresAutoSolve,
    bool requiresRenderedMetrics,
    const std::string& selectedCandidateId,
    float selectedRenderScore,
    bool selectedRenderScoreValid,
    const std::string& bestCandidateId,
    float bestRenderScore,
    int successCount,
    int failureCount);

} // namespace Stack::Editor::DevelopRenderedFeedbackRecords
