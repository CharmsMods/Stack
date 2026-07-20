# Alpha Channel And Math Defaults

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-18-1801-output-scenarios-and-explicit-alpha.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-19-1332-channel-output-export-and-alpha-parity.md`
- Type: idea
- Topic: alpha-policy
- Verification: partially-verified against current Data Math and Technical Image behavior

## User Direction

Ordinary full-image edits should not unexpectedly alter opacity. Brightness,
contrast, or similar edits should normally affect RGB and preserve alpha.

Advanced users should still be able to run the same arithmetic over alpha when
they explicitly want RGBA behavior. The user proposed a graph-settings toggle,
off by default, that controls whether full-image math includes alpha.

### Image Combine Boundary — 2026-07-17

The desired Image Combine presentation has four ordered Channel inputs: R, G,
B, and A. R/G/B form the visible color result; A is visible but optional. When
A is unconnected, Image Combine supplies an opaque constant alpha of 1.0.

Alpha may vary per pixel when connected. In current 8-bit PNG output that
normalized Channel is quantized to values from 0 through 255; PNG alpha is not
limited to only fully transparent or fully opaque values.

### Missing Alpha Direction Reopened — 2026-07-18

The earlier hidden `A = 1.0` default is no longer a complete product direction.
When Image Combine becomes usable without a manually connected alpha input,
the user wants Stack to create and visibly connect an opaque Alpha Channel. It
must be possible to branch that Channel, insert ordinary compatible nodes
before Combine, or replace its source.

The leading representation to investigate is a **Constant Channel** node whose
visible Value is `1.0`, whose output role is Alpha, and whose extent is matched
to the resolved color inputs. It is a logical Channel at the graph boundary;
the execution backend may retain it as a constant-field broadcast and avoid a
physical full-resolution texture until an operation genuinely requires one.

Still unresolved:

- which connected component or extent resolver determines its dimensions;
- what happens when connected color Channels have different extents; and
- whether automatic creation occurs on the first color connection, when the
  Combine output is connected, or at first downstream evaluation.

This is a recorded design direction, not implementation authorization.

### Alpha Is An Ordinary Editable Channel — Confirmed 2026-07-19

Alpha belongs to the same primary **Channel** family as R, G, and B. `Alpha` is
its role in an Image or compositing relationship, not a separate mathematical
container that loses normal Channel flexibility.

Consequently, an Alpha Channel can be branched, previewed, passed through
compatible Channel operations, replaced, or used elsewhere in the graph. Its
role may affect labels, diagnostics, expected range, and compositing rules, but
does not make it structurally less editable than another Channel. The proposed
opaque Constant Channel must preserve this same behavior.

## Why The Default Is Sensible

For an operation presented as an image appearance adjustment, preserving alpha
is usually the least surprising behavior. Independent alpha access remains
available through Channel Split and Combine. Technical Image Exposure already
changes RGB while preserving alpha, whereas generic Data Math currently works
componentwise over RGBA.

The node contract must distinguish:

- RGB operation with alpha preserved;
- independent operation on all present channels;
- alpha-only operation;
- coverage/compositing operation with a specific straight or premultiplied
  formula; and
- operation that generates or removes alpha.

## Where The Override Should Live

A global runtime switch is convenient, but it can silently change the promised
output of every existing math node and make project results depend on a setting
far away from the node. Alternatives should be compared:

1. **Per-node Channels control:** RGB, RGBA, Alpha, or explicit custom channel
   selection. Most honest and local, but adds controls to many nodes.
2. **Graph preference as creation default:** new applicable nodes default to
   RGB or RGBA, but each node stores its own choice. The setting does not
   reinterpret existing nodes.
3. **Separate node variants:** `Add RGB` and `Add All Channels`. Clear but
   expands the browser and formula catalog.
4. **Explicit channel wiring:** split alpha only when editing it. Clear and
   flexible, but can make simple graphs verbose.
5. **Compound-level policy:** a convenience compound exposes an `Affect Alpha`
   option and routes channels explicitly inside.

A promising hybrid is a graph preference that chooses the default for newly
created nodes, combined with a visible per-node stored channel-participation
policy. That preserves the user's desired default without creating hidden
project-wide reinterpretation.

## Questions For The Next Conversation

- Does “full-image math” mean RGB by default for every arithmetic node, or only
  for nodes presented as photographic/image adjustments?
- Should generic vector math remain all-components by definition while friendly
  image compounds preserve alpha?
- How does the UI expose channel selection without cluttering every node?
- Does a mask input blend alpha as well as RGB, or only the channels selected by
  the node's participation policy?
- What happens to missing alpha: treat it as opaque, absent, or create it when
  a user selects alpha?
- How should premultiplied RGB be handled before a color filter that preserves
  alpha but changes RGB?
- Should channel-participation changes create a new definition version, an
  instance parameter, or select a separate definition?

## Related Docs

- `2026-07-17-channel-first-data-model-and-language.md`
- `2026-07-17-graph-rules-and-visual-feedback.md`
- `2026-07-17-compound-nodes-and-decomposition.md`
- `../../decision-register.md` — NMR-019, NMR-104, NMR-127
