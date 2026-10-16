#include "Editor/EditorModule.h"
#include "AutoBracketCoordinator.h"
#include "AutoBracketPresentation.h"
#include "Editor/Bracketing/BracketingGallery.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/RawRenderService.h"
#include "App/WorkspacePresentation.h"
#include <imgui_internal.h>
#include <algorithm>
#include <unordered_map>
#include <set>

namespace {
bool AutoBracketPathExists(const std::filesystem::path& path) {
    std::error_code error;return !path.empty()&&std::filesystem::exists(path,error)&&!error;
}
}

void EditorModule::TickAutoBracketing(bool externalBusy) {
    if((!m_DocumentPersistenceEnabled&&!m_IsAutoBracketWorkspace)||Stack::Workspace::IsPreview())return;
    const double now=ImGui::GetTime();
    if(!m_AutoBracket) {
        m_AutoBracket=std::make_unique<Stack::AutoBracket::AutoBracketCoordinator>();
        m_AutoBracketPresentation=std::make_unique<Stack::AutoBracket::Presentation>();
        m_AutoBracket->SetProtectedProjectIds(m_AutoBracketProtectedProjectIds,m_AutoBracketProtectedProjectPaths);
        m_AutoBracket->NoteActivity(now);
    }
    auto& coordinator=*m_AutoBracket;
    coordinator.SetNotificationScope(GetNotifier());
    const auto& input=ImGui::GetIO();
    const bool userActive=input.MouseDelta.x!=0||input.MouseDelta.y!=0||
        input.MouseWheel!=0||input.MouseWheelH!=0||!input.InputQueueCharacters.empty()||
        ImGui::IsAnyItemActive()||std::any_of(std::begin(input.KeysData),std::end(input.KeysData),
            [](const ImGuiKeyData& key){return key.Down&&key.DownDuration==0.f;});
    if(userActive)coordinator.NoteActivity(now);
    m_AutoBracketPresentation->Tick(now);

    const auto root=m_RawWorkspace.workspaceRoot.lexically_normal();
    const bool rootCurrent=!m_AutoBracketWorkspaceClosing&&coordinator.SetRoot(root,now);
    const auto scan=GetRawWorkspaceScanSnapshot();
    const bool ready=rootCurrent&&!root.empty()&&scan.completedSuccessfully&&!Async::IsBusy(scan.state)&&
        !IsRawWorkspaceThumbnailBusy()&&!m_RawWorkspaceThumbnailWorkerActive&&
        !m_RawWorkspaceSimilarityWorkerActive&&!m_RawWorkspaceSimilarityRebuildPending&&
        m_RawWorkspaceSimilarityPublishedGeneration==m_RawWorkspaceSimilarityGeneration.load()&&
        m_RawWorkspaceSimilarityWorkspaceKey==root.generic_string();
    // Catalog-content invalidation is shared with the navigator. Selection,
    // folder scope and thumbnail readiness do not change bracket candidates;
    // similarity and authored grouping changes have their own revisions.
    const auto catalogContentsRevision = m_RawWorkspaceNavigatorRevision;
    if(ready&&(m_AutoBracketRoot!=root.generic_string()||m_AutoBracketCatalogRevision!=catalogContentsRevision||
        m_AutoBracketGroupingGeneration!=m_RawWorkspaceSimilarityPublishedGeneration||
        m_AutoBracketOrganizationRevision!=m_RawWorkspaceFilmstripOrganizationRevision)) {
        const auto candidates=Stack::AutoBracket::CollectCandidates(
            m_RawWorkspace.sources,ResolveRawWorkspaceFilmstripStacks());
        std::unordered_map<std::string,const Stack::RawWorkspace::SourceRecord*> sourcesByKey;
        for(const auto& source:m_RawWorkspace.sources)sourcesByKey.emplace(source.relativePathKey,&source);
        std::unordered_map<std::string,std::filesystem::path> projectPaths;
        for(const auto& project:m_RawWorkspace.sourceSetProjects)projectPaths.emplace(project.projectId,project.absolutePath);
        std::map<std::string,std::pair<std::string,std::filesystem::path>> represented;
        // Badge eligibility and automatic skipping share the same exact,
        // complete owning source-set membership contract.
        for(const auto& set:Stack::RawWorkspace::BuildGalleryCompletedBurstSourceSets(m_RawWorkspace)) {
            const auto project=projectPaths.find(set.projectId);
            if(project==projectPaths.end()||!AutoBracketPathExists(project->second))continue;
            std::vector<Stack::AutoBracket::Source> sources;sources.reserve(set.sourceKeys.size());
            for(const auto& key:set.sourceKeys) {
                const auto found=sourcesByKey.find(key);
                if(found!=sourcesByKey.end())sources.push_back({found->second->absolutePath,
                    found->second->fingerprint,found->second->fileSizeBytes});
            }
            const auto identity=Stack::AutoBracket::SourceIdentity(sources);
            if(!identity.empty())represented.emplace(identity,std::make_pair(set.projectId,project->second));
        }
        coordinator.UpdateCandidates(candidates,represented);
        m_AutoBracketRoot=root.generic_string();m_AutoBracketCatalogRevision=catalogContentsRevision;
        m_AutoBracketGroupingGeneration=m_RawWorkspaceSimilarityPublishedGeneration;
        m_AutoBracketOrganizationRevision=m_RawWorkspaceFilmstripOrganizationRevision;
    }
    // Dedicated automatic and Gallery workspaces never consume document
    // editing activity here. CollectActivity also formats catalog-wide detail,
    // so do not scan every source for an unused result each rendered frame.
    bool editingBusy=false;
    if(!m_IsAutoBracketWorkspace&&!m_PermanentGalleryWorkspace) {
        Stack::UiActivity::Snapshot foreground;CollectActivity(foreground);
        editingBusy=foreground.Busy()||
            ImGui::IsAnyItemActive()||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)||
            (m_Bracketing&&(m_Bracketing->metadataJob||m_Bracketing->preparationJob||m_Bracketing->preparationRequested||
                m_Bracketing->detailJob||m_Bracketing->inspectionJob||!m_Bracketing->awaitingProject.empty()||
                (m_Bracketing->pending&&m_Bracketing->publishRequested)));
    }
    const bool busy=externalBusy||m_AutoBracketWorkspaceClosing||m_AutoBracketPresentation->animation.active||
        (!m_IsAutoBracketWorkspace&&!m_PermanentGalleryWorkspace&&editingBusy);
    const auto foregroundProjectId=m_IsAutoBracketWorkspace?std::string():
        (m_Project->snapshot?m_Project->snapshot->projectId:std::string());
    // The document editor only schedules. The shell transfers its ready queue into a
    // separate workspace before any worker can change a project on disk.
    m_AutoBracketWorkspaceRequested=!m_IsAutoBracketWorkspace&&coordinator.CanStartWork(now,ready,busy,foregroundProjectId);
    coordinator.Tick(now,ready,busy||!m_IsAutoBracketWorkspace,[](Raw::OpenGlTask task,std::string& error) {
        return Stack::EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking(
            std::move(task),error,Raw::OpenGlTaskPriority::Background);
    },foregroundProjectId);
    if(auto work=coordinator.Current())if(work->generation!=m_AutoBracketPresentation->generation)
        m_AutoBracketPresentation->Begin(work,now);
    if(auto completed=coordinator.ConsumeCompleted()) {
        // Only catalog presentation is updated. The editor's document and result are untouched.
        if(completed->project && root==completed->root) {
            Stack::Editor::UpdateBracketingGalleryProject(m_RawWorkspace,completed->project.snapshot,
                completed->item.projectPath,completed->project.store);
            InvalidateRawWorkspaceGalleryPresentation();
            m_RawWorkspaceCatalogChanged=true;
        }
        m_AutoBracketPresentation->Complete(completed,now);
    }
}

