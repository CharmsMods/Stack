#include "Editor/EditorModule.h"

#include "Raw/RawAutoBase.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawLoader.h"
#include "Utils/ImGuiExtras.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void TooltipIfHovered(const char* text, ImGuiHoveredFlags flags = 0) {
    if (text != nullptr && text[0] != '\0' && ImGui::IsItemHovered(flags)) {
        ImGui::SetTooltip("%s", text);
    }
}

std::string EllipsizeTextToWidth(const std::string& text, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f) {
        return text;
    }
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }

    static constexpr const char* kEllipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
    if (ellipsisWidth >= maxWidth) {
        return kEllipsis;
    }

    std::string clipped = text;
    while (!clipped.empty()) {
        clipped.pop_back();
        const std::string candidate = clipped + kEllipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }
    return kEllipsis;
}

void RenderDisabledSummaryLine(const std::string& text, float width) {
    const std::string clipped = EllipsizeTextToWidth(text, std::max(32.0f, width));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(clipped.c_str());
    ImGui::PopStyleColor();
}

void RenderDisabledWrappedLine(const std::string& text, float width) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(32.0f, width));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

std::uint64_t MixHash(std::uint64_t seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

std::uint64_t HashString64(const std::string& value) {
    return static_cast<std::uint64_t>(std::hash<std::string>{}(value));
}

std::uint64_t HashFloat64(float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    return static_cast<std::uint64_t>(std::hash<float>{}(value));
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F');
    });
}

std::uint64_t BuildAnalysisHash(const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
    std::uint64_t seed = HashString64(analysis.sourceKey);
    seed = MixHash(seed, HashFloat64(stats.p01Ev));
    seed = MixHash(seed, HashFloat64(stats.p05Ev));
    seed = MixHash(seed, HashFloat64(stats.p50Ev));
    seed = MixHash(seed, HashFloat64(stats.p99Ev));
    seed = MixHash(seed, HashFloat64(stats.p999Ev));
    seed = MixHash(seed, HashFloat64(stats.dynamicRangeEv));
    seed = MixHash(seed, HashFloat64(analysis.highlight.displayClipPercent));
    seed = MixHash(seed, HashFloat64(analysis.highlight.hdrPixelPercent));
    return seed;
}

std::string BuildRecipeFingerprint(const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    return Stack::RawRecipe::SerializeRecipe(recipe).dump();
}

std::string JoinStringsComma(const std::vector<std::string>& parts) {
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
    return summary;
}

std::string JoinControlLabels(
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& controls) {
    if (controls.empty()) {
        return "None";
    }
    std::vector<std::string> labels;
    labels.reserve(controls.size());
    for (Stack::RawAutoStartPoint::RawAutoStartPointControl control : controls) {
        const std::string label = Stack::RawAutoStartPoint::ControlLabel(control);
        if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
            labels.push_back(label);
        }
    }
    return JoinStringsComma(labels);
}

std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> RemoveDisplayFitControl(
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> controls) {
    return Stack::EditorModuleTypes::RawStartingPointControlsWithoutDisplayFit(std::move(controls));
}

std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> MergeStartingPointControls(
    std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> controls,
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& additions) {
    for (Stack::RawAutoStartPoint::RawAutoStartPointControl control : additions) {
        if (std::find(controls.begin(), controls.end(), control) == controls.end()) {
            controls.push_back(control);
        }
    }
    return controls;
}

std::string MergeStartingPointSummaryText(
    const std::string& previous,
    const std::string& current) {
    if (Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(previous)) {
        return Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(current)
            ? std::string("None")
            : current;
    }
    if (Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(current) ||
        previous.find(current) != std::string::npos) {
        return previous;
    }
    return previous + "; " + current;
}

std::string StartingPointControlValueText(
    const std::string& values,
    Stack::RawAutoStartPoint::RawAutoStartPointControl control) {
    const char* alternateLabel =
        control == Stack::RawAutoStartPoint::RawAutoStartPointControl::DisplayFit
            ? "Display Fit"
            : nullptr;
    return Stack::EditorModuleTypes::RawStartingPointRelevantControlValueText(
        values,
        Stack::RawAutoStartPoint::ControlLabel(control),
        alternateLabel);
}

std::string MergeStartingPointAppliedValues(
    const std::string& previousValues,
    const std::string& currentValues,
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& cumulativeControls,
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& currentControls) {
    std::vector<std::string> parts;
    parts.reserve(cumulativeControls.size());
    for (Stack::RawAutoStartPoint::RawAutoStartPointControl control : cumulativeControls) {
        const bool changedThisPass =
            std::find(currentControls.begin(), currentControls.end(), control) !=
            currentControls.end();
        std::string value = StartingPointControlValueText(
            changedThisPass ? currentValues : previousValues,
            control);
        if (value.empty() && changedThisPass) {
            value = StartingPointControlValueText(previousValues, control);
        }
        if (!value.empty()) {
            parts.push_back(std::move(value));
        }
    }
    if (parts.empty()) {
        return "None";
    }
    std::string summary;
    for (const std::string& part : parts) {
        if (!summary.empty()) {
            summary += "; ";
        }
        summary += part;
    }
    return summary;
}

std::string JoinPlanSummaries(const std::vector<std::string>& parts) {
    const std::string joined = JoinStringsComma(parts);
    return joined.empty() ? "None" : joined;
}

bool IsEmptyOrNoneSummary(const std::string& summary) {
    return Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(summary);
}

std::string BuildStartingPointDisplaySummary(std::string summary) {
    if (summary.empty()) {
        return "No Build Starting Point result yet.";
    }

    const std::string withheldMarker = " Withheld:";
    const std::size_t withheldAt = summary.find(withheldMarker);
    if (withheldAt != std::string::npos) {
        summary.erase(withheldAt);
    }

    const std::string planPrefix = "Build Starting Point plan: ";
    if (summary.rfind(planPrefix, 0) == 0) {
        summary = summary.substr(planPrefix.size());
    }

    const std::string pendingPrefix = "Build Starting Point pending: ";
    if (summary.rfind(pendingPrefix, 0) == 0) {
        summary = "Pending: " + summary.substr(pendingPrefix.size());
    }

    const std::string canceledPrefix = "Build Starting Point canceled: ";
    if (summary.rfind(canceledPrefix, 0) == 0) {
        summary = "Canceled: " + summary.substr(canceledPrefix.size());
    }

    if (!summary.empty() && summary.back() != '.') {
        summary += ".";
    }
    return summary;
}

void SetStartingPointUiResult(
    Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState& ui,
    std::string summary,
    std::string appliedControls,
    std::string withheldControls,
    std::string evidenceSummary = std::string(),
    std::string warningSummary = std::string(),
    std::string appliedValuesSummary = std::string()) {
    Stack::EditorModuleTypes::SetRawStartingPointUiResult(
        ui,
        std::move(summary),
        std::move(appliedControls),
        std::move(withheldControls),
        std::move(evidenceSummary),
        std::move(warningSummary),
        std::move(appliedValuesSummary));
}

void ClearStartingPointUiResult(
    Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState& ui) {
    Stack::EditorModuleTypes::ClearRawStartingPointUiResult(ui);
}

void AppendStartingPointActionDiagnostics(
    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics,
    const Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan& plan,
    const char* state,
    const char* pendingState,
    bool undoAvailable) {
    const bool displayFitPending =
        pendingState != nullptr &&
        std::string(pendingState) != "None";
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> appliedControls =
        displayFitPending
            ? RemoveDisplayFitControl(plan.visibleEdits.touchedControls)
            : plan.visibleEdits.touchedControls;
    diagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Starting Point action",
        state == nullptr ? "Ready" : state,
        plan.summary
    });
    diagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Starting Point applied controls",
        JoinControlLabels(appliedControls),
        displayFitPending
            ? "Visible upstream controls applied so far. Display Fit is still pending from the post-edit analysis."
            : "Visible recipe controls only. Applied values are undoable from the automatic-action snapshot."
    });
    diagnostics.uiView.lines.push_back({
        plan.withheldSummaries.empty()
            ? Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info
            : Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Warning,
        "Starting Point withheld controls",
        JoinPlanSummaries(plan.withheldSummaries),
        "Withheld controls remain available as manual edits or explicit secondary actions."
    });
    diagnostics.uiView.lines.push_back({
        plan.evidenceSummaries.empty()
            ? Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Warning
            : Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Starting Point evidence",
        JoinPlanSummaries(plan.evidenceSummaries),
        "Names the staged evidence trusted by the one-click plan; pending entries explain which candidate render is still needed."
    });
    diagnostics.uiView.lines.push_back({
        plan.warnings.empty()
            ? Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info
            : Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Warning,
        "Starting Point warnings",
        JoinPlanSummaries(plan.warnings),
        "Warnings name partial evidence, stale stages, or safety reasons that limited the one-click visible-control plan."
    });
    diagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Starting Point pending state",
        pendingState == nullptr ? "None" : pendingState,
        "Queued Starting Point work is source- and recipe-fingerprinted before it can continue."
    });
    diagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Starting Point undo",
        undoAvailable ? "Available" : "Unavailable",
        "Undo restores the recipe snapshot from before the one-click Starting Point action."
    });
}

bool SourceHasStoredProject(const Stack::RawWorkspace::SourceRecord& source) {
    return source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded;
}

std::string BuildAppliedSummary() {
    return "Fit Display applied: View Transform fit from current frame. RAW Exposure unchanged.";
}

std::string BuildUnchangedSummary() {
    return "RAW Exposure unchanged. White balance unchanged. No local edits applied.";
}

std::string FormatSignedEv(float value) {
    std::ostringstream out;
    out << (value >= 0.0f ? "+" : "") << std::fixed << std::setprecision(2) << value << " EV";
    return out.str();
}

std::string FormatStartingPointDisplayFitValueSummary(
    const Stack::RawAutoBase::ViewTransformFit& fit) {
    if (!fit.valid) {
        return "Display Fit refreshed from post-edit analysis";
    }
    std::ostringstream out;
    out << "Display Fit refreshed: middle grey " << std::fixed << std::setprecision(3)
        << fit.middleGrey
        << ", black " << std::setprecision(2) << fit.blackEv << " EV"
        << ", white +" << fit.whiteEv << " EV"
        << ", shoulder " << fit.shoulder
        << ", toe " << fit.toe;
    return out.str();
}

std::string BuildStartingPointAppliedValuesSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointVisibleRecipeEdits& edits,
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& controls) {
    std::vector<std::string> parts;
    for (Stack::RawAutoStartPoint::RawAutoStartPointControl control : controls) {
        switch (control) {
            case Stack::RawAutoStartPoint::RawAutoStartPointControl::RawExposure:
                if (edits.rawExposureEvValid) {
                    parts.push_back("RAW Exposure " + FormatSignedEv(edits.preToneExposureEv));
                }
                break;
            case Stack::RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance:
                if (edits.whiteBalanceValid) {
                    parts.push_back(edits.whiteBalancePolicy.empty()
                        ? std::string("White Balance updated")
                        : "White Balance " + edits.whiteBalancePolicy);
                }
                break;
            case Stack::RawAutoStartPoint::RawAutoStartPointControl::LocalRange:
                if (edits.localRangeValid) {
                    parts.push_back(edits.localRangeSummary.empty()
                        ? ("Local Range " + std::to_string(edits.localRangePointCount) + " point(s)")
                        : edits.localRangeSummary);
                }
                break;
            case Stack::RawAutoStartPoint::RawAutoStartPointControl::FinishTone:
                if (edits.finishToneValid) {
                    parts.push_back(edits.finishToneSummary.empty()
                        ? std::string("Finish Tone mild visible curve")
                        : edits.finishToneSummary);
                }
                break;
            case Stack::RawAutoStartPoint::RawAutoStartPointControl::DisplayFit:
                if (edits.displayFitValid) {
                    parts.push_back(edits.viewTransformSummary.empty()
                        ? std::string("Display Fit refreshed")
                        : edits.viewTransformSummary);
                }
                break;
        }
    }
    const std::string summary = JoinStringsComma(parts);
    return summary.empty() ? "None" : summary;
}

std::string LocalSuggestionLabel(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    return suggestion.label.empty()
        ? std::string(Stack::RawAutoBase::SuggestedLocalAdjustmentKindLabel(suggestion.kind))
        : suggestion.label;
}

