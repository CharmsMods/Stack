#include "FloatMosaicImport.h"
#include "RawProcessingMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw {
bool ImportFloatDngMosaic(const float* samples,int stride,RawImageData& raw,
    const std::function<bool()>& cancel) {
    auto& m=raw.metadata;
    if(!samples||!m.isDng||m.rawWidth<=0||m.rawHeight<=0||stride<m.rawWidth||!m.dngLinearizationTable.empty()) {
        m.error="Floating-point DNG has invalid dimensions or an unsupported linearization table.";return false;
    }
    // WhiteLevel defaults to 1 for floating-point DNG (DNG 1.4 section 4).
    if(m.dngWhiteLevelValues.empty()){m.whiteLevel=1;m.dngWhiteLevelValues={1};}
    auto normalized=std::make_shared<std::vector<float>>(std::size_t(m.rawWidth)*m.rawHeight);
    const auto active=Processing::ResolveActiveArea(m);RawDevelopSettings settings;
    float minimum=std::numeric_limits<float>::infinity(),maximum=-minimum;
    for(int y=0;y<m.rawHeight;++y) {
        if(cancel&&cancel()){m.error="RAW load canceled.";return false;}
        for(int x=0;x<m.rawWidth;++x) {
            const int color=Processing::CfaColorAt(m,active,x,y);
            const float black=Processing::BlackLevelAt(m,settings,active,x,y,color);
            const float white=Processing::WhiteLevelForColor(m,settings,color);
            const float sample=samples[std::size_t(y)*stride+x];
            const float value=(sample-black)/(white-black);
            if(!std::isfinite(sample)||!std::isfinite(value)||!(white>black)) {
                m.error="Floating-point DNG contains invalid samples or black/white levels.";return false;
            }
            (*normalized)[std::size_t(y)*m.rawWidth+x]=value;
            minimum=std::min(minimum,sample);maximum=std::max(maximum,sample);
        }
    }
    raw.normalizedMosaicBuffer=std::move(normalized);
    raw.normalizedMosaicContentHash=raw.contentIdentityHash;
    raw.normalizedMosaicInputContract=NormalizedMosaicInputContract::FloatDngPreGain;
    m.rawMinimum=minimum;m.rawMaximum=maximum;m.bitDepth=32;m.uploadFormat="R32F";
    return true;
}
}
