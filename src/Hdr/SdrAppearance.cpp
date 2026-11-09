#include "SdrAppearance.h"

namespace Stack::Hdr {
namespace {
constexpr std::uint16_t table[]={
#include "Generated/SdrAppearanceLut.inc"
};
static_assert(sizeof(table)/sizeof(table[0])==SdrAppearanceEdge*SdrAppearanceEdge*SdrAppearanceEdge*3);
Rgb Sample(const std::array<int,3>& p) {
    const auto i=3*((p[2]*SdrAppearanceEdge+p[1])*SdrAppearanceEdge+p[0]);
    return {table[i]/65535.f,table[i+1]/65535.f,table[i+2]/65535.f};
}
}
const std::uint16_t* SdrAppearanceTable() { return table; }
Rgb SdrAppearance(Rgb hdr709) {
    auto rgb=To2020(hdr709);
    // HDR transport may carry signed Rec.709. Only negative Rec.2020 lies
    // outside the table's physical input gamut; compress that toward neutral.
    const float y=std::max(0.f,.2627f*rgb[0]+.6780f*rgb[1]+.0593f*rgb[2]);
    const float low=std::min({rgb[0],rgb[1],rgb[2]});
    const float chroma=low<0.f ? y/std::max(1e-8f,y-low) : 1.f;
    std::array<int,3> p{};
    Rgb f{};
    for(int c=0;c<3;++c) {
        const float v=std::clamp(y+(rgb[c]-y)*chroma,0.f,64.f);
        const float position=std::log2(1.f+4.f*std::sqrt(v))*(128.f/5.04439411936f);
        p[c]=std::min(int(position),127);
        f[c]=position-p[c];
    }
    // Four-corner tetrahedral interpolation preserves neutral diagonals, unlike
    // hardware trilinear interpolation through this curved appearance mapping.
    std::array<int,3> order{0,1,2};
    if(f[order[0]]<f[order[1]]) std::swap(order[0],order[1]);
    if(f[order[1]]<f[order[2]]) std::swap(order[1],order[2]);
    if(f[order[0]]<f[order[1]]) std::swap(order[0],order[1]);
    const auto a=Sample(p); ++p[order[0]];
    const auto b=Sample(p); ++p[order[1]];
    const auto c=Sample(p); ++p[order[2]];
    const auto d=Sample(p);
    Rgb result{};
    for(int k=0;k<3;++k) {
        const float value=a[k]+f[order[0]]*(b[k]-a[k])+f[order[1]]*(c[k]-b[k])+f[order[2]]*(d[k]-c[k]);
        result[k]=value*value;
    }
    return result;
}
}
