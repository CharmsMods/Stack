# MFD Development Status

Last updated: August 4, 2026.

## Current State

```text
Phase: 13 - fused Bayer result publishes through shared RAW development; RAW Lab UI refinement pending
Algorithm: RA-CFA V1
Processing: RAW Lab can run the real Bayer processor, atomically adopt its normalized float mosaic, preview the developed color result, and feed the managed MFD graph output
Verification: Release build, all 12 MFD suites, MFD/multi-source project validation, and graph behavior passed
```

## Implemented Boundary

- Phase 0 freezes CFA coordinates, versioned parameters, output semantics, and
  explicit reference fallback. New MFD sets use operation schema 3.
- Phase 1 prepares tiled, still-mosaiced Bayer data with reversible pointwise
  gains and separate saturation, defect, repair, and clipping provenance.
- Phase 2 resolves per-CFA Poisson-Gaussian noise through metadata, calibrated,
  conservative estimated, explicit generic, or unavailable quality states.
- Phase 3 provides the strict 4-by-4 Keys same-CFA sampler with per-tap gain,
  derivatives, propagated variance, full-footprint masks, and no edge padding.
- Phase 4 provides deterministic CPU multichannel phase correlation, subpixel
  peak/PSR/uniqueness diagnostics, provisional exposure, common-scale Huber
  IRLS with per-CFA scale/intercept checks, double-precision affine refinement,
  parameter covariance, plausibility guards, and explicit translation fallback
  or frame rejection. Identity is never substituted silently.
- Phase 5 provides four-level same-CFA intensity/variance/validity pyramids,
  deterministic hierarchical best/second-best tile search, robust subpixel
  translation, Hessian covariance, explicit `Structured`/`FlatSafe`/`Rejected`
  nodes, reverse closure, and confidence interpolation with disagreement
  covariance and hard ambiguity/source-border rejection.
- Phase 6 provides 5-by-5 Bayer-cell standardized-residual reliability,
  noise-quality confidence, boundary min-filter protection, per-pixel flat-top
  rejection, a low-confidence absolute safety gate, frame-usability decisions,
  and deterministic tiled 16-bit/float reliability storage.
- Phase 7 provides deterministic double-precision tile fusion, inverse
  effective-variance weights, per-alternate and low-confidence total caps,
  exact local reference fallback, the three-alternate reference-defect
  consensus exception, and effective-sample/variance/rejection diagnostics.
- Phase 8 provides dependency-complete cache keys, corruption-checked disk and
  memory fusion-tile caches, conservative motion-aware source ROIs,
  deterministic memory-budgeted tile scheduling, bounded parallel execution,
  cancellation-safe strict/reference recovery, and generation-checked atomic
  result publication.
- Phase 9 provides versioned RAW fidelity/noise/detail metrics, motion-mask and
  covariance/confidence calibration, stable ablation reports, 16-bit visual
  diagnostics, performance/memory traces, content-addressed ordered corpus
  manifests, parameter freezing, and machine-readable production gates.
- The corpus-intake pass now parses a versioned definition, streams exact-file
  SHA-256 identities, rejects duplicate or incompatible mosaiced RAW frames,
  derives camera-mode identity from capture metadata, binds frame order,
  reference choice, and ground truth into the source-set identity, and writes
  a recoverable locked-manifest report. Informational labels and paths do not
  control identity.
- Phase 10 connects locked decode, two-pass preparation/noise binding,
  CFA-balanced exposure sampling, global/local registration, reliability,
  robust fusion, memory refusal, exact-reference fallback, and atomic offline
  PFM/JSON publication. It does not activate the managed graph result.
- Phase 11 adds neutral camera-native reference/output previews, signed
  difference and contribution diagnostics, and Phase 9 evaluation against an
  optional controlled monochrome PFM ground truth. These are inspection
  artifacts, not final developed color or a graph result.
- Phase 12 invokes that processor from RAW Lab on embedded project originals,
  keeps work off the UI thread, guards adoption by project/set/input revision,
  preserves the previous valid inspection on cancel/failure, and exposes four
  documented RA-CFA controls plus frame and fusion diagnostics. Inspection now
  borrows the completed Bayer buffers instead of copying them. Its processing
  budget now defaults to live available RAM minus a protected reserve, with one
  persisted GiB override (`0` = automatic); resource-only changes do not stale
  an inspection. Frames skipped by preflight are reported as not attempted.
  RAW Lab now shows real stage-weighted progress, frame/tile counts, elapsed
  and last-checkpoint time; preparation, local motion, reliability, and fusion
  provide progress plus responsive cancellation checkpoints. A persisted
  alignment-mode selector now supports full registration, global translation
  without affine/local search, or identity coordinates without registration
  pyramids or exposure fitting. The fast modes retain residual, reliability,
  sampling, defect/saturation, and exact-reference fusion safeguards, and an
  alignment-mode change stales the earlier inspection and cache identity.
