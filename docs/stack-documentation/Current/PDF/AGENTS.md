# Stack Manual Workspace Instructions

- Read `README.md` and `STATUS.md` before editing this workspace.
- Treat `source/manual.typ` as the document entry point and
  `source/style.typ` as the shared visual source of truth.
- Treat the research-paper visual system in `README.md` as a hard working rule
  unless the user explicitly directs otherwise. Preserve an explicit departure
  at the narrowest sensible scope and record a manual-wide change in
  `STATUS.md`.
- Use the shared charcoal paper tone on every page, including cover and section
  title pages. Keep ordinary text white. Warm cream is the sole document-level
  accent and must be used sparingly for meaningful structural emphasis,
  formulas, or visual assets; do not introduce other decorative colors.
- Keep the body layout minimal and research-paper-like: use the shared serif
  typography, numbered headings, generous margins, and simple page numbers.
  Do not reintroduce decorative bars, running headers, or page furniture
  without an explicit user request.
- Render every top-level section as a separate shared-charcoal title page with its Roman
  numeral and title centered horizontally and vertically. Suppress all chrome
  on those pages and begin the section's prose on the following page.
- Multiple fonts are permitted only by stable role. New Computer Modern is the
  default for ordinary text; additional families need a specific purpose, such
  as a cover display face, monospaced code, or a visual label. Do not mix fonts
  decoratively. Confirm availability, build, and inspect before adopting a new
  document font.
- Treat the user as the primary author of non-mathematical explanations and
  expressive prose. Preserve their voice and do not fill those passages with
  generic writing unless asked. Codex owns math drafting and checking, and
  should verify source-dependent claims against the actual Stack code when
  needed or requested. Mark verified behavior, intent, inference, and proposal
  as different kinds of claims.
- Keep substantial manual topics in independent files under `source/content/`.
  `source/content/outline.typ` is the only publication-order list: reorder by
  moving include lines there, never by renaming files, hand-editing page or
  chapter numbers, or rewriting unrelated content. Use stable semantic labels
  for cross-references and allow Typst to resolve displayed numbers.
- Keep `source/contents.typ` automatic. Never type table-of-contents entries
  or page numbers by hand; headings included through `outline.typ` are the
  source of truth.
- Before adding or changing a PDF asset, read `assets/README.md` and update
  `assets/ASSET_MANIFEST.tsv`. Preserve original media, reference only the
  manifest's PDF-ready path in Typst, and archive rather than casually delete
  retired assets. GIFs must use a deliberate static poster or frame sequence
  in the PDF.
- Keep the folder layout shallow. Reuse `source/content/` and the existing
  `assets/` categories before creating more folders.
- Follow the filename rules in `README.md`.
- Never hand-edit files under `output/` or `tmp/`; they are generated.
- After every meaningful document change, run `build.cmd` and inspect all PNG
  pages under `tmp/pdfs/pages/` for clipping, overlap, legibility, and visual
  consistency.
- Update `STATUS.md` when scope, decisions, structure, verification, or the
  next handoff changes.
- Do not route ordinary Stack implementation notes into this folder. This
  workspace is for material intended for the manual or its production.
