# Phase 00 Checkpoint

- Checkpoint: `phase-00-v1`
- Date: 2026-07-09
- Decision: PASS
- Solver behavior changed: no
- Recipe mutation: prohibited and not performed
- Next phase activated: no

## Outcome

Phase 00 closes the research needed for a safe diagnostic implementation
start. It freezes Pass 94, establishes content-addressed corpus rules, accepts
only measurements with declared domains and uncertainty, bounds the first
visible parameter space, and defines candidate isolation before any optimizer
is selected.

The phase does **not** claim that objective weights, halo thresholds, display
targets, or an optimizer are ready. Those values require the controlled and
real-image experiments assigned to later phases. This checkpoint makes those
unknowns explicit without forcing Phase 01 to invent raw formulas or state
rules.

## Accepted Artifacts

| Version | Artifact |
| --- | --- |
| `pass-94-baseline-v1` | [Pass 94 baseline](pass-94-baseline-v1.md) |
| `corpus-contract-v1` | [Corpus, manifest, and split contract](corpus-contract-v1.md) |
| `corpus-manifest-v1` | [Content-addressed local manifest](corpus-manifest-v1.tsv) |
| `failure-taxonomy-v1` | [Failure taxonomy and review record](failure-taxonomy-and-review-v1.md) |
| `raw-math-v1` | [DNG, clipping, noise, luminance, and WB math](raw-math-v1.md) |
| `rendered-math-v1` | [Multiscale, halo, color, and display math](rendered-math-v1.md) |
| `feature-ledger-v1` | [Accepted feature ledger](feature-ledger-v1.md) |
| `parameter-objective-v0` | [Initial parameter space and objective contract](parameter-space-and-objective-v0.md) |
| `candidate-contract-v0` | [Candidate identity, cache, cancellation, and no-mutation design](candidate-evaluation-contract-v0.md) |
| `fixture-plan-v0` | [Phase 01 and Phase 02 fixture plan](phase-01-02-fixture-plan-v0.md) |

## Gate Audit

| Gate | Evidence | Result |
| --- | --- | --- |
| Pass 94 reproducible | Build, two automated validations, executable hashes, and native interaction review are frozen. | Pass |
| Corpus and partitions | 178 unique ARWs plus initial DNG sensor records are content-addressed; duplicate/session grouping and split rules are explicit. | Pass |
| Critical failure coverage | Every family maps to a development/validation record type or a controlled fixture; unobserved technical states must remain synthetic until legally acquired. | Pass |
| Credible locked test | A never-reviewed, session-grouped quota and replacement rule are defined. Existing browsed files are not mislabeled as locked. | Pass |
| Raw and graph fixtures | Exact normalization, clipping, noise, graph, identity, and no-mutation fixtures are specified. | Pass |
| Feature classification | Every Phase 00 proposal is accepted, prototype-only, rejected, or needs a human-label study. | Pass |
| Objective terms inspectable | Units, stage, reference, uncertainty, validation, and tier are named; no unsupported final weight exists. | Pass |
| First parameter space bounded | A 13-dimensional maximum active space is declared, with per-image conditional dimensions and visible ownership. | Pass |
| Research closure | Highest-priority sources have Stack-specific extractions; remaining optimizer reading is experiment-triggered, not a Phase 01 dependency. | Pass |
| Phase 01 needs no invented raw formula | `raw-math-v1` and `fixture-plan-v0` define the required inputs, equations, fallbacks, and failure states. | Pass |

## Accepted Decisions

- Pass 94 remains the fast path, warm start, fallback, and baseline.
- Raw safety is content/source keyed and evaluated before display quality.
- Missing metadata yields an unavailable or uncertain measurement, never a
  fabricated zero.
- A raw exposure multiplication reveals noise; it does not improve raw SNR.
- Partial-channel, all-channel, and non-linear response states remain distinct.
- Edge-aware filters are prototypes, not halo guarantees.
- The first solver uses fixed measured graph x locations and optimizes only
  bounded visible y/delta/width/display values.
- Precise candidates remain isolated through Phase 05. Phase 06 is the first
  phase allowed to apply one atomically.
- No optimizer is selected until Phase 03 samples the actual objective surface.

## Rejected Approaches

- Repeating Pass 94 formulas and calling the pass count convergence.
- Using final display clipping as raw sensor safety.
- A single brightness, histogram, naturalness, or expert-edit target.
- ISO alone as a shadow-noise model.
- Automatically replacing as-shot WB when estimators disagree.
- Treating bilateral, guided, or local-Laplacian processing as inherently
  artifact-free.
- Hidden denoise, reconstruction, profile, or display correction outside the
  visible Stack recipe.
- Stochastic optimization without a declared seed and deterministic replay.

## Known Limitations

- The local set is dominated by one Sony ARW camera/session family and one
  Samsung DNG session. Cross-camera evidence must be added before Phase 07.
- The 356-file gallery contains two filesystem copies of 178 ARWs. The second
  copy is excluded by content identity.
- Existing local images were browsed during discovery, so none is called a
  locked test image.
- Stack's current raw record path reports that full DNG ActiveArea/MaskedAreas
  parsing and channel-aligned all-channel clipping are not yet implemented.
- Halo and acceptability thresholds intentionally remain uncalibrated.
- Absolute-luminance perceptual metrics remain advisory unless display and
  ambient conditions are known.

## Handoff

Decision: PASS

Accepted artifacts: the ten versioned records above.

Rejected approaches: listed above and in the feature ledger.

Frozen versions: `pass-94-baseline-v1`, `corpus-contract-v1`,
`raw-math-v1`, `rendered-math-v1`, `feature-ledger-v1`,
`parameter-objective-v0`, `candidate-contract-v0`, `fixture-plan-v0`.

Known limitations: listed above. They do not require Phase 01 to guess raw
math; they restrict later acceptance and final validation.

Next phase entry evidence: this checkpoint plus every linked artifact.

Next allowed slice: Phase 01 source/decode identity and non-mutating raw
normalization diagnostics, only after explicit activation in the parent
progress ledger.

Do not do next: optimizer selection, recipe mutation, objective-weight tuning,
automatic graph writes, or Phase 02 multiscale implementation.
