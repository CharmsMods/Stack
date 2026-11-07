#include "AutoBracketPresentation.h"
#include <imgui.h>
#include <algorithm>
namespace Stack::AutoBracket {
Presentation::~Presentation()=default;
void Presentation::Begin(const std::shared_ptr<Work>& work,double now) {
    generation=work->generation;animation.active=true;animation.began=now;visible=true;
}
void Presentation::Complete(const std::shared_ptr<Work>&,double) {animation.active=false;completed.reset();}
void Presentation::SetForeground(bool value) {foreground=value;}
void Presentation::Tick(double) {}
void DrawPresentation(Presentation& ui,AutoBracketCoordinator& coordinator,bool workspace) {
    if (!workspace && (!ui.visible || !coordinator.HasWork())) return;
    if (!workspace) {
        ImGui::SetNextWindowSize({360,0});
        ImGui::Begin("Automatic bracketing",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoFocusOnAppearing);
    }
    ImGui::TextUnformatted("Automatic bracketing");
    if (const auto work=coordinator.Current()) {
        ImGui::ProgressBar(float(std::clamp(work->progress.load(),0.,1.)),{-1,6},"");
        {std::lock_guard<std::mutex> lock(work->mutex);ImGui::TextWrapped("%s",work->status.c_str());}
        if (ImGui::Button("Cancel current")) {coordinator.PauseUntilIdle(ImGui::GetTime());coordinator.CancelCurrent();}
    } else ImGui::TextWrapped("Waiting for available resources. You can keep editing.");
    if (ImGui::Button(coordinator.GetQueue().paused?"Resume":"Pause"))
        coordinator.SetPaused(!coordinator.GetQueue().paused);
    if (!coordinator.Error().empty()) ImGui::TextWrapped("%s",coordinator.Error().c_str());
    if (!workspace) ImGui::End();
}
}
