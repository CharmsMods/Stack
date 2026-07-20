#pragma once

#include "Raw/RawAutoBase.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RawPreciseIntegration.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Stack::EditorModuleTypes {

enum class RawAutoValueOwner {
    None,
    AutoBase,
    User
};

enum class RawStartingPointPendingPhase {
    None,
    WaitingForInitialAnalysis,
    WaitingForPostApplyAnalysis
};

inline constexpr int kRawStartingPointMaxUpstreamApplyPasses = 4;

struct RawWorkspaceStartingPointPendingAction {
    bool active = false;
    RawStartingPointPendingPhase phase = RawStartingPointPendingPhase::None;
    int upstreamApplyPassCount = 0;
    std::string sourceKey;
    std::uint64_t sourceHash = 0;
    std::string recipeFingerprint;
    Stack::RawRecipe::RawDevelopmentRecipe originalRecipe;
    Stack::RawRecipe::RawDevelopmentRecipe plannedUpstreamRecipe;
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> appliedControls;
    std::string planSummary;
    std::string appliedControlsSummary;
    std::string appliedValuesSummary;
    std::string withheldControlsSummary;
    std::string evidenceSummary;
    std::string warningSummary;
    std::string cancellationReason;
};

inline bool RawStartingPointShouldContinueUpstreamApply(
    const RawWorkspaceStartingPointPendingAction& pending,
    bool nextPlanHasUpstreamChanges) {
    return pending.active &&
        pending.phase == RawStartingPointPendingPhase::WaitingForPostApplyAnalysis &&
        nextPlanHasUpstreamChanges &&
        pending.upstreamApplyPassCount > 0 &&
        pending.upstreamApplyPassCount < kRawStartingPointMaxUpstreamApplyPasses;
}

inline bool RawStartingPointReachedUpstreamApplyLimit(
    const RawWorkspaceStartingPointPendingAction& pending,
    bool nextPlanHasUpstreamChanges) {
    return pending.active &&
        pending.phase == RawStartingPointPendingPhase::WaitingForPostApplyAnalysis &&
        nextPlanHasUpstreamChanges &&
        pending.upstreamApplyPassCount >= kRawStartingPointMaxUpstreamApplyPasses;
}

struct RawWorkspaceStartingPointCandidateRenderQueueState {
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest> requests;
    std::string sourceKey;
    std::uint64_t sourceHash = 0;
    std::uint64_t generation = 0;
};

struct RawWorkspaceAutoBaseUiState {
    bool hasAppliedViewFit = false;
    bool startingPointDisplayFitPending = false;
    bool hasRevertSnapshot = false;
    bool suggestionsOpen = false;
    Stack::RawRecipe::RawDevelopmentRecipe beforeAutoBase;
    RawAutoValueOwner viewTransformOwner = RawAutoValueOwner::None;
    std::uint64_t sourceHash = 0;
    std::uint64_t appliedAnalysisHash = 0;
    bool preciseAppliedDisplayFitAwaitingRender = false;
    std::string preciseAppliedRecipeIdentity;
    std::uint64_t appliedSuggestionSourceHash = 0;
    std::uint64_t appliedSuggestionAnalysisHash = 0;
    std::string appliedSuggestionKey;
    std::string appliedSuggestionLabel;
    std::string appliedSuggestionSection;
    std::string sourceKey;
    std::string summary;
    std::string startingPointResultSummary;
    std::string startingPointAppliedControlsSummary;
    std::string startingPointAppliedValuesSummary;
    std::string startingPointWithheldControlsSummary;
    std::string startingPointControlStatusSummary;
    std::string startingPointEvidenceSummary;
    std::string startingPointWarningSummary;
    bool hasMetadataSummary = false;
    Stack::RawAnalysis::RawMetadataSummary metadataSummary;
    Stack::RawAutoBase::AutoBaseRecommendations recommendations;
    RawWorkspaceStartingPointPendingAction pendingStartingPoint;
    Stack::PreciseIntegration::IntegrationState preciseStartingPoint;
};

