#include "HdrColorFusion.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing {
void GatherColorCell(const std::vector<std::vector<Observation>>& groups,
    std::size_t topLeft,std::size_t rowStride,ColorObservations& samples) {
    for(unsigned site=0;site<4;++site) {
        samples[site].resize(groups.size());
        const auto index=topLeft+(site/2)*rowStride+site%2;
        for(std::size_t g=0;g<groups.size();++g)samples[site][g]=groups[g][index];
    }
}

std::array<Pixel,4> BlendColor(const ColorObservations& samples,const std::vector<double>& requested,
    const BracketingRecipe& recipe,unsigned sites) {
    std::array<Pixel,4> result;
    const auto groups=recipe.groups.size();
    if(sites<1||sites>4||groups>64||(!requested.empty()&&requested.size()!=groups))return result;
    for(unsigned s=0;s<sites;++s)if(samples[s].size()!=groups)return result;
    // A denoise-only group keeps its established arithmetic and all sidecars.
    if(groups<2) {
        for(unsigned s=0;s<sites;++s)result[s]=Blend(samples[s],requested,recipe);
        return result;
    }
    std::array<double,64> headroom{};
    std::vector<double> weights(groups);
    bool completeSupport=false;
    const bool automatic=requested.empty()||recipe.automatic;
    for(std::size_t g=0;g<groups;++g) {
        if(!recipe.groups[g].enabled)continue;
        double safety=1,variance=0;
        for(unsigned s=0;s<sites;++s) {
            const auto& v=samples[s][g];
            if(!(v.support>0)||!v.finite||!std::isfinite(v.value)||!std::isfinite(v.variance)||!std::isfinite(v.headroom)) {
                safety=0;break;
            }
            safety=std::min(safety,std::clamp(v.headroom,0.0,1.0));
            variance+=std::max(1e-12,v.variance)/sites;
        }
        headroom[g]=safety;
        completeSupport|=safety>0;
        // Trace precision minimizes the sum of channel errors subject to one
        // shared exposure weight. Model/alignment risk belongs in this decision,
        // but Blend still propagates sensor and model variances separately.
        weights[g]=automatic?1/std::max(1e-12,variance):requested[g];
    }
    if(!completeSupport) {
        // Different exposures can supply different unclipped colors. Never
        // invent clipping or discard these recoverable measurements merely
        // because no single exposure contains a complete usable color cell.
        for(unsigned s=0;s<sites;++s)
            result[s]=Blend(samples[s],automatic?std::vector<double>{}:requested,recipe);
        // If a color has no accepted measurement, independent fallback can
        // pair clipped-reference red/blue with a dark, displaced green from
        // another exposure. Prefer one complete, unclipped exposure instead.
        // Even individually supported colors can describe different edges
        // when the complete exposures were rejected for registration mismatch.
        // Pure partial clipping can still recover independent color channels.
        if(std::any_of(result.begin(),result.begin()+sites,[](const Pixel& p){return !p.valid||p.localRejected;})) {
            const bool needsClippedFallback=std::any_of(result.begin(),result.begin()+sites,
                [](const Pixel& p){return !p.valid||p.fallback;});
            std::size_t selected=groups;double exposure=std::numeric_limits<double>::infinity();
            bool selectedClipped=true;
            for(std::size_t g=0;g<groups;++g) {
                if(!recipe.groups[g].enabled)continue;
                bool usable=true,clipped=false;double longest=0;
                for(unsigned s=0;s<sites;++s) {
                    const auto& v=samples[s][g];
                    usable&=v.finite&&std::isfinite(v.fallback);
                    clipped|=v.fallbackClipped;
                    longest=std::max(longest,v.exposure);
                }
                if(usable&&(!clipped||needsClippedFallback)&&
                    (selected==groups||(selectedClipped&&!clipped)||
                    (selectedClipped==clipped&&longest<exposure))) {
                    selected=g;exposure=longest;selectedClipped=clipped;
                }
            }
            if(selected<groups)for(unsigned s=0;s<sites;++s) {
                const auto& v=samples[s][selected];auto& p=result[s];
                const bool rejected=p.localRejected;p={};
                // Keep all colors on the selected fallback capture. Mixing a
                // group's accepted mean with that capture's missing channels
                // recreates the same edge mismatch within an exposure group.
                // When every exposure clips, retain the shortest capture's
                // color relationship and report the lost channel. Combining
                // its red/blue with a longer exposure's capped green invents
                // magenta even though no complete highlight is recoverable.
                p.value=v.fallback;p.clipped=v.fallbackClipped;
                p.actual[selected]=1;p.fallback=true;p.localRejected=rejected;
                p.fallbackReason=MeasurementFallbackReason::ShortestExposure;
            }
        }
        return result;
    }
    double total=0;
    for(std::size_t g=0;g<groups;++g)total+=weights[g]*headroom[g];
    const bool fallback=total<=1e-20;
    if(fallback) {
        // Authored sources may all be clipped. Choose among complete usable
        // colors before falling back to independently assembled channels.
        for(std::size_t g=0;g<groups;++g) {
            double variance=0;
            for(unsigned s=0;s<sites;++s)variance+=std::max(1e-12,samples[s][g].variance)/sites;
            weights[g]=1/std::max(1e-12,variance);
        }
    }
    for(unsigned s=0;s<sites;++s) {
        result[s]=Blend(samples[s],weights,recipe,&headroom);
        if(fallback) {
            result[s].fallback=true;
            if(result[s].fallbackReason==MeasurementFallbackReason::None)
                result[s].fallbackReason=MeasurementFallbackReason::AutomaticValidInput;
        }
    }
    return result;
}
}
