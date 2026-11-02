#include "Editor/UI/EditorViewport.h"
#include "Editor/UI/ViewComparisonLayout.h"
#include "Editor/EditorModule.h"
#include "Notifications/NotificationPresenter.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace {
ImRect ComparisonBounds() {
    // Photo/overlay draw-list clips may already be narrowed to one pane.
    // The window clip is the stable area left by shell chrome for both panes.
    return ImGui::GetCurrentWindow()->ClipRect;
}
Stack::Editor::ViewComparisonLayout ComparisonLayout(ImVec2 minimum, ImVec2 maximum,
    ImVec2 fittedSize, float progress) {
    const auto visible = ComparisonBounds();
    return Stack::Editor::ViewComparisonLayout::Resolve(minimum, maximum,
        visible.Min, visible.Max, fittedSize, progress);
}
}

bool EditorViewport::ComparisonControlsOwnPointer(ImVec2 origin) const {
    if (!m_ShowSplitCompare || !m_Comparison.HasCaptureControls()) return false;
    const auto visible = ComparisonBounds();
    origin.x = std::max(origin.x, visible.Min.x);
    origin.y = std::max(origin.y, visible.Min.y);
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
    if (m_ShowSplitCompare && editor->IsViewMode()) {
        // One bounded original raster survives panel resizing and navigation.
        const int edge = 4096;
        editor->UpdateViewComparisonPreview(m_Comparison, edge);
    }
    const bool shown = m_ShowSplitCompare && editor->IsViewMode() && m_Comparison.Texture();
    m_RawSplitProgress = Stack::Notifications::SystemReducedMotion() ? (shown ? 1.f : 0.f) :
        std::clamp(m_RawSplitProgress + (shown ? 1.f : -1.f) * io.DeltaTime / .22f, 0.f, 1.f);
    // A requested comparison may still be preparing its texture. Suspending
    // that job here used to cancel it on every loading frame.
    if ((!m_ShowSplitCompare || !editor->IsViewMode()) && m_RawSplitProgress == 0.f)
        m_Comparison.Suspend();
    return m_RawSplitProgress > 0.f;
}

void EditorViewport::PresentRawComparison(ImVec2& minimum, ImVec2& maximum,
    ImVec2 viewMinimum, ImVec2 viewMaximum, ImVec2 fittedSize) {
    if (m_RawSplitProgress <= 0.f) return;
    const auto layout = ComparisonLayout(
        viewMinimum,viewMaximum,fittedSize,m_RawSplitProgress);
    m_RawComparisonBeforeMin = layout.Present(minimum,false);
    m_RawComparisonBeforeMax = layout.Present(maximum,false);
    minimum = layout.Present(minimum,true);
    maximum = layout.Present(maximum,true);
}

void EditorViewport::MapRawComparisonInput(ImVec2& mouse, ImVec2& delta,
    ImVec2 minimum, ImVec2 maximum, ImVec2 fittedSize) const {
    if (m_RawSplitProgress <= 0.f) return;
    const auto layout = ComparisonLayout(
        minimum,maximum,fittedSize,m_RawSplitProgress);
    mouse = layout.CameraPoint(mouse,mouse.x >= layout.center.x);
    delta = {delta.x/layout.scale,delta.y/layout.scale};
}

void EditorViewport::PushRawComparisonClip(ImDrawList* list, ImVec2 minimum, ImVec2 maximum, bool after) const {
    const auto visible = ComparisonBounds();
    minimum = visible.Min;
    maximum = visible.Max;
    const float t = m_RawSplitProgress;
    const float blend = t*t*(3.f-2.f*t);
    const float center = (minimum.x+maximum.x)*.5f;
    const float gap = 1.f;
    if (after) minimum.x += (center+gap*.5f-minimum.x)*blend;
    else maximum.x = center-gap*.5f;
    list->PushClipRect(minimum,maximum,true);
}

void EditorViewport::DrawRawComparisonBefore(ImDrawList* list, ImVec2 minimum, ImVec2 maximum) const {
    if (!m_Comparison.Texture() || m_RawSplitProgress <= 0.f) return;
    const float t=m_RawSplitProgress;
    PushRawComparisonClip(list,minimum,maximum,false);
    list->AddImage((ImTextureID)(intptr_t)m_Comparison.Texture(),
        m_RawComparisonBeforeMin, m_RawComparisonBeforeMax, ImVec2(0, 0), ImVec2(1, 1),
        ImGui::GetColorU32(ImVec4(1,1,1,t*t*(3.f-2.f*t))));
    list->PopClipRect();
    const auto visible = ComparisonBounds();
    const float center = (visible.Min.x+visible.Max.x)*.5f;
    list->AddLine({center,visible.Min.y},{center,visible.Max.y},
        ImGui::GetColorU32(ImVec4(.55f,.55f,.55f,t)),1.f);
}

void EditorViewport::DrawRawComparisonControls(ImVec2 origin) {
    if (!m_ShowSplitCompare) return;
    const auto visible = ComparisonBounds();
    origin.x = std::max(origin.x, visible.Min.x);
    origin.y = std::max(origin.y, visible.Min.y);
    const auto cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 12.f, origin.y + 12.f));
    ImGui::PushID("RawSplitComparison");
    m_Comparison.DrawControls();
    if (!m_Comparison.Status().empty() && !m_Comparison.Texture())
        ImGui::TextDisabled("%s", m_Comparison.Status().c_str());
    ImGui::PopID();
    ImGui::SetCursorScreenPos(cursor);
}
