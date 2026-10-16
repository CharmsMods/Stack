#include "ReconstructionDepth.h"
#include <algorithm>
#include <cmath>

namespace Raw {
DepthSample SampleReconstructionDepth(const ReconstructionDepth& map,double x,double y) {
    if(!map.Available()||!std::isfinite(x)||!std::isfinite(y))return {};
    const double u=std::clamp((x+.5)*map.width/map.sensorWidth-.5,0.,double(map.width-1));
    const double v=std::clamp((y+.5)*map.height/map.sensorHeight-.5,0.,double(map.height-1));
    const unsigned ix=unsigned(u),iy=unsigned(v);const double fx=u-ix,fy=v-iy;
    double sum=0,depth=0,confidence=0,nearest=0,farthest=1;
    for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx) {
        const auto p=std::size_t(std::min(iy+dy,map.height-1))*map.width+std::min(ix+dx,map.width-1);
        const double w=(dx?fx:1-fx)*(dy?fy:1-fy),c=map.confidence[p];
        if(c<.25||w<1e-6)continue;
        nearest=std::max(nearest,double(map.inverseDepth[p]));
        farthest=std::min(farthest,double(map.inverseDepth[p]));
        sum+=w;depth+=w*map.inverseDepth[p];confidence+=w*c;
    }
    // Do not invent an intermediate surface by interpolating across an
    // occlusion boundary. The renderer keeps these unresolved pixels sharp.
    if(sum<.75||nearest-farthest>.15)return {};
    return {float(depth/sum),float(confidence)};
}
} // namespace Raw
