#pragma once
#include "App/settings/CreamPalette.h"
#include <imgui.h>
namespace ImGuiExtras {
// Frame-local requests only. The cursor animation never writes mouse input.
void BeginGraphCursorFrame();
void ConfigureGraphCursor(const void* graph,ImVec2 min,ImVec2 max,bool hovered,
    const StackAppearance::ResolvedCreamPalette& palette,ImGuiViewport* viewport);
void RegisterGraphCursorSurface(const void* graph,ImVec2 min,ImVec2 max,ImVec4 surface);
void RequestGraphValueCursor(const void* graph,ImGuiID value,bool hovered,bool dragging,
    ImVec2 topAnchor,ImVec4 accent);
// Synchronize the first return frame with the host's restored pickup position.
void SetGraphCursorReleaseTarget(ImVec2 position);
bool RenderGraphCursor(bool focused=true,bool pointerCaptured=false,bool softwarePreview=false);
struct GraphCursorSnapshot {
    bool visible=false, adjusting=false, returning=false;
    ImVec2 position{};
    float morph=0,glow=0,rotation=0;
    bool nativeDot=false;
    float scale=1;
    ImVec4 dotColor{},glowColor{};
};
GraphCursorSnapshot GetGraphCursorSnapshot();
}
