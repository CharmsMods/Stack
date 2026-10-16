#include "Raw/RawAssetCapture.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Stack::RawAssetCapture {
namespace {
bool UsesChannel(Channels channels,int c) {
    return c==0 ? channels!=Channels::Chroma : channels!=Channels::Luminance;
}
void Validate(const Selection& selection) {
    if ((selection.shape!=Shape::Line && selection.shape!=Shape::Circle && selection.shape!=Shape::Stroke) ||
        (selection.channels!=Channels::Luminance && selection.channels!=Channels::Chroma && selection.channels!=Channels::Both))
        throw std::runtime_error("Unsupported capture selection.");
    for (float coordinate : {selection.start[0],selection.start[1],selection.end[0],selection.end[1]})
        if (!std::isfinite(coordinate) || coordinate<0 || coordinate>1) throw std::runtime_error("Invalid capture coordinates.");
    if (!std::isfinite(selection.radiusU) || !std::isfinite(selection.radiusV) || selection.radiusU<0 || selection.radiusV<0 ||
        !std::isfinite(selection.innerFeather) || !std::isfinite(selection.outerFeather) ||
        selection.innerFeather<0 || selection.innerFeather>.5f || selection.outerFeather<0 || selection.outerFeather>2)
        throw std::runtime_error("Invalid capture interpolation range.");
}
void CheckCanceled(const std::function<bool()>& canceled) {
    if (canceled && canceled()) throw std::runtime_error("Capture canceled.");
}
template<class Visit>
void VisitSelection(int width,int height,const Selection& selection,const Visit& visit,
    const std::function<bool()>& canceled={}) {
    Validate(selection);
    if (width<=0 || height<=0) throw std::runtime_error("Capture dimensions are unavailable.");
    if(selection.shape==Shape::Stroke) {
        Selection segment=selection;segment.shape=Shape::Line;
        for(size_t i=1;i<selection.path.size();++i) {segment.start=selection.path[i-1];segment.end=selection.path[i];VisitSelection(width,height,segment,visit,canceled);}
        return;
    }
    if (selection.shape==Shape::Line) {
        const auto pixel=[](float uv,int extent) {
            return static_cast<int>(std::min(double(extent-1),std::floor(double(uv)*extent)));
        };
        int x=pixel(selection.start[0],width),y=pixel(selection.start[1],height);
        const int x2=pixel(selection.end[0],width),y2=pixel(selection.end[1],height);
        const int dx=std::abs(x2-x),dy=-std::abs(y2-y),sx=x<x2?1:-1,sy=y<y2?1:-1;
        std::int64_t error=std::int64_t(dx)+dy;
        for (;;) {
            CheckCanceled(canceled);visit(x,y);if (x==x2 && y==y2) break;
            const auto twice=error*2;
            if (twice>=dy) {error+=dy;x+=sx;} if (twice<=dx) {error+=dx;y+=sy;}
        }
    } else if (selection.radiusU>0 && selection.radiusV>0) {
        const auto boundary=[](double value,int extent,bool upper) {
            return static_cast<int>(std::clamp(upper?std::ceil(value):std::floor(value),0.0,double(extent-1)));
        };
        const int firstX=boundary((double(selection.start[0])-selection.radiusU)*width,width,false);
        const int lastX=boundary((double(selection.start[0])+selection.radiusU)*width,width,true);
        const int firstY=boundary((double(selection.start[1])-selection.radiusV)*height,height,false);
        const int lastY=boundary((double(selection.start[1])+selection.radiusV)*height,height,true);
        for (int y=firstY;y<=lastY;++y) {
            CheckCanceled(canceled);
            for (int x=firstX;x<=lastX;++x) {
                const float u=((x+.5f)/width-selection.start[0])/selection.radiusU;
                const float v=((y+.5f)/height-selection.start[1])/selection.radiusV;
                if (u*u+v*v<=1) visit(x,y);
            }
        }
    }
}
std::array<float,3> Values(const std::vector<float>& rgba,int width,int height,int x,int y,
    Raw::RawWorkingSpace workingSpace,bool chroma=true) {
    const auto offset=(std::size_t(height-1-y)*width+x)*4;
    if(!std::isfinite(rgba[offset+3]) || rgba[offset+3]<=0 || !std::isfinite(rgba[offset]) || !std::isfinite(rgba[offset+1]) || !std::isfinite(rgba[offset+2])) {
        const float missing=std::numeric_limits<float>::quiet_NaN();return {missing,missing,missing};
    }
    const std::array<float,3> rgb{rgba[offset],rgba[offset+1],rgba[offset+2]};
    const float Y=workingSpace==Raw::RawWorkingSpace::LinearRec2020D65 ?
        .2627002f*rgb[0]+.6779981f*rgb[1]+.0593017f*rgb[2] :
        .2126729f*rgb[0]+.7151522f*rgb[1]+.0721750f*rgb[2];
    if (!chroma) return {Y,0,0};
    const auto color=RawRecipe::WorkingRgbToColorWarpCoordinate(rgb,workingSpace);
    return {Y,color.a,color.b};
}
bool Include(Range& range,const std::array<float,3>& values,Channels channels) {
    for (int c=0;c<3;++c) if (UsesChannel(channels,c) && !std::isfinite(values[c])) return false;
    for (int c=0;c<3;++c) if (UsesChannel(channels,c)) {
        range.minimum[c]=range.count?std::min(range.minimum[c],values[c]):values[c];
        range.maximum[c]=range.count?std::max(range.maximum[c],values[c]):values[c];
    }
    ++range.count;return true;
}
void ValidateImage(const std::vector<float>& rgba,int width,int height) {
    if (width<=0 || height<=0 || rgba.size()!=std::size_t(width)*height*4)
        throw std::runtime_error("The capture image is unavailable.");
}
} // namespace

