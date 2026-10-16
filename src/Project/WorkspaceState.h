#pragma once

#include <string_view>

namespace Stack::Project {

enum class WorkspaceMode { Graph, View };

// Presentation belongs to the project, not to application root-tab routing.
// Only the mode is persisted. Every image/project load closes View's panel;
// ordinary mode changes preserve View's panel choice. Graph always opens with
// its node library closed.
struct WorkspaceState {
    WorkspaceMode mode = WorkspaceMode::Graph;
    bool viewPanelOpen = false;
    bool graphPanelOpen = false;

    void Loaded(WorkspaceMode restoredMode = WorkspaceMode::View) {
        mode = restoredMode;
        viewPanelOpen = false;
        graphPanelOpen = false;
    }
};

inline constexpr const char* WorkspaceModeName(WorkspaceMode mode) {
    return mode == WorkspaceMode::View ? "view" : "graph";
}

inline bool ParseWorkspaceMode(std::string_view value, WorkspaceMode& mode) {
    if (value == "view") { mode = WorkspaceMode::View; return true; }
    if (value == "graph") { mode = WorkspaceMode::Graph; return true; }
    return false;
}

} // namespace Stack::Project