enum class RawStartingPointContinuationDecision {
    Inactive,
    WaitForAnalysis,
    ContinueInitialAnalysis,
    ContinuePostApplyAnalysis,
    CancelSourceMissing,
    CancelSourceChanged,
    CancelRecipeChanged,
    CancelUnknownPhase
};

struct RawStartingPointContinuationContext {
    bool sourceExists = false;
    std::string activeSourceKey;
    std::string selectedSourceKey;
    std::uint64_t sourceHash = 0;
    std::string analysisSourceKey;
    bool analysisValid = false;
    std::string recipeFingerprint;
};

inline bool IsRawStartingPointSummaryEmptyOrNone(const std::string& summary) {
    return summary.empty() || summary == "None";
}

inline bool RawStartingPointSummaryContains(
    const std::string& summary,
    const char* text) {
    return text != nullptr && text[0] != '\0' && summary.find(text) != std::string::npos;
}

inline bool RawStartingPointSummaryContains(
    const std::string& summary,
    const std::string& text) {
    return !text.empty() && summary.find(text) != std::string::npos;
}

inline bool RawStartingPointAnySummaryContains(
    const std::string& a,
    const std::string& b,
    const std::string& c,
    const char* text) {
    return RawStartingPointSummaryContains(a, text) ||
        RawStartingPointSummaryContains(b, text) ||
        RawStartingPointSummaryContains(c, text);
}

inline bool RawStartingPointControlMentioned(
    const std::string& summary,
    const char* label,
    const char* alternateLabel = nullptr) {
    return RawStartingPointSummaryContains(summary, label) ||
        RawStartingPointSummaryContains(summary, alternateLabel);
}

