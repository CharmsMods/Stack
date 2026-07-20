# 16 — Current Stack Implementation Mapped to the Proposed Model

Ground-truth source: `STACK_IMAGE_MATH_AND_PIPELINE_AUDIT as of 7-12-26.md`  
Status date: 2026-07-12

## Purpose

This file connects the general operation research to the actual audited Stack code. It does not replace the full audit. It extracts the constraints that materially change the primitive/compound-node design.

## Current architecture in one paragraph

Stack is a C++17 Windows editor using GLFW, Dear ImGui, OpenGL 4.3, stb image I/O, and LibRaw. The editor owns a flat typed DAG. The renderer evaluates recursively from the requested output, caches by fingerprints, reuses fan-out results, and normally gives each layer its own full-canvas `GL_RGBA16F` pass/texture. A connected graph mask adds another pass. There is no general pointwise fusion, general target pool, cache byte budget/LRU, semantic image descriptor, executable nested graph, or shared compound definition.

## Current image pipeline

| Stage | Audited behavior | Consequence |
|---|---|---|
| PNG/JPEG decode | `stbi_load(...,4)` produces RGBA8; 16-bit PNG is reduced through this path; color chunks/profile state are not retained | Ordinary inputs begin as untagged encoded numeric values with lost precision/meaning |
| Ordinary GPU upload | Unsized normalized `GL_RGBA` from unsigned bytes | Exact source component allocation is driver-dependent/unknown; no sRGB texture decode |
| RAW | LibRaw + sensor metadata → normalization/demosaic/WB/camera transform/exposure → RGBA16F near linear-sRGB primaries | RAW has stronger scene meaning until it becomes generic `Image` |
| Generic node output | Separate RGBA16F full-canvas target | Negative/>1 values can survive only until a node clamps; meaning remains undeclared |
| Wires | Five coarse types: Image, Mask, Value, Analysis, Raw | Image and Mask are semantically overloaded; no scalar/vector/matrix/curve/coordinate/histogram values |
| Viewport | Worker texture goes directly to ImGui; no Stack-managed display shader/OETF | Working values are not reliably converted to the display encoding |
| Export | Fresh full-res render → RGBA8 readback → untagged 8-bit PNG | Final clamp/quantization and color ambiguity; preview and export are separate executions |

## Current primitive-like surface

| Family | Live capability | Reusability judgment |
|---|---|---|
| Scalar/data math | Clamp, Add, Subtract, Multiply, Divide, Average, Min, Max, Difference, Remap | Strongest existing primitive family, but values are texture/red-channel based and Divide loses denominator sign |
| Masks | Solid, linear/radial/noise generators; max/add-like, subtract, intersect, difference; invert/remap/threshold | Useful primitive base; Mask type carries little semantic detail |
| Channels | Split and Combine | Useful vector boundary; no general swizzle/vector/matrix type |
| Mix | Normal, Average, Add, Multiply, Screen, Alpha Over | Useful multi-input family; Alpha Over is mathematically defective and alpha convention is undeclared |
| LUT | 1D/3D table, domain, transfer options | Useful special transform; no machine-enforced input/output color identity |
| Curves | Tone Curve builds a private 256-entry GPU LUT | Efficient but trapped inside one payload; not a graph value |
| Neighborhood | Blur, sharpen, bilateral, NLM, median, mean, effects | Broad visible coverage, but each node owns private sampling code; no generic kernel/border/ROI contract |
| Frequency | FFT/IFFT, masks, spectrum math | Specialized global engine; current RGB/alpha loss and magnitude/recombine semantics prevent use as a general faithful primitive base |
| Reductions | Scopes and RAW auto routines | Specialized/private; no reusable scalar/histogram outputs |
| RAW/calibration | LibRaw decode, demosaic, matrices, exposure, tone/view stages | Strong special pipeline; metadata meaning disappears at generic Image boundary |

## Current high-level catalog

The audit reconciled 115 discoverable entries: 52 visible registry-backed layers and 63 explicit non-layer variants. The breadth is real, but visual breadth is not the same as composable backend breadth.

Examples of monolithic or node-specific high-level work include:

