#pragma once

#include "ThirdParty/json.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace Raw { struct RawMetadata; }

// Historical names are retained for source compatibility. This contract owns
// sensor compatibility only and has no dependency on a saved project or editor.
namespace Stack::Project {
using json = nlohmann::json;

enum class MfdInputDomain {
    MosaicCfa,
    LinearRgbUnsupported
};

struct RawCaptureCompatibilitySummary {
    MfdInputDomain inputDomain = MfdInputDomain::MosaicCfa;
    std::string pixelLayout;
    std::string cfaPattern;
    std::string cameraMake;
    std::string cameraModel;
    std::string uniqueCameraModel;
    int rawWidth = 0;
    int rawHeight = 0;
    int visibleWidth = 0;
    int visibleHeight = 0;
    int leftMargin = 0;
    int topMargin = 0;
    int bitDepth = 0;
    int orientation = 0;
    double exposureTimeSeconds = 0.0;
    double isoSpeed = 0.0;
    double apertureFNumber = 0.0;
    double focalLengthMm = 0.0;
    double focusDistanceMeters = 0.0;
    std::string lensModel;
    bool hasDngNoiseProfile = false;
    int dngNoiseProfilePlaneCount = 0;
    bool hasDngLinearResponseLimit = false;
    double dngLinearResponseLimit = 1.0;
    int dngUnsupportedOpcodeCount = 0;
    std::string exposureConfidenceProvenance;
    std::int64_t captureTimestamp = 0;
    bool supported = false;
    std::string rejectionReason;
};

const char* MfdInputDomainName(MfdInputDomain value);
bool ParseMfdInputDomain(const std::string& value, MfdInputDomain& result);

RawCaptureCompatibilitySummary BuildRawCaptureCompatibilitySummary(
    const Raw::RawMetadata& metadata);
json SerializeRawCaptureCompatibilitySummary(
    const RawCaptureCompatibilitySummary& summary);
bool DeserializeRawCaptureCompatibilitySummary(
    const json& value,
    RawCaptureCompatibilitySummary& summary,
    std::string* errorMessage = nullptr);
bool AreMfdCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason = nullptr);
bool AreHdrCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason = nullptr,
    std::vector<std::string>* warnings = nullptr);
} // namespace Stack::Project
