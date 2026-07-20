# Parameter Space And Objective v0

## First Precise Parameter Space

The first maximum active vector has 13 visible dimensions:

```text
theta = [
  raw_exposure_ev,                         # 1
  local_target_ev, local_delta_ev,
  local_width_ev, local_feather,           # 4
  finish_y_1, finish_y_2, finish_y_3,      # 3
  display_black_ev, display_white_ev,
  display_middle_grey, display_shoulder,
  display_toe                              # 5
]
```

Most images use a smaller active subspace. Inactive dimensions are copied
exactly from the candidate's input recipe.

### Bounds And Activation

| Parameter | Initial bound | Activation/evidence |
| --- | --- | --- |
| RAW Exposure | Intersection of UI `[-8,+8]`, warm start `e0 +/- 2 EV`, and uncertainty-adjusted raw headroom | Always eligible when normalized raw safety is valid; negative and positive bounds are separate. |
| Local target | Within recipe `[minEv,maxEv]` (default `[-8,+6]`) and within a measured problem interval | Only for a reliable unresolved regional conflict. Target proposals are discrete measured anchors before local continuous refinement. |
| Local delta | `[-1,+1] EV`, further reduced by noise/highlight/halo evidence | One interior point maximum in v0; endpoints remain neutral. |
| Local width | `[0.02,1.5]` in the existing visible region-mask width convention or an equivalent declared EV width for EV masks | Active only with an owned visible mask representation. No hidden mask. |
| Local feather | `[0,1]` | Starts from an evidence-derived safe value; must pass boundary fixtures. |
| Finish `y_1..y_3` | `[0,1]`, ordered between fixed endpoints `(0,0)` and `(1,1)` | Fixed x locations are measured once from the accepted upstream candidate and frozen during the solve. |
| Display black EV | UI `[-16,0]`, with `blackEv < 0` | Active after upstream technical acceptance. |
| Display white EV | UI `[0,16]`, with sufficient separation from black | Same. |
| Middle grey | UI `[0.01,1]` | Same; no universal 0.18 target is imposed. |
| Shoulder | UI `[0.05,4]` | Same. |
| Toe | UI `[0,1]` | Same. |

The `+/-2 EV` exposure trust range is a conservative search envelope, not a
brightness target or permission to violate headroom. Phase 03 may shrink it;
widening requires an objective-surface record and raw fixtures.

### Explicit Deferrals

- White balance optimization: deferred. Phase 01/02 may report as-shot and
  estimator disagreement; the first precise vector keeps WB unchanged.
- Second Local Range point: deferred until one-point failure evidence exists.
- Free movement of Finish Tone x coordinates: deferred to keep graph order and
  search dimension stable.
- Local color target, semantic mask geometry, strength/smoothness/edge/detail
  controls, Display Exposure/contrast/saturation, and any hidden processing:
  deferred.

Existing user-owned non-neutral graphs block replacement in v0. A later
explicit policy may optimize an additive owned delta, but Phase 00 does not
authorize it.

## Objective Contract

The objective is lexicographic. A candidate first passes Tier 0 and Tier 1;
only then can lower tiers rank it.

### Tier 0: Identity, Ownership, And Graph Validity

| Term | Units/stage | Rule |
| --- | --- | --- |
| Source/decode/recipe match | Boolean, pre-render | Exact identity or reject. |
| Visible representation | Boolean, recipe | Every numeric change maps to a serialized visible control. |
| User ownership | Boolean, recipe | Existing user-owned graph/control remains unchanged or reject. |
| Finite/serializable | Boolean, recipe round trip | NaN, Inf, missing field, or unequal canonical round trip rejects. |
| Graph validity | order, count, slopes, curvature | Endpoints, order, capacity, monotonicity, and fixture-derived shape limits. |
| Candidate isolation | Boolean, application state | Current project recipe equals its pre-solve fingerprint. |

### Tier 1: Raw Safety And Irreversible Artifacts

