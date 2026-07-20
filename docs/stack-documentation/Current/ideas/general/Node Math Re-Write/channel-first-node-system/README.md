# Channel-First Node System Discussion Workspace

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: idea
- Topic: node-math-rewrite
- Verification: partially-verified

This folder separates the 2026-07-17 channel-first discussion into topics that
can be revisited one at a time. It is a design and research workspace, not an
active implementation phase.

## Current Conversation Checkpoint

The first guided rounds have established a strong direction for standalone
Channel viewing and contextual roles:

- a standalone Channel previews as Neutral grayscale by default;
- Neutral, Red, Green, and Blue are non-destructive viewport interpretations;
- Image Combine exposes R/G/B/A sockets and assigns component role within the
  resulting Image;
- pins and wires retain the broad Channel family while contextual badges show
  roles such as Mask or G;
- per-pixel Amount Channels and Mask Channels share the Channel shape but enter
  node formulas at different stages; and
- a Value accepted by a `Value/Channel` input should normally be reused as a
  uniform rather than materialized as a constant texture.

The normal friendly pin label for a mask is now **Mask**, while detailed UI may
show **Channel · Mask**. Channel roles are downstream semantic information, not
only preview decoration, and obvious operations should preserve them by
default without turning ambiguity into constant dialogs or hard blocks.

The next focused design topic is the internal shape and authoring UI of a
Channel descriptor: component assignment, semantic purpose, units/range, and
preview interpretation may need separate fields even when the canvas presents
one compact badge.

## Active Guided Question Round

Design closure now proceeds through one or two specific questions per round.
Each question is grounded in current code and existing decisions; each answer
is archived and merged into the owning discussion document before the next
round.

Round 1, issued 2026-07-18, asks only about:

1. whether single-Channel Output interpretation is saved on Output and applies
   consistently to viewport and export; and
2. whether unconnected Image Combine components are invalid or explicitly
   supplied constant defaults.

The exact options and code evidence are recorded in
`discussion-topics/2026-07-17-channel-first-data-model-and-language.md`.

The first response partially answers the second question and asks for concrete
scenarios before answering the first. Unconnected color sockets should mean
that those Channels are semantically absent, even if a fixed RGBA render target
must read an absent component numerically as zero. Missing alpha should become
a visible, connected, reusable opaque Alpha Channel rather than remain a hidden
shader default. Classification of partial channel bundles, constant-Channel
extent, automatic-node timing, and Output interpretation ownership remain open.

The 2026-07-19 follow-up confirms semantic channel absence and Alpha's parity
with every other editable Channel. It also separates viewing from construction:
a standalone Channel may be inspected as Neutral grayscale, but an RGB
grayscale export is authored by connecting that Channel explicitly to R, G,
and B. A preview interpretation must not silently manufacture export Channels.
The exact export response while Output is only inspecting one Channel remains
the next narrow decision.

The central product direction expressed in the source session is:

> Stack should present image editing as understandable manipulation of values
> and channels, while remaining honest about operations that depend on several
> channels, neighborhoods, complete images, multiple frames, or specialized
> algorithms.

The existing Node Math Rewrite contracts remain authoritative until a later
conversation deliberately changes them. In particular, this workspace does not
silently replace the current permissive-graph, explicit-color, compound, alpha,
or connection-presentation decisions.

## Folder Map

### Start Here

- `2026-07-17-intake-triage.md` separates implementation-ready work, strong direction,
  open design questions, research, and possible conflicts with existing
  decisions.

### Discussion Topics

- `discussion-topics/2026-07-17-channel-first-data-model-and-language.md` — a smaller
  mental model for images, channels, masks, one-value controls, and per-pixel
  values; includes the automatic import-split idea.
- `discussion-topics/2026-07-17-graph-rules-and-visual-feedback.md` — reliable connection
  rules, required-channel relationships, visual cues, and the minimum node
  contract needed to support them.
- `discussion-topics/2026-07-17-color-management-and-display-pipeline.md` — why color
  meaning matters, when math can remain permissive, automatic visible setup
  options, and the current View Transform problem.
- `discussion-topics/2026-07-17-alpha-channel-and-math-defaults.md` — preserving alpha by
  default while keeping explicit RGBA math possible.
- `discussion-topics/2026-07-17-compound-nodes-and-decomposition.md` — combining nodes,
  dissolving truthful high-level nodes into subgraphs, canonical math,
  optimized equivalents, and opaque limitations.
- `discussion-topics/2026-07-17-unified-node-library-and-future-growth.md` — replacing the
  legacy layer mindset with one definition system without losing the ability
  to grow a broad creative library.
- `discussion-topics/2026-07-17-operation-scope-and-performance.md` — why pointwise,
  neighborhood, reduction, geometry, global, multi-image, and specialized
  distinctions have execution value rather than being descriptive trivia.

### Research Backlog

- `research-backlog/2026-07-17-naming-and-industry-language.md`
- `research-backlog/2026-07-17-color-management-workflow-research.md`
- `research-backlog/2026-07-17-compound-and-decomposition-research.md`
- `research-backlog/2026-07-18-implementation-readiness-audit.md` — current-code
  audit of what is ready, nearly ready, already present, or still blocked by
  product decisions.

These files preserve research questions and candidate sources. No online
research was needed for this intake pass.

The 2026-07-18 readiness audit found two bounded implementation candidates:
the signed Divide correction and a presentation-only Channel/Mask vocabulary
slice. It does not activate either candidate or Phase 7.

### Implementation-Ready

- `implementation-ready/2026-07-17-signed-divide-correction.md` — the one unambiguous code
  defect selected in this session. It is specified enough to activate as a
  small implementation slice, but no code change is authorized by this intake.

## Suggested Conversation Order

1. Finish the user-facing Channel-role language and propagation rules.
2. Agree on the minimum node and connection-rule contract.
3. Define compound and decomposition honesty classes.
4. Resolve the alpha default and override location.
5. Work through the color import, editing, view, and output experience.
6. Use those answers to plan the legacy-library unification and first public
   node set.
7. Activate only the small implementation slices whose behavior is fully
   specified.

## Related Authority

- `../README.md`
- `../program-contract.md`
- `../decision-register.md`
- `../phase-5-compound-contract-v1.md`
- `../phase-5b-typed-connection-ui-contract.md`
- `../implementation-progress.md`
