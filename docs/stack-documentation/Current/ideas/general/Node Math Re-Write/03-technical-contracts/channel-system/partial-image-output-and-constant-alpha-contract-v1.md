# Partial Image, Output Inspection, And Constant Alpha Contract v1

- Status: accepted for implementation
- Accepted: 2026-07-28
- Decision: NMR-142
- Owns: C1, C2, and C3
- Initial implementation pass: Phase 7B1
- Current implementation state: Phase 7B1 and Phase 7B2 complete; Phase 7B3 active

This contract turns the accepted partial-Image, standalone-Channel inspection,
and visible opaque-Alpha direction into one vertical implementation boundary.
It does not introduce durable Channel roles, general Value-or-Channel ports,
component-participation controls, source dissolution, or broader Phase 7
library work.

## Product Result

Stack distinguishes these authored results:

```text
Image · R, B        G is absent
Image · R, G, B     complete RGB; A is absent
Image · R, G, B, A  complete RGBA
Channel             one spatial value, inspected but not made into an Image
```

An absent component is not a connected component whose samples happen to be
zero. Fixed RGBA rendering may substitute zero for absent color and one for
absent Alpha, but that fallback never changes semantic presence, saved graph
state, connection meaning, or export eligibility.

## Descriptor Schema 3

Semantic descriptor schema 3 adds a dedicated Image-component-presence field.
It is independent of texture format, channel packing, alpha association,
semantic Channel purpose, units/range, sampling, and spatial extent.

The public contract uses this exact set:

- `R`
- `G`
- `B`
- `A`

The in-memory representation is a four-bit set in R, G, B, A order. The JSON
representation is a readable, duplicate-free `presentImageComponents` array in
that canonical order. A known Color Image descriptor must contain at least one
component. Non-Image values record the field as Not Applicable. A Color Image
may record it as Unknown only when current evidence cannot establish presence.

Schema-1 and schema-2 descriptors migrate as follows:

- known RGB layout becomes `R, G, B`;
- known RGBA layout becomes `R, G, B, A`;
- a known named layout containing only recognized component names becomes the
  corresponding set;
- any other Color Image layout becomes Unknown rather than guessed; and
- non-Image values become Not Applicable.

Component presence participates in structural equality, strict semantic
matching, canonical descriptor content, semantic fingerprints, diagnostics,
and renderer cache identities.

### Source And Operation Propagation

- Ordinary decoded sources publish RGB when the original source has one to
  three channels and RGBA when it has four channels. A one-channel ordinary
  source remains a color Image with R, G, and B present because Stack's decoder
  expands it to the existing visible grayscale RGB relationship.
- Identity, pointwise component-independent work, Technical Image work,
  geometry, and direct Image output preserve the set.
- Channel Split exposes only components known present. Attempting to execute an
  absent component publishes a typed missing-component failure naming it.
- Image Combine publishes exactly the set of connected R/G/B/A inputs. It does
  not infer presence from physical values.
- An operation requiring complete RGB remains authored but cannot execute until
  R, G, and B are present. Its diagnostic names every missing component.

## Image Combine v2

Image Combine retains stable input IDs `r`, `g`, `b`, and `a` and output ID
`imageOut`. All inputs are Channels. At least one color input is required for a
used result; Alpha alone does not form a display Image.

Every connected input must have a known finite extent equal to every other
connected input, including origin, full window, data window, raster origin, and
pixel aspect. Unknown extent or mismatch is a hard planning error. The graph
remains connected and suggests an explicit Reformat; rendering never stretches,
crops, resamples, or silently chooses one input's size.

Missing color components materialize as zero only at the fixed RGBA execution
boundary. Missing Alpha materializes as one for viewport/file storage only.
Those substitutions do not alter the descriptor.

Image Combine produces Unknown color identity, transfer, reference, and alpha
association unless a later contract can prove them. It preserves compatible
known range, precision, sampling, and spatial information only when the
connected Channels agree; otherwise the applicable field becomes Unknown or a
hard spatial error as specified above.

## Output v2 And Channel Inspection

Output exposes one primary input with stable ID `imageIn` and visible label
`Result · Image or Channel`. The port accepts exactly Color Image or Channel.
It no longer uses hidden R/G/B/A construction pins. Image construction belongs
to Image Combine.

When the input is an Image, Output preserves and presents the Image descriptor.
When the input is a Channel, Output preserves the Channel descriptor and uses
one saved inspection mode:

- Neutral, the default: copy the Channel into display R, G, and B;
- Red: copy it into display R and use zero for G and B;
- Green: copy it into display G and use zero for R and B; or
- Blue: copy it into display B and use zero for R and G.

The inspection mode changes only viewport presentation. It does not change
Channel samples, assign an Image component, change the wire descriptor, or
create export components. Output settings schema 1 stores only the normal mode.
A temporary viewport override is a separate unsaved editor state and may be
implemented after the saved path without changing this contract.

Ordinary RGB/RGBA PNG export is disabled when Output receives a Channel. The
diagnostic directs the user to connect it explicitly to Image Combine. A
partial Image remains exportable; absent color samples are written as zero and
absent Alpha as opaque at the current fixed RGBA PNG boundary without changing
the authored descriptor.

Output's definition advances to major version 2 because its public accepted
input contract changes. Graph schema 8 stores Output settings and no longer
creates new four-pin Output graphs. The schema-7 reader may preserve one
legacy single-Channel Output link by migrating it to `imageIn`; legacy
multi-component Output construction remains unresolved until replaced with an
explicit Image Combine.

