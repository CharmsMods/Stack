#include "Raw/Denoise/RawDenoiseControlMap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Stack::RawRecipe {
namespace {

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float JsonFloat(
    const nlohmann::json& value,
    const char* key,
    float fallback) {
    const auto item = value.find(key);
    if (item == value.end() || !item->is_number()) {
        return fallback;
    }
    const double parsed = item->get<double>();
    if (!std::isfinite(parsed) ||
        parsed < -static_cast<double>(std::numeric_limits<float>::max()) ||
        parsed > static_cast<double>(std::numeric_limits<float>::max())) {
        return fallback;
    }
    return static_cast<float>(parsed);
}

std::uint64_t JsonId(
    const nlohmann::json& value,
    const char* key,
    std::uint64_t fallback) {
    const auto item = value.find(key);
    if (item == value.end() || !item->is_number_unsigned()) {
        return fallback;
    }
    return item->get<std::uint64_t>();
}

float CompactPointWeight(
    const RawDenoiseControlPoint& point,
    const RawDenoiseControlMap& map,
    float frequency,
    float sceneEv) {
    const bool spansAllFrequencies =
        point.frequency <= 0.0001f || point.frequency >= 0.9999f;
    const bool spansAllLuminances =
        point.sceneEv <= map.minimumEv + 0.0001f ||
        point.sceneEv >= map.maximumEv - 0.0001f;
    const float frequencyDistance = spansAllFrequencies
        ? 0.0f
        : (frequency - point.frequency) /
            std::max(0.01f, point.frequencyRadius);
    const float luminanceDistance = spansAllLuminances
        ? 0.0f
        : (sceneEv - point.sceneEv) /
            std::max(0.05f, point.luminanceRadiusEv);
    const float radius = std::sqrt(
        frequencyDistance * frequencyDistance +
        luminanceDistance * luminanceDistance);
    if (radius >= 1.0f) {
        return 0.0f;
    }
    const float inverse = 1.0f - radius;
    const float inverseSquared = inverse * inverse;
    return inverseSquared * inverseSquared * (4.0f * radius + 1.0f);
}

float EvaluateSanitizedControlMap(
    const RawDenoiseControlMap& map,
    float normalizedFrequency,
    float sceneEv) {
    const float frequency = std::clamp(
        FiniteOr(normalizedFrequency, 0.5f), 0.0f, 1.0f);
    const float luminance = std::clamp(
        FiniteOr(sceneEv, 0.0f), map.minimumEv, map.maximumEv);
    float multiplier = map.baseMultiplier;
    for (const RawDenoiseControlPoint& point : map.points) {
        if (!point.enabled || std::abs(point.multiplierDelta) <= 0.000001f) {
            continue;
        }
        multiplier += point.multiplierDelta *
            CompactPointWeight(point, map, frequency, luminance);
    }
    return std::clamp(multiplier, 0.0f, kRawDenoiseMaximumMultiplier);
}

} // namespace

RawDenoiseControlMap SanitizeRawDenoiseControlMap(
    RawDenoiseControlMap map,
    float fallbackBaseMultiplier) {
    map.baseMultiplier = std::clamp(
        FiniteOr(map.baseMultiplier, fallbackBaseMultiplier),
        0.0f,
        kRawDenoiseMaximumMultiplier);
    map.minimumEv = std::clamp(
        FiniteOr(map.minimumEv, kRawDenoiseMinimumEv),
        -24.0f,
        23.0f);
    map.maximumEv = std::clamp(
        FiniteOr(map.maximumEv, kRawDenoiseMaximumEv),
        map.minimumEv + 1.0f,
        24.0f);

    std::unordered_set<std::uint64_t> ids;
    std::vector<RawDenoiseControlPoint> points;
    points.reserve(std::min(map.points.size(), kMaxRawDenoiseControlPoints));
    std::uint64_t replacementId = 1;
    for (RawDenoiseControlPoint point : map.points) {
        if (points.size() >= kMaxRawDenoiseControlPoints) {
            break;
        }
        if (point.id == 0 || ids.find(point.id) != ids.end()) {
            while (replacementId == 0 || ids.find(replacementId) != ids.end()) {
                ++replacementId;
            }
            point.id = replacementId++;
        }
        ids.insert(point.id);
        point.frequency = std::clamp(
            FiniteOr(point.frequency, 0.5f), 0.0f, 1.0f);
        point.sceneEv = std::clamp(
            FiniteOr(point.sceneEv, -2.0f), map.minimumEv, map.maximumEv);
        point.frequencyRadius = std::clamp(
            FiniteOr(point.frequencyRadius, 0.18f), 0.01f, 1.0f);
        point.luminanceRadiusEv = std::clamp(
            FiniteOr(point.luminanceRadiusEv, 1.5f), 0.05f, 24.0f);
        point.multiplierDelta = std::clamp(
            FiniteOr(point.multiplierDelta, 0.0f),
            0.0f,
            kRawDenoiseMaximumMultiplier);
        points.push_back(point);
    }
    map.points = std::move(points);
    return map;
}

