# Implementation Progress Ledger

Last updated: July 1, 2026.

## Purpose

This is the small progress file for the RAW Auto Starting Point work. It exists
so an implementation agent can start cold, resume after compaction, or continue
after another pass without rereading every research file or drifting into a
different project.

Keep this file short. Do not turn it into a research note. Update it before
starting an implementation pass and again after finishing that pass.

## Mandatory Use Rule

Before changing code for this feature, read:

```text
AGENTS.md
README.md
agent-reread-guide.md
implementation-progress.md
implementation-contract.md
implementation-pass-readiness.md
```

If the planned code touches readbacks, stats domains, or DNG metadata, also
read `code-web-research-readbacks-and-dng.md`.

At the end of every implementation pass, update the fields below. If a pass is
interrupted, update `Current State`, `Last Completed`, `Next Allowed Work`, and
`Open Blockers` before stopping.

## Current State

```text
Phase: Ready for implementation
Current pass: Pass 0
Status: Not started
Last completed: Research/docs setup through durable entrypoint, active checklist, code readback, and DNG audit
Next allowed work: Pass 0 data model and diagnostics scaffolding
Do not skip to: Applying automatic RAW Exposure, Local Range, Finish Tone, or multi-control recipes
```

## Pass Sequence

| Pass | State | Scope | Completion Rule |
| --- | --- | --- | --- |
| Pass 0 | Next | Add `RawAutoStartPoint`-style data types, stage enums, subscore structs, and diagnostics payloads without changing behavior. | Project compiles; no new automatic image changes occur. |
| Pass 1 | Waiting | Add named stage stats readbacks, starting with pre-View-Transform and final display, then neutral/raw-placement boundaries. | Diagnostics prove which image stage each stat came from. |
| Pass 2 | Waiting | Rename or separate current View Transform-only action as `Fit Display` / `Refit Display`. | Existing safe automation is clearly View Transform-only. |
| Pass 3 | Waiting | Add dry-run `Build Starting Point` candidate report for CurrentFit/Base without applying. | Diagnostics show candidate values, stage stats, and scores. |
| Pass 4 | Waiting | Allow Base mode to apply visible RAW Exposure and Display Fit with undo. | Only visible recipe fields change, with one undo snapshot. |
| Pass 5 | Waiting | Add conservative Balanced Local Range authoring. | One or two visible graph points max, with confidence and reason. |
| Pass 6 | Waiting | Add mild Finish Tone authoring only where visible/editable. | Authored tone changes are exposed or clearly bridged to controls. |
| Pass 7 | Waiting | Tune constants against real RAW validation images. | Constants are based on validation records, not theory alone. |

## Last Completed

Research and documentation are ready enough to begin Pass 0. The docs now
separate visible-control ownership, staged evidence, solver math, code readback
boundaries, display-domain labeling, current DNG parsing/application facts, and
the durable repo-root entrypoint for this work.

## Next Allowed Work

Pass 0 is the next allowed implementation step. It should add structure and
diagnostics only. It should not apply a new automatic result, add the final
button behavior, or make Local Range / Finish Tone edits automatically.

Required Pass 0 outputs:

```text
data types for starting-point stages and candidates
score/subscore structures with inspectable fields
diagnostic serialization or UI-readable payload shape
clear ownership comments tying values to visible recipe fields
no behavior change
```

## Active Pass Checklist

Keep only the active pass checklist here. When a pass completes, move the short
result into `Progress Log`, replace this checklist with the next pass, and do
not carry forward old detailed checklist noise.

```text
Pass: Pass 0 - Data model and diagnostics scaffolding

Entry reads:
- AGENTS.md
- README.md
- agent-reread-guide.md
- implementation-progress.md
- implementation-contract.md
- implementation-pass-readiness.md

Allowed changes:
- Data types for RAW starting-point stages, candidates, subscores, and diagnostics.
- Serialization or UI-readable diagnostics payload shape.
- Ownership comments tying automatic values to visible editable recipe fields.
- Minimal docs/progress updates for the pass.

Forbidden changes:
- Do not apply automatic RAW Exposure, Local Range, Finish Tone, or Display Fit values.
- Do not add the final Build Starting Point button behavior.
- Do not change existing render output or make hidden automatic image corrections.
- Do not skip to Pass 1 readbacks or Pass 4 visible application.

Verification:
- Run .\build.cmd from the repo root.
- Review the diff for behavior-changing call sites.
- Confirm any new diagnostics are inert until later passes wire them in.
- Update this file before starting and after finishing or blocking the pass.
```

## Open Blockers

```text
No blocker for Pass 0.
Pass 1 must inspect current renderer code before adding readbacks.
Pass 4+ require validation confidence before defaulting to stronger edits.
```

## Entrypoint Tracking Note

The repo currently ignores `/AGENTS.md` as local agent configuration in
`.gitignore`. The local file exists in this workspace and is the intended Codex
entrypoint here. If this guidance must become version-controlled later, change
that repository policy deliberately instead of assuming `git status` will show
the file by default.

## Pass Update Template

Copy this block when beginning or finishing a pass. Keep each field short.

```text
Date:
Pass:
State: Planned | Active | Complete | Blocked
Goal:
Files touched:
Behavior changed:
Diagnostics added:
Verification:
Docs updated:
Next allowed work:
Do not do next:
```

## Progress Log

### July 1, 2026 - Progress Ledger Created

```text
Pass: Pre-implementation setup
State: Complete
Goal: Add a small mandatory progress file so future passes can resume safely.
Files touched: implementation-progress.md, README.md, agent-reread-guide.md, implementation-contract.md, implementation-pass-readiness.md
Behavior changed: None
Diagnostics added: None
Verification: ASCII scan passed; routing search found ledger references in README, agent guide, contract, and readiness docs
Docs updated: This file plus folder routing
Next allowed work: Pass 0 data model and diagnostics scaffolding
Do not do next: Do not jump directly to applying multi-control automatic edits
```

### July 1, 2026 - Durable Entry And Checklist Setup

```text
Pass: Pre-implementation setup
State: Complete
Goal: Add a repo-root Codex entrypoint and exact active-pass checklist so future passes can start cold.
Files touched: AGENTS.md, implementation-progress.md, README.md, agent-reread-guide.md
Behavior changed: None
Diagnostics added: None
Verification: ASCII scan found no non-ASCII; routing search found root entrypoint, active checklist, build command, and skip guards; /AGENTS.md ignore behavior documented
Docs updated: Root entrypoint, active pass checklist, cold-start routing
Next allowed work: Pass 0 data model and diagnostics scaffolding
Do not do next: Do not skip to automatic visible recipe application or stage readbacks
```
