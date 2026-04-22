# Companion Implementation Plan for the Render Tab

This document turns the three reference videos by entity["people","Sebastian Lague","youtube creator"] on entity["organization","YouTube","video platform"] into a single, ordered implementation plan for the new render tab inside your existing Dear ImGui-based editor. The scope deliberately combines the first video’s baseline renderer features such as camera rays, diffuse and specular transport, progressive rendering, triangles, anti-aliasing, and depth of field; the second video’s BVH construction and traversal work, GPU-friendly data layout, iterative traversal, SAH thinking, and multi-model support; and the third video’s smooth glass, bug fixes, NaN handling, absorption, frosted glass, glass-ball validation, caustics, and spectral dispersion. Per your requirement, water and raindrop-specific work is excluded, but mist is included as a first-class participating-medium feature. citeturn5search0turn5search3turn5search2turn7search0

## Project charter

The most important constraint is architectural, not graphical: this must be **one integrated renderer** inside **one render tab**, not a pile of separate special-purpose renderers for glass, mist, or caustics. Preview and final renders may use different presets or even different integrator modes, but they must still share the same scene data, camera model, materials, media, lights, AOVs, output pipeline, and editor controls so that a single shot can combine rough glass, tinted absorption, metallic objects, emissive lights, mist, depth of field, and dispersion in one render. That requirement lines up with the rendering literature as well: BSDFs, media, and integrators are intended to compose in one transport system rather than being authored as disconnected effect pipelines. citeturn8search0turn8search5turn7search1turn9search4

The quality bar should also be explicit from the start. “Performance optimization” in this project should mean acceleration structures, better sampling, iterative GPU-friendly execution, asynchronous asset work, and optional denoising, not shortcuts that materially change light transport. OpenEXR is designed for scene-linear HDR image data; ACEScg is a scene-linear CGI working space where values above 1.0 are expected and should not be clamped during rendering; and Open Image Denoise is explicitly designed for ray-traced images and supports beauty plus albedo and normal auxiliary buffers. In other words, the renderer should remain physically grounded internally, and any denoiser or display transform should be layered on top of that rather than replacing it. citeturn16search0turn16search2turn16search3turn17search0turn17search2

The implementation discipline should be just as strict as the rendering discipline. The three reference videos already imply a clean build order: first the basic ray/path tracing foundations, then acceleration and GPU-friendly structure, then advanced optics and caustics. Preserving that order in the codebase is what will keep an AI assistant from “trying to do everything at once,” which is exactly the failure mode you want to avoid. citeturn5search0turn5search3turn5search2

## Architecture for one integrated renderer

Target **OpenGL 4.6 core** if available, with **OpenGL 4.3** as the absolute minimum because compute shaders and shader storage buffer objects are core from 4.3 onward. Use compute shaders for the renderer proper, SSBOs for scene data and per-frame structures, image load/store for accumulation targets, and the correct `glMemoryBarrier()` calls whenever compute outputs are consumed by later texture, image, or SSBO reads. Compute shaders are the right fit here because they give you arbitrary write access and let the renderer operate as a general data-processing pipeline rather than forcing it into raster output semantics. citeturn10search3turn10search0turn10search1

Keep the **primary OpenGL context on the UI thread** and treat it as the owner of all viewport rendering, Dear ImGui drawing, tone mapping, and on-screen compositing. OpenGL contexts are thread-local, and a single context cannot be current on multiple threads at the same time. If you later decide to add shared contexts for background uploads, the visibility rules are stricter than many engines assume: cross-context updates require explicit synchronization, and shared-object contents may need rebinding in the consuming context before changes become visible. For this reason, the simplest and safest first version is: UI thread owns rendering, worker threads handle import, texture decode, BVH build jobs, EXR writing, and denoise execution. citeturn11search0turn11search4turn11search5

Use the Dear ImGui docking branch from the beginning. Docking is available in that branch, is described by the project as well maintained and recommended, and the same branch also contains multi-viewports. That matters because your requirement is not just multiple panels, but panels that can be hidden, reopened, docked, undocked, and popped out as native OS windows. With OpenGL, multi-viewports require the additional platform-window rendering calls and restoration of the current GL context after rendering detached windows. citeturn6search1turn6search0turn22search0

