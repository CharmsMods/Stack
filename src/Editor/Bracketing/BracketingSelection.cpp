#include "Raw/Bracketing/Panorama/Compatibility.h"
#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "Persistence/BracketingProject.h"
#include "App/WorkspacePresentation.h"
#include "App/AppPaths.h"
#include "Raw/RawLoader.h"
#include "Async/TaskSystem.h"
#include <algorithm>
#include <unordered_set>

bool EditorModule::IsBracketingToolActive() const {
    return m_RawWorkspaceLabUi.activeTool==RawLabTool::MultiFrame &&
        (!IsMultiFrameRawProjectActive()||IsBracketingActive()||(m_Bracketing&&m_Bracketing->newProject));
}
void EditorModule::OpenBracketingTool() {
    if (m_PermanentGalleryWorkspace) {
        ExecuteRawGalleryAction(Stack::RawGalleryActions::Action::CreateCaptureSet,
            CaptureRawGalleryActionContext());
        return;
    }
    if(RequestAutoBracketForeground("open Bracketing",[this]{OpenBracketingTool();}))return;
    if(Stack::Workspace::IsPreview())return;
    m_RawWorkspaceLabUi.galleryWorkspaceOpen = false;
    m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
    m_RawWorkspaceLabUi.lastGalleryHost = RawGalleryHost::Filmstrip;
    if (m_RawWorkspaceLabUi.activeTool != RawLabTool::MultiFrame)
        m_RawLabLastEditTool = m_RawWorkspaceLabUi.activeTool;
    m_RawWorkspaceLabUi.activeTool=RawLabTool::MultiFrame;
    m_RawWorkspaceLabUi.settingsTabActive=false;
    m_RawWorkspaceLabUi.galleryNavigationMode=RawGalleryNavigationMode::ProjectRoot;
    m_RawWorkspaceGalleryContentMode=Stack::RawWorkspace::GalleryContentMode::Gallery;
    BeginBracketingDraft();
}
void EditorModule::BeginBracketingDraft(bool fresh) {
    if(Stack::Workspace::IsPreview())return;
    if(!fresh&&IsBracketingActive()) {
        TickBracketing(); RestoreBracketingSelection();
        if(m_Bracketing&&!m_Bracketing->newProject&&!m_Bracketing->processRequired&&!m_Bracketing->failed&&
            !m_Bracketing->result&&!m_Bracketing->job)m_Bracketing->publishRequested=true;
        return;
    }
    if(!fresh&&m_Bracketing&&m_Bracketing->newProject)return;
    if(!fresh&&IsMultiFrameRawProjectActive())return;
    m_Bracketing=std::make_shared<Stack::Editor::BracketingSession>();
    m_Bracketing->newProject=true;
    m_Bracketing->boundDocument=GetCurrentProjectFileName();
    m_Bracketing->processRequired=true;
    m_Bracketing->status="Select captures in the filmstrip or Gallery, then press Process.";
    SyncBracketingSelection();
}
void EditorModule::RestoreBracketingSelection() {
    if(!m_Bracketing||m_Bracketing->selectionRestored||m_Bracketing->newProject||!IsBracketingActive())return;
    auto& ui=*m_Bracketing;
    const auto* set=Stack::Project::FindSourceSet(*m_Project->snapshot,ui.setId);
    if(!set)return;
    ui.sources.clear();m_RawWorkspace.selectedSourceKeys.clear();
    std::vector<std::pair<std::string,Stack::Editor::BracketingDraftSource>> materialize;
    const auto selected=set->settings.find(ui.processRequired?"bracketingDraftSelection":"bracketingSelection");
    for(const auto& group:ui.recipe.groups)for(const auto& frame:group.frames) {
        if(selected!=set->settings.end()&&selected->is_array()&&std::find(selected->begin(),selected->end(),frame.id)==selected->end())continue;
        const auto f=std::find_if(set->frames.begin(),set->frames.end(),[&](const auto& f){return f.frameId==frame.id;});
        if(f==set->frames.end())continue;
        const auto* asset=Stack::Project::FindEmbeddedAsset(*m_Project->snapshot,f->assetId);
        if(!asset)continue;
        Stack::Editor::BracketingDraftSource source;
        source.frameId=frame.id;source.path=asset->originalSourcePath;source.inspected=true;
        Stack::Project::DeserializeRawCaptureCompatibilitySummary(asset->captureMetadataSummary,source.metadata,nullptr);
        for(const auto& candidate:m_RawWorkspace.sources) {
            if((!asset->sha256.empty()&&candidate.fingerprint==asset->sha256)||candidate.absolutePath==source.path||
                (!asset->workspaceRelativeSourcePath.empty()&&candidate.relativePathKey==asset->workspaceRelativeSourcePath)) {
                source.sourceKey=candidate.relativePathKey;source.path=candidate.absolutePath;
                m_RawWorkspace.selectedSourceKeys.push_back(source.sourceKey);break;
            }
        }
        if(source.sourceKey.empty()) {
            std::error_code ec;
            if(!std::filesystem::is_regular_file(source.path,ec)) {
                const auto managed=m_Project->store->StoragePath()/std::filesystem::u8path(asset->projectAssetPath);
                if(std::filesystem::is_regular_file(managed,ec))source.path=managed;
                else {
                    source.path=AppPaths::GetCacheDirectory()/"BracketingOriginals"/
                        (asset->sha256+std::filesystem::path(asset->originalFilename).extension().string());
                    if(!std::filesystem::is_regular_file(source.path,ec))materialize.emplace_back(asset->assetId,source);
                }
            }
            Stack::RawWorkspace::SourceRecord record;
            source.sourceKey="bracket-original:"+ui.projectId+"/"+frame.id;
            record.relativePathKey=source.sourceKey;record.absolutePath=source.path;
            record.fileName=asset->originalFilename;record.fingerprint=asset->sha256;record.fileSizeBytes=asset->byteLength;
            record.parentFolderKey="Bracket originals";
            record.thumbnail=Stack::RawWorkspace::BuildThumbnailInfo(Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot),record);
            m_RawWorkspace.sources.push_back(std::move(record));
            m_RawWorkspace.selectedSourceKeys.push_back(source.sourceKey);
            if(!materialize.empty()&&materialize.back().second.frameId==frame.id)materialize.back().second=source;
        }
        ui.frameLabels[frame.id]=asset->originalFilename;
        ui.sourceHistory[source.frameId]=source;
        ui.sources.push_back(std::move(source));
    }
    ui.projectName=m_Project->snapshot->projectName;
    ui.selectionRestored=true;
    if(!materialize.empty()) {
        auto job=std::make_shared<Stack::Editor::BracketingMetadataJob>();ui.metadataJob=job;
        auto store=m_Project->store;
        if(!ProjectTasks().Submit([job,store,materialize=std::move(materialize)]()mutable {
            for(auto& item:materialize) {
                std::error_code ec;std::filesystem::create_directories(item.second.path.parent_path(),ec);
                store->CopyAssetToFile(item.first,item.second.path,&item.second.error);
                job->sources.push_back(std::move(item.second));
            }
            job->done=true;
        })) {ui.metadataJob.reset();ui.status="Could not load stored originals. Reopen the bracket to retry.";}
    }
    InvalidateRawWorkspaceGalleryPresentation();
}
void EditorModule::SyncBracketingSelection() {
    if(!m_Bracketing||m_Bracketing->synchronizing||Stack::Workspace::IsPreview())return;
    auto& ui=*m_Bracketing;
    if(!ui.awaitingProject.empty())return;
    std::vector<Stack::Editor::BracketingDraftSource> sources;
    // External/embedded sources have no catalog selection to mirror.
    for(const auto& source:ui.sources)if(source.sourceKey.empty())sources.push_back(source);
    for(const auto& key:m_RawWorkspace.selectedSourceKeys) {
        const auto* record=FindRawWorkspaceSourceByKey(key);if(!record)continue;
        const auto old=std::find_if(ui.sources.begin(),ui.sources.end(),[&](const auto& s){return s.sourceKey==key;});
        if(old!=ui.sources.end())sources.push_back(*old);
        else {
            const auto previous=std::find_if(ui.sourceHistory.begin(),ui.sourceHistory.end(),[&](const auto& item) {
                return item.second.sourceKey==key&&item.second.path==record->absolutePath;
            });
            if(previous!=ui.sourceHistory.end()){sources.push_back(previous->second);continue;}
            Stack::Editor::BracketingDraftSource source;
            source.frameId=Stack::Project::GenerateStableUuid();source.sourceKey=key;source.path=record->absolutePath;
            sources.push_back(std::move(source));
        }
    }
    bool same=sources.size()==ui.sources.size();
    if(same)for(const auto& source:sources)if(std::none_of(ui.sources.begin(),ui.sources.end(),
        [&](const auto& prior){return source.frameId==prior.frameId;})){same=false;break;}
    if(same)return;
    for(const auto& source:ui.sources)ui.sourceHistory[source.frameId]=source;
    ui.sources=std::move(sources);
    Stack::Editor::RegroupBracketingDraft(ui);
    ui.processRequired=true;
    CommitBracketingEdit();
    ui.processRequired=true;ui.pending=false;ui.publishRequested=false;
    ui.status=ui.newProject?"Draft ready. Press Process to merge the selected captures.":
        "Selection changed. Press Process to update the result.";
    for(const auto& source:ui.sources)ui.frameLabels[source.frameId]=source.path.filename().string();
}
void EditorModule::SelectBracketingSources(const std::vector<std::string>& keys,bool toggle,bool range) {
    if(RequestAutoBracketForeground("change bracket inputs",[this,keys,toggle,range]{SelectBracketingSources(keys,toggle,range);}))return;
    if(IsBracketingPresentationActive()||keys.empty()||Stack::Workspace::IsPreview())return;
    BeginBracketingDraft();
    std::vector<std::string> ordered;
    for(const auto& stack:ResolveRawWorkspaceFilmstripStacks())
        for(const auto& key:stack.sourceKeys)if(std::find(ordered.begin(),ordered.end(),key)==ordered.end())ordered.push_back(key);
    auto& selection=m_RawWorkspace.selectedSourceKeys;
    if(range) {
        const auto a=std::find(ordered.begin(),ordered.end(),m_RawWorkspace.selectedSourceKey);
        const auto b=std::find(ordered.begin(),ordered.end(),keys.back());
        if(a!=ordered.end()&&b!=ordered.end())selection.assign(std::min(a,b),std::max(a,b)+1);
        else selection=keys;
    } else if(toggle) {
        const bool all=std::all_of(keys.begin(),keys.end(),[&](const auto& key){return std::find(selection.begin(),selection.end(),key)!=selection.end();});
        for(const auto& key:keys) {
            const auto found=std::find(selection.begin(),selection.end(),key);
            if(all&&found!=selection.end())selection.erase(found);
            else if(!all&&found==selection.end())selection.push_back(key);
        }
        m_RawWorkspace.selectedSourceKey=keys.front();
    } else {selection=keys;m_RawWorkspace.selectedSourceKey=keys.front();}
    SyncBracketingSelection();InvalidateRawWorkspaceGalleryPresentation();
}
void EditorModule::AddBracketingDraftFiles(const std::vector<std::filesystem::path>& paths) {
    BeginBracketingDraft();if(!m_Bracketing)return;
    const auto before=m_Bracketing->sources.size();
    for(const auto& path:paths) {
        if(!Raw::RawLoader::IsRawPath(path.string()))continue;
        if(std::any_of(m_Bracketing->sources.begin(),m_Bracketing->sources.end(),[&](const auto& s){return s.path==path;}))continue;
        Stack::Editor::BracketingDraftSource source;
        source.frameId=Stack::Project::GenerateStableUuid();source.path=path;
        m_Bracketing->frameLabels[source.frameId]=path.filename().string();
        m_Bracketing->sources.push_back(std::move(source));
    }
    if(before==m_Bracketing->sources.size())return;
    m_Bracketing->processRequired=true;
    Stack::Editor::RegroupBracketingDraft(*m_Bracketing);CommitBracketingEdit();
}
void EditorModule::RemoveBracketingDraftSource(const std::string& id) {
    if(!m_Bracketing)return;
    auto& ui=*m_Bracketing;
    for(const auto& s:ui.sources)if(s.frameId==id) {
        auto& keys=m_RawWorkspace.selectedSourceKeys;
        keys.erase(std::remove(keys.begin(),keys.end(),s.sourceKey),keys.end());
    }
    ui.sources.erase(std::remove_if(ui.sources.begin(),ui.sources.end(),[&](const auto& s){return s.frameId==id;}),ui.sources.end());
    ui.processRequired=true;
    Stack::Editor::RegroupBracketingDraft(ui);CommitBracketingEdit();
    InvalidateRawWorkspaceGalleryPresentation();
}
void EditorModule::TickBracketingDraft() {
    if(!m_Bracketing||Stack::Workspace::IsPreview())return;
    auto& ui=*m_Bracketing;
    if(ui.metadataJob&&ui.metadataJob->done) {
        for(const auto& inspected:ui.metadataJob->sources)for(auto& source:ui.sources)
            if(source.frameId==inspected.frameId){source=inspected;ui.sourceHistory[source.frameId]=source;}
        ui.metadataJob.reset();
        const auto origin=std::find_if(ui.sources.begin(),ui.sources.end(),[](const auto& s){return s.inspected&&s.error.empty();});
        if(origin!=ui.sources.end())for(auto& source:ui.sources)if(source.inspected&&source.error.empty()) {
            auto candidate=source.metadata;
            candidate.orientation=origin->metadata.orientation;
            if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama)
                Raw::Bracketing::Panorama::Compatible(origin->metadata,candidate,&source.error);
            else Stack::Project::AreHdrCapturesStructurallyCompatible(origin->metadata,candidate,&source.error);
        }
        // Only unprocessed drafts are regrouped by asynchronously obtained metadata.
        // Existing arrangements remain intact, including user-created groups.
        Stack::Editor::RegroupBracketingDraft(ui);
        ui.editedRecipe=Raw::Bracketing::Serialize(ui.recipe).dump();
    }
    if(!ui.metadataJob) {
        auto job=std::make_shared<Stack::Editor::BracketingMetadataJob>();
        for(const auto& s:ui.sources)if(!s.inspected)job->sources.push_back(s);
        if(!job->sources.empty()) {
            ui.metadataJob=job;
            if(!ProjectTasks().Submit("Reading capture details",[job] {
                for(auto& source:job->sources) {
                    try {
                        Raw::RawMetadata metadata;
                        if(Raw::RawLoader::LoadMetadata(source.path.string(),metadata)) {
                            source.metadata=Stack::Project::BuildRawCaptureCompatibilitySummary(metadata);
                            source.error=source.metadata.rejectionReason;
                        } else source.error="Could not read capture metadata.";
                    } catch(const std::exception& e){source.error=e.what();}
                    source.inspected=true;
                }
                job->done=true;
            })) {ui.metadataJob.reset();ui.status="Could not inspect captures. Retry selection.";}
        }
    }
}
