#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include <algorithm>

// The former modal presentation is retired. Processing no longer locks View.
bool EditorModule::IsBracketingPresentationActive() const { return false; }
void EditorModule::UpdateBracketingPresentation(bool) {}
void EditorModule::CancelBracketingPresentation() {
    if (!m_Bracketing) return;
    auto& ui=*m_Bracketing;
    ui.pending=false;ui.publishRequested=false;ui.previewDirty=false;
    if(ui.waitingForMemory) {
        ui.waitingForMemory=false;ui.failed=true;ui.status="Canceled. Previous result retained.";
        ui.activityNotifier.CancelActivity(ui.activity,"Bracket processing cancelled.");
        ui.activityLease.reset();
        m_MultiFrameGraphProcessingTaskState=Async::TaskState::Idle;
    }
    if (ui.job) {ui.job->canceled=true;ui.status="Canceling...";}
}
void EditorModule::RenderBracketingPresentation(ImVec4,ImVec2 position,ImVec2 size) {
    if (!m_Bracketing || (!m_Bracketing->waitingForMemory && (!m_Bracketing->job || m_Bracketing->job->done))) return;
    const float width=std::min(360.f,std::max(1.f,size.x-32.f));
    ImGui::SetNextWindowPos({position.x+(size.x-width)*.5f,position.y+size.y-66.f});
    ImGui::SetNextWindowSize({width,0});
    if (ImGui::Begin("Bracket progress",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoFocusOnAppearing)) {
        const auto job=m_Bracketing->job;
        ImGui::ProgressBar(job?float(std::clamp(job->progress.load(),0.,1.)):0.f,{-1,5},"");
        ImGui::TextUnformatted(m_Bracketing->waitingForMemory?"Waiting for memory":"Processing bracket");ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) CancelBracketingPresentation();
    }
    ImGui::End();
}
