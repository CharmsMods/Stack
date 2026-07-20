# Phase 1 Semantic And Node-Definition Contract v1

- Status: accepted Phase 1 contract
- Accepted: 2026-07-16
- Schema generation: `1`
- Owns: NMR-100, NMR-106, NMR-108, descriptor propagation, diagnostic staging,
  and the forward project envelope

This is the single canonical Phase 1 schema. Research files 12, 14, 16, and 17
remain rationale and reference material; where they disagree with this file or
the user-confirmed direction, this file governs the rewrite.

## Phase Boundary

Phase 1 defines, implements, serializes, and tests the contracts that later
phases will use. It does not yet attach descriptors to the live graph, alter
current project saving/loading, insert conversions, change renderer scheduling,
or change pixels. Those integrations begin in Phase 2 after this contract is
proven.

The Phase 1 implementation is therefore a compiled contract library plus a
standalone test target. It is part of Stack's source tree, but no current graph,
renderer, persistence, viewport, export, or UI path calls it yet.

## Accepted Identity Model — NMR-108

### Definition identity

A definition is identified by an exact triple:

```text
definition ID + semantic version + SHA-256 content identity
```

- Definition IDs are lowercase readable namespaced strings:
  `<namespace>:<path>`.
- A built-in example is `stack:image/identity`.
- Namespaces and path segments use lowercase ASCII letters, digits, dots, and
  hyphens. Paths may contain `/` between stable segments.
- Definition labels and browser names are not identities.
- Built-in IDs are deterministic constants. They are never generated from a
  translated label or current menu location.
- Versions are exact `major.minor.patch` triples with nonnegative integers.
  Prerelease/range syntax is intentionally outside v1.
- Content identities use lowercase `sha256:` plus 64 hexadecimal digits.
- Definition content identity is SHA-256 over the canonical, length-delimited
  `stack.node-definition.canonical.v1` representation. It includes the exact
  definition ID/version, technical semantics, ordered ports and parameters,
  policies, field dispositions, diagnostic rules, exact implementation
  references/tolerances/evidence, tests, limits, and lifecycle notes. It
  excludes only the definition hash field itself.
- Each implementation contract has its own SHA-256 over the canonical
  `stack.node-implementation.canonical.v1` representation. Changing an
  implementation target, tolerance, evidence set, or other promised field
  invalidates that implementation identity and, in turn, the containing
  definition identity.
- Canonical hashes are computed and checked by the Phase 1 library. A string
  that merely has the right `sha256:` shape is not accepted as the identity of
  mismatched definition or implementation content.

Semantic-version meaning:

- Patch: implementation, documentation, or performance changes that remain
  within the same promised semantics and tolerance.
- Minor: additive interface changes whose defaults preserve existing instance
  results.
- Major: removed/retyped ports, meaning changes, changed output-affecting
  defaults, changed operation order/domain, or corrected behavior that changes
  promised output.

### Scoped interface identities

Port, parameter, implementation, and diagnostic identifiers are stable
lowercase tokens. They begin with a letter and then use letters, digits, dots,
underscores, or hyphens. Port and parameter IDs are unique within one
definition and are never reused for a different meaning.

Connections save instance UUIDs and port IDs, not labels. Parameter overrides
save parameter IDs, not display text.

### Instance and project identities

Project and node-instance identities are canonical lowercase RFC-4122-style
UUID strings. Copying an instance creates a new instance UUID but retains its
exact definition reference. A saved instance never changes definition version
because a newer library definition exists.

## Accepted Logical Value Family

The v1 contract includes only the value families needed to describe current
major paths and the immediately planned definition system. Declaring a type
does not make it a live graph socket; first-class live values belong to Phase 3.

| Type | Meaning in v1 |
| --- | --- |
| `Boolean` | True/false parameter or uniform value. |
| `Integer` | Count, index, enum-like value, or discrete parameter. |
| `Scalar` | One numeric value or scalar field, distinguished by the port contract. |
| `Vector2/3/4` | Generic typed vectors; color meaning is never inferred from component count. |
| `Matrix3/4` | Linear or affine transform value with declared units/source/destination where relevant. |
| `Curve1D` | Domain, interpolation, extrapolation, and curve data. |
| `Lut` | Versioned 1D/3D table or process resource with declared input/output contract. |
| `ColorImage` | Color-bearing sampled image. |
| `Mask` | Scalar coverage/selection field; noncolor. |
| `DataImage` | Sampled noncolor data such as channel data, IDs, or generic measurements. |
| `ComplexSpectrum` | Complex frequency-domain data; not a color image. |
| `Histogram` | Binned reduction output with population/domain information. |
| `Statistics` | Scalar/vector statistical result with units and population information. |
| `Metadata` | Profile, calibration, camera, external-resource, or other structured context. |
| `Raw` | Sensor-domain source or stage with required calibration metadata. |
| `Analysis` | Analysis sink/result that is not treated as an image. |
| `Failure` | Typed failed result, distinct from black, transparent, empty, or missing data. |

