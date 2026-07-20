# Stack Manual Workspace

This folder is the long-lived authoring workspace for the Stack manual PDF. It
is intentionally small: editable Typst source, media assets, build tooling,
generated output, and temporary page previews.

The manual's title, automatic contents page, and initial section openings are
in place. Its substantive prose, technical explanations, math, and visuals are
still to be authored.

## Quick Start (Windows)

From this folder, run:

```bat
build.cmd
```

The first build downloads the pinned Typst compiler (`0.14.2`) from the
official Typst GitHub release, verifies its SHA-256 checksum, and stores it in
the ignored `.tools/` folder. Later builds are offline and fast.

Outputs:

- PDF: `output/pdf/stack-manual.pdf`
- rendered review pages: `tmp/pdfs/pages/page-*.png`

Useful commands:

```bat
build.cmd -Open
build.cmd -Watch
build.cmd -Clean
build.cmd -NoPreview
```

- `-Open` opens the PDF after a successful build.
- `-Watch` recompiles the PDF whenever source or assets change. Run a normal
  build afterward to refresh review PNGs.
- `-Clean` removes generated PDF and preview files, but keeps the compiler.
- `-NoPreview` skips PNG page rendering for a quicker draft build.

## Where Things Go

| Path | Purpose |
| --- | --- |
| `source/manual.typ` | Single document entry point, cover, and global setup. |
| `source/style.typ` | Shared page, type, color, and component rules. |
| `source/content/outline.typ` | The only file that controls publication order. |
| `source/content/` | Independent manual chapters and appendices. |
| `assets/images/` | Render comparisons and other non-UI raster images. |
| `assets/screenshots/` | Stack UI screenshots. |
| `assets/diagrams/` | SVG diagrams and large visual explanations. |
| `assets/animations/` | Original GIFs plus their PDF-ready posters or frame sequences. |
| `assets/ASSET_MANIFEST.tsv` | Stable asset IDs, source/PDF paths, usage, alt text, and provenance. |
| `scripts/` | Build implementation; most users only run `build.cmd`. |
| `output/pdf/` | Generated final PDF. Do not edit it directly. |
| `tmp/pdfs/` | Generated page previews used for visual review. |
| `STATUS.md` | Short cross-conversation handoff and open decisions. |

Do not add a new top-level folder unless the current layout has become
genuinely ambiguous. Prefer a few well-named files over deeper nesting.

## Adding or Editing Content

1. Add a self-contained topic file under `source/content/`.
2. Add one include line to `source/content/outline.typ` when the topic should
   appear in the assembled PDF.
3. Add media under the closest `assets/` folder.
4. Add or update its row in `assets/ASSET_MANIFEST.tsv`.
5. Refer to assets with project-root paths, for example:

   ```typst
   #image("/assets/screenshots/editor-node-browser.png", width: 100%)
   ```

6. Run `build.cmd` and inspect every PNG in `tmp/pdfs/pages/` after meaningful
   layout changes.
7. Update `STATUS.md` when a pass changes scope, structure, decisions, or the
   next handoff.

## Independent Writing and Reorder-Safe Assembly

Write each substantial topic, chapter, appendix, or long visual explanation in
its own file under `source/content/`. The file's name is its stable identity,
not its current place in the book. You can therefore write several distant
parts of the manual at once, and another author or agent can work in a
different topic file without either person needing to move or rewrite prose.

`source/content/outline.typ` is the single, short publication-order list. To
put twenty finished pages between two existing chapters, create or finish the
new topic files and add their include lines between the two existing lines in
that file. Do not rename, renumber, or rewrite the surrounding chapters merely
because their order changed.

`source/contents.typ` renders the table of contents from the assembled heading
structure. Do not type section entries or page numbers into that file. Give
each publishable top-level section a labelled level-one heading and include it
through `outline.typ`; Typst then keeps the contents order, chapter numbers,
and page numbers current.

Use these rules:

