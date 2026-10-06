#include "Editor/Internal/RawLab/RawLabAreaGraph.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Editor::RawLabInternal {
bool DrawRawLabAreaGraph(RawRecipe::RawZoneArea& area,
    EditorModuleTypes::RawZoneAreaGraphState& state,
    const RawRecipe::RawZoneAreaStatistics* stats, bool maskGestureActive) {
    const bool dragging = maskGestureActive || state.interaction.draggingPoint >= 0 || state.interaction.draggingSegment >= 0;
    const std::size_t maskKey = RawRecipe::ZoneAreaMaskFingerprint(area);
    if (!dragging && stats && stats->valid && stats->maskFingerprint == maskKey &&
        (!state.fittedFullResolution || stats->fullResolution || state.fittedMask != maskKey)) {
        state.minimumEv = stats->minimumEv;
        state.maximumEv = stats->maximumEv;
        if (state.maximumEv - state.minimumEv < 0.1f) { state.minimumEv -= 0.05f; state.maximumEv += 0.05f; }
        state.fittedMask = maskKey;
        state.fittedFullResolution = stats->fullResolution;
    }
    if (!dragging) {
        // Retain the vertical view after an exposure drag so the curve stays
        // where it was moved. Expand only when a gain needs more room.
        for (const auto& p : area.points) {
            state.minimumGain = std::min(state.minimumGain, area.offsetEv + p.deltaEv - 0.25f);
            state.maximumGain = std::max(state.maximumGain, area.offsetEv + p.deltaEv + 0.25f);
        }
    }
    const ImVec2 size(std::max(120.0f, ImGui::GetContentRegionAvail().x), 230.0f);
    ImGui::InvisibleButton("##AreaGainGraph", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImRect plot(ImVec2(origin.x + 34, origin.y + 24), ImVec2(origin.x + size.x - 24, origin.y + size.y - 28));
    auto* draw = ImGui::GetWindowDrawList();
    const float span = state.maximumEv - state.minimumEv, gains = state.maximumGain - state.minimumGain;
    const auto y = [&](float ev) { return plot.Max.y - (ev - state.minimumGain) / gains * plot.GetHeight(); };
    draw->PushClipRect(plot.Min, plot.Max, true);
    if (stats && stats->valid && stats->maskFingerprint == maskKey) {
        float peak = 0;
        for (float count : stats->histogram) peak = std::max(peak, count);
        if (peak > 0) for (int i = 0; i < 256; ++i) {
            const float lo = -32.0f + i * 0.25f;
            const float x0 = plot.Min.x + (lo - state.minimumEv) / span * plot.GetWidth();
            const float x1 = x0 + 0.25f / span * plot.GetWidth();
            const float top = plot.Max.y - std::sqrt(stats->histogram[i] / peak) * plot.GetHeight() * .8f;
            draw->AddRectFilled(ImVec2(x0, top), ImVec2(x1, plot.Max.y), IM_COL32(188, 183, 136, 58));
        }
    }
    for (int i = 0; i <= 4; ++i) {
        const float yy = plot.Min.y + i * plot.GetHeight() / 4;
        draw->AddLine(ImVec2(plot.Min.x, yy), ImVec2(plot.Max.x, yy), IM_COL32(150, 150, 150, 32));
    }
    draw->AddLine(ImVec2(plot.Min.x, y(0)), ImVec2(plot.Max.x, y(0)), IM_COL32(180, 180, 180, 130));
    auto points = RawRecipe::ZoneAreaBezierPoints(area, state.minimumEv, state.maximumEv, state.minimumGain, state.maximumGain);
    // Continue endpoint gain where the fitted mask extends beyond authored points.
    if (!points.empty()) {
        const auto xx = [&](float value) {return plot.Min.x + value * plot.GetWidth();};
        if (points.front().x > 0) draw->AddLine(ImVec2(plot.Min.x, y(area.offsetEv + area.points.front().deltaEv)),
            ImVec2(xx(points.front().x), y(area.offsetEv + area.points.front().deltaEv)), IM_COL32(230,215,132,245),1.65f);
        if (points.back().x < 1) draw->AddLine(ImVec2(xx(points.back().x), y(area.offsetEv + area.points.back().deltaEv)),
            ImVec2(plot.Max.x, y(area.offsetEv + area.points.back().deltaEv)), IM_COL32(230,215,132,245),1.65f);
    }
    const auto segmentBuilder = [&](const std::vector<RawRecipe::RawBezierCurvePoint>& displayed, std::size_t index) {
        auto physical = displayed;
        for (auto& p : physical) {
            p.x = (state.minimumEv + p.x * span + 32.0f) / 64.0f;
            p.y = (state.minimumGain + p.y * gains - area.offsetEv + 32.0f) / 64.0f;
            for (auto* h : {&p.incoming, &p.outgoing}) {h->offsetX *= span / 64; h->offsetY *= gains / 64;}
        }
        auto segment = RawRecipe::BuildRawBezierSegment(physical, index);
        const auto x = [&](float value) { return (value * 64 - 32 - state.minimumEv) / span; };
        const auto yy = [&](float value) { return (value * 64 - 32 + area.offsetEv - state.minimumGain) / gains; };
        segment.left.x = x(segment.left.x); segment.left.y = yy(segment.left.y);
        segment.right.x = x(segment.right.x); segment.right.y = yy(segment.right.y);
        segment.leftHandleX = x(segment.leftHandleX); segment.leftHandleY = yy(segment.leftHandleY);
        segment.rightHandleX = x(segment.rightHandleX); segment.rightHandleY = yy(segment.rightHandleY);
        return segment;
    };
    const bool changed = DrawBezierCurveInteraction(plot, points, state.interaction, state.selectedPoint,
        IM_COL32(230, 215, 132, 245), ImGui::GetColorU32(ImGuiCol_Text), int(RawRecipe::kMaxRawPointCurvePoints), segmentBuilder, false);
    draw->PopClipRect();
    for (int i = 0; i <= 4; ++i) {
        char text[24]; std::snprintf(text, sizeof(text), "%+.1f", state.maximumGain - i * gains / 4);
        draw->AddText(ImVec2(origin.x, plot.Min.y + i * plot.GetHeight() / 4 - 7), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }
    char lo[40], hi[40];
    std::snprintf(lo, sizeof(lo), "%+.2f EV", state.minimumEv);
    std::snprintf(hi, sizeof(hi), "%+.2f EV", state.maximumEv);
    draw->AddText(ImVec2(plot.Min.x, plot.Max.y + 8), ImGui::GetColorU32(ImGuiCol_TextDisabled), lo);
    draw->AddText(ImVec2(plot.Max.x - ImGui::CalcTextSize(hi).x, plot.Max.y + 8), ImGui::GetColorU32(ImGuiCol_TextDisabled), hi);
    if (changed) RawRecipe::StoreZoneAreaBezierPoints(area, points, state.minimumEv, state.maximumEv, state.minimumGain, state.maximumGain);
    if (!stats || stats->maskFingerprint != maskKey) ImGui::TextDisabled("Measuring area brightness...");
    else if (!stats->valid) ImGui::TextDisabled("The area has no painted coverage.");
    else {
        ImGui::TextDisabled(stats->fullResolution ? "Full image range" : "Preview range, full image pending");
        if (stats->hasBlack) ImGui::TextDisabled("Zero luminance appears at the black end.");
    }
    return changed;
}
}
