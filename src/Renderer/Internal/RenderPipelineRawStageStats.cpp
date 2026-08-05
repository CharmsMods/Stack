#include "Renderer/RenderPipeline.h"

#include "Raw/RawImageAnalysis.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {

float PercentToFraction(float value) {
    return std::clamp(value * 0.01f, 0.0f, 1.0f);
}

Stack::RawAutoStartPoint::RawAutoStartPointPercentiles LumaPercentilesFromTextureStats(
    const RenderTextureStats& stats) {
    Stack::RawAutoStartPoint::RawAutoStartPointPercentiles percentiles;
    percentiles.valid = stats.valid;
    percentiles.p01 = stats.p01Luma;
    percentiles.p05 = stats.p05Luma;
    percentiles.p10 = stats.p10Luma;
    percentiles.p25 = stats.p25Luma;
    percentiles.p50 = stats.p50Luma;
    percentiles.p75 = stats.p75Luma;
    percentiles.p90 = stats.p90Luma;
    percentiles.p95 = stats.p95Luma;
    percentiles.p99 = stats.p99Luma;
    percentiles.p999 = stats.p999Luma;
    return percentiles;
}

Stack::RawAutoStartPoint::RawAutoStartPointPercentiles EvPercentilesFromTextureStats(
    const RenderTextureStats& stats) {
    Stack::RawAutoStartPoint::RawAutoStartPointPercentiles percentiles;
    percentiles.valid = stats.valid;
    percentiles.p01 = Stack::RawAnalysis::SafeLog2Luma(stats.p01Luma);
    percentiles.p05 = Stack::RawAnalysis::SafeLog2Luma(stats.p05Luma);
    percentiles.p10 = Stack::RawAnalysis::SafeLog2Luma(stats.p10Luma);
    percentiles.p25 = Stack::RawAnalysis::SafeLog2Luma(stats.p25Luma);
    percentiles.p50 = Stack::RawAnalysis::SafeLog2Luma(stats.p50Luma);
    percentiles.p75 = Stack::RawAnalysis::SafeLog2Luma(stats.p75Luma);
    percentiles.p90 = Stack::RawAnalysis::SafeLog2Luma(stats.p90Luma);
    percentiles.p95 = Stack::RawAnalysis::SafeLog2Luma(stats.p95Luma);
    percentiles.p99 = Stack::RawAnalysis::SafeLog2Luma(stats.p99Luma);
    percentiles.p999 = Stack::RawAnalysis::SafeLog2Luma(stats.p999Luma);
    return percentiles;
}

Stack::RawAutoStartPoint::RawAutoStartPointStageDiagnostics BuildStageDiagnostics(
    const RawDevelopmentStageStatsReadback& readback) {
    Stack::RawAutoStartPoint::RawAutoStartPointStageDiagnostics diagnostics;
    diagnostics.stage = readback.stage;
    diagnostics.status = readback.valid
        ? readback.status
        : Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Unavailable;
    diagnostics.confidence01 =
        diagnostics.status == Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete
            ? 1.0f
            : (readback.valid ? 0.65f : 0.0f);
    diagnostics.statusMessage = readback.sourceDescription.empty()
        ? readback.measurementDomain
        : readback.sourceDescription + " Domain: " + readback.measurementDomain + ".";
    if (diagnostics.status == Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Fallback) {
        diagnostics.warnings.push_back(
            "Fallback evidence is measured from an adjacent render boundary and does not replace the true named candidate render.");
    }
    if (readback.rawSafetyStats) {
        diagnostics.rawSafety = readback.rawSafety;
        diagnostics.rawSafety.valid = readback.valid && readback.rawSafety.valid;
        if (diagnostics.rawSafety.statusMessage.empty()) {
            diagnostics.rawSafety.statusMessage =
                "Raw safety stats were captured from RAW source data and metadata.";
        }
        return diagnostics;
    }

    diagnostics.rawSafety.statusMessage =
        "Raw safety stats are not part of this texture readback.";

    if (readback.displayMappedLinearRgb) {
        diagnostics.display.valid = readback.valid;
        diagnostics.display.status = diagnostics.status;
        diagnostics.display.transferFamily = readback.measurementDomain;
        diagnostics.display.displayClipHighFraction =
            PercentToFraction(readback.textureStats.displayClipHighPercent);
        diagnostics.display.displayClipLowFraction =
            PercentToFraction(readback.textureStats.displayClipLowPercent);
        diagnostics.display.displayLinearP05 = readback.textureStats.p05Luma;
        diagnostics.display.displayLinearP50 = readback.textureStats.p50Luma;
        diagnostics.display.displayLinearP95 = readback.textureStats.p95Luma;
        diagnostics.display.displayP05 = readback.textureStats.p05Luma;
        diagnostics.display.displayP50 = readback.textureStats.p50Luma;
        diagnostics.display.displayP95 = readback.textureStats.p95Luma;
        diagnostics.display.displaySpread =
            std::max(0.0f, readback.textureStats.p95Luma - readback.textureStats.p05Luma);
        diagnostics.display.readabilityScore = 0.0f;
        const bool encodedDisplay =
            readback.measurementDomain.find("sRGB") != std::string::npos ||
            readback.measurementDomain.find("encoded") != std::string::npos;
        diagnostics.display.metricsAreLinearDisplay = !encodedDisplay;
        diagnostics.display.statusMessage =
            encodedDisplay
                ? "Final display readback contains explicitly sRGB-encoded display values."
                : "Final display readback contains display-mapped linear RGB values.";
        return diagnostics;
    }

    diagnostics.scene.valid = readback.valid;
    diagnostics.scene.stage = readback.stage;
    diagnostics.scene.status = diagnostics.status;
    diagnostics.scene.validPixelFraction = PercentToFraction(readback.textureStats.validPixelPercent);
    diagnostics.scene.lumaPercentiles = LumaPercentilesFromTextureStats(readback.textureStats);
    diagnostics.scene.evPercentiles = EvPercentilesFromTextureStats(readback.textureStats);
    diagnostics.scene.logAverageY = readback.textureStats.logAverageLuma;
    diagnostics.scene.midSpreadEv =
        diagnostics.scene.evPercentiles.p75 - diagnostics.scene.evPercentiles.p25;
    diagnostics.scene.wideSpreadEv =
        diagnostics.scene.evPercentiles.p95 - diagnostics.scene.evPercentiles.p05;
    diagnostics.scene.shadowMassFraction = 0.0f;
    diagnostics.scene.highlightMassFraction =
        PercentToFraction(readback.textureStats.hdrPixelPercent);
    diagnostics.scene.statusMessage =
        "Scene-stage texture stats from the named pre-View-Transform readback.";
    return diagnostics;
}

} // namespace

