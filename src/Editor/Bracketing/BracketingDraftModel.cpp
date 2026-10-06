#include "BracketingSession.h"
#include "Persistence/BracketingProject.h"
#include <algorithm>
#include <unordered_set>

namespace Stack::Editor {
void RegroupBracketingDraft(BracketingSession& ui) {
    Project::RawProjectSnapshot snapshot;
    Project::MultiFrameSourceSet set;
    std::unordered_set<std::string> selected;
    for(const auto& source:ui.sources) {
        selected.insert(source.frameId);
        if(!source.inspected)continue;
        Project::EmbeddedAssetRecord asset;
        asset.assetId=source.frameId;
        asset.captureMetadataSummary=Project::SerializeRawCaptureCompatibilitySummary(source.metadata);
        snapshot.embeddedAssets.push_back(std::move(asset));
        Project::SourceSetFrame frame;frame.frameId=source.frameId;frame.assetId=source.frameId;
        set.frames.push_back(std::move(frame));
    }
    const auto suggested=Project::SuggestBracketingGroups(snapshot,set);
    const auto previous=ui.recipe.groups;
    // A published origin remains as a disabled calibration anchor if deselected.
    for(auto& group:ui.recipe.groups) {
        group.frames.erase(std::remove_if(group.frames.begin(),group.frames.end(),[&](auto& frame) {
            if(selected.count(frame.id))return false;
            if(!ui.newProject&&frame.id==ui.recipe.originFrameId){frame.enabled=false;return false;}
            return true;
        }),group.frames.end());
    }
    ui.recipe.groups.erase(std::remove_if(ui.recipe.groups.begin(),ui.recipe.groups.end(),
        [](const auto& group){return group.frames.empty();}),ui.recipe.groups.end());
    std::unordered_set<std::string> retained;
    for(const auto& group:ui.recipe.groups)for(const auto& frame:group.frames)retained.insert(frame.id);
    for(const auto& group:suggested.groups) {
        auto target=ui.recipe.groups.end();
        for(auto it=ui.recipe.groups.begin();it!=ui.recipe.groups.end();++it) {
            if(std::any_of(group.frames.begin(),group.frames.end(),[&](const auto& f){
                return std::any_of(it->frames.begin(),it->frames.end(),[&](const auto& old){return old.id==f.id;});
            })){target=it;break;}
        }
        std::vector<Raw::Bracketing::Frame> added;
        for(const auto& frame:group.frames)if(!retained.count(frame.id))added.push_back(frame);
        if(added.empty())continue;
        if(target==ui.recipe.groups.end()) {
            auto next=group;next.frames=std::move(added);ui.recipe.groups.push_back(std::move(next));
        } else target->frames.insert(target->frames.end(),added.begin(),added.end());
    }
    if(ui.newProject||ui.recipe.originFrameId.empty())ui.recipe.originFrameId=suggested.originFrameId;
    if(previous.empty())ui.recipe.knots=Raw::Bracketing::EqualCurves(ui.recipe.groups.size());
    else Raw::Bracketing::RemapCurves(ui.recipe,previous);
    Raw::Bracketing::ConstrainEnabledCurves(ui.recipe);
    ui.selectedPoint=-1;
}
}
