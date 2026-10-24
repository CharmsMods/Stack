#include "AppShell.h"
#include "UiPerformanceTrace.h"
#include "UiPerformanceProbe.h"
#include "EdgeChrome.h"
#include "WorkspacePresentation.h"
#include "WorkspaceInputScope.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr const char* ToolbarWindow = "StackRawMaskToolbar";
constexpr const char* ToggleWindow = "StackPanelToggle";

bool BelongsTo(const ImGuiWindow* window, const char* name) {
    return window && std::strcmp(window->RootWindow->Name, name) == 0;
}

void CloseMaskMenu(Stack::Navigation::RawMaskToolbarState& toolbar) {
    auto* context = ImGui::GetCurrentContext();
    for (int i = 0; i < context->OpenPopupStack.Size; ++i) {
        if (context->OpenPopupStack[i].PopupId == toolbar.popupId) {
            ImGui::ClosePopupToLevel(i, false);
            break;
        }
    }
    toolbar.menuOpen = false;
    toolbar.target.reset();
    toolbar.targetWorkspace = 0;
    toolbar.commandError.clear();
}
} // namespace

bool AppShell::TopBarOwnsPanelToggle() const {
    return m_RawMaskToolbar.revealAmount > m_NavigationRail.revealAmount;
}

void AppShell::ToggleWorkspaceSectionPanel() {
    if (m_Editor->IsBracketingPresentationActive() || m_Editor->IsAutoBracketWorkspace()) return;
    m_NavigationRail.panelOpen = !m_NavigationRail.panelOpen;
    if (m_CurrentTabId == 1) m_Editor->SetWorkspacePanelOpen(m_NavigationRail.panelOpen);
    CancelProjectPillPreview();
}

void AppShell::LayoutRawMaskToolbar() {
    auto& toolbar = m_RawMaskToolbar;
    const auto* viewport = ImGui::GetMainViewport();
    const auto& io = ImGui::GetIO();
    const float scale = m_NavigationRail.scale;
    const float height = Stack::Navigation::BarExtent * scale;
    const bool processing = m_Editor->IsBracketingPresentationActive() || m_Editor->IsAutoBracketWorkspace();
    toolbar.enabled = ((m_CurrentTabId == 1 && m_Editor->IsViewMode()) || m_CurrentTabId == 5 || m_CurrentTabId == 3) &&
        m_ActiveProjectWorkspace != m_GalleryWorkspaceId &&
        !m_Editor->IsRawWorkspaceGalleryWorkspaceOpen() && !processing;
    toolbar.menuDrawn = false;
    const bool focused = m_UiPerformanceCapture || glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE;
    toolbar.menuOpen = toolbar.popupId && ImGui::IsPopupOpen(toolbar.popupId, ImGuiPopupFlags_None);
    if (toolbar.target) {
        const auto available = m_Editor->QueryRawOperationMaskAvailability();
        if (!toolbar.enabled || !focused || toolbar.targetWorkspace != m_ActiveProjectWorkspace ||
            !available.target || !(*toolbar.target == *available.target))
            CloseMaskMenu(toolbar);
    }
    if (!toolbar.menuOpen) toolbar.target.reset();
    if (!toolbar.enabled) {
        if (toolbar.menuOpen || toolbar.target) CloseMaskMenu(toolbar);
        toolbar.visibleUntil = 0.0;
        // Keep the same visible geometry through View -> Graph. Clearing the
        // bar immediately also teleports the destination canvas upward.
        if (processing || Stack::Notifications::SystemReducedMotion()) toolbar.revealAmount = 0.f;
        else Stack::Header::ApproachMotion(toolbar.revealAmount, 0.f, .24f);
    } else {
        const bool inWidth = io.MousePos.x >= viewport->Pos.x && io.MousePos.x < viewport->Pos.x + viewport->Size.x;
        const bool atEdge = inWidth && io.MousePos.y >= viewport->Pos.y && io.MousePos.y <= viewport->Pos.y + 2.f * scale;
        const bool hovered = inWidth && toolbar.revealAmount > .001f && io.MousePos.y >= viewport->Pos.y &&
            io.MousePos.y < viewport->Pos.y + height * toolbar.revealAmount;
        const auto* context = ImGui::GetCurrentContext();
        const auto belongs = [&](const ImGuiWindow* window) {
            return BelongsTo(window, ToolbarWindow) || (TopBarOwnsPanelToggle() && BelongsTo(window, ToggleWindow));
        };
        const bool interacting = (context->ActiveId && belongs(context->ActiveIdWindow)) ||
            (Stack::Navigation::NavigationInputActive() && belongs(context->NavWindow));
        Stack::Navigation::UpdateEdgeReveal(toolbar.revealAmount, toolbar.visibleUntil, focused,
            atEdge, hovered, interacting, toolbar.menuOpen, Stack::Notifications::SystemReducedMotion());
    }
    toolbar.visibleHeight = height * toolbar.revealAmount;
    toolbar.position = ImVec2(viewport->Pos.x, viewport->Pos.y - height + toolbar.visibleHeight);
    toolbar.maskButtonPosition = ImVec2(viewport->Pos.x + (viewport->Size.x - Stack::Navigation::ButtonExtent * scale) * .5f,
        toolbar.position.y + Stack::Navigation::BarPadding * scale);

}

