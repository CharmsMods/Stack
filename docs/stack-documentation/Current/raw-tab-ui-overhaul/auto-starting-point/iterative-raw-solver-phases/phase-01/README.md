# Phase 01 Checkpoint

- Checkpoint: `phase-01-v1`
- Date: 2026-07-09
- Decision: PASS
- Raw evidence schema: `raw-technical-evidence-v1`
- Normalization version: `dng-linear-reference-v1`
- Decoder identity version: `stack-libraw-dng-supplement-v2`
- Solver behavior changed: no
- Recipe mutation: prohibited and not performed
- Next phase activated: no

## Outcome

Phase 01 provides a versioned, non-mutating raw technical evidence API keyed by
content SHA-256 plus a canonical decode identity. It parses and reports sensor
geometry, DNG normalization metadata, per-plane robust measurements,
CFA-aligned clipping states, technical-WB headroom, DNG noise/SNR, defect
evidence, opcode/profile coverage, provenance, units, validity, uncertainty,
and explicit fallbacks.

The new record is isolated from Pass 94. No existing candidate score, recipe
planner, slider, graph, render output, or project state consumes it. The
command-line corpus audit and serializer are the diagnostic surface for this
phase; `SummarizeRawTechnicalEvidence` supplies a compact consumer-facing
summary API without wiring the record into solver decisions.

## Accepted Artifacts

| Version | Artifact |
| --- | --- |
| `raw-technical-evidence-v1` | `src/Raw/RawTechnicalEvidence.h/.cpp` |
| `stack-libraw-dng-supplement-v2` | Extended DNG parsing in `src/Raw/LibRawDecoder.cpp` and metadata fields in `src/Raw/RawImageData.h` |
| `phase-01-fixtures-v1` | `tools/raw_evidence_tests.cpp` and `StackRawEvidenceTests.exe` |
| `phase-01-corpus-coverage-v1` | [Full 186-source JSON record](corpus-coverage-v1.json) |
| `phase-01-schema-contract-v1` | [Schema and API contract](raw-evidence-schema-v1.md) |

The corpus JSON SHA-256 is:

```text
9FF3054B4976B40A3D06B83E16EE7CE3A2FDDBDCB0C4A050E4EA2EC6AC2643C9
```

## Behavior And Files Changed

- `RawMetadata` now retains content identity and the DNG tags Phase 01 needs:
  LinearizationTable, full BlackLevel values, horizontal/vertical black
  deltas, WhiteLevel values, ActiveArea, MaskedAreas, LinearResponseLimit,
  BaselineNoise, NoiseProfile, opcode-list counts, and profile-gain-map
  presence.
- The DNG supplement parser now follows classic-TIFF IFD/SubIFD trees, chooses
  the raw IFD using raw-layout/tag evidence, and preserves IFD-relative inline
  values correctly.
- The decoder computes the original-file SHA-256 before returning decoded RAW
  data. Identical bytes at different paths therefore share source identity.
- `RawTechnicalEvidence` computes normalization in sensor coordinates. It
  applies a linearization table before black subtraction and uses the maximum
  applicable black value for each CFA color, including row/column class
  deltas, in the white-range denominator.
- Per-plane records retain sample count, black/white/response limits, p999,
  near-nonlinear/near-white/clipped fractions, unscaled headroom, technical WB
  gain, and WB-scaled headroom.
- CFA-aligned 2x2 superpixels distinguish no-, one-, two-, and all-color-plane
  clipping. Mosaic-wide plane counts are not mislabeled as co-sited clipping.
- DNG NoiseProfile parameters remain in normalized raw units and produce
  explicit SNR buckets. Missing profiles remain unavailable; BaselineNoise is
  reported but not substituted as camera-specific truth.
- Masked-area black values and same-CFA hot/dead outlier rates are diagnostic
  measurements only. They do not rewrite black level or pixels.
- `--validate-raw-evidence-foundation` writes a path-scrubbed JSON corpus
  report and returns failure on source identity, decode, or record failure.

## Fixture Gate

`StackRawEvidenceTests` covers the Phase 00 fixture plan:

| Fixture family | Result |
| --- | --- |
| `P01-ID-01/02` | Same bytes share SHA-256 across paths; one-byte change invalidates identity; standard `abc` SHA-256 vector passes. |
| `P01-DECODE-01` | Every declared decode-affecting option changes the canonical decode identity; repeat construction is deterministic. |
| `P01-NORM-01..04` | Black, midpoint, and white mappings; repeating black; H/V black deltas; and linearization-before-black all match analytic values. |
| `P01-AREA-01/02` | Sensor measurements are invariant across orientations; photographic transform is explicit; masked black and active scene samples stay separate. |
| `P01-CLIP-01/02` | Near-white and clipped states are distinct; 0/1/2/3-plane CFA superpixels produce exact fractions. |
| `P01-LRL-01` | Near-nonlinear pressure below white differs from white clipping; missing LRL is an explicit white-outer-limit fallback. |
| `P01-WB-01` | Known technical gains produce the analytic limiting plane and WB-scaled EV headroom. |
| `P01-NOISE-01/02` | DNG S/O variance, sigma, SNR, gain-squared variance propagation, and pure-gain SNR invariance match the equations. |
| `P01-META-01` | Present/missing WB, exposure, noise, opcode, and gain/profile capabilities preserve provenance and omissions. |
| `P01-STATE-01` | Success, cancellation, source change, and decode change preserve input state and invalidate only the correct cache key. Existing editor-state tests retain render-failure/recipe/undo coverage. |
| defensive | NaN, invalid white range, missing mosaic data, cancellation, and missing metadata fail explicitly. |

## Corpus Coverage

