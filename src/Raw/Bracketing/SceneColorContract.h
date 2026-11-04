#pragma once
#include "Raw/SceneColorGuide.h"
#include <array>
#include <limits>

namespace Raw::Bracketing {
inline constexpr const char* kSceneColorEvidenceRevision="source-color-noise-support-v1";
// Public planning/identity contract only. Desktop resource planning must not
// depend on the worker's observation estimator or HDR fusion implementation.
namespace SceneColorEvidence {
inline constexpr std::size_t kAccumulatorBytes=14*sizeof(double);
inline constexpr std::size_t kTileDescriptorBytes=8*sizeof(unsigned)+4*sizeof(void*);
inline std::array<unsigned,2> Dimensions(unsigned sensorWidth,unsigned sensorHeight) {
    const unsigned w=sensorWidth/2,h=sensorHeight/2,longest=std::max(w,h);
    if(!w||!h)return {};
    const auto side=SceneColorGuide::MaximumDimension;
    if(longest<=side)return {w,h};
    return {std::max(1u,unsigned(std::uint64_t(w)*side/longest)),
        std::max(1u,unsigned(std::uint64_t(h)*side/longest))};
}
inline std::uint64_t WorkingBytes(unsigned sensorWidth,unsigned sensorHeight,unsigned tilePixels) {
    const auto size=Dimensions(sensorWidth,sensorHeight);if(!tilePixels||!size[0]||!size[1])return 0;
    const long double columns=(std::uint64_t(sensorWidth)+tilePixels-1)/tilePixels;
    const long double rows=(std::uint64_t(sensorHeight)+tilePixels-1)/tilePixels;
    const long double count=static_cast<long double>(size[0])*size[1];
    const long double bytes=count*8*sizeof(float)+
        (count+(size[0]+columns)*(size[1]+rows))*kAccumulatorBytes+columns*rows*kTileDescriptorBytes;
    return bytes>=std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():std::uint64_t(bytes);
}
}
}
