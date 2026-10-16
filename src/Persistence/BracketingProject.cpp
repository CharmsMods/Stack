#include "BracketingProject.h"
#include "Raw/Bracketing/GroupSuggestion.h"

namespace Stack::Project {
Raw::Bracketing::BracketingRecipe SuggestBracketingGroups(const RawProjectSnapshot& snapshot,const MultiFrameSourceSet& set) {
    std::vector<Raw::Bracketing::CaptureExposure> captures;
    captures.reserve(set.frames.size());
    for (const auto& frame : set.frames) {
        const auto* asset=FindEmbeddedAsset(snapshot,frame.assetId);
        RawCaptureCompatibilitySummary metadata;
        if (asset) DeserializeRawCaptureCompatibilitySummary(asset->captureMetadataSummary,metadata,nullptr);
        captures.push_back({frame.frameId,metadata.exposureTimeSeconds,metadata.apertureFNumber,metadata.isoSpeed});
    }
    return Raw::Bracketing::SuggestGroups(captures);
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