On the GPU side, start with a **single-kernel progressive path tracer** because it is the fastest route to correctness. However, leave the data model ready for a later **wavefront** upgrade. The reason is straightforward: GLSL does not support recursion, so both path integration and BVH traversal must already be iterative; and the wavefront path tracing literature shows why a large monolithic GPU kernel can become inefficient as control-flow divergence and register pressure increase. In practice, that means version one should be an iterative megakernel for simplicity, while version two may split work into queues for extension rays, shadow rays, and specialized shading passes if profiling proves it worthwhile. citeturn12search0turn13search0

The core runtime objects should be deliberately few and stable: `Scene`, `Camera`, `Material`, `Medium`, `Light`, `RenderSettings`, `RenderBuffers`, `RenderJob`, `AOVConfig`, and `RenderTabState`. If a new feature cannot be expressed as an extension of one of those objects, the default assumption should be that the feature is being added the wrong way. That rule is what will prevent “glass mode,” “mist mode,” and “caustics mode” from turning into disconnected architectural branches. The renderer should vary by **materials, media, and integrators**, not by separate apps hiding inside the same app. citeturn8search0turn8search5turn7search2turn15search1turn15search0

## Codebase and file layout

Since this is being built inside an already existing Dear ImGui window system with a render tab ready for it, the cleanest approach is to keep the first full implementation inside **one dedicated module folder** for that tab instead of scattering new files all over the whole repository. That gives you local cohesion while the feature set is still moving quickly, and it cleanly separates editor-panel code, render-core code, scene data, GPU shaders, job execution, and tests. The layout below is intentionally biased toward clarity and phase-by-phase growth rather than premature abstraction. citeturn6search1turn11search0turn10search3turn8search3

```text
src/
  editor/
    tabs/
      render_tab/
        render_tab.cpp
        render_tab.h
        render_tab_state.h
        render_tab_state.cpp

        layout/
          render_dock_layout.cpp
          render_dock_layout.h
          panel_registry.cpp
          panel_registry.h

        panels/
          viewport_panel.cpp
          viewport_panel.h
          outliner_panel.cpp
          outliner_panel.h
          inspector_panel.cpp
          inspector_panel.h
          settings_panel.cpp
          settings_panel.h
          render_manager_panel.cpp
          render_manager_panel.h
          statistics_panel.cpp
          statistics_panel.h
          console_panel.cpp
          console_panel.h
          aov_panel.cpp
          aov_panel.h
          asset_browser_panel.cpp
          asset_browser_panel.h

        core/
          render_app_services.h
          render_command_bus.h
          render_events.h
          render_persistence.cpp
          render_persistence.h
          render_profiles.json
          default_layout.ini

        runtime/
          device/
            gl_renderer_device.cpp
            gl_renderer_device.h
            gl_resource_cache.cpp
            gl_resource_cache.h
            gl_sync.cpp
            gl_sync.h

          scene/
            scene.cpp
            scene.h
            scene_ids.h
            scene_serializer.cpp
            scene_serializer.h
            transform.cpp
            transform.h
            mesh.cpp
            mesh.h
            camera.cpp
            camera.h
            light.cpp
            light.h
            environment.cpp
            environment.h

          geometry/
            intersection.h
            sphere.h
            triangle.h
            aabb.h
            geometry_upload.cpp
            geometry_upload.h

          bvh/
            bvh_types.h
            bvh_builder_cpu.cpp
            bvh_builder_cpu.h
            bvh_refit.cpp
            bvh_refit.h
            bvh_upload.cpp
            bvh_upload.h

          materials/
            material_types.h
            material_system.cpp
            material_system.h
            texture_decode.cpp
            texture_decode.h
            ior_tables.cpp
            ior_tables.h

          media/
            medium_types.h
            medium_system.cpp
            medium_system.h
            density_grid.cpp
            density_grid.h

          integrators/
            integrator_mode.h
            path_trace_common.h
            caustics_mode.h

          buffers/
            accumulation_buffer.cpp
            accumulation_buffer.h
            aov_buffers.cpp
            aov_buffers.h
            render_targets.cpp
            render_targets.h

          jobs/
            render_job.cpp
            render_job.h
            render_queue.cpp
            render_queue.h
            render_manager.cpp
            render_manager.h

          output/
            exr_writer.cpp
            exr_writer.h
            oidn_runner.cpp
            oidn_runner.h
            image_export.cpp
            image_export.h

          debug/
            debug_views.cpp
            debug_views.h
            validation_scenes.cpp
            validation_scenes.h
            convergence_metrics.cpp
            convergence_metrics.h

        shaders/
          path_trace.comp
          tone_map.frag
          fullscreen_quad.vert
          intersect_common.glsl
          bvh_traversal.glsl
          bsdf_lambert.glsl
          bsdf_conductor.glsl
          bsdf_dielectric.glsl
          bsdf_thin_dielectric.glsl
          bsdf_rough_dielectric.glsl
          medium_homogeneous.glsl
          medium_grid.glsl
          spectral.glsl
          lights.glsl
          rng.glsl
          debug_views.glsl

        tests/
          scenes/
            cornell_box.json
            glass_slab.json
            glass_ball.json
            frosted_panel.json
            tinted_thickness.json
            prism_dispersion.json
            refractive_caustic.json
            mist_volume.json
          image_baselines/
          integration_tests.md
```

