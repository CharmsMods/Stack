# SAM 2.1 Small Workspace Preparation

- Captured: 2026-07-24 12:41
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-24-1241-sam-2-1-small-workspace-preparation.md`
- Type: update
- Topic: sam-2-1-small-integration
- Verification: verified

## Direction

SAM 2.1 Small is the provisional lead candidate for Stack's future
AI-assisted subject-selection system.

The documentation workspace should be complete enough that a later
implementation task can begin with artifact and legal verification instead of
rediscovering product boundaries, source seams, package architecture, or test
requirements.

No implementation is requested in this preparation pass.

## Organization Decision

Keep two sibling folders:

```text
manual-first-raw-workflow/
  ai-assisted-subject-selection-research/
    comparative research, sources, model/license matrix

  sam-2-1-small-integration/
    candidate-specific contracts, gates, roadmap, ledgers, progress
```

This prevents the implementation record from erasing the reasons alternatives
were accepted or rejected.

## Follow-Up

The first future action is Phase 01 artifact freeze and licensing review. It is
not application integration.

## Related Docs

- `README.md`
- `implementation-progress.md`
- `phase-roadmap.md`
- `../ai-assisted-subject-selection-research/README.md`
