#include "HdrColorFusion.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
namespace {
struct Donor {
    std::size_t group=0;
    double scale=1;
    double exposure=0;
    double colorError=0;
    unsigned clippedSites=0;
};

// A clipped reference is a lower bound. Trust its surviving channels for
// location and brightness. A rejected exposure can suggest a color ratio but
// cannot supply a valid observation or move the edge onto another surface.
bool EstimateDonor(const ColorObservations& samples,std::size_t g,const std::array<Pixel,4>& pixels,
    std::size_t reference,unsigned anchors,Donor& donor) {
    const double exposure=samples[0][g].exposure;
    if(!(exposure>0&&exposure<samples[0][reference].exposure))return false;
    double cross=0,squared=0;
    unsigned clippedSites=0;
    for(unsigned s=0;s<4;++s) {
        const auto& v=samples[s][g];
        clippedSites+=v.fallbackClipped;
        if(!v.finite||(anchors&&v.fallbackClipped)||!(v.fallback>0)||!std::isfinite(v.fallback)||v.exposure!=exposure)return false;
        if(pixels[s].valid) {cross+=v.fallback*pixels[s].value;squared+=v.fallback*v.fallback;}
    }
    const double scale=anchors&&squared>0?cross/squared:1.;
    if(!std::isfinite(scale)||scale<=0)return false;
    double colorError=0;
    for(unsigned s=0;s<4;++s) {
        const double estimate=scale*samples[s][g].fallback;
        if(!(estimate>0)||!std::isfinite(estimate))return false;
        if(pixels[s].valid) {
            const auto& p=pixels[s];
            // Tolerate noise in a surviving channel, but reject an incompatible hue.
            const double tolerance=std::max(.10*std::abs(p.value),4*std::sqrt(std::max(0.,p.measurementVariance)));
            colorError=std::max(colorError,std::abs(estimate-p.value)/std::max(1e-12,tolerance));
        } else {
            const double floor=samples[s][reference].fallback;
            if(!anchors && estimate<.97*floor)return false;
            // Fade before the saturation-bound veto instead of jumping at 97%.
            colorError=std::max(colorError,2.*std::max(0.,floor-estimate)/std::max(1e-12,.03*floor));
        }
    }
    donor={g,scale,exposure,colorError,clippedSites};return true;
}

bool DonorColorLess(const ColorObservations& samples,const Donor& a,const Donor& b) {
    for(unsigned s=0;s<4;++s) {
        const double av=a.scale*samples[s][a.group].fallback,bv=b.scale*samples[s][b.group].fallback;
        if(av!=bv)return av<bv;
    }
    // Equal bounds can still have different surviving channels. Keep the
    // clipping mask deterministic too, including when neutral fill is needed.
    for(unsigned s=0;s<4;++s) {
        const bool ac=samples[s][a.group].fallbackClipped,bc=samples[s][b.group].fallbackClipped;
        if(ac!=bc)return ac<bc;
    }
    return false;
}
double ConfidenceFade(double x) {
    x=std::clamp(x,0.,1.);
    return x*x*(3.-2.*x);
}

// Blend whole donor colors with one weight per exposure. Anchor disagreement
// fades the estimate into the already prepared neutral/boundary endpoint;
// crossing a donor acceptance threshold must not replace that endpoint abruptly.
struct DonorBlend {
    std::array<double,4> value{};
    std::array<double,64> weights{};
    double confidence=0;
};
DonorBlend BlendDonors(const ColorObservations& samples,const std::array<Pixel,4>& pixels,
    const std::array<Donor,64>& donors,std::size_t count,unsigned anchors) {
    DonorBlend result;
    double total=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& donor=donors[i];
        double weight=1.-ConfidenceFade((donor.colorError-.5)/1.5);
        if(anchors==1)weight*=1.-ConfidenceFade((std::abs(donor.scale-1.)-.05)/.05);
        // No donor can override a reliable channel, even with high confidence.
        result.confidence=std::max(result.confidence,weight);
        total+=weight;
        result.weights[donor.group]+=weight;
        for(unsigned site=0;site<4;++site)if(!pixels[site].valid)
            result.value[site]+=weight*donor.scale*samples[site][donor.group].fallback;
    }
    if(total>0) {
        for(auto& v:result.value)v/=total;
        for(auto& w:result.weights)w/=total;
        double disagreement=0;
        for(unsigned site=0;site<4;++site)if(!pixels[site].valid) {
            double variance=0;
            for(std::size_t i=0;i<count;++i) {
                const auto& donor=donors[i];
                const double delta=donor.scale*samples[site][donor.group].fallback-result.value[site];
                variance+=result.weights[donor.group]*delta*delta;
            }
            disagreement=std::max(disagreement,std::sqrt(variance)/std::max(1e-12,result.value[site]));
        }
        result.confidence*=1.-ConfidenceFade((disagreement-.05)/.15);
    }
    return result;
}
}