inline std::string TrimRawStartingPointSummarySegment(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

inline bool RawStartingPointSegmentStartsWithKnownControl(const std::string& segment) {
    const std::string trimmed = TrimRawStartingPointSummarySegment(segment);
    const char* labels[] = {
        "RAW Exposure",
        "White Balance",
        "Suggested WB",
        "Local Range",
        "Finish Tone",
        "Display Fit",
        "View Transform"
    };
    for (const char* label : labels) {
        const std::size_t length = std::string(label).size();
        if (trimmed.rfind(label, 0) == 0 &&
            (trimmed.size() == length ||
             trimmed[length] == ' ' ||
             trimmed[length] == ':' ||
             trimmed[length] == '/')) {
            return true;
        }
    }
    return false;
}

inline void AppendRawStartingPointRelevantControlText(
    std::string& output,
    const std::string& summary,
    const char* label,
    const char* alternateLabel = nullptr) {
    std::size_t start = 0;
    while (start <= summary.size()) {
        const std::size_t comma = summary.find(',', start);
        const std::size_t semicolon = summary.find(';', start);
        std::size_t end = std::string::npos;
        if (comma != std::string::npos && semicolon != std::string::npos) {
            end = std::min(comma, semicolon);
        } else if (comma != std::string::npos) {
            end = comma;
        } else {
            end = semicolon;
        }
        const std::string segment =
            summary.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (RawStartingPointControlMentioned(segment, label, alternateLabel)) {
            if (!output.empty()) {
                output += " ";
            }
            output += segment;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

inline void AppendRawStartingPointRelevantControlValueText(
    std::string& output,
    const std::string& summary,
    const char* label,
    const char* alternateLabel = nullptr) {
    std::string collected;
    bool collecting = false;
    std::size_t start = 0;
    while (start <= summary.size()) {
        const std::size_t comma = summary.find(',', start);
        const std::size_t semicolon = summary.find(';', start);
        std::size_t end = std::string::npos;
        bool endedBySemicolon = false;
        if (comma != std::string::npos && semicolon != std::string::npos) {
            endedBySemicolon = semicolon < comma;
            end = std::min(comma, semicolon);
        } else if (comma != std::string::npos) {
            end = comma;
        } else {
            end = semicolon;
            endedBySemicolon = semicolon != std::string::npos;
        }

        const std::string segment =
            TrimRawStartingPointSummarySegment(
                summary.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start));
        const bool mentionsTarget =
            RawStartingPointControlMentioned(segment, label, alternateLabel);
        const bool startsKnownControl =
            RawStartingPointSegmentStartsWithKnownControl(segment);

        if (!segment.empty()) {
            if (collecting && startsKnownControl && !mentionsTarget) {
                if (!output.empty()) {
                    output += " ";
                }
                output += collected;
                collected.clear();
                collecting = false;
            }

            if (mentionsTarget) {
                if (collecting && !collected.empty()) {
                    if (!output.empty()) {
                        output += " ";
                    }
                    output += collected;
                }
                collected = segment;
                collecting = true;
            } else if (collecting) {
                collected += ", ";
                collected += segment;
            }
        }

        if (collecting && endedBySemicolon) {
            if (!output.empty()) {
                output += " ";
            }
            output += collected;
            collected.clear();
            collecting = false;
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    if (collecting && !collected.empty()) {
        if (!output.empty()) {
            output += " ";
        }
        output += collected;
    }
}

inline std::string RawStartingPointRelevantControlText(
    const std::string& resultSummary,
    const std::string& withheldControls,
    const char* label,
    const char* alternateLabel = nullptr) {
    std::string text;
    AppendRawStartingPointRelevantControlText(text, resultSummary, label, alternateLabel);
    AppendRawStartingPointRelevantControlText(text, withheldControls, label, alternateLabel);
    return text;
}

inline std::string RawStartingPointRelevantControlValueText(
    const std::string& appliedValues,
    const char* label,
    const char* alternateLabel = nullptr) {
    std::string text;
    AppendRawStartingPointRelevantControlValueText(text, appliedValues, label, alternateLabel);
    return text;
}

inline bool RawStartingPointSummaryContainsControlPhrase(
    const std::string& summary,
    const char* label,
    const char* alternateLabel,
    const char* phrase) {
    if (label != nullptr && label[0] != '\0') {
        if (RawStartingPointSummaryContains(summary, std::string(label) + phrase)) {
            return true;
        }
    }
    return alternateLabel != nullptr &&
        alternateLabel[0] != '\0' &&
        RawStartingPointSummaryContains(summary, std::string(alternateLabel) + phrase);
}

inline bool RawStartingPointAnySummaryContainsControlPhrase(
    const std::string& a,
    const std::string& b,
    const char* label,
    const char* alternateLabel,
    const char* phrase) {
    return RawStartingPointSummaryContainsControlPhrase(a, label, alternateLabel, phrase) ||
        RawStartingPointSummaryContainsControlPhrase(b, label, alternateLabel, phrase);
}

inline std::string RawStartingPointSingleControlStatus(
    const std::string& resultSummary,
    const std::string& appliedControls,
    const std::string& withheldControls,
    const std::string& appliedValues,
    const char* statusLabel,
    const char* searchLabel,
    const char* alternateSearchLabel = nullptr) {
    const bool changed =
        RawStartingPointControlMentioned(appliedControls, searchLabel, alternateSearchLabel) ||
        RawStartingPointControlMentioned(appliedValues, searchLabel, alternateSearchLabel);
    if (changed) {
        return std::string(statusLabel) + ": changed";
    }

    const bool previewPending =
        RawStartingPointAnySummaryContains(
            resultSummary,
            withheldControls,
            appliedValues,
            "Preview analysis pending") ||
        RawStartingPointAnySummaryContains(
            resultSummary,
            withheldControls,
            appliedValues,
            "analyzing this RAW");
    if (previewPending) {
        return std::string(statusLabel) + ": pending analysis";
    }

    const std::string controlText =
        RawStartingPointRelevantControlText(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel);
    const bool mentioned = !controlText.empty();
    const bool genericFailure =
        !mentioned &&
        IsRawStartingPointSummaryEmptyOrNone(appliedControls) &&
        (RawStartingPointSummaryContains(resultSummary, "canceled") ||
         RawStartingPointSummaryContains(resultSummary, "failed") ||
         RawStartingPointSummaryContains(withheldControls, "render failed") ||
         RawStartingPointSummaryContains(withheldControls, "worker produced"));

    const bool controlFailed =
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " canceled") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " failed") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " could not") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " was not refreshed");
    const bool controlPending =
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " pending") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " awaiting") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " will be refreshed") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " needs");
    const bool controlNotProposed =
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " unchanged") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " already near target") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " not proposed") ||
        RawStartingPointSummaryContains(controlText, std::string(searchLabel) + " unchanged: no proposal") ||
        RawStartingPointSummaryContains(controlText, std::string(searchLabel) + " unchanged: no one-click proposal");
    const bool controlBlocked =
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " withheld") ||
        RawStartingPointAnySummaryContainsControlPhrase(
            resultSummary,
            withheldControls,
            searchLabel,
            alternateSearchLabel,
            " suggestion kept manual") ||
        RawStartingPointSummaryContains(controlText, "Additional RAW Exposure lift withheld");

    if (mentioned && controlNotProposed) {
        return std::string(statusLabel) + ": not proposed";
    }

    if (mentioned && controlFailed) {
        return std::string(statusLabel) + ": failed";
    }

    if (mentioned && controlPending) {
        return std::string(statusLabel) + ": pending";
    }

    if (mentioned || controlBlocked) {
        return std::string(statusLabel) + ": blocked by policy";
    }

    if (genericFailure) {
        return std::string(statusLabel) + ": failed";
    }

    return std::string(statusLabel) + ": not proposed";
}

