#pragma once
struct ImGuiWindow;
namespace Stack::Editor {
// Native scrollbar behavior drawn over the panel seam, never a layout gutter.
void DrawControlsPanelScrollbar(ImGuiWindow* body, float seamX);
}
