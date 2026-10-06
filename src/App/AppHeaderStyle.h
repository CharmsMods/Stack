#pragma once

#include "imgui.h"

struct ImRect;

namespace Stack::Header {
inline constexpr float CaptionHeight = 44.0f;
inline constexpr float TabTop = 6.0f;
inline constexpr float TabHeight = CaptionHeight - TabTop;
inline constexpr float ControlHeight = 30.0f;
inline constexpr float ControlTop = (CaptionHeight - ControlHeight) * 0.5f;
inline constexpr float PreferredTabWidth = 232.0f;
inline constexpr float MinimumTabWidth = 132.0f;
inline constexpr float CloseTargetWidth = 32.0f;

struct Palette {
    ImVec4 caption, activeTab, text, mutedText, hover, pressed, selected, focus;
};

Palette ResolvePalette(ImVec4 workspace, bool windowFocused = true);
// Fully expanded background tint. Fade it with the panel's animated expansion;
// text, icons and widget feedback keep their own opacity.
inline constexpr float SectionTintOpacity = .64f;
float ResolveSectionTintOpacity(float expandedAmount);
ImVec4 ResolvePanelColor(ImVec4 workspace, float opacity = 1.0f);
ImVec4 Blend(ImVec4 from, ImVec4 to, float amount);
void Approach(float& value, float target, float speed = 20.0f);
ImRect AlignToPixels(const ImRect& rect);
void DrawActiveTab(ImDrawList* draw, const ImRect& rect, ImU32 color);
void DrawControlFeedback(const ImRect& rect, const Palette& palette,
    float selected, bool hovered, bool held, bool focused);
}
