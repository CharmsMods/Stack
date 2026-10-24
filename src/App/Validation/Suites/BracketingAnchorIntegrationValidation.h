#pragma once
#include "Raw/Bracketing/AlignmentProxy.h"
#include "Raw/Bracketing/TemporalGpu.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

namespace Stack::Validation {
// Real component integration, not decoder/full-Process or universal quality
// validation. The caller supplies an already available compute GL context.
inline void ValidateBracketingAnchorIntegration(const Raw::OpenGlTaskExecutor& execute,
    const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;
    using namespace Raw::Mfd;
    const auto require=[](bool ok,const std::string& message) {
        if(!ok)throw std::runtime_error("Alignment anchor integration: "+message);
    };
    constexpr unsigned side=256;
    for(bool blurred:{true,false}) {
        PreparedDataset data;data.directory=directory/(blurred?"blurred":"equal");
        data.width=data.height=side;data.origin=0;data.sources.resize(4);
        DirectoryNormalizedTileCache cache(data.directory);
        ProcessingRequest request;request.executeOpenGlTask=execute;
        request.preferGpuRegistration=request.preferGpuCombining=true;
        request.memoryBudgetBytes=256ull*1024*1024;request.workerCount=1;
        request.recipe.alignmentMode=AlignmentMode::AutomaticLocal;request.recipe.groups.resize(2);
        PreparedRawTile origin;std::string error;std::vector<std::string> diagnostics;
        const std::array<double,4> angle{0,.008,-.006,.004},dx{0,1.25,-1.5,.5},dy{0,-.75,.5,1.5};
        for(unsigned i=0;i<4;++i) {
            auto& source=data.sources[i];source.id="anchor-integration-"+std::to_string(i);
            source.group=i?1:0; // Unity, unmeasured gains: only image evidence may calibrate.
            source.original=std::make_shared<CapturePreview>();
            source.original->width=source.original->height=side/2;
            source.alignmentDiagnostic.accepted=true;
            source.alignmentDiagnostic.model=i?CaptureAlignment::Model::GlobalAffine:CaptureAlignment::Model::Identity;
            AffineModel truth;truth.centerRaw={side/2.,side/2.};
            truth.linear={std::cos(angle[i]),-std::sin(angle[i]),std::sin(angle[i]),std::cos(angle[i])};
            truth.translationRaw={dx[i],dy[i]};source.alignment=truth;
            // Same accepted, imperfect global seed as the bounded outcome study.
            if(i){source.alignment.translationRaw.x+=.8;source.alignment.translationRaw.y-=.5;}
            auto& frame=source.frame;frame.cacheKey=frame.sourceContentSha256=std::string(64,char('a'+i));
            frame.sensorActiveArea={0,0,side,side};frame.activeExtent={side,side};
            frame.activeCfaPattern=Raw::CfaPattern::RGGB;frame.tileRawPixels=side;frame.tileColumns=frame.tileRows=1;
            frame.calibration.maximumBlackByCfaSite.fill(64);frame.calibration.whiteLevelByCfaSite.fill(4159);
            frame.calibration.usableSpanByCfaSite.fill(4095);
            const double sigma=i?.001:.002;source.noise.preparedFrameCacheKey=frame.cacheKey;
            source.noise.quality=NoiseModelQuality::TrustedMetadata;
            for(auto& site:source.noise.sites){site.offsetVariance=sigma*sigma;site.quantizationVariance=1./(12.*4095*4095);site.quantizationIncluded=true;}
            Frame recipeFrame;recipeFrame.id=source.id;request.recipe.groups[source.group].frames.push_back(recipeFrame);
            PreparedRawTile tile;tile.extent={side,side};tile.normalizedMosaic.resize(side*side);
            tile.comparisonGain.assign(side*side,1);tile.sampleFlags.assign(side*side,0);
            AffineModel inverse;require(InvertAffineModel(truth,inverse),"fixture inverse");
            std::mt19937 random(7301+i);std::normal_distribution<double> normal(0,sigma);
            const double blur=blurred?(i==2?6:i==3?5:0):0;
            const double a=std::exp(-.5*blur*blur*(.21*.21+.19*.19));
            const double b=std::exp(-.5*blur*blur*(.13*.13+.17*.17));
            AlignmentProxyAccumulator proxy(side,side,2);
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
                const auto p=inverse.Map({double(x),double(y)});
                const double scene=.3+.08*(a*std::cos(.21*p.x)*std::cos(.19*p.y)+.4*b*std::sin(.13*p.x+.17*p.y));
                const float value=float((i?scene:scene*.25)+normal(random));tile.normalizedMosaic[y*side+x]=value;
                const unsigned site=(y%2)*2+x%2;proxy.Add(x,y,site==0?0:site==3?2:1,value,false,false);
            }
            proxy.Finish(source.original->rgb,source.original->clipped);
            require(cache.Write(frame.cacheKey,tile,&error),"write fixture: "+error);if(i==0)origin=std::move(tile);
        }
        const auto checkState=[&] {
            require(data.origin==0&&!data.sources[0].localMotion&&data.sources[0].scale==1,"fixed origin changed");
            for(unsigned i=0;i<4;++i) {
                const auto& source=data.sources[i];
                require(source.enabled&&source.group==(i?1u:0u)&&source.exposureCalibrated,"membership/calibration lost");
                require(std::isfinite(source.scale)&&source.scale>0&&std::isfinite(source.scaleVariance)&&source.scaleVariance>=0,"invalid gain uncertainty");
                require(std::abs(std::log2(source.scale/(i?.25:1)))<.05,"image gain disagrees with fixture radiometry");
            }
        };
        require(Calibrate(request,data,diagnostics,error,true,true),"initial image calibration: "+error);checkState();
        require(AlignLocal(request,data,diagnostics,error),"local alignment: "+error);checkState();
        const auto gains=[&] {std::array<std::array<double,2>,4> result{};
            for(unsigned i=0;i<4;++i)result[i]={data.sources[i].scale,data.sources[i].scaleVariance};return result;};
        const auto before=gains();
        if(Calibrate(request,data,diagnostics,error,false)) {
            require(AlignLocal(request,data,diagnostics,error),"retained-field reliability refresh: "+error);
        } else {
            require(!error.empty()&&gains()==before,"failed optional refit changed measured gains");error.clear();
        }
        checkState();
        // Deterministic missing-evidence contract, independent of driver/search
        // differences: one proxy cannot supply any registered colour patches.
        auto& mask=data.sources[1].original->clipped;const auto savedMask=mask;
        std::fill(mask.begin(),mask.end(),1);const auto measured=gains();
        require(!Calibrate(request,data,diagnostics,error,false)&&!error.empty(),"missing-evidence refit unexpectedly succeeded");
        require(gains()==measured,"failed refit partially published gain/variance");mask=savedMask;error.clear();checkState();
        TemporalGpu gpu(request);require(gpu.Ready(error),"GPU readiness: "+error);
        std::vector<std::vector<Observation>> combined;
        require(gpu.Combine(request,data,origin,false,combined,error),"GPU temporal combination: "+error);
        require(combined.size()==2&&combined[1].size()==side*side,"combined layout");
        unsigned valid=0,total=0;double squaredError=0;
        for(unsigned y=32;y+32<side;++y)for(unsigned x=32;x+32<side;++x) {
            ++total;const auto& sample=combined[1][y*side+x];
            if(!sample.finite||sample.support<=0)continue;
            require(std::isfinite(sample.value)&&std::isfinite(sample.variance)&&sample.variance>0&&
                std::isfinite(sample.support)&&sample.support<=3.0001,"nonfinite/invalid combined observation");
            ++valid;const double truth=.25*(.3+.08*(std::cos(.21*x)*std::cos(.19*y)+.4*std::sin(.13*x+.17*y)));
            squaredError+=(sample.value-truth)*(sample.value-truth);
        }
        // Broad scene-scale safeguards, not hardware-specific bitwise targets.
        require(valid>total*.95&&std::sqrt(squaredError/valid)<.01,"combined support/radiometry lost");checkState();
    }
}
} // namespace Stack::Validation
