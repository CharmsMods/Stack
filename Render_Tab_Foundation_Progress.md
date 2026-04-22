# Render Tab Foundation Progress

## Source Of Truth

This file exists to track implementation progress without editing the prepared specification files.

Always re-read these before advancing the next milestone:

- `Stack/Redefining Rendering/MAIN FILE.txt`
- `Stack/Redefining Rendering/Main technical companion.md`
- `Stack/Redefining Rendering/Viewport, State, and Pipeline Specification.md`
- `Stack/Redefining Rendering/Compute Shader & Spectral Memory Layout Specification.md`
- `Stack/Redefining Rendering/Anti-Goals & Constraints Specification.md`

## Completed Slices

### Slice 1

Status: complete

This first slice replaced the Render tab placeholder with a bounded foundation module that restored:

- the left-inspector / center-viewport / right-scene layout
- in-memory scene/material/light/camera state
- library save/load compatibility
- accumulation/display epoch tracking
- temporary projected viewport proxies

### Slice 2

Status: complete

This slice replaces the projected proxy viewport with a real 3D authoring path while keeping the God/spec files untouched and preserving the existing top-level render-project payload shape.

Delivered in this slice:

- restored runtime authoring layer under `Stack/src/RenderTab/Runtime/` for:
  - scene compilation
  - camera state
  - raster viewport rendering
  - geometry transforms
  - BVH construction
- new contract layer under `Stack/src/RenderTab/Contracts/` introducing:
  - `ViewportInputFrame`
  - `SceneChangeSet`
  - `DirtyFlags`
  - `ResetClass`
  - `SceneSnapshot`
  - `SceneCompiler`
  - `AccumulationManager`
  - `RenderDelegator`
  - `ViewportController`
- `Unlit` viewport now renders a depth-tested 3D scene from the compiled snapshot instead of drawing 2D proxy glyphs
- CPU authoring selection now uses ray/BVH picking against the compiled snapshot, not viewport proxy hit tests
- viewport interaction is routed through the dedicated controller for:
  - `RMB` fly camera
  - `W / E / R` transform mode switching
  - `T` local/world transform space toggle
  - transform-handle dragging
  - selection and context-menu ownership ordering
- render camera promoted to a first-class scene object in the outliner/inspector flow
- dockable/floating `Render Camera` window added for direct camera control
- library preview generation now prefers the restored 3D viewport render path before falling back
- snapshot serialization upgraded to version `2` with explicit camera object identity while staying backward-compatible

Key files added in this slice:

- `Stack/src/RenderTab/Contracts/RenderContracts.h`
- `Stack/src/RenderTab/Contracts/AccumulationManager.h`
- `Stack/src/RenderTab/Contracts/AccumulationManager.cpp`
- `Stack/src/RenderTab/Contracts/SceneCompiler.h`
- `Stack/src/RenderTab/Contracts/SceneCompiler.cpp`
- `Stack/src/RenderTab/Contracts/RenderDelegator.h`
- `Stack/src/RenderTab/Contracts/RenderDelegator.cpp`
- `Stack/src/RenderTab/Contracts/ViewportController.h`
- `Stack/src/RenderTab/Contracts/ViewportController.cpp`
- `Stack/src/RenderTab/Runtime/...`
- `Stack/src/RenderTab/Shaders/RenderRasterPreview.vert`
- `Stack/src/RenderTab/Shaders/RenderRasterPreview.frag`
- `Stack/src/RenderTab/Shaders/RenderOutlineComposite.vert`
- `Stack/src/RenderTab/Shaders/RenderOutlineComposite.frag`

## Explicit Deferrals

The following are intentionally still deferred after Slice 2:

- real spectral transport
- compute shaders
- GPU wavefront path tracing
- ImGuizmo integration
- imported mesh authoring
- CPU/GPU scene compiler split beyond the current runtime bridge
- layered BSDF evaluation
- volumetric transport
- denoiser/AOV pipeline
- final offline render jobs

These remain deferred to keep the current milestone reviewable and aligned with the anti-goals around avoiding unstable complexity and interaction blocking.

### Slice 3

Status: complete

This slice keeps the Slice 2 authoring architecture intact and replaces the `Path Trace` placeholder with the first truthful GPU compute transport path.

Delivered in this slice:

