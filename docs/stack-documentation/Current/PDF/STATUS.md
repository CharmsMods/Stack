# Stack Manual Status

Last updated: 2026-07-13

## Current State

- The authored Introduction, Math foundations, and Overview are in progress.
  Math covers visible pixels, RGB subpixels, 8-bit RGB values, alpha
  compositing, a pixel-to-operation bridge, and its first three
  transform-catalog categories. Overview now introduces the node graph, a
  simple Gaussian-blur chain, mask inputs, and a larger graph project.
- Typst is pinned to `0.14.2` and bootstrapped by `build.cmd`.
- The source structure exercises modular content, dynamic contents, page
  furniture, and PDF/PNG output.
- The asset library uses a manifest-led workflow for images, screenshots,
  diagrams, GIF sources, and PDF-ready derivatives.
- Generated files are intentionally ignored.

## Latest Verification

- `build.cmd` completed successfully after the first Overview pass. The latest
  PDF is a seventeen-page, approximately 5.2 MiB document; its final graph page
  is landscape US Letter and the remaining pages are portrait US Letter. All
  seventeen rendered pages were visually inspected for crop, contrast,
  alignment, legibility, and page flow.
- `build.cmd` completed successfully on Windows with Typst `0.14.2`.
- The document assembled successfully through `source/content/outline.typ`.
- Output is a tagged, unencrypted, seventeen-page PDF with title and author
  metadata.
- The title page, generated contents page, and both section-opening pages were
  visually inspected after the structural pass. The contents entries and page
  numbers correctly follow the assembled section order.
- The charcoal-paper, white-text, and cream-accent palette was visually
  inspected across the document. Pixel checks confirm a consistent `#343434`
  background on every page.
- The minimal paper-style layout was visually inspected across the document:
  serif type, generous margins, numbered section headings, and page numbers
  only, with no decorative rules or running headers.
- Roman-numeral section title pages have been selected for implementation;
  the temporary header-study page is removed from the assembled PDF.
- The centered Roman-numeral title pages and matching contents entries were
  visually inspected across the rebuilt PDF.
- The Introduction's empty-editor figure, two-lane processing diagram,
  side-by-side render comparison, and order algebra were visually inspected.
  The two comparison labels remain text-only below the supplied images. Its
  arithmetic pair is vertically arranged as `(2 + 3) * 4 = 20` and
  `2 + 3 * 4 = 14`; there are no duplicate operation labels.
- The processing diagram is conceptual rather than a source-verified Stack
  model. It contains the two orders `Image -> Contrast -> Brightness -> Result
  A` and `Image -> Brightness -> Contrast -> Result B`; saturation has been
  removed from the Introduction.
- The transition from the editor figure through the processing diagram to the
  phone-editing paragraph uses local spacing rather than changing the global
  document rhythm; the two diagram rows have additional separation as well.
- The two comparison originals are preserved, while the PDF uses matching
  1200x1800 PNG derivatives to keep the generated PDF responsive.
- The Math foundations visually inspect cleanly: the native pixel-field and
  RGB-subpixel visualizations are sharp, the 2x2 RGB samples retain their 16:9
  ratio and labels, and the paired alpha examples make transparent and opaque
  backgrounds visibly distinct.
- The Math catalog now has a dedicated pixel-to-operation bridge page followed
  by one two-column page per category for fundamental per-pixel math, channel
  operations, and tone/brightness operations. Its borderless
  operation/formula/use layout remains legible at compact text scale; formulas
  do not exceed body-text size.
- The nested Math contents entries are deliberately unnumbered. Roman numerals
  remain reserved for top-level sections and their centred title pages.
- The three current double-column catalog pages were visually rebalanced with
  per-column row spacing. Their final entries now finish close to the lower
  usable margin, while formulas and body text remain at the established compact
  scale.
- Overview is the third top-level section and appears as `III` on its centered
  title page. Its unnumbered contents entries link to The node graph, A simple
  adjustment chain, Mask inputs, and A graph at scale.
- The Overview pages were visually inspected with their supplied Stack captures:
  the simple blur chain, matching before/after renders, a linear-gradient mask
  example, and the wide complex graph. The final complex-graph page uses a
  landscape layout so the graph remains readable.
- The before/after and complex-graph originals are preserved. Cropped,
  downsampled PDF-ready derivatives are recorded in
  `assets/ASSET_MANIFEST.tsv` to keep the document responsive.
- The Overview's image-versus-mask socket wording was checked against the
  current `EditorNodeGraphDefinitions.cpp` and `EditorNodeGraph.cpp`
  implementation. The opening node-graph visual currently uses the supplied
  simple blur chain because the asset folder does not yet contain a distinct
  full editor-tab capture with annotations and an output connection.

## Next Handoff

1. Replace the opening Overview visual when the user supplies a distinct full
   editor-tab screenshot with annotations and an output connection; the figure
   can be swapped without restructuring the section.
2. Continue author-led Overview writing in
   `source/content/overview-node-graph.typ`, or add further Overview subsections
   as independent files included by `source/content/overview.typ`.
3. Continue the transform catalog from the Node Math Rewrite research library
   under `docs/stack-documentation/Current/ideas/general/Node Math Re-Write/05-research/image-operations/`,
   beginning with color-grading operations when requested. Keep one category
   per compact two-column page, with each operation's name, equation, and purpose.
4. Add future top-level sections through `source/content/outline.typ`.
5. Keep future top-level section files headed with a labelled level-one title;
   the shared style renders their Roman-numeral title pages automatically.
6. Register every asset in `assets/ASSET_MANIFEST.tsv` before its first PDF use.

## Defaults That Are Easy to Change

- Page size: US Letter.
- Body and heading type: New Computer Modern.
- Math type: New Computer Modern Math.
- Palette: every page, including cover and section title pages, uses charcoal
  (`#343434`); ordinary text is white; warm cream (`#f1dec0`) is the only
  document-level accent.
- Layout: minimal research-paper treatment with New Computer Modern, generous
  margins, centered page numbers, and Roman-numeral top-level title pages.
- Visual system: a hard working rule unless the user explicitly requests a
  scoped or manual-wide departure.
- Fonts: multiple families may be used only by stable purpose; body, heading,
  contents, captions, and page numbers remain New Computer Modern by default.
- Display interlude: `Bodoni MT Poster` is used only for the requested Why not
  change that? line in the Introduction; it is not a body-text font.
- Visual direction: research-paper oriented, with explicit user descriptions
  taking precedence at the appropriate scope.
- Authorship: the user writes most non-mathematical explanations and expressive
  passages; Codex handles math drafting/checking and verifies source-dependent
  claims against current Stack code when needed or requested.
- Assembly: independent topic files are published and reordered only through
  `source/content/outline.typ`; filenames and labels remain stable.
- Assets: preserve originals, record stable IDs in `assets/ASSET_MANIFEST.tsv`,
  and use only manifest-recorded PDF-ready files in the document.
- Generated PDF is not committed.
- Catalog notation: $x$ and $y$ normally mean an input and output channel
  value; $p$ means an RGB vector. When the source table describes a family of
  methods rather than one universal equation, use a compact named transform and
  state that constraint in the entry's purpose rather than inventing a false
  universal formula.

## Open Decisions

- Manual audience and depth: user guide, technical reference, design book, or
  a layered combination.
- Overall chapter order and whether explanations, design direction, and math
  are integrated or separated into parts.
- Citation/source policy for technical and mathematical claims.
- Final page size and print requirements.
