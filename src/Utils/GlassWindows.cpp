// SPDX-License-Identifier: GPL-3.0-only
#include "GlassControls.h"

namespace ImGuiExtras {
namespace {
// Only background color is suppressed during native Begin. The native window,
// popup, menu and child lifecycle (including title bars and input) stays intact.
class Background {
public:
    explicit Background(ImGuiCol role,bool eligible=true):role_(role),tint_(ImGui::GetStyleColorVec4(role)),enabled_(eligible&&GlassFrameEnabled()) {
        if(enabled_)ImGui::PushStyleColor(role,ImVec4(tint_.x,tint_.y,tint_.z,0));
    }
    void Finish(bool visible,bool attachedLeft=false){
        if(!enabled_)return;
        ImGui::PopStyleColor();enabled_=false;
        if(!visible)return;
        auto* window=ImGui::GetCurrentWindow();
        if(window->BeginCount>1)return; // Append-Begin must not cover existing content.
        const ImVec2 min(window->Pos.x,window->Pos.y+window->TitleBarHeight);
        const ImVec2 max(window->Pos.x+window->Size.x,window->Pos.y+window->Size.y);
        auto* draw=window->DrawList;
        draw->PushClipRect(window->OuterRectClipped.Min,window->OuterRectClipped.Max,false);
        const bool attached=attachedLeft || (min.x<=ImGui::GetMainViewport()->Pos.x+.5f && !(window->Flags&ImGuiWindowFlags_Popup));
        const float radius=attached?14.f*ImGui::GetFontSize()/16.f:window->WindowRounding;
        if(!DrawGlassSurface(*draw,min,max,tint_,radius,attached))
            draw->AddRectFilled(min,max,ImGui::GetColorU32(tint_),window->WindowRounding);
        draw->PopClipRect();
    }
    ~Background(){if(enabled_)ImGui::PopStyleColor();}
private:
    ImGuiCol role_;ImVec4 tint_;bool enabled_;
};
bool MainWindow(const char* name){
    const auto* old=ImGui::FindWindowByName(name);
    if(GImGui->NextWindowData.HasFlags&ImGuiNextWindowDataFlags_HasViewport)
        return GImGui->NextWindowData.ViewportId==ImGui::GetMainViewport()->ID;
    return !old || old->Viewport==ImGui::GetMainViewport();
}
bool MainCurrent(){return ImGui::GetWindowViewport()==ImGui::GetMainViewport();}
}
bool GlassBegin(const char* name,bool* open,ImGuiWindowFlags flags,bool attachedLeft){
    Background background(ImGuiCol_WindowBg,MainWindow(name)&&!(flags&ImGuiWindowFlags_NoBackground));
    const bool visible=ImGui::Begin(name,open,flags);background.Finish(visible,attachedLeft);return visible;
}
bool GlassBeginChild(const char* id,const ImVec2& size,ImGuiChildFlags childFlags,ImGuiWindowFlags flags){
    Background background(ImGuiCol_ChildBg,MainCurrent()&&!(flags&ImGuiWindowFlags_NoBackground)&&ImGui::GetStyleColorVec4(ImGuiCol_ChildBg).w>0);
    const bool visible=ImGui::BeginChild(id,size,childFlags,flags);background.Finish(visible);return visible;
}
bool GlassBeginChild(ImGuiID id,const ImVec2& size,ImGuiChildFlags childFlags,ImGuiWindowFlags flags){
    Background background(ImGuiCol_ChildBg,MainCurrent()&&!(flags&ImGuiWindowFlags_NoBackground)&&ImGui::GetStyleColorVec4(ImGuiCol_ChildBg).w>0);
    const bool visible=ImGui::BeginChild(id,size,childFlags,flags);background.Finish(visible);return visible;
}
bool GlassBeginPopup(const char* id,ImGuiWindowFlags flags){Background b(ImGuiCol_PopupBg,MainCurrent());const bool v=ImGui::BeginPopup(id,flags);b.Finish(v);return v;}
bool GlassBeginPopupModal(const char* id,bool* open,ImGuiWindowFlags flags){Background b(ImGuiCol_PopupBg,MainCurrent());const bool v=ImGui::BeginPopupModal(id,open,flags);b.Finish(v);return v;}
bool GlassBeginPopupContextItem(const char* id,ImGuiPopupFlags flags){Background b(ImGuiCol_PopupBg,MainCurrent());const bool v=ImGui::BeginPopupContextItem(id,flags);b.Finish(v);return v;}
bool GlassBeginPopupContextWindow(const char* id,ImGuiPopupFlags flags){Background b(ImGuiCol_PopupBg,MainCurrent());const bool v=ImGui::BeginPopupContextWindow(id,flags);b.Finish(v);return v;}
bool GlassBeginPopupContextVoid(const char* id,ImGuiPopupFlags flags){Background b(ImGuiCol_PopupBg,MainCurrent());const bool v=ImGui::BeginPopupContextVoid(id,flags);b.Finish(v);return v;}
bool GlassBeginMenu(const char* label,bool enabled){Background b(ImGuiCol_PopupBg,MainCurrent());const bool v=ImGui::BeginMenu(label,enabled);b.Finish(v);return v;}
}
