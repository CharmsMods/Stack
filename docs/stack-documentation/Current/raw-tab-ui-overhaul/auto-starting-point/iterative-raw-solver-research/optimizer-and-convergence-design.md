# Optimizer And Convergence Design

- Captured: 2026-07-09
- Source: derivative-free optimization research, current Stack staged rendering, and imaging objective research
- Type: research
- Topic: iterative-raw-solver
- Verification: design hypothesis; no optimizer has been selected or implemented

## Goal

Define how a slower precise solver could explore visible Stack control values,
use rendered evidence, and stop because it converged—not merely because it
reached a small fixed pass count.

## Why The Problem Is A Black-Box Optimization Candidate

Stack's renderer includes clamps, masks, graph interpolation, clipping,
demosaicing effects, color transforms, and non-linear display mapping. A useful
quality objective may include connected components, multiscale filters,
perceptual metrics, and semantic masks. Analytic derivatives through the entire
C++/OpenGL path are not currently available.

The initial research model is therefore:

```text
theta -> render(theta) -> measure(theta) -> constraints(theta), score(theta)
```

where each evaluation is expensive and may be noisy at proxy resolution.

## Use The Current Solver As A Warm Start

The current rule-based solver should provide `theta_0` plus safe parameter
bounds. This gives the precise solver:

- a valid visible recipe;
- a fast acceptable fallback;
- raw exposure/headroom restrictions;
- initial region proposals;
- a likely useful curve family;
- a deterministic reference for improvement.

The optimizer should be required to beat the warm start by a meaningful margin
without violating any higher-tier constraint.

## Parameterization

Do not begin by optimizing every serialized field.

### First Technical Subspace

```text
e_raw
one Local Range lift target, delta, and width
three interior Finish Tone y values
display black EV, white EV, middle grey, shoulder, toe
```

Graph x positions can initially come from measured EV quantiles while y values
and widths are optimized. Later research can allow bounded x movement or more
knots if the simpler space fails.

### Conditional Dimensions

- WB only when metadata/policy and neutral evidence allow it.
- A second Local Range point only when one point cannot resolve two conflicting
  regions.
- Color target and region-mask parameters only when mask evidence is reliable.
- Reconstruction and denoise parameters remain outside the first visible
  optimization space unless Stack exposes them as owned manual controls.

## Proposed Hybrid Solve

### Phase A: Build Evidence And Bounds

1. Decode and cache raw technical evidence.
2. Render the neutral/current recipe at proxy resolution.
3. Estimate uncertainty and active regions.
4. Derive feasible bounds from headroom, graph validity, user ownership, and
   control limits.
5. Run the current heuristic to produce `theta_0`.

### Phase B: Global Exposure Search

Exposure is one-dimensional and should not require a general optimizer.
Evaluate a bounded bracket around the warm start using raw headroom as a hard
ceiling. Fit a local response model or use safeguarded interval refinement.

The best Exposure candidate should be judged before Display Fit is allowed to
hide its scene-placement error.

### Phase C: Local Range Search

Generate target EV locations from region medians, lower quantiles, subject
contrast, and clipped-region boundaries. For each target, search a bounded
delta and width. Render candidates in batches and reject those with halo,
noise, mask-leak, or highlight violations.

This is a mixed discrete/continuous problem:

```text
discrete: which region/problem to target
continuous: target EV, delta EV, width, feather, mask strength
```

### Phase D: Finish Tone Search

Optimize a small monotonic curve after Exposure and Local Range settle. A
constrained quadratic or derivative-free step can adjust the interior knot
values while enforcing endpoints, monotonicity, slope, and curvature limits.

### Phase E: Display Fit Search

Fit scene bounds to the display, then search small shoulder/toe/contrast
refinements against display and perceptual losses. Do not let this phase repair
an upstream candidate that failed scene-placement or local-contrast goals.

### Phase F: Joint Refinement

Once the ordered stages produce a safe candidate, allow a small joint trust
region across the active controls. This can correct interactions such as:

- less RAW Exposure plus more Local Range;
- a smaller Local Range lift plus a gentler tone curve;
- slightly different white EV plus less upper-tone compression.

### Phase G: Full-Resolution Verification

Render the best proxy candidate at full resolution. Recompute raw/display
safety, noise, mask edges, halo metrics, graph state, and output perception.
Reject or shrink toward the warm start if proxy/full-resolution disagreement
exceeds tolerance.

## Optimizer Candidates

