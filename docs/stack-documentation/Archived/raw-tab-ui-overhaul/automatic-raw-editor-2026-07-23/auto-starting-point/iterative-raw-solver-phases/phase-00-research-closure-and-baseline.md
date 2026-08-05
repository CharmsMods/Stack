# Phase 00: Research Closure And Baseline

- Program state: completed (`phase-00-v1`, PASS)
- Recipe mutation: prohibited
- Solver code: prohibited
- Owns: research acceptance, validation corpus, baseline freeze, and prototype entry contract

## Purpose

Convert the initial research bundle into Stack-specific, testable decisions and
establish the evidence needed to judge every later implementation. This is a
closure phase, not an invitation to continue broad research indefinitely.

## Required Reading

Read:

```text
../implementation-progress.md
../implementation-contract.md
README.md
program-contract.md
checkpoint-protocol.md
research-dependency-map.md
../iterative-raw-solver-research/README.md
```

Then follow the research bundle's complete required reading order and its
highest-priority source queue.

## Entry Conditions

- Pass 94 builds and its current native behavior is reproducible.
- The research folder and source ledger exist.
- The product start, finish, and stop boundaries in `program-contract.md` are
  accepted.
- A local location for user-owned RAW validation data can be defined without
  automatically committing large or proprietary files.

## Workstream A: Freeze The Baseline

Create a versioned Pass 94 baseline record containing:

- commit/worktree identity and build configuration;
- solver version and relevant constant version;
- changed controls and render count;
- stage diagnostics before and after;
- runtime and failure status;
- representative native screenshots or records where allowed;
- structured human labels;
- exact tests and commands.

Capture the already-used Tennis DNG cases plus ordinary, dark, wide-range, and
well-exposed examples. The baseline is not a claim of readiness; it is the
comparison every later method must beat.

## Workstream B: Create The Validation Corpus Contract

Define a manifest and storage policy covering:

- source hash and non-secret local path key;
- camera, ISO, exposure metadata, orientation, and crop state;
- scene and failure-category tags;
- metadata capabilities such as NoiseProfile, MaskedAreas, and
  LinearResponseLimit;
- partition: development, validation, or locked test;
- permission and redistribution status;
- expected technical fixtures where known;
- human review record links;
- near-duplicate/session grouping.

The corpus must cover every mandatory family in
`../iterative-raw-solver-research/validation-data-and-experiments.md`, including
well-exposed images that should barely change.

Large RAWs remain outside Git unless separately approved. Hashes, manifests,
small legal fixtures, and derived non-sensitive metrics may be versioned.

## Workstream C: Finish The Priority Math Extractions

At minimum, create Stack-specific derivations for:

1. DNG raw normalization, active/masked areas, black/white levels,
   BaselineExposure, AsShotNeutral, LinearResponseLimit, NoiseProfile, and
   opcode/gain/profile stage meaning.
2. Per-channel headroom and partial/all-channel clipping classification.
3. Raw shot/read-noise propagation through WB, Exposure, local lift, and
   display visibility.
4. Scene luminance and EV definitions with working-space and negative-value
   policy.
5. Bilateral, guided, and local-Laplacian alternatives for measurement/mask
   construction, including halo and gradient-reversal behavior.
6. A photographic halo/edge-profile measurement hypothesis and labeled
   fixtures.
7. Color/WB estimator disagreement, hue/gamut pressure, and mixed-light
   uncertainty.
8. Display-adaptive contrast and perceptual terms that are usable without a
   unique expert-edited reference.
9. Range-based technical objectives, hard constraints, and uncertainty.
10. Candidate-surface requirements that will decide the optimizer in Phase 04.

Every extraction follows `checkpoint-protocol.md` and updates the research
source ledger status.

## Workstream D: Define The First Accepted Parameter Space

Approve the smallest useful initial precise-solver parameter space. The
research hypothesis is:

```text
RAW Exposure
one Local Range target/delta/width/feather when justified
three interior Finish Tone y values with fixed measured x positions
Display Fit black EV, white EV, middle grey, shoulder, and toe
conditional WB only under existing policy
```

Any dimension not supported by reliable evidence is deferred explicitly. A
smaller approved space is better than pretending every serialized graph field
is safe to optimize.

## Workstream E: Define Objective And Constraint Version 0

Produce an inspectable specification containing:

- Tier 0 source/recipe/ownership/graph constraints;
- Tier 1 raw safety and irreversible artifact constraints;
- accepted Tier 2 technical terms;
- accepted Tier 3 display/perceptual terms;
- optional declared-intent ranking only after safety;
- uncertainty treatment;
- edit-minimality tie-breakers;
- feature version and stage dependencies;
- fixtures for every hard constraint;
- no final weights unless supported by the corpus.

Acceptable ranges are preferred over forcing one histogram or target value.

## Required Deliverables

- Frozen Pass 94 baseline specification and initial records.
- Versioned RAW corpus manifest/schema and split rules.
- Failure taxonomy and human-review template.
- Stack-specific math extraction notes for the required priority areas.
- Accepted/rejected/prototype/human-study feature ledger.
- First parameter-space decision.
- Objective/constraint v0 specification.
- Candidate identity, proxy, cache, cancellation, and no-mutation design.
- Automated-fixture plan for Phase 01 and Phase 02.

The completed, versioned deliverables are indexed at
[`phase-00/README.md`](phase-00/README.md). That checkpoint is the durable
handoff; this file remains the phase contract.

## Checkpoint

Phase 00 passes only when:

- every named critical failure family has development/validation coverage and
  a credible locked-test plan;
- Pass 94 is reproducible as a baseline;
- raw and graph constraints have concrete fixture definitions;
- the first feature set is classified rather than merely listed;
- objective terms name units, stage, reference, uncertainty, and validation;
- the first parameter space is explicitly bounded;
- no highest-priority implementation assumption remains only an “initial
  extraction”;
- Phase 01 can implement raw evidence without inventing formulas or fallbacks.

## Stop Rules

Do not:

- implement new solver behavior;
- tune Pass 94 constants from the new corpus;
- choose an optimizer before objective-surface evidence;
- add machine learning;
- define a single expert edit as ground truth;
- move to Phase 01 because the documents are long or the research feels
  sufficient.

## Handoff

Record the accepted math/specification versions, corpus version, baseline
version, rejected approaches, remaining optional research, and exact Phase 01
fixture list. Activate Phase 01 only through `../implementation-progress.md`.

Completed handoff: [`phase-00/README.md`](phase-00/README.md). Phase 01 was not
activated as part of this checkpoint.
