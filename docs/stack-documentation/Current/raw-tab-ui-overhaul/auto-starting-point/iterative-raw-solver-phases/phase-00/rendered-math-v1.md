# Rendered And Perceptual Math v1

## Domain

This note covers demosaiced scene-linear measurements and final display
measurements. Every feature must declare working space, transfer function,
reference grey/white, crop/orientation, proxy resolution, and the exact
candidate recipe. Raw safety remains in `raw-math-v1.md`.

## Edge-Aware Alternatives

### Bilateral

Durand and Dorsey's [bilateral HDR method](https://people.csail.mit.edu/fredo/PUBLI/Siggraph2002/DurandBilateral.pdf)
filters log intensity:

```text
J_s = (1/k(s)) * sum_p f(p-s) * g(I_p-I_s) * I_p
k(s) = sum_p f(p-s) * g(I_p-I_s)
```

The base/detail method compresses only the base range and retains detail. The
normalizer `k(s)` is useful support/confidence evidence; low support is an
uncertain mask. Risks include direct cost, parameter sensitivity, edge
over-sharpening, staircase behavior, and local W-estimator minima.

### Guided

He, Sun, and Tang's [guided filter](https://people.csail.mit.edu/kaiming/eccv10/eccv10ppt.pdf)
uses, in window `omega_k`:

```text
q_i = a_k*I_i + b_k
min sum_i ((a_k*I_i + b_k - p_i)^2 + epsilon*a_k^2)
a_k = cov(I,p) / (var(I) + epsilon)
b_k = mean(p) - a_k*mean(I)
```

Overlapping-window predictions are averaged. Radius and `epsilon` define scale
and edge sensitivity. The local relation `grad(q)=a*grad(I)` supports edge
alignment, but does not guarantee halo-free behavior; guide errors, low
variance, and averaging can leak or flatten boundaries.

### Local Laplacian

