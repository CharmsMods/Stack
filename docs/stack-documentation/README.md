# Stack Documentation Home

This folder is the consolidated home for reorganized Stack documentation.

## Structure

- `Current`: active implementation packets, handoffs, protocols, live work,
  and fallback intake notes under `ideas/`, `updates/`, and `fixes/` when no
  clearer workstream home exists yet.
- `Info`: reference material, research notes, audits, supporting background,
  and fallback intake notes under `research/` and `questions/` when no clearer
  workstream home exists yet.
- `Archived`: completed phases, superseded plans, historical documents we want
  to preserve, and raw intake source records under `source-notes/`.

## Notes

- The review tracker remains separate in
  `docs/documentation-reorg-tracker/` so audit history and move history stay
  easy to inspect.
- Ongoing user-note intake is governed by
  `docs/stack-documentation/IDEA_INTAKE_PROTOCOL.md` and tracked in
  `docs/stack-documentation/IDEA_INTAKE_LEDGER.tsv`.
- For task-specific work, prefer naming one entry file for the conversation and
  letting that file own the detailed read chain instead of expanding
  `AGENTS.md`.
- Path-coupled documentation bundles should still be rehomed only when their
  companion references are updated together in the same pass.
- Repo-root operational docs are also mirrored under
  `docs/stack-documentation/Info/repo-root/` so the consolidated documentation
  home contains the full non-instruction doc set while the root entrypoints
  stay intact.
- `AGENTS.md`, `AGENT RULES/`, and the product website HTML files remain
  outside this folder because they are repo instructions or product assets
  rather than normal documentation.

