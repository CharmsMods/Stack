#include "Raw/RawCaptureCompatibility.h"
#include "Raw/RawImageData.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Stack::Project {
namespace {
std::string Lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}
}
const char* MfdInputDomainName(MfdInputDomain value) {
    switch (value) {
        case MfdInputDomain::MosaicCfa: return "mosaic-cfa";
        case MfdInputDomain::LinearRgbUnsupported: return "linear-rgb-unsupported";
    }
    return "mosaic-cfa";
}

bool ParseMfdInputDomain(const std::string& value, MfdInputDomain& result) {
    if (value == "mosaic-cfa") {
        result = MfdInputDomain::MosaicCfa;
        return true;
    }
    if (value == "linear-rgb-unsupported") {
        result = MfdInputDomain::LinearRgbUnsupported;
        return true;
    }
    return false;
}

RawCaptureCompatibilitySummary BuildRawCaptureCompatibilitySummary(
    const Raw::RawMetadata& metadata) {
    RawCaptureCompatibilitySummary summary;
    summary.pixelLayout = Raw::RawPixelLayoutName(metadata.pixelLayout);
    summary.cfaPattern = Raw::CfaPatternName(metadata.cfaPattern);
    summary.cameraMake = metadata.cameraMake;
    summary.cameraModel = metadata.cameraModel;
    summary.uniqueCameraModel = metadata.dngUniqueCameraModel;
    summary.rawWidth = metadata.rawWidth;
    summary.rawHeight = metadata.rawHeight;
    summary.visibleWidth = metadata.visibleWidth;
    summary.visibleHeight = metadata.visibleHeight;
    summary.leftMargin = metadata.leftMargin;
    summary.topMargin = metadata.topMargin;
    summary.bitDepth = metadata.bitDepth;
    summary.orientation = metadata.orientation;
    summary.exposureTimeSeconds = metadata.exposureTimeSeconds;
    summary.isoSpeed = metadata.isoSpeed;
    summary.apertureFNumber = metadata.apertureFNumber;
    summary.focalLengthMm = metadata.focalLengthMm;
    summary.focusDistanceMeters = metadata.focusDistanceMeters;
    summary.lensModel = metadata.lensModel;
    summary.hasDngNoiseProfile = metadata.hasDngNoiseProfile;
    summary.dngNoiseProfilePlaneCount = static_cast<int>(metadata.dngNoiseProfile.size());
    summary.hasDngLinearResponseLimit = metadata.hasDngLinearResponseLimit;
    summary.dngLinearResponseLimit = metadata.dngLinearResponseLimit;
    summary.dngUnsupportedOpcodeCount = metadata.dngUnsupportedOpcodeCount;
    if (metadata.hasExposureTime && metadata.hasApertureFNumber && metadata.hasIsoSpeed) {
        summary.exposureConfidenceProvenance = "shutter-aperture-iso";
    } else if (metadata.hasExposureTime) {
        summary.exposureConfidenceProvenance = "shutter-only";
    } else {
        summary.exposureConfidenceProvenance = "image-fit-only";
    }
    summary.captureTimestamp = metadata.captureTimestamp;
    if (metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb) {
        summary.inputDomain = MfdInputDomain::LinearRgbUnsupported;
        summary.rejectionReason =
            "This image no longer contains a supported sensor mosaic. "
            "Linear RAW multi-frame denoise is planned for a future workflow.";
    } else if (metadata.pixelLayout != Raw::RawPixelLayout::MosaicBayer) {
        summary.rejectionReason = "The RAW pixel layout is unsupported for MFD.";
    } else if (metadata.cfaPattern == Raw::CfaPattern::Unknown) {
        summary.rejectionReason =
            "MFD currently requires a supported 2x2 Bayer CFA pattern.";
    } else if (metadata.rawWidth <= 0 || metadata.rawHeight <= 0 ||
               metadata.visibleWidth <= 0 || metadata.visibleHeight <= 0) {
        summary.rejectionReason = "The RAW sensor geometry is incomplete.";
    } else {
        summary.inputDomain = MfdInputDomain::MosaicCfa;
        summary.supported = true;
    }
    return summary;
}