The frozen audit contains exactly the Phase 00 sensor/metadata layer: 178
unique Sony ARWs and the eight initial Samsung DNG records.

| Measure | Result |
| --- | ---: |
| Unique content identities | 186 |
| Identity failures | 0 |
| Decode failures | 0 |
| Incomplete evidence records | 0 |
| Sony ILCE-7M2 ARWs | 178 |
| Samsung SM-S921U DNGs | 8 |
| DNG LinearResponseLimit available | 8 / 8 |
| DNG NoiseProfile available | 8 / 8 |
| DNG AsShotNeutral available | 8 / 8 |
| DNG OpcodeList2 GainMap available | 8 / 8 |
| Explicit DNG ActiveArea available | 0 / 8 |
| Explicit DNG MaskedAreas available | 0 / 8 |
| Files with no co-sited clipped color plane | 179 |
| Files with partial co-sited plane clipping | 4 |
| Files with all-plane co-sited clipping | 3 |

The real DNG sequence progresses as expected: the first record has no sampled
CFA-superpixel clipping, the middle records add partial one/two-plane clipping,
and the final three contain measurable all-plane clipped superpixels. This
closes the previous limitation that mosaic-wide plane counts could not claim
co-sited all-plane clipping.

The DNG NoiseProfile SNR buckets span `0.6636` to `17.4564` linear SNR across
the declared diagnostic signal levels. This is a coverage fact, not a solver
threshold. Sony ARWs expose decoder technical WB but no DNG NoiseProfile; their
noise model remains explicitly unavailable.

Measured audit cost on this machine:

```text
total command runtime:       102411.8 ms
raw evidence computation:     39028.9 ms
mean evidence per source:       209.8 ms
peak decoded RAW bytes:      48674304
```

The total includes content hashing and LibRaw decode. The evidence-only time
includes deterministic one-million-sample limits and same-CFA defect sampling.

## Automated Verification

These commands passed:

```text
.\build.cmd
.\build\StackRawEvidenceTests.exe
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-raw-starting-point-editor-state
```

The existing eight-DNG Pass 94 validation command also loaded metadata and raw
safety for all eight records. Every record retained
`visibleRecipeWriteAudit.validationCommandMutatesRecipe=false`, changed no
visible field, and left the legacy planner path intact.

## Gate Audit

| Phase 01 gate | Evidence | Result |
| --- | --- | --- |
| Every raw value has identity, stage, units, validity, uncertainty, provenance | Measurement wrappers plus source/decode/evidence SHA-256 in schema and JSON. | Pass |
| Controlled fixtures match known math | Focused fixture executable covers every Phase 01 family above. | Pass |
| Missing metadata fails conservatively | Null/invalid measurement with reason; no fabricated NoiseProfile, LRL, ActiveArea, or MaskedAreas. | Pass |
| Per-channel clipping/headroom survives diagnostics | Three plane records plus CFA-superpixel clip classes in every JSON record. | Pass |
| NoiseProfile and fallback tested | Analytic fixtures and eight real DNG profiles; 178 ARWs report unavailable. | Pass |
| Source/decode invalidates stale evidence | Content hash, canonical decode hash, evidence key, and cache fixtures. | Pass |
| Pass 94 recipe behavior unchanged | New record has no planner dependency; graph/editor-state and eight-DNG no-mutation validations pass. | Pass |
| Stable Phase 02 input exists | Frozen header/API, schema document, fixture executable, and corpus report. | Pass |

## Known Limitations

- The corpus still covers only two camera families. Cross-camera locked data is
  a Phase 07 requirement.
- None of the eight local DNGs contains an explicit ActiveArea or MaskedAreas
  tag. Synthetic fixtures prove their math and separation, but real tagged
  files remain an acquisition item.
- The DNG supplement parser intentionally supports classic TIFF DNG IFD trees;
  BigTIFF supplement-tag parsing remains unavailable and explicit.
- OpcodeList1/3 and ProfileGainTableMap are reported, not implemented as image
  corrections. This phase does not add reconstruction, denoise, or profile
  enhancement.
- Hot/dead-pixel rates are high-uncertainty same-CFA diagnostics, not a defect
  correction mask or objective term.
- The native RAW Diagnostics drawer still displays the frozen Pass 94 raw
  safety payload. Phase 01 exposes a serializer and compact summary API, but
  intentionally does not feed the new record into the planner or mutate UI
  controls.
- Sampling is deterministic and bounded. Rare isolated pixels can differ from
  a full exhaustive count; full-resolution verification remains a later
  candidate/apply requirement.

## Handoff

Decision: PASS

Accepted artifacts: `raw-technical-evidence-v1`,
`stack-libraw-dng-supplement-v2`, `phase-01-fixtures-v1`,
`phase-01-corpus-coverage-v1`, and `phase-01-schema-contract-v1`.

Rejected approaches: path/mtime as evidence identity; display-texture raw
safety; one aggregate highlight boolean; fabricated NoiseProfile/LRL defaults;
using new evidence to tune or move controls; applying unsupported opcodes or
profile gains invisibly.

Frozen versions: listed above. The corpus JSON hash is recorded above.

Known limitations: listed above. They remain explicit inputs or uncertainty
for Phase 02 and do not justify reparsing raw state independently.

Next phase entry evidence: this checkpoint, [the schema contract](raw-evidence-schema-v1.md),
the focused fixture executable, and [the full corpus report](corpus-coverage-v1.json).

Next allowed slice: stop. Phase 02 may start only after explicit activation in
the parent progress ledger.

Do not do next: recipe mutation, candidate scoring, objective tuning, optimizer
selection, automatic graph writes, or Phase 03 candidate rendering.
