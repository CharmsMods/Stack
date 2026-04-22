# Development Log

This log tracks the web-to-native migration of Modular Studio into `Stack/`.

Use it as a continuation record:

- what was just adapted from the web app
- what native implementation choice was made
- what still needs to be ported next

Do not read any single entry as a claim that the native build is "done" or locked.

## [Current Phase]
**Stage:** Editor layer parity, responsiveness migration, and Render tab phased buildout
### [2026-04-18] - Render Phase 5C Stabilization: Restoring Path Trace Visibility
- **Completed:** Identified and fixed a critical logic bug in `ComputePreview.comp` where `intersectSphere` and `intersectTriangle` always returned `false` even on successful hits. This caused the path-trace integrator to effectively ignore all scene geometry and only display the environment/background.
- **Completed:** Identified and fixed a memory safety bug in `RenderBvh.cpp` where recursive `BuildNode` calls could invalidate the `RenderBvhNode&` reference during vector reallocation. Switched to index-based access to ensure BVH child indices and bounds are correctly preserved.
- **Completed:** Restored visible geometry across all validation scenes in `Path Trace Preview` mode, including the new smooth-glass dielectric scenes.
- **Completed:** Confirmed that brute-force and BVH traversal paths both return valid hits now that intersection success is correctly reported to the integrator.
- **Next Likely Target:** Finalize the Phase 5C glass milestone validation now that the render path is stable, then proceed with the next optics slice (absorption or rough transmission).

---

### [2026-04-18] - Render Phase 5C Slice: First Smooth-Glass Dielectric Milestone
- **Completed:** Extended the shared Render material/runtime model with first-pass dielectric controls for `transmission` and `ior`, threading those values through resolved materials, GPU upload structs, Inspector editing, imported-material persistence defaults, and `.renderscene` snapshot round-tripping.
- **Completed:** Extended the shared compute renderer so `Path Trace Preview` now includes the first bounded smooth-glass transport branch: dielectric Fresnel evaluation, stochastic reflection-versus-refraction selection, Snell refraction, total internal reflection, geometric-normal inside/outside handling, scale-aware ray-origin offsets, and NaN/Inf-safe dielectric propagation.
- **Completed:** Kept the raster/editor side intentionally stable in this phase. `Raster Preview` still renders dielectric-tagged objects as opaque authoring materials, so picking, gizmos, duplicate/delete, glTF import, and the shared selection outline remain usable while accurate glass stays in `Path Trace Preview`.
- **Completed:** Added built-in `Glass Slab`, `Window Pane`, and `Glass Sphere Study` validation scenes with world light off by default, black backgrounds, emissive-box lighting, and patterned backdrops aimed at making refraction errors obvious rather than cinematic.
- **Completed:** Tightened direct-light correctness for the clear-glass slice by letting visibility rays skip through smooth dielectrics instead of treating them as fully opaque blockers, which avoids the most obvious solid-black-glass-shadow failure for this first milestone.
- **Deferred Intentionally:** This slice still does not add Beer-Lambert absorption, frosted/rough transmission, spectral dispersion, caustics-specific backend work, media/mist, denoise, EXR/output jobs, or glTF dielectric-extension import.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the Phase 5C C++ changes landed. The remaining verification is the runtime shader pass, because `ComputePreview.comp` is still compiled at app launch instead of by MSBuild.
- **Next Likely Target:** Validate the new glass scenes in-app, then continue with the next optics follow-up such as absorption/rough transmission only after the first smooth-glass slice proves visually and numerically stable.

### [2026-04-18] - Render Phase 5B Slice: True Raster Preview, Selection Outline, And Static glTF Import
- **Completed:** Added an organized `RenderRasterPreviewRenderer` under `src/RenderTab/Runtime/` so `Raster Preview` is now a real OpenGL raster authoring path with dedicated color, depth, object-id, and composite targets instead of relying on the older compute-based fast-preview behavior.
- **Completed:** Replaced the old projected-bounds selection box with a true geometry-derived outline by rendering a selected-object mask/object-id path and compositing a gold outline around the chosen object. The same outline path is now shared across `Raster Preview`, `Path Trace Preview`, and `Debug Preview`.
- **Completed:** Added worker-backed static glTF 2.0 import for `.gltf` and `.glb` through the Render `Asset Browser`, keeping import CPU/IO work on the shared async task system and applying GL resources plus scene insertion back on the main thread.
- **Completed:** Extended the shared Render material/runtime model with imported asset records, imported texture records, texture references for base color / metallic-roughness / emissive / normal maps, and first-pass texture-array GPU storage reused by both raster preview and the current path-trace preview.
- **Completed:** Extended `.renderscene` persistence so imported assets, imported textures, textured materials, richer mesh data, stable object ids, and imported mesh-instance bindings round-trip instead of collapsing imported content into anonymous validation-scene geometry.
- **Completed:** Kept the scope bounded: this slice still does not start smooth-glass optics, rough transmission, absorption, media/mist, denoise, EXR/output jobs, animation import, skinning, morph targets, or other advanced glTF extension work.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the raster/outline/glTF slice landed.
- **Next Likely Target:** Validate raster authoring, the new selection outline, textured glTF import, and imported-scene snapshot reloads in-app, then move into the first bounded smooth-glass optics milestone on top of the now-shared raster/path-trace/import material/runtime path.

