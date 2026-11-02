#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "AppShell.h"
#include "WorkspaceInputScope.h"
#include "AppHeaderStyle.h"
#include "ToolGraphPickerDrawing.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <iterator>

namespace {
bool AltKeyDown(GLFWwindow* window, bool right) {
#if defined(_WIN32)
    return (GetAsyncKeyState(right ? VK_RMENU : VK_LMENU) & 0x8000) != 0;
#else
    return glfwGetKey(window, right ? GLFW_KEY_RIGHT_ALT : GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
#endif
}
}

void AppShell::CancelToolSwitcher() {
    auto& sw = m_ToolSwitcher;
    if (sw.cursorCaptured && m_Window) {
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE)
            glfwSetCursorPos(m_Window, sw.restoreCursor.x, sw.restoreCursor.y);
    }
    sw.cursorCaptured = false;
    sw.Close();
}

void AppShell::TickToolSwitcher() {
    auto& sw = m_ToolSwitcher;
    auto& io = ImGui::GetIO();
    const bool rightAlt = AltKeyDown(m_Window, true);
    const bool down = AltKeyDown(m_Window, false) || rightAlt;
    const bool focused = glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE;
    const bool chord = io.KeyCtrl || io.KeySuper || io.KeyShift ||
        ImGui::IsKeyDown(ImGuiKey_Tab) || ImGui::IsKeyDown(ImGuiKey_F4);
    const bool mouseRequest = m_Editor->ConsumeRawToolPickerRequest();
    ImVec2 minimum, maximum;
    const bool hasBounds = m_Editor->GetRawActiveControlBounds(minimum, maximum);
    if (!hasBounds) {
        const auto* viewport = ImGui::GetMainViewport();
        minimum = viewport->WorkPos;
        maximum = {minimum.x+viewport->WorkSize.x,minimum.y+viewport->WorkSize.y};
    }
    sw.count = static_cast<int>(m_Editor->GetRawLayerToolLabels().size());
    const bool available = focused && !chord && !m_CloseRequested && !m_NotificationPresenter.BlocksInput() &&
        ((m_CurrentTabId == 1 && m_Editor->IsViewMode()) || m_CurrentTabId == 5 || m_CurrentTabId == 3) && sw.HasChoices() &&
        !m_Editor->IsWorkspaceTransitionPending() && !m_Editor->IsNodeOpenTransitionActive() &&
        !m_Editor->IsRawWorkspaceLockedByEditorProject() &&
        !m_Editor->IsAutoBracketWorkspace() && !m_Editor->IsRawBracketModeActive() &&
        !m_Editor->IsRawWorkspaceGalleryWorkspaceOpen() && !m_Editor->IsBracketingPresentationActive() &&
        !m_WorkspaceSwitcher.Visible() && m_StartupRevealVisual.AllowsInput() &&
        !m_MainWindowCloseSavePending && !m_FileActionSavePending && !m_ProjectLoadSavePending &&
        !m_RawWorkspaceSwitchSavePending && m_ContinueMainWindowCloseSource.empty() &&
        ActiveProjectLoadPhase() == LibraryToEditorProjectLoadPhase::None &&
        !m_RootTabBodyFadeActive && !ImGui::GetTopMostPopupModal();
    if (!down) sw.suppressed = false;
    if (sw.discardMouse) {
        bool busy = false;
        for (int i = 0; i < 5; ++i) busy |= io.MouseDown[i] || io.MouseReleased[i];
        if (!busy) sw.discardMouse = false;
    }
    const auto releaseCursor = [&] {
        if (!sw.cursorCaptured) return;
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (focused && !chord) glfwSetCursorPos(m_Window, sw.restoreCursor.x, sw.restoreCursor.y);
        sw.cursorCaptured = false;
    };
    const auto close = [&](bool commit) {
        const int selection = sw.hovered;
        if (commit && selection >= 0) m_Editor->RequestRawLayerToolIndex(selection);
        releaseCursor();
        sw.Close();
        sw.suppressed = down;
    };
    if (sw.Open()) {
        if (hasBounds) { sw.controlsMin = minimum; sw.controlsMax = maximum; }
        if (!available || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close(false);
        else {
            const auto layout = sw.Geometry(m_NavigationRail.scale);
            if (sw.held) {
                double x, y;
                glfwGetCursorPos(m_Window, &x, &y);
                sw.graphCenter.x += static_cast<float>(x - sw.lastPointerX) / sw.graphZoom;
                sw.graphCenter.y += static_cast<float>(y - sw.lastPointerY) / sw.graphZoom;
                sw.lastPointerX = x; sw.lastPointerY = y;
            }
            sw.graphZoom=std::clamp(sw.graphZoom*std::exp(io.MouseWheel*.15f),.2f,1.5f);
            const auto crosshair=sw.held ? layout.center : io.MousePos;
            sw.hovered=Stack::Tools::PickGraphTool(sw,layout,m_Editor->GetRawLayerToolGraph(),crosshair);
            if (!sw.held && (std::abs(crosshair.x-layout.center.x)>layout.radius ||
                std::abs(crosshair.y-layout.center.y)>layout.radius*.7f)) sw.hovered=-1;
            if (sw.held && !down) close(true);
            else if (sw.mouseOpen && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) close(sw.hovered >= 0);
        }
    } else if (available && !io.WantTextInput && !ImGui::IsAnyMouseDown() &&
        !ImGui::IsAnyItemActive() && !sw.discardMouse &&
        (mouseRequest || (down && !sw.wasDown && !sw.suppressed))) {
        if (m_Editor->FinishWorkspaceInteraction()) {
            Stack::Workspace::CloseOrdinaryPopups();
            ImGui::ClearActiveID();
            CancelProjectPillPreview();
            m_SettingsPopupOpen = false;
            m_SettingsPopupOpenedAt = 0;
            ReleaseLockedScrubCursor(false);
            sw.controlsMin = minimum; sw.controlsMax = maximum;
            sw.Begin(!mouseRequest);
            sw.graphZoom=.65f;
            const auto& graph=m_Editor->GetRawLayerToolGraph();
            const int active=m_Editor->GetRawLayerToolIndex();
            for(const auto& card:graph.cards) if(card.choice==active) {
                sw.graphCenter={card.position.x+95,card.position.y+30};break;
            }
            if (sw.held) {
                double x, y;
                glfwGetCursorPos(m_Window, &x, &y);
                sw.restoreCursor = ImVec2(static_cast<float>(x), static_cast<float>(y));
                glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
                glfwGetCursorPos(m_Window, &sw.lastPointerX, &sw.lastPointerY);
                sw.cursorCaptured = true;
            }
        }
    }
    if (!sw.Open()) releaseCursor();
    sw.wasDown = down;
    sw.Tick(io.DeltaTime, Stack::Notifications::SystemReducedMotion());
}

void AppShell::DrawToolSwitcher() {
    auto& sw = m_ToolSwitcher;
    if (!sw.Visible()) return;
    ImVec2 minimum, maximum;
    if (m_Editor->GetRawActiveControlBounds(minimum, maximum)) {
        sw.controlsMin = minimum; sw.controlsMax = maximum;
    }
    const auto& labels = m_Editor->GetRawLayerToolLabels();
    if (labels.size() < 2) return;
    auto* viewport = ImGui::GetMainViewport();
    const auto layout = sw.Geometry(m_NavigationRail.scale);
    const auto palette = Stack::Header::ResolvePalette(m_Appearance ? m_Appearance->GetClearColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetMouseCursor(sw.held ? ImGuiMouseCursor_None : ImGuiMouseCursor_Arrow);
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("RawToolGraphPicker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
    auto* draw = ImGui::GetWindowDrawList();
    Stack::Tools::DrawGraphPicker(*draw,sw,layout,m_Editor->GetRawLayerToolGraph(),palette,
        m_Editor->GetRawLayerToolIndex());
    m_WorkspaceCompositor.KeepToolsFixed(draw);
    ImGui::End();
    ImGui::PopStyleVar(2);
}
