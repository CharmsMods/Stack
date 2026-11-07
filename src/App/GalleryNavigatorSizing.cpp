#include "NavigationRail.h"
#include "WorkspacePresentation.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Stack::Navigation {
namespace {
// Startup reserves room for the revealed sidebar above the 240-unit panel
// minimum. Manual resizing may still go narrower during the current session.
constexpr float kStartupPanelMinimum = 360.f;
void RegisterWidth(const char* type, float& width) {
    width=std::max(width,kStartupPanelMinimum);
    ImGuiSettingsHandler handler;
    handler.TypeName = type;
    handler.TypeHash = ImHashStr(type);
    handler.UserData = &width;
    handler.ClearAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h) {
        *static_cast<float*>(h->UserData) = kStartupPanelMinimum;
    };
    handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* h, const char* name) -> void* {
        return std::strcmp(name, "Layout") == 0 ? h->UserData : nullptr;
    };
    handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
        float parsed = 0;
        if (std::sscanf(line, "Width=%f", &parsed) == 1 && std::isfinite(parsed))
            *static_cast<float*>(entry) = std::clamp(parsed, kStartupPanelMinimum, 640.f);
    };
    handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* out) {
        out->appendf("[%s][Layout]\nWidth=%.1f\n\n", h->TypeName, *static_cast<float*>(h->UserData));
    };
    ImGui::AddSettingsHandler(&handler);
}
}

void RegisterGalleryNavigatorSettings(RailState& state) {
    // Registered before the context loads its ini. The handler and state have
    // the same AppShell/context lifetime; no callback is registered by drawing.
    RegisterWidth("StackGalleryNavigator", state.galleryPanelWidth);
    RegisterWidth("StackQueueNavigator", state.queuePanelWidth);
    RegisterWidth("StackEditorNavigator", state.editorPanelWidth);
}

void FinishGalleryNavigatorResize(RailState& state) {
    if (!state.galleryResizeActive) return;
    state.galleryResizeActive = false;
    if (state.resizeAmount < .5f) state.panelOpen = false;
    state.resizeAmount = 1.f;
    const float width = state.editorResize ? state.editorPanelWidth : state.galleryResizeForQueue ? state.queuePanelWidth : state.galleryPanelWidth;
    if (width != state.galleryResizeOriginalWidth) ImGui::MarkIniSettingsDirty();
}

namespace {
void RenderNavigatorResize(RailState& state, bool queue, bool editor=false) {
    float& panelWidth = editor ? state.editorPanelWidth : queue ? state.queuePanelWidth : state.galleryPanelWidth;
    const float s = state.scale;
    const float width = 20 * s;
    const float height = 64 * s;
    const ImVec2 origin(state.panelPosition.x + state.visiblePanelWidth + 6*s, state.panelPosition.y + (state.panelSize.y-height)*.5f);
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(editor ? "EditorNavigatorResize" : queue ? "QueueNavigatorResize" : "GalleryNavigatorResize", nullptr, flags)) {
        ImGui::InvisibleButton(queue ? "Resize queue navigator" : "Resize source navigator", ImVec2(width, height));
        if (ImGui::IsItemActivated()) {
            state.galleryResizeActive = true;
            state.panelOpen = true;
            state.galleryResizeForQueue = queue;
            state.editorResize = editor;
            state.galleryResizeOriginalWidth = panelWidth;
            state.galleryResizeStartWidth = (state.visiblePanelWidth + state.visibleWidth) / s;
            state.galleryResizeStartX = ImGui::GetMousePos().x;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0,1.f)) {
            const float delta = (ImGui::GetMousePos().x - state.galleryResizeStartX) / s;
            const float requested=state.galleryResizeStartWidth+delta;
            const float railWidth=state.visibleWidth/s;
            panelWidth=std::clamp(requested,240.f+railWidth,640.f);
            state.resizeAmount=std::clamp((requested-railWidth)/240.f,0.f,1.f);
            state.panelAmount=state.resizeAmount;
            if (state.resizeAmount < .5f) {
                state.suppressEdgeRevealUntilLeave = true;
                state.visibleUntil = 0.0;
            }
        }
        if (ImGui::IsItemDeactivated()) FinishGalleryNavigatorResize(state);
        const bool hot=ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        auto* draw=ImGui::GetWindowDrawList();
        auto fill=ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);fill.w=hot?.95f:.75f;
        draw->AddRectFilled(origin,{origin.x+width,origin.y+height},ImGui::GetColorU32(fill),width*.5f);
        auto ink=ImGui::GetStyleColorVec4(ImGuiCol_Text);ink.w*=hot?.85f:.5f;
        draw->AddLine({origin.x+width*.5f,origin.y+height*.3f},
            {origin.x+width*.5f,origin.y+height*.7f},ImGui::GetColorU32(ink),2*s);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to resize; drag left to close");
    }
    if (ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar(4);
}
}
void RenderGalleryNavigatorResize(RailState& state) { RenderNavigatorResize(state, false); }
void RenderEditorNavigatorResize(RailState& state) { RenderNavigatorResize(state, false, true); }
void RenderQueueNavigatorResize(RailState& state) { RenderNavigatorResize(state, true); }
} // namespace Stack::Navigation
