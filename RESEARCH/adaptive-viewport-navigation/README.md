# Adaptive viewport navigation and GPU throughput

This follow-up starts from `865ef73` (the merge of PR #8). It investigates slow
edited-bracket opening, repeated full-quality work after minimize/restore, slow
global HSL completion, control-switch hesitation and camera stutter during native
section rendering. No color-calibration math or saved project schema changes.

## Findings that changed the implementation

| Symptom | Confirmed code path | Change |
| --- | --- | --- |
| Slow bracket processing with Light/Tone edits | `DrawRawFullscreenPass` fenced, flushed and waited after every 512x128 tile. The enclosing worker also drained its queue. | Actual RAW draws and existing demosaic dispatch boundaries enter the same bounded submission queue as other graph passes. |
| HSL and native refinement throughput | CPU cancellation probes also counted as GPU checkpoints; idle native work used the aggressive two-checkpoint drain. | Cheap cancellation remains independent. Actual submissions form bounded batches, with up to two batches overlapping. Idle native work uses the ordinary eight-submission limit. |
| Full-quality loss after minimize/restore | A zero drawable extent reset the HDR presenter. `AnyPreviewActive()` changed, which participates in image-content identity; restoration could request another image refresh. | Suspend presentation without losing HDR capability, target ownership or pending transfers. Defer real display changes until a drawable returns. Ignore iconified viewport demand. |
| First drag after selecting another control | Ordinary panel selection marked unchanged image pixels dirty and cleared other tools' analysis caches. Descriptor lookup copied the compiled graph for each visible tool. | Separate selection from pixel invalidation, retain revision-keyed scope packets, and borrow the immutable compiled graph. Missing analysis gets its own request. |
| Native blocks interrupt navigation | Completion moved directly toward native sections instead of explicitly completing screen-density coverage first. | Carry screen/coverage/native intent with each request; prioritize current visible screen demand and pause speculative work during motion. |
| A ready foreground still took longer to arrive | Regional output could trigger a second overview graph before the worker returned the main result. | Publish regional results immediately. Offscreen coverage is a separate, interruptible worker request. |
| False memory refusal on weak devices | The old admission estimate still included that second graph. An earlier legacy cache trim could also evict dependencies before generic viewport admission. | Reserve only an actual bounded whole-raster copy and apply the generic graph budget before trimming. |

Saved bracket result/source verification and sidecar loading already use worker
paths and immutable image payloads. No speculative storage rewrite was justified
by this investigation. This work improves developing and displaying the saved
bracket, not reconstructing its original exposure stack again.

## Demand and event handling

The node graph remains the processing authority. Source/output/view identities,
complete-input dependencies, masks, sample-grid coordinates, precision and export
semantics still pass through its existing planner and worker. Scheduling changes
do not make global operations regional by assumption.

| Event/state | Work selected | Presentation behavior |
| --- | --- | --- |
| An image adjustment is active | Existing measured interactive sampling and FPS/detail-floor policy | Controls and camera continue independently of image completion; completed useful edits can appear. |
| Adjustment release with missing visible detail | Visible region at physical screen sampling, capped at native sampling | Settle what is actually visible before pursuing invisible native pixels. |
| Camera moving/recently moved, screen coverage already useful | No new speculative coverage/native job | Reuse accepted pixels. Current-content pixels may be up to 20% below display density during motion only if the user's detail floor allows it. Spatial coverage is never relaxed. |
| Camera becomes quiet, screen covered | Bounded whole-image navigation coverage | Prepare useful offscreen fallback without replacing a finer accepted foreground patch. |
| Camera quiet and coverage ready | One native section per worker dispatch | Prefer missing visible sections, then existing whole-image completion intent. |
| Navigation needs visible detail during background work | New visible-screen request interrupts coverage/native refinement at cooperative boundaries | Already finished sections survive; an in-flight screen job completes while further input coalesces. |
| Revisiting an already completed native area | Reload admitted native/display data from the section store | Do not execute the graph again for stored pixels. |
| Optional coverage fails or is denoise-provisional | Retire its packet and retry later | Keep the settled foreground and its completion status. The worker owns model continuation lifetime. |
| Minimize/restore | Suspend/resume window presentation | Retain image identity, section completion and HDR resources; genuine resize/display changes remain effective. |

The screen edge includes a one-physical-pixel rounding allowance. Whole coverage
does not repeat solely because already accepted whole-image pixels satisfy the
screen without that extra planning allowance. A camera quiet interval is 120 ms;
the existing image-edit quiet interval is 200 ms.

Whole-image navigation work is bounded by the current screen sampling edge,
four visible sample rasters and 8,388,608 pixels, whichever is smaller. At high
magnification this deliberately becomes a coarser global fallback instead of an
accidental second full-native render. Complete upstream dependencies still need
worker memory admission. Native completion retains the existing 80%-visible
arming rule: once armed it survives navigation, while a new edit at high
magnification can prioritize only the visible native area.

An impossible texture geometry refuses submission and backs off independently
of animated camera changes. Optional coverage has its own retry delay. Transient
execution failures, memory deferral and invalid plans are distinct from successful
image completion. A rejected submission restores the older in-flight request's
generation/intent rather than taking ownership of it.

Missing scope/input analysis can run after settled screen density, ahead of the
next idle native section. It remains behind active edits, navigation and worker
completion. This avoids making a newly selected tool wait for every native tile.
Hover-only tone probing is suppressed during camera movement; explicit target
selection keeps its existing sampling behavior.

## GPU scheduling and memory tradeoff

Raster submissions retain their full-image coordinates and use scissor tiles:
1024x128 during UI interaction, 1024x512 while idle. Each completed raster row
reconsiders current interaction, so new input can shrink the rest of an idle
pass. Small passes still report their real submission. Existing demosaic compute dispatch sizes are unchanged.

Each batch ends after two actual submissions during interaction or eight while
idle, or when the existing refresh-derived CPU submission interval expires.
The worker fences/flushes the new batch and polls or waits only for the older
batch. At most two batches are outstanding. CPU cancellation checks never create
fences. The final completion fence is still required before publication and the
next request; wait/allocation failures retain a worker-only completion fallback.
Terminal exports participate in cooperative scheduling during interaction but
cannot be discarded as obsolete previews.

This trades a small bounded amount of queued work for avoiding repeated empty
GPU pipelines. It is a submission bound, not a GPU-time guarantee. A shader,
allocation, driver call or already-submitted GPU command is not made preemptible
by a CPU cancellation predicate. Completed section RAM/disk ownership and its
existing 256 MiB shared retained-RAM allowance remain unchanged.

## Focused measurement

These are mechanism comparisons on **llvmpipe / LLVM 19.1.7 / OpenGL 4.5 /
Mesa 25.0.7**, not measurements of the user's PC or an end-to-end project load.
The baseline is the merged source above. Both paths were warmed, then measured
as three paired old/current samples. The values below are medians and include
completion of submitted work.

| Production path | Work per sample | Baseline | Updated | Time reduction | Fences across three samples |
| --- | --- | ---: | ---: | ---: | ---: |
| HSL, idle native policy | 2048x1536, four passes | 96.9283 ms | 75.2850 ms | 22.3% | 36 -> 9 |
| HSL, interaction policy | 2048x1536, four passes | 113.513 ms | 103.956 ms | 8.4% | 144 -> 144 |
| Linear RAW/bracket, idle | 3072x2048, two passes | 179.247 ms | 86.7384 ms | 51.6% | 576 -> 9 |
| Linear RAW/bracket, interaction | 3072x2048, two passes | 186.542 ms | 109.175 ms | 41.5% | 576 -> 144 |

The HSL comparison uses the actual baseline/current `FullscreenQuad`, the
unchanged current HSL shader and the production queue. The RAW comparison uses
the actual baseline/current `DrawRawFullscreenPass` bodies and unchanged
covered-linear shader, with nontrivial alpha, white balance, exposure and tone.
It conservatively omits the baseline's additional outer worker drains.
All compared pixels were byte-identical. These checks also verified at most two
live queue fences, no self-wait on the first batch, cancellation cleanup and
restoration of prior scissor state. Mid-pass idle-to-active-to-idle changes
produced exact, contiguous, nonoverlapping raster rows and byte-identical output.

## Validation and practical limits

The bounded Windows workflow compiles/runs the geometry/refinement policy,
native section publication/residency, memory budget, worker/denoise lifecycle,
prepared node math, cloud ownership, GPU scheduling, analysis scheduling and HDR
retention checks. New production-method fixtures exercise result currency and
zero-extent presentation without pretending to be full application interaction.
Windows SDK syntax checks cover affected processing, editor and native HDR
translation units; asset-dependent shell/UI units still belong to the full
product build. The actual shell suspension body has a separate extracted check.

Local checks passed for the portable geometry/refinement/timing contracts,
worker lifecycle and actual-submission accounting, analysis policy/wiring, HDR
retention/pacing, and affected translation units. Local section-policy execution
excludes Windows file-I/O cases; the Windows workflow runs the complete section
test against real temporary files. The baseline HDR begin method fails the
new minimize-retention assertion. Replacing the moving-camera adoption method with the baseline also fails the
covered-screen regression (the nearby zoom result is rejected).

No full Windows product link or physical HDR/display-rate qualification was
possible in this environment. CI status is attached to the PR. These checks do
not establish that a ten-second render takes any particular time now, or that
all machines maintain monitor refresh through every driver spike.

Neutral HSL skips shader creation; the first nonneutral edit can still incur
one-time driver compilation. The retained pipeline reuses the program on later
edits. Local tone guide programs are also lazy. This PR removes repeated
selection invalidation and queue drains without moving speculative shader work
ahead of the first visible project render.

Useful hardware follow-up is the reported saved bracket: measure first display,
settled screen display, native completion and UI frame-time tails separately;
repeat minimize/restore after native completion; compare first-use and returning
control drags; navigate rapidly through previously seen and unseen regions. A
low-VRAM device and a high-refresh display should both be included. No claim of
universal frame pacing is based on software-backend shader timings.

## External research and decisions

Primary references informed the design; their defaults were not imported as
requirements for Stack.

- [OpenSeadragon: flexible pyramids and rendering controls](https://openseadragon.github.io/examples/tilesource-flexible-pyramid/)
  describes choosing useful levels, fallback coverage and the tradeoff between
  early coarse imagery and immediate high-detail requests. This supports distinct
  screen demand and bounded navigation coverage, not mandatory powers of two.
- [GEGL features](https://www.gegl.org/features.html) describes chunk/subregion
  dependency processing, subgraph caches, tiled mipmaps and floating-point
  images. This supports keeping graph semantics while scheduling smaller views.
- [libvips evaluation](https://www.libvips.org/API/current/how-it-works.html)
  explains demand-driven regions and reusable calculated buffers. Stack retains
  its immutable graph/payload owners and avoids per-tool graph copies.
- [darktable: multiple devices and pixelpipes](https://docs.darktable.org/usermanual/development/en/special-topics/opencl/multiple-devices/)
  distinguishes full, preview, export and thumbnail workloads. That supports
  separate priorities/ownership; this PR does not introduce multidevice routing
  or change Stack's processing backend requirements.
- [NVIDIA: synchronization performance](https://developer.nvidia.com/blog/advanced-api-performance-synchronization/)
  warns about excess fences and tiny asynchronous work creating pipeline bubbles.
  The bounded paired measurements above test that mechanism in Stack.
- [Khronos ARB_sync](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sync.txt)
  defines fence scope, polling/waiting and flush requirements. This is why final
  completion/readiness stays explicit even after reducing intermediate waits.
- [GLFW window guide](https://www.glfw.org/docs/3.4/window_guide.html) and
  [Microsoft WM_SIZE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-size)
  distinguish iconification from ordinary drawable resize. Suspension should not
  be treated as new image content or permanent device loss.
- [Microsoft GPU preemption granularity](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ne-dxgi1_2-dxgi_graphics_preemption_granularity)
  exposes hardware/driver-dependent interruption boundaries. This limits what
  application queue bounds alone can promise about monitor-rate UI response.