## Visible Constant Channel And Opaque Alpha

Constant Channel is a new version-1 Channel definition with:

- `value`, a finite float defaulting to `1.0`;
- `channelOut`, an ordinary branchable Channel output;
- `matchExtent`, an advanced required Channel reference that supplies spatial
  extent without supplying sample values; and
- an initial purpose hint of Alpha for generated opaque-Alpha nodes. Durable
  general role schema and propagation remain C4.

The renderer may broadcast the value lazily. If a texture is required, it
materializes one scalar field at the exact Match Extent spatial contract.
Missing or invalid Match Extent is a typed failure; Stack never invents a
canvas size.

### Creation Transaction

When an Image Combine result first becomes connected downstream, Stack checks
the pre-transaction graph:

1. at least one of R, G, or B is connected;
2. A is disconnected; and
3. automatic opaque Alpha has not been deliberately suppressed for that
   Combine instance.

If all conditions hold, the same undoable graph command:

1. creates Constant Channel with value `1.0`;
2. selects the extent source in stable R, then G, then B order;
3. connects that exact upstream Channel branch to `matchExtent`;
4. connects `channelOut` to the Combine A input; and
5. creates the originally requested downstream connection.

All nodes, strings, links, and vector capacity are prepared before publishing
the command. Any allocation, validation, or connection failure leaves the
pre-transaction graph unchanged. Undo removes the downstream connection,
generated node, and both generated links together; redo restores the exact
prepared command.

The generated node is placed beside and below Image Combine using deterministic
canvas spacing. Placement may avoid an occupied rectangle, but layout never
changes the chosen extent source or graph meaning.

Deleting the generated source, or deliberately removing its A connection, sets
a saved suppression flag on that Combine instance. It does not immediately
respawn. `Create Opaque Alpha` is an explicit one-command action that clears
suppression and recreates the same structure. If the saved Match Extent source
later disappears, Constant Channel remains unresolved and offers `Rematch
Extent`; it never silently changes from R to G or B.

## Failure And Diagnostic Rules

New stable rules cover:

- malformed or empty known component sets;
- a requested missing Image component;
- Image Combine with Alpha only or no used color input;
- unknown or mismatched Combine extents;
- unresolved Constant Channel Match Extent;
- legacy multi-pin Output construction;
- Channel inspection export rejection; and
- transactional opaque-Alpha creation failure.

Structural and extent failures are hard errors. Unknown color/transfer/alpha
meaning remains informative or warning-level when numeric execution is still
defined. Diagnostics never mutate pixels or insert a hidden repair.

## Persistence And Forward Identity

- Semantic descriptor schema advances from 2 to 3 with deterministic migration.
- Graph JSON advances from 7 to 8 for Output settings, Combine automatic-Alpha
  state, and Constant Channel nodes.
- Output advances to definition major version 2.
- Image Combine advances to definition major version 2.
- Constant Channel begins at definition version 1.0.0.
- Stable port IDs named above are persistence identities; labels are not.
- Component presence and saved Output inspection mode participate in semantic
  and graph fingerprints where they can affect analysis or presentation.

Pre-rewrite compatibility remains outside the program. Supported schema
migrations must fail explicitly rather than collapse absence into zero.

## Implementation Passes

The contract is implemented in bounded passes:

1. **Phase 7B1 — component-presence foundation:** descriptor schema 3,
   migration, propagation helpers, Image Combine semantic description,
   fingerprints, and CPU/graph persistence tests.
2. **Phase 7B2 — Output inspection:** exact Image-or-Channel input, saved mode,
   renderer presentation, export rejection, graph schema 8, UI, and live GPU
   cases.
3. **Phase 7B3 — Constant Alpha transaction:** Constant Channel, exact extent
   validation, transactional create/delete/restore/undo/redo, lazy execution,
   persistence, and live graph cases.

Each pass must keep later behavior inactive until its prerequisites and tests
pass.

## Required Evidence

Automated evidence must prove:

- every nonempty component subset round-trips and fingerprints distinctly;
- absence differs from a present zero Channel;
- schema-1/2 migration is deterministic;
- partial Images preserve presence through identity, pointwise, and Reformat;
- missing complete-RGB inputs fail by exact component name;
- Output inspection modes produce the declared display mapping without changing
  the Channel descriptor;
- Channel Output blocks PNG while explicit Image Combine enables it;
- mismatched Combine extents never execute or stretch;
- auto Alpha chooses R/G/B deterministically and materializes exact opaque
  samples;
- allocation/failure rollback, delete suppression, restore, save/load, undo,
  and redo preserve graph integrity; and
- focused graph, registry, live GPU, all Node Math CTests, and the preferred
  Windows build pass before the vertical slice is complete.

Native visual review must cover partial-Image wire text, Output's inspection
control and export-disabled explanation, Constant Channel placement, advanced
Match Extent discoverability, and undo/redo behavior.

## Deferred Boundary

This contract does not implement durable Neutral/Mask/Alpha/Luminance/EV role
propagation, Set Channel Role, general Value-or-Channel parameters, per-node
RGB/RGBA participation, source dissolution, compound interaction revisions,
monitor-profile presentation, or unrelated node-library expansion. Those
remain C4 through C8 and R1/R3/R4 in dependency order.
