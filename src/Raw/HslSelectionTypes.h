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
    // Negative chroma preserves the hue-only behavior of existing selections.
    float sourceChroma=-1, chromaReach=.09f;
    bool warpMask=false;
    bool allColors=false;
    bool neutralOnly=false;
    bool independentChannels=false; // Wheel ranges override only channels they edit.
    std::array<float,3> grade{}; // Normalized Oklab tint a/b and tonal brightness.
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