### [2026-04-18] - Render Phase 5A Slice: Raster Authoring, Picking, And Gizmos
- **Completed:** Added `Raster Preview` as the fast editor-style Render viewport mode beside `Path Trace Preview` and `Debug Preview`, while keeping all three modes on the same Render scene/runtime objects.
- **Completed:** Reworked Render object selection around durable object ids for spheres, standalone triangles, and mesh instances so Outliner clicks, viewport clicks, duplication, deletion, and scene rebuilds no longer depend on transient vector ordering.
- **Completed:** Added viewport click-picking against the current camera/scene geometry path and mapped mesh-triangle hits back to their owning mesh instance selection.
- **Completed:** Added the first in-viewport transform gizmo slice with translate / rotate / scale modes, world/local axis control, visible selection bounds, axis highlighting, and shared object-transform updates that immediately affect both raster and path-trace views.
- **Completed:** Added duplicate/delete support for the selected Render object via viewport shortcuts (`Ctrl+D`, `Delete`) and matching toolbar actions.
- **Completed:** Kept the scope bounded: this slice does not add undo/redo yet, does not start glTF import yet, and does not begin the dielectric/smooth-glass optics milestone yet.
- **Build Note:** `cmake --build Stack/build --config Release` passed.
- **Next Likely Target At The Time:** Start Phase 5B import work by adding static glTF 2.0 mesh/material/texture import through the Render `Asset Browser`, then follow with the first bounded smooth-glass optics slice.

### [2026-04-18] - Render Phase 4 Slice: Preview Tonemap And Highlight Rolloff
- **Completed:** Extended `RenderSettings` with bounded beauty-view tonemap modes (`Linear Clamp`, `Reinhard`, and `ACES Film`) so the Render preview can roll off highlights more gracefully without changing the underlying scene/runtime foundation.
- **Completed:** Reworked the compute preview display path so the new tonemap modes affect only the path-trace beauty-family views after exposure/accumulation, while debug preview and inspection AOVs remain un-tonemapped for analysis stability.
- **Completed:** Updated the Render Settings, AOV / Debug, and Statistics panels so the tonemap state is visible and adjustable from the existing Render UI surface.
- **Completed:** Kept the scope bounded: this slice still does not add denoise, output-pipeline color management, EXR/export work, dielectric glass, media/mist, or the later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed.
- **Next Likely Target:** Validate the new tonemap modes in-app against the current beauty view, then continue the baseline transport/preview-correctness family without jumping early into denoise, output, dielectric, or media work.

### [2026-04-17] - Render Phase 4 Slice: Sun/Sky Validation And Direct Environment Sampling
- **Completed:** Added a dedicated `Sun / Sky Study` validation scene so the new decoupled world-light controls have an explicit in-app target instead of being exercised only through the older debug-heavy scenes.
- **Completed:** Extended the path-trace preview with a first direct-environment sampling pass for world-lit scenes, keeping it bounded to baseline outdoor/world-light quality rather than starting HDRI import, analytic light objects, or later optics work.
- **Completed:** Updated the Render Settings, AOV / Debug, Asset Browser, and console messaging so the new world-lit baseline slice is discoverable from the existing Render panel set.
- **Completed:** Kept the scope disciplined: this slice still does not add HDRI/environment asset import, adaptive sampling, dielectric glass, media/mist, denoise, EXR/output jobs, or the later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed.
- **Next Likely Target:** Validate `Sun / Sky Study` and the new direct-environment response in-app, then continue the baseline transport family without jumping early into dielectric, media, denoise, or output-pipeline work.

### [2026-04-17] - Render Phase 4 Slice: Decoupled World Environment And Sun Controls
- **Completed:** Extended `RenderSettings` with separate world-light controls for environment intensity, sun intensity, sun azimuth, and sun elevation so Render no longer relies on a single hardcoded world-light direction when the world-light toggle is on.
- **Completed:** Reworked the path-trace preview shader so miss lighting now comes from a dedicated sky/environment evaluation path and direct sun now follows the new settings-driven direction/intensity controls instead of borrowing the old debug-style `Background Mode` coupling.
- **Completed:** Updated the Render Settings, AOV / Debug, and Statistics panels so the new world-light controls are surfaced and inspectable from the existing panel set.
- **Completed:** Kept the scope bounded: this slice still does not add analytic light objects, HDRI import, adaptive sampling, dielectric glass, media/mist, denoise, EXR/output jobs, or the later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed after closing the running app that was holding the Release executable open.
- **Next Likely Target:** Validate the new world-lit path-trace controls in-app, then continue extending the baseline transport family without jumping early into dielectric, media, denoise, or output-pipeline work.

### [2026-04-17] - Render Phase 4 Slice: Convergence Inspection With Sample Count And Variance AOVs
- **Completed:** Extended `RenderBuffers` with a dedicated per-pixel moment texture so the Render tab now has a lightweight convergence-inspection path instead of relying only on sample count text in the UI.
- **Completed:** Added `Sample Count AOV` and `Variance AOV` to the shared `Display Mode` path in the compute renderer, keeping the work bounded to inspection and visualization rather than changing the transport model again.
- **Completed:** Updated the Render Settings, AOV / Debug, and Statistics panels so the new convergence views are surfaced from the existing panel set instead of being buried as internal-only debug state.
- **Completed:** Kept the scope disciplined: this slice still does not add adaptive sampling, denoise, dielectric glass, rough transmission, media/mist, EXR/output jobs, or the later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed after closing the running app that was holding the Release executable open.
- **Next Likely Target:** Validate the new variance/sample-count inspection views in-app, then continue the baseline transport phase with a broader environment/light sampling upgrade before any dielectric or output-pipeline expansion.

### [2026-04-17] - Render Phase 4 Slice: Stronger Direct-Light Weighting On The Shared Opaque Baseline
- **Completed:** Reworked the path-trace preview shader around one shared opaque BSDF path for direct-light response instead of leaving direct sun, emissive-light samples, and bounce transport on separate shortcut formulas.
- **Completed:** Updated direct world/sun lighting so it now respects the same baseline diffuse-plus-glossy material evaluation as the bounce path, which makes metallic and glossy surfaces respond more coherently to the current direct-light path.
- **Completed:** Upgraded emissive-light sampling to a stronger MIS-style weighting pass by balancing light-sampling pdfs against the BSDF pdf, and emissive hits after the first bounce now also weight against the corresponding sampled-light pdf instead of always adding raw emission.
- **Completed:** Updated the current Render settings/debug messaging so the new lighting slice is surfaced in the existing panels without introducing another renderer-mode panel.
- **Deferred Intentionally:** This slice still does not add full explicit analytic light objects, broader environment/light MIS, variance/sample-count AOVs, dielectric glass, rough transmission, media/mist, denoise, EXR/output jobs, or later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the lighting slice landed, but the shader changes still need a live in-app validation pass because the compute shader is loaded at runtime rather than by the C++ build itself.
- **Next Likely Target:** Validate the new lighting behavior in `Emissive Showcase` and `Depth Of Field Study`, then continue the baseline phase with either a variance/sample-count inspection slice or a broader environment/light sampling upgrade before any dielectric or output-pipeline expansion.

