#pragma once

namespace Stack::Editor::RawLabInternal {
// Returns the requested mode, or -1 when no button was pressed.
int RenderRawLabModeSwitcher(int selectedMode, bool enabled,
    bool showBracket, bool showEdit, int editCount = 0,
    const char* bracketLabel = "Bracket", bool localLayout = false);
}
