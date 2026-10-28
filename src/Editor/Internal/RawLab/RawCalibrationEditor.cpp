#include "Editor/Internal/RawLab/RawCalibrationEditor.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Stack::Editor::RawLabInternal {
bool DrawCalibrationEditor(RawRecipe::RawColorCalibrationRecipe& recipe,
    CalibrationEditorState& state, const ImVec2& size,
    const std::array<bool, 3>& hueDriven, const std::array<bool, 3>& saturationDriven, bool fixedFit) {
    using namespace RawRecipe;
    auto& io = ImGui::GetIO();
    ImGui::InvisibleButton("##CalibrationPrimaries", size);
    const bool enabled = recipe.enabled && !(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled);
    const bool hovered = ImGui::IsItemHovered();
    const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImVec2 center = bounds.GetCenter();
    const float screenRadius = std::max(1.f, std::min(size.x, size.y) * .5f - 14.f);
    auto primaries = ColorCalibrationPrimaries(recipe);
    // Fit externally changed or re-opened values without changing the recipe.
    if (state.dragging < 0) {
        if (fixedFit) state.radius=.65;
        for (const auto& p : primaries) {
            const double extent = std::hypot(p[0]-kCalibrationWhite[0], p[1]-kCalibrationWhite[1]);
            if (std::isfinite(extent) && extent > state.radius) state.radius = extent * 1.12;
        }
    }
    if (!fixedFit && hovered && state.dragging < 0 && io.MouseWheel != 0 && !io.KeyAlt) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        const double radius = state.radius * std::exp(-double(io.MouseWheel) * .15);
        if (std::isfinite(radius) && radius > std::numeric_limits<double>::min()) state.radius = radius;
    }
    const double scale = screenRadius / state.radius;
    const auto position = [&](const std::array<double,2>& xy) {
        return ImVec2(center.x + float((xy[0]-kCalibrationWhite[0])*scale),
            center.y - float((xy[1]-kCalibrationWhite[1])*scale));
    };
    int hot = -1;
    float closest = 14.f * 14.f;
    for (int i=0; i<3; ++i) {
        const auto p = position(primaries[i]);
        const float dx = io.MousePos.x-p.x, dy = io.MousePos.y-p.y;
        const float distance = dx*dx+dy*dy;
        if (distance < closest) { closest = distance; hot = i; }
    }
    bool changed = false;
    if (enabled && hovered && hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state.selected = hot;
        if (!hueDriven[hot] || !saturationDriven[hot]) {
            state.gestureStart = recipe;
            state.dragging = hot;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (!hueDriven[hot]) recipe.primaries[hot].hue = 0.f;
                if (!saturationDriven[hot]) recipe.primaries[hot].saturation = 0.f;
                state.dragging = -1;
                ImGui::ClearActiveID();
                changed = true;
            }
        }
    }
    if (state.dragging >= 0) {
        if (!enabled || io.AppFocusLost || (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            recipe = state.gestureStart;
            state.dragging = -1;
            ImGui::ClearActiveID();
            changed = true;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsItemActive()) {
            const int i = state.dragging;
            auto p = position(primaries[i]);
            // Keep the handle inside the tool. Zooming changes the amount of
            // mathematical space visible, never the operation's values.
            const float fine = io.KeyShift ? .1f : 1.f;
            if (!ImGui::IsItemActivated()) {
                p.x = std::clamp(p.x + io.MouseDelta.x*fine, bounds.Min.x+12.f, bounds.Max.x-12.f);
                p.y = std::clamp(p.y + io.MouseDelta.y*fine, bounds.Min.y+12.f, bounds.Max.y-12.f);
                const auto controls = ColorCalibrationControlForPosition(i,
                    {kCalibrationWhite[0]+(p.x-center.x)/scale, kCalibrationWhite[1]-(p.y-center.y)/scale}, recipe.primaries[i]);
                auto& primary = recipe.primaries[i];
                if (!hueDriven[i] && primary.hue != controls.hue) { primary.hue = controls.hue; changed = true; }
                if (!saturationDriven[i] && primary.saturation != controls.saturation) { primary.saturation = controls.saturation; changed = true; }
            }
        } else state.dragging = -1;
    }
    primaries = ColorCalibrationPrimaries(recipe);
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 colors[] = {IM_COL32(240,108,104,255), IM_COL32(112,209,143,255), IM_COL32(108,162,250,255)};
    const ImU32 guide = ImGui::GetColorU32(ImGuiCol_TextDisabled, .35f);
    draw->PushClipRect(bounds.Min,bounds.Max,true);
    for (int i=0;i<3;++i) {
        const auto original = position(kCalibrationPrimaries[i]);
        const auto p = position(primaries[i]);
        const auto next = position(primaries[(i+1)%3]);
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        draw->AddLine(original,position(kCalibrationPrimaries[(i+1)%3]),guide,1.f);
        if (std::isfinite(next.x) && std::isfinite(next.y)) draw->AddLine(p,next,colors[i],1.6f);
        draw->AddCircle(original,4.f,guide,20,1.f);
        draw->AddLine(center,p,guide,1.f);
        draw->AddCircleFilled(p,5.f,colors[i]);
        if ((enabled && hot==i) || state.dragging==i) draw->AddCircle(p,8.f,colors[i],24,1.4f);
        if (hueDriven[i] && saturationDriven[i]) draw->AddCircle(p,10.f,guide,24,1.f);
    }
    draw->AddCircleFilled(center,2.5f,ImGui::GetColorU32(ImGuiCol_TextDisabled));
    draw->PopClipRect();
    if (hovered && hot>=0 && state.dragging<0) {
        const char* labels[] = {"Red", "Green", "Blue"};
        const auto& primary = recipe.primaries[hot];
        ImGui::SetTooltip("%s   %.2f\xC2\xB0   \xC3\x97%.3g\nDrag to edit. Shift: fine. Double-click: reset.%s",
            labels[hot], primary.hue*.2f, std::exp2(double(primary.saturation)/100.),
            hueDriven[hot] || saturationDriven[hot] ? "\nConnected parameters are controlled by Graph." : "");
    }
    return changed;
}
} // namespace Stack::Editor::RawLabInternal
