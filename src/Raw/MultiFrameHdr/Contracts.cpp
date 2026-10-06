#include "Raw/MultiFrameHdr/Contracts.h"

#include <algorithm>
#include <cmath>

namespace Raw::Hdr {

const char* AlignmentModeName(AlignmentMode mode) {
    switch (mode) {
        case AlignmentMode::AutoTranslation: return "auto-translation";
        case AlignmentMode::Identity: return "identity";
        case AlignmentMode::VerifyOnly: return "verify-only";
    }
    return "auto-translation";
}

bool ParseAlignmentMode(const std::string& value, AlignmentMode& mode) {
    if (value == "verify-only") { mode = AlignmentMode::VerifyOnly; return true; }
    if (value == "auto-translation" || value == "translation") {
        mode = AlignmentMode::AutoTranslation;
        return true;
    }
    if (value == "identity" || value == "none") {
        mode = AlignmentMode::Identity;
        return true;
    }
    return false;
}

bool ValidateParameters(const Parameters& p, std::string* error) {
    const auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (p.tileRawPixels < 64u || p.tileRawPixels > 2048u)
        return fail("HDR tile size must be between 64 and 2048 RAW pixels.");
    if (!std::isfinite(p.minimumExposureSpanEv) || p.minimumExposureSpanEv < 0.0 ||
        p.minimumExposureSpanEv > 8.0)
        return fail("HDR minimum exposure span is invalid.");
    if (!std::isfinite(p.mixedIsoWarningEv) || !std::isfinite(p.mixedIsoStrongUncertaintyEv) ||
        p.mixedIsoWarningEv < 0.0 || p.mixedIsoStrongUncertaintyEv < p.mixedIsoWarningEv)
        return fail("HDR exposure-confidence thresholds are invalid.");
    if (!std::isfinite(p.unverifiedContributionCap) || p.unverifiedContributionCap <= 0.0 ||
        p.unverifiedContributionCap > 0.5)
        return fail("HDR unverified contribution cap must be in (0, 0.5].");
    if (!std::isfinite(p.saturationHeadroomSigma) || p.saturationHeadroomSigma < 0.0 ||
        !std::isfinite(p.robustCutoffSigma) || p.robustCutoffSigma < 1.0 ||
        !std::isfinite(p.disagreementFallbackSigma) ||
        p.disagreementFallbackSigma < p.robustCutoffSigma)
        return fail("HDR robust fusion thresholds are invalid.");
    if (!std::isfinite(p.numericalVarianceFloor) || p.numericalVarianceFloor <= 0.0)
        return fail("HDR numerical variance floor must be positive.");
    if (p.minimumExposureFitSamples < 16u)
        return fail("HDR exposure fitting requires at least 16 samples.");
    if (p.highlightNeighborhoodRadiusCfa < 1u ||
        p.highlightNeighborhoodRadiusCfa > 4u)
        return fail("HDR highlight neighborhood radius must be between 1 and 4 CFA samples.");
    const std::uint32_t neighborhoodDiameter =
        p.highlightNeighborhoodRadiusCfa * 2u + 1u;
    const std::uint32_t availableNeighbors =
        neighborhoodDiameter * neighborhoodDiameter - 1u;
    if (p.highlightHoleMinimumClippedNeighbors < 1u ||
        p.highlightHoleMinimumClippedNeighbors > availableNeighbors)
        return fail("HDR highlight-hole support count is invalid.");
    if (!std::isfinite(p.highlightHoleNormalizedCeiling) ||
        p.highlightHoleNormalizedCeiling < 0.25 ||
        p.highlightHoleNormalizedCeiling > 0.98)
        return fail("HDR highlight-hole normalized ceiling is invalid.");
    if (p.colorCoherenceRadiusCfa < 1u || p.colorCoherenceRadiusCfa > 3u)
        return fail("HDR color-coherence radius must be between 1 and 3 CFA samples.");
    if (p.colorCoherenceMinimumSupportingChannels < 1u ||
        p.colorCoherenceMinimumSupportingChannels > 3u)
        return fail("HDR color-coherence support must use between 1 and 3 other channels.");
    if (!std::isfinite(p.colorCoherenceDarkRatio) ||
        p.colorCoherenceDarkRatio <= 0.0 || p.colorCoherenceDarkRatio >= 0.75 ||
        !std::isfinite(p.colorCoherenceNeighborRatio) ||
        p.colorCoherenceNeighborRatio <= p.colorCoherenceDarkRatio ||
        p.colorCoherenceNeighborRatio > 1.5)
        return fail("HDR color-coherence ratios are invalid.");
    if (!std::isfinite(p.colorCoherenceMaximumRepairErrorEv) ||
        p.colorCoherenceMaximumRepairErrorEv <= 0.0 ||
        p.colorCoherenceMaximumRepairErrorEv > 2.0 ||
        !std::isfinite(p.colorCoherenceMinimumImprovementEv) ||
        p.colorCoherenceMinimumImprovementEv <= 0.0 ||
        p.colorCoherenceMinimumImprovementEv > 3.0)
        return fail("HDR color-coherence repair thresholds are invalid.");
    if (error) error->clear();
    return true;
}

nlohmann::json SerializeParameters(const Parameters& p) {
    return {
        { "alignmentMode", AlignmentModeName(p.alignmentMode) },
        { "tileRawPixels", p.tileRawPixels },
        { "minimumExposureSpanEv", p.minimumExposureSpanEv },
        { "mixedIsoWarningEv", p.mixedIsoWarningEv },
        { "mixedIsoStrongUncertaintyEv", p.mixedIsoStrongUncertaintyEv },
        { "unverifiedContributionCap", p.unverifiedContributionCap },
        { "saturationHeadroomSigma", p.saturationHeadroomSigma },
        { "robustCutoffSigma", p.robustCutoffSigma },
        { "disagreementFallbackSigma", p.disagreementFallbackSigma },
        { "numericalVarianceFloor", p.numericalVarianceFloor },
        { "minimumExposureFitSamples", p.minimumExposureFitSamples },
        { "highlightNeighborhoodRadiusCfa", p.highlightNeighborhoodRadiusCfa },
        { "highlightHoleMinimumClippedNeighbors",
            p.highlightHoleMinimumClippedNeighbors },
        { "highlightHoleNormalizedCeiling", p.highlightHoleNormalizedCeiling },
        { "colorCoherenceRadiusCfa", p.colorCoherenceRadiusCfa },
        { "colorCoherenceMinimumSupportingChannels",
            p.colorCoherenceMinimumSupportingChannels },
        { "colorCoherenceDarkRatio", p.colorCoherenceDarkRatio },
        { "colorCoherenceNeighborRatio", p.colorCoherenceNeighborRatio },
        { "colorCoherenceMaximumRepairErrorEv",
            p.colorCoherenceMaximumRepairErrorEv },
        { "colorCoherenceMinimumImprovementEv",
            p.colorCoherenceMinimumImprovementEv }
    };
}

bool DeserializeParameters(
    const nlohmann::json& value,
    Parameters& parameters,
    std::string* error) {
    if (!value.is_object()) {
        if (error) *error = "HDR parameters are not an object.";
        return false;
    }
    Parameters decoded;
    if (!ParseAlignmentMode(value.value("alignmentMode", std::string("auto-translation")),
            decoded.alignmentMode)) {
        if (error) *error = "HDR alignment mode is invalid.";
        return false;
    }
    decoded.tileRawPixels = value.value("tileRawPixels", decoded.tileRawPixels);
    decoded.minimumExposureSpanEv = value.value(
        "minimumExposureSpanEv", decoded.minimumExposureSpanEv);
    decoded.mixedIsoWarningEv = value.value("mixedIsoWarningEv", decoded.mixedIsoWarningEv);
    decoded.mixedIsoStrongUncertaintyEv = value.value(
        "mixedIsoStrongUncertaintyEv", decoded.mixedIsoStrongUncertaintyEv);
    decoded.unverifiedContributionCap = value.value(
        "unverifiedContributionCap", decoded.unverifiedContributionCap);
    decoded.saturationHeadroomSigma = value.value(
        "saturationHeadroomSigma", decoded.saturationHeadroomSigma);
    decoded.robustCutoffSigma = value.value("robustCutoffSigma", decoded.robustCutoffSigma);
    decoded.disagreementFallbackSigma = value.value(
        "disagreementFallbackSigma", decoded.disagreementFallbackSigma);
    decoded.numericalVarianceFloor = value.value(
        "numericalVarianceFloor", decoded.numericalVarianceFloor);
    decoded.minimumExposureFitSamples = value.value(
        "minimumExposureFitSamples", decoded.minimumExposureFitSamples);
    decoded.highlightNeighborhoodRadiusCfa = value.value(
        "highlightNeighborhoodRadiusCfa", decoded.highlightNeighborhoodRadiusCfa);
    decoded.highlightHoleMinimumClippedNeighbors = value.value(
        "highlightHoleMinimumClippedNeighbors",
        decoded.highlightHoleMinimumClippedNeighbors);
    decoded.highlightHoleNormalizedCeiling = value.value(
        "highlightHoleNormalizedCeiling", decoded.highlightHoleNormalizedCeiling);
    decoded.colorCoherenceRadiusCfa = value.value(
        "colorCoherenceRadiusCfa", decoded.colorCoherenceRadiusCfa);
    decoded.colorCoherenceMinimumSupportingChannels = value.value(
        "colorCoherenceMinimumSupportingChannels",
        decoded.colorCoherenceMinimumSupportingChannels);
    decoded.colorCoherenceDarkRatio = value.value(
        "colorCoherenceDarkRatio", decoded.colorCoherenceDarkRatio);
    decoded.colorCoherenceNeighborRatio = value.value(
        "colorCoherenceNeighborRatio", decoded.colorCoherenceNeighborRatio);
    decoded.colorCoherenceMaximumRepairErrorEv = value.value(
        "colorCoherenceMaximumRepairErrorEv",
        decoded.colorCoherenceMaximumRepairErrorEv);
    decoded.colorCoherenceMinimumImprovementEv = value.value(
        "colorCoherenceMinimumImprovementEv",
        decoded.colorCoherenceMinimumImprovementEv);
    if (!ValidateParameters(decoded, error)) return false;
    parameters = decoded;
    return true;
}

} // namespace Raw::Hdr
