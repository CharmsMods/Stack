# Graph Rules And Visual Feedback

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1632-channel-ui-confirmations-and-uniform-reuse.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1649-channel-role-semantics.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-18-1801-output-scenarios-and-explicit-alpha.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-19-1332-channel-output-export-and-alpha-parity.md`
- Type: idea
- Topic: graph-rules
- Verification: partially-verified against current connection and presentation contracts

## User Direction

Stack needs graph rules that are reliable under the surface and visually
understandable. A user should be able to tell why a channel can connect to one
node, why another node needs related RGB channels, and when a connection is
legal but potentially surprising.

Friendly language must not lie about the operation.

## Minimum Reliable Node Contract

Each production node should be able to answer, where applicable:

- What value shapes can enter and leave?
- Which channel roles are required, optional, preserved, created, or changed?
- Does it operate on each channel independently or depend on relationships
  among channels?
- Does it use only the current pixel, nearby pixels, a complete image, several
  images, or specialized data?
- Does it require or merely prefer a known transfer/color/reference state?
- Does it preserve alpha, operate on alpha, require straight/premultiplied
  alpha, or produce new coverage?
- What are its units, range, zero, negative, non-finite, and precision rules?
- Does it preserve extent, require matching extents, or resample?
- What does a mask mean for this node, and at what stage is it applied?
- Can it be represented by a canonical subgraph, and can an optimized backend
  prove equivalence?

The current Node Math Rewrite definition schema already contains much of this
structure. The remaining problem is applying it consistently to the entire
public library and presenting the most useful subset without overwhelming the
canvas.

## Connection Outcome Levels

A candidate visual grammar should distinguish at least:

1. **Compatible** — the connection satisfies structural and declared semantic
   requirements.
2. **Compatible with information** — the math is valid, but a state is Unknown
   or the result may be unconventional.
3. **Compatible with warning** — executable, but the node depends on a
   relationship the input does not clearly provide, or the operation may cause
   channel misregistration, alpha surprise, range problems, or color-domain
   problems.
4. **Requires an explicit operation** — extraction, combine, broadcast,
   reduction, conversion, resampling, or another meaningful step is missing.
5. **Runtime/specialized failure** — structurally valid but required pixels,
   external providers, model data, metadata, or execution support are missing.

Hard blocking should remain reserved for a real structural or execution
impossibility. A warning should never secretly alter the graph.

### Mask Range Direction — 2026-07-17

Mask is a Channel role whose conventional range is 0 to 1. That expectation is
advisory at the graph-value level: Stack may warn about values below 0, above 1,
or non-finite values, but it should allow the user to create, connect, inspect,
and display unusual numeric Channels.

The receiving node still owns its exact formula. If a particular mask input
clamps to 0 through 1, preserves extended values, rejects non-finite values, or
uses another mapping, that behavior must be declared by the node rather than
applied as one hidden global mask repair.

### Accepted Contextual Role Presentation — 2026-07-17

The broad pin and wire family remains **Channel**. A contextual text/shape badge
shows how the receiving boundary uses it:

- a reusable standalone stream is `Channel` and previews as Neutral grayscale;
- a connection entering a mask input is `Channel · Mask` in detailed UI;
- a connection entering Image Combine's G socket receives a visible `G` role
  within the resulting Image; and
- the receiving role does not silently retag the upstream Channel everywhere
  else it is used.

Amount and Mask inputs may therefore share the Channel pin family while keeping
different labels and formulas. `Amount · Value/Channel` changes an operation's
parameter; `Mask · Channel` controls the blend between the original and the
processed result.

### Role Label And Propagation Direction — 2026-07-17

The normal node label is **Mask**; detailed inspection may say
**Channel · Mask**. This keeps the canvas readable while revealing the precise
family and role when requested.

Channel roles are semantic graph data that downstream nodes and UI can receive.
They are not merely viewport decorations. Definitions should preserve an
unambiguous role by default, set a new role when the node has a declared result,
and return Neutral with non-blocking information when mixed or arbitrary math
makes the meaning genuinely ambiguous.

Manual role assignment changes the downstream descriptor and presentation but
does not itself alter the numeric Channel values. Any clamp, normalization,
unit conversion, or remap remains an explicit node operation. A visible,
branch-local assignment boundary is preferred over an invisible global
property because the same Channel may be reused with different meanings.

### Present Channels Versus Numeric Fallback — 2026-07-18

Image Combine needs to distinguish a Channel that is **absent** from a Channel
that is **present and filled with zero**. A fixed RGBA GPU representation may
return zero when an absent color component is sampled, but its descriptor must
still report the actual present-component set.

This distinction affects both graph rules and presentation:

- a bundle created from R and B reports R and B as present and G as absent;
- its wire and detailed socket information expose that component set;
- operations declared component-independent may operate on the present
  Channels; and
- operations declared to require an RGB relationship can diagnose the missing
  G component before execution, even though a low-level RGBA sample would read
  zero in that position.

This requires a richer bundle compatibility rule than the current optional
Combine sockets and fixed Image output express. The user-facing name and exact
descriptor representation for a partial component bundle remain open.

The absent-versus-present-zero distinction was confirmed on 2026-07-19. Alpha
was also confirmed as the same broad Channel family as R, G, and B; its Alpha
role provides downstream meaning and compositing rules without removing normal
Channel editing or branching behavior.

## Visual Ideas To Compare

- Pin shape or inner mark for Value, Channel, Image bundle, Resource, and
  Specialized data.
- Wire color by broad data family, with role labels such as R, Alpha, Mask, EV,
  or Complex.
- Small node badges for per-channel, RGB-related, neighborhood, full-frame,
  multi-image, and specialized behavior.
- Hover cards that explain the exact requirement and suggest explicit repair
  nodes.
- A visible channel participation control on applicable math nodes.
- Expandable Image bundle pins for R/G/B/A without requiring permanent graph
  expansion.
- Connection previews that show “allowed,” “allowed with warning,” or “needs
  explicit extraction/conversion” before the user releases the wire.

These cues must be accessible through text and shape as well as color.

## Questions For The Next Conversation

- Which node facts deserve permanent badges, and which belong only in hover or
  inspection UI?
- Should a node that mathematically works on a replicated single channel but is
  designed for RGB accept the connection with a warning or require an explicit
  conversion to an image bundle?
- How should Stack distinguish “requires three channels” from “accepts any
  number of channels and processes independently”?
- Should a spatial warp of one channel warn at the warp node, at Channel
  Combine, or both?
- Should advanced users be able to acknowledge or suppress a stable warning?
- Can the same definition metadata generate browser tags, pin compatibility,
  wire labels, documentation, planner capability, and tests without making one
  field carry several unrelated meanings?

## Related Docs

- `2026-07-17-channel-first-data-model-and-language.md`
- `2026-07-17-operation-scope-and-performance.md`
- `2026-07-17-alpha-channel-and-math-defaults.md`
- `2026-07-17-compound-nodes-and-decomposition.md`
- `../../program-contract.md`
- `../../phase-5b-typed-connection-ui-contract.md`
