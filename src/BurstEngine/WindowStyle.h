#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

namespace Stack::BurstEngine::WindowStyle {
// Paint only: native buttons retain focus, accessibility and check/radio behavior.
LRESULT Button(NMCUSTOMDRAW& draw, bool primary, unsigned dpi);
COLORREF Background();
COLORREF Foreground(bool secondary = false);
bool HighContrast();
}
