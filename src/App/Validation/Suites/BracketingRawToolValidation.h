#pragma once
#include "Editor/EditorModule.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/Bracketing/BracketingGallery.h"
#include "Persistence/BracketingProject.h"
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <thread>

struct BracketingRawToolValidationAccess {
    static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    static void Selection(EditorModule& e) {
        e.m_RawWorkspace.sources.clear();
        e.m_RawWorkspace.selectedSourceKeys.clear();
        e.m_RawWorkspace.workspaceRoot="C:/Stack-validation";
        for(const char* key:{"c.arw","a.arw","b.arw"}) {
            Stack::RawWorkspace::SourceRecord source;source.relativePathKey=key;
            source.fileName=key;source.absolutePath=e.m_RawWorkspace.workspaceRoot/key;
            e.m_RawWorkspace.sources.push_back(source);
        }
        auto& organization=e.m_RawWorkspaceManualGroupings[e.m_RawWorkspace.workspaceRoot.generic_string()];
        organization.sortMode=Stack::RawWorkspace::RawGalleryFilmstripSortMode::FileNameAscending;
        e.OpenBracketingTool();
        e.SelectBracketingSources({"a.arw"},false,false);
        e.SelectBracketingSources({"c.arw"},false,true);
        Check(e.m_Bracketing->sources.size()==3,"Shift-selection did not follow displayed order");
        e.SelectBracketingSources({"b.arw"},true,false);
        Check(e.m_Bracketing->sources.size()==2,"Ctrl-toggle did not remove the capture");
        e.SelectBracketingSources({"a.arw","b.arw","c.arw"},false,false);
        Check(e.m_Bracketing->sources.size()==3,"Collapsed stack did not select every original");
        e.SelectBracketingSources({"a.arw","b.arw","c.arw"},true,false);
        Check(e.m_Bracketing->sources.empty(),"Ctrl-toggle did not deselect a complete stack");
        Check(e.m_Bracketing->processRequired&&!e.m_Bracketing->job&&!e.m_HdrAdoptedRawResult,
            "Selection started processing or published a RAW result");
        e.SelectBracketingSources({"a.arw","b.arw"},false,false);
        for(auto& source:e.m_Bracketing->sources) {
            source.inspected=true;source.metadata.exposureTimeSeconds=.01;source.metadata.isoSpeed=100;
        }
        Stack::Editor::RegroupBracketingDraft(*e.m_Bracketing);
        Check(e.m_Bracketing->recipe.groups.size()==1,"Equal-exposure selection did not form a denoise group");
        auto before=e.m_Bracketing->recipe;
        auto curve=before;curve.knots.front().ev-=1;
        Check(!Stack::Editor::BracketingInputsChanged(before,curve),"Curve edit requires capture analysis");
        curve.alignmentMode=Raw::Bracketing::AlignmentMode::FixedCoordinates;
        Check(Stack::Editor::BracketingInputsChanged(before,curve),"Alignment edit did not require Process");
        e.RemoveBracketingDraftSource(e.m_Bracketing->sources.front().frameId);
        Check(e.m_Bracketing->sources.size()==1&&e.m_RawWorkspace.selectedSourceKeys.size()==1,
            "Removing a capture did not synchronize source selection");
        e.m_RawWorkspaceLabUi.activeTool=EditorModule::RawLabTool::Tone;
        Check(!e.IsBracketingToolActive()&&e.m_Bracketing->sources.size()==1,"Changing tools lost the draft");
        e.m_RawWorkspace.sources.clear();e.m_RawWorkspace.selectedSourceKeys.clear();
        e.m_Bracketing.reset();
    }
    static void SetWorkflowSources(EditorModule& e,const std::filesystem::path& root,
        const std::vector<std::filesystem::path>& files) {
        e.m_RawWorkspace.workspaceRoot=root;
        e.OpenBracketingTool();e.AddBracketingDraftFiles(files);
    }
    static void Panel(EditorModule& e) {
        e.OpenBracketingTool();
        auto& ui=*e.m_Bracketing;
        e.m_RawWorkspace.sources.clear();
        for(unsigned i=0;i<4;++i) {
            Stack::Editor::BracketingDraftSource source;
            source.frameId="capture-"+std::to_string(i);source.path=source.frameId+".dng";source.inspected=true;
            source.metadata.exposureTimeSeconds=i<2?.01:.04;source.metadata.isoSpeed=100;
            ui.sources.push_back(source);ui.frameLabels[source.frameId]=source.path.filename().string();
            Stack::RawWorkspace::SourceRecord record;record.relativePathKey=source.frameId;
            record.fileName=source.path.filename().string();record.absolutePath=source.path;
            e.m_RawWorkspace.sources.push_back(record);
            ui.sources.back().sourceKey=record.relativePathKey;
        }
        Stack::Editor::RegroupBracketingDraft(ui);
        ui.recipe.automatic=false;
        ui.status="Changes pending. Press Process to update the result.";
        e.InvalidateRawWorkspaceGalleryPresentation();
    }
    static void Gallery(EditorModule& e,bool filmstrip) {e.RenderRawWorkspaceLabGalleryContent(filmstrip,false,0.f);}
    static void PrepareDraftWorkspace(EditorModule& e,bool withCaptures) {
        // Avoid loading the user's catalog or app preferences in UI diagnostics.
        e.m_RawWorkspaceAppStateLoaded=true;
        e.m_RawWorkspace.workspaceRoot.clear();
        e.OpenBracketingTool();
        if(withCaptures) {
            Panel(e);
            e.m_RawWorkspace.workspaceRoot="C:/Stack-validation";
            for(const auto& source:e.m_Bracketing->sources)
                e.m_RawWorkspace.selectedSourceKeys.push_back(source.sourceKey);
            e.m_RawWorkspace.selectedSourceKey=e.m_RawWorkspace.selectedSourceKeys.front();
        }
        e.m_RawWorkspaceLabUi.galleryHost=EditorModule::RawGalleryHost::Filmstrip;
        e.m_RawWorkspaceLabUi.filmstripHeight=200.f;
    }
    static void FinishDraftWorkspace(EditorModule& e) {
        e.m_RawWorkspaceAppStateLoaded=false;
        e.m_RawWorkspace.workspaceRoot.clear();
    }
    static std::shared_ptr<Stack::Editor::BracketingSession> Draft(const EditorModule& e){return e.m_Bracketing;}
    static bool DraftOnly(const EditorModule& e) {
        return e.m_Bracketing&&e.m_Bracketing->newProject&&e.m_Bracketing->processRequired&&
            !e.m_Bracketing->job&&!e.m_HdrAdoptedRawResult&&!e.IsRawWorkspaceProjectActive();
    }
    static std::size_t SelectionCount(const EditorModule& e){return e.m_Bracketing->sources.size();}
    static bool MetadataReady(const EditorModule& e) {
        return e.m_Bracketing&&!e.m_Bracketing->metadataJob&&
            std::all_of(e.m_Bracketing->sources.begin(),e.m_Bracketing->sources.end(),[](const auto& s){return s.inspected;});
    }
    static void PendingPersistence(EditorModule& e) {
        e.OpenBracketingTool();
        std::string error;
        Check(e.CommitBracketingDraft(false,&error),error.c_str());
        Check(!e.HasPendingBracketingDraft(),"Saving a completed bracket created a pending draft");
        const auto original=e.m_Bracketing->recipe.alignmentMode;
        e.m_Bracketing->recipe.alignmentMode=original==Raw::Bracketing::AlignmentMode::FixedCoordinates?
            Raw::Bracketing::AlignmentMode::TranslationOnly:Raw::Bracketing::AlignmentMode::FixedCoordinates;
        e.CommitBracketingEdit();
        Check(e.HasPendingBracketingDraft(),"Input edit did not remain pending");
        const auto prior=e.m_HdrAdoptedRawResult?e.m_HdrAdoptedRawResult->rawData:nullptr;
        e.EnterBracketingRaw();
        Check(!e.m_Bracketing->job&&e.m_HdrAdoptedRawResult&&e.m_HdrAdoptedRawResult->rawData==prior,"RAW entry processed pending changes");
        Check(!e.StartActiveMultiFrameProcessingForQueue(&error),"Queue accepted an unprocessed input draft");
        Check(e.CommitBracketingDraft(false,&error),error.c_str());
        auto saved=Stack::Project::OpenProjectStore(e.GetCurrentProjectFileName());
        Check(bool(saved),saved.message.c_str());
        Check(saved.snapshot.sourceSets.front().settings.contains("bracketingDraft"),"Save lost the pending draft");
        Check(!e.m_Bracketing->job,"Save started a merge");
        {
            EditorModule reopened;
            EditorLoadedProjectData data;data.sourceState=ProjectSourceState::LazyAsset;
            data.projectStore=saved.store;data.rawProjectSnapshot=std::make_shared<Stack::Project::RawProjectSnapshot>(saved.snapshot);
            data.pipelineData=saved.snapshot.pipelineData;data.rawWorkspaceData=saved.snapshot.rawWorkspaceData;
            data.projectKind=StackBinaryFormat::kRawProjectKind;data.projectName=saved.snapshot.projectName;
            data.projectFileName=e.GetCurrentProjectFileName();
            Check(reopened.ApplyLoadedProject(data),"Could not reopen the pending bracket draft");
            reopened.OpenBracketingTool();reopened.TickBracketing();
            const auto restoreDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(reopened.m_Bracketing->job&&std::chrono::steady_clock::now()<restoreDeadline) {
                Check(reopened.m_Bracketing->job->restoredFromProject,"Reopening a pending draft started a merge");
                reopened.TickBracketing();std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            Check(reopened.HasPendingBracketingDraft()&&!reopened.m_Bracketing->job&&
                !reopened.m_Bracketing->publishRequested,"The saved result did not finish opening beside its pending draft");
            const auto restored=reopened.m_HdrAdoptedRawResult?reopened.m_HdrAdoptedRawResult->rawData:nullptr;
            Check(prior&&restored&&prior->contentIdentity==restored->contentIdentity&&
                prior->linearFloatBuffer==restored->linearFloatBuffer&&
                ((!prior->normalizedMosaicBuffer&&!restored->normalizedMosaicBuffer)||
                 (prior->normalizedMosaicBuffer&&restored->normalizedMosaicBuffer&&
                  *prior->normalizedMosaicBuffer==*restored->normalizedMosaicBuffer)),
                "Opening a pending draft lost the previously saved result");
            Check(reopened.m_Bracketing->recipe.alignmentMode==e.m_Bracketing->recipe.alignmentMode,
                "Reopening a pending draft lost the edited alignment mode");
        }
        e.DiscardBracketingDraft();
        Check(!e.HasPendingBracketingDraft()&&e.m_Bracketing->recipe.alignmentMode==original,
            "Discard did not restore the submitted configuration");
        e.m_Bracketing->job=std::make_shared<Stack::Editor::BracketingJob>();
        auto obsolete=e.m_Bracketing->job;
        for(auto& group:e.m_Bracketing->recipe.groups)group.enabled=false;
        e.CommitBracketingEdit();
        Check(obsolete->canceled,"Input edit did not cancel the obsolete merge");
        obsolete->done=true;e.TickBracketing();
        Check(!e.m_Bracketing->job&&e.m_HdrAdoptedRawResult->rawData==prior,"Obsolete merge replaced the previous result");
        Check(e.SaveActiveMultiFrameRawProject(&error),error.c_str());
        saved=Stack::Project::OpenProjectStore(e.GetCurrentProjectFileName());Check(bool(saved),saved.message.c_str());
        Raw::Bracketing::BracketingRecipe inactive;
        Check(Raw::Bracketing::Deserialize(saved.snapshot.sourceSets.front().settings["bracketingDraft"],inactive,error,true),error.c_str());
        Check(std::none_of(inactive.groups.begin(),inactive.groups.end(),[](const auto& g){return g.enabled;}),"Saving changed disabled draft groups");
        Check(!e.RequestQueueExportImage("should-not-export.png"),"PNG export accepted a pending draft");
        e.DiscardBracketingDraft();
    }
    static void ResultStack(EditorModule& e) {
        const auto& snapshot=*e.m_Project->snapshot;
        const auto before=e.ResolveRawWorkspaceFilmstripStacks();
        Stack::Editor::UpdateBracketingGalleryProject(e.m_RawWorkspace,snapshot,e.GetCurrentProjectFileName());
        e.InvalidateRawWorkspaceGalleryPresentation();
        const auto stacks=e.ResolveRawWorkspaceFilmstripStacks();
        Check(stacks.size()==before.size(),"Saving a bracket changed original grouping");
        for(const auto& stack:stacks)Check(stack.resultProjectId.empty(),"A result replaced an original stack");
        const auto previousMode=e.m_RawWorkspaceGalleryContentMode;
        e.m_RawWorkspaceGalleryContentMode=Stack::RawWorkspace::GalleryContentMode::Bracket;
        const auto& brackets=e.GetRawWorkspaceCategoryPresentation();
        Check(std::any_of(brackets.projects.begin(),brackets.projects.end(),[&](const auto& project) {
            return project.projectId==snapshot.projectId&&project.bracketingProject;
        }),"Saved bracket is absent from Bracket");
        e.m_RawWorkspaceGalleryContentMode=Stack::RawWorkspace::GalleryContentMode::Projects;
        const auto& projects=e.GetRawWorkspaceCategoryPresentation();
        Check(std::none_of(projects.projects.begin(),projects.projects.end(),[](const auto& project) {
            return project.bracketingProject;
        }),"Projects contains bracket projects");
        e.m_RawWorkspaceGalleryContentMode=previousMode;
        for(const auto& source:e.m_Bracketing->sources)
            Check(source.sourceKey.rfind("project-overlay:",0)!=0,"The bracket result became an original capture");
        std::cout<<"Bracket categorization and preserved source grouping: passed\n";
    }
    static void InteractiveCurve(EditorModule& e) {
        const auto original=e.m_Bracketing->recipe;
        const auto analysis=e.m_Bracketing->result->analysis;
        const auto published=e.m_HdrAdoptedRawResult->rawData;
        Check(!e.m_Bracketing->preview.samples.empty(),"The reconstruction lost its bounded interactive blend samples");
        e.m_Bracketing->detail=e.m_Bracketing->preview;
        e.m_Bracketing->detailMode=true;
        e.m_Bracketing->detail.resultRgb.front()=-9876.f;
        e.m_Bracketing->recipe.automatic=false;
        e.m_Bracketing->recipe.knots.front().ev-=.1;
        ImGui::NewFrame();e.CommitBracketingEdit();e.TickBracketing();ImGui::EndFrame();
        Check(!e.HasPendingBracketingDraft()&&e.m_Bracketing->interactiveRaw,"Curve edit did not create a bounded preview");
        Check(e.m_Bracketing->result->analysis==analysis,"Curve edit replaced prepared analysis");
        Check(e.m_HdrAdoptedRawResult->rawData==published,"Interactive preview was adopted as the full result");
        const auto rendered=e.BuildRenderSnapshot(123);
        bool interactive=false;
        for(const auto& node:rendered.graph.nodes)interactive|=node.rawDevelopment.embeddedRawData==e.m_Bracketing->interactiveRaw;
        Check(interactive,"RAW rendering did not receive the interactive blend");
        Check(!e.RequestQueueExportImage("should-not-export.png"),"Export accepted an unfinished contribution edit");
        e.m_Bracketing->recipe=original;
        ImGui::NewFrame();e.CommitBracketingEdit();ImGui::EndFrame();
    }
    static void UpdatedNativeRegion(EditorModule& e) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(e.m_Bracketing->detailJob&&std::chrono::steady_clock::now()<deadline) {
            e.TickBracketing();std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(!e.m_Bracketing->detailJob&&e.m_Bracketing->detailMode&&
            e.m_Bracketing->detail.resultRgb.front()!=-9876.f,"Native inspection retained an obsolete contribution result");
        e.m_Bracketing->detailMode=false;
    }
};
