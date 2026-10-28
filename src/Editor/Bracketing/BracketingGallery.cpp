#include "BracketingGallery.h"
#include "Editor/EditorModule.h"
#include "App/AppPaths.h"
#include "Raw/Bracketing/Recipe.h"
#include "Persistence/BracketingResultStore.h"
#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace Stack::Editor {
void UpdateBracketingGalleryProject(RawWorkspace::WorkspaceState& workspace,const Project::RawProjectSnapshot& snapshot,
    const std::filesystem::path& path, const Project::ProjectStoreHandle& store) {
    const auto* set=Project::FindSourceSet(snapshot,snapshot.activeSourceSetId);
    if(!set||!set->settings.contains("bracketing"))return;
    Raw::Bracketing::BracketingRecipe recipe;std::string error;
    if(!Raw::Bracketing::Deserialize(set->settings["bracketing"],recipe,error))return;
    auto entry=std::find_if(workspace.sourceSetProjects.begin(),workspace.sourceSetProjects.end(),[&](const auto& p){return p.projectId==snapshot.projectId;});
    if(entry==workspace.sourceSetProjects.end()){workspace.sourceSetProjects.emplace_back();entry=std::prev(workspace.sourceSetProjects.end());}
    entry->projectId=snapshot.projectId;entry->projectName=snapshot.projectName;entry->absolutePath=path;
    entry->bracketingProject=true;
    entry->hasSavedBracketingResult = false;
    entry->sourceKeys.clear();
    entry->referenceSourceKey.clear();
    entry->multiFrameProject=true;entry->status=RawWorkspace::ProjectStatus::Existing;
    entry->totalFrameCount=entry->rawSetCount=entry->rasterSetCount=0;
    entry->sourceSetCount=snapshot.sourceSets.size();
    if(!snapshot.coverThumbnailBytes.empty()&&entry->coverThumbnailBytes!=snapshot.coverThumbnailBytes) {
        entry->coverThumbnailBytes=snapshot.coverThumbnailBytes;
        const auto directory=AppPaths::GetCacheDirectory()/"BracketingCovers";
        std::error_code ec;std::filesystem::create_directories(directory,ec);
        std::uint64_t hash=14695981039346656037ull;
        for(auto c:snapshot.coverThumbnailBytes){hash^=c;hash*=1099511628211ull;}
        const auto target=directory/(snapshot.projectId+"-"+std::to_string(hash)+".png");
        std::ofstream output(target,std::ios::binary);output.write(reinterpret_cast<const char*>(snapshot.coverThumbnailBytes.data()),snapshot.coverThumbnailBytes.size());
        if(output)entry->coverThumbnailCachePath=target;
    }
    struct PreparedSet {
        const Project::MultiFrameSourceSet* set = nullptr;
        bool completed = false;
        std::uint64_t selectedFrameCount = 0;
        std::vector<std::pair<const Project::SourceSetFrame*, const Project::EmbeddedAssetRecord*>> frames;
    };
    std::vector<PreparedSet> prepared;
    std::vector<std::string> identities;
    for (const auto& sourceSet : snapshot.sourceSets) {
        PreparedSet input;
        input.set = &sourceSet;
        input.completed = store && Project::HasSavedBracketingResult(store, snapshot, sourceSet);
        entry->hasSavedBracketingResult |= input.completed;
        if (sourceSet.inputFamily == Project::MultiFrameInputFamily::Raw) ++entry->rawSetCount;
        else ++entry->rasterSetCount;
        for (const auto& frame : sourceSet.frames) {
            const auto selection = sourceSet.settings.find("bracketingSelection");
            if (selection != sourceSet.settings.end() &&
                (!selection->is_array() || std::find(selection->begin(), selection->end(), frame.frameId) == selection->end())) continue;
            ++entry->totalFrameCount;
            ++input.selectedFrameCount;
            const auto* asset = Project::FindEmbeddedAsset(snapshot, frame.assetId);
            if (!asset) continue;
            identities.push_back(asset->sha256);
            input.frames.emplace_back(&frame, asset);
        }
        prepared.push_back(std::move(input));
    }
    entry->sourceIdentityKey = RawWorkspace::BuildGallerySourceIdentityKey(std::move(identities));
    for(auto& source:workspace.sources) {
        auto& memberships=source.sourceSetProjectMemberships;
        memberships.erase(std::remove_if(memberships.begin(),memberships.end(),[&](const auto& m){return m.projectId==snapshot.projectId;}),memberships.end());
        bool member = false;
        for (const auto& input : prepared) {
            const auto match = std::find_if(input.frames.begin(), input.frames.end(), [&](const auto& candidate) {
                const auto* asset = candidate.second;
                return (!source.fingerprint.empty() && source.fingerprint == asset->sha256) ||
                    source.absolutePath.lexically_normal() == std::filesystem::path(asset->originalSourcePath).lexically_normal();
            });
            if (match == input.frames.end()) continue;
            RawWorkspace::SourceSetProjectMembership membership;
            membership.projectId = snapshot.projectId;
            membership.projectName = snapshot.projectName;
            membership.projectPath = path;
            membership.sourceSetId = input.set->sourceSetId;
            membership.sourceSetName = input.set->name;
            membership.projectIsMultiFrame = true;
            membership.projectCoverThumbnailCachePath = entry->coverThumbnailCachePath;
            membership.hasSavedBracketingResult = input.completed;
            membership.sourceSetFrameCount = input.selectedFrameCount;
            memberships.push_back(std::move(membership));
            member = true;
            if (entry->referenceSourceKey.empty() || match->first->frameId == input.set->referenceFrameId) {
                entry->referenceSourceKey = source.relativePathKey;
            }
        }
        if (member) entry->sourceKeys.push_back(source.relativePathKey);
    }
}
void AddBracketingResultStacks(std::vector<RawWorkspace::RawGallerySimilarityStack>& stacks,
    const RawWorkspace::WorkspaceState& workspace) {
    std::vector<RawWorkspace::RawGallerySimilarityStack> results;
    std::unordered_set<std::string> assigned;
    for(const auto& project:workspace.sourceSetProjects) {
        if(!project.multiFrameProject)continue;
        RawWorkspace::RawGallerySimilarityStack stack;
        stack.resultProjectId=project.projectId;stack.resultProjectName=project.projectName;
        stack.resultProjectPath=project.absolutePath;stack.resultCoverPath=project.coverThumbnailCachePath;
        for(std::size_t i=0;i<workspace.sources.size();++i) {
            const auto& source=workspace.sources[i];
            if(std::none_of(source.sourceSetProjectMemberships.begin(),source.sourceSetProjectMemberships.end(),
                [&](const auto& m){return m.projectId==project.projectId;}))continue;
            if(stack.sourceKeys.empty()){stack.anchorCatalogIndex=i;stack.folderKey=source.parentFolderKey;}
            stack.sourceKeys.push_back(source.relativePathKey);assigned.insert(source.relativePathKey);
        }
        if(!stack.sourceKeys.empty())results.push_back(std::move(stack));
    }
    for(auto& stack:stacks)stack.sourceKeys.erase(std::remove_if(stack.sourceKeys.begin(),stack.sourceKeys.end(),
        [&](const auto& key){return assigned.count(key)!=0;}),stack.sourceKeys.end());
    stacks.erase(std::remove_if(stacks.begin(),stacks.end(),[](const auto& s){return s.sourceKeys.empty();}),stacks.end());
    stacks.insert(stacks.end(),results.begin(),results.end());
}
}

void EditorModule::RefreshBracketingProjectCard() {
    if (!IsBracketingActive() || m_RawWorkspace.workspaceRoot.empty()) return;
    const auto root = Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot).projectsDirectory;
    const auto path = std::filesystem::path(GetCurrentProjectFileName()).lexically_normal();
    const auto relative = path.lexically_relative(root.lexically_normal());
    const auto browsed = m_RawWorkspace.workspaceRoot.lexically_normal();
    const bool directlyBrowsed = path == browsed || path.parent_path() == browsed;
    if (!directlyBrowsed && (relative.empty() || relative.is_absolute() || *relative.begin() == "..")) return;
    Stack::Editor::UpdateBracketingGalleryProject(m_RawWorkspace, *m_Project->snapshot, path, m_Project->store);
    InvalidateRawWorkspaceGalleryPresentation();
}
