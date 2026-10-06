#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include <functional>

namespace Stack::Renderer::RawDenoise {
inline std::size_t RestormerNeutralFingerprint(std::size_t input,
    const Stack::RawRecipe::RawRgbDenoiseRecipe& settings) {
    const auto combine = [&](const std::string& value) {
        input ^= std::hash<std::string>{}(value) + 0x9e3779b97f4a7c15ULL + (input << 6U) + (input >> 2U);
    };
    combine(Stack::RawRecipe::RgbDenoiseMethodStableString(settings.method));
    combine(settings.packageId);
    combine(settings.packageVersion);
    combine(settings.modelSha256);
    combine(settings.adapterVersion);
    return input == 0 ? 1 : input;
}

inline std::size_t RestormerApplicationFingerprint(std::size_t model,
    Raw::RawWorkingSpace workingSpace, const Stack::RawRecipe::RawRgbDenoiseRecipe& settings) {
    const auto combine = [&](std::size_t value) {
        model ^= value + 0x9e3779b97f4a7c15ULL + (model << 6U) + (model >> 2U);
    };
    combine(static_cast<std::size_t>(workingSpace));
    combine(static_cast<std::size_t>(settings.mapping));
    combine(std::hash<float>{}(settings.colorNoise));
    combine(std::hash<float>{}(settings.luminanceNoise));
    combine(std::hash<float>{}(settings.detailProtection));
    return model == 0 ? 1 : model;
}
}
