# RAW UI Visual Research

- Captured: 2026-07-23 15:01
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-23-1501-raw-ui-visual-redesign-research.md`
- Type: research
- Topic: raw-ui-visual-research
- Verification: partially-verified

## Purpose

This folder is the working record for redesigning Stack's RAW tab as a mostly
fixed, resizable, seamless workspace. It records the current implementation
before comparing it with Adobe photo-editing surfaces and DaVinci Resolve's
Color page. This pass is research only and does not authorize UI implementation.

## User Direction

- Replace the movable/dockable RAW workspace with a mostly fixed layout.
- Keep major regions resizable without allowing them to be rearranged.
- Make adjacent regions read as one continuous surface without window seams.
- Prefer compact icon rows and direct graph interaction over a long inspector
  made from expandable sections, dropdowns, checkboxes, and stacked sliders.
- Keep dropdowns only where choosing one item from a real set is the clearest
  interaction; avoid using dropdown-like disclosure sections as navigation.
- Reduce borders, boxes, separators, button containers, and graph frames.
- Focus first on Local Exposure, Global Exposure, Tone Curve, and Display
  Transform. Other existing controls may be reconsidered later.
- Use Adobe and DaVinci Resolve as design research, not as templates to clone.

## Working Files

- `current-stack-ui-audit.md` — what Stack renders and how its layout works.
- `adobe-layout-research.md` — Lightroom and Camera Raw findings.
- `davinci-resolve-layout-research.md` — Color page findings.
- `interaction-design-evidence.md` — direct-manipulation, disclosure, icon, and
  grouping guardrails.
- `design-synthesis.md` — cross-product patterns and Stack layout directions.
- `prototype-tab-strategy.md` — how to build a separate RAW Lab presentation
  without duplicating RAW state or processing.
- `source-ledger.md` — research URLs, source quality, and claims supported.
- `research-journal.md` — compact chronological checkpoints for continuity.

## Status

The first current-code, Adobe, Resolve, and interaction-design passes are
complete. The synthesis recommends Canvas + Precision Rail as the strongest
default direction, with two fixed-workbench alternatives for comparison. A
temporary top-level RAW Lab tab is the recommended prototyping boundary.
