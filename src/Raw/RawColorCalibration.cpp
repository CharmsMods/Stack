#include "Raw/RawColorCalibration.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Stack::RawRecipe {
namespace {
using Matrix = std::array<double, 9>;

Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix result {};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            for (int k = 0; k < 3; ++k)
                result[row * 3 + col] += a[row * 3 + k] * b[k * 3 + col];
    return result;
}

Matrix Inverse(const Matrix& a) {
    Matrix result {
        a[4]*a[8]-a[5]*a[7], a[2]*a[7]-a[1]*a[8], a[1]*a[5]-a[2]*a[4],
        a[5]*a[6]-a[3]*a[8], a[0]*a[8]-a[2]*a[6], a[2]*a[3]-a[0]*a[5],
        a[3]*a[7]-a[4]*a[6], a[1]*a[6]-a[0]*a[7], a[0]*a[4]-a[1]*a[3]
    };
    const double determinant = a[0]*result[0] + a[1]*result[3] + a[2]*result[6];
    if (!std::isfinite(determinant) || determinant == 0.0)
        throw std::runtime_error("Color Calibration primary matrix is singular");
    for (auto& value : result) value /= determinant;
    return result;
}

Matrix PrimariesToXyz(const CalibrationChromaticities& primaries) {
    Matrix columns {};
    for (int col = 0; col < 3; ++col) {
        columns[col] = primaries[col][0];
        columns[3 + col] = primaries[col][1];
        columns[6 + col] = 1.0 - primaries[col][0] - primaries[col][1];
    }
    // Homogeneous columns support virtual primaries whose y can cross zero.
    const auto inverse = Inverse(columns);
    const std::array<double, 3> white {
        kCalibrationWhite[0] / kCalibrationWhite[1], 1.0,
        (1.0 - kCalibrationWhite[0] - kCalibrationWhite[1]) / kCalibrationWhite[1]
    };
    for (int col = 0; col < 3; ++col) {
        double scale = 0.0;
        for (int k = 0; k < 3; ++k) scale += inverse[col * 3 + k] * white[k];
        for (int row = 0; row < 3; ++row) columns[row * 3 + col] *= scale;
    }
    return columns;
}

float Control(float value) {
    return std::isfinite(value) ? value : 0.0f;
}
} // namespace

RawColorCalibrationRecipe SanitizeColorCalibration(const RawColorCalibrationRecipe& input) {
    auto recipe = input;
    if (recipe.version != 1 && recipe.version != 2)
        throw std::runtime_error("Unsupported Color Calibration version");
    for (auto& primary : recipe.primaries) {
        primary.hue = Control(primary.hue);
        primary.saturation = Control(primary.saturation);
        // Keep legacy documents byte-canonical until an unrestricted value
        // is authored. Older readers reject v2 instead of silently clamping.
        if (std::abs(primary.hue) > 100.f || std::abs(primary.saturation) > 100.f) recipe.version = 2;
    }
    return recipe;
}

bool IsColorCalibrationActive(const RawColorCalibrationRecipe& input) {
    const auto recipe = SanitizeColorCalibration(input);
    if (!recipe.enabled) return false;
    for (const auto& primary : recipe.primaries)
        if (primary.hue != 0.0f || primary.saturation != 0.0f) return true;
    return false;
}

CalibrationChromaticities ColorCalibrationPrimaries(const RawColorCalibrationRecipe& input) {
    const auto recipe = SanitizeColorCalibration(input);
    auto result = kCalibrationPrimaries;
    for (int i = 0; i < 3; ++i) {
        const double angle = std::remainder(double(recipe.primaries[i].hue), 1800.0) * (0.2 * 3.14159265358979323846 / 180.0);
        const double scale = std::exp2(recipe.primaries[i].saturation / 100.0);
        const double x = result[i][0] - kCalibrationWhite[0];
        const double y = result[i][1] - kCalibrationWhite[1];
        result[i] = {
            kCalibrationWhite[0] + scale * (std::cos(angle)*x - std::sin(angle)*y),
            kCalibrationWhite[1] + scale * (std::sin(angle)*x + std::cos(angle)*y)
        };
    }
    return result;
}

