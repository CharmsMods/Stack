#pragma once
#include "HdrColorFusion.h"
#include "SceneColorEvidence.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
struct NeutralGuideTile { unsigned left=0,top=0,width=0,height=0; };

// Consume the same measurements used by the group cache: canonicalized floats
// for multiple inputs, original GPU doubles for the uncached single-input path.
// Callers own disjoint guide regions and per-tile histograms; reduce by tile index.
// Invalid shapes leave every destination untouched.
inline bool BuildNeutralGuideTile(const BracketingRecipe& recipe,const LuminanceTransform& luminance,
    const NeutralGuideTile& tile,unsigned cellWidth,const std::vector<std::vector<Observation>>& observations,
    std::vector<float>& guideEv,std::array<float,256>& counts,std::vector<double>& histogram,std::string& error,
    SceneColorEvidence::Tile* sceneColor=nullptr) {
    const auto groups=recipe.groups.size();
    const auto pixels=std::uint64_t(tile.width)*tile.height;
    if(!groups||groups>Pixel{}.actual.size()||observations.size()!=groups||!cellWidth||!tile.width||!tile.height||
       ((tile.left|tile.top|tile.width|tile.height)&1u)||guideEv.size()%cellWidth||
       std::uint64_t(tile.left)/2+tile.width/2>cellWidth||
       std::uint64_t(tile.top)/2+tile.height/2>guideEv.size()/cellWidth||
       std::any_of(observations.begin(),observations.end(),[&](const auto& g){return g.size()!=pixels;})) {
        error="Neutral guide measurements do not match their output tile.";return false;
    }
    if(sceneColor)sceneColor->Initialize(cellWidth*2,unsigned(guideEv.size()/cellWidth)*2,
        tile.left,tile.top,tile.width,tile.height);
    counts.fill(0);histogram.assign(256*groups,0);
    ColorObservations colorInput;std::vector<double> weights(groups);
    for(unsigned y=0;y<tile.height;y+=2)for(unsigned x=0;x<tile.width;x+=2) {
        std::array<double,4> cell{};
        std::fill(weights.begin(),weights.end(),0.0);
        GatherColorCell(observations,std::size_t(y)*tile.width+x,tile.width,colorInput);
        const auto color=BlendColor(colorInput,{},recipe);
        if(sceneColor)sceneColor->Add(tile.left+x,tile.top+y,
            SceneColorEvidence::Estimate(colorInput,color,recipe,luminance.channels));
        for(unsigned c=0;c<4;++c) {
            const auto& b=color[c];cell[c]=b.value;
            for(std::size_t g=0;g<groups;++g)weights[g]+=b.actual[g]*.25;
        }
        const double ev=std::log2(std::max(1e-9,Luminance(luminance,cell))/.18);
        guideEv[((std::size_t(tile.top)+y)/2)*cellWidth+(std::size_t(tile.left)+x)/2]=static_cast<float>(ev);
        const auto bin=static_cast<std::size_t>(std::clamp((ev+12)/20*255,0.0,255.0));
        counts[bin]+=1;
        for(std::size_t g=0;g<groups;++g)histogram[bin*groups+g]+=weights[g];
    }
    return true;
}
}