inline std::string BuildRawStartingPointControlStatusSummary(
    const std::string& resultSummary,
    const std::string& appliedControls,
    const std::string& withheldControls,
    const std::string& appliedValues) {
    if (IsRawStartingPointSummaryEmptyOrNone(resultSummary) &&
        IsRawStartingPointSummaryEmptyOrNone(appliedControls) &&
        IsRawStartingPointSummaryEmptyOrNone(withheldControls) &&
        IsRawStartingPointSummaryEmptyOrNone(appliedValues)) {
        return "None";
    }

    std::string summary = RawStartingPointSingleControlStatus(
        resultSummary,
        appliedControls,
        withheldControls,
        appliedValues,
        "RAW Exposure",
        "RAW Exposure");
    summary += "; ";
    summary += RawStartingPointSingleControlStatus(
        resultSummary,
        appliedControls,
        withheldControls,
        appliedValues,
        "Local Range",
        "Local Range");
    summary += "; ";
    summary += RawStartingPointSingleControlStatus(
        resultSummary,
        appliedControls,
        withheldControls,
        appliedValues,
        "Finish Tone",
        "Finish Tone");
    summary += "; ";
    summary += RawStartingPointSingleControlStatus(
        resultSummary,
        appliedControls,
        withheldControls,
        appliedValues,
        "Display Fit / View Transform",
        "Display Fit",
        "View Transform");
    return summary;
}

inline void SetRawStartingPointUiResult(
    RawWorkspaceAutoBaseUiState& ui,
    std::string summary,
    std::string appliedControls,
    std::string withheldControls,
    std::string evidenceSummary = std::string(),
    std::string warningSummary = std::string(),
    std::string appliedValuesSummary = std::string()) {
    ui.startingPointResultSummary = std::move(summary);
    ui.startingPointAppliedControlsSummary =
        appliedControls.empty() ? std::string("None") : std::move(appliedControls);
    ui.startingPointAppliedValuesSummary =
        appliedValuesSummary.empty() ? std::string("None") : std::move(appliedValuesSummary);
    ui.startingPointWithheldControlsSummary =
        withheldControls.empty() ? std::string("None") : std::move(withheldControls);
    ui.startingPointControlStatusSummary =
        BuildRawStartingPointControlStatusSummary(
            ui.startingPointResultSummary,
            ui.startingPointAppliedControlsSummary,
            ui.startingPointWithheldControlsSummary,
            ui.startingPointAppliedValuesSummary);
    ui.startingPointEvidenceSummary =
        evidenceSummary.empty() ? std::string("None") : std::move(evidenceSummary);
    ui.startingPointWarningSummary =
        warningSummary.empty() ? std::string("None") : std::move(warningSummary);
}

