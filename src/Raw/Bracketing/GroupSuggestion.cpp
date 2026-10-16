#include "GroupSuggestion.h"
#include "NodeMath/Uuid.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
BracketingRecipe SuggestGroups(const std::vector<CaptureExposure>& inputs) {
    struct Capture { std::string id; double shutter, iso, ev; };
    std::vector<Capture> captures;
    captures.reserve(inputs.size());
    for (const auto& input : inputs) {
        const double aperture=input.apertureFNumber>0?2*std::log2(input.apertureFNumber):0;
        const double shutter=(input.exposureTimeSeconds>0?std::log2(input.exposureTimeSeconds):0)-aperture;
        const double iso=input.isoSpeed>0?std::log2(input.isoSpeed):0;
        captures.push_back({input.frameId,shutter,iso,shutter+iso});
    }
    std::stable_sort(captures.begin(),captures.end(),[](const auto& a,const auto& b){return a.ev<b.ev;});
    Raw::Bracketing::BracketingRecipe recipe;
    if(captures.empty()) return recipe;
    recipe.originFrameId=captures[captures.size()/2].id;
    struct Spread {double minShutter,maxShutter,minIso,maxIso;};
    std::vector<Spread> anchors;
    for(const auto& c:captures) {
        std::size_t g=0;
        for(;g<anchors.size();++g) if(std::max(c.shutter,anchors[g].maxShutter)-std::min(c.shutter,anchors[g].minShutter)<=.15&&std::max(c.iso,anchors[g].maxIso)-std::min(c.iso,anchors[g].minIso)<=.05) break;
        if(g==anchors.size()) {
            anchors.push_back({c.shutter,c.shutter,c.iso,c.iso});
            recipe.groups.push_back({Stack::NodeMath::GenerateCanonicalUuid(),"Exposure "+std::to_string(g+1),true,{}});
        }
        auto& spread=anchors[g];spread.minShutter=std::min(spread.minShutter,c.shutter);spread.maxShutter=std::max(spread.maxShutter,c.shutter);
        spread.minIso=std::min(spread.minIso,c.iso);spread.maxIso=std::max(spread.maxIso,c.iso);
        recipe.groups[g].frames.push_back({c.id,true,false,c.ev-captures[captures.size()/2].ev});
    }
    recipe.knots=Raw::Bracketing::EqualCurves(recipe.groups.size());return recipe;
}
} // namespace Raw::Bracketing
