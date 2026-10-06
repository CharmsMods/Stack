#include "Raw/Bracketing/Panorama/Compatibility.h"
#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "App/WorkspacePresentation.h"
#include "Utils/FileDialogs.h"
#include <algorithm>
#include <cstdio>

void EditorModule::RenderBracketingUI() {
    const bool preview=Stack::Workspace::IsPreview();
    if(!preview)BeginBracketingDraft();
    if(IsMultiFrameRawProjectActive()&&!IsBracketingActive()&&(!m_Bracketing||!m_Bracketing->newProject)) {
        RenderMultiFrameRawLabTool();return;
    }
    ImGui::BeginDisabled(preview);
    if(!m_Bracketing){ImGui::TextWrapped("Select captures in the filmstrip or Gallery.");ImGui::EndDisabled();return;}
    auto& ui=*m_Bracketing;
    const bool running=ui.job&&!ui.job->done;
    const bool readingCaptures=ui.metadataJob||std::any_of(ui.sources.begin(),ui.sources.end(),
        [](const auto& source){return !source.inspected;});
    ImGui::Text("%zu capture%s selected",ui.sources.size(),ui.sources.size()==1?"":"s");
    ImGui::BeginDisabled(running||readingCaptures||ui.sources.empty()||!ui.awaitingProject.empty());
    if(ImGui::Button(ui.failed?"Retry":"Process",ImVec2(-1,std::max(32.f,ImGui::GetFrameHeight()))))CommitBracketingDraft(true,nullptr);
    ImGui::EndDisabled();
    if(running) {
        std::string message;{std::lock_guard<std::mutex> lock(ui.job->mutex);message=ui.job->message;}
        ImGui::ProgressBar(static_cast<float>(ui.job->progress.load()),ImVec2(-1,5),"");
        ImGui::TextWrapped("%s",message.c_str());
        if(ImGui::Button("Cancel")) {
            ui.job->canceled=true;ui.pending=false;ui.publishRequested=false;ui.failed=true;
            ui.status="Canceled. Previous result retained.";
            if(ui.interactiveRaw){ui.interactiveRaw.reset();MarkRenderDirty();}
        }
    }
    if(readingCaptures)ImGui::TextWrapped("Reading capture details. Process will be available when this finishes.");
    else ImGui::TextWrapped("%s",ui.status.c_str());
    ImGui::BeginDisabled(running||!ui.awaitingProject.empty());
    if(ImGui::SmallButton("New bracket")){BeginBracketingDraft(true);ImGui::EndDisabled();ImGui::EndDisabled();return;}
    ImGui::SameLine();if(ImGui::SmallButton("Save"))CommitBracketingDraft(false,nullptr);
    ImGui::SameLine();if(ImGui::SmallButton("Import")) {
        std::vector<std::filesystem::path> paths;
        for(const auto& path:FileDialogs::OpenMultipleFilesDialog("Add RAW captures"))paths.emplace_back(path);
        AddBracketingDraftFiles(paths);
    }
    if(ui.recipe.reconstruction!=Raw::Bracketing::ReconstructionMode::Panorama && !readingCaptures && !ui.sources.empty() && ImGui::SmallButton("Review orientations")) {
        ui.orientationChoices=ui.recipe.orientationOverrides;
        ui.orientationReviewRequested=true;
        ui.processAfterOrientationReview=true;
    }
    if(ui.processRequired&&!ui.newProject&&ImGui::SmallButton("Discard input changes"))DiscardBracketingDraft();
    ImGui::EndDisabled();
    char name[256];std::snprintf(name,sizeof(name),"%s",ui.projectName.c_str());
    ImGui::SetNextItemWidth(-1);
    if(ImGui::InputTextWithHint("##BracketName","Bracket name",name,sizeof(name))) {
        ui.projectName=name;
        if(!ui.newProject&&m_Project->snapshot) {
            m_Project->snapshot->projectName=ui.projectName;
            MarkDirty();
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("BracketToolControls",ImVec2(0,0));
    bool changed=false;
    if(ImGui::CollapsingHeader("Processing",ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* reconstructionItems[]={"Standard","Super-resolution 1x","Super-resolution 2x","Panorama"};
        int reconstruction=static_cast<int>(ui.recipe.reconstruction);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::Combo("##Reconstruction",&reconstruction,reconstructionItems,4)){ui.recipe.reconstruction=static_cast<Raw::Bracketing::ReconstructionMode>(reconstruction);
            const auto reference=std::find_if(ui.sources.begin(),ui.sources.end(),[](const auto& source){return source.inspected&&source.metadata.supported;});
            if(reference!=ui.sources.end())for(auto& source:ui.sources)if(source.inspected) {
                source.error=source.metadata.rejectionReason;
                if(!source.error.empty())continue;
                if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama)
                    Raw::Bracketing::Panorama::Compatible(reference->metadata,source.metadata,&source.error);
                else {auto candidate=source.metadata;candidate.orientation=reference->metadata.orientation;
                    Stack::Project::AreHdrCapturesStructurallyCompatible(reference->metadata,candidate,&source.error);}
                ui.sourceHistory[source.frameId]=source;
            }
            changed=true;}
        if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama) {
            const char* projections[]={"Auto projection","Perspective","Spherical"};int projection=int(ui.recipe.panorama.projection);
            ImGui::SetNextItemWidth(-1);
            if(ImGui::Combo("##PanoramaProjection",&projection,projections,3)){ui.recipe.panorama.projection=static_cast<Raw::Bracketing::PanoramaProjection>(projection);changed=true;}
            float percent=float(ui.recipe.panorama.outputScale*100);ImGui::SetNextItemWidth(-1);
            if(ImGui::SliderFloat("Output size",&percent,1,100,"%.0f%%")){ui.recipe.panorama.outputScale=percent/100.;changed=true;}
            int denoise=ui.recipe.panorama.overlapDenoise?0:1;const char* noiseItems[]={"Overlap denoise: Auto","Overlap denoise: Off"};ImGui::SetNextItemWidth(-1);
            if(ImGui::Combo("##PanoramaDenoise",&denoise,noiseItems,2)){ui.recipe.panorama.overlapDenoise=denoise==0;changed=true;}
            ImGui::TextWrapped("Stitch overlapping captures in any direction. Uncovered borders remain transparent.");
        } else {
        const char* alignmentItems[]={"Automatic local","Global only","Translation only","Fixed coordinates"};
        int alignment=static_cast<int>(ui.recipe.alignmentMode);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::Combo("##Alignment",&alignment,alignmentItems,4)){ui.recipe.alignmentMode=static_cast<Raw::Bracketing::AlignmentMode>(alignment);changed=true;}
        }
    }
    if(changed){ui.redo.clear();CommitBracketingEdit();}
    ui.hoverFrame.clear();
    if(ImGui::CollapsingHeader("Captures",ImGuiTreeNodeFlags_DefaultOpen))RenderBracketingGroups();
    const bool panorama=ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama;
    if(!ui.recipe.groups.empty()&&ImGui::CollapsingHeader(panorama?"History":"Contributions",ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto restoreSelection=[&] {
            ui.sources.clear();m_RawWorkspace.selectedSourceKeys.clear();
            for(const auto& group:ui.recipe.groups)for(const auto& frame:group.frames) {
                const auto found=ui.sourceHistory.find(frame.id);if(found==ui.sourceHistory.end())continue;
                ui.sources.push_back(found->second);
                if(!found->second.sourceKey.empty())m_RawWorkspace.selectedSourceKeys.push_back(found->second.sourceKey);
            }
            InvalidateRawWorkspaceGalleryPresentation();
        };
        if(!ImGui::IsAnyItemActive())ui.gestureActive=false;
        ImGui::BeginDisabled(ui.undo.empty());
        if(ImGui::SmallButton("Undo")) {
            auto target=ui.undo.back();ui.undo.pop_back();auto undo=ui.undo;auto redo=ui.redo;redo.push_back(ui.recipe);
            ui.recipe=target;restoreSelection();CommitBracketingEdit();ui.undo=undo;ui.redo=redo;
        }
        ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(ui.redo.empty());
        if(ImGui::SmallButton("Redo")) {
            auto target=ui.redo.back();ui.redo.pop_back();auto redo=ui.redo;
            ui.recipe=target;restoreSelection();CommitBracketingEdit();ui.redo=redo;
        }
        ImGui::EndDisabled();
        if(!panorama&&Stack::Editor::DrawBracketingCurves(ui,ImVec2(std::max(80.f,ImGui::GetContentRegionAvail().x-8),180))) {
            ui.redo.clear();CommitBracketingEdit();
        }
    }
    ImGui::EndChild();ImGui::EndDisabled();
    RenderBracketingOrientationReview();
}
