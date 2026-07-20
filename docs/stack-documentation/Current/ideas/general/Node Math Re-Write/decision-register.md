# Node Math Rewrite Decision Register

- Status: active planning register
- Updated: 2026-07-17

This file separates established constraints from recommended direction and
unresolved product choices. A proposal in the research package is not silently
promoted to a product decision.

## Status Terms

| Status | Meaning |
| --- | --- |
| `Confirmed constraint` | Established by user intent or current code evidence. |
| `Adopted direction` | Used to organize the roadmap; still subject to evidence-based refinement inside that boundary. |
| `Open — blocking` | Must be resolved before the named phase can make dependent production choices. |
| `Open — nonblocking` | Can remain open while earlier independent work proceeds. |
| `Deferred` | Explicitly outside the current phase or initial program scope. |
| `Superseded` | Retained for history with a pointer to the replacing decision. |

## Confirmed Constraints

| ID | Status | Decision or constraint | Basis | Blocks or governs |
| --- | --- | --- | --- | --- |
| NMR-001 | Confirmed constraint | Stack must support factual low-level operations and friendly high-level tools; neither view replaces the other. | User intent preserved in the historical conversation handoff and research prompt. | Whole program |
| NMR-002 | Confirmed constraint | Friendly editing names do not define a unique formula. Every stable node needs an exact technical contract. | User intent plus the audited formula/name contradictions. | Definition schema and library work |
| NMR-003 | Confirmed constraint | The graph remains plug-and-play where math is structurally executable. Color, alpha, range, or display mismatch informs the user but does not normally block a connection or insert a hidden conversion. | User-confirmed 2026-07-15; consistent with the original permissive shader-graph intent. | Semantic validation and UI |
| NMR-004 | Confirmed constraint | Compatibility with pre-rewrite project files and historical pixel output is not required. The archived pre-rewrite executable is the compatibility path. | User-confirmed 2026-07-15. | Project format and all rewrite phases |
| NMR-005 | Confirmed constraint | A broad primitive catalog cannot be put directly on the current one-node/one-full-frame-pass model. | Audit verified full-canvas RGBA16F passes, no general fusion, and unbudgeted persistent caches. | IR/fusion before catalog expansion |
| NMR-006 | Confirmed constraint | RAW nodes remain specialized and nondeconstructible by default. Individual RAW nodes may be reconsidered only after the main rewrite, case by case. | User-confirmed 2026-07-15 plus audit and metadata research. | Compounds and specialized stages |
| NMR-007 | Confirmed constraint | Generative/semantic AI workflows are not part of the first primitive/compound program. | User scope and research boundary document 11. | Scope control |
| NMR-008 | Confirmed constraint | Verification targets the new intended math and implementation as changes land; Stack will not maintain a stored corpus of old projects/images or freeze historical output. | User-confirmed 2026-07-15. | Test strategy and every implementation pass |
| NMR-009 | Confirmed constraint | Fusion and optimization must preserve the exact dependency and operation order authored in the graph. `A -> B` cannot become `B -> A` unless equivalence for those exact definitions is proved and the result is unchanged. | User-confirmed 2026-07-15. | Semantic IR, compiler, and execution planning |

## Adopted Architectural Direction

