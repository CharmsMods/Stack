#pragma once
#include "ThirdParty/json.hpp"
#include <cmath>
#include <cstdint>
#include <string>

namespace Stack::NodeMath {
// Uniform parameters are computation inputs. Their evaluated values never
// replace the manual authored payload and are independent of mask coverage.
struct UniformParameter {
    std::string id, label, path;
    double minimum = 0, maximum = 0;
    bool bounded = false;
};
struct UniformParameterFeedback {
    std::string documentId, graphId, nodeUuid, definitionHash, parameterId;
    std::uint64_t revision = 0;
    std::size_t sourceIdentity = 0;
    double value = 0;
    bool available = false;
    std::string reason;
};
inline bool ValidateUniformParameter(const UniformParameter& parameter, double value, std::string& error) {
    error.clear();
    if (!std::isfinite(value)) {
        error = parameter.label + " requires a finite uniform value."; return false;
    }
    if (parameter.bounded && (value < parameter.minimum || value > parameter.maximum)) {
        error = parameter.label + " is outside its declared parameter range."; return false;
    }
    return true;
}
inline bool WriteUniformParameter(nlohmann::json& payload, const UniformParameter& parameter,
    double value, std::string& error) {
    if (!ValidateUniformParameter(parameter,value,error)) return false;
    try {
        payload[nlohmann::json::json_pointer(parameter.path)] = value;
        return true;
    } catch (const std::exception&) {
        error = parameter.label + " has no valid parameter storage path."; return false;
    }
}
}
