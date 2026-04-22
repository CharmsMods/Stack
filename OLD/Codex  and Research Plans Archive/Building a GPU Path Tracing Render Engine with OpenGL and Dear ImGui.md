# Building a GPU Path Tracing Render Engine with OpenGL and Dear ImGui

The most direct way to build what you want is a **progressive GPU path tracer** in **C++** using **OpenGL 4.6 compute shaders** for ray traversal and shading, **Dear ImGui** docking for the editor, and **worker threads** for scene import, BVH building/refitting, texture decoding, denoising, EXR writing, and render-job orchestration. If your target is the same family of effects shown in the referenced video by entity["people","Sebastian Lague","youtube creator"], the renderer must correctly implement smooth glass refraction and reflection, NaN-safe math, Beer–Lambert absorption for tinted glass, rough dielectric transmission for frosted glass, accurate sphere refraction tests, spectral dispersion for rainbows, and a dedicated strategy for refractive caustics. Per your request, I am excluding the water-surface and raindrop-specific parts and treating **mist** as participating media instead. citeturn4youtube12turn18view0turn8search2turn9search0turn10search0turn10search1turn12search2

The referenced video’s own chapter list is a very useful implementation checklist: **Glass**, **Glass Bugs**, **NaNs**, **Absorption**, **Frosted Glass**, **Glass Ball Test**, **Stack versus Stoch**, **Visualizing Caustics**, **Rainbow**, and **Spectral Experiment**. The public source repo associated with the project is described as a “fairly simple (and slow) path tracer,” which is exactly why it is a good conceptual baseline but not the final architecture you should copy one-for-one. Your engine should preserve the optics and debugging lessons, while adopting a stronger editor/runtime architecture than the original prototype. citeturn4youtube12turn18view0

## Scope to match from the video

To reproduce the same class of believable results, your renderer has to get a few physical and numerical details right at the same time. The optics side is governed by **Snell’s law**, **dielectric Fresnel**, total internal reflection, rough-microfacet transmission for frosted glass, wavelength-dependent index of refraction for dispersion, and distance-based transmittance for absorption. The Monte Carlo side is governed by next-event estimation, multiple importance sampling, wavelength sampling, and a dedicated plan for caustics. The engineering side is governed by explicit iterative path state, robust ray-origin offsets, careful normal handling, and aggressive invalid-value guards. citeturn11search0turn11search3turn15search4turn30search1turn33search1turn16search1turn31search1

The most important design decision is this: **do not define success as “a ray tracer that can render glass eventually.”** Define success as **an editor-backed reference renderer** that can stay interactive while accumulating preview samples, can export deterministic final jobs, and can tell you exactly why an image is wrong when it is wrong. That means the editor and render manager are not extra polish. They are part of the correctness story. citeturn10search0turn10search1turn23search0turn26search3

## Architecture that fits OpenGL and Dear ImGui

Use a **single primary OpenGL context** on the UI thread, because OpenGL contexts are thread-local and **one context cannot be current in multiple threads at the same time**. Keep the primary context responsible for the compute dispatches that render the current preview, the tonemap/display pass, and all Dear ImGui drawing. Use CPU worker threads for everything that does not strictly require the main context: file IO, asset parsing, triangle preprocessing, BVH construction or refit planning, denoise jobs, EXR output, and queue management. If you later want background GPU uploads, use explicitly shared contexts and explicit synchronization; the Khronos guidance is clear that shared-object visibility across contexts is not automatic and requires both GL sync and normal inter-thread coordination. citeturn27search0turn27search5turn27search3

On the GPU side, structure your renderer around **SSBO-backed scene buffers** and one or more HDR accumulation images. OpenGL compute shaders have no fixed outputs, so the shader must fetch its own inputs from textures, SSBOs, and other resources and explicitly write results to image load/store targets or storage buffers. SSBOs are large, writable, and appropriate for BVH nodes, triangles, materials, lights, transform arrays, render queues, and per-pixel path state if you later move to a wavefront design. After compute writes, use the correct `glMemoryBarrier()` bits before sampling those resources in later passes. citeturn8search2turn8search0turn9search0turn9search2

For the first playable version, I recommend a **single-kernel progressive path tracer**: one compute dispatch launches one sample per pixel per iteration, traces a bounded number of bounces iteratively, and accumulates into an `rgba32f` beauty buffer plus a small set of AOVs. That gets you to a correct image fastest. Once the feature set is stable, you can choose whether to stay with the megakernel or move to a **wavefront** design with specialized queues for extension rays, shadow rays, and material classes. The wavefront literature shows why this is often better on GPUs: divergence and register pressure make large monolithic kernels progressively worse as scene/material complexity rises. citeturn28search0turn8search2

