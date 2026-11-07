#include "Raw/Bracketing/Panorama/Compatibility.h"
#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "App/WorkspacePresentation.h"
#include "App/WorkspaceInputScope.h"
#include "Utils/FileDialogs.h"
#include <algorithm>
#include <cstdio>

void EditorModule::RenderBracketingUI() {
    const bool preview=Stack::Workspace::IsPreview();
    if(!preview)BeginBracketingDraft();
    if(IsMultiFrameRawProjectActive()&&!IsBracketingActive()&&(!m_Bracketing||!m_Bracketing->newProject)) {
        RenderMultiFrameRawLabTool();return;
    }
    RenderBracketingSidePanelUI();
}

bool EditorModule::HasBracketingContributionTool() const {
    if(!m_Bracketing)return false;
    Stack::Editor::PrepareBracketingControls(*m_Bracketing);
    return m_Bracketing->controls.hasContributions;
}

void EditorModule::RenderBracketingSidePanelUI() {
    const bool preview=Stack::Workspace::IsPreview();
    if(!m_Bracketing) {
        ImGui::TextWrapped("Open Bracketing to review captures and processing settings.");
        ImGui::BeginDisabled(preview);
        if(ImGui::Button("Open Bracketing"))OpenBracketingTool();
        ImGui::EndDisabled();
        return;
    }
    auto& ui=*m_Bracketing;
    const bool running=ui.job&&!ui.job->done;
    using Phase=Stack::Project::ProjectLifecyclePhase;
    const auto phase=m_Project->lifecycle.Phase();
    const bool readOnly=phase==Phase::ReadOnlyRecovery||(m_Project->store&&m_Project->store->IsReadOnlyRecovery());
    const bool unavailable=!ui.awaitingProject.empty()||(!ui.newProject&&
        (phase==Phase::Loading||phase==Phase::Importing||phase==Phase::Conflict||readOnly||IsWorkspaceTransitionPending()));
    if(unavailable)ImGui::TextWrapped(readOnly?"Save a repaired copy before editing this recovered project.":
        phase==Phase::Conflict?"Resolve the project storage conflict before editing.":"The bracket project is busy.");
    ImGui::BeginDisabled(preview||unavailable);
    Stack::Editor::PrepareBracketingControls(ui);
    if(!ImGui::IsAnyItemActive())ui.gestureActive=false;
    const bool readingCaptures=ui.metadataJob||!ui.controls.readingCaptions.empty();
    ImGui::Text("%zu capture%s",ui.sources.size(),ui.sources.size()==1?"":"s");
    ImGui::BeginDisabled(running||readingCaptures||ui.sources.empty()||!ui.awaitingProject.empty());
    if(ImGui::Button(ui.failed?"Retry":"Process",ImVec2(-1,std::max(32.f,ImGui::GetFrameHeight()))))CommitBracketingDraft(true,nullptr);
    ImGui::EndDisabled();
    if(readingCaptures)ImGui::TextWrapped("Reading capture details. Process will be available when this finishes.");
    else ImGui::TextWrapped("%s",ui.status.c_str());
    ImGui::BeginDisabled(!ui.awaitingProject.empty());
    if(ImGui::SmallButton("New bracket")){BeginBracketingDraft(true);ImGui::EndDisabled();ImGui::EndDisabled();return;}
    ImGui::SameLine();if(ImGui::SmallButton("Save")) {
        if(ui.newProject)CommitBracketingDraft(false,nullptr);
        else RequestSaveCurrentProject();
    }
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
    bool changed=false;
    if(ImGui::CollapsingHeader("Processing",ImGuiTreeNodeFlags_DefaultOpen)) {
        static constexpr const char* reconstructionItems[]={"Standard (Bayer)","Super-resolution 1x","Super-resolution 2x","Panorama","Auto resolution"};
        int reconstruction=static_cast<int>(ui.recipe.reconstruction);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::Combo("##Reconstruction",&reconstruction,reconstructionItems,IM_ARRAYSIZE(reconstructionItems))){ui.recipe.reconstruction=static_cast<Raw::Bracketing::ReconstructionMode>(reconstruction);
            if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama)ui.recipe.aiAlignment=false;
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
        if(ui.recipe.reconstruction!=Raw::Bracketing::ReconstructionMode::Panorama) {
            static constexpr const char* smoothingItems[]={"Off","Super-resolution only","All bracket results"};
            int smoothing=static_cast<int>(ui.recipe.smoothing);
            if(ImGui::Combo("Burst-guided smoothing",&smoothing,smoothingItems,IM_ARRAYSIZE(smoothingItems))) {
                ui.recipe.smoothing=static_cast<Raw::Bracketing::BurstSmoothing>(smoothing);changed=true;
            }
        }
        if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama) {
            const char* projections[]={"Auto projection","Perspective","Spherical"};int projection=int(ui.recipe.panorama.projection);
            ImGui::SetNextItemWidth(-1);
            if(ImGui::Combo("##PanoramaProjection",&projection,projections,3)){ui.recipe.panorama.projection=static_cast<Raw::Bracketing::PanoramaProjection>(projection);changed=true;}
            float percent=float(ui.recipe.panorama.outputScale*100);
            ImGui::TextUnformatted("Output size");ImGui::SetNextItemWidth(-1);
            if(ImGui::SliderFloat("##OutputSize",&percent,1,100,"%.0f%%")){ui.recipe.panorama.outputScale=percent/100.;changed=true;}
            int denoise=ui.recipe.panorama.overlapDenoise?0:1;const char* noiseItems[]={"Overlap denoise: Auto","Overlap denoise: Off"};ImGui::SetNextItemWidth(-1);
            if(ImGui::Combo("##PanoramaDenoise",&denoise,noiseItems,2)){ui.recipe.panorama.overlapDenoise=denoise==0;changed=true;}
            ImGui::TextWrapped("Stitch overlapping captures in any direction. Uncovered borders remain transparent.");
        } else {
        const char* alignmentItems[]={"Automatic local","Global only","Translation only","Off (fixed sensor coordinates)"};
        int alignment=static_cast<int>(ui.recipe.alignmentMode);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::Combo("##Alignment",&alignment,alignmentItems,4)){ui.recipe.alignmentMode=static_cast<Raw::Bracketing::AlignmentMode>(alignment);
            if(ui.recipe.alignmentMode!=Raw::Bracketing::AlignmentMode::AutomaticLocal) {
                ui.recipe.aiAlignment=false;
            }
            changed=true;}
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("Off combines captures at identical sensor coordinates. AI assistance requires automatic local alignment.");
        ImGui::BeginDisabled(ui.recipe.alignmentMode!=Raw::Bracketing::AlignmentMode::AutomaticLocal);
        if(ImGui::Checkbox("AI-assisted alignment (experimental)",&ui.recipe.aiAlignment))changed=true;
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Uses SEA-RAFT to suggest motion, followed by RAW verification. Requires the optional model package. May take longer; never generates image pixels.");
        ImGui::EndDisabled();
        if(ui.recipe.alignmentMode!=Raw::Bracketing::AlignmentMode::FixedCoordinates) {
            ImGui::TextWrapped("Depth masks are created automatically when the captures show parallax.");
            if(ui.result && ui.result->raw && ui.result->raw->reconstructionDepth)
                ImGui::TextWrapped("%s",ui.result->raw->reconstructionDepth->message.c_str());
        }
        }
    }
    if(changed){ui.redo.clear();CommitBracketingEdit();}
    ui.hoverFrame.clear();
    if(ImGui::CollapsingHeader("Captures",ImGuiTreeNodeFlags_DefaultOpen))RenderBracketingGroups();
    if(!ui.recipe.groups.empty()&&ImGui::CollapsingHeader("History")) {
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
    }
    ImGui::EndDisabled();
    RenderBracketingOrientationReview();
}

void EditorModule::RenderBracketingContributionUI() {
    if(!m_Bracketing||!HasBracketingContributionTool())return;
    auto& ui=*m_Bracketing;
    const bool blocked=Stack::Workspace::IsPreview()||
        (ImGui::GetCurrentContext()->CurrentItemFlags&ImGuiItemFlags_Disabled)!=0;
    Stack::Workspace::InputScope input(blocked,false);
    ImGui::BeginDisabled(blocked);
    if(!ImGui::IsAnyItemActive())ui.gestureActive=false;
    if(Stack::Editor::DrawBracketingContributionTool(ui)&&!blocked) {
        ui.redo.clear();CommitBracketingEdit();
    }
    ImGui::EndDisabled();
}
