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
        // Tags suggest editable UI groups only. Missing or malformed values
        // must not produce NaN/Inf recipe EVs or violate sort ordering; the
        // worker calibrates actual relative exposure from registered images.
        const auto logHint=[](double value) {
            return std::isfinite(value)&&value>0?std::log2(value):0.;
        };
        const double aperture=2*logHint(input.apertureFNumber);
        const double shutter=logHint(input.exposureTimeSeconds)-aperture;
        const double iso=logHint(input.isoSpeed);
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
        // The value is only a non-manual display hint. Keep extreme but
        // finite tags within the recipe's editable range rather than rejecting
        // a dataset before its image-derived calibration can run.
        const double relative=std::clamp(c.ev-captures[captures.size()/2].ev,-32.,32.);
        recipe.groups[g].frames.push_back({c.id,true,false,relative});
    }
    recipe.knots=Raw::Bracketing::EqualCurves(recipe.groups.size());return recipe;
}
} // namespace Raw::Bracketing
