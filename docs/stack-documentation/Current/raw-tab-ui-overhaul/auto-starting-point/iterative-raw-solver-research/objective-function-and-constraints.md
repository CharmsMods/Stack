# Objective Function And Constraints

- Captured: 2026-07-09
- Source: current Stack candidate scoring, display-adaptive tone mapping, perceptual metrics, and derivative-free optimization research
- Type: research
- Topic: iterative-raw-solver
- Verification: mathematical design draft; no weights or losses are approved

## Problem Definition

Let the editable candidate parameter vector be:

```text
theta = [
  e_raw,
  wb,
  local_range_knots,
  local_color_target,
  local_region_mask,
  finish_tone_knots,
  display_exposure,
  display_black_ev,
  display_white_ev,
  middle_grey,
  shoulder,
  toe,
  display_contrast,
  display_saturation
]
```

Not every solve should enable every dimension. The active subspace depends on
evidence, policy, user ownership, and uncertainty.

Let `R_s(theta)` be a render at stage `s`, and `F_s` be the feature extractor
for that stage. The optimizer observes:

```text
z_s(theta) = F_s(R_s(theta), rawEvidence, metadata)
```

The goal is not simply to maximize brightness. It is to find a safe, editable
candidate that improves a declared set of technical and perceptual objectives.

## Safety Must Be Lexicographic

The solver should evaluate tiers in order:

```text
Tier 0: source, recipe, graph, and ownership validity
Tier 1: raw safety and irreversible artifact constraints
Tier 2: technical image quality
Tier 3: perceptual/display quality
Tier 4: aesthetic or user-intent preference
Tier 5: edit minimality and runtime tie-breakers
```

A Tier 4 improvement cannot pay for a Tier 1 violation. This is safer and more
explainable than one weighted sum in which enough “beauty” could outweigh raw
clipping.

## Hard Constraints

Initial hard-constraint candidates:

### Identity And Ownership

- Candidate evidence matches source hash and exact recipe fingerprint.
- User-owned non-neutral graphs are not overwritten without explicit policy.
- All accepted operations map to visible Stack controls.
- Parameter values remain serializable, editable, and undoable.

### Raw Safety

- Positive Exposure remains below the uncertainty-adjusted limiting-channel
  headroom.
- Candidate does not increase all-channel raw clipping beyond tolerance.
- Partial-channel clipping and non-linear-response regions do not cross a
  policy threshold without reconstruction evidence.
- Invalid or missing raw evidence cannot be replaced by display statistics.

### Graph Validity

- Finish Tone is monotone when monotonic mode is required.
- Endpoints and domain remain valid.
- Slope and curvature stay within artifact-tested bounds.
- Local Range point capacity, ordering, and mask validity are preserved.
- Candidate cannot introduce NaN, infinity, negative illegal values, or
  discontinuities.

### Runtime And State

- Solve cancels on source or recipe change.
- Candidate render failures cannot partially overwrite the accepted recipe.
- Full-resolution verification is required before final apply in precise mode.

## Soft Objective Families

A provisional technical/perceptual cost is:

```text
J(theta) =
    w_place   * L_place(theta)
  + w_shadow  * L_shadow(theta)
  + w_high    * L_highlight(theta)
  + w_local   * L_local_contrast(theta)
  + w_tone    * L_tone(theta)
  + w_color   * L_color(theta)
  + w_noise   * L_noise(theta)
  + w_halo    * L_halo(theta)
  + w_display * L_display(theta)
  + w_edit    * L_edit(theta)
  + w_smooth  * L_smooth(theta)
  + w_uncert  * L_uncertainty(theta)
```

This equation organizes research. It does not approve weights or imply every
term should be scalarized together.

## Candidate Loss Definitions

### Scene Placement

Use robust full-image and region-weighted placement, not a single permanent
median target:

```text
L_place = rho(key_full - target_full)
        + alpha_subject * rho(key_subject - target_subject)
        + alpha_bg * rho(key_background - allowed_background)
```

`rho` should be robust, such as Huber or bounded loss. Targets may be ranges
rather than points and should depend on declared intent and scene class.