std::string BuildBalancedLocalAppliedSummary(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& appliedSuggestions) {
    std::ostringstream out;
    out << "Balanced Local applied: "
        << appliedSuggestions.size()
        << (appliedSuggestions.size() == 1
            ? " visible Local Range adjustment point"
            : " visible Local Range adjustment points");
    if (!appliedSuggestions.empty()) {
        out << " (";
        for (std::size_t i = 0; i < appliedSuggestions.size(); ++i) {
            if (i > 0) {
                out << "; ";
            }
            const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion =
                appliedSuggestions[i];
            out << LocalSuggestionLabel(suggestion)
                << " " << FormatSignedEv(suggestion.deltaEv);
            if (suggestion.colorQualifierEnabled) {
                out << " color-targeted";
            }
        }
        out << ")";
    }
    out << ". RAW Exposure unchanged. White balance unchanged. Display Fit unchanged. Finish Tone unchanged.";
    return out.str();
}

std::string BuildMildFinishToneAppliedSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointFinishToneProposal& proposal) {
    std::ostringstream out;
    out << "Mild Finish Tone applied: visible 5-point log-scene graph";
    if (std::isfinite(proposal.strength)) {
        out << ", strength " << std::fixed << std::setprecision(2) << proposal.strength;
    }
    out << ". RAW Exposure unchanged. White balance unchanged. Local Range unchanged. Display Fit unchanged.";
    return out.str();
}

nlohmann::json BuildVisibleFinishToneLayerJson(
    const nlohmann::json& currentLayerJson,
    const Stack::RawAutoStartPoint::RawAutoStartPointFinishToneProposal& proposal) {
    nlohmann::json finishTone = currentLayerJson.is_object()
        ? currentLayerJson
        : Stack::RawRecipe::DefaultFinishToneJson();
    finishTone["type"] = "ToneCurve";
    const auto copyVisibleKey = [&](const char* key) {
        const auto it = proposal.layerJson.find(key);
        if (it != proposal.layerJson.end()) {
            finishTone[key] = *it;
        }
    };
    copyVisibleKey("mode");
    copyVisibleKey("domain");
    copyVisibleKey("activeGraphView");
    copyVisibleKey("logMinEv");
    copyVisibleKey("logMaxEv");
    copyVisibleKey("points");
    copyVisibleKey("preparedPoints");
    return finishTone;
}

const Stack::RawAutoStartPoint::RawAutoStartPointCandidate* FindStartPointCandidate(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics,
    Stack::RawAutoStartPoint::RawAutoStartPointCandidateKind kind) {
    for (const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate :
         diagnostics.candidates) {
        if (candidate.kind == kind) {
            return &candidate;
        }
    }
    return nullptr;
}

const char* WhiteBalanceMethodLabel(Stack::RawAutoBase::WhiteBalanceRecommendation::Method method) {
    switch (method) {
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::CameraAsShot:
            return "Camera/as-shot";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld:
            return "Gray World";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::ShadesOfGray:
            return "Shades of Gray";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::GreyEdge:
            return "Grey Edge";
        default:
            return "Unknown";
    }
}

struct AutoBaseRecommendationCounts {
    int suggestions = 0;
    int warnings = 0;
    bool hasNoiseAdvisory = false;
};

AutoBaseRecommendationCounts CountAutoBaseRecommendations(
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations) {
    AutoBaseRecommendationCounts counts;

    const Stack::RawAutoBase::RawExposureRecommendation& exposure =
        recommendations.exposure;
    if (exposure.valid &&
        exposure.action == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue &&
        !exposure.blockedByHighlightRisk) {
        ++counts.suggestions;
    }
    if (exposure.blockedByHighlightRisk) {
        ++counts.warnings;
    }

    const Stack::RawAutoBase::WhiteBalanceRecommendation& whiteBalance =
        recommendations.whiteBalance;
    if (whiteBalance.alternateCandidateAvailable &&
        whiteBalance.action == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue &&
        !whiteBalance.manualWhiteBalanceProtected) {
        ++counts.suggestions;
    }

    const Stack::RawAutoBase::HighlightRecommendation& highlight =
        recommendations.highlight;
    if (highlight.recommendProtectiveViewShoulder &&
        highlight.protectionAction == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue) {
        ++counts.suggestions;
    }
    if (highlight.recommendNoPositiveRawExposure ||
        highlight.recommendReconstruction ||
        highlight.recommendAchromaticClip) {
        ++counts.warnings;
    }

    for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion :
         recommendations.localAdjustments) {
        if (suggestion.valid) {
            ++counts.suggestions;
        }
    }

    const Stack::RawAutoBase::NoiseDetailRecommendation& noiseDetail =
        recommendations.noiseDetail;
    counts.hasNoiseAdvisory =
        noiseDetail.suggestChromaDenoise ||
        noiseDetail.suggestLumaDenoise ||
        noiseDetail.suggestReduceSharpening ||
        noiseDetail.shadowLiftEv >= 0.5f;
    return counts;
}

std::string FormatCountBadge(int count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

std::string JoinAssistSummaryParts(const std::vector<std::string>& parts) {
    std::string summary;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!summary.empty()) {
            summary += "  |  ";
        }
        summary += part;
    }
    return summary;
}

std::string JoinCommaSummaryParts(const std::vector<std::string>& parts) {
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
    return summary;
}

std::string BuildBaseAssistReadinessSummary(
    bool hasUsableStats,
    bool hasAppliedViewFit,
    bool canRevert) {
    std::vector<std::string> parts;
    parts.push_back(hasUsableStats ? "Ready" : "Will analyze first");
    if (hasUsableStats && hasAppliedViewFit) {
        parts.push_back("Display Fit set");
    }
    if (canRevert) {
        parts.push_back("Undo available");
    }
    return JoinAssistSummaryParts(parts);
}

std::string BuildBaseAssistDisplayFitStateSummary(
    bool hasUsableStats,
    bool hasAppliedViewFit,
    bool startingPointDisplayFitPending,
    Stack::EditorModuleTypes::RawAutoValueOwner owner,
    std::uint64_t appliedAnalysisHash,
    std::uint64_t currentAnalysisHash) {
    if (startingPointDisplayFitPending) {
        return hasUsableStats
            ? "Display Fit state: Refit pending. Build Starting Point applied upstream controls and still needs a current Display Fit."
            : "Display Fit state: Refit pending. Render or analyze a preview before finishing Display Fit.";
    }

    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::User) {
        return "Display Fit state: Manual/locked. Refit Display is explicit; automatic fitting will not rewrite manual View Transform edits.";
    }

    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::AutoBase && hasAppliedViewFit) {
        if (!hasUsableStats) {
            return "Display Fit state: Auto Fit recorded. Render or analyze a preview to confirm it against current frame statistics.";
        }
        if (appliedAnalysisHash != 0 && appliedAnalysisHash == currentAnalysisHash) {
            return "Display Fit state: Auto-current. The displayed View Transform was fit from the current analysis.";
        }
        return "Display Fit state: Auto-stale. The current analysis differs from the last Display Fit; use Refit Display when ready.";
    }

    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::AutoBase) {
        return "Display Fit state: Auto-adjusted. A visible View Transform adjustment is auto-owned, but it was not recorded as a full Display Fit.";
    }

    if (!hasUsableStats) {
        return "Display Fit state: Preview pending. Render or analyze a preview before fitting the display.";
    }

    return "Display Fit state: Ready. Fit Display can map the current edit without changing RAW Exposure.";
}

std::string BuildSelectedStartPointInlineSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics,
    bool hasUsableStats) {
    if (!hasUsableStats || diagnostics.candidates.empty()) {
        return "Preview analysis pending. Build Starting Point will analyze before writing visible controls.";
    }
    if (!diagnostics.hasSelectedCandidate ||
        diagnostics.selectedCandidateIndex < 0 ||
        diagnostics.selectedCandidateIndex >= static_cast<int>(diagnostics.candidates.size())) {
        return "No candidate selected yet. Open Diagnostics after preview analysis for candidate details.";
    }

    const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate =
        diagnostics.candidates[static_cast<std::size_t>(diagnostics.selectedCandidateIndex)];
    const Stack::RawAutoStartPoint::RawAutoStartPointVisibleRecipeEdits& edits =
        candidate.visibleEdits;
    auto controlTouched = [&](Stack::RawAutoStartPoint::RawAutoStartPointControl control) {
        return std::find(
            edits.touchedControls.begin(),
            edits.touchedControls.end(),
            control) != edits.touchedControls.end();
    };
    std::vector<std::string> changingParts;
    std::vector<std::string> manualParts;
    if (edits.rawExposureEvValid) {
        const std::string summary = "RAW Exposure " + FormatSignedEv(edits.preToneExposureEv);
        if (controlTouched(Stack::RawAutoStartPoint::RawAutoStartPointControl::RawExposure)) {
            changingParts.push_back(summary);
        } else {
            manualParts.push_back(summary + " kept/manual");
        }
    }
    if (std::find(
            edits.touchedControls.begin(),
            edits.touchedControls.end(),
            Stack::RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) !=
        edits.touchedControls.end()) {
        changingParts.push_back("Suggested WB");
    } else if (edits.whiteBalanceValid && !edits.whiteBalancePolicy.empty()) {
        manualParts.push_back("White Balance " + edits.whiteBalancePolicy);
    }
    if (edits.displayFitValid) {
        const std::string summary = edits.viewTransformSummary.empty()
            ? std::string("Display Fit proposed")
            : edits.viewTransformSummary;
        if (controlTouched(Stack::RawAutoStartPoint::RawAutoStartPointControl::DisplayFit)) {
            changingParts.push_back(summary);
        } else {
            manualParts.push_back(summary);
        }
    }
    if (edits.localRangeValid || !edits.localRangeSummary.empty()) {
        const std::string summary = edits.localRangeSummary.empty()
            ? ("Local Range " + std::to_string(std::max(0, edits.localRangePointCount)) + " point(s)")
            : "Local Range " + edits.localRangeSummary;
        if (controlTouched(Stack::RawAutoStartPoint::RawAutoStartPointControl::LocalRange)) {
            changingParts.push_back(summary);
        } else {
            manualParts.push_back(summary);
        }
    }
    if (edits.finishToneValid || !edits.finishToneSummary.empty()) {
        const std::string summary = edits.finishToneSummary.empty()
            ? std::string("Finish Tone proposed")
            : "Finish Tone " + edits.finishToneSummary;
        if (controlTouched(Stack::RawAutoStartPoint::RawAutoStartPointControl::FinishTone)) {
            changingParts.push_back(summary);
        } else {
            manualParts.push_back(summary);
        }
    }

    const std::string candidateLabel = candidate.label.empty()
        ? Stack::RawAutoStartPoint::CandidateKindLabel(candidate.kind)
        : candidate.label;
    return candidateLabel +
        ". Would change: " +
        (changingParts.empty() ? std::string("None") : JoinCommaSummaryParts(changingParts)) +
        ". Manual/pending: " +
        (manualParts.empty() ? std::string("None") : JoinCommaSummaryParts(manualParts)) +
        ".";
}

std::string BuildCurrentStartingPointPlanInlineSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan& plan,
    bool hasUsableStats) {
    if (!hasUsableStats) {
        return "Preview analysis pending. Build Starting Point will analyze before writing visible controls.";
    }
    if (!plan.valid) {
        return plan.summary.empty()
            ? "One-click plan unavailable until candidate diagnostics are ready."
            : plan.summary;
    }

    const std::string changedValues =
        BuildStartingPointAppliedValuesSummary(
            plan.visibleEdits,
            plan.visibleEdits.touchedControls);
    const std::string withheld = JoinPlanSummaries(plan.withheldSummaries);

    std::ostringstream out;
    out << "Would change: " << changedValues;
    if (!IsEmptyOrNoneSummary(withheld)) {
        out << ". Left manual/pending: " << withheld;
    }
    out << ".";
    return out.str();
}

std::string ReplaceAllCopy(std::string value, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return value;
    }
    std::size_t at = 0;
    while ((at = value.find(from, at)) != std::string::npos) {
        value.replace(at, from.size(), to);
        at += to.size();
    }
    return value;
}

std::string BuildCurrentStartingPointControlPlanSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan& plan,
    bool hasUsableStats) {
    if (!hasUsableStats) {
        return Stack::EditorModuleTypes::BuildRawStartingPointControlStatusSummary(
            "Preview analysis pending",
            "None",
            "Preview analysis pending",
            "None");
    }

    const std::string changedControls =
        plan.valid
            ? JoinControlLabels(plan.visibleEdits.touchedControls)
            : std::string("None");
    const std::string changedValues =
        plan.valid
            ? BuildStartingPointAppliedValuesSummary(
                plan.visibleEdits,
                plan.visibleEdits.touchedControls)
            : std::string("None");
    const std::string withheld =
        plan.valid
            ? JoinPlanSummaries(plan.withheldSummaries)
            : std::string("None");
    const std::string summary =
        plan.summary.empty()
            ? std::string("One-click plan unavailable")
            : plan.summary;
    return ReplaceAllCopy(
        Stack::EditorModuleTypes::BuildRawStartingPointControlStatusSummary(
            summary,
            changedControls,
            withheld,
            changedValues),
        ": changed",
        ": would change");
}