### [2026-04-17] - Render Phase 4 Slice: Scene Default World-Light Isolation And First Stable AOVs
- **Completed:** Reworked Render validation-scene selection so realism-focused scenes such as `Emissive Showcase` and `Depth Of Field Study` now default the shared `World Light` toggle off when selected, instead of inheriting the earlier directional world-light path and making one-light box-scene checks harder to judge.
- **Completed:** Added the first stable path-trace AOV display slice on top of the existing baseline transport path. `Display Mode` now includes `Albedo`, `World Normal`, `Depth`, `Material ID`, and `Primitive ID` in the shared compute renderer instead of limiting inspection to beauty/luminance/sample tint plus the older debug-preview-only views.
- **Completed:** Routed scene selection through Render-tab actions instead of mutating scene state directly from the Asset Browser panel, which keeps validation-scene defaults and renderer settings under the correct owners.
- **Completed:** Updated the existing Render settings/debug/statistics messaging so the new scene defaults and AOV displays are visible from the current panel set instead of hiding them in renderer internals.
- **Deferred Intentionally:** This slice still does not add MIS, a full variance/sample-count AOV, dielectric glass, rough transmission, media/mist, denoise, EXR/output jobs, or later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the scene-default world-light and AOV slice landed.
- **Next Likely Target:** Validate the new one-light scene defaults plus AOV displays in-app, then continue the baseline transport phase with stronger next-event / MIS-style lighting work before moving into dielectric or output-pipeline phases.

### [2026-04-17] - Render Phase 4 Slice: Thin-Lens Depth Of Field And Camera Persistence
- **Completed:** Extended the Render camera runtime with a real `Aperture Radius` control on top of the existing `Focus Distance`, instead of leaving focus distance as a placeholder-only value in the early preview path.
- **Completed:** Updated the shared compute preview shader so `Path Trace Preview` now generates thin-lens camera rays when aperture is non-zero, while `Debug Preview` intentionally stays on the old pinhole path for stable geometry/traversal validation.
- **Completed:** Extended `.renderscene` snapshot persistence so camera aperture now round-trips alongside camera position, yaw, pitch, field of view, focus distance, and exposure without breaking older snapshot files.
- **Completed:** Added a dedicated `Depth Of Field Study` validation scene so the new camera behavior has a built-in focus-plane quality gate instead of relying only on ad hoc manual testing.
- **Completed:** Updated the current Inspector/Settings messaging so users can discover the new depth-of-field controls from the existing Render panel layout without introducing a separate camera tool panel.
- **Deferred Intentionally:** This slice still does not introduce MIS, a full AOV set, dielectric glass, rough transmission, media/mist, denoise, EXR/output jobs, or the later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the thin-lens depth-of-field slice landed.
- **Next Likely Target:** Validate `Depth Of Field Study` and `Emissive Showcase` in-app, then continue the Phase 4 baseline with stronger direct-light weighting / MIS-style work or the first stable AOV slice before any dielectric or output-pipeline expansion.

### [2026-04-17] - Render Phase 4 Slice: Roughness/Metallic Materials And Direct Emissive-Light Sampling
- **Completed:** Extended the shared Render material model with first-pass opaque baseline controls for `roughness` and `metallic`, and threaded those values through scene state, snapshot serialization, validation scenes, Inspector editing, and GPU upload instead of leaving the current path tracer limited to pure diffuse/emissive surfaces.
- **Completed:** Updated the path-trace preview shader so it now uses the richer material data for broader opaque-surface response and directly samples emissive scene geometry, which improves the current Cornell-box-style `Emissive Showcase` workflow without introducing dielectric or media features yet.
- **Completed:** Reworked the built-in `Emissive Showcase` material set so it now mixes matte walls with more obviously metallic hero objects, making the baseline realism scene more useful for roughness/metalness progression checks.
- **Completed:** Updated the current material-facing UI so Inspector and Asset Browser surfaces now reflect roughness/metallic state rather than only base color and emission.
- **Deferred Intentionally:** This slice still does not implement MIS, explicit analytic light objects, dielectric refraction, rough transmission, media/mist, denoise, EXR/output jobs, or the later optics milestones.
- **Build Note:** Source compilation succeeded for this slice, but the final Release link was blocked because a running `ModularStudioStack.exe` kept the output binary open.
- **Next Likely Target:** Rebuild after closing the running app, validate the new roughness/metallic plus emissive-light-sampling baseline in `Emissive Showcase`, then continue baseline transport quality work without crossing into dielectric or output-pipeline phases.

### [2026-04-17] - Render Phase 4 Follow-Up Slice: World-Light Toggle And Black Background Isolation
- **Completed:** Added a `Black` Render background mode so the preview no longer has to show gradient/checker/grid misses when the user wants to judge shadows, silhouettes, and bounce lighting without background distraction.
- **Completed:** Added a Render-settings `World Light` toggle that disables the current direct world/sun light estimate, which makes emissive-only box testing practical without removing the earlier baseline light path entirely.
- **Completed:** Set the built-in `Emissive Showcase` validation scene to default to the black background so the most realism-focused built-in test scene starts in a cleaner isolation state.
- **Completed:** Updated the Settings, AOV / Debug, Statistics, and Outliner panels so the new world-light/background state is visible and controllable from the current UI instead of hiding it in renderer internals.
- **Deferred Intentionally:** This is still only a control/isolation pass on the current baseline transport slice. It does not introduce MIS, explicit light objects, glossy/specular reflection, dielectric glass, media/mist, denoise, EXR/output jobs, or later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the world-light/background isolation pass landed.
- **Next Likely Target:** Keep extending baseline path-trace realism carefully, with the next work centered on better light transport quality and broader material behavior before moving into the later dielectric or output phases.

