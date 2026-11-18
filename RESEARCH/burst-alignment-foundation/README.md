# Burst engine: alignment foundation, passes 1–2

This is the working record for the continuing **draft** engine PR. Keep the PR
draft until the owner explicitly requests readiness. Baseline: `748f300`.
Priority: image quality, measured resolution, then performance. Learned models
may propose correspondence; reconstructed pixels come from captured samples.

## Scope and verified findings

The shipping standalone and integrated hosts already call the same `StackBurst`
processing implementation. Native local alignment already has coarse search,
FP64 subpixel fitting, covariance, ambiguity checks and bidirectional closure.
The current super-resolution path already gathers original CFA measurements;
it is not just resizing a merged image. These passes improve the motion foundation
shared by those operations, without adding a parallel engine or a new model.

| Verified problem | Change | Important limit |
| --- | --- | --- |
| Subpixel scoring changes valid samples and interpolation-dependent noise denominators while the solver differentiates only the signal. | One common support and one fixed noise weighting for each bounded fit, with sufficient-decrease backtracking. | Final uncertainty remains an approximation; this is not a complete likelihood or posterior. |
| Repeated pyramid smoothing treats already overlapping filtered samples as independent. | Compute each coarse pixel's marginal variance from the complete operator on native samples. | Neighboring coarse pixels remain correlated; later Keys interpolation still uses marginal variances. |
| AI/external proposals disable calibrated exposure cohorts and measured anchor ranking. | Preserve both native paths; transform origin-based proposals into the actual pair's coordinates. | Proposal transport can fail locally and contribute no hint. A hint never authorizes fusion. |
| Several processing/cache owners name their algorithms independently. | Include one shared native-motion revision in Bracketing, MFD/HDR and graph-derived identities. | Existing historical graph adapters still need a later architectural review; this is not a claim that every higher-level fusion path is unified. |
| The pass-1 runner-up check misses lower-ranked rivals that become competitive after common-support selection. | Cap finest-level uniqueness against every eligible separated enumerated rival, including those beyond the GPU numerical shortlist. | This checks the existing finite search domain, not all continuous motions or unsearched basins. |
| Point corrections omit the native grid's positional-precision confidence factor. | Apply that factor before acceptance and publish the accepted fit's sigma/conditioning diagnostics. | The existing covariance/confidence model remains approximate; no acceptance thresholds were relaxed. |
| Centered differences at cohort motion-grid knots can cancel opposing slopes and understate transported covariance. | Reuse analytic one-sided derivatives and form a PSD covariance upper envelope; reject over-ceiling interior and padded output nodes. | First-order local models only; link correlations and generic interpolation/scatter clipping remain outside this change. |

## Native refinement objective

At the discrete seed, select a fixed set `S` whose source interpolation footprints
remain valid throughout the existing bounded refinement neighborhood. This avoids
immobilizing legitimate edge motion while preventing a trial from dropping hard
measurements. Existing minimum coverage and sample-count requirements still apply.

For reference sample `R_i`, source interpolation `I_i(u)`, and measured exposure
scale `a`, hold `s_i^2 = v_Ri + a^2 v_Ii(u_seed) + epsilon` fixed for that fit:

```math
\begin{aligned}
E(u) &= |S|^{-1}\sum_{i\in S}\rho_{\mathrm{Huber}}\!\left(\frac{R_i-aI_i(u)}{s_i}\right),\\
J_i &= -a\nabla I_i(u)/s_i,\\
H &= \sum_i w_i J_i^T J_i,\qquad g=\sum_i w_i J_i^T r_i,\\
(H+\lambda I)\delta &= -g.
\end{aligned}
```

Huber influence weights are recomputed from the current residual; the measurement
identities and noise denominators do not change during the solve. Retain the
existing displacement/conditioning bounds. Backtracking uses bounded halvings
and sufficient decrease, rather than repeating an unchanged failed full/half
step. Final photometric, covariance and closure checks use physical variances
re-evaluated at the accepted coordinate on the same support. The optimization
score must not be reported as calibrated measurement confidence.

At the finest search level, every originally eligible hypothesis separated by the
existing two-RAW-pixel distance is compared on that common support and fixed seed
denominators. Preserve GPU eligibility before shortlist invalidation, derive its
coverage from integer counts instead of a rounded FP32 fraction, and let successful
native verification restore eligibility. The retained uniqueness can only decrease
from the original gap; a missing competitive score reduces it to zero. FlatSafe
retains its separate parent-evidence policy.

