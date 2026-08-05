# Alpha Channel And Math Defaults

- Started: 2026-07-17
- Consolidated: 2026-07-20
- Authority: `../accepted-product-direction.md`
- Verification: current Data Math, Technical Image, Channel Combine, and PNG boundaries inspected on 2026-07-20

This file owns Alpha's Channel role, opaque-alpha construction, component
participation, and alpha/compositing defaults.

## Alpha Is A Channel

Alpha has the same per-pixel mathematical shape and editing flexibility as R,
G, and B. Its role supplies coverage/compositing meaning; it does not create a
different primary container.

An Alpha Channel may be split, branched, viewed, processed by compatible
Channel operations, reassigned, replaced, or connected elsewhere. Straight and
premultiplied association remain separate explicit Image state.

## Opaque Alpha At Image Combine

When Image Combine first becomes used downstream, at least one color component
is present, and A is unconnected, Stack creates a visible **Constant Channel**:

- Value `1.0`;
- role `Alpha`;
- advanced `Match Extent` input referencing one present color Channel;
- ordinary Channel output that may be branched or processed; and
- lazy constant execution unless materialization is required.

The creation is one undoable graph mutation. Deleting the generated node is a
deliberate override and does not immediately trigger recreation. A visible
`Create Opaque Alpha` action restores it.

All present Image Combine Channels must have compatible extents. Stack reports
a mismatch and suggests explicit Reformat; it does not resize or choose a
different component silently. If no color extent is available, the Constant
Channel remains unresolved rather than inventing a canvas size.

## Alpha Participation In Math

Friendly image adjustments default to RGB and preserve Alpha. This prevents a
brightness or contrast edit from unexpectedly changing opacity.

Applicable nodes store their own participation policy:

- RGB;
- RGBA;
- Alpha; or
- an explicit supported component selection.

A graph setting chooses the creation default for new applicable nodes. Changing
that preference never changes existing instances. Generic component math must
declare its policy; friendly compounds may expose a simpler `Affect Alpha`
control backed by the same stored state.

When Alpha is split into a standalone Channel, ordinary Channel math edits it
normally. No global setting exempts it.

## Mask Interaction

A Mask blends between original and processed results for the components chosen
by the receiving node. If Alpha is preserved by that node, the Mask does not
quietly blend Alpha. A compositing or coverage node may declare a different
exact formula.

Mask values conventionally represent 0–1 strength. Graph authoring remains
permissive; the receiving node declares clamp, extended-range, and non-finite
behavior.

## Compositing Boundaries

Each node definition must state whether it:

- preserves Alpha;
- processes selected components independently;
- operates on Alpha only;
- requires straight or premultiplied association;
- performs a named coverage/compositing formula; or
- creates, removes, or replaces Alpha.

Role assignment alone never premultiplies, unpremultiplies, clamps, or rewrites
pixels.

## Current Code Gap

Current Technical Image Exposure preserves Alpha, explicit Premultiply and
Unpremultiply exist, and straight/premultiplied Source Over are separate. Data
Math currently works componentwise over fixed RGBA. Channel Combine still uses
an invisible shader fallback of `A = 1.0`; Constant Channel, Match Extent,
creation/deletion persistence, and stored general participation do not yet
implement this design.

Exact remaining work is C3 and C6 in `../work-still-to-define.md`.

## Related Topics

- `channels-values-images-and-output.md`
- `connections-errors-and-visual-feedback.md`
- `execution-and-performance.md`
- `../../03-technical-contracts/node-and-data/node-data-and-definition-contract-v1.md`
