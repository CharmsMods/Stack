#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include <imgui.h>
#include <cmath>

bool EditorNodeGraphUI::RenderMixControls(NodeContentRenderContext& context) {
    auto& node = context.node;
    auto& cache = m_MixBars[node.id];
    const bool live = m_GraphContext.graph == &context.graph && !m_RenderPreviewOnly && !m_RenderCaptureOnly;
    if (!live || !cache.contributions || cache.revision != context.graph.GetStructureRevision() ||
        cache.weights != node.dataMathSettings.mixWeights) {
        cache.contributions.emplace(context.graph, node);
        cache.revision = live ? context.graph.GetStructureRevision() : 0;
        cache.weights = node.dataMathSettings.mixWeights;
    }
    auto& mix = *cache.contributions;
    if (!mix.count) {
        ImGui::TextDisabled("Connect inputs to set their contributions.");
        return false;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(1.f, context.controlWidth);
    const float height = ImGui::GetFrameHeight();
    ImGui::Dummy({width, height});
    const auto after = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    bool changed = false;
    float left = origin.x;
    for (int i = 0; i < mix.count; ++i) {
        const float right = left + width * static_cast<float>(mix.shares[i]);
        const auto color = ImGui::GetColorU32(i % 2 ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        draw->AddRectFilled({left, origin.y}, {right, origin.y + height}, color);
        const char label[] = {char('A' + mix.slots[i]), 0};
        if (right - left > ImGui::CalcTextSize(label).x + 6)
            draw->AddText({left + 3, origin.y + ImGui::GetStyle().FramePadding.y}, ImGui::GetColorU32(ImGuiCol_Text), label);
        if (i + 1 < mix.count) {
            draw->AddLine({right, origin.y}, {right, origin.y + height}, ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::SetCursorScreenPos({right - 3, origin.y});
            ImGui::PushID(i);
            ImGui::InvisibleButton("boundary", {6, height});
            context.captureIfActive();
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0) && ImGui::GetIO().MouseDelta.x != 0) {
                mix.MoveBoundary(i, ImGui::GetIO().MouseDelta.x / width);
                changed = true;
            }
            ImGui::PopID();
        }
        left = right;
    }
    ImGui::SetCursorScreenPos(after);
    for (int i = 0; i < mix.count; ++i) {
        const char label[] = {char('A' + mix.slots[i]), ' ', '%', 0};
        double percent = mix.shares[i] * 100;
        ImGui::SetNextItemWidth(width * .72f);
        if (ImGui::InputDouble(label, &percent, 0, 0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue) &&
            std::isfinite(percent) && mix.count > 1 && std::clamp(percent, 0.0, 100.0) != mix.shares[i] * 100) {
            mix.SetShare(i, percent / 100);
            changed = true;
        }
        context.captureIfActive();
    }
    if (changed) mix.Store(node);
    return changed;
}
