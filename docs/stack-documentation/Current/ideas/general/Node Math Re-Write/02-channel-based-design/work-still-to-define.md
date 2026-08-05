# Channel-System Work Still To Define

- Updated: 2026-07-28
- Status: product direction is accepted; items below are implementation-contract or research work
- Authority: `accepted-product-direction.md`

This is the master index for unresolved channel-system work and recently
completed research whose result still feeds future contracts. Topic files and
the plain-language question sheet may discuss individual items, but each item
should retain its C/R identifier here so status does not drift.

The recommendations below are Codex's best-fit starting choices based on the
accepted direction. C1-C3 were accepted through NMR-142 on 2026-07-28 and are
owned by the combined vertical contract. The remaining recommendations are not
adopted implementation contracts until the user reviews them and the owning
contract records exact behavior and tests.

## Contract Queue

### C1. Partial Image Descriptor And Persistence

**Status:** implemented by Phase 7B1 on 2026-07-28.

Define the exact serialized present-component representation, compatibility
queries, fingerprints, and diagnostic identities for partial Images. Prove
that absent and present-zero components survive save/load and do not collapse
because the GPU uses fixed RGBA storage.

**Recommended starting choice:** Persist a readable named component-presence
set (`R`, `G`, `B`, `A`) and normalize it to a compact bitset internally. Keep
it separate from physical texture layout, include it in semantic fingerprints,
and let nodes query whether their required component subset is present.

Owner: data model and graph rules. Gate: Output/Image Combine vertical slice.

### C2. Output And Inspection State

**Status:** implemented by Phase 7B2 on 2026-07-28.

Specify the exact Output sockets or inspection boundary, saved normal
inspection state, temporary override state, export-disabled UI, project
persistence, and direct-viewport interaction. Inspection and component
construction must remain visually unmistakable.

**Recommended starting choice:** Give Output one primary input that explicitly
accepts `Image/Channel`. An Image is the exportable authored result. A Channel
reveals a saved `View As` control defaulting to Neutral, while a separately
marked temporary viewport override remains unsaved. Ordinary RGB/RGBA export
is disabled for a Channel; a partial Image may display/export absent color
components numerically as zero without gaining semantic presence in Stack.

Owner: data model. Gate: Output/Image Combine vertical slice.

### C3. Constant Alpha Transaction

**Status:** accepted under NMR-142; Phase 7B3 active.

Specify one undoable creation transaction, `Match Extent` wiring,
source-choice rule, node placement, serialization, deletion override, restore
action, and behavior when no color extent can resolve. Prove that mismatched
present inputs fail without hidden Reformat.

**Recommended starting choice:** Treat the first downstream connection from a
used Image Combine as one undoable transaction that may add Constant Channel,
its Alpha link, and an advanced Match Extent link. Select the extent source in
stable R, then G, then B order. If that reference later disappears, report the
unresolved extent and offer `Rematch Extent`; do not silently rewire. Deleting
the generated source suppresses immediate recreation until `Create Opaque
Alpha` is chosen.

Owner: alpha and graph rules. Gate: Output/Image Combine vertical slice.

### C4. Durable Channel Role Schema

Define separate fields for semantic purpose, Image component assignment,
units/range, spatial state, and preview state. Specify unary, matching-role,
mixed-role, role-defining, and manual-assignment propagation. Select the first
small role set and its persistence/tests.

**Recommended starting choice:** Keep semantic purpose, Image component,
units/range, spatial state, and preview state as separate fields. Ship Neutral,
Mask, Alpha, Luminance, and EV first; defer Depth until its domain contract is
real. Preserve a clear role through role-agnostic unary work, return Neutral
for ambiguous mixed math, and use visible Set Channel Role for downstream
reassignment.

Owner: data model and graph rules. Gate: durable role vertical slice.

### C5. First `Value/Channel` Ports

Choose the first pointwise parameters, define overload presentation and
serialization, resolve spatial extent, and name compiler/runtime paths.
Neighborhood-varying parameters remain excluded until their support contract
is defined.

**Recommended starting choice:** Prove the rule with Channel Add Amount,
Channel Multiply Amount, and Exposure EV. A Value broadcasts without acquiring
an extent or texture. A Channel parameter must match the main spatial extent or
use explicit Reformat. Defer blur radius, kernel size, and other
neighborhood-varying parameters.

Owner: data model and operation scope. Gate: first live overload slice.

### C6. Stored Channel Participation

