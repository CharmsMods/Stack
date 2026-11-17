# Render resources and responsiveness

Base: `45a42ea` (main, 2026-11-17). This record accompanies the rendering
optimization PR; code remains the implementation authority.

## Scope and verified findings

Preserve the reorderable graph, authored image math, source files, independent
project ownership, explicit export quality, and the existing viewport-first
editing/native-completion flow. The baseline already has asynchronous GPU timing,
cooperative GPU submission, viewport demand planning, retained presentation
leases, and native float32 section caching. Replacing those foundations is not
needed to fix the problems found here.

| Finding | Consequence | Implemented change |
| --- | --- | --- |
| Section coverage queries share a mutex with file I/O and large copies | UI can wait behind a slow cache device | Publish immutable ready flags/counters separately from the payload lock |
| Native GPU section budget omits preview copies and protects every visible native tile | Visible residency can exceed its allowance; naive eviction would cause reload loops | Plan an admitted visible set before eviction and reload |
| Section store publishes before its GPU-slot vector allocation succeeds | Allocation failure can leave inconsistent counts | Publish the two allocations together |
| Missing DXGI telemetry becomes an unlimited allowance | Weak/remote/unmeasured devices can overcommit | Explicit bounded fallback and small optional cache allowance |
| Some complete RAW outputs are estimated at native resolution despite sampled execution | Preview selection can overestimate image work | Separate verified sampled output work from fixed sensor residency |
| Shared cancellation callback writes a plain bool from CPU helpers | Cancellation race and lost cancellation | Monotonic atomic latch |
| Persistent pipeline retains callback references to expired request locals | Later cancellation checks can access dead state | Request-scoped callback cleanup |
| Ready denoise futures release native/model image buffers under scheduling lock | Submission and progress polling can stall | Detach under lock, drain and destroy on the worker after unlocking |

The section display path also clips coarse copies away from finer matching
accepted imagery. Residency eviction therefore cannot cover a sharp main patch
with its quarter-resolution fallback. Section reloads receive their own GPU
admission, including paired native and display allocations, before upload.

## Resource estimates and limits

For the modeled source-only Bayer graph, at a 1024-pixel preview with ordinary
single-pass development and empty correction metadata:

| Sensor extent | Previous planner estimate | Corrected planner estimate |
| --- | ---: | ---: |
| 7952 × 5304 | 5148.61 MiB | 326.72 MiB |
| 9568 × 6376 | 7446.97 MiB | 434.33 MiB |

These are **admission estimates**, not measured allocations or speedups. Native
sensor/correction fields remain in the corrected estimate. DNG noise variance
and the count-derived correction/gain-map metadata atlas add their own costs.
Normalized bracket mosaics follow their distinct upload path. Native outputs,
multipass demosaicing and unproven legacy dependencies retain conservative
accounting. Warm native sensor residency is still charged conservatively; this
does not claim optimal packing on a marginal GPU.

Unknown GPU telemetry has a 512 MiB planning allowance and at most 64 MiB of
optional graph cache within its unused portion. Measured strong GPUs keep their
adaptive headroom. GPU section copies use a separately bounded, sampled allowance
that counts native/preview and SDR/HDR together. A large native dependency can
still be deferred if it cannot fit; this PR does not add native tiled demosaicing
to operations whose contracts currently require complete inputs.

## Event contracts

| Event | Expected behavior |
| --- | --- |
| Edit while rendering | Coalesce pending previews; cancellation stays latched; retain accepted presentation |
| Pan or zoom | Reprioritize visible sections without changing content identity or authored graph |
| Stop editing | Keep the existing quiet interval and visible-first native completion |
| Reach GPU allowance | Evict optional copies/caches, defer inadmissible work, preserve CPU/disk native pixels |
| Recover memory | Retry deferred work after useful budget or demand change; do not exhaust execution-error retries |
| Switch/close project | Results and cancellation remain associated with their original owner |
| Export during edits | Keep captured terminal export semantics; preview coalescing cannot discard it |
| Cache I/O or execution failure | Preserve prior accepted imagery; publish no partial native section |

## Research and decisions

- [Microsoft: DXGI process budgets](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info)
  and [budget queries](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo):
  available headroom is budget minus current process usage. Counting resident
  presentation again against that headroom is a different quantity; exceeding
  the OS budget can produce paging and stutter.
- [Khronos ARB_sync](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sync.txt)
  and [timer queries](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt):
  preserve producer flushes, completion fences, asynchronous timing polling and
  worker-only waits. Removing synchronization is not a valid latency shortcut.
- [GEGL operation contracts](https://www.gegl.org/gegl-operation.h.html):
  required input, invalidated output and cached regions are different contracts.
  Global/neighborhood operations cannot be cropped as if they were pointwise.
- [OpenImageIO tile cache](https://openimageio.readthedocs.io/en/latest/imagecache.html):
  byte limits and explicit pinning support bounded reuse. Stack keeps native
  CPU/disk data authoritative while GPU display copies are disposable.
- [Khronos buffer streaming](https://wikis.khronos.org/opengl/Buffer_Object_Streaming):
  asynchronous staging helps only when transfer and consumption can overlap.
  A new PBO ring, persistent mapping or API replacement needs measured evidence;
  those are not prerequisites for these fixes.

No shader approximation, blanket precision reduction, node fusion, new graphics
API requirement or replacement image-processing algorithm is proposed. Those
would need separate image-equivalence and hardware measurements.

## Validation checklist

- [x] Focused residency, budget and production graph-planning checks.
- [x] Worker/service lifecycle and denoise launch/completion regressions.
- [x] Changed worker implementation syntax checks with the project headers.
- [x] Changed editor integration and section restore implementation syntax checks.
- [x] Independent review of memory ownership, metadata publication, deferred
  retry behavior, scoped cancellation, residency convergence and drawing quality.
- [ ] Windows MSVC contract job: section store (real temporary-file I/O),
  viewport policies, residency/drawing, GPU budgets, worker and denoise lifetimes.

The local environment is Linux; the PR includes a bounded Windows MSVC job.
CPU policy checks and source compilation do not establish full Windows
application behavior, measured FPS, driver allocation peaks, visual parity on a
physical display, or performance on the user's GPU. No benchmark speedup is
claimed. Full product build and real-GPU interaction checks remain separate.
