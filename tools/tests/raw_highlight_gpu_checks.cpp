#include "Raw/RawGpuPipeline.h"
#include "Raw/RawProcessingMath.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Raw::RawImageData Flat(std::array<float,3> color,Raw::CfaPattern pattern) {
    Raw::RawImageData raw;
    auto& m=raw.metadata;
    m.rawWidth=m.visibleWidth=m.rawHeight=m.visibleHeight=32;
    m.orientation=1;m.cfaPattern=pattern;m.pixelLayout=Raw::RawPixelLayout::MosaicBayer;
    m.blackLevel=512;m.whiteLevel=65000;m.hasCameraMatrix=true;
    // Deliberately non-neutral row sums verify completion after conversion.
    m.cameraToSrgb={1.1f,-.1f,.05f, .02f,.96f,0.f, -.03f,.02f,1.05f};
    const auto area=Raw::Processing::ResolveActiveArea(m);
    raw.rawBuffer.resize(32*32);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)
        raw.rawBuffer[y*32+x]=static_cast<std::uint16_t>(std::min(65535.f,512+64488*color[Raw::Processing::CfaColorAt(m,area,x,y)]));
    return raw;
}
std::vector<float> Render(Raw::RawGpuPipeline& pipeline,const Raw::RawImageData& raw,const Raw::RawDevelopSettings& settings) {
    const auto texture=pipeline.Render(raw,settings);
    Require(texture!=0,pipeline.GetLastError().c_str());
    std::vector<float> result(32*32*3);
    glBindTexture(GL_TEXTURE_2D,texture);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_FLOAT,result.data());
    for(float value:result)Require(std::isfinite(value),"Highlight repair produced non-finite pixels");
    return result;
}
}

void TestRawHighlightReconstruction() {
    using namespace Raw;
    RawGpuPipeline pipeline;
    RawDevelopSettings settings;
    Require(settings.highlightMode==HighlightReconstructionMode::ColorReconstruction && settings.highlightStrength==1,
        "New RAW developments do not default to full color reconstruction");
    settings.whiteBalanceMode=WhiteBalanceMode::Manual;
    settings.manualWhiteBalance={2.f,1.f,1.5f};
    settings.cameraTransformSource=RawCameraTransformSource::LibRawRgbCam;
    settings.mosaicDenoise.enabled=false;
    for(auto pattern:{CfaPattern::RGGB,CfaPattern::BGGR,CfaPattern::GRBG,CfaPattern::GBRG})
        for(auto method:{DemosaicMethod::Bilinear,DemosaicMethod::MalvarHeCutler,DemosaicMethod::Lmmse}) {
            settings.demosaicMethod=method;
            for(auto levels:{std::array<float,3>{1,1,1},std::array<float,3>{.65f,1,.8f}}) {
                const auto raw=Flat(levels,pattern);
                for(auto mode:{HighlightReconstructionMode::ClipNeutral,HighlightReconstructionMode::Luminance,HighlightReconstructionMode::ColorReconstruction}) {
                    settings.highlightMode=mode;
                    const auto pixels=Render(pipeline,raw,settings);
                    for(int y=8;y<24;++y)for(int x=8;x<24;++x) {
                        const auto i=(y*32+x)*3;
                        Require(std::abs(pixels[i]-pixels[i+1])<.004f && std::abs(pixels[i+2]-pixels[i+1])<.004f,
                            "Unrecoverable highlight retained false color or CFA phase");
                    }
                }
            }
            const auto healthy=Flat({.8f,.2f,.6f},pattern);
            settings.highlightMode=HighlightReconstructionMode::Off;
            const auto baseline=Render(pipeline,healthy,settings);
            settings.highlightMode=HighlightReconstructionMode::ColorReconstruction;
            Require(Render(pipeline,healthy,settings)==baseline,"Healthy saturated color was altered");
            settings.highlightStrength=0;
            const auto clipped=Flat({.65f,1,.8f},pattern);
            const auto zero=Render(pipeline,clipped,settings);
            settings.highlightMode=HighlightReconstructionMode::Off;
            Require(Render(pipeline,clipped,settings)==zero,"Zero reconstruction strength was not a bypass");
            settings.highlightStrength=1;settings.highlightMode=HighlightReconstructionMode::ColorReconstruction;
        }
    settings.demosaicMethod=DemosaicMethod::Bilinear;
    std::vector<float> previous;
    for(int step=0;step<=26;++step) {
        const auto raw=Flat({.65f,.976f+step*.001f,.8f},CfaPattern::RGGB);
        const auto pixels=Render(pipeline,raw,settings);
        if(!previous.empty())for(int c=0;c<3;++c) {
            const float delta=std::abs(previous[16*32*3+16*3+c]-pixels[16*32*3+16*3+c]);
            if(delta>=.06f)std::cerr<<"Transition step "<<step<<" channel "<<c<<" from "<<previous[16*32*3+16*3+c]<<" to "<<pixels[16*32*3+16*3+c]<<" delta "<<delta<<'\n';
            Require(delta<.06f,"Near-clipping transition has a hard color step");
        }
        previous=pixels;
    }
    // Same flat-field fixture with an unclipped colored boundary. Reconstruction
    // must borrow hue while retaining the center's surviving red/blue levels.
    auto colored=Flat({.3f,.75f,.3f},CfaPattern::RGGB);
    const auto center=Flat({.6f,1.f,.6f},CfaPattern::RGGB);
    for(int y=14;y<18;++y)for(int x=14;x<18;++x)colored.rawBuffer[y*32+x]=center.rawBuffer[y*32+x];
    settings.cameraTransformEnabled=false;
    const auto repaired=Render(pipeline,colored,settings);
    const auto index=(16*32+16)*3;
    Require(std::abs(repaired[index]-1.2f)<.01f && std::abs(repaired[index+1]-1.5f)<.02f && std::abs(repaired[index+2]-.9f)<.01f,
        "Reliable neighboring highlight color was not reconstructed");
    // Floating HDR input has no single-frame saturation boundary.
    auto hdr=Flat({1,1,1},CfaPattern::RGGB);
    hdr.normalizedMosaicInputContract=NormalizedMosaicInputContract::HdrVirtualAnchorPreGain;
    hdr.normalizedMosaicBuffer=std::make_shared<std::vector<float>>(32*32,3.f);
    const auto enabled=Render(pipeline,hdr,settings);
    settings.highlightMode=HighlightReconstructionMode::Off;
    Require(Render(pipeline,hdr,settings)==enabled,"RAW repair interpreted HDR overrange as sensor clipping");
    Require(*std::max_element(enabled.begin(),enabled.end())>1.f,"HDR highlight range was clipped");
    std::cout<<"RAW highlights: neutral fallback, color donors, all CFA phases, three demosaicers, threshold continuity, bypass and HDR preservation passed.\n";
}
