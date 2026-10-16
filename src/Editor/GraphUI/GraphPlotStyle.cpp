#include "Editor/GraphUI/GraphPlotStyle.h"

#include <algorithm>
#include <cmath>

namespace Stack::Editor::GraphUI {

GraphPlotStyle ResolveGraphPlotStyle(float scale) {
    if (scale <= 0.0f)
        scale = ImGui::GetFontSize() / std::max(1.0f, ImGui::GetStyle().FontSizeBase);
    scale = std::max(0.01f, scale);
    return {
        ImGui::GetColorU32(ImGuiCol_Text),
        ImGui::GetColorU32(ImGuiCol_Text),
        ImGui::GetColorU32(ImGuiCol_SliderGrab),
        ImGui::GetColorU32(ImGuiCol_SliderGrabActive),
        ImGui::GetColorU32(ImGuiCol_Text, .23f),
        ImGui::GetColorU32(ImGuiCol_SliderGrabActive, .45f),
        ImGui::GetColorU32(ImGuiCol_Text, .10f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled, .72f),
        ImGui::GetColorU32(ImGuiCol_Text),
        0, 0, // The shared default remains frameless, without a grid or fill.
        {ImGui::GetColorU32(ImVec4(1.f,.34f,.30f,1.f)),
         ImGui::GetColorU32(ImVec4(.34f,.94f,.48f,1.f)),
         ImGui::GetColorU32(ImVec4(.38f,.62f,1.f,1.f))},
        {ImGui::GetColorU32(ImVec4(.96f,.18f,.14f,.050f)),
         ImGui::GetColorU32(ImVec4(.18f,.88f,.32f,.045f)),
         ImGui::GetColorU32(ImVec4(.16f,.42f,1.f,.055f))},
        {ImGui::GetColorU32(ImVec4(.10f,.88f,.90f,.042f)),
         ImGui::GetColorU32(ImVec4(.92f,.16f,.82f,.038f)),
         ImGui::GetColorU32(ImVec4(.96f,.82f,.12f,.045f))},
        {ImVec4(.72f,.78f,.80f,.14f), ImVec4(.92f,.18f,.16f,.10f),
         ImVec4(.18f,.82f,.36f,.09f), ImVec4(.18f,.46f,.98f,.10f)},
        {ImVec4(.82f,.88f,.90f,.34f), ImVec4(.96f,.28f,.24f,.34f),
         ImVec4(.26f,.90f,.44f,.32f), ImVec4(.28f,.58f,1.f,.34f)},
        scale, 6.0f * scale, 1.7f * scale, 2.35f * scale, 2.8f * scale,
        3.8f * scale, 5.2f * scale, 23.0f * scale, 4.0f * scale
    };
}

void DrawGraphBackdrop(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    const GraphPlotStyle& style) {
    if (style.background & IM_COL32_A_MASK)
        draw.AddRectFilled(minimum, maximum, style.background);
    if (style.grid & IM_COL32_A_MASK) {
        for (int i = 1; i < 4; ++i) {
            const float x = minimum.x + (maximum.x - minimum.x) * i / 4.0f;
            const float y = minimum.y + (maximum.y - minimum.y) * i / 4.0f;
            draw.AddLine({x, minimum.y}, {x, maximum.y}, style.grid, style.scale);
            draw.AddLine({minimum.x, y}, {maximum.x, y}, style.grid, style.scale);
        }
    }
}

void DrawGraphReference(ImDrawList& draw, ImVec2 start, ImVec2 end,
    const GraphPlotStyle& style) {
    draw.AddLine(start, end, style.reference, style.scale);
}

void DrawGraphChannelWash(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    int channel, const GraphPlotStyle& style) {
    if (channel < 0 || channel >= 3) return;
    draw.AddTriangleFilled(minimum, {maximum.x, minimum.y}, {minimum.x, maximum.y}, style.channelWash[channel]);
    draw.AddTriangleFilled({maximum.x, minimum.y}, maximum, {minimum.x, maximum.y}, style.complementWash[channel]);
}

void DrawGraphBezier(ImDrawList& draw, ImVec2 start, ImVec2 outgoing,
    ImVec2 incoming, ImVec2 end, ImU32 color, const GraphPlotStyle& style,
    bool hovered, bool selected) {
    draw.AddBezierCubic(start, outgoing, incoming, end, color,
        selected ? style.selectedWidth : hovered ? style.hoveredWidth : style.curveWidth);
}

void DrawGraphPoint(ImDrawList& draw, ImVec2 point, const GraphPlotStyle& style,
    bool selected, bool filled) {
    const float radius = selected ? style.selectedPointRadius : style.pointRadius;
    const ImU32 color = selected ? style.selected : style.point;
    if (filled) draw.AddCircleFilled(point, radius, color, 24);
    else draw.AddCircle(point, radius, color, 24, (selected ? 2.0f : 1.3f) * style.scale);
}

void DrawGraphProbe(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    float x, const GraphPlotStyle& style) {
    draw.AddLine({x, minimum.y}, {x, maximum.y}, style.guide, style.scale);
}

void DrawGraphPointHalo(ImDrawList& draw, ImVec2 point, float pointerDistance,
    const GraphPlotStyle& style) {
    const float reach = 30.0f * style.scale;
    if (pointerDistance >= reach) return;
    for (int ring = 0; ring < 4; ++ring) {
        auto color = ImGui::ColorConvertU32ToFloat4(style.point);
        color.w *= (1.0f - pointerDistance / reach) * .22f / (ring + 1);
        draw.AddCircle(point, (6.0f + ring * 2) * style.scale,
            ImGui::ColorConvertFloat4ToU32(color), 24, 2.0f * style.scale);
    }
}

void DrawGraphDial(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    float value, bool enabled, const GraphPlotStyle& style) {
    const float width = std::max(1.0f, maximum.x - minimum.x);
    const float centerX = (minimum.x + maximum.x) * .5f;
    const float centerY = (minimum.y + maximum.y) * .5f;
    const float spacing = 5.0f * style.scale;
    draw.PushClipRect(minimum, maximum, true);
    for (float x = minimum.x - spacing + std::fmod(value * 60.0f * style.scale, spacing);
         x < maximum.x + spacing; x += spacing) {
        const float fade = std::clamp(std::min(x - minimum.x, maximum.x - x) / (12.0f * style.scale), 0.0f, 1.0f);
        const float height = (5.0f + 3.0f * (1.0f - std::abs(x - centerX) / (width * .5f))) * style.scale;
        auto color = ImGui::ColorConvertU32ToFloat4(style.dial);
        color.w *= fade * (enabled ? .85f : .2f);
        draw.AddLine({x, centerY - height}, {x, centerY + height},
            ImGui::ColorConvertFloat4ToU32(color), 2.0f * style.scale);
    }
    draw.PopClipRect();
}

} // namespace Stack::Editor::GraphUI
