#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <string>

namespace Stack::NodeMath {

enum class ArithmeticMode {
    Clamp, Add, Subtract, Multiply, Divide, Mix, Min, Max, Difference, Remap
};

struct PrimitiveArithmetic {
    const char* name;
    const char* symbol;
    float defaultB;
    double (*calculate)(double a, double b);
};

inline constexpr bool UnaryArithmetic(ArithmeticMode mode) {
    return mode == ArithmeticMode::Clamp || mode == ArithmeticMode::Remap;
}

inline constexpr int kMixSlotCount = 8;
using MixWeights = std::array<double, kMixSlotCount>;

inline constexpr int ArithmeticInputCount(ArithmeticMode mode) {
    return mode == ArithmeticMode::Mix ? kMixSlotCount :
        UnaryArithmetic(mode) ? 1 : 2;
}

inline const PrimitiveArithmetic* Primitive(ArithmeticMode mode) {
    static constexpr PrimitiveArithmetic add{"Add", "+", 0, [](double a, double b) { return a + b; }};
    static constexpr PrimitiveArithmetic subtract{"Subtract", "-", 0, [](double a, double b) { return a - b; }};
    static constexpr PrimitiveArithmetic multiply{"Multiply", "*", 1, [](double a, double b) { return a * b; }};
    static constexpr PrimitiveArithmetic divide{"Divide", "/", 1, [](double a, double b) { return b == 0 ? 0.0 : a / b; }};
    static constexpr PrimitiveArithmetic minimum{"Minimum", "min", 0, [](double a, double b) { return std::min(a,b); }};
    static constexpr PrimitiveArithmetic maximum{"Maximum", "max", 0, [](double a, double b) { return std::max(a,b); }};
    static constexpr PrimitiveArithmetic difference{"Absolute Difference", "abs-difference", 0, [](double a, double b) { return std::abs(a-b); }};
    switch (mode) {
    case ArithmeticMode::Min: return &minimum;
    case ArithmeticMode::Max: return &maximum;
    case ArithmeticMode::Difference: return &difference;
    case ArithmeticMode::Add: return &add;
    case ArithmeticMode::Subtract: return &subtract;
    case ArithmeticMode::Multiply: return &multiply;
    case ArithmeticMode::Divide: return &divide;
    default: return nullptr;
    }
}

// The snapshot derives execution storage from the connected operands.
enum class ArithmeticDomain { Spatial, Values, Channel };

struct ArithmeticSettings {
    ArithmeticDomain domain = ArithmeticDomain::Spatial;
    std::string requiredLinearBasis;
    bool boundedMask = false;
    bool percentageInput = false;
    MixWeights mixWeights{1,1,1,1,1,1,1,1};
    float constantA = 0.0f;
    float constantB = 1.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float outMin = 0.0f;
    float outMax = 1.0f;
};

inline const char* ArithmeticLimitsError(ArithmeticMode mode, const ArithmeticSettings& settings) {
    if (!UnaryArithmetic(mode)) return nullptr;
    if (!std::isfinite(settings.minValue) || !std::isfinite(settings.maxValue))
        return "Range endpoints must be finite.";
    if (mode == ArithmeticMode::Clamp && settings.minValue > settings.maxValue)
        return "Clamp minimum exceeds its maximum.";
    if (mode == ArithmeticMode::Remap && settings.minValue == settings.maxValue)
        return "Remap Range needs two distinct input endpoints.";
    if (mode == ArithmeticMode::Remap && (!std::isfinite(settings.outMin) || !std::isfinite(settings.outMax)))
        return "Remap Range needs finite output endpoints.";
    return nullptr;
}

inline bool EvaluateUniformArithmetic(ArithmeticMode mode, const ArithmeticSettings& settings,
    const std::vector<double>& inputs, double& result, std::string& error) {
    error.clear();
    const auto fail = [&](const char* reason) { error = reason; return false; };
    if (settings.domain != ArithmeticDomain::Values)
        return fail("This math operation does not produce a uniform scalar value.");
    if (std::any_of(inputs.begin(), inputs.end(), [](double value) { return !std::isfinite(value); }))
        return fail("Value Math requires finite inputs.");
    const double a = inputs.empty() ? settings.constantA : inputs.front();
    const double b = inputs.size() < 2 ? settings.constantB : inputs[1];
    if (!std::isfinite(a) || !std::isfinite(b)) return fail("Value Math constants must be finite.");
    if (auto reason = ArithmeticLimitsError(mode, settings)) return fail(reason);
    switch (mode) {
    case ArithmeticMode::Clamp:
        result = std::clamp(a, double(settings.minValue), double(settings.maxValue)); break;
    case ArithmeticMode::Add:
    case ArithmeticMode::Subtract:
    case ArithmeticMode::Multiply:
    case ArithmeticMode::Divide:
    case ArithmeticMode::Min:
    case ArithmeticMode::Max:
    case ArithmeticMode::Difference:
        result = Primitive(mode)->calculate(a, b);
        break;
    case ArithmeticMode::Mix: return fail("Mix executes its weighted arithmetic recipe.");
    case ArithmeticMode::Remap: {
        const double low = settings.minValue, high = settings.maxValue;
        const double t = (a - low) / (high - low);
        result = settings.outMin + t * (double(settings.outMax) - settings.outMin); break;
    }
    default: return fail("This arithmetic operation is unavailable.");
    }
    return std::isfinite(result) || fail("Value Math produced a non-finite result.");
}

} // namespace Stack::NodeMath