| ID | Status | Direction | Consequence |
| --- | --- | --- | --- |
| NMR-010 | Adopted direction | Separate authored graph, typed semantic IR, and physical execution plan. | Visible nodes define meaning; passes and textures become planner choices. |
| NMR-011 | Adopted direction | Separate node definition, instance, and implementation. | Stable interfaces can survive optimized or graph-defined implementation changes. |
| NMR-012 | Adopted direction | Separate logical value type, per-value semantic descriptor, physical resource, and view/output configuration. | Storage reuse no longer implies semantic equivalence, and no graph-wide normalized state is assumed. |
| NMR-013 | Adopted direction | Use stable definition, port, and parameter identities with semantic versions from the rewrite forward. | Renames and future formula changes can be managed without preserving the pre-rewrite format. |
| NMR-014 | Adopted direction | Lower a small vetted pointwise set into an order-preserving fusable expression IR before publishing many tiny nodes. | Fine-grained authoring need not imply fine-grained materialization or a fixed operation order. |
| NMR-015 | Adopted direction | Classify high-level nodes as transparent graph compounds, graph-defined semantics with optimized equivalents, or opaque specialized operators. | Inspectability matches the real algorithm boundary. |
| NMR-016 | Adopted direction | Keep the operation catalogs encyclopedic and the default public browser selective. | Research breadth is not treated as an implementation backlog. |
| NMR-017 | Adopted direction | Keep validation non-mutating and attach diagnostics to stable semantic identities. Semantic mismatches are information/warnings; hard failure is reserved for structural impossibility, missing execution-critical data, or runtime failure. | Users retain deliberate freedom without hidden repair. |
| NMR-018 | Adopted direction | Do not impose one hidden internal working color space. Color identity and transfer state travel with each value or remain `Unknown`; users may insert explicit conversions anywhere. | “Working default” can only be an optional source/view convenience, not a graph-wide restriction. |
| NMR-019 | Adopted direction | Preserve independent R, G, B, and A control and support both straight and premultiplied alpha through explicit state and conversion. | No universal premultiplied representation is forced; individual compositing/filter nodes declare their math. |
| NMR-020 | Adopted direction | Do not silently normalize, tone-map, gamut-map, or otherwise repair the graph for viewport/output. Allow clipping or unusual appearance and surface the relevant state near the viewport/output UI. | The program informs the user instead of doing the edit for them. |
| NMR-102 | Adopted direction | Retain an embedded source color profile as descriptive metadata without changing pixels. Label an untagged ordinary source as `Unknown` until the user explicitly assigns a color meaning or converts it. | Source state becomes visible and traceable without guessing, normalization, or automatic conversion. |
| NMR-105 | Adopted direction | Display the graph's connected output directly in the main viewport, provide no separate optional preview transform, and show the connected output's current color state in the viewport footer. | Any appearance-changing color transform must be authored in the graph; the footer explains what the viewport is receiving. |

## Blocking Product Decisions

These questions should be resolved through small decision documents or
prototypes during the phase shown. Do not answer them accidentally in a data
structure or shader.

| ID | Status | Decision needed | Must be resolved by | Evidence or choices required |
| --- | --- | --- | --- | --- |
| NMR-109 | Adopted contract — 2026-07-16 | The first pointwise IR is an SSA-style, typed, single-input RGBA float-field program with uniform scalar/vec4 constants. Its vetted operations are identity, add, subtract, multiply, minimum, maximum, absolute difference, explicit clamp, Exposure EV, premultiply, and guarded unpremultiply. Authored operand/dependency order is immutable. Generated shader arithmetic is float32 and live materialization remains RGBA16F; CPU references use higher precision and compare at recorded operation/target tolerances. Uniform constants must be finite to lower; pixel NaN/Inf are not silently scrubbed. Divide, remap, comparisons/conditionals, curves/LUTs, matrices, reductions, multi-image expressions, and specialized operators are barriers. Fan-out, requested previews, unsupported kinds, precision/semantic boundaries, and a 48-operation/64-KiB generated-source limit materialize explicitly. Normal execution fuses only groups of at least two operations; requesting an intermediate output creates a temporary materialization plan. Generated programs use deterministic structural fingerprints, a bounded 64-program LRU compile cache, source-mapped failure fallback, a reusable transient target pool, and a 512-MiB byte-budgeted persistent graph cache. | Phase 4 compiler and planner; recorded in `phase-4-ir-contract-v1.md`. |
| NMR-110 | Adopted contract — 2026-07-16 | Phase 5 uses exact, self-contained compound embedding. Projects and copied graph payloads carry the transitive definition dependency closure they use. Instances pin exact definition ID/version/hash references; version ranges and automatic latest-version updates are rejected. Missing, invalid, hash-mismatched, or recursive dependencies remain typed unresolved instances with saved interface snapshots. Make Unique clones a definition under a new ID; Unpack expands a fresh canonical graph copy and rewires stable ports. Shipped templates seed a project but do not become mutable external runtime dependencies. | Phase 5; authority: `phase-5-compound-contract-v1.md`. |