inline void ClearRawStartingPointUiResult(RawWorkspaceAutoBaseUiState& ui) {
    ui.startingPointResultSummary.clear();
    ui.startingPointAppliedControlsSummary.clear();
    ui.startingPointAppliedValuesSummary.clear();
    ui.startingPointWithheldControlsSummary.clear();
    ui.startingPointControlStatusSummary.clear();
    ui.startingPointEvidenceSummary.clear();
    ui.startingPointWarningSummary.clear();
}

inline void ClearRawStartingPointCandidateRenderQueue(
    RawWorkspaceStartingPointCandidateRenderQueueState& queue) {
    queue.requests.clear();
    queue.sourceKey.clear();
    queue.sourceHash = 0;
    queue.generation = 0;
}

inline void ClearRawStartingPointCandidateEvidenceCache(
    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics,
    RawWorkspaceStartingPointCandidateRenderQueueState& queue,
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderResult>& results) {
    diagnostics = Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics();
    ClearRawStartingPointCandidateRenderQueue(queue);
    results.clear();
}

inline bool RawStartingPointSourceHashesCompatible(
    std::uint64_t queuedSourceHash,
    std::uint64_t otherSourceHash) {
    return queuedSourceHash == 0 ||
        otherSourceHash == 0 ||
        queuedSourceHash == otherSourceHash;
}

inline bool RawStartingPointCandidateRenderQueueMatchesSource(
    const RawWorkspaceStartingPointCandidateRenderQueueState& queue,
    const std::string& sourceKey,
    std::uint64_t sourceHash = 0) {
    return !sourceKey.empty() &&
        queue.sourceKey == sourceKey &&
        RawStartingPointSourceHashesCompatible(queue.sourceHash, sourceHash);
}

inline bool RawStartingPointCandidateRenderResultsMatchSource(
    const RawWorkspaceStartingPointCandidateRenderQueueState& queue,
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderResult>& results,
    const std::string& sourceKey,
    std::uint64_t sourceHash = 0) {
    return !results.empty() &&
        RawStartingPointCandidateRenderQueueMatchesSource(
            queue,
            sourceKey,
            sourceHash);
}

inline void StoreRawStartingPointCandidateRenderQueue(
    RawWorkspaceStartingPointCandidateRenderQueueState& queue,
    std::string sourceKey,
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest> requests,
    std::uint64_t generation,
    std::uint64_t sourceHash = 0) {
    if (sourceKey.empty() || requests.empty()) {
        ClearRawStartingPointCandidateRenderQueue(queue);
        return;
    }
    queue.sourceKey = std::move(sourceKey);
    queue.sourceHash = sourceHash;
    queue.requests = std::move(requests);
    queue.generation = generation;
}

inline bool ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
    RawWorkspaceStartingPointCandidateRenderQueueState& queue,
    const std::string& activeSourceKey,
    std::uint64_t activeSourceHash = 0) {
    if (queue.sourceKey.empty() ||
        RawStartingPointCandidateRenderQueueMatchesSource(
            queue,
            activeSourceKey,
            activeSourceHash)) {
        return false;
    }
    ClearRawStartingPointCandidateRenderQueue(queue);
    return true;
}

