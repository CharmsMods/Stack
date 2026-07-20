# Forward-Only Rewrite And Graph Flexibility Decisions

- Captured: 2026-07-15 22:43
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-15-2243-node-math-forward-only-decisions.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-15-2318-node-math-source-viewport-decisions.md`
- Type: update
- Topic: node-math-rewrite
- Verification: user-confirmed direction applied to the canonical program documents

## Confirmed Direction

- The Node Math Rewrite is forward-only. Stack does not need to open or
  reproduce projects created before the rewrite; the archived older executable
  owns those projects.
- Do not create a saved collection of old images/projects or freeze historical
  pixel results. Add automated math, generated in-memory, GPU/reference, build,
  and user visual tests alongside the new or changed behavior.
- Keep the graph permissive. Users may connect ordinary numeric operations in
  any color or alpha state and in any order. Semantic state informs; it does not
  normally restrict or automatically correct.
- Do not impose a graph-wide internal working color space or claim that all
  image data is normalized. Color/transfer state belongs to each value or is
  explicitly `Unknown`; users place conversions wherever they want.
- Fusion must preserve authored dependency and operation order exactly. It may
  combine an ordered chain into one GPU program but may not substitute a fixed
  editing order.
- Preserve independent R, G, B, and A access. Support straight and
  premultiplied alpha explicitly rather than forcing one representation across
  the program.
- RAW nodes remain specialized and nondeconstructible by default. Reconsider
  them only after the main program, one node at a time.
- Real inspectable compound nodes, first-class values, and order-preserving
  fusion match the intended product direction.
- Build the foundation before expanding the public library, and do not treat
  the operation encyclopedia as an implementation checklist.
- Do not hide automatic normalization, color repair, tone mapping, or output
  correction. It is acceptable for values to clip or look unusual when that is
  what the graph produces; show relevant state near the viewport/output.

## Resolved Follow-Up Decisions

1. **Source labeling:** Stack retains an embedded color profile as descriptive
   metadata without changing pixels. An untagged ordinary image enters as
   `Unknown` until the user explicitly assigns a color meaning or converts it.
2. **Viewport presentation:** the main viewport displays the graph's connected
   output directly and has no separate optional preview transform. The viewport
   footer shows the current color state connected to the output.

These user-confirmed decisions close NMR-102 and NMR-105 in
`decision-register.md`.

## Related Docs

- `README.md`
- `program-contract.md`
- `decision-register.md`
- `phase-roadmap.md`
- `implementation-progress.md`
