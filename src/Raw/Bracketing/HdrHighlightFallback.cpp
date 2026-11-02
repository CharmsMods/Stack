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
        if(pixels[s].valid) {
            const auto& p=pixels[s];
            // Tolerate noise in a surviving channel, but reject an incompatible hue.
            const double tolerance=std::max(.10*std::abs(p.value),4*std::sqrt(std::max(0.,p.measurementVariance)));
            colorError=std::max(colorError,std::abs(estimate-p.value)/std::max(1e-12,tolerance));
        } else if(estimate<.97*samples[s][reference].fallback)return false;
    }
    donor={g,scale,exposure,colorError,clippedSites};return true;
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
        return site[ref].clippedReference==ClippedReference::LowerBound;
    });
    for(std::size_t g=0;needDonor&&g<recipe.groups.size();++g)
        if(recipe.groups[g].enabled&&EstimateDonor(samples,g,pixels,ref,anchors,donors[n]))++n;
    if(!anchors&&n) {
        // No surviving reference color can set a scale. Use one mapped,
        // exposure-normalized capture that meets every saturation bound.
        // This is display fallback only and retains zero measurement support.
        const auto donor=std::max_element(donors.begin(),donors.begin()+n,
            [](const Donor& a,const Donor& b){
                if(a.clippedSites!=b.clippedSites)return a.clippedSites>b.clippedSites;
                return a.clippedSites==4?a.exposure>b.exposure:a.exposure<b.exposure;
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
    if(anchors&&n) {
        auto end=std::partition(donors.begin(),donors.begin()+n,[](const Donor& d){return d.colorError<=1.;});
        if(end!=donors.begin())n=static_cast<std::size_t>(end-donors.begin());
        else {
            // At a noisy or subpixel color edge no donor may match every anchor.
            // Use independent exposures only if they predict the same missing
            // channels; this avoids isolated magenta holes at a hard color gate.
            const Donor* best=nullptr;
            for(std::size_t i=0;i<n;++i)for(std::size_t j=i+1;j<n;++j) {
                const auto& a=donors[i];const auto& b=donors[j];
                if(a.colorError>2.||b.colorError>2.||a.exposure==b.exposure)continue;
                bool agree=true;
                for(unsigned s=0;s<4;++s)if(!pixels[s].valid) {
                    const double av=a.scale*samples[s][a.group].fallback,bv=b.scale*samples[s][b.group].fallback;
                    agree&=std::abs(av-bv)<=.10*std::min(av,bv);
                }
                const auto& closer=a.colorError<b.colorError?a:b;
                if(agree&&(!best||closer.colorError<best->colorError))best=&closer;
            }
            if(best)donors[0]=*best;
            n=best?1:0;
        }
    }
    for(unsigned s=0;s<4;++s) {
        auto& p=pixels[s];
        if(p.valid||samples[s][ref].clippedReference==ClippedReference::None)continue;
        auto chosen=ref;
        const double floor=samples[s][ref].fallback;
        double value=samples[s][chosen].fallback;
        bool estimated=false;
        if(samples[s][ref].clippedReference==ClippedReference::EstimatedColor) {
            value=std::max(floor,samples[s][ref].value);
            estimated=anchors>0;
        } else if(n) {
            const auto brightness=[&](const Donor& d){return d.scale*samples[s][d.group].fallback;};
            auto mid=donors.begin()+n/2;
            std::nth_element(donors.begin(),mid,donors.begin()+n,
                [&](const Donor& a,const Donor& b){return brightness(a)<brightness(b);});
            // With only one surviving reference color a displaced donor has no
            // independent constraint on its chromaticity.
            if(anchors>1||n>1||std::abs(mid->scale-1.)<=.10) {
                chosen=mid->group;value=std::max(floor,brightness(*mid));estimated=true;
            }
        }
        const bool rejected=p.localRejected;p={};
        p.value=value;p.actual[chosen]=1;p.fallback=true;p.localRejected=rejected;p.clipped=!estimated;
        p.fallbackReason=MeasurementFallbackReason::HighlightFallback;
    }
    return true;
}
} // namespace Raw::Bracketing
