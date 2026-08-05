# SAM 2.1 Small Integration Progress

Last updated: July 24, 2026.

Keep this file short. It owns the active phase, allowed next work, and stop
rules.

## Current State

```text
Phase: 00 - documentation workspace preparation
Status: complete
Implementation: not started
Model selection: SAM 2.1 Small is provisional, not production-approved
Next allowed phase: 01 - artifact freeze and legal/provenance gate
```

## Completed in Phase 00

- Created a candidate-specific documentation entry point.
- Separated broad model research from candidate implementation records.
- Defined the product and non-goal boundaries.
- Established a gated phase roadmap.
- Created empty artifact, license, conversion, and release-approval records.
- Defined the intended external helper and signed package boundary.
- Defined the benchmark categories and current Stack source map.
- Preserved open decisions instead of selecting unverified defaults.

## Next Allowed Work

Only after the user explicitly asks to begin:

1. freeze an exact official SAM 2.1 Small upstream commit/tag;
2. retrieve the official checkpoint outside tracked source directories;
3. calculate and record hashes;
4. preserve the exact upstream license and notices;
5. inventory transitive reference dependencies and training provenance;
6. decide whether the candidate passes the legal/provenance gate;
7. create an external reference benchmark environment.

## Stop Rules

Do not:

- commit weights, runtime DLLs, Python environments, downloaded archives, or
  generated ONNX files to the source repository;
- add SAM code directly to `Stack.exe`;
- revive or extend the retired `NeuralDenoiseManager` as a general model loader;
- use Hugging Face `trust_remote_code=True`;
- execute downloaded Python or arbitrary package code from Stack;
- modify the RAW recipe schema before mask-persistence requirements are
  approved;
- alter Local Exposure math, RAW processing, or exports to accommodate the
  model;
- begin UI integration before the benchmark and native-helper gates pass;
- describe the model as Stack-authored.

## Phase Completion Rule

No phase is complete because a demo works. Each phase must satisfy its
documented evidence gate and update this file, the decision register, and the
artifact/license ledger.
