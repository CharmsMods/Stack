# Next Step: Render Phase 2 Skeleton And Compute Viewport

**Summary**
- Prioritize `Render` Phase 2 next, not more Editor-layer ports.
- Move the app’s GL baseline to **OpenGL 4.3 core** now so the Render tab can own a real compute-driven preview path.
- Keep this phase strictly to the **renderer skeleton and scene backbone**: runtime types, compute preview, accumulation/reset rules, and Render Manager job plumbing. Do **not** start BVH, path tracing, denoising, asset import, glass, mist, or caustics yet.

**Key Changes**
- **Platform / GL baseline**
  - Update `AppShell` startup to request an OpenGL 4.3 core context and update the ImGui OpenGL backend shader version accordingly.
  - Extend the existing custom `GLLoader` instead of replacing it: add compute-shader, SSBO, image load/store, texture storage, and memory-barrier symbols needed for this phase.
  - Treat 4.3+ as a hard requirement for this phase: if the context or required symbols are unavailable, fail startup with a clear error instead of adding a fallback renderer path.
- **Render runtime skeleton**
  - Add render-local runtime types under `src/RenderTab/Runtime/` for `Scene`, `Camera`, `RenderSettings`, `RenderBuffers`, and `RenderJob`.
  - Keep these types deliberately small:
    - `Scene`: placeholder scene metadata, environment/background mode, version counter.
    - `Camera`: view parameters that are enough to drive reset behavior and future ray generation.
    - `RenderSettings`: resolution, preview sample target, accumulation toggle, display mode.
    - `RenderBuffers`: compute output textures/images, sample counter, resize/recreate rules.
    - `RenderJob`: single active preview job state only for now (`Idle`, `Queued`, `Running`, `Canceled`, `Completed`, `Failed`).
  - Add one render-local device/driver object, e.g. `ComputePreviewRenderer`, that owns compute program creation, dispatch, and output presentation into the Viewport panel.
- **Render tab behavior**
  - Replace the current static placeholder viewport with a deterministic compute result written into a texture each frame.
  - The compute output should stay simple and stable: gradient/checker/grid style output with visible sample count and resolution response, not transport logic.
  - Wire the existing panels to real runtime state:
    - `Outliner`: show the root scene and default camera.
    - `Inspector`: edit the selected camera’s basic parameters only.
    - `Settings`: edit renderer-wide preview settings only.
    - `Render Manager`: start, cancel, reset, and report one preview job.
    - `Statistics`: show sample count, dispatch dimensions, buffer size, and reset count.
  - Keep `Console`, `AOV / Debug`, and `Asset Browser` present but limited to skeleton-era placeholders unless the runtime state naturally gives them simple data.
- **Accumulation / reset rules**
  - Add explicit “dirty” versioning so preview accumulation resets when:
    - camera parameters change
    - resolution changes
    - render settings that affect output change
    - scene version changes
  - Do not add implicit heuristics; use clear revision counters and a single reset path.
  - The viewport should resize correctly and recreate render buffers only when dimensions actually change.
- **Render Manager scope**
  - Implement **one active preview job**, not a full preview/final queue yet.
  - The panel should expose: `Start Preview`, `Cancel Preview`, `Reset Accumulation`, current status, current sample count, and last reset reason.
  - Defer final-job presets, EXR, denoise, and job history to later phases.
- **Docs / tracking**
  - Update `DEV_LOG.md`, `AI_CONTEXT.md`, `README.md`, and the checklist file when this phase lands.
  - Record that OpenGL 4.3+ became a deliberate baseline for Render work, and that Phase 2 still excludes transport features.

**Public Interfaces / Types**
- New render-local runtime contracts:
  - `RenderScene`
  - `RenderCamera`
  - `RenderSettings`
  - `RenderBuffers`
  - `RenderJob`
  - `ComputePreviewRenderer`
- `RenderTab` gains ownership of the runtime skeleton and reset coordination.
- `GLLoader` and app initialization gain compute-era OpenGL requirements and symbols.

**Test Plan**
- Build with `cmake --build Stack/build --config Release`.
- Launch verifies the app creates a 4.3+ context; failure path is explicit and understandable if unsupported.
- `Render` Viewport shows compute output, not the old static placeholder, and resizes correctly.
- Sample count advances while preview runs and resets to zero on camera, settings, and resolution changes.
- `Render Manager` can start preview, cancel it, and report status transitions correctly.
- `Editor` and `Library` still open and render normally after the GL baseline upgrade.
- Render panels still dock, detach, reopen, and persist their visibility/layout as in Phase 1.

**Assumptions**
- Chosen priority: `Render Phase 2` now, not more Editor-layer work first.
- Chosen GL policy: **bump to 4.3+ now** with no fallback path in this phase.
- This phase keeps the custom loader architecture and extends it rather than swapping to a new GL loading library.
- This phase implements only a **single preview job** and only a **deterministic compute preview**, not transport, BVH, materials, denoise, import, or output pipeline features.
