#include "AutoBracketEditingScope.h"
#include "Editor/EditorModule.h"
#include <vector>
namespace Stack::AutoBracket {
EditingScope::EditingScope(EditorModule& editor,ImVec2 minimum,ImVec2 size) {
    active_=editor.AutoBracketWorkActive();
    if(!active_)return;
    const auto& io=ImGui::GetIO();
    const bool hovered=io.MousePos.x>=minimum.x&&io.MousePos.y>=minimum.y&&
        io.MousePos.x<minimum.x+size.x&&io.MousePos.y<minimum.y+size.y;
    const float wheel=hovered?io.MouseWheel:0.f,wheelH=hovered?io.MouseWheelH:0.f;
    bool action=wheel!=0||wheelH!=0;
    int clicked=-1;
    for(int i=0;i<5;++i)if(hovered&&ImGui::IsMouseClicked(i)){action=true;clicked=i;}
    std::vector<ImGuiKey> keys;
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        for(int key=ImGuiKey_NamedKey_BEGIN;key<ImGuiKey_GamepadStart;++key) {
            if(key>=ImGuiKey_LeftCtrl&&key<=ImGuiKey_RightSuper)continue;
            if(ImGui::IsKeyPressed(static_cast<ImGuiKey>(key),false))keys.push_back(static_cast<ImGuiKey>(key));
        }
    action|=!keys.empty();
    if(action) {
        const auto position=io.MousePos;
        const auto windowId=ImGui::GetCurrentWindow()->ID;
        const auto windowPosition=ImGui::GetWindowPos(),windowSize=ImGui::GetWindowSize();
        const auto modifiers=io.KeyMods;
        editor.RequestAutoBracketForeground("continue editing",[position,clicked,wheel,wheelH,keys,windowId,windowPosition,windowSize,modifiers] {
            // Replay only while the original tool layout still occupies this region.
            // A moved or closed window must not redirect a deferred click elsewhere.
            const auto* window=ImGui::FindWindowByID(windowId);
            if(!window||!window->WasActive||window->Pos.x!=windowPosition.x||window->Pos.y!=windowPosition.y||
                window->Size.x!=windowSize.x||window->Size.y!=windowSize.y)return;
            auto& input=ImGui::GetIO();const auto currentModifiers=input.KeyMods;input.AddMousePosEvent(position.x,position.y);
            for(const auto mod:{ImGuiMod_Ctrl,ImGuiMod_Shift,ImGuiMod_Alt,ImGuiMod_Super})
                input.AddKeyEvent(static_cast<ImGuiKey>(mod),(modifiers&mod)!=0);
            if(clicked>=0){input.AddMouseButtonEvent(clicked,true);input.AddMouseButtonEvent(clicked,false);}
            if(wheel!=0||wheelH!=0)input.AddMouseWheelEvent(wheelH,wheel);
            for(const auto key:keys){input.AddKeyEvent(key,true);input.AddKeyEvent(key,false);}
            for(const auto mod:{ImGuiMod_Ctrl,ImGuiMod_Shift,ImGuiMod_Alt,ImGuiMod_Super})
                input.AddKeyEvent(static_cast<ImGuiKey>(mod),(currentModifiers&mod)!=0);
        });
    }
    characters_=io.InputQueueCharacters;
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1.f);ImGui::BeginDisabled();
    input_=std::make_unique<Workspace::InputScope>(true,false);
}
EditingScope::~EditingScope(){if(active_){input_.reset();ImGui::GetIO().InputQueueCharacters=characters_;ImGui::EndDisabled();ImGui::PopStyleVar();}}
}
