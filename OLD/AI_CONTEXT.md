# Modular Studio Stack - AI Migration Context

This file is a handoff for the native `Stack/` rebuild.

It is not a stability contract and it is not trying to describe the native app as "finished." The purpose is to keep the migration from the current web Studio into the C++/OpenGL/ImGui program easy to resume in later sessions.

## Migration Goal

Port the current web Studio's practical feature set into a native C++ application under `Stack/`, adapting behavior to fit:

- C++17
- OpenGL fullscreen-pass rendering
- Dear ImGui layout and controls
- the current native module split

Important scope rule:

- Move features and workflows, not exact web code.
- Preserve behavior where it matters.
- Adapt UI and implementation details when the web version depends on DOM, browser workers, or other browser-only systems.
- `Stitch` is not part of this native migration scope.

## Working Rules

- Organization: For any new implementation, fix, update, or change, keep the project organized first. Decide whether the work needs its own subsystem/engine or should extend an existing file cleanly. Prefer the option that keeps the structure maintainable.
- Responsiveness: When adding or adapting features, actively balance new functionality with worker-backed CPU/IO execution where practical so long-running work does not lock the ImGui/OpenGL UI. Keep GL resource creation and render passes on the main thread unless a shared-context rendering subsystem is intentionally introduced.

## Source Of Truth For What To Port

When deciding what the native app should gain next, read these first:

- `../index.html`
- `../Important Sources/Whole Site Context.txt`
- `../src/registry/layerRegistry.json`
- the matching shader files in `../Shaders/`

If the native code and these source references disagree, treat the native code as "implemented so far" and the web app as the migration target.

## Current Native Architecture

- `AppShell`
  - top-level module router and tab shell
- `EditorModule`
  - current main migration target
  - owns the layer stack, viewport, sidebar, and scopes
- `LibraryModule`
  - native project library/persistence surface
  - now includes fullscreen project inspection with rename/delete actions, automatic disk refresh while the library view is open, parsed pipeline summaries, rendered before/after preview compare, a real `Assets` browser for saved rendered outputs, and binary library import/export
- `RenderTab`
  - Phase 3-slice runtime shell for the new `Render` workspace that replaces the old `3D Studio` placeholder
  - owns the render-tab panel registry, dock-layout seeding, local panel-visibility persistence, runtime skeleton state, validation-scene selection, snapshot-capable transform-aware primitive plus mesh-instance BVH debug data, shared scene materials, interactive fly-camera navigation, a dedicated raster authoring renderer with true selection outlining, worker-backed static glTF import, and compute-preview / early path-trace orchestration
  - now also owns the first bounded smooth-glass dielectric slice in `Path Trace Preview`, including transmission/IOR material controls, first-pass refraction/TIR handling, direct-light visibility through clear glass, and built-in glass validation scenes
  - intentionally still stops short of absorption, frosted transmission, spectral dispersion, caustics, media/mist, denoise, final-output pipeline work, and the later optics-heavy renderer features
- `Async::TaskSystem`
  - shared worker-thread queue plus main-thread completion pump
  - handles CPU/IO-heavy decode, encode, project-load prep, preview prep, and library bundle work without freezing the UI shell
- `RenderPipeline`
  - sequential ping-pong FBO engine
  - layer `N + 1` always processes the output of layer `N`
- `StackBinaryFormat`
  - native binary persistence subsystem for `.stack` project files and `.stacklib` library bundle files
  - chunked sections keep metadata and thumbnails cheap to scan while full source image data stays available on demand

## Current Editor Migration Snapshot

These editor systems are in the native app and compiling:

- worker-backed source image decode with main-thread GPU apply
- sequential layer stack execution
- drag-and-drop layer reordering
- selected-layer inspector
- viewport zoom, pan, and compare behavior
- scopes panel
- pipeline serialization/deserialization
- worker-backed rendered PNG export with main-thread capture

