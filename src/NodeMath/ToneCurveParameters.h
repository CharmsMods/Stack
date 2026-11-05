#pragma once
#include "ThirdParty/json.hpp"
#include "Raw/Tone/SceneTone.h"

namespace Stack::NodeMath {
inline constexpr char kDefaultToneCurveMode[] = "luminance";
inline std::string ToneCurveMode(const nlohmann::json& parameters) {
    return parameters.value("curveMode", std::string(kDefaultToneCurveMode));
}
nlohmann::json DefaultToneCurveParameters();
nlohmann::json CaptureToneCurveParameters(const nlohmann::json& editorValue);
// Analysis requests are transient; history restores never restart them.
void ClearPendingToneCurveAnalysis(nlohmann::json& parameters);
bool IsToneCurveParameterActive(const nlohmann::json& parameters,const std::string& id);
// Resolve the scene curve actually executed by a graph Tone Curve operation,
// including representation selection and a locked curve amount reference.
RawRecipe::SceneTone EffectiveToneCurveSceneTone(const nlohmann::json& parameters);
bool ValidateToneCurveParameters(const nlohmann::json& parameters, std::string& error);
}
