#include "HdrColorFusion.h"
#include "PreparedTileSampler.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
namespace {
using Color=std::array<double,4>;
using Moments=std::array<double,9>; // weight, weighted color and squared color
constexpr int radius=32; // CFA cells; an all-clipped interior beyond this stays unknown

double Fade(double x) {
    x=std::clamp(x,0.,1.);
    return x*x*x*(10+x*(-15+6*x));
}

bool Clipped(std::uint8_t flags) {
    return Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)||
        Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip);
}
bool Bad(std::uint8_t flags) {
    return Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Defective)||
        Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::DecoderRepaired);
}
std::size_t Site(std::size_t first,unsigned width,unsigned s) {
    return first+(s/2)*width+s%2;
}

// With just one reference channel a single RGB ratio cannot verify location.
// Use a chromatically uniform mapped patch only when nearby reference anchors
// independently predict the same scale. Borrow chromaticity, never detail or
// measurement support. A textured edge or a contradictory bound vetoes it.
bool TrySmoothDonor(std::vector<Observation>& reference,const std::vector<Observation>& donor,
    unsigned width,unsigned height,unsigned x,unsigned y,unsigned anchor) {
    if(width<10 || height<10)return false;
    const auto first=std::size_t(y)*width+x;
    const auto ai=Site(first,width,anchor);
    const auto& a=donor[ai];
    if(!a.finite || a.fallbackClipped || !(a.fallback>0 && a.exposure>0 &&
        a.exposure<reference[ai].exposure))return false;
    const double scale=reference[ai].value/a.fallback;
    if(!(scale>=.7071 && scale<=1.4142))return false;
    Color estimate{};
    for(unsigned s=0;s<4;++s) {
        const auto i=Site(first,width,s);
        estimate[s]=scale*donor[i].fallback;
        if(!(estimate[s]>0) || (s!=anchor && estimate[s]<reference[i].fallback))return false;
    }
    // A narrow edge may have good support only on its own side. Require one
    // coherent two-dimensional footprint with matched reference anchors.
    // Compare donor chromaticity, so genuine luminance texture is preserved.
    const auto coherent=[&](int sx,int sy) {
        if (int(x)+4*sx<0 || int(y)+4*sy<0 || int(x)+4*sx+1>=int(width) ||
            int(y)+4*sy+1>=int(height)) return false;
        unsigned verified=0;
        for (int dy=0;dy<=4;dy+=2) for (int dx=0;dx<=4;dx+=2) {
            const auto cell=std::size_t(int(y)+dy*sy)*width+(int(x)+dx*sx);
            const auto& ref=reference[Site(cell,width,anchor)];
            const double localAnchor=donor[Site(cell,width,anchor)].fallback;
            if (!(localAnchor>0)) return false;
            for(unsigned s=0;s<4;++s) {
                const auto& v=donor[Site(cell,width,s)];
                const double expected=donor[Site(first,width,s)].fallback*(localAnchor/a.fallback);
                if(!v.finite || v.fallbackClipped || v.exposure!=a.exposure ||
                    !(std::abs(v.fallback-expected)<=.10*expected))return false;
            }
            if(ref.support>0 && !ref.clipped) {
                if(std::abs(scale*localAnchor-ref.value)>.10*ref.value)return false;
                ++verified;
            }
        }
        return verified>=4;
    };
    if (!coherent(-1,-1) && !coherent(1,-1) && !coherent(-1,1) && !coherent(1,1))return false;
    for(unsigned s=0;s<4;++s)if(s!=anchor) {
        auto& v=reference[Site(first,width,s)];
        v.value=estimate[s];v.clippedReference=ClippedReference::EstimatedColor;
    }
    return true;
}

