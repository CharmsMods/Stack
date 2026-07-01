#include "Editor/EditorModule.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace {

const char* AvailabilityLabel(bool value) {
    return value ? "yes" : "no";
}

float FiniteOrZero(float value) {
    return std::isfinite(value) ? value : 0.0f;
}

void RenderStatsRow(const char* label, float ev, float luma) {
    ImGui::TextDisabled(
        "%s  %+.2f EV  %.5f luma",
        label,
        FiniteOrZero(ev),
        std::max(0.0f, FiniteOrZero(luma)));
}

void RenderDiagnosticsRationale(const char* label, float confidence, const std::string& rationale) {
    if (rationale.empty()) {
        return;
    }
    ImGui::TextDisabled("%s  Confidence %.0f%%", label, std::clamp(confidence, 0.0f, 1.0f) * 100.0f);
    ImGui::TextWrapped("%s", rationale.c_str());
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
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations =
        m_RawWorkspaceAutoBaseUi.recommendations;

    ImGui::SeparatorText("Analysis State");
    if (analysis.sourceKey.empty()) {
        ImGui::TextDisabled("Analysis unavailable");
        ImGui::TextDisabled("Render a RAW preview to populate analysis and suggestions.");
    } else {
        ImGui::TextDisabled("Source: %s", analysis.sourceKey.c_str());
        if (analysis.sourceKey != m_ActiveRawWorkspaceSourceKey) {
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
    if (recommendations.exposure.usedCurrentFrameFallback) {
        ImGui::TextDisabled("Using fallback stats for RAW Exposure suggestion.");
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

    ImGui::SeparatorText("Withheld / Advisory");
    bool renderedWithheld = false;
    if (recommendations.exposure.blockedByHighlightRisk) {
        ImGui::TextDisabled("Withheld: positive RAW Exposure blocked by highlight risk.");
        renderedWithheld = true;
    }
    if (recommendations.whiteBalance.manualWhiteBalanceProtected &&
        recommendations.whiteBalance.alternateCandidateAvailable) {
        ImGui::TextDisabled("Withheld: White Balance suggestion protected by manual WB.");
        renderedWithheld = true;
    }
    if (recommendations.highlight.recommendReconstruction) {
        ImGui::TextDisabled("Advisory: highlight reconstruction has no RAW workspace control yet.");
        renderedWithheld = true;
    }
    if (recommendations.highlight.recommendAchromaticClip) {
        ImGui::TextDisabled("Advisory: achromatic highlight handling may reduce color artifacts.");
        renderedWithheld = true;
    }
    const Stack::RawAutoBase::NoiseDetailRecommendation& noiseDetail =
        recommendations.noiseDetail;
    if (noiseDetail.suggestChromaDenoise ||
        noiseDetail.suggestLumaDenoise ||
        noiseDetail.suggestReduceSharpening ||
        noiseDetail.shadowLiftEv >= 0.5f) {
        ImGui::TextDisabled("Advisory: detail/noise recommendation is informational in this UI pass.");
        ImGui::TextDisabled(
            "ISO %.0f  effective noise %.0f%%  shadow lift %+.2f EV",
            noiseDetail.iso,
            std::clamp(noiseDetail.effectiveNoiseScore, 0.0f, 1.0f) * 100.0f,
            noiseDetail.shadowLiftEv);
        renderedWithheld = true;
    }
    if (!renderedWithheld) {
        ImGui::TextDisabled("No withheld suggestions or advisories for the current analysis.");
    }

    ImGui::SeparatorText("Auto Base Rationale");
    bool renderedRationale = false;
    auto renderRationale = [&](const char* label, float confidence, const std::string& rationale) {
        if (rationale.empty()) {
            return;
        }
        RenderDiagnosticsRationale(label, confidence, rationale);
        renderedRationale = true;
    };
    renderRationale(
        "RAW Exposure",
        recommendations.exposure.confidence,
        recommendations.exposure.rationale);
    renderRationale(
        "White Balance",
        recommendations.whiteBalance.confidence,
        recommendations.whiteBalance.rationale);
    renderRationale(
        "Highlight Risk",
        recommendations.highlight.confidence,
        recommendations.highlight.rationale);
    renderRationale(
        "Noise / Detail",
        recommendations.noiseDetail.confidence,
        recommendations.noiseDetail.rationale);
    if (!recommendations.localSuggestionRationale.empty()) {
        ImGui::TextDisabled("Local Range");
        ImGui::TextWrapped("%s", recommendations.localSuggestionRationale.c_str());
        renderedRationale = true;
    }
    for (std::size_t i = 0; i < recommendations.localAdjustments.size(); ++i) {
        const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion =
            recommendations.localAdjustments[i];
        if (!suggestion.valid || suggestion.rationale.empty()) {
            continue;
        }
        const std::string label = suggestion.label.empty()
            ? std::string(Stack::RawAutoBase::SuggestedLocalAdjustmentKindLabel(suggestion.kind))
            : suggestion.label;
        RenderDiagnosticsRationale(label.c_str(), suggestion.confidence, suggestion.rationale);
        renderedRationale = true;
    }
    if (!renderedRationale) {
        ImGui::TextDisabled("No Auto Base rationale is available yet.");
    }
}