These library/project-browser behaviors are now in the native app:

- project grid thumbnails
- automatic Library refresh while the Library tab is open
- resizable Library filters rail
- binary `.stack` project persistence with sectioned metadata/thumbnail/source/pipeline storage
- legacy JSON `.stack` compatibility while older projects migrate forward through native saves
- fullscreen project preview overlay
- fullscreen project preview using the saved rendered asset when available, with native rerender fallback when needed
- before/after compare against the original source image
- rename and delete actions from the fullscreen preview
- parsed layer/settings summary for saved editor pipelines
- rendered asset export into `Library/Assets` whenever a project save updates the Library
- `Assets` tab thumbnails, fullscreen asset preview, and asset download back out to disk
- whole-library `.stacklib` export/import from the Library header
- worker-backed project load into the Editor with main-thread project apply
- worker-backed fullscreen project preview prep and asset preview prep with latest-request-wins behavior
- visible async status for save/load/import/export flows so the Library and Canvas surfaces stay responsive while background work runs
- optimized Library save packaging so source-image reuse, thumbnail generation, and project/asset file writing stay off the main thread after the required render snapshot is captured
- fullscreen Library project previews now prefer the saved rendered asset for the `after` side and only fall back to native rerender when that saved render is unavailable
- source-image snapshots saved into `.stack` projects are normalized back to file-oriented row order so future Library `before` previews do not inherit the temporary save-orientation regression

These render-tab shell behaviors are now in the native app:

