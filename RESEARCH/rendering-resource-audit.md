# Rendering responsiveness and resource audit

Baseline: `45a42ea47c76930354a5f64d808559c9ae501d47` (the HSL selections push).
Scope: interactive graph/RAW View rendering, native section completion,
publication, resource lifetime, memory admission, timing, and related UI paths.
This is a source audit plus a bounded implementation, not a measured claim that
all rendering paths or all hardware are optimized. No original image, authored
node ordering, saved recipe, shader precision, or export behavior is changed.

## 1. User-facing priorities

The useful target is **time to the right pixels without starving input**, not
just the number of GL calls submitted per second. Distinguish UI frame time,
image-update cadence, input-to-current-image latency, and time to settled native
coverage. A 60 Hz UI can still show an old edit, and a busy GPU can produce many
obsolete frames. Track those separately.

During a drag, retain a valid edited image, coalesce obsolete work, respect the
requested detail floor, and choose the highest *admitted* candidate supported by
completed timing evidence. The FPS target cannot override an impossible memory
allocation or make an intrinsically expensive full-input operation cheap.
After release, refine the visible area first; complete other sections only when
the existing completion policy requests them. Camera motion must not change the
image-content identity. A failed optional refinement must not replace the last
valid presentation with blank or partial pixels.

The changes here target specific causes of interruption: UI waits behind cache
I/O, duplicate native-pixel allocations during pan reversal, repeated scans of
already completed sections, stale resolution predictions, and quality labels
that described a different candidate from the one actually dispatched. End-user
FPS and latency improvements still require real-application measurements.

## 2. Inspected architecture and useful existing behavior

| Area | Inspected paths | Assessment |
| --- | --- | --- |
| View demand and refinement | `Editor/Internal/EditorModuleViewportPreview.cpp`, `ViewportPreviewState.h`, `ViewportCompletionPolicy.h`, `ViewportFramePacer.h` | Separate camera/content identities, visible-first refinement, quiet interval, phase-preserving pacing and bounded retry are valuable; preserve them. |
| Native section ownership | `Renderer/ViewportSectionStore.*`, `Editor/Internal/EditorModuleViewportSections.cpp`, `EditorRenderWorkerViewportSections.cpp` | Correct native sample-grid intent and off-thread retirement; cache metadata/I/O coupling and redundant reload copies needed correction. |
| Timing and quality | `Renderer/ViewportWorkloadTiming.h`, `ViewportPreviewPolicy.h`, `Editor/Internal/EditorRenderWorkerViewportTiming.cpp` | Completed-service measurements and separate regional/full-input features are better than timing CPU dispatch alone. Two evidence/reporting defects are fixed below. |
| Graph scheduling | `Renderer/Internal/RenderPipelineGraphSchedule.h`, `RenderPipelineGraphExecution.cpp`, `RenderPipelineGraphPhysicalSchedule.cpp`, `RenderPipelineGraphPointwiseFusion.cpp` | Requested inspection outputs are materialization boundaries. Fusion already exists for eligible pointwise chains; this is not a reason to flatten or reorder the authored graph. |
| Memory and caches | `Renderer/GraphResourcePolicy.h`, `GpuMemoryBudget.cpp`, `Editor/Internal/EditorRenderWorkerGraph.cpp`, `Renderer/Internal/RenderPipelineGraphTextureCache.cpp` | Adapter-aware headroom, retained float32 requirements, active-output protection and graph-cache reclamation already exist. They must remain separate from presentation-cache residency. |
| UI and lifetime | `Editor/Internal/RawWorkspace/EditorModuleRawWorkspacePreview.cpp`, `EditorRenderWorker.cpp`, implementation guidance on state/lifetimes, processing and persistence | Driver calls, publication fences, project ownership and background cleanup all matter to responsiveness; moving a function to a worker is not sufficient when the UI shares its lock. |

Some large implementation files were inspected in relevant ranges rather than
read exhaustively. This audit is not a claim to have verified every node or every
branch of the product.

## 3. Implemented changes

### I/O-independent section metadata

Previously `Store`, `Load`, `Has`, `Covers`, `Complete`, `NextMissing` and
`DiskBytes` used one mutex. A disk write, disk-space query, or large copy could
therefore hold up a UI coverage query even though the I/O itself ran on a worker.

`ViewportSectionIndex` now owns the immutable grid and monotonic readiness.
Writers install an entire immutable entry before release-publishing readiness;
readers acquire it before borrowing pixels. The worker-only I/O mutex protects
file-cursor operations and entry installation, not UI metadata. Completion and
disk-byte counters are atomically published. The store still requires an owner
for its lifetime; atomics do not replace the existing shared snapshot ownership.

