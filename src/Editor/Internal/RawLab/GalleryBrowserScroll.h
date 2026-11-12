#pragma once
#include <imgui_internal.h>
#include <algorithm>

namespace Stack::Editor::RawLabInternal {
// Draw after content. Neither the edge fades nor the thumb participates in layout.
inline void RenderGalleryBrowserScroll(float unit) {
    auto* window=ImGui::GetCurrentWindow();
    if(window->SkipItems || window->ScrollMax.y<=0)return;
    const auto bounds=window->InnerRect;
    auto* storage=ImGui::GetStateStorage();
    const auto scrollKey=window->GetID("BrowserLastScroll"),timeKey=window->GetID("BrowserScrollTime");
    const float now=static_cast<float>(ImGui::GetTime());
    if(storage->GetFloat(scrollKey,-1)!=window->Scroll.y) {
        storage->SetFloat(scrollKey,window->Scroll.y);storage->SetFloat(timeKey,now);
    }
    auto color=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImU32 opaque=ImGui::GetColorU32(color);
    color.w=0;const ImU32 clear=ImGui::GetColorU32(color);
    const float fade=std::min(16*unit,bounds.GetHeight()*.1f);
    if(window->Scroll.y>0)window->DrawList->AddRectFilledMultiColor(bounds.Min,
        {bounds.Max.x,bounds.Min.y+fade},opaque,opaque,clear,clear);
    if(window->Scroll.y<window->ScrollMax.y)window->DrawList->AddRectFilledMultiColor(
        {bounds.Min.x,bounds.Max.y-fade},bounds.Max,clear,clear,opaque,opaque);
    const ImGuiID id=window->GetID("BrowserOverlayScrollbar");
    const ImRect rail({bounds.Max.x-10*unit,bounds.Min.y+6*unit},{bounds.Max.x-2*unit,bounds.Max.y-6*unit});
    const bool hover=ImGui::IsWindowHovered() && rail.Contains(ImGui::GetIO().MousePos);
    const bool dragging=GImGui->ActiveId==id;
    const float alpha=hover || dragging?1.f:std::clamp((storage->GetFloat(timeKey)+.85f-now)/.25f,0.f,1.f);
    if(alpha<=0)return;
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,{0,0,0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding,5*unit);
    ImS64 scroll=static_cast<ImS64>(window->Scroll.y);
    const ImS64 visible=static_cast<ImS64>(bounds.GetHeight());
    ImGui::ScrollbarEx(rail,id,ImGuiAxis_Y,&scroll,visible,visible+static_cast<ImS64>(window->ScrollMax.y),ImDrawFlags_RoundCornersAll);
    if(scroll!=static_cast<ImS64>(window->Scroll.y))ImGui::SetScrollY(static_cast<float>(scroll));
    ImGui::PopStyleVar(2);ImGui::PopStyleColor();
}
}
