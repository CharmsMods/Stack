# Render Handoff Status

Last updated: April 18, 2026

## Purpose

This file is the handoff document for the `Render` tab work. It is meant to let a new session or a new contributor pick up the current Render implementation without having to reconstruct the phase history from chat alone.

Use this file together with:

- `Stack/Codex  and Research Plans Archive/Companion Implementation Plan for the Render Tab.md`
- `Stack/Codex  and Research Plans Archive/Building a GPU Path Tracing Render Engine with OpenGL and Dear ImGui.md`
- `Stack/AI_CONTEXT.md`
- `Stack/DEV_LOG.md`
- `Stack/CurrentFixesUpdatesIdeas.md`

## Source Of Truth

Ordering and scope rules:

1. `Companion Implementation Plan for the Render Tab.md` is the phase-order source of truth.
2. `Building a GPU Path Tracing Render Engine with OpenGL and Dear ImGui.md` is secondary architecture guidance.
3. If those two documents disagree on ordering, follow the companion implementation plan.
4. Keep Render as one shared runtime inside the existing `Render` tab. Do not split work into parallel scene systems or separate renderer-specific data models.

## Current Status

### Phase 1: Render Tab Foundation

Status: complete

Implemented:

- `Render` root tab replaced the old `3D Studio` placeholder.
- Dedicated `src/RenderTab/` module exists.
- Panel lifecycle, docking, panel persistence, toolbar toggles, and Render-local state file exist.

### Phase 2: Runtime Skeleton And Compute Viewport

Status: complete

Implemented:

- OpenGL 4.3 baseline
- Render-local scene/camera/settings/buffers/job types
- Compute-driven viewport
- Reset and accumulation rules
- Render Manager baseline

### Phase 3: Scene / Geometry / BVH / Runtime Foundation

Status: complete in bounded slices

Implemented:

- validation scenes
- sphere and triangle scene content
- BVH debug traversal
- transform-aware geometry
- mesh instancing
- scene snapshot save/load
- fly camera controls

### Phase 4: Baseline Path Trace / Materials / Lighting / AOV / Tonemap

Status: complete in bounded slices

Implemented:

- baseline path-trace preview
- emissive material support
- world light toggle and black background option
- roughness and metallic controls
- basic emissive-light sampling
- depth of field
- AOV/debug display modes
- variance/sample-count inspection views
- separate sky/sun controls
- preview tonemap modes

### Phase 5A: Raster Authoring / Picking / Gizmos

Status: complete

Implemented:

- `Raster Preview`
- viewport picking
- translate/rotate/scale gizmos
- local/world transform mode
- duplicate/delete
- stable object ids for selection/manipulation

### Phase 5B: True Raster Path / Selection Outline / glTF Import

Status: complete

Implemented:

- dedicated raster renderer
- real geometry-derived selection outline
- static `.gltf` / `.glb` import
- imported mesh definitions, instances, materials, and textures
- `.renderscene` persistence for imported assets

### Phase 5C: First Smooth-Glass Dielectric Milestone

Status: complete

Implemented:

- `RenderMaterial` dielectric fields: `transmission`, `ior`
- dielectric transport branch in `ComputePreview.comp`
- Fresnel, Snell refraction, total internal reflection, inside/outside handling
- `Glass Slab`, `Window Pane`, and `Glass Sphere Study` validation scenes
- `.renderscene` persistence for dielectric fields
- inspector-side dielectric authoring controls
- **Stabilized (Fixed April 18):** Restored visibility in `Path Trace Preview` by fixing immediate `false` return in primitive intersection functions (`ComputePreview.comp`) and fixing a dangling reference in the BVH builder (`RenderBvh.cpp`).

## Recommended Next Step

Validate the visual behavior of the new glass scenes. Since `Path Trace Preview` is visible again, the focus should shift to confirming the correctness of the dielectric transport (refraction, TIR, Fresnel).

1. Confirm `Glass Slab`, `Window Pane`, and `Glass Sphere Study` produce plausible results.
2. Confirm `Mixed Debug`, `Emissive Showcase`, and `Sun / Sky Study` still render correctly as regression tests.
3. Once validated, proceed to Phase 5D: absorption / Beer-Lambert follow-up.