// Summed-area moments keep the cost bounded even for a large blown sky. Use
// only complete, bright, unclipped reference cells. The transition over the
// last stop of the sensor range suppresses unrelated dark background colors.
class BoundaryColors {
public:
    explicit BoundaryColors(const Mfd::PreparedRawTile& tile):
        width(static_cast<int>(tile.extent.width/2)),height(static_cast<int>(tile.extent.height/2)),
        stride(width+1),table(std::size_t(height+1)*stride) {
        for(int y=0;y<height;++y) {
            Moments row{};
            for(int x=0;x<width;++x) {
                Color color{};double peak=0;bool usable=true;
                const auto first=2*(std::size_t(y)*tile.extent.width+x);
                for(unsigned s=0;s<4;++s) {
                    const auto i=Site(first,tile.extent.width,s);
                    const auto v=tile.normalizedMosaic[i],gain=tile.comparisonGain[i];
                    if(Clipped(tile.sampleFlags[i])||Bad(tile.sampleFlags[i])||
                       !(v>0&&gain>0&&std::isfinite(v)&&std::isfinite(gain))){usable=false;break;}
                    peak=std::max(peak,double(v));color[s]=double(v)*gain;
                }
                if(usable&&peak>.5) {
                    const double w=(2*peak-1)*(2*peak-1),maxColor=*std::max_element(color.begin(),color.end());
                    row[0]+=w;
                    for(unsigned s=0;s<4;++s) {
                        const double v=color[s]/maxColor;
                        row[1+s]+=w*v;row[5+s]+=w*v*v;
                    }
                }
                const auto i=std::size_t(y+1)*stride+x+1;
                for(unsigned k=0;k<row.size();++k)table[i][k]=table[i-stride][k]+row[k];
            }
        }
    }
    bool HasSamples() const {return table.back()[0]>0;}
    bool Estimate(int x,int y,Color& mean,Color& deviation,double& confidence) const {
        Moments total{};
        // Nested normalized windows give nearby boundary samples more weight
        // without changing discontinuously when the nearest ring gains a sample.
        for(int r=1;r<=radius;r*=2) {
            const int l=std::max(0,x-r),t=std::max(0,y-r);
            const int b=std::min(height,y+r+1),e=std::min(width,x+r+1);
            const double w=1./(double(r)*r*r);
            for(unsigned k=0;k<total.size();++k)
                total[k]+=w*(table[std::size_t(b)*stride+e][k]-table[std::size_t(t)*stride+e][k]
                    -table[std::size_t(b)*stride+l][k]+table[std::size_t(t)*stride+l][k]);
        }
        if(total[0]<=1e-9)return false;
        for(unsigned s=0;s<4;++s) {
            mean[s]=total[1+s]/total[0];
            if(!(mean[s]>0))return false;
            deviation[s]=std::sqrt(std::max(0.,total[5+s]/total[0]-mean[s]*mean[s]));
        }
        // A finite search radius must fade into unresolved-color completion,
        // not draw a square where its last boundary sample leaves the window.
        // The same summed-area table locates the nearest usable boundary in
        // logarithmic time, independent of the clipped area's size.
        const auto weight=[&](int r) {
            const int l=std::max(0,x-r),t=std::max(0,y-r);
            const int b=std::min(height,y+r+1),e=std::min(width,x+r+1);
            return table[std::size_t(b)*stride+e][0]-table[std::size_t(t)*stride+e][0]
                -table[std::size_t(b)*stride+l][0]+table[std::size_t(t)*stride+l][0];
        };
        int lo=0,hi=radius;
        while(lo<hi) {
            const int middle=(lo+hi)/2;
            if(weight(middle)>1e-9)hi=middle;else lo=middle+1;
        }
        confidence=Fade((radius-lo)/(.75*radius));
        return true;
    }
private:
    int width,height,stride;
    std::vector<Moments> table;
};
}

