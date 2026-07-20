# Research Dependency Map

## Purpose

This is the bridge between the guided phases and the existing math, image
science, color science, data science, standards, and product research. Phase
files point here so future agents know where claims originate and do not invent
new formulas from memory.

## Research Authority

Start with:

```text
../iterative-raw-solver-research/README.md
../iterative-raw-solver-research/research-session-handoff.md
```

Then follow the task routes below. The research bundle contains hypotheses and
source extractions; `checkpoint-protocol.md` defines when a hypothesis is
accepted for implementation.

## Core Research Routes

| Question | Open first | Then use | Consumed by |
| --- | --- | --- | --- |
| What does the current four-pass solver actually do? | `../iterative-raw-solver-research/current-system-audit.md` | Current code and `../implementation-progress.md` | Phase 00 baseline, every comparison phase |
| Which data domain and pipeline stage owns a measurement? | `../iterative-raw-solver-research/data-domains-and-pipeline-order.md` | `../auto-raw-processing-math-and-science.md`, DNG specification | Phases 00-03 |
| Which features might describe raw safety, noise, regions, halos, color, or display quality? | `../iterative-raw-solver-research/measurement-and-feature-catalog.md` | Primary papers routed by `../iterative-raw-solver-research/source-ledger.md` | Phases 00-03 |
| How should safety and quality objectives be organized? | `../iterative-raw-solver-research/objective-function-and-constraints.md` | `../implementation-contract.md`, display/perceptual sources | Phases 00, 03-05 |
| How could candidates be searched and convergence measured? | `../iterative-raw-solver-research/optimizer-and-convergence-design.md` | Primary optimizer papers in the source ledger | Phases 03-05 |
| What data and experiments are required? | `../iterative-raw-solver-research/validation-data-and-experiments.md` | `../implementation-pass-readiness.md` validation records | Every phase, especially 00 and 07 |
| Which sources are fully derived or still only initially extracted? | `../iterative-raw-solver-research/source-ledger.md` | Full source itself | Phase 00 and any phase adding new math |
| What are the non-negotiable UI/ownership rules? | `../implementation-contract.md` | `../auto-manual-compute-model.md`, `../human-workflow-notes.md` | Every phase |
| What does current Stack read back and parse? | `../code-web-research-readbacks-and-dng.md` | Current source code | Phases 01-03 |
| What were the first-generation formulas and score helpers? | `../implementation-pass-readiness.md` | `../auto-starting-point-solver-research.md` | Baseline comparison and warm start |

## Math And Science Work Queue

Before Phase 01, Phase 00 must convert the highest-priority source-ledger items
into Stack-specific records.

| Research area | Minimum extraction | First phase that may implement it |
| --- | --- | --- |
| DNG raw evidence | Tag, type, units, stage, normalization, fallback, uncertainty, current Stack coverage, fixture | Phase 01 |
| Raw noise | Poisson-Gaussian/DNG model, units before and after normalization, WB/exposure propagation, fallback | Phase 01 diagnostics; Phase 02 visible-noise features |
| Raw highlights | Per-plane states, nonlinearity, clipping/reconstruction uncertainty, boundary behavior | Phase 01 safety; Phase 02 artifact features |
| Scene luminance | Working space, coefficients, reference grey/white, negative-value policy, EV definition | Phase 02 |
| Bilateral/guided/local-Laplacian analysis | Equations, edge assumptions, halos/gradient reversal, scale/cost, mask use | Phase 02 |
| Halo measurement | Edge selection, normal profiles, reversal/overshoot/band terms, visibility weighting, labeled fixtures | Phase 02 |
| Color and WB | Camera/as-shot meaning, estimator assumptions, disagreement, hue/gamut metric, mixed-light policy | Phase 02 |
| Display/perceptual terms | Display model, contrast representation, reference requirements, units, licensing/runtime | Phase 02 diagnostics and Phase 03 objective |
| Objective structure | Hard constraints, range goals, normalized terms, uncertainty, no-unique-ground-truth policy | Phase 03 |
| Derivative-free optimization | Bounds/constraints, noisy evaluation behavior, convergence, determinism, evaluation cost | Phase 04 |
| Human acceptability | Labels, pairwise protocol, acceptable range, reviewer consistency, dataset split | Phase 00 corpus design and Phase 07 completion |

## Phase Reading Routes

### Phase 00

Read the complete iterative research bundle in its required order. Fully work
through the highest-priority queue in `source-ledger.md`. Use
`validation-data-and-experiments.md` to define the corpus and
`current-system-audit.md` to freeze the baseline.

### Phase 01

Read:

```text
../code-web-research-readbacks-and-dng.md
../auto-raw-processing-math-and-science.md
../iterative-raw-solver-research/data-domains-and-pipeline-order.md
../iterative-raw-solver-research/measurement-and-feature-catalog.md
the accepted Phase 00 DNG/noise/highlight crosswalks
```

Recheck current DNG/LibRaw/OpenGL code before editing.

### Phase 02

Read the feature catalog, accepted Phase 00 derivations, data-domain order,
validation experiments, and the source-ledger entries for guided filtering,
local Laplacian, bilateral tone mapping, color constancy, HDR-VDP/TMQI, and
display-adaptive tone mapping.

### Phase 03

Read objective/constraints, optimizer design, sampling design, implementation
contract, and accepted Phase 02 feature ledger. Do not let a research-only
feature enter candidate scoring.

### Phase 04

Read the accepted objective-surface records before reading optimizer papers.
Algorithm selection follows the measured surface; the paper does not select
the algorithm by reputation.

### Phase 05

Read the selected optimizer decision, candidate/cache/cancellation contract,
accepted objective terms, and full-resolution agreement evidence. Preserve the
no-mutation rule.

### Phase 06

Read UI ownership, one-click alignment, current active pass, native state tests,
and the Phase 05 dry-run checkpoint. This phase consumes solver science; it
does not silently change it while adding apply behavior.

### Phase 07

Read the complete validation plan, frozen corpus manifest, baseline record,
solver version record, and program completion contract. Changes triggered by
test failures return to the owning phase and create a new frozen version.

## Formula Provenance Rule

Every formula moved into code or an implementation specification must record:

```text
formula id and version
citation or local derivation path
variables and units
data domain and color space
reference grey/white or display model
validity assumptions
uncertainty and fallback
fixtures
corpus evidence
owning phase
```

Do not copy an equation between raw, scene-linear, and display domains without
an explicit conversion and justification.

## Research Expansion Rule

New online research is appropriate when a phase exposes a concrete missing
equation, assumption, or failure mode. Add it to the research bundle and source
ledger first, then create the Stack-specific extraction required by the
checkpoint protocol.

Do not expand research merely to collect more methods after an accepted method
already meets the phase gate. The goal is evidence sufficient for a safe
decision, not an exhaustive image-science encyclopedia.