Deferred from v1: dedicated flow, depth, coordinate-field, temporal sequence,
multiview, arbitrary channel-set, and collection types. Current geometry,
multi-image, and temporal paths remain representable through typed image ports,
repeated ports, parameters, metadata handles, and capability declarations. A
future schema generation can add dedicated types without changing existing IDs.

## Knowledge State And No-Guess Rule

Every semantic field is explicitly one of:

- `Known`: a concrete value is present and validated;
- `Unknown`: the field applies but Stack does not know its value; or
- `NotApplicable`: the field does not apply to this logical value.

An empty string or zero is never used as a secret semantic default. `Unknown`
does not equal a known value and does not equal another unknown value for a
strict comparison.

Structural equality used for lossless copying/testing can still confirm that
two descriptor records contain the same explicit states. Strict semantic
matching is a separate operation and always returns false if any compared
applicable field is `Unknown`.

For example:

- a tagged ordinary color image has Known profile identity and Known
  provenance `Embedded` without changing pixels;
- an untagged ordinary color image has Unknown color identity/transfer and
  Known provenance `Untagged`;
- a mask has NotApplicable color, reference, transfer, and alpha fields; and
- a Raw value has NotApplicable color/alpha until a declared development stage
  produces a `ColorImage`.

## Semantic Descriptor v1 — NMR-100

Every value has a logical type. Applicable fields below are present with a
knowledge state even when their value is Unknown.

| Field group | v1 contents | Applicability |
| --- | --- | --- |
| Channels | Stable channel layout and roles: Gray, RGB, RGBA, XY, complex pair, or named data role. | Required Known/Unknown for image-like and Raw values; otherwise NotApplicable unless a vector contract uses it. |
| Color identity | Stable color-space/profile/config identity plus optional profile hash and Standard/Derived relation. | Required Known/Unknown for `ColorImage`; NotApplicable for masks, data, spectra, Raw, and ordinary nonimage values. |
| Transfer | Linear, sRGB, gamma, log, PQ, HLG, custom, or Unknown, with a stable key/parameter where needed. | Required Known/Unknown for `ColorImage`; otherwise NotApplicable. |
| Reference state | Scene, display, output/picture, data, or Unknown. | Required Known/Unknown for `ColorImage`; NotApplicable for noncolor values. |
| Alpha | Absent, opaque, straight, premultiplied, or Unknown. | Required Known/Unknown for `ColorImage`; NotApplicable for Mask/Data/Spectrum/Raw and nonimage values. |
| Numeric range | Nominal bounds when known, negative/above-nominal allowance, and non-finite policy. Nominal bounds never imply automatic clamping. | Required Known/Unknown for numeric and image-like values. |
| Precision requirement | UInt8/UInt16/Float16/Float32/Float64 or Unknown as the minimum logical need. | Required Known/Unknown for numeric and image-like values. Physical allocation remains separate. |
| Spatial state | Unknown, Empty, or finite full/data windows; origin, width/height, and pixel aspect. | Required for image-like and Raw values; NotApplicable for uniform/structured values. |
| Sampling | Coordinate convention, reconstruction filter, and border policy. | Required Known/Unknown for sampled image-like values; otherwise NotApplicable. |
| Units | Unitless, EV, pixels, normalized coordinate, degrees, percent, code value, luminance, or stable custom key. | Required Known/Unknown for scalar/vector/matrix/statistical values; NotApplicable where the definition fully supplies meaning elsewhere. |
| Provenance | Embedded, Untagged, Assigned, Converted, Generated, Derived, RawDeveloped, External, or Unknown plus stable source/operation identity. | Required Known/Unknown for every executable value except `Failure`. |

The descriptor is immutable analysis data. Propagation produces a new
descriptor. It never changes pixel data.

### Descriptor invariants

- `ColorImage` has Known or Unknown color identity, transfer, reference,
  alpha, range, precision, spatial, sampling, and provenance fields.
- Mask, DataImage, ComplexSpectrum, and Raw never receive a color transform by
  component-count inference.
- Known finite spatial state has positive pixel aspect, valid half-open full and
  data windows, and a data window contained by the full window unless the
  operation explicitly declares otherwise in a later schema.
- Known nominal range has finite ordered bounds.
- Known premultiplied or straight alpha is never silently changed.
- Physical texture format, cache ownership, pass schedule, and GPU handles are
  not semantic descriptor fields.

### Explicitly deferred descriptor breadth

