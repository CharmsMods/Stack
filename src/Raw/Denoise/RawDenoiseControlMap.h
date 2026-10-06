#pragma once

#include "ThirdParty/json.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Stack::RawRecipe {

inline constexpr std::size_t kMaxRawDenoiseControlPoints = 128;
inline constexpr float kRawDenoiseMinimumEv = -8.0f;
inline constexpr float kRawDenoiseMaximumEv = 6.0f;
inline constexpr float kRawDenoiseMaximumMultiplier = 4.0f;

enum class RawDenoiseMapLayer {
    Luma = 0,
    Chroma = 1
};

enum class RawDenoiseDiagnosticMode {
    None = 0,
    SelectedControlCoverage,
    SelectedControlInfluence,
    TotalChange,
    LumaChange,
    ChromaChange,
    EstimatedNoise
};

struct RawDenoiseControlPoint {
    std::uint64_t id = 0;
    std::uint64_t linkGroup = 0;
    bool enabled = true;
    // Zero is the coarsest represented structure. One is the finest detail.
    float frequency = 0.5f;
    // Scene exposure value relative to 18 percent middle grey.
    float sceneEv = -2.0f;
    float frequencyRadius = 0.18f;
    float luminanceRadiusEv = 1.5f;
    // Added to the automatic-threshold multiplier inside this point's reach.
    float multiplierDelta = 0.0f;
};

struct RawDenoiseControlMap {
    // Zero keeps coefficients unchanged. One uses the automatic threshold.
    float baseMultiplier = 0.0f;
    float minimumEv = kRawDenoiseMinimumEv;
    float maximumEv = kRawDenoiseMaximumEv;
    std::vector<RawDenoiseControlPoint> points;
};

RawDenoiseControlMap SanitizeRawDenoiseControlMap(
    RawDenoiseControlMap map,
    float fallbackBaseMultiplier);

float EvaluateRawDenoiseControlMap(
    const RawDenoiseControlMap& map,
    float normalizedFrequency,
    float sceneEv);

bool HasRawDenoiseControlMapEffect(
    const RawDenoiseControlMap& map);

std::vector<float> BakeRawDenoiseControlMap(
    const RawDenoiseControlMap& map,
    int frequencySamples,
    int luminanceSamples);

nlohmann::json SerializeRawDenoiseControlMap(
    const RawDenoiseControlMap& map);

RawDenoiseControlMap DeserializeRawDenoiseControlMap(
    const nlohmann::json& value,
    float fallbackBaseMultiplier);

} // namespace Stack::RawRecipe
