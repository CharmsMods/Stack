# Iterative RAW Solver Research

- Captured: 2026-07-09 18:41
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-09-1841-iterative-raw-solver-research.md`
- Type: research
- Topic: iterative-raw-solver
- Verification: partially-verified from current code, primary papers, standards, and official product documentation

## Purpose

This folder is the research home for a future RAW Starting Point solver that
does more than run fixed formulas after measuring a few brightness statistics.
It owns the mathematical and data-science investigation needed to define:

- what information can be recovered from the RAW mosaic, metadata, demosaiced
  scene-linear image, regional masks, and final display image;
- which measurements are valid at each processing stage;
- which operations must happen before or after other operations;
- how candidate visible controls should be rendered, measured, compared, and
  refined;
- how safety, perceptual quality, noise, color, local contrast, halos, and user
  intent become explicit constraints or objective terms;
- how a slower high-quality solve can converge instead of repeatedly applying
  the same threshold rules.

This is a research and specification bundle. It does not authorize solver code,
constant tuning, a machine-learning dependency, hidden image processing, or a
change to the active one-click behavior.

## Product Boundary

The future solver must still write its accepted result into Stack's visible,
editable controls. The initial parameter space is:

```text
RAW Exposure
Suggested White Balance when policy allows it
Local Range graph, color target, and region mask
Finish Tone graph
Display Fit / View Transform
```

Raw-domain reconstruction, denoising, demosaicing, camera profiling, and
semantic analysis may supply evidence or define safety bounds. They must not
silently become an unreported enhancement layer under the visible recipe.

## Relationship To Existing Research

The parent folder already contains strong first-generation research:

- `auto-starting-point-solver-research.md` describes the present
  percentile/formula-based control policy.
- `auto-raw-processing-math-and-science.md` separates raw mosaic evidence from
  demosaiced scene-linear evidence.
- `auto-starting-point-sampling-design.md` defines the existing staged render
  model.
- `implementation-contract.md` owns the non-negotiable visible-write,
  ownership, undo, and evidence rules.

This folder does not replace those documents. It starts the next research
question: how to turn their staged measurements into a constrained,
render-in-the-loop optimization system with quantified uncertainty and
convergence.

## Guided Phase Consumer

The gated research-to-production program that consumes this bundle lives at
`../iterative-raw-solver-phases/README.md`. That program points each numbered
phase back to the relevant math and image-science documents, requires
Stack-specific equation extraction before implementation, and prohibits recipe
mutation through its dry-run phase.

This research folder remains the evidence source. The guided phase folder owns
sequencing and checkpoints. Neither folder activates code work; the parent
`implementation-progress.md` remains the active pass authority.

## Required Reading Order

```text
README.md
research-session-handoff.md
current-system-audit.md
data-domains-and-pipeline-order.md
measurement-and-feature-catalog.md
objective-function-and-constraints.md
optimizer-and-convergence-design.md
validation-data-and-experiments.md
source-ledger.md
```

Before any future implementation, also reread the parent folder's
`implementation-progress.md`, `implementation-contract.md`, and
`implementation-pass-readiness.md`.

## Document Ownership

| Document | Owns |
| --- | --- |
| `research-session-handoff.md` | First-session conclusions, verification limits, unresolved risks, and exact resume sequence after compaction. |
| `current-system-audit.md` | Exact distinction between the current heuristic loop and a true optimizer. |
| `data-domains-and-pipeline-order.md` | Processing stages, data validity, dependencies, and ordering hypotheses. |
| `measurement-and-feature-catalog.md` | Candidate measurements, formulas, uncertainty, provenance, and feature records. |
| `objective-function-and-constraints.md` | Parameter vector, hard constraints, losses, priorities, and user-intent boundary. |
| `optimizer-and-convergence-design.md` | Search methods, candidate rendering, acceptance, stopping, caching, and failure behavior. |
| `validation-data-and-experiments.md` | RAW corpus, experiments, metrics, human review, and readiness gates. |
| `source-ledger.md` | Source quality, research extraction status, unresolved claims, and follow-up reading. |

## Core Research Questions

1. What is technically recoverable, merely estimable, or permanently lost in
   each RAW image?
2. Which statistics describe the sensor and which describe the current render?
3. How should global scene placement be separated from local relighting and
   display mapping?
4. How can noise amplification, clipping, hue shifts, halos, and gradient
   reversals be predicted before accepting a candidate?
5. What objective can rank two safe candidates without pretending there is one
   universal aesthetically correct photograph?
6. Which parameters can be optimized jointly, and which require a strict
   dependency order?
7. What derivative-free or constrained method gives useful convergence with an
   expensive OpenGL render in the loop?
8. How should uncertainty change the allowed search radius and stopping rule?
9. How do we validate on diverse real RAWs without training and testing on the
   same photographic preferences?
10. How does every accepted mathematical parameter map back to visible Stack
    graph points and sliders?

## Working Thesis

The current solver should become a deterministic warm start and safety prior,
not be discarded. A future precise mode can then:

```text
measure -> bound the safe space -> propose candidates -> render proxies
-> score -> refine -> full-resolution verify -> write visible controls
```

Safety should be lexicographic: no improvement in aesthetic or display score
may compensate for violating a raw headroom, graph validity, user ownership, or
artifact constraint.

## Research Rules

- Prefer primary papers, standards, official documentation, and source code.
- Record source limitations and the exact claim supported by each source.
- Distinguish sensor-domain clipping from rendered/display clipping.
- Distinguish technical quality from aesthetic preference.
- Do not assign final objective weights until a representative RAW corpus and
  human-review protocol exist.
- Do not treat an expert-edited image as a unique ground truth; multiple valid
  renditions can exist.
- Do not cite a learned method as evidence that Stack should adopt machine
  learning. Learned proposals are one option to compare against interpretable
  optimization.
- Preserve formulas with units, data domain, reference white/grey, and sampling
  stage.
- Mark unimplemented measurements and metadata explicitly.

## Initial Research Result

The first source pass supports a hybrid direction:

1. Use raw metadata and mosaic measurements to establish black level, white
   level, channel headroom, non-linearity limits, and signal-dependent noise.
2. Use demosaiced scene-linear, multiscale, edge-aware, chromatic, regional,
   and semantic measurements to define candidate quality.
3. Use constrained visible curves and sliders as the optimization variables.
4. Use the current heuristic result as a warm start.
5. Use batched proxy renders with a derivative-free trust-region or structured
   coordinate search before considering more expensive stochastic methods.
6. Verify the final candidate at full resolution and reject it if its safety or
   artifact measurements do not agree with the proxy.

This is a hypothesis to test, not an implementation decision.

## Follow-Up

Phase 00 completed the implementation-critical source reading and converted the
first candidate measurements into:

```text
accepted measurement
rejected measurement
needs prototype
needs human-label study
```

The accepted math, feature ledger, corpus, objective v0, and fixtures are
indexed by `../iterative-raw-solver-phases/phase-00/README.md`. Further online
research is triggered by a failed fixture or a concrete later-phase question;
it is no longer an open-ended prerequisite for Phase 01.
