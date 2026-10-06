#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "App/WorkspacePresentation.h"

bool EditorModule::RenderBracketingViewport() {
    if(!m_Bracketing)return false;
    auto& ui=*m_Bracketing;
    if(ImGui::RadioButton("RAW result",ui.gradedResult))ui.gradedResult=true;
    ImGui::SameLine();
    if(ImGui::RadioButton("Inspect sources and masks",!ui.gradedResult))ui.gradedResult=false;
    if(ui.processRequired) {
        if(ui.newProject||!ui.result)
            ImGui::TextWrapped("No bracket result yet. Select captures, then press Process in the Bracketing panel.");
        else ImGui::TextDisabled("Previous result. Input changes require Process.");
    }
    else if(ui.interactiveRaw)ImGui::TextWrapped(ui.result&&ui.result->reconstructionInspection?
        "Interactive temporal preview. Full reconstruction pending.":"Interactive contribution preview. Full result processing.");
    // A draft without an opened image has nothing for the ordinary RAW
    // viewport to display. Do not replace these instructions with its prompt
    // to open an individual source project.
    if(ui.processRequired&&!ui.result&&!IsRawWorkspaceProjectActive()&&!IsMultiFrameRawProjectActive())return true;
    const bool inspecting=!ui.gradedResult||!ui.hoverFrame.empty();
    if(!inspecting)return false;
    if(ui.result&&!ui.result->analysis) {
        if(!ui.preparationJob&&!ui.preparationFailed)ui.preparationRequested=true;
        if(ui.preparationJob) {
            ImGui::TextWrapped("Preparing source inspection. The saved result stays available in RAW.");
            if(ImGui::SmallButton("Cancel preparation")){ui.preparationJob->canceled=true;ui.preparationFailed=true;}
        }else if(ui.preparationFailed) {
            ImGui::TextWrapped("%s",ui.inspectionError.c_str());
            if(ImGui::SmallButton("Retry preparation")){ui.preparationFailed=false;ui.preparationRequested=true;}
        }
    }
    ImGui::TextDisabled("Neutral inspection / independent of RAW adjustments");
    ImGui::BeginDisabled(Stack::Workspace::IsPreview());
    Stack::Editor::DrawBracketingPreview(ui,ImGui::GetContentRegionAvail());
    ImGui::EndDisabled();
    return true;
}