std::string BuildCurrentStartingPointPlanValuesSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan& plan,
    bool hasUsableStats) {
    if (!hasUsableStats || !plan.valid) {
        return "None";
    }
    return BuildStartingPointAppliedValuesSummary(
        plan.visibleEdits,
        plan.visibleEdits.touchedControls);
}

std::string BuildBaseAssistActionScopeSummary() {
    return "Action scope: Build Starting Point is the primary one-click base action and writes safe visible controls. "
        "Advanced Local Range and Mild Tone actions are temporary scaffolding for inspecting individual control writes. "
        "Suggested WB is applied only when camera/as-shot metadata is unavailable and neutral evidence is strong.";
}

const char* WhiteBalanceBadgeLabel(Stack::RawRecipe::WhiteBalanceMode mode) {
    switch (mode) {
        case Stack::RawRecipe::WhiteBalanceMode::AsShot:
            return "WB as shot";
        case Stack::RawRecipe::WhiteBalanceMode::Auto:
            return "WB auto";
        case Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers:
            return "WB custom";
        case Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint:
            return "WB gray point";
        default:
            return "WB set";
    }
}

void RenderAutoBasePill(const char* label, bool warning = false) {
    const ImVec4 base = ImGui::GetStyleColorVec4(warning ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg);
    const ImVec4 hover = ImGui::GetStyleColorVec4(warning ? ImGuiCol_ButtonHovered : ImGuiCol_FrameBgHovered);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, base);
    ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

} // namespace

void EditorModule::ResetRawWorkspaceAutoBaseState() {
    m_RawWorkspaceAutoBaseUi = RawWorkspaceAutoBaseUiState();
    Stack::EditorModuleTypes::ClearRawStartingPointCandidateEvidenceCache(
        m_RawWorkspaceStartPointDiagnostics,
        m_RawWorkspaceStartPointCandidateRenderQueue,
        m_RawWorkspaceStartPointCandidateRenderResults);
}

void EditorModule::CancelRawWorkspacePendingStartingPoint(std::string reason) {
    Stack::EditorModuleTypes::CancelRawStartingPointPendingAction(
        m_RawWorkspaceAutoBaseUi,
        std::move(reason));
}

void EditorModule::CaptureRawWorkspaceAutoBaseRevertSnapshotForSelectedSource(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    if (m_ActiveRawWorkspaceSourceKey.empty()) {
        return;
    }

    const Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    const bool hasSourceIdentity = source != nullptr;
    const std::uint64_t sourceHash =
        hasSourceIdentity ? BuildRawWorkspaceAutoBaseSourceHash(*source) : 0;
    m_RawWorkspaceAutoBaseUi.beforeAutoBase = recipe;
    m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;
    m_RawWorkspaceAutoBaseUi.sourceKey = m_ActiveRawWorkspaceSourceKey;
    if (hasSourceIdentity) {
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }
}

std::uint64_t EditorModule::BuildRawWorkspaceAutoBaseSourceHash(
    const Stack::RawWorkspace::SourceRecord& source) const {
    std::uint64_t seed = HashString64(source.relativePathKey);
    seed = MixHash(seed, HashString64(source.absolutePath.string()));
    seed = MixHash(seed, HashString64(source.fingerprint));
    seed = MixHash(seed, static_cast<std::uint64_t>(source.fileSizeBytes));
    seed = MixHash(seed, static_cast<std::uint64_t>(source.modifiedTimeTicks));
    return seed;
}

bool EditorModule::RawWorkspaceRecipeLooksDefaultForAutoBase(
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) const {
    Stack::RawRecipe::RawDevelopmentRecipe defaults = BuildRawWorkspaceDefaultRecipe(source);
    return Stack::RawRecipe::SerializeRecipe(defaults).dump() ==
        Stack::RawRecipe::SerializeRecipe(recipe).dump();
}

bool EditorModule::RawWorkspaceViewTransformAutoOwnedForSource(const std::string& sourceKey) const {
    return !sourceKey.empty() &&
        m_RawWorkspaceAutoBaseUi.sourceKey == sourceKey &&
        m_RawWorkspaceAutoBaseUi.viewTransformOwner == RawAutoValueOwner::AutoBase;
}

Stack::RawAnalysis::RawMetadataSummary EditorModule::ResolveRawWorkspaceMetadataSummaryForAutoBase() const {
    const std::string activeSourcePath = m_ActiveRawWorkspaceRecipe.source.sourcePath;
    auto summaryFromMetadata = [](const Raw::RawMetadata& metadata) {
        return Stack::RawAnalysis::BuildRawMetadataSummary(metadata);
    };

    if (m_ActiveManagedRawSection.rawSourceNodeId > 0) {
        const EditorNodeGraph::Node* rawSourceNode =
            m_NodeGraph.FindNode(m_ActiveManagedRawSection.rawSourceNodeId);
        if (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
            return summaryFromMetadata(rawSourceNode->rawSource.metadata);
        }
    }

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawSource) {
            continue;
        }
        if (activeSourcePath.empty() || node.rawSource.sourcePath == activeSourcePath) {
            return summaryFromMetadata(node.rawSource.metadata);
        }
    }

    if (!activeSourcePath.empty()) {
        Raw::RawMetadata metadata;
        Raw::RawLoader::LoadMetadata(activeSourcePath, metadata);
        return summaryFromMetadata(metadata);
    }

    return Stack::RawAnalysis::RawMetadataSummary();
}

void EditorModule::RefreshRawWorkspaceAutoBaseRecommendations(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const Stack::RawAutoBase::AutoBaseRecommendations previousRecommendations =
        m_RawWorkspaceAutoBaseUi.recommendations;
    if (m_ActiveRawWorkspaceSourceKey.empty() ||
        m_RawWorkspaceAnalysis.sourceKey != m_ActiveRawWorkspaceSourceKey ||
        !m_RawWorkspaceAnalysis.currentFrameStats.valid) {
        m_RawWorkspaceAutoBaseUi.recommendations =
            Stack::RawAutoBase::AutoBaseRecommendations();
        return;
    }

    if (m_RawWorkspaceAutoBaseUi.hasMetadataSummary) {
        m_RawWorkspaceAnalysis.metadata = m_RawWorkspaceAutoBaseUi.metadataSummary;
    }
    Stack::RawAutoBase::AutoBaseRecommendations updated =
        Stack::RawAutoBase::BuildAutoBaseRecommendations(m_RawWorkspaceAnalysis, recipe);
    if (previousRecommendations.localReport.valid ||
        !previousRecommendations.localAdjustments.empty() ||
        !previousRecommendations.localSuggestionRationale.empty()) {
        updated.localAdjustments = previousRecommendations.localAdjustments;
        updated.localReport = previousRecommendations.localReport;
        updated.localSuggestionRationale = previousRecommendations.localSuggestionRationale;
    }
    updated.noiseDetail =
        Stack::RawAutoBase::BuildNoiseDetailRecommendation(
            m_RawWorkspaceAnalysis,
            recipe,
            &updated.localAdjustments,
            false);
    m_RawWorkspaceAutoBaseUi.recommendations = std::move(updated);
}

void EditorModule::MarkRawWorkspaceViewTransformUserEdited() {
    if (m_ActiveRawWorkspaceSourceKey.empty() ||
        m_RawWorkspaceAutoBaseUi.sourceKey != m_ActiveRawWorkspaceSourceKey) {
        return;
    }
    if (m_RawWorkspaceAutoBaseUi.viewTransformOwner != RawAutoValueOwner::AutoBase) {
        return;
    }
    m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::User;
    m_RawWorkspaceAutoBaseUi.pendingStartingPoint =
        Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction();
    m_RawWorkspaceAutoBaseUi.startingPointDisplayFitPending = false;
    m_RawWorkspaceAutoBaseUi.summary =
        "View Transform edited manually. RAW Exposure unchanged. No local edits applied.";
}