## Resolved Or Narrowed By The 2026-07-15 Direction Update

| ID | Status | Result |
| --- | --- | --- |
| NMR-101 | Superseded | A mandatory project-wide working color identity is rejected by NMR-018. NMR-102 defines non-converting source labels, while NMR-105 rejects a separate preview transform. Neither constrains graph math. |
| NMR-103 | Deferred | ICC, OpenColorIO, or another implementation backend is a technical choice to research when an explicit profile/color-transform feature is scheduled. The graph contract should not hard-code one now. |
| NMR-104 | Adopted direction | NMR-019 replaces the idea of one universal alpha association. Both straight and premultiplied states are explicit; exact zero-alpha and compositing formulas belong to individual node definitions. |
| NMR-107 | Adopted direction | NMR-003 and NMR-017 establish permissive semantic diagnostics. The remaining question is presentation density and acknowledgement UX, tracked by NMR-124. |
| NMR-125 | Adopted direction | On 2026-07-15, Phase 0 established independent double-precision CPU reference formulas over generated in-memory values, with selected GPU implementations read back as float and compared using a formula-specific recorded tolerance. This replaces an old-output corpus; later definitions may add property, algorithm-specific, and user visual tests without changing this basic oracle pattern. Evidence: `StackNodeMathReferenceTests` and `2026-07-15-phase-0-current-architecture-delta.md`. |
| NMR-100 | Adopted contract — 2026-07-16 | Descriptor v1 uses an explicit logical type plus applicable fields whose knowledge state is Known, Unknown, or NotApplicable. It includes channel, color identity, transfer, reference, alpha, range, logical precision, spatial, sampling, units, and provenance. Dedicated temporal/flow/depth/coordinate/collection and physical-resource breadth is deferred without guessed defaults. Authority: `phase-1-contract-v1.md`. |
| NMR-106 | Adopted contract — 2026-07-16 | The rewrite starts `stack.node-graph` generation 1/schema 1. Every instance pins an exact definition ID/version/SHA-256 triple. Pre-rewrite or unknown generations fail explicitly; missing definitions remain unresolved without fallback; updates/migrations are intentional. Built-ins resolve from the shipped catalog; compound packaging remains NMR-110. Authority: `phase-1-contract-v1.md`. |
| NMR-108 | Adopted contract — 2026-07-16 | Definitions use deterministic readable lowercase `<namespace>:<path>` IDs; ports/parameters/implementations use stable scoped tokens; project/instance identities use canonical UUIDs; semantic versions and content hashes are exact persistence identity. Definition and implementation SHA-256 values are checked against canonical v1 content rather than accepted by shape alone. Authority: `phase-1-contract-v1.md`. |
| NMR-127 | Adopted contract — 2026-07-16; guard wording clarified during Phase 4 | `Unpremultiply` divides RGB by alpha only above `1e-6`. At or below the guard it sets RGB to black and preserves the independently controlled alpha channel; zero alpha therefore produces transparent black. Straight Source Over and premultiplied Source Over are separate explicit formulas/modes. A formula/input-association mismatch warns but remains executable. Evidence: Phase 2 CPU reference cases/live shader integration and Phase 4 IR equivalence checks. |
| NMR-128 | Adopted contract — 2026-07-16 | The first Technical Image family is Assign sRGB, Assign Linear sRGB, Assign Linear Display-P3, sRGB Decode, sRGB Encode, linear sRGB→Display-P3, linear Display-P3→sRGB, Exposure (EV), Premultiply, and Unpremultiply. Assign operations change descriptors only. Pixel operations preserve extended negative and greater-than-one values; signed extended-sRGB transfer and fixed D65 linear matrices are used without hidden clamping. |
| NMR-129 | Adopted contract — 2026-07-16 | Direct PNG export does not insert a color transform, tone/gamut repair, normalization, or alpha conversion. It writes sRGB metadata for declared sRGB+sRGB transfer, Display-P3 cICP for declared Display-P3+sRGB transfer, or an exactly retained PNG iCCP payload whose dependency identity still matches. Unknown/unavailable state exports untagged with information; premultiplied output is blocked until the user places Unpremultiply. PNG's existing 8-bit quantization remains an explicit output limitation. |
| NMR-130 | Adopted contract — 2026-07-16 | Phase 2 semantic inspection uses compact wire tooltips, output-node notices, and a direct-viewport footer showing color, transfer, alpha, and notice count. Diagnostics are stable, non-mutating, and informational/warning unless execution is structurally impossible. Phase 4 may extend execution inspection without changing this permissive baseline. |
| NMR-131 | Adopted contract — 2026-07-16 | Phase 3 first-class values distinguish availability (Known, Unknown, Missing, Failure) from storage class (Uniform, PerPixelField, StructuredResource, SpecializedHandle) and logical type. Boolean, integer, scalar, vector, matrix, coordinate, curve, histogram/statistics, metadata/resource, and specialized-handle envelopes serialize exactly. Broadcast, component extraction, uniform-vector reduction, and units are explicit; no reduction scheduler or implicit conversion was introduced. |
| NMR-132 | Adopted contract — 2026-07-16 | Phase 3 live definitions use deterministic `stack:...` ID, semantic version, and SHA-256 content identity. One validated registry supplies browser metadata and static ports, incorporates the layer timeline parameter catalog, declares serialized settings envelopes and animation policy, and resolves graph-schema-5 nodes exactly. Missing or mismatched forward identities remain unresolved without fallback. |
| NMR-133 | Adopted contract — 2026-07-16 | Phase 5B makes authored compound traversal resolve the exact public output and its canonical contributing public inputs before renderer submission. Every socket has centralized presentation metadata and explicit Unknown states without tightening permissive connections. Expanded nodes label displayed pins; compact forms reveal labels on interaction; advanced sockets use progressive reveal and the Connections inspector; shared pin/wire detail cards wait 0.7 seconds. Program appearance settings version 8 adds Adaptive/Always/Interaction Only/Off wire labels and Floating/Break Line layout, defaulting to Adaptive/Floating. Project serialization does not store these visual preferences. Authority: `phase-5b-typed-connection-ui-contract.md`. |
| NMR-134 | Adopted contract — 2026-07-16 | Phase 5B-C makes connection text follow the local wire tangent while staying upright; anchors delayed cards to the inspected pin/wire and resets them on interaction or focus loss; removes the on-node Connections dropdown and Image/Output cursor-following preview tooltip; and adds appearance version 9 text-size, Zoom-Aware/Fixed sizing, default-off outline, and node width/UI/grab-surface preferences. Full-image outputs are structurally incompatible with scalar-field/mask inputs: Stack rejects the drag, never auto-spawns Luminance Mask, and names explicit extraction choices. This does not restrict structurally executable color, transfer, alpha, or range experimentation. Authority: `phase-5b-typed-connection-ui-contract.md` and `2026-07-16-phase-5b-c-corrective-completion-evidence.md`. |
| NMR-135 | Adopted contract — 2026-07-17 | Phase 6A standardizes half-open finite regions, full/data windows, explicit raster origin, pixel aspect, render scale, ROI/halo mappings, planner-derived tile support, and incomplete-result cancellation. The first live neighborhood proof is the unchanged Gaussian/Box square filter with exact `int(max(1, amount))` support and clamp border. No public kernel/sampler/reduction value is introduced. Authority: `phase-6-region-contract-v1.md`. |
| NMR-136 | Implemented — 2026-07-17 | Phase 6B introduces `Field Mean` as the first reusable reduction: an explicit one-channel per-pixel `ScalarField` becomes one uniform `Scalar` using deterministic compensated float64 arithmetic mean over the full current field. Images require an explicit channel/luminance extractor; empty or non-finite populations fail; reduction is a full-frame/fusion boundary; runtime results are fingerprint-cached and not serialized. The authored live graph measured 0.5 from 32 samples, reused the cache, and matched constant Exposure EV with maximum difference 0. Authority: `phase-6b-reduction-contract-v1.md` and `2026-07-17-phase-6b-completion-evidence.md`. |
| NMR-137 | Implemented — 2026-07-17 | Phase 6C introduces `Reformat` version 1 as the first true extent-changing graph operation. It declares an exact finite output width/height, inverse pixel-center mapping, Nearest or Linear reconstruction, Clamp border, independent RGBA sampling, descriptor-preserving extent propagation, and a full-frame Sample/Resample materialization boundary. Different input extents at an operation without an alignment policy fail planning and require an explicit Reformat; Stack does not silently stretch or crop. Authority: `phase-6c-geometry-and-specialized-contract-v1.md`. |
| NMR-138 | Implemented — 2026-07-17 | Phase 6C brings representative RAW, RAW neural/external, multi-frame, FFT, inverse FFT, retained-spectrum, scope, preview, and export work under typed stage plans that declare logical boundaries, locality, scale, cancellation, and failure behavior without pretending they are pointwise nodes. RAW remains opaque; scope/preview/export are non-mutating consumer boundaries. The authored FFT-to-inverse-FFT graph proves live specialized planning and execution. Authority: `phase-6c-geometry-and-specialized-contract-v1.md` and `2026-07-17-phase-6c-and-phase-6-completion-evidence.md`. |
| NMR-124 | Adopted contract — 2026-07-16 | Phase 4 execution inspection extends the existing opt-in Graph Performance popup rather than adding permanent graph clutter. It reports ordered fused groups, nodes and avoided passes, RGBA16F target bytes, CPU submit timing when requested, program-cache reuse, persistent/transient memory and evictions, fingerprints, and source-mapped failures. Normal wire and viewport presentation remains unchanged. |