bool EditorModule::ConsumeAutoBracketWorkspaceRequest() {
    const bool requested=m_AutoBracketWorkspaceRequested;
    m_AutoBracketWorkspaceRequested=false;
    return requested;
}
bool EditorModule::TransferAutoBracketingTo(EditorModule& target) {
    if(&target==this||m_IsAutoBracketWorkspace||!m_AutoBracket||target.m_AutoBracket||
        target.GetProjectSessionKind()!=ProjectSessionKind::Empty||target.IsDirty())return false;
    target.m_AutoBracket=std::move(m_AutoBracket);
    target.m_AutoBracketPresentation=std::move(m_AutoBracketPresentation);
    target.m_IsAutoBracketWorkspace=true;
    target.m_AutoBracketWorkspaceClosing=false;
    target.m_AutoBracketWorkspaceRequested=false;
    target.m_AutoBracketProtectedProjectIds=m_AutoBracketProtectedProjectIds;
    target.m_AutoBracketProtectedProjectPaths=m_AutoBracketProtectedProjectPaths;
    if(m_Project->snapshot)target.m_AutoBracketProtectedProjectIds.push_back(m_Project->snapshot->projectId);
    target.m_AutoBracket->SetProtectedProjectIds(target.m_AutoBracketProtectedProjectIds,target.m_AutoBracketProtectedProjectPaths);
    // The target scans the shared folder independently. Reconcile its own catalog
    // once ready, keeping the transferred queue's durable identities and progress.
    target.m_AutoBracketRoot.clear();
    target.m_AutoBracketCatalogRevision=target.m_AutoBracketGroupingGeneration=target.m_AutoBracketOrganizationRevision=0;
    m_AutoBracketWorkspaceRequested=false;
    m_AutoBracketRoot.clear();
    m_AutoBracketCatalogRevision=m_AutoBracketGroupingGeneration=m_AutoBracketOrganizationRevision=0;
    return true;
}
void EditorModule::SetAutoBracketProtectedProjectIds(const std::vector<std::string>& projectIds,
    const std::vector<std::filesystem::path>& paths) {
    if(m_AutoBracketProtectedProjectIds==projectIds&&m_AutoBracketProtectedProjectPaths==paths)return;
    m_AutoBracketProtectedProjectIds=projectIds;
    m_AutoBracketProtectedProjectPaths=paths;
    if(m_AutoBracket)m_AutoBracket->SetProtectedProjectIds(projectIds,paths);
}
bool EditorModule::IsAutoBracketProjectBusy(const std::filesystem::path& path) const {
    return m_AutoBracket&&m_AutoBracket->IsWritingProject(path);
}
void EditorModule::CancelAutoBracketingForWorkspaceClose(bool pauseQueue) {
    if(!m_IsAutoBracketWorkspace)return;
    m_AutoBracketWorkspaceClosing=true;
    m_AutoBracketWorkspaceRequested=false;
    if(m_AutoBracket) {
        m_AutoBracket->DismissForeground();
        if(pauseQueue)m_AutoBracket->PauseUntilIdle(ImGui::GetTime());
        m_AutoBracket->CancelCurrent();
        m_AutoBracket->NoteActivity(ImGui::GetTime());
    }
    if(m_AutoBracketPresentation)m_AutoBracketPresentation->visible=false;
}
bool EditorModule::AutoBracketWorkActive() const {
    return m_AutoBracket&&(m_AutoBracket->HasWork()||m_AutoBracket->Yielding());
}
bool EditorModule::RequestAutoBracketForeground(const std::string& label,std::function<void()> action) {
    if(!m_AutoBracket||m_IsAutoBracketWorkspace)return false;
    m_AutoBracket->NoteActivity(ImGui::GetTime());
    return m_AutoBracket->RequestForeground(label,[this,action=std::move(action)] {
        if(m_AutoBracketPresentation){m_AutoBracketPresentation->visible=false;m_AutoBracketPresentation->animation.active=false;}
        action();
    });
}
void EditorModule::ShutdownAutoBracketing() {
    if(m_AutoBracket)m_AutoBracket->Shutdown();
    m_AutoBracketPresentation.reset();m_AutoBracket.reset();
    m_IsAutoBracketWorkspace=false;m_AutoBracketWorkspaceClosing=false;m_AutoBracketWorkspaceRequested=false;
    m_RawWorkspaceCatalogChanged=false;
}
void EditorModule::RenderAutoBracketOverlay() {
    if(!m_IsAutoBracketWorkspace&&m_AutoBracket&&m_AutoBracketPresentation)
        Stack::AutoBracket::DrawPresentation(*m_AutoBracketPresentation,*m_AutoBracket);
}
void EditorModule::RenderAutoBracketWorkspace() {
    if(!m_IsAutoBracketWorkspace||!m_AutoBracket||!m_AutoBracketPresentation)return;
    if(!m_AutoBracketPresentation->animation.active)RenderAutoBracketQueueItems();
    Stack::AutoBracket::DrawPresentation(*m_AutoBracketPresentation,*m_AutoBracket,true);
}