A clean CPU/GPU split for version one looks like this:

```cpp
// UI / main thread owns the primary GL context
while (!quit) {
    pump_os_events();
    process_completed_jobs_from_workers();   // import, build, denoise, save
    apply_scene_updates();                   // upload dirty buffers/textures
    maybe_reset_accumulation_if_scene_changed();

    begin_imgui_frame();
    draw_dockspace_and_editor_windows();
    dispatch_path_tracer_compute();          // one sample per pixel or per tile
    composite_and_tonemap_viewport();
    render_imgui();
    present();
}
```

That architecture maps well to Dear ImGui docking, progressive accumulation, and a render manager that can maintain separate preview and final-frame jobs. It also keeps OpenGL’s context rules from becoming the source of hard-to-debug race conditions. citeturn10search0turn10search1turn27search0turn27search5

## Renderer core that should exist before advanced optics

Your base integrator should already be a **real path tracer**, not a “reflection/refraction demo.” That means: camera ray generation, triangle and sphere intersections, a BVH accelerator, emissive geometry and analytic lights, direct-light sampling with **next-event estimation**, and **multiple importance sampling** between BSDF sampling and light sampling. The pbrt reference specifically calls out that sampling both the BSDF and the light and weighting them with MIS substantially reduces variance compared with light-only sampling, which matters immediately for glass scenes with bright emitters and HDR environments. citeturn33search1turn33search0turn33search2

For geometry, use **triangles for production assets** and keep **analytic spheres and simple boxes** as permanent debug primitives. The sphere matters more than it sounds: the “glass ball test” only tells you anything useful if you can validate refraction against a shape whose optics are easy to reason about. For shading, distinguish **geometric normals** from **shading normals**. The pbrt conventions are especially useful here: the surface normal defines the outside/inside meaning for transmissive objects, while shading normals may be perturbed by interpolation or bump mapping. In practice, use the **geometric normal** to determine medium entry/exit and for robust ray spawning, and use the **shading frame** for BSDF evaluation. citeturn36search0turn36search2

Use **scene-linear HDR** internally from the start. Display through a tone-mapping step, but save final images and AOVs in **OpenEXR**, which is designed for scene-linear HDR data and supports multi-part/multi-channel workflows. If you want a color pipeline that scales cleanly, keep the renderer’s working data linear and expose a display transform in your Settings window rather than baking display-referred values into the transport itself. ACES documentation stresses that ACEScg is a scene-linear CGI working space and that values above 1.0 are expected and should not be clamped as part of rendering. citeturn26search3turn26search2turn25search1turn25search2

For material authoring and asset interchange, make **glTF 2.0 metallic-roughness** your baseline import target for common opaque assets, because the spec gives a portable, compact PBR material with base color, metallic, roughness, normal, occlusion, and emissive fields. Then extend your internal material model with what glTF core does not cover well enough for this project: solid dielectric glass, thin dielectric sheet glass, per-channel absorption, rough transmission, spectral IOR, and participating media overrides. citeturn24search0turn24search1turn34search0

## Glass, frosted transmission, caustics, rainbow, and mist

### Smooth glass

A correct smooth-glass implementation needs **exact dielectric Fresnel**, **Snell refraction**, and explicit handling of **total internal reflection**. This is not an optional detail. If Fresnel is wrong, your glass looks flat. If inside/outside handling is wrong, reflections will appear on the wrong side or transmission will break. If TIR is wrong, edge behavior will be visibly incorrect. The pbrt dielectric model performs reflection vs transmission sampling stochastically based on the Fresnel terms, which is exactly the strategy you want on the GPU as your default mode. citeturn11search0turn11search3turn34search0

For OpenGL specifically, **do not rely on recursion**. GLSL does not allow recursive function calls, which means your path integrator must be iterative. That fits this use case well: store a compact `PathState` struct per pixel sample, loop over bounces, and choose one event per bounce. If you want a debugging/reference mode that explicitly branches reflected and refracted paths, do it with a small explicit stack or a queued wavefront stage, not with function recursion. citeturn32search0turn8search2

A minimal GPU-side path state is enough for the first version:

```glsl
struct PathState {
    vec3 origin;
    vec3 dir;
    vec3 throughput;
    vec3 radiance;
    float etaCurrent;
    uint rngState;
    int bounce;
    bool insideMedium;
};
```

