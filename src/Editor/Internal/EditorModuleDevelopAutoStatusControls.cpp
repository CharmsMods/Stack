#include "Editor/Internal/EditorModuleDevelopAutoStatusControls.h"

#include "Raw/RawImageData.h"

#include <cmath>
#include <imgui.h>
#include <string>

namespace Stack::Editor::DevelopAutoStatusControls {

void RenderDevelopAutoStatusReadouts(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const CandidateFeedbackStatus& candidateFeedback) {
    const Raw::RawDevelopSettings& settings = payload.settings;
    const Raw::RawDetailFusionSettings& scenePrepSettings = payload.scenePrepSettings;
    const nlohmann::json& toneJson = payload.integratedToneLayerJson;
    if (candidateFeedback.deferred) {
        if (candidateFeedback.quietRemainingSeconds > 0.01) {
            ImGui::TextDisabled(
                "Candidate feedback: waiting for edits to settle (%.1fs)",
                candidateFeedback.quietRemainingSeconds);
        } else {
            ImGui::TextDisabled("Candidate feedback: queued after edits settled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Auto keeps the viewport responsive while pausing expensive candidate feedback during recent Develop edits.");
        }
    }
    if (toneJson.value("autoSceneStatsValid", false)) {
        ImGui::TextDisabled(
            "Auto mode: %s",
            EditorNodeGraph::DevelopAutoIntentLabel(payload.autoGuidance.intent));
        if (toneJson.contains("autoCandidateSelectedLabel")) {
            ImGui::TextDisabled(
                "Auto candidate: %s  |  score %.2f  |  pass %d%s",
                toneJson.value("autoCandidateSelectedLabel", std::string("Base Solve")).c_str(),
                toneJson.value("autoCandidateSelectedScore", 0.0f),
                toneJson.value("autoCandidateConvergencePass", 0),
                toneJson.value("autoCandidateConverged", false) ? " converged" : "");
            const std::string renderMetricsStatus =
                toneJson.value("autoCandidateRenderMetricsStatus", std::string());
            if (!renderMetricsStatus.empty()) {
                ImGui::TextDisabled(
                    "Candidate renders: %s  |  measured %d  |  failed %d",
                    renderMetricsStatus.c_str(),
                    toneJson.value("autoCandidateRenderedCount", 0),
                    toneJson.value("autoCandidateRenderedFailureCount", 0));
                if (toneJson.value("autoCandidateRenderedTimingVersion", std::string()) == "CandidateRenderTimingV1") {
                    const std::string slowestLabel =
                        toneJson.value("autoCandidateRenderedSlowestLabel", std::string());
                    ImGui::TextDisabled(
                        "Feedback timing: total %.0f ms  |  graph %.0f  |  readback %.0f  |  analysis %.0f%s%s",
                        toneJson.value("autoCandidateRenderedTotalElapsedMs", 0.0),
                        toneJson.value("autoCandidateRenderedFinalGraphMs", 0.0) +
                            toneJson.value("autoCandidateRenderedPreFinishGraphMs", 0.0),
                        toneJson.value("autoCandidateRenderedFinalReadbackMs", 0.0) +
                            toneJson.value("autoCandidateRenderedPreFinishReadbackMs", 0.0),
                        toneJson.value("autoCandidateRenderedFinalAnalysisMs", 0.0) +
                            toneJson.value("autoCandidateRenderedPreFinishAnalysisMs", 0.0),
                        slowestLabel.empty() ? "" : "  |  slowest ",
                        slowestLabel.c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Diagnostic timing for rendered Auto candidate feedback. These numbers do not affect scoring.");
                    }
                }
                if (toneJson.value("autoCandidateRenderedMetricReadbackCapped", false)) {
                    ImGui::TextDisabled(
                        "Metric readback: capped to %d px  |  final %d  |  pre-finish %d",
                        toneJson.value("autoCandidateRenderedMetricReadbackMaxDimension", 0),
                        toneJson.value("autoCandidateRenderedMetricReadbackDownsampledCount", 0),
                        toneJson.value("autoCandidateRenderedPreFinishMetricReadbackDownsampledCount", 0));
                }
                const std::string renderedBestLabel =
                    toneJson.value("autoCandidateRenderedBestLabel", std::string());
                if (!renderedBestLabel.empty()) {
                    ImGui::TextDisabled(
                        "Rendered best: %s  |  metric %.2f",
                        renderedBestLabel.c_str(),
                        toneJson.value("autoCandidateRenderedBestScore", 0.0f));
                }
            }
        }
        ImGui::TextDisabled(
            "Current solve: exposure %+.2f EV  |  WB %s  |  highlights %s",
            settings.exposureStops,
            Raw::WhiteBalanceModeName(settings.whiteBalanceMode),
            Raw::HighlightReconstructionModeName(settings.highlightMode));
        ImGui::TextDisabled(
            "Scene prep: strength %.2f  |  shadow bias %+.2f EV  |  highlight guard %+.2f",
            scenePrepSettings.strength,
            scenePrepSettings.maxEvBias,
            scenePrepSettings.highlightProtectionBias);
        ImGui::TextDisabled(
            "Brightness distribution: intent %+.2f  |  RAW scale x%.2f  |  local EV %+.2f / %+.2f  |  tone contrast %+.2f",
            toneJson.value("autoBrightnessIntent", payload.autoGuidance.exposureBias),
            toneJson.value("autoAuthoredRawExposureScale", std::exp2(settings.exposureStops)),
            toneJson.value("autoAuthoredLocalMinEvBias", scenePrepSettings.minEvBias),
            toneJson.value("autoAuthoredLocalMaxEvBias", scenePrepSettings.maxEvBias),
            toneJson.value("autoContrastBias", payload.autoGuidance.contrastBias));
        ImGui::TextDisabled(
            "Exposure diagnostics: clipping %.2f%%  |  highlight pressure %.2f  |  noise risk %.2f  |  HDR spread %.2f EV",
            toneJson.value("autoExposureDiagnosticClippingRatio", 0.0f) * 100.0f,
            toneJson.value("autoExposureDiagnosticHighlightPressure", 0.0f),
            toneJson.value("autoExposureDiagnosticNoiseRisk", 0.0f),
            toneJson.value("autoExposureDiagnosticHdrSpreadEv", 0.0f));
        if (toneJson.value("autoDynamicRangeStrategyVersion", std::string()) == "DynamicRangeStrategyV1") {
            ImGui::TextDisabled(
                "Range strategy: %s  |  highlight %.2f  |  shadow %.2f  |  noise %.2f",
                toneJson.value("autoDynamicRangeStrategyLabel", std::string("Balanced Range")).c_str(),
                toneJson.value("autoDynamicRangeHighlightImportance", 0.0f),
                toneJson.value("autoDynamicRangeShadowReadability", 0.0f),
                toneJson.value("autoDynamicRangeNoiseConstraint", 0.0f));
            ImGui::TextDisabled(
                "Range probes: broad %.2f  |  lum %.2f  |  read %.2f  |  halo %.2f  |  sep %.2f  |  spec %.2f  |  floor %.2f",
                toneJson.value("autoDynamicRangeBroadHighlightGuardNeed", 0.0f),
                toneJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", 0.0f),
                toneJson.value("autoDynamicRangeShadowReadabilityLiftNeed", 0.0f),
                toneJson.value("autoDynamicRangeLocalHaloGuardNeed", 0.0f),
                toneJson.value("autoDynamicRangeNaturalContrastGuardNeed", 0.0f),
                toneJson.value("autoDynamicRangeSpecularHighlightToleranceNeed", 0.0f),
                toneJson.value("autoDynamicRangeShadowNoiseFloorNeed", 0.0f));
            if (toneJson.value("autoDynamicRangeStrategyMapVersion", std::string()) == "DynamicRangeStrategyMapV1") {
                ImGui::TextDisabled(
                    "Strategy map: highlight/shadow %+.2f  |  contrast/range %+.2f",
                    toneJson.value("autoDynamicRangeStrategyMapHighlightShadowAxis", 0.0f),
                    toneJson.value("autoDynamicRangeStrategyMapContrastRangeAxis", 0.0f));
            }
            if (toneJson.value("autoSubjectSceneIntentVersion", std::string()) == "SubjectSceneIntentV1") {
                ImGui::TextDisabled(
                    "Subject / scene: %s  |  %s  |  regions %d  |  strokes %d  |  conf %.2f  |  subject/scene %+.2f  |  mood/read %+.2f",
                    toneJson.value("autoSubjectSceneIntentLabel", std::string("Automatic Scene Balance")).c_str(),
                    toneJson.value("autoSubjectSceneUserGuidanceStatus", std::string("notAvailable")).c_str(),
                    toneJson.value("autoSubjectSceneImportanceRegionCount", 0),
                    toneJson.value("autoSubjectSceneImportanceStrokeCount", 0),
                    toneJson.value("autoSubjectSceneAutomaticConfidence", 0.0f),
                    toneJson.value("autoSubjectSceneSubjectSceneAxis", 0.0f),
                    toneJson.value("autoSubjectSceneMoodReadabilityAxis", 0.0f));
                if (toneJson.value("autoSubjectSceneImportanceMapVersion", std::string()) == "SubjectImportanceMapV1") {
                    ImGui::TextDisabled(
                        "Importance map: %s  |  cov %.2f  |  peak %.2f  |  low %.2f  |  center %.2f",
                        toneJson.value("autoSubjectSceneImportanceMapStatus", std::string("disabled")).c_str(),
                        toneJson.value("autoSubjectSceneImportanceMapCoverage", 0.0f),
                        toneJson.value("autoSubjectSceneImportanceMapPeak", 0.0f),
                        toneJson.value("autoSubjectSceneImportanceMapLowPriorityCoverage", 0.0f),
                        toneJson.value("autoSubjectSceneImportanceMapCenterBias", 0.0f));
                }
                if (toneJson.value("autoSubjectSceneRefinedMapVersion", std::string()) == "SubjectRefinedMapV1") {
                    ImGui::TextDisabled(
                        "Refined map: %s  |  cov %.2f  |  conf %.2f  |  read %.2f  |  prot %.2f  |  mood %.2f",
                        toneJson.value("autoSubjectSceneRefinedMapStatus", std::string("disabled")).c_str(),
                        toneJson.value("autoSubjectSceneRefinedMapCoverage", 0.0f),
                        toneJson.value("autoSubjectSceneRefinedMapConfidence", 0.0f),
                        toneJson.value("autoSubjectSceneRefinedMapReadabilityCoverage", 0.0f),
                        toneJson.value("autoSubjectSceneRefinedMapProtectionCoverage", 0.0f),
                        toneJson.value("autoSubjectSceneRefinedMapMoodCoverage", 0.0f));
                }
                const nlohmann::json subjectSolveNotes =
                    toneJson.value("autoSubjectSceneSolveNotes", nlohmann::json::array());
                if (subjectSolveNotes.is_array() && !subjectSolveNotes.empty()) {
                    int shownSubjectNotes = 0;
                    for (const nlohmann::json& note : subjectSolveNotes) {
                        if (!note.is_object()) {
                            continue;
                        }
                        const std::string text = note.value("text", std::string());
                        if (text.empty()) {
                            continue;
                        }
                        ImGui::TextWrapped("Subject note: %s", text.c_str());
                        ++shownSubjectNotes;
                        if (shownSubjectNotes >= 2) {
                            break;
                        }
                    }
                }
            }
            if (toneJson.value("autoDynamicRangeLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1") {
                ImGui::TextDisabled(
                    "Local exposure: %s  |  range %.2f  |  high %.2f  |  shadow %.2f  |  guard %.2f / %.2f  |  damage %.2f",
                    toneJson.value("autoDynamicRangeLocalExposureStrategyLabel", std::string("Balanced Local Prep")).c_str(),
                    toneJson.value("autoDynamicRangeLocalExposureRangeRedistribution", 0.0f),
                    toneJson.value("autoDynamicRangeLocalExposureHighlightCompression", 0.0f),
                    toneJson.value("autoDynamicRangeLocalExposureShadowOpening", 0.0f),
                    toneJson.value("autoDynamicRangeLocalExposureNoiseGuard", 0.0f),
                    toneJson.value("autoDynamicRangeLocalExposureHaloGuard", 0.0f),
                    toneJson.value("autoDynamicRangeLocalExposureDamageRisk", 0.0f));
            }
            const std::string strategyReason =
                toneJson.value("autoDynamicRangeStrategyReason", std::string());
            if (!strategyReason.empty()) {
                ImGui::TextWrapped("%s", strategyReason.c_str());
            }
            if (toneJson.value("autoDynamicRangeRegionEvidenceValid", false)) {
                ImGui::TextDisabled(
                    "Regional evidence: highlight %.2f  |  meaning %.2f  |  gray %.2f  |  shadow %.2f  |  local %.2f  |  EV %.2f / %.1f",
                    toneJson.value("autoDynamicRangeLocalHighlightHotspotRisk", 0.0f),
                    toneJson.value("autoDynamicRangeMeaningfulHighlightPressure", 0.0f),
                    toneJson.value("autoDynamicRangeHighlightGrayRisk", 0.0f),
                    toneJson.value("autoDynamicRangeLocalShadowHotspotRisk", 0.0f),
                    toneJson.value("autoDynamicRangeLocalRangeConflict", 0.0f),
                    toneJson.value("autoDynamicRangeLocalEvConflict", 0.0f),
                    toneJson.value("autoDynamicRangeLocalEvSpreadStops", 0.0f));
            }
        }
        ImGui::TextDisabled(
            "RAW placement: authored %+.2f EV  |  requested base %+.2f EV",
            toneJson.value("autoAuthoredRawExposureEv", settings.exposureStops),
            toneJson.value("autoExposureDiagnosticRecommendedBaseEv", 0.0f));
        if (toneJson.contains("middleGrey") || toneJson.contains("localBaselineStrength")) {
            ImGui::TextDisabled(
                "Tone placement: middle grey %.3f  |  prepared local %.2f",
                toneJson.value("middleGrey", 0.18f),
                toneJson.value("localBaselineStrength", 0.0f));
        }
    } else {
        ImGui::TextDisabled("Develop will analyze the connected RAW image on the next render.");
    }
    ImGui::TextDisabled("Switch to Manual when the automatic result is close and you want direct curve, targeting, or RAW cleanup edits.");

}

} // namespace Stack::Editor::DevelopAutoStatusControls