- Phase 13 retains the atomic fused mosaic without a full-resolution copy and
  publishes it as the managed MFD node's image output. The shared truthful RAW
  pipeline recognizes that the samples are already linearized and black/white
  normalized, applies the reference GainMap once, then uses the reference CFA,
  white balance, camera matrices, baseline exposure, and orientation. The
  shared post-MFD recipe owns later RAW development and internal/external View
  placement. Input changes invalidate the runtime result; cancel/failure and
  stale completions cannot replace it. RAW Lab currently provides a simple
  developed preview beside the diagnostic controls; visual/tool organization
  is intentionally deferred to the next UI pass. The first native follow-up
  registered the protected frame/MFD/source-set definitions (including exact
  migration of earlier empty identities), exposed selection count and project
  creation in every RAW Lab Gallery host, and stopped identical in-session
  saves from advancing storage revisions and causing false conflict prompts.
  Gallery tightening now keeps `Create New MFD Project` distinct from `Add to
  Current Burst`, permits explicit double-click/Open replacement through a
  Save/Discard/Cancel gate, retains rollback state until the candidate opens,
  and labels zero-contribution processing as an exact-reference result. The
  processor now explicitly uses its strict generic-low-confidence noise
  fallback when valid low-signal RAWs lack a usable metadata or fitted model,
  allowing alternates to be attempted under the existing reliability gates and
  weight caps. Spatial frame admission is now independent of the model-quality
  multiplier, while final reliability and fusion caps retain that uncertainty;
  low-signal validation requires real alternate pixel contribution. An
  unprocessed MFD graph source is now a quiet waiting state; only an adopted
  result becomes executable. The compact result readout now reports mean
  effective samples so an accepted five-frame burst does not imply five equal
  contributors. Shared post-MFD orientation can be rotated left/right or
  flipped on either axis from the RAW Lab command strip without rerunning MFD.

Parameter schema 3 serializes the Phase 6 reliability safeguards. Schema 1 and
2 payloads remain readable and are normalized to conservative current defaults.

Noise models are bound to their prepared-frame cache identity. Unsupported or
weak evidence remains reference-only. Graph publication is experimental and
does not imply that the corpus quality/release gates have passed.

## Validation

```text
.\build\Stack.exe --validate-mfd-phase0
.\build\Stack.exe --validate-mfd-phase1
.\build\Stack.exe --validate-mfd-phase2
.\build\Stack.exe --validate-mfd-phase3
.\build\Stack.exe --validate-mfd-phase4
.\build\Stack.exe --validate-mfd-phase5
.\build\Stack.exe --validate-mfd-phase6
.\build\Stack.exe --validate-mfd-phase7
.\build\Stack.exe --validate-mfd-phase8
.\build\Stack.exe --validate-mfd-phase9
.\build\Stack.exe --validate-mfd-phase9 --output <absolute-directory>
.\build\Stack.exe --validate-mfd-corpus-intake
.\build\Stack.exe --write-mfd-corpus-template <absolute-output-json>
.\build\Stack.exe --lock-mfd-corpus <absolute-definition-json> --output <absolute-report-json>
.\build\Stack.exe --validate-mfd-end-to-end
.\build\Stack.exe --run-mfd-corpus-entry <absolute-definition-json> --sample <sample-id> --output <absolute-directory> [--memory-mib <count>] [--workers <count>]
.\build\Stack.exe --validate-mfd-project-foundation
.\build\Stack.exe --validate-multi-source-projects
ctest --test-dir build -C Release -R "StackRaw.Mfd(Phase[0-9]|CorpusIntake|EndToEnd)$"
```

Phase 9 known-answer validation covers same-CFA metrics, noise reduction versus
`sqrt(N_eff)`, motion rejection, exact covariance coverage, stable ablations,
visual artifacts, manifest ordering/content invalidation, freeze tampering, and
both blocked and conforming release-gate logic. Corpus-intake validation covers
content/order/reference invalidation, derived camera identity, required ground
truth, duplicate content, geometry/orientation compatibility, and Linear RGB
rejection. The end-to-end suite also proves a translated noisy Bayer burst can
reduce error against known mosaic truth, while incompatible, canceled, and
insufficient-memory paths cannot publish a partial or falsely denoised result.
It also covers identity and translation-only denoise candidates, registration
bypass diagnostics, and alignment-mode cache separation. All 12 registered MFD
suites pass. The conforming packet is a gate-logic
self-test, not real-camera evidence.

## Next Gate

Refine the RAW Lab presentation so Multi-Frame, Exposure, Zones, Curve, and
View edit one shared post-MFD recipe without the current diagnostic wall. In
parallel, use the runner on controlled plus uncontrolled bursts from at least
two camera modes, cover 2/4/8/16/32 frames, capture reference-moment ground
truth, review diagnostics, freeze parameters, and rerun without hand-selected
exceptions. Production approval remains blocked until the corpus report passes.

Design source: [multi-frame-mosaic-denoise-research-and-v1-design.md](multi-frame-mosaic-denoise-research-and-v1-design.md)
