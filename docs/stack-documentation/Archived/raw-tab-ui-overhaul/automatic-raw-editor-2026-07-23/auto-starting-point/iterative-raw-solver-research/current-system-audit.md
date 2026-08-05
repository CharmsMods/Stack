# Current Solver Audit: Heuristic Loop Versus Optimization

- Captured: 2026-07-09
- Source: current Stack code and the iterative RAW solver research intake
- Type: research
- Topic: iterative-raw-solver
- Verification: verified against the current July 9, 2026 code paths

## Conclusion

Stack currently has a closed-loop heuristic planner. It measures real image
data, writes visible controls, renders again, and can run another pass. It is
not yet a numerical optimizer.

The distinction matters:

```text
Current loop:
measure -> fixed decision formulas -> apply safe visible subset -> re-render

Future optimizer:
measure -> define bounded parameter space -> render multiple alternatives
-> evaluate objective and constraints -> choose/refine -> converge
```

## What Is Already Data-Driven

- RAW and current-frame percentiles in luma and EV.
- Dynamic-range estimates and log-average luminance.
- Per-channel and display clipping reports where evidence exists.
- Noise/detail recommendations from metadata and current measurements.
- Regional summaries for bright background, sky, foliage, shadows, and center.
- Named stage evidence for Raw Technical, Neutral Scene, Raw Placement, Local
  Candidate, Finish Tone Candidate, and Display Candidate.
- Re-rendering after visible upstream changes.
- Recipe and evidence identity checks that reject stale candidate measurements.

These are valuable foundations for optimization. They should be extended and
given explicit uncertainty, not replaced by a black box.

## What Is Formula- or Threshold-Driven

The current decisions are generated from hand-selected constants and fixed
curve families. Representative examples include:

| Area | Current fixed structure |
| --- | --- |
| RAW Exposure | Median target relative to the white anchor, confidence gates, small-move threshold, and fixed EV caps. |
| Local Range | Fixed confidence gates, maximum deltas, at most two selected suggestions, fixed fallback width/feather, and detector-specific thresholds. |
| Finish Tone | Five fixed horizontal knot locations with vertical positions calculated from dark or range scores. |
| Display Fit | Fixed black/white margins, middle-grey clamp, shoulder/toe ranges, and contrast formula. |
| Candidate score | Fixed term weights and fixed noise, color, and hidden-compensation penalties. |
| Iteration | Maximum of four upstream apply passes. |

The measurements vary by image, so the output is not a static preset. The
mapping from measurements to output is nevertheless largely predetermined.

## What The Four-Pass Loop Actually Converges

The current loop provides dependency convergence, not objective convergence.
It allows a later stage to become safe after an earlier stage has rendered. For
example:

```text
pass 1: apply RAW Exposure
pass 2: measure new scene and apply Local Range
pass 3: measure post-local scene and apply Finish Tone
pass 4: obtain final evidence and fit the display
```

The loop does not currently:

- render several competing exposure values in one pass;
- compare different Local Range graph locations or widths;
- move tone knots in response to a decreasing loss;
- backtrack when a later result is worse;
- estimate a gradient, local response model, or trust region;
- prove that the final candidate is better than nearby candidates;
- stop because an objective improvement fell below a tolerance.

It stops because no additional safe visible change is proposed, because
evidence is pending, or because the pass limit is reached.

## Candidate Scores Are Not Yet A Search

Stack scores a small family of candidates using Raw Safety, Scene Placement,
Local Conflict, Tone Shape, Display Readability, and Edit Conservatism. That is
useful diagnostic structure. The family is still discrete and primarily
policy-defined. Candidate scoring does not currently generate a continuous
neighborhood of alternatives around Exposure, graph knots, and Display Fit.

The future design should preserve the score decomposition but promote it into
an objective/constraint system whose terms can be measured on every rendered
candidate.

## Existing Strengths To Preserve

1. All accepted work is represented by visible recipe controls.
2. Raw Technical evidence can block unsafe positive exposure.
3. Candidate stages record whether evidence is complete, projected, pending,
   fallback, or unavailable.
4. Recipe fingerprints prevent reusing evidence from a different control state.
5. One user action has one undo snapshot.
6. Existing manual non-neutral graphs are not silently overwritten.
7. Diagnostics expose changed, withheld, pending, and rejected controls.

## Research Gaps

| Gap | Why it matters |
| --- | --- |
| No explicit objective function | There is no measurable definition of “better” across nearby candidates. |
| No uncertainty propagation | A noisy percentile and a reliable raw headroom measurement can influence policy too similarly. |
| Limited sensor metadata | Masked black pixels, linear response limit, full noise profiles, and camera profile/gain metadata are incomplete. |
| Limited multiscale analysis | Global percentiles cannot describe local contrast preservation or halos. |
| Limited color objective | White balance, hue stability, gamut pressure, and partial-channel clipping need coupled evaluation. |
| Limited semantic evidence | Subject importance and multiple illuminants remain weakly represented. |
| No alternative render search | Fixed formulas are not tested against neighboring values. |
| No calibrated stopping rule | Four passes bound runtime but do not establish convergence. |
| No representative corpus | Constants and objectives cannot be calibrated or rejected reliably. |

## Audit Decision

The current solver is an appropriate baseline and warm-start generator. The
next system should be researched as a constrained optimizer around that
baseline, with the heuristic path retained as:

- a fast mode;
- a safe fallback when optimization cannot run;
- an initial candidate;
- a prior that limits unreasonable search regions;
- a comparison baseline in every experiment.

No current constant should be promoted into a permanent mathematical truth
without evidence from the validation corpus described in
`validation-data-and-experiments.md`.