std::shared_ptr<const PreviewField> PreparePreview(const std::vector<float>& rgba,int width,int height,
    Raw::RawWorkingSpace workingSpace,const std::function<bool()>& canceled) {
    ValidateImage(rgba,width,height);
    auto field=std::make_shared<PreviewField>();field->width=width;field->height=height;
    field->values.resize(std::size_t(width)*height*3);
    for (int y=0;y<height;++y) {
        CheckCanceled(canceled);
        for (int x=0;x<width;++x) {
            const auto v=Values(rgba,width,height,x,y,workingSpace);
            std::copy(v.begin(),v.end(),field->values.begin()+(std::size_t(y)*width+x)*3);
        }
    }
    return field;
}
Range Measure(const PreviewField& field,const Selection& selection) {
    if (field.width<=0 || field.height<=0 || field.values.size()!=std::size_t(field.width)*field.height*3)
        throw std::runtime_error("Capture feedback is unavailable.");
    Range range;
    VisitSelection(field.width,field.height,selection,[&](int x,int y) {
        const auto offset=(std::size_t(y)*field.width+x)*3;
        Include(range,{field.values[offset],field.values[offset+1],field.values[offset+2]},selection.channels);
    });
    return range;
}
Qualifier MakeQualifier(const Range& range,const Selection& selection) {
    Validate(selection);
    if (!range.count) throw std::runtime_error("The selection contains no finite image values.");
    Qualifier qualifier;qualifier.channels=selection.channels;
    for (int c=0;c<3;++c) if (UsesChannel(selection.channels,c)) {
        const double low=range.minimum[c],high=range.maximum[c],center=(low+high)*.5;
        if (!std::isfinite(low) || !std::isfinite(high) || low>high) throw std::runtime_error("Invalid capture range.");
        // Point selections remain featherable. Floors are in actual channel
        // units, independent of display mapping and HDR clipping.
        const double span=std::max(high-low,c==0?std::max(.18,std::abs(center))*.02:.01);
        qualifier.innerMinimum[c]=static_cast<float>(std::min(center,low+selection.innerFeather*span));
        qualifier.innerMaximum[c]=static_cast<float>(std::max(center,high-selection.innerFeather*span));
        qualifier.outerMinimum[c]=static_cast<float>(low-selection.outerFeather*span);
        qualifier.outerMaximum[c]=static_cast<float>(high+selection.outerFeather*span);
        if (!std::isfinite(qualifier.outerMinimum[c]) || !std::isfinite(qualifier.outerMaximum[c]))
            throw std::runtime_error("Capture range exceeds supported precision.");
    }
    return qualifier;
}
float Coverage(const std::array<float,3>& value,const Qualifier& qualifier) {
    float coverage=1;
    for (int c=0;c<3;++c) if (UsesChannel(qualifier.channels,c)) {
        if (!std::isfinite(value[c])) return 0;
        float channel=1;
        if (value[c]<qualifier.innerMinimum[c]) {
            const float width=qualifier.innerMinimum[c]-qualifier.outerMinimum[c];
            const float t=width>0?std::clamp((value[c]-qualifier.outerMinimum[c])/width,0.f,1.f):0;
            channel=t*t*(3-2*t);
        } else if (value[c]>qualifier.innerMaximum[c]) {
            const float width=qualifier.outerMaximum[c]-qualifier.innerMaximum[c];
            const float t=width>0?std::clamp((qualifier.outerMaximum[c]-value[c])/width,0.f,1.f):0;
            channel=t*t*(3-2*t);
        }
        coverage=std::min(coverage,channel);
    }
    return coverage;
}
Assets::Record Capture(const std::vector<float>& rgba,int width,int height,
    Raw::RawWorkingSpace workingSpace,const Selection& selection,const std::function<bool()>& canceled) {
    ValidateImage(rgba,width,height);
    if(selection.shape==Shape::Stroke) return CaptureStroke(rgba,width,height,workingSpace,selection,canceled);
    const bool luminance=selection.channels!=Channels::Chroma,chroma=selection.channels!=Channels::Luminance;
    auto samples=std::make_shared<Assets::FloatData>();
    samples->height=1;samples->channels=luminance?(chroma?3:1):2;
    samples->units=luminance?(chroma?"linear-Y,Oklab-a,Oklab-b":"linear-Y"):"Oklab-a,Oklab-b";
    Range range;
    VisitSelection(width,height,selection,[&](int x,int y) {
        const auto v=Values(rgba,width,height,x,y,workingSpace,chroma);
        if (Include(range,v,selection.channels))
            for (int c=0;c<3;++c) if (UsesChannel(selection.channels,c)) samples->values.push_back(v[c]);
    },canceled);
    const auto qualifier=MakeQualifier(range,selection);
    if (range.count>std::size_t(std::numeric_limits<int>::max()))
        throw std::runtime_error("The selection exceeds the sample table's supported extent.");
    samples->width=static_cast<int>(range.count);
    auto mask=std::make_shared<Assets::FloatData>();
    mask->width=width;mask->height=height;mask->units="coverage";mask->values.resize(std::size_t(width)*height);
    for (int y=0;y<height;++y) {
        CheckCanceled(canceled);
        for (int x=0;x<width;++x)
            mask->values[std::size_t(y)*width+x]=Coverage(Values(rgba,width,height,x,y,workingSpace,chroma),qualifier);
    }
    if (!mask->Valid(true) || !samples->Valid(false)) throw std::runtime_error("Capture produced invalid values.");
    Assets::Record result;
    result.kind=Assets::Kind::Mask;result.data=std::move(mask);result.samples=std::move(samples);
    result.provenance={{"captureVersion",2},{"shape",static_cast<int>(selection.shape)},
        {"channels",static_cast<int>(selection.channels)},{"workingSpace",static_cast<int>(workingSpace)},
        {"start",selection.start},{"end",selection.end},{"radiusU",selection.radiusU},{"radiusV",selection.radiusV},
        {"rangeMinimum",range.minimum},{"rangeMaximum",range.maximum},
        {"innerFeather",selection.innerFeather},{"outerFeather",selection.outerFeather},
        {"innerMinimum",qualifier.innerMinimum},{"innerMaximum",qualifier.innerMaximum},
        {"outerMinimum",qualifier.outerMinimum},{"outerMaximum",qualifier.outerMaximum},
        {"behavior","fixed-range-mask"}};
    return result;
}
} // namespace Stack::RawAssetCapture
