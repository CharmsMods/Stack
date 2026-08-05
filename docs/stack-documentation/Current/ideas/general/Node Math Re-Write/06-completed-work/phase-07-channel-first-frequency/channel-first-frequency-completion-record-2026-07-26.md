# Phase 7A Channel-First Frequency Completion Evidence

> **Historical completion record:** This file describes the state when this
> bounded slice ended. Current status and current code/tests supersede its
> point-in-time language.

- Date: 2026-07-26
- Phase 7A result: complete
- Program state after this pass: stopped; no implementation slice active
- Accepted decision: NMR-141
- Contract: `../../03-technical-contracts/regions-reductions-and-specialized-processing/channel-first-frequency-contract-v1.md`

## Product Result

The normal frequency workflow is now:

```text
Channel -> Frequency Filter -> Channel
```

The editable advanced workflow is:

```text
Channel -> Fourier Transform -> Apply Frequency Response
        -> Inverse Fourier Transform -> Channel
```

Stack exposes the approved ten-node family: Frequency Filter, Frequency
Response, Fourier Transform, Inverse Fourier Transform, Spectrum View, Apply
Frequency Response, Combine Spectra, Separate Spectrum, Recombine Spectrum,
and Spectrum Analyzer. The browser keeps Frequency Filter and five friendly
presets in the main Frequency section and places the remaining nine nodes under
Advanced Frequency.

Spectrum, Frequency Response, Magnitude, and Phase have exact specialized
sockets. They cannot connect to ordinary Image, Channel, or Mask sockets.
Legacy frequency definitions remain unresolved under schema 7 and are not
silently reinterpreted.

## Runtime And Math

The renderer owns typed RG32F complex resources and caches carrying source
role, original/padded extents, padding origin, edge policy, normalization,
precision, coordinate convention, and Hermitian state. Forward transforms use
centered power-of-two padding and no scale; inverse transforms apply `1/N`,
require a real-compatible Hermitian spectrum, crop to the source extent, and
preserve signed unclamped Channel values.

Mirror, Wrap, and Zero Pad are implemented. Response evaluation covers Smooth,
Gaussian, Butterworth, and Hard profiles plus symmetric multiple-notch
rejection. Apply Response uses the declared strength interpolation.
Separate/Recombine preserve raw amplitude and phase. Only Spectrum View applies
display mapping. Spectrum Analyzer reports the 256-bin `|F|^2` radial series,
non-DC selected-band power, peak frequency, and peak direction.

All Pass and zero-strength Frequency Filter paths bypass FFT work exactly.
Frequency resources and analysis results use typed fingerprints and bounded
caches.

## Editor And Persistence

Frequency Filter and Frequency Response provide numeric response controls,
cycles-per-pixel and reciprocal-detail readouts, response heatmaps, incoming
spectrum previews, draggable cutoff rings, and mirrored notch handles.
Previews are capped, cached, cancelable, and independent of render results.

Graph-input-capable parameters can be exposed as persistent Value sockets with
stable parameter-derived IDs. Connected values are authoritative while stored
inline settings remain fallbacks. Dynamic notch sockets use stable notch UUIDs.

Frequency Filter supports Extract Response Node and Expand to Advanced Nodes.
Both mutations preserve graph connections/settings, participate in one-step
undo/redo, and retain local settings when an external response disables them.

## Numerical And Behavioral Evidence

The GPU reference validator compares odd `7x5`, non-power-of-two, negative,
high-dynamic-range Channel data against an independent CPU DFT for forward and
inverse behavior under Mirror, Wrap, and Zero Pad.

The authored advanced graph:

```text
Channel -> FFT -> Separate -> Recombine -> IFFT -> Channel
```

round-tripped within the declared tolerance. The same live suite verified all
response profiles, strength-zero bypass, multiple mirrored notches, analyzer
results for a deterministic quarter-cycle spectrum, typed cache hits/misses,
Response extraction, advanced expansion, and undo/redo. The specialized live
maximum difference was `0.000959337`.

Graph behavior coverage verifies exact sockets, invalid Image/Mask/Spectrum
rejection and repair guidance, schema-7 persistence, browser presets, stable
parameter exposure, and legacy unresolved behavior.