bool EditorModule::ApplyRawWorkspaceAutoBaseViewFitForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool explicitApply) {
    if (source.relativePathKey.empty() ||
        source.relativePathKey != m_ActiveRawWorkspaceSourceKey ||
        source.relativePathKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(source);
    if (m_RawWorkspaceAutoBaseUi.sourceKey != source.relativePathKey ||
        m_RawWorkspaceAutoBaseUi.sourceHash != sourceHash) {
        ResetRawWorkspaceAutoBaseState();
        m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }

    if (!explicitApply) {
        if (m_RawWorkspaceAutoBaseUi.hasAppliedViewFit ||
            m_RawWorkspaceAutoBaseUi.viewTransformOwner == RawAutoValueOwner::User) {
            return false;
        }
        if (SourceHasStoredProject(source) ||
            !RawWorkspaceRecipeLooksDefaultForAutoBase(source, recipe)) {
            m_RawWorkspaceAutoBaseUi.summary =
                "Display Fit ready: existing or edited RAW recipes are not refit automatically.";
            return false;
        }
    }

    const Stack::RawAutoBase::ViewFitDecision decision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(m_RawWorkspaceAnalysis, recipe);
    if (!decision.canApply) {
        m_RawWorkspaceAutoBaseUi.summary = decision.summary.empty()
            ? "Display Fit pending: render preview to analyze the frame."
            : decision.summary;
        if (explicitApply) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "Render a RAW preview before fitting the display.",
                "raw-workspace-auto-base-no-stats");
        }
        return false;
    }

    if (!m_RawWorkspaceAutoBaseUi.hasRevertSnapshot ||
        m_RawWorkspaceAutoBaseUi.sourceKey != source.relativePathKey) {
        m_RawWorkspaceAutoBaseUi.beforeAutoBase = recipe;
        m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;
    }

    Stack::RawAutoBase::ApplyViewTransformFitToRecipe(recipe, decision.fit);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    recipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.appliedAnalysisHash = BuildAnalysisHash(m_RawWorkspaceAnalysis);
    m_RawWorkspaceAutoBaseUi.hasAppliedViewFit = true;
    m_RawWorkspaceAutoBaseUi.startingPointDisplayFitPending = false;
    m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::AutoBase;
    m_RawWorkspaceAutoBaseUi.summary = BuildAppliedSummary() + " " + BuildUnchangedSummary();
    RefreshRawWorkspaceAutoBaseRecommendations(recipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceBuildStartingPointForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    if (source.relativePathKey.empty() ||
        source.relativePathKey != m_ActiveRawWorkspaceSourceKey ||
        source.relativePathKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(source);
    if (m_RawWorkspaceAutoBaseUi.sourceKey != source.relativePathKey ||
        m_RawWorkspaceAutoBaseUi.sourceHash != sourceHash) {
        ResetRawWorkspaceAutoBaseState();
        m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }

    if (m_RawWorkspaceAnalysis.sourceKey != source.relativePathKey ||
        !m_RawWorkspaceAnalysis.currentFrameStats.valid) {
        Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction pending;
        pending.active = true;
        pending.phase =
            Stack::EditorModuleTypes::RawStartingPointPendingPhase::WaitingForInitialAnalysis;
        pending.sourceKey = source.relativePathKey;
        pending.sourceHash = sourceHash;
        pending.recipeFingerprint = BuildRecipeFingerprint(editedRecipe);
        pending.originalRecipe = editedRecipe;
        pending.plannedUpstreamRecipe = editedRecipe;
        pending.planSummary = "Build Starting Point queued until preview analysis is ready.";
        pending.appliedControlsSummary = "None";
        pending.withheldControlsSummary = "Preview analysis pending";
        pending.evidenceSummary = "Preview analysis pending";
        m_RawWorkspaceAutoBaseUi.pendingStartingPoint = std::move(pending);
        m_RawWorkspaceAutoBaseUi.summary =
            "Build Starting Point pending: analyzing this RAW before applying visible controls.";
        SetStartingPointUiResult(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAutoBaseUi.summary,
            "None",
            "Preview analysis pending",
            "Preview analysis pending");
        MarkRenderRefreshDirty();
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Analyzing this RAW before building the Starting Point.",
            "raw-workspace-starting-point-base-no-stats");
        return false;
    }

    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            editedRecipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations);
    if (Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
            m_RawWorkspaceStartPointCandidateRenderQueue,
            source.relativePathKey,
            sourceHash)) {
        m_RawWorkspaceStartPointCandidateRenderResults.clear();
    }
    if (Stack::EditorModuleTypes::RawStartingPointCandidateRenderResultsMatchSource(
            m_RawWorkspaceStartPointCandidateRenderQueue,
            m_RawWorkspaceStartPointCandidateRenderResults,
            source.relativePathKey,
            sourceHash)) {
        m_RawWorkspaceStartPointDiagnostics =
            Stack::RawAutoStartPoint::MergeCandidateRenderResults(
                std::move(m_RawWorkspaceStartPointDiagnostics),
                m_RawWorkspaceStartPointCandidateRenderResults,
                editedRecipe,
                m_RawWorkspaceAnalysis,
                m_RawWorkspaceAutoBaseUi.recommendations);
    } else {
        m_RawWorkspaceStartPointCandidateRenderResults.clear();
    }

    const Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan plan =
        Stack::RawAutoStartPoint::BuildConservativeStartingPointPlan(
            editedRecipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations,
            m_RawWorkspaceStartPointDiagnostics);
    if (!plan.valid) {
        m_RawWorkspaceAutoBaseUi.summary =
            plan.summary.empty()
                ? "Build Starting Point unavailable: candidate diagnostics are not ready."
                : plan.summary;
        SetStartingPointUiResult(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAutoBaseUi.summary,
            "None",
            JoinPlanSummaries(plan.withheldSummaries),
            JoinPlanSummaries(plan.evidenceSummaries),
            JoinPlanSummaries(plan.warnings));
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Build Starting Point needs current analysis before applying visible controls.",
            "raw-workspace-starting-point-base-no-candidate");
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = plan.upstreamRecipe;
    if (!plan.hasUpstreamRecipeChanges) {
        const Stack::RawAutoBase::ViewFitDecision decision =
            Stack::RawAutoBase::BuildAutoBaseViewFitDecision(m_RawWorkspaceAnalysis, recipe);
        if (!decision.canApply) {
            m_RawWorkspaceAutoBaseUi.summary = decision.summary.empty()
                ? "Build Starting Point pending: render preview to analyze the frame."
                : decision.summary;
            SetStartingPointUiResult(
                m_RawWorkspaceAutoBaseUi,
                m_RawWorkspaceAutoBaseUi.summary,
                "None",
                "Display Fit pending",
                JoinPlanSummaries(plan.evidenceSummaries),
                JoinPlanSummaries(plan.warnings));
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "Render a RAW preview before finishing Display Fit.",
                "raw-workspace-starting-point-no-fit");
            return false;
        }
        Stack::RawAutoBase::ApplyViewTransformFitToRecipe(recipe, decision.fit);
    }

    m_RawWorkspaceAutoBaseUi.beforeAutoBase = editedRecipe;
    m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;

    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    const bool waitingForPostApplyDisplayFit =
        plan.hasUpstreamRecipeChanges && plan.needsPostApplyDisplayFit;
    if (waitingForPostApplyDisplayFit) {
        Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction pending;
        pending.active = true;
        pending.phase =
            Stack::EditorModuleTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
        pending.upstreamApplyPassCount = 1;
        pending.sourceKey = source.relativePathKey;
        pending.sourceHash = sourceHash;
        pending.recipeFingerprint = BuildRecipeFingerprint(editedRecipe);
        pending.originalRecipe = m_RawWorkspaceAutoBaseUi.beforeAutoBase;
        pending.plannedUpstreamRecipe = editedRecipe;
        pending.appliedControls = RemoveDisplayFitControl(plan.visibleEdits.touchedControls);
        pending.planSummary = plan.summary;
        pending.appliedControlsSummary = JoinControlLabels(pending.appliedControls);
        pending.appliedValuesSummary =
            BuildStartingPointAppliedValuesSummary(plan.visibleEdits, pending.appliedControls);
        pending.withheldControlsSummary = JoinPlanSummaries(plan.withheldSummaries);
        pending.evidenceSummary = JoinPlanSummaries(plan.evidenceSummaries);
        pending.warningSummary = JoinPlanSummaries(plan.warnings);
        Stack::EditorModuleTypes::MarkRawStartingPointUpstreamApplied(
            m_RawWorkspaceAutoBaseUi,
            std::move(pending));
    } else {
        m_RawWorkspaceAutoBaseUi.pendingStartingPoint =
            Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction();
        m_RawWorkspaceAutoBaseUi.appliedAnalysisHash = BuildAnalysisHash(m_RawWorkspaceAnalysis);
        m_RawWorkspaceAutoBaseUi.hasAppliedViewFit = true;
        m_RawWorkspaceAutoBaseUi.startingPointDisplayFitPending = false;
        m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::AutoBase;
        m_RawWorkspaceAutoBaseUi.summary = plan.summary;
        SetStartingPointUiResult(
            m_RawWorkspaceAutoBaseUi,
            plan.summary,
            JoinControlLabels(plan.visibleEdits.touchedControls),
            JoinPlanSummaries(plan.withheldSummaries),
            JoinPlanSummaries(plan.evidenceSummaries),
            JoinPlanSummaries(plan.warnings),
            BuildStartingPointAppliedValuesSummary(
                plan.visibleEdits,
                plan.visibleEdits.touchedControls));
    }
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            editedRecipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations);
    AppendStartingPointActionDiagnostics(
        m_RawWorkspaceStartPointDiagnostics,
        plan,
        waitingForPostApplyDisplayFit ? "Applied upstream" : "Applied",
        waitingForPostApplyDisplayFit ? "Waiting for post-edit Display Fit" : "None",
        true);
    MarkRenderRefreshDirty();
    return true;
}

bool EditorModule::BeginRawWorkspacePreciseStartingPointForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    if (source.relativePathKey.empty() ||
        source.relativePathKey != m_ActiveRawWorkspaceSourceKey ||
        source.relativePathKey != m_RawWorkspace.selectedSourceKey ||
        !IsRawWorkspaceProjectActive()) {
        return false;
    }
    if (!m_RenderWorkerAvailable) {
        m_RawWorkspaceAutoBaseUi.summary =
            "Precise Starting Point unavailable: background renderer is not available. Fast mode remains available.";
        QueueUiNotification(
            UiNotificationSeverity::Info,
            m_RawWorkspaceAutoBaseUi.summary,
            "raw-workspace-precise-no-worker");
        return false;
    }
    if (m_ActiveRawWorkspaceMode != Stack::RawWorkspace::RawProjectMode::RecipeBacked) {
        m_RawWorkspaceAutoBaseUi.summary =
            "Precise Starting Point preserves managed or custom graphs; use the recipe-backed RAW view or Fast mode.";
        QueueUiNotification(
            UiNotificationSeverity::Info,
            m_RawWorkspaceAutoBaseUi.summary,
            "raw-workspace-precise-recipe-backed-only");
        return false;
    }
    Stack::PreciseIntegration::IntegrationState& precise =
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
    if (precise.active && Stack::PreciseIntegration::IsRunning(precise.state)) {
        return false;
    }
    CancelRawWorkspacePendingStartingPoint(
        "Precise mode replaced the queued Fast Starting Point action.");
    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(source);
    const std::string candidateSourceIdentity = source.fingerprint.empty()
        ? editedRecipe.source.fingerprint
        : source.fingerprint;
    Stack::PreciseIntegration::BeginSolve(
        precise,
        source.relativePathKey,
        sourceHash,
        LooksLikeSha256(candidateSourceIdentity)
            ? candidateSourceIdentity
            : std::string(),
        editedRecipe);
    m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.summary =
        "Precise Starting Point started. The current recipe remains unchanged while candidates render.";
    SetStartingPointUiResult(
        m_RawWorkspaceAutoBaseUi,
        m_RawWorkspaceAutoBaseUi.summary,
        "None",
        "Precise solve in progress",
        "Analyzing raw evidence; no candidate has been applied.");
    MarkRenderRefreshDirty();
    QueueUiNotification(
        UiNotificationSeverity::Info,
        "Precise Starting Point is evaluating rendered alternatives.",
        "raw-workspace-precise-started");
    return true;
}

void EditorModule::CancelRawWorkspacePreciseStartingPoint(std::string reason) {
    Stack::PreciseIntegration::IntegrationState& precise =
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
    if (!Stack::PreciseIntegration::Cancel(precise, std::move(reason))) return;
    if (m_RenderWorkerAvailable) {
        m_RenderWorker.InvalidateSnapshotsBefore(m_RenderGeneration + 1);
    }
    m_RawWorkspaceAutoBaseUi.summary =
        "Precise Starting Point canceled. The current recipe was not changed.";
    SetStartingPointUiResult(
        m_RawWorkspaceAutoBaseUi,
        m_RawWorkspaceAutoBaseUi.summary,
        "None",
        "Canceled before apply",
        "Isolated candidate work was discarded.",
        precise.terminalReason);
    m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary =
        "RAW Exposure: unchanged (canceled before apply); "
        "Local Range: unchanged (canceled before apply); "
        "Finish Tone: unchanged (canceled before apply); "
        "Display Fit / View Transform: unchanged (canceled before apply)";
    QueueUiNotification(
        UiNotificationSeverity::Info,
        m_RawWorkspaceAutoBaseUi.summary,
        "raw-workspace-precise-canceled");
}

bool EditorModule::ApplyRawWorkspacePreciseCandidateAtomically(
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const std::string& expectedBaseRecipeIdentity,
    std::string& reason) {
    reason.clear();
    if (!IsRawWorkspaceProjectActive() ||
        m_ActiveRawWorkspaceMode != Stack::RawWorkspace::RawProjectMode::RecipeBacked ||
        source.relativePathKey.empty() ||
        source.relativePathKey != m_ActiveRawWorkspaceSourceKey ||
        source.relativePathKey != m_RawWorkspace.selectedSourceKey) {
        reason = "Precise apply preflight no longer matches the active recipe-backed RAW project.";
        return false;
    }
    if (Stack::PreciseRaw::RecipeIdentity(m_ActiveRawWorkspaceRecipe) !=
        expectedBaseRecipeIdentity) {
        reason = "The RAW recipe changed before the atomic precise apply.";
        return false;
    }
    if (!Stack::PreciseIntegration::VisibleProjectionMatches(
            recipe,
            Stack::RawRecipe::DeserializeRecipe(
                Stack::RawRecipe::SerializeRecipe(recipe)))) {
        reason = "The precise recipe failed its persistence round-trip before apply.";
        return false;
    }

    std::vector<EditorNodeGraph::Node*> targetNodes;
    for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) continue;
        const std::string& key = node.rawDevelopment.recipe.source.relativePathKey;
        if (key.empty() || key == m_ActiveRawWorkspaceSourceKey) {
            targetNodes.push_back(&node);
        }
    }
    if (targetNodes.empty()) {
        reason = "The active recipe-backed project has no RAW Development node.";
        return false;
    }

    Stack::RawWorkspace::SourceRecord* mutableSource =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    if (!mutableSource || mutableSource->project.absolutePath.empty()) {
        reason = "The active RAW project is not ready for an atomic persisted recipe write.";
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe preparedActive;
    std::vector<Stack::RawRecipe::RawDevelopmentRecipe> preparedNodes;
    try {
        preparedActive = recipe;
        preparedNodes.assign(targetNodes.size(), recipe);
    } catch (const std::exception& error) {
        reason = std::string("Could not prepare the complete visible recipe: ") + error.what();
        return false;
    }
    if (!Stack::PreciseIntegration::VisibleProjectionMatches(recipe, preparedActive)) {
        reason = "The prepared precise recipe does not exactly match the verified candidate.";
        return false;
    }
    for (const Stack::RawRecipe::RawDevelopmentRecipe& preparedNode : preparedNodes) {
        if (!Stack::PreciseIntegration::VisibleProjectionMatches(recipe, preparedNode)) {
            reason = "A prepared RAW Development node does not exactly match the verified candidate.";
            return false;
        }
    }

    // Everything that can reject has completed. Move the fully prepared recipe
    // into the live model as one main-thread transaction; no candidate value was
    // exposed before this point.
    m_ActiveRawWorkspaceRecipe = std::move(preparedActive);
    for (std::size_t i = 0; i < targetNodes.size(); ++i) {
        EditorNodeGraph::Node& node = *targetNodes[i];
        node.rawDevelopment.recipe = std::move(preparedNodes[i]);
        node.rawDevelopment.projectStatus = "Edited";
        node.rawDevelopment.edited = true;
        node.rawDevelopment.autosaved = false;
    }
    mutableSource->project.status = Stack::RawWorkspace::ProjectStatus::Existing;
    mutableSource->project.mode = m_ActiveRawWorkspaceMode;
    mutableSource->project.autosaved = false;
    mutableSource->project.dirty = true;

    for (EditorNodeGraph::Node* node : targetNodes) {
        MarkRenderDirty(node->id);
    }
    MarkDirty();
    NoteRawWorkspaceRecipePreviewEdit(false);
    InvalidateRawWorkspaceGalleryPresentation();
    return true;
}