float EvaluateRawDenoiseControlMap(
    const RawDenoiseControlMap& input,
    float normalizedFrequency,
    float sceneEv) {
    const RawDenoiseControlMap map =
        SanitizeRawDenoiseControlMap(input, 0.0f);
    return EvaluateSanitizedControlMap(map, normalizedFrequency, sceneEv);
}

bool HasRawDenoiseControlMapEffect(
    const RawDenoiseControlMap& input) {
    const RawDenoiseControlMap map =
        SanitizeRawDenoiseControlMap(input, 0.0f);
    if (map.baseMultiplier > 0.000001f) {
        return true;
    }
    return std::any_of(
        map.points.begin(),
        map.points.end(),
        [](const RawDenoiseControlPoint& point) {
            return point.enabled && point.multiplierDelta > 0.000001f;
        });
}

std::vector<float> BakeRawDenoiseControlMap(
    const RawDenoiseControlMap& input,
    int frequencySamples,
    int luminanceSamples) {
    frequencySamples = std::clamp(frequencySamples, 2, 512);
    luminanceSamples = std::clamp(luminanceSamples, 2, 512);
    const RawDenoiseControlMap map =
        SanitizeRawDenoiseControlMap(input, 0.0f);
    std::vector<float> result(
        static_cast<std::size_t>(frequencySamples) *
            static_cast<std::size_t>(luminanceSamples),
        map.baseMultiplier);
    for (int y = 0; y < luminanceSamples; ++y) {
        const float yFraction = static_cast<float>(y) /
            static_cast<float>(luminanceSamples - 1);
        const float sceneEv = map.minimumEv +
            yFraction * (map.maximumEv - map.minimumEv);
        for (int x = 0; x < frequencySamples; ++x) {
            const float frequency = static_cast<float>(x) /
                static_cast<float>(frequencySamples - 1);
            result[static_cast<std::size_t>(y * frequencySamples + x)] =
                EvaluateSanitizedControlMap(map, frequency, sceneEv);
        }
    }
    return result;
}

nlohmann::json SerializeRawDenoiseControlMap(
    const RawDenoiseControlMap& input) {
    const RawDenoiseControlMap map =
        SanitizeRawDenoiseControlMap(input, 0.0f);
    nlohmann::json points = nlohmann::json::array();
    for (const RawDenoiseControlPoint& point : map.points) {
        points.push_back({
            { "id", point.id },
            { "linkGroup", point.linkGroup },
            { "enabled", point.enabled },
            { "frequency", point.frequency },
            { "sceneEv", point.sceneEv },
            { "frequencyRadius", point.frequencyRadius },
            { "luminanceRadiusEv", point.luminanceRadiusEv },
            { "multiplierDelta", point.multiplierDelta }
        });
    }
    return {
        { "baseMultiplier", map.baseMultiplier },
        { "minimumEv", map.minimumEv },
        { "maximumEv", map.maximumEv },
        { "points", std::move(points) }
    };
}

RawDenoiseControlMap DeserializeRawDenoiseControlMap(
    const nlohmann::json& value,
    float fallbackBaseMultiplier) {
    RawDenoiseControlMap map;
    map.baseMultiplier = fallbackBaseMultiplier;
    if (!value.is_object()) {
        return SanitizeRawDenoiseControlMap(
            std::move(map), fallbackBaseMultiplier);
    }
    map.baseMultiplier = JsonFloat(
        value, "baseMultiplier", fallbackBaseMultiplier);
    map.minimumEv = JsonFloat(value, "minimumEv", map.minimumEv);
    map.maximumEv = JsonFloat(value, "maximumEv", map.maximumEv);
    const nlohmann::json points =
        value.value("points", nlohmann::json::array());
    if (points.is_array()) {
        map.points.reserve(
            std::min(points.size(), kMaxRawDenoiseControlPoints));
        for (const nlohmann::json& item : points) {
            if (!item.is_object() ||
                map.points.size() >= kMaxRawDenoiseControlPoints) {
                continue;
            }
            RawDenoiseControlPoint point;
            point.id = JsonId(item, "id", point.id);
            point.linkGroup = JsonId(item, "linkGroup", point.linkGroup);
            point.enabled = item.value("enabled", point.enabled);
            point.frequency = JsonFloat(item, "frequency", point.frequency);
            point.sceneEv = JsonFloat(item, "sceneEv", point.sceneEv);
            point.frequencyRadius = JsonFloat(
                item, "frequencyRadius", point.frequencyRadius);
            point.luminanceRadiusEv = JsonFloat(
                item, "luminanceRadiusEv", point.luminanceRadiusEv);
            point.multiplierDelta = JsonFloat(
                item, "multiplierDelta", point.multiplierDelta);
            map.points.push_back(point);
        }
    }
    return SanitizeRawDenoiseControlMap(
        std::move(map), fallbackBaseMultiplier);
}

} // namespace Stack::RawRecipe