That state, plus a bounded iterative loop, is sufficient for smooth dielectrics, rough dielectrics, absorption, and even homogeneous volumes if you add medium coefficients and a phase/event flag. The point is to keep the path logic explicit and inspectable. citeturn32search0turn11search3turn12search2

### Robustness fixes for glass bugs and NaNs

The renderer will not survive glass without a **robust ray-origin offset**. The common “just add a tiny epsilon” trick is often scene-scale-dependent and fails in both directions: too small causes self-intersections, too large causes leaks and missed nearby geometry. The Ray Tracing Gems chapter on self-intersection exists precisely because this problem is pervasive and worth solving carefully. At minimum, offset along the geometric normal with a scale-aware strategy; ideally, adopt a robust method derived from floating-point error bounds. citeturn16search1turn16search2

You also need aggressive defenses against invalid floating-point values. In practice, NaNs and infinities in path tracers usually come from invalid square roots, division by near-zero PDFs or denominators in Fresnel/BTDF math, normalization of zero-length vectors, or bad texture inputs. GLSL exposes `isnan()` and `isinf()`, and you should use them in debug builds around path state and final sample contributions. If a path goes invalid, terminate it or zero its contribution rather than letting the value pollute the accumulation buffer. pbrt follows essentially the same philosophy at the film/sample boundary by detecting NaNs and infinities and replacing them with black. citeturn31search1turn31search0turn15search0

### Tinted and absorbing glass

To match the “absorption” part of the video, your glass cannot just be surface-only transmission. It needs **distance-based attenuation through the interior volume**. In rendering terms, this is the same transmittance model pbrt describes for homogeneous media: transmitted radiance falls exponentially with optical thickness, and in the homogeneous case this becomes Beer’s law. For tinted glass, use a per-channel or spectral absorption coefficient `sigma_a` and multiply throughput by `exp(-sigma_a * distanceInside)` for every segment traveled through the solid. citeturn15search4turn14search2

This also implies an important modeling distinction: **solid glass** versus **thin sheet glass**. Solid glass has an interior and therefore supports path-length-dependent absorption. Thin glass, like a window pane in many real-time production scenes, is often better modeled with a specialized thin-dielectric BSDF that collapses the two nearby interfaces into one efficient scattering model, as pbrt does. Support both. Use thin dielectric for architectural sheet glass and solid dielectric for prisms, spheres, dragons, bottles, and any object where thickness should visibly change color and brightness. citeturn34search0

### Frosted glass

Frosted glass is not “diffuse transparency.” It is **rough dielectric transmission**, best modeled with microfacet theory. The pbrt rough dielectric model uses a visible microfacet distribution, computes Fresnel at the sampled microfacet normal, and then chooses reflection or transmission stochastically. This is the right physical basis for the frosted-glass look in the video. Use a GGX/Trowbridge–Reitz roughness parameter, support anisotropy later if needed, and expose roughness in the Inspector. citeturn30search0turn30search1

If your goal is realism first, a single roughness should govern both reflection and transmission because they are manifestations of the same microsurface. If your goal is “match the exact artistic look from the experiment faster,” you can add an **optional non-physical override** that decouples reflected and transmitted roughness. Make it visibly labeled as an artistic override in the Inspector so you do not accidentally debug a non-physical material as though it were physical. That recommendation is an engineering judgment, but it sits cleanly on top of the physically based rough-dielectric core. citeturn30search1turn4youtube12

### Rainbows and spectral rendering

An RGB path tracer can fake dispersion, but it cannot produce a truly wavelength-driven rainbow. If you want the “rainbow” and “spectral experiment” chapters to hold up, move the integrator to **spectral transport**. The pbrt spectral framework samples visible wavelengths and explicitly supports **wavelength-dependent `eta`** for dielectrics; it notes that if `eta` varies with wavelength, different wavelengths refract differently and produce dispersion. That is the physically correct hook for rainbows through glass. citeturn13search2turn13search0turn29search0

For a practical GPU implementation, do not start with dozens of wavelengths per path. Start with a **hero-wavelength** or a very small stratified wavelength packet per camera sample, propagate that spectral throughput through the path, and only convert to display RGB after accumulation. The visible range in pbrt is approximately **380–780 nm**, which is a good working interval for this purpose. This approach gives you dispersion without exploding storage and bandwidth. citeturn13search2turn13search0

### Caustics

