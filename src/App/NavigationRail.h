#pragma once

#include "imgui.h"
#include "Animation/ControlMotion.h"
#include "Library/LibraryControls.h"
#include <cstdint>

namespace Stack::Navigation {

// Shell presentation only. Project documents and jobs keep their existing owners.
struct RailState {
    Stack::Animation::ControlMotion stackMenuMotion, libraryMotion, queueMotion;
    Stack::Animation::ControlMotion panelToggleMotion, activityMotion;
    bool panelOpen = true;
    bool galleryView = true;
    LibraryUi::ViewSwitchMotion libraryViewMotion;
    float panelAmount = 1.0f;
    float revealAmount = 0.0f;
    double visibleUntil = 0.0;
    float scale = 1.0f;
    ImVec2 position{};
    float visibleWidth = 0.0f;
    ImVec2 panelPosition{}, panelSize{}, bodyPosition{}, bodySize{};
    float visiblePanelWidth = 0.0f;
    float galleryPanelWidth = 288.f;
    float queuePanelWidth = 288.f;
    float galleryResizeStartWidth = 288.f, galleryResizeStartX = 0.f;
    float galleryResizeOriginalWidth = 288.f;
    bool galleryResizeActive = false;
    bool galleryResizeForQueue = false;
    int requestedDestination = -1;
    std::uint64_t requestedWorkspace = 0;
    std::uint64_t requestedLoadGeneration = 0;
    std::uint64_t lastEditingWorkspace = 0;
    std::uint64_t hoveredProject = 0;
    std::uint64_t previewProject = 0;
    std::uint64_t suppressedPreview = 0;
    int previewRootView = -1;
    double hoverStarted = 0.0;
    float previewAmount = 0.0f;
    bool menuOpen = false;
};

enum class Glyph { Panel, Stack, Raw, Graph, Library, Queue, Mask, Capture };
void DrawGlyph(ImDrawList* draw, Glyph glyph, ImVec2 minimum, float size, ImU32 color);
void RegisterGalleryNavigatorSettings(RailState& state);
void RenderGalleryNavigatorResize(RailState& state);
void RenderQueueNavigatorResize(RailState& state);
void FinishGalleryNavigatorResize(RailState& state);

} // namespace Stack::Navigation
