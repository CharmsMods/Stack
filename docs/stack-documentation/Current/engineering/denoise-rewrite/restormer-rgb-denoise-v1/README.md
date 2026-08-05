# Restormer RGB Denoise V1

Last updated: July 24, 2026.

## Purpose

This folder owns the new optional Restormer system for Stack's
post-demosaic RGB Denoise stage. It does not reactivate the archived neural
node, its old model-pack schema, or its `[0, 1]` scene-linear clamp.

The intended order remains:

```text
CFA Denoise
Demosaic / White Balance / working-space conversion
RGB Denoise
RAW Exposure
Zones
Finish Tone
View Transform
```

The built-in Classical Multiscale method remains available without any
external package. Restormer is an optional local/offline package and runs in
`StackModelService.exe`, outside `Stack.exe`.

## Current Implementation State

- RAW recipe schema 11 records the AI method, mapping, exact package version,
  model SHA-256, and adapter version.
- RAW Lab exposes Classical Multiscale, Restormer Real Photo, and Restormer
  Gaussian (Blind), plus Scene-linear Safe and Processed RGB Match mappings.
- A CPU reference adapter implements the proxy and delta-transfer contract.
- Package parsing validates path containment, artifact SHA-256 values,
  protocol/adapter compatibility, exact project pins, and development versus
  release trust policy.
- The external Windows ML service, shared-memory client, tiled inference,
  cancellation boundary, neutral-output cache, and asynchronous CPU/model
  job are implemented as a development foundation. Live RAW OpenGL remains
  owned by the UI context; the unsafe legacy RAW render worker is not enabled.
- Protocol, adapter, tiling, package-integrity, and recipe behavior have
  automated coverage.
- The two officially published checkpoints were acquired into the ignored
  development workspace, converted to self-contained FP32 ONNX at opset 18,
  and verified against PyTorch. Both models complete through the packaged
  helper with DirectML and the explicit CPU fallback.
- End-to-end image-quality validation on the private MotionCam corpus remains
  open. The first live run found and corrected a blocking UI integration path;
  responsiveness and visible-result acceptance require a fresh user test.
- No official checkpoint or converted ONNX derivative is committed or
  approved for public redistribution.

## Files

- [implementation-progress.md](implementation-progress.md) - authoritative
  checklist and handoff.
- [artifact-and-license-ledger.md](artifact-and-license-ledger.md) - every
  package artifact and its legal gate.
- [checkpoint-authorization-request.md](checkpoint-authorization-request.md)
  - permission scope that must be obtained in writing.
- [runtime-and-ipc-contract.md](runtime-and-ipc-contract.md) - helper, shared
  memory, tiling, cancellation, and failure rules.
- [model-adapter-contract.md](model-adapter-contract.md) - proxy and residual
  mapping truth.
- [motioncam-sidd-test-manifest.md](motioncam-sidd-test-manifest.md) - frozen
  evaluation categories without committing private photographs.
- [local-development-acquisition-record.md](local-development-acquisition-record.md)
  - exact upstream commit, checkpoint IDs/hashes, conversions, and local-only
  test evidence.

## Release Rule

A developer-local package may be used only through an explicit development
trust path. A public package must remain rejected until checkpoint permission,
the frozen artifact ledger, signature/allowlist entry, model equivalence
report, licenses, notices, and SBOM all pass review.