inline bool ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
    const RawWorkspaceStartingPointCandidateRenderQueueState& queue,
    const std::string& resultSourceKey,
    const std::string& activeSourceKey,
    std::uint64_t resultSourceHash = 0,
    std::uint64_t activeSourceHash = 0) {
    if (queue.sourceKey.empty()) {
        return false;
    }
    const bool resultMatchesQueue =
        RawStartingPointCandidateRenderQueueMatchesSource(
            queue,
            resultSourceKey,
            resultSourceHash);
    const bool queueMatchesActive =
        RawStartingPointCandidateRenderQueueMatchesSource(
            queue,
            activeSourceKey,
            activeSourceHash);
    return resultMatchesQueue || !queueMatchesActive;
}

inline bool ShouldClearRawStartingPointCandidateRenderQueueForFailedResult(
    const RawWorkspaceStartingPointCandidateRenderQueueState& queue,
    const std::string& resultSourceKey,
    const std::string& activeSourceKey,
    std::uint64_t resultSourceHash = 0,
    std::uint64_t activeSourceHash = 0) {
    return ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
        queue,
        resultSourceKey,
        activeSourceKey,
        resultSourceHash,
        activeSourceHash);
}

inline std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>
RawStartingPointControlsWithoutDisplayFit(
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> controls) {
    controls.erase(
        std::remove(
            controls.begin(),
            controls.end(),
            Stack::RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
        controls.end());
    return controls;
}

inline std::string JoinRawStartingPointSummaryParts(const std::vector<std::string>& parts) {
    std::string summary;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += part;
    }
    return summary.empty() ? std::string("None") : summary;
}

inline std::string RawStartingPointControlLabels(
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& controls) {
    std::vector<std::string> labels;
    labels.reserve(controls.size());
    for (Stack::RawAutoStartPoint::RawAutoStartPointControl control : controls) {
        const std::string label = Stack::RawAutoStartPoint::ControlLabel(control);
        if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
            labels.push_back(label);
        }
    }
    return JoinRawStartingPointSummaryParts(labels);
}

inline std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>
RawStartingPointControlsWithDisplayFit(
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> controls) {
    if (std::find(
            controls.begin(),
            controls.end(),
            Stack::RawAutoStartPoint::RawAutoStartPointControl::DisplayFit) ==
        controls.end()) {
        controls.push_back(Stack::RawAutoStartPoint::RawAutoStartPointControl::DisplayFit);
    }
    return controls;
}

inline void MarkRawStartingPointUpstreamApplied(
    RawWorkspaceAutoBaseUiState& ui,
    RawWorkspaceStartingPointPendingAction pending) {
    ui.hasAppliedViewFit = false;
    ui.startingPointDisplayFitPending = true;
    ui.summary = pending.planSummary;
    if (!ui.summary.empty()) {
        ui.summary += " ";
    }
    ui.summary += "Display Fit pending: rendering once more before final fit.";
    SetRawStartingPointUiResult(
        ui,
        ui.summary,
        pending.appliedControlsSummary,
        pending.withheldControlsSummary,
        pending.evidenceSummary,
        pending.warningSummary,
        pending.appliedValuesSummary);
    ui.pendingStartingPoint = std::move(pending);
}

inline std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>
MarkRawStartingPointPostFitApplied(
    RawWorkspaceAutoBaseUiState& ui,
    RawWorkspaceStartingPointPendingAction pending,
    std::uint64_t appliedAnalysisHash,
    std::string displayFitValueSummary = "Display Fit refreshed from post-edit analysis") {
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> touchedControls =
        RawStartingPointControlsWithDisplayFit(pending.appliedControls);
    ui.appliedAnalysisHash = appliedAnalysisHash;
    ui.hasAppliedViewFit = true;
    ui.startingPointDisplayFitPending = false;
    ui.viewTransformOwner = RawAutoValueOwner::AutoBase;
    ui.summary = pending.planSummary;
    if (!ui.summary.empty()) {
        ui.summary += " ";
    }
    ui.summary += "Display Fit refreshed from post-edit analysis.";
    std::vector<std::string> appliedValueParts;
    if (!IsRawStartingPointSummaryEmptyOrNone(pending.appliedValuesSummary)) {
        appliedValueParts.push_back(pending.appliedValuesSummary);
    }
    if (IsRawStartingPointSummaryEmptyOrNone(displayFitValueSummary)) {
        displayFitValueSummary = "Display Fit refreshed from post-edit analysis";
    }
    appliedValueParts.push_back(std::move(displayFitValueSummary));
    const std::string appliedValuesSummary =
        JoinRawStartingPointSummaryParts(appliedValueParts);
    SetRawStartingPointUiResult(
        ui,
        ui.summary,
        RawStartingPointControlLabels(touchedControls),
        pending.withheldControlsSummary,
        pending.evidenceSummary,
        pending.warningSummary,
        appliedValuesSummary);
    ui.pendingStartingPoint = RawWorkspaceStartingPointPendingAction();
    return touchedControls;
}