Coverage visits only the rows/columns intersecting the visible region. Whole
image completion keeps a monotonic first-missing cursor instead of repeatedly
scanning the completed prefix. Visible sections retain their original row-major
priority. A small recheck handles publication between readiness and cursor
advancement. Invalid/mismatched geometry cannot claim coverage.

### Shared immutable upload payloads and truthful retained RAM

`LoadShared` returns an immutable lease. RAM hits share the retained vector
allocations instead of copying a second pair of RGBA32F buffers. The render
worker holds the lease through both uploads. This is **not** zero-copy DMA:
OpenGL/driver transfers and synchronization still exist.

The shared 256 MiB retained-RAM allowance now charges vector *capacity*, including
spare allocation, and follows the payload's final owner. Retiring the store does
not release the allowance while an upload lease remains alive. The backing
vectors are freed before their charge is returned. Disk-loaded transfer buffers
remain transient worker memory, not permanent cache residency; this change does
not claim a cap on the entire application's RAM.

For scale, a 1024 x 1024 RGBA32F plane is 16 MiB; paired SDR/HDR planes are 32 MiB.
An interior section's four-pixel border makes that pair about 32.5 MiB. A RAM
restore no longer makes an additional application-side copy of that pair.
This is an allocation/copy bound derived from dimensions, not a measured FPS gain.

The copy-based `Load` API remains available. It now commits replacement vectors
only after both planes are available. The Windows temporary-file ownership and
free-space reserve remain; a small POSIX transport lets the production CPU
store be tested independently of the Windows/OpenGL application.

### Expiration cannot leave a stale fitted workload model

`ViewportWorkloadTiming::Record` could erase expired observations and then reject
an outlier before refitting. `Estimate` saw only fresh surviving observations,
but used coefficients and rank derived from deleted evidence. That can make a
smaller render appear cheaper than the remaining evidence supports.

When pruning changes the point set, the rejected-outlier path now refits from
that surviving set. One surviving point stays conservative; three consistent
load changes still establish a new regime. The test reproduces the former false
10 ms prediction and requires the surviving-evidence 14 ms estimate instead.
These are controlled test inputs, not timings measured from Stack on hardware.

### Quality explanations follow the actual candidate

The policy can retain a dispatched edge during stabilization. Previously its
cost was updated but its limiting reason could remain attached to the preferred
candidate. A slow actual candidate could retain a frame-budget label from a fast
one; an unknown actual candidate could keep a measured explanation.

The explanation is now recomputed whenever the candidate changes. Memory
admission, FPS selection, minimum detail and the available candidate set are
unchanged. Tests cover fixed-work, detail-floor and learning explanations, and
confirm that stabilization does not bypass memory admission.

## 4. Event and ownership contract

| Event | Required engine action | Expected presentation / status |
| --- | --- | --- |
| Open an image | Establish source/project ownership; obtain a display-sized edited frame before expensive settled work. | No artificial wait for native completion before first useful pixels. Existing behavior preserved. |
| Begin/continue an edit | New content identity; cancel/coalesce obsolete preview demand; retain accepted presentation until replacement is ready. Train only compatible completed work, not a cold load or a cache hit. | Sliders remain responsive; image updates can be slower than UI redraws. No promise that the FPS slider is an absolute guarantee. |
| End an edit | Keep the existing quiet interval and request native visible sections first. | Preview sharpens without changing authored values or precision. |
| Pan or zoom | Update camera demand, not image revision; prioritize missing visible sections and reuse matching native pixels. | Returning to a cached area avoids graph execution and, for RAM hits, avoids duplicate CPU payloads. |
| RAM allowance exhausted | Spill only owned derived pixels; check space; do not publish readiness before the write succeeds. | Existing valid pixels stay available. Metadata readers no longer wait on spill I/O. |
| GPU headroom shrinks | Reclaim only evictable resources; preserve leases; deny/defer inadmissible work rather than silently claiming native quality. | A truthful memory-limited status is preferable to paging/stutter. Remaining presentation-cache issues are discussed below. |
| New image revision during cache work | Keep the old operation's owner alive; accept only matching revision/generation; retire resources on the established worker path. | No stale tile from the previous edit appears. This PR does not change acceptance rules. |
| Analysis/scope demand | Keep independent roots and identity; schedule optional work only when foreground demand permits. | Opening scopes must not restart full-image refinement. Existing behavior preserved. |
| Export/capture | Preserve terminal operation and source ownership; do not treat a display cache as authority for export completeness. | Export is not dropped by interactive preview coalescing. No export algorithm changes here. |
| Failure or close | Keep prior accepted state; use existing bounded retries; drain GPU/resource owners before destruction. | No fabricated completion, blank replacement, or early memory-budget release. Driver/device-loss qualification remains separate. |

