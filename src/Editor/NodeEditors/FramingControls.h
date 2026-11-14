#pragma once

#include "NodeMath/GeometryMath.h"
#include "Utils/ImGuiExtras.h"
#include <algorithm>

namespace Stack::NodeEditors {

// Shared by Graph's controls and RAW's registered fallback editor. Authored
// values stay in the node draft; the host owns commit and undo.
inline bool DrawFramingControls(NodeMath::ReformatSettings& settings, float width) {
    bool changed = false;
    const char* ratios[] = {"Original", "16:9", "3:2", "4:3", "Square", "Portrait 9:16", "Custom ratio"};
    int ratio = std::clamp(settings.canvasRatio - 1, 0, 6);
    ImGui::SetNextItemWidth(width);
    if (ImGuiExtras::NodeCombo("Canvas", "##FramingRatio", &ratio, ratios, 7, width)) {
        settings.canvasRatio = ratio + 1;
        changed = true;
    }
    if (settings.canvasRatio == 7) {
        ImGui::SetNextItemWidth(width);
        changed |= ImGuiExtras::NodeInputFloat("Canvas aspect", "##FramingCustom", &settings.customCanvasAspect, 0.01f, 0.1f, "%.4f", width);
        settings.customCanvasAspect = std::clamp(settings.customCanvasAspect, 0.01f, 100.0f);
    }
    if (settings.canvasRatio == 0) {
        ImGui::TextDisabled("Saved custom size: %d x %d", settings.width, settings.height);
        if (ImGui::Button("Use native-resolution framing")) {
            settings.customCanvasAspect = float(settings.width) / std::max(1, settings.height);
            settings.canvasRatio = 7;
            changed = true;
        }
    } else ImGui::TextDisabled("Native-resolution crop; no output resizing");
    ImGui::SetNextItemWidth(width);
    changed |= ImGuiExtras::NodeInputFloat("Visible aspect", "##FramingAspect", &settings.visibleAspect, 0.01f, 0.1f, "%.3f", width);
    settings.visibleAspect = std::clamp(settings.visibleAspect, 0.0f, 100.0f);
    ImGui::TextDisabled("0 = no aspect bars; cinema = 2.39");
    changed |= ImGuiExtras::NodeSliderFloat("Side bars", "##FramingSideBars", &settings.sideBars, 0.0f, 0.45f, "%.3f");
    changed |= ImGuiExtras::NodeSliderFloat("Top/bottom bars", "##FramingTopBars", &settings.topBottomBars, 0.0f, 0.45f, "%.3f");
    changed |= ImGuiExtras::NodeSliderFloat("Crop zoom", "##FramingScale", &settings.sourceScale, 1.0f, 8.0f, "%.2f x");
    changed |= ImGuiExtras::NodeSliderFloat("Source X", "##FramingX", &settings.sourceX, -1.0f, 1.0f, "%.3f");
    changed |= ImGuiExtras::NodeSliderFloat("Source Y", "##FramingY", &settings.sourceY, -1.0f, 1.0f, "%.3f");
    ImGui::TextDisabled("Translation is bounded to covered source pixels");
    return changed;
}

} // namespace Stack::NodeEditors