The most important naming rule is negative rather than positive: **do not create** folders such as `glass_renderer/`, `mist_renderer/`, or `caustics_renderer/`. Glass belongs in `materials/` and the BSDF shader library. Mist belongs in `media/`. Caustics belong either in the main path tracer’s sampling logic or in an integrator selection inside `integrators/`, but they still consume the same `Scene`, `Material`, `Medium`, `Camera`, and output structures. That is exactly how physically based rendering references present the problem: material scattering, media, and light transport algorithms are separate concerns that interoperate through a shared scene and transport framework. citeturn7search1turn23search0turn8search5turn15search1turn15search0

## Phased implementation plan

The entire project should proceed through hard phases with explicit stop rules. A phase is not finished when code compiles; it is finished when its validation scenes pass and when the next phase can depend on it without hidden assumptions. That is the mechanism that keeps development organized and prevents an AI assistant from jumping into absorption, spectral transport, denoising, and multi-viewports all at the same time. The sequence below is the safest order for the specific scope you described. citeturn5search0turn5search3turn5search2turn7search2

1. **Foundation and panel lifecycle.** Build the `RenderTab` shell, the fullscreen dockspace, the panel registry, the persistent panel visibility state, the Window menu, toolbar toggles, and the default layout restore path. At this stage the Viewport can be backed by a dummy texture or a simple gradient. The stop rule is absolute: do not write transport code yet. The only exit criteria are that panels can be opened from buttons and menu items, closed from the title-bar X, docked and undocked cleanly, popped out into detached OS windows, and restored across application restarts using ImGui layout persistence and your own render-tab state serialization. citeturn6search1turn6search0turn21search2turn21search6turn22search0

2. **Renderer skeleton and scene backbone.** Add the render-facing `Scene`, `Camera`, `RenderSettings`, `RenderBuffers`, and `RenderJob` objects, plus a minimal compute dispatch that writes deterministic test output into the Viewport texture. Build accumulation reset rules now: camera movement, material edits, resolution changes, and environment changes must invalidate accumulation in a controlled way. The stop rule here is also strict: no BVH, no path tracer, no denoiser, no asset import complexity. The exit criteria are that the Viewport renders from a compute shader, resizing works, accumulation resets correctly, and the Render Manager can create, start, cancel, and report a preview job without any path-tracing logic yet. citeturn10search3turn10search0turn10search1turn11search0

3. **Geometry ingestion, intersections, and BVH.** Implement spheres, triangles, AABBs, object transforms, mesh upload, and a CPU-side BVH builder that produces a flat GPU-friendly node array. The reference BVH video is very clear about the progression you should follow: debug views first, then nested boxes, then traversal, then iterative traversal, then deeper trees, distance tests and child ordering, SAH, compact nodes, transforms, and support for multiple models. Keep those debug views permanently; they are not temporary scaffolding. The glass-ball milestone later depends on sphere correctness, and robust sphere intersection formulations are worth adopting because naive quadratic formulations can be numerically fragile. The stop rule is that **no dielectric work begins** until ray–sphere, ray–triangle, and BVH traversal are correct and visualized. citeturn5search3turn12search0turn19search2turn19search8

