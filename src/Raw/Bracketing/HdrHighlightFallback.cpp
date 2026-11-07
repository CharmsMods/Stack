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
        } else if(estimate<.97*samples[s][reference].fallback)return false;
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
bool PreferDonor(const ColorObservations& samples,const Donor& a,const Donor& b) {
    if(a.colorError!=b.colorError)return a.colorError<b.colorError;
    if(a.exposure!=b.exposure)return a.exposure>b.exposure;
    return DonorColorLess(samples,a,b);
}

const Donor* CoherentDonor(const ColorObservations& samples,const std::array<Pixel,4>& pixels,
    const std::array<Donor,64>& donors,std::size_t count) {
    if(!count)return nullptr;
    std::array<double,4> median{};
    std::array<double,64> values{};
    for(unsigned s=0;s<4;++s)if(!pixels[s].valid) {
        for(std::size_t i=0;i<count;++i)
            values[i]=donors[i].scale*samples[s][donors[i].group].fallback;
        auto middle=values.begin()+count/2;
        std::nth_element(values.begin(),middle,values.begin()+count);
        median[s]=*middle;
    }
    // Use the channel medians only to rank whole donor colors. Selecting each
    // missing channel's median independently assembles a hue no capture saw.
    // Relative distances also keep the choice invariant to the radiance gauge.
    const Donor* best=nullptr;double bestDistance=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& donor=donors[i];double distance=0;
        std::array<double,4> color{};
        for(unsigned s=0;s<4;++s)if(!pixels[s].valid) {
            color[s]=donor.scale*samples[s][donor.group].fallback;
            const double delta=(color[s]-median[s])/std::max(color[s],median[s]);
            distance+=delta*delta;
        }
        const bool betterTie=best&&distance==bestDistance&&PreferDonor(samples,donor,*best);
        if(!best||distance<bestDistance||betterTie) {
            best=&donor;bestDistance=distance;
        }
    }
    return best;
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
                const auto& closer=PreferDonor(samples,a,b)?a:b;
                if(agree&&(!best||PreferDonor(samples,closer,*best)))best=&closer;
            }
            if(best)donors[0]=*best;
            n=best?1:0;
        }
    }
    const Donor* donor=CoherentDonor(samples,pixels,donors,n);
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
        } else if(donor) {
            // With only one surviving reference color a displaced donor has no
            // independent constraint on its chromaticity.
            if(anchors>1||n>1||std::abs(donor->scale-1.)<=.10) {
                chosen=donor->group;
                value=std::max(floor,donor->scale*samples[s][chosen].fallback);estimated=true;
            }
        }
        if(!estimated&&samples[s][ref].clippedReference==ClippedReference::NeutralCompletion) {
            // Prefer a constrained donor whenever available. Neutral completion
            // only prevents an unresolved clipped channel becoming false color;
            // keep it clipped and unsupported, even with surviving anchors.
            value=std::max(floor,samples[s][ref].value);
        }
        const bool rejected=p.localRejected;p={};
        p.value=value;p.actual[chosen]=1;p.fallback=true;p.localRejected=rejected;p.clipped=!estimated;
        p.fallbackReason=MeasurementFallbackReason::HighlightFallback;
    }
    return true;
}
} // namespace Raw::Bracketing
