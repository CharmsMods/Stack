#pragma once
#include "ThirdParty/json.hpp"
#include "Raw/Curves/CurveTypes.h"
#include <array>
#include <vector>

namespace Stack::RawRecipe {

// The slope representation is retained only for existing saved edits. New
// instances author ordinary input/output points in the EV -16..16 domain.
struct SceneTonePoint { float ev = 0.0f; float contrast = 1.0f; };
struct SceneTone {
    bool direct = true;
    std::vector<RawBezierCurvePoint> curve {{0,0,{1},{1}},{1,1,{1},{1}}};
    CurveAmountState curveAmount;
    bool enabled = true;
    float anchorOffsetEv = 0.0f;
    float contrast = 1.0f;
    float pivotEv = 0.0f;
    float coreHalfWidthEv = 1.0f;
    float transitionEv = 2.0f;
    float outerProtection = 0.0f;
    std::vector<SceneTonePoint> points {{-16,1},{-4,1},{-1,1},{0,1},{1,1},{4,1},{16,1}};
};
inline constexpr float kSceneToneSlopeFloor = 0.025f;
inline constexpr float kSceneToneLutMinEv = -40.0f;
inline constexpr float kSceneToneLutMaxEv = 40.0f;
inline constexpr int kSceneToneLutSize = 4097;

SceneTone SanitizeSceneTone(SceneTone tone);
SceneTone ReadSceneTone(const nlohmann::json& value);
nlohmann::json SerializeSceneTone(const SceneTone& tone);
bool IsSceneToneActive(const SceneTone& tone);
// Effective contrast can be smaller than requested to keep every slope
// strictly positive. The editor must show the limit, never hide it.
float EffectiveSceneToneContrast(const SceneTone& tone);
float EvaluateSceneToneContrast(const SceneTone& tone, float ev);
float EvaluateSceneToneEv(const SceneTone& tone, float ev);
std::array<float,3> ApplySceneTone(const SceneTone& tone,
    std::array<float,3> rgb, const std::array<float,3>& lumaWeights);
// Curve-view editing changes the same slope points. Returns the achieved EV
// after enforcing the slope limits. The zero-EV point edits the anchor.
float SetSceneToneOutput(SceneTone& tone, std::size_t point, float outputEv);
std::vector<float> BuildSceneToneLut(const SceneTone& tone);

} // namespace Stack::RawRecipe
