# Accepted Channel-Based Product Direction

- Accepted: 2026-07-20
- Updated: 2026-07-22 (R2 user-facing terminology)
- Status: adopted product direction; implementation contracts remain required
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-20-1509-accept-design-and-clean-docs.md`
- Verification: checked against current Node Math Rewrite authority and current code on 2026-07-20

This is the authoritative product-direction summary for Stack's channel-based
node system. Topic files explain individual areas; source notes preserve the
conversation history. If a topic file and this document disagree, this document
owns the accepted direction until an explicit later decision changes it.

## Core Principles

- Teach a small model: **Value**, **Channel**, **Image**, **Data**, and
  **Specialized**.
- Keep mathematical shape separate from semantic meaning and physical storage.
- Keep numerically meaningful graph authoring permissive, but fail clearly when
  execution-critical structure or data is missing.
- Never insert a pixel-changing repair, conversion, resize, or export mapping
  invisibly.
- Friendly language may simplify terminology; it may not misstate the math.

## Values, Channels, Images, And Roles

- A **Value** is one Boolean, number, vector, coordinate, matrix, or similar
  uniform value.
- A **Channel** is one value at each pixel location.
- An **Image** is a spatial bundle whose Channels have an image/display
  relationship. An Image may be partial.
- **Data** covers curves, LUTs, histograms, statistics, metadata, and other
  structured resources.
- **Specialized** covers RAW, spectra, frame collections, external-model
  handles, and domains with dedicated rules.

Mask, Alpha, Luminance, EV, and Depth are Channel roles, not separate primary
mathematical containers. Component assignment inside an Image (R/G/B/A),
semantic role, units/range, spatial state, and preview interpretation remain
separate descriptor concerns even when the UI summarizes them in one badge.

## Partial Images And Component Presence

- An Image containing R and B but no G is valid and is presented as
  `Image · R, B`; G is semantically absent.
- A complete RGB Image has R, G, and B present.
- A present constant-zero Channel is different from an absent Channel.
- A fixed RGBA texture or framebuffer may use numeric zero when displaying an
  absent color component, but that storage fallback does not create semantic
  presence.
- Component-independent operations process the Channels that are present.
- Operations that require a complete RGB relationship may remain connected for
  permissive authoring, but cannot evaluate until R/G/B are present. They name
  the missing component and suggest an explicit repair.
- **Channel Set** is reserved for arbitrary related spatial Channels that do
  not claim one image/display relationship. It is not a new prominent everyday
  browser category.

## Channel Inspection And Export

- A standalone Channel defaults to Neutral grayscale inspection in the
  viewport. Red, Green, and Blue inspection modes place it only in that display
  component.
- Output owns the saved normal Channel inspection mode. A clearly temporary
  viewport override may be used for debugging.
- Inspection never changes Channel values, assigns an Image component, lives on
  a wire, or manufactures export Channels.
- Ordinary RGB/RGBA export is unavailable when Output is only inspecting a
  standalone Channel. The UI directs the user to construct an Image.
- A grayscale RGB Image is authored by connecting the same Channel explicitly
  to R, G, and B.
- A future deliberately selected one-channel file format is a separate export
  contract, not an implicit grayscale conversion.

## Image Combine And Opaque Alpha

Image Combine exposes R, G, B, and A Channel inputs. Unconnected color inputs
are absent. When its output first becomes connected downstream, at least one
color Channel is present, and A is unconnected, Stack creates and visibly
connects an opaque **Constant Channel**:

- Value `1.0`;
- role `Alpha`;
- ordinary editable, branchable, replaceable Channel behavior; and
- lazy constant execution when a physical texture is unnecessary.

The auto-created Constant Channel uses an advanced **Match Extent** reference to
one present color Channel. All present Image Combine inputs must have compatible
extents. A mismatch fails with an explicit Reformat suggestion; Stack does not
silently resize. Deleting the generated Alpha source is a deliberate override,
so it does not immediately respawn; `Create Opaque Alpha` can restore it.

## Role Propagation And Assignment

- Role-agnostic unary Channel operations preserve an unambiguous input role.
- Role-defining operations set their declared result role.
- Matching-role operations preserve that role when the result retains the same
  meaning.
- Mixed or arbitrary math whose result has no honest role returns Neutral with
  compact information, not a modal question or hard block.
- A visible branch-local **Set Channel Role** operation changes downstream
  semantics and presentation without altering, clamping, or remapping numbers.
- A receiving socket may show a contextual role without globally retagging the
  upstream Channel.
- Mask values conventionally use 0–1, but unusual values remain authorable and
  viewable. Each receiving node declares whether its formula clamps them.

## Value/Channel Inputs And Masks

- Automatic broadcast exists only on a port explicitly declared
  `Value/Channel`.
- A Value is reused at every pixel without requiring a generated texture.
- A Channel supplies a spatially varying parameter.
- Normal UI and accessible text spell out the accepted alternatives, such as
  `Amount · Value or Channel`. Contracts and space-constrained technical
  surfaces may retain `Value/Channel` as shorthand.
- Socket color and icons may reinforce the type, but readable text remains
  available wherever the distinction carries meaning.
- A Mask has the same Channel shape but enters the formula at a different
  stage: it blends original and processed results rather than varying the
  operation parameter.
- A reusable spatial constant uses Constant Channel with an extent contract;
  ordinary Value broadcast does not create one.
- Initial live `Value/Channel` ports should be pointwise parameters. Varying
  neighborhood support requires a later exact contract.

## Alpha Participation In Math

- Friendly image adjustments default to RGB and preserve Alpha.
- Applicable nodes store their own visible component-participation policy.
- A graph preference chooses the default for newly created applicable nodes,
  such as RGB or RGBA; changing it never reinterprets existing nodes.
- Generic component math declares its participation rather than assuming that
  all friendly image edits affect Alpha.
- Once Alpha is a standalone Channel, ordinary compatible Channel math edits it
  normally.
- Mask blending affects only the components selected by the receiving node
  unless that node declares a different exact formula.

## Imported Images And Channel Access

Imported images remain compact by default. **Dissolve Into Channels** is one
undoable, selected-source graph action that inserts truthful Channel Split and
Image Combine structure, exposes R/G/B/A, and reconnects former Image consumers
to the Combine result. Generated nodes are ordinary editable nodes. Absent
source components stay absent unless a visible node supplies them. Visible
channel wiring does not imply separate physical textures or render passes.

## Compounds And Honest Decomposition

- **Inspect** opens canonical internals read-only.
- **Edit / Make Unique** creates an editable local definition rather than
  silently changing a pinned shared definition.
- **Dissolve / Unpack** replaces the instance with an exact canonical graph.

The inspectability classes are Primitive, Transparent Compound, Optimized
Compound, and Structured Specialized Operation. An optimized compound may run
a fused or specialized implementation only when it is tested against the
canonical graph. A specialized operation may expose truthful stages and
intermediates without claiming a complete primitive decomposition. Arbitrary
shader reverse engineering is not a supported promise.

## Operation Scope And Execution

Node definitions declare factual execution capabilities such as pointwise,
neighborhood, geometry/resampling, reduction, global, multi-image, and
specialized/external behavior. These facts drive fusion, tiling, halo, extent,
materialization, caching, cancellation, and failure handling. The browser and
normal canvas use simpler language and do not turn planner classes into noisy
top-level categories.

## Color And Display Direction

- Color identity, transfer, reference, and alpha state travel with Image
  values or remain Unknown.
- Import preserves pixels and known metadata; untagged input remains Unknown.
- Assign operations change descriptors only. Decode, Convert, Encode, tone
  mapping, and gamut mapping are explicit pixel operations.
- Generic math remains permitted in any state. Named operations declare their
  intended domain and provide non-mutating diagnostics and visible repair
  actions.
- Templates may author compact, inspectable source-preparation and output
  compounds; there is no mandatory hidden working color space.
- The current View Transform should be divided into accurately named tone/range
  mapping and output-encoding stages.
- Professional monitor-profile presentation requires research. If adopted, it
  must be defined as non-exporting display presentation, deliberately revising
  the relevant direct-viewport authority rather than becoming hidden creative
  graph math.

## Unified Node Library

Every primitive, compound, migrated legacy operation, generator, and
specialized stage uses one versioned definition contract for formula, ports,
descriptors, alpha/color/spatial behavior, execution capability, diagnostics,
decomposition, presentation, lifecycle, and tests. Browser grouping is
task-oriented presentation metadata with technical search tags. Migration
starts with one pointwise Channel-safe operation, one RGB-relationship
operation, one neighborhood operation, one geometry operation, and one
specialized operation.

## Implementation Dependency Order

This sequence controls implementation dependencies. It does not restrict the
order of discussion, documentation, research, or user goal changes.

1. Specify Output, partial Image, and Constant Alpha behavior as one vertical
   contract.
2. Specify and implement a small durable Channel-role slice.
3. Add selected pointwise `Value/Channel` inputs and stored alpha participation.
4. Specify and implement source dissolution.
5. Complete compound interaction research and case studies.
6. Complete color/display research and explicitly revise authority if needed.
7. Finalize and activate the representative Phase 7 library slice. Candidate
   comparison may begin after the earlier C1–C6 contracts are stable.

The signed Divide correction and presentation-only Channel/Mask vocabulary are
independent bounded candidates. This design document does not activate any code
work or Phase 7.

## Technical Detail Files

- Data language and Image/Output: `design-details/channels-values-images-and-output.md`
- Connections and diagnostics: `design-details/connections-errors-and-visual-feedback.md`
- Alpha policy: `design-details/alpha-masks-and-math.md`
- Execution capabilities: `design-details/execution-and-performance.md`
- Compounds: `design-details/compound-nodes-and-unpacking.md`
- Color/display: `design-details/color-editing-display-and-export.md`
- Library migration: `design-details/node-library-and-migration.md`
