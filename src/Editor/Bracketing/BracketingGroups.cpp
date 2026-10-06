#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "App/WorkspacePresentation.h"
#include <cstdio>

void EditorModule::RenderBracketingGroups() {
    if(!m_Bracketing)return;
    auto& ui=*m_Bracketing;
    if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama) {
        bool changed=false;ui.hoverFrame.clear();
        for(auto& group:ui.recipe.groups) {
            ImGui::PushID(group.id.c_str());
            if(!group.enabled)changed|=ImGui::Checkbox("Enable this capture group",&group.enabled);
            for(auto& frame:group.frames) {
                ImGui::PushID(frame.id.c_str());ImGui::BeginDisabled(!group.enabled);
                changed|=ImGui::Checkbox("##Enabled",&frame.enabled);ImGui::SameLine();
                const auto label=ui.frameLabels.count(frame.id)?ui.frameLabels.at(frame.id):frame.id;
                if(ImGui::Selectable(label.c_str(),ui.selectedFrame==frame.id)){ui.selectedFrame=frame.id;ui.view=1;ui.gradedResult=false;}
                if(ImGui::IsItemHovered())ui.hoverFrame=frame.id;
                if(ImGui::BeginPopupContextItem("Panorama capture")) {
                    if(ImGui::MenuItem("Use as reference",nullptr,frame.id==ui.recipe.originFrameId)&&frame.enabled){ui.recipe.originFrameId=frame.id;changed=true;}
                    ImGui::EndPopup();
                }
                ImGui::EndDisabled();ImGui::PopID();
            }
            ImGui::PopID();
        }
        if(changed&&!Stack::Workspace::IsPreview()){ui.redo.clear();CommitBracketingEdit();}
        return;
    }
    if(IsBracketingActive()) {
        const auto* set=Stack::Project::FindSourceSet(*m_Project->snapshot,ui.setId);
        if(set&&set->settings.contains("identicalCaptureExclusions")&&ImGui::CollapsingHeader("Identical captures excluded"))
            for(const auto& exclusion:set->settings["identicalCaptureExclusions"])
                ImGui::TextWrapped("%s / same capture as %s",exclusion.value("excluded",std::string()).c_str(),exclusion.value("kept",std::string()).c_str());
    }
    bool changed=false;const bool preview=Stack::Workspace::IsPreview();
    std::string remove;
    for(const auto& source:ui.sources)if(!source.inspected) {
        ImGui::TextWrapped("%s / reading capture settings...",source.path.filename().string().c_str());
    }
    int moveGroup=-1,moveFrame=-1,moveTo=-1;
    ui.hoverFrame.clear();

    ImGui::BeginDisabled(preview);
    ImGui::TextUnformatted("Exposure groups");
    ImGui::TextWrapped("Hover a capture to compare. Click its circle to pin it.");

    for(std::size_t g=0;g<ui.recipe.groups.size();++g) {
        auto& group=ui.recipe.groups[g];ImGui::PushID(group.id.c_str());
        changed|=ImGui::Checkbox("##GroupEnabled",&group.enabled);ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text,Stack::Editor::BracketColor(ui.recipe,g));
        if(ImGui::Selectable(group.name.c_str(),ui.selectedGroup==static_cast<int>(g))) {ui.selectedGroup=static_cast<int>(g);ui.textureIdentity.clear();}
        ImGui::PopStyleColor();
        if(ImGui::BeginPopupContextItem("Group options")) {
            char name[256];std::snprintf(name,sizeof(name),"%s",group.name.c_str());
            if(ImGui::InputText("Name",name,sizeof(name))) {group.name=name;changed=true;}
            ImGui::EndPopup();
        }
        for(std::size_t i=0;i<group.frames.size();++i) {
            auto& frame=group.frames[i];ImGui::PushID(frame.id.c_str());
            const auto source=std::find_if(ui.sources.begin(),ui.sources.end(),[&](const auto& s){return s.frameId==frame.id;});
            if(source==ui.sources.end()&&frame.id==ui.recipe.originFrameId) {
                ImGui::TextDisabled("Exposure origin retained for calibration.");ImGui::PopID();continue;
            }
            changed|=ImGui::Checkbox("##FrameEnabled",&frame.enabled);ImGui::SameLine();
            const auto label=ui.frameLabels.count(frame.id)?ui.frameLabels.at(frame.id):frame.id;
            ui.frameLabels[frame.id]=label;
            if(ImGui::RadioButton("##CompareCapture",!ui.gradedResult&&ui.selectedFrame==frame.id)) {ui.gradedResult=false;ui.selectedFrame=frame.id;ui.selectedGroup=static_cast<int>(g);ui.view=3;}
            if(ImGui::IsItemHovered())ui.hoverFrame=frame.id;
            ImGui::SameLine();
            const bool open=ImGui::TreeNode("Capture","%s%s",label.c_str(),frame.id==ui.recipe.originFrameId?" [0 EV origin]":"");
            if(ImGui::IsItemHovered())ui.hoverFrame=frame.id;
            if(open) {
                if(source!=ui.sources.end()) {
                    const auto& metadata=source->metadata;
                    ImGui::TextDisabled("%.6g s / ISO %.0f / f/%.1f",metadata.exposureTimeSeconds,metadata.isoSpeed,metadata.apertureFNumber);
                    if(!source->error.empty())ImGui::TextWrapped("%s",source->error.c_str());
                    if(ImGui::SmallButton("Remove capture"))remove=frame.id;
                } else ImGui::TextDisabled("Retained exposure origin, excluded from the merge.");
                if(ui.result&&ui.result->analysis) {
                    const auto& analysis=*ui.result->analysis;
                    const auto calibrated=analysis.calibratedEv.find(frame.id);
                    if(calibrated!=analysis.calibratedEv.end()) ImGui::TextDisabled("Measured relative exposure: %+.2f EV",calibrated->second);
                    const auto noise=analysis.noiseConfidence.find(frame.id);
                    if(noise!=analysis.noiseConfidence.end()) ImGui::TextWrapped("Noise estimate: %s",noise->second.c_str());
                    const auto alignmentStatus=analysis.alignments.find(frame.id);
                    if(alignmentStatus!=analysis.alignments.end()) {
                        const auto& status=alignmentStatus->second;
                        ImGui::TextWrapped("Alignment: %s",status.message.c_str());
                        if(status.model!=Raw::Bracketing::CaptureAlignment::Model::Identity)
                            ImGui::TextDisabled("Shift %+.2f, %+.2f px / rotation %+.3f deg / overlap %.1f%%",
                                status.translationX,status.translationY,status.rotationDegrees,status.overlapFraction*100);
                        if(status.localApplied) ImGui::TextDisabled(
                            "Local accepted %.1f%% / uncertain %.1f%% / rejected %.1f%% / %s",
                            status.acceptedCoverage*100,status.uncertainCoverage*100,
                            status.rejectedCoverage*100,status.backend.c_str());
                    }
                }
                ImGui::BeginDisabled(frame.id==ui.recipe.originFrameId);
                changed|=ImGui::Checkbox("Manual relative EV",&frame.manualExposure);
                ImGui::SetNextItemWidth(90);float ev=static_cast<float>(frame.relativeEv);
                if(ImGui::InputFloat("EV",&ev,.1f,1,"%.2f")) {frame.relativeEv=std::clamp(static_cast<double>(ev),-32.0,32.0);frame.manualExposure=true;changed=true;}
                ImGui::EndDisabled();
                if(ImGui::BeginCombo("Move to",group.name.c_str())) {
                    for(std::size_t target=0;target<ui.recipe.groups.size();++target) if(target!=g&&ImGui::Selectable(ui.recipe.groups[target].name.c_str())) {moveGroup=static_cast<int>(g);moveFrame=static_cast<int>(i);moveTo=static_cast<int>(target);}
                    if(group.frames.size()>1&&ImGui::Selectable("New group")) {moveGroup=static_cast<int>(g);moveFrame=static_cast<int>(i);moveTo=static_cast<int>(ui.recipe.groups.size());}
                    ImGui::EndCombo();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::Separator();ImGui::PopID();
    }
    if(moveGroup>=0) {
        const auto previousGroups=ui.recipe.groups;
        auto frame=ui.recipe.groups[moveGroup].frames[moveFrame];
        ui.recipe.groups[moveGroup].frames.erase(ui.recipe.groups[moveGroup].frames.begin()+moveFrame);
        if(moveTo==ui.recipe.groups.size()) ui.recipe.groups.push_back({Stack::Project::GenerateStableUuid(),"Exposure "+std::to_string(moveTo+1),true,{}});
        ui.recipe.groups[moveTo].frames.push_back(std::move(frame));
        if(ui.recipe.groups[moveGroup].frames.empty()) ui.recipe.groups.erase(ui.recipe.groups.begin()+moveGroup);
        Raw::Bracketing::RemapCurves(ui.recipe,previousGroups);changed=true;ui.selectedPoint=-1;
    }
    ImGui::EndDisabled();
    if(ui.result&&ui.result->analysis&&ImGui::CollapsingHeader("Measurement status")) {
        for(const auto& message:ui.result->analysis->diagnostics) ImGui::TextWrapped("%s",message.c_str());
        ImGui::Text("Unrecoverable samples: %llu",static_cast<unsigned long long>(ui.result->unrecoverableSamples));
        ImGui::Text("Fallback samples: %llu",static_cast<unsigned long long>(ui.result->fallbackSamples));
    }

    if(changed&&!preview){ui.redo.clear();CommitBracketingEdit();}
    if(!remove.empty())RemoveBracketingDraftSource(remove);
}
