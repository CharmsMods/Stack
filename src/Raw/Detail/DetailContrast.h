#pragma once
#include "ThirdParty/json.hpp"
#include <array>

namespace Stack::RawRecipe {
inline constexpr int kDetailBands=8;
inline constexpr int kDetailEvSamples=17;
inline constexpr float kDetailMinEv=-12;
inline constexpr float kDetailMaxEv=12;
struct DetailContrast {
    bool enabled=true;
    float maximumScale=256; // Source pixels, before output geometry.
    float edgeProtection=0.75f;
    bool targetEnabled=false;
    float targetEv=0;
    float targetHalfWidthEv=2;
    float targetFeatherEv=2;
    std::array<float,kDetailBands> scaleGains{1,1,1,1,1,1,1,1};
    // EV-major residual to the scale response. Neutral is zero residual,
    // whereas the resulting contrast gain is one.
    std::array<float,kDetailBands*kDetailEvSamples> evScaleResidual{};
};
DetailContrast SanitizeDetailContrast(DetailContrast value);
DetailContrast ReadDetailContrast(const nlohmann::json& value);
nlohmann::json SerializeDetailContrast(const DetailContrast& value);
bool IsDetailContrastActive(const DetailContrast& value);
float DetailBandScale(const DetailContrast& value,int band);
float EvaluateDetailGain(const DetailContrast& value,int band,float inputEv);
std::array<float,kDetailBands*kDetailEvSamples> BuildDetailGainMap(const DetailContrast& value);
// Macros modify the authored response, not an additional serial operation.
void AdjustDetailMacro(DetailContrast& value,int firstBand,int lastBand,float delta);
}
