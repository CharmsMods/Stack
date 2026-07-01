# RAW Workspace Mode Contract

This document defines ownership. Use it whenever the RAW tab and Editor graph interact.

## Current V1 Notes

As of 2026-06-29, Phase 7 Managed Decomposition V1 is intentionally conservative:

- No flexible/reorderable stage slots are supported yet. The contract allows them only after a later pass expands recipe serialization, UI explanation, and validation together.
- Managed graph validation rejects attempted stage-order metadata or graph-link reordering instead of treating the graph as RAW-tab editable.
- Graph-quality Finish Tone and View Transform state can round-trip through managed Tone Curve and View Transform nodes when validation passes.
- Active/non-default Local Range or legacy `localExposure` recipes are not representable as managed decomposed graphs until managed stage mappings and exact round-trip validator support exist.
- Native/manual validation of the interactive mode transitions is still pending. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

## Modes

### Preview-Only Selection

The user has selected a RAW file from the folder gallery, but no image-affecting edit has happened.

- No per-image `.stack` project is created.
- Browser/gallery tiles may show neutral thumbnails or placeholders.
- The active preview should stage a neutral same-source RAW render when available; that live preview staging does not create a project or leave preview-only mode.
- Ratings, flags, labels, and selection do not leave this mode.
- First image-affecting edit creates a project and enters recipe-backed mode.

### Recipe-Backed Mode

The normal edited RAW mode.

- The per-image `.stack` project exists.
- The RAW development recipe is the source for supported RAW development controls.
- The Editor graph consumes the recipe through a compact `RAW Development` node by default.
- RAW tab controls are editable.
- Downstream graph edits happen after the compact node.

### Managed Decomposed Mode

The user intentionally decomposed the recipe into graph nodes using `Decompose To Nodes`.

- The graph has a clearly identified managed RAW section.
- The RAW recipe and managed graph section represent the same supported pipeline.
- RAW tab controls and managed node parameters sync both directions.
- Flexible supported stages may be reordered only inside validated slots. V1 currently defines no reorderable slots.
- Structural validation must pass after graph edits.

### Custom Graph Mode

The managed RAW graph section no longer matches Stack's supported RAW-chain contract, or the user explicitly detached it.

- The custom graph section owns the rendered RAW result.
- RAW tab editing controls are read-only for that image.
- The RAW tab can still show preview, status, and the reason editing is disabled.
- `Repair RAW Chain` can restore mechanical validity when possible.
- `Re-adopt Graph As RAW Recipe` is allowed only when the graph validates cleanly.
- `Detach From RAW Tab` is allowed when the user intentionally wants full graph ownership.

### Expert Graph-First Work

Experts may start with a RAW file directly in the graph instead of entering through the RAW workspace folder flow.

- If Stack can validate/adopt the graph chain into the supported RAW-chain contract, the image can enter managed decomposed mode and sync with the RAW tab.
- If Stack cannot validate/adopt the graph chain, the graph remains custom graph work and the RAW tab should be read-only or unavailable for those RAW controls.
- Graph-first support must not require the RAW tab to understand arbitrary node graphs.

## Transitions

- Preview-only -> Recipe-backed: first image-affecting edit.
- Recipe-backed -> Managed decomposed: user runs `Decompose To Nodes`.
- Recipe-backed with unsupported active recipe fields, including active/non-default Local Range or legacy `localExposure`, -> Managed decomposed: blocked until those fields can round-trip exactly through managed graph nodes.
- Graph-first valid chain -> Managed decomposed: user runs an explicit adopt/import action and validation passes.
- Graph-first arbitrary chain -> Custom graph: user keeps graph ownership or validation fails.
- Managed decomposed -> Recipe-backed: explicit `Collapse To Compact RAW Development` or explicit `Re-adopt And Collapse` action after validation passes. This must not happen silently as a side effect of ordinary graph edits.
- Managed decomposed -> Custom graph: validation fails or user detaches.
- Custom graph -> Managed decomposed: user repairs or re-adopts and validation passes.
- Custom graph -> Recipe-backed: only through an explicit, validated re-adoption path that also collapses to compact recipe mode.

## Re-Adopt Vs Collapse

`Re-adopt Graph As RAW Recipe` means Stack validates the current graph section and updates the recipe from it. Re-adoption may return a custom graph to managed decomposed mode.

`Collapse To Compact RAW Development` means Stack replaces a valid managed graph section with the compact `RAW Development` node while preserving the recipe and downstream graph connections.

If the UI combines these, the action text must say so clearly, for example `Re-adopt And Collapse To Compact`.

## Required Read-Only Message

Use this exact meaning, with wording close to:

```text
This RAW chain has been customized in the graph.
RAW tab editing is read-only for this image until the chain is repaired or re-adopted.
```

## First Edit Definition

An image-affecting edit includes:

- Slider/control changes.
- Crop selection.
- Rotation.
- Any rendered-image adjustment in the RAW tab.

These are not image-affecting edits:

- Selecting an image.
- Scanning folders.
- Generating thumbnails.
- Rating, flagging, or labeling.
- Switching gallery display mode.