For current gap cap `U`, seed cost `E_seed`, and `N` fixed measurements, a rival
cannot lower the cap once its accumulated nonnegative Huber losses exceed
`N * (E_seed + U * (E_seed + epsilon) + tieTolerance)`. Remaining and unavailable
losses optimistically contribute zero to this lower bound. A pruned score is
explicitly incomplete, never a valid full-patch score. Stop the search when the
cap already fails the unchanged uniqueness threshold. This is exact exclusion
for the finite objective, without expanding the original candidate domain.

Only one extra double persists per search state. Two state buffers per direction
cost 16 bytes per grid node; memory admission charges both simultaneous directions
using a ceil-rounded grid bound (32 bytes per spatial node for equal extents).
Bracketing and the MFD adapter include this addition. Candidate lists remain
temporary, and common-support scratch reuses the existing per-worker allowance.

This is an engineering choice informed by robust Lucas–Kanade optimization and
registration resampling research, not an algorithm claimed verbatim from a paper.

## Positional precision and cohort uncertainty

Point corrections now use the same existing precision factor as structured grid
fits, `1 / (1 + (sigma_raw / confidence_scale)^2)`, alongside photometric,
coverage and closure factors. A good photometric fit and reverse closure cannot
replace precision along a weak image direction. Check the final confidence before
publishing a Structured correction; rejected output stays rejected. Correction
covariance remains local-only because the capture-motion consumer retains the base
field's covariance separately. Accepted sigma and Hessian diagnostics are filled.

For origin-to-anchor covariance `C_a` and each supported anchor-to-source local
Jacobian `J_k`, transport `C_k = J_k C_a J_k^T`. At a grid knot, combine the
supported models in deterministic order:

```math
E_0=C_0,\qquad E_{k+1}=E_k+(C_{k+1}-E_k)_+,\qquad C_{\mathrm{out}}=E+C_b.
```

The positive spectral part gives both `E_next - E >= 0` and `E_next - C_next >= 0`
in PSD order. Closed-form 2×2 arithmetic needs fixed stack storage. Identical
models remain exact; noncommuting models retain a valid, order-dependent upper
envelope. One field evaluation supplies the local models instead of the former
center plus four finite-difference samples. This is a reduced component call count,
not a measured end-to-end speedup.

Reject missing derivative evidence and composed nodes above the configured
covariance ceiling, including boundary padding added after composition. Preserve
the existing valid-descriptor semantics when all nodes reject so scene uncertainty
does not become a fatal reliability-request error. Closure continues testing each
side separately. The envelope does not model nonlinear errors spanning cells or
correlation between the fitted links; generic field interpolation can still add
scatter and clip covariance. Larger honest covariance can also make a downstream
variance-normalized photometric gate more permissive, so this is not a claim of
globally monotonic rejection or calibrated probability.

## Exact pyramid marginals

For a native variance diagonal `D` and the complete signal-filter operator `P_l`
from native samples to level `l`, the required marginal is:

```math
\operatorname{Var}(y_\ell[j])=[P_\ell D P_\ell^T]_{jj}
=\sum_i P_\ell[j,i]^2 v_i.
```

Taking only the diagonal after each intermediate filter loses cross terms from
shared native samples. For independent unit-variance native noise and the current
five-tap binomial filter, the interior two-dimensional results are:

| Level | Previous recursive marginal | Complete-operator marginal | Understatement factor |
| --- | ---: | ---: | ---: |
| 1 | 0.07476806640625 | 0.07476806640625 | 1.000 |
| 2 | 0.00559026375 | 0.01524585858 | 2.727 |
| 3 | 0.00041797321 | 0.00364326658 | 8.717 |

The implementation composes separable one-dimensional stencils, merges duplicate
native indices before squaring, and follows reflection at each intermediate
extent. A 32-column block and bounded row cache avoid a full intermediate image;
the explicit scratch allowance is 32 KiB. Signal values and validity propagation
stay with the existing pyramid owner. Native noise-model validity and correlations
already present in original/virtual input measurements remain separate assumptions.

One bounded Linux `g++ -O2` check of a 1024×768 single CFA plane measured 9.48 ms
for the corrected variance passes through levels 1–3 (median of three warmed
samples). This is a component cost, **not** a production speedup: the former
variance arithmetic shared its loop with signal filtering. Hardware, decoding,
GPU transfers and complete reconstruction were not measured by this check.

