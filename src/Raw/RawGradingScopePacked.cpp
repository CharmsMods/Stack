#include "Raw/RawGradingScope.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Raw {

std::shared_ptr<const RawGradingScopeVisualization> BuildGradingScopeVisualizationFromPacked(
    const RawDevelopmentGradingScopeReadback& readback, const std::vector<std::uint32_t>& packed) {
    if (!readback.valid || packed.size() != kGradingScopePackedValueCount) return {};
    using Plot = RawGradingScopeVisualization;
    auto result = std::make_shared<Plot>();
    std::memcpy(result->histogram.data(), packed.data(), sizeof(result->histogram));
    if (!std::all_of(result->histogram.begin(), result->histogram.end(),
        [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; })) return {};
    const auto color = [](std::uint32_t value) {
        return std::array<float, 4> { (value & 255u) / 255.0f,
            ((value >> 8) & 255u) / 255.0f, ((value >> 16) & 255u) / 255.0f,
            (value >> 24) / 255.0f };
    };
    const auto vectorBegin = packed.begin() + Plot::kHistogramBins;
    const auto paradeBegin = vectorBegin + Plot::kVectorscopeResolution * Plot::kVectorscopeResolution;
    const auto visible = [](std::uint32_t value) { return (value >> 24) != 0; };
    result->vectorscopePoints.reserve(std::count_if(vectorBegin, paradeBegin, visible));
    result->paradePoints.reserve(std::count_if(paradeBegin, packed.end(), visible));
    for (int y = 0; y < Plot::kVectorscopeResolution; ++y) {
        for (int x = 0; x < Plot::kVectorscopeResolution; ++x) {
            const auto value = vectorBegin[y * Plot::kVectorscopeResolution + x];
            if (!visible(value)) continue;
            result->vectorscopePoints.push_back({
                (x + 0.5f) / Plot::kVectorscopeResolution,
                (y + 0.5f) / Plot::kVectorscopeResolution, color(value) });
        }
    }
    for (int channel = 0; channel < 3; ++channel) {
        for (int y = 0; y < Plot::kParadeRows; ++y) {
            for (int x = 0; x < Plot::kParadeColumns; ++x) {
                const auto value = paradeBegin[(channel * Plot::kParadeRows + y) * Plot::kParadeColumns + x];
                if (!visible(value)) continue;
                result->paradePoints.push_back({
                    (channel + (x + 0.5f) / Plot::kParadeColumns) / 3.0f,
                    (y + 0.5f) / Plot::kParadeRows, color(value) });
            }
        }
    }
    result->source = readback.source;
    result->sourceKey = readback.sourceKey;
    result->generation = readback.generation;
    return result;
}

} // namespace Raw