Absolute luminance calibration, time/view/eye, chromaticity coordinates,
arbitrary channel lists, ROI/halo, render scale, detailed physical resource
state, and full transform histories are deferred to the phase that needs them.
The v1 `Metadata` and extension mechanism can carry opaque versioned handles in
the meantime. Nothing in v1 invents values for the deferred fields.

## Canonical Node Definition Schema

A definition is separate from an instance and an implementation.

### Definition

A v1 definition contains:

- exact identity triple;
- technical name and semantic operation ID;
- exact formula, named algorithm, or staged semantic description;
- capability class: Pointwise, Sample/Resample, Neighborhood, Reduction,
  Multipass/Iterative, or Specialized/External;
- inspectability classification: Transparent Graph,
  Graph-Defined/Optimized-Equivalent, or Opaque Specialized;
- stable input/output ports;
- declarative parameters;
- descriptor propagation rule;
- numerical, alpha, spatial, and metadata policies;
- diagnostic rule IDs;
- canonical and target implementations with exact identities and tolerances;
- reference/property test IDs, user visual-review policy, and known limits; and
- forward lifecycle notes.

Definitions fail validation when any required policy is Unspecified, a stable
ID is malformed or duplicated, a default is outside its hard domain, a
promised implementation lacks evidence/tolerance, or the meaning is only a
friendly label.

### Port

Each port declares stable ID, direction, logical value type, single/variadic
arity, minimum/maximum connection count, optionality, broadcast policy, units,
and semantic requirement policy:

- `Agnostic`: treats applicable data as generic numbers;
- `Recommended`: mismatch warns but does not block defined numeric work; or
- `Strict`: mismatch is a hard error because the promised operation cannot be
  produced without the required state.

### Parameter

Each parameter declares stable ID, type, unit, default, hard validity domain,
serialization behavior, UI hint/range, and whether it may become a graph input.
UI ranges are suggestions and cannot replace hard validity rules.

### Instance and connection

An instance contains a UUID, an exact definition reference, parameter
overrides, and presentation state. A connection contains source instance/port
and destination instance/port identities. An unresolved instance retains its
reference, overrides, and connections; it is not replaced by a generic layer.

### Implementation

An implementation has a stable scoped ID, an exact owning-definition binding,
kind, exact implementation version and hash, target/capability constraints,
and equivalence tolerance. Schedule or backend changes do not change the
definition unless promised semantics change.

## Forward Project Generation — NMR-106

The rewritten graph begins a new forward-only project schema generation:

```text
format: stack.node-graph
generation: 1
schemaVersion: 1
definitionResolution: exact
missingDefinitionPolicy: unresolved
```

The envelope contains a project UUID and a definition manifest. Every manifest
entry contains the exact definition ID, version, content hash, and source kind:
BuiltIn, Embedded, or External.

Rules:

1. Projects without the generation-1 envelope are pre-rewrite and rejected by
   the rewritten loader with an explicit UnsupportedPreRewrite result.
2. Unknown future or past generations are rejected explicitly; the loader does
   not guess.
3. Exact ID/version/hash is required. Semver ranges and automatic compatible
   upgrades are not used by v1.
4. Built-ins resolve from the shipped exact-definition catalog and are not
   embedded by default.
5. Embedded/external definition packaging is represented by the envelope, but
   compound packaging and portability details remain owned by NMR-110/Phase 5.
6. Missing ID, version mismatch, and hash mismatch are distinct resolution
   results. The instance remains typed unresolved and cannot execute.
7. Definition updates and migrations are explicit user actions and record the
   before/after exact references. No project silently follows the newest
   library definition.
8. Existing MSTK container version 2 and flat graph JSON version 3 remain the
   pre-rewrite format until a later integration pass deliberately installs this
   envelope. Phase 1 does not mutate current files.

## Diagnostic Contract

Diagnostics are non-mutating analysis records with stable rule IDs. Every
record contains stage, severity, authored source identity, affected port/link
where available, semantic fingerprint, message, and suggested explicit repair.

Stages:

1. Connection: fast structural checks and obvious strict mismatch.
2. Semantic: whole-graph propagation, semantic warnings, range/extent/metadata
   analysis.
3. Lowering: implementation capability, precision, planner, and target errors.
4. Runtime: shader/resource/non-finite/allocation/external failures.

Severities:

- Hard Error: structurally impossible or missing execution-critical meaning.
- Warning: numerically executable but suspicious or destructive.
- Information: unusual but fully declared behavior or cost.
- Runtime Fault: failure observed during execution.

Warnings may be acknowledged by rule ID plus semantic fingerprint. Hard errors
and runtime faults cannot be suppressed as warnings. Acknowledgement never
changes the graph or pixels and resets when the relevant identity/fingerprint
changes.

