# Modular Studio Stack

`Stack/` is the native C++ rebuild of the current web-based Modular Studio editor.

This repository slice should be treated as an in-progress migration project, not as a frozen native product spec. The job here is to carry the web Studio's editor functionality into a C++/OpenGL/ImGui environment, adapting implementation details where browser-specific systems do not apply. `Stitch` is intentionally out of scope for this native build.

## Working Rules

- Organization: For any new implementation, fix, update, or change, keep the project organized first. Decide whether the work needs its own subsystem/engine or should extend an existing file cleanly. Prefer the option that keeps the structure maintainable.
- Responsiveness: When adding or adapting features, actively balance new functionality with worker-backed CPU/IO execution where practical so long-running work does not lock the ImGui/OpenGL UI. Keep GL resource creation and render passes on the main thread unless a shared-context rendering subsystem is intentionally introduced.

## Current Focus

The active migration target is the web Editor pipeline:

- sequential image-processing layers
- native ImGui inspector and pipeline UI
- OpenGL fullscreen-pass rendering
- native project/library persistence
- worker-backed responsiveness for current CPU/IO-heavy native flows

Alongside that migration track, the new `Render` tab has started its own phased build. It now has a runtime skeleton plus a snapshot-capable, fly-camera-capable, always-live mesh-instancing geometry/BVH foundation, the first visible path-trace kickoff slice, a first shared-material/emissive-light baseline, a first thin-lens depth-of-field camera slice, a true raster authoring path with a real selection outline, the first worker-backed static glTF import slice, and now the first bounded smooth-glass dielectric milestone on top of that shared material/runtime path. Later optics and output features are still intentionally deferred.

The web app remains the feature reference:

- `../index.html`
- `../Important Sources/Whole Site Context.txt`
- `../src/registry/layerRegistry.json`
- `../Shaders/`

## Native Progress Snapshot

Editor systems already ported:

- sequential ping-pong FBO render pipeline
- layer add/remove/reorder flow
- selected-layer inspector
- viewport zoom/pan/compare
- scopes panel
- worker-backed source-image decode with main-thread apply
- worker-backed rendered PNG export with main-thread capture
- native library loading and project persistence scaffolding
- binary `.stack` project persistence and `.stacklib` whole-library bundle import/export
- fullscreen library project inspection with before/after compare, rename/delete actions, automatic library refresh while the library tab is open, and saved-render-backed `after` previews with native rerender fallback when needed
- worker-backed library project load, fullscreen preview prep, asset preview prep, and library import/export requests
- Library `Assets` tab for saved rendered outputs, including fullscreen preview and native download
- optimized Library save packaging so only the required render snapshot stays on the main thread while thumbnail generation, source-image packaging, and file writes continue in the worker path
- fullscreen Library project previews now prefer the saved rendered asset for the `after` pane and only use a native rerender when that saved render is unavailable
- a dedicated `Render` tab shell under `src/RenderTab` with a panel registry, default dock layout seeding, Window menu, toolbar toggles, and local JSON-backed panel visibility persistence
- the old `3D Studio` placeholder has been removed in favor of the new `Render` workspace shell
- the Render tab now owns a Phase 2 runtime skeleton (`RenderScene`, `RenderCamera`, `RenderSettings`, `RenderBuffers`, `RenderJob`, `ComputePreviewRenderer`) and a deterministic compute-driven preview viewport
- the Render tab now also owns validation scene templates, editable local-space scene primitives with per-object transforms, built-in mesh definitions with transformed mesh instances, a flat CPU-built BVH, and debug preview modes that exercise brute-force versus BVH traversal without starting path tracing yet
- the Render Asset Browser now supports saving and loading `.renderscene` snapshots so edited validation scenes and custom mesh-backed scene states can round-trip locally
- the Render viewport now supports direct fly-camera navigation with `RMB` look, `WASD`/`Q`/`E` movement, shift/ctrl speed modifiers, camera-position snapshot persistence, and a soft sample goal that no longer stops the live viewport loop
- the Render tab now also includes a first Phase 4 kickoff `Path Trace Preview` mode with diffuse multi-bounce accumulation, bounce-limit controls, and an external compute shader file at `src/RenderTab/Shaders/ComputePreview.comp`, while `Debug Preview` remains available for geometry/traversal validation
- the Render runtime now also owns shared scene materials, primitive material assignment/tinting, emissive-material support in the path-trace preview, and a built-in `Emissive Showcase` validation scene so the baseline transport path is no longer limited to raw debug-colored surfaces
- the Render path-trace baseline now also includes realism-isolation controls: `World Light` can be toggled from Render settings, `Background Mode` now includes `Black`, and `Emissive Showcase` defaults to that black background so box-scene shadow/bounce testing is easier to judge
- the Render baseline now also exposes first-pass opaque material controls for `roughness` and `metallic`, and the path-trace preview now performs direct emissive-light sampling for the built-in light geometry so box-scene realism checks converge more meaningfully before dielectric work starts
- the Render camera/runtime path now also includes a first thin-lens depth-of-field slice: `Focus Distance` and `Aperture Radius` drive lens-sampled path-trace camera rays, persist through `.renderscene` snapshots, and are backed by a dedicated `Depth Of Field Study` validation scene
- realism-focused validation scenes such as `Emissive Showcase` and `Depth Of Field Study` now default the Render `World Light` toggle off when they are selected, so one-light box-scene testing starts without the earlier directional world-light contribution
- the Render baseline now also includes a first stable path-trace AOV slice through `Display Mode`, with `Albedo`, `World Normal`, `Depth`, `Material ID`, and `Primitive ID` displays available alongside the existing beauty/luminance/sample-tint views
- the Render baseline lighting path now also uses stronger shared direct-light weighting: direct sun now flows through the same opaque diffuse/glossy BSDF response as the bounce path, and emissive-light sampling now uses MIS-style weighting against the BSDF pdf instead of the earlier always-add naive path
- the Render baseline inspection path now also includes a convergence-view slice: `RenderBuffers` carries a per-pixel moment texture, and `Display Mode` now exposes `Sample Count AOV` and `Variance AOV` alongside the earlier beauty/material/debug inspection views
- the Render baseline world-light path is now decoupled from the earlier debug-background coupling: `RenderSettings` now exposes separate environment intensity plus sun intensity/azimuth/elevation controls, and the path-trace preview uses those controls for miss lighting and direct sun instead of borrowing `Background Mode`
- the Render baseline world-lit path now also includes a dedicated `Sun / Sky Study` validation scene plus a first direct-environment sampling pass, so outdoor/world-lit scenes are no longer limited to miss lighting plus the earlier direct-sun shortcut
- the Render authoring path now also includes `Raster Preview`, durable object-id selection, viewport click-picking, first translate/rotate/scale gizmos with world/local axis control, and duplicate/delete actions that all update the same shared scene/runtime objects used by the path tracer
- the Render authoring path now also includes a dedicated raster renderer with real color/depth/object-id targets plus a true geometry-derived selection outline, so selected objects are outlined directly instead of being wrapped only by a projected bounds box
- the Render Asset Browser now also supports worker-backed static glTF 2.0 import for `.gltf` and `.glb`, creating imported asset records, imported textures, textured shared materials, reusable mesh definitions, and scene mesh instances on the same runtime path as the validation scenes
- the Render material/runtime path now also carries first-pass imported texture references for base color, metallic-roughness, emissive, and normal maps; raster preview samples all four while the current path-trace preview samples base color, metallic-roughness, and emissive textures from shared texture-array storage
- `.renderscene` snapshots now also round-trip imported asset metadata, imported texture records, textured materials, stable object ids, and imported mesh-instance bindings instead of treating imported scenes as validation-scene-only flattened content
- the Render material/runtime path now also carries first-pass dielectric fields (`transmission`, `ior`), and `.renderscene` snapshots now round-trip those fields alongside the existing textured-material state
- `Path Trace Preview` now includes the first bounded smooth-glass transport slice with dielectric Fresnel, Snell refraction, total internal reflection, inside/outside handling from geometric normals, and direct-light visibility that skips through clear glass instead of treating it as a black blocker
- the Render validation-scene browser now also includes `Glass Slab`, `Window Pane`, and `Glass Sphere Study` so first-dielectric sanity checks live in the app beside the earlier emissive/world-light studies
- `Raster Preview` intentionally keeps dielectric-tagged objects on the stable opaque authoring path for this phase, while accurate glass remains a `Path Trace Preview` responsibility
- the Render baseline preview path now also includes bounded beauty-view tonemapping controls: `Preview Tonemap` supports `Linear Clamp`, `Reinhard`, and `ACES Film`, and only the path-trace beauty-family views use that step while debug and inspection AOVs stay raw for analysis
- OpenGL 4.3 core is now the deliberate baseline for native Render work

