#include "Editor/NodeEditors/NodeEditorPanelPresentation.h"
#include "Notifications/NotificationPresenter.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
void DrawControlsPanelScrollbar(ImGuiWindow* body,float seamX) {
    auto* host=ImGui::GetCurrentWindow();
    const auto id=host->GetID("ControlsOverlayScroll");
    auto& storage=body->StateStorage;
    const auto positionId=ImHashStr("ControlsScrollPosition");
    const auto timeId=ImHashStr("ControlsScrollTime");
    const float now=static_cast<float>(ImGui::GetTime());
    const bool moved=storage.GetFloat(positionId,body->Scroll.y)!=body->Scroll.y;
    if (moved || ImGui::GetActiveID()==id) storage.SetFloat(timeId,now);
    storage.SetFloat(positionId,body->Scroll.y);
    if (body->ScrollMax.y<=0) return;
    const float age=now-storage.GetFloat(timeId,-100.f);
    if (age>=.43f && ImGui::GetActiveID()!=id) return;
    const bool reduced=Stack::Notifications::SystemReducedMotion();
    const float opacity=reduced ? (age<.35f ? 1.f : 0.f) :
        std::clamp(1.f-(age-.25f)/.18f,0.f,1.f);
    if (opacity<=0 && ImGui::GetActiveID()!=id) return;
    const float scale=ImGui::GetFontSize()/13.f;
    const ImRect rail({seamX-2.f*scale,body->InnerRect.Min.y},
        {seamX+1.f*scale,body->InnerRect.Max.y});
    const auto oldClip=host->ClipRect;
    host->ClipRect.Max.x=std::max(host->ClipRect.Max.x,rail.Max.x);
    host->DrawList->PushClipRect({rail.Min.x-5.f*scale,rail.Min.y},{rail.Max.x+5.f*scale,rail.Max.y},false);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarPadding,0.f);
    const auto touchPadding=ImGui::GetStyle().TouchExtraPadding;
    ImGui::GetStyle().TouchExtraPadding=ImVec2(4.f*scale,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding,3.f*scale);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,ImVec4(0,0,0,0));
    const auto ink=ImGui::GetStyleColorVec4(ImGuiCol_Text);
    for (const auto color:{ImGuiCol_ScrollbarGrab,ImGuiCol_ScrollbarGrabHovered,ImGuiCol_ScrollbarGrabActive})
        ImGui::PushStyleColor(color,ImVec4(ink.x,ink.y,ink.z,ink.w*opacity*.42f));
    const auto visible=static_cast<ImS64>(std::ceil(body->InnerRect.GetHeight()));
    ImS64 scroll=static_cast<ImS64>(body->Scroll.y);
    ImGui::ScrollbarEx(rail,id,ImGuiAxis_Y,&scroll,visible,
        visible+static_cast<ImS64>(std::ceil(body->ScrollMax.y)),ImDrawFlags_RoundCornersAll);
    if (static_cast<ImS64>(body->Scroll.y)!=scroll) {
        ImGui::SetScrollY(body,static_cast<float>(scroll));
        storage.SetFloat(timeId,now);
    }
    ImGui::PopStyleColor(4);ImGui::PopStyleVar(2);
    ImGui::GetStyle().TouchExtraPadding=touchPadding;
    host->DrawList->PopClipRect();host->ClipRect=oldClip;
}
}
