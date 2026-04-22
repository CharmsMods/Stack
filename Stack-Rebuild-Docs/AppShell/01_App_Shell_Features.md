# App Shell Design

## What It Is
The App Shell represents the overarching native window and routing mechanism for "Stack." It holds the single application state and provides the visual container and top-level navigation to switch between different "Tabs."

## Application Layout
- **Top Navigation Bar:** A persistent horizontal strip across the top of the window that holds the section buttons: Library, Editor, Composite, 3D, Logs, and Settings.
- **Central Workspace Area:** The remainder of the window where the selected Tab renders its specific UI and Canvas. 
- **Wait States & Visibility:** When a tab is switched away from, its elements are hidden from rendering, but its state remains resident in memory. For instance, the Editor canvas isn't destroyed when looking at the Library.

# App Shell Features

## What Can Be Done
- **Tab Switching:** The user navigates between entirely different domain workspaces without losing progress. There is no forced "Save before you switch" step when moving between Editor or 3D.
- **Modal Isolation:** If a blocking dialog (like "Save Replacement") is up, the background workspace is unclickable.
- **Window Resizing Behavior:** The UI inside the central workspace scales elastically (especially the canvas viewports) while side panels generally maintain fixed or clamped widths to preserve tool readability.

## Modularity Considerations
In C++, the shell must maintain an abstract "Active Context" pointer. If the user clicks "Editor", the rendering loop merely points `currentWorkspace` to the Editor object and asks it to draw its ImGui panes and OpenGL framebuffers. Making this purely modular means we can add a new Tab in the future without disturbing the rest.
