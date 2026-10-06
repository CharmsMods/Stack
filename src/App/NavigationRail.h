#pragma once

#include "imgui.h"
#include <cstdint>

namespace Stack::Navigation {

// Shell presentation only. Project documents and jobs keep their existing owners.
struct RailState {
    bool panelOpen = true;
    bool galleryView = true;
    float panelAmount = 1.0f;
    float revealAmount = 0.0f;
    double visibleUntil = 0.0;
    float scale = 1.0f;
    ImVec2 position{};
    float visibleWidth = 0.0f;
    ImVec2 panelPosition{}, panelSize{}, bodyPosition{}, bodySize{};
    float visiblePanelWidth = 0.0f;
    int requestedDestination = -1;
    std::uint64_t lastEditingWorkspace = 0;
    std::uint64_t hoveredProject = 0;
    std::uint64_t previewProject = 0;
    std::uint64_t suppressedPreview = 0;
    int previewRootView = -1;
    double hoverStarted = 0.0;
    float previewAmount = 0.0f;
    bool menuOpen = false;
};

enum class Glyph { Panel, Stack, Raw, Graph, Library, Queue, Mask };
void DrawGlyph(ImDrawList* draw, Glyph glyph, ImVec2 minimum, float size, ImU32 color);

} // namespace Stack::Navigation
