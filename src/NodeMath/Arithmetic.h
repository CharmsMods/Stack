#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <string>

namespace Stack::NodeMath {

enum class ArithmeticMode {
    Clamp, Add, Subtract, Multiply, Divide, Average, Min, Max, Difference, Remap, ImageAverage
};

// Values is uniform arithmetic; Spatial retains the existing image/channel
// computation. This authored domain keeps socket types stable on disconnect.
enum class ArithmeticDomain { Spatial, Values };

struct ArithmeticSettings {
    ArithmeticDomain domain = ArithmeticDomain::Spatial;
    float constantA = 0.0f;
    float constantB = 1.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float outMin = 0.0f;
    float outMax = 1.0f;
};

inline bool EvaluateUniformArithmetic(ArithmeticMode mode, const ArithmeticSettings& settings,
    const std::vector<double>& inputs, double& result, std::string& error) {
    error.clear();
    const auto fail = [&](const char* reason) { error = reason; return false; };
    if (settings.domain != ArithmeticDomain::Values || mode == ArithmeticMode::ImageAverage)
        return fail("This math operation does not produce a uniform scalar value.");
    if (std::any_of(inputs.begin(), inputs.end(), [](double value) { return !std::isfinite(value); }))
        return fail("Value Math requires finite inputs.");
    const double a = inputs.empty() ? settings.constantA : inputs.front();
    const double b = inputs.size() < 2 ? settings.constantB : inputs[1];
    if (!std::isfinite(a) || !std::isfinite(b)) return fail("Value Math constants must be finite.");
    switch (mode) {
    case ArithmeticMode::Clamp:
        if (!std::isfinite(settings.minValue) || !std::isfinite(settings.maxValue) || settings.minValue > settings.maxValue)
            return fail("Clamp requires finite limits ordered from minimum to maximum.");
        result = std::clamp(a, double(settings.minValue), double(settings.maxValue)); break;
    case ArithmeticMode::Add: result = a + b; break;
    case ArithmeticMode::Subtract: result = a - b; break;
    case ArithmeticMode::Multiply: result = a * b; break;
    case ArithmeticMode::Divide:
        if (b == 0) return fail("Value Math cannot divide by zero.");
        result = a / b; break;
    case ArithmeticMode::Average:
        if (inputs.empty()) result = a;
        else {
            result = inputs.front();
            for (std::size_t i = 1; i < inputs.size(); ++i)
                result = result * (double(i) / double(i + 1)) + inputs[i] / double(i + 1);
        }
        break;
    case ArithmeticMode::Min: result = std::min(a, b); break;
    case ArithmeticMode::Max: result = std::max(a, b); break;
    case ArithmeticMode::Difference: result = std::abs(a - b); break;
    case ArithmeticMode::Remap: {
        const double low = settings.minValue, high = settings.maxValue;
        if (!std::isfinite(low) || !std::isfinite(high) || low >= high ||
            !std::isfinite(settings.outMin) || !std::isfinite(settings.outMax))
            return fail("Remap requires a finite increasing input range and finite output limits.");
        const double t = std::clamp((a - low) / (high - low), 0.0, 1.0);
        result = (1 - t) * settings.outMin + t * settings.outMax; break;
    }
    case ArithmeticMode::ImageAverage: return fail("Average Images requires spatial image inputs.");
    default: return fail("This arithmetic operation is unavailable.");
    }
    return std::isfinite(result) || fail("Value Math produced a non-finite result.");
}

} // namespace Stack::NodeMath
