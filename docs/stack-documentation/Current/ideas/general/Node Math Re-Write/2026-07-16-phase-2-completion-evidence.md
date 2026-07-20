# Phase 2 Completion Evidence

- Phase: 2 — Semantic Spine, Diagnostics, And Technical Image Boundaries
- Completed: 2026-07-16
- Status: implementation and automated gates complete; native human visual
  confirmation recorded as a follow-up
- Compatibility policy: forward-only; pre-rewrite project compatibility and
  historical pixel matching remain out of scope
- Next phase status at this handoff: Phase 3 was pending and not activated;
  see the later `2026-07-16-phase-3-completion-evidence.md` for its completion

## Completion Decision

Phase 2 code work is complete. Stack now carries descriptive image meaning
from ordinary sources through live graph links, renderer cache identities,
direct viewport presentation, and PNG output. The graph remains permissive:
semantic analysis explains suspicious color, transfer, alpha, range, or output
state without changing the graph, inserting conversions, or preventing
numerically executable work.

The phase also adds a deliberately small set of explicit Technical Image
operations and two exact Source Over formulas. Pixel-changing behavior occurs
only when the user places one of those operations or selects the corresponding
blend mode. Phase 3 value types and Phase 4 semantic IR/fusion were not begun.

The native app could not be controlled visually in this session, so no visual
pass is claimed. The short human checklist at the end remains the only recorded
follow-up; the build, math, semantic, graph-connectivity, and persistence gates
all passed.

## Implemented Contract Owners

| Owner | Result |
| --- | --- |
| `src/NodeMath/DescriptorSerialization.*` | Exact JSON serialization/parsing for every Phase 1 `ValueDescriptor` field, canonical content, and stable SHA-256 descriptor identity. Malformed content fails cleanly. |
| `src/NodeMath/SourceColorMetadata.*` | Non-converting PNG/JPEG color-metadata inspection, retained payload bytes, descriptive descriptor, issues, and stable dependency identity. |
| `src/NodeMath/TechnicalImageMath.*` | Canonical CPU formulas, descriptor propagation, diagnostics, compact labels, and direct PNG policy for the first explicit technical operations. |
| `src/NodeMath/SemanticSpine.*` | Deterministic, non-mutating semantic graph analysis with descriptor-bearing edges, output descriptors, stable diagnostics, and one semantic fingerprint. |
| `src/NodeMath/PngMetadataWriter.*` | Valid CRC-bearing insertion/replacement of one justified PNG color-metadata representation after `IHDR`. Conflicting hidden metadata choices fail. |
| `tools/node_math_phase2_tests.cpp` | Generated descriptor, source metadata, PNG metadata, transfer/color, Exposure, alpha, compositing, semantic, cache-identity, and output-policy checks. |

## Source Meaning And Provenance

### Ordinary image import

The original encoded source is inspected without changing decoded pixels.
Phase 2 recognizes and retains these first signals:

- PNG `cICP` for the supported sRGB and Display-P3 cases;
- PNG `iCCP` payload bytes and profile label;
- PNG `sRGB`;
- recognized PNG `gAMA` plus `cHRM` combinations; and
- assembled JPEG APP2 ICC profile segments.

An embedded signal produces a descriptive tagged state and stable dependency
identity. An ordinary untagged image produces explicit `Unknown` color,
transfer, and reference state with Untagged provenance. Stack does not assume
sRGB and does not convert pixels during inspection.

The source metadata is serialized independently from the embedded project-
storage PNG, so a storage re-encode cannot silently replace the meaning of the
original source.

### Specialized and generated sources

Generated and opaque specialized live sources enter semantic analysis with an
explicit Unknown image descriptor unless their owner supplies a stronger
contract. RAW behavior remains specialized and was not decomposed in this
phase.

## Explicit Technical Image Operations

The node browser now contains an `Image Technical` family with these ten
operations:

| Operation | Pixel behavior | Descriptor behavior |
| --- | --- | --- |
| Assign sRGB | No pixel change | Declares sRGB color identity and sRGB transfer. |
| Assign Linear sRGB | No pixel change | Declares linear sRGB. |
| Assign Linear Display-P3 | No pixel change | Declares linear Display-P3. |
| sRGB Decode | Applies signed extended-sRGB decode to RGB | Declares linear transfer; preserves alpha and extended values. |
| sRGB Encode | Applies signed extended-sRGB encode to RGB | Declares sRGB transfer; preserves alpha and extended values. |
| Linear sRGB to Display-P3 | Applies the fixed D65 linear matrix to RGB | Declares Display-P3 color identity; preserves transfer/alpha where applicable. |
| Linear Display-P3 to sRGB | Applies the inverse fixed D65 linear matrix to RGB | Declares sRGB color identity; preserves transfer/alpha where applicable. |
| Exposure (EV) | Multiplies RGB by `2^EV` in its authored graph position | Preserves alpha; warns when the connected transfer is known to be nonlinear but still runs. |
| Premultiply | Multiplies RGB by alpha | Declares premultiplied alpha. |
| Unpremultiply | Divides RGB by alpha above `1e-6`; otherwise outputs transparent black | Declares straight alpha and reports suspicious input association. |