### [2026-04-17] - Render Phase 4 Slice: Shared Materials, Emissive Surfaces, And A Lit Validation Scene
- **Completed:** Added an organized Render material runtime under `src/RenderTab/Runtime/Materials` and threaded scene-owned material definitions through validation scenes, snapshots, primitive editing, and compute uploads instead of leaving the path-trace preview bound to raw debug colors only.
- **Completed:** Added per-primitive material assignment plus albedo tinting for spheres, standalone triangles, and mesh-backed triangles, while keeping the existing mesh-instance tint path intact for the current built-in mesh validation scenes.
- **Completed:** Extended the compute preview/path-trace shader so surfaces now carry resolved albedo and emissive contribution, letting `Path Trace Preview` accumulate actual emissive scene lighting on top of the existing diffuse multi-bounce baseline.
- **Completed:** Added a dedicated `Emissive Showcase` validation scene so Render now has one built-in scene aimed at baseline transport/material checks rather than only traversal/debug compositions.
- **Completed:** Updated the current panels instead of adding a separate asset system too early: the Inspector can now select/edit shared materials from selected primitives, the Asset Browser lists active scene materials, and scene statistics now report material/emissive counts.
- **Deferred Intentionally:** This slice still does not introduce explicit light objects, MIS, glossy/specular reflection, dielectric glass, media/mist, denoise, EXR/output jobs, or the later optics milestones.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the shared-material/emissive slice landed.
- **Next Likely Target:** Validate the new emissive/material baseline in-app, then continue with richer baseline transport work such as cleaner light sampling and broader material behavior without jumping ahead to dielectric, volume, or output-pipeline phases.

### [2026-04-17] - Render Phase 4 Kickoff Slice: First Visible Diffuse Path-Trace Preview
- **Completed:** Added the first honest path-trace preview slice on top of the existing geometry/BVH foundation: diffuse multi-bounce transport with progressive accumulation, bounce limits, Russian-roulette continuation, environment lighting, and a simple direct-sun estimate inside the shared compute renderer.
- **Completed:** Added renderer-wide controls for `Integrator Mode` and `Max Bounces` in Render settings, keeping `Debug Preview` available alongside the new `Path Trace Preview` mode instead of deleting the current traversal-debug workflow.
- **Completed:** Moved the compute shader source out of the oversized C++ string literal and into `src/RenderTab/Shaders/ComputePreview.comp`, with the runtime now loading that shader file explicitly through the existing GL helper path.
- **Completed:** Kept the scope disciplined: this was only the first Phase 4 kickoff slice before the shared-material/emissive follow-up landed. It still did not attempt MIS, glossy/specular reflection, depth of field transport, glass, mist, caustics, denoise, EXR/output jobs, or full renderer output handling.
- **Deferred Intentionally At The Time:** Full Phase 4 capabilities such as emissive scene authoring, next-event estimation and MIS, richer AOVs, depth of field transport, and broader material support remained for subsequent slices.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the path-trace kickoff slice landed.
- **Next Likely Target:** Validate the new path-trace preview in-app, then continue the baseline transport phase with richer light/material handling and cleaner preview/debug switching before moving to dielectric work.

### [2026-04-17] - Render Phase 3 Slice: Live Viewport Preview Instead Of Hard Sample Completion
- **Completed:** Changed the interactive Render viewport so the sample goal no longer hard-stops the viewport loop. The viewport now keeps updating past the goal and behaves like a live editor surface instead of requiring a manual restart after the first accumulation target is reached.
- **Completed:** Reframed the Render Manager and Render viewport UI around live-viewport semantics: pause/resume/reset controls, soft sample-goal wording, and status text that no longer implies the interactive viewport should complete and stay frozen.
- **Completed:** Added a small quality-of-life bridge so starting viewport fly navigation can resume the live viewport automatically if it was paused, keeping direct camera interaction aligned with editor-style expectations.
- **Completed:** Kept the scope disciplined: this slice only changes interactive viewport lifecycle behavior. It still does not add picking, transform gizmos, raster mode, path tracing, denoise, EXR/output jobs, glass, mist, or caustics.
- **Deferred Intentionally:** Viewport picking, in-viewport transform gizmos, raster/path-trace mode switching, material/light systems, and transport integrators remain for later phases.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the live-viewport update landed.
- **Next Likely Target:** Validate the now-always-live viewport behavior in-app, then move into picking/gizmo-style scene interaction on top of the current camera/runtime foundation.

### [2026-04-17] - Render Phase 3 Slice: Interactive Viewport Fly Camera And Camera-Position Snapshots
- **Completed:** Promoted `RenderCamera` from yaw/pitch-only orbit-style state into a real camera pose with explicit world-space position, shared forward/right/up vector helpers, and a reset-to-default-view path that still preserves the previous default framing.
- **Completed:** Reworked the `Viewport` panel and `RenderTab` runtime flow so the preview supports direct fly navigation with `RMB` look, `WASD` motion, `Q`/`E` vertical movement, and shift/ctrl speed modifiers, all routed through the existing camera revision/reset path instead of a separate temporary input state.
- **Completed:** Updated the compute preview renderer so camera rays now originate from the camera's explicit position instead of reconstructing origin from a fixed scene target, which keeps the debug renderer aligned with real free-camera navigation.
- **Completed:** Extended `.renderscene` snapshots to persist explicit camera position while staying backward-compatible with earlier snapshot files that only stored yaw/pitch/focus-style orbit values.
- **Completed:** Kept the scope disciplined: this slice stops at viewport navigation and camera/runtime correctness, not viewport picking, transform gizmos, raster mode, path tracing, denoise, EXR/output jobs, glass, mist, or caustics.
- **Deferred Intentionally:** Viewport picking, in-viewport transform gizmos, raster/path-trace mode switching, material/light systems, and transport integrators remain for later phases.
- **Build Note:** `cmake --build Stack/build --config Release` passed after the fly-camera slice landed.
- **Next Likely Target:** Validate the new viewport fly-camera behavior and snapshot compatibility in-app, then continue the next Render runtime step only after direct navigation and camera restoration feel stable.