void RenderPipeline::ClearRawDevelopmentStageStatsReadbacks() {
    m_RawDevelopmentViewTransformInputStats = {};
    m_RawDevelopmentFinalDisplayStats = {};
    m_RawDevelopmentStageStatsReadbacks.clear();
    m_RawDevelopmentStageImageReadbacks.clear();
    m_RawDevelopmentGraphScopeReadback = {};
}

Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics
RenderPipeline::BuildRawDevelopmentStartPointDiagnostics(
    const std::string& sourceKey) const {
    if (sourceKey.empty()) {
        return Stack::RawAutoStartPoint::MakeUnavailableDiagnostics(
            {},
            "No RAW workspace source is active.");
    }

    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics;
    diagnostics.valid = std::any_of(
        m_RawDevelopmentStageStatsReadbacks.begin(),
        m_RawDevelopmentStageStatsReadbacks.end(),
        [](const RawDevelopmentStageStatsReadback& readback) { return readback.valid; });
    diagnostics.dryRunOnly = true;
    diagnostics.appliedRecipeValues = false;
    diagnostics.sourceKey = sourceKey;
    diagnostics.statusMessage = diagnostics.valid
        ? "RAW starting-point stage readbacks are available for diagnostics only."
        : "RAW starting-point stage readbacks are unavailable.";

    Stack::RawAutoStartPoint::RawAutoStartPointCandidate candidate;
    candidate.valid = diagnostics.valid;
    candidate.kind = Stack::RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
    candidate.id = "current-render-stage-readbacks";
    candidate.label = "Current Render";
    candidate.summary =
        "Named stage stats for the current RAW workspace render; no recipe values were applied.";
    for (const RawDevelopmentStageStatsReadback& readback : m_RawDevelopmentStageStatsReadbacks) {
        const Stack::RawAutoStartPoint::RawAutoStartPointStageStatus status = readback.valid
            ? readback.status
            : Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Unavailable;
        candidate.stageDiagnostics.push_back(BuildStageDiagnostics(readback));
        diagnostics.uiView.lines.push_back({
            status == Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete
                ? Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info
                : Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Warning,
            readback.label,
            status == Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete
                ? "Ready"
                : Stack::RawAutoStartPoint::StageStatusStableString(status),
            readback.sourceDescription + " Domain: " + readback.measurementDomain + "."
        });
    }
    diagnostics.candidates.push_back(std::move(candidate));

    diagnostics.uiView.title = "Build Starting Point Stage Diagnostics";
    diagnostics.uiView.summary = diagnostics.statusMessage;
    diagnostics.uiView.lines.push_back({
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Info,
        "Recipe writes",
        "None",
        "Pass 1 only records named readback evidence."
    });
    return diagnostics;
}