inline RawStartingPointContinuationDecision EvaluateRawStartingPointContinuation(
    const RawWorkspaceStartingPointPendingAction& pending,
    const RawStartingPointContinuationContext& context) {
    if (!pending.active) {
        return RawStartingPointContinuationDecision::Inactive;
    }
    if (!context.sourceExists) {
        return RawStartingPointContinuationDecision::CancelSourceMissing;
    }
    if (context.activeSourceKey != pending.sourceKey ||
        context.selectedSourceKey != pending.sourceKey ||
        context.sourceHash != pending.sourceHash) {
        return RawStartingPointContinuationDecision::CancelSourceChanged;
    }
    if (context.analysisSourceKey != pending.sourceKey || !context.analysisValid) {
        return RawStartingPointContinuationDecision::WaitForAnalysis;
    }
    if (context.recipeFingerprint != pending.recipeFingerprint) {
        return RawStartingPointContinuationDecision::CancelRecipeChanged;
    }
    if (pending.phase == RawStartingPointPendingPhase::WaitingForInitialAnalysis) {
        return RawStartingPointContinuationDecision::ContinueInitialAnalysis;
    }
    if (pending.phase == RawStartingPointPendingPhase::WaitingForPostApplyAnalysis) {
        return RawStartingPointContinuationDecision::ContinuePostApplyAnalysis;
    }
    return RawStartingPointContinuationDecision::CancelUnknownPhase;
}

inline const char* RawStartingPointContinuationCancellationReason(
    RawStartingPointContinuationDecision decision) {
    switch (decision) {
        case RawStartingPointContinuationDecision::CancelSourceMissing:
            return "source disappeared before the queued Starting Point action completed.";
        case RawStartingPointContinuationDecision::CancelSourceChanged:
            return "source changed before the queued Starting Point action completed.";
        case RawStartingPointContinuationDecision::CancelRecipeChanged:
            return "recipe changed before the queued Starting Point action completed.";
        case RawStartingPointContinuationDecision::CancelUnknownPhase:
            return "unknown pending Starting Point phase.";
        default:
            return "";
    }
}

inline std::string RawStartingPointContinuationCancellationReason(
    RawStartingPointContinuationDecision decision,
    const RawWorkspaceStartingPointPendingAction& pending,
    const RawStartingPointContinuationContext& context) {
    if (decision == RawStartingPointContinuationDecision::CancelSourceChanged) {
        if (context.activeSourceKey == pending.sourceKey &&
            context.selectedSourceKey == pending.sourceKey &&
            context.sourceHash != pending.sourceHash) {
            return "source identity changed before the queued Starting Point action completed.";
        }
        if (context.selectedSourceKey != pending.sourceKey) {
            return "selected source changed before the queued Starting Point action completed.";
        }
        if (context.activeSourceKey != pending.sourceKey) {
            return "active source changed before the queued Starting Point action completed.";
        }
    }
    return RawStartingPointContinuationCancellationReason(decision);
}

inline bool RawStartingPointContinuationCancels(
    RawStartingPointContinuationDecision decision) {
    return RawStartingPointContinuationCancellationReason(decision)[0] != '\0';
}