Paris, Hasinoff, and Kautz's
[Local Laplacian Filters](https://people.csail.mit.edu/sparis/publi/2011/siggraph/Paris_11_Local_Laplacian_Filters_lowres.pdf)
use Gaussian and Laplacian pyramids:

```text
G_0 = I
G_(l+1) = downsample(G_l)
L_l = G_l - upsample(G_(l+1))
```

For each output coefficient, a local reference `g0` remaps the input, builds
the required subpyramid, copies the coefficient, and collapses the pyramid.
One published family is:

```text
r_d(i) = g0 + sign(i-g0)*sigma_r*f_d(|i-g0|/sigma_r)
f_d(delta) = delta^alpha
r_e(i) = g0 + sign(i-g0)*(f_e(|i-g0|-sigma_r)+sigma_r)
f_e(a) = beta*a
```

`alpha` controls detail and `beta<1` edge/tone compression. It is a strong
multiscale prototype, but its original cost is approximately `O(N log N)` and
its parameters, noise response, proxy agreement, and Stack graph projection
still need tests.

### Decision

All three are `prototype-only`. Phase 02 compares them on identical fixtures.
No source proves that selecting one makes a photographic result halo-free.

## Halo And Gradient-Reversal Hypothesis

For each confident, non-texture-dominated edge, sample the neutral/reference
and candidate log-luminance along normals. Align profiles at the maximum
reference gradient and orient them dark-to-bright.

Let `r(t)` and `c(t)` be reference and candidate profiles, `d(t)=c(t)-r(t)`,
and `s` a scale band.

```text
reversal_s = integral 1[sign(c'(t)) != sign(r'(t))] * |c'(t)| dt
overshoot_s = max(0, max_bright(c)-plateau_bright)
            + max(0, plateau_dark-min_dark(c))
band_s = integral_near_edge |d(t)-plateau_side(t)| * w_s(t) dt
edge_shift_s = |argmax |c'(t)| - argmax |r'(t)||
```

Normalize luminance terms by robust local edge contrast and record the
denominator. Aggregate with robust high percentiles, never only a mean. Reject
profiles with insufficient support, clipping, a crop/border crossing, or
ambiguous reference direction.

Also measure texture variance just inside and away from each side of an edge;
their ratio detects contrast halos/texture suppression. Hessel and Morel's
[base/detail artifact evaluation](https://arxiv.org/abs/1808.09411) motivates
separate luminance halo, contrast halo, staircasing, and compartmentalization
fixtures. Trentacoste et al.,
[Countershading and Halos](https://www.cl.cam.ac.uk/~rkm38/pdfs/trentacoste12countershading.pdf),
shows that objectionability depends strongly on angular profile width and that
simple-edge thresholds do not fully generalize to complex images.

Therefore:

- edge-profile reversal may become a high-confidence hard constraint;
- overshoot, band energy, and texture loss remain separate recorded terms;
- viewing distance/display size or pixels-per-degree is required for a
  perceptual threshold;
- no numeric threshold is accepted until synthetic profiles and human-labeled
  photographs agree across scales.

## Color, Hue, And Gamut

Compare neutral and candidate in a declared scene-linear working space and in
a declared perceptual display space. Record:

```text
hue_shift = circular_distance(h_candidate, h_reference)
gamut_pressure = distance_to_output_gamut_before_mapping
saturation_clip = fraction at output channel limits
neutral_error = distance of high-confidence neutral samples from neutral axis
```

Weighting by chroma and reliability is required because hue is unstable near
neutral. Clipped or reconstruction-dependent highlights are reported
separately. Skin/memory-color penalties require confident labels and remain a
human-study feature, never a universal detector assumption.

Spatial disagreement between WB estimators is mixed-light evidence. It narrows
or disables global WB; Phase 00 does not authorize local WB.

## Display-Adaptive Contrast

Mantiuk, Daly, and Kerofsky's
[Display Adaptive Tone Mapping](https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk08datm.pdf)
models a display as:

```text
L_d(L') = (L')^gamma*(L_max-L_black) + L_black + L_refl
```

It represents log-luminance contrast with a Laplacian pyramid, bins by
background luminance, and uses monotone piecewise-linear tone differences
`d_i = y_(i+1)-y_i`. Its constrained problem requires:

```text
d_i >= 0
sum_i d_i <= log display dynamic range
```

After local linearization of the contrast transducer, it solves a small
quadratic program and iterates. The paper reports a compact node count and a
few linearize/solve iterations for its own calibrated setup.

Stack accepts these ideas as:

- multiscale contrast evidence;
- monotone display-range constraints;
- a future diagnostic for Finish Tone/Display Fit surfaces.

It does not accept the paper as the whole solver objective. Absolute display
luminance, black, reflection/ambient, and viewing condition are required for
its perceptual claim. With unknown display conditions, use normalized display
clipping, monotonicity, rolloff continuity, and relative contrast evidence
with lower confidence.

## Reference-Based Metrics

- [TMQI](https://ece.uwaterloo.ca/~z70wang/research/tmqi/) combines multiscale
  structural fidelity with statistical naturalness. It needs an HDR reference,
  and naturalness can penalize intentional high/low-key work. It is
  offline/advisory only.
- [HDR-VDP](https://www.cl.cam.ac.uk/~rkm38/software.html) predicts visible
  differences/quality using absolute luminance and a display model. It is
  offline/reference validation only when a valid reference and calibrated
  viewing condition exist.

Neither is accepted as a no-reference production objective.

## Acceptability Rather Than One Target

The [MIT-Adobe FiveK study](https://people.csail.mit.edu/sparis/publi/2011/cvpr_auto/Bychkovsky_11_Learning_Photo_Adjustment.pdf)
contains multiple trained retoucher results and shows why simple histogram
rules fail on high-key, low-key, and backlit scenes. The follow-up
[acceptable-adjustment research](https://projects.csail.mit.edu/acceptable-adj/)
supports modeling acceptable ranges rather than one adjustment.

Stack therefore uses range losses:

```text
range_loss(z; lo,hi) = 0                 if lo <= z <= hi
                      robust(lo-z)       if z < lo
                      robust(z-hi)       if z > hi
```

Ranges require corpus/human evidence and intent confidence. Until calibrated,
they are labels and experiment outputs, not hard-coded production targets.

## Required Phase 02 Validation

Every prototype feature must demonstrate:

- monotonic response to its controlled perturbation;
- low response to unrelated perturbations;
- declared minimum resolution and proxy/full sensitivity;
- correlation with human technical-failure labels on real photographs;
- cost, memory, valid-pixel fraction, uncertainty, and fallback;
- stable results across crop/orientation and important scene classes.

Failure of any item rejects or narrows the feature; implementation effort is
not evidence.