The CPU and GLSL implementations use the same formulas. Transfer and matrix
operations do not clamp negative or greater-than-one values. This phase does
not create a graph-wide working color space and does not restrict where the
user can place these nodes.

## Alpha And Compositing

`Blend Images` now has separate `Straight Source Over` and `Premultiplied
Source Over` modes in addition to its numeric blend modes. Input B is the
source and input A is the backdrop. The factor controls source opacity.

- Straight Source Over evaluates the unassociated-RGB equation and returns
  straight RGB plus output alpha.
- Premultiplied Source Over evaluates the associated-RGB equation and returns
  premultiplied RGB plus output alpha.
- A known input-association mismatch produces a stable warning but does not
  block execution.
- R, G, B, and A remain independently available through the existing channel
  paths.

No universal alpha association is imposed. A user who needs a different state
places Premultiply or Unpremultiply explicitly.

## Live Semantic Spine

The editor's render snapshot now builds a separate pure semantic graph after
the normal render graph is assembled.

- Source nodes provide descriptive source descriptors.
- Technical Image nodes propagate the exact declared operation.
- The two Source Over modes propagate their explicit alpha formula.
- Direct Output records the connected descriptor without applying a preview or
  output transform.
- Useful image links carry the propagated descriptor and its stable content
  identity.
- The snapshot records output descriptor identity, stable diagnostics, and one
  semantic fingerprint.
- Render-node cache fingerprints include the semantic descriptor identity, so
  descriptive meaning changes cannot silently reuse a cache entry created for
  different meaning.

Analysis never edits links, inserts nodes, converts pixels, normalizes values,
or chooses a working color space. Structural impossibility can still fail;
unusual but numeric work remains executable and visible through information or
warning diagnostics.

## Graph Model And Persistence

Phase 2 adds `TechnicalImage` as a live graph and render node kind. The graph
model now treats it as an ordinary image pass-through for:

- image connection validation;
- cycle and completed-chain traversal;
- scalar-field pass-through where the existing graph permits scalar image
  streams;
- source-reference resolution;
- main-chain navigation;
- node-browser thumbnails; and
- graph validation.

Graph JSON version 4 serializes the operation and Exposure parameter. Image
payloads serialize the original `SourceColorMetadata` separately from embedded
storage pixels. A graph behavior test proves that
`Image -> Exposure -> Output`, its Exposure value, its Unknown untagged source
state, and its dependency identity survive round trip.

This is intentionally forward-only. No loader compatibility layer or formula
emulation was added for pre-rewrite project output.

## Inspection And Direct Viewport

Phase 2 exposes the live semantic state without adding a hidden display path.

- Image nodes show a compact source color/transfer/alpha label.
- Technical Image nodes expose their operation-specific controls and notes.
- Output nodes identify the direct semantic result and applicable notices.
- Hovering an image wire shows compact color, transfer, alpha, extent, range,
  provenance, and relevant diagnostics.
- The viewport reserves a compact footer reading `Direct graph result`, the
  connected color/transfer/alpha state, and the notice count.
- Static comparison presentation uses the same direct footer boundary.

There is no independent preview transform. A color transform that changes the
image belongs in the authored graph.

## Direct PNG Output Policy

PNG export evaluates the descriptor connected to the chosen output before
encoding.

- Declared sRGB color with sRGB transfer writes an `sRGB` chunk.
- Declared Display-P3 color with sRGB transfer writes Display-P3 `cICP`.
- An exactly retained PNG `iCCP` payload may be written only when its dependency
  identity still matches the connected descriptor.
- Unknown color or a profile that cannot be represented by this first writer
  exports untagged and reports information rather than guessing.
- Premultiplied output is blocked with an instruction to place Unpremultiply,
  because PNG stores straight alpha.
- Conflicting metadata choices fail instead of writing contradictory chunks.

The output path does not add a color conversion, tone map, gamut map,
normalization, or alpha repair. PNG encoding remains the existing 8-bit
quantizing boundary; Phase 2 reports that limitation rather than presenting it
as full floating-point preservation.

JPEG ICC input is retained descriptively, but this first direct PNG writer does
not transcode a JPEG ICC payload into a new PNG `iCCP` representation. General
ICC/OCIO transform backend work remains deferred.

## Decisions Closed In This Phase

- NMR-127: guarded Unpremultiply and the first explicit compositing formulas.
- NMR-128: the first Technical Image operation set and numerical policy.
- NMR-129: direct PNG metadata and premultiplied-output policy.
- NMR-130: compact semantic inspection and permissive diagnostic presentation.
- NMR-124 is narrowed: Phase 2 avoids permanent dense wire badges; Phase 4 may
  decide additional fused-execution inspection.

## Generated And Regression Evidence

### Phase 2 generated checks

`StackNodeMathPhase2Tests.exe` passed 30/30 checks covering:

- exact descriptor JSON round trip and stable identity;
- malformed descriptor rejection;
- Display-P3 cICP, Unknown untagged, and retained PNG iCCP source inspection;
- source metadata JSON round trip and stable dependency identity;
- valid sRGB and Display-P3 PNG output metadata plus conflicting-choice
  rejection;
