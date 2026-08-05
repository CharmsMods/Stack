# SAM 2.1 Small Phase Roadmap

## Phase 00 - Documentation Workspace

Output:

- contracts, decisions, roadmap, ledgers, source map, benchmark plan, and stop
  rules.

Gate:

- workspace is coherent;
- no implementation or model artifact has entered the repository.

Status: complete.

## Phase 01 - Artifact Freeze and Legal/Provenance Gate

Work:

- freeze official upstream source and SAM 2.1 Small checkpoint;
- record original hashes, license files, notices, model card, and provenance;
- inventory reference dependencies and conversion tooling;
- review commercial use, redistribution, modifications, patents, trademarks,
  and training-data disclosures;
- decide whether Stack may continue with the frozen candidate.

Gate:

- every proposed artifact has a ledger row and source;
- no unresolved license applies only to code while leaving weights unclear;
- legal review has a concrete frozen artifact set;
- no model is distributed yet.

## Phase 02 - External Reference and ONNX Benchmark

Work:

- build an isolated reference environment outside Stack;
- establish deterministic analysis-proxy fixtures;
- export or produce audited ONNX artifacts;
- compare reference and ONNX masks numerically;
- measure Small and, if useful, Tiny on the same corpus and hardware.

Gate:

- conversion parity is documented;
- boundary quality and user correction effort justify integration;
- cold and warm performance are understood;
- runtime/provider choice is evidence-based.

## Phase 03 - Native Helper and Protocol

Work:

- implement `StackSubjectService.exe`;
- implement fixed versioned IPC and shared-buffer validation;
- load only one approved local package;
- add timeouts, cancellation, generation rejection, crash recovery, and
  diagnostics;
- prove the helper cannot mutate Stack state or replace the accepted preview.

Gate:

- service passes malformed-input and lifecycle tests;
- Stack operates normally when service/model/runtime is absent or broken;
- reference and helper results match within documented tolerance.

## Phase 04 - Signed Optional Package Path

Work:

- define immutable package layout and manifest;
- implement allowlist, signature, hash, license, and compatibility checks;
- add staged atomic install, health check, rollback, and uninstall;
- expose notices and provenance;
- ensure existing projects keep baked masks after removal.

Gate:

- no arbitrary package path, DLL, or code execution is possible;
- corrupted, tampered, incomplete, and incompatible packs are rejected;
- base installer remains model-free.

## Phase 05 - Editor Selection Integration

Work:

- add explicit AI Refine entry to the existing targeting workflow;
- add positive/negative point and box prompts;
- show provisional and returned outlines without tinting/replacing the photo;
- compose AI and deterministic masks;
- accept/cancel as a transaction;
- persist accepted mask and provenance.

Gate:

- prompt-only actions do not dirty the project;
- accept creates one undoable change;
- Local Exposure remains interactive without rerunning inference;
- RAW and RAW Lab share the capability without duplicating the backend.

## Phase 06 - Product Validation and Release Review

Work:

- complete usability, hardware, DPI, failure, project reload, export, package,
  security, privacy, and license validation;
- verify final release contents and aggregated notices;
- recheck exact hashes against the approved ledger;
- decide whether the feature is experimental, optional production, or held
  back.

Gate:

- all acceptance evidence is recorded;
- release counsel approves the actual package;
- no known blocker is hidden behind a fallback.

## Later Possibilities

Only after the still-image path is complete:

- automatic main-subject proposals;
- portrait matting refinement;
- people-part selection;
- reusable named selection objects;
- additional permissive providers;
- video tracking/propagation.
