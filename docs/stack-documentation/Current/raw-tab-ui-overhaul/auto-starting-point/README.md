# RAW Auto Starting Point Research

## Purpose

This folder keeps the automatic/foundational RAW control research separate from
the broader RAW side-panel redesign notes. It is the place to reread before
making UI or implementation decisions about:

- RAW Exposure.
- Local Exposure / Local Range.
- Finish Tone / Tone Graph.
- Display Fit / View Transform.
- Automatic suggestions that write into those visible controls.
- Raw-domain processing that constrains automatic visible controls without
  becoming a hidden edit pass.

## If You Open This Folder Cold

The repo-root `AGENTS.md` is the automatic Codex entrypoint for this RAW work.
If you reached this folder from that file, continue here. If you opened this
folder directly, start here, then immediately open `agent-reread-guide.md`. Do
not begin by reading the longest research files from top to bottom unless the
task really requires it.

Current resume point:

```text
The active RAW Starting Point state is in implementation-progress.md.
Pass 9 code exists, but Pass 10 product alignment corrected the interpretation:
the earlier native RAW tab was still a multi-action Starting Point assist
panel, not the accepted one-click RAW base experience. The first Pass 10 UI
slice now presents Build Starting Point as the primary full-width action, keeps
Analyze/Fit/Undo/Diagnostics as secondary tools, and moves Add Local Range and
Add Mild Tone behind an Advanced disclosure.

This is still not validated real-image readiness and not final interaction
design. Solver behavior and constants are unchanged. Next RAW work should
continue with the one-click solve specification, deeper image-processing
research where needed, manual acceptance on representative RAW files, real
validation records, and editor-state tests before widening behavior or tuning
constants.
```

From the repo root, the intended flow is:

```text
AGENTS.md when starting from the repo root
-> implementation-progress.md
-> README.md
-> agent-reread-guide.md
-> implementation-contract.md when implementation rules matter
-> implementation-pass-readiness.md when code changes are planned
-> human-workflow-notes.md
-> one task-specific long file
```

From this folder directly, read `README.md`, then `agent-reread-guide.md`, then
`implementation-progress.md` before any implementation or resume work.

If the task involves code changes, also read `implementation-progress.md` and
`implementation-pass-readiness.md` before editing. If the task is only a
user-facing explanation, prefer `human-workflow-notes.md` first and use the
longer research files only to check the technical basis.

When returning to this folder to look something up, use
`agent-reread-guide.md` as the router. It tells you which document owns naming,
state ownership, staged sampling, solver math, raw safety, validation gaps, and
implementation sequencing.

## Files

- `agent-reread-guide.md`: the restart guide for future agents after context
  compaction or uncertainty, including task-specific reread paths.
- `human-workflow-notes.md`: the canonical beginner-facing explanation of the
  four controls, their order, and preferred UI language.
- `one-click-starting-point-alignment.md`: the product-alignment correction
  that separates the current multi-action scaffold from the desired one-click
  Starting Point behavior.
- `implementation-contract.md`: the compact implementation guardrails for
  visible automatic writes, stage evidence, ownership, and recompute timing.
- `implementation-progress.md`: the compact active pass ledger. Read and
  update it before and after implementation passes so work can resume from any
  point without drifting. It owns the current pass, next allowed work, active
  stop rules, and verification commands.
- `implementation-pass-readiness.md`: the implementation-readiness gap closure
  note, including normalized score formulas, validation protocol, and the
  historical Pass 0-7 plan. Use it as reference only; do not restart that plan
  when `implementation-progress.md` records later completed work.
- `code-web-research-readbacks-and-dng.md`: the current code and standards
  snapshot for readback boundaries, display-domain labels, and DNG metadata /
  gain-map handling.
- `auto-controls-ordering-research.md`: the broad naming, ordering, and mental
  model research for automatic RAW controls.
- `auto-manual-compute-model.md`: ownership and recompute timing rules for
  automatic, suggested, advisory, manual, live, settled, and on-request states.
- `auto-starting-point-sampling-design.md`: the staged sampling and
  implementation proposal for a one-click starting point button.
- `auto-starting-point-gap-audit.md`: what was still missing after the first
  ordering/sampling research pass.
- `auto-starting-point-solver-research.md`: cited algorithm and math notes for
  deciding starting values in RAW Exposure, Local Range, Finish Tone, and View
  Transform.
- `auto-raw-processing-math-and-science.md`: deeper raw-processing math for
  automatic decisions before and after demosaicing, including raw safety,
  scene-linear measurement, candidate scoring, and current STACK hooks.
- `iterative-raw-solver-research/README.md`: entrypoint for the next-generation
  research-only workstream on sensor evidence, multiscale/perceptual features,
  constrained objectives, render-in-the-loop optimization, convergence, and
  validation. It does not authorize implementation or replace the active pass.
- `iterative-raw-solver-phases/README.md`: gated research-to-production program
  for the future precise solver. It maps the research into Phase 00-07 entry
  conditions, checkpoint evidence, non-mutating prototypes, visible one-click
  integration, product completion, and the final scope boundary. Its existence
  does not activate a phase; `implementation-progress.md` remains authoritative.

## Reading Style

The folder intentionally mixes two documentation styles. Research notes should
be prose-first so the reasoning can be read in order without juggling dozens of
bullets. Implementation-facing docs may keep tables, formulas, and short
checklists where precision and scanning matter. When updating this folder, do
not turn every paragraph into bullets, and do not bury implementation contracts
inside long prose.

## Reread Order

For normal implementation work or after context compaction, start with the small
canonical set:

```text
README.md
agent-reread-guide.md
implementation-progress.md for implementation/resume work
human-workflow-notes.md
implementation-contract.md
```

Then open the longer research file that matches the task. Use
`agent-reread-guide.md` as the routing table.

When beginning actual implementation, also read
`implementation-progress.md` and `implementation-pass-readiness.md` before
editing code.

## Current Working Thesis

STACK should support a one-click **Build Starting Point** action, but it should
not be a hidden image process. It should render or analyze candidate stages,
choose visible recipe values, and write those values into the same controls the
user can manually edit afterward.

The core beginner workflow remains:

`Fit to see -> expose the scene -> fix regions -> shape contrast -> refit display`

The target automatic experience is one primary `Build Starting Point` click
that may internally analyze, render candidates, apply safe visible values, and
summarize withheld controls. Separate Local Range or Finish Tone automatic
buttons are current scaffolding, not the desired first-run base workflow.

## Scope Across Updates

This folder is not documenting a one-pass fix or a narrow MVP. It describes the
target architecture and the guardrails that should hold across a series of
implementation updates. When a file says "first implementation," "early pass,"
or "Base mode," read that as sequencing: build the safest observable foundation
first, then continue toward the fuller Starting Point system through later
passes.

Early-pass advice must not erase the broader design. The long-term target still
requires staged raw safety evidence, scene-linear candidate evidence, visible
manual recipe writes, validation records, and later expansion into conservative
Local Range and Finish Tone authoring when the UI can expose those edits
honestly.