- 3-Way Color Grade
- Tone Curve
- View Transform
- Scene/Classical/Neural denoise
- Bilateral/NLM/Median/Mean filters
- Tilt-Shift, Optical Blur, Airy Bloom, Glare
- DCT/chroma/wavelet compression simulations
- Analog Video, lens/ripple/heatwave effects
- RAW Decode, RAW Development, Advanced Auto Develop
- HDR Merge and MFSR shell

These nodes demonstrate that Stack can execute complex work. They do not demonstrate user-editable compound graphs.

## Current graph and compound state

| Desired capability | Current state | Gap |
|---|---|---|
| Flat DAG and cycle prevention | **Supported** | Keep and reuse |
| Demand evaluation/fan-out reuse | **Supported** | Extend into physical-plan scheduler |
| Visual groups | **Supported** | Only titled frames; no execution/interface boundary |
| Copyable presets | **Supported as copies** | No shared definition or instance relationship |
| Nested/executable compounds | **Absent** | Model, evaluator, snapshot, serialization, undo and UI are flat |
| Exposed compound inputs/parameters | **Absent** | Parameters are class/payload-specific |
| Stable definition/interface IDs | **Absent** | Current links use integer endpoints; pasted IDs are remapped copies |
| Compound version/migration | **Absent** | JSON writes version 3 but loader does not enforce it |
| Make unique/unpack | **Absent as compounds** | Presets already copy, but are not linked definitions |
| Pointwise subgraph flattening/fusion | **Absent** | Every ordinary node materializes a pass/target |

## Performance consequence of tiny public nodes

A 3840×2160 RGBA16F image is roughly 63.3 MiB before allocator/driver overhead. Under the current design, a chain of small pointwise nodes can retain one cached full-resolution texture per node. Therefore:

- Decomposing a convenience node into ten ordinary nodes is not only a UI issue.
- A public primitive library must not imply one materialized pass per primitive.
- Pointwise expression fusion and resource-lifetime planning are architectural prerequisites.
- Debug boundaries should be selectable: users may inspect an intermediate, but unobserved intermediates can stay virtual.

## Current formula mappings that should remain explicit

| Visible node | Audited formula/behavior | Future treatment |
|---|---|---|
| Brightness | `rgb += amount`, then clamp | Rename/describe as RGB Offset inside technical details; friendly Brightness may offer methods |
| Contrast | `(rgb-.5)*(1+amount)+.5`, then clamp | Contrast Around Pivot with pivot `.5`; declare domain |
| Saturation | Rec.709 dot, then `mix(gray,rgb,1+amount)` | A valid named RGB-axis saturation method; declare coefficients/domain |
| Warmth | `R += .1w`, `B -= .1w` | Creative Warmth, not temperature/WB/adaptation |
| Sharpen | Four-neighbor average, threshold gate, detail add | Specific compact sharpen; not generic convolution/unsharp |
| 3-Way Grade | Rec.709 tonal weights; additive shadow/mid chroma; multiplicative highlight; clamp | Version and document; do not generalize as the only three-way model |
| Tone Curve | Private 256-sample curve LUT; scene/display options | Promote Curve1D to a first-class value; preserve optimized LUT evaluation |
| View Transform | Tone/range/gamut work and clamp, no final OETF | Split working→rendering/view→display encoding contracts |
| Mix Alpha Over | Backdrop contribution omits backdrop alpha | Correct with explicit migration/versioning; add reference tests |

## Defects that must not become standards

The following labels/behaviors are implementation findings, not definitions for the new library:

- DataMath Divide discards the denominator sign.
- Mix Alpha Over is wrong for straight and premultiplied source-over.
- Alpha association is unspecified across the system.
- View Transform omits final display encoding.
- HDR Compressor darkens shadows rather than performing a meaningful highlight compressor/recovery.
- RAW demosaic UI/settings are ignored because the runtime always selects Bilinear.
- Error Diffusion is not recursive error diffusion.
- Expand Canvas does not change extent.
- MFSR passes Reference through.
- Spectrum Analyzer has no evaluator.
- Spectrum Magnitude/Recombine is not a correct round trip.
- Median Search Radius is ignored.
- Noise exposes modes/parameters that are unimplemented or unserialized.
- Several effects force alpha to 1 or process alpha inconsistently.

