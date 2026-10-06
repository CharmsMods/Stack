#include "Editor/EditorModule.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace {

const char* AvailabilityLabel(bool value) {
    return value ? "yes" : "no";
}

float FiniteOrZero(float value) {
    return std::isfinite(value) ? value : 0.0f;
}

float Percent01(float value) {
    return std::clamp(FiniteOrZero(value), 0.0f, 1.0f) * 100.0f;
}

std::uint64_t MixDisplayFitDiagnosticsHash(std::uint64_t seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

std::uint64_t HashDisplayFitDiagnosticsFloat(float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    return static_cast<std::uint64_t>(std::hash<float>{}(value));
}

std::uint64_t BuildDisplayFitDiagnosticsAnalysisHash(
    const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
    std::uint64_t seed = static_cast<std::uint64_t>(std::hash<std::string>{}(analysis.sourceKey));
    seed = MixDisplayFitDiagnosticsHash(seed, HashDisplayFitDiagnosticsFloat(stats.p01Ev));
    seed = MixDisplayFitDiagnosticsHash(seed, HashDisplayFitDiagnosticsFloat(stats.p05Ev));
    seed = MixDisplayFitDiagnosticsHash(seed, HashDisplayFitDiagnosticsFloat(stats.p50Ev));
    seed = MixDisplayFitDiagnosticsHash(seed, HashDisplayFitDiagnosticsFloat(stats.p99Ev));
    seed = MixDisplayFitDiagnosticsHash(seed, HashDisplayFitDiagnosticsFloat(stats.p999Ev));
    seed = MixDisplayFitDiagnosticsHash(seed, HashDisplayFitDiagnosticsFloat(stats.dynamicRangeEv));
    seed = MixDisplayFitDiagnosticsHash(
        seed,
        HashDisplayFitDiagnosticsFloat(analysis.highlight.displayClipPercent));
    seed = MixDisplayFitDiagnosticsHash(
        seed,
        HashDisplayFitDiagnosticsFloat(analysis.highlight.hdrPixelPercent));
    return seed;
}

const char* DisplayFitOwnerDiagnosticsLabel(
    Stack::EditorModuleTypes::RawAutoValueOwner owner,
    bool hasAppliedViewFit) {
    switch (owner) {
        case Stack::EditorModuleTypes::RawAutoValueOwner::AutoBase:
            return hasAppliedViewFit ? "Auto Fit" : "Auto adjustment";
        case Stack::EditorModuleTypes::RawAutoValueOwner::User:
            return "Manual/locked";
        case Stack::EditorModuleTypes::RawAutoValueOwner::None:
        default:
            return "None";
    }
}

std::string BuildDisplayFitDiagnosticsStateSummary(
    bool hasSelectedSource,
    bool stateMatchesSelectedSource,
    bool hasUsableStats,
    bool hasAppliedViewFit,
    Stack::EditorModuleTypes::RawAutoValueOwner owner,
    std::uint64_t appliedAnalysisHash,
    std::uint64_t currentAnalysisHash) {
    if (!hasSelectedSource) {
        return "Display Fit state: no RAW source is selected.";
    }
    if (!stateMatchesSelectedSource) {
        return "Display Fit state: preview pending. No Display Fit state is recorded for the selected RAW source yet.";
    }
    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::User) {
        return "Display Fit state: manual/locked. Refit Display is explicit; automatic fitting will not rewrite manual View Transform edits.";
    }
    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::AutoBase && hasAppliedViewFit) {
        if (!hasUsableStats) {
            return "Display Fit state: auto fit recorded. Render or analyze a preview to compare it with current frame statistics.";
        }
        if (appliedAnalysisHash != 0 && appliedAnalysisHash == currentAnalysisHash) {
            return "Display Fit state: auto-current. The displayed View Transform was fit from the current analysis.";
        }
        return "Display Fit state: needs refit. Current analysis differs from the last Display Fit.";
    }
    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::AutoBase) {
        return "Display Fit state: auto-adjusted. A visible View Transform adjustment is auto-owned, but it was not recorded as a full Display Fit.";
    }
    if (!hasUsableStats) {
        return "Display Fit state: preview pending. Render or analyze a preview before fitting the display.";
    }
    return "Display Fit state: ready. Fit Display can map the current edit without changing RAW Exposure.";
}

