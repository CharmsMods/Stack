# Interactive RAW Preview Performance

Last updated: July 23, 2026.

## Purpose

This pass establishes the performance groundwork that must precede further RAW
workspace visual redesign. Its immediate goal is to make manual slider and
graph manipulation responsive without changing the settled render's image
math or export quality.

The implementation deliberately separates two workloads:

- **interactive image work**: produce the next visible proxy as quickly as
  possible;
- **settled evidence work**: after interaction stops, render at full
  resolution and collect safety/statistical diagnostics.

This is the same broad product distinction exposed by mature editors through
GPU editing, proxy modes, and render caches. It is not permission to silently
substitute lower-quality math for final output.

## Critical Before-State Audit

The approximately half-second feel was not an intentional 500 ms debounce.
RAW interactions used a 6 ms submit delay, but the submitted render then ran
the whole monolithic RAW graph synchronously on the UI thread.

Before this pass, an ordinary RAW slider update could:

1. build a fixed 2048-pixel-edge proxy even when the displayed image was near
   900 pixels wide;
2. rerun RAW loading/proxy resolution and demosaic for downstream tone or
   Local Range changes;
3. render a second neutral-exposure RAW image when RAW Exposure was nonzero;
4. synchronously read multiple GPU textures back to the CPU for percentiles,
   stage evidence, local suggestions, and blank-output probes;
5. sort CPU luminance samples for each readback; and
6. run archived automatic recommendations, dry-run diagnostics, and hidden
   candidate renders after the visible render.

That architecture was not representative of the interaction strategy exposed
by Adobe, Resolve, or darktable. Stack was doing high-latency measurement and
retired automatic work in the direct manipulation path, while also failing to
cache the obvious RAW-placement dependency boundary.

## External Evidence

### Adobe

Adobe's public Camera Raw documentation says GPU support accelerates display,
most Process Version 5 adjustments while editing, and open/save processing:

- <https://helpx.adobe.com/camera-raw/kb/acr-gpu-faq.html>
- <https://helpx.adobe.com/lightroom-classic/desktop/kb/gpu-preview-generation.html>

Adobe does not publicly document Camera Raw's internal dependency graph,
texture ownership, or cache keys. It is therefore valid to use the documented
GPU/product behavior as a target, but not to claim that Stack now reproduces
Adobe's private architecture.

### DaVinci Resolve

Blackmagic's Resolve documentation exposes three distinct performance tools:
on-the-fly Timeline Proxy Mode, cached expensive effects/color corrections,
and lower-bandwidth optimized/proxy media. The public guide supports the
principle that interaction resolution and cached intermediates may differ
from final delivery while delivery retains full quality:

- <https://documents.blackmagicdesign.com/SupportNotes/DaVinci_Resolve_17_New_Features_Guide.pdf>
- <https://documents.blackmagicdesign.com/SupportNotes/DaVinci_Resolve_20_New_Features_Guide.pdf>

Blackmagic does not publish the exact node evaluator or GPU scheduling
implementation. Stack should copy the explicit quality contract, not invent
unsupported claims about Resolve internals.

### darktable

darktable provides the most useful open implementation reference:

- its OpenCL documentation says GPU processing improves interactive work and
  export while CPU/GPU results are intended to differ only by rounding:
  <https://docs.darktable.org/usermanual/development/en/special-topics/opencl/activate-opencl/>
- it uses separate full-image and preview pixelpipes and can schedule them on
  CPU/GPU according to the selected profile:
  <https://docs.darktable.org/usermanual/development/en/special-topics/opencl/scheduling-profile/>
- its normal pixelpipe cache is a product feature, not only a benchmark tool:
  <https://docs.darktable.org/usermanual/development/en/special-topics/program-invocation/darktable/>
- the open source defines separate preview/full cache capacities and hashes
  upstream module state for stage-cache reuse:
  <https://github.com/darktable-org/darktable/blob/master/src/develop/pixelpipe_hb.c>
  and
  <https://github.com/darktable-org/darktable/blob/master/src/develop/pixelpipe_cache.c>

### OpenGL readback behavior

Stack's repeated `glReadPixels` calls were synchronization points, not cheap
metadata queries. Khronos documents that client-memory pixel downloads wait
for the relevant rendering to complete, while Pixel Buffer Objects can support
asynchronous transfers only when mapping is deferred:

- <https://wikis.khronos.org/opengl/Synchronization>
- <https://wikis.khronos.org/opengl/Pixel_Buffer_Object>

## Implemented First Round

### 1. Explicit interactive-versus-settled analysis policy

`RenderPipeline` now has an explicit RAW analysis policy. Interactive proxy
renders still execute the same visible demosaic, exposure, Local Exposure,
Local Range, Finish Tone, and View Transform operations, but they skip:

- RAW safety sample construction;
- the extra neutral-exposure RAW render;
- stage percentile readbacks;
- local-suggestion image readback;
- pre-local automatic analysis;
- diagnostic blank-output probes; and
- stage image evidence.

The settled render restores all of these checks. A requested Local Range target
sample remains available during interaction because it is an explicit user
operation.

The analysis policy participates in the graph fingerprint. An analysis-free
proxy result therefore cannot incorrectly satisfy a settled diagnostic render.

### 2. Dormant automatic backend during manual renders

Ordinary manual RAW renders no longer build archived Auto Base
recommendations, dry-run starting-point candidates, or hidden multi-pass
candidate renders. The backend remains callable by explicit validation and
isolated precise jobs; it is no longer incidental work attached to every
manual edit.

### 3. Display-sized CFA-aware proxy

