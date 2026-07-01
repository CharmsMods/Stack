#include "App/Validation/Suites/DevelopAutoSolveValidationCandidateProbes.h"

#include "App/Validation/Suites/DevelopAutoSolveValidationHelpers.h"

#include <cmath>

namespace Stack::Validation::Detail {

DevelopAutoSolveCandidateProbeSummary BuildDevelopAutoSolveCandidateProbeSummary(
    const nlohmann::json& candidateSolves,
    EditorNodeGraph::DevelopAutoGuidance fallbackGuidance) {
    DevelopAutoSolveCandidateProbeSummary summary;
    summary.cleanShadowCandidateGuidance = fallbackGuidance;
    summary.preserveTextureCandidateGuidance = fallbackGuidance;
    summary.highlightProtectedMidsGuidance = fallbackGuidance;
    summary.finishToneProbeGuidance = fallbackGuidance;
    summary.naturalContrastGuardGuidance = fallbackGuidance;
    summary.brightHighlightRolloffGuidance = fallbackGuidance;
    summary.luminousHighlightAnchorGuidance = fallbackGuidance;
    summary.specularHighlightToleranceGuidance = fallbackGuidance;
    summary.whiteBalanceProbeGuidance = fallbackGuidance;

    if (!candidateSolves.is_array()) {
        return summary;
    }

    for (const nlohmann::json& candidate : candidateSolves) {
        if (!candidate.is_object()) {
            continue;
        }
        const std::string id = candidate.value("id", std::string());
        const std::string status = candidate.value("status", std::string());
        const bool eligible = status == "selected" || status == "survivor";
        if (id.rfind("modeNeighbor", 0) == 0) {
            summary.modeNeighborCandidateGenerated = true;
            summary.modeNeighborCandidateEligible = summary.modeNeighborCandidateEligible || eligible;
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            summary.modeNeighborCandidateHumanReadable =
                summary.modeNeighborCandidateHumanReadable ||
                (!label.empty() &&
                 label.find("modeNeighbor") == std::string::npos &&
                 reason.find("neighboring") != std::string::npos);
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            if (changes.is_object()) {
                const float totalDelta =
                    std::fabs(changes.value("brightnessIntentDelta", 0.0f)) +
                    std::fabs(changes.value("dynamicRangeDelta", 0.0f)) +
                    std::fabs(changes.value("shadowLiftDelta", 0.0f)) +
                    std::fabs(changes.value("highlightGuardDelta", 0.0f)) +
                    std::fabs(changes.value("contrastBiasDelta", 0.0f));
                summary.modeNeighborCandidateMeaningfullyDifferent =
                    summary.modeNeighborCandidateMeaningfullyDifferent || totalDelta > 0.30f;
            }
        }
        if (IsDevelopAutoSolveFinishToneProbeId(id)) {
            summary.finishToneProbeGenerated = true;
            summary.finishToneProbeEligible = summary.finishToneProbeEligible || eligible;
            if (summary.finishToneProbeId.empty() || eligible) {
                summary.finishToneProbeId = id;
                summary.finishToneProbeGuidance =
                    DevelopAutoSolveGuidanceFromCandidateJson(
                        candidate.value("guidance", nlohmann::json::object()),
                        fallbackGuidance);
            }
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            summary.finishToneProbeHumanReadable =
                summary.finishToneProbeHumanReadable ||
                (!label.empty() &&
                 label.find("tone") == std::string::npos &&
                 label.find("Tone") != std::string::npos &&
                 reason.find("finish-tone") != std::string::npos);
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            if (changes.is_object()) {
                const float toneDelta =
                    std::fabs(changes.value("highlightCharacterDelta", 0.0f)) +
                    std::fabs(changes.value("contrastBiasDelta", 0.0f));
                const float upstreamDelta =
                    std::fabs(changes.value("brightnessIntentDelta", 0.0f)) +
                    std::fabs(changes.value("shadowLiftDelta", 0.0f));
                summary.finishToneProbeMeaningfullyDifferent =
                    summary.finishToneProbeMeaningfullyDifferent ||
                    (toneDelta > 0.26f && upstreamDelta < 0.24f);
            }
            if (id == "brightHighlightRolloff") {
                summary.brightHighlightRolloffGenerated = true;
                summary.brightHighlightRolloffEligible =
                    summary.brightHighlightRolloffEligible || eligible;
                summary.brightHighlightRolloffGuidance =
                    DevelopAutoSolveGuidanceFromCandidateJson(
                        candidate.value("guidance", nlohmann::json::object()),
                        fallbackGuidance);
                const nlohmann::json scoreComponents =
                    candidate.value("scoreComponents", nlohmann::json::object());
                const nlohmann::json dimensions =
                    scoreComponents.value("dimensions", nlohmann::json::object());
                const nlohmann::json candidateChanges =
                    candidate.value("changes", nlohmann::json::object());
                summary.brightHighlightRolloffHumanReadable =
                    summary.brightHighlightRolloffHumanReadable ||
                    (label.find("Bright") != std::string::npos &&
                     label.find("Highlight") != std::string::npos &&
                     reason.find("bright light") != std::string::npos &&
                     reason.find("does not recover") != std::string::npos);
                summary.brightHighlightRolloffDiagnosticsWritten =
                    summary.brightHighlightRolloffDiagnosticsWritten ||
                    (dimensions.value("brightnessHierarchy", -1.0f) > 0.50f &&
                     candidateChanges.value("highlightCharacterDelta", 0.0f) > 0.18f &&
                     std::fabs(candidateChanges.value("brightnessIntentDelta", 0.0f)) < 0.08f);
            }
            if (id == "luminousHighlightAnchor") {
                summary.luminousHighlightAnchorGenerated = true;
                summary.luminousHighlightAnchorEligible =
                    summary.luminousHighlightAnchorEligible || eligible;
                summary.luminousHighlightAnchorGuidance =
                    DevelopAutoSolveGuidanceFromCandidateJson(
                        candidate.value("guidance", nlohmann::json::object()),
                        fallbackGuidance);
                const nlohmann::json scoreComponents =
                    candidate.value("scoreComponents", nlohmann::json::object());
                const nlohmann::json dimensions =
                    scoreComponents.value("dimensions", nlohmann::json::object());
                const nlohmann::json signals =
                    scoreComponents.value("signals", nlohmann::json::object());
                const nlohmann::json candidateChanges =
                    candidate.value("changes", nlohmann::json::object());
                summary.luminousHighlightAnchorHumanReadable =
                    summary.luminousHighlightAnchorHumanReadable ||
                    (label.find("Luminous") != std::string::npos &&
                     label.find("Highlight") != std::string::npos &&
                     reason.find("stay luminous") != std::string::npos &&
                     reason.find("does not recover clipped detail") != std::string::npos);
                summary.luminousHighlightAnchorDiagnosticsWritten =
                    summary.luminousHighlightAnchorDiagnosticsWritten ||
                    (dimensions.value("luminousHighlightAnchor", -1.0f) > 0.58f &&
                     dimensions.value("brightnessHierarchy", -1.0f) > 0.54f &&
                     dimensions.value("contrastShape", -1.0f) > 0.54f &&
                     signals.value("highlightBrightnessSignal", -1.0f) >= 0.0f &&
                     candidateChanges.value("highlightCharacterDelta", 0.0f) > 0.22f &&
                     candidateChanges.value("contrastBiasDelta", 0.0f) > 0.10f &&
                     candidateChanges.value("dynamicRangeDelta", 0.0f) < -0.04f &&
                     std::fabs(candidateChanges.value("brightnessIntentDelta", 0.0f)) < 0.04f);
            }
            if (id == "naturalContrastGuard") {
                summary.naturalContrastGuardGenerated = true;
                summary.naturalContrastGuardEligible =
                    summary.naturalContrastGuardEligible || eligible;
                summary.naturalContrastGuardGuidance =
                    DevelopAutoSolveGuidanceFromCandidateJson(
                        candidate.value("guidance", nlohmann::json::object()),
                        fallbackGuidance);
                const nlohmann::json scoreComponents =
                    candidate.value("scoreComponents", nlohmann::json::object());
                const nlohmann::json dimensions =
                    scoreComponents.value("dimensions", nlohmann::json::object());
                const nlohmann::json candidateChanges =
                    candidate.value("changes", nlohmann::json::object());
                summary.naturalContrastGuardHumanReadable =
                    summary.naturalContrastGuardHumanReadable ||
                    (label.find("Natural") != std::string::npos &&
                     label.find("Contrast") != std::string::npos &&
                     reason.find("believable separation") != std::string::npos &&
                     reason.find("lighting hierarchy") != std::string::npos);
                summary.naturalContrastGuardDiagnosticsWritten =
                    summary.naturalContrastGuardDiagnosticsWritten ||
                    (dimensions.value("naturalContrastGuard", -1.0f) > 0.54f &&
                     dimensions.value("brightnessHierarchy", -1.0f) > 0.54f &&
                     dimensions.value("contrastShape", -1.0f) > 0.54f &&
                     candidateChanges.value("contrastBiasDelta", 0.0f) > 0.18f &&
                     candidateChanges.value("dynamicRangeDelta", 0.0f) < -0.06f &&
                     std::fabs(candidateChanges.value("brightnessIntentDelta", 0.0f)) < 0.04f);
            }
            if (id == "specularHighlightTolerance") {
                summary.specularHighlightToleranceGenerated = true;
                summary.specularHighlightToleranceEligible =
                    summary.specularHighlightToleranceEligible || eligible;
                summary.specularHighlightToleranceGuidance =
                    DevelopAutoSolveGuidanceFromCandidateJson(
                        candidate.value("guidance", nlohmann::json::object()),
                        fallbackGuidance);
                const nlohmann::json scoreComponents =
                    candidate.value("scoreComponents", nlohmann::json::object());
                const nlohmann::json dimensions =
                    scoreComponents.value("dimensions", nlohmann::json::object());
                const nlohmann::json candidateChanges =
                    candidate.value("changes", nlohmann::json::object());
                summary.specularHighlightToleranceHumanReadable =
                    summary.specularHighlightToleranceHumanReadable ||
                    (label.find("Specular") != std::string::npos &&
                     label.find("Highlight") != std::string::npos &&
                     reason.find("tiny specular") != std::string::npos &&
                     reason.find("not clipped-data recovery") != std::string::npos);
                summary.specularHighlightToleranceDiagnosticsWritten =
                    summary.specularHighlightToleranceDiagnosticsWritten ||
                    (dimensions.value("specularTolerance", -1.0f) > 0.54f &&
                     dimensions.value("brightnessHierarchy", -1.0f) > 0.50f &&
                     candidateChanges.value("highlightCharacterDelta", 0.0f) > 0.24f &&
                     candidateChanges.value("highlightGuardDelta", 0.0f) < -0.10f);
            }
        }
        if (IsDevelopAutoSolveWhiteBalanceProbeId(id)) {
            summary.whiteBalanceProbeGenerated = true;
            summary.whiteBalanceProbeEligible = summary.whiteBalanceProbeEligible || eligible;
            if (summary.whiteBalanceProbeId.empty() || eligible) {
                summary.whiteBalanceProbeId = id;
                summary.whiteBalanceProbeGuidance =
                    DevelopAutoSolveGuidanceFromCandidateJson(
                        candidate.value("guidance", nlohmann::json::object()),
                        fallbackGuidance);
            }
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            summary.whiteBalanceProbeHumanReadable =
                summary.whiteBalanceProbeHumanReadable ||
                (!label.empty() &&
                 (label.find("WB") != std::string::npos || label.find("White") != std::string::npos) &&
                 reason.find("white balance") != std::string::npos);
            const nlohmann::json rawOverrides =
                candidate.value("rawOverrides", nlohmann::json::object());
            const nlohmann::json candidateChanges =
                candidate.value("changes", nlohmann::json::object());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            if (rawOverrides.is_object() &&
                candidateChanges.is_object() &&
                dimensions.is_object()) {
                const std::string mode =
                    rawOverrides.value("whiteBalanceMode", std::string());
                summary.whiteBalanceProbeMode = summary.whiteBalanceProbeMode.empty()
                    ? mode
                    : summary.whiteBalanceProbeMode;
                summary.whiteBalanceProbeDiagnosticsWritten =
                    summary.whiteBalanceProbeDiagnosticsWritten ||
                    (!mode.empty() &&
                     candidateChanges.value("whiteBalanceMode", std::string()) == mode &&
                     dimensions.value("colorPlausibility", -1.0f) >= 0.0f &&
                     dimensions.value("moodColorPreservation", -1.0f) >= 0.0f);
            }
        }
        if (id == "highlightProtectedMids") {
            summary.highlightProtectedMidsGenerated = true;
            summary.highlightProtectedMidsEligible =
                summary.highlightProtectedMidsEligible || eligible;
            summary.highlightProtectedMidsGuidance =
                DevelopAutoSolveGuidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    fallbackGuidance);
            const nlohmann::json candidateChanges =
                candidate.value("changes", nlohmann::json::object());
            if (candidateChanges.is_object()) {
                const bool lowersBrightness =
                    candidateChanges.value("brightnessIntentDelta", 0.0f) < -0.10f;
                const bool liftsLocalRange =
                    candidateChanges.value("dynamicRangeDelta", 0.0f) > 0.20f &&
                    candidateChanges.value("shadowLiftDelta", 0.0f) > 0.10f &&
                    candidateChanges.value("highlightGuardDelta", 0.0f) > 0.20f;
                summary.highlightProtectedMidsMeaningfullyDifferent =
                    summary.highlightProtectedMidsMeaningfullyDifferent ||
                    (lowersBrightness && liftsLocalRange);
            }
        }
        if (!eligible) {
            continue;
        }
        if (id == "cleanShadows") {
            summary.cleanShadowCandidateGuidance =
                DevelopAutoSolveGuidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    fallbackGuidance);
            summary.cleanShadowCandidateGenerated = true;
        } else if (id == "preserveTexture") {
            summary.preserveTextureCandidateGuidance =
                DevelopAutoSolveGuidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    fallbackGuidance);
            summary.preserveTextureCandidateGenerated = true;
        }
    }

    return summary;
}

} // namespace Stack::Validation::Detail
