#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "Persistence/BracketingProject.h"
#include "Persistence/ProjectIndex.h"
#include "App/WorkspacePresentation.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

bool EditorModule::HasPendingBracketingDraft() const {
    if(m_Bracketing&&m_Bracketing->processRequired)return true;
    if(!IsBracketingActive())return false;
    const auto* set=Stack::Project::FindSourceSet(*m_Project->snapshot,m_Project->snapshot->activeSourceSetId);
    return set&&set->settings.contains("bracketingDraft");
}
bool EditorModule::HasUnsavedBracketingDraft() const {
    if (!m_Bracketing) return false;
    // A saved input draft may still need Process. That is independent of
    // whether its document has changes to save before closing.
    if (m_Bracketing->newProject)
        return !m_Bracketing->sources.empty() || !m_Bracketing->recipe.groups.empty();
    return IsBracketingActive() && IsDirty();
}

bool EditorModule::NeedsWorkspaceSaveBeforeTransition() const {
    if (IsDirty() || HasUnsavedBracketingDraft()) return true;
    const auto kind = GetProjectSessionKind();
    return kind != ProjectSessionKind::Empty && kind != ProjectSessionKind::RawPreview &&
        HasProjectContent() && GetCurrentProjectFileName().empty();
}

bool EditorModule::RequestSaveWorkspaceBeforeClose(std::function<void(bool)> onComplete) {
    if (!m_DocumentPersistenceEnabled || !FinishWorkspaceInteraction()) {
        if (onComplete) onComplete(false);
        return false;
    }
    // Close and switch actions obtain foreground ownership before saving.
    // Never leave their completion waiting on a dismissible
    // second takeover prompt.
    if (AutoBracketWorkActive()) {
        PostNotification(UiNotificationSeverity::Warning,
            "Finish automatic bracketing before saving this workspace.", "bracketing-close-save-busy");
        if (onComplete) onComplete(false);
        return false;
    }
    if (!NeedsWorkspaceSaveBeforeTransition() && !IsProjectFileSaveBusy() &&
        !IsRawWorkspaceProjectSaveBusy()) {
        if (onComplete) onComplete(true);
        return true;
    }
    const std::string name = GetCurrentProjectName().empty()
        ? "Untitled Project" : GetCurrentProjectName();
    if (!m_Bracketing || !m_Bracketing->newProject || !HasUnsavedBracketingDraft())
        return RequestSaveCurrentProject(name, std::move(onComplete));
    // A new bracket can be prepared while an edited RAW project is open.
    // Save that document before creating the bracket's own project.
    if (IsDirty() || IsProjectFileSaveBusy() || IsRawWorkspaceProjectSaveBusy()) {
        return RequestSaveCurrentProject(name, [this, onComplete](bool saved) {
            if (saved) RequestSaveWorkspaceBeforeClose(onComplete);
            else if (onComplete) onComplete(false);
        });
    }
    auto state = m_Bracketing;
    if (state->saveBeforeCloseCompletion || !state->awaitingProject.empty()) {
        if (onComplete) onComplete(false);
        return false;
    }
    if (Stack::Editor::NeedsBracketingOrientationReview(*state)) {
        PostNotification(UiNotificationSeverity::Warning,
            "Review the capture orientations in Bracket before saving. This workspace will stay open.",
            "bracketing-close-orientation-review");
        if (onComplete) onComplete(false);
        return false;
    }
    state->saveBeforeCloseCompletion = std::move(onComplete);
    std::string error;
    const bool accepted = CommitBracketingDraft(false, &error);
    if (!accepted || state->awaitingProject.empty()) {
        auto completion = std::exchange(state->saveBeforeCloseCompletion, {});
        if (!accepted && !error.empty())
            PostNotification(UiNotificationSeverity::Error, error, "bracketing-close-save");
        if (completion) completion(accepted);
    }
    return accepted;
}

