#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "AppShell.h"
#include "WorkspaceInputScope.h"
#include "AppHeaderStyle.h"
#include "ToolListPickerDrawing.h"
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
    sw.progress=0.f;
    m_Editor->SetRawToolPickerAmount(0.f);
}

void AppShell::TickToolSwitcher() {
    if(m_Editor->IsFocusedNodeEditor()) {if(m_ToolSwitcher.Visible())CancelToolSwitcher();return;}
    auto& sw = m_ToolSwitcher;
    auto& io = ImGui::GetIO();
    const bool rightAlt = AltKeyDown(m_Window, true);
    const bool down = AltKeyDown(m_Window, false) || rightAlt;
    const bool focused = glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE;
    const bool chord = io.KeyCtrl || io.KeySuper || io.KeyShift ||
        ImGui::IsKeyDown(ImGuiKey_Tab) || ImGui::IsKeyDown(ImGuiKey_F4);
    const bool mouseRequest = m_Editor->ConsumeRawToolPickerRequest();
    // An idle picker needs only its activation edge, not a node catalog walk.
    if (!sw.Visible() && !sw.discardMouse && !mouseRequest &&
        (!down || sw.wasDown || sw.suppressed)) {
        if (!down) sw.suppressed=false;
        sw.wasDown=down;
        return;
    }
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
        const auto& graph=m_Editor->GetRawLayerToolGraph();
        const auto workSize=ImGui::GetMainViewport()->WorkSize;
        const bool changed=sw.document!=graph.documentId || sw.owner!=graph.layerId ||
            sw.revision!=graph.revision || sw.sourceRevision!=graph.sourceRevision ||
            workSize.x!=sw.openedWorkSize.x || workSize.y!=sw.openedWorkSize.y;
        if (!available || changed) { close(false); sw.progress=0.f; }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close(false);
        else {
            if (sw.held) {
                double x, y;
                glfwGetCursorPos(m_Window, &x, &y);
                constexpr float pointerGain=.65f;
                sw.listPosition=std::clamp(sw.listPosition+pointerGain*float(y-sw.lastPointerY)/sw.rowHeight,
                    0.f,float(sw.count)-.001f);
                sw.lastPointerX=x;sw.lastPointerY=y;
                sw.listPosition=std::clamp(sw.listPosition-io.MouseWheel,0.f,float(sw.count)-.001f);
                sw.hovered=int(sw.listPosition);
                if(sw.hovered<sw.firstRow) sw.firstRow=sw.hovered;
                if(sw.hovered>=sw.firstRow+sw.visibleRows) sw.firstRow=sw.hovered-sw.visibleRows+1;
            } else if(sw.Amount()>=.99f) {
                sw.firstRow=std::clamp(sw.firstRow-int(io.MouseWheel),0,std::max(0,sw.count-sw.visibleRows));
                const auto p=io.MousePos;
                sw.hovered=p.x>=sw.listMin.x && p.x<=sw.listMax.x &&
                    p.y>=sw.listMin.y+8 && p.y<sw.listMax.y-8 ?
                    sw.firstRow+int((p.y-sw.listMin.y-8)/sw.rowHeight) : -1;
            }
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
            // Reverse an unfinished fade from its existing geometry. Hidden
            // controls may not have published bounds on this frame.
            const bool reversing=sw.Visible();
            if (!reversing) { sw.controlsMin = minimum; sw.controlsMax = maximum; }
            sw.Begin(!mouseRequest);
            const auto& graph=m_Editor->GetRawLayerToolGraph();
            sw.document=graph.documentId;sw.owner=graph.layerId;
            sw.revision=graph.revision;sw.sourceRevision=graph.sourceRevision;
            sw.openedWorkSize=ImGui::GetMainViewport()->WorkSize;
            const int active=m_Editor->GetRawLayerToolIndex();
            if (!reversing) Stack::Tools::PlaceToolList(sw,m_NavigationRail.scale,active);
            else sw.hovered=std::clamp(int(sw.listPosition),0,sw.count-1);
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
    if (sw.progress != (sw.Open() ? 1.f : 0.f))
        sw.Tick(io.DeltaTime, Stack::Notifications::SystemReducedMotion());
    m_Editor->SetRawToolPickerAmount(sw.Amount());
}

void AppShell::DrawToolSwitcher() {
    auto& sw = m_ToolSwitcher;
    if (!sw.Visible()) return;
    const auto& labels = m_Editor->GetRawLayerToolLabels();
    if (labels.size() < 2) return;
    auto* viewport = ImGui::GetMainViewport();
    const auto palette = Stack::Header::ResolvePalette(m_Appearance ? m_Appearance->GetClearColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetMouseCursor(sw.held ? ImGuiMouseCursor_None : ImGuiMouseCursor_Arrow);
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("RawToolListPicker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
    auto* draw = ImGui::GetWindowDrawList();
    Stack::Tools::DrawToolList(*draw,sw,labels,palette);
    m_WorkspaceCompositor.KeepToolsFixed(draw);
    ImGui::End();
    ImGui::PopStyleVar(2);
}
