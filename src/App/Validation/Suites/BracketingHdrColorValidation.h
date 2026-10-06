#pragma once
#include "Raw/Bracketing/HdrColorFusion.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingHdrColor() {
    using namespace Raw::Bracketing;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    BracketingRecipe recipe;recipe.groups.resize(2);
    ColorObservations samples;
    for(auto& site:samples)for(unsigned g=0;g<2;++g) {
        Observation o;o.value=g?.8:1;o.variance=.001;o.measurementVariance=.0008;
        o.uncertaintyVariance=.0002;o.support=3;o.headroom=1;o.finite=true;
        site.push_back(o);
    }
    // An exposure transition through a neutral edge must not choose a different
    // source for red just because red reaches the sensor shoulder first.
    samples[0][0].headroom=0;
    const auto coherent=BlendColor(samples,{.5,.5},recipe);
    const double oldFringe=std::abs(Blend(samples[0],{.5,.5},recipe).value-
        Blend(samples[1],{.5,.5},recipe).value);
    check(oldFringe>.09,"HDR fringe fixture no longer exercises a channel transition");
    for(const auto& p:coherent) {
        check(std::abs(p.value-.8)<1e-12&&p.valid,"HDR exposure transition introduced false color");
        check(std::abs(p.measurementVariance-.0008)<1e-12&&std::abs(p.uncertaintyVariance-.0002)<1e-12,
            "Coordinated HDR lost separate sensor and model noise");
        check(p.support==3,"Coordinated HDR counted adjacent color sites as captures");
    }
    recipe.automatic=false;
    for(auto& site:samples)site[0].headroom=1;
    const auto authored=BlendColor(samples,{.2,.8},recipe);
    for(const auto& p:authored)check(std::abs(p.value-.84)<1e-12&&std::abs(p.actual[0]-.2)<1e-12,
        "Coordinated HDR changed supported authored contributions");
    const double colors[4]={1.8,-.002,.24,.018};
    for(unsigned s=0;s<4;++s)for(auto& o:samples[s])o.value=colors[s];
    const auto edge=BlendColor(samples,{.2,.8},recipe);
    for(unsigned s=0;s<4;++s)check(std::abs(edge[s].value-colors[s])<1e-12,
        "Coordinated HDR neutralized a real color edge or clipped scene-linear samples");
    // No complete exposure, but every color remains recoverable.
    for(unsigned s=0;s<4;++s) {samples[s][s%2].support=0;samples[s][s%2].headroom=0;}
    const auto partial=BlendColor(samples,{.2,.8},recipe);
    for(unsigned s=0;s<4;++s)check(partial[s].valid&&!partial[s].clipped&&std::abs(partial[s].value-colors[s])<1e-12,
        "HDR color coordination discarded recoverable channels");
    // Only the complete exposure is eligible when authored weights request the
    // other source. This exercises coordinated automatic fallback as well.
    for(auto& site:samples)for(auto& o:site){o.support=1;o.headroom=1;}
    samples[0][0].support=0;
    const auto fallback=BlendColor(samples,{1,0},recipe);
    for(const auto& p:fallback)check(p.valid&&p.fallback&&p.actual[1]==1,
        "Authored fallback split colors between different exposures");
    samples[0][0].value=std::numeric_limits<double>::quiet_NaN();
    const auto missing=BlendColor(samples,{.5,.5},recipe);
    for(const auto& p:missing)check(p.valid&&std::isfinite(p.value),
        "A rejected nonfinite channel contaminated a complete usable color");
    samples[0][0].value=colors[0];
    // Real skylight failure: clipped reference greens have no accepted
    // replacement, but its red/blue remain usable. Do not combine those with
    // nearest-sample greens from the rejected shorter exposure.
    recipe.automatic=true;
    const double darkColor[4]={.10,.26,.24,.18};
    for(unsigned s=0;s<4;++s) {
        samples[s][0]={};auto& dark=samples[s][0];
        dark.finite=true;dark.localRejected=true;dark.exposure=.25;dark.fallback=darkColor[s];
        samples[s][1]={};auto& bright=samples[s][1];
        bright.finite=true;bright.exposure=1;bright.variance=.001;bright.headroom=1;
        bright.value=s==0?.49:s==3?.99:1.;bright.fallback=bright.value;
        bright.clipped=s==1||s==2;bright.support=bright.clipped?0:1;
        bright.fallbackClipped=bright.clipped;
    }
    const auto colorFallback=BlendColor(samples,{},recipe);
    for(unsigned s=0;s<4;++s)check(colorFallback[s].value==darkColor[s]&&
        colorFallback[s].actual[0]==1&&colorFallback[s].fallback&&!colorFallback[s].clipped&&
        !colorFallback[s].valid&&colorFallback[s].support==0,
        "HDR fallback mixed exposures or promoted rejected measurements to valid support");
    // Each channel can have support while no exposure supports the whole
    // edge. The rejected short exposure remains the coherent fallback.
    for(unsigned s=1;s<3;++s) {
        samples[s][0].value=darkColor[s];samples[s][0].variance=.001;
        samples[s][0].measurementVariance=.001;samples[s][0].support=1;samples[s][0].headroom=1;
    }
    const auto splitSupport=BlendColor(samples,{},recipe);
    for(unsigned s=0;s<4;++s)check(splitSupport[s].value==darkColor[s]&&splitSupport[s].actual[0]==1,
        "Locally rejected HDR colors were split across incompatible exposures");
    // One accepted capture can land red/blue on the sky while the group's
    // fallback lands on the support. All fallback channels must use the latter.
    for(unsigned s=0;s<4;++s) {
        auto& dark=samples[s][0];dark.support=(s==0||s==3)?1:0;
        dark.value=s==0?1.49:2.66;dark.headroom=dark.support;dark.variance=.001;
    }
    const auto mixedCapture=BlendColor(samples,{},recipe);
    for(unsigned s=0;s<4;++s)check(mixedCapture[s].value==darkColor[s]&&
        !mixedCapture[s].valid&&mixedCapture[s].support==0&&mixedCapture[s].actual[0]==1,
        "HDR fallback mixed accepted captures with another capture's missing channels");
    samples[0][0].fallbackClipped=true;
    const auto clippedCapture=BlendColor(samples,{},recipe);
    for(unsigned s=0;s<4;++s)check(clippedCapture[s].value==darkColor[s]&&
        clippedCapture[s].clipped==(s==0)&&!clippedCapture[s].valid,
        "Unrecoverable HDR highlights split exposures or concealed fallback clipping");
    for(auto& site:samples)site[1].fallbackClipped=false;
    const auto unclippedAlternative=BlendColor(samples,{},recipe);
    for(unsigned s=0;s<4;++s)check(unclippedAlternative[s].value==samples[s][1].fallback&&
        !unclippedAlternative[s].clipped&&unclippedAlternative[s].actual[1]==1,
        "A clipped fallback displaced an available complete unclipped capture");
    auto supportedOnly=samples;
    for(unsigned s=0;s<4;++s)for(unsigned g=0;g<2;++g) {
        auto& v=supportedOnly[s][g];v.support=g==s%2?1:0;v.headroom=1;v.clipped=false;
        v.value=.1+.1*s;v.fallback=.9;v.fallbackClipped=true;v.localRejected=true;
    }
    const auto recoveredColors=BlendColor(supportedOnly,{},recipe);
    for(unsigned s=0;s<4;++s)check(recoveredColors[s].valid&&!recoveredColors[s].fallback&&
        recoveredColors[s].value==supportedOnly[s][s%2].value,
        "Clipped fallback discarded independently recoverable HDR channels");
    recipe.groups.resize(1);
    for(auto& site:samples)site.resize(1);
    const auto single=BlendColor(samples,{1},recipe);
    for(unsigned s=0;s<4;++s) {
        const auto old=Blend(samples[s],{1},recipe);
        check(single[s].value==old.value&&single[s].variance==old.variance&&single[s].support==old.support&&
            single[s].fallbackReason==old.fallbackReason,"Single exposure denoising changed in HDR consolidation");
    }
    std::cout<<"HDR color fusion: channel-transition fringe "<<oldFringe<<" -> 0; authored weights, partial clipping, true color edges, signed range and noise propagation passed.\n";
}
}
