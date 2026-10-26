#pragma once

#include <imgui.h>

namespace Stack::Animation { struct ControlInput; }

namespace Stack::Editor::GraphUI {

// Presentation only: callers supply plot geometry and own data and gestures.
// Resolve once per drawing call so the current application theme and scale apply.
struct GraphPlotStyle {
    ImU32 curve, point, hovered, selected, reference, guide, histogram, label, dial;
    ImU32 background, grid;
    ImU32 channel[3];
    ImU32 channelWash[3], complementWash[3];
    ImVec4 histogramFill[4], histogramLine[4];
    float scale, inset, curveWidth, hoveredWidth, selectedWidth;
    float pointRadius, selectedPointRadius, controlHeight, controlGap;
    bool liquidPoints=false;
};

GraphPlotStyle ResolveGraphPlotStyle(float scale = 0.0f);
void DrawGraphBackdrop(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    const GraphPlotStyle& style);
void DrawGraphChannelWash(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    int channel, const GraphPlotStyle& style);
void DrawGraphReference(ImDrawList& draw, ImVec2 start, ImVec2 end,
    const GraphPlotStyle& style);
void DrawGraphBezier(ImDrawList& draw, ImVec2 start, ImVec2 outgoing,
    ImVec2 incoming, ImVec2 end, ImU32 color, const GraphPlotStyle& style,
    bool hovered = false, bool selected = false);
void DrawGraphPoint(ImDrawList& draw, ImVec2 point, const GraphPlotStyle& style,
    bool selected = false, bool filled = false, ImGuiID identity = 0,
    bool hovered = false, bool held = false, const Animation::ControlInput* nativeInput = nullptr);
void DrawGraphPointHalo(ImDrawList& draw, ImVec2 point, float pointerDistance,
    const GraphPlotStyle& style);
void DrawGraphProbe(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    float x, const GraphPlotStyle& style);
void DrawGraphDial(ImDrawList& draw, ImVec2 minimum, ImVec2 maximum,
    float value, bool enabled, const GraphPlotStyle& style);

} // namespace Stack::Editor::GraphUI