## Commands And Results

```powershell
.\build.cmd
.\build\StackGraphBehaviorTests.exe
ctest --test-dir build -C Release -R StackNodeMath --output-on-failure
.\build\Stack.exe --validate-node-math-phase6
.\build\Stack.exe --validate-layer-registry
```

Results:

- repository-preferred Windows build: passed;
- graph behavior tests: passed;
- Node Math CTests: 13/13 passed, including five live-GPU suites;
- explicit Phase 6/7A live validation: passed;
- layer-registry validation: passed;
- signed/HDR CPU-to-GPU FFT and inverse references: passed;
- response, component, analyzer, cache, editor-action, and persistence
  validations: passed.

## Native Visual Follow-Up

The high-DPI graph implementation uses the editor's scale-aware node metrics,
pin layout, preview bounds, and hit regions, and those paths compile with the
full editor. A native screenshot review was attempted after the successful
build, but Windows capture could not activate the freshly launched Stack
window after its allowed recovery retry. A human high-DPI appearance pass
therefore remains a recorded, non-blocking visual QA item.

## 2026-07-27 Preview Correction

The first manual Frequency Filter test exposed a preview-only integration bug.
The editor intentionally creates temporary FFT and Spectrum View nodes with
negative IDs, but region planning and graph execution rejected every
`outputNodeId <= 0` before checking whether that node existed. This produced the
toast `Preview produced no pixels`.

Phase 7A-C1 changed both boundaries to validate exact output-node existence
instead of node-ID sign. Persisted editor nodes remain governed by the graph's
positive-ID contract; internal transient render nodes can now execute without
weakening missing-output validation.

The live regression constructs the same negative-ID
`Channel -> FFT -> Spectrum View` preview branch, requires a valid region plan,
and requires a nonempty `7x5` pixel result. It passed along with graph behavior,
13/13 Node Math CTests, layer-registry validation, explicit live GPU validation,
and the repository-preferred `build.cmd`.

## 2026-07-27 Viewport Traversal Correction

The next manual test showed the node-local preview working while the viewport
still reported `Connect the graph to the output to preview it`. The visible
Image connection was real. The completed-chain walker omitted Frequency Filter
and still queried the removed `imageIn` contract for Fourier Transform, inverse
transform, Spectrum View, and Spectrum Analyzer. The valid graph was therefore
discarded before viewport render submission.

Phase 7A-C2 gives every revised frequency node its exact traversal
dependencies. Frequency Filter follows its required Channel and optional
Response. The advanced nodes follow Spectrum, Response, Magnitude, and Phase
as declared by their sockets.

Graph coverage now requires both:

```text
Image -> Channel Split -> Frequency Filter -> Channel Combine -> Output
```

and the complete typed advanced Spectrum View chain to pass
`IsOutputConnected`. Graph behavior, 13/13 Node Math CTests, live GPU
validation, layer-registry validation, and `build.cmd` passed.

## Main Owners

- `src/Editor/NodeGraph/EditorNodeGraph.*`
- `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`
- `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`
- `src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.cpp`
- `src/Editor/Internal/EditorModuleGraphMutation.cpp`
- `src/Editor/Internal/EditorModuleGraphUiNodes.cpp`
- `src/Editor/Internal/EditorModuleRenderWorker.cpp`
- `src/NodeMath/NodeDataContract.*`
- `src/NodeMath/SpecializedPlanning.*`
- `src/Renderer/RenderPipeline.h`
- `src/Renderer/Internal/GpuFft.cpp`
- `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`
- `src/Renderer/Internal/RenderPipelineNodePasses.cpp`
- `src/Renderer/Internal/RenderPipelinePrograms.cpp`
- `src/App/Validation/Suites/NodeMathPhase6Validation.cpp`
- `tools/graph_behavior_tests.cpp`
- `tools/node_math_phase5_tests.cpp`
- `tools/node_math_phase6_tests.cpp`

## Stop Boundary

This completion does not implement partial Images, general Value/Channel
broadcast, source dissolution, the remaining C1-C8 contracts, unrelated
frequency-domain operations, the deferred Divide fix, RAW behavior changes, or
any broad Phase 7 catalog expansion. Those subjects require their own
activation and contract.