Editor layers currently ported:

- `Crop / Rotate / Flip`
- `Adjustments`
- `Blur`
- `Noise`
- `Vignette & Focus`
- `3-Way Color Grade`
- `HDR Emulation`
- `Chromatic Aberration`
- `Lens Distortion`
- `Tilt-Shift Blur`
- `Dithering`
- `Cell Shading`
- `Compression`
- `Heatwave & Ripples`
- `Palette Reconstructor`
- `Edge Effects`
- `Airy Bloom`
- `Image Breaks`

Important missing editor work still lives in the web registry, including:

- `Text Overlay`
- `Generator`
- `Background Remover & Patcher`
- `Expander`
- additional non-Stitch editor effects from the web app

Two native adaptation notes matter here:

- `Palette Reconstructor` is currently layer-local in native instead of using the web app's shared Studio palette and palette-extraction flow, and its native inspector now grows via `Add Color` with per-color removal.
- `Airy Bloom` currently ports the core diffraction bloom pass, while the web layer's optional mask-gating path still waits on a broader native masking system.

## Build

Configure:

```powershell
cmake -S Stack -B Stack/build
```

Build:

```powershell
cmake --build Stack/build --config Release
```

Run:

```powershell
./Stack/build/bin/Release/ModularStudioStack.exe
```

## Structure

- `src/App`
  - native shell and module routing
- `src/Editor`
  - current primary migration target
- `src/Library`
  - native project library, rendered asset browser, and persistence
- `src/RenderTab`
  - phased Render workspace module, including panel registry, dock layout helper, `Render/render_tab_state.json` persistence, runtime skeleton types, `.renderscene` snapshot serialization, transform-aware validation-scene and mesh-instancing BVH data, shared materials/emissive surfaces, interactive fly-camera controls, a dedicated raster authoring renderer with true selection outlining, worker-backed static glTF import, external shader sources, and both raster-preview and compute-preview/path-trace orchestration
- `src/Persistence`
  - custom binary `.stack` / `.stacklib` format engine
- `src/Renderer`
  - OpenGL pipeline and helper code

## Documentation Rule

Keep the Stack docs migration-oriented.

They should explain:

- what part of the web Studio has been adapted
- how it was adapted for native
- where worker-backed background execution was added to keep the UI responsive
- what still needs to move over next

They should not read like the current native build is already complete or architecturally locked.