bool PrepareHighlightFallback(const ProcessingRequest& request,const PreparedDataset& data,
    const Mfd::PreparedRawTile& origin,std::vector<std::vector<Observation>>& groups,std::string& error) {
    const auto& source=data.sources[data.origin];
    auto& reference=groups[source.group];
    const unsigned width=origin.extent.width,height=origin.extent.height;
    Color neutral{};
    Mfd::CfaLayout layout;bool hasNeutral=Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
    const auto& wb=source.metadata.cameraWhiteBalance;
    for(unsigned s=0;hasNeutral&&s<4;++s) {
        const auto site=layout.SiteAt(s%2,s/2);
        const double gain=wb[site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1];
        hasNeutral=gain>0&&std::isfinite(gain)&&wb[1]>0&&std::isfinite(wb[1]);
        if(hasNeutral)neutral[s]=wb[1]/gain;
    }
    std::vector<std::size_t> missing;
    int x0=width,y0=height,x1=0,y1=0;
    for(unsigned y=0;y+1<height;y+=2)for(unsigned x=0;x+1<width;x+=2) {
        const auto first=std::size_t(y)*width+x;unsigned missingSites=0;
        for(unsigned s=0;s<4;++s) {
            const auto i=Site(first,width,s);auto& v=reference[i];
            if(v.support>0||!Clipped(origin.sampleFlags[i])||Bad(origin.sampleFlags[i]))continue;
            const double floor=double(origin.normalizedMosaic[i])*origin.comparisonGain[i]*source.scale;
            if(!(floor>0&&std::isfinite(floor)))continue;
            bool supported=false;
            for(std::size_t g=0;g<groups.size();++g)if(request.recipe.groups[g].enabled) {
                const auto& o=groups[g][i];supported|=o.finite&&!o.clipped&&o.support>0;
            }
            if(!supported) {
                // The group may itself be a noise burst. Its chosen fallback
                // can be a different frame from the actual fixed reference.
                v.fallback=floor;v.exposure=1/source.scale;
                v.finite=true;v.clipped=v.fallbackClipped=true;
                v.clippedReference=ClippedReference::LowerBound;++missingSites;
            }
        }
        if(missingSites==4) {
            // Without even one measured color, boundary chromaticity is
            // unconstrained at the point. Publish the dimmest as-shot neutral
            // consistent with all four bounds, with clipping still reported.
            if(hasNeutral) {
                double brightness=0;
                for(unsigned s=0;s<4;++s)
                    brightness=std::max(brightness,reference[Site(first,width,s)].fallback/neutral[s]);
                for(unsigned s=0;s<4;++s) {
                    auto& v=reference[Site(first,width,s)];
                    v.value=std::max(v.fallback,brightness*neutral[s]);
                    v.clippedReference=ClippedReference::EstimatedColor;
                }
            }
        } else if(missingSites) {
            if(hasNeutral) {
                // A bounded neighborhood cannot provide chromaticity deep
                // inside a clipped highlight. Keep a separate last-resort
                // completion instead of publishing a capped channel beside
                // measured brighter colors (which invents magenta/other hues).
                // Preserve every measured site and every saturation bound.
                double brightness=0;
                for(unsigned s=0;s<4;++s) {
                    const auto i=Site(first,width,s);
                    const double raw=double(origin.normalizedMosaic[i])*origin.comparisonGain[i]*source.scale;
                    if(!Bad(origin.sampleFlags[i])&&std::isfinite(raw))
                        brightness=std::max(brightness,raw/neutral[s]);
                }
                for(unsigned s=0;s<4;++s) {
                    auto& v=reference[Site(first,width,s)];
                    if(v.clippedReference!=ClippedReference::LowerBound)continue;
                    v.value=std::max(v.fallback,brightness*neutral[s]);
                    v.clippedReference=ClippedReference::NeutralCompletion;
                }
            }
            if(missingSites==3) {
                unsigned anchor=0;
                while(anchor<4 && reference[Site(first,width,anchor)].clippedReference!=ClippedReference::None)++anchor;
                bool recovered=false;
                if(anchor<4 && reference[Site(first,width,anchor)].support>0)
                    for(std::size_t g=0;g<groups.size() && !recovered;++g)
                        if(g!=source.group && request.recipe.groups[g].enabled)
                            recovered=TrySmoothDonor(reference,groups[g],width,height,x,y,anchor);
                if(recovered)continue;
            }
            missing.push_back(first);x0=std::min(x0,int(x));y0=std::min(y0,int(y));
            x1=std::max(x1,int(x)+2);y1=std::max(y1,int(y)+2);
        }
    }
    if(missing.empty())return true;
    const int left=std::max(0,int(origin.originX)+x0-2*radius),top=std::max(0,int(origin.originY)+y0-2*radius);
    const int right=std::min(int(data.width),int(origin.originX)+x1+2*radius);
    const int bottom=std::min(int(data.height),int(origin.originY)+y1+2*radius);
    Mfd::PreparedRawTile region;PreparedTileSampler sampler(data.directory);
    if(!sampler.ReadRegion(source.frame,left,top,right-left,bottom-top,region,&error))return false;
    const BoundaryColors colors(region);
    if(!colors.HasSamples())return true;
    for(const auto first:missing) {
        const int x=(int(origin.originX)+int(first%width)-left)/2,y=(int(origin.originY)+int(first/width)-top)/2;
        Color hue{},spread{};double confidence=0;
        if(!colors.Estimate(x,y,hue,spread,confidence))continue;
        Color raw{};double cross=0,norm=0,lower=0;bool compatible=true;
        double mismatch=0;
        for(unsigned s=0;s<4;++s) {
            const auto i=Site(first,width,s);
            raw[s]=double(origin.normalizedMosaic[i])*origin.comparisonGain[i]*source.scale;
            if(Bad(origin.sampleFlags[i])||!std::isfinite(raw[s])){compatible=false;break;}
            if(Clipped(origin.sampleFlags[i]))lower=std::max(lower,raw[s]/hue[s]);
            else {cross+=raw[s]*hue[s];norm+=hue[s]*hue[s];}
        }
        const double scale=norm>0?cross/norm:lower;
        if(!compatible||!(scale>0&&std::isfinite(scale)))continue;
        // A compatible boundary must explain the surviving reference colors
        // and reach the observed saturation bounds. Use its actual color spread
        // and the measurement noise to avoid transferring hue across a color edge.
        for(unsigned s=0;s<4;++s) {
            const auto i=Site(first,width,s);const auto predicted=scale*hue[s];
            const double tolerance=3*std::hypot(scale*spread[s],std::sqrt(std::max(0.,reference[i].measurementVariance)));
            const double error=Clipped(origin.sampleFlags[i])?std::max(0.,raw[s]-predicted):std::abs(raw[s]-predicted);
            if(error>tolerance)compatible=false;
            mismatch=std::max(mismatch,error/std::max(1e-12,tolerance));
        }
        if(!compatible)continue;
        confidence*=Fade(2*(1-mismatch));
        for(unsigned s=0;s<4;++s) {
            auto& v=reference[Site(first,width,s)];
            if(v.clippedReference!=ClippedReference::LowerBound&&
               v.clippedReference!=ClippedReference::NeutralCompletion)continue;
            const double estimated=std::max(v.fallback,scale*hue[s]);
            if(v.clippedReference==ClippedReference::NeutralCompletion) {
                v.value+=confidence*(estimated-v.value);
                if(confidence==1)v.clippedReference=ClippedReference::EstimatedColor;
            } else {
                // Without calibrated white balance there is no neutral endpoint.
                v.value=estimated;v.clippedReference=ClippedReference::EstimatedColor;
            }
        }
    }
    return true;
}
} // namespace Raw::Bracketing
