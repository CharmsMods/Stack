#pragma once
#include "Raw/Bracketing/InspectionImage.h"
#include "Raw/Bracketing/HdrColorFusion.h"
#include "ThirdParty/stb_image_write.h"

namespace Stack::Validation {
inline void WriteBracketQualityChecks(Raw::Bracketing::ProcessingRequest request,
    const Raw::Bracketing::BracketingResult& result,const std::filesystem::path& output,nlohmann::json& report) {
    using namespace Raw::Bracketing;
    request.analysis=result.analysis;
    const auto& data=*result.analysis->prepared;
    for(const auto& source:data.sources)report["quality"]["captures"].push_back({{"frame",source.id},
        {"whiteLevel",source.metadata.whiteLevel},{"whiteLevelSource",source.metadata.whiteLevelSource},
        {"clippedSensorSamples",source.frame.saturatedSampleCount}});
    const auto count=request.recipe.groups.size();const auto& preview=result.preview;
    std::uint64_t checked=0,redirected=0;
    auto manualRecipe=request.recipe;manualRecipe.automatic=false;
    for(std::size_t p=0;p<preview.samples.size();p+=4*count) {
        ColorObservations observations;
        for(unsigned c=0;c<4;++c)for(std::size_t g=0;g<count;++g) {
            const auto& s=preview.samples[p+c*count+g];
            observations[c].push_back({s.value,s.variance,s.headroom,s.support,s.fallback,s.exposure,
                s.finite,s.clipped,s.localRejected,s.fixedReference,s.measurementVariance,s.uncertaintyVariance});
        }
        const auto automatic=BlendColor(observations,{},request.recipe);
        for(unsigned c=0;c<4;++c) {
            bool valid=false;for(std::size_t g=0;g<count;++g)
                valid|=request.recipe.groups[g].enabled&&observations[c][g].support>0;
            if(!valid)continue;
            for(std::size_t g=0;g<count;++g)if(observations[c][g].clipped&&request.recipe.groups[g].enabled) {
                ++checked;std::vector<double> forced(count);forced[g]=1;
                const auto manual=BlendColor(observations,forced,manualRecipe);
                if(automatic[c].actual[g]!=0||manual[c].actual[g]!=0||!manual[c].valid)
                    throw std::runtime_error("Clipped data overrode an available valid highlight measurement.");
                ++redirected;
            }
        }
    }
    report["quality"]["clippedMeasurementsChecked"]=checked;
    report["quality"]["manualRequestsRedirectedToValidMeasurements"]=redirected;
    const auto native=RenderInspectionImage(request,result,InspectionImageKind::Capture,request.recipe.originFrameId,0,16384);
    if(!native||native->width!=data.width||native->height!=data.height)
        throw std::runtime_error("The selected real capture did not load at native resolution.");
    report["quality"]["nativeCaptureDimensions"]={native->width,native->height};
    const auto write=[&](const CapturePreview& image,const char* name) {
        const unsigned step=std::max(1u,(image.width+1535)/1536),w=(image.width+step-1)/step,h=(image.height+step-1)/step;
        std::vector<unsigned char> rgba(std::size_t(w)*h*4);const auto& metadata=preview.metadata;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const auto p=(std::size_t(y)*step*image.width+x*step)*3,q=(std::size_t(y)*w+x)*4;
            for(unsigned c=0;c<3;++c) {
                double v=0;for(unsigned k=0;k<3;++k)v+=metadata.cameraToSrgb[c*3+k]*image.rgb[p+k]*metadata.cameraWhiteBalance[k]/metadata.cameraWhiteBalance[1];
                v=std::max(0.,v);v/=1+v;v=v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055;
                rgba[q+c]=std::uint8_t(std::clamp(v,0.,1.)*255+.5);
            }
            rgba[q+3]=255;
        }
        stbi_flip_vertically_on_write(0);
        if(!stbi_write_png((output/name).string().c_str(),w,h,4,rgba.data(),w*4))throw std::runtime_error("Could not save the quality comparison.");
    };
    write(*native,"capture-inspection.png");
    const auto merged=RenderInspectionImage(request,result,InspectionImageKind::Result,{},0,1536);
    if(!merged)throw std::runtime_error("The completed result inspection did not load.");
    write(*merged,"merged-inspection.png");
    request.shouldCancel=[] {return true;};
    if(RenderInspectionImage(request,result,InspectionImageKind::Capture,request.recipe.originFrameId,0,16384))
        throw std::runtime_error("Canceled inspection published pixels.");
    report["quality"]["inspectionCancellation"]=true;
}
}