bool RecoverClippedBayerFallback(const ColorObservations& samples,const BracketingRecipe& recipe,
    std::array<Pixel,4>& pixels) {
    std::size_t ref=recipe.groups.size();
    for(unsigned s=0;s<4&&ref==recipe.groups.size();++s)if(!pixels[s].valid) {
        const auto& site=samples[s];
        const auto marked=std::find_if(site.begin(),site.end(),[](const Observation& o){
            return o.clippedReference!=ClippedReference::None;
        });
        if(marked!=site.end())ref=static_cast<std::size_t>(marked-site.begin());
    }
    if(ref==recipe.groups.size())return false;
    unsigned anchors=0;
    for(unsigned s=0;s<4;++s) {
        if(pixels[s].valid){++anchors;continue;}
        const auto& v=samples[s][ref];
        if(v.clippedReference!=ClippedReference::None&&
           (!v.finite||!v.fallbackClipped||!(v.fallback>0)||!std::isfinite(v.fallback)))return false;
    }
    if(anchors==4)return false;

    std::array<Donor,64> donors;
    std::size_t n=0;
    const bool needDonor=!anchors||std::any_of(samples.begin(),samples.end(),[&](const auto& site){
        return site[ref].clippedReference==ClippedReference::LowerBound||
            site[ref].clippedReference==ClippedReference::NeutralCompletion;
    });
    for(std::size_t g=0;needDonor&&g<recipe.groups.size();++g)
        if(recipe.groups[g].enabled&&EstimateDonor(samples,g,pixels,ref,anchors,donors[n]))++n;
    if(!anchors&&n) {
        // No surviving reference color can set a scale. Use one mapped,
        // exposure-normalized capture that meets every saturation bound.
        // This is display fallback only and retains zero measurement support.
        const auto donor=std::max_element(donors.begin(),donors.begin()+n,
            [&](const Donor& a,const Donor& b){
                if(a.clippedSites!=b.clippedSites)return a.clippedSites>b.clippedSites;
                if(a.exposure!=b.exposure)return a.clippedSites==4?a.exposure>b.exposure:a.exposure<b.exposure;
                return DonorColorLess(samples,b,a);
            });
        // If the shorter capture also clips, use its own saturation bounds.
        // Carry forward the as-shot neutral only for missing channels.
        double neutralScale=0;
        const bool hasNeutral=std::all_of(samples.begin(),samples.end(),[&](const auto& site){
            return site[ref].clippedReference==ClippedReference::EstimatedColor && site[ref].value>0;
        });
        if(donor->clippedSites && hasNeutral)for(unsigned s=0;s<4;++s)
            neutralScale=std::max(neutralScale,samples[s][donor->group].fallback/samples[s][ref].value);
        for(unsigned s=0;s<4;++s) {
            auto& p=pixels[s];
            const bool rejected=p.localRejected;
            p={};p.localRejected=rejected;p.fallback=true;
            p.value=std::max(samples[s][ref].fallback,samples[s][donor->group].fallback);
            p.clipped=samples[s][donor->group].fallbackClipped;
            if(p.clipped && hasNeutral)p.value=std::max(p.value,neutralScale*samples[s][ref].value);
            p.actual[donor->group]=1;
            p.fallbackReason=MeasurementFallbackReason::HighlightFallback;
        }
        return true;
    }
    const auto donor=BlendDonors(samples,pixels,donors,n,anchors);
    for(unsigned s=0;s<4;++s) {
        auto& p=pixels[s];
        if(p.valid||samples[s][ref].clippedReference==ClippedReference::None)continue;
        const auto& reference=samples[s][ref];
        const double floor=reference.fallback;
        double value=floor;
        bool estimated=false;
        double donorWeight=0;
        if(reference.clippedReference==ClippedReference::EstimatedColor) {
            value=std::max(floor,reference.value);
            estimated=anchors>0;
        } else {
            if(reference.clippedReference==ClippedReference::NeutralCompletion)
                value=std::max(floor,reference.value);
            if(donor.confidence>0) {
                donorWeight=donor.confidence;
                value+=donorWeight*(std::max(floor,donor.value[s])-value);
                estimated=donorWeight>=1.;
            }
        }
        const bool rejected=p.localRejected;p={};
        p.value=std::max(floor,value);p.fallback=true;p.localRejected=rejected;p.clipped=!estimated;
        p.actual[ref]=1.-donorWeight;
        for(std::size_t g=0;g<recipe.groups.size();++g)p.actual[g]+=donorWeight*donor.weights[g];
        p.fallbackReason=MeasurementFallbackReason::HighlightFallback;
    }
    return true;
}
} // namespace Raw::Bracketing