4. **Baseline path tracing and standard capabilities.** Once geometry and traversal are trustworthy, implement the real baseline renderer: camera-ray generation, Lambertian and emissive materials, environment and sky lighting, next-event estimation, multiple importance sampling, progressive accumulation, Russian roulette, anti-aliasing, glossy/specular reflection, and depth of field. This phase should also produce the first stable AOV set: beauty, albedo, normal, depth, material ID, object ID, and sample count or variance estimate. The first reference video’s progression and the pbrt path integrator guidance align very well here: get the fundamental transport loop and MIS right before moving on to difficult materials. If you want asset portability at this stage, use glTF metallic-roughness as the baseline opaque/PBR import path. The stop rule is: no glass while direct lighting, MIS, and the AOV pipeline are still moving targets. citeturn5search0turn7search2turn18search1turn18search0

5. **Dielectric correctness and the glass milestone.** This is the longest and most important milestone. Implement smooth dielectric Fresnel, Snell refraction, total internal reflection, medium entry/exit logic using the geometric normal, shading-frame evaluation for BSDFs, robust ray-origin offsets to avoid self-intersection, `isnan()` and `isinf()` debugging guards, Beer–Lambert absorption for solid tinted glass, thin-dielectric mode for sheet glass, and rough dielectric transmission for frosted glass. Keep the validation scenes brutally simple: glass slab, window pane, glass sphere over checkerboard or text, frosted panel, and a thickness ramp for tinted absorption. Use the glass ball as a permanent regression scene, and do not move on to caustics until clear glass, frosted glass, and absorbing glass all behave correctly in the same renderer. citeturn5search2turn9search4turn7search3turn23search0turn7search1turn23search2turn8search0turn19search1turn24search1turn24search0

6. **Spectral transport, refractive caustics, and mist.** Add wavelength-dependent transport next, not before. The reason is simple: RGB can fake coloration, but true dispersion and rainbow splitting require wavelength-dependent IOR. Follow the spectral path used in pbrt’s design: sample visible wavelengths, keep wavelength-dependent `eta`, and build a dispersion validation scene such as a prism or dispersive lens. For mist, introduce a homogeneous participating medium first with `sigma_a`, `sigma_s`, extinction, transmittance, and a Henyey–Greenstein phase function; only after that is validated should you add bounded local media or density grids. For caustics, choose one of three paths without breaking the single-renderer rule: use MNEE for focused refractive caustics, use BDPT for a broader unbiased upgrade, or use SPPM as a specialized caustics-capable mode if implementation risk and schedule push you there. All three still operate on the same scene, materials, media, and output system; they are integrator choices, not separate renderers. citeturn9search0turn9search4turn9search3turn9search9turn8search5turn8search1turn8search3turn14search5turn15search1turn15search0

7. **Output, denoise, render-management polish, and hardening.** Only after the transport stack is stable should you add final production features: OpenEXR multi-channel save, scene-linear output defaults, optional ACEScg-oriented color-management workflow, Open Image Denoise integration, per-job output presets, asynchronous EXR save jobs, tile or crop renders, and automation around test scenes and image baselines. The Render Manager should become a true job system here: preview and final render are not two engines, but two presets over the same renderer. For example, preview can run at lower spp with optional denoising and the same materials/media, while final can raise spp, enable spectral mode, or switch to the chosen caustics-capable integrator. Exit criteria are not “it can render a glass sphere,” but “one scene can combine emissive lighting, rough glass, absorption, mist, depth of field, metallic objects, and optionally spectral dispersion or caustics in one coherent output pipeline.” citeturn16search0turn16search2turn16search3turn17search0turn17search2turn6search1

## Editor window system and settings design

Every window should have an explicit persistent visibility entry and should be driven by the same pattern: `ImGui::Begin("Panel Name", &isOpen, flags)`. That single `bool` is what enables close-by-X behavior, toolbar buttons, menu toggles, and state persistence without inventing a second UI system on top of Dear ImGui. The dockspace should be created over the main viewport, and multi-viewports should be enabled so detached windows become true platform windows. Because OpenGL detached windows need context handling after `RenderPlatformWindowsDefault()`, make that logic part of the normal frame loop, not an afterthought. citeturn6search1turn6search0turn21search2turn21search6turn22search0

The default visible window set should stay compact and useful: **Viewport** in the center, **Outliner** on the left, **Inspector** on the right, **Settings** below or tabbed with Inspector on the right, and **Render Manager** across the bottom or lower right. Additional windows should exist but be hidden by default: **Statistics**, **Console**, **AOV/Debug**, and **Asset Browser**. This gives you the “helpful amount” of windows you asked for without making the first-run layout overwhelming. The crucial rule is that hidden panels are not deleted panels: every one of them must be recoverable from a Window menu, a toolbar strip, or a command palette, and every one of them must support docking and detaching. citeturn6search1turn6search0turn22search0

