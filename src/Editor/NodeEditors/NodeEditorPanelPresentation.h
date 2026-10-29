#pragma once
struct ImGuiWindow;
namespace Stack::Editor {
struct NodeEditorPanelGroup;
// Returns an explicitly activated instance, or -1 for an unchanged header.
int DrawControlsPanelHeader(const NodeEditorPanelGroup& group, float width);

// Smooth native wheel requests before child layout; pair with EndChild().
bool BeginControlsPanelBody();

// Native scrollbar behavior drawn over the panel seam, never a layout gutter.
void DrawControlsPanelScrollbar(ImGuiWindow* body, float seamX);
}
