# Channel-First Discussion Intake Triage

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: update
- Topic: node-math-rewrite
- Verification: partially-verified against current code and Node Math Rewrite contracts

## Implementation-Ready Now

### Signed Divide correction

The current Data Math Divide shader divides by `max(abs(b), epsilon)`, which
discards the sign of a negative denominator. The user explicitly selected this
as a defect that should be fixed. The defect and the intended sign-preserving
guard are already documented by the current audit and research package.

This is ready to become a small, explicitly activated implementation slice. It
still requires the normal pixel-changing definition/version update and tests.

## Strong Direction, But Not Yet An Implementation Contract

- Treat channels as a central user-facing mental model, including independent
  access to alpha.
- Preserve alpha during ordinary full-image edits by default, while offering an
  explicit way to apply math to alpha too.
- Replace the split between legacy layers and newer typed nodes with one
  reliable definition, connection, diagnostic, and test mindset.
- Keep the node library broadly expandable without keeping the old browser
  categories or old implementation structure.
- Give every first-class value reliable rules even before many consumers exist.
- Make high-level nodes inspectable or dissolvable only when their internal
  graph is a truthful definition of their result.
- Use friendly language without hiding or misrepresenting the real math.

Each item needs at least one focused design pass before source edits begin.

## Open Product Questions

- Should an imported image visibly create Channel Split and Channel Combine
  nodes, expose virtual channel pins, or remain one image-bundle node whose
  channels can be expanded on demand?
- What user-facing names should replace or supplement `Scalar` and
  `ScalarField`?
- Should `Mask` remain a top-level data category, or be presented as a
  one-channel value with a mask role and expected range?
- Which node requirements block a connection, and which merely warn?
- How should a node visually declare that it requires an RGB relationship,
  luminance, alpha, neighboring pixels, a complete frame, or several images?
- Should alpha participation be a project rule, a default for newly created
  nodes, a per-node control, separate node variants, or explicit channel
  wiring?
- Should source color preparation be manually authored, visibly auto-authored,
  offered as a template, or handled by a separate source/output workflow?
- What should replace or complete the current View Transform?
- Which high-level nodes are transparent graphs, optimized equivalents,
  inspectable staged algorithms, or opaque specialized operations?
- What are the limits on dissolving a node whose implementation is iterative,
  stateful, external-model driven, frequency-domain, multi-frame, or RAW?

## Existing Decisions Potentially Reopened

These annotations do not automatically revoke prior decisions, but later
conversations may choose to revise them deliberately:

- **NMR-018 / NMR-102:** explicit per-value color meaning, no mandatory hidden
  working space, and untagged sources remain Unknown. Automatic *visible*
  preparation nodes may still be compatible, but require a new product choice.
- **NMR-105:** direct viewport output with no separate preview transform. A
  monitor/display transform proposal must explain whether it changes graph
  pixels or only presents them.
- **NMR-134:** full images cannot connect directly to one-channel inputs and
  Stack does not auto-spawn extractors. The automatic import split idea directly
  revisits this no-auto-spawn boundary.
- **NMR-019 / NMR-104:** alpha association is explicit rather than globally
  forced. A graph-level alpha participation switch must not silently invalidate
  saved node promises.

## Research Needed

- Professional and user-facing language for uniforms, scalar fields, image
  planes, channels, masks, maps, and resources.
- Color-managed source, working, display, and export workflows in professional
  image applications and interchange standards.
- Compound/group/gizmo/subgraph behavior and truthful decomposition models in
  established node systems.

## Deliberately Not Authorized

- No automatic import graph mutation.
- No scalar/field/mask rename.
- No new global alpha switch.
- No automatic color conversion.
- No View Transform rewrite.
- No legacy layer migration.
- No compound-contract change.
- No Phase 7 activation.

## Readiness Reassessment — 2026-07-18

The verified current-code audit is
`research-backlog/2026-07-18-implementation-readiness-audit.md`.

Its result is deliberately narrower than the overall design direction:

- **Ready for explicit activation:** the signed Divide correctness fix.
- **Ready if scoped as presentation-only:** show `ScalarField` as Channel in
  user-facing details and show Mask normally / `Channel · Mask` in detailed
  presentation, without changing types, persistence, connections, or pixels.
- **Nearly ready:** standalone Neutral Channel output and Image Combine rules;
  exact missing-component, presentation-owner, and persistence behavior remain
  open.
- **Not ready:** a durable role schema/propagation system, generic live
  `Value/Channel` ports, alpha-default changes, color/display workflow changes,
  imported-source dissolve behavior, broad decomposition, legacy migration,
  and Phase 7 library expansion.

No implementation slice was activated during the audit.