- signed extended-sRGB encode/decode round trip across negative, fractional,
  zero, one, and greater-than-one values;
- linear sRGB/Display-P3 round trip with extended RGB and preserved alpha;
- Exposure multiplication and alpha preservation;
- Premultiply/Unpremultiply round trip and the zero-alpha guard;
- straight and premultiplied Source Over reference values;
- encoded Exposure warning while remaining executable;
- deterministic semantic and descriptor-bearing edge fingerprints;
- Unknown source/direct-output visibility without blocking;
- alpha-formula mismatch warning without blocking;
- Display-P3 direct PNG policy; and
- explicit rejection of premultiplied direct PNG output.

### Commands and results

```powershell
cmake --build build --config Release --target StackGraphBehaviorTests
.\build\StackGraphBehaviorTests.exe
```

Result: build passed; graph behavior passed, including the new Technical Image
connectivity and persistence round trip.

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Result: 4/4 registered tests passed:

- `StackNodeMathReference.Cpu`
- `StackNodeMathReference.Gpu`
- `StackNodeMathContract.Phase1`
- `StackNodeMathSemantic.Phase2`

```powershell
cmake --build build --config Release --target Stack
```

Result: passed; `build/Stack.exe` linked successfully.

```powershell
.\build.cmd
```

Result: passed. The preferred Windows build produced `build/Stack.exe` and all
configured test executables, including `StackNodeMathPhase2Tests.exe`.

## Standards Used For The First Formulas

- W3C CSS Color 4 for extended signed sRGB transfer behavior and the published
  sRGB/Display-P3 matrices: <https://www.w3.org/TR/css-color-4/>
- W3C Compositing and Blending Level 1 for Source Over straight/premultiplied
  relationships: <https://www.w3.org/TR/compositing-1/>
- PNG Third Edition for color metadata and straight-alpha output constraints:
  <https://www.w3.org/TR/png-3/>

These references define the selected first contract; they are not permission
to add every color/profile feature in Phase 2.

## Known Limitations And Follow-Up

- Native human UI review was not completed in this session because Windows app
  automation was unavailable. No screenshot or visual result is claimed.
- The first source inspector is deliberately bounded to the recorded PNG/JPEG
  signals. It is not a general ICC or OCIO transform engine.
- Direct export policy is currently PNG-focused and preserves only an exactly
  reusable PNG iCCP payload; other export formats require their own explicit
  policy.
- PNG output is 8-bit and cannot preserve arbitrary floating-point range.
- Generated CPU cases and live shader compilation are covered, but this phase
  did not add a general GPU readback comparison for every new Technical Image
  shader operation.
- Unknown specialized/RAW source semantics remain Unknown unless their owning
  node supplies a descriptor. Phase 2 did not decompose RAW nodes.
- The live graph still uses the existing coarse socket/value system. First-
  class scalar/vector/matrix/curve work belongs to Phase 3.
- The renderer still executes these nodes as individual passes. Semantic IR,
  fusion, and resource planning belong to Phase 4.

## Native Human Visual Checklist

Before activating Phase 3, confirm in `build/Stack.exe`:

1. The Node Browser shows all ten `Image Technical` entries and adding each
   produces image input/output pins.
2. `Image -> Exposure (EV) -> Output` connects, renders, and changes only RGB
   when EV changes.
3. An encoded/Unknown source can still pass through Exposure and shows a notice
   instead of being blocked or auto-converted.
4. Hovering relevant image wires shows the compact semantic tooltip without
   clipping or permanent visual clutter.
5. The viewport footer says `Direct graph result` and shows the connected
   color/transfer/alpha state and notice count.
6. Output-node notices remain informational unless output is structurally
   impossible.
7. Premultiply/Unpremultiply and the two Source Over modes behave distinctly on
   a translucent example.
8. PNG export tags declared sRGB/P3, leaves Unknown untagged with a message, and
   blocks premultiplied output until Unpremultiply is added.

If a visual or interaction defect is found, reopen only a bounded Phase 2 fix.
Do not use that fix to begin Phase 3 or Phase 4.

## Exit Assessment

| Phase 2 exit condition | Result |
| --- | --- |
| Declared color, alpha, range, extent, provenance, view, and output behavior | Pass in generated/semantic tests and live descriptor propagation; native visual status is explicitly pending. |
| Encoded image can connect to Exposure, receive an explanation, and run authored math | Pass. |
| Clipping/unusual appearance is permitted and not silently repaired | Pass by formula, diagnostic, viewport, and output policy. |
| Tagged profile is retained without conversion; untagged ordinary source remains Unknown | Pass for the supported PNG/JPEG source signals. |
| Viewport has no independent preview transform and identifies connected state | Implemented; native visual confirmation pending. |
| Diagnostics insert no hidden pixel conversion | Pass. |
| Phase 3/4 scope remains untouched | Pass. |

Phase 2 is closed for code implementation. No implementation pass is active.
Phase 3 requires a new bounded activation after the visual checklist is
confirmed or any discovered Phase 2 UI defect is fixed.
