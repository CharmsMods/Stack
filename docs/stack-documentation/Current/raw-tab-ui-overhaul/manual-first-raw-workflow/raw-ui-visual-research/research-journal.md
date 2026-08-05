# Research Journal

## 2026-07-23 15:01 — Intake and routing

- Archived the user's request under the documentation intake protocol.
- Routed the work to a new sibling folder in the established manual-first RAW
  workstream.
- Confirmed that this pass is research and design synthesis only.
- Began code inspection before web research, as requested.
- Initial code search found a RAW dockspace with Controls/Image/Tone Graphs and
  a separate floating Gallery.
- Initial code search found the Editor tab's fixed, borderless child-pane and
  splitter approach, which is a promising local layout precedent.

## 2026-07-23 15:12 — Current Stack code audit

- Confirmed the RAW layout is a normal dockspace seeded as Controls/Image/Tone
  Graphs at roughly 27%/center/30%, plus a floating Gallery.
- Confirmed `Reset Panels` exists because the main regions are movable docked
  windows.
- Mapped the primary form hierarchy: RAW Pipeline, Base Light, White Balance,
  and additional source/output sections in the left window.
- RAW Exposure and the whole View Transform currently share Base Light, with
  advanced view controls nested one level deeper.
- Local Range and Finish Tone share the right scroll area. Both use expandable
  headers; both graphs have visible outer/inner frames and supporting form
  controls.
- Confirmed the Editor tab's panes are manually positioned borderless children
  with a transparent resize overlay. This can provide fixed roles and
  resizability without docking or seams.
- Recorded a first set of constraints and preservation rules in
  `current-stack-ui-audit.md`.

## 2026-07-23 15:06 — Adobe official-source pass

- Separated Lightroom Classic, current Lightroom desktop, and Camera Raw rather
  than treating them as one UI.
- Lightroom Classic provides a clear spatial hierarchy and persistent
  histogram, but its reorderable accordion stack is the pattern Stack wants to
  move away from.
- Current Lightroom and Camera Raw use a more useful two-level model: a compact
  tool selector chooses the context, then one right-side panel supplies that
  context's controls.
- Adobe keeps image view state, before/after, zoom, and reference close to the
  preview rather than mixing them into tone sections.
- Adobe uses small dots and eye indicators to communicate changed/bypass state
  without another full container.
- Masking shows how direct interaction on the image and a contextual panel can
  temporarily replace the normal global-adjustment surface.
- Added six official Adobe visual references and limitations to the source
  ledger.

## 2026-07-23 15:09 — Resolve official-source and PDF pass

- Downloaded and visually inspected the interface-review pages of Blackmagic's
  official Resolve 20 Colorist Guide using the PDF workflow.
- The labeled default layout is fixed by role: Gallery, Viewer, Node Editor,
  thumbnails, Primaries, Curves, Mini-timeline, and Keyframe/Scopes/Info.
- Optional regions are shown/hidden to redistribute space; at low resolution,
  palette groups merge. This is adaptive layout without arbitrary docking.
- Resolve's palette buttons are compact icon rows. The selected palette is
  named in its content region and every icon exposes its name on hover.
- The default simultaneous Primaries plus Curves layout suggests Stack should
  consider keeping RAW Exposure available while a precision graph is active.
- Resolve keeps comparison, matte/highlight, and palette-specific onscreen
  controls attached to the Viewer.
- Curves are a full palette with a live histogram. Scopes are a dedicated
  switchable measurement palette rather than inline diagnostic prose.
- Added four official Blackmagic references and their limitations to the source
  ledger.

## 2026-07-23 15:12 — Interaction evidence and first synthesis

- Added direct manipulation as the governing principle for graphs and image
  targeting: visible objects, incremental reversible edits, immediate feedback.
- Distinguished progressive disclosure from accordions. Tool switching,
  contextual controls, and one consistent secondary sheet can reveal depth
  without a long expanding page.
- Added an icon-navigation policy: stable order, active tool name, tooltip,
  changed-state cue, accessible name, and keyboard focus.
- Recorded that bare icon visuals should retain at least a practical invisible
  pointer target; minimal styling must not mean pixel-perfect line clicking.
- Used proximity/alignment as the default grouping language and reserved frames
  for unusually strong separation.
- Synthesized four layout directions. Canvas + Precision Rail is the
  recommendation; Resolve-style Lower Workbench, Full-Canvas Floating Shelf,
  and Two Fixed Precision Columns remain useful alternatives or responsive
  states.
- Defined a shared frameless graph language and tool-specific ideas for Local,
  Finish Tone, and View Transform.

## 2026-07-23 15:12 — Research-pass closeout

- Marked the workspace partially verified: current Stack claims were checked in
  code, product-layout claims use official sources, and Stack-specific layout
  recommendations remain design hypotheses.
- No Stack UI implementation was performed.
- The next useful artifact is an annotated three-width wireframe, followed by a
  user decision among the recommended and alternative layouts.

## 2026-07-23 — RAW Lab architecture follow-up

- Inspected the root-tab descriptors and RAW lifecycle checks in `AppShell`.
- Confirmed that RAW workspace, recipe, preview, persistence, and graph state
  are centrally owned by one `EditorModule`; a second presentation does not
  need a second backend.
- Recommended a temporary top-level `RAW Lab` tab that shares the active RAW
  session with the original RAW tab.
- Identified the main lifecycle requirement: replace single-tab checks with a
  RAW-family predicate so RAW ↔ RAW Lab does not unload/reload the workspace.
- Recommended intentionally limiting the first Lab surface to Light, Zones,
  Tone, and View while preserving all hidden recipe values.
- Recorded a phased extraction strategy that shares edit state and graph math
  without importing or copying the existing long-panel presentation.
