# Phase 01: RAW Evidence Foundation

- Program state: completed and stopped; checkpoint `phase-01/README.md`
- Recipe mutation: prohibited
- Solver decision/apply behavior: prohibited
- Owns: raw metadata/mosaic evidence, units, provenance, uncertainty, and fixtures

## Purpose

Implement the first new solver-related code as non-mutating technical evidence.
The output of this phase is trustworthy diagnostics, not a smarter-looking
image.

## Required Reading

Read the standard cold-start set, Phase 00 checkpoint, and:

```text
../code-web-research-readbacks-and-dng.md
../auto-raw-processing-math-and-science.md
../iterative-raw-solver-research/data-domains-and-pipeline-order.md
../iterative-raw-solver-research/measurement-and-feature-catalog.md
accepted Phase 00 DNG/noise/highlight crosswalks
```

Reinspect current DNG, LibRaw, mosaic, GainMap, demosaic, and readback code
before editing.

## Entry Conditions

- Phase 00 passed.
- Corpus and fixture manifests are versioned.
- Raw evidence formulas, units, stages, and fallback rules are accepted.
- The raw-evidence struct/schema has a versioning plan.
- Phase 01 is active in `../implementation-progress.md`.

## Required Capability

Build an immutable or isolated raw technical record keyed by source/decode
identity. It should cover, when available:

```text
source and decode identity
CFA/color-plane layout
active and masked areas
black levels and spatial deltas
white levels
LinearResponseLimit
per-plane robust percentiles
per-plane near-nonlinear/near-white/clipped fractions
one-, multi-, and all-plane clipping states
AsShotNeutral and technical WB-scaled headroom
BaselineExposure
NoiseProfile coefficients and units
shadow SNR buckets
hot/dead-pixel evidence
opcode/gain/profile coverage and omissions
camera/profile confidence
validity, uncertainty, fallback, and reason fields
```

Do not collapse per-channel evidence into one luma number before clipping,
headroom, and noise decisions are recorded.

## Implementation Slices

Suggested bounded slices:

1. Versioned evidence schema and identity/cancellation tests.
2. Active/masked area and black/white normalization.
3. Per-plane headroom, nonlinearity, and clipping-pattern classification.
4. As-shot/technical WB headroom propagation.
5. NoiseProfile parsing, normalized noise/SNR, and explicit fallbacks.
6. Gain/opcode/profile coverage reporting.
7. Diagnostics serialization/UI summary and fixture audit.

Each slice must remain useful with missing metadata and must state whether a
value is measured, metadata-derived, estimated, fallback, or unavailable.

## Required Tests

- Unit tests for normalization and EV/headroom formulas.
- Synthetic plane fixtures for no clipping, one-plane, multi-plane, and
  all-plane clipping.
- Nonlinearity-limit fixtures distinct from white-level clipping.
- Masked-area and spatial black-level fixtures where legally available.
- NoiseProfile coefficient/unit fixtures and missing-profile fallbacks.
- WB-scaled limiting-channel tests.
- CFA layout, orientation/crop separation, and active-area tests.
- Source/decode identity invalidation and cache determinism.
- NaN, infinity, invalid metadata, and zero-range defensive tests.
- Regression tests proving Build Starting Point output is unchanged.

## Corpus Review

Run the diagnostics across the Phase 00 sensor/metadata layer. Record:

- camera/file coverage;
- metadata availability rates;
- limiting-channel behavior;
- raw clip classes;
- predicted SNR ranges;
- fallback frequency;
- disagreements with controlled brackets, dark frames, or known fixtures;
- runtime and memory.

## Checkpoint

Phase 01 passes only when:

- every raw value has stage, units, identity, validity, and uncertainty;
- fixture results match known signals and metadata interpretations;
- missing metadata fails explicitly and conservatively;
- per-channel clipping/headroom survives into diagnostics;
- NoiseProfile calculations and fallbacks are tested;
- source/decode changes invalidate stale evidence;
- current Pass 94 recipe behavior is unchanged;
- Phase 02 can consume a stable versioned raw record.

## Stop Rules

Do not:

- use new evidence to move Exposure or any graph;
- add candidate scoring;
- infer raw clipping from display textures;
- implement raw reconstruction, denoise, or profile enhancement merely because
  metadata was exposed;
- hide missing metadata behind normal-looking default values;
- tune thresholds from a handful of cameras.

## Handoff

Freeze the raw-evidence version, fixtures, coverage report, known camera gaps,
and measurement cost. Phase 02 must receive explicit APIs and uncertainty, not
reparse raw state independently.

Phase 01 passed as `phase-01-v1`. The accepted artifacts, test commands,
coverage record, known limitations, and exact next stop rule are in
[`phase-01/README.md`](phase-01/README.md). Phase 02 is not active.
