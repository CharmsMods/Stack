#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "AppShell.h"
#include "WorkspaceInputScope.h"
#include "AppHeaderStyle.h"
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
    const bool available = focused && !chord && !m_CloseRequested && !m_NotificationPresenter.BlocksInput() &&
        (m_CurrentTabId == 5 || m_CurrentTabId == 3) && hasBounds &&
        !m_Editor->IsWorkspaceTransitionPending() && !m_Editor->IsRawWorkspaceLockedByEditorProject() &&
        !m_Editor->IsAutoBracketWorkspace() && !m_Editor->IsRawBracketModeActive() &&
        !m_Editor->IsRawWorkspaceGalleryWorkspaceOpen() && !m_Editor->IsBracketingPresentationActive() &&
        !m_WorkspaceSwitcher.Visible() && m_StartupRevealVisual.AllowsInput() &&
        !m_ShowLegalGateReview && (!m_LegalManager || m_LegalManager->IsAccepted()) &&
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
        if (commit && selection >= 0) m_Editor->RequestRawLabToolIndex(selection);
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
                sw.pointer.x += static_cast<float>(x - sw.lastPointerX);
                sw.pointer.y += static_cast<float>(y - sw.lastPointerY);
                sw.lastPointerX = x; sw.lastPointerY = y;
                const float length = std::hypot(sw.pointer.x, sw.pointer.y);
                if (length > layout.radius * .8f) {
                    sw.pointer.x *= layout.radius * .8f / length;
                    sw.pointer.y *= layout.radius * .8f / length;
                }
            } else sw.pointer = ImVec2(io.MousePos.x - layout.center.x, io.MousePos.y - layout.center.y);
            sw.hovered = sw.Hit(sw.pointer, layout);
            if (sw.mouseOpen && std::hypot(sw.pointer.x, sw.pointer.y) > layout.radius) sw.hovered = -1;
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
    constexpr const char* labels[] = {"Transform", "Denoise", "Color", "Global\nExposure", "Curves", "View\nTransform"};
    static_assert(std::size(labels) == Stack::EditorModuleTypes::kRawLabDrawerTools.size());
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
    ImGui::Begin("RawToolRadial", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
    auto* draw = ImGui::GetWindowDrawList();
    const float alpha = sw.Amount(), radius = layout.radius * (.96f + .04f * alpha);
    const auto tint = [&](ImVec4 color, float opacity) { color.w *= alpha * opacity; return ImGui::GetColorU32(color); };
    draw->AddCircleFilled(layout.center, radius, tint(palette.caption, .93f), 96);
    constexpr float pi = 3.14159265359f;
    const int active = m_Editor->GetRawLabToolIndex();
    for (int i = 0; i < Stack::Tools::Switcher::Count; ++i) {
        const float angle = -pi * .5f + 2.f * pi * i / Stack::Tools::Switcher::Count;
        const float half = pi / Stack::Tools::Switcher::Count - .025f;
        if (i == sw.hovered || i == active) {
            draw->PathArcTo(layout.center, radius - 4.f, angle - half, angle + half, 18);
            for (int j = 18; j >= 0; --j) {
                const float a = angle - half + 2.f * half * j / 18.f;
                draw->PathLineTo(ImVec2(layout.center.x + std::cos(a) * layout.inner,
                    layout.center.y + std::sin(a) * layout.inner));
            }
            draw->PathFillConcave(tint(palette.selected, i == sw.hovered ? 1.f : .45f));
        }
        const float fontSize = std::min(ImGui::GetFontSize(), radius * .105f);
        const auto text = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0, labels[i]);
        const ImVec2 position(layout.center.x + std::cos(angle) * radius * .68f - text.x * .5f,
            layout.center.y + std::sin(angle) * radius * .68f - text.y * .5f);
        draw->AddText(ImGui::GetFont(), fontSize, position,
            tint(palette.text, i == sw.hovered || i == active ? 1.f : .72f), labels[i]);
    }
    const char* center = sw.hovered >= 0 ? (sw.held ? "Release" : "Choose") : "Cancel";
    const auto text = ImGui::CalcTextSize(center);
    draw->AddText(ImVec2(layout.center.x - text.x * .5f, layout.center.y - text.y * .5f),
        tint(palette.mutedText, .85f), center);
    if (sw.held && std::hypot(sw.pointer.x, sw.pointer.y) > 2.f)
        draw->AddCircleFilled(ImVec2(layout.center.x + sw.pointer.x, layout.center.y + sw.pointer.y),
            3.f * m_NavigationRail.scale, tint(palette.text, 1.f), 16);
    m_WorkspaceCompositor.KeepToolsFixed(draw);
    ImGui::End();
    ImGui::PopStyleVar(2);
}
