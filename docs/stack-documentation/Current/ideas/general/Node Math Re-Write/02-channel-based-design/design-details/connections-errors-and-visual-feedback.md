# Graph Rules And Visual Feedback

- Updated: 2026-08-01
- Authority: `../accepted-product-direction.md`; NMR-144; NMR-145
- Scope: connection outcomes, diagnostics, pins, and source-oriented wire readouts

This guide defines how a graph connection explains what flows through it and
the Channel-role compatibility invariant used by the current staged socket
model. It does not evaluate a graph or author graph state.

## Reliable Node Contract

Every published node definition declares its accepted and produced Value,
Channel, Image, Data, or Specialized families; component relationships; units,
range, color/alpha, spatial and sampling requirements; output descriptor
propagation; execution boundaries; and failure behavior. Connection presentation
derives from those facts rather than maintaining a second label model.

## Connection Outcomes

1. **Valid:** structural and semantic requirements are satisfied.
2. **Valid with information or warning:** execution remains possible, but a
   declared meaning or intended use needs attention.
3. **Connected but not executable:** execution-critical data is missing or
   failed. The authored graph remains visible and publishes a typed failure
   until repaired.

Hard drag rejection is reserved for a shape that cannot make the declared
connection, such as a full Image to a one-Channel input without an explicit
extractor. Stack never inserts a conversion or extractor behind the user's
back.

### Channel-role compatibility invariant

`Channel` is the one-value-per-pixel payload shape. `Mask` and `ScalarField`
remain staged socket spellings for declared single-channel roles, so a Channel
output connects directly to those inputs when the target is registered as a
single-channel field target. Receiving context does not retag the source
Channel; it determines how that Channel participates at the target.

One shared connection predicate owns this bridge for drag authoring, graph
validation, and renderer-link eligibility. Those paths must not carry separate
Channel-to-Mask exceptions. Ordinary Channel compatibility is evaluated
outside the Specialized frequency branch. Spectrum, Frequency Response,
Spectrum Magnitude, and Spectrum Phase remain Specialized values and still
require exact types. An invalid ordinary Channel connection receives Channel
guidance rather than a frequency-family error.

## Wire Readout Contract

Holding `F` while the graph can receive editor command keys reveals the two
rotated wire lines. It is the only reveal gesture. Fade behavior, text size,
sizing mode, outline, floating/break-line layout, collision handling, and
delayed detail cards remain presentation settings.

The readout is a non-persisted `WireReadoutInput`, resolved for each link:

- the source socket and its resolved descriptor when available;
- an optional already-computed uniform output snapshot; and
- only diagnostics attributable to that exact source output.

It passes through a pure typed-template formatter that produces primary text,
secondary text, severity, accessible text, and facts for the delayed card.
The input and result are excluded from semantic fingerprints, project files,
and appearance/project serialization. A missing computed snapshot never starts
evaluation.

The upper line is always a stable description of the source-carried family.
The lower line is one deterministic type-specific summary. It never rotates
between facts and never contains provenance, user-authored text, or destination
compatibility.

| Carried family | Upper line | Healthy lower line |
| --- | --- | --- |
| Image | `Image · R, G, B[, A]` | color identity · reference/transfer · alpha |
| Channel / field | `Channel · Alpha`, `Channel · Mask`, or `Field · Vector 3` | units · declared range; extent only when numeric context is unavailable |
| Uniform Value | `Value · Scalar`, `Value · Vector 3`, and so on | compact finite payload · units |
| Data | `Histogram`, `Lookup table`, `Statistics`, and so on | deterministic type-specific summary such as bins/domain or measure count |
| Specialized | `Spectrum · Complex`, `RAW`, and so on | domain-specific structural state |
| Failure / unknown | truthful family fallback | concise source failure or explicit unknown state |

Only an unambiguous role appears in a Channel readout. Mask is always
`Channel · Mask`; receiving context never retags the upstream source.
Matrices, overlong values, and non-finite payloads fall back to type/unit
wording rather than displaying misleading partial data.

Source hard errors and actionable source warnings replace the healthy lower
line and receive error/warning text styling. Unknown and not-applicable states
remain explicit without alert styling. Destination compatibility remains in
pin/drop feedback and in detail inspection; it never becomes a visible
source-wire alert.

## Delayed Detail Card

The card repeats the carried family/current readout and keeps the information
that would make the wire noisy: provenance, full extent, full range,
precision, sampling, and non-compact payloads. It may also show destination
inspection diagnostics because it is an inspection surface, not the source
readout.

## Validation Requirements

The formatter has direct coverage for known, unknown, and failed families;
finite payload formatting/truncation; severity precedence; source-only
diagnostic filtering; accessibility text; `F` visibility; collision and
break-line geometry; and save/reload proof that no wire label or presentation
state persists.

Connection compatibility also has an authored regression for
`Image -> Channel Split R -> Contrast Mask -> Output`. It must pass drag
acceptance, graph validation, renderer scheduling, save/reload, and live pixel
comparison proving that the red Channel controls the per-pixel layer blend.

## Historical Record

The completed Phase 5B/5B-C connection UI contract remains historical evidence
for the existing pin geometry, delayed-card, and appearance-preference work.
NMR-144 supersedes its fixed two-line wire-label wording only; it does not
reopen Phase 5 completion or change its historical technical record.

## Related Authority

- `../accepted-product-direction.md`
- `../../01-start-here/decision-log.md`
- `../../03-technical-contracts/graph-connections/compound-output-and-connection-ui-contract.md`
- `channels-values-images-and-output.md`
- `execution-and-performance.md`