Correcting these may change old project output. That requires explicit compatibility behavior, not a silent formula replacement.

## Capability gap matrix

| Required future capability | Current evidence | Priority |
|---|---|---|
| Semantic image descriptor | Generic Image has none | Foundational |
| Explicit alpha association | Undeclared/inconsistent | Foundational |
| Correct working→view→display/export path | Incomplete/unmanaged | Foundational |
| Profile-aware high-bit-depth ordinary I/O | stb path loses metadata and 16-bit PNG precision | Foundational |
| Scalar/vector/matrix/curve types | Mostly absent | Before broad primitive expansion |
| Coordinate/extent/sampler types | Absent; fixed canvas | Before true geometry primitives |
| Histogram/reduction result types | Private only | Before inspectable Auto nodes |
| Pointwise semantic IR and fusion | Absent | Before many tiny math nodes |
| Kernel/ROI/halo abstraction | Node-specific | Before generic filters and efficient tiling |
| Physical pass/resource planner | Recursive per-node passes/caches | Before large compound graphs |
| Compound definition/instance model | Absent | Before editable convenience compounds |
| Stable version/migration IDs | Weak/unforced graph version | Before project-facing semantic changes |
| Formula/color/alpha golden tests | Absent | Before changing behavior |

## Recommended transition boundaries

### Boundary 1 — Make current output interpretable

- Add semantic descriptors/provenance.
- Establish alpha convention and correct source-over.
- Establish explicit input assignment, working state, viewport view/display, and export destination.
- Preserve 16-bit/profile-aware ordinary input where supported.
- Add reference image/pixel tests before changing formulas.

### Boundary 2 — Make math values first class

- Add scalar, Boolean, integer, vector, matrix, curve, LUT, coordinate, histogram/statistics, and metadata handles.
- Stop representing every scalar as the red channel of a texture.
- Separate color images, masks, data images, depth, flow, and spectra.

### Boundary 3 — Make primitives affordable

- Lower pointwise graphs to one semantic expression IR.
- Fuse compatible operations.
- Materialize only at observable, sampled, reduction, precision, cache, or explicit debug boundaries.
- Add temporary-target pooling and cache budget/LRU.

### Boundary 4 — Make compounds real

- Add definition/instance, stable interface IDs, versioning, exposed parameters, nested serialization, migrations, dependency handling, Make Unique, and Unpack.
- Preserve current visual groups as a separate feature.

### Boundary 5 — Expand the operation library

- Begin with math, vectors/channels, masks/compositing, curves, explicit transfers, and correct alpha.
- Then add generic sampled filters/geometry/reductions.
- Keep RAW, neural, FFT, heavy denoise, and other specialized systems as dedicated backends with declared contracts.

## What can be reused

Stack does not need to start over. Reusable foundations include:

- Existing graph mutation and traversal.
- Cycle and coarse compatibility checks.
- Recursive evaluation and fingerprints.
- Fan-out reuse.
- Node UI and browser.
- Selection export/remap logic.
- Layer registry and payload serialization patterns.
- RGBA16F fullscreen programs.
- Node-specific multipass and FFT/compute examples.
- RAW metadata boundary.
- Existing mask generators/combine/utilities.
- Existing Channel Split/Combine, Data Math, Mix, and LUT code as migration inputs.

The redesign is primarily about giving those pieces truthful semantics, a compiler/scheduler layer, and a real reusable compound model.

## Verification requirement

The returned audit had high confidence in static/build-selected code paths but did not rebuild/visually exercise every dirty-tree change. Before implementation decisions are locked, rerun a clean controlled build and add instrumentation for:

- actual source/default framebuffer formats;
- sRGB enable/state and display path;
- exact intermediate pixels before/after view;
- alpha test vectors;
- import/export profile/bit-depth round trips;
- node formula golden images;
- CPU/GPU parity;
- cache memory accounting and per-node GPU time.

The audit is authoritative for the inspected state. The tests are needed to turn that snapshot into an enforceable production contract.
