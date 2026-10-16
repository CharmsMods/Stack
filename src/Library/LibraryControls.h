#pragma once

#include <imgui.h>

namespace Stack::LibraryUi {

enum class Icon { Gallery, Projects, Bracket, Sort, View, More, Details,
    Folder, OpenFolder, Filmstrip, SavedEdits, AllFolders, MoreHorizontal, ChevronDown };

struct ViewSwitchMotion {
    float leading = -1.0f;
    float trailing = -1.0f;
};

// Presentation only: callers own navigation, action eligibility and execution.
bool ReducedMotion();
void DrawIcon(Icon icon, ImVec2 origin, float size, ImU32 color);
bool IconButton(const char* label, Icon icon, bool selected = false, bool enabled = true);
bool TextButton(const char* label, bool enabled = true, const ImVec2& size = ImVec2());
// Returns the requested view, or -1 when neither segment was pressed.
int ViewSwitch(bool gallery, ViewSwitchMotion& motion, bool enabled, float scale = 0.f);

} // namespace Stack::LibraryUi
