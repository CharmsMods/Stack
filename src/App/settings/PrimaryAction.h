#pragma once
#include "CreamPalette.h"
namespace StackAppearance {
// Scope both foreground and background. No color survives the button call.
class ScopedPrimaryActionStyle {
public:
    explicit ScopedPrimaryActionStyle(const SurfaceColors& colors);
    ~ScopedPrimaryActionStyle();
    ScopedPrimaryActionStyle(const ScopedPrimaryActionStyle&) = delete;
    ScopedPrimaryActionStyle& operator=(const ScopedPrimaryActionStyle&) = delete;
private:
    bool disabled_ = false;
};
bool PrimaryActionButton(const char* label, const ImVec2& size = ImVec2(0,0),
    const SurfaceColors* colors = nullptr);
}