## Open But Not Yet Blocking

| ID | Status | Question | Earliest owning phase |
| --- | --- | --- | --- |
| NMR-120 | Open — nonblocking | Which primary browser families, accents, icons, and search tags should ship? | Phase 7 |
| NMR-121 | Open — nonblocking | Which first public primitives and high-level compounds provide the smallest useful product slice? | Phase 7 delivery |
| NMR-122 | Narrowed — nonblocking | Phase 6A exposes no general kernel/sampler values. Phase 6C adds only the exact Reformat vertical slice with Nearest/Linear and Clamp. Which general kernel, morphology, sampler, and reconstruction values justify public exposure remains open. | Phase 7 selection or a later dedicated program |
| NMR-123 | Narrowed — nonblocking | Field Mean is selected and owned by NMR-136. Which additional reductions justify public reuse remains open. | Phase 7 selection or a later dedicated program |
| NMR-126 | Open — nonblocking | When should an inexpensive fan-out expression be recomputed versus materialized? Phase 4 deliberately chose materialization for v1. | Future planner refinement after Phase 4 |

## Deferred Scope

| ID | Status | Deferred topic | Revisit condition |
| --- | --- | --- | --- |
| NMR-200 | Deferred | General learned/generative node workflows | Only after the conventional typed operator/diagnostic contract is stable and a separate program is approved. |
| NMR-201 | Deferred | Implementing every operation listed in files 01–11 | Only when a product need, node contract, execution class, and reference tests justify a specific operation. |
| NMR-202 | Deferred | Recursive or cyclic compound definitions | Only if Stack introduces a deliberately bounded iteration construct; ordinary graph cycles remain invalid. |
| NMR-203 | Deferred | Full HDR display and print-proof product breadth | May begin with a bounded SDR contract, provided the descriptor and output architecture do not preclude later targets. |

## Decision Recording Rule

When a decision closes:

1. change its status and record the date;
2. state the chosen behavior and rejected alternatives briefly;
3. cite the evidence, prototype, standard, or user confirmation;
4. name affected definitions, schemas, phases, generated tests, and future
   version behavior;
5. update the phase gate that depended on it; and
6. do not rewrite historical audit findings to match the new design.