## 5. What to optimize next, with evidence gates

These are deliberately distinguished from implemented fixes.

**P1 - native GPU presentation working set.** In
`NextViewportNativeSection`, visible native sections are pinned while the
ordinary native-cache trimming selects non-visible victims. The visible set can
itself exceed the nominal native-cache allowance. Restore also deserves its own
admission accounting, distinct from graph execution. This is a source-level risk,
not a demonstrated out-of-memory incident. A robust fix needs a stable
budget-admitted desired resident set, room for an in-flight publication, and a
truthful distinction between CPU-native completion and GPU-visible quality.
Simply evicting visible sections can create an endless reload/evict loop. Keep
a lower-resolution edited presentation while deferred; retry on demand/budget
changes rather than every frame. Validate on small-VRAM and shared-memory GPUs.

**P1 - driver memory queries on UI paths.** `RefreshRawWorkspaceGpuMemoryBudget`
periodically queries DXGI on the UI path; `GraphFrameTransition` also requests a
budget during transitions. The provider serializes queries. Measure the UI cost
and contention, then publish an adapter-identified snapshot from a worker.
Use freshness and a conservative fallback for presentation, while allocation
admission remains authoritative on the execution side. Budget notifications may
replace unnecessary polling. Do not accidentally select another adapter on a
hybrid system. A nonblocking mutex alone does not remove the driver's call cost.

**P1 - synchronous native readback.** `ReadNative` uses `glGetTexImage` to client
memory; this can synchronize GPU work. A bounded PBO/fence transfer queue can
help only if useful work overlaps the transfer and mapping is delayed until the
fence is signaled. Define separately: render ready, GPU-visible publication,
readback pending, stored-native ready. Do not mark the store ready when only a
PBO request has been submitted. Reserve staging bytes, retain source/operation
leases, bound queue depth, and discard superseded work safely. Two paired
interior sections alone are roughly 65 MiB of staging; weak devices may need one
slot or the direct path. Do not add a PBO and immediately map it: that moves the
wait rather than removing it.

**P2 - cache eviction and pooling.** Texture-cache trimming currently repeatedly
scans for a victim. Profile large branched graphs before replacing it with an
ordered eviction plan/heap. Preserve current regional-versus-complete preference,
protected outputs, LUT ownership and GL-context-safe deletion. Pool textures,
FBOs and transfer storage only when format/extent/ownership match and all GPU
readers have completed; an overly large pool is retained memory, not free speed.

**P2 - physical schedule and exact math.** Extend the existing pointwise IR rather
than changing the authored graph. Fuse only where masks, linked values,
fan-out/inspection boundaries, alpha semantics and precision permit. Preserve
operation order; floating-point reassociation and FMA can change results even
when algebra looks equivalent. Cache uniforms separately from shader structure.
Neighborhood operators require proven halos and boundary conditions; global
histograms/reductions and some RAW operations cannot be made local by cropping
blindly. Separable kernels, bounded lookup approximations and multiscale previews
need operation-specific error and export-parity tests, not a generic fast-math
switch. This PR intentionally changes none of these image calculations.

**P2 - background fairness.** The worker already limits outstanding GPU work and
native completion submits one section at a time. Retain that backpressure.
Measure worst-case section/band duration and input-to-replacement latency before
increasing concurrency. Use completed work to adapt cooperative chunk sizes;
more queued GPU work can increase latency even if throughput rises. Optional
scopes, prefetch and prewarm should yield to current image edits and terminal
exports without starving forever. UI animations should not be used to hide
missing completion or substitute for a measured latency fix.

**P3 - cache compression / out-of-core expansion.** Lossless disk compression can
trade CPU time for bandwidth, but requires measured compressibility, cancellation
boundaries and accounting for codec scratch plus compressed/uncompressed copies.
A lossy or half-float native cache would change the contract and is not an
acceptable invisible memory optimization. On integrated GPUs, host and device
budgets can describe overlapping physical memory; do not add them as independent
free pools.

## 6. Measurement and hardware qualification