std::string BuildAppliedSuggestionDiagnosticsStateSummary(
    bool hasSelectedSource,
    bool hasAppliedSuggestion,
    bool stateMatchesSelectedSource,
    bool sourceIdentityMatches,
    bool hasUsableStats,
    std::uint64_t appliedAnalysisHash,
    std::uint64_t currentAnalysisHash) {
    if (!hasSelectedSource) {
        return "Applied suggestion state: no RAW source is selected.";
    }
    if (!hasAppliedSuggestion) {
        return "Applied suggestion state: none recorded for the selected RAW source.";
    }
    if (!stateMatchesSelectedSource) {
        return "Applied suggestion state: recorded for another RAW source.";
    }
    if (!sourceIdentityMatches) {
        return "Applied suggestion state: recorded source identity no longer matches the selected RAW source.";
    }
    if (appliedAnalysisHash == 0) {
        return "Applied suggestion state: recorded for the selected RAW source; analysis freshness was not recorded.";
    }
    if (!hasUsableStats) {
        return "Applied suggestion state: recorded for the selected RAW source. Render or analyze a preview to compare it with current frame statistics.";
    }
    if (appliedAnalysisHash == currentAnalysisHash) {
        return "Applied suggestion state: current. The applied suggestion was recorded against the current analysis.";
    }
    return "Applied suggestion state: stale. Current analysis differs from the analysis that produced the applied suggestion.";
}

std::string BuildUndoSnapshotDiagnosticsStateSummary(
    bool hasSelectedSource,
    bool hasUndoSnapshot,
    bool stateMatchesSelectedSource,
    bool sourceIdentityMatches) {
    if (!hasSelectedSource) {
        return "Undo snapshot state: no RAW source is selected.";
    }
    if (!hasUndoSnapshot) {
        return "Undo snapshot state: none recorded for the selected RAW source.";
    }
    if (!stateMatchesSelectedSource) {
        return "Undo snapshot state: recorded for another RAW source.";
    }
    if (!sourceIdentityMatches) {
        return "Undo snapshot state: recorded source identity no longer matches the selected RAW source.";
    }
    return "Undo snapshot state: available. Undo restores the recipe snapshot from before the last visible automatic action.";
}

std::string FormatOneDecimal(float value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f", FiniteOrZero(value));
    return std::string(buffer);
}

void RenderStatsRow(const char* label, float ev, float luma) {
    ImGui::TextDisabled(
        "%s  %+.2f EV  %.5f luma",
        label,
        FiniteOrZero(ev),
        std::max(0.0f, FiniteOrZero(luma)));
}

void RenderTextureStageStatsRow(const RawDevelopmentStageStatsReadback& readback) {
    const RenderTextureStats& stats = readback.textureStats;
    ImGui::TextDisabled("%s", readback.label.empty() ? readback.stageId.c_str() : readback.label.c_str());
    ImGui::TextDisabled(
        "Status: %s",
        Stack::RawAutoStartPoint::StageStatusStableString(
            readback.valid
                ? readback.status
                : Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Unavailable));
    if (!readback.sourceDescription.empty()) {
        ImGui::TextWrapped("%s", readback.sourceDescription.c_str());
    }
    if (!readback.measurementDomain.empty()) {
        ImGui::TextDisabled("Domain: %s", readback.measurementDomain.c_str());
    }
    if (!stats.valid) {
        ImGui::TextDisabled("Stats unavailable");
        return;
    }
    ImGui::TextDisabled(
        "p5 %.5f  p50 %.5f  p95 %.5f  log avg %.5f",
        std::max(0.0f, FiniteOrZero(stats.p05Luma)),
        std::max(0.0f, FiniteOrZero(stats.p50Luma)),
        std::max(0.0f, FiniteOrZero(stats.p95Luma)),
        std::max(0.0f, FiniteOrZero(stats.logAverageLuma)));
    ImGui::TextDisabled(
        "range %.2f EV  HDR %.2f%%  edge high %.2f%%  edge low %.2f%%",
        FiniteOrZero(stats.dynamicRangeEv),
        FiniteOrZero(stats.hdrPixelPercent),
        FiniteOrZero(stats.displayClipHighPercent),
        FiniteOrZero(stats.displayClipLowPercent));
}