void EditorModule::HandleRawWorkspacePreciseSolveResult(
    const Stack::PreciseIntegration::NativeSolveResult& result) {
    Stack::PreciseIntegration::IntegrationState& precise =
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
    if (result.candidate.identity.requestId != precise.identity.requestId) {
        return;
    }
    if (!precise.active) {
        return;
    }
    if (result.canceled) {
        Stack::PreciseIntegration::MarkTerminal(
            precise,
            Stack::PreciseIntegration::LifecycleState::Canceled,
            result.reason.empty() ? "Precise solve canceled safely." : result.reason);
        m_RawWorkspaceAutoBaseUi.summary = precise.terminalReason;
        SetStartingPointUiResult(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAutoBaseUi.summary,
            "None",
            "Canceled before apply",
            "No candidate recipe was applied.");
        m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary =
            "RAW Exposure: unchanged (canceled before apply); "
            "Local Range: unchanged (canceled before apply); "
            "Finish Tone: unchanged (canceled before apply); "
            "Display Fit / View Transform: unchanged (canceled before apply)";
        return;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    Stack::PreciseIntegration::ApplyContext context;
    context.expected = precise.identity;
    context.activeSourceKey = m_ActiveRawWorkspaceSourceKey;
    context.selectedSourceKey = m_RawWorkspace.selectedSourceKey;
    context.activeSourceHash = source ? BuildRawWorkspaceAutoBaseSourceHash(*source) : 0;
    context.projectActive = IsRawWorkspaceProjectActive();
    context.recipeBacked =
        m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    context.currentRecipe = m_ActiveRawWorkspaceRecipe;
    const Stack::PreciseIntegration::ApplyDecision decision =
        Stack::PreciseIntegration::ValidateForAtomicApply(result.candidate, context);
    if (!decision.allowed || !source) {
        const std::string failureReason = !decision.reason.empty()
            ? decision.reason
            : (result.reason.empty()
                ? "Precise solve failed safely before apply."
                : result.reason);
        Stack::PreciseIntegration::MarkTerminal(
            precise,
            result.failed
                ? Stack::PreciseIntegration::LifecycleState::Failed
                : Stack::PreciseIntegration::LifecycleState::Blocked,
            failureReason);
        m_RawWorkspaceAutoBaseUi.summary =
            "Precise Starting Point did not apply: " + failureReason;
        SetStartingPointUiResult(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAutoBaseUi.summary,
            "None",
            "All controls left unchanged",
            "Full-resolution or identity gate did not authorize apply.",
            failureReason);
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_RawWorkspaceAutoBaseUi.summary,
            "raw-workspace-precise-failed-safe");
        return;
    }

    const Stack::PreciseIntegration::ProjectionSummary projection =
        Stack::PreciseIntegration::SummarizeProjection(
            precise.originalRecipe,
            decision.recipe);
    const std::string evidence =
        std::string("raw-precise-dry-run-v1; ") +
        std::to_string(result.candidate.proxyRenderCount) +
        " proxy renders, " + std::to_string(result.candidate.proxyCacheHits) +
        " cache hits; full resolution " +
        std::to_string(result.candidate.fullWidth) + "x" +
        std::to_string(result.candidate.fullHeight) + " accepted.";
    const std::string warnings = JoinStringsComma(result.candidate.warnings);
    if (!decision.changesRecipe) {
        Stack::PreciseIntegration::MarkTerminal(
            precise,
            Stack::PreciseIntegration::LifecycleState::AppliedNoChange,
            "Precise Starting Point verified that the current visible recipe should remain unchanged.");
        m_RawWorkspaceAutoBaseUi.summary = precise.terminalReason;
        SetStartingPointUiResult(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAutoBaseUi.summary,
            "None",
            "RAW Exposure, Local Range, Finish Tone, and Display Fit verified unchanged",
            evidence,
            warnings);
        m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary =
            projection.controlStatus;
        QueueUiNotification(
            UiNotificationSeverity::Success,
            m_RawWorkspaceAutoBaseUi.summary,
            "raw-workspace-precise-no-change");
        return;
    }

    precise.state = Stack::PreciseIntegration::LifecycleState::Applying;
    precise.statusText = "Applying visible recipe...";
    std::string applyReason;
    if (!ApplyRawWorkspacePreciseCandidateAtomically(
            *source,
            decision.recipe,
            precise.identity.inputRecipeIdentity,
            applyReason)) {
        if (applyReason.empty()) {
            applyReason = "The atomic visible-recipe apply was rejected before commit.";
        }
        Stack::PreciseIntegration::MarkTerminal(
            precise,
            Stack::PreciseIntegration::LifecycleState::Failed,
            applyReason);
        m_RawWorkspaceAutoBaseUi.summary =
            "Precise Starting Point failed safely: " + applyReason;
        SetStartingPointUiResult(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAutoBaseUi.summary,
            "None",
            "All controls left unchanged",
            evidence,
            applyReason);
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_RawWorkspaceAutoBaseUi.summary,
            "raw-workspace-precise-atomic-apply-failed");
        return;
    }

    m_RawWorkspaceAutoBaseUi.beforeAutoBase = precise.originalRecipe;
    m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;
    m_RawWorkspaceAutoBaseUi.sourceKey = source->relativePathKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = BuildRawWorkspaceAutoBaseSourceHash(*source);
    m_RawWorkspaceAutoBaseUi.hasAppliedViewFit = true;
    m_RawWorkspaceAutoBaseUi.startingPointDisplayFitPending = true;
    m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::AutoBase;
    m_RawWorkspaceAutoBaseUi.appliedAnalysisHash = 0;
    m_RawWorkspaceAutoBaseUi.preciseAppliedDisplayFitAwaitingRender = true;
    m_RawWorkspaceAutoBaseUi.preciseAppliedRecipeIdentity =
        Stack::PreciseRaw::RecipeIdentity(m_ActiveRawWorkspaceRecipe);
    Stack::PreciseIntegration::MarkTerminal(
        precise,
        Stack::PreciseIntegration::LifecycleState::Applied,
        "Precise Starting Point applied one full-resolution-verified visible recipe.");
    m_RawWorkspaceAutoBaseUi.summary = precise.terminalReason;
    SetStartingPointUiResult(
        m_RawWorkspaceAutoBaseUi,
        m_RawWorkspaceAutoBaseUi.summary,
        JoinStringsComma(projection.changedGroups),
        projection.unchangedGroups.empty()
            ? "None"
            : "Verified unchanged: " + JoinStringsComma(projection.unchangedGroups),
        evidence,
        warnings,
        projection.changedValues);
    m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary =
        projection.controlStatus;
    m_RawWorkspaceStartPointDiagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Precise Starting Point",
        "Applied atomically",
        evidence
    });
    m_RawWorkspaceStartPointDiagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Precise visible controls",
        JoinStringsComma(projection.changedGroups),
        projection.controlStatus
    });
    m_RawWorkspaceStartPointDiagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Precise Undo",
        "Available",
        "One Undo restores the complete recipe from before this precise action."
    });
    RefreshRawWorkspaceAutoBaseRecommendations(m_ActiveRawWorkspaceRecipe);
    QueueUiNotification(
        UiNotificationSeverity::Success,
        m_RawWorkspaceAutoBaseUi.summary,
        "raw-workspace-precise-applied");
}

void EditorModule::AdoptRawWorkspacePreciseAppliedRender() {
    RawWorkspaceAutoBaseUiState& ui = m_RawWorkspaceAutoBaseUi;
    if (!ui.preciseAppliedDisplayFitAwaitingRender) return;
    if (ui.sourceKey != m_ActiveRawWorkspaceSourceKey ||
        ui.preciseAppliedRecipeIdentity.empty() ||
        ui.preciseAppliedRecipeIdentity !=
            Stack::PreciseRaw::RecipeIdentity(m_ActiveRawWorkspaceRecipe)) {
        ui.preciseAppliedDisplayFitAwaitingRender = false;
        ui.preciseAppliedRecipeIdentity.clear();
        ui.startingPointDisplayFitPending = false;
        return;
    }
    ui.appliedAnalysisHash = BuildAnalysisHash(m_RawWorkspaceAnalysis);
    ui.preciseAppliedDisplayFitAwaitingRender = false;
    ui.preciseAppliedRecipeIdentity.clear();
    ui.startingPointDisplayFitPending = false;
}

bool EditorModule::ApplyRawWorkspaceStartingPointBalancedLocalForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    if (source.relativePathKey.empty() ||
        source.relativePathKey != m_ActiveRawWorkspaceSourceKey ||
        source.relativePathKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(source);
    if (m_RawWorkspaceAutoBaseUi.sourceKey != source.relativePathKey ||
        m_RawWorkspaceAutoBaseUi.sourceHash != sourceHash) {
        ResetRawWorkspaceAutoBaseState();
        m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }

    if (m_RawWorkspaceAnalysis.sourceKey != source.relativePathKey ||
        !m_RawWorkspaceAnalysis.currentFrameStats.valid) {
        m_RawWorkspaceAutoBaseUi.summary =
            "Balanced Local pending: render a RAW preview before applying Local Range.";
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Render a RAW preview before adding Balanced Local Range.",
            "raw-workspace-starting-point-balanced-local-no-stats");
        return false;
    }

    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> selectedSuggestions =
        Stack::RawAutoBase::SelectBalancedLocalRangeAdjustments(
            m_RawWorkspaceAutoBaseUi.recommendations);
    if (selectedSuggestions.empty()) {
        m_RawWorkspaceAutoBaseUi.summary =
            "Balanced Local unavailable: no Local Range suggestion passed the confidence/delta caps.";
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "No Balanced Local Range point passed the confidence and delta caps.",
            "raw-workspace-starting-point-balanced-local-no-candidate");
        m_RawWorkspaceStartPointDiagnostics =
            Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
                m_RawWorkspaceStartPointDiagnostics,
                editedRecipe,
                m_RawWorkspaceAnalysis,
                m_RawWorkspaceAutoBaseUi.recommendations);
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> appliedSuggestions;
    appliedSuggestions.reserve(selectedSuggestions.size());
    for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion : selectedSuggestions) {
        Stack::RawRecipe::RawDevelopmentRecipe candidateRecipe = recipe;
        if (!Stack::RawAutoBase::ApplySuggestedLocalAdjustment(suggestion, candidateRecipe)) {
            continue;
        }
        recipe = std::move(candidateRecipe);
        appliedSuggestions.push_back(suggestion);
        if (appliedSuggestions.size() >= 2) {
            break;
        }
    }

    if (appliedSuggestions.empty()) {
        m_RawWorkspaceAutoBaseUi.summary =
            "Balanced Local unavailable: Local Range graph is full or overlaps an existing point.";
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Balanced Local Range overlaps an existing point or the graph is full.",
            "raw-workspace-starting-point-balanced-local-overlap");
        return false;
    }

    m_RawWorkspaceAutoBaseUi.beforeAutoBase = editedRecipe;
    m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.appliedAnalysisHash = BuildAnalysisHash(m_RawWorkspaceAnalysis);
    m_RawWorkspaceAutoBaseUi.appliedSuggestionKey = "balanced-local";
    m_RawWorkspaceAutoBaseUi.appliedSuggestionLabel =
        appliedSuggestions.size() == 1
            ? "Balanced Local 1 point"
            : "Balanced Local 2 points";
    m_RawWorkspaceAutoBaseUi.appliedSuggestionSection = "Local Range";
    m_RawWorkspaceAutoBaseUi.appliedSuggestionSourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.appliedSuggestionAnalysisHash =
        BuildAnalysisHash(m_RawWorkspaceAnalysis);
    m_RawWorkspaceAutoBaseUi.summary =
        BuildBalancedLocalAppliedSummary(appliedSuggestions);
    m_RawWorkspaceLocalRangeOverlayMode =
        appliedSuggestions.front().colorQualifierEnabled ? "region-mask" : "affected-tones";
    ClearRawWorkspaceLocalRangeOverlayState();
    MarkRenderRefreshDirty();
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            editedRecipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations);
    return true;
}

