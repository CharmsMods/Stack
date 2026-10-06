#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <array>
#include <algorithm>

namespace Stack::Workspace {
inline void CloseOrdinaryPopups() {
    // Explicit View project actions keep the reviewed notification dialog.
    if (ImGui::GetTopMostPopupModal()) return;
    // ClosePopupToLevel requires an existing entry even in release builds.
    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::ClosePopupToLevel(0, true);
}
// Suppress direct polling as well as widgets. BeginDisabled alone does not
// block IsKeyPressed/IsMouseClicked in canvas tools.
class InputScope {
public:
    InputScope(bool block, bool finish) : active(block || finish) {
        if (!active) return;
        auto& io=ImGui::GetIO(); auto& g=*ImGui::GetCurrentContext();
        pos=io.MousePos; delta=io.MouseDelta; wheel=io.MouseWheel; wheelH=io.MouseWheelH;
        ctrl=io.KeyCtrl; shift=io.KeyShift; alt=io.KeyAlt; super=io.KeySuper; mods=io.KeyMods;
        characters = io.InputQueueCharacters;
        for(int i=0;i<ImGuiKey_NamedKey_COUNT;++i) { keys[i]=io.KeysData[i]; io.KeysData[i]={}; io.KeysData[i].DownDuration=io.KeysData[i].DownDurationPrev=-1; }
        for(int i=0;i<5;++i) {
            down[i]=io.MouseDown[i]; clicked[i]=io.MouseClicked[i]; released[i]=io.MouseReleased[i]; doubleClicked[i]=io.MouseDoubleClicked[i];
            counts[i]=io.MouseClickedCount[i]; io.MouseClickedCount[i]=0;
            io.MouseDown[i]=io.MouseClicked[i]=io.MouseDoubleClicked[i]=false;
            io.MouseReleased[i]=finish && down[i];
        }
        io.MouseDelta=ImVec2(0,0); io.MouseWheel=io.MouseWheelH=0;
        io.KeyCtrl=io.KeyShift=io.KeyAlt=io.KeySuper=false; io.KeyMods=0;
        nav=g.NavActivateId; g.NavActivateId=0;
        if(finish) {
            auto* enter=ImGui::GetKeyData(ImGuiKey_Enter); enter->Down=true; enter->DownDuration=0; enter->DownDurationPrev=-1;
        } else { io.MousePos=ImVec2(-FLT_MAX,-FLT_MAX); io.InputQueueCharacters.resize(0); }
    }
    ~InputScope() {
        if(!active) return;
        auto& io=ImGui::GetIO(); auto& g=*ImGui::GetCurrentContext();
        io.MousePos=pos; io.MouseDelta=delta; io.MouseWheel=wheel; io.MouseWheelH=wheelH;
        io.KeyCtrl=ctrl; io.KeyShift=shift; io.KeyAlt=alt; io.KeySuper=super; io.KeyMods=mods;
        io.InputQueueCharacters = characters;
        for(int i=0;i<ImGuiKey_NamedKey_COUNT;++i) io.KeysData[i]=keys[i];
        for(int i=0;i<5;++i) { io.MouseDown[i]=down[i]; io.MouseClicked[i]=clicked[i]; io.MouseReleased[i]=released[i]; io.MouseDoubleClicked[i]=doubleClicked[i]; io.MouseClickedCount[i]=counts[i]; }
        g.NavActivateId=nav;
    }
private:
    bool active,ctrl=false,shift=false,alt=false,super=false;
    bool down[5]{},clicked[5]{},released[5]{},doubleClicked[5]{};
    unsigned short counts[5]{};
    ImGuiKeyChord mods=0; ImGuiID nav=0;
    ImVec2 pos,delta; float wheel=0,wheelH=0;
    std::array<ImGuiKeyData,ImGuiKey_NamedKey_COUNT> keys;
    ImVector<ImWchar> characters;
};
}
