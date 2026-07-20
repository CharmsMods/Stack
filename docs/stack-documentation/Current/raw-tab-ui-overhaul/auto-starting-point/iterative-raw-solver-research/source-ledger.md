# Source Ledger And Research Backlog

- Captured: 2026-07-09
- Source: initial online research pass
- Type: research
- Topic: iterative-raw-solver
- Verification: Phase 00 closed the implementation-critical derivations and
  linked Stack-specific decisions; optional and later-phase research remains

## Source Quality Rules

Priority order:

1. Standards and specifications.
2. Primary peer-reviewed papers and author project pages.
3. Official source code and technical manuals.
4. Official company product/engineering documentation.
5. Secondary summaries only for discovery, never for final mathematical claims.

`Initial extraction` means the source and relevant claim were checked, but the
entire paper has not necessarily been worked through equation by equation.

## Standards And Authoritative Pipeline Sources

| Source | Contribution | Status |
| --- | --- | --- |
| [Adobe DNG 1.7.1.0 specification](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf) | Raw linearization, black/white levels, masked areas, WB metadata, BaselineExposure, NoiseProfile, LinearResponseLimit, opcode stages, gain/profile metadata. | Phase 00 extraction accepted for diagnostics in `../iterative-raw-solver-phases/phase-00/raw-math-v1.md`; current-code gaps become Phase 01 fixtures. |
| [Adobe DNG resources and SDK](https://helpx.adobe.com/camera-raw/digital-negative.html) | Current official specification and SDK entrypoint. | Verified entrypoint. |
| [OpenColorIO ViewTransform](https://opencolorio.readthedocs.io/en/v2.4.2/api/viewtransform.html) | Scene-referred to display-referred boundary. | Initial extraction. |
| [darktable module order](https://docs.darktable.org/usermanual/3.6/en/special-topics/module-order/) | Practical ordering of raw black/white, highlight reconstruction, raw denoise, demosaic, scene editing, and display work. | Initial extraction; compare against current release/source. |
| [darktable demosaic](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/demosaic/) | Demosaic domain and algorithm-dependent detail/noise/overshoot tradeoffs. | Initial extraction. |
| [darktable highlight reconstruction](https://docs.darktable.org/usermanual/development/de/module-reference/processing-modules/highlight-reconstruction/) | Raw per-channel clipping and pre-demosaic reconstruction reasoning. | Initial extraction; English/current-page follow-up. |
| [darktable tone equalizer](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/) | Guided luminance mask, EV graph, local-contrast-preserving scene relighting. | Initial extraction; high priority. |
| [darktable filmic RGB](https://docs.darktable.org/usermanual/4.2/en/module-reference/processing-modules/filmic-rgb/) | Scene bounds, display mapping, curve feasibility, hue/gamut behavior. | Initial extraction. |
| [darktable sigmoid](https://docs.darktable.org/usermanual/4.2/en/module-reference/processing-modules/sigmoid/) | Alternative scene-to-display curve and separation of scene processing from display transform. | Initial extraction. |

## Company And Product Research

| Source | Contribution | Status |
| --- | --- | --- |
| [Adobe Camera Raw tone controls](https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html) | Auto writes visible controls; individual roles for Exposure, Highlights, Shadows, Whites, Blacks, and curves; clipping previews. | Initial extraction. |
| [Adobe Adaptive Profile engineering overview](https://blog.adobe.com/en/publish/2024/10/14/the-adobe-adaptive-profile) | Describes global, regional, and local tone behavior using Profile Gain Table Maps plus sky/subject color tables. | Phase 00 product extraction complete; DNG PGTM stage math is crosswalked. Evidence for spatial/tone-conditioned gain only, not a hidden Stack profile. |
| [Adobe Photoshop automatic tonal adjustments](https://helpx.adobe.com/photoshop/using/making-quick-tonal-adjustments.html) | Percentile clipping and black/white mapping as a simple baseline, including known limitations. | Initial extraction. |

## Tone Mapping, Local Contrast, And Perception

| Source | Contribution | Status |
| --- | --- | --- |
| [Reinhard et al., Photographic Tone Reproduction](https://www-old.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf) | Scene key, global photographic mapping, and local dodge/burn formulation. | Initial extraction; derive equations and failure cases. |
| [Durand and Dorsey, Fast Bilateral Filtering for HDR Display](https://people.csail.mit.edu/fredo/PUBLI/Siggraph2002/DurandBilateral.pdf) | Edge-preserving base/detail decomposition for dynamic-range compression. | Phase 00 equation/cost/failure extraction complete; prototype-only pending identical halo fixtures. |
| [He, Sun, Tang, Guided Image Filtering](https://people.csail.mit.edu/kaiming/eccv10/eccv10ppt.pdf) | Explicit edge-aware filter suitable for mask/base estimation. | Phase 00 equation/cost/failure extraction complete; prototype-only, not a halo guarantee. |
| [Paris, Hasinoff, Kautz, Local Laplacian Filters](https://people.csail.mit.edu/sparis/publi/2011/siggraph/Paris_11_Local_Laplacian_Filters_lowres.pdf) | Multiscale edge-aware tone/detail operations designed to avoid halos. | Phase 00 pyramid/remap extraction complete; prototype-only pending performance, noise, and proxy tests. |
| [Mantiuk, Daly, Kerofsky, Display Adaptive Tone Mapping](https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk08datm.pdf) | Perceptual contrast-distortion objective with display constraints and quadratic optimization. | Phase 00 display model/contrast/QP extraction complete; accepted as diagnostic inspiration, not the whole objective. |
| [Mantiuk et al., HDR-VDP-2](https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk11hdrvdp2.pdf) | Visibility and quality prediction across luminance conditions. | Phase 00 requirements reviewed; rejected for default no-reference production scoring, retained for calibrated offline validation. |
| [Yeganeh and Wang, TMQI](https://ece.uwaterloo.ca/~z70wang/research/tmqi/) | Multiscale structural fidelity plus statistical naturalness for tone-mapped images. | Phase 00 requirements reviewed; rejected for default production scoring, retained as offline/advisory. |
| [Trentacoste et al., Unsharp Masking, Countershading and Halos](https://www.cl.cam.ac.uk/~rkm38/pdfs/trentacoste12countershading.pdf) | Perceptual transition from useful countershading to objectionable halos as a function of angular profile width. | Phase 00 extraction complete; heuristic guidance and human-label fixtures required because simple stimuli do not fully generalize. |
| [Hessel and Morel, Quantitative Evaluation of Base/Detail Filters](https://arxiv.org/abs/1808.09411) | Separate luminance halo, contrast halo, staircase, and compartmentalization patterns and measurements. | Phase 00 extraction complete; fixture family accepted, numeric weights not accepted. |

## Color, Noise, And Raw Evidence

| Source | Contribution | Status |
| --- | --- | --- |
| [Finlayson and Trezzi, Shades of Gray](https://pdfs.semanticscholar.org/acf3/6cdadfec869f136602ea41cad8b07e3f8ddb.pdf) | Minkowski-norm family connecting Gray World and Max-RGB style illuminant estimation. | Phase 00 family extraction complete; estimator disagreement accepted only as uncertainty, with parameter study pending. |
| [van de Weijer, Gevers, Gijsenij, Edge-Based Color Constancy](https://citeseerx.ist.psu.edu/document?doi=f14a55562e6f34d8fbae6dbaf01e0e8cc1ed6bad&repid=rep1&type=pdf) | Gray-Edge illuminant estimation from image derivatives. | Phase 00 family extraction complete; estimator disagreement accepted only as uncertainty. |
| [DNG NoiseProfile section](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf) | Normalized signal-dependent shot plus signal-independent read-noise model. | Phase 00 equation, plane ordering, assumptions, fallback limits, and gain propagation accepted for Phase 01 diagnostics. |
| [Foi et al., Practical Poissonian-Gaussian Noise Modeling](http://sp.cs.tut.fi/pubdl/Foi2008-PracticalD.pdf) | Affine signal-dependent variance model and fitting context for raw noise. | Phase 00 model extraction complete; rendered visibility/camera fallback remains a Phase 02 prototype. |

## Data-Driven And Learned Adjustment

| Source | Contribution | Status |
| --- | --- | --- |
| [Bychkovsky et al., Learning Photographic Global Tonal Adjustment](https://people.csail.mit.edu/sparis/publi/2011/cvpr_auto/Bychkovsky_11_Learning_Photo_Adjustment.pdf) | MIT-Adobe FiveK, content-aware global adjustment, and evidence that simple histogram rules fail on low/high-key and backlit scenes. | Phase 00 product implication extracted; multiple valid edits and histogram failure accepted. Dataset/license review still required before use. |
| [Jaroensri et al., Predicting Ranges of Acceptable Tonal Adjustments](https://projects.csail.mit.edu/acceptable-adj/) | Models the boundary of acceptable photographic tonal adjustment instead of one target edit. | Phase 00 review protocol implication accepted; Stack-specific ranges require human labels. |
| [Gharbi et al., Deep Bilateral Learning](https://groups.csail.mit.edu/graphics/hdrnet/) | Low-resolution learned proposals with edge-preserving bilateral upsampling and local affine transforms. | Initial extraction; proposal-only relevance. |
| [Talebi and Milanfar, NIMA](https://arxiv.org/abs/1709.05424) | Rating-distribution prediction for technical/aesthetic image quality. | Initial extraction; advisory-only relevance. |

## Optimization Sources

| Source | Contribution | Status |
| --- | --- | --- |
| [PDFO / Powell derivative-free solvers](https://arxiv.org/abs/2302.13246) | Model-based trust-region methods for expensive objectives with no analytic gradients. | Phase 00 requirements extracted; candidate family only until Phase 03 surface evidence. |
| [Snoek, Larochelle, Adams, Practical Bayesian Optimization](https://papers.nips.cc/paper_files/paper/2012/hash/05311655a15b75fab86956663e1819cd-Abstract.html) | Surrogate-guided expensive evaluation, variable cost, and parallel candidate selection. | Phase 00 requirements extracted; later comparison only. |
| [Hansen, CMA-ES Tutorial](https://arxiv.org/abs/1604.00772) | Stochastic optimization of nonlinear, non-convex continuous black-box objectives. | Phase 00 requirements extracted; stochastic benchmark only, not selected. |

## Phase 00 Closure Of The Priority Queue

1. DNG tags, stages, units, normalization, clipping, noise, and fallbacks are
   extracted in `../iterative-raw-solver-phases/phase-00/raw-math-v1.md`.
2. Display-adaptive contrast/display requirements are extracted in
   `../iterative-raw-solver-phases/phase-00/rendered-math-v1.md`.
3. Local Laplacian, guided, and bilateral equations and failure hypotheses are
   extracted and classified prototype-only.
4. Noise gain propagation is derived; rendered visibility remains an explicit
   Phase 02 prototype rather than an invented threshold.
5. HDR-VDP and TMQI are rejected as default production objectives and retained
   only for qualified offline/reference use.
6. Acceptable-range review is adopted; FiveK bytes/license are not assumed.
7. Optimizer family requirements are extracted. Budget estimation is correctly
   deferred until Phase 03 measures Stack's surface; no optimizer is selected.
8. PGTM/ProfileGainTableMap stage meaning is extracted as architectural
   evidence, not a hidden correction or Stack parameterization.

The implementation-critical queue is closed for Phase 01. Later reading is
triggered by a failed fixture or measured surface question, not by open-ended
collection.

## Missing Research Areas

- Primary RAW highlight reconstruction algorithms and uncertainty.
- Modern demosaic/noise interactions for analysis proxies.
- Perceptual hue/gamut-preserving scene-to-display mapping.
- Multiple-illuminant estimation and local WB.
- Reliable face/skin/subject importance without forcing aesthetic bias.
- Formal halo and gradient-reversal metrics validated on photographs.
- Camera/lens gain maps and spatially varying noise.
- Acceptable-range rather than single-target photographic adjustment studies.
- Licensing and redistribution constraints for every proposed dataset or model.

## Extraction Template

For each full source review, append or link a note containing:

```text
Citation and stable URL
Source type and publication status
Problem definition
Data domain and assumed color space
Variables and units
Equations
Optimization method
Required inputs/reference image/training data
Computational cost
Reported failure cases
Licensing/code availability
Exact implication for Stack
What the source does not establish
Prototype or validation needed
```