- new PT runtime layer under `Stack/src/RenderTab/PathTrace/` introducing:
  - `CompiledPathTraceScene`
  - `PathTraceBuffers`
  - `PathTraceRenderer`
  - `PathTraceFeatureMask`
  - `SpectralBasisCoefficients`
- `SceneCompiler` now emits both:
  - the raster authoring scene for `Unlit`
  - the flattened path-trace scene for `Path Trace`
- OpenGL sync-object loading added to the local GL loader for:
  - `glFenceSync`
  - `glClientWaitSync`
  - `glDeleteSync`
- `RenderDelegator` now remains the single main-thread GL entry point for both authoring raster and path-trace compute dispatch
- `AccumulationManager` upgraded to:
  - dual-slot visibility/render ownership
  - partial/full reset routing
  - viewport resize debounce/commit tracking
  - fence-driven sample count progression
- first PT shader set added under `Stack/src/RenderTab/Shaders/`:
  - `PathTraceCommon.glsl`
  - `PathTraceGenerate.comp`
  - `PathTraceIntersect.comp`
  - `PathTraceShadeSurface.comp`
  - `PathTraceShadow.comp`
  - `PathTraceAccumulate.comp`
- `Path Trace` now uses:
  - spectral lane state from the start
  - compute-driven progressive accumulation
  - sphere / cube / plane support
  - diffuse / metal / emissive / smooth glass support
  - point / spot / area / directional light support
  - analytic environment support on miss
- PT-mode UI truthfulness improved:
  - unsupported material controls are hidden from PT mode
  - fog/volume controls stay hidden in PT mode until the volume slice
  - the old `Path Trace Pending` messaging has been removed
- snapshot serialization upgraded to version `3` while keeping backward compatibility

Key files added in this slice:

- `Stack/src/RenderTab/PathTrace/PathTraceTypes.h`
- `Stack/src/RenderTab/PathTrace/PathTraceRenderer.h`
- `Stack/src/RenderTab/PathTrace/PathTraceRenderer.cpp`
- `Stack/src/RenderTab/Shaders/PathTraceCommon.glsl`
- `Stack/src/RenderTab/Shaders/PathTraceGenerate.comp`
- `Stack/src/RenderTab/Shaders/PathTraceIntersect.comp`
- `Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp`
- `Stack/src/RenderTab/Shaders/PathTraceShadow.comp`
- `Stack/src/RenderTab/Shaders/PathTraceAccumulate.comp`

### Slice 4A

Status: complete

This slice keeps the Slice 3 PT-core architecture intact and closes the first missing glass-transport contracts without widening into full layering or volumes.

Delivered in this slice:

- PT glass transport now distinguishes:
  - smooth dielectric when `transmissionRoughness == 0`
  - rough dielectric sampling when `transmissionRoughness > 0`
- thin-walled glass no longer reuses the closed-solid path:
  - thin sheets stay out of the interior-medium state
  - closed solids still carry Beer-Lambert absorption through traveled thickness
- first wavelength-dependent dielectric eta hook added inside the PT glass path:
  - authoring still exposes one `IOR` control
  - the PT material payload now carries internal spectral dielectric parameters derived from that control
- PT material payload widened to carry extra dielectric transport parameters without changing the top-level render-project payload shape
- `PathTraceFeatureMask` widened so compiled PT scenes can truthfully report:
  - rough dielectric glass support
  - thin-walled glass support
  - spectral dielectric eta support
- PT-mode UI truthfulness improved again:
  - `Transmission Roughness` is now live in `Path Trace`
  - `Clear Coat`, `Thin Film`, and `Subsurface` remain hidden from PT mode until their transport slices land
- the scene/phase notes now reflect that rough and thin-sheet glass are part of the active PT core

Key files updated in this slice:

- `Stack/src/RenderTab/PathTrace/PathTraceTypes.h`
- `Stack/src/RenderTab/Contracts/SceneCompiler.cpp`
- `Stack/src/RenderTab/Shaders/PathTraceCommon.glsl`
- `Stack/src/RenderTab/RenderTab.cpp`

### Slice 4A Follow-Up

Status: complete

This follow-up pass stays inside Slice 4A and fixes the first round of glass-path correctness and PT-truthfulness issues discovered during live debugging.

Delivered in this follow-up:

- smooth solid-glass PT now uses the locked entry/exit dielectric convention without the old heuristic `max(baseSpectrum, vec4(0.25))` floor in the delta branch
- PT thin-walled behavior is now truthfully sheet-only:
  - plane geometry may compile as thin-sheet PT glass
  - sphere and cube PT paths compile closed transmissive materials as solid glass even when the authoring material has `thinWalled = true`
- ray spawning now uses a scale-aware epsilon instead of the old fixed `0.0015` offset
- PT now exposes a small built-in glass debug path:
  - selected-ray bounce log
  - refracted-source classification overlay
  - self-hit heatmap
- PT material UI is more truthful:
  - glass keeps the controls the current PT path actually honors
  - closed primitives show `Thin Walled` as disabled with a PT note instead of pretending it is live
  - PT-only debug settings are now part of the Render tab state and project payload

Key files updated in this follow-up:

- `Stack/src/RenderTab/Foundation/RenderFoundationTypes.h`
- `Stack/src/RenderTab/Foundation/RenderFoundationSerialization.cpp`
- `Stack/src/Renderer/GLLoader.h`
- `Stack/src/Renderer/GLLoader.cpp`
- `Stack/src/RenderTab/Contracts/SceneCompiler.cpp`
- `Stack/src/RenderTab/Contracts/RenderDelegator.h`
- `Stack/src/RenderTab/PathTrace/PathTraceRenderer.h`
- `Stack/src/RenderTab/PathTrace/PathTraceRenderer.cpp`
- `Stack/src/RenderTab/Shaders/PathTraceCommon.glsl`
- `Stack/src/RenderTab/Shaders/PathTraceGenerate.comp`
- `Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp`
- `Stack/src/RenderTab/Shaders/PathTraceAccumulate.comp`
- `Stack/src/RenderTab/RenderTab.cpp`

### Slice 4B

Status: complete

This slice adds the first ordered material-layer contract without widening into volumes or advanced caustic integrators.

Delivered in this slice:

- render-project snapshot materials now carry an explicit ordered layer stack while preserving backward compatibility with the older flat-material payload
- the material inspector now edits:
  - one required base layer
  - one optional clear-coat layer
  - shared emission and absorption properties
- PT scene compilation now emits:
  - per-material headers with layer offset/count metadata
  - a flat `MaterialLayer[]` buffer for GPU upload
- PT shader/runtime now supports the first coat-over-base combinations:
  - clear coat over diffuse
  - clear coat over metal
  - clear coat over dielectric
  - polished-over-frosted dielectric behavior using the existing rough-glass base path plus the new outer coat
- thin film and subsurface remain deferred and are now shown as such instead of pretending to be active PT features
- the open glass-ring artifact remains tracked, but it is no longer blocking the layer-stack milestone

Key files updated in this slice:

- `Stack/src/RenderTab/Foundation/RenderFoundationTypes.h`
- `Stack/src/RenderTab/Foundation/RenderFoundationSerialization.cpp`
- `Stack/src/RenderTab/Foundation/RenderFoundationState.cpp`
- `Stack/src/RenderTab/Contracts/SceneCompiler.cpp`
- `Stack/src/RenderTab/PathTrace/PathTraceTypes.h`
- `Stack/src/RenderTab/PathTrace/PathTraceRenderer.h`
- `Stack/src/RenderTab/PathTrace/PathTraceRenderer.cpp`
- `Stack/src/RenderTab/Shaders/PathTraceCommon.glsl`
- `Stack/src/RenderTab/RenderTab.cpp`

## Next Milestone

Target: Slice 4C, volumes and fog objects

Next work should build on the now-stable surface/layer contract rather than jumping straight to camera imaging or caustic solvers:

1. Add transformable fog/volume objects to the scene model and inspector.
2. Compile homogeneous volume descriptors into the immutable snapshot and GPU upload path.
3. Keep the PT UI truthful by only exposing volume controls that have real transport behind them.
4. Preserve the current main-thread GL delegator model, immutable snapshot flow, and dual-slot accumulation behavior.

## Guardrails

- Do not edit the God/spec files.
- Do not port the archived Render tab back wholesale.
- Do not introduce fake raster shortcuts into the future `Path Trace` backend.
- Keep new files small and phase-specific so the next slices can be replaced cleanly if needed.