bool EditorModule::IsUserStartedProcessingBusy() const {
    return Async::IsBusy(m_MultiFrameGraphProcessingTaskState) ||
        Async::IsBusy(m_MfdExperimentalProcessingTaskState) ||
        Async::IsBusy(m_HdrProcessingTaskState);
}
void EditorModule::RenderAutoBracketControls(bool submenu) {
    if (submenu) {
        if (!ImGui::BeginMenu("Auto bracket", m_AutoBracket != nullptr)) return;
    } else {
        if (!m_AutoBracket) return;
        if (ImGui::SmallButton("Auto bracket")) ImGui::OpenPopup("Auto bracket controls");
        if (!ImGui::BeginPopup("Auto bracket controls")) return;
    }
    {
        if(ImGui::Button("Run now")){m_AutoBracket->RunNow();ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button(m_AutoBracket->GetQueue().paused?"Resume":"Pause until idle")) {
            if(m_AutoBracket->GetQueue().paused)m_AutoBracket->SetPaused(false);
            else m_AutoBracket->PauseUntilIdle(ImGui::GetTime());
        }
        if(m_AutoBracket->HasWork()) {
            if(ImGui::Button("Show processing"))m_AutoBracketPresentation->visible=true;
            ImGui::SameLine();if(ImGui::Button("Cancel current")){m_AutoBracket->PauseUntilIdle(ImGui::GetTime());m_AutoBracket->CancelCurrent();}
        }
        unsigned completed=0,remaining=0;
        for(const auto& item:m_AutoBracket->GetQueue().items) {
            completed+=item.state==Stack::AutoBracket::State::Completed;
            remaining+=item.available&&(item.state==Stack::AutoBracket::State::Pending||item.state==Stack::AutoBracket::State::Running);
        }
        ImGui::Text("%u completed, %u remaining",completed,remaining);
        ImGui::TextWrapped("Processes existing stacks throughout this folder. Each bracket is saved separately.");
        ImGui::TextWrapped("Starts automatically when the folder is ready. After a pause or stop, resumes after one minute without input.");
        if(!m_AutoBracket->Error().empty())ImGui::TextWrapped("%s",m_AutoBracket->Error().c_str());
        RenderAutoBracketQueueItems();
        if (submenu) ImGui::EndMenu();
        else ImGui::EndPopup();
    }
}
void EditorModule::RenderAutoBracketQueueItems() {
    if(!m_AutoBracket)return;
    const auto& queue=m_AutoBracket->GetQueue();
    std::size_t pending=0,completed=0,attention=0;
    for(const auto& item:queue.items) {
        completed+=item.state==Stack::AutoBracket::State::Completed;
        pending+=item.available&&(item.state==Stack::AutoBracket::State::Pending||item.state==Stack::AutoBracket::State::Running);
        attention+=item.state==Stack::AutoBracket::State::Attention||item.state==Stack::AutoBracket::State::Failed;
    }
    ImGui::TextDisabled("%zu saved, %zu remaining, %zu need attention%s",completed,pending,attention,queue.paused?" / Paused":"");
    if(ImGui::CollapsingHeader("Bracket processing details")) {
        for(const auto& item:queue.items) {
            if(!item.available&&item.state!=Stack::AutoBracket::State::Completed)continue;
            ImGui::PushID(item.candidate.identity.c_str());
            ImGui::Text("%s / %s",item.candidate.name.c_str(),Stack::AutoBracket::StateName(item.state));
            if(item.state==Stack::AutoBracket::State::Running) {
                if(auto work=m_AutoBracket->Current())ImGui::ProgressBar(static_cast<float>(work->progress.load()),{180,0});
            }
            if(!item.error.empty())ImGui::TextWrapped("%s",item.error.c_str());
            if(item.state==Stack::AutoBracket::State::Attention||item.state==Stack::AutoBracket::State::Failed||item.state==Stack::AutoBracket::State::Excluded) {
                if(ImGui::SmallButton("Retry"))m_AutoBracket->Retry(item.candidate.identity);
                ImGui::SameLine();
                if(!m_IsAutoBracketWorkspace&&!item.projectPath.empty()&&AutoBracketPathExists(item.projectPath/"project.stack")) {
                    if(ImGui::SmallButton("Open"))RequestOpenRawWorkspaceProjectFromGallery(item.projectPath);
                }else if(!m_IsAutoBracketWorkspace&&!item.candidate.sources.empty()&&ImGui::SmallButton("Review captures")) {
                    std::vector<std::filesystem::path> paths;
                    for(const auto& source:item.candidate.sources)paths.push_back(source.path);
                    const auto review=[this,paths] {
                        OpenBracketingTool();BeginBracketingDraft(true);AddBracketingDraftFiles(paths);
                    };
                    if(!RequestAutoBracketForeground("review these captures",review))review();
                }
            }
            if(item.state==Stack::AutoBracket::State::Pending) {
                ImGui::SameLine();if(ImGui::SmallButton("Skip"))m_AutoBracket->Exclude(item.candidate.identity);
            }
            ImGui::PopID();
        }
    }
}
const Stack::RawWorkspace::GalleryPresentation& EditorModule::GetRawWorkspaceCategoryPresentation() {
    const auto& all=GetRawWorkspaceGalleryPresentation();
    if(m_RawWorkspaceGalleryContentMode==Stack::RawWorkspace::GalleryContentMode::Gallery)return all;
    const auto queueRevision=m_AutoBracket?m_AutoBracket->Revision():0;
    if(m_RawWorkspaceCategoryRevision!=m_RawWorkspaceGalleryRevision||m_RawWorkspaceCategoryMode!=m_RawWorkspaceGalleryContentMode||
        m_RawWorkspaceCategoryQueueRevision!=queueRevision) {
        m_RawWorkspaceLabFilmstripProjectTimelineActive=false;
        m_RawWorkspaceCategoryPresentation=all;
        auto& projects=m_RawWorkspaceCategoryPresentation.projects;
        const bool bracket=m_RawWorkspaceGalleryContentMode==Stack::RawWorkspace::GalleryContentMode::Bracket;
        Stack::RawWorkspace::FilterGalleryProjectCards(m_RawWorkspaceCategoryPresentation,
            m_RawWorkspaceGalleryContentMode);
        if(bracket) AppendRawWorkspaceQueuedBrackets(projects);
        m_RawWorkspaceCategoryRevision=m_RawWorkspaceGalleryRevision;m_RawWorkspaceCategoryMode=m_RawWorkspaceGalleryContentMode;
        m_RawWorkspaceCategoryQueueRevision=queueRevision;
    }
    return m_RawWorkspaceCategoryPresentation;
}