The interactive RAW proxy max edge is now derived from the visible preview
panel, framebuffer scale, and modest sampling headroom. It is aligned to
64-pixel steps and clamped to 768-2048 pixels. The existing CFA-aware proxy
builder remains responsible for preserving Bayer phase and rejecting unsafe
metadata cases.

The proxy applies only while controls are active. Stack still queues a
full-resolution settled render after the existing quiet period.

### 4. RAW-placement stage cache

The recipe-backed monolithic `RawDevelopment` node now has a persistent cache
at the exact output of RAW placement. Its fingerprint includes:

- source identity;
- processing version;
- demosaic method;
- working space and necessary technical policy;
- white-balance policy/multipliers;
- RAW exposure;
- orientation;
- proxy/full-resolution dimension; and
- the RAW output-encoding field.

Local Exposure, Local Range, Finish Tone, and View Transform state are excluded
because they occur downstream. Changing one of those controls can therefore
reuse the exact RGBA16F RAW-placement texture. Changing RAW Exposure, WB,
demosaic, source, processing version, orientation, or proxy size invalidates
the cache.

A second neutral-placement cache avoids repeating the neutral demosaic needed
by settled diagnostics when its upstream dependencies have not changed.

### 5. Visible performance telemetry

The Graph Performance overlay now separates:

- complete main-render latency;
- graph execution latency;
- post-render analysis latency;
- interactive proxy versus settled full-resolution mode;
- whether analysis was captured; and
- the current RAW proxy edge.

RAW stage cache hits/misses were already present and now report the new
recipe-backed cache activity.

### 6. Real-RAW regression contract

The real-RAW smoke validation now checks the recipe-backed RAW workspace path.
For a real camera file it verifies:

- analysis-on and analysis-off renders are exactly equal in both RGBA16F
  readback values and final 8-bit pixels;
- a downstream View Transform edit hits the RAW-placement cache; and
- an upstream RAW Exposure edit misses that cache.

It also prints cold, analyzed, interactive, downstream-cache, and
upstream-invalidation timings as measurement evidence. Timing is reported,
not used as a brittle pass/fail threshold.

### July 23 real-camera measurement

On the current development machine, using a 4080 x 3060 Bayer DNG and a
1024-pixel preview edge, the validation reported:

```text
cold load/shader warm-up:     1703.24 ms
warmed settled analysis:       163.64 ms
interactive analysis-free:       8.28 ms
downstream cached edit:           6.25 ms
upstream RAW Exposure edit:      56.50 ms
```

The analyzed render hit both RAW-placement caches, the interactive and
downstream renders hit the current RAW-placement cache, and the changed RAW
Exposure produced a required cache miss. Analysis-on and analysis-off outputs
were exactly equal in the full RGBA16F readback and final 8-bit pixels.

These numbers characterize one file, GPU, driver, and build. They demonstrate
the removed stall class; they are not a cross-machine performance guarantee.

## Quality Contract

This pass does not change the settled render or export quality:

- no lower-quality demosaic is selected automatically;
- no tone or exposure operation is approximated;
- the cached boundary stores the exact rendered RGBA16F intermediate;
- interactive proxies are explicit temporary resolution changes; and
- full-resolution rendering and diagnostic validation resume after input
  settles.

During active interaction only, a shader failure that would normally be caught
by a synchronous blank-output probe may appear in the proxy. The settled pass
still runs that protection. This is an intentional latency/safety split, not a
change to accepted final output.

## Remaining Architectural Limit

Normal RAW rendering is still owned by the UI thread. The existing background
worker cannot simply be enabled: it currently reads the entire RAW result back
to CPU memory and uploads it again on the UI thread, and repository comments
record driver failures when worker and UI RAW pipelines overlap.

Consequently this pass removes avoidable stalls, but it does not yet provide a
fully asynchronous Adobe/Resolve-class render service. A full-resolution
settled render can still pause the interface after the user releases a
control.

## Follow-On Roadmap

Do these in order, with measurement before and after each step:

1. **Single-owner RAW render service.** Move all live RAW evaluation to one GL
   context/owner, publish shared textures plus fences, and remove full-frame
   CPU readback/re-upload from viewport delivery.
2. **Latest-wins scheduling.** Coalesce slider events, cancel stale work at
   stage boundaries, and never publish a generation older than the newest
   requested edit.
3. **Decompose the recipe graph.** Replace the monolithic node with explicit
   Decode/Demosaic+WB/RAW Exposure/Local/Tone/View stages so invalidation and
   cache reuse are structural rather than hand-maintained.
4. **Asynchronous analytics.** Move scopes, percentiles, and diagnostics to
   decimated GPU reductions or double-buffered PBO readbacks at a controlled
   cadence. Never map a PBO in the frame that issued its download.
5. **Eliminate redundant upload hashes.** Fingerprint immutable RAW buffers
   when they are created, not by walking the full buffer during each render.
6. **Profile pass fusion and allocation.** Measure GPU timestamps, pool
   temporary textures, and fuse compatible pointwise Finish/View operations
   only where numeric equivalence is proven.
7. **Redesign the UI.** Once the render service is stable, change the visible
   layout and control surface.
8. **Re-audit performance.** Reprofile real interaction traces after the UI
   redesign because new scopes, windows, overlays, and invalidation patterns
   can change the bottleneck.

## Stop Rules

Do not:

- enable the existing RAW worker by deleting its driver-safety guard;
- hide a permanent quality reduction behind an unlabeled "fast" mode;
- run automatic candidate generation from ordinary manual edits;
- make CPU readbacks part of every drag event;
- reuse a cached RAW boundary after any upstream dependency changes; or
- claim Adobe/Resolve internal parity from public product documentation.
