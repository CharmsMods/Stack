#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw {
// Immutable, source-derived color evidence. This is guidance for downstream
// processing, never image pixels, an edit mask, geometric alpha or denoise input.
struct SceneColorGuide {
    static constexpr unsigned MaximumDimension=512;
    unsigned width=0,height=0,sensorWidth=0,sensorHeight=0;
    std::string identity;
    // Top-down, before EXIF orientation, over the complete native active sensor
    // rectangle [0,sensorWidth) x [0,sensorHeight), with no RAW margins. Guide
    // pixel centers are ((x+.5)*sensorWidth/width,(y+.5)*sensorHeight/height).
    // Reconstruction/proxy resizing retains this native reference rectangle.
    // RGB is linear normalized camera color after pointwise calibration and
    // exposure alignment, before white balance and camera conversion. It has
    // the reconstructedCameraRgb contract: do not apply gain maps again.
    std::vector<float> cameraRgb;
    // Interleaved RGB conservative marginal measurement + model uncertainty
    // in cameraRgb squared units. Cross-channel/spatial covariance is unknown;
    // this is not a calibrated posterior covariance or a denoise strength.
    std::vector<float> variance;
    // Suitability for color guidance, conditional on actual source support.
    // Includes noise, admitted-source disagreement and compact-grid color
    // mixing. This score is not a probability or an object classification.
    std::vector<float> reliability;
    // Fraction of the footprint with measured, unclipped support in all RGB
    // channels. Inferred highlight/fallback colors have zero support. Separate
    // from reliability and from output geometric coverage; consumers use both.
    std::vector<float> coverage;

    bool Valid() const {
        if(!width||!height||width>MaximumDimension||height>MaximumDimension||
            !sensorWidth||!sensorHeight||width>sensorWidth||height>sensorHeight||identity.empty())return false;
        const auto count=std::size_t(width)*height;
        if(cameraRgb.size()!=count*3||variance.size()!=count*3||
            reliability.size()!=count||coverage.size()!=count)return false;
        return std::all_of(cameraRgb.begin(),cameraRgb.end(),[](float v){return std::isfinite(v);})&&
            std::all_of(variance.begin(),variance.end(),[](float v){return std::isfinite(v)&&v>=0;})&&
            std::all_of(reliability.begin(),reliability.end(),[](float v){return std::isfinite(v)&&v>=0&&v<=1;})&&
            std::all_of(coverage.begin(),coverage.end(),[](float v){return std::isfinite(v)&&v>=0&&v<=1;});
    }
    std::uint64_t ResidentBytes() const {
        return (std::uint64_t(cameraRgb.capacity())+variance.capacity()+reliability.capacity()+coverage.capacity())*sizeof(float);
    }
};
}
