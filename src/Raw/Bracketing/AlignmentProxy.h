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
         sums(static_cast<std::size_t>(width)*height),flags(sums.size()) {}

    static double Center(unsigned stride) {return .5*(stride-1);}

    void Add(unsigned x,unsigned y,unsigned channel,double value,bool clipped,bool invalid) {
        const unsigned px=x/stride,py=y/stride;
        if(px>=width||py>=height) return;
        const auto p=static_cast<std::size_t>(py)*width+px;
        if(invalid||!std::isfinite(value)) flags[p]|=2;
        else sums[p][channel]+=value;
        if(clipped) flags[p]|=1;
    }

    void Finish(std::vector<float>& rgb,std::vector<std::uint8_t>& clipped) const {
        rgb.resize(sums.size()*3);clipped.resize(sums.size());
        const double sitesPerColor=double(stride)*stride/4;
        for(std::size_t p=0;p<sums.size();++p) {
            clipped[p]=flags[p]!=0;
            for(unsigned c=0;c<3;++c)
                rgb[p*3+c]=(flags[p]&2)?std::numeric_limits<float>::quiet_NaN():
                    static_cast<float>(sums[p][c]/(sitesPerColor*(c==1?2:1)));
        }
    }

    const unsigned width,height,stride;
private:
    std::vector<std::array<double,3>> sums;
    std::vector<std::uint8_t> flags;
};
} // namespace Raw::Bracketing
