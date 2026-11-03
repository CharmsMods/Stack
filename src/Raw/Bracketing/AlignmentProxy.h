#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Raw::Bracketing {
// Search/exposure evidence only. Average complete, even Bayer blocks before
// decimation; the reconstruction continues to sample the original RAW tiles.
// Omitting a partial trailing block keeps every proxy sample on one regular
// sensor grid, including when the sensor dimensions are not stride multiples.
class AlignmentProxyAccumulator {
public:
    AlignmentProxyAccumulator(unsigned rawWidth,unsigned rawHeight,unsigned stride)
        :width(rawWidth/stride),height(rawHeight/stride),stride(stride),
         sums(static_cast<std::size_t>(width)*height),counts(sums.size()),flags(sums.size()) {}

    static double Center(unsigned stride) {return .5*(stride-1);}

    void Add(unsigned x,unsigned y,unsigned channel,double value,bool clipped,bool invalid) {
        const unsigned px=x/stride,py=y/stride;
        if(px>=width||py>=height||channel>=3) return;
        const auto p=static_cast<std::size_t>(py)*width+px;
        if(!invalid&&std::isfinite(value)) {
            sums[p][channel]+=value;
            ++counts[p][channel];
        }
        if(clipped) flags[p]|=static_cast<std::uint8_t>(1u<<channel);
    }

    void Finish(std::vector<float>& rgb,std::vector<std::uint8_t>& clipped,
        std::vector<std::uint8_t>* calibrationMask=nullptr) const {
        rgb.resize(sums.size()*3);clipped.resize(sums.size());
        if(calibrationMask) calibrationMask->resize(sums.size());
        const double sitesPerColor=double(stride)*stride/4;
        for(std::size_t p=0;p<sums.size();++p) {
            unsigned supported=0;
            for(unsigned c=0;c<3;++c) {
                const double expected=sitesPerColor*(c==1?2:1);
                const auto count=counts[p][c];
                // A sparse sensor defect must not erase a whole coarse block,
                // or its other colors, as the proxy stride grows with a burst.
                // Retain only nearly complete footprints (at most 1% missing)
                // so partial coverage cannot stand in for a differently placed
                // scene patch. Missing samples are never counted as black.
                const bool usable=count>0&&count>=.99*expected;
                rgb[p*3+c]=usable?static_cast<float>(sums[p][c]/count):
                    std::numeric_limits<float>::quiet_NaN();
                if(usable&&!(flags[p]&(1u<<c))) supported|=1u<<c;
            }
            // Luminance/affine callers require a complete unclipped color;
            // exposure fitting may still use independently supported channels.
            clipped[p]=supported!=7;
            if(calibrationMask) (*calibrationMask)[p]=static_cast<std::uint8_t>(supported);
        }
    }

    const unsigned width,height,stride;
private:
    std::vector<std::array<double,3>> sums;
    std::vector<std::array<std::uint32_t,3>> counts;
    std::vector<std::uint8_t> flags;
};
} // namespace Raw::Bracketing