Record p50/p95/p99 UI frame time, event-to-current-image latency, completed image
cadence, time to native visible coverage, native restore hit/miss latency, peak
retained/transient CPU bytes, GPU resident/evictable/in-flight bytes, disk bytes,
queue depth and superseded-work fraction. Keep CPU submission, GPU timestamps,
completed service and queue delay separate. Do not sum overlapping CPU/GPU time.
Use the existing UI performance probe and completed viewport telemetry rather
than inserting blocking diagnostic readbacks into every frame.

Use a repeatable small set of real, read-only inputs: a normal RAW, a large RAW,
a signed/HDR result, a branched graph with inspected intermediates, and a graph
with a full-input operation. Compare cold open, warm slider drag/release, rapid
pan reversal, fit-to-1:1 zoom, resize/high-DPI, HDR on/off, scopes, another project
opening during completion, export during background work, and low free memory.
Keep input/recipe hashes, exact build, driver, adapter, display refresh and
viewport size with results. Compare output pixels and geometry, not screenshots
alone.

| Hardware class | Primary questions |
| --- | --- |
| Integrated/shared-memory, modest system RAM | Do retained and staging budgets compete with the desktop? Does the UI avoid paging and long driver calls? |
| Small dedicated VRAM | Does visible native demand exceed the admitted presentation set? Do repeated pans cause churn rather than progress? |
| Midrange discrete | Do RAM cache hits reduce copies/allocations without causing more GPU upload or cache misses? |
| High-end discrete, high-refresh/high-DPI display | Does CPU scheduling/driver overhead dominate? Are the UI and image-update cadence measured independently? |
| Slow or space-constrained temporary storage | Do metadata queries remain independent of I/O? Are failures explicit and previous pixels preserved? |

Do not claim these hardware classes are qualified by CPU unit tests. Real GPU
upload/fence behavior, full Windows application build, preview/export parity,
interactive feel and device-loss handling remain required release evidence.

## 7. Focused validation performed

The independent project is `tools/tests/render_resources`. It builds actual
production headers and the section store without replacing them with test
stand-ins. Eight cases pass with GCC 14.2 (Release) and Clang 17
(AddressSanitizer + UndefinedBehaviorSanitizer, leak detection enabled). The
concurrent publication case also passes GCC ThreadSanitizer. The local spill
checks exercise the POSIX transport; the Windows CI job exercises Win32.

The new timing and actual-candidate explanation cases were also compiled against
the original, SHA-verified baseline headers. Both fail on the original code and
pass on this change. Geometry and unmodified local source dependencies were
verified against their Git blob hashes before testing. Test success is not a
measured application speedup. Hosted CI status, when available, is attached to
the PR and should be checked independently.

## 8. Research used and implications

Primary references checked for this work (not promises of a specific speedup):

1. [C++ Core Guidelines CP.43](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rconc-time): minimize critical-section duration. Here the important change is eliminating the UI's dependency on the I/O critical section, not merely making copies faster.
2. [Khronos: Pixel Buffer Objects](https://wikis.khronos.org/opengl/Pixel_Buffer_Objects): asynchronous transfers require delayed access; immediate mapping defeats overlap. This is why a PBO ring is a separate measured design, not an untested one-line substitution.
3. [Khronos: Synchronization](https://wikis.khronos.org/opengl/Synchronization) and [Sync Objects](https://wikis.khronos.org/opengl/Sync_Object): command submission is not completion. Preserve explicit ownership and producer flush/publication rules; never recycle an in-flight resource.
4. [Microsoft: DXGI_QUERY_VIDEO_MEMORY_INFO](https://learn.microsoft.com/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info): budget is a process target, not nominal card capacity; going over it can incur stuttering/paging. Budget changes and hybrid-adapter identity matter.
5. [Microsoft: residency](https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency): notification and budget concepts inform admission policy. Direct3D12 residency commands are not assumed to be available through Stack's OpenGL backend.
6. [Halide: scheduling multi-stage pipelines](https://halide-lang.org/docs/tutorial/lesson_08_scheduling_2.html): locality, redundant work, materialization and parallelism trade off. This supports separate authored algorithms and physical schedules, not flattening the node editor.
7. [libvips: demand-driven execution](https://github.com/libvips/libvips/wiki/Libvips) and [thread ownership](https://www.libvips.org/API/current/using-threads.html): bounded region execution is a useful model; mutable regional state still requires clear ownership. This PR does not add a libvips dependency or assume its benchmark results transfer to Stack.

The selected changes are the ones whose benefit and invariants can be established
from the current ownership/scheduling code and focused checks. Larger GPU and
math changes need the additional evidence above, rather than a claim that more
threads, more buffers, lower precision or more aggressive fusion is always faster.