Practical validation priority:

- First confirm visibility and basic shading in `Mixed Debug` and `Emissive Showcase`.
- Then validate `Glass Slab`, `Window Pane`, and `Glass Sphere Study`.

## Important Files

Current hot spots for the active blocker:

- `Stack/src/RenderTab/Shaders/ComputePreview.comp`
- `Stack/src/RenderTab/Runtime/ComputePreviewRenderer.cpp`
- `Stack/src/RenderTab/Runtime/Debug/ValidationScenes.cpp`
- `Stack/src/RenderTab/Runtime/Materials/RenderMaterial.h`
- `Stack/src/RenderTab/Runtime/Materials/RenderMaterial.cpp`
- `Stack/src/RenderTab/Runtime/RenderSceneSerialization.cpp`
- `Stack/src/RenderTab/RenderTab.cpp`

Useful supporting files:

- `Stack/src/RenderTab/Runtime/RenderRasterPreviewRenderer.cpp`
- `Stack/src/RenderTab/Panels/RenderInspectorPanel.cpp`
- `Stack/src/RenderTab/Panels/RenderSettingsPanel.cpp`
- `Stack/src/RenderTab/Panels/RenderAovDebugPanel.cpp`
- `Stack/src/RenderTab/Panels/RenderStatisticsPanel.cpp`

## Remaining Work By Phase

### Finish Phase 5C

Required before Phase 5C can be considered done:

- restore visible geometry in `Path Trace Preview`
- confirm older opaque scenes still render correctly
- confirm glass scenes behave plausibly
- confirm raster remains stable for glass-tagged objects
- confirm AOVs, tonemap, world-light controls, picking, gizmos, outline, duplicate/delete, import, and `.renderscene` save/load still work

### After Phase 5C

Only after Phase 5C is stable:

- absorption / Beer-Lambert follow-up
- rough / frosted transmission
- later optics work such as caustics, spectral features, and media/mist
- later denoise / output-pipeline work

## Organization Rules

These rules should stay in force for future Render work.

### Architecture Rules

1. One Render runtime only.
2. One shared scene model only.
3. One shared material model only.
4. Extend the existing `Render` tab architecture; do not fork separate glass/raster/import scene systems.
5. Keep GPU execution on the main/UI GL thread. Use worker threads only for CPU or file-heavy tasks such as import parsing and image decode.

### Scope Rules

1. Work in bounded slices with explicit stop rules.
2. Do not jump to later optics while the current slice is unstable.
3. Do not add “temporary” parallel systems just to move faster.
4. Prefer permanent validation scenes for each milestone instead of ad hoc tests.
5. Treat successful C++ builds and successful runtime shader behavior as separate validation steps.

### Code Organization Rules

1. Keep Render code under `Stack/src/RenderTab/`.
2. Keep subsystem boundaries clear:
   - `Runtime/`
   - `Panels/`
   - `Shaders/`
   - `Layout/`
3. New features should extend existing shared types where possible:
   - `RenderScene`
   - `RenderCamera`
   - `RenderSettings`
   - `RenderBuffers`
   - `RenderJob`
   - `RenderMaterial`
4. Prefer stable ids over list indices for anything user-selectable or persistent.

### Documentation Rules

When a phase meaningfully changes, update:

- `Stack/AI_CONTEXT.md`
- `Stack/DEV_LOG.md`
- `Stack/README.md`
- `Stack/CurrentFixesUpdatesIdeas.md`

Do not leave major Render changes tracked only in chat.

### Reliability Rules

1. Build with:

   `cmake --build Stack/build --config Release`

2. For shader-heavy work, always validate both:
   - C++ build success
   - runtime app behavior
3. Treat “no shader compile errors” as necessary but not sufficient.
4. Use older stable scenes as regression checks before trusting a new scene.
5. If raster works and path trace does not, prioritize compute path debugging before adding features.

## Handoff Summary

If work resumes in another session, the correct first move is:

1. Read this file.
2. Read the companion implementation plan.
3. Treat the current task as **Phase 5C stabilization**, not new feature development.
4. Restore visible geometry in `Path Trace Preview` on older opaque scenes first.
5. Only after that, finish validating the glass milestone and continue to the next optics slice.