### [2026-04-17] - Render Phase 3 Slice: Scene Snapshot Serialization And Asset Browser Workflow
- **Completed:** Added a Render-local JSON snapshot format under `src/RenderTab/Runtime/RenderSceneSerialization.*` that round-trips the current Render scene plus camera state without pulling Render settings, jobs, or output policy into the format yet.
- **Completed:** Added `.renderscene` open/save helpers in `Utils/FileDialogs.*` and wired the `Asset Browser` to expose `Save Scene Snapshot` and `Load Scene Snapshot` actions instead of remaining a read-only validation-scene list.
- **Completed:** Added runtime apply paths so loading a snapshot now restores mesh definitions, mesh instances, spheres, standalone triangles, background mode, scene label/description, and camera values through one coherent scene/camera update path.
- **Completed:** Preserved scope discipline: this slice still stops at local scene snapshot persistence and validation-scene workflow, not external mesh import, materials, denoise, EXR/output jobs, glass, mist, or caustics.
- **Deferred Intentionally:** Full Render settings/job serialization, external mesh import, richer scene asset references, and transport integrators remain for later phases.
- **Build Note:** Source compilation passed for this slice, but the final Release link was blocked by a running `ModularStudioStack.exe` holding the output binary open.
- **Next Likely Target:** Close the running app, verify the new snapshot workflow in-app, then continue the next Render runtime step only after snapshot save/load and custom-scene reopening behave correctly.

### [2026-04-17] - Render Phase 3 Slice: Mesh Definitions, Instances, And Flattened Multi-Model BVH Input
- **Completed:** Added organized mesh-runtime support under `src/RenderTab/Runtime/Geometry` for reusable `RenderMeshDefinition` data, `RenderMeshInstance` transforms/tints, and CPU-side flattening into the same resolved triangle path already used by the debug renderer.
- **Completed:** Extended Render scene templates and runtime rebuild logic so validation scenes can now carry built-in mesh definitions and multiple transformed model instances while still rebuilding one shared primitive-ref list and flat BVH.
- **Completed:** Updated compute-preview uploads so the GPU triangle buffer now comes from resolved scene triangles, which lets standalone triangles and mesh-backed triangles coexist without a separate renderer path.
- **Completed:** Reworked `Outliner`, `Inspector`, `Statistics`, and `Asset Browser` so mesh instances are visible, selectable, editable, and countable from the existing Render workspace panels.
- **Completed:** Kept the scope disciplined: this slice still stops at geometry/runtime correctness and multi-model scene plumbing, not path tracing, materials, denoise, EXR/output jobs, glass, mist, or caustics.
- **Deferred Intentionally:** External mesh import, richer scene serialization, real material/light systems, and transport integrators remain for later phases.
- **Next Likely Target:** Validate the current mesh-instancing geometry/BVH slice in-app, then continue the next Render runtime step only after instance transforms, counts, and traversal debug views behave correctly.

### [2026-04-17] - Render Phase 3 Slice: Transform-Aware Geometry And Local-Space Editing
- **Completed:** Added organized geometry support under `src/RenderTab/Runtime/Geometry` for shared math types, per-object `RenderTransform`, and local-space primitive definitions that resolve into world-space data through one CPU-side path.
- **Completed:** Updated Render scene change detection, bounds generation, primitive references, and compute-preview uploads so spheres and triangles now rebuild and upload from transform-resolved data instead of assuming authoring-time world-space geometry.
- **Completed:** Reworked the Render `Inspector` so spheres and triangles expose translation, rotation, scale, local geometry editing, and read-only world-space summaries driven by the same resolve helpers used by the BVH/debug renderer.
- **Completed:** Converted validation-scene templates to instantiate their current objects through the new transform-aware primitive layout while preserving the existing debug-scene compositions.
- **Completed:** Kept the scope disciplined: this slice still stops at geometry/runtime correctness and object transforms, not path tracing, lights, materials, denoise, EXR/output jobs, glass, mist, or caustics.
- **Deferred Intentionally At The Time:** Real mesh import, richer scene serialization, instancing beyond the current validation-scene runtime, material/light systems, and transport integrators remained for later phases.
- **Next Likely Target At The Time:** Add reusable mesh definitions and mesh-instance support on top of the local-space primitive path before taking on more complex renderer features.

### [2026-04-17] - Render Phase 3 Slice: Validation Scenes, Primitives, And BVH Debug Preview
- **Completed:** Added organized scene-runtime support for validation scenes, analytic spheres, editable triangles, primitive references, bounds, and a CPU-built flat BVH under `src/RenderTab/Runtime/Geometry`, `Runtime/Bvh`, and `Runtime/Debug`.
- **Completed:** Upgraded the Render tab from a scene-less compute backdrop to a real intersection-driven debug preview that uploads scene data through SSBOs and renders spheres/triangles through either brute-force traversal or BVH traversal in the compute shader.
- **Completed:** Reworked `Outliner`, `Inspector`, `Statistics`, `AOV / Debug`, and `Asset Browser` so the user can pick validation scenes, inspect/edit scene primitives, switch debug views, toggle BVH traversal, and inspect scene/BVH counts from the existing panel shell.
- **Completed:** Kept the scope disciplined: this slice stops at geometry/intersection/BVH correctness and validation-scene workflow, not path tracing, MIS, lights, materials, denoise, EXR, glass, mist, or caustics.
- **Deferred Intentionally:** Mesh import, richer scene serialization, real material/light systems, and transport integrators still remain for later phases.
- **Next Likely Target At The Time:** Extend the Phase 3 geometry/runtime foundation with transform-aware scene objects before taking on more complex renderer features.

