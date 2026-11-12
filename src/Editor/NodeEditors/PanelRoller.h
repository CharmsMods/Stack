#pragma once
#include <imgui.h>
#include <unordered_map>

namespace Stack::Editor {
struct PanelRollerState {
    bool dragging=false;
    float before=0, exact=0, phase=0;
    ImVec2 press{};
    void EndInteraction() { dragging=false; }
};
struct PanelControlState {
    std::unordered_map<ImGuiID,PanelRollerState> rollers;
    int hslChannel=0;
    int selectedFamily=0;
    bool settingsOpen=false;
    void EndInteraction() { for(auto& entry:rollers) entry.second.EndInteraction(); }
};
struct PanelRollerGestureResult {
    bool started=false;
    bool cancelled=false;
};
// Presentation only. The caller edits a draft and owns the transaction.
bool PanelRoller(const char* label,const char* id,float& value,float minimum,float maximum,
    float speed,float reset,const char* format,PanelControlState& state,float width=0,PanelRollerGestureResult* gestureResult=nullptr);
void PanelRollerPlaceholder(const char* label,float width=0);
}