Specify the per-node RGB/RGBA/custom representation, graph creation preference,
mask interaction, serialization, UI density, and exact behavior for generic
math versus friendly compounds.

**Recommended starting choice:** Store `RGB`, `RGBA`, `Alpha`, or supported
custom components on each applicable node. The graph preference supplies only
the creation default; friendly image adjustments begin as RGB and preserve
Alpha. Show a compact RGB/RGBA badge or inspector control only on relevant
nodes. Standalone Channel operations do not need this setting.

Owner: alpha. Gate: alpha-participation slice.

### C7. Dissolve Into Channels Mutation

Specify placement, naming, fan-out preservation, source-without-alpha
behavior, compound-boundary behavior, undo/redo, persistence, and failure
rollback.

**Recommended starting choice:** Start with top-level imported Source nodes.
One undoable action inserts Split and Combine, keeps the Source, routes all
former Image consumers through Combine, and preserves fan-out. Connect A only
when the source semantically contains Alpha; otherwise let the normal visible
Constant Alpha rule supply opacity when the recombined Image is used. Defer
crossing compound boundaries until this mutation is proven.

Owner: data model and compounds. Gate: source-dissolve slice.

### C8. Compound Interaction Contract

Define Inspect, Edit/Make Unique, and Dissolve/Unpack UI and exact behavior;
optional intermediate outputs; parameter mapping; graph-explosion safeguards;
and the first representative case studies.

**Recommended starting choice:** Open Inspect as a read-only nested graph view;
Make Unique creates and selects a local editable definition; Dissolve replaces
the instance in one undoable transaction. Warn, but do not forbid, when a
canonical graph is large. Start with a pointwise adjustment, Saturation,
Gaussian Blur, a masked adjustment, output finishing, and one deliberately
specialized operation.

Owner: compounds. Gate: high-level node decomposition work.

## Research Queue

### R1. Color And Monitor Presentation

Produce the plain-language glossary, professional SDR pipeline, View Transform
replacement, source/output compound policy, and monitor-profile boundary.
Decide whether and how to revise NMR-105 without changing exported graph pixels.

**Recommended direction:** Research a clearly indicated, bypassable,
non-exporting monitor-profile presentation stage outside authored creative
math. Adopt it only through an explicit NMR-105 revision. Keep source
preparation, tone/gamut decisions, encoding, and exported pixels visible in the
graph or its inspectable compounds.

### R2. Remaining Industry Language — Complete

Validate `Channel Set`, component/purpose/unit terminology, and detailed pin and
wire wording. The primary Value, Channel, Image, Data, and Specialized
vocabulary is no longer open.

**Research status:** completed 2026-07-21 and adopted by user confirmation on
2026-07-22. See
`../05-research/channel-system-findings/2026-07-21-naming-and-industry-terms.md`.
The evidence supports the accepted primary names, Channel Set, Constant
Channel, separate descriptor concerns, and purpose-specific Coordinate aliases.
Normal UI and accessible text say `Value or Channel`; compact technical
shorthand may remain `Value/Channel`.

**Adopted direction:** Keep the accepted primary names and researched detailed
terminology. Spell out `Value or Channel` for normal users and accessibility.

### R3. Compound Comparisons

Compare mature node systems only for unresolved interaction, shared-definition,
port-stability, optimized-equivalence, and opaque-operation behavior.

**Recommended direction:** Compare official interaction and persistence
contracts, not surface appearance. Stack's Inspect/Make Unique/Dissolve and
honesty classes remain the evaluation criteria.

### R4. Phase 7 Selection

Compare representative pointwise, RGB-related, neighborhood, geometry, and
specialized candidates after C1–C6 are stable. Final implementation selection
waits for the later dependency work below.

**Recommended direction:** Use Multiply, Saturation, Gaussian Blur, Reformat,
and RAW Development as the first representative set unless contract work
reveals a smaller or more diagnostic choice.

## Dependency Order

```text
C1 + C2 + C3
    -> Output/Image Combine contract
    -> C4 durable roles
    -> C5 + C6 pointwise overload/participation
    -> R4 candidate comparison may begin
    -> C7 source dissolve
    -> C8 compounds
    -> R1 color authority decision
    -> final Phase 7 implementation selection
```

R2 is complete. R3 may run whenever research is requested. Research may
produce evidence for a recorded revision; it may not silently change accepted
product direction. No item authorizes implementation by itself.
