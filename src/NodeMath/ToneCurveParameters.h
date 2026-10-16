#pragma once
#include "ThirdParty/json.hpp"

namespace Stack::NodeMath {
nlohmann::json DefaultToneCurveParameters();
nlohmann::json CaptureToneCurveParameters(const nlohmann::json& editorValue);
// Analysis requests are transient; history restores never restart them.
void ClearPendingToneCurveAnalysis(nlohmann::json& parameters);
bool IsToneCurveParameterActive(const nlohmann::json& parameters,const std::string& id);
bool ValidateToneCurveParameters(const nlohmann::json& parameters, std::string& error);
}