### [2026-04-16] - Render Tab Phase 2 Skeleton And Compute Viewport
- **Completed:** Raised the native app's render baseline to an explicit OpenGL 4.3 core context in `AppShell`, updated the ImGui OpenGL backend shader version, and extended the custom `GLLoader` with compute-era symbols such as `glDispatchCompute`, `glBindImageTexture`, `glTexStorage2D`, `glMemoryBarrier`, and SSBO-era buffer hooks.
- **Completed:** Added render-local runtime types under `src/RenderTab/Runtime/`: `RenderScene`, `RenderCamera`, `RenderSettings`, `RenderBuffers`, `RenderJob`, and `ComputePreviewRenderer`.
- **Completed:** Replaced the old shell-only viewport placeholder with a deterministic compute-driven preview texture that responds to resolution, camera values, display mode, background mode, and accumulation state without introducing any transport logic.
- **Completed:** Wired `Outliner`, `Inspector`, `Settings`, `Render Manager`, `Statistics`, `Console`, `AOV / Debug`, and `Asset Browser` to the new runtime skeleton so the tab now has one coherent Phase 2 state path instead of isolated placeholders.
- **Completed:** Added explicit revision-driven accumulation reset handling for scene, camera, settings, and resolution changes, plus a single active preview job with `Start Preview`, `Cancel Preview`, `Reset Accumulation`, status text, sample count, and reset-reason reporting.
- **Completed:** Preserved the Phase 1 panel lifecycle work: panel visibility still persists through `Render/render_tab_state.json`, while ImGui `.ini` layout persistence continues handling docked and detached panel layout.
- **Deferred Intentionally:** This phase still excludes BVH construction, real path tracing, denoising, EXR/output presets, asset import, materials, media, glass, mist, caustics, and any multi-job Render Manager queue.
- **Next Likely Target:** Build the next Render phase on top of this skeleton only after validating runtime behavior in-app, focusing on scene/runtime expansion and renderer plumbing instead of jumping straight into transport features.

### [2026-04-16] - Render Tab Phase 1 Shell And Panel Lifecycle
- **Completed:** Replaced the old `3D Studio` placeholder root tab with a real `Render` workspace driven by a lightweight tab-descriptor registry in `AppShell`.
- **Completed:** Added a dedicated `src/RenderTab/` module with a panel registry, per-panel placeholder implementations, a render-only Window menu, a toolbar strip, and a default dock-layout helper instead of keeping the tab inline in `AppShell`.
- **Completed:** Added local JSON-backed `RenderTabState` persistence for panel open/closed state, toolbar visibility, and the one-time default-layout-applied flag, stored under `Render/render_tab_state.json`.
- **Completed:** Wired the Phase 1 render panel set and default visibility rules: visible by default are `Viewport`, `Outliner`, `Inspector`, `Settings`, and `Render Manager`; hidden by default are `Statistics`, `Console`, `AOV / Debug`, and `Asset Browser`.
- **Completed:** Kept the Render viewport intentionally deterministic and shell-only with no scene/runtime objects, compute dispatch, BVH, camera system, or transport code in this phase.
- **Completed:** Updated the Stack docs/checklists so the companion render implementation plan remains the ordering source of truth and Phase 1 is explicitly recorded as UI/lifecycle work only.
- **Deferred Intentionally At The Time:** Render Phase 2 types such as `Scene`, `Camera`, `RenderSettings`, `RenderBuffers`, and `RenderJob`, plus any compute-driven viewport path, were still deferred until the shell was validated.
- **Next Likely Target At The Time:** Start Render Phase 2 with the renderer skeleton and scene backbone only, while keeping BVH, actual path tracing, glass, mist, and caustics out of scope until that skeleton was stable.

### [2026-04-16] - Library Preview Orientation And Saved-Render Stability Fix
- **Completed:** Normalized `GetSourcePixels()` back to file-oriented row order before `.stack` source-image encoding so future Library `before` previews do not inherit the temporary source-snapshot inversion regression.
- **Completed:** Changed fullscreen Library project previews to prefer the saved rendered asset for the `after` pane whenever it exists, instead of forcing a large live rerender path that could still produce blank results on heavy projects.
- **Completed:** Unified the fullscreen compare widget back onto one texture-orientation convention for both sides so saved render previews and source previews stop fighting different UV assumptions.

### [2026-04-16] - Save/Preview Reliability Pass And Image Breaks
- **Completed:** Ported `Image Breaks` into the native editor with row/column displacement, square-block displacement, blur, and seed controls that map cleanly to the web shader path.
- **Completed:** Updated `Palette Reconstructor` so users can explicitly `Add Color` to the end of the local palette and remove individual palette entries instead of relying only on a count slider.
- **Completed:** Removed the extra source-image GPU readback from `Save To Library` by caching source pixels in the render pipeline and moving source-image packaging, thumbnail generation, and project/asset file writing into the worker-side save path.
- **Completed:** Kept the unavoidable rendered-output snapshot on the main thread for Library saves, but cut out the previous extra main-thread packaging work that made larger saves hitch harder than necessary.
- **Completed:** Hardened fullscreen Library project previews for larger projects by keeping the saved rendered asset path available as the stable `after` preview source when native rerendering is not reliable enough.
- **Deferred Intentionally:** `Expander` still needs real output-canvas resizing support in the native pipeline, so it remains separate from the current fixed-dimension fullscreen-pass layers.
- **Next Likely Targets:** Continue with the remaining non-Stitch editor work that still needs dedicated support beyond the current pass list: `Expander`, `Text Overlay`, `Generator`, and then the higher-risk `Background Remover & Patcher`.

