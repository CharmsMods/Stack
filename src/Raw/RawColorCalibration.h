#pragma once

#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"
#include <array>

namespace Stack::RawRecipe {

struct RawPrimaryCalibration {
    float hue = 0.0f;
    float saturation = 0.0f;
};

struct RawColorCalibrationRecipe {
    int version = 1;
    bool enabled = true;
    std::array<RawPrimaryCalibration, 3> primaries {};
};

using CalibrationChromaticities = std::array<std::array<double, 2>, 3>;
inline constexpr std::array<double, 2> kCalibrationWhite {0.3127, 0.3290};
inline constexpr CalibrationChromaticities kCalibrationPrimaries {{
    {0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}
}};

struct RawColorCalibrationTransform {
    bool active = false;
    // Row-major; upload to GLSL with transpose enabled.
    std::array<float, 9> matrix {1,0,0, 0,1,0, 0,0,1};
};

RawColorCalibrationRecipe SanitizeColorCalibration(const RawColorCalibrationRecipe& recipe);
bool IsColorCalibrationActive(const RawColorCalibrationRecipe& recipe);
CalibrationChromaticities ColorCalibrationPrimaries(const RawColorCalibrationRecipe& recipe);
RawColorCalibrationTransform BuildColorCalibrationTransform(
    const RawColorCalibrationRecipe& recipe, Raw::RawWorkingSpace workingSpace);
nlohmann::json SerializeColorCalibration(const RawColorCalibrationRecipe& recipe);
RawColorCalibrationRecipe DeserializeColorCalibration(const nlohmann::json& value);

} // namespace Stack::RawRecipe