void EditorModule::DiscardBracketingDraft() {
    if(!m_Bracketing)return;
    if(m_Bracketing->newProject){BeginBracketingDraft(true);return;}
    auto* set=Stack::Project::FindSourceSet(*m_Project->snapshot,m_Bracketing->setId);
    if(!set)return;
    auto& ui=*m_Bracketing;
    if(Raw::Bracketing::Deserialize(set->settings["bracketing"],ui.recipe,ui.status)) {
        set->settings.erase("bracketingDraft");ui.processRequired=false;ui.pending=false;
        set->settings.erase("bracketingDraftSelection");
        ui.editedRecipe=Raw::Bracketing::Serialize(ui.recipe).dump();
        ui.selectionRestored=false;RestoreBracketingSelection();
        ui.status="Pending input changes discarded.";MarkDirty();
    }
}
bool EditorModule::CommitBracketingDraft(bool process,std::string* error) {
    if(RequestAutoBracketForeground("process this bracket",[this,process]{CommitBracketingDraft(process,nullptr);}))return true;
    if(Stack::Workspace::IsPreview()||!m_Bracketing)return false;
    auto state=m_Bracketing;auto& ui=*state;
    auto fail=[&](const std::string& message){ui.status=message;if(error)*error=message;return false;};
    if(IsDeferredLoadedProjectApplyActive()||!ui.awaitingProject.empty())return fail("Opening the bracket project...");
    if(ui.sources.empty()&&(process||ui.newProject))return fail("Select at least one RAW capture.");
    if(ui.metadataJob||std::any_of(ui.sources.begin(),ui.sources.end(),[](const auto& s){return !s.inspected;}))
        return fail("Wait for capture metadata inspection to finish.");
    if((process || ui.newProject) && Stack::Editor::NeedsBracketingOrientationReview(ui)) {
        ui.orientationChoices=ui.recipe.orientationOverrides;
        ui.orientationReviewRequested=true;
        ui.processAfterOrientationReview=process;
        return fail("Review the capture orientations before processing.");
    }
    if(!Raw::Bracketing::Validate(ui.recipe,ui.status,!process))return fail(ui.status);
    if(process && m_RawWorkspaceRootTabActive) {
        CloseRawWorkspaceLabNativeGallery();
        m_RawWorkspaceLabUi.galleryHost=RawGalleryHost::Closed;
        m_RawWorkspaceLabFilmstripDrawerState={};
        SaveRawWorkspaceAppState();
    }
    if(ui.newProject) {
        const auto root=m_RawWorkspace.workspaceRoot.empty()?ui.sources.front().path.parent_path():m_RawWorkspace.workspaceRoot;
        auto directory=Stack::RawWorkspace::BuildManagedLayout(root).projectsDirectory;
        std::string name=ui.projectName.empty()?"Bracket":std::filesystem::path(ui.projectName).filename().string();
        // Concurrent imports need independent paths before either writes its bundle.
        const auto path=Stack::Project::ProjectIndex::BuildUniqueProjectPath(
            directory,name,Stack::Project::GenerateStableUuid());
        std::vector<std::filesystem::path> paths;for(const auto& source:ui.sources)paths.push_back(source.path);
        // Use the normal project-replacement guard; never silently discard RAW edits.
        if(IsDirty()&&!m_RawWorkspaceReplacementAuthorized) {
            QueueRawWorkspaceProjectReplacement("create bracket",name,[this,state,process](std::string* message) {
                m_Bracketing=state;return CommitBracketingDraft(process,message);
            });
            return false;
        }
        ui.awaitingProject=path;
        if(!CreateMultiFrameRawProject(path,Stack::Project::ProjectStorageKind::DirectoryBundle,
            name,"Captures",Stack::Project::MultiFrameOperationIntent::RawCaptureSet,paths,0,&ui.status,ui.recipe.orientationOverrides)) {
            ui.awaitingProject.clear();return fail(ui.status);
        }
        ui.awaitingProject=path;ui.processAfterImport=process;
        ui.status="Importing selected captures...";return true;
    }
    if(!IsBracketingActive())return fail("The bracket project is no longer active.");
    auto* set=Stack::Project::FindSourceSet(*m_Project->snapshot,ui.setId);
    const auto submitted=set->settings["bracketing"];
    const auto submittedRevision=m_Project->snapshot->hdrInputRevision;
    std::vector<std::filesystem::path> added;
    for(const auto& source:ui.sources)if(std::none_of(set->frames.begin(),set->frames.end(),[&](const auto& f){return f.frameId==source.frameId;})) {
        const bool imported=std::any_of(set->frames.begin(),set->frames.end(),[&](const auto& frame) {
            const auto* a=Stack::Project::FindEmbeddedAsset(*m_Project->snapshot,frame.assetId);
            return a&&std::filesystem::path(a->originalSourcePath)==source.path;
        });
        if(!imported)added.push_back(source.path);
    }
    if(!added.empty()&&!AddFramesToMultiFrameSourceSet(ui.setId,added,&ui.status,false))return fail(ui.status);
    set=Stack::Project::FindSourceSet(*m_Project->snapshot,ui.setId);
    set->settings["bracketing"]=submitted;
    m_Project->snapshot->hdrInputRevision=submittedRevision;
    std::unordered_map<std::string,std::string> remap;
    std::unordered_set<std::string> valid;
    for(const auto& frame:set->frames)valid.insert(frame.frameId);
    for(auto& source:ui.sources) {
        if(valid.count(source.frameId))continue;
        for(const auto& frame:set->frames) {
            const auto* asset=Stack::Project::FindEmbeddedAsset(*m_Project->snapshot,frame.assetId);
            if(asset&&std::filesystem::path(asset->originalSourcePath)==source.path) {
                remap[source.frameId]=frame.frameId;source.frameId=frame.frameId;break;
            }
        }
    }
    auto previous=ui.recipe.groups;
    std::unordered_set<std::string> used;
    for(auto& group:ui.recipe.groups) {
        for(auto& frame:group.frames)if(remap.count(frame.id))frame.id=remap[frame.id];
        group.frames.erase(std::remove_if(group.frames.begin(),group.frames.end(),[&](const auto& frame) {
            return !valid.count(frame.id)||!used.insert(frame.id).second;
        }),group.frames.end());
    }
    ui.recipe.groups.erase(std::remove_if(ui.recipe.groups.begin(),ui.recipe.groups.end(),[](const auto& g){return g.frames.empty();}),ui.recipe.groups.end());
    if(remap.count(ui.recipe.originFrameId))ui.recipe.originFrameId=remap[ui.recipe.originFrameId];
    if(!valid.count(ui.recipe.originFrameId))ui.recipe.originFrameId=submitted.value("origin",std::string());
    Raw::Bracketing::RemapCurves(ui.recipe,previous);
    Raw::Bracketing::ConstrainEnabledCurves(ui.recipe);
    if(!Raw::Bracketing::Validate(ui.recipe,ui.status,!process))return fail(ui.status);
    const auto recipe=Raw::Bracketing::Serialize(ui.recipe);
    nlohmann::json selection=nlohmann::json::array();
    for(const auto& source:ui.sources)if(valid.count(source.frameId))selection.push_back(source.frameId);
    if(process) {
        set->settings["algorithmVersion"]=Raw::Bracketing::RecipeVersion;
        set->settings["bracketing"]=recipe;set->settings.erase("bracketingDraft");
        set->settings["bracketingSelection"]=selection;set->settings.erase("bracketingDraftSelection");
        ++m_Project->snapshot->hdrInputRevision;
        ui.storedRecipe=recipe.dump();ui.editedRecipe=ui.storedRecipe;
        ui.processRequired=false;ui.pending=true;ui.failed=false;
        if(ui.job)ui.job->canceled=true;
        ui.publishRequested=true;
    } else if(ui.processRequired || recipe!=submitted) {
        set->settings["bracketingDraft"]=recipe;ui.processRequired=true;ui.pending=false;
        set->settings["bracketingDraftSelection"]=selection;
    }
    m_Project->snapshot->projectName=ui.projectName;
    ui.selectionRestored=false;RestoreBracketingSelection();MarkDirty();
    if(process) {
        if(!SaveActiveMultiFrameRawProject(error))return false;
        return StartBracketingProcessing(true,error);
    }
    ui.savingDraft=true;
    const bool saved=SaveActiveMultiFrameRawProject(error);
    ui.savingDraft=false;
    if (saved) RefreshBracketingProjectCard();
    return saved;
}