## Additive proposal coordinates

Let `A` be the RAW-verified origin-to-cohort-anchor mapping, `F` the proposed
origin-to-source flow, and `R` its reverse. For a cohort pair:

```math
\operatorname{forward}(q)=F(A^{-1}(q)),\qquad
\operatorname{reverse}(s)=A(R(s)).
```

The optional inverse uses bounded Newton/backtracking on `A` and its actual
analytic derivative. Domain, conditioning, orientation and round-trip checks
qualify only the availability of a search hint. Missing/ambiguous inverse evidence
does not create a warp or supply confidence. Direct-origin pairs retain their
existing coordinates. Native search and verification remain authoritative.

The adapter still holds one learned forward/reverse field pair. Callback leases
retain accepted motion without copying pyramids or image payloads; reused native
requests clear stale callbacks. The SEA-RAFT runtime, model, proxy resolution and
four inference iterations are unchanged. Actual CUDA inference was not exercised
by the component checks.

## Research used

| Primary source | Decision it informs |
| --- | --- |
| [Wronski et al., Handheld Multi-Frame Super-Resolution, 2019](https://storage.googleapis.com/gweb-research2023-media/pubtools/5211.pdf), §§3.2, 5.2 | Accurate subpixel registration and motion-aware rejection are prerequisites for useful measured detail. Stack already has these stages; improve their consistency. |
| [Baker and Matthews, Lucas–Kanade 20 Years On, 2004](https://publications.ri.cmu.edu/storage/publications/pub_files/pub3/baker_simon_2004_1/baker_simon_2004_1.pdf) | Keep objective, derivative, warp update and descent acceptance mathematically consistent. Damping alone does not guarantee better alignment. |
| [Aganj et al., On Removing Interpolation and Resampling Artifacts in Rigid Image Registration, 2013](https://pubmed.ncbi.nlm.nih.gov/23076044/) | Interpolation can alter registration optima and gradients; a lower resampled score alone is not proof of better physical correspondence. |
| [NIST TN 1297, Appendix A: propagation of uncertainty](https://www.nist.gov/pml/nist-technical-note-1297/nist-tn-1297-appendix-law-propagation-uncertainty) | Retain covariance terms created by reusing measurements; composing the linear operator gives exact marginals under independent native noise. |
| [Hasinoff et al., Burst Photography for HDR and Low-Light Imaging, 2016](https://people.csail.mit.edu/hasinoff/pubs/HasinoffEtAl16-hdrplus.pdf) | RAW measurement processing and robust alignment are valuable foundations. Its constant-exposure acquisition differs from Stack's mixed-exposure brackets. |
| [Wang et al., SEA-RAFT, 2024](https://arxiv.org/abs/2405.14793) and [official code](https://github.com/princeton-vl/SEA-RAFT) | Optical flow supplies correspondence proposals. Published flow benchmark performance does not establish precision on Stack's native RAW brackets. |
| [Robinson and Milanfar, Fundamental Performance Limits in Image Registration, 2004](https://pubmed.ncbi.nlm.nih.gov/15449581/) | Registration precision is constrained by measurements and noise; do not equate a sharper-looking fit or inverse Hessian with fully calibrated certainty. |
| [Clement, Bierbaum and Sethna, Image Registration and Super Resolution from First Principles, 2018](https://arxiv.org/abs/1809.05583) | Noise in both images and image boundaries can invalidate naive precision interpretations. Their translation/noise assumptions are not a calibration for mixed-exposure CFA registration. |
| [Higham, What Is the Nearest Positive Semidefinite Matrix?, 2021](https://nhigham.com/2021/01/26/what-is-the-nearest-positive-semidefinite-matrix/) | Positive spectral projection supplies the building block for the local covariance envelope; the upper-bound proof above is the implementation's derivation. |

The uncertainty review did **not** justify subtracting an expected gradient-noise
Hessian and treating its inverse as calibrated covariance. Residual-dependent
robust weights, noisy gradients, resampling correlations and blur/exposure mismatch
remain coupled. A later bounded study can propagate independent native noise
through the frozen linearized score map via interpolation coefficients, preserving
shared-sample covariance without a dense image covariance matrix. That would still
need empirical coverage checks and explicit model assumptions.

## Verification and remaining work

One bounded native-solver comparison used the existing 128×128 analytic CFA
fixture, a known `(0.65, -0.55)` RAW-pixel translation, and symmetric source blur.
Both baseline `748f300` and this pass produced structured fits at the **same nine
locations** (`x,y` in `{20,52,84}`); no rejected nodes were removed from either
median. Endpoint-error median changed from **0.513343 to 0.180381 RAW pixels**;
the new maximum was 0.293559. This one-level fixture isolates the subpixel solve
from the pyramid change. Its final confidence remained low (about 0.019–0.027),
as expected with blur mismatch. This demonstrates a component-level geometric
improvement, not improved final fusion or real-camera image quality. The exact
case is retained in the existing Phase 5 suite with a 0.30-pixel median guard.
The original code also fails the added public-API in-fit cancellation check.

Pass 2 retains these additional bounded regressions:

| Controlled case | Previous draft behavior | Required behavior now |
| --- | --- | --- |
| 128×128 repeated texture, a distinguishing border stripe removed by common support, and heterogeneous source noise. | Three border nodes remain Structured with confidence approximately 0.568, 0.615 and 0.677. A lower-ranked +8-RAW-pixel repeat ties the seed on the common objective. | All three reject as AmbiguousMatch with zero uniqueness/confidence. No search expansion or threshold change. |
| Anisotropic texture with one weak image direction, the same known displacement and two declared noise levels. | The imprecise point correction is accepted with confidence 1; sigma/conditioning diagnostics are absent. | The imprecise case rejects under the existing 0.2 rule; its more precise counterpart remains accepted with diagnostics and reduced confidence. |
| Piecewise motion `u(x)=0.5*abs(x-x_k)`, anchor covariance `I`, pair covariance `0.01I`. | Centered slope gives composed x variance 1.01 RAW pixels². | One-sided x slopes 0.5 and 1.5 give envelope x variance 2.26, while y variance remains 1.01. Affine transport remains exact. |
| Interior covariance `15.9I` with boundary padding adding `0.25I`. | A guard before padding alone would republish covariance above the variance ceiling of 16. | Keep nine supported interior nodes and reject sixteen oversized padding nodes. An all-rejected composition remains valid data with no supported evidence. |

A bounded pruning check compared six rivals at three cost limits: all complete
scores matched unbounded evaluation exactly; all four early exclusions had full
cost above the applicable limit; wholly unavailable evidence remained incomplete.
Two controlled accelerator-callback variants of the same repeated-texture fixture
cover native eligibility promotion and an unshortlisted rival with exact coverage
`280/400 = 0.70` but a packed FP32 fraction below 0.70. These verify the public
accelerator contract, not physical GPU execution.
No timing or real-photograph improvement is inferred from these checks.

- [x] Independent basis-image arithmetic for pyramid marginals, levels 1–3,
  reflection, odd/small extents, heterogeneous noise and block seams.
- [x] Production proposal loading/identity, coordinate rebasing, bounded inverse,
  callback ownership and stale-pair checks on Linux.
- [x] Existing native-motion suite plus focused support, subpixel and proposal
  regressions, point precision, common-domain ambiguity, and cohort covariance
  checks; retain the existing image-correctness thresholds.
- Windows gate: linked native-motion, proposal/anchor and pyramid checks plus
  compile checks of changed integrated owners. Current results are recorded in
  [the draft PR's checks](https://github.com/CharmsMods/Stack/pull/10/checks).
- [x] Final source review and [draft PR publication](https://github.com/CharmsMods/Stack/pull/10).

Full Windows product/worker linking, physical-GPU execution, actual model inference
and representative camera bursts remain distinct verification gaps. No measured
end-to-end quality or speed improvement is claimed without those captures.

## Later passes in this same draft

1. Continue uncertainty/observability and correspondence work with representative
   RAW cases: interpolation covariance, blur, occlusions and difficult exposure
   overlap. Test empirical error coverage before changing confidence calibration.
2. Strengthen genuine resolution recovery: sampling/frequency support, optical and
   exposure models, and the explicit-2× versus adaptive-kernel behavior. Evaluate
   candidate changes against held-out measurements; no generated texture.
3. Continue common-engine contracts and data flow, streaming/device residency and
   UI-independent scheduling, preserving a single authoritative processing path.

The next pass should be chosen from measured limitations of this foundation,
not by accepting looser fusion thresholds or a larger output canvas as quality.