### [2026-04-16] - Native Layer Migration Batch: Heatwave, Palette, Edge, Airy
- **Completed:** Ported `Heatwave & Ripples` into the native editor as a direct optics pass with intensity, phase, scale, and direction controls.
- **Completed:** Ported `Palette Reconstructor` into the native editor using a layer-local editable palette bank, smoothing controls, and palette-count adaptation in place of the web app's shared document palette flow.
- **Completed:** Ported `Edge Effects` into the native editor with both edge-overlay and saturation-mask modes, including bloom spread and smoothness controls for the saturation-mask path.
- **Completed:** Ported `Airy Bloom` into the native editor as the core diffraction-style bloom pass with intensity, aperture, threshold, threshold-fade, and cutoff controls.
- **Completed:** Wired the four new layers into the native layer factory, serialization/deserialization path, and the `Layers` tab so they can be added normally and survive project save/load cycles.
- **Deferred Intentionally:** `Text Overlay` remains separate because the web version is not just a fragment pass; it depends on generated text surfaces plus on-canvas move/resize/rotate interaction that still needs a dedicated native text subsystem.
- **Next Likely Targets:** Continue with the remaining non-Stitch editor work that is still outside the native pass list: `Expander`, `Text Overlay`, `Generator`, and then the higher-risk `Background Remover & Patcher`.

### [2026-04-16] - Native Layer Migration Batch: Tilt-Shift, Dither, Compression, Cell
- **Completed:** Ported `Tilt-Shift Blur` into the native editor as a dedicated fullscreen-pass layer with native inspector controls for focus center, focus radius, transition, blur type, and blur strength.
- **Completed:** Ported `Dithering` into the native editor with ordered/noise/native-error-diffusion-style modes, controllable bit depth and palette size, and a local inspector-managed palette bank adapted from the web app's shared palette behavior.
- **Completed:** Ported `Compression` into the native editor as a degradation-style fullscreen pass with method, quality, block size, blend, and iteration controls adapted to the current native render model.
- **Completed:** Ported `Cell Shading` into the native editor with quantization controls, edge detection styles, band controls, and a real color-preserve blend instead of copying the web executor's checkbox-like behavior.
- **Completed:** Wired the four new layers into the native layer factory, serialization/deserialization path, and the `Layers` tab so saved projects and Library summaries resolve their native names correctly.
- **Follow-On Direction Recorded At The Time:** Continue with the remaining non-Stitch editor passes that still fit the current fullscreen-pass model before taking on the heavier authoring tools, especially text, generated overlays, and the broader authoring-focused layers.

### [2026-04-16] - Worker-Backed Responsiveness Pass
- **Completed:** Added a shared async subsystem under `src/Async/` with a bounded worker pool and a main-thread completion pump wired through `AppShell`.
- **Completed:** Moved source-image decode, rendered PNG export, Library project load, fullscreen project preview prep, fullscreen asset preview prep, and library bundle import/export onto request-based worker flows where possible.
- **Completed:** Kept OpenGL texture creation, pipeline execution, and project apply on the main thread so the current single-context GLFW/OpenGL architecture remains valid.
- **Completed:** Added latest-request-wins handling for Library preview and project-load requests so stale background completions do not overwrite newer user intent.
- **Completed:** Added visible task-state feedback in the Library header, fullscreen preview actions, Canvas controls, and the Editor save flow.
- **Completed:** Updated the migration docs to explicitly require balancing new native features with organization and worker-backed responsiveness.

### [2026-04-16] - Binary Project Format And Library Bundle Pass
- **Completed:** Replaced JSON/base64 `.stack` project storage with a native chunked binary container that separates metadata, thumbnail, source image, and pipeline sections.
- **Completed:** Added a dedicated persistence subsystem under `src/Persistence/` with custom typed-value encoding, file header/versioning, and section tables designed to scale to future project kinds.
- **Completed:** Updated Library scanning so it can read project metadata and thumbnails without always loading the full source image section just to render the Library grid.
- **Completed:** Added binary whole-library export into `.stacklib` bundle files containing projects, rendered assets, and metadata.
- **Completed:** Added binary whole-library import from `.stacklib` bundle files and hooked it into the Library header buttons.
- **Completed:** Kept legacy JSON `.stack` files readable during the migration so older saved projects still load and are rewritten into the new format on future native saves.
- **Completed:** Fixed a post-load Library crash caused by clearing the fullscreen preview selection in the same ImGui frame that still rendered the rest of the popup UI.

### [2026-04-16] - Library Assets And Layout Pass
- **Completed:** Fixed the fullscreen Library compare view so the rerendered `after` image is oriented correctly against the original source image.
- **Completed:** Made the Library filters rail horizontally resizable instead of fixed-width.
- **Completed:** Added shared native file-dialog helpers so the Editor canvas export flow and Library asset-download flow use the same Windows dialog path cleanly.
- **Completed:** Updated Library project saves so each project also writes its latest full rendered output into `Library/Assets` using a stable filename tied to the saved project file.
- **Completed:** Added real `Assets` tab population in the Library by scanning rendered asset files on disk, generating thumbnails, and opening them in a fullscreen native preview.
- **Completed:** Added asset download from the fullscreen Library asset view and linked asset cards back to their saved editor projects.
- **Completed:** Updated project delete flow so removing a Library project also removes its linked rendered asset image.

### [2026-04-16] - Library Fullscreen Workflow Pass
- **Completed:** Reworked the Library project overlay into a real fullscreen inspector instead of a simple enlarged thumbnail view.
- **Completed:** Added automatic on-open full-quality project rendering for Library previews by replaying the saved source image and saved editor pipeline through the native editor engine.
- **Completed:** Added a loading spinner state for fullscreen Library previews while the full-quality rendered result is being generated.
- **Completed:** Added interactive before/after compare in fullscreen Library preview using the original source image against the rendered final edit.
- **Completed:** Added project rename-in-place from the fullscreen Library window.
- **Completed:** Added project delete with explicit confirmation from the fullscreen Library window.
- **Completed:** Added a parsed pipeline summary panel in the fullscreen Library window so saved layer settings are readable without loading the project into the editor first.
- **Completed:** Changed the confusing `Save Library` button behavior into explicit `Refresh Now` wording and added automatic disk refresh while the Library view is open.

