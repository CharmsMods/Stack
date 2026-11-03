#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

class FullscreenQuad;

namespace Stack::Renderer {

// A scene-log-luminance guided-filter field, owned and used on one GL context.
// The input must be the complete logical image with straight scene-linear RGB.
// Radius is a fraction of its longest axis; the analysis grid has at most 512
// samples along that axis. Source-pixel moments retain small highlights and
// alpha coverage when reducing the image to that grid.
//
// This bounded-grid approximation can smooth weak edges and differs when the
// upstream image itself is resampled. It is not a guarantee against tone halos.
// The caller owns tone-curve application and full-image viewport dependencies.
class LocalToneGuide {
public:
    static constexpr int kMaximumAnalysisDimension = 512;

    // CPU planning uses exactly the same bounded geometry as execution. The
    // returned field is one of these three surfaces, not a fourth allocation.
    static std::pair<int, int> AnalysisExtent(int width, int height) noexcept {
        if (width <= 0 || height <= 0) return {0, 0};
        const double scale = std::min(1.0, double(kMaximumAnalysisDimension) / std::max(width, height));
        return {std::max(1, int(std::lround(width * scale))),
            std::max(1, int(std::lround(height * scale)))};
    }
    static std::uint64_t WorkingBytes(int width, int height) noexcept {
        const auto extent = AnalysisExtent(width, height);
        return std::uint64_t(extent.first) * extent.second * 3u * 16u;
    }

    LocalToneGuide() = default;
    LocalToneGuide(const LocalToneGuide&) = delete;
    LocalToneGuide& operator=(const LocalToneGuide&) = delete;
    ~LocalToneGuide();

    // Returns an owned RGBA32F texture, or zero on failure/cancellation.
    // R,G are the smoothed coefficients a,b multiplied by coverage; B is that
    // coverage and A is reserved. After bilinear sampling, divide RG by B when
    // B > 0, then evaluate baseEV = a * log2(Y / .18) + b. With no coverage use
    // the original EV. No tone curve or RGB modification happens in this owner.
    // Finite parameters are limited to radius .002.. .25 and range .05..2 EV.
    // All calls, result deletion and destruction require the owning GL context.
    unsigned int Render(unsigned int inputTexture, int width, int height,
        const std::array<float, 3>& lumaWeights, float radiusFraction, float rangeEv,
        FullscreenQuad& quad, const std::function<bool()>& shouldCancel = {},
        std::string* error = nullptr);

private:
    bool Ensure(std::string* error);
    unsigned int m_InitializeProgram = 0;
    unsigned int m_BoxProgram = 0;
};

} // namespace Stack::Renderer