void RenderDiagnosticsRationale(const char* label, float confidence, const std::string& rationale) {
    if (rationale.empty()) {
        return;
    }
    ImGui::TextDisabled("%s  Confidence %.0f%%", label, std::clamp(confidence, 0.0f, 1.0f) * 100.0f);
    ImGui::TextWrapped("%s", rationale.c_str());
}

const char* StartPointDiagnosticSeverityLabel(
    Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity severity) {
    switch (severity) {
        case Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Warning:
            return "Warning";
        case Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Error:
            return "Error";
        case Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info:
        default:
            return "Info";
    }
}

std::string BuildStartPointDiagnosticLineHeader(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine& line) {
    std::string header = StartPointDiagnosticSeverityLabel(line.severity);
    if (!line.label.empty() || !line.value.empty()) {
        header += ": ";
    }
    if (!line.label.empty()) {
        header += line.label;
    }
    if (!line.value.empty()) {
        if (!line.label.empty()) {
            header += " - ";
        }
        header += line.value;
    }
    return header;
}

void RenderDisabledWrappedText(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void RenderStartPointDiagnosticsViewLines(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView& view) {
    for (const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine& line :
         view.lines) {
        RenderDisabledWrappedText(BuildStartPointDiagnosticLineHeader(line));
        if (!line.detail.empty()) {
            ImGui::TextWrapped("%s", line.detail.c_str());
        }
    }
}

void RenderStartPointRawSafetySummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats& stats) {
    if (!stats.valid) {
        return;
    }
    ImGui::TextDisabled(
        "Raw safety evidence: active %.1f%%  headroom %+.2f EV  WB-scaled %+.2f EV",
        Percent01(stats.activeValidFraction),
        FiniteOrZero(stats.headroomEv),
        FiniteOrZero(stats.wbScaledHeadroomEv));
    ImGui::TextDisabled(
        "Raw clipping: single %.3f%%  multi %.3f%%  full %.3f%%  hot %.3f%%",
        Percent01(stats.singleChannelClipFraction),
        Percent01(stats.multiChannelClipFraction),
        Percent01(stats.fullClipFraction),
        Percent01(stats.hotPixelFraction));
    if (!stats.blackLevelSource.empty() || !stats.whiteLevelSource.empty()) {
        ImGui::TextDisabled(
            "Raw levels: black %s  white %s",
            stats.blackLevelSource.empty() ? "unknown" : stats.blackLevelSource.c_str(),
            stats.whiteLevelSource.empty() ? "unknown" : stats.whiteLevelSource.c_str());
    }
    if (stats.blackDriftWarning) {
        ImGui::TextWrapped("Warning: raw black-level drift was reported for this stage.");
    }
    if (!stats.statusMessage.empty()) {
        ImGui::TextWrapped("%s", stats.statusMessage.c_str());
    }
}

void RenderStartPointSceneSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointSceneStageStats& stats) {
    if (!stats.valid) {
        return;
    }
    ImGui::TextDisabled(
        "Scene evidence: valid %.1f%%  log avg %.5f  mid %.2f EV  wide %.2f EV",
        Percent01(stats.validPixelFraction),
        std::max(0.0f, FiniteOrZero(stats.logAverageY)),
        FiniteOrZero(stats.midSpreadEv),
        FiniteOrZero(stats.wideSpreadEv));
    if (stats.evPercentiles.valid || stats.lumaPercentiles.valid) {
        ImGui::TextDisabled(
            "Scene percentiles: p50 %+.2f EV / %.5f luma  p95 %+.2f EV / %.5f luma",
            FiniteOrZero(stats.evPercentiles.p50),
            std::max(0.0f, FiniteOrZero(stats.lumaPercentiles.p50)),
            FiniteOrZero(stats.evPercentiles.p95),
            std::max(0.0f, FiniteOrZero(stats.lumaPercentiles.p95)));
    }
    ImGui::TextDisabled(
        "Scene guards: shadows %.1f%%  highlights %.1f%%  gamut %.1f%%  halo risk %.1f%%",
        Percent01(stats.shadowMassFraction),
        Percent01(stats.highlightMassFraction),
        Percent01(stats.wideGamutPressure),
        Percent01(stats.localMaskHaloRisk));
    if (stats.neutralSampleSummary.valid) {
        ImGui::TextDisabled(
            "Neutral evidence: %d samples  eligible %.1f%%  chroma %.3f  gain %.2f EV",
            stats.neutralSampleSummary.sampleCount,
            Percent01(stats.neutralSampleSummary.eligibleFraction),
            FiniteOrZero(stats.neutralSampleSummary.medianChroma),
            FiniteOrZero(stats.neutralSampleSummary.gainDistanceEv));
    }
    if (!stats.statusMessage.empty()) {
        ImGui::TextWrapped("%s", stats.statusMessage.c_str());
    }
}

void RenderStartPointDisplaySummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointDisplayStageStats& stats) {
    if (!stats.valid) {
        return;
    }
    const float p05 = stats.metricsAreLinearDisplay ? stats.displayLinearP05 : stats.displayP05;
    const float p50 = stats.metricsAreLinearDisplay ? stats.displayLinearP50 : stats.displayP50;
    const float p95 = stats.metricsAreLinearDisplay ? stats.displayLinearP95 : stats.displayP95;
    const char* domain = stats.transferFamily.empty()
        ? (stats.metricsAreLinearDisplay ? "linear display" : "display")
        : stats.transferFamily.c_str();
    ImGui::TextDisabled(
        "Display evidence (%s): p05 %.5f  p50 %.5f  p95 %.5f  spread %.5f",
        domain,
        std::max(0.0f, FiniteOrZero(p05)),
        std::max(0.0f, FiniteOrZero(p50)),
        std::max(0.0f, FiniteOrZero(p95)),
        std::max(0.0f, FiniteOrZero(stats.displaySpread)));
    ImGui::TextDisabled(
        "Display clipping: high %.3f%%  low %.3f%%  readability %.0f%%",
        Percent01(stats.displayClipHighFraction),
        Percent01(stats.displayClipLowFraction),
        Percent01(stats.readabilityScore));
    if (!stats.statusMessage.empty()) {
        ImGui::TextWrapped("%s", stats.statusMessage.c_str());
    }
}

std::string BuildStartPointScoreBreakdownLabel(
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidateScore& score) {
    std::string label = "Score breakdown";
    if (!score.terms.empty() || !score.penalties.empty()) {
        label += " - ";
        label += std::to_string(score.terms.size());
        label += score.terms.size() == 1 ? " term" : " terms";
        if (!score.penalties.empty()) {
            label += ", ";
            label += std::to_string(score.penalties.size());
            label += score.penalties.size() == 1 ? " penalty" : " penalties";
        }
    }
    label += "##StartPointScoreBreakdown";
    return label;
}

void RenderStartPointScoreBreakdown(
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidateScore& score) {
    for (const Stack::RawAutoStartPoint::RawAutoStartPointScoreTerm& term : score.terms) {
        ImGui::TextDisabled(
            "%s  %.0f%% x %.0f = %.1f",
            term.label.c_str(),
            std::clamp(term.value01, 0.0f, 1.0f) * 100.0f,
            FiniteOrZero(term.weight),
            FiniteOrZero(term.weightedValue));
        if (!term.rationale.empty()) {
            ImGui::TextWrapped("%s", term.rationale.c_str());
        }
    }
    for (const Stack::RawAutoStartPoint::RawAutoStartPointPenaltyTerm& penalty : score.penalties) {
        ImGui::TextDisabled(
            "Penalty: %s  %.0f%% x %.0f = -%.1f",
            penalty.label.c_str(),
            std::clamp(penalty.value, 0.0f, 1.0f) * 100.0f,
            FiniteOrZero(penalty.weight),
            FiniteOrZero(penalty.weightedValue));
        if (!penalty.rationale.empty()) {
            ImGui::TextWrapped("%s", penalty.rationale.c_str());
        }
    }
}