- Give normal content files stable, topic-based names such as
  `editor-workspace.typ`, `render-comparisons.typ`, or
  `display-transform-math.typ`.
- Keep each file locally understandable: its heading, prose, figures,
  equations, and imports should live together. Do not make one chapter depend
  on the neighboring chapter's filename or position.
- Use stable semantic labels, never page numbers or chapter numbers, for
  references: `<ch-editor-workspace>`, `<fig-node-browser>`, and
  `<eq-display-transform>`. Refer to those labels in prose rather than writing
  "see page 37" or "as described in Chapter 4."
- Let Typst calculate heading, figure, table, and page numbers from the final
  assembly order. Never hand-edit numbers after inserting or moving material.
- Avoid manual page breaks solely to preserve a temporary page position. Use a
  page break only when a new major division genuinely needs one.
- A draft can remain outside `outline.typ` while it is being written. It cannot
  affect the assembled PDF until it is deliberately added to the outline.
- Keep `outline.typ` as a simple ordered list of includes. Do not put chapter
  prose, equations, or layout rules there.
- Keep `contents.typ` as an automatic outline, not a second manual list of
  entries. The outline is the source of order; the contents page is its view.

This arrangement makes reordering a small editorial decision instead of a
document-wide rewrite. A later chapter may change its displayed number after a
reorder, but its filename, labels, cross-references, and content remain stable.

## Design Rules and User Intent

Unless the user explicitly directs otherwise, the following is the manual's
required visual system. Treat it as a hard working rule, not a loose aesthetic
suggestion: new pages must look like they belong in the same restrained,
research-paper-oriented document.

- Use the shared New Computer Modern page system with generous print-like
  margins and a simple centered page number.
- Give every top-level section its own shared-charcoal title page: its Roman numeral and
  title are centered both horizontally and vertically, with no running chrome
  or page number. Section prose begins on the following page.
- Keep hierarchy typographic: size, weight, spacing, numbering, captions,
  equations, and cross-references should do the work.
- Do not add decorative bars, running headers, boxed callouts, ornamental
  dividers, or other page furniture merely to make a page feel designed.
- Keep the contents page automatic and structurally accurate.
- Keep figures, tables, equations, and sources clear enough to be read as
  research material, even when their subject is visual or creative.

Explicit user direction remains authoritative. Apply a requested departure at
the narrowest sensible scope, such as one figure, page, section, or edition.
Only change the whole document system when the user clearly asks for a
manual-wide shift; then update `source/style.typ` and `STATUS.md` together.

### Typography and Multiple Fonts

Multiple font families are allowed, but every family must have a stable job.
New Computer Modern remains the default for body text, headings, contents,
captions, and page numbers. Its companion math face should serve equations.

Additional fonts may be used only for a clear role, such as:

- a distinct cover or title-page display face;
- a monospaced face for code, file paths, commands, or UI identifiers;
- labels embedded in diagrams or screenshots when that label needs to match
  the visual system being explained; or
- an explicitly requested editorial or section-level treatment.

Do not introduce a font only for decoration, switch fonts inside ordinary
prose without meaning, or give several fonts competing roles. Before adopting
a new document font, confirm it is available to Typst, state its purpose in
the relevant source or `STATUS.md`, build the PDF, and inspect the rendered
pages. Keep the number of active text families as small as the roles allow.

The working palette is deliberately restrained:

- Every page, including the cover and top-level section title pages, uses the
  shared charcoal paper tone (`#343434`).
- Ordinary body text, headings, captions, contents entries, and page numbers
  are white.
- Warm cream (`#f1dec0`) is the only document-level accent. Use it sparingly
  for meaningful structural emphasis, such as a Roman section numeral, or
  inside formulas and visual assets when it carries information.
- Do not introduce other decorative colors into surrounding text, rules,
  borders, or page furniture.

These rules do not prevent the user from describing a different tone, density,
layout, typography, visual storytelling, or structure in ordinary language.
They also do not make the manual cramped or limit it to conventional journal
pages: large diagrams, render comparisons, UI screenshots, and visual
explanations remain first-class content.

