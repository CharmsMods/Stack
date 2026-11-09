#pragma once
namespace Stack::Navigation {
enum class ProjectMenuAction { None, Open, Close, ForceClose };
// Called immediately after the project item. Presentation only; the shell
// routes the returned command to the captured project identity between frames.
ProjectMenuAction DrawProjectMenu(const char* name, bool dirty, bool automatic,
    const char* busyReason, float scale);
}