json SerializeRawCaptureCompatibilitySummary(
    const RawCaptureCompatibilitySummary& summary) {
    return {
        { "schemaVersion", 2 },
        { "inputDomain", MfdInputDomainName(summary.inputDomain) },
        { "pixelLayout", summary.pixelLayout },
        { "cfaPattern", summary.cfaPattern },
        { "cameraMake", summary.cameraMake },
        { "cameraModel", summary.cameraModel },
        { "uniqueCameraModel", summary.uniqueCameraModel },
        { "rawWidth", summary.rawWidth },
        { "rawHeight", summary.rawHeight },
        { "visibleWidth", summary.visibleWidth },
        { "visibleHeight", summary.visibleHeight },
        { "leftMargin", summary.leftMargin },
        { "topMargin", summary.topMargin },
        { "bitDepth", summary.bitDepth },
        { "orientation", summary.orientation },
        { "exposureTimeSeconds", summary.exposureTimeSeconds },
        { "isoSpeed", summary.isoSpeed },
        { "apertureFNumber", summary.apertureFNumber },
        { "focalLengthMm", summary.focalLengthMm },
        { "focusDistanceMeters", summary.focusDistanceMeters },
        { "lensModel", summary.lensModel },
        { "hasDngNoiseProfile", summary.hasDngNoiseProfile },
        { "dngNoiseProfilePlaneCount", summary.dngNoiseProfilePlaneCount },
        { "hasDngLinearResponseLimit", summary.hasDngLinearResponseLimit },
        { "dngLinearResponseLimit", summary.dngLinearResponseLimit },
        { "dngUnsupportedOpcodeCount", summary.dngUnsupportedOpcodeCount },
        { "exposureConfidenceProvenance", summary.exposureConfidenceProvenance },
        { "captureTimestamp", summary.captureTimestamp },
        { "supported", summary.supported },
        { "rejectionReason", summary.rejectionReason }
    };
}

bool DeserializeRawCaptureCompatibilitySummary(
    const json& value,
    RawCaptureCompatibilitySummary& summary,
    std::string* errorMessage) {
    if (!value.is_object()) {
        if (errorMessage) *errorMessage = "RAW capture compatibility summary is not an object.";
        return false;
    }
    RawCaptureCompatibilitySummary decoded;
    if (!ParseMfdInputDomain(
            value.value("inputDomain", std::string("mosaic-cfa")),
            decoded.inputDomain)) {
        if (errorMessage) *errorMessage = "RAW capture input domain is invalid.";
        return false;
    }
    decoded.pixelLayout = value.value("pixelLayout", std::string());
    decoded.cfaPattern = value.value("cfaPattern", std::string());
    decoded.cameraMake = value.value("cameraMake", std::string());
    decoded.cameraModel = value.value("cameraModel", std::string());
    decoded.uniqueCameraModel = value.value("uniqueCameraModel", std::string());
    decoded.rawWidth = value.value("rawWidth", 0);
    decoded.rawHeight = value.value("rawHeight", 0);
    decoded.visibleWidth = value.value("visibleWidth", 0);
    decoded.visibleHeight = value.value("visibleHeight", 0);
    decoded.leftMargin = value.value("leftMargin", 0);
    decoded.topMargin = value.value("topMargin", 0);
    decoded.bitDepth = value.value("bitDepth", 0);
    decoded.orientation = value.value("orientation", 0);
    decoded.exposureTimeSeconds = value.value("exposureTimeSeconds", 0.0);
    decoded.isoSpeed = value.value("isoSpeed", 0.0);
    decoded.apertureFNumber = value.value("apertureFNumber", 0.0);
    decoded.focalLengthMm = value.value("focalLengthMm", 0.0);
    decoded.focusDistanceMeters = value.value("focusDistanceMeters", 0.0);
    decoded.lensModel = value.value("lensModel", std::string());
    decoded.hasDngNoiseProfile = value.value("hasDngNoiseProfile", false);
    decoded.dngNoiseProfilePlaneCount = value.value("dngNoiseProfilePlaneCount", 0);
    decoded.hasDngLinearResponseLimit = value.value("hasDngLinearResponseLimit", false);
    decoded.dngLinearResponseLimit = value.value("dngLinearResponseLimit", 1.0);
    decoded.dngUnsupportedOpcodeCount = value.value("dngUnsupportedOpcodeCount", 0);
    decoded.exposureConfidenceProvenance = value.value(
        "exposureConfidenceProvenance", std::string());
    decoded.captureTimestamp = value.value("captureTimestamp", std::int64_t { 0 });
    decoded.supported = value.value("supported", false);
    decoded.rejectionReason = value.value("rejectionReason", std::string());
    summary = std::move(decoded);
    if (errorMessage) errorMessage->clear();
    return true;
}