The settings model should be hierarchical and intuitive rather than one giant flat list. The **Inspector** should own object-local parameters: transforms, mesh assignment, material assignment, medium assignment, and per-object visibility flags. The **Settings** window should own renderer-wide defaults: resolution, spp targets, bounce limits, MIS and light sampling choices, spectral toggle, caustics mode selection, denoise defaults, tone mapping, and output defaults. The **Render Manager** should own per-job overrides and queue state: camera, crop, resolution, output path, whether EXR is written, whether denoising runs, and whether the job is preview or final. This separation matters because it keeps scene authoring, renderer configuration, and job orchestration from becoming one tangled panel. citeturn7search2turn16search2turn17search0

For material and medium authoring, expose physically meaningful controls first and only then expose advanced toggles. Opaque/PBR materials should begin with base color, roughness, metallic, emissive, normal, and occlusion. Dielectrics should then extend the same material system with IOR, thin-vs-solid mode, absorption coefficients, transmission roughness, and spectral-dispersion presets. Media should expose density, absorption, scattering, anisotropy `g`, bounds, and homogeneous-vs-grid mode. The value of doing this through one Inspector is that the artist or user can decide how much realism to apply per object without the engine changing modes behind the scenes. citeturn18search1turn23search0turn7search1turn8search5turn8search3

The AOV and denoise path deserves its own UI logic because transparent and specular scenes are one of the places where denoisers often mislead teams. Open Image Denoise explicitly notes that auxiliary albedo and normal features often work better for reflections and transparent surfaces if you follow perfect specular paths and store features from a subsequent non-delta hit rather than always using the first hit. That means the AOV/Debug window should let you inspect both the first-hit and specular-follow feature policies, and the Denoise section in Settings should make the policy visible instead of burying it in implementation detail. citeturn17search0

## Quality gates and expansion rules

The single best guardrail against losing the plot is this: **only one active phase at a time, and no new subsystem without a must-pass validation scene set**. The basic video already gives you transport and camera milestones; the BVH video gives you traversal and acceleration milestones; the glass/rainbow video gives you optics milestones. Keep that exact ladder in the code. If a phase is still failing its reference scenes, adding the next feature is not progress; it is just burying a known bug under more code. citeturn5search0turn5search3turn5search2

The validation scene pack should be considered part of the renderer itself. Keep a diffuse Cornell box for transport sanity, a glossy/depth-of-field scene for baseline camera and BSDF sanity, a triangle-heavy mesh scene for BVH stress, a glass slab for refraction direction sanity, a glass sphere for inversion and precision sanity, a frosted panel for rough transmission sanity, a tinted-thickness ramp for absorption sanity, a prism for dispersion sanity, a bright refractive-caustic scene for advanced transport sanity, and a mist scene for volume sanity. These scene templates are not optional extras; they are the quality gates that make future expansion safe. citeturn5search0turn5search3turn5search2turn7search0turn8search5

Your performance policy should stay conservative until feature parity is achieved. Quality-neutral or quality-improving optimizations are fair game from the beginning: BVHs, SAH-based build quality, iterative traversal, GPU-friendly node layouts, better light sampling, Russian roulette, asynchronous IO, and later wavefront scheduling are all directly compatible with high-quality output. By contrast, shortcuts that replace the intended transport model should either stay out entirely or remain clearly labeled defaults-off debug options: fake fog in place of participating media, screen-space refraction in place of dielectric transport, RGB-only “rainbows,” or transport-clamping tricks used to hide bugs instead of fixing them. citeturn5search3turn13search0turn7search2turn7search1turn8search5

If you follow this plan literally, the end state is not just “a path tracer that can render a few cool scenes.” It is an organized render-tab module whose UI can be closed, reopened, docked, and popped out cleanly; whose code layout stays understandable to an AI or a human contributor; and whose renderer can combine baseline path tracing, BVH acceleration, glass, frosted transmission, absorption, mist, and spectral dispersion in one scene with intuitive settings and high-quality output. That is the right foundation not only for matching the reference videos, but for expanding beyond them later without throwing the architecture away. citeturn6search1turn6search0turn7search1turn8search5turn9search4turn16search2