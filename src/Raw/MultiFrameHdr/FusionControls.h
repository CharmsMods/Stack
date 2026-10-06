#pragma once

#include "ThirdParty/json.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::Hdr {

inline constexpr std::array<float, 7> kFusionCurveEv { -12, -8, -4, -2, 0, 2, 6 };
inline constexpr std::size_t kFusionMaxSources = 20;

struct FusionSourcePreference {
    std::string sourceId;
    bool enabled = true;
    float biasStops = 0;
};

struct FusionLocalMask {
    std::string name = "Local exposure";
    bool enabled = true;
    float centerX = 0.5f, centerY = 0.5f;
    float radiusX = 0.2f, radiusY = 0.2f;
    float feather = 0.5f;
    float targetEv = 0, preferredEv = 0;
};

struct FusionControls {
    float targetEv = 0;
    float preferredEv = 0;
    float targetFollow = 0;
    float blendWidthEv = 2;
    bool preferSources = false;
    std::array<float, 7> targetCurve {};
    std::array<float, 7> preferenceCurve {};
    std::vector<FusionSourcePreference> sources;
    std::vector<FusionLocalMask> masks;
};

bool ValidateFusionControls(const FusionControls&, std::string* error = nullptr);
bool DeserializeFusionControls(const nlohmann::json&, FusionControls&, std::string* error = nullptr);
nlohmann::json SerializeFusionControls(const FusionControls&);
bool IsNeutralFusion(const FusionControls&);
float EvaluateFusionCurve(const std::array<float, 7>&, float analysisEv);
float FusionMaskCoverage(const FusionLocalMask&, float x, float y);

struct FusionControlField {
    float targetEv = 0;
    float preferredEv = 0;
};
FusionControlField EvaluateFusionField(const FusionControls&, float analysisEv, float x, float y);

// The automatic estimator freezes physical gates before manual preference.
// Zero automatic weight remains zero regardless of the user's bias.
struct FusionObservation {
    float scene = 0;
    float variance = 0;
    float automaticWeight = 0;
    float headroom = 0;
    float effectiveSamples = 1;
    float motionConfidence = 0;
    bool anchorFallback = false;
};

struct FusionSourceInfo {
    std::string sourceId;
    float captureEv = 0;
    float alignmentConfidence = 1;
};

struct FusionPixel {
    float scene = 0;
    float variance = 0;
    float effectiveSamples = 0;
    float targetEv = 0;
    float preferredEv = 0;
    bool fallback = false;
    bool noValidMeasurement = false;
    std::array<float, kFusionMaxSources> contribution {};
};

FusionPixel EvaluateFusionPixel(
    const FusionControls&, const FusionSourceInfo*, const FusionObservation*,
    std::size_t sourceCount, std::size_t anchor,
    float analysisEv, float x, float y);

// Bounded, camera-linear CFA samples for interactive inspection. No RAW
// decoding, registration, or grading is performed while editing this view.
struct FusionPreview {
    std::string contentIdentity;
    std::string graphInputIdentity;
    std::uint32_t width = 0, height = 0;
    std::uint64_t rawWidth = 0, rawHeight = 0;
    std::uint32_t stride = 1;
    std::size_t anchor = 0;
    std::array<int, 4> channelByParity { 0, 1, 1, 2 };
    std::array<float, 3> whiteBalance { 1, 1, 1 };
    std::vector<FusionSourceInfo> sources;
    // Pixel-major, then four CFA parities, then source.
    std::vector<FusionObservation> observations;
    std::vector<float> analysisEv;
};

} // namespace Raw::Hdr