### [2026-04-16] - Native Layer Migration Pass And Doc Reset
- **Completed:** Ported these additional web Editor layers into the native pipeline:
  1. `3-Way Color Grade`
  2. `HDR Emulation`
  3. `Chromatic Aberration`
  4. `Lens Distortion`
- **Completed:** Wired the new layers into the native layer factory, serialization/deserialization path, and the `Layers` tab UI.
- **Completed:** Updated `Stack/CMakeLists.txt` so normal local rebuilds reuse cached FetchContent sources under `Stack/build/_deps/*-src` instead of trying to contact GitHub every configure, and so newly added source files are picked up during reconfigure.
- **Completed:** Reframed `AI_CONTEXT.md` and `README.md` around the actual migration/adaptation task instead of presenting the current native app like a final, stable spec.
- **Follow-On Direction Recorded At The Time:** Continue with the remaining editor-only web layers that fit the current fullscreen-pass model cleanly before taking on heavier authoring workflows such as text, generator, and background-removal tooling.

### [2026-04-16] - Professional Diagnostics & Workflow Refinement
- **Completed:** Implemented Professional Scopes Panel including Histogram (R, G, B, Lum), Vectorscope (Y'CbCr), and RGB Parade.
- **Completed:** Added high-performance GPU-to-CPU feedback loop for scopes. Uses a 256x256 downsampled blit for zero-latency analysis.
- **Completed:** Refined Library System with persistent background loading and flipping fixes for thumbnails.
- **Completed:** Implemented High-Resolution (HQ) Previews in the Library that load full-res assets on demand.
- **Completed:** Added "Smart Redirect" in Library. Loading a project now automatically switches the shell to the Editor tab.
- **Completed:** Stabilized Workspace Layout. Locked the Editor container into the application shell to prevent accidental workspace undocking.
- **Completed:** Extended custom `GLLoader` with support for `glBlitFramebuffer` and other GL 3.0+ symbols.

### [2026-04-15] - Viewport Interaction & Pipeline Logic
- **Completed:** Implemented "Only Render Up To Active Layer" in the Canvas tab. This slices the processing pipeline in real-time based on the user's selection in the stack.
- **Completed:** Added Advanced Viewport Zoom. Users can scroll to zoom in/out (up to 100x magnification).
- **Completed:** Implemented "Smart Pan" where the zoomed-in view tracks the mouse position naturally, allowing for quick inspection of different image regions.
- **Completed:** Added "L" Hotkey to lock/unlock viewport transformation (Zoom level and Pan position).
- **Completed:** Implemented "Hover to Compare" logic in the viewport. When the user hovers over the canvas, the processed stack fades out smoothly to reveal the original source image.
- **Completed:** Implemented Intelligent Layer Naming. Adding multiple instances of a module (e.g. Blur) now automatically suffixes them with `(2)`, `(3)`, etc.
- **Completed:** Converted the rendering pipeline from hardcoded to fully dynamic. Users now start with an empty stack and add layers as needed.
- **Completed:** Implemented Drag-and-Drop layer reordering in the `PipelineTab`. Sequential order is updated in real-time on the GPU.
- **Completed:** Scaffolded the "Program Context Tabs" (Editor, Library, Composite, 3D Studio) at the root level.
- **Completed:** Scoped the Editor UI into a self-contained "Workspace" window with its own internal docking logic, separating its concerns from future modules.
- **Completed:** Updated `LayersTab` for high-efficiency workflow (flat listing of all modules with one-click injection).

### [2026-04-15] - Engine Pipeline & First Layers
- **Completed:** Implemented the `RenderPipeline` using sequential ping-pong Framebuffer Objects (FBOs). This strictly enforces the "Layer N+1 only sees Layer N" architecture.
- **Completed:** Integrated `stb_image` for native CPU image decoding and OpenGL texture creation.
- **Completed:** Implemented `GLLoader` a custom lightweight OpenGL extension loader using `glfwGetProcAddress` to eliminate external dependencies (GLAD/GLEW).
- **Completed:** Built functional UI for `CanvasTab` including a native Windows File Dialog (`comdlg32`) for image loading.
- **Completed:** Implemented the first 3 chronological rendering modules:
  1. `CropTransformLayer`: Crop, 360° Rotate, Flip H/V.
  2. `AdjustmentsLayer`: Brightness, Contrast, Saturation, Warmth, Sharpening.
  3. `VignetteLayer`: Intensity, Radius, Softness, Color.
- **Completed:** Wired the `SelectedTab` inspector to directly control live GPU uniforms for the active layer.
- **Completed:** Successfully compiled the full native application with real-time GPU processing.

### [2026-04-15] - Editor UI Layout Initialization
- **Completed:** Validated zero-setup CMake execution and GLFW/ImGui dockspace rendering natively with the user.
- **In Progress:** Structuring the core `Editor` module. Creating strictly partitioned class files for the layout (Sidebar, Viewport, and the 5 sub-tabs) to maintain high organization and prevent monoliths.
  
### [2026-04-15] - Engine Scaffolding
- **Completed:** Generated the completely separated `/Stack/` root directory.
- **Completed:** Created `AI_CONTEXT.md` and `DEV_LOG.md` strictly separating the codebase from the Web environment legacy code.
- **Completed:** Scaffolded a Zero-Setup `CMakeLists.txt` relying on `FetchContent` to safely download GLFW and ImGui natively via Git, eliminating local dependency configuration headaches.
- **Completed:** Built `main.cpp` entrypoint and mapped a decoupled `AppShell` architecture.
