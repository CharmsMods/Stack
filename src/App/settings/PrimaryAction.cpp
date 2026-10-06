#include "PrimaryAction.h"
#include <imgui_internal.h>
namespace StackAppearance {
ScopedPrimaryActionStyle::ScopedPrimaryActionStyle(const SurfaceColors& colors) {
    const bool disabled=(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled)!=0;
    disabled_=disabled;
    // The palette supplies disabled colors. Avoid applying a second fade to
    // both the cream background and its dark foreground.
    if (disabled_) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetCurrentContext()->DisabledAlphaBackup);
    ImGui::PushStyleColor(ImGuiCol_Text,disabled ? colors.disabledForeground : colors.foreground);
    ImGui::PushStyleColor(ImGuiCol_Button,disabled ? colors.disabledBackground : colors.background);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,colors.hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,colors.active);
    ImGui::PushStyleColor(ImGuiCol_Border,colors.border);
    ImGui::PushStyleColor(ImGuiCol_NavCursor,colors.focus);
}
ScopedPrimaryActionStyle::~ScopedPrimaryActionStyle() { ImGui::PopStyleColor(6); if (disabled_) ImGui::PopStyleVar(); }
bool PrimaryActionButton(const char* label,const ImVec2& size,const SurfaceColors* colors) {
    static const auto fallback=ResolveCreamPalette(CreamPalette{});
    ScopedPrimaryActionStyle scope(colors ? *colors : fallback.primaryAction);
    return ImGui::Button(label,size);
}
}