RawPrimaryCalibration ColorCalibrationControlForPosition(
    int primary, const std::array<double, 2>& xy, RawPrimaryCalibration previous) {
    const auto& original = kCalibrationPrimaries.at(primary);
    const double x = xy[0] - kCalibrationWhite[0], y = xy[1] - kCalibrationWhite[1];
    const double ox = original[0] - kCalibrationWhite[0], oy = original[1] - kCalibrationWhite[1];
    const double radius = std::hypot(x, y);
    if (radius > 0.0) {
        const double angle = (std::atan2(y,x) - std::atan2(oy,ox)) * (900.0 / 3.14159265358979323846);
        previous.hue += static_cast<float>(std::remainder(angle - double(previous.hue), 1800.0));
        previous.saturation = static_cast<float>(100.0 * std::log2(radius / std::hypot(ox,oy)));
    } else {
        // A finite value whose exponential is exactly zero at machine
        // precision. JSON round-trips it without an infinity/null sentinel.
        previous.saturation = std::numeric_limits<float>::lowest();
    }
    return previous;
}

RawColorCalibrationTransform BuildColorCalibrationTransform(
    const RawColorCalibrationRecipe& recipe, Raw::RawWorkingSpace workingSpace) {
    RawColorCalibrationTransform result;
    result.active = IsColorCalibrationActive(recipe);
    if (!result.active) return result;
    static const Matrix reference = PrimariesToXyz(kCalibrationPrimaries);
    static const Matrix referenceInverse = Inverse(reference);
    static const Matrix srgb = PrimariesToXyz({{{0.64,0.33}, {0.30,0.60}, {0.15,0.06}}});
    static const Matrix srgbInverse = Inverse(srgb);
    const auto adjusted = PrimariesToXyz(ColorCalibrationPrimaries(recipe));
    const Matrix transform = workingSpace == Raw::RawWorkingSpace::LinearRec2020D65
        ? Multiply(referenceInverse, adjusted)
        : Multiply(Multiply(Multiply(srgbInverse, adjusted), referenceInverse), srgb);
    for (std::size_t i = 0; i < transform.size(); ++i) {
        result.matrix[i] = static_cast<float>(transform[i]);
        if (!std::isfinite(result.matrix[i]))
            throw std::runtime_error("Color Calibration has no finite transform for these primaries");
    }
    return result;
}

nlohmann::json SerializeColorCalibration(const RawColorCalibrationRecipe& input) {
    const auto recipe = SanitizeColorCalibration(input);
    auto primaries = nlohmann::json::array();
    for (const auto& primary : recipe.primaries)
        primaries.push_back({{"hue", primary.hue}, {"saturation", primary.saturation}});
    return {{"version", recipe.version}, {"enabled", recipe.enabled}, {"primaries", primaries}};
}

RawColorCalibrationRecipe DeserializeColorCalibration(const nlohmann::json& value) {
    RawColorCalibrationRecipe recipe;
    if (!value.is_object()) return recipe;
    recipe.version = value.value("version", 1);
    recipe.enabled = value.value("enabled", true);
    const auto primaries = value.find("primaries");
    if (primaries != value.end() && primaries->is_array()) {
        for (std::size_t i = 0; i < std::min(primaries->size(), recipe.primaries.size()); ++i) {
            const auto& primary = (*primaries)[i];
            if (!primary.is_object()) continue;
            const auto read = [&](const char* key) {
                const auto found = primary.find(key);
                return found != primary.end() && found->is_number() ? Control(found->get<float>()) : 0.0f;
            };
            recipe.primaries[i] = {read("hue"), read("saturation")};
        }
    }
    return SanitizeColorCalibration(recipe);
}
} // namespace Stack::RawRecipe
