#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>
namespace Raw {
// Camera-linear RGBA, bottom-up rows. Sample coordinates are top-left normalized.
inline bool EstimateWhiteBalance(const std::vector<float>& pixels,int w,int h,
    const std::array<float,3>& neutral,std::optional<std::array<float,2>> sample,
    std::array<float,3>& gains,std::string& error) {
    if(w<=0||h<=0||pixels.size()!=std::size_t(w)*h*4){error="White balance image is incomplete.";return false;}
    double sum[3]{};int count=0;
    const int cx=sample?std::clamp(int((*sample)[0]*w),0,w-1):0;
    const int cy=sample?std::clamp(int((1-(*sample)[1])*h),0,h-1):0;
    for(int y=sample?std::max(0,cy-3):0;y<(sample?std::min(h,cy+4):h);++y)
    for(int x=sample?std::max(0,cx-3):0;x<(sample?std::min(w,cx+4):w);++x) {
        const auto* p=&pixels[(std::size_t(y)*w+x)*4];
        if(p[3]<.99f || !std::isfinite(p[0]+p[1]+p[2]) ||
           std::min({p[0],p[1],p[2]})<.0001f || std::max({p[0],p[1],p[2]})>=.98f)continue;
        for(int c=0;c<3;++c)sum[c]+=p[c];++count;
    }
    if(count<4){error="Choose an unclipped neutral area with more light.";return false;}
    for(int c=0;c<3;++c) {
        const double gain=(neutral[c]/sum[c])/(neutral[1]/sum[1]);
        if(!std::isfinite(gain)||gain<.05||gain>16){error="This area does not give a usable neutral balance.";return false;}
        gains[c]=float(gain);
    }
    return true;
}
}
