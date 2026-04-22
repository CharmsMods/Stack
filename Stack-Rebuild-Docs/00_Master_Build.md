# Stack Master Build Plan

## Architecture Goal
The goal of "Stack" is to completely recreate the "Modular Studio V2" web application as a native C++ application using OpenGL and ImGui.

## Core Directives
1. **Shell and Main UI First:** The build process must first construct at least most of the main App Shell UI to house everything. We will not try to constantly expand the application with features before the shell is solid. 
2. **Phased Section Integration:** Once the shell is built, work should proceed in separate, independent passes for each section of the program (e.g., Editor, Composite, 3D, Library, etc.).
3. **Modularity:** The codebase must be highly modular so that new things can always be dropped in later.
4. **Data-Driven UI:** UI panels, layouts, and properties should be expressed independently of the rendering engines.

## Sections (Tabs) Overview
Stack is a single application with the following top-level workspaces (Tabs):
- **Library:** File/project/asset management.
- **Editor:** The core layer-based image editing engine.
- **Composite:** 2D multi-element composition engine for arranging multiple assets.
- **3D:** Path-traced and rasterized 3D workspace.
- **Logs:** Runtime diagnostics and process streams.
- **Settings:** Persistent preferences for the entire application.

This documentation folder describes EXACTLY what each section does and what its layout requires.

## Documentation Structure
- `AppShell/` - Describes the main application container and state management.
- `Library/` - Browser, grid layouts, and item payloads.
- `Editor/` - The base canvas, layer stack, and right-panel settings.
- `Composite/` - Stage layout, selection handling, and multi-layer management.
- `3D/` - Viewport layout, outliner, properties, and path-tracing overlays.
- `Logs/` - Process cards and download controls.
- `Settings/` - Options UI, tabs, and limits.