This is the feature most likely to separate a pretty preview from a convincing renderer. Plain camera-started path tracing **can** render caustics, but sharp refractive caustics are one of its classic weak cases. The pbrt bidirectional-path-tracing chapter explicitly points out that ordinary path tracing has high variance in difficult lighting situations and that **BDPT** handles such cases more robustly by connecting camera and light subpaths. For specifically **refractive caustics**, **Manifold Next Event Estimation** was introduced precisely to connect surfaces to lights across transmissive interfaces and is lightweight in memory compared with photon-mapping-style caches. If you want a practical progression, build in this order: baseline path tracing with NEE/MIS first, then add either **BDPT** for a general unbiased upgrade or **MNEE** as a targeted refractive-caustics feature. citeturn17search0turn35search1turn35search4

If you want the fastest path to strong caustics and can tolerate bias, **photon mapping / SPPM** remains a valid option. The pbrt photon-mapping discussion explicitly calls out why photon methods shine for glass-caustic cases: there are light-carrying paths that are effectively impossible for incremental unbiased methods to sample well, but forward-traced photons refracted through glass can densely cover the receiving surface and make the estimate practical. In other words, if your “Render Manager” needs a dedicated “Caustics mode,” SPPM is a perfectly reasonable specialized backend even if the default integrator is a path tracer. citeturn37search0

### Mist as participating media

For mist, start with a **homogeneous medium** and only move to a grid medium later. The pieces are standard: extinction `sigma_t = sigma_a + sigma_s`, exponential transmittance along a segment, and a phase function for in-scattering. The pbrt volume chapter describes transmittance and the Henyey–Greenstein phase function in exactly these terms, with `g > 0` corresponding to forward scattering. That is a good approximation for mist and fog-like media. citeturn15search4turn12search0turn12search2

A very usable first version of mist is this: put the whole scene or a bounded region inside a homogeneous medium, sample medium events along rays, use Henyey–Greenstein for scattering, and expose density and anisotropy in the Inspector. Once that works, add **grid media** for local fog volumes and god-ray setups, using a 3D density texture or imported voxel/grid data. pbrt’s `GridMedium` shows the basic pattern for spatially varying `sigma_a`, `sigma_s`, and emitted radiance. citeturn11search2turn12search0turn12search2

## Editor layout that will actually help you build this

Use the **Dear ImGui docking branch** and enable docking from day one. The project wiki explicitly says docking is in the docking branch, is well maintained, and is safe and recommended to use; it also includes multi-viewports. That makes it a good fit for the editor you described. citeturn10search0turn10search2

The editor should be arranged around a fullscreen dockspace and five persistent windows:

**Viewport** should show the tonemapped progressive render, camera controls, sample count, current exposure/display transform, a status overlay for render mode, and quick AOV switching. It should also show path-tracing health signals such as invalid-sample count, current bounce cap, and whether denoising is active. If you later enable Dear ImGui multi-viewports, remember the OpenGL backend note: additional platform windows require backing up and restoring the current GL context. citeturn10search1turn23search0

**Outliner** should display a scene graph of cameras, meshes, lights, volumes, materials, and render settings presets. Keep it structural rather than visual. The point is not to imitate a DCC package; the point is to let you see which object actually owns the medium, which material instance is attached where, and which lights are enabled for the current render job. citeturn10search0

**Inspector** should be component-driven. A selected mesh should expose transforms, material assignment, smooth-shading flags, medium assignment, visibility flags, and mesh statistics. A selected material should expose the physically meaningful parameters first: base color/albedo, metallic, roughness, emission, dielectric IOR, thin/solid mode, absorption coefficients, rough transmission, anisotropy, spectral-eta preset, and optional artist overrides. A selected medium should expose density, `sigma_a`, `sigma_s`, `g`, bounds, and whether it is homogeneous or grid-backed. citeturn24search0turn30search1turn12search0turn11search2

**Settings** should own renderer-wide controls: integrator choice, spp target, max bounces, Russian roulette threshold, light sampler mode, spectral toggle, wavelength mode, denoiser choices, color-management/display transform, and output format defaults. This window is also where you should put debug toggles for “use geometric normals for spawn,” “highlight NaN paths,” “view path length,” “view caustic-only contribution,” and “freeze random seed.” Those controls are worth their weight in gold when debugging glass. citeturn33search1turn31search1turn16search1

