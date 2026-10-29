#include "Editor/UI/EditorViewport.h"
#include "Editor/EditorModule.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

bool EditorViewport::ComparisonControlsOwnPointer(ImVec2 origin) const {
    if (!m_ShowSplitCompare || !m_Comparison.HasCaptureControls()) return false;
    return ImRect(ImVec2(origin.x + 12.f, origin.y + 12.f),
        ImVec2(origin.x + 340.f, origin.y + 12.f + ImGui::GetFrameHeight())).Contains(ImGui::GetMousePos());
}

bool EditorViewport::PrepareRawSplitComparison(EditorModule* editor, const ImVec2& available) {
    const auto& io = ImGui::GetIO();
    const bool keys = editor->IsViewMode() && editor->CanConsumeEditorCommandKeys() &&
        !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper &&
        !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        (ImGui::IsWindowHovered() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    if (keys && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        m_ShowSplitCompare = !m_ShowSplitCompare;
        m_ShowStaticSingleCompare = false; m_StaticSingleCompareBlend = 0.f;
        m_StaticCompareRectsInitialized = false;
    }
    if (!m_ShowSplitCompare || !editor->IsViewMode()) { m_Comparison.Suspend(); return false; }
    const int edge = std::clamp(int(std::ceil(std::max(available.x, available.y) / 512.f)) * 512, 512, 4096);
    editor->UpdateViewComparisonPreview(m_Comparison, edge);
    return m_Comparison.Texture() != 0;
}

void EditorViewport::DrawRawComparisonBefore(ImDrawList* list, ImVec2 minimum, ImVec2 maximum) const {
    if (!m_Comparison.Texture()) return;
    list->PushClipRect(minimum, ImVec2((minimum.x + maximum.x) * .5f, maximum.y), true);
    list->AddImage((ImTextureID)(intptr_t)m_Comparison.Texture(), minimum, maximum, ImVec2(0, 0), ImVec2(1, 1));
    list->PopClipRect();
}

void EditorViewport::DrawRawComparisonControls(ImVec2 origin) {
    if (!m_ShowSplitCompare) return;
    const auto cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 12.f, origin.y + 12.f));
    ImGui::PushID("RawSplitComparison");
    m_Comparison.DrawControls();
    if (!m_Comparison.Status().empty() && !m_Comparison.Texture())
        ImGui::TextDisabled("%s", m_Comparison.Status().c_str());
    ImGui::PopID();
    ImGui::SetCursorScreenPos(cursor);
}
