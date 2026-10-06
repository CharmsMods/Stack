#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "Persistence/BracketingProject.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

bool EditorModule::CaptureBracketingDraftForCopy(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    Stack::Project::RawProjectSnapshot& snapshot, std::string& error) const {
    if (!m_Bracketing || m_Bracketing->newProject || !m_Bracketing->processRequired) return true;
    const auto& ui = *m_Bracketing;
    auto* set = Stack::Project::FindSourceSet(snapshot, ui.setId);
    if (!m_Project->snapshot || ui.projectId != m_Project->snapshot->projectId ||
        !set || !Stack::Project::IsBracketing(*set)) {
        error = "The bracket draft no longer belongs to the project being copied.";
        return false;
    }
    if (ui.metadataJob || std::any_of(ui.sources.begin(), ui.sources.end(),
            [](const auto& source) { return !source.inspected; })) {
        error = "Wait for capture metadata inspection before copying the bracket draft.";
        return false;
    }
    auto recipe = ui.recipe;
    if (!Raw::Bracketing::Validate(recipe, error, true)) return false;

    std::unordered_map<std::string, std::string> remap;
    for (const auto& source : ui.sources) {
        if (std::any_of(set->frames.begin(), set->frames.end(),
                [&](const auto& frame) { return frame.frameId == source.frameId; })) continue;
        if (!source.error.empty()) { error = source.error; return false; }
        Stack::Project::EmbeddedAssetRecord asset;
        if (!store->StageAssetFile(transaction, source.path, Stack::Project::MultiFrameInputFamily::Raw,
                Stack::Project::SerializeRawCaptureCompatibilitySummary(source.metadata), asset, &error)) return false;
        const auto existing = std::find_if(set->frames.begin(), set->frames.end(),
            [&](const auto& frame) { return frame.assetId == asset.assetId; });
        if (existing != set->frames.end()) {
            remap[source.frameId] = existing->frameId;
            continue;
        }
        if (!Stack::Project::FindEmbeddedAsset(snapshot, asset.assetId)) snapshot.embeddedAssets.push_back(asset);
        Stack::Project::SourceSetFrame frame;
        frame.frameId = source.frameId;
        frame.assetId = asset.assetId;
        set->frames.push_back(std::move(frame));
    }
    const auto previousGroups = recipe.groups;
    std::unordered_set<std::string> used;
    for (auto& group : recipe.groups) {
        for (auto& frame : group.frames) {
            if (const auto found = remap.find(frame.id); found != remap.end()) frame.id = found->second;
            if (std::none_of(set->frames.begin(), set->frames.end(),
                    [&](const auto& stored) { return stored.frameId == frame.id; })) {
                error = "A pending bracket capture could not be included in the project copy.";
                return false;
            }
        }
        group.frames.erase(std::remove_if(group.frames.begin(), group.frames.end(),
            [&](const auto& frame) { return !used.insert(frame.id).second; }), group.frames.end());
    }
    recipe.groups.erase(std::remove_if(recipe.groups.begin(), recipe.groups.end(),
        [](const auto& group) { return group.frames.empty(); }), recipe.groups.end());
    if (const auto found = remap.find(recipe.originFrameId); found != remap.end()) recipe.originFrameId = found->second;
    Raw::Bracketing::RemapCurves(recipe, previousGroups);
    Raw::Bracketing::ConstrainEnabledCurves(recipe);
    if (!Raw::Bracketing::Validate(recipe, error, true)) return false;
    auto selection = nlohmann::json::array();
    used.clear();
    for (const auto& source : ui.sources) {
        const auto found = remap.find(source.frameId);
        const auto& id = found == remap.end() ? source.frameId : found->second;
        if (used.insert(id).second) selection.push_back(id);
    }
    // Copy the draft without promoting it or changing the submitted input revision.
    set->settings["bracketingDraft"] = Raw::Bracketing::Serialize(recipe);
    set->settings["bracketingDraftSelection"] = std::move(selection);
    snapshot.projectName = ui.projectName;
    return true;
}
