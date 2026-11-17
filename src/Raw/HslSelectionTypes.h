#pragma once
#include <array>
#include <string>
#include <vector>

namespace Stack::RawRecipe {
inline constexpr int kMaxHslSelections=16;
inline constexpr int kMaxHslBands=16;
inline constexpr int kMaxHslOutlinePoints=128;
struct HslBrightnessBand {
    float low=-2,high=2,softness=.5f;
};
// Authored in the complete incoming image, never viewport coordinates.
struct HslSelection {
    std::string id,name="Selection";
    bool enabled=true;
    float hue=0,hueWidth=25,hueSoftness=15;
    bool allColors=false;
    std::vector<HslBrightnessBand> bands,excludedBands;
    std::vector<std::array<float,2>> outline;
    bool areaEnabled=false;
    float areaSoftness=.01f;
    float influence=1;
    std::array<bool,3> own{};
    // A frozen global curve preserves hue interpolation when a channel is
    // detached. These are adjustment values, not an additional image pass.
    std::array<std::array<float,3>,8> values{};
};
}
