# Phase 1: Render Tab Foundation And Panel Lifecycle

**Summary**
- Treat `Stack/Codex  and Research Plans Archive/Companion Implementation Plan for the Render Tab.md` as the phase-order source of truth. Use `Building a GPU Path Tracing Render Engine with OpenGL and Dear ImGui.md` only as secondary guidance for future architecture choices such as single-renderer scope, UI-thread GL ownership, and Render Manager direction.
- Replace the current `3D Studio` placeholder with a real `Render` tab, but keep Phase 1 strictly UI/lifecycle-only. No transport code, no scene backbone, no BVH, no compute dispatch, no camera system.
- Use a lightweight shared tab-registry pattern in `AppShell` plus a dedicated `Stack/src/RenderTab/` module so the new tab is organized now and expandable later without entangling it with the current `Editor` pipeline.

**Implementation Changes**
- Add a dedicated `RenderTab` module under `Stack/src/RenderTab/` with repo-native naming, not the lowercase example from the archived plan. Core files should include `RenderTab.cpp/.h`, `RenderTabState.cpp/.h`, `Layout/RenderDockLayout.*`, `Panels/`, and `PanelRegistry.*`.
- Rename the root tab label from `3D Studio` to `Render` and replace the inline placeholder body in `AppShell` with a small tab-registration layer. Keep `Library` and `Editor` behavior intact; do not do a full app-wide `IAppModule` conversion in this phase.
- Create a `RenderTabState` type that owns:
  - persistent open/closed booleans for each panel
  - toolbar toggle state
  - a one-time “default layout applied” flag
- Define the panel set now and keep it stable:
  - visible by default: `Viewport`, `Outliner`, `Inspector`, `Settings`, `Render Manager`
  - hidden by default: `Statistics`, `Console`, `AOV/Debug`, `Asset Browser`
- Render every panel through the same lifecycle pattern: `ImGui::Begin("Panel Name", &isOpen, flags)`. The same state must drive title-bar close, Window-menu reopen, toolbar toggles, and restore on restart.
- Add a Render-only Window menu and toolbar strip inside the tab shell. Hidden panels must always be recoverable from those controls.
- Add a default dock layout helper for the Render tab only. First launch should seed the planned arrangement; later launches should preserve the user’s ImGui layout instead of re-forcing it.
- Use ImGui `.ini` persistence for docked/detached window layout and a Render-local JSON state file for panel visibility and toolbar state. Do not build app-wide UI persistence yet.
- Keep the Viewport as a deterministic placeholder only: dummy texture or gradient plus “shell-only” status text. Do not reuse `Editor`’s `RenderPipeline` or add path-tracing-era types here.
- Keep the archived render-plan docs unchanged. Track live progress in `Stack/CurrentFixesUpdatesIdeas.md`, `Stack/DEV_LOG.md`, `Stack/AI_CONTEXT.md`, and `Stack/README.md`.

**Public Interfaces / Types**
- `AppShell` gains a lightweight tab descriptor/registry instead of inline hardcoded tab bodies.
- New top-level module boundary: `RenderTab`.
- New Render-local state contract: `RenderTabState`.
- New panel identity/layout helpers: `PanelRegistry` and `RenderDockLayout`.
- Future renderer-facing types such as `Scene`, `Camera`, `RenderSettings`, `RenderBuffers`, and `RenderJob` are explicitly deferred to Phase 2.

**Test Plan**
- Build still passes with `cmake --build Stack/build --config Release`.
- `Render` replaces `3D Studio` in the root tabs without breaking `Library` or `Editor`.
- Each Render panel can be closed with the title-bar X, reopened from the Window menu, and toggled from the toolbar.
- Panels dock, undock, and detach into OS windows cleanly; at least `Viewport` and one hidden-by-default panel should be validated detached and redocked.
- After restart, the Render tab restores dock layout via ImGui and restores panel visibility/toolbar state via `RenderTabState`.
- Hidden-by-default panels remain recoverable.
- Documentation/checklists explicitly state that Render Phase 1 is complete and that no render-core/transport work has started yet.

**Assumptions**
- Chosen scope: `Shared registry` in `AppShell`, not a minimal patch and not a full module-system conversion.
- Chosen persistence: `Render-local state`, not app-wide UI state.
- The companion render plan controls ordering whenever it conflicts with the broader GPU renderer writeup.
- OpenGL 4.3+/compute upgrades, scene/runtime objects, accumulation reset rules, and actual Render Manager jobs belong to Phase 2, not this phase.