| Method | Strength | Risk | Initial role |
| --- | --- | --- | --- |
| Structured coordinate/pattern search | Deterministic, easy to inspect, exploits control ownership and stage order. | Can miss coupled directions or stall on narrow valleys. | Strong first prototype. |
| Model-based derivative-free trust region | Uses expensive evaluations efficiently and builds a local response model. | Constraint integration and noisy proxies require care. | Strong candidate for joint refinement. |
| Constrained quadratic curve solve | Natural for smooth/monotonic curve knots and contrast-distortion terms. | Requires sufficiently quadratic/convex local formulation. | Research for Finish Tone and Display Fit subproblems. |
| Bayesian optimization | Useful when evaluations are expensive; can model uncertainty and parallel batches. | Scaling and mixed constraints become complex; surrogate choices can dominate behavior. | Compare after baseline DFO. |
| CMA-ES | Handles nonlinear, non-convex continuous black-box problems. | Many evaluations, stochastic variation, harder deterministic handoff. | Later benchmark, not first implementation. |
| Differentiable renderer/optimizer | Could use gradients and joint optimization. | Large architecture change; many current operations and metrics are non-differentiable. | Long-term research only. |
| Learned proposal network | Can generate content-dependent warm starts from examples. | Dataset bias, opaque failures, licensing/model runtime, no inherent safety. | Optional proposal/prior after deterministic solver. |

Primary optimization references:

- Powell-style derivative-free trust-region family and PDFO:
  https://arxiv.org/abs/2302.13246
- Bayesian optimization with variable-cost and parallel evaluations:
  https://papers.nips.cc/paper_files/paper/2012/hash/05311655a15b75fab86956663e1819cd-Abstract.html
- CMA-ES tutorial:
  https://arxiv.org/abs/1604.00772

## Candidate Batching

OpenGL can render a small batch of nearby candidates from shared upstream data.
Research should measure whether the following is practical:

```text
one upstream neutral render cache
4-8 proxy candidates per batch
parallel/stateless feature extraction
top candidates promoted to a finer proxy
one final full-resolution verification
```

Candidate identity must include all visible controls, decode state, proxy
resolution, feature version, and source hash.

## Multiresolution Strategy

Suggested research ladder:

```text
small proxy: global placement, rough regions, broad candidate rejection
medium proxy: masks, multiscale contrast, color, preliminary halo metrics
full resolution: noise, fine edges, reconstruction, final artifact validation
```

Some measurements do not scale predictably. Raw clipping can use sensor samples
independent of display proxy, while demosaic noise and thin halos need higher
resolution. The feature catalog must declare minimum valid resolution.

## Acceptance Rule

Use a monotone accepted-state sequence:

1. Reject any hard-constraint violation.
2. Require improvement in the active safety/technical tier.
3. Do not accept a lower-tier improvement that worsens a protected higher-tier
   term beyond tolerance.
4. Store the accepted candidate and its full evidence record.
5. Backtrack or shrink the trust region after rejection.

The current visible recipe should remain unchanged until the final accepted
candidate is ready, unless a future preview mode explicitly displays a
temporary candidate without claiming it is applied.

## Convergence Criteria

A precise solve may stop when all applicable criteria hold:

```text
relative objective improvement < epsilon_J for k accepted iterations
maximum normalized parameter step < epsilon_theta
no hard-constraint boundary is oscillating
proxy/full-resolution agreement is within tolerance
active region assignments are stable
maximum evaluation/time budget not exceeded
```

Budget exhaustion is not convergence. The UI and diagnostics must distinguish:

```text
converged
safe improvement found, budget exhausted
warm start retained
blocked by missing evidence
canceled by source/recipe change
failed candidate render
```

## Runtime Modes To Research

Do not set final times yet. Benchmark these conceptual modes:

- Fast: current heuristic loop.
- Refined: small deterministic neighborhood search.
- Precise: multistage proxy optimization plus full-resolution verification.
- Diagnostic: record candidate surfaces and objective terms without applying.

The user has explicitly allowed longer processing for greater reliability, but
runtime should buy measured candidate quality—not repeated application of the
same rule.

## Learned Methods As Proposals, Not Authorities

MIT-Adobe FiveK, Deep Bilateral Learning, and semantic-aware adjustment show
that content-dependent edits can be learned from human examples. Adobe's
Adaptive Profile also describes global, regional, and local tone behavior using
gain-table and semantic components.

Research implication:

- A learned system may propose `theta_0`, region masks, or objective priors.
- Raw safety, graph validity, ownership, and final visible projection remain
  deterministic constraints.
- Stack must retain a no-model solver and transparent diagnostics.

Sources:

- https://people.csail.mit.edu/sparis/publi/2011/cvpr_auto/Bychkovsky_11_Learning_Photo_Adjustment.pdf
- https://groups.csail.mit.edu/graphics/hdrnet/
- https://blog.adobe.com/en/publish/2024/10/14/the-adobe-adaptive-profile

## Prototype Gate

No optimizer prototype should begin until:

1. The first feature set is accepted.
2. Hard constraints are specified with units and stages.
3. The objective terms have test fixtures.
4. A representative RAW corpus exists.
5. The candidate-render cache and cancellation contract are designed.
6. Precise mode can run without changing the current recipe until final apply.
7. Diagnostics can explain why the winning candidate beat the warm start.