void RenderStartPointScore(
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidateScore& score) {
    if (!score.valid) {
        ImGui::TextDisabled("Score unavailable");
        return;
    }
    ImGui::TextDisabled("Score %.1f / 100", FiniteOrZero(score.totalScore));
    if (!score.summary.empty()) {
        ImGui::TextWrapped("%s", score.summary.c_str());
    }
    if (score.terms.empty() && score.penalties.empty()) {
        return;
    }
    const std::string disclosureLabel = BuildStartPointScoreBreakdownLabel(score);
    if (ImGui::TreeNodeEx(disclosureLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        RenderStartPointScoreBreakdown(score);
        ImGui::TreePop();
    }
}

void RenderStartPointVisibleEdits(
    const Stack::RawAutoStartPoint::RawAutoStartPointVisibleRecipeEdits& edits) {
    if (edits.rawExposureEvValid) {
        ImGui::TextDisabled("RAW Exposure candidate: %+.2f EV", edits.preToneExposureEv);
    }
    if (edits.whiteBalanceValid && !edits.whiteBalancePolicy.empty()) {
        ImGui::TextDisabled("White Balance: %s", edits.whiteBalancePolicy.c_str());
    }
    if (!edits.localRangeSummary.empty()) {
        ImGui::TextDisabled("Local Range: %s", edits.localRangeSummary.c_str());
    }
    if (!edits.finishToneSummary.empty()) {
        ImGui::TextDisabled("Finish Tone: %s", edits.finishToneSummary.c_str());
    }
    if (edits.displayFitValid && !edits.viewTransformSummary.empty()) {
        ImGui::TextDisabled("Display Fit: %s", edits.viewTransformSummary.c_str());
    } else if (!edits.viewTransformSummary.empty()) {
        ImGui::TextDisabled("Display Fit: %s", edits.viewTransformSummary.c_str());
    }
    if (!edits.touchedControls.empty()) {
        std::string controls;
        for (Stack::RawAutoStartPoint::RawAutoStartPointControl control : edits.touchedControls) {
            if (!controls.empty()) {
                controls += ", ";
            }
            controls += Stack::RawAutoStartPoint::ControlLabel(control);
        }
        ImGui::TextDisabled("Would touch: %s", controls.c_str());
    } else {
        ImGui::TextDisabled("Would touch: none");
    }
}

const Stack::RawAutoStartPoint::RawAutoStartPointCandidate* SelectedStartPointCandidate(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics) {
    if (!diagnostics.hasSelectedCandidate ||
        diagnostics.selectedCandidateIndex < 0 ||
        diagnostics.selectedCandidateIndex >= static_cast<int>(diagnostics.candidates.size())) {
        return nullptr;
    }
    return &diagnostics.candidates[static_cast<std::size_t>(diagnostics.selectedCandidateIndex)];
}

std::string BuildStartPointRecipeWriteStateText(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics) {
    if (diagnostics.appliedRecipeValues) {
        return "Recipe state: visible recipe values were reported as applied for this Starting Point result.";
    }
    if (diagnostics.dryRunOnly) {
        return "Recipe state: dry run only; no recipe values were applied from this report.";
    }
    return "Recipe state: no recipe values were reported as applied from this report.";
}

void RenderStartPointSelectedCandidateSummary(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics) {
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidate* selected =
        SelectedStartPointCandidate(diagnostics);
    if (selected == nullptr) {
        return;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Selected Candidate");
    const std::string selectedLabel = selected->label.empty()
        ? Stack::RawAutoStartPoint::CandidateKindLabel(selected->kind)
        : selected->label;
    RenderDisabledWrappedText(
        std::string(diagnostics.dryRunOnly ? "Selected dry-run candidate: " : "Selected candidate: ") +
        selectedLabel);
    if (selected->score.valid) {
        ImGui::TextDisabled("Selected score: %.1f / 100", FiniteOrZero(selected->score.totalScore));
    }
    if (!selected->summary.empty()) {
        ImGui::TextWrapped("%s", selected->summary.c_str());
    }
    RenderStartPointVisibleEdits(selected->visibleEdits);
    RenderDisabledWrappedText(BuildStartPointRecipeWriteStateText(diagnostics));
}

std::string BuildStartPointCandidateDisclosureLabel(
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate,
    std::size_t candidateIndex,
    bool selected) {
    std::string label = candidate.label.empty()
        ? Stack::RawAutoStartPoint::CandidateKindLabel(candidate.kind)
        : candidate.label;
    if (selected) {
        label += " (selected)";
    }
    if (candidate.score.valid) {
        label += " - score ";
        label += FormatOneDecimal(candidate.score.totalScore);
    }
    label += "##StartPointCandidateEvidence";
    label += std::to_string(candidateIndex);
    return label;
}

std::string BuildStartPointStageDisclosureLabel(
    const Stack::RawAutoStartPoint::RawAutoStartPointStageDiagnostics& stage,
    std::size_t stageIndex) {
    std::string label = Stack::RawAutoStartPoint::StageLabel(stage.stage);
    label += " - ";
    label += Stack::RawAutoStartPoint::StageStatusStableString(stage.status);
    if (!stage.warnings.empty()) {
        label += " - ";
        label += std::to_string(stage.warnings.size());
        label += stage.warnings.size() == 1 ? " warning" : " warnings";
    }
    label += "##StartPointStageEvidence";
    label += std::to_string(stageIndex);
    return label;
}

void RenderStartPointStageEvidence(
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointStageDiagnostics>& stages) {
    if (stages.empty()) {
        ImGui::TextDisabled("Stage evidence unavailable");
        return;
    }
    for (std::size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex) {
        const Stack::RawAutoStartPoint::RawAutoStartPointStageDiagnostics& stage =
            stages[stageIndex];
        const std::string disclosureLabel =
            BuildStartPointStageDisclosureLabel(stage, stageIndex);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (stage.status != Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete ||
            !stage.warnings.empty()) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (!ImGui::TreeNodeEx(disclosureLabel.c_str(), flags)) {
            continue;
        }
        if (!stage.statusMessage.empty()) {
            ImGui::TextWrapped("%s", stage.statusMessage.c_str());
        }
        RenderStartPointRawSafetySummary(stage.rawSafety);
        RenderStartPointSceneSummary(stage.scene);
        RenderStartPointDisplaySummary(stage.display);
        for (const std::string& warning : stage.warnings) {
            ImGui::TextWrapped("Warning: %s", warning.c_str());
        }
        ImGui::TreePop();
    }
}

void RenderStartPointCandidateEvidence(
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate) {
    if (!candidate.summary.empty()) {
        ImGui::TextWrapped("%s", candidate.summary.c_str());
    }
    RenderStartPointVisibleEdits(candidate.visibleEdits);
    RenderStartPointScore(candidate.score);
    if (!candidate.warnings.empty()) {
        for (const std::string& warning : candidate.warnings) {
            ImGui::TextWrapped("Warning: %s", warning.c_str());
        }
    }
    RenderStartPointStageEvidence(candidate.stageDiagnostics);
}

void RenderStartPointCandidateReport(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics) {
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView view =
        Stack::RawAutoStartPoint::BuildDiagnosticsView(diagnostics);
    ImGui::SeparatorText(
        view.title.empty() ? "Build Starting Point Diagnostics" : view.title.c_str());
    if (!view.summary.empty()) {
        ImGui::TextWrapped("%s", view.summary.c_str());
    }
    RenderStartPointDiagnosticsViewLines(view);
    ImGui::TextDisabled(
        "Dry run: %s  recipe writes: %s",
        diagnostics.dryRunOnly ? "yes" : "no",
        diagnostics.appliedRecipeValues ? "applied" : "none");
    if (diagnostics.candidates.empty()) {
        ImGui::TextDisabled("Render a RAW preview to produce candidate diagnostics.");
        return;
    }

    RenderStartPointSelectedCandidateSummary(diagnostics);

    ImGui::Spacing();
    ImGui::SeparatorText("Candidate Evidence");
    for (std::size_t candidateIndex = 0; candidateIndex < diagnostics.candidates.size(); ++candidateIndex) {
        const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate =
            diagnostics.candidates[candidateIndex];
        const bool selected =
            diagnostics.hasSelectedCandidate &&
            diagnostics.selectedCandidateIndex == static_cast<int>(candidateIndex);
        ImGui::Spacing();
        const std::string disclosureLabel =
            BuildStartPointCandidateDisclosureLabel(candidate, candidateIndex, selected);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (ImGui::TreeNodeEx(disclosureLabel.c_str(), flags)) {
            RenderStartPointCandidateEvidence(candidate);
            ImGui::TreePop();
        }
    }

    if (diagnostics.hasSelectedCandidate &&
        diagnostics.selectedCandidateIndex >= 0 &&
        diagnostics.selectedCandidateIndex < static_cast<int>(diagnostics.candidates.size())) {
        const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& selected =
            diagnostics.candidates[static_cast<std::size_t>(diagnostics.selectedCandidateIndex)];
        ImGui::TextDisabled(
            "Diagnostic selection: %s",
            selected.label.empty()
                ? Stack::RawAutoStartPoint::CandidateKindLabel(selected.kind)
                : selected.label.c_str());
    }
}

} // namespace

void EditorModule::RenderRawWorkspaceAnalysisPanel(float controlWidth) {
    (void)controlWidth;

    if (m_RawWorkspaceLayoutUi.diagnosticsOpenRequested) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        m_RawWorkspaceLayoutUi.diagnosticsOpenRequested = false;
    }
    const bool diagnosticsOpen = ImGui::CollapsingHeader("Diagnostics");
    m_RawWorkspaceLayoutUi.diagnosticsOpen = diagnosticsOpen;
    if (!diagnosticsOpen) {
        return;
    }

    const Stack::RawAnalysis::RawImageAnalysis& analysis = m_RawWorkspaceAnalysis;

    ImGui::SeparatorText("Analysis State");
    if (analysis.sourceKey.empty()) {
        ImGui::TextDisabled("Analysis unavailable");
        ImGui::TextDisabled("Render a RAW preview to populate technical diagnostics.");
    } else {
        ImGui::TextDisabled("Source: %s", analysis.sourceKey.c_str());
        if (analysis.sourceKey != m_Project->rawSourceKey) {
            ImGui::TextDisabled("Source mismatch");
        } else if (m_RenderPending) {
            ImGui::TextDisabled("Analyzing");
        } else if (!analysis.valid) {
            ImGui::TextDisabled("Analysis unavailable");
        } else {
            ImGui::TextDisabled("Analysis ready");
        }
    }
    if (!analysis.statusMessage.empty()) {
        ImGui::TextWrapped("%s", analysis.statusMessage.c_str());
    }

    ImGui::SeparatorText("Technical RAW");
    ImGui::TextDisabled(
        "Technical RAW stage: %s",
        Stack::RawAnalysis::AnalysisStageStatusLabel(analysis.technicalStats.status));
    if (!analysis.technicalStats.statusMessage.empty()) {
        ImGui::TextWrapped("%s", analysis.technicalStats.statusMessage.c_str());
    }

    const Stack::RawAnalysis::RawMetadataSummary& metadata = analysis.metadata;
    ImGui::TextDisabled(
        "Metadata: camera WB %s  baseline exposure %s  noise %s  sharpness %s",
        AvailabilityLabel(metadata.hasCameraWhiteBalance),
        AvailabilityLabel(metadata.hasBaselineExposure),
        AvailabilityLabel(metadata.hasBaselineNoise),
        AvailabilityLabel(metadata.hasBaselineSharpness));
    if (!metadata.hasCameraWhiteBalance ||
        !metadata.hasBaselineNoise ||
        !metadata.hasBaselineSharpness) {
        ImGui::TextDisabled("Metadata incomplete");
    }
    if (metadata.hasCameraWhiteBalance) {
        ImGui::TextDisabled(
            "Camera WB gains R %.3f  G %.3f  B %.3f",
            metadata.cameraWbR,
            metadata.cameraWbG,
            metadata.cameraWbB);
    }
    if (metadata.hasBaselineExposure) {
        ImGui::TextDisabled("DNG baseline exposure %+.2f EV", metadata.baselineExposureEv);
    }
    ImGui::TextDisabled(
        "DNG OpcodeList2 applied %d  unsupported L1/L2/L3 %d/%d/%d",
        metadata.appliedOpcodeList2Count,
        metadata.unsupportedOpcodeList1Count,
        metadata.unsupportedOpcodeList2Count,
        metadata.unsupportedOpcodeList3Count);
    if (metadata.unsupportedOpcodeList1Count +
            metadata.unsupportedOpcodeList2Count +
            metadata.unsupportedOpcodeList3Count >
        0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.68f, 0.24f, 1.0f));
        ImGui::TextWrapped(
            "This file requests DNG corrections Stack does not apply. Treat the rendered result as incomplete, not camera-faithful.");
        ImGui::PopStyleColor();
    }

    ImGui::SeparatorText("Highlight Signals");
    ImGui::TextDisabled(
        "RAW sensor clipping: %s",
        Stack::RawAnalysis::AnalysisStageStatusLabel(analysis.highlight.sensorStatus));
    ImGui::TextDisabled(
        "Positive RAW exposure blocked: %s",
        AvailabilityLabel(analysis.highlight.blocksPositiveRawExposure));
    ImGui::TextDisabled(
        "Display stage: %s",
        Stack::RawAnalysis::AnalysisStageStatusLabel(analysis.highlight.displayStatus));
    if (analysis.highlight.valid) {
        ImGui::TextDisabled(
            "Display-edge %.2f%%  HDR > 1.0 %.2f%%",
            FiniteOrZero(analysis.highlight.displayClipPercent),
            FiniteOrZero(analysis.highlight.hdrPixelPercent));
    }
    if (!analysis.highlight.statusMessage.empty()) {
        ImGui::TextWrapped("%s", analysis.highlight.statusMessage.c_str());
    }

    const Stack::RawAnalysis::PercentileStats& currentFrame = analysis.currentFrameStats;
    ImGui::SeparatorText("Current Frame Stats");
    ImGui::TextDisabled(
        "Stage: %s",
        Stack::RawAnalysis::AnalysisStageStatusLabel(currentFrame.status));
    if (currentFrame.valid) {
        RenderStatsRow("p0.1", currentFrame.p001Ev, currentFrame.p001Luma);
        RenderStatsRow("p1", currentFrame.p01Ev, currentFrame.p01Luma);
        RenderStatsRow("p5", currentFrame.p05Ev, currentFrame.p05Luma);
        RenderStatsRow("p50", currentFrame.p50Ev, currentFrame.p50Luma);
        RenderStatsRow("p95", currentFrame.p95Ev, currentFrame.p95Luma);
        RenderStatsRow("p99", currentFrame.p99Ev, currentFrame.p99Luma);
        RenderStatsRow("p99.9", currentFrame.p999Ev, currentFrame.p999Luma);
        ImGui::TextDisabled(
            "Log average %.5f  dynamic range %.2f EV  valid %.1f%%",
            std::max(0.0f, FiniteOrZero(currentFrame.logAverageLuma)),
            FiniteOrZero(currentFrame.dynamicRangeEv),
            FiniteOrZero(currentFrame.validPixelPercent));
    } else if (!currentFrame.statusMessage.empty()) {
        ImGui::TextWrapped("%s", currentFrame.statusMessage.c_str());
    } else {
        ImGui::TextDisabled("Render a RAW preview to populate current-frame stats.");
    }

}