void AppShell::RenderRawMaskToolbar() {
    Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Toolbar,
        Stack::UiPerformance::Probe::Key(m_RawMaskToolbar.revealAmount));
    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::NavigationBars);
    auto& toolbar = m_RawMaskToolbar;
    if (toolbar.revealAmount<=.001f && !toolbar.menuOpen) return;
    const auto* viewport = ImGui::GetMainViewport();
    const float scale = m_NavigationRail.scale;
    const auto palette = Stack::Header::ResolvePalette(m_Editor ? m_Editor->GetWorkspaceBaseColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImVec4 tint = palette.caption;
    tint.w = Stack::Header::ResolveSectionTintOpacity(m_NavigationRail.panelAmount);
    ImGui::SetNextWindowPos(toolbar.position);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, Stack::Navigation::BarExtent * scale));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, tint);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin(ToolbarWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
        (!toolbar.enabled || m_Editor->IsNodeOpenTransitionActive() ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None));
    if (!toolbar.enabled) {
        // Only the retained visual exits. Do not resolve a mask target or run
        // widget/menu work for a surface that can no longer accept commands.
        auto color = palette.mutedText;
        if (!toolbar.maskButtonEnabled) color.w *= ImGui::GetStyle().DisabledAlpha;
        Stack::Navigation::DrawGlyph(ImGui::GetWindowDrawList(), Stack::Navigation::Glyph::Mask,
            ImVec2(toolbar.maskButtonPosition.x+8.5f*scale,toolbar.maskButtonPosition.y+8.5f*scale),
            21.f*scale,ImGui::GetColorU32(color));
        m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        return;
    }
    Stack::Workspace::InputScope transitionInput(m_Editor->IsNodeOpenTransitionActive(), false);
    toolbar.popupId = ImGui::GetID("OperationMaskMenu");
    auto available = m_Editor->QueryRawOperationMaskAvailability();
    if (ActiveProjectLoadPhase() != LibraryToEditorProjectLoadPhase::None || m_RootTabBodyFadeActive ||
        m_Editor->IsBracketingPresentationActive() || m_NavigationRail.previewAmount > .001f ||
        Stack::Workspace::IsPreview()) {
        available.target.reset();
        available.disabledReason = "Wait for the active RAW workspace to finish its transition.";
        if (toolbar.target) CloseMaskMenu(toolbar);
    }
    ImGui::SetCursorScreenPos(toolbar.maskButtonPosition);
    toolbar.maskButtonEnabled = available.target.has_value();
    if (Stack::Navigation::IconButton("Create mask", Stack::Navigation::Glyph::Mask,
            ImGui::IsPopupOpen("OperationMaskMenu"), available.target.has_value(), scale, palette, available.disabledReason.c_str())) {
        CancelProjectPillPreview();
        toolbar.target = available.target;
        toolbar.targetWorkspace = m_ActiveProjectWorkspace;
        toolbar.commandError.clear();
        ImGui::OpenPopup("OperationMaskMenu");
    }
    if (ImGui::IsPopupOpen("OperationMaskMenu")) {
        const float anchorX = std::clamp(toolbar.maskButtonPosition.x - 85.f * scale,
            viewport->Pos.x, std::max(viewport->Pos.x, viewport->Pos.x + viewport->Size.x - 220.f * scale));
        // Follow the revealing bar until it settles without retaining old screen coordinates.
        ImGui::SetNextWindowPos(ImVec2(anchorX, viewport->Pos.y + toolbar.visibleHeight + 4.f * scale));
    }
    if (ImGui::BeginPopup("OperationMaskMenu")) {
        toolbar.menuDrawn = true;
        if (!toolbar.target) ImGui::CloseCurrentPopup();
        else {
            ImGui::TextUnformatted(toolbar.target->displayLabel.c_str());
            ImGui::TextDisabled("%s", toolbar.target->layerLabel.c_str());
            ImGui::Separator();
            if (ImGui::Selectable("Draw values (C-drag)",false,ImGuiSelectableFlags_NoAutoClosePopups)) {
                if (m_Editor->BeginRawAssetCapture(*toolbar.target,Stack::RawAssetCapture::Shape::Stroke,
                        Stack::RawAssetCapture::Channels::Both,toolbar.commandError)) ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            for (auto kind : {EditorNodeGraph::MaskGeneratorKind::RadialGradient,
                             EditorNodeGraph::MaskGeneratorKind::LinearGradient,
                             EditorNodeGraph::MaskGeneratorKind::Square}) {
                const char* name = kind == EditorNodeGraph::MaskGeneratorKind::RadialGradient ? "Radial" :
                    kind == EditorNodeGraph::MaskGeneratorKind::LinearGradient ? "Linear" : "Rectangle";
                if (ImGui::Selectable(name, false, ImGuiSelectableFlags_NoAutoClosePopups)) {
                    if (m_Editor->CreateRawOperationMask(*toolbar.target, kind, toolbar.commandError))
                        ImGui::CloseCurrentPopup();
                }
            }
            if (!toolbar.commandError.empty()) ImGui::TextWrapped("%s", toolbar.commandError.c_str());
        }
        m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
        ImGui::EndPopup();
    }
    toolbar.menuOpen = ImGui::IsPopupOpen("OperationMaskMenu");
    m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void AppShell::RenderSharedPanelToggle() {
    if (m_Editor->IsBracketingPresentationActive() || m_Editor->IsAutoBracketWorkspace()) return;
    const float amount = std::max(m_NavigationRail.revealAmount, m_RawMaskToolbar.revealAmount);
    const float scale = m_NavigationRail.scale;
    const bool glassLeft = !TopBarOwnsPanelToggle() && m_Appearance && m_Appearance->GetGlassChromeEnabled();
    const float toggleWidth = glassLeft ? std::min(Stack::Navigation::ButtonExtent*scale,
        m_NavigationRail.visibleWidth-Stack::Navigation::BarPadding*scale) : Stack::Navigation::ButtonExtent*scale;
    // This sibling window must reveal with the rail, not float outside its
    // clipped width or remain visible when both edge bars are fully hidden.
    if (glassLeft && toggleWidth <= 0.f) return;
    const auto palette = Stack::Header::ResolvePalette(m_Editor ? m_Editor->GetWorkspaceBaseColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetNextWindowPos(m_RawMaskToolbar.panelTogglePosition);
    ImGui::SetNextWindowSize(ImVec2(std::max(1.f,toggleWidth), Stack::Navigation::ButtonExtent * scale));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::Begin(ToggleWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
        (amount <= .001f || m_Editor->IsReturningToGraph() ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None));
    Stack::Workspace::InputScope transitionInput(m_Editor->IsReturningToGraph(), false);
    if (Stack::Navigation::IconButton(m_NavigationRail.panelOpen ? "Close side panel###PanelToggle" : "Open side panel###PanelToggle",
            Stack::Navigation::Glyph::Panel, false, true, scale, palette, nullptr,
            glassLeft && m_RailGlass.Error().empty(), &m_RailGlass))
        ToggleWorkspaceSectionPanel();
    m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

bool AppShell::RawMaskToolbarOwnsInput() const {
    const auto& toolbar = m_RawMaskToolbar;
    const auto* context = ImGui::GetCurrentContext();
    // The shared toggle also suppresses canvas polling when the left bar owns
    // it and a held pointer has moved beyond that bar's rectangle.
    if (context->ActiveId && BelongsTo(context->ActiveIdWindow, ToggleWindow)) return true;
    if (!toolbar.enabled) return false;
    if (toolbar.menuOpen || toolbar.menuDrawn) return true;
    const auto* viewport = ImGui::GetMainViewport();
    if (toolbar.visibleHeight > .5f && ImRect(viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + toolbar.visibleHeight)).Contains(ImGui::GetIO().MousePos))
        return true;
    return context->ActiveId && BelongsTo(context->ActiveIdWindow, ToolbarWindow);
}
