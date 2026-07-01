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
- `implementation-contract.md`: the compact implementation guardrails for
  visible automatic writes, stage evidence, ownership, and recompute timing.
- `implementation-progress.md`: the compact active pass ledger. Read and
  update it before and after implementation passes so work can resume from any
  point without drifting. It owns the current pass, next allowed work, active
  checklist, and verification commands.
- `implementation-pass-readiness.md`: the implementation-readiness gap closure
  note, including normalized score formulas, validation protocol, and a
  multi-pass implementation plan.
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
