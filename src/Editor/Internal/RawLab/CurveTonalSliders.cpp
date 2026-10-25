#include "Utils/SidePanelSlider.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Editor::RawLabInternal {
using namespace RawRecipe;

CurveTonalSliderDomain ExposureTonalSliderDomain(float minimumEv, float maximumEv) {
    CurveTonalSliderDomain domain;
    constexpr std::array<float, 5> stops {{-5.f, -2.5f, 0.f, 2.5f, 5.f}};
    const float span = maximumEv - minimumEv;
    for (std::size_t i = 0; i < stops.size(); ++i)
        domain.positions[i] = span > 0.f ? (stops[i] - minimumEv) / span : -1.f;
    return domain;
}

bool DrawCurveTonalSliders(std::vector<RawBezierCurvePoint>& points,
    CurveCanvasState& state, const CurveTonalSliderDomain& domain,
    int maximumPoints, bool disabled) {
    constexpr const char* labels[] {"Blacks", "Shadows", "Midtones", "Highlights", "Whites"};
    const auto style = GraphUI::ResolveGraphPlotStyle();
    const float width = std::max(1.f, ImGui::GetContentRegionAvail().x);
    const float labelWidth = 82.f * style.scale;
    const float readoutWidth = 47.f * style.scale;
    const float trackWidth = std::max(20.f * style.scale, width - labelWidth - readoutWidth);
    const float rowStartX = ImGui::GetCursorPosX();
    bool changed = false;
    const bool editable = !disabled && !(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled);
    if (!editable) {
        state.tonalSliderGesture = false;
        state.activeTonalSlider = 0;
    }
    if (state.tonalSliderGesture && (ImGui::GetIO().AppFocusLost ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        changed = !SameCurvePoints(points, state.beforeTonalSliders);
        if (changed) points = state.beforeTonalSliders;
        state.tonalSliderGesture = false;
        state.activeTonalSlider = 0;
        ImGui::ClearActiveID();
    }

    for (int region = 0; region < 5; ++region) {
        ImGui::PushID(region);
        // Position each row before submitting its real item. A trailing cursor
        // reset after the final slider would extend bounds without an item.
        ImGui::SetCursorPosX(rowStartX);
        const auto owned = std::find_if(points.begin(), points.end(), [region](const auto& point) {
            return point.tonal.region == region;
        });
        const float fixedX = domain.positions[region];
        const bool available = owned != points.end() ||
            (std::isfinite(fixedX) && fixedX > 0.f && fixedX < 1.f);
        float value = 100.f * TonalCurveSliderValue(points, region);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(disabled || !available);
        ImGui::SetCursorScreenPos({origin.x + labelWidth, origin.y});
        ImGui::SetNextItemWidth(trackWidth);
        for (const auto color : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered,
                 ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive})
            ImGui::PushStyleColor(color, ImVec4(0,0,0,0));
        bool edited = ImGuiExtras::SidePanelSliderFloat("##tonal", &value, -100.f, 100.f, "",
            ImGuiSliderFlags_NoInput | ImGuiSliderFlags_NoRoundToFormat);
        ImGui::PopStyleColor(5);
        const ImRect track(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        const bool enabled = !(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled);
        const auto beginEdit = [&] {
            if (!state.tonalSliderGesture) state.beforeTonalSliders = points;
            state.tonalSliderGesture = true;
            state.activeTonalSlider = ImGui::GetItemID();
        };
        if (enabled && ImGui::IsItemActivated()) beginEdit();
        if (enabled && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            value = 0.f;
            edited = true;
        }
        if (hovered) {
            if (!available) ImGui::SetTooltip("%s lies outside this curve's exposure domain.", labels[region]);
            else ImGui::SetTooltip("%s: %+.4g%%. Adjust its fixed curve point vertically. Zero removes the slider point; custom edits are retained. Double-click resets; right-click enters a value.", labels[region], value);
        }

        if (ImGui::IsItemVisible()) {
            auto* draw = ImGui::GetWindowDrawList();
            const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
            const float radius = 4.f * style.scale;
            const float left = track.Min.x + radius + style.scale;
            const float right = std::max(left, track.Max.x - radius - style.scale);
            const float y = track.GetCenter().y;
            const float x = left + (right - left) * (value + 100.f) / 200.f;
            draw->AddText({origin.x, y - ImGui::GetFontSize() * .5f}, ink, labels[region]);
            draw->AddLine({left,y}, {std::max(left,x-radius-2.f*style.scale),y}, ink, style.scale);
            draw->AddLine({std::min(right,x+radius+2.f*style.scale),y}, {right,y}, ink, style.scale);
            draw->AddLine({(left+right)*.5f,y-3.f*style.scale},
                {(left+right)*.5f,y+3.f*style.scale}, style.reference, style.scale);
            draw->AddCircle({x,y}, radius, ink, 16, 1.5f*style.scale);
            char text[24];
            if (value == 0.f) std::snprintf(text, sizeof(text), "0");
            else std::snprintf(text, sizeof(text), std::abs(value) < .05f ? "%+.2g" : "%+.1f", value);
            const float textWidth = ImGui::CalcTextSize(text).x;
            draw->AddText({origin.x+width-textWidth,y-ImGui::GetFontSize()*.5f},ink,text);
        }
        if (ImGui::BeginPopupContextItem("##tonal-value")) {
            ImGui::TextUnformatted(labels[region]);
            ImGui::SetNextItemWidth(160.f * style.scale);
            if (ImGui::InputFloat("##exact", &value, 0.f, 0.f, "%.3f"))
                edited = std::isfinite(value);
            if (ImGui::IsItemActivated()) beginEdit();
            if (ImGui::Button("Reset")) { value = 0.f; edited = true; }
            ImGui::EndPopup();
        }
        if (edited && enabled) {
            const auto result = SetTonalCurveSlider(points, region, fixedX,
                std::clamp(value * .01f, -1.f, 1.f), std::size_t(maximumPoints));
            changed |= result == TonalCurveEditResult::Changed;
            if (result == TonalCurveEditResult::Changed) state.selected.assign(points.size(), false);
            if (result == TonalCurveEditResult::CapacityReached)
                ImGui::SetTooltip("The curve has reached its point limit. Remove a custom point before adding this tonal point.");
            else if (result == TonalCurveEditResult::Conflict)
                ImGui::SetTooltip("A nearby point prevents adding this tonal point. Move that custom point slightly and try again.");
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (state.tonalSliderGesture && ImGui::GetActiveID() != state.activeTonalSlider) {
        state.tonalSliderGesture = false;
        state.activeTonalSlider = 0;
    }
    return changed;
}
}