- the root `Render` tab now replaces the old `3D Studio` placeholder
- a dedicated `src/RenderTab/` module owns the tab instead of leaving its UI inline inside `AppShell`
- persistent panel visibility is stored in a local JSON state file at `Render/render_tab_state.json`
- the Render workspace now has a render-only Window menu, toolbar strip, dockspace, default layout seeding, and detachable panels
- the Phase 1 panel set is wired and restorable: `Viewport`, `Outliner`, `Inspector`, `Settings`, `Render Manager`, `Statistics`, `Console`, `AOV / Debug`, and `Asset Browser`
- the Render tab now owns a Phase 2 runtime skeleton: `RenderScene`, `RenderCamera`, `RenderSettings`, `RenderBuffers`, `RenderJob`, and `ComputePreviewRenderer`
- the Render viewport now shows a deterministic compute-driven preview texture instead of the old shell-only placeholder
- the Render runtime now also includes validation scene templates, editable spheres/triangles, local-space primitives with per-object transforms, built-in mesh definitions with transformed mesh instances, primitive references, and a CPU-built flat BVH for debug traversal
- the Render Asset Browser can now save and load `.renderscene` snapshots that round-trip the current Render scene plus camera into a custom-scene state path
- the Render camera now owns an explicit world-space position, supports direct fly navigation from the viewport (`RMB` look plus `WASD`/`Q`/`E` movement), and persists that position through new snapshots while remaining backward-compatible with earlier orbit-style snapshot files
- the Render viewport sample goal is now a soft progress marker instead of a hard stop, so the interactive viewport keeps updating after the goal is reached and Render Manager now functions as a live viewport pause/resume/reset surface
- the Render tab now also owns the first bounded Phase 4 kickoff slice: a visible diffuse multi-bounce path-trace preview mode with accumulation, bounce limits, BVH/brute-force traversal support, and a dedicated external compute shader file under `src/RenderTab/Shaders/ComputePreview.comp`
- the Render runtime now also owns its first shared-material/emissive-light slice: scene-owned materials, per-primitive material assignment and tinting, emissive contribution inside `Path Trace Preview`, and a dedicated `Emissive Showcase` validation scene for less debug-oriented baseline transport checks
- the Render baseline now also includes path-trace isolation controls for realism testing: `World Light` can be toggled in Render settings, `Background Mode` now supports `Black`, and `Emissive Showcase` defaults to that black background so emissive-box testing is less distracted by the earlier gradient/grid backgrounds
- the Render material/runtime path now also carries first-pass opaque baseline material controls for `roughness` and `metallic`, and the path-trace preview now samples emissive scene geometry directly instead of relying only on environment misses and the earlier sun estimate
- the Render camera/runtime path now also includes a first real thin-lens depth-of-field slice: camera `Focus Distance` and `Aperture Radius` persist through `.renderscene` snapshots, `Path Trace Preview` uses those values for lens-sampled camera rays, and a built-in `Depth Of Field Study` validation scene now exists for focus-plane checks
- realism-focused validation scenes such as `Emissive Showcase` and `Depth Of Field Study` now default their shared Render `World Light` state off when selected, so box-scene testing starts from the scene's own light sources instead of the earlier directional world light
- the Render baseline now also includes its first stable path-trace AOV slice through `Display Mode`: `Albedo`, `World Normal`, `Depth`, `Material ID`, and `Primitive ID` now render through the shared compute path on top of the existing beauty/luminance/sample-tint modes
- the Render baseline lighting path now also uses a stronger shared opaque BSDF path for direct light response: direct sun now respects the same diffuse/glossy material evaluation as the bounce path, direct emissive samples use MIS-style weighting against the BSDF pdf, and emissive hits after the first bounce are weighted against light-sampling pdfs instead of the earlier naive always-add emission path
- the Render baseline inspection path now also includes a simple convergence-view slice: `RenderBuffers` owns an extra per-pixel moment texture, and `Display Mode` now includes `Sample Count AOV` and `Variance AOV` on top of the earlier beauty/material/debug inspection modes
- the Render baseline world-light path is now decoupled from the old debug-background coupling: `RenderSettings` now carries separate environment intensity plus sun intensity/azimuth/elevation controls, and the path-trace preview uses those controls for miss lighting and direct sun instead of borrowing `Background Mode`
- the Render baseline world-lit path now also includes a dedicated `Sun / Sky Study` validation scene plus a first direct-environment sampling pass, so outdoor/world-lit scenes no longer rely only on miss lighting plus the earlier direct-sun shortcut
- the Render baseline preview path now also includes bounded beauty-view tonemapping controls: `RenderSettings` exposes `Linear Clamp`, `Reinhard`, and `ACES Film`, and the path-trace beauty-family views use that tonemap step after exposure/accumulation while debug and inspection AOVs stay un-tonemapped
- the Render editor-authoring path now also includes `Raster Preview`, durable object-id selection for spheres / standalone triangles / mesh instances, viewport click-picking, first translate/rotate/scale gizmos with world/local axis control, and duplicate/delete actions that all operate on the same shared scene/runtime objects as the path tracer
- the Render authoring path now also includes a dedicated `RenderRasterPreviewRenderer` OpenGL path with real color/depth/object-id targets, textured raster shading for the shared scene objects, and a geometry-derived gold selection outline that replaces the earlier projected-bounds box highlight
- the Render viewport outline path is now shared across `Raster Preview`, `Path Trace Preview`, and `Debug Preview` by compositing the selected-object mask produced by the raster renderer instead of drawing a box around the selected object
- the Render Asset Browser now supports worker-backed static glTF 2.0 import for `.gltf` and `.glb`, creating imported asset records, imported textures, shared imported materials, reusable mesh definitions, and scene mesh instances on the same runtime path already used by validation scenes and the current path tracer
- the Render material/runtime path now also carries first-pass imported texture references for base color, metallic-roughness, emissive, and normal maps; raster preview samples all four while the current path-trace preview samples base color, metallic-roughness, and emissive textures from shared texture-array storage
- `.renderscene` snapshot persistence now round-trips imported asset metadata, imported texture records, textured materials, richer mesh data, stable object ids, and imported mesh-instance bindings instead of flattening the new import workflow back into validation-scene-only anonymous geometry
- the Render material/runtime path now also carries first-pass dielectric fields (`transmission`, `ior`), and `.renderscene` snapshot persistence round-trips those fields alongside the current textured-material state
- `Path Trace Preview` now owns the first bounded smooth-glass transport slice: dielectric Fresnel, Snell refraction, total internal reflection, geometric-normal inside/outside handling, NaN-safe ray propagation, and direct-light visibility that skips through clear glass instead of treating it as a black blocker
- the Render validation-scene set now also includes `Glass Slab`, `Window Pane`, and `Glass Sphere Study` for first-dielectric sanity checks, while `Raster Preview` intentionally keeps dielectric-tagged objects as stable opaque authoring materials in this phase
- the Render preview can now switch between brute-force and BVH traversal and show simple debug views such as world normals, hit distance, primitive ID, and BVH depth
- accumulation resets are revision-driven for scene, camera, settings, and resolution changes, and the current Render Manager scope is still only one active preview job
- OpenGL 4.3 core is now a deliberate native baseline for Render work

