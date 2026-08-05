# Current Status

- Updated: 2026-08-01
- Code work active: yes — Phase 7B3 Constant Alpha transaction
- Completed foundation: implementation Phases 0 through 6 and the bounded
  Phase 7A channel-first frequency slice, plus Phase 7B1 partial-Image
  component presence, Phase 7B2 Output Channel inspection, NMR-144 connection
  readouts, and the NMR-145 Channel-role compatibility correction
- Accepted future direction: channel-based node design (NMR-139)
- Implemented from that future direction: exact Channel frequency processing,
  partial-Image/Output foundations, and staged Channel-role compatibility; not
  the complete channel system

## What Is Already Real

Earlier phases added the foundation for exact node definitions, semantic image
information, first-class values, order-preserving pointwise optimization,
real compound nodes, clearer connection presentation, region and tile
planning, one whole-image reduction, real image reformatting, and typed
specialized processing boundaries. Phase 7A additionally provides the complete
Channel-first frequency family, exact Spectrum/Response/Magnitude/Phase types,
typed FFT resources, analysis, editor controls, and graph actions.

The detailed proof for those phases is under
[Completed Work](../06-completed-work/README.md).

## Active Implementation

NMR-142 accepts the combined C1-C3 contract using the documented recommended
defaults. Phase 7B1 is complete: semantic descriptor schema 3, explicit
R/G/B/A presence, deterministic migration, propagation helpers, exact Image
Combine description, fingerprints, and focused CPU/graph persistence evidence
are implemented without changing renderer pixels.

Phase 7B2 is complete: Output definition v2 has one `imageIn` accepting Image
or Channel, saved Channel inspection mode, exact viewport-only presentation,
Channel export rejection, graph schema 8 migration, UI, and CPU/live evidence.

Phase 7B3 is the active bounded pass: visible Constant Channel, exact Match
Extent validation, the atomic automatic opaque-Alpha graph transaction,
suppression/restore/rematch behavior, undo/redo, persistence, lazy execution,
and focused/live evidence.

The first Phase 7B3 checkpoint is complete. Constant Channel v1 is now a
visible, editable node with finite `value`, required advanced `matchExtent`,
branchable `channelOut`, schema-8 persistence, and lazy exact-extent GPU
materialization. Image Combine is definition v2 and persists its automatic
Alpha suppression state. Missing Match Extent fails explicitly instead of
inventing a canvas. The automatic create/delete/restore transaction and its
undo/redo history are the next active slice.

## What Is Designed But Not Yet Built

The accepted channel-based direction defines Values, Channels, Images, Data,
and Specialized values; partial Images; standalone Channel inspection;
visible constant Alpha; Channel roles; controlled Value/Channel broadcast;
per-node component participation; source dissolution; honest compound
inspection; and one unified node-library model.

Those are product decisions. C1 and C2 are implemented, and C3 is active under
their accepted exact contract. C4-C8 listed in
[Work Still To Define](../02-channel-based-design/work-still-to-define.md)
remain unimplemented.

## Completed Implementation

Phase 7A implemented the user-approved channel-first frequency-node family.
Its dependency boundary is intentionally narrow: it introduces the exact
public Channel socket/value distinction needed by Channel Split/Combine and
frequency processing, plus typed Spectrum, Frequency Response, Magnitude, and
Phase resources. It does not activate the remaining C1–C8 image/component,
source-dissolution, broadcast, or general node-library work.

The implemented contract is
[Channel-First Frequency Contract v1](../03-technical-contracts/regions-reductions-and-specialized-processing/channel-first-frequency-contract-v1.md).
The reproducible handoff is
[Phase 7A Completion Evidence](../06-completed-work/phase-07-channel-first-frequency/channel-first-frequency-completion-record-2026-07-26.md).

## Completed Preview Correction

Phase 7A-C1 corrected the expanded Frequency Filter preview discovered during
the first manual low-pass test. Preview-only graph nodes were assigned negative
IDs while render submission rejected non-positive output IDs, so the
preview returned no pixels before executing. Region planning and graph
execution now accept an internal transient ID when that exact node exists in
the render snapshot. A live regression proves the negative-ID FFT to Spectrum
View branch emits preview pixels. Frequency math and ordinary graph Output
behavior are unchanged.

## Completed Viewport Traversal Correction

Phase 7A-C2 corrected ordinary viewport traversal for the new exact frequency
nodes. The first manual low-pass graph is visibly connected to Output, but the
completed-chain walker still followed the removed Image-based frequency socket
contract and had no Frequency Filter case. It therefore discarded the chain
before render submission. The walker now follows exact Channel, Spectrum,
Response, Magnitude, and Phase dependencies. Both the user's Image → Split →
Filter → Combine → Output graph and the full advanced chain qualify as
completed viewport outputs.

## Completed Channel-Role Compatibility Correction

NMR-145 corrected the pictured `Channel Split R -> Contrast Mask` rejection.
The staged `Mask` and `ScalarField` socket spellings now accept the real
one-value-per-pixel `Channel` payload whenever the destination is registered as
a single-channel field target. One shared predicate owns this rule for drag
authoring, graph validation, and renderer scheduling; Specialized frequency
payloads remain exact-match. Ordinary invalid Channel links no longer produce
the misleading frequency-family message.

The exact Image/Split/Contrast/Output topology passes authoring, validation,
save/reload, renderer-link eligibility, and a live GPU comparison showing that
the red Channel controls the per-pixel Contrast blend. All Node Math/graph
CTest gates and the preferred Windows build pass.

## Completed Partial-Image Foundation

Phase 7B1 makes Image component presence an exact semantic property independent
of fixed RGBA storage. Descriptor schema 3 stores canonical R/G/B/A presence,
migrates schema 1/2 deterministically, fingerprints every subset distinctly,
and rejects component/layout/alpha contradictions. Channel Split reports an
exact missing-component failure, Image Combine describes precisely its
connected inputs, and mismatched or unknown Channel extents fail rather than
imply hidden stretching. Graph save/load preserves partial topology and its
descriptor identity. The renderer's current fixed RGBA pixel behavior was not
changed in this pass.

The reproducible handoff is
[Phase 7B1 Completion Evidence](../06-completed-work/phase-07-partial-image/partial-image-foundation-completion-record-2026-07-28.md).

## Completed Output Inspection

Phase 7B2 replaces Output's hidden construction pins with one stable
`Result · Image or Channel` input. A Channel remains a Channel in semantic and
wire state while Neutral, Red, Green, or Blue changes only its opaque viewport
presentation. PNG export fails before capture and directs the user to Image
Combine. Output definition v2 and graph schema 8 persist the saved mode;
schema-7 single-component Output links migrate to Result while ambiguous
multi-component construction is preserved unresolved.

The reproducible handoff is
[Phase 7B2 Completion Evidence](../06-completed-work/phase-07-partial-image/output-inspection-completion-record-2026-07-28.md).

## Ready But Not Started

- The negative-denominator Divide bug has a complete implementation packet.
- A presentation-only change from the internal phrase `ScalarField` to the
  user-facing word **Channel** is narrow enough to specify, but it does not yet
  have a standalone implementation packet equal to the Divide fix.

Neither item is active. See [Implementation](../04-implementation/README.md).

## Current Contract

The accepted next vertical contract combines partial Images, Output/Channel
inspection, and visible constant Alpha creation:
[Partial Image, Output Inspection, And Constant Alpha Contract v1](../03-technical-contracts/channel-system/partial-image-output-and-constant-alpha-contract-v1.md).

All unrelated Phase 7 and C4-C8 work remains inactive.
