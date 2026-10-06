#include "CaptureSharpness.h"
#include "PreparedTileSampler.h"
#include "BurstNoise.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
namespace {
struct Spectrum {double fine=0,coarse=0,noise=0;unsigned count=0;};
bool Patch(PreparedTileSampler& sampler,const PreparedSource& source,
    const Mfd::CfaLayout& layout,Mfd::RawCoordinate reference,Spectrum& spectrum,std::string& error) {
    auto mapped=source.alignment.Map(reference);
    if(source.localMotion) {
        Mfd::LocalMotionFieldSample motion;Mfd::LocalMotionOptions options;
        if(!EvaluateCaptureMotion(source,reference,options,motion)||motion.alignmentConfidence<.4) return false;
        mapped=motion.sourceRaw;
    } else if(source.alignmentDiagnostic.localApplied&&source.alignmentDiagnostic.backend!="fixed-reference") return false;
    for(unsigned s=0;s<4;++s) {
        const auto site=static_cast<Mfd::CfaSite>(s);const auto plane=layout.RawToPlane(mapped,site);
        if(!std::isfinite(plane.x)||!std::isfinite(plane.y)) return false;
        const auto center=layout.PlanePixelToRaw({std::llround(plane.x),std::llround(plane.y),site});
        std::array<double,81> value{},noise{};
        for(int y=-4;y<=4;++y) for(int x=-4;x<=4;++x) {
            const auto rx=static_cast<std::int64_t>(center.x)+2*x,ry=static_cast<std::int64_t>(center.y)+2*y;
            if(rx<0||ry<0||rx>=source.frame.activeExtent.width||ry>=source.frame.activeExtent.height) return false;
            Mfd::SameCfaTapInput tap;
            if(!sampler.ReadSample(source.frame,rx,ry,tap,&error)) return false;
            if(tap.sampleFlags||!std::isfinite(tap.normalizedSample)||tap.normalizedSample>.9) return false;
            const auto i=(y+4)*9+x+4;const double gain=tap.comparisonGain*source.scale;
            value[i]=tap.normalizedSample*gain;
            const auto& profile=source.noise.sites[s];
            noise[i]=gain*gain*(profile.shotScale*std::max(0.,tap.normalizedSample)+profile.offsetVariance+
                (profile.quantizationIncluded?0:profile.quantizationVariance));
        }
        for(int y=2;y<7;++y) for(int x=2;x<7;++x) {
            const int p=y*9+x;double energies[2]{},variances[2]{};
            for(int scale=1;scale<=2;++scale) {
                double laplacian=4*value[p],variance=16*noise[p];
                for(int offset:{-scale,scale,-9*scale,9*scale}) {laplacian-=value[p+offset];variance+=noise[p+offset];}
                energies[scale-1]=laplacian*laplacian-variance;variances[scale-1]=variance;
            }
            spectrum.fine+=energies[0];spectrum.coarse+=energies[1];
            spectrum.noise+=std::max(variances[0],variances[1]);++spectrum.count;
        }
    }
    return spectrum.count&&spectrum.coarse>16*spectrum.noise&&spectrum.fine>2*spectrum.noise;
}
}

bool MeasureCaptureSharpness(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    ProcessingTimer timer(data.stageSeconds,"capture_sharpness");
    if(data.sources.size()<2) return true;
    const auto& origin=data.sources[data.origin];Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(origin.frame.activeCfaPattern,layout)) return false;
    struct Anchor {Mfd::RawCoordinate coordinate;double detailEnergy;};std::vector<Anchor> anchors;
    PreparedTileSampler sampler(data.directory,12);
    const unsigned stride=std::max(24u,static_cast<unsigned>(std::sqrt(double(data.width)*data.height/384))/2*2);
    for(unsigned y=12;y+12<data.height;y+=stride) for(unsigned x=12;x+12<data.width;x+=stride) {
        if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
        Spectrum evidence;
        if(Patch(sampler,origin,layout,{double(x),double(y)},evidence,error))
            anchors.push_back({{double(x),double(y)},evidence.fine/evidence.count});
        if(!error.empty()) return false;
    }
    std::vector<double> scores(data.sources.size(),1);
    std::vector<std::size_t> evidenceCount(data.sources.size());evidenceCount[data.origin]=anchors.size();
    std::vector<bool> measured(data.sources.size(),false);measured[data.origin]=anchors.size()>=16;
    for(std::size_t i=0;i<data.sources.size();++i) {
        const auto& source=data.sources[i];if(i==data.origin||!source.enabled) continue;
        std::vector<double> ratios;
        for(const auto& anchor:anchors) {
            if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
            Spectrum evidence;
            if(Patch(sampler,source,layout,anchor.coordinate,evidence,error)) {
                // Exposure calibration already puts these energies on one
                // radiometric scale. Dividing by coarse energy here would
                // hide blur in scenes dominated by one spatial frequency.
                const auto ratio=evidence.fine/evidence.count/anchor.detailEnergy;
                if(std::isfinite(ratio)&&ratio>0) ratios.push_back(std::log2(ratio));
            }
            if(!error.empty()) return false;
        }
        evidenceCount[i]=ratios.size();
        if(ratios.size()>=16) {scores[i]=std::exp2(std::clamp(MedianNoiseValue(ratios),-3.,3.));measured[i]=true;}
    }
    for(std::size_t i=0;i<data.sources.size();++i) {
        auto& source=data.sources[i];if(!source.enabled) continue;
        if(!measured[i]) {
            diagnostics.push_back(source.id+": retained equal detail preference; only "+
                std::to_string(evidenceCount[i])+" reliable sharpness patches.");continue;
        }
        double best=scores[i];
        for(std::size_t j=0;j<data.sources.size();++j)
            if(measured[j]&&data.sources[j].enabled&&data.sources[j].group==source.group) best=std::max(best,scores[j]);
        // Ignore small spectral differences and limit the response. Uncertain
        // measurements keep their default preference rather than losing noise support.
        const double ratio=std::clamp(scores[i]/best,0.,1.);
        source.detailPreference=ratio>.85?1.:std::max(.1,std::pow(ratio/.85,3));
        diagnostics.push_back(source.id+": measured detail preference "+std::to_string(source.detailPreference)+
            " from "+std::to_string(evidenceCount[i])+" patches; broad-tone temporal support is retained.");
    }
    return true;
}
}
