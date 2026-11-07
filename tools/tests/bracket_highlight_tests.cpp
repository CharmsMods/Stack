#include "Raw/Bracketing/HdrColorFusion.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void CheckCompletion(unsigned phase,unsigned reference,double gauge,bool donor,bool boundary) {
    using namespace Raw::Bracketing;
    BracketingRecipe recipe;recipe.groups.resize(2);
    ColorObservations samples;std::array<Pixel,4> pixels{};
    const double measured[4]={.5,1.,1.,.8};
    for(unsigned s=0;s<4;++s) {
        const unsigned c=(s+phase)%4;const bool missing=c==1||c==2;
        samples[s].resize(2);auto& ref=samples[s][reference];
        ref.finite=true;ref.exposure=1;ref.fallback=gauge*measured[c];
        ref.value=gauge*(missing?(boundary?1.3:1.25):measured[c]);
        ref.fallbackClipped=ref.clipped=missing;
        ref.clippedReference=missing?(boundary?ClippedReference::EstimatedColor:
            ClippedReference::NeutralCompletion):ClippedReference::None;
        if(!missing) {
            auto& p=pixels[s];p.valid=true;p.value=ref.value;p.support=2;
            p.measurementVariance=gauge*gauge*.001;p.actual[reference]=1;
        }
        if(donor) {
            auto& alt=samples[s][1-reference];alt.finite=true;alt.localRejected=true;
            alt.exposure=.25;alt.fallback=gauge*(missing?1.4:measured[c]);
        }
    }
    const auto before=pixels;
    Check(RecoverClippedBayerFallback(samples,recipe,pixels),"Missing clipped color was not handled");
    for(unsigned s=0;s<4;++s) {
        const unsigned c=(s+phase)%4;const bool missing=c==1||c==2;
        const auto& p=pixels[s];
        if(!missing) {
            Check(p.valid&&p.value==before[s].value&&p.support==before[s].support&&
                p.measurementVariance==before[s].measurementVariance&&p.actual==before[s].actual,
                "Highlight completion changed measured color or its evidence");
        } else {
            const double expected=boundary?1.3:donor?1.4:1.25;
            Check(std::abs(p.value/gauge-expected)<1e-12,"Incorrect highlight completion priority");
            Check(!p.valid&&p.support==0&&p.fallback&&p.fallbackReason==MeasurementFallbackReason::HighlightFallback,
                "Estimated highlight acquired measurement support");
            Check(p.clipped==(!boundary&&!donor),"Unresolved color lost its clipping marker");
            Check(p.actual[donor&&!boundary?1-reference:reference]==1,"Incorrect completion provenance");
            Check(p.value>=samples[s][reference].fallback,"Completion contradicted saturation bound");
        }
    }
}
}
int main() {
    try {
        for(unsigned phase=0;phase<4;++phase)for(unsigned reference=0;reference<2;++reference)
            for(double gauge:{.125,1.,8.})for(bool donor:{false,true})for(bool boundary:{false,true})
                CheckCompletion(phase,reference,gauge,donor,boundary);
        std::cout<<"96 highlight cases passed: CFA phase, group order, radiance scale, donor priority, measured-color preservation and clipping evidence.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
