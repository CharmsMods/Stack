#include "NavigationRail.h"
#include "WorkspacePresentation.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Stack::Navigation {
namespace {
void RegisterWidth(const char* type, float& width) {
    ImGuiSettingsHandler handler;
    handler.TypeName = type;
    handler.TypeHash = ImHashStr(type);
    handler.UserData = &width;
    handler.ClearAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h) {
        *static_cast<float*>(h->UserData) = 288.f;
    };
    handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* h, const char* name) -> void* {
        return std::strcmp(name, "Layout") == 0 ? h->UserData : nullptr;
    };
    handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
        float parsed = 0;
        if (std::sscanf(line, "Width=%f", &parsed) == 1 && std::isfinite(parsed))
            *static_cast<float*>(entry) = std::clamp(parsed, 240.f, 360.f);
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
}

void FinishGalleryNavigatorResize(RailState& state) {
    if (!state.galleryResizeActive) return;
    state.galleryResizeActive = false;
    const float width = state.galleryResizeForQueue ? state.queuePanelWidth : state.galleryPanelWidth;
    if (width != state.galleryResizeOriginalWidth) ImGui::MarkIniSettingsDirty();
}

namespace {
void RenderNavigatorResize(RailState& state, bool queue) {
    float& panelWidth = queue ? state.queuePanelWidth : state.galleryPanelWidth;
    const float s = state.scale;
    const float width = 7 * s;
    const ImVec2 origin(state.panelPosition.x + state.visiblePanelWidth - width, state.panelPosition.y);
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(width, state.panelSize.y));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(queue ? "QueueNavigatorResize" : "GalleryNavigatorResize", nullptr, flags)) {
        ImGui::InvisibleButton(queue ? "Resize queue navigator" : "Resize source navigator", ImVec2(width, state.panelSize.y));
        if (ImGui::IsItemActivated()) {
            state.galleryResizeActive = true;
            state.galleryResizeForQueue = queue;
            state.galleryResizeOriginalWidth = panelWidth;
            state.galleryResizeStartWidth = state.visiblePanelWidth / s;
            state.galleryResizeStartX = ImGui::GetMousePos().x;
        }
        if (ImGui::IsItemActive()) {
            const float delta = (ImGui::GetMousePos().x - state.galleryResizeStartX) / s;
            panelWidth = delta == 0.f ? state.galleryResizeOriginalWidth :
                std::clamp(state.galleryResizeStartWidth + delta, 240.f, 360.f);
        }
        if (ImGui::IsItemDeactivated()) FinishGalleryNavigatorResize(state);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text); color.w *= .4f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x + width - 1, origin.y),
                ImVec2(origin.x + width - 1, origin.y + state.panelSize.y), ImGui::GetColorU32(color));
        }
    }
    if (ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar(4);
}
}
void RenderGalleryNavigatorResize(RailState& state) { RenderNavigatorResize(state, false); }
void RenderQueueNavigatorResize(RailState& state) { RenderNavigatorResize(state, true); }
} // namespace Stack::Navigation
