#pragma once
#include "ProcessingInternal.h"
#include "BorderSupport.h"
#include "BurstNoise.h"
#include <array>

namespace Raw::Bracketing {
// One bounded radiometric refinement after motion estimation. Use supported,
// observable correspondences in spatial bins; retain the joint exposure-graph
// estimate when the fixed origin has no reliable overlap with this exposure.
inline void RefineLocallyRegisteredExposure(const ProcessingRequest& request,
    const PreparedSource& reference,PreparedSource& source,const Mfd::CfaPlanePyramid& referencePyramid,
    const Mfd::CfaPlanePyramid& alternatePyramid,const Mfd::LocalMotionGrid& motion,
    const Mfd::LocalMotionOptions& options,std::vector<std::string>& diagnostics) {
    if(request.recipe.groups.size()<2) return;
    for(const auto& group:request.recipe.groups) for(const auto& frame:group.frames)
        if(frame.id==source.id&&frame.manualExposure) return;
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(reference.frame.activeCfaPattern,layout)) return;
    std::array<std::vector<double>,64> bins;
    const auto width=reference.frame.activeExtent.width,height=reference.frame.activeExtent.height;
    const unsigned step=std::max(8u,static_cast<unsigned>(std::sqrt(double(width)*height/4096))/2*2);
    for(unsigned y=4;y+4<height;y+=step) {
        if(request.shouldCancel&&request.shouldCancel()) return;
        for(unsigned x=4;x+4<width;x+=step) {
            std::vector<double> colorRatios;
            for(unsigned siteIndex=0;siteIndex<4;++siteIndex) {
                const auto site=static_cast<Mfd::CfaSite>(siteIndex);
                const auto offset=layout.OffsetFor(site);
                const Mfd::RawCoordinate raw={double(x+offset.x),double(y+offset.y)};
                Mfd::LocalMotionFieldSample mapped;
                if(!Mfd::EvaluateLocalMotionField(motion,raw,options,mapped,nullptr)||mapped.alignmentConfidence<.5) continue;
                const auto* a=Mfd::FindCfaPyramidLevel(referencePyramid,site,0);
                const auto* b=Mfd::FindCfaPyramidLevel(alternatePyramid,site,0);
                if(!a||!b) continue;
                const auto referencePlane=layout.RawToPlane(raw,site);
                const auto index=static_cast<std::size_t>(referencePlane.y)*a->extent.width+
                    static_cast<std::size_t>(referencePlane.x);
                if(index>=a->signal.size()||!a->validMask[index]) continue;
                Mfd::KeysBicubicFootprint footprint;
                if(!BuildSupportedFootprint(layout.RawToPlane(mapped.sourceRaw,site),b->extent,footprint)) continue;
                double value=0,variance=0;bool valid=true;
                for(std::size_t tap=0;tap<Mfd::kSameCfaTapCount;++tap) {
                    const double weight=footprint.coefficients[tap];if(std::abs(weight)<1e-12) continue;
                    const auto& pixel=footprint.taps[tap];const auto p=pixel.y*b->extent.width+pixel.x;
                    if(!b->validMask[p]) {valid=false;break;}
                    value+=weight*b->signal[p];variance+=weight*weight*b->variance[p];
                }
                const double target=a->signal[index],scaled=value*source.scale;
                if(!valid||target<=0||scaled<=0||!std::isfinite(scaled)||
                    target*target<100*a->variance[index]||value*value<100*variance) continue;
                const double ratio=std::log2(target/scaled);
                if(std::isfinite(ratio)) colorRatios.push_back(ratio);
            }
            if(colorRatios.size()<3) continue;
            const auto [low,high]=std::minmax_element(colorRatios.begin(),colorRatios.end());
            if(*high-*low>.05) continue;
            const auto bin=std::min<std::uint64_t>(7,y*8/height)*8+std::min<std::uint64_t>(7,x*8/width);
            bins[bin].push_back(MedianNoiseValue(std::move(colorRatios)));
        }
    }
    std::vector<double> ratios;
    for(auto& bin:bins) if(bin.size()>=8) ratios.push_back(MedianNoiseValue(std::move(bin)));
    if(ratios.size()<8) return;
    std::sort(ratios.begin(),ratios.end());
    const double correction=MedianNoiseValue(ratios);
    const double spread=ratios[ratios.size()*3/4]-ratios[ratios.size()/4];
    if(std::abs(correction)>.1||spread>.03) {
        diagnostics.push_back(source.id+": local exposure evidence was inconsistent; retained the joint exposure estimate.");return;
    }
    const double sigma=std::max(.0001,1.58*spread/std::sqrt(double(ratios.size())));
    source.scale*=std::exp2(correction);
    source.scaleVariance=std::pow(std::log(2.)*source.scale*sigma,2);
    diagnostics.push_back(source.id+": local correspondence exposure correction "+std::to_string(correction)+
        " EV from "+std::to_string(ratios.size())+" spatial bins.");
}
}
