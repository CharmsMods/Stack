#include "BracketingProject.h"
#include <algorithm>
#include <cmath>

namespace Stack::Project {
Raw::Bracketing::BracketingRecipe SuggestBracketingGroups(const RawProjectSnapshot& snapshot,const MultiFrameSourceSet& set) {
    struct Capture {std::string id;double shutter,iso,ev;};
    std::vector<Capture> captures;
    for(const auto& f:set.frames) {
        const auto* asset=FindEmbeddedAsset(snapshot,f.assetId);RawCaptureCompatibilitySummary m;
        if(asset) DeserializeRawCaptureCompatibilitySummary(asset->captureMetadataSummary,m,nullptr);
        const double aperture=m.apertureFNumber>0?2*std::log2(m.apertureFNumber):0;
        const double shutter=(m.exposureTimeSeconds>0?std::log2(m.exposureTimeSeconds):0)-aperture;
        const double iso=m.isoSpeed>0?std::log2(m.isoSpeed):0;
        captures.push_back({f.frameId,shutter,iso,shutter+iso});
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
            recipe.groups.push_back({GenerateStableUuid(),"Exposure "+std::to_string(g+1),true,{}});
        }
        auto& spread=anchors[g];spread.minShutter=std::min(spread.minShutter,c.shutter);spread.maxShutter=std::max(spread.maxShutter,c.shutter);
        spread.minIso=std::min(spread.minIso,c.iso);spread.maxIso=std::max(spread.maxIso,c.iso);
        recipe.groups[g].frames.push_back({c.id,true,false,c.ev-captures[captures.size()/2].ev});
    }
    recipe.knots=Raw::Bracketing::EqualCurves(recipe.groups.size());return recipe;
}
void InitializeBracketing(MultiFrameSourceSet& set,const RawProjectSnapshot& snapshot) {
    auto recipe=SuggestBracketingGroups(snapshot,set);
    const auto identicalCaptureExclusions =
        set.settings.value("identicalCaptureExclusions", nlohmann::json::array());
    set.operationIntent=MultiFrameOperationIntent::RawBurstHdr;set.operationSchemaVersion=kHdrOperationSchemaVersion;
    set.settings=MakeDefaultHdrOperationSettings();
    set.settings["algorithmId"]="stack-bracketing";set.settings["algorithmVersion"]=Raw::Bracketing::RecipeVersion;
    set.settings["bracketing"]=Raw::Bracketing::Serialize(recipe);
    if (!identicalCaptureExclusions.empty())
        set.settings["identicalCaptureExclusions"] = identicalCaptureExclusions;
    set.settings["radiometricAnchorFrameId"]=recipe.originFrameId;
    set.referenceFrameId=recipe.originFrameId;
}
} // namespace Stack::Project