bool AreMfdCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason) {
    const auto fail = [&](const std::string& message) {
        if (reason) *reason = message;
        return false;
    };
    if (!reference.supported || !candidate.supported) {
        return fail("Every MFD frame must be a supported mosaiced RAW capture.");
    }
    if (reference.inputDomain != MfdInputDomain::MosaicCfa ||
        candidate.inputDomain != MfdInputDomain::MosaicCfa) {
        return fail("MFD currently accepts still-mosaiced CFA RAW captures only.");
    }
    const std::string referenceCamera = !reference.uniqueCameraModel.empty()
        ? reference.uniqueCameraModel
        : Lower(reference.cameraMake + "\n" + reference.cameraModel);
    const std::string candidateCamera = !candidate.uniqueCameraModel.empty()
        ? candidate.uniqueCameraModel
        : Lower(candidate.cameraMake + "\n" + candidate.cameraModel);
    if (Lower(referenceCamera) != Lower(candidateCamera)) {
        return fail("The selected frames were captured by different camera models.");
    }
    if (reference.cfaPattern != candidate.cfaPattern) {
        return fail("The selected frames use different CFA patterns.");
    }
    if (reference.bitDepth != candidate.bitDepth) {
        return fail("The selected frames use different RAW sample bit depths.");
    }
    if (reference.orientation != candidate.orientation) {
        return fail("The selected frames use different sensor orientation metadata.");
    }
    if (reference.rawWidth != candidate.rawWidth ||
        reference.rawHeight != candidate.rawHeight ||
        reference.visibleWidth != candidate.visibleWidth ||
        reference.visibleHeight != candidate.visibleHeight ||
        reference.leftMargin != candidate.leftMargin ||
        reference.topMargin != candidate.topMargin) {
        return fail("The selected frames use different sensor or active-image geometry.");
    }
    if (reason) reason->clear();
    return true;
}

bool AreHdrCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason,
    std::vector<std::string>* warnings) {
    if (!AreMfdCapturesStructurallyCompatible(reference, candidate, reason)) return false;
    const auto incompatible = [&](const std::string& message) {
        if (reason) *reason = message;
        return false;
    };
    if (reference.focalLengthMm > 0.0 && candidate.focalLengthMm > 0.0 &&
        std::abs(reference.focalLengthMm - candidate.focalLengthMm) > 0.1) {
        return incompatible("HDR brackets must use the same focal length.");
    }
    if (!reference.lensModel.empty() && !candidate.lensModel.empty() &&
        Lower(reference.lensModel) != Lower(candidate.lensModel)) {
        return incompatible("HDR brackets must use the same lens.");
    }
    if (warnings) {
        if (reference.apertureFNumber <= 0.0 || candidate.apertureFNumber <= 0.0)
            warnings->push_back("Aperture metadata is missing; exposure matching may need per-frame offsets.");
        if (reference.focalLengthMm <= 0.0 || candidate.focalLengthMm <= 0.0)
            warnings->push_back("Focal-length metadata is missing; lens geometry could not be fully verified.");
        if (reference.focusDistanceMeters <= 0.0 || candidate.focusDistanceMeters <= 0.0) {
            warnings->push_back("Focus-distance metadata is missing; focus compatibility could not be verified.");
        } else {
            const double scale = std::max(
                reference.focusDistanceMeters,
                candidate.focusDistanceMeters);
            if (std::abs(reference.focusDistanceMeters - candidate.focusDistanceMeters) >
                std::max(0.01, scale * 0.02)) {
                warnings->push_back(
                    "Focus distances differ. Bracketing will preserve the original sample "
                    "coordinates without correcting focus breathing or sharpness changes.");
            }
        }
    }
    if (reason) reason->clear();
    return true;
}

}
