# Documentation Reorg Tracker

This folder tracks the first pass of consolidating Stack's documentation into
one managed documentation home.

## Goal

- Locate every real documentation file in the repo.
- Review documentation in small, safe batches.
- Verify documents against the current code when needed.
- Decide whether each document belongs in `Current`, `Info`, or `Archived`.
- Avoid moving or deleting files before their batch has been reviewed.

## Working Destination Definitions

- `Current`: active plans, implementation notes, handoffs, and documents that
  describe work still in progress or expected next steps.
- `Info`: reference material, research, technical notes, audits, math, and
  supporting background that is useful but is not itself the active instruction
  set.
- `Archived`: completed phases, superseded plans, outdated notes, and
  historical material we want to retain without treating it as current
  guidance.

## Snapshot

- Audit date: `2026-07-03`
- Documentation files mapped in the tracked inventory: `136`
- Current documentation directories in `source-map.md`: `32`
- Completed so far: source discovery, tracker setup, and `B01` through `B18` review/classification
- Move phase has started: fourteen move or mirror subsets were applied. The mapped documentation corpus is consolidated under `docs/stack-documentation/`, while repo-root operational docs are mirrored there, instruction files remain intentional keep-in-place exceptions, excluded website assets are recorded separately, and the reviewed `_local_archive` documentation has now been rehomed into the consolidated tree.

## Tracker Files

- `resume-protocol.md`: the exact operating rules for future review and move
  passes.
- `review-batches.md`: the recommended review queue and batch definitions for
  working through the repo a set at a time.
- `batch-status.tsv`: the live source of truth for batch-level progress.
- `folder-status.tsv`: the live source of truth for folder-group progress.
- `source-map.md`: the current documentation locations grouped by directory.
- `file-inventory.tsv`: a per-file tracker with status, verification, and
  eventual destination columns.
- `non-documentation-candidates.tsv`: a per-file ledger of docs-like files that
  were reviewed and intentionally excluded from the documentation workflow.
- `move-log.tsv`: the append-only record of any documentation move or rename.
- `progress-log.md`: the narrative history of completed tracker work.

## Suggested Workflow

1. Follow `resume-protocol.md` before touching any documentation batch.
2. Pick one batch from `review-batches.md` and mark it in `batch-status.tsv`.
3. Read the files in that batch and determine what each one actually is.
4. Update `file-inventory.tsv` and `folder-status.tsv` as each file is
   reviewed or verified.
5. Record any move or rename in `move-log.tsv` immediately after it happens.
6. Append a short result entry to `progress-log.md` before ending the pass.

## Notes

- This tracker is intentionally separate from any future destination folder so
  the audit work does not get mixed with the final migration.
- `batch-status.tsv`, `folder-status.tsv`, `file-inventory.tsv`, and
  `move-log.tsv` are meant to prevent re-reading, re-moving, or re-deciding
  the same documentation twice.
- `source-map.md` now reflects current locations after moves; `folder-status.tsv`
  remains the historical source-location ledger.
- `docs/stack-documentation/Current/raw-tab-ui-overhaul/auto-starting-point/`
  remains an active current packet, so future edits there should still be
  handled carefully while `implementation-progress.md` is live.
- `non-documentation-candidates.tsv` records the reviewed website HTML assets
  that should not be reconsidered during documentation moves unless the scope
  expands to website content.





