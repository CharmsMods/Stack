#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace Stack::Editor::RawLabInternal {

inline constexpr float kRawLabFilmstripMinimumHeight = 180.0f;
inline constexpr float kRawLabFilmstripMaximumHeight = 280.0f;
inline constexpr float kRawLabFilmstripTileHeight = 104.0f;
inline constexpr float kRawLabLowerShelfDefaultHeight = 180.0f;
inline constexpr float kRawLabFloatingScopesHeight = 112.0f;
inline constexpr float kRawLabFloatingScopesGap = 8.0f;

float AnimateRawLabDrawerHeight(
    float current,
    float target,
    float deltaTimeSeconds);

void LabTooltip(const char* text, ImGuiHoveredFlags flags = 0);

bool FramelessTextButton(
    const char* label,
    bool active = false,
    bool enabled = true);

bool BareTextButton(
    const char* label,
    bool active = false,
    bool enabled = true,
    const ImVec2& size = ImVec2(0.0f, 0.0f));

bool BypassEyeButton(
    const char* id,
    bool enabled,
    const char* enabledTooltip,
    const char* bypassedTooltip);

// Continue a compact button row only when the next button fits.
void ContinueRawLabControlRow(const char* label, float spacing = 2.0f);

bool BareSliderFloat(
    const char* label,
    const char* id,
    float* value,
    float minimum,
    float maximum,
    const char* format,
    float width = 0.0f);

bool BareSliderInt(
    const char* label,
    const char* id,
    int* value,
    int minimum,
    int maximum,
    const char* format,
    float width = 0.0f);

ImVec2 CompactLabGraphSize(
    float reservedHeight,
    float minimumHeight,
    float maximumHeight);

bool BareToolIslandButton(
    const char* label,
    bool active,
    float horizontalPadding = 7.0f);

ImVec2 LabPopupAnchorBelowButtonText(
    const ImRect& buttonBounds,
    const char* label);

ImVec2 FitLabImage(float width, float height, const ImVec2& bounds);

} // namespace Stack::Editor::RawLabInternal