void EditorModule::AppendRawWorkspaceQueuedBrackets(
    std::vector<Stack::RawWorkspace::GalleryProjectView>& projects, bool resolveProjectPaths) {
    if(m_AutoBracket)for(const auto& item:m_AutoBracket->GetQueue().items) {
            if(item.state==Stack::AutoBracket::State::Excluded||(!item.available&&item.state!=Stack::AutoBracket::State::Completed))continue;
            auto found=std::find_if(projects.begin(),projects.end(),[&](const auto& project){
                return !item.projectId.empty()&&project.projectId==item.projectId;
            });
            if(found==projects.end()) {
                if(item.state==Stack::AutoBracket::State::Completed)continue;
                projects.emplace_back();found=std::prev(projects.end());
                found->projectId=item.projectId.empty()?"queued:"+item.candidate.identity:item.projectId;
                found->projectName=item.candidate.name;
                found->bracketingProject=true;found->multiFrameProject=true;
                found->frameCount=item.candidate.sources.size();
                if(resolveProjectPaths&&!item.projectPath.empty()&&AutoBracketPathExists(item.projectPath/"project.stack"))found->projectPath=item.projectPath;
                if(!item.candidate.sources.empty()) {
                    const auto source=std::find_if(m_RawWorkspace.sources.begin(),m_RawWorkspace.sources.end(),[&](const auto& source){
                        return source.fingerprint==item.candidate.sources.front().fingerprint;
                    });
                    if(source!=m_RawWorkspace.sources.end()) {
                        found->referenceSourceKey=source->relativePathKey;
                        found->referenceSourceIndex=static_cast<std::size_t>(std::distance(m_RawWorkspace.sources.begin(),source));
                    }
                }
            }
            found->projectName+=" / "+std::string(Stack::AutoBracket::StateName(item.state));
        }
}
