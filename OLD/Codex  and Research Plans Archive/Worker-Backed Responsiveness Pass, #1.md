# Worker-Backed Responsiveness Pass

## Summary
Add a reusable async job subsystem and convert the current blocking CPU/IO-heavy flows to request-based background work, while keeping all OpenGL object creation, framebuffer work, and layer-pipeline execution on the main thread for this pass. The goal is to stop Library and Editor workflows from freezing the UI during file reads, image decode/encode, bundle import/export, and project-load preparation, and to update the docs so future work explicitly balances feature growth with responsiveness and organization.

## Implementation Changes
### Async architecture
- Add a small shared async subsystem under a dedicated cross-module area such as `src/Async/`.
- Use a bounded worker pool sized to `max(2, min(4, hardware_concurrency - 1))`.
- Expose:
  - background job submission for CPU/IO work
  - a main-thread completion queue
  - per-request generation IDs so stale completions can be discarded
  - a lightweight task-state enum: `Idle`, `Queued`, `Running`, `Applying`, `Failed`
- Pump main-thread completions once per frame from `AppShell` before module UI renders.

### Library and Editor request flows
- Replace UI-triggered blocking Library actions with request-based async flows:
  - project load into editor
  - fullscreen project preview preparation
  - fullscreen asset preview preparation
  - library import
  - library export
  - save-to-library final packaging
- Keep low-level synchronous helpers private for worker/main-thread internals only.

### Main-thread vs worker split
- Worker thread responsibilities:
  - read `.stack` / `.stacklib` files
  - parse binary sections
  - decode PNG/source image bytes to RGBA
  - encode PNG bytes
  - build thumbnails from CPU pixel buffers
  - write project, asset, and library bundle files
- Main-thread responsibilities:
  - create/delete GL textures and FBOs
  - call `RenderPipeline::Execute(...)`
  - call `glReadPixels`
  - initialize GL-backed layers
  - apply decoded project payloads into `EditorModule`
- Do not introduce a shared OpenGL worker context in this pass.

### Concrete behavior changes
- Project load from Library:
  - worker reads/parses/decode project source image and pipeline payload
  - main thread applies pixels, rebuilds layers, updates current project metadata, then switches to the Editor tab only after apply succeeds
- Fullscreen project preview:
  - worker loads project document, decodes source image, prepares pipeline summary payload
  - main thread uploads source preview texture and performs the actual preview rerender
  - latest preview request wins; stale results are ignored
- Fullscreen asset preview:
  - worker decodes asset image
  - main thread uploads the texture
- Save/export:
  - main thread captures only the required GL-bound snapshot data
  - worker handles thumbnail generation, PNG encoding, `.stack` writing, asset writing, and bundle writing
- Source image load from the Canvas tab:
  - decode on worker
  - upload/apply on main thread

### UI status and coalescing
- Add clear busy states and disable conflicting actions while jobs are active.
- Show visible status in:
  - Library header for import/export/save
  - fullscreen Library project and asset windows
  - Canvas load/save/export controls
- Use “latest intent wins” semantics for preview/open requests:
  - each surface keeps its latest request generation
  - completed stale jobs are dropped without touching UI state

### Public interfaces and data updates
- `LibraryManager` should expose request-oriented APIs plus read-only task status access for the UI.
- `EditorModule` should gain a single main-thread apply path for decoded project payloads instead of UI code directly invoking a blocking load routine.
- `ProjectEntry` and `AssetEntry` should gain explicit async status fields and request generation counters; reuse their existing preview flags where practical instead of creating parallel ad hoc state.

### Documentation updates
Add a short explicit rules section to `AI_CONTEXT.md`, `README.md`, `DEV_LOG.md`, and keep the top of `CurrentFixesUpdatesIdeas.md` aligned with it.

Use this wording as the rule baseline:
- Organization: For any new implementation, fix, update, or change, keep the project organized first. Decide whether the work needs its own subsystem/engine or should extend an existing file cleanly. Prefer the option that keeps the structure maintainable.
- Responsiveness: When adding or adapting features, actively balance new functionality with worker-backed CPU/IO execution where practical so long-running work does not lock the ImGui/OpenGL UI. Keep GL resource creation and render passes on the main thread unless a shared-context rendering subsystem is intentionally introduced.

Update the current migration notes so they explicitly say the next development phase must balance remaining feature parity work with responsiveness work, rather than treating new features and UI smoothness as separate concerns.

## Test Plan
- Build: `cmake --build Stack/build --config Release`
- Manual verification:
  - open a Library project into the Editor while moving/resizing the app and confirm the UI stays responsive
  - open fullscreen project preview and asset preview repeatedly; only the latest clicked item should finish and display
  - rapidly click different projects/assets and confirm stale jobs do not overwrite the newest preview
  - import and export a large library bundle and confirm progress/busy state is visible and the app remains interactive
  - save/export from the Editor and confirm the UI stays responsive except for the unavoidable main-thread GL snapshot step
  - load a new source image from Canvas and confirm decode no longer blocks the UI
  - close preview windows or switch tabs while jobs are running and confirm there is no crash or invalid state apply
  - test corrupted/missing `.stack`, `.stacklib`, and asset files and confirm failures surface as status text instead of hangs or crashes

## Assumptions
- “Broader refactor” means a reusable app-wide worker system plus async conversion of current blocking file/decode/encode flows, not a shared OpenGL worker-context rewrite.
- Clear status means visible spinners/progress labels plus disabled conflicting actions, not silent background work.
- Coalescing means the latest preview/open request replaces earlier ones; stale completions are ignored.
- The current single-window GLFW/OpenGL architecture remains in place for this pass.
