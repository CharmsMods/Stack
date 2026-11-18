# Default-node performance and UI scheduling

Baseline: `872f30e7deb4fa432b08ab10fa40033777cb95cd` (main after PR #7).
Scope: HSL / Color Warp first, then Local EV, Tone Curve and their connection to
interactive presentation. Color Calibration is unchanged. Node ordering, authored
recipes, working spaces and the existing viewport quality controls are preserved.

## Findings and decisions

| Area | Observed cost or defect | Change |
| --- | --- | --- |
| HSL GPU | Ordinary HSL used the full legacy-pin shader; repeated hue interval, area-distance and guide work | Compile a smaller HSL-only variant; compact active legacy uniforms in the full variant; share pixel invariants and reject out-of-area selections early |
| HSL CPU | Cloud samples repeatedly rebuilt coefficients and selection geometry | Own an immutable prepared recipe, coefficients, bounds and active pin metadata for each batch |
| Color Warp correctness | Stage cache keys omitted ordered HSL selections and limit-to-selections; inactive array samplers could alias the input sampler's unit | Hash all image-affecting selection fields and bind distinct sampler units even when their branches are inactive |
| Live color cloud | Up to 4,096 color transforms ran in UI drawing; spatial HSL evaluation used the default center coordinate | Prepare on project workers from a bounded RGB/UV snapshot; use each sample's real UV and adopt completed vertices on the UI GL context |
| Tone Curve | The scalar evaluator performed 18 inverse iterations before discarding them for linear-X handles; LUT construction rebuilt segments per sample | Take the existing analytic linear-X path first and prepare segments/tail slopes once per table |
| Local EV | Neutral authored curves and inactive Light bands still performed per-pixel work; uniform arrays were uploaded element by element | Exact no-op guards including manual handles, bulk uniforms, and light analysis only when smoothing or active automatic Light requires it |
| UI/GPU scheduling | Time-only enqueue limits let fast CPUs submit many draws; common previews were below the old raster-splitting threshold; mouse motion did not activate the interaction lease | Bound both submission count and elapsed time, split ordinary processing passes, and recognize pointer motion |
| Compatible HDR presentation | A changed frame required separate compare and readback polling turns | While interacting, queue compare and readback under one fence using the existing PBO; retain compare-first idle operation |

The GPU variants share the same shader body and selection composition. Prepared
CPU helpers keep the scalar evaluator as a supported entry point. No approximate
trigonometry, lower precision, changed sample grid, or node fusion was introduced.
The existing CPU distinction between near-neutral global hue and selection hue
is preserved, as are hue-wrap tie behavior and ordered overlapping selections.

## Interaction and ownership

| Event | Processing and presentation behavior |
| --- | --- |
| Pointer motion, drag, wheel, key input or animation | The shared interaction lease selects the tighter processing submission budget; display refresh is separate from desired image FPS |
| Rapid parameter edits | One cloud job per node coalesces demand. A compatible completed parameter result may be displayed while the latest job runs; it retains its actual revision |
| New source, space, dimensions, selection membership or feedback | Incompatible cloud work is cancelled/rejected; old vertices are not relabeled and stale selection highlighting is suppressed |
| Node/editor destruction | Workers retain immutable values and cancellation state, never an editor pointer or GL context; destruction does not wait for the cloud job |
| Background queue refuses work | Retry optional cloud preparation after 100 ms, keeping accepted imagery |
| Heavy preview/refinement | Raster tiles keep full-image coordinates. Active UI uses at most 128 Ki pixels per tile, idle work 512 Ki; only the submitting worker waits for its own GPU fence |
| Explicit export during interaction | Export cooperates with the same UI budget without being discarded as obsolete preview work |
| Idle presentation | Larger processing batches and compare-first HDR transfer reduce scheduling and bandwidth overhead |

Cloud admission is bounded to two queued/running jobs across all owners. Only a
64-by-64-or-smaller sample grid is captured, rather than duplicating the full
readback. GPU submission checks drain after at most two checkpoints during active
UI/native refinement and eight during idle viewport work, or after a clamped
quarter-display-frame enqueue interval (0.25–2 ms), whichever comes first. These
are cooperative command limits, not GPU execution deadlines.

## Bounded measurements

CPU: Linux x86_64, Intel Xeon Platinum 8573C, GCC 14.2 with `-O2`. Comparisons use
the main-branch implementation and the new implementation on the same inputs.
Seven timing samples were taken per path; table values are medians.

| Measured operation | Baseline | Updated | Ratio | Output check |
| --- | ---: | ---: | ---: | --- |
| HSL evaluation for one 4,096-point cloud, including new batch preparation | 3.53868 ms | 1.16885 ms | 3.03× faster | All 12,288 RGB float components identical |
| Build one 4,096-entry RGBA Tone Curve table | 5.435 ms | 0.257 ms | 21.15× faster | Output bytes identical |

The HSL case contains three selections with own curves, brightness bands and hard
and soft areas; each timing sample averages four batches. Baseline range was
3.45835–3.73496 ms, updated 1.10393–1.35828 ms. The tonal case uses a five-point
red curve and matching luminance curve, with identity remaining components. These
measure CPU math/table preparation only. They do not measure complete node time,
GPU speedup, readback/upload cost, UI frame rate or representative desktop CPUs.

## Verification and limits

- Actual old/new HSL shaders compiled and ran in a hidden EGL OpenGL context:
  192 cases, both the full and HSL-only variants, with zero old/new component
  difference. Maximum CPU/GPU absolute difference was `1.52588e-05`, inside the
  existing relative tolerance. Cases included both working spaces, HDR/signed
  inputs, alpha, hue wrap, own curves, brightness bands, overlapping and regional
  selections, and hard/soft outlines up to 128 points.
- Actual Local EV image and overlay shaders matched the baseline exactly in 96
  cases covering neutral/active/manual curves, Light, regions, zones, both
  working spaces, HDR/signed values and alpha.
- Shader execution used Mesa 25.0.7 llvmpipe (LLVM 19.1.7, OpenGL 4.5 Core), not a
  physical Windows GPU. This establishes bounded output agreement, not GPU speed.
- Focused production-source checks cover prepared HSL/scalar agreement and stage
  keys, tonal curves/tails/sample grids, Local EV no-op admission, cloud ownership
  and coalescing, GPU scheduling, HDR transfer phases and worker cancellation.
  Linux syntax checks cover the changed processing/editor translation units.
- The Windows render-resource workflow runs the focused CPU contracts with MSVC
  and compiles the changed processing/editor/native HDR paths. The PR's Actions
  result is the authoritative Windows outcome. This is not a full product link;
  the one-expression AppShell mouse-motion change is excluded from standalone
  compilation because it requires generated embedded application assets.
- The existing hidden native HDR validation now checks interactive transfer
  cadence and unchanged-frame suppression. Its physical HDR-display execution
  remains unverified here, as do real mouse/monitor cadence and vendor drivers.

## Tradeoffs and source guidance

Smaller raster submissions and more worker fences favor interaction latency at
some throughput cost. Idle batches remain larger. Long allocation calls, unsplit
compute dispatches, resource sharing and driver scheduling can still delay UI
frames. Windows exposes several hardware-dependent preemption granularities; an
application cannot promise every refresh during arbitrary GPU load.

The compatible HDR interaction path spends full-window GPU readback bandwidth
even if comparison finds unchanged pixels. It adds no PBO and avoids full CPU
copy/D3D upload for those unchanged frames. Idle operation keeps its previous
bandwidth-saving path. Physical transfer throughput can still limit cadence.

Primary references that informed the decisions:

- [Khronos ARB_sync](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sync.txt):
  fence completion and nonblocking polling; GPU waits belong to the worker.
- [Khronos ARB_timer_query](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt):
  CPU submission time does not establish GPU completion time; asynchronous GPU
  timing remains distinct from the bounded CPU measurements above.
- [Microsoft GPU preemption](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-preemption)
  and [DXGI granularity](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ne-dxgi1_2-dxgi_graphics_preemption_granularity):
  long GPU packets can delay presentation, and preemption capability varies.
- [NVIDIA OpenGL ES programming guidance](https://docs.nvidia.com/jetson/archives/r35.6.1/DeveloperGuide/SD/Graphics/GraphicsProgramming/OpenglEsProgrammingTips.html):
  remove repeated invariant/state work and measure the CPU/memory/bandwidth
  tradeoff. Its platform-specific advice is not treated as a desktop guarantee.
