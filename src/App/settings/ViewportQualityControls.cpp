#include "App/settings/ViewportQualityControls.h"
#include "Editor/EditorModule.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Stack::Settings {
namespace {
ImU32 QualityColor(const EditorRendering::ViewportQualitySample& sample) {
    if (!sample.known) return IM_COL32(76,80,87,255);
    if (sample.targetLimited || sample.displayDetailPercent < 50) return IM_COL32(145,77,37,255);
    if (sample.displayDetailPercent < 99.5) return IM_COL32(139,113,42,255);
    return IM_COL32(45,119,83,255);
}
void DescribeQuality(const EditorRendering::ViewportQualitySample& sample) {
    if (!sample.known) {
        ImGui::TextUnformatted("No current timing evidence. Adjust an image control to learn this workload.");
        return;
    }
    ImGui::Text("Estimated editing display sampling: %.0f%%",sample.displayDetailPercent);
    if (sample.predictedMs > 0)
        ImGui::Text("Estimated processing: %.1f ms (%.1f updates/s capacity)",
            sample.predictedMs,1000.0/sample.predictedMs);
    if (sample.targetLimited) ImGui::TextUnformatted("Target exceeds the estimated processing capacity at the allowed detail.");
    if (sample.provisional) ImGui::TextUnformatted("Includes estimates beyond directly measured sizes.");
}
}
void RenderViewportQualityControls(EditorModule& editor, float width) {
    int target = editor.GetRawViewportRequestedFps();
    const int minimum = Raw::kMinimumViewportTargetFps;
    const int maximum = std::max({240,editor.GetRawViewportMaximumFps(),target});
    const auto forecast = editor.GetViewportQualityBar(maximum);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    // Mirror the standard integer slider's positive logarithmic usable track.
    // The normal SliderInt still owns input, keyboard navigation and its handle.
    const float padding = 2.f;
    const float sliderSize = std::max(0.f,width-padding*2);
    const float grab = std::min(sliderSize,std::max(ImGui::GetStyle().GrabMinSize,
        sliderSize/float(double(maximum)-minimum+1)));
    const float left = start.x+padding+grab*.5f;
    const float right = start.x+width-padding-grab*.5f;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start,ImVec2(start.x+width,start.y+height),ImGui::GetColorU32(ImGuiCol_FrameBg),
        ImGui::GetStyle().FrameRounding);
    for (std::size_t i=0; i<forecast.samples.size(); ++i) {
        const float x0 = left+(right-left)*float(i)/forecast.samples.size();
        const float x1 = left+(right-left)*float(i+1)/forecast.samples.size();
        draw->AddRectFilled(ImVec2(x0,start.y+padding),ImVec2(x1,start.y+height-padding),QualityColor(forecast.samples[i]));
    }
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,ImVec4(.9f,.92f,.95f,1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,ImVec4(1,1,1,1));
    const bool changed = ImGui::SliderInt("Target viewport FPS",&target,minimum,maximum,"%d FPS",
        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    const bool finished = ImGui::IsItemDeactivatedAfterEdit();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(5);
    if (changed) editor.SetRawViewportTargetFps(target);
    if (finished) editor.FinishRawViewportTargetFpsEdit();
    const auto mouse = ImGui::GetIO().MousePos;
    if (hovered && mouse.x >= left && mouse.x <= right && right>left) {
        const int index = std::clamp(int((mouse.x-left)/(right-left)*forecast.samples.size()),0,
            int(forecast.samples.size())-1);
        const auto& sample = forecast.samples[index];
        ImGui::BeginTooltip();
        if (sample.requestedFps > 0) ImGui::Text("Around %d target FPS",sample.requestedFps);
        if (sample.requestedFps > sample.effectiveFps && sample.effectiveFps > 0)
            ImGui::Text("Display limit uses %d FPS",sample.effectiveFps);
        DescribeQuality(sample);
        ImGui::EndTooltip();
    }
    ImGui::TextWrapped("Green: full display detail. Amber: reduced detail. Orange: large reduction or target too fast. Gray: unknown.");
    ImGui::TextDisabled("Display limit: %d FPS",editor.GetRawViewportMaximumFps());
    if (target > editor.GetRawViewportMaximumFps())
        ImGui::TextDisabled("Requested %d FPS; using %d FPS on this display",target,editor.GetRawViewportTargetFps());
    // This is a forecast for active editing, not the currently displayed frame.
    if (!changed && forecast.current.known) DescribeQuality(forecast.current);
    else ImGui::TextDisabled("Colors estimate the last learned editing workload at this zoom.");

    auto preferences = editor.GetRawViewportPreferences();
    // Preserve the existing storage semantics: disabled TargetFps maps to Auto,
    // even though its saved (inactive) minimum percentage commonly defaults to100.
    int detail = preferences.mode == Raw::ViewportInteractionMode::PreserveDetail ? preferences.minimumDetailPercent : 24;
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderInt("Minimum editing detail",&detail,24,100,detail==24 ? "Auto" : "%d%%",
            ImGuiSliderFlags_AlwaysClamp)) {
        preferences.mode = detail==24 ? Raw::ViewportInteractionMode::TargetFps : Raw::ViewportInteractionMode::PreserveDetail;
        if (detail>=25) preferences.minimumDetailPercent = detail;
        editor.SetRawViewportPreferences(preferences);
    }
    ImGui::TextWrapped("Auto favors the FPS target. A detail floor can lower the achieved update rate. 100%% matches physical display detail at this zoom, up to native source detail; it is not a whole-image quality percentage.");
}
}
