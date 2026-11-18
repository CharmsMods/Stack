# Burst engine: alignment foundation, pass 1

This is the working record for the continuing **draft** engine PR. Keep the PR
draft until the owner explicitly requests readiness. Baseline: `748f300`.
Priority: image quality, measured resolution, then performance. Learned models
may propose correspondence; reconstructed pixels come from captured samples.

## First-pass scope and findings

The shipping standalone and integrated hosts already call the same `StackBurst`
processing implementation. Native local alignment already has coarse search,
FP64 subpixel fitting, covariance, ambiguity checks and bidirectional closure.
The current super-resolution path already gathers original CFA measurements;
it is not just resizing a merged image. This pass improves the motion foundation
shared by those operations, without adding a parallel engine or a new model.

| Verified problem | First-pass change | Important limit |
| --- | --- | --- |
| Subpixel scoring changes valid samples and interpolation-dependent noise denominators while the solver differentiates only the signal. | One common support and one fixed noise weighting for each bounded fit, with sufficient-decrease backtracking. | Final uncertainty remains an approximation; this is not a complete likelihood or posterior. |
| Repeated pyramid smoothing treats already overlapping filtered samples as independent. | Compute each coarse pixel's marginal variance from the complete operator on native samples. | Neighboring coarse pixels remain correlated; later Keys interpolation still uses marginal variances. |
| AI/external proposals disable calibrated exposure cohorts and measured anchor ranking. | Preserve both native paths; transform origin-based proposals into the actual pair's coordinates. | Proposal transport can fail locally and contribute no hint. A hint never authorizes fusion. |
| Several processing/cache owners name their algorithms independently. | Include one shared native-motion revision in Bracketing, MFD/HDR and graph-derived identities. | Existing historical graph adapters still need a later architectural review; this is not a claim that every higher-level fusion path is unified. |

## Native refinement objective

At the discrete seed, select a fixed set `S` whose source interpolation footprints
remain valid throughout the existing bounded refinement neighborhood. This avoids
immobilizing legitimate edge motion while preventing a trial from dropping hard
measurements. Existing minimum coverage and sample-count requirements still apply.

For reference sample `R_i`, source interpolation `I_i(u)`, and measured exposure
scale `a`, hold `s_i^2 = v_Ri + a^2 v_Ii(u_seed) + epsilon` fixed for that fit:

```math
E(u) = |S|^{-1} sum_{i in S} rho_Huber((R_i - a I_i(u)) / s_i).
J_i = -a grad I_i(u) / s_i
H = sum_i w_i J_i^T J_i,  g = sum_i w_i J_i^T r_i
(H + lambda I) delta = -g
```

Huber influence weights are recomputed from the current residual; the measurement
identities and noise denominators do not change during the solve. Retain the
existing displacement/conditioning bounds. Backtracking uses bounded halvings
and sufficient decrease, rather than repeating an unchanged failed full/half
step. Final photometric, covariance and closure checks use physical variances
re-evaluated at the accepted coordinate on the same support. The optimization
score must not be reported as calibrated measurement confidence.

The existing distinct runner-up is re-scored on exactly the common support and
fixed seed denominators. The retained uniqueness is capped by the smaller of
the original and common-support gaps; unavailable comparison evidence cannot
authorize a structured match. This prevents an excluded border feature from
silently carrying the old rival's uniqueness forward. It is a bounded check of
that rival, not a certificate over every searched basin: a formerly lower-ranked
candidate could become competitive on the smaller support.

This is an engineering choice informed by robust Lucas–Kanade optimization and
registration resampling research, not an algorithm claimed verbatim from a paper.

## Exact pyramid marginals

For a native variance diagonal `D` and the complete signal-filter operator `P_l`
from native samples to level `l`, the required marginal is:

```math
Var(y_l[j]) = [P_l D P_l^T]_{jj} = sum_i P_l[j,i]^2 v_i.
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
forward(q) = F(A^{-1}(q))
reverse(s) = A(R(s)).
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

- [x] Independent basis-image arithmetic for pyramid marginals, levels 1–3,
  reflection, odd/small extents, heterogeneous noise and block seams.
- [x] Production proposal loading/identity, coordinate rebasing, bounded inverse,
  callback ownership and stale-pair checks on Linux.
- [x] Existing native-motion suite plus focused support, subpixel and proposal
  regressions; retain its image-correctness thresholds.
- Windows gate: linked native-motion, proposal/anchor and pyramid checks plus
  compile checks of changed integrated owners. Current results are recorded in
  [the draft PR's checks](https://github.com/CharmsMods/Stack/pull/10/checks).
- [x] Final source review and [draft PR publication](https://github.com/CharmsMods/Stack/pull/10).

Full Windows product/worker linking, physical-GPU execution, actual model inference
and representative camera bursts remain distinct verification gaps. No measured
end-to-end quality or speed improvement is claimed without those captures.

## Later passes in this same draft

1. Audit uncertainty/observability and correspondence at motion boundaries,
   blur, occlusions and difficult exposure overlap, with representative RAW cases.
2. Strengthen genuine resolution recovery: sampling/frequency support, optical and
   exposure models, and the explicit-2× versus adaptive-kernel behavior. Evaluate
   candidate changes against held-out measurements; no generated texture.
3. Continue common-engine contracts and data flow, streaming/device residency and
   UI-independent scheduling, preserving a single authoritative processing path.

The next pass should be chosen from measured limitations of this foundation,
not by accepting looser fusion thresholds or a larger output canvas as quality.
