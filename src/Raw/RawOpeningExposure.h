#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace Stack::RawExposure {

struct OpeningLuminance {
    float median = 0.f;
    float upperMidtone = 0.f;
    float white = 0.f;
};

// The caller supplies finite positive scene luminances with valid coverage.
inline OpeningLuminance MeasureOpeningLuminance(std::vector<float>& samples) {
    if (samples.size() < 128) return {};
    const auto percentile = [&](std::size_t percent) {
        const auto position = samples.begin() + (samples.size() - 1) * percent / 100;
        std::nth_element(samples.begin(), position, samples.end());
        return *position;
    };
    return {percentile(50), percentile(90), percentile(99)};
}

inline float EstimateOpeningEv(const OpeningLuminance& light, float whiteLimit = .72f) {
    if (light.median < .012f || (light.median < .04f && light.upperMidtone >= .18f))
        return 0.f;
    return std::max(0.f, std::min({1.f, std::log2(.12f / light.median),
        std::log2(.36f / light.upperMidtone), std::log2(whiteLimit / light.white)}));
}

} // namespace Stack::RawExposure