inline bool CancelRawStartingPointPendingAction(
    RawWorkspaceAutoBaseUiState& ui,
    std::string reason) {
    if (!ui.pendingStartingPoint.active) {
        return false;
    }
    if (reason.empty()) {
        reason = "source or recipe changed before the queued Starting Point action completed.";
    }
    const RawWorkspaceStartingPointPendingAction pending = ui.pendingStartingPoint;
    const bool upstreamControlsAlreadyApplied =
        pending.phase == RawStartingPointPendingPhase::WaitingForPostApplyAnalysis &&
        (!IsRawStartingPointSummaryEmptyOrNone(pending.appliedControlsSummary) ||
         !pending.appliedControls.empty());
    const std::string appliedControlsSummary =
        upstreamControlsAlreadyApplied
            ? (IsRawStartingPointSummaryEmptyOrNone(pending.appliedControlsSummary)
                ? RawStartingPointControlLabels(pending.appliedControls)
                : pending.appliedControlsSummary)
            : std::string("None");
    std::string withheldSummary = reason;
    if (upstreamControlsAlreadyApplied) {
        const std::string displayFitCanceled = "Display Fit canceled: " + reason;
        withheldSummary =
            IsRawStartingPointSummaryEmptyOrNone(pending.withheldControlsSummary)
                ? displayFitCanceled
                : pending.withheldControlsSummary + ", " + displayFitCanceled;
    }
    ui.pendingStartingPoint = RawWorkspaceStartingPointPendingAction();
    ui.startingPointDisplayFitPending = upstreamControlsAlreadyApplied;
    ui.summary = upstreamControlsAlreadyApplied
        ? "Build Starting Point canceled after applying upstream controls: " +
            reason + " Display Fit was not refreshed."
        : "Build Starting Point canceled: " + reason;
    SetRawStartingPointUiResult(
        ui,
        ui.summary,
        appliedControlsSummary,
        withheldSummary,
        pending.evidenceSummary,
        pending.warningSummary,
        pending.appliedValuesSummary);
    return true;
}

inline std::string MarkRawStartingPointPostFitFailure(
    RawWorkspaceAutoBaseUiState& ui,
    std::string reason) {
    if (reason.empty()) {
        reason = "post-edit analysis could not produce a Display Fit.";
    }
    const RawWorkspaceStartingPointPendingAction pending = ui.pendingStartingPoint;
    ui.pendingStartingPoint = RawWorkspaceStartingPointPendingAction();
    ui.startingPointDisplayFitPending = true;
    ui.summary =
        "Build Starting Point applied upstream controls, but Display Fit is still pending: " +
        reason + " Use Refit Display when analysis is ready.";
    const std::string withheldSummary =
        IsRawStartingPointSummaryEmptyOrNone(pending.withheldControlsSummary)
            ? "Display Fit pending: " + reason
            : pending.withheldControlsSummary + ", Display Fit pending: " + reason;
    SetRawStartingPointUiResult(
        ui,
        ui.summary,
        pending.appliedControlsSummary,
        withheldSummary,
        pending.evidenceSummary,
        pending.warningSummary,
        pending.appliedValuesSummary);
    return withheldSummary;
}

inline bool MarkRawStartingPointRenderFailure(
    RawWorkspaceAutoBaseUiState& ui,
    std::string reason) {
    if (!ui.pendingStartingPoint.active) {
        return false;
    }
    if (reason.empty()) {
        reason = "render failed before the queued Starting Point action completed.";
    }
    if (ui.pendingStartingPoint.phase == RawStartingPointPendingPhase::WaitingForPostApplyAnalysis) {
        MarkRawStartingPointPostFitFailure(
            ui,
            "render failed before post-edit Display Fit: " + reason);
        return true;
    }
    CancelRawStartingPointPendingAction(
        ui,
        "render failed before Starting Point analysis completed: " + reason);
    return true;
}

} // namespace Stack::EditorModuleTypes
