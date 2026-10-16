#include "Raw/RawAssetCapture.h"
#include "Raw/StrokeValueMatcher.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <cstdint>
#include <stdexcept>

namespace Stack::RawAssetCapture {
namespace {
struct Sample { int x,y; std::array<float,4> rgba; std::array<float,3> values; };
void Check(const std::function<bool()>& canceled) {if(canceled && canceled()) throw std::runtime_error("Capture canceled.");}
template<class Visitor> void Walk(const std::vector<std::array<float,2>>& path,int w,int h,Visitor visit) {
    int previousX=-1,previousY=-1;
    const auto pixel=[](float u,int extent){
        if(!std::isfinite(u) || u<0 || u>1) throw std::runtime_error("Invalid stroke coordinates.");
        return std::min(extent-1,int(u*extent));
    };
    for(size_t i=0;i<path.size();++i) {
        int x=pixel(path[i?i-1:0][0],w),y=pixel(path[i?i-1:0][1],h);
        const int x2=pixel(path[i][0],w),y2=pixel(path[i][1],h);
        const int dx=std::abs(x2-x),dy=-std::abs(y2-y),sx=x<x2?1:-1,sy=y<y2?1:-1;
        int err=dx+dy;
        for(;;) {
            // Remove adjacent duplicate pixels, but retain later revisits in order.
            if(x!=previousX || y!=previousY) {visit(x,y);previousX=x;previousY=y;}
            if(x==x2 && y==y2)break;
            const int twice=err*2;if(twice>=dy){err+=dy;x+=sx;}if(twice<=dx){err+=dx;y+=sy;}
        }
    }
}
float Luma(const std::array<float,4>& rgba,Raw::RawWorkingSpace space) {
    return space==Raw::RawWorkingSpace::LinearRec2020D65?
        rgba[0]*.2627002f+rgba[1]*.6779981f+rgba[2]*.0593017f:
        rgba[0]*.2126729f+rgba[1]*.7151522f+rgba[2]*.0721750f;
}
bool Read(const std::vector<float>& rgba,int w,int h,int x,int y,Raw::RawWorkingSpace space,Sample& s,bool readColor=true) {
    s.x=x;s.y=y;const size_t at=(size_t(h-1-y)*w+x)*4;
    for(int c=0;c<4;++c) {s.rgba[c]=rgba[at+c];if(!std::isfinite(s.rgba[c]))return false;}
    if(s.rgba[3]<=0)return false;
    s.values={Luma(s.rgba,space),0,0};
    if (readColor) {
        const auto color=RawRecipe::WorkingRgbToColorWarpCoordinate({s.rgba[0],s.rgba[1],s.rgba[2]},space);
        s.values[1]=color.a;s.values[2]=color.b;
    }
    return std::all_of(s.values.begin(),s.values.end(),[](float f){return std::isfinite(f);});
}

}
Assets::Record CaptureStroke(const std::vector<float>& rgba,int w,int h,
    Raw::RawWorkingSpace space,const Selection& selection,const std::function<bool()>& canceled) {
    if(w<=0 || h<=0 || rgba.size()!=size_t(w)*h*4 || selection.path.empty())throw std::runtime_error("The stroke has no source pixels.");
    std::vector<Sample> values;
    Walk(selection.path,w,h,[&](int x,int y){Check(canceled);Sample s;if(Read(rgba,w,h,x,y,space,s))values.push_back(s);});
    if(values.empty())throw std::runtime_error("The stroke contains no valid pixels.");
    auto samples=std::make_shared<Assets::FloatData>();
    samples->width=int(values.size());samples->height=3;samples->channels=4;
    samples->units="rows: u,v,Y,EV; linear-R,G,B,alpha; Oklab-a,b,chroma,EV-valid";
    samples->values.resize(values.size()*12);
    for(size_t i=0;i<values.size();++i) {
        const auto& s=values[i];const float Y=s.values[0];
        const float first[4]{(s.x+.5f)/w,(s.y+.5f)/h,Y,Y>0?std::log2(Y/.18f):0};
        const float last[4]{s.values[1],s.values[2],std::hypot(s.values[1],s.values[2]),Y>0?1.f:0.f};
        std::copy(first,first+4,samples->values.begin()+i*4);
        std::copy(s.rgba.begin(),s.rgba.end(),samples->values.begin()+(values.size()+i)*4);
        std::copy(last,last+4,samples->values.begin()+(values.size()*2+i)*4);
    }
    Assets::Record out;out.samples=samples;out.kind=Assets::Kind::DataRange;
    out.provenance={{"captureVersion",3},{"behavior","baked-stroke"},{"workingSpace",int(space)},
        {"width",w},{"height",h},{"path",selection.path},{"exclusion",selection.exclusion},
        {"output",int(selection.output)},{"sampling","ordered native pixels, adjacent duplicates removed, transparent and invalid pixels excluded"}};
    if(selection.output==Output::Samples) {out.data=samples;return out;}
    auto data=std::make_shared<Assets::FloatData>();
    const bool strip=selection.output==Output::LumaStrip || selection.output==Output::ChromaStrip || selection.output==Output::ColorStrip;
    const bool image=selection.output==Output::LumaImage || selection.output==Output::ChromaImage;
    if(strip || image) {
        data->width=strip?int(values.size()):w;data->height=strip?1:h;
        data->channels=selection.output==Output::ColorStrip?3:(selection.output==Output::ChromaStrip || selection.output==Output::ChromaImage?2:1);
        data->units=data->channels==3?"linear-R,G,B":data->channels==2?"Oklab-a,Oklab-b":"linear-Y";
        data->values.resize(size_t(data->width)*data->height*data->channels);
        const auto write=[&](const Sample& s,size_t i) {
            if(data->channels==3)std::copy(s.rgba.begin(),s.rgba.begin()+3,data->values.begin()+i*3);
            else if(data->channels==2){data->values[i*2]=s.values[1];data->values[i*2+1]=s.values[2];}
            else data->values[i]=s.values[0];
        };
        if(strip)for(size_t i=0;i<values.size();++i)write(values[i],i);
        else for(int y=0;y<h;++y){Check(canceled);for(int x=0;x<w;++x){Sample s;if(!Read(rgba,w,h,x,y,space,s))throw std::runtime_error("A full value texture requires valid pixels. Save the sampled strip instead.");write(s,size_t(y)*w+x);}}
        out.kind=data->channels==1?Assets::Kind::ScalarField:Assets::Kind::Texture;
    } else {
        data->width=w;data->height=h;data->channels=1;data->units="coverage";data->values.resize(size_t(w)*h);
        out.kind=Assets::Kind::Mask;
        if(selection.output==Output::SpatialMask) {
            const float radius=std::max(1.f,selection.strokeRadius*std::min(w,h));
            const auto paint=[&](const auto& path,bool erase) {
                Walk(path,w,h,[&](int cx,int cy){Check(canceled);
                    for(int y=std::max(0,int(cy-radius));y<=std::min(h-1,int(cy+radius));++y)
                        for(int x=std::max(0,int(cx-radius));x<=std::min(w-1,int(cx+radius));++x) {
                            const float weight=std::clamp((1-std::hypot(float(x-cx),float(y-cy))/radius)*4,0.f,1.f);
                            auto& value=data->values[size_t(y)*w+x];value=erase?std::min(value,1-weight):std::max(value,weight);
                        }
                });
            };paint(selection.path,false);paint(selection.exclusion,true);
            for(int y=0;y<h;++y){Check(canceled);for(int x=0;x<w;++x){Sample s;if(!Read(rgba,w,h,x,y,space,s))data->values[size_t(y)*w+x]=0;}}
        } else {
            const bool luma=selection.output!=Output::ColorMatch,color=selection.output!=Output::LumaMatch;
            const float tolerance=std::max(.2f,1+selection.outerFeather*4);
            StrokeValueMatcher include(luma,color,tolerance),exclude(luma,color,tolerance);
            for(const auto& s:values)include.Add(s.values);
            Walk(selection.exclusion,w,h,[&](int x,int y){Check(canceled);Sample s;if(Read(rgba,w,h,x,y,space,s,color))exclude.Add(s.values);});
            include.Build();exclude.Build();
            for(int y=0;y<h;++y){Check(canceled);for(int x=0;x<w;++x){Sample s;if(Read(rgba,w,h,x,y,space,s,color))data->values[size_t(y)*w+x]=include.Coverage(s.values)*(1-exclude.Coverage(s.values));}}
            out.provenance["matching"]="exact signed-log-Y / Oklab samples, continuous distance falloff";
            out.provenance["matchingVersion"]=2;
            out.provenance["tolerance"]=tolerance;
        }
    }
    if(!data->Valid(out.kind==Assets::Kind::Mask) || !samples->Valid(false))throw std::runtime_error("Capture produced non-finite values.");
    out.data=std::move(data);return out;
}
}
