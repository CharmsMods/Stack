#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace Stack::Assets {
// Coverage is an area quantity. Downsampling integrates each destination pixel's
// footprint; upsampling interpolates at pixel centers. Both axes use the same rule.
inline std::vector<float> ResampleMaskCoverage(const float* source, int sw, int sh, int dw, int dh, bool flipY=false) {
    if (!source || sw<=0 || sh<=0 || dw<=0 || dh<=0) return {};
    std::vector<float> result(std::size_t(dw)*dh);
    if (sw==dw && sh==dh) {
        for(int y=0;y<dh;++y) std::copy_n(source+std::size_t(flipY?sh-1-y:y)*sw,sw,result.data()+std::size_t(y)*dw);
        return result;
    }
    struct Weight {int index;double value;};
    const auto weights=[](int from,int to) {
        std::vector<std::vector<Weight>> axis(to);
        const double scale=double(from)/to;
        for(int p=0;p<to;++p) {
            if(to<from) {
                const double first=p*scale,last=(p+1)*scale;
                for(int i=int(std::floor(first));i<std::min(from,int(std::ceil(last)));++i)
                    axis[p].push_back({i,(std::min(last,double(i+1))-std::max(first,double(i)))/scale});
            } else {
                const double center=(p+.5)*scale-.5;const int left=int(std::floor(center));const double t=center-left;
                axis[p].push_back({std::clamp(left,0,from-1),1-t});
                axis[p].push_back({std::clamp(left+1,0,from-1),t});
            }
        }
        return axis;
    };
    const auto horizontal=weights(sw,dw),vertical=weights(sh,dh);
    std::vector<float> rows(std::size_t(dw)*sh);
    for(int y=0;y<sh;++y) for(int x=0;x<dw;++x) {
        double value=0;for(const auto& w:horizontal[x]) value+=source[std::size_t(y)*sw+w.index]*w.value;
        rows[std::size_t(y)*dw+x]=float(value);
    }
    for(int y=0;y<dh;++y) for(int x=0;x<dw;++x) {
        double value=0;for(const auto& w:vertical[flipY?dh-1-y:y]) value+=rows[std::size_t(w.index)*dw+x]*w.value;
        result[std::size_t(y)*dw+x]=std::clamp(float(value),0.f,1.f);
    }
    return result;
}
}