| Term | Units/stage/reference | Rule and uncertainty |
| --- | --- | --- |
| WB-scaled raw headroom | EV by plane, normalized raw | Positive exposure below limiting-plane bound. Missing evidence blocks positive expansion. |
| Non-linear response pressure | Pixel/region fraction, normalized raw | Cannot materially increase without an explicit reconstruction policy; tag absence lowers confidence. |
| Partial/all clipping | Fraction and connected regions, aligned raw/CFA-aware stage | All-channel increase rejects; partial-channel boundary/color risk may reject. Mosaic-only counts cannot claim all-channel safety. |
| Halo reversal | Normalized edge gradient energy, scene/display profiles | High-confidence reversal rejects once Phase 02 calibrates threshold; until then recorded and candidate space remains conservative. |
| Graph/state failure | Boolean | Reject. |
| Full/proxy disagreement | Feature-specific normalized deltas | Final candidate rejects or shrinks to warm start when Phase 05 tolerances fail. |

### Tier 2: Technical Acceptable Ranges

Candidate terms are recorded separately:

```text
scene placement deficit by reliable region        [EV outside allowed range]
recoverable shadow visibility deficit             [EV/visible contrast]
predicted and rendered noise visibility           [normalized sigma/contrast]
highlight detail and rolloff discontinuity        [multiscale/profile units]
multiscale local contrast change                   [log-contrast distance]
halo overshoot/band/texture loss                   [normalized edge contrast]
hue shift and gamut pressure                       [degrees/distance/fraction]
```

The range loss is zero inside `[lo,hi]` and robust outside it. Every range
names corpus version, intent confidence, and uncertainty. No final range or
weight is approved by v0.

### Tier 3: Display And Perceptual Ranking

Record display black/white clip fractions, middle-grey readability, shoulder
and toe continuity, and multiscale contrast. Absolute perceptual terms require
a calibrated display/ambient model. Unknown viewing conditions use relative
terms with lower confidence. Display quality cannot compensate for a failed
upstream scene or raw constraint.

### Tier 4: Declared Intent

Only explicit or high-confidence reviewed intent may choose between already
safe, technically acceptable candidates. Unknown intent does nothing. It
cannot relax Tier 0 or Tier 1.

### Tier 5: Tie Breakers

Prefer, in order:

1. fewer changed visible controls;
2. smaller normalized L1 distance from the current/warm recipe;
3. lower full-resolution cost;
4. deterministic candidate order.

## Uncertainty

Every feature returns `(value, uncertainty, valid_fraction, reason)`. Bounds
shrink as uncertainty rises. A missing required feature blocks only dimensions
that depend on it and may retain the warm start. It never contributes a fake
zero to a weighted sum.

Phase 03 may compare expectation-plus-risk or bounded worst-case formulations
inside a tier. Phase 00 does not select `kappa`, weights, or a Pareto policy.

## Candidate Surface Requirements For Phase 04

Before optimizer selection, Phase 03 must record for identical bounds/budgets:

- determinism and render/feature noise at repeated points;
- discontinuities from clamps, masks, graph ordering, and connected regions;
- invalid/failing evaluation frequency;
- local curvature and coupling on named 1-D/2-D slices;
- scale of each normalized dimension;
- objective/constraint evaluation cost and batchability;
- number and width of acceptable basins;
- whether finite-difference directions are repeatable;
- proxy/full rank agreement.

These observations decide between structured pattern search, a model-based
trust region, a constrained subsolve, Bayesian optimization, or a benchmark.
PDFO's Powell-family methods explicitly address derivative-free trust regions
and failed/noisy evaluations
([primary overview](https://arxiv.org/abs/2302.13246)); Bayesian optimization
can model expensive variable-cost evaluations
([Snoek et al.](https://papers.nips.cc/paper_files/paper/2012/hash/05311655a15b75fab86956663e1819cd-Abstract.html));
CMA-ES is a stochastic nonlinear/non-convex continuous benchmark
([Hansen](https://arxiv.org/abs/1604.00772)). None is selected here.
