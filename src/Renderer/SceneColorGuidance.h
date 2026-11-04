#pragma once

#include <cstdint>

namespace Stack::Renderer {

// CPU planning shares the prepared-evidence owner's two-entry GPU cap without
// importing OpenGL resource owners. Local filter scratch is budgeted separately.
inline constexpr std::uint64_t kMaximumPreparedSceneColorGuideBytes=10ull*1024*1024;

// Borrowed, immutable scene evidence for one execution. Both textures cover
// the complete input window in normalized coordinates and remain alive until
// that execution finishes. Color is straight scene-linear RGB in the same
// working space as the input; confidence is a separate finite [0,1] R channel.
// Color alpha is not confidence. Zero confidence means no usable evidence.
// A zero pair selects the consumer's ordinary, input-only behavior.
struct SceneColorGuidance {
    unsigned int colorTexture = 0;
    unsigned int confidenceTexture = 0;
    std::uint64_t revision = 0;

    bool IsAvailable() const noexcept {
        return colorTexture != 0 && confidenceTexture != 0;
    }
};

} // namespace Stack::Renderer