### Shadows

Reward visibility only where recoverable and important:

```text
L_shadow = visibility_deficit
         + noise_weight * predicted_noise_amplification
         + black_crush_penalty
```

### Highlights

Separate raw safety from display appearance:

```text
L_highlight = partial_channel_color_risk
            + display_rolloff_discontinuity
            + highlight_detail_loss
            + reconstruction_uncertainty
```

All-channel raw clipping is not recoverable luminance evidence and should not
be rewarded for invented detail.

### Local Contrast And Structure

At scales `s` and regions `r`:

```text
L_local_contrast = sum_r sum_s a_rs * distance(D_ref_rs, D_candidate_rs)
```

Weights should emphasize subject boundaries and visually important structures
without turning sensor noise into “detail.”

### Noise

Provisional noise cost:

```text
L_noise = mean_affected(
    importance(x) * visibility(x) *
    max(0, sigma_after(x) - allowed_sigma(x))
)
```

The model should combine raw predicted noise, rendered residual noise, local
lift, and display visibility.

### Halos

Use edge profile reversals, overshoot, and band energy from
`measurement-and-feature-catalog.md`. A strong halo penalty should be close to
a hard constraint when confidence is high.

### Color

Possible components:

```text
L_color = hue_shift
        + gamut_distance
        + saturation_clipping
        + neutral_error
        + skin_or_memory_color_penalty_when_confident
```

Technical WB and creative mood must be separate. The solver should not force a
stylized warm or cool image to a neutral estimate merely because an estimator
can produce one.

### Display

Candidate terms may draw from display-adaptive contrast distortion, HDR-VDP,
or TMQI-inspired structural/naturalness measurements:

```text
L_display = visible_contrast_distortion
          + output_clip_penalty
          + midgrey_readability_penalty
          + gamut_and_hue_penalty
```

The display loss must also penalize hidden compensation when a readable output
depends on leaving upstream scene placement or tone in a poor state.

### Edit Minimality

Prefer the smallest sufficient visible edit:

```text
L_edit = ||W * (theta - theta_current)||_1
       + changed_control_count_penalty
```

This encourages understandable handoff and reduces unnecessary curve motion.

### Curve Smoothness

For ordered knots `(x_i, y_i)`:

```text
L_smooth = sum_i (y_{i+1} - 2*y_i + y_{i-1})^2
```

Slope and curvature bounds should still be explicit constraints. Smoothness
alone cannot prevent every artifact.

### Uncertainty

Possible robust formulation:

```text
J_robust(theta) = E[J(theta)] + kappa * sqrt(Var[J(theta)])
```

or evaluate worst-case cost over a bounded uncertainty set. The practical
choice depends on whether feature uncertainty can be calibrated.

## Multiobjective Alternatives

Research should compare:

1. Lexicographic constrained optimization.
2. Weighted sum within each safety tier.
3. Pareto-front generation followed by a declared product policy.
4. Goal programming with acceptable ranges instead of ideal points.
5. Separate technical solve followed by user-intent selection.

The initial recommendation is lexicographic safety plus range-based technical
goals. Aesthetic scoring should initially rank already-safe candidates, not
generate unconstrained corrections.

## No Unique Ground Truth

The MIT-Adobe FiveK work demonstrates that multiple trained retouchers produce
different valid adjustments. A future Stack objective should therefore learn
or encode a range of acceptable outcomes rather than reproduce one editor.

Potential intent profiles:

```text
Natural neutral starting point
Protect highlights
Low-noise shadow recovery
Bright subject
Flat grading base
Camera-intent preserving
```

These would alter soft targets and weights, not raw safety constraints.

## Required Research Before Weight Selection

- Feature reliability and correlation on real RAWs.
- Sensitivity of each loss to proxy resolution and demosaic method.
- Human ratings of acceptability, not only preference.
- Correlation between halo/noise metrics and reported artifacts.
- Pareto tradeoffs between shadow visibility, noise, and highlight protection.
- Stability across adjacent images and repeated runs.
- Whether objective terms duplicate or contradict one another.

No final numeric weights belong in this document until those experiments are
recorded.