These web editor layers are currently ported into native form:

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

These are native adaptations, not exact web UI clones. Example: chromatic aberration center control is exposed as inspector sliders in native instead of the web app's center-pin interaction. The native `Dithering` and `Palette Reconstructor` layers also use inspector-managed local palette banks instead of depending on the web app's shared Studio palette state. `Palette Reconstructor` now supports explicit `Add Color` growth and per-color removal instead of only count-based control. Native `Airy Bloom` currently ports the core diffraction bloom pass but not the web layer's optional mask gates because the broader native mask system is still missing.

## Layers Still Missing From The Web Editor

The web registry still contains important layers that are not yet native:

- `Text Overlay`
- `Generator`
- `Background Remover & Patcher`
- `Expander`
- other remaining non-Stitch editor passes from the web registry

When choosing the next port, prefer layers that fit the current native fullscreen-pass model cleanly before taking on tools that require extra authoring UI or multi-stage runtime state.

## Build Notes

- CMake root: `Stack/CMakeLists.txt`
- configure: `cmake -S Stack -B Stack/build`
- build: `cmake --build Stack/build --config Release`
- executable: `Stack/build/bin/Release/ModularStudioStack.exe`

Local build hardening already added:

- `CONFIGURE_DEPENDS` on the source glob so newly added layer files are picked up on reconfigure
- offline-friendly `FetchContent` behavior that reuses `Stack/build/_deps/*-src` when those local dependency caches already exist

## Documentation Rule For Future Sessions

Keep `AI_CONTEXT.md`, `README.md`, and `DEV_LOG.md` centered on:

- what web feature is being migrated
- how it was adapted to native
- what is implemented now
- what is still missing
- what the next sensible migration target is

Avoid writing these docs as if the current native app is already the final product definition.

## Next Sensible Targets

With the Library persistence work, the first worker-backed responsiveness pass, and Render Phase 2 now in place, there are two sensible next tracks:

- continue the remaining non-Stitch Editor layer migration from the web registry
- extend the new Render tab beyond the Phase 2 skeleton using `Codex  and Research Plans Archive/Companion Implementation Plan for the Render Tab.md` as the ordering source of truth

Continue balancing feature-parity work with responsiveness work instead of treating them as separate tracks.

The most sensible next group is:

- `Expander`
- `Text Overlay`
- `Generator`

The next sensible render-specific step is:

- validate the new smooth-glass validation scenes and dielectric material controls in-app before treating the Phase 5C slice as stable
- once the first dielectric slice proves stable, continue Render with the next optics follow-up instead of jumping directly to media, denoise, or final-output work
- keep real transport expansion disciplined: absorption and frosted transmission first, then later optics such as mist/media, caustics, and output-pipeline work only after the first dielectric slice is trustworthy

The higher-risk follow-up group still needs extra care:

- `Background Remover & Patcher`
- the more authoring-heavy text and generator workflows
