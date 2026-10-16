#pragma once

#include "NodeMath/GeometryMath.h"
#include "Utils/ImGuiExtras.h"
#include <algorithm>

namespace Stack::NodeEditors {

// Shared by Graph's controls and RAW's registered fallback editor. Authored
// values stay in the node draft; the host owns commit and undo.
inline bool DrawFramingControls(NodeMath::ReformatSettings& settings, float width) {
    bool changed = false;
    const char* ratios[] = {"Custom dimensions", "Original", "16:9", "3:2", "4:3", "Square", "Portrait 9:16", "Custom ratio"};
    ImGui::SetNextItemWidth(width);
    changed |= ImGuiExtras::NodeCombo("Canvas", "##FramingRatio", &settings.canvasRatio, ratios, 8, width);
    if (settings.canvasRatio == 7) {
        ImGui::SetNextItemWidth(width);
        changed |= ImGuiExtras::NodeInputFloat("Canvas aspect", "##FramingCustom", &settings.customCanvasAspect, 0.01f, 0.1f, "%.4f", width);
        settings.customCanvasAspect = std::clamp(settings.customCanvasAspect, 0.01f, 100.0f);
    }
    ImGui::SetNextItemWidth(width);
    changed |= ImGuiExtras::NodeInputInt("Width", "##FramingWidth", &settings.width, 1, 128, width);
    if (settings.canvasRatio == 0) {
        ImGui::SetNextItemWidth(width);
        changed |= ImGuiExtras::NodeInputInt("Height", "##FramingHeight", &settings.height, 1, 128, width);
    } else if (settings.canvasRatio != 1) {
        const auto canvas = NodeMath::ResolveFramingCanvas(settings, 1, 1);
        ImGui::Text("Output: %d x %d", canvas.width, canvas.height);
    } else ImGui::TextDisabled("Height follows incoming source aspect");
    settings.width = std::clamp(settings.width, 1, NodeMath::kMaximumReformatDimension);
    settings.height = std::clamp(settings.height, 1, NodeMath::kMaximumReformatDimension);
    ImGui::SetNextItemWidth(width);
    changed |= ImGuiExtras::NodeInputFloat("Visible aspect", "##FramingAspect", &settings.visibleAspect, 0.01f, 0.1f, "%.3f", width);
    settings.visibleAspect = std::clamp(settings.visibleAspect, 0.0f, 100.0f);
    ImGui::TextDisabled("0 = no aspect bars; cinema = 2.39");
    changed |= ImGuiExtras::NodeSliderFloat("Side bars", "##FramingSideBars", &settings.sideBars, 0.0f, 0.45f, "%.3f");
    changed |= ImGuiExtras::NodeSliderFloat("Top/bottom bars", "##FramingTopBars", &settings.topBottomBars, 0.0f, 0.45f, "%.3f");
    changed |= ImGuiExtras::NodeSliderFloat("Source scale", "##FramingScale", &settings.sourceScale, 1.0f, 8.0f, "%.2f x");
    changed |= ImGuiExtras::NodeSliderFloat("Source X", "##FramingX", &settings.sourceX, -1.0f, 1.0f, "%.3f");
    changed |= ImGuiExtras::NodeSliderFloat("Source Y", "##FramingY", &settings.sourceY, -1.0f, 1.0f, "%.3f");
    ImGui::TextDisabled("Translation is bounded to covered source pixels");
    return changed;
}

} // namespace Stack::NodeEditors