bool EditorModule::ApplyRawWorkspaceStartingPointMildToneForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    if (source.relativePathKey.empty() ||
        source.relativePathKey != m_ActiveRawWorkspaceSourceKey ||
        source.relativePathKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(source);
    if (m_RawWorkspaceAutoBaseUi.sourceKey != source.relativePathKey ||
        m_RawWorkspaceAutoBaseUi.sourceHash != sourceHash) {
        ResetRawWorkspaceAutoBaseState();
        m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }

    if (m_RawWorkspaceAnalysis.sourceKey != source.relativePathKey ||
        !m_RawWorkspaceAnalysis.currentFrameStats.valid) {
        m_RawWorkspaceAutoBaseUi.summary =
            "Mild Finish Tone pending: render a RAW preview before applying Finish Tone.";
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Render a RAW preview before adding Mild Finish Tone.",
            "raw-workspace-starting-point-mild-tone-no-stats");
        return false;
    }

    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            editedRecipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations);

    const Stack::RawAutoStartPoint::RawAutoStartPointFinishToneProposal proposal =
        Stack::RawAutoStartPoint::BuildMildFinishToneProposal(
            editedRecipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceStartPointDiagnostics);
    if (!proposal.valid) {
        m_RawWorkspaceAutoBaseUi.summary = proposal.summary.empty()
            ? "Mild Finish Tone unavailable: no conservative visible tone proposal is ready."
            : proposal.summary;
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Mild Finish Tone was withheld for this RAW file.",
            "raw-workspace-starting-point-mild-tone-no-candidate");
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    recipe.finishTone.layerJson =
        BuildVisibleFinishToneLayerJson(editedRecipe.finishTone.layerJson, proposal);

    m_RawWorkspaceAutoBaseUi.beforeAutoBase = editedRecipe;
    m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.appliedAnalysisHash = BuildAnalysisHash(m_RawWorkspaceAnalysis);
    m_RawWorkspaceAutoBaseUi.appliedSuggestionKey = "mild-finish-tone";
    m_RawWorkspaceAutoBaseUi.appliedSuggestionLabel = "Mild Finish Tone";
    m_RawWorkspaceAutoBaseUi.appliedSuggestionSection = "Finish Tone";
    m_RawWorkspaceAutoBaseUi.appliedSuggestionSourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.appliedSuggestionAnalysisHash =
        BuildAnalysisHash(m_RawWorkspaceAnalysis);
    m_RawWorkspaceAutoBaseUi.summary = BuildMildFinishToneAppliedSummary(proposal);
    MarkRenderRefreshDirty();
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            editedRecipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseExposureSuggestion(
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const Stack::RawAutoBase::RawExposureRecommendation recommendation =
        m_RawWorkspaceAutoBaseUi.recommendations.exposure;
    if (!recommendation.valid ||
        recommendation.action != Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue ||
        recommendation.blockedByHighlightRisk) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    recipe.preToneExposureEv = std::clamp(recommendation.suggestedEv, -8.0f, 8.0f);
    CaptureRawWorkspaceAutoBaseRevertSnapshotForSelectedSource(editedRecipe);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.summary =
        "Applied RAW Exposure suggestion: " + FormatSignedEv(recommendation.deltaEv) + ".";
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseWhiteBalanceSuggestion(
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const Stack::RawAutoBase::WhiteBalanceRecommendation recommendation =
        m_RawWorkspaceAutoBaseUi.recommendations.whiteBalance;
    if (!recommendation.valid ||
        recommendation.action != Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue ||
        !recommendation.alternateCandidateAvailable ||
        recommendation.manualWhiteBalanceProtected) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    Stack::RawAutoBase::ApplyWhiteBalanceRecommendationToRecipe(recipe, recommendation);
    CaptureRawWorkspaceAutoBaseRevertSnapshotForSelectedSource(editedRecipe);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.summary =
        std::string("Applied WB suggestion: ") + WhiteBalanceMethodLabel(recommendation.method) + ".";
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseHighlightProtection(
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const Stack::RawAutoBase::HighlightRecommendation recommendation =
        m_RawWorkspaceAutoBaseUi.recommendations.highlight;
    if (!recommendation.valid ||
        recommendation.protectionAction != Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue ||
        !recommendation.recommendProtectiveViewShoulder) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    const Stack::RawAutoBase::ViewTransformFit fit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(m_RawWorkspaceAnalysis);
    Stack::RawAutoBase::ApplyHighlightProtectionToRecipe(recipe, recommendation, fit);
    CaptureRawWorkspaceAutoBaseRevertSnapshotForSelectedSource(editedRecipe);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::AutoBase;
    m_RawWorkspaceAutoBaseUi.summary =
        "Applied highlight protection to View Transform shoulder/white EV. RAW Exposure unchanged.";
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseLocalSuggestion(
    std::size_t suggestionIndex,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions =
        m_RawWorkspaceAutoBaseUi.recommendations.localAdjustments;
    if (suggestionIndex >= suggestions.size()) {
        return false;
    }

    const Stack::RawAutoBase::SuggestedLocalAdjustment suggestion =
        suggestions[suggestionIndex];
    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    if (!Stack::RawAutoBase::ApplySuggestedLocalAdjustment(suggestion, recipe)) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Local Range suggestion overlaps an existing point or the graph is full.",
            "raw-workspace-auto-base-local-overlap");
        return false;
    }

    CaptureRawWorkspaceAutoBaseRevertSnapshotForSelectedSource(editedRecipe);

    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.sourceKey = m_ActiveRawWorkspaceSourceKey;
    if (const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey)) {
        m_RawWorkspaceAutoBaseUi.sourceHash = BuildRawWorkspaceAutoBaseSourceHash(*source);
    }
    m_RawWorkspaceAutoBaseUi.summary =
        "Applied Local Range suggestion: " +
        (suggestion.label.empty()
            ? std::string(Stack::RawAutoBase::SuggestedLocalAdjustmentKindLabel(suggestion.kind))
            : suggestion.label) +
        ".";
    m_RawWorkspaceLocalRangeOverlayMode =
        suggestion.colorQualifierEnabled ? "region-mask" : "affected-tones";
    ClearRawWorkspaceLocalRangeOverlayState();
    MarkRenderRefreshDirty();
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::RevertRawWorkspaceAutoBaseForSelectedSource() {
    if (!m_RawWorkspaceAutoBaseUi.hasRevertSnapshot ||
        m_RawWorkspaceAutoBaseUi.sourceKey.empty() ||
        m_RawWorkspaceAutoBaseUi.sourceKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe revertRecipe =
        m_RawWorkspaceAutoBaseUi.beforeAutoBase;
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(revertRecipe, false)) {
        return false;
    }

    const std::string sourceKey = m_ActiveRawWorkspaceSourceKey;
    const std::uint64_t sourceHash = m_RawWorkspaceAutoBaseUi.sourceHash;
    ResetRawWorkspaceAutoBaseState();
    m_RawWorkspaceAutoBaseUi.sourceKey = sourceKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    ClearStartingPointUiResult(m_RawWorkspaceAutoBaseUi);
    m_RawWorkspaceAutoBaseUi.summary =
        "Automatic RAW action reverted: restored the recipe snapshot from before the automatic action.";
    return true;
}

void EditorModule::TryContinueRawWorkspaceStartingPointOnAnalysis() {
    Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction& pending =
        m_RawWorkspaceAutoBaseUi.pendingStartingPoint;
    if (!pending.active) {
        return;
    }

    CancelRawWorkspacePendingStartingPoint(
        "Automatic Starting Point is archived in the manual-first RAW editor.");
    return;

#if 0
    // Archived continuation implementation. Keep with the retired automatic
    // solver backend until a later cleanup pass decides its compatibility
    // boundary.
    const Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(pending.sourceKey);
    Stack::RawRecipe::RawDevelopmentRecipe recipe = m_ActiveRawWorkspaceRecipe;
    Stack::EditorModuleTypes::RawStartingPointContinuationContext continuation;
    continuation.sourceExists = source != nullptr;
    continuation.activeSourceKey = m_ActiveRawWorkspaceSourceKey;
    continuation.selectedSourceKey = m_RawWorkspace.selectedSourceKey;
    continuation.sourceHash = source ? BuildRawWorkspaceAutoBaseSourceHash(*source) : 0;
    continuation.analysisSourceKey = m_RawWorkspaceAnalysis.sourceKey;
    continuation.analysisValid = m_RawWorkspaceAnalysis.currentFrameStats.valid;
    continuation.recipeFingerprint = BuildRecipeFingerprint(recipe);

    const Stack::EditorModuleTypes::RawStartingPointContinuationDecision continuationDecision =
        Stack::EditorModuleTypes::EvaluateRawStartingPointContinuation(
            pending,
            continuation);
    if (continuationDecision ==
        Stack::EditorModuleTypes::RawStartingPointContinuationDecision::WaitForAnalysis) {
        return;
    }
    if (Stack::EditorModuleTypes::RawStartingPointContinuationCancels(
            continuationDecision)) {
        CancelRawWorkspacePendingStartingPoint(
            Stack::EditorModuleTypes::RawStartingPointContinuationCancellationReason(
                continuationDecision,
                pending,
                continuation));
        return;
    }

    if (!m_RawWorkspaceAutoBaseUi.hasMetadataSummary ||
        m_RawWorkspaceAutoBaseUi.sourceKey != pending.sourceKey) {
        m_RawWorkspaceAutoBaseUi.metadataSummary = ResolveRawWorkspaceMetadataSummaryForAutoBase();
        m_RawWorkspaceAutoBaseUi.hasMetadataSummary = true;
    }
    m_RawWorkspaceAnalysis.metadata = m_RawWorkspaceAutoBaseUi.metadataSummary;
    RefreshRawWorkspaceAutoBaseRecommendations(recipe);
    if (continuationDecision ==
        Stack::EditorModuleTypes::RawStartingPointContinuationDecision::ContinueInitialAnalysis) {
        ApplyRawWorkspaceBuildStartingPointForSource(*source, recipe);
        return;
    }

    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            recipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations);

    const Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan continuationPlan =
        Stack::RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations,
            m_RawWorkspaceStartPointDiagnostics);
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>
        continuationControls =
            RemoveDisplayFitControl(continuationPlan.visibleEdits.touchedControls);
    if (!continuationControls.empty() &&
        Stack::EditorModuleTypes::RawStartingPointShouldContinueUpstreamApply(
            pending,
            continuationPlan.hasUpstreamRecipeChanges)) {
        const Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction
            pendingBeforeContinuation = pending;
        Stack::RawRecipe::RawDevelopmentRecipe continuationRecipe =
            continuationPlan.upstreamRecipe;
        if (!ApplyRawWorkspaceRecipeEditForSelectedSource(continuationRecipe, false)) {
            return;
        }

        recipe = m_ActiveRawWorkspaceRecipe;
        Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction continuedPending =
            pendingBeforeContinuation;
        continuedPending.upstreamApplyPassCount =
            pendingBeforeContinuation.upstreamApplyPassCount + 1;
        continuedPending.recipeFingerprint = BuildRecipeFingerprint(recipe);
        continuedPending.plannedUpstreamRecipe = recipe;
        continuedPending.appliedControls = MergeStartingPointControls(
            std::move(continuedPending.appliedControls),
            continuationControls);
        continuedPending.planSummary = continuationPlan.summary;
        continuedPending.appliedControlsSummary =
            JoinControlLabels(continuedPending.appliedControls);
        const std::string continuationValues =
            BuildStartingPointAppliedValuesSummary(
                continuationPlan.visibleEdits,
                continuationControls);
        continuedPending.appliedValuesSummary = MergeStartingPointAppliedValues(
            pendingBeforeContinuation.appliedValuesSummary,
            continuationValues,
            continuedPending.appliedControls,
            continuationControls);
        continuedPending.withheldControlsSummary =
            JoinPlanSummaries(continuationPlan.withheldSummaries);
        continuedPending.evidenceSummary = MergeStartingPointSummaryText(
            pendingBeforeContinuation.evidenceSummary,
            JoinPlanSummaries(continuationPlan.evidenceSummaries));
        continuedPending.warningSummary = MergeStartingPointSummaryText(
            pendingBeforeContinuation.warningSummary,
            JoinPlanSummaries(continuationPlan.warnings));
        Stack::EditorModuleTypes::MarkRawStartingPointUpstreamApplied(
            m_RawWorkspaceAutoBaseUi,
            std::move(continuedPending));

        RefreshRawWorkspaceAutoBaseRecommendations(recipe);
        m_RawWorkspaceStartPointDiagnostics =
            Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
                m_RawWorkspaceStartPointDiagnostics,
                recipe,
                m_RawWorkspaceAnalysis,
                m_RawWorkspaceAutoBaseUi.recommendations);
        AppendStartingPointActionDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            continuationPlan,
            "Applied bounded upstream continuation",
            "Waiting for next rendered pass",
            m_RawWorkspaceAutoBaseUi.hasRevertSnapshot);
        MarkRenderRefreshDirty();
        return;
    }

    if (Stack::EditorModuleTypes::RawStartingPointReachedUpstreamApplyLimit(
            pending,
            continuationPlan.hasUpstreamRecipeChanges)) {
        const std::string limitSummary =
            "Additional upstream controls withheld: bounded four-pass one-click limit reached";
        pending.withheldControlsSummary = MergeStartingPointSummaryText(
            pending.withheldControlsSummary,
            limitSummary);
        pending.warningSummary = MergeStartingPointSummaryText(
            pending.warningSummary,
            "Build Starting Point stopped iterating before final Display Fit to keep the solve bounded.");
    }

    const Stack::RawAutoBase::ViewFitDecision decision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(m_RawWorkspaceAnalysis, recipe);
    if (!decision.canApply) {
        const std::string reason = decision.reason.empty()
            ? "post-edit analysis could not produce a Display Fit."
            : decision.reason;
        const Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction pendingBeforeFailure =
            pending;
        const std::string withheldSummary =
            Stack::EditorModuleTypes::MarkRawStartingPointPostFitFailure(
                m_RawWorkspaceAutoBaseUi,
                reason);
        Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan pendingPlan;
        pendingPlan.valid = true;
        pendingPlan.summary = m_RawWorkspaceAutoBaseUi.summary;
        pendingPlan.visibleEdits.touchedControls = pendingBeforeFailure.appliedControls;
        if (!Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(
                pendingBeforeFailure.evidenceSummary)) {
            pendingPlan.evidenceSummaries.push_back(pendingBeforeFailure.evidenceSummary);
        }
        if (!Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(withheldSummary)) {
            pendingPlan.withheldSummaries.push_back(withheldSummary);
        }
        if (!Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(
                pendingBeforeFailure.warningSummary)) {
            pendingPlan.warnings.push_back(pendingBeforeFailure.warningSummary);
        }
        AppendStartingPointActionDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            pendingPlan,
            "Applied upstream",
            "Refit Display pending",
            m_RawWorkspaceAutoBaseUi.hasRevertSnapshot);
        return;
    }

    Stack::RawAutoBase::ApplyViewTransformFitToRecipe(recipe, decision.fit);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return;
    }

    const Stack::EditorModuleTypes::RawWorkspaceStartingPointPendingAction pendingBeforeCompletion =
        pending;
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl> finalTouchedControls =
        Stack::EditorModuleTypes::MarkRawStartingPointPostFitApplied(
            m_RawWorkspaceAutoBaseUi,
            pendingBeforeCompletion,
            BuildAnalysisHash(m_RawWorkspaceAnalysis),
            FormatStartingPointDisplayFitValueSummary(decision.fit));
    recipe = m_ActiveRawWorkspaceRecipe;
    RefreshRawWorkspaceAutoBaseRecommendations(recipe);
    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            m_RawWorkspaceStartPointDiagnostics,
            recipe,
            m_RawWorkspaceAnalysis,
            m_RawWorkspaceAutoBaseUi.recommendations);

    Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan finalPlan;
    finalPlan.valid = true;
    finalPlan.summary = m_RawWorkspaceAutoBaseUi.summary;
    finalPlan.visibleEdits.touchedControls = finalTouchedControls;
    if (!pendingBeforeCompletion.evidenceSummary.empty() &&
        pendingBeforeCompletion.evidenceSummary != "None") {
        finalPlan.evidenceSummaries.push_back(pendingBeforeCompletion.evidenceSummary);
    }
    if (!pendingBeforeCompletion.withheldControlsSummary.empty() &&
        pendingBeforeCompletion.withheldControlsSummary != "None") {
        finalPlan.withheldSummaries.push_back(pendingBeforeCompletion.withheldControlsSummary);
    }
    if (!Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(
            pendingBeforeCompletion.warningSummary)) {
        finalPlan.warnings.push_back(pendingBeforeCompletion.warningSummary);
    }
    AppendStartingPointActionDiagnostics(
        m_RawWorkspaceStartPointDiagnostics,
        finalPlan,
        "Applied",
        "None",
        m_RawWorkspaceAutoBaseUi.hasRevertSnapshot);
