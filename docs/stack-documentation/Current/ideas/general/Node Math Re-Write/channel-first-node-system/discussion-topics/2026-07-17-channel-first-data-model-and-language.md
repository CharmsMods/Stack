# Channel-First Data Model And User Language

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1508-channel-language-first-answers.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1533-channel-preview-mask-and-broadcast.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1632-channel-ui-confirmations-and-uniform-reuse.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1649-channel-role-semantics.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-18-1749-guided-question-cadence.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-18-1801-output-scenarios-and-explicit-alpha.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-19-1332-channel-output-export-and-alpha-parity.md`
- Type: question
- Topic: channel-language
- Verification: partially-verified against the current value and socket types

## User Direction

The user wants Stack to be mentally understandable as collections of channels
and values being manipulated. Current terms such as `Scalar`, `ScalarField`,
and `Mask` feel like too many disconnected categories for things that can all
be understood as numbers with different shapes and roles.

The goal is not to erase real differences. It is to reduce the number of ideas
the user must learn while retaining mechanically reliable types.

## A Candidate Smaller Mental Model

This is a discussion proposal, not a naming decision:

1. **Value** — one value for the entire operation or graph location. It may be
   a number, Boolean, vector, matrix, coordinate, or another small uniform
   value.
2. **Channel** — one value at each pixel location. Red, green, blue, alpha,
   masks, depth, exposure maps, and similar maps can share this basic shape.
3. **Image** — a related bundle of channels whose relationship matters. An RGB
   image is not merely three unrelated arrays when an operation uses luminance,
   hue, saturation, white balance, or a color transform.
4. **Resource or Data** — curves, LUTs, histograms, statistics, metadata, and
   other structured values that are neither one uniform value nor a pixel
   grid.
5. **Specialized data** — RAW sensor values, complex spectra, external-model
   handles, frame collections, and other domains with dedicated rules.

Under this model, a mask is mathematically a Channel but semantically has the
role `mask`, usually a unitless 0–1 expectation, and mask-specific UI. A red
plane is a Channel with role `red`. An exposure map is a Channel with EV units.
Shape and meaning remain distinct without forcing the user to learn an
unrelated word for every role.

## One Number For A Whole Texture

A single number is not literally a texture. It can be *broadcast* across a
texture-shaped operation: the same value is used at every pixel. This is the
relationship between a current uniform `Scalar` and a per-pixel
`ScalarField`.

The language should explain that relationship plainly. Possible user-facing
pairs to research include:

- Value / Channel
- Constant / Channel
- Number / Pixel Channel
- Uniform Value / Pixel Field
- Control Value / Image Map

The internal terms do not have to be identical to the user-facing labels, but
both need one unambiguous mapping.

## Imported Image Channel Access

The user proposed automatically creating a split-all-channels graph followed by
a combine node. This would make the channel model visually undeniable and make
every channel directly accessible.

Several product forms should be compared before choosing one:

1. **Always materialized visible Split/Combine nodes.** Very explicit, but even
   a simple import begins with graph clutter and may imply work that the
   compiler does not need to perform.
2. **An expandable Image node.** The node normally exposes one Image output and
   reveals R/G/B/A channel outputs on demand.
3. **A collapsed built-in compound.** The default source appears compact, but
   can be opened or dissolved to reveal source, split, and combine structure.
4. **Virtual channel access.** Image wires remain bundles, while dragging from
   a channel affordance creates an explicit extraction edge or node only when
   used.
5. **Project/import template.** Users can choose “simple image” or
   “channel-ready image” when importing without changing the fundamental source
   node contract.

The design must distinguish visual authoring from execution. Exposing channels
does not require four physical textures or four render passes when the renderer
can reference components of one image resource.

## Questions For The Next Conversation

- Is `Channel` acceptable as the primary name for any one-value-per-pixel
  stream, including masks and non-color maps?
- Should an image be presented as a bundle that can be opened, or should the
  split/combine structure always be visible?
- Are alpha and auxiliary channels members of the same expandable bundle?
- Does a user-created arbitrary four-channel bundle become an Image, a Data
  Image, or simply a named channel bundle?
- Which distinctions must remain visible on the wire: role, units, range,
  extent, color relationship, or all of them on demand?
- Can a Mask connect anywhere a generic Channel can, with warnings, or should
  mask-role connections remain a separate compatibility family?

## Guided Conversation Round 1 — 2026-07-17

Status: partially answered; vocabulary and import behavior are substantially
narrowed, while vector/coordinate presentation, image-role UI, mask-role rules,
and broadcast presentation remain under discussion.

This round starts with five product choices. The options are conversation aids,
not a forced multiple-choice decision. The user may combine them or propose a
different model.

### 1. Primary User-Facing Vocabulary

Should Stack teach the following five broad categories?

- **Value:** one Boolean, number, vector, matrix, coordinate, or similar small
  value used everywhere an operation needs it.
- **Channel:** one value at every pixel location.
- **Image:** a related bundle of channels whose joint meaning may matter.
- **Data:** structured resources such as curves, LUTs, histograms, statistics,
  and metadata.
- **Specialized:** RAW, spectra, frame collections, external-model handles, and
  other values with dedicated rules.

Starting recommendation: use these plain labels in the main UI and retain
technical terms such as `uniform scalar` and `per-pixel scalar field` in
developer-facing contracts and advanced inspection. `Scalar` is a professional
mathematical term, but the user does not need it as the first word Stack teaches.

Decision needed: accept this vocabulary, alter one or more labels, or choose a
different smaller model.

### 2. Is Mask A Separate Kind Or A Channel Role?

Mathematically, a mask is one value per pixel. Semantically, it normally means
how strongly an operation applies, usually from 0 to 1.

Starting recommendation: present Mask as a named **role of Channel** while
retaining mask-specific range, preview, and connection rules. This reduces the
mental categories without pretending an arbitrary red channel is already a
well-formed mask.

Decision needed: Mask as a Channel role, Mask as a completely separate type,
or a hybrid presentation where users see Mask but advanced inspection explains
that it is a one-channel field with mask semantics.

### 3. What Makes Several Channels An Image?

Three arbitrary channels are not always a color image. RGB channels have an
ordered relationship; an operation such as saturation depends on that
relationship. A depth, mask, and temperature channel bundled together would be
data, but not an RGB image.

Starting recommendation: define Image as an ordered related channel bundle with
declared roles. Let arbitrary bundles use a separate name such as Channel Set or
Data Image rather than calling every multi-channel value a color image.

Decision needed: whether Stack needs this distinction and which user-facing
name should describe an arbitrary bundle.

### 4. How Should One Value Apply To Every Pixel?

A single Value can be repeated, or broadcast, across a Channel or Image
operation. Requiring a visible Broadcast node for every slider-like input is
very explicit but can make ordinary graphs unnecessarily large.

Starting recommendation: permit automatic broadcast only on ports whose
definition explicitly accepts either a Value or Channel. Show that behavior in
the pin/wire explanation, such as `Value — repeated for every pixel`. Require an
explicit operation when a port does not declare that behavior.

Decision needed: selective visible broadcast, explicit Broadcast nodes
everywhere, or another rule.

### 5. How Should Imported Image Channels Appear?

Always creating visible Split and Combine nodes makes channels obvious, but it
also adds several nodes before the user performs an edit and may suggest four
physical image passes.

Starting recommendation: keep the imported Image output compact, but make it an
expandable channel bundle with accessible R/G/B/A pins or a channel extraction
affordance. Create explicit Split/Combine structure only when the user asks to
open or dissolve that bundle. The renderer can still access components without
physically splitting the texture.

Decision needed: always-visible Split/Combine, expandable Image node, collapsed
compound, virtual channel access, import template choice, or a combination.

### Answer Capture Template

For each question, record:

- User preference:
- User reasoning:
- Terms the user dislikes or prefers:
- Follow-up explanation needed:
- Provisional decision, unresolved question, or implementation requirement:

## Guided Conversation Round 1 Response Record — 2026-07-17 15:08

### Confirmed Direction

- Use **Value**, **Channel**, **Image**, **Data**, and **Specialized** as the
  primary beginner-facing categories.
- Keep `scalar`, `uniform`, and `field` as precise internal or advanced
  terminology rather than the first vocabulary Stack teaches.
- Keep imported images compact in their normal current form.
- Add a selected-node/right-click action to **Dissolve Into Channels**,
  including alpha. The exact expansion and rewiring behavior still needs a
  later compound/decomposition contract.
- Broadcasting one Value across all pixels is mathematically acceptable. Its
  authored-graph and UI behavior needs the clearer distinction below before it
  becomes an implementation rule.

### Value Subtypes: Boolean, Vector, And Coordinate

`Value` is the umbrella category. Its subtype still matters and should remain
visible on the node and pin:

- **Boolean Value:** one `true`/`false` choice. It can enable a mode, invert a
  result, select an optional branch, or control another genuinely binary
  property.
- **Number Value:** one numeric amount such as exposure EV, blur amount,
  threshold, opacity, or a multiplier.
- **Vector Value:** several related numbers treated as one quantity. A 2D
  vector can represent direction, offset, or scale; a 3D vector can represent
  RGB multipliers or a three-axis direction; a 4D vector can represent an RGBA
  value or another four-component control.
- **Coordinate Value:** a position, commonly `(x, y)`. It can represent a
  gradient endpoint, radial center, crop point, sampled point, lens-distortion
  center, or another place in image space.

A Coordinate and Vector2 may both store two numbers, but they promise different
meaning. `(0.25, 0.75)` as a Coordinate is a location; `(0.25, 0.75)` as a
Vector might be a direction or displacement. Keeping separate subtypes lets
pins reject or explicitly convert an accidental position-to-direction
connection.

Current-code boundary: Stack can author, serialize, and exact-type-check these
Value subtypes, but vectors and coordinates do not yet have ordinary live edit
consumers. The demonstrated live uniform path is a Number Value driving
Exposure EV. Vector and Coordinate examples above describe intended useful
node contracts, not features that are already fully wired.

Provisional presentation recommendation: keep all of these under **Value** in
the primary mental model. Show the subtype as `Boolean Value`, `2D Vector`,
`Coordinate`, and so on when the distinction is relevant.

### Mask Is Mathematically A Channel

The user's understanding is correct: a mask is mathematically one value at each
pixel, so it has the same basic shape as a single Channel.

The reason to retain a `Mask` role is not that it needs a different basic
mathematical container. The role tells Stack and the user that:

- the values normally mean application strength or selection coverage;
- the expected range is commonly 0 to 1;
- a specialized mask preview may be useful;
- mask combine/remap/threshold operations are relevant; and
- a mask input may clamp or interpret the channel according to the receiving
  node's exact contract.

Provisional direction: **Mask is a Channel with mask semantics**, not a sixth
primary data category. Whether the main UI says `Mask Channel`, simply `Mask`,
or `Channel · Mask` remains open.

### Three Channels Can Form An Image

The user does not want three inputs to require pre-existing R/G/B tags before
they can become an Image. Three Channels should be connectable to an Image
combination boundary, with Stack or the user assigning how those slots are
displayed.

Accepted direction after the guided conversation rounds:

- One Channel can be previewed neutrally by repeating it across display R, G,
  and B, producing grayscale.
- A Channel may be assigned a red, green, or blue display role and previewed in
  only that display component.
- Three Channels connected to an Image combiner can define the three display
  component slots regardless of their earlier roles.
- Alpha is an optional fourth Channel and remains independently accessible.

This separates *being a Channel* from *which display/image slot currently uses
that Channel*. Standalone preview interpretation belongs to the output/viewing
context and does not retag the Channel. Connecting the Channel to a labeled
Image Combine socket assigns its component role inside the resulting Image.
Text and shape badges on the receiving socket and connection make that role
visible without relying only on color or changing the reusable upstream
Channel.

Still unresolved is the general propagation rule for semantic roles. For
example, Stack must define when Blur preserves a Mask role, when arithmetic
produces a neutral Channel instead, and whether an explicit role-assignment
operation is required after ambiguous math.

### Broadcasting Explained More Concretely

Two related situations were previously combined under the word broadcasting:

1. **A node input accepts either one Value or a per-pixel Channel.**
   For example, a Multiply `Amount` input can accept the Value `0.5`, meaning
   every pixel uses `0.5`, or a gradient Channel, meaning every pixel uses the
   amount stored at that location. With a Value, the shader can use one uniform
   number and does not need a generated texture. The pin explicitly promises
   both forms.
2. **A hypothetical workflow explicitly materializes a constant Channel.**
   This would allocate a spatial Channel containing the same number at every
   pixel. It is mathematically valid, but the discussion has not identified a
   convincing public authoring workflow that requires it.

The first form keeps common control inputs compact and is normally a real
memory and execution optimization: the backend can supply one uniform number
instead of allocating, filling, storing, and sampling an entire constant
texture. The exact performance benefit depends on the backend and surrounding
operation, so the graph contract should promise equivalent values rather than a
specific speedup.

Current direction: allow `Value or Channel` only on ports that explicitly
declare both and lower a connected Value without manufacturing a texture when
possible. Do not add a public Fill/Broadcast node merely for theoretical
completeness. Reconsider an explicit `Make Channel` operation only after a real
workflow requires a spatial Channel value in its own right. Current Stack has
first-class broadcast helpers in its value infrastructure, but this general
live UI/execution rule is not implemented yet.

### Status Of The Earlier Focused Decisions

1. **Direction established:** Image Combine sockets assign R/G/B/A roles in the
   resulting Image; an incoming Channel does not require a permanent component
   role first.
2. **Open:** Should `Mask` appear in normal UI as `Mask`, `Mask Channel`, or
   `Channel · Mask`?
3. **Narrowed direction:** implicit reuse is allowed only for a declared
   `Value/Channel` input; no public Fill/Broadcast node is currently justified.
4. **Open:** For Value subtypes, should the browser visibly list Boolean Value, Number
   Value, Vector, and Coordinate separately, or primarily offer one Value node
   whose subtype is selected inside it?

## Guided Conversation Round 2 Response Record — 2026-07-17 15:33

### Confirmed And Narrowed Direction

- The Vector-versus-Coordinate distinction is understood well enough to retain
  both as Value subtypes for later use.
- Mask is accepted as a Channel with the semantic purpose of application
  strength or selection coverage.
- A Mask's usual 0-to-1 range should inform and warn, not globally prevent
  unusual values from existing or being displayed.
- A standalone Channel connected to Output defaults to a **Neutral** grayscale
  preview by repeating its value into display R, G, and B.
- Neutral, Red, Green, and Blue preview mappings match the intended mental
  model exactly.
- Image Combine should visibly have four ordered component inputs: R, G, B,
  and optional A. A missing A input produces opaque alpha.
- The previously listed follow-up product decisions are intentionally deferred
  until the mask, amount-channel, preview, and broadcast concepts below are
  fully explained.

### Image Combine Alpha Clarification

The Image Combine UI should show all four sockets even though alpha is optional:

```text
R — required Channel
G — required Channel
B — required Channel
A — optional Channel; defaults to 1.0 when unconnected
```

PNG alpha is itself a two-dimensional Channel and may vary independently at
every pixel. An 8-bit PNG can store alpha values from 0 through 255, not only 0
or 255. Current Stack exports 8-bit PNG, so a normalized alpha Channel is
quantized into that range. The constant plane of `1.0`/`255` is specifically
the correct default when no alpha Channel is connected.

### Preview UI Walkthrough

Preview interpretation should not silently modify Channel data or permanently
assign an image role. A concrete interaction to evaluate is:

1. The user connects a standalone Channel directly to Output.
2. The viewport displays it as Neutral grayscale by default.
3. Selecting the Output node, Channel-producing node, or inspected wire reveals
   a **View Channel As** control in the relevant inspector:
   `Neutral`, `Red`, `Green`, `Blue`, and role-specific mask views.
4. Choosing Red displays the Channel in the red output component while setting
   green and blue to zero. The underlying Channel remains unchanged.
5. Returning to Neutral again repeats the value into all three display
   components.

For a Mask-role Channel, useful viewing choices are:

- **Grayscale:** 0 is black, 1 is white, intermediate values are gray.
- **Color Overlay:** tint the mask with a selectable overlay color and opacity,
  optionally over a chosen reference Image.
- **Raw/Range Inspection:** show the direct values while visibly identifying
  below-zero, above-one, NaN, or infinite regions rather than silently
  normalizing them.

The first implementation need not provide every view. Neutral grayscale is the
required default; overlay and range inspection can be added deliberately.

Actual role assignment happens at composition boundaries. Connecting a Channel
to Image Combine's R socket assigns it to the resulting Image's red component;
temporarily previewing that same Channel as Red does not.

Candidate UI elements, stated concretely:

- `View Channel As` selector in the node/output inspector;
- small text/shape badge on an inspected wire showing `Neutral`, `Mask`, or an
  actual assigned component role;
- Image Combine sockets permanently labeled R, G, B, and A;
- context actions such as `Connect To Image Component > R/G/B/A`; and
- optional reordering controls on Image Combine without retagging upstream
  Channels.

### Amount Channel Versus Mask Channel

The user's comparison is correct: both are per-pixel Channels that spatially
control an edit. They differ in *where the Channel enters the formula*.

Let `F(image, amount)` be an edit.

**Per-pixel Amount Channel:**

```text
output(x, y) = F(input(x, y), amountChannel(x, y))
```

The operation's own parameter changes at every pixel.

**Mask Channel:**

```text
processed(x, y) = F(input(x, y), oneChosenAmount)
output(x, y) = mix(input(x, y), processed(x, y), mask(x, y))
```

The edit is calculated at one chosen amount, then the mask chooses how much of
that finished edit replaces the original.

These can coincide for some simple linear formulas, but they are not generally
equivalent. For Exposure:

```text
per-pixel EV: output = input * 2 ^ evChannel
masked fixed EV: output = mix(input, input * 2 ^ fixedEv, mask)
```

The two expressions usually produce different numbers.

UI unification should therefore happen at the data-family level without merging
the mathematical roles:

- both use the common Channel pin/wire family;
- the parameter pin can read `Amount · Value/Channel`;
- the coverage pin can read `Mask · Channel`;
- the node's exact definition states whether one or both exist and where each
  enters the formula; and
- an optional Mask pin can remain collapsed until connected when reducing
  clutter is useful.

Some specially defined nodes may prove that a per-pixel strength Channel is
equivalent to masking for their exact formula. Stack should record that proof
per definition rather than assuming it for the whole library.

### Reassessment Of Fill Channel

`Value -> Fill Channel -> ...` was a hypothetical explicit conversion, not a
current required workflow. It meant “make a Channel whose value is the same at
every pixel.” For example, Value `0.5` over a 1920-by-1080 extent would define a
Channel containing `0.5` at all 2,073,600 positions.

That operation is mathematically valid, but the earlier examples using blur and
Field Mean were not compelling product use cases: blurring a constant Channel
does not change it, and its mean is already the original Value. Image Combine
could itself declare `Value or Channel` if constant component inputs are useful,
allowing the renderer to broadcast lazily without creating a texture.

Current direction: do not add a public Fill Channel node merely for theoretical
completeness. Keep broadcast as a definition capability and reconsider an
explicit `Make Channel` operation only when a real workflow needs a reusable
spatial extent or Channel-only output.

## Guided Conversation Round 3 Response Record — 2026-07-17 16:32

### Confirmed Direction

- The complete standalone Channel preview interaction is accepted: Neutral
  grayscale is the default, component preview choices affect only viewing, and
  Image Combine sockets assign actual R/G/B/A roles in the resulting Image.
- Contextual text/shape role badges on sockets and wires are accepted. A source
  Channel remains neutral and reusable even when a receiving connection shows
  `Mask` or `G`.
- The distinction between a per-pixel Amount Channel and a Mask Channel is now
  understood as changing a parameter versus blending between the original and
  the fully processed result.
- Reusing one Value across every pixel instead of materializing a constant
  Channel is understood as an execution and memory optimization as well as a
  concise graph rule.

### Next Conversation Queue

The next discussion should finish **Channel roles and their UI language**:

1. Decide whether a mask input is labeled `Mask`, `Mask Channel`, or
   `Channel · Mask` in normal and detailed UI.
2. Define when a Channel role is preserved through an operation, deliberately
   changed, or dropped back to Neutral because the result is ambiguous.
3. Decide whether users can manually assign roles such as Mask, Alpha, EV, or
   Neutral outside Image Combine, and what that assignment promises.
4. Define which warnings come from a role, such as the conventional Mask range
   of 0 to 1, without restricting unusual math.

After those answers, proceed to the declared `Value/Channel` input rule, Value
node browser presentation, and then the broader connection compatibility and
warning system.

## Guided Conversation Round 4 Response Record — 2026-07-17 16:49

### Confirmed Direction

- A normal mask input uses the friendly label **Mask**. Detailed inspection may
  identify its full family and role as **Channel · Mask**.
- Role preservation should be the default when the result still has a clear
  meaning, but the system should not burden ordinary graph authoring with
  repeated questions, dialogs, or hard blocks.
- A manually changed Channel role is real downstream semantic information. It
  changes what downstream pins receive and what badges, labels, default views,
  diagnostics, and compatibility feedback the UI presents.

### Recommended Role-Propagation Contract To Evaluate

Each Channel-producing definition declares one deterministic output policy:

1. **Preserve primary role.** Operations such as Blur, Transform, or multiplying
   by a role-neutral Value normally retain the input Channel's role.
2. **Preserve matching roles.** An operation involving Channels with compatible
   matching roles may keep that role. For example, combining two Mask Channels
   can still produce a Mask even if a separate range warning notes that the
   values may leave 0 through 1.
3. **Set a declared role.** A node whose purpose creates a known meaning, such
   as a mask generator or Image component extraction, declares that output
   role directly.
4. **Return Neutral when ambiguous.** Mixed-role or arbitrary math whose result
   has no honest single meaning produces a Neutral Channel with a small
   informational explanation and a quick reassignment action. It does not open
   a dialog or prevent execution.

This makes preservation convenient without allowing stale role metadata to
survive obviously meaning-changing math.

### Role Assignment Versus Numeric Conversion

Changing a Channel role should change the semantic descriptor received
downstream, not silently rewrite the pixel numbers. For example, assigning
`Mask` can change preview defaults, labels, compatibility, and range warnings,
but the values remain exactly what they were. A node that clamps, remaps,
normalizes, or converts the numbers is a separate explicit mathematical
operation.

Because role assignment affects downstream behavior, it must be visible in the
authored graph. A branch-specific **Set Channel Role** boundary is the clearest
candidate. A right-click action such as `Set Channel Role > Mask` may insert or
configure that boundary so the interaction remains quick. This avoids changing
every branch when one source Channel is reused with different meanings.

The role should influence compatibility, diagnostics, display defaults, and
port presentation. A downstream node's declared input still owns the formula;
generic math should not secretly switch algorithms only because metadata was
retagged.

### Important Descriptor Question

`Red`, `Mask`, and `EV` may look like peer labels in a compact UI, but they
describe different things:

- **component assignment:** R, G, B, or A inside an Image;
- **semantic purpose:** Neutral/Data, Mask/Coverage, Depth, Luminance, and
  similar meanings;
- **unit or scale:** EV, pixels, degrees, normalized coverage, and others; and
- **preview interpretation:** Neutral grayscale, isolated Red/Green/Blue, mask
  overlay, or diagnostic range view.

The next discussion must decide whether Stack stores these as separate
descriptor fields while presenting one compact contextual badge. Keeping them
separate internally would allow, for example, an extracted red component to be
used contextually as a Mask without confusing its origin, unit, and current
receiving role.

### Next Questions

1. Should manual semantic assignment be a visible `Set Channel Role` node, a
   property on a connection, or a right-click shortcut that visibly inserts
   the node?
2. When roles conflict, is Neutral-with-information the desired non-annoying
   fallback, or should Stack preserve the primary input's role more
   aggressively?
3. Which roles belong in the first public set: Neutral, Mask, Alpha, R/G/B
   component, Luminance, EV, Depth, or a smaller subset?
4. Should unit and expected range be separate from semantic role, even though
   the normal canvas may combine them into one badge or tooltip?

## Guided Implementation Question Round 1 — Issued 2026-07-18 17:49

### Cadence

Future rounds should ask only one or two narrowly connected questions. Each
round must:

- identify the current documented direction;
- inspect the current implementation and tests rather than assuming behavior;
- explain concrete options and consequences in ordinary language;
- record the questions before answers arrive; and
- archive and route the user's answers into the owning design documents before
  advancing.

### Verified Current Behavior

- Channel Split exposes R/G/B/A as separate `ScalarField` outputs.
- Channel Combine exposes R/G/B/A inputs, but currently marks all four optional.
- Its shader supplies `0.0` for each unconnected R/G/B component and `1.0` for
  unconnected A.
- Current connection tests route a split R output dropped on Output's main
  Image input to Output's R component socket. It therefore displays red rather
  than using the proposed Neutral grayscale interpretation.
- The adopted direct-viewport rule says the viewport displays its connected
  authored Output without a separate hidden appearance transform.

### Question 1 — Who Owns Single-Channel Output Interpretation?

When one Channel is connected directly to Output, where should the
`View Channel As` interpretation live, and should export match the viewport?

**Option A — Saved Output interpretation (recommended).**

- Output accepts the Channel at an explicit Channel boundary.
- That Output stores `Channel View: Neutral / Red / Green / Blue`, with Neutral
  as the default.
- Neutral repeats the value into output R/G/B; component modes place it in only
  the selected component.
- The viewport and an export made from that Output use the same mapping.
- The upstream Channel and its role remain unchanged.

This is reproducible, branch-local, and consistent with direct viewport output.
It treats the mapping as an authored Output interpretation rather than a hidden
monitor transform.

**Option B — Viewport-only interpretation.**

- The viewport toolbar stores how the Channel is inspected.
- The graph and Channel remain unchanged.
- Export requires an explicit Image Combine or another authored mapping.

This keeps preview state outside image math, but the viewport can show something
that export will not reproduce. Project sharing also needs a separate decision
about whether the viewport choice is saved.

**Option C — Connection-owned interpretation.**

- The wire entering Output stores Neutral/R/G/B.
- Different branches can interpret the same Channel differently.
- Viewport and export may both follow the connection property.

This is flexible, but an important authored behavior becomes easier to miss on
the wire and requires new link persistence and editing UI.

### Question 2 — What Does An Unconnected Image Combine Component Mean?

Image Combine visibly has R, G, B, and A sockets. What should happen when one
or more RGB sockets are unconnected?

**Option A — R/G/B are required for a valid Image.**

- The node may exist while the user is wiring it, but it does not publish a
  valid Image until R, G, and B are connected.
- A remains optional and defaults to opaque `1.0`.
- Missing required components receive clear non-destructive diagnostics.

This follows the earlier wording that three connected Channels define an
Image, but prevents convenient partial-color construction unless users create
constant Channels explicitly.

**Option B — Unconnected components have visible constants (recommended).**

- R/G/B inputs default to constant `0.0`; A defaults to constant `1.0`.
- The resulting Image always has R/G/B/A component planes even when some are
  supplied by defaults rather than wires.
- The node UI visibly shows each unconnected default so the behavior is not
  hidden.

This preserves the current shader result and Stack's permissive mathematical
style. It also allows a pure-red Image by connecting only R. Three components
still define the Image, but some may be explicit constant Values rather than
connected Channel textures.

**Option C — Incomplete preview, blocked final use.**

- Missing RGB is filled with zero for editing previews.
- Export or another final consumer refuses the result until all RGB inputs are
  connected.

This offers temporary convenience, but makes the same wire mean “viewable” and
“not a valid result” depending on its consumer. It adds state and diagnostics
without a clear mathematical advantage.

### Status

Partially answered. The user requested concrete scenarios before choosing the
owner of single-Channel Output interpretation. The user rejected both required
RGB and always-present constant RGB planes in favor of semantic channel
absence, with numeric zero only when a fixed-component consumer needs a sample.
No implementation behavior or prior authority is changed by this discussion.

## Guided Implementation Question Round 1 — Clarification And Partial Answer

### Question 1 Scenario Comparison

The three ownership choices become easier to distinguish in ordinary editing
situations.

#### Scenario A — Inspecting And Exporting A Mask

A user connects a Mask Channel directly to Output and expects a grayscale PNG.

- **Saved on Output:** Output defaults to Neutral, so the viewport and export
  both repeat the Channel into R/G/B. Reopening the project produces the same
  result.
- **Viewport only:** the viewport can inspect the Mask as grayscale, but export
  still needs an explicit Combine or mapping node. Exporting without that
  authored mapping cannot silently copy the inspection choice.
- **Saved on the connection:** the wire can store Neutral and export correctly,
  but deleting and reconnecting the wire may discard the mapping unless the
  user notices and restores the link property.

#### Scenario B — Reusing One Channel In Two Roles

A user sends one Channel to a grayscale diagnostic Output and also uses it as
the red component of a constructed Image.

- **Saved on Output:** the diagnostic Output stores Neutral while Image
  Combine's R socket assigns red only in that Image. The upstream Channel stays
  reusable and untagged by either viewing choice.
- **Viewport only:** temporary grayscale inspection is easy, but a reproducible
  diagnostic export still needs an explicit mapping in the graph.
- **Saved on the connection:** the two branches can store different mappings,
  but the important distinction lives on wires rather than the more visible
  Output and Combine boundaries.

#### Scenario C — Temporarily Debugging An Intermediate Channel

A user wants to inspect whether a blur, mask, or exposure map contains the
expected values, without changing the authored result.

- **Saved on Output:** the user needs a debug Output or temporarily changes the
  active Output connection. The state is explicit and reproducible, but not the
  fastest temporary inspection.
- **Viewport only:** this is the fastest model for temporary inspection because
  `View As` can change without changing graph meaning or export.
- **Saved on the connection:** the inspection becomes authored link state even
  when the user meant it to be temporary.

#### Scenario D — Reopening Or Sharing A Project

A collaborator opens a project that displays a standalone Channel.

- **Saved on Output:** the intended mapping travels with the Output and matches
  export.
- **Viewport only:** the collaborator may see a different inspection mode if
  viewport state is not shared, and saved viewport state still does not define
  export.
- **Saved on the connection:** the result can be reproducible, but the cause is
  less visible and reconnecting the graph can change it.

The scenarios suggest a possible hybrid: authored Output interpretation owns
the reproducible viewport/export mapping, while a clearly temporary viewport
inspection override may be offered for debugging and must never change export.
That hybrid is not yet a decision.

### Question 2 Partial Answer — Absent Is Not The Same As Zero-Filled

The requested contract distinguishes semantic structure from the numeric form
used by a renderer:

- an unconnected R, G, or B socket means that Channel is absent from the
  resulting bundle;
- when a fixed RGBA shader or framebuffer samples an absent color component,
  its numeric fallback is zero;
- that fallback does not make a zero-filled Channel semantically present; and
- the node, output wire, hover information, and downstream compatibility rules
  must be able to report which Channels are actually present.

For example, connecting R and B creates a bundle containing R and B. A
component-independent consumer may process those two Channels. A saturation,
color-space, luminance, or other declared RGB-relationship operation must be
able to see that G is absent and provide the appropriate incompatibility,
repair suggestion, or warning rather than assuming the zero sample proves a G
Channel exists.

Current Stack does not yet fully represent this contract. The Combine shader
always writes a four-component value, and the current public logical types
distinguish `ColorImage`, `DataImage`, and individual channel-like fields but do
not yet publish an arbitrary present-component set as this proposed authored
value. A later question must decide whether the R+B result is called an Image,
Channel Set, Data Image, or another compact bundle name.

### Missing Alpha Direction

Missing alpha should not remain only an invisible `1.0` substitution. The
desired graph behavior is a visible auto-connected source whose output is a
reusable **Channel · Alpha**:

- its value is opaque `1.0` at every location;
- its spatial extent follows the resolved color-channel extent;
- users can branch its output, insert nodes between it and Combine, or replace
  it; and
- the renderer may implement the constant lazily as one Value broadcast over
  the extent instead of allocating and filling a full texture.

Being a real logical Channel means that it has extent, Alpha role, a visible
output connection, and downstream reuse. It does not require the backend to
store millions of physical copies of `1.0` when a constant representation is
equivalent.

The likely node concept is **Constant Channel**, with a visible Value of `1.0`
and an Alpha role in this auto-created use. Exact rules remain open for which
input supplies its extent, what happens when color-channel extents disagree,
and when it spawns: first color connection, first Combine output connection, or
first downstream use.

## Guided Implementation Question Round 1 — Decision Record 2026-07-19

### Confirmed Present-Component Semantics

- An unconnected component is semantically absent.
- An absent color component may read numerically as zero when a fixed RGBA
  representation requires a value.
- A connected constant-zero Channel is present and remains distinguishable from
  an absent Channel.
- Downstream compatibility and detailed wire information must use semantic
  presence rather than infer presence from the numeric RGBA storage value.

### Output Inspection Does Not Construct Export Channels

The earlier hybrid recommendation is corrected at the export boundary. A
standalone Channel may still default to a Neutral grayscale viewport
interpretation, and the viewer may offer other inspection modes, but this does
not replicate the Channel into authored export components.

For RGB or RGBA export, the graph must state the intended construction:

- one Channel connected to a general Channel-inspection boundary previews as
  Neutral grayscale but does not thereby become an RGB image;
- one Channel connected to an R component socket is authored as R, not
  grayscale; and
- to construct a grayscale RGB image from a Mask or other Channel, the same
  Channel is connected explicitly to R, G, and B.

The user described these as the three mask places; mechanically they are the
R, G, and B component inputs receiving the same Mask Channel. Stack should not
insert or imply those three authored connections merely to help with export.

This preserves the accepted separation between preview interpretation and
component role. Wires do not own standalone viewing state, and viewing state
does not change Channel values or exported component structure.

### Remaining Export-Boundary Question

The contract still needs one explicit failure rule for an export request made
while Output is only inspecting a standalone Channel. The leading direction is
to disable ordinary RGB/RGBA export with a clear explanation that the user must
author R/G/B component connections. A deliberately selected one-channel file
format, if Stack later supports one, would be a separate export contract rather
than an implicit grayscale conversion.

## Related Docs

- `2026-07-17-graph-rules-and-visual-feedback.md`
- `../../phase-1-contract-v1.md`
- `../../phase-5b-typed-connection-ui-contract.md`
- `../../phase-6b-reduction-contract-v1.md`
