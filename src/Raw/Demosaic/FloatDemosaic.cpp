#include "FloatDemosaicInternal.h"
#include "Raw/RawProcessingMath.h"
#include <limits>
#include <new>

namespace Raw::Demosaic {
namespace {
using namespace Internal;
float SmoothStep(float low,float high,float value) {
    const float t=std::clamp((value-low)/(high-low),0.f,1.f);
    return t*t*(3-2*t);
}
void LinearMethod(const Input& in, DemosaicMethod method, std::vector<float>& rgb) {
    rgb.resize(in.samples.size()*3);
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            std::array<float,3> value{};
            if(method==DemosaicMethod::MalvarHeCutler)
                value=Processing::DemosaicMalvarHeCutlerAt(in.samples,in.width,in.height,in.pattern,x,y);
            else if(method==DemosaicMethod::NearestNeighbor)
                value=Processing::DemosaicNearestNeighborAt(in.samples,in.width,in.height,in.pattern,x,y);
            else for(int c=0;c<3;++c) {
                if(in.Cfa(x,y)==c) { value[c]=in.At(x,y); continue; }
                float sum=0; int count=0;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
                    if(in.Cfa(x+dx,y+dy)==c) { sum+=in.At(x+dx,y+dy); ++count; }
                value[c]=sum/count;
            }
            const auto i=(static_cast<std::size_t>(y)*in.width+x)*3;
            std::copy(value.begin(),value.end(),rgb.begin()+i);
        }
    }
}
float DetailAt(const Input& in, int x,int y) {
    float energy=0,level=0;
    // Compare only equal CFA colors. A colored flat field is not an edge.
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        const float c=in.At(x+dx,y+dy);
        energy+=std::abs(in.At(x+dx-2,y+dy)-in.At(x+dx+2,y+dy))+
            std::abs(in.At(x+dx,y+dy-2)-in.At(x+dx,y+dy+2));
        level+=std::abs(c);
    }
    return SmoothStep(0.025f,0.20f,energy/(2*level+0.02f));
}
void Combined(const Input& in,DemosaicMethod method,std::vector<float>& rgb) {
    std::vector<float> smooth;
    if(method==DemosaicMethod::MalvarHamilton) {
        LinearMethod(in,DemosaicMethod::MalvarHeCutler,smooth);
        Directional(in,DemosaicMethod::HamiltonAdams,rgb);
    } else {
        Lmmse(in,smooth);
        if(method==DemosaicMethod::HamiltonLmmse)
            Directional(in,DemosaicMethod::HamiltonAdams,rgb);
        else Dcb(in,true,rgb);
    }
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            if(method==DemosaicMethod::DcbLmmseChroma) {
                rgb[3*i]=rgb[3*i+1]+smooth[3*i]-smooth[3*i+1];
                rgb[3*i+2]=rgb[3*i+1]+smooth[3*i+2]-smooth[3*i+1];
                continue;
            }
            // Brightness is in normalized, white-balanced camera values.
            const float brightness=0.25f*(smooth[3*i]+2*smooth[3*i+1]+smooth[3*i+2]);
            float weight=method==DemosaicMethod::DcbLmmseShadows ?
                SmoothStep(0.015f,0.15f,brightness) : DetailAt(in,x,y);
            if(method==DemosaicMethod::DcbLmmseMalvar)
                weight*=SmoothStep(0.01f,0.10f,brightness);
            for(int c=0;c<3;++c) rgb[3*i+c]=smooth[3*i+c]+weight*(rgb[3*i+c]-smooth[3*i+c]);
            if(method==DemosaicMethod::DcbLmmseMalvar) {
                const float highlight=SmoothStep(0.75f,1.05f,brightness);
                if(highlight>0) {
                    const auto mhc=Processing::DemosaicMalvarHeCutlerAt(in.samples,in.width,in.height,in.pattern,x,y);
                    for(int c=0;c<3;++c) rgb[3*i+c]+=highlight*(mhc[c]-rgb[3*i+c]);
                }
            }
        }
    }
}
} // namespace

bool Reconstruct(const std::vector<float>& mosaic,int width,int height,CfaPattern pattern,
    DemosaicMethod method,std::vector<float>& rgb,std::string& error,const std::function<bool()>& cancel,
    const std::array<float,9>& cameraToSrgb) {
    error.clear();
    const auto count=width>0 && height>0 ? static_cast<std::size_t>(width)*height : 0;
    if(width<2 || height<2 || count!=mosaic.size() || count>static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        pattern==CfaPattern::Unknown) {
        error="Demosaicing requires a valid Bayer mosaic of at least 2 by 2 pixels.";
        return false;
    }
    try {
        const Input input{mosaic,width,height,pattern,cancel,cameraToSrgb};
        input.Check();
        switch(method) {
            case DemosaicMethod::Bilinear:
            case DemosaicMethod::NearestNeighbor:
            case DemosaicMethod::MalvarHeCutler: LinearMethod(input,method,rgb); break;
            case DemosaicMethod::HamiltonAdams:
            case DemosaicMethod::EdgeWeighted:
            case DemosaicMethod::SmoothHue: Directional(input,method,rgb); break;
            case DemosaicMethod::Lmmse: Lmmse(input,rgb); break;
            case DemosaicMethod::LmmseRefined:
                Lmmse(input,rgb); RefineChroma(input,rgb,2); break;
            case DemosaicMethod::Dcb: Dcb(input,false,rgb); break;
            case DemosaicMethod::DcbEnhanced: Dcb(input,true,rgb); break;
            case DemosaicMethod::Ppg: Ppg(input,rgb); break;
            case DemosaicMethod::Vng: Vng(input,false,rgb); break;
            case DemosaicMethod::VngFourColor: Vng(input,true,rgb); break;
            case DemosaicMethod::Ahd: Ahd(input,rgb); break;
            case DemosaicMethod::MalvarHamilton:
            case DemosaicMethod::HamiltonLmmse:
            case DemosaicMethod::DcbLmmseDetail:
            case DemosaicMethod::DcbLmmseShadows:
            case DemosaicMethod::DcbLmmseChroma:
            case DemosaicMethod::DcbLmmseMalvar: Combined(input,method,rgb); break;
            default: error="Unknown demosaicing method."; return false;
        }
        for(int y=0;y<height;++y) {
            input.Check(y);
            for(int x=0;x<width;++x) {
                const auto i=static_cast<std::size_t>(y)*width+x;
                // Interpolation never changes the recorded channel sample.
                rgb[3*i+input.Cfa(x,y)]=mosaic[i];
                for(int c=0;c<3;++c) if(!std::isfinite(rgb[3*i+c])) {
                    error="Demosaicing produced a non-finite sample.";
                    rgb.clear(); return false;
                }
            }
        }
        return true;
    } catch(const Canceled&) { error="Demosaicing canceled."; }
      catch(const std::bad_alloc&) { error="Not enough memory for this demosaicing method at the source resolution."; }
      catch(const std::exception& e) { error=std::string("Demosaicing failed: ")+e.what(); }
    rgb.clear();
    return false;
}
} // namespace Raw::Demosaic