The Phase 1 fingerprint is a SHA-256 identity derived from the rule, authored
source identity, and relevant semantic discriminator. Live graph integration
in Phase 2 will add the exact instance/port/link identities available there.

The compiled v1 rule registry includes structural missing-definition/input/
port/type/cycle rules; semantic color/transfer/alpha/extent/precision/metadata
rules; lowering implementation/capability rules; and runtime shader/resource/
non-finite/allocation rules. Rule IDs are tested for validity and uniqueness.

## Representative Descriptor Propagation

| Operation | v1 propagation |
| --- | --- |
| Ordinary source | Embedded profile information becomes Known descriptive metadata without pixel conversion. Untagged color identity and transfer remain Unknown with Known Untagged provenance. |
| Identity | Preserve every field exactly. |
| Generic arithmetic | Preserve type, channels, spatial, sampling, alpha, units, and numeric policy; mark standard color relation Derived when arbitrary math no longer represents a standard encoded signal. Never invent a color space. |
| Exposure EV | Preserve color identity, alpha, spatial, and sampling; mark range/provenance derived. Known nonlinear or Unknown transfer produces a warning, not a blocked connection. |
| Mask extraction/generation | Produce `Mask`; color/reference/transfer/alpha become NotApplicable; spatial is preserved or generated; range is explicitly declared by the mask formula. |
| Geometry/resample | Preserve color/reference/transfer/alpha; replace spatial and sampling state with the declared output contract. |
| Color transform | Require a `ColorImage` and execution-critical source identity. Produce the explicitly declared destination identity/transfer/reference; never infer a destination. Unknown required source identity is a hard error. |
| Composite | Require structurally compatible image types and a declared extent/alignment policy. Color mismatch warns when numeric composition remains defined. A strict alpha-formula mismatch is a hard error. Output alpha and extent are explicitly declared. |
| Reduction | Produce `Scalar`, `Vector`, `Histogram`, or `Statistics` with units/population provenance; image color/alpha/spatial sampling fields become NotApplicable. |
| Direct output | Preserve the connected graph result exactly. It does not tone-map, normalize, encode, or repair the image. |
| Unknown/external | Must explicitly Preserve, Replace, Invalidate, Consume, Generate, or Drop every applicable field. An undeclared field policy is a definition hard error. |

## Current Major-Path Coverage

The v1 model can describe every current major path without pretending to know
missing meaning:

| Current path | Contract representation |
| --- | --- |
| Ordinary image source | `ColorImage` with Known or Unknown color fields and exact source provenance. |
| RAW source/stages | `Raw` plus `Metadata`; declared development produces `ColorImage`. |
| Generic layer/data math | Typed image/scalar ports, Pointwise/Neighborhood/etc. capability, explicit propagation and numerical policy. |
| Masks and channel streams | `Mask` or `DataImage` with channel role and noncolor state. |
| Geometry | Color/Data image plus explicit Sample/Resample capability and spatial policy. |
| LUT/color transform | `Lut`/`Metadata` resource and strict or recommended color requirements. |
| Mix/HDR/multiple images | Repeated typed image ports with explicit alignment, color, alpha, and range policy. |
| FFT/frequency | `ComplexSpectrum`, with explicit visualization/conversion to `ColorImage` or `DataImage`. |
| Scopes/auto analysis | `Histogram`, `Statistics`, or `Analysis` reduction result. |
| CPU/model/RAW specialized work | Specialized/External implementation with declared typed boundary and runtime diagnostics. |
| Viewport/export/output | Direct output descriptor plus later explicit output encoding contract; no hidden preview transform. |

Unknown is used wherever current evidence cannot justify a stronger field. This
is representability without invented meaning.

## Phase 1 Verification Plan

The standalone Phase 1 contract suite must prove:

- stable ID, UUID, semver, and content-hash grammar;
- descriptor applicability and Known/Unknown/NotApplicable invariants;
- tagged and untagged source behavior without pixel conversion;
- every representative propagation rule above, including warning versus hard
  error behavior;
- complete, unique diagnostic rule registration and acknowledgement policy;
- validation of representative source, arithmetic, exposure, mask, geometry,
  color, composite, reduction, output, frequency, RAW, multi-image, and
  external node definitions;
- exact project-envelope JSON round trip;
- explicit UnsupportedPreRewrite/UnsupportedGeneration/malformed results;
- exact definition resolution plus distinct missing/version/hash failures;
- major-current-path coverage; and
- full Stack build plus Phase 0 and existing graph-behavior regression tests.

User visual review is not applicable because Phase 1 changes no live pixels or
UI. Phase 2 must add user review when descriptors and diagnostics become
visible in the live program.
