# SAM 2.1 Small Integration

## Purpose

This is the entry point for the possible addition of Meta SAM 2.1 Small as an
optional, local, AI-assisted subject-selection provider for Stack.

This folder owns the future integration program. The sibling
`ai-assisted-subject-selection-research/` folder remains the authority for
vendor comparisons, alternative models, general licensing research, and the
reason SAM 2.1 Small became the provisional lead candidate.

## Current Status

```text
Phase: 00 - documentation workspace preparation
Status: prepared; implementation not started or authorized
Candidate: SAM 2.1 Small, provisional
Artifacts downloaded into repository: none
Model/runtime packaged: no
Application code changed: no
```

Preparing this workspace does not authorize downloading or committing model
weights, adding runtime binaries, modifying the installer, or implementing the
selection UI. A later user request must explicitly start the program.

## Read Order

Before doing any SAM 2.1 Small work, read:

```text
README.md
implementation-progress.md
program-contract.md
decision-register.md
phase-roadmap.md
artifact-and-license-ledger.md
runtime-and-ipc-contract.md
benchmark-and-validation-plan.md
source-map.md
open-questions.md
../ai-assisted-subject-selection-research/README.md
../ai-assisted-subject-selection-research/model-and-license-matrix.md
../ai-assisted-subject-selection-research/integration-architecture.md
```

## Workspace Map

- `implementation-progress.md` - current phase, allowed next action, and stop
  rules.
- `program-contract.md` - product boundaries and invariants.
- `decision-register.md` - accepted, provisional, and open decisions.
- `phase-roadmap.md` - gated delivery sequence.
- `artifact-and-license-ledger.md` - exact upstream artifacts, hashes,
  licenses, conversion records, and release approval.
- `runtime-and-ipc-contract.md` - helper-process, package, tensor, security,
  and mask-delivery contracts.
- `benchmark-and-validation-plan.md` - quality, latency, memory, hardware,
  parity, and user-effort evaluation.
- `source-map.md` - current Stack code and documentation seams.
- `open-questions.md` - decisions that must not be guessed during
  implementation.
- `2026-07-24-workspace-preparation.md` - captured direction for this setup.

## Authority Boundary

This folder may narrow or implement the SAM 2.1 Small candidate but must not
silently rewrite the broader model/licensing conclusions. New legal or model
research belongs in the sibling research packet and should be reflected here
only through an explicit decision-register update.

This is engineering documentation, not legal advice. Public or commercial
distribution requires review of the exact frozen artifacts.
