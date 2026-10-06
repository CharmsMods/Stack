#pragma once
#include "Raw/Bracketing/BorderSupport.h"
#include "Raw/Bracketing/CaptureFallback.h"
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingBorders() {
    using namespace Raw;
    const auto check=[](bool ok,const char* message){if(!ok) throw std::runtime_error(message);};
    for(const auto extent:{Mfd::PixelExtent{4608,3072},Mfd::PixelExtent{3072,4608},Mfd::PixelExtent{1002,770}}) {
        Mfd::LocalMotionGrid grid;grid.valid=true;
        grid.referenceRawExtent=grid.sourceRawExtent=extent;
        grid.originRawX=grid.originRawY=36;grid.spacingRawX=grid.spacingRawY=128;
        grid.width=static_cast<unsigned>((extent.width-1-72)/128)+1;
        grid.height=static_cast<unsigned>((extent.height-1-72)/128)+1;
        grid.nodes.resize(static_cast<std::size_t>(grid.width)*grid.height);
        for(auto& node:grid.nodes) {
            node.state=Mfd::MotionNodeState::Structured;node.confidence=1;
            node.covarianceRaw={.0025,0,.0025};
        }
        Bracketing::ExtendMotionBoundary(grid);
        for(unsigned y=0;y<extent.height;y+=17) for(unsigned x=0;x<extent.width;x+=19) {
            Mfd::LocalMotionFieldSample sample;
            check(Mfd::EvaluateLocalMotionField(grid,{double(x),double(y)},{},sample),
                "Extended motion field still contains an artificial unsupported border");
            check(sample.sourceRaw.x==x&&sample.sourceRaw.y==y,"Boundary extension changed a supported identity mapping");
        }
        Mfd::LocalMotionFieldSample sample;
        check(Mfd::EvaluateLocalMotionField(grid,{double(extent.width-1),double(extent.height-1)},{},sample),
            "Motion field lost the bottom/right corner");
        // A real displacement beyond the source is still missing data.
        for(auto& node:grid.nodes) node.residualRaw={-4,0};
        check(!Mfd::EvaluateLocalMotionField(grid,{0,40},{},sample),"Boundary support invented out-of-image measurements");
    }
    for(const auto site:{Mfd::CfaSite::Red,Mfd::CfaSite::Green0,Mfd::CfaSite::Green1,Mfd::CfaSite::Blue}) {
        for(const auto point:{Mfd::CfaPlaneCoordinate{0,0,site},Mfd::CfaPlaneCoordinate{63,63,site},
            Mfd::CfaPlaneCoordinate{.25,62.75,site},Mfd::CfaPlaneCoordinate{62.8,.2,site}}) {
            Mfd::KeysBicubicFootprint footprint;
            check(Bracketing::BuildSupportedFootprint(point,{64,64},footprint),"Supported boundary sampler rejected usable data");
            std::array<Mfd::SameCfaTapInput,16> taps;
            for(unsigned i=0;i<taps.size();++i) {
                check(footprint.taps[i].site==site,"Boundary sampling mixed CFA sites");
                taps[i]={-.2+.01*footprint.taps[i].x+.02*footprint.taps[i].y,1,0};
            }
            Mfd::SiteNoiseProfile profile;profile.offsetVariance=.001;profile.quantizationIncluded=true;
            Mfd::SameCfaScalarParameters parameters;parameters.referenceComparisonPilot=.1;
            Mfd::SameCfaSampleResult sample;
            check(Mfd::EvaluateSameCfaFootprintScalar(footprint,taps,profile,64000,parameters,sample),
                "Boundary sampling did not propagate measurement variance");
            check(std::abs(sample.value-(-.2+.01*point.x+.02*point.y))<1e-12,
                "Boundary sampler changed a linear scene or clipped signed data");
            if(point.x==0||point.x==63)
                check(std::abs(sample.interpolationVariance-.001)<1e-12,"Repeated boundary taps claimed independent noise reduction");
            const auto strongest=std::max_element(footprint.coefficients.begin(),footprint.coefficients.end(),
                [](double a,double b){return std::abs(a)<std::abs(b);})-footprint.coefficients.begin();
            taps[strongest].sampleFlags|=static_cast<std::uint16_t>(Mfd::PreparedSampleFlag::Saturated);
            Mfd::SameCfaSampleResult rejected;
            check(!Mfd::EvaluateSameCfaFootprintScalar(footprint,taps,profile,64000,parameters,rejected),
                "A clipped footprint was promoted to an accepted measurement");
            const auto fallback=Bracketing::InterpolateCaptureFallback(footprint,taps,parameters.exposureScale);
            check(fallback.finite&&fallback.clipped&&std::abs(fallback.value-sample.value)<1e-12,
                "Clipping changed fallback geometry or lost the clipping marker");
        }
    }
}
}