When a visual request is ambiguous and would materially change the result,
show or describe the reasonable interpretations and let the user choose. Do
not convert a flexible preference into a permanent rule without clear intent.

## Authorship and Verification Roles

The user is the primary author of the manual's non-mathematical explanations
and expressive writing. This includes the narrative voice, intuitive
explanations, opinions, design philosophy, creative direction, motivation,
examples from experience, and other passages where personal wording and intent
matter. Codex should preserve that voice, help organize or lightly edit it when
useful, and avoid silently replacing it with generic prose.

This is a preferred collaboration model, not a prohibition. The user may ask
Codex to draft, rewrite, expand, or propose expressive prose at any time. When
that has not been requested, Codex should leave room for the user's writing and
use prompts, outlines, placeholders, or focused questions instead of assuming
authorship of those passages.

Codex is responsible for the technical support around that writing:

- Draft, typeset, and check equations, derivations, notation, variable
  definitions, units, domains, assumptions, constraints, and boundary cases.
- Check mathematical consistency, dimensional consistency, and agreement
  between equations, diagrams, examples, and surrounding claims.
- Explain proposed math clearly enough for the user to review, revise, or
  supply the surrounding plain-language interpretation.
- When a manual claim depends on Stack's current implementation, inspect the
  actual repository source code when verification is needed or the user asks
  for it. Do not rely on memory or an older documentation claim as a substitute
  for current code.
- Distinguish clearly between verified current behavior, intended design,
  informed inference, and an unverified proposal.
- Preserve useful source paths, symbols, and line references in working notes
  so technical claims can be checked again as Stack changes.

Code verification should be proportional to the claim. Verify implementation
details, algorithms, parameter behavior, pipeline order, UI behavior, defaults,
and other source-dependent statements when they materially affect accuracy.
Purely expressive passages do not need code inspection unless the user asks.

## File Naming

Keep names readable in File Explorer and easy to type:

- Use lowercase kebab-case: `node-graph-basics.typ`.
- Do not encode a chapter's reading order in its filename. Publication order
  belongs only in `source/content/outline.typ`.
- Do not put versions in working filenames. Git records versions; avoid names
  such as `final-v2-new.typ`.
- Add a date only when time is part of the meaning, especially UI captures:
  `develop-panel-auto-state-2026-07-12.png`.
- Name comparisons by subject and state:
  `glass-sphere-before-tone-map.png` and
  `glass-sphere-after-tone-map.png`.
- Name UI screenshots by area and state:
  `editor-node-browser-search-open.png`.
- Use stable, descriptive Typst labels such as `<fig-editor-node-browser>` and
  `<eq-display-transform>`.

## Visual and Math Guidance

- Prefer SVG for diagrams, pipelines, plots, and other large explanatory
  visuals. It stays sharp at any zoom level.
- Keep screenshots lossless (usually PNG). Capture at least twice the intended
  printed dimensions when practical.
- Keep related comparison images at the same pixel dimensions and crop.
- Use a full page for dense diagrams instead of shrinking labels until they are
  difficult to read.
- Keep document chrome grayscale. Reserve non-grayscale color for formulas and
  visual assets where it carries information or clarifies a comparison.
- Write equations in Typst math rather than baking them into images. Define
  each symbol immediately before or after the equation.
- Keep source or capture notes near the relevant content until a formal
  bibliography/source ledger is introduced.

## Toolchain Decision

Typst is pinned because it gives this manual strong math, vector and raster
image support, reusable layout components, cross-references, and fast PDF
builds without a large LaTeX installation. The Windows bootstrap is local to
this workspace and does not alter the user's system PATH.

The initial page format is US Letter. The visual baseline is a restrained,
research-paper-oriented Stack style, deliberately centralized in
`source/style.typ`. It remains adaptable to explicit user direction at the
manual, chapter, page, or visual level.
