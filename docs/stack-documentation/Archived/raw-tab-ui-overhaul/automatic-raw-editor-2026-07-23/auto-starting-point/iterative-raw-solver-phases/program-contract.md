# Iterative RAW Solver Program Contract

## Product Objective

The completed system is a precise one-click RAW Starting Point solver. It uses
raw, scene-linear, regional, tone, and display evidence to search bounded
alternatives, then applies one technically strong and naturally editable
starting recipe.

The solver is not required to reproduce one expert's finished photograph. It
must deliver a defensible handoff from automatic analysis to manual editing.

## Start Line

The current Pass 94 heuristic solver is the frozen behavioral baseline. It is
retained as the fast mode, warm start, fallback, safety prior, and comparison
candidate.

The first new solver implementation work is non-mutating diagnostic evidence:

```text
source/decode identity
-> raw metadata and mosaic measurements
-> units, stage, validity, uncertainty, and provenance
-> diagnostics and automated fixtures
```

No optimizer, graph search, or candidate application starts until those
measurements pass their phase gates.

## Finish Line

The program is functionally complete when one explicit Starting Point action
can perform this sequence:

```text
validate source and ownership
-> build cached raw technical evidence
-> measure a neutral/current scene baseline
-> run the Pass 94 heuristic warm start
-> derive safe visible-control bounds
-> render and score competing proxy candidates
-> reject hard-constraint violations
-> refine accepted candidates with backtracking
-> stop by convergence or report budget/failure honestly
-> verify the best candidate at full resolution
-> atomically write visible controls
-> preserve one-action Undo and diagnostic evidence
```

The accepted parameter subset may include:

- RAW Exposure;
- Suggested White Balance only when policy and evidence allow it;
- Local Range graph points and any visible owned mask/region parameters;
- Finish Tone graph points;
- Display Fit / View Transform values.

Every applied numeric value must match the visible recipe and survive project
save/load. There is no hidden image correction behind the sliders.

## Required Functional Behavior

### Technical understanding

The finished solver must distinguish at least:

- dark but recoverable data from clipped or noise-dominated data;
- wide dynamic range from sensor clipping;
- one-channel, multi-channel, and all-channel clipping;
- scene placement from local relighting and display mapping;
- high-key or low-key intent from accidental under/overexposure when evidence
  supports that distinction;
- subject/background or regional conflicts from globally dark images;
- camera/as-shot WB from image-derived neutral estimates;
- proxy evidence from full-resolution evidence;
- converged improvement from evaluation-budget exhaustion.

### Search behavior

The finished solver must evaluate actual rendered alternatives. Reapplying the
same formula over several passes does not satisfy this contract.

It must be able to:

- use the current heuristic result as a warm start;
- explore neighboring Exposure, graph, and display values;
- enforce source, ownership, raw safety, and graph constraints before lower
  priority scoring;
- accept, reject, and backtrack;
- reduce its search region as it gains evidence;
- retain the warm start when no candidate is safely better;
- produce deterministic results for the same source, recipe, feature version,
  mode, and evaluation budget unless a deliberately stochastic method is
  explicitly approved;
- explain why the winning candidate beat the warm start.

### Difficult-image behavior

The validation set must demonstrate defensible behavior for:

- severely underexposed but recoverable RAWs;
- high dynamic range with no sensor clipping;
- partial-channel and all-channel clipping;
- backlit people and objects;
- high-ISO shadow recovery;
- high-key and intentionally low-key scenes;
- mixed/artificial illumination and intentionally warm or cool scenes;
- saturated lights, neon, fire, sunsets, and specular highlights;
- smooth bright boundaries where halos are easy to create;
- rotated, cropped, and orientation-tagged images;
- existing user-owned graphs;
- ordinary well-exposed images that should change little.

The solver does not have to make the same stylistic decision for all of these.
It has to avoid critical technical failures and return an editable starting
point or honestly retain the baseline.

## Non-Negotiable Invariants

1. Automatic output is represented by visible manual controls.
2. User-owned edits are preserved unless an explicit approved policy says the
   requested solve may replace them.
3. Raw sensor safety cannot be inferred from final display pixels.
4. A lower-tier aesthetic improvement cannot buy a raw-safety or graph-safety
   violation.
5. The current project recipe remains unchanged until final apply.
6. Final apply is atomic and covered by one Undo snapshot.
7. A source, recipe, or decode identity change cancels stale work.
8. Full-resolution verification precedes production apply.
9. Missing evidence becomes uncertainty or a blocked result, never an invented
   zero or false certainty.
10. Diagnostics distinguish converged, budget exhausted, warm start retained,
    blocked, canceled, and failed-render outcomes.
11. The fast heuristic path remains available when precise solving is
    unavailable or unnecessary.
12. A well-exposed image is allowed to remain nearly unchanged.

## What “Strong” Means

Strength is not a fixed EV lift. A strong result uses as much recoverable
information as the evidence safely supports.

It may use substantial RAW Exposure or Local Range lift on a dark clean RAW
with headroom. It may use little global lift and stronger regional correction
on a backlit subject. It may hold back on a high-ISO file whose shadows are
noise dominated. It may compress or protect highlights without creating a
bright edge band. It may deliberately leave a low-key image dark.

The solver is strong when it solves the actual measured problem rather than
when every slider moves far.

## What “Smart” Means

The solver is smart only if it can demonstrate all of these:

- richer evidence than global brightness percentiles;
- correct data-domain and stage provenance;
- competing rendered candidates;
- explicit constraints and objective terms;
- uncertainty-aware search limits;
- calibrated stopping and fallback behavior;
- measured improvement over Pass 94 on a locked corpus;
- human-review agreement on technical acceptability;
- visible, editable, understandable output.

A larger collection of thresholds without those properties is not the target.

## Completion Gate

The program is complete only when Phase 07 records that:

- every named critical failure family is represented in the locked test set;
- the precise solver improves accepted outcomes over Pass 94 without raising
  critical-failure rates;
- full-resolution verification catches proxy-only failures;
- native graph/slider handoff, Undo, cancellation, save/load, and source switch
  behavior pass;
- diagnostics explain representative decisions;
- repeated runs are deterministic within the declared contract;
- the product behavior has been manually accepted across the agreed corpus;
- unresolved limitations are documented and fail safely.

## Stop Boundary

This program stops after it produces an excellent technical starting point.
It does not own:

- a final creative or stylized grade;
- automatic crop, straightening, retouching, healing, object removal, or
  compositing;
- content-aware creative effects;
- continuous automatic rewriting while the user edits;
- forcing every image toward one median, histogram, scene key, or “normal”
  brightness;
- treating image-derived white balance or semantic detection as unquestioned
  truth;
- inventing detail when all raw channels are clipped;
- a hidden denoise, demosaic, highlight-reconstruction, lens-correction, or
  profile-enhancement layer outside Stack's owned RAW pipeline and visible
  product contract;
- mandatory machine learning or cloud inference;
- personalized aesthetic learning;
- unlimited search for a mathematically perfect image;
- broad redesign of unrelated Editor Graph or RAW-tab features.

If research reveals that a needed correction has no visible Stack control, it
remains diagnostic or becomes a separately approved product workstream. It is
not smuggled into this solver.

## End-State User Experience

The intended experience is:

> Open a RAW, click Build Starting Point in the precise mode, wait while Stack
> evaluates the image, and receive a strong but natural editable starting point
> across the appropriate sliders and graphs.

After apply, the solver stops. The user owns the recipe.
