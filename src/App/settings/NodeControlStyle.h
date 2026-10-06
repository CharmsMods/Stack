#pragma once
#include "CreamPalette.h"

namespace StackAppearance {
// Confine light node controls, including their owned dropdowns, to this scope.
class ScopedNodeControlStyle {
public:
    explicit ScopedNodeControlStyle(const NodeAppearance& n) {
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,0.8f);
        auto push=[&](ImGuiCol slot,ImVec4 color) { ImGui::PushStyleColor(slot,color); ++count_; };
        push(ImGuiCol_Text,n.text); push(ImGuiCol_TextDisabled,n.mutedText);
        push(ImGuiCol_Border,n.control.border);
        push(ImGuiCol_ChildBg,n.surface); push(ImGuiCol_PopupBg,n.control.background);
        for (auto slot : {ImGuiCol_FrameBg,ImGuiCol_Button,ImGuiCol_Header}) push(slot,n.control.background);
        for (auto slot : {ImGuiCol_FrameBgHovered,ImGuiCol_ButtonHovered,ImGuiCol_HeaderHovered}) push(slot,n.control.hovered);
        for (auto slot : {ImGuiCol_FrameBgActive,ImGuiCol_ButtonActive,ImGuiCol_HeaderActive}) push(slot,n.control.active);
        push(ImGuiCol_SliderGrab,n.number); push(ImGuiCol_SliderGrabActive,n.numberActive);
        push(ImGuiCol_CheckMark,n.number); push(ImGuiCol_NavCursor,n.focus);
        push(ImGuiCol_TextSelectedBg,n.control.active);
        push(ImGuiCol_Separator,n.control.border);
        push(ImGuiCol_SeparatorHovered,n.focus); push(ImGuiCol_SeparatorActive,n.focus);
    }
    ~ScopedNodeControlStyle() { End(); }
    void End() { if (count_) { ImGui::PopStyleColor(count_); ImGui::PopStyleVar(); count_=0; } }
    ScopedNodeControlStyle(const ScopedNodeControlStyle&)=delete;
    ScopedNodeControlStyle& operator=(const ScopedNodeControlStyle&)=delete;
private:
    int count_=0;
};
}