**Render Manager** should manage jobs rather than parameters. A job should capture camera, resolution, crop, spp or time budget, integrator, denoiser, AOV list, output path, and state. That lets you run a low-latency preview mode and a high-quality final-frame mode from the same scene. It also gives your worker threads something coherent to do: EXR writing, denoise execution, and job completion notifications should all flow through this panel. Open Image Denoise’s API is thread-safe, though operations on the same device are serialized, which makes a job-oriented design especially natural. citeturn23search0turn26search3

If you want the startup layout to feel like a real editor immediately, create the dockspace programmatically. Dear ImGui’s DockBuilder API is not polished, but the official wiki gives a workable example for splitting nodes and docking named windows. That is enough to create your intended top-level structure on first launch and then let the user’s `.ini` persistence take over. citeturn10search0

## Build order that minimizes rework

Build this engine in phases, and do not skip ahead.

Start with the **shell**: OpenGL initialization, fullscreen dockspace, the five editor windows, a camera, a dummy accumulation texture, and a tonemap/display path. Until the Viewport, Settings, and Render Manager can already talk to each other, everything after this will be harder to debug. citeturn10search0turn8search2turn26search3

Then build the **reference renderer kernel**: analytic spheres, triangles, BVH, lambertian + emissive materials, area lights or emissive quads, next-event estimation, MIS, progressive accumulation, and reset-on-change behavior. This gives you a trustworthy non-glass baseline and lets you test all the editor wiring against a stable image. citeturn33search1turn33search2turn9search0turn8search0

Then add **smooth glass** and nothing else: dielectric Fresnel, refraction, TIR, geometric-normal medium tests, ray-offset fixes, and NaN guards. Your validation scenes here should be a glass slab, a window pane, and a glass sphere over a checkerboard or text background. If those are wrong, do not move on. citeturn11search0turn34search0turn16search1turn31search1

Then add **absorption and rough transmission**: Beer–Lambert attenuation, solid vs thin dielectric distinction, rough dielectric transmission, and the Inspector UI for tinted/frosted materials. This is the phase where “plausible” glass becomes “convincing” glass. citeturn15search4turn30search1turn34search0

Then add **spectral mode**: wavelength sampling, wavelength-dependent `eta`, spectral accumulation, and a prism or dispersive lens test scene. Do not attempt rainbow-quality output in RGB mode and then retrofit the internals later; it creates exactly the kind of architecture debt you do not want in a renderer/editor project. citeturn13search0turn29search0

Then add **mist**: homogeneous medium first, then bounded local volumes, then optional grid media. At that point the render engine can already produce a broad range of cinematic “real life” atmosphere without touching water simulation. citeturn12search0turn12search2turn11search2

Only after all of that is stable should you add **advanced caustics**: BDPT, MNEE, or SPPM. If you implement one of these before the rest of the engine is numerically healthy, you will bury simple bugs under a complex transport algorithm and lose weeks. citeturn17search0turn35search1turn37search0

## Quality bar, output, and validation

You should treat validation scenes as part of the codebase, not as temporary tests. Keep at least these always available in the Outliner templates: Cornell box, glass slab, thin window, frosted cube, glass sphere inversion scene, prism/dispersion scene, tinted-thickness ramp, small bright-light caustic scene, and volumetric mist cone/beam scene. Those scenes map directly to the failure modes you care about: wrong normal orientation, bad offsets, broken Fresnel, wrong absorption distance, over-dark rough transmission, fake dispersion, and volume transmittance errors. citeturn4youtube12turn11search0turn15search4turn12search0

Save more than the beauty pass. At minimum, export **beauty, albedo, normal, depth, object ID, material ID, variance/noise estimate, and transmission or path-length diagnostics**. OpenEXR is the right target because it is designed for scene-linear HDR images and supports strong multi-part, multi-channel workflows. Pair that with Open Image Denoise using beauty plus albedo and normal auxiliary buffers so you can get clean previews and clean finals without destroying geometric detail. citeturn26search3turn26search2turn23search0turn23search1

If your explicit goal is “the same real-life effects the video reproduced,” the non-negotiable features are these: a physically correct dielectric model, robust self-intersection handling, NaN-safe shader math, Beer–Lambert absorption, rough dielectric transmission, spectral wavelength transport for dispersion, scene-linear HDR output, and a caustics strategy stronger than naive low-spp path tracing. The UI can be minimal and the renderer can still succeed. But if any one of those pieces is wrong, the image will look less real no matter how polished the editor becomes. citeturn11search0turn15search4turn30search1turn13search0turn16search1turn17search0turn35search1turn26search3