#endif
}

bool EditorModule::RenderRawWorkspaceAutoBasePanel(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
    float controlWidth) {
    ImGui::SeparatorText("Starting Point");

    if (selectedSource == nullptr) {
        ImGui::TextDisabled("Starting Point unavailable: no RAW selected.");
        return false;
    }

    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(*selectedSource);
    if (!m_RawWorkspaceAutoBaseUi.sourceKey.empty() &&
        (m_RawWorkspaceAutoBaseUi.sourceKey != selectedSource->relativePathKey ||
         m_RawWorkspaceAutoBaseUi.sourceHash != sourceHash)) {
        ResetRawWorkspaceAutoBaseState();
    }
    if (m_RawWorkspaceAutoBaseUi.sourceKey.empty()) {
        m_RawWorkspaceAutoBaseUi.sourceKey = selectedSource->relativePathKey;
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }

    bool recipeUpdated = false;
    const bool hasUsableStats =
        selectedSource->relativePathKey == m_ActiveRawWorkspaceSourceKey &&
        m_RawWorkspaceAnalysis.sourceKey == selectedSource->relativePathKey &&
        m_RawWorkspaceAnalysis.currentFrameStats.valid;
    if (hasUsableStats) {
        RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    }
    const bool canRevert =
        m_RawWorkspaceAutoBaseUi.hasRevertSnapshot &&
        m_RawWorkspaceAutoBaseUi.sourceKey == selectedSource->relativePathKey;
    Stack::PreciseIntegration::IntegrationState& preciseState =
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
    const bool preciseRunning =
        preciseState.active && Stack::PreciseIntegration::IsRunning(preciseState.state);
    const bool preciseAvailable =
        m_RenderWorkerAvailable &&
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    const bool canBuildStartingPoint =
        !preciseRunning &&
        (preciseState.mode == Stack::PreciseIntegration::ProductMode::Fast ||
         preciseAvailable);
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> balancedLocalSuggestions =
        hasUsableStats
            ? Stack::RawAutoBase::SelectBalancedLocalRangeAdjustments(
                m_RawWorkspaceAutoBaseUi.recommendations)
            : std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>();
    const bool canBuildBalancedLocal =
        hasUsableStats && !balancedLocalSuggestions.empty();
    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics buttonDiagnostics =
        m_RawWorkspaceStartPointDiagnostics;
    if (hasUsableStats) {
        buttonDiagnostics =
            Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
                buttonDiagnostics,
                editedRecipe,
                m_RawWorkspaceAnalysis,
                m_RawWorkspaceAutoBaseUi.recommendations);
    }
    Stack::RawAutoStartPoint::RawAutoStartPointConservativePlan currentOneClickPlan;
    if (hasUsableStats) {
        currentOneClickPlan =
            Stack::RawAutoStartPoint::BuildConservativeStartingPointPlan(
                editedRecipe,
                m_RawWorkspaceAnalysis,
                m_RawWorkspaceAutoBaseUi.recommendations,
                buttonDiagnostics);
    }
    const Stack::RawAutoStartPoint::RawAutoStartPointFinishToneProposal mildToneProposal =
        hasUsableStats
            ? Stack::RawAutoStartPoint::BuildMildFinishToneProposal(
                editedRecipe,
                m_RawWorkspaceAnalysis,
                buttonDiagnostics)
            : Stack::RawAutoStartPoint::RawAutoStartPointFinishToneProposal();
    const bool canAddMildTone = hasUsableStats && mildToneProposal.valid;

    const float buttonGap = 6.0f;
    const float rowButtonWidth = (controlWidth - buttonGap * 2.0f) / 3.0f;
    const float minFitDisplayButtonWidth =
        ImGui::CalcTextSize("Refit Display").x + ImGui::GetStyle().FramePadding.x * 2.0f + 2.0f;
    const bool stackButtons = rowButtonWidth < minFitDisplayButtonWidth;
    const float buttonWidth = stackButtons
        ? controlWidth
        : std::max(minFitDisplayButtonWidth, rowButtonWidth);
    const float optionalActionRowButtonWidth = (controlWidth - buttonGap) / 2.0f;
    const float minOptionalActionButtonWidth =
        std::max(
            ImGui::CalcTextSize("Add Local Range").x,
            ImGui::CalcTextSize("Add Mild Tone").x) +
        ImGui::GetStyle().FramePadding.x * 2.0f + 2.0f;
    const bool stackOptionalActionButtons =
        optionalActionRowButtonWidth < minOptionalActionButtonWidth;
    const float optionalActionButtonWidth = stackOptionalActionButtons
        ? controlWidth
        : std::max(minOptionalActionButtonWidth, optionalActionRowButtonWidth);

    ImGui::BeginDisabled(preciseRunning);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::BeginCombo(
            "##RawStartingPointMode",
            Stack::PreciseIntegration::ProductModeName(preciseState.mode))) {
        for (const Stack::PreciseIntegration::ProductMode mode : {
                 Stack::PreciseIntegration::ProductMode::Precise,
                 Stack::PreciseIntegration::ProductMode::Fast }) {
            const bool selected = preciseState.mode == mode;
            if (ImGui::Selectable(
                    Stack::PreciseIntegration::ProductModeName(mode),
                    selected)) {
                preciseState.mode = mode;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    TooltipIfHovered(
        "Precise evaluates real rendered alternatives and verifies one full-resolution recipe before a single apply. Fast keeps the existing bounded Pass 94 behavior.",
        ImGuiHoveredFlags_AllowWhenDisabled);

    const std::string readinessSummary =
        preciseState.mode == Stack::PreciseIntegration::ProductMode::Precise
            ? (preciseAvailable
                ? "Precise mode ready: slower rendered search, cancelable before one final apply."
                : "Precise mode unavailable here; choose Fast for this project state.")
            : BuildBaseAssistReadinessSummary(
                hasUsableStats,
                m_RawWorkspaceAutoBaseUi.hasAppliedViewFit,
                canRevert);
    RenderDisabledSummaryLine(readinessSummary, controlWidth);
    TooltipIfHovered(
        "Build Starting Point is the primary one-click base action. If preview analysis is pending, it queues analysis before writing visible controls.");

    ImGui::Spacing();
    ImGui::BeginDisabled(!canBuildStartingPoint);
    if (ImGuiExtras::RichFullWidthButton("Build Starting Point", controlWidth, 0.0f)) {
        if (preciseState.mode == Stack::PreciseIntegration::ProductMode::Precise) {
            BeginRawWorkspacePreciseStartingPointForSource(*selectedSource, editedRecipe);
        } else {
            recipeUpdated =
                ApplyRawWorkspaceBuildStartingPointForSource(*selectedSource, editedRecipe);
        }
    }
    ImGui::EndDisabled();
    TooltipIfHovered(
        "In Precise mode, searches isolated rendered candidates and applies only the full-resolution-verified visible recipe. In Fast mode, runs the existing bounded heuristic path.",
        ImGuiHoveredFlags_AllowWhenDisabled);
    if (preciseRunning) {
        const EditorRenderWorker::RenderProgress progress = m_RenderWorker.GetProgress();
        RenderDisabledSummaryLine(
            progress.label.empty() ? preciseState.statusText : progress.label,
            controlWidth);
        if (ImGuiExtras::RichFullWidthButton(
                "Cancel Precise Solve##RawStartingPointCancelPrecise",
                controlWidth,
                0.0f)) {
            CancelRawWorkspacePreciseStartingPoint("Canceled by the user before apply.");
        }
        TooltipIfHovered(
            "Discards isolated candidate work. The visible recipe and Undo snapshot remain unchanged.");
    }

    const std::string currentPlanSummary =
        BuildCurrentStartingPointPlanInlineSummary(currentOneClickPlan, hasUsableStats);
    const std::string currentControlPlanSummary =
        BuildCurrentStartingPointControlPlanSummary(currentOneClickPlan, hasUsableStats);
    const std::string currentPlanValuesSummary =
        BuildCurrentStartingPointPlanValuesSummary(currentOneClickPlan, hasUsableStats);
    const std::string selectedCandidateSummary =
        BuildSelectedStartPointInlineSummary(buttonDiagnostics, hasUsableStats);
    ImGui::Spacing();

    const bool hasStartingPointResult =
        m_RawWorkspaceAutoBaseUi.sourceKey == selectedSource->relativePathKey &&
        (!m_RawWorkspaceAutoBaseUi.startingPointResultSummary.empty() ||
         !m_RawWorkspaceAutoBaseUi.startingPointAppliedControlsSummary.empty() ||
         !m_RawWorkspaceAutoBaseUi.startingPointAppliedValuesSummary.empty() ||
         !m_RawWorkspaceAutoBaseUi.startingPointWithheldControlsSummary.empty() ||
         !m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary.empty() ||
         !m_RawWorkspaceAutoBaseUi.startingPointEvidenceSummary.empty() ||
         !m_RawWorkspaceAutoBaseUi.startingPointWarningSummary.empty());
    std::string buildStatusSummary = hasStartingPointResult
        ? BuildStartingPointDisplaySummary(
            m_RawWorkspaceAutoBaseUi.startingPointResultSummary)
        : (hasUsableStats
            ? std::string("Ready. Click Build Starting Point to apply a safe editable base.")
            : std::string("Click Build Starting Point; it will analyze this RAW first."));
    if (buildStatusSummary.empty()) {
        buildStatusSummary = "Build Starting Point completed. Open Details for the full result.";
    }
    if (preciseRunning) {
        const EditorRenderWorker::RenderProgress progress = m_RenderWorker.GetProgress();
        buildStatusSummary = progress.label.empty()
            ? preciseState.statusText
            : progress.label;
    }

    // Keep this result block at three single-line rows. It sits above every
    // manual control, so wrapped or conditional rows would move an active
    // slider or graph when a render refreshes the analysis.
    ImGui::BeginGroup();
    ImGui::TextDisabled(hasStartingPointResult ? "Build result" : "Build status");
    RenderDisabledSummaryLine(buildStatusSummary, controlWidth);
    if (hasStartingPointResult &&
        !m_RawWorkspaceAutoBaseUi.startingPointAppliedControlsSummary.empty() &&
        !IsEmptyOrNoneSummary(m_RawWorkspaceAutoBaseUi.startingPointAppliedControlsSummary)) {
        RenderDisabledSummaryLine(
            "Changed: " + m_RawWorkspaceAutoBaseUi.startingPointAppliedControlsSummary,
            controlWidth);
    } else {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
    }
    ImGui::EndGroup();
    TooltipIfHovered(
        "Durable summary of the last Build Starting Point action for this RAW source.");

    const bool hasLastActionSummary =
        !m_RawWorkspaceAutoBaseUi.summary.empty() &&
        m_RawWorkspaceAutoBaseUi.sourceKey == selectedSource->relativePathKey &&
        m_RawWorkspaceAutoBaseUi.sourceHash == sourceHash;
    ImGui::Spacing();
    ImGui::TextDisabled("Last visible automatic action");
    RenderDisabledSummaryLine(
        hasLastActionSummary
            ? m_RawWorkspaceAutoBaseUi.summary
            : std::string("None yet."),
        controlWidth);
    TooltipIfHovered(
        "Summarizes the last visible automatic action for this RAW source without applying new edits.");

    const std::uint64_t currentAnalysisHash =
        hasUsableStats ? BuildAnalysisHash(m_RawWorkspaceAnalysis) : 0;
    const std::string displayFitStateSummary =
        BuildBaseAssistDisplayFitStateSummary(
            hasUsableStats,
            m_RawWorkspaceAutoBaseUi.hasAppliedViewFit,
            m_RawWorkspaceAutoBaseUi.startingPointDisplayFitPending ||
                (m_RawWorkspaceAutoBaseUi.pendingStartingPoint.active &&
                 m_RawWorkspaceAutoBaseUi.pendingStartingPoint.phase ==
                    Stack::EditorModuleTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis),
            m_RawWorkspaceAutoBaseUi.viewTransformOwner,
            m_RawWorkspaceAutoBaseUi.appliedAnalysisHash,
            currentAnalysisHash);

    const bool waitingForPostStartingPointFit =
        m_RawWorkspaceAutoBaseUi.pendingStartingPoint.active &&
        m_RawWorkspaceAutoBaseUi.pendingStartingPoint.phase ==
            Stack::EditorModuleTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
    const char* fitDisplayLabel =
        (m_RawWorkspaceAutoBaseUi.hasAppliedViewFit ||
         m_RawWorkspaceAutoBaseUi.startingPointDisplayFitPending ||
         waitingForPostStartingPointFit)
        ? "Refit Display"
        : "Fit Display";

    ImGui::Spacing();
    if (ImGui::TreeNodeEx("Details##RawWorkspaceStartingPointDetails")) {
        ImGui::TextDisabled("Current one-click plan");
        RenderDisabledWrappedLine(currentPlanSummary, controlWidth);
        TooltipIfHovered(
            "Shows the conservative plan the Build Starting Point button would execute now, including controls left manual or pending.");
        if (hasUsableStats &&
            !selectedCandidateSummary.empty() &&
            !IsEmptyOrNoneSummary(selectedCandidateSummary)) {
            ImGui::TextDisabled("Selected solver proposal");
            RenderDisabledWrappedLine(selectedCandidateSummary, controlWidth);
            TooltipIfHovered(
                "Shows the selected solver candidate before conservative one-click policy. Compare this with the control plan to see proposed-but-manual or pending settings.");
        }
        if (!currentControlPlanSummary.empty() &&
            !IsEmptyOrNoneSummary(currentControlPlanSummary)) {
            ImGui::TextDisabled("Current control plan");
            RenderDisabledWrappedLine(currentControlPlanSummary, controlWidth);
        }
        if (!currentPlanValuesSummary.empty() &&
            !IsEmptyOrNoneSummary(currentPlanValuesSummary)) {
            ImGui::TextDisabled("Planned values");
            RenderDisabledWrappedLine(currentPlanValuesSummary, controlWidth);
        }

        if (hasStartingPointResult) {
            if (!m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary.empty() &&
                !IsEmptyOrNoneSummary(m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary)) {
                ImGui::TextDisabled("Control status");
                RenderDisabledWrappedLine(
                    m_RawWorkspaceAutoBaseUi.startingPointControlStatusSummary,
                    controlWidth);
            }
            if (!m_RawWorkspaceAutoBaseUi.startingPointAppliedValuesSummary.empty() &&
                !IsEmptyOrNoneSummary(m_RawWorkspaceAutoBaseUi.startingPointAppliedValuesSummary)) {
                ImGui::TextDisabled("Changed values");
                RenderDisabledWrappedLine(
                    m_RawWorkspaceAutoBaseUi.startingPointAppliedValuesSummary,
                    controlWidth);
            }
            if (!m_RawWorkspaceAutoBaseUi.startingPointWithheldControlsSummary.empty() &&
                !IsEmptyOrNoneSummary(m_RawWorkspaceAutoBaseUi.startingPointWithheldControlsSummary)) {
                ImGui::TextDisabled("Left manual / unchanged");
                RenderDisabledWrappedLine(
                    m_RawWorkspaceAutoBaseUi.startingPointWithheldControlsSummary,
                    controlWidth);
            }
            if (!m_RawWorkspaceAutoBaseUi.startingPointEvidenceSummary.empty() &&
                !IsEmptyOrNoneSummary(m_RawWorkspaceAutoBaseUi.startingPointEvidenceSummary)) {
                ImGui::TextDisabled("Evidence used");
                RenderDisabledWrappedLine(
                    m_RawWorkspaceAutoBaseUi.startingPointEvidenceSummary,
                    controlWidth);
            }
            if (!m_RawWorkspaceAutoBaseUi.startingPointWarningSummary.empty() &&
                !IsEmptyOrNoneSummary(m_RawWorkspaceAutoBaseUi.startingPointWarningSummary)) {
                ImGui::TextDisabled("Warnings");
                RenderDisabledWrappedLine(
                    m_RawWorkspaceAutoBaseUi.startingPointWarningSummary,
                    controlWidth);
            }
        }

        ImGui::TextDisabled("Last visible automatic action");
        RenderDisabledSummaryLine(
            hasLastActionSummary
                ? m_RawWorkspaceAutoBaseUi.summary
                : std::string("None yet."),
            controlWidth);
        TooltipIfHovered(
            "Summarizes the last visible automatic action for this RAW source without applying new edits.");

        ImGui::TextDisabled("Display Fit state");
        RenderDisabledSummaryLine(displayFitStateSummary, controlWidth);
        TooltipIfHovered(
            "Read-only Display Fit ownership and freshness state from the current RAW workspace. Refit Display remains an explicit visible action.");

        if (ImGui::SmallButton("Diagnostics##RawWorkspaceBaseAssistDiagnostics")) {
            m_RawWorkspaceLayoutUi.diagnosticsOpenRequested = true;
        }
        TooltipIfHovered(
            "Open Diagnostics for Starting Point candidate scores, visible controls, action readiness, and warnings.");
        ImGui::TreePop();
    }

    if (ImGui::CollapsingHeader("Advanced starting-point actions##RawWorkspaceAdvancedStartingPoint")) {
        if (ImGuiExtras::RichFullWidthButton("Analyze only##RawStartingPointAnalyzeOnly", buttonWidth, 0.0f)) {
            MarkRenderRefreshDirty();
            m_RawWorkspaceAutoBaseUi.summary = m_RawWorkspaceAnalysis.currentFrameStats.valid
                ? "Display Fit analyzed current frame. Fit Display to update View Transform."
                : "Display Fit pending: render preview to analyze the frame.";
        }
        TooltipIfHovered("Refreshes current-frame analysis without changing recipe values.");

        if (!stackButtons) {
            ImGui::SameLine(0.0f, buttonGap);
        }
        ImGui::BeginDisabled(!hasUsableStats || waitingForPostStartingPointFit);
        const std::string fitDisplayButtonLabel =
            std::string(fitDisplayLabel) + " only##RawStartingPointFitDisplayOnly";
        if (ImGuiExtras::RichFullWidthButton(fitDisplayButtonLabel.c_str(), buttonWidth, 0.0f)) {
            recipeUpdated = ApplyRawWorkspaceAutoBaseViewFitForSource(*selectedSource, editedRecipe, true);
        }
        ImGui::EndDisabled();
        TooltipIfHovered(
            "Fits the display rendering for this RAW file using robust scene-linear frame statistics. This makes the image readable without changing RAW Exposure.",
            ImGuiHoveredFlags_AllowWhenDisabled);

        if (!stackButtons) {
            ImGui::SameLine(0.0f, buttonGap);
        }
        ImGui::BeginDisabled(!canRevert);
        if (ImGuiExtras::RichFullWidthButton("Undo##RawStartingPointUndo", buttonWidth, 0.0f)) {
            recipeUpdated = RevertRawWorkspaceAutoBaseForSelectedSource();
            if (recipeUpdated) {
                editedRecipe = m_ActiveRawWorkspaceRecipe;
            }
        }
        ImGui::EndDisabled();
        TooltipIfHovered(
            "Restores the recipe values from before Display Fit, Build Starting Point, Local Range, or Mild Tone was applied.",
            ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::Spacing();
        const std::string actionScopeSummary = BuildBaseAssistActionScopeSummary();
        ImGui::TextDisabled("Visible action scope");
        RenderDisabledSummaryLine(actionScopeSummary, controlWidth);
        TooltipIfHovered(
            "Read-only map of which visible recipe controls each Starting Point action is allowed to write.");

        ImGui::Spacing();
        ImGui::BeginDisabled(!canBuildBalancedLocal);
        if (ImGuiExtras::RichFullWidthButton(
                "Add Local Range##RawStartingPointBalancedLocal",
                optionalActionButtonWidth,
                0.0f)) {
            recipeUpdated =
                ApplyRawWorkspaceStartingPointBalancedLocalForSource(*selectedSource, editedRecipe);
        }
        ImGui::EndDisabled();
        TooltipIfHovered(
            "Adds up to two capped visible Local Range adjustment points from Balanced evidence. RAW Exposure, White Balance, Display Fit, and Finish Tone remain unchanged.",
            ImGuiHoveredFlags_AllowWhenDisabled);

        if (!stackOptionalActionButtons) {
            ImGui::SameLine(0.0f, buttonGap);
        }
        ImGui::BeginDisabled(!canAddMildTone);
        if (ImGuiExtras::RichFullWidthButton(
                "Add Mild Tone##RawStartingPointMildTone",
                optionalActionButtonWidth,
                0.0f)) {
            recipeUpdated =
                ApplyRawWorkspaceStartingPointMildToneForSource(*selectedSource, editedRecipe);
        }
        ImGui::EndDisabled();
        TooltipIfHovered(
            "Adds a mild visible Finish Tone graph only when the compact Finish Tone section can show the authored points. RAW Exposure, White Balance, Local Range, and Display Fit remain unchanged.",
            ImGuiHoveredFlags_AllowWhenDisabled);
    }

    return recipeUpdated;
}
