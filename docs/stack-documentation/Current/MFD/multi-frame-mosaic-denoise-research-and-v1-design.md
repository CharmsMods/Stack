# Multi-Frame Mosaic Denoise: Research Review and V1 Engineering Design

**Target artifact:** first production processing path for a desktop RAW editor  
**Input contract:** compatible, still-mosaiced Bayer RAW frames from one camera mode, with the same CFA pattern, sensor geometry, bit depth, and active area  
**Output contract:** one original-resolution, denoised Bayer mosaic in the reference frame's camera/sensor coordinate system  
**Research snapshot:** 2026-08-02  
**Selected V1:** **Reference-Anchored CFA-Plane Robust Inverse-Variance Fusion**, abbreviated **RA-CFA**

---

## Executive decision

The recommended V1 is a **reference-anchored, noise-normalized, CFA-plane-preserving burst fusion method**:

1. Linearize and normalize each RAW frame without demosaicing.
2. Build four half-resolution planes (`R`, `G0`, `G1`, `B`) and never compare or interpolate incompatible CFA sites.
3. Register every alternate to the reference with multiscale global initialization followed by confidence-bearing local tile translations and subpixel refinement.
4. Reject motion and registration failures with hard validity masks, forward/backward consistency, motion-field disagreement, patch-level noise-normalized residuals, and a final per-pixel redescending gate.
5. Fuse accepted same-color measurements by inverse effective variance, with the reference always present and an exact reference-copy fallback wherever alternate support is weak or doubtful.
6. Return a denoised Bayer mosaic for the editor's existing demosaic and camera-color pipeline.

This is an engineering synthesis, not a claim that any company ships this exact algorithm. Its main precedents are Google's Bayer-domain HDR+ architecture and reference fallback, Google's Handheld Multi-Frame Super-Resolution work on subpixel CFA registration and robust local fusion, the published Poisson-Gaussian RAW noise model, Adobe's DNG processing semantics, and Apple patent disclosures describing reference/candidate similarity and variance-aware fusion. The synthesis deliberately avoids undocumented commercial details.

The selected method is less ambitious than joint demosaicing/super-resolution, but it is the strongest first production choice for this application boundary: it preserves the RAW graph contract, has understandable failure behavior, can be tiled and streamed, avoids a training dependency, and can collapse locally to the exact reference instead of producing a plausible-looking ghost or blur.

---

## How to read the evidence labels

This report uses four labels to prevent published facts from being mixed with design judgment:

- **Published evidence** — a peer-reviewed paper, official research publication, standards document, or public implementation.
- **Patent disclosure** — technical material disclosed in a patent. It is neither peer review nor proof that the described method is used in a shipping product.
- **V1 decision** — the concrete engineering choice recommended here.
- **Needs experiment** — a parameter or behavior that the literature does not settle and that must be calibrated on a real RAW corpus.

This document is a technical design, not a patent freedom-to-operate opinion.


## Table of contents

1. [Scope, assumptions, and non-goals](#1-scope-assumptions-and-non-goals)
2. [What the strongest public work says](#2-what-the-strongest-public-work-says)
3. [Coordinate system and CFA conventions](#3-coordinate-system-and-cfa-conventions)
4. [Normative end-to-end V1 pipeline](#4-normative-end-to-end-v1-pipeline)
5. [RAW normalization, calibration, and processing order](#5-raw-normalization-calibration-and-processing-order)
6. [Sensor-noise model and uncertainty propagation](#6-sensor-noise-model-and-uncertainty-propagation)
7. [CFA-safe registration](#7-cfa-safe-registration)
8. [Robust rejection and ghost prevention](#8-robust-rejection-and-ghost-prevention)
9. [The fusion estimator](#9-the-fusion-estimator)
10. [Output semantics](#10-output-semantics)
11. [Initial parameter set](#11-initial-parameter-set)
12. [C++ architecture, memory, performance, and caching](#12-c-architecture-memory-performance-and-caching)
13. [Numerical safeguards](#13-numerical-safeguards)
14. [Python derivation and numerical sanity checks](#14-python-derivation-and-numerical-sanity-checks)
15. [Validation plan](#15-validation-plan)
16. [Actionable implementation sequence](#16-actionable-implementation-sequence)
17. [Future alternatives](#17-future-alternatives-recorded-but-not-designed-in-parallel)
18. [Known uncertainties and required corpus experiments](#18-known-uncertainties-and-required-corpus-experiments)
19. [Final V1 statement](#19-final-v1-statement)
20. [Primary references and implementations](#20-primary-references-and-implementations)

---

# 1. Scope, assumptions, and non-goals

## 1.1 Inputs accepted by V1

A frame is eligible only when all of the following are compatible with the reference:

- one-plane Bayer mosaic, not linear RGB and not already demosaiced;
- identical active sensor dimensions and crop geometry;
- identical CFA repeat size and color offsets;
- identical sensor orientation and pixel coordinate convention;
- compatible bit depth, linearization semantics, white-level semantics, and pre-demosaic calibration operations;
- the same camera and capture mode, or an explicitly calibrated equivalent mode;
- an exposure/gain difference that can be represented by one positive scalar common to all CFA colors;
- enough geometric overlap to align and fuse safely.

The application already owns frame ordering, reference selection, inclusion/exclusion, exact-original storage, and graph connectivity. This design starts at the processing boundary after those project decisions have been made.

## 1.2 Small exposure drift is supported; HDR bracketing is not

V1 estimates a scalar radiometric correction per alternate, so modest shutter/gain drift is supported. It does **not** attempt deliberate bracketed HDR, highlight replacement, long/short exposure motion segmentation, or exposure-dependent deblur. If the reference sample is clipped, V1 preserves the reference's clipped state rather than borrowing a shorter exposure. That keeps the estimator a denoiser instead of quietly turning it into an HDR merger.

## 1.3 Explicit non-goals

V1 does not perform:

- demosaicing, joint demosaicing, or super-resolution;
- deconvolution or frame-selection deblurring;
- learned fusion, learned optical flow, or semantic masks;
- multi-camera fusion;
- rolling-shutter camera-pose estimation;
- depth estimation or explicit layered motion;
- local tone mapping, white balance, camera-to-XYZ conversion, sharpening, or display rendering;
- geometric lens-distortion correction that resamples or mixes the Bayer mosaic before fusion;
- exact reconstruction of saturated reference highlights.

These are not declarations that the techniques are unhelpful. They are boundary choices that make the first implementation testable and safe.

---

# 2. What the strongest public work says

## 2.1 Google HDR+: the closest published ancestor to a Bayer-output V1

**Published evidence.** Google's HDR+ paper begins with Bayer RAW frames, uses constant exposure, aligns a burst, and merges it to reduce noise while retaining a high-bit-depth linear result. Its public 2016 implementation description uses an FFT-based alignment method and a hybrid 2D/3D Wiener merge; the reported mobile implementation processed a 12-megapixel image in about four seconds at that time. ([Hasinoff et al., *Burst Photography for High Dynamic Range and Low-Light Imaging on Mobile Cameras*, SIGGRAPH Asia 2016](https://hdrplusdata.org/hdrplus.pdf); [official Google Research page](https://research.google/pubs/burst-photography-for-high-dynamic-range-and-low-light-imaging-on-mobile-cameras/); [supplement](https://hdrplusdata.org/hdrplus_supp.pdf); [public dataset](https://hdrplusdata.org/))

The peer-reviewed IPOL analysis and open implementation are unusually useful because they expose operational details that are difficult to infer from a systems paper. The implementation forms a grayscale registration proxy from Bayer quads, builds a coarse-to-fine pyramid, performs tile block matching, refines subpixel shifts by fitting a quadratic to the local cost surface, and merges the four CFA planes separately. ([Monod, Delon, and Veit, *An Analysis and Implementation of the HDR+ Burst Denoising Method*, IPOL 2021](https://doi.org/10.5201/ipol.2021.336); [article](https://www.ipol.im/pub/art/2021/336/article_lr.pdf); [source code](https://github.com/amonod/hdrplus-python))

A useful HDR+ reference-fallback form is the pairwise frequency-domain blend. If `T_0` is a reference tile, `T_z` is an aligned alternate tile, `D_z = T_0 - T_z`, and `sigma^2` is a noise estimate, the published/open analysis describes a mismatch factor of the form

$$
A_z(\omega)
=
\frac{\left|D_z(\omega)\right|^2}
     {\left|D_z(\omega)\right|^2 + c\,\sigma^2},
$$

followed by a contribution resembling

$$
\widetilde T_z(\omega)
=
\bigl(1-A_z(\omega)\bigr)T_z(\omega)
+
A_z(\omega)T_0(\omega).
$$

Large disagreement drives the result toward the reference; small disagreement permits averaging. This behavior matters more to V1 than the exact FFT formulation.

**What V1 takes from HDR+:**

- RAW/Bayer-domain processing;
- a designated reference;
- multiscale tile registration;
- separate treatment of the four CFA sites;
- explicit noise dependence;
- smooth fallback toward the reference;
- overlapping processing regions rather than independent hard-edged tiles.

**What V1 does not copy:**

- the frequency-domain Wiener merge;
- the Bayer-quad grayscale proxy as the final registration representation;
- any undocumented finishing pipeline;
- parameter details whose public descriptions are incomplete.

The IPOL work explicitly identifies ambiguities and implementation choices around parts of the high-frequency merge/noise shaping. That is a reason not to make an exact HDR+ clone the production contract.

## 2.2 Google Handheld Multi-Frame Super-Resolution: strongest public CFA reconstruction design

**Published evidence.** Handheld Multi-Frame Super-Resolution, or HFSR, reconstructs complete RGB directly from a burst of CFA RAW images. It uses natural handheld subpixel shifts, local registration, anisotropic kernel regression, and robustness weights to handle local motion, occlusion, and scene change. It replaces the usual "merge Bayer, then demosaic" split with a joint reconstruction and reports about 100 milliseconds per 12-megapixel input frame on contemporary mass-produced mobile hardware. ([Wronski et al., *Handheld Multi-Frame Super-Resolution*, ACM TOG 2019](https://arxiv.org/abs/1905.03277); [official Google Research page](https://research.google/pubs/handheld-multi-frame-super-resolution/); [paper PDF](https://3dvar.com/Wronski2019Handheld.pdf))

The IPOL implementation describes a practical four-level block-matching registration followed by a few inverse-compositional alignment iterations. Fusion collects nearby warped samples, weights them by an anisotropic reconstruction kernel, and multiplies by a robustness term. In simplified notation,

$$
I(x,y)
=
\frac{
\sum_n r_n
\sum_{(u,v)\in\mathcal N_3}
w_n(u,v)\,J_n(u,v)
}{
\sum_n r_n
\sum_{(u,v)\in\mathcal N_3}
w_n(u,v)
}.
$$

Here, `J_n` is a warped frame sample, `w_n` is a spatial/kernel-regression weight, `r_n` is frame robustness, and `N_3` is a local 3-by-3 neighborhood. The public implementation also describes noise-aware correction of photometric differences and a robustness expression of the general form

$$
R
=
\operatorname{clamp}
\left(
s\exp\left(-\frac{d^2}{\sigma^2}\right)-t,
0,1
\right),
$$

with spatial filtering of the robustness map to protect boundaries. ([Lafenetre, Facciolo, and Eboli, *Implementing Handheld Burst Super-Resolution*, IPOL 2023](https://doi.org/10.5201/ipol.2023.460); [article](https://www.ipol.im/pub/art/2023/460/article_lr.pdf); [open implementation](https://github.com/Jamy-L/Handheld-Multi-Frame-Super-Resolution))

**What V1 takes from HFSR:**

- subpixel local registration rather than integer-only raw shifts;
- multilevel block matching plus a small continuous refinement;
- explicit robustness maps;
- motion-irregularity and neighborhood protection;
- sequential frame accumulation and bounded local support;
- the principle that CFA samples can be reconstructed safely when their color/site geometry is explicit.

**What V1 defers:**

- direct full-RGB reconstruction;
- joint demosaic and super-resolution;
- anisotropic kernel regression onto a denser RGB grid;
- detail synthesis that depends on favorable subpixel sampling.

HFSR is the strongest future V2 candidate, but its output and reconstruction responsibilities cross the current graph boundary. It creates more opportunities for color artifacts, kernel blur, and hard-to-isolate failures than a Bayer-output denoiser.

## 2.3 Adobe: public RAW-output evidence, but not enough merge math to reproduce

**Published evidence.** Adobe's Project Indigo article says its front end aligns and combines up to 32 RAW frames, uses less spatial smoothing because temporal combination reduces noise, and can save a scene-linear DNG before demosaicing with one color per pixel. It also distinguishes a robust handheld merge from a long-exposure mode that simply accumulates temporal change. ([Levoy and Kainz, *Project Indigo — a computational photography camera app*, Adobe Research, 2025](https://research.adobe.com/articles/indigo/indigo.html))

This is useful confirmation that a multi-frame, pre-demosaic DNG is a practical product boundary. The article does not publish enough registration and fusion math to reproduce Indigo's robust merger. This report therefore makes no claim about Indigo's internal estimator.

Adobe's DNG specification is much more actionable for the RAW normalization order, black/white semantics, noise metadata, and pre-/post-demosaic opcode boundaries. Those requirements are used directly in Sections 5 and 6. ([Adobe DNG 1.7.1.0 specification, September 2023](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf); [official DNG resources](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html))

## 2.4 Apple: useful patent disclosures, not a documented commercial pipeline

**Patent disclosure.** Apple's patent *Generalized fusion techniques based on minimizing variance and asymmetric distance measures* describes a reference image, registered candidate images, noise-dependent similarity, global and/or local registration, minimum-variance fusion weights, and asymmetric distance measures intended to reduce ghosting around edges. ([US11189017B1](https://patents.google.com/patent/US11189017B1/en))

A related patent describes fusion-adaptive noise reduction and contribution-aware noise maps. ([US11094039B1](https://patents.google.com/patent/US11094039B1/en)) A multi-band temporal denoising patent discusses pyramid representations and similarity measures that can depend on noise, black-level behavior, and scene information. ([US9626745B2](https://patents.google.com/patent/US9626745B2/en)) A later disclosure considers learned image-fusion decisions and reference selection. ([US11151702B1](https://patents.google.com/patent/US11151702B1/en))

These disclosures support several general engineering choices in V1:

- registered candidate measurements should be compared with the reference using a noise-dependent distance;
- accepted independent measurements should be weighted by predicted variance;
- edge asymmetry and neighborhood context are relevant to ghost suppression;
- reference selection and fallback are first-class decisions.

They do **not** establish that a particular iPhone pipeline uses these exact equations or thresholds. V1 uses the standard statistical inverse-variance derivation, not a transcription of patent-rendered equations.

## 2.5 Other respected approaches considered

### Robust variational reconstruction

Farsiu et al. formulate multiframe super-resolution with robust `L1` data terms and bilateral total-variation-style regularization. It is principled and resistant to some motion/model errors, but iterative optimization, regularizer tuning, and the risk of smoothing genuine texture make it a higher-risk first production path. ([Farsiu et al., *Fast and Robust Multiframe Super Resolution*, IEEE TIP 2004](https://users.soe.ucsc.edu/~milanfar/publications/journal/SRfinal.pdf); [DOI](https://doi.org/10.1109/TIP.2004.834669))

### Joint demosaicing and super-resolution

Joint reconstruction can exploit the fact that handheld shifts move different CFA samples over the scene. It can recover detail that a same-color merge followed by ordinary demosaicing cannot. That is the central advantage of HFSR and earlier joint demosaic/SR work. ([Farsiu et al., joint color SR/demosaicing paper](https://people.duke.edu/~sf59/tip_demos_final_color.pdf)) It is deferred because V1's primary goal is dependable denoising, not resolution recovery.

### Learned burst denoising

Kernel Prediction Networks jointly learn per-pixel kernels for aligned/noisy bursts and are a major reference point for learned RAW burst denoising. ([Mildenhall et al., *Burst Denoising with Kernel Prediction Networks*, CVPR 2018](https://openaccess.thecvf.com/content_cvpr_2018/papers/Mildenhall_Burst_Denoising_With_CVPR_2018_paper.pdf); [code](https://github.com/google/burst-denoising)) Learned methods can be excellent, but training data, camera-domain shift, model packaging, accelerator variance, explainability, and deterministic CPU fallback are unfavorable for the first native C++ method.

The "unprocessing" literature is valuable for generating more realistic synthetic RAW training/evaluation data, even though V1 is not learned. ([Brooks et al., *Unprocessing Images for Learned Raw Denoising*, CVPR 2019](https://openaccess.thecvf.com/content_CVPR_2019/papers/Brooks_Unprocessing_Images_for_Learned_Raw_Denoising_CVPR_2019_paper.pdf); [code](https://github.com/timothybrooks/unprocessing))

### Fourier burst accumulation and deblurring

Fourier Burst Accumulation can select sharper frequency content across differently blurred frames. It is a compelling future deblur path, not the right default for noise-only fusion because it can amplify frame-specific structure and does not by itself solve CFA-safe local motion rejection. ([Delbracio and Sapiro, *Burst Deblurring: Removing Camera Shake Through Fourier Burst Accumulation*, CVPR 2015](https://openaccess.thecvf.com/content_cvpr_2015/papers/Delbracio_Burst_Deblurring_Removing_2015_CVPR_paper.pdf))

### Fine-tuned joint demosaic/denoise

Joint demosaic/denoise networks and burst-specific fine tuning can improve RGB reconstruction, but they inherit the same output-boundary and model-risk objections as other learned methods. ([Ehret et al., *Joint Demosaicking and Denoising by Fine-Tuning of Bursts of Raw Images*, ICCV 2019](https://openaccess.thecvf.com/content_ICCV_2019/papers/Ehret_Joint_Demosaicking_and_Denoising_by_Fine-Tuning_of_Bursts_of_Raw_ICCV_2019_paper.pdf))

## 2.6 Comparison and V1 selection

| Approach | Bayer-safe | Ghost fallback | Detail potential | Explainability | CPU/desktop implementation risk | V1 decision |
|---|---:|---:|---:|---:|---:|---|
| Classic/public HDR+ family | Yes | Strong reference fallback | Good denoise; limited direct SR | High | Medium | Primary ancestor |
| HFSR joint CFA reconstruction | Yes | Strong robustness | Highest classical detail/SR potential | Medium-high | High | Future V2 |
| Reference-anchored inverse-variance spatial fusion | Yes | Explicit and exact | Good if registration is accurate | High | Medium | **Selected** |
| Robust variational `L1` reconstruction | Can be | Depends on formulation | High, but regularization-sensitive | High | High | Future alternative |
| Learned KPN/flow/fusion | Usually | Learned/implicit | Potentially very high | Low-medium | High | Future alternative |
| Fourier burst accumulation | After careful CFA handling | Weak for local scene change | Deblur-oriented | Medium | Medium | Separate deblur mode |

**V1 decision.** Use the registration and robustness lessons of HDR+ and HFSR, but perform a direct spatial-domain, same-CFA, reference-anchored inverse-variance estimate. This avoids the public ambiguities and FFT tuning of an HDR+ clone, avoids joint demosaic/SR complexity, and gives every output pixel an auditable explanation: which frames contributed, with what variance, confidence, gate, and fallback state.

---

# 3. Coordinate system and CFA conventions

These conventions are normative for implementation. Mixing coordinate units is one of the easiest ways to create one-pixel color errors.

## 3.1 Raw coordinates

Let the active RAW mosaic have width `W` and height `H`. Pixel centers have integer coordinates

$$
\mathbf p = (x,y)^\mathsf T,
\qquad
x\in\{0,\ldots,W-1\},
\quad
y\in\{0,\ldots,H-1\}.
$$

Coordinates are measured in **raw pixels**. The top-left sample of the active area is `(0,0)` after subtracting the stored active-area offset. EXIF display orientation is not applied inside this algorithm.

If the active area begins at full-sensor offset `a=(a_x,a_y)`, adjust CFA phase before defining active-area offsets:

$$
\mathbf o_c^{\mathrm{active}}
=
(\mathbf o_c^{\mathrm{sensor}}-\mathbf a)\bmod 2.
$$

Do not crop first and then reuse an unadjusted full-sensor CFA pattern.

## 3.2 Bayer color offsets

For a two-by-two Bayer repeat, define four site labels

$$
\mathcal C = \{R,G_0,G_1,B\}
$$

and one integer offset for each:

$$
\mathbf o_c=(o_{c,x},o_{c,y})^\mathsf T,
\qquad
o_{c,x},o_{c,y}\in\{0,1\}.
$$

For RGGB:

| Site | Offset `o_c` |
|---|---:|
| `R` | `(0,0)` |
| `G0` | `(1,0)` |
| `G1` | `(0,1)` |
| `B` | `(1,1)` |

Other Bayer layouts are represented by changing the labels assigned to the four offsets. `G0` and `G1` remain distinct through calibration, noise modeling, registration, and fusion even though they use nominally the same spectral filter.

For an active area that is not exactly even-sized, plane dimensions are

$$
W_c
=
\left\lfloor
\frac{W-1-o_{c,x}}{2}
\right\rfloor+1,
\qquad
H_c
=
\left\lfloor
\frac{H-1-o_{c,y}}{2}
\right\rfloor+1.
$$

The half-resolution plane for frame `i` and site `c` is

$$
P_i^c(u,v)
=
Q_i(2u+o_{c,x},\,2v+o_{c,y}),
$$

where `Q_i` is the normalized mosaic and `(u,v)` is measured in **plane pixels**. One plane pixel equals two raw pixels on either axis.

## 3.3 Warp direction

Define

$$
\mathbf W_i(\mathbf p)
$$

as the coordinate in source frame `i` corresponding to reference-frame raw coordinate `p`. The reference warp is identity:

$$
\mathbf W_0(\mathbf p)=\mathbf p.
$$

The source coordinate in plane `c` is

$$
\boldsymbol\xi_i^c(u,v)
=
\frac{
\mathbf W_i\!\left(2(u,v)^\mathsf T+\mathbf o_c\right)
-\mathbf o_c
}{2}.
$$

The source is interpolated **only in `P_i^c`** around `xi_i^c`. It is never interpolated in the packed Bayer mosaic as if neighboring samples shared a color.

## 3.4 Warp model

V1 uses

$$
\mathbf W_i(\mathbf p)
=
\mathbf A_i\mathbf p+\mathbf b_i
+
\Delta_i(\mathbf p),
$$

where:

- `A_i` is a 2-by-2 global affine linear component;
- `b_i` is a two-component global translation in raw pixels;
- `Delta_i(p)` is a confidence-interpolated local residual displacement in raw pixels.

The local field is not assumed valid everywhere. A warp sample is accompanied by a covariance and confidence, and fusion can decline to use it.


# 4. Normative end-to-end V1 pipeline

The following stage order is the implementation contract.

| Stage | Output | Cacheable? | Frame-local? |
|---|---|---:|---:|
| 0. Compatibility validation | accepted/rejected frame list and reasons | Yes | Yes |
| 1. Decode and safe RAW calibration | linearized normalized mosaic, masks, metadata | Yes | Yes |
| 2. Noise-model resolution | per-frame/per-CFA `S`, `O`, quality, optional maps | Yes | Yes |
| 3. Radiometric preparation | safe gain maps and initial exposure scalar | Yes | Yes |
| 4. CFA-plane pyramids | four corrected half-resolution pyramids | Yes | Yes |
| 5. Global registration | translation seed, affine warp, covariance/quality | Yes | Per alternate |
| 6. Exposure scalar refinement | robust `a_i` and uncertainty | Yes | Per alternate |
| 7. Local registration | tile residual shifts, covariances, confidence | Yes | Per alternate |
| 8. Reliability construction | per-alternate cell-grid acceptance map | Yes | Per alternate |
| 9. Tiled robust fusion | denoised Bayer mosaic and diagnostics | Yes | Streaming |
| 10. Output-domain conversion | canonical reference-domain mosaic | Yes | Tiled |
| 11. Atomic publish | output/cache commit or reference fallback | — | — |

A global cancellation token is checked at frame, pyramid-level, motion-grid row, and output-tile boundaries, and periodically inside long block-search loops.

## 4.1 Top-level behavior

```text
process(project, reference, includedFrames, parameters, cancel):
    validate reference
    compatible = []

    ref = prepare_frame(reference)
    resolve_noise_model(ref)

    for frame in includedFrames in stable project order:
        if frame == reference:
            continue
        result = validate_and_prepare(frame, ref)
        if result.compatible:
            compatible.append(result)
        else:
            record_skip(frame, result.reason)

    if compatible is empty:
        return exact_reference_output("No compatible alternate frames")

    build_reference_cfa_pyramids(ref)

    accepted = []
    for frame in compatible in stable order:
        check_cancel()
        build_frame_cfa_pyramids(frame)
        global = estimate_global_warp(ref, frame)
        if not global.valid:
            record_skip(frame, "GlobalRegistrationFailed")
            continue

        exposure = refine_exposure_scale(ref, frame, global)
        if not exposure.valid:
            record_skip(frame, "RadiometricScaleFailed")
            continue

        local = estimate_local_motion(ref, frame, global, exposure)
        if not local.has_usable_area:
            record_skip(frame, "LocalRegistrationFailed")
            continue

        reliability = build_reliability(ref, frame, local, exposure)
        accepted.append({frame, global, exposure, local, reliability})

    if accepted is empty:
        return exact_reference_output("All alternate frames rejected")

    for outputTile in reference_tiles in deterministic order:
        check_cancel()
        fusedTile = fuse_tile(ref, accepted, outputTile)
        write_to_temporary_result(fusedTile)

    atomically_commit_result_and_caches()
    return output_with_diagnostics()
```

A single alternate failure does not fail the node. The node fails only if the reference cannot be decoded or cannot satisfy the declared Bayer input contract.

---

# 5. RAW normalization, calibration, and processing order

## 5.1 Why order matters

**Published evidence.** The DNG specification's raw mapping sequence is, in broad form, linearization, black subtraction, normalization by the usable code range, and clipping. It defines black level as the zero-light encoding level, white level as the saturation coding level, and `NoiseProfile` in normalized linear RAW units. It also separates operations applied to as-read data, linear RAW before demosaic, and data after demosaic. ([Adobe DNG 1.7.1.0 specification](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf))

**V1 decision.** Convert every source format into one explicit internal contract before registration:

> A floating-point, scene-linear, black-subtracted, white-normalized Bayer mosaic with negative values preserved; a separate saturation mask; explicit safe multiplicative calibration maps; and no white balance, color conversion, demosaic, tone curve, sharpening, or geometric resampling.

## 5.2 Symbols and units

For frame `i`, raw position `p`, and CFA site `c=cfa(p)`:

| Symbol | Meaning | Unit |
|---|---|---|
| `r_i(p)` | stored sensor code | DN/code value |
| `L_i,c(r)` | pointwise linearization mapping | linearized DN |
| `B_i(p,c)` | computed black level | linearized DN |
| `Cwhite_i,c` | white/saturation level | linearized DN |
| `Bden_i,c` | denominator black reference required by the source format | linearized DN |
| `D_i,c = Cwhite_i,c - Bden_i,c` | usable linearized code span | DN |
| `q_i(p)` | normalized linear sensor measurement before multiplicative flat-field correction | dimensionless |
| `g_i(p,c)` | reversible pointwise calibration gain, principally lens shading/flat field | dimensionless |
| `a_i` | scalar mapping frame `i` to reference exposure | dimensionless |
| `k_i(p,c)=a_i g_i(p,c)` | total comparison-domain gain | dimensionless |
| `z_i(p)=k_i(p,c)q_i(p)` | comparison/fusion-domain signal | normalized reference-domain signal |

`a_0=1` for the reference.

## 5.3 Linearization, black, and white normalization

The conceptual mapping is

$$
q_i(\mathbf p)
=
\frac{
L_{i,c}\!\left(r_i(\mathbf p)\right)
-
B_i(\mathbf p,c)
}{
D_{i,c}
},
\qquad
D_{i,c}
=
C^{\mathrm{white}}_{i,c}
-
B^{\mathrm{den}}_{i,c}.
$$

For DNG, `Bden_i,c` must follow the specification's denominator convention, including the maximum computed black level for the relevant sample plane. For proprietary RAW, the decoder must expose an equivalent normalized-linear contract rather than forcing the fusion layer to guess format-specific rules.

The black model may contain:

- a repeating per-CFA black level;
- horizontal or vertical black deltas;
- a row/column correction derived from masked optical-black pixels;
- a camera/mode-specific correction already applied by the decoder.

**V1 decisions:**

1. Preserve negative `q_i` values. They are legitimate noise realizations after black subtraction and are useful to an unbiased shadow estimator.
2. Do not clamp `q_i` to `[0,1]` before alignment or fusion.
3. Record saturation separately from numerical clipping.
4. Do not infer black from the darkest visible scene regions. A black offset error creates correlated color and registration errors that can masquerade as scene change.
5. If optical-black pixels are available, a robust per-row/per-plane residual correction may be applied before active-area cropping. The correction and its uncertainty must be stored in the cache key and diagnostics.

## 5.4 Safe and unsafe pre-demosaic operations

A pre-demosaic operation is safe for V1 when it is pointwise or same-site and its effect on variance can be propagated. Examples include:

- pointwise linearization;
- black subtraction;
- a per-pixel or per-plane multiplicative gain map;
- known bad-pixel identification;
- a scalar digital scaling operation with known gain.

An operation is unsafe in V1 when it:

- geometrically warps the packed Bayer image;
- interpolates across incompatible CFA sites;
- performs an unknown nonlinear local transform;
- denoises or sharpens before the burst algorithm;
- repairs defects without exposing which samples were synthesized;
- changes CFA phase or crop geometry in a way that cannot be represented exactly.

For DNG, the decoder must inspect the opcode lists rather than assuming every pre-demosaic opcode is harmless. A required geometric or nonlocal opcode that cannot be deferred while preserving same-CFA samples makes the frame incompatible with V1.

## 5.5 Lens shading and flat-field correction

Camera shake moves a scene point across sensor locations. If lens shading or pixel-response variation is not corrected before photometric comparison, the same scene point can acquire a different value solely because it landed at a different sensor radius or pixel response.

Let `g_i(p,c)>0` be the product of all safe, reversible multiplicative corrections required to put the measurement into a spatially uniform comparison domain. Then

$$
x_i(\mathbf p)
=
g_i(\mathbf p,c)\,q_i(\mathbf p)
$$

is the exposure-uncorrected flat-fielded value and

$$
z_i(\mathbf p)
=
a_i\,x_i(\mathbf p)
=
a_i g_i(\mathbf p,c)q_i(\mathbf p)
$$

is the reference-exposure comparison value.

**V1 decision.** Apply/evaluate the gain at the source sample coordinate before temporal comparison and propagate the gain into the variance. Do not apply lens shading after fusion as though every contributing measurement came from the reference sensor location.

The reference-domain output is discussed in Section 10. In the recommended contract, the fused comparison-domain result is divided by the reference gain map so the existing downstream RAW pipeline can apply the reference's calibration once.

## 5.6 White balance and camera-space order

White balance is not needed to make same-CFA samples comparable. It is a per-color gain intended for color rendering, not sensor denoising.

**V1 decisions:**

- do not bake white balance into the fused mosaic;
- do not apply per-frame `AsShotNeutral` gains before fusion;
- retain the reference frame's white-balance metadata for downstream use;
- if a scalar visualization/phase-correlation guide is ever constructed, use one fixed set of reference white-balance coefficients for all frames, never each frame's own coefficients;
- do not apply camera color matrices, chromatic adaptation, tone curves, local contrast, or sharpening.

The processing order is therefore:

```text
stored RAW
  -> required as-read pointwise correction
  -> linearization
  -> black subtraction
  -> normalized linear mosaic
  -> masks/noise model
  -> safe flat-field/lens-shading gain for comparison
  -> scalar exposure matching
  -> CFA-safe registration and fusion
  -> return to selected reference mosaic domain
  -> ordinary downstream RAW development
```

## 5.7 Saturation and highlight policy

A sample is invalid as an alternate contribution when it is clipped or sufficiently close to the calibrated white level that clipping probability is material.

Define the normalized saturation guard

$$
m_{\mathrm{sat},i,c}
=
\max\left(
\frac{4}{D_{i,c}},
\;
2\sqrt{S_{i,c}+O_{i,c}}
\right),
$$

where the first term is four DN and the second is two modeled standard deviations near normalized signal 1. An initial validity test is

$$
q_i(\mathbf p) < 1-m_{\mathrm{sat},i,c}.
$$

The mask must also honor explicit decoder clipping/overflow flags. The two-sigma and four-DN values are initial engineering values, not standards.

**V1 policy:**

- a clipped alternate never contributes;
- interpolation support touching a clipped alternate sample is invalid;
- a clipped reference remains the output at that site;
- V1 does not replace clipped reference data from alternates;
- no universal highlight-mask dilation is applied unless sensor blooming tests justify it.

This policy gives up highlight recovery to avoid silently becoming a bracketed HDR algorithm.

## 5.8 Defective pixels

Known hot, dead, stuck, or otherwise defective samples are invalid temporal measurements.

- For registration proxies only, defects may be filled from a robust same-CFA neighborhood so one bad site does not disrupt a tile cost.
- For fusion, the original defective sample and any interpolation footprint touching it are invalid.
- If the reference site is defective, V1 may reconstruct it only when at least three high-confidence alternates agree under the same robust test. Otherwise, invoke the editor's existing single-frame defect repair after fusion.
- If the decoder has already repaired a site, it should expose a repair mask. When provenance is unavailable, the frame receives reduced quality status.

## 5.9 Robust scalar exposure/gain matching

Define the pre-scale flat-fielded source and reference values

$$
x_{i,j}=g_i(\mathbf p_j,c_j)q_i(\mathbf p_j),
\qquad
y_{0,j}=g_0(\mathbf p_j,c_j)q_0(\mathbf p_j),
$$

for sample locations `j` after a coarse geometric alignment.

When shutter time `t_i` and a calibrated linear system gain `G_i` are trustworthy, initialize with

$$
a_i^{\mathrm{meta}}
\approx
\frac{t_0G_0}{t_iG_i}.
$$

`G_i` must mean a gain proportional to RAW signal, not an arbitrary marketing ISO label. If the decoder's normalization or white-level semantics already absorb part of the gain, the camera profile must account for that. Otherwise use a robust median of valid `y_0/x_i` ratios as the initializer.

The desired scalar is

$$
a_i
=
\arg\min_{a>0}
\sum_j
\rho_\delta
\left(
\frac{y_{0,j}-a x_{i,j}}
{\sqrt{v_{0,j}+a^2v_{i,j}+\epsilon_v}}
\right),
$$

where:

- `rho_delta` is the Huber loss;
- `v_0,j` and `v_i,j` are pre-scale variances;
- `delta=2.5` initially;
- `epsilon_v` prevents division by zero.

The Huber weight is

$$
h_\delta(r)
=
\begin{cases}
1, & |r|\le\delta,\\[4pt]
\delta/|r|, & |r|>\delta.
\end{cases}
$$

With the denominator frozen at the current iterate, one IRLS update is

$$
\omega_j
=
\frac{
h_\delta(r_j)
}{
v_{0,j}+a^2v_{i,j}+\epsilon_v
},
$$

$$
a_{\mathrm{new}}
=
\frac{
\sum_j \omega_j x_{i,j}y_{0,j}
}{
\sum_j \omega_j x_{i,j}^2+\epsilon_a
}.
$$

Use three iterations, initialized by trustworthy exposure metadata or a robust median ratio.

### Exposure-fit sample selection

Use a deterministic stratified grid of at least `max(10,000, 0.001*W*H)` eligible samples when available. Exclude:

- saturated and near-black-invalid samples;
- defects and boundary-invalid samples;
- samples outside approximately `[0.01,0.85]` in normalized reference signal;
- tiles with failed coarse registration;
- tiles whose coarse patch difference is already a strong motion outlier.

Use all four CFA sites, but fit one scalar common to them.

### Exposure-fit diagnostics

After fitting the common scalar, compute—but do not apply—four per-site diagnostic fits `a_i,c` and optional intercepts `b_i,c`.

Initial rejection/downweight rules:

- hard scalar range: `0.5 <= a_i <= 2.0`;
- if trustworthy metadata exists, fitted scale should be within `±0.5 EV` of metadata;
- the four diagnostic scales should generally agree with the common scale within 2%;
- a fitted intercept whose magnitude exceeds `max(4/D, 2*sqrt(O))` indicates unresolved black offset;
- fewer than 10,000 valid samples or a nearly singular fit produces low radiometric confidence.

The 2%, `±0.5 EV`, and sample-count rules are **Needs experiment** values. A plane-dependent scale is not silently applied because it would change RAW chromaticity and could hide a black/white/metadata incompatibility.

### Scale uncertainty

An approximate scalar variance is

$$
\sigma_{a_i}^2
\approx
s_r^2
\left(
\sum_j \omega_j x_{i,j}^2+\epsilon_a
\right)^{-1},
$$

where `s_r` is a robust standardized residual scale. This uncertainty later contributes a signal-dependent variance term.

## 5.10 Canonical output-domain choice

There are two coherent graph contracts:

1. **Post-gain output:** the denoiser consumes lens shading/flat-field corrections and downstream nodes must not apply them again.
2. **Reference pre-gain output:** fusion occurs in the corrected domain, then the result is divided by the reference gain map so the ordinary reference RAW pipeline remains intact.

**V1 decision.** Prefer the second contract:

$$
\widehat q_0(\mathbf p)
=
\frac{
\widehat z(\mathbf p)
}{
g_0(\mathbf p,c)
}.
$$

If no alternate contributes materially, copy `q_0(p)` directly rather than computing `g_0 q_0 / g_0`; this makes the fallback exact in the canonical float representation.

Requirements:

- `g_0` must be finite and strictly positive;
- very large or invalid gains make the site reference-only;
- the cache and output metadata must state that the mosaic is linearized, black-subtracted, normalized, and pre-reference-gain;
- if the application's existing internal RAW contract is already post-gain, adapt the boundary once rather than mixing contracts per node.

Writing an enhanced DNG from this result is a separate metadata task. The original `NoiseProfile` no longer exactly describes the fused, selected, spatially varying, correlated residual noise.

---

# 6. Sensor-noise model and uncertainty propagation

## 6.1 Poisson-Gaussian model

**Published evidence.** A widely used RAW model combines Poisson photon-counting noise with signal-independent Gaussian disturbances such as read noise. Foi et al. also model clipping and estimate the signal-dependent standard deviation from RAW data. ([Foi et al., *Practical Poissonian-Gaussian Noise Modeling and Fitting for Single-Image Raw-Data*, IEEE TIP 2008](https://doi.org/10.1109/TIP.2008.2001399); [paper PDF](https://webpages.tuni.fi/foi/papers/Foi-PoissonianGaussianClippedRaw-2007-IEEE_TIP.pdf))

Let the collected photoelectron count be

$$
N_e\sim\operatorname{Poisson}(\lambda_e),
$$

and read noise in electrons be

$$
\eta_e\sim\mathcal N(0,\sigma_{r,e}^2).
$$

If conversion gain is `g` DN/electron and the normalized usable code span is `D` DN, then

$$
Y
=
\frac{g(N_e+\eta_e)}{D},
$$

$$
\mu
=
\mathbb E[Y]
=
\frac{g\lambda_e}{D},
$$

and

$$
\operatorname{Var}(Y\mid\mu)
=
\frac{g}{D}\mu
+
\left(\frac{g\sigma_{r,e}}{D}\right)^2.
$$

Therefore the normalized heteroscedastic model is

$$
Y=\mu+n,
\qquad
\mathbb E[n]=0,
\qquad
\operatorname{Var}(n\mid\mu)=S\mu+O,
$$

where:

- `Y` and `mu` are dimensionless normalized linear sensor signals;
- `S=g/D` is the shot-noise slope;
- `O=(g sigma_r,e/D)^2` is the read-noise variance;
- standard deviation is `sqrt(S mu+O)`.

At extremely low electron counts the Gaussian approximation to the Poisson component is imperfect. V1 uses the model for variance normalization and weighting, not as a claim that every shadow sample is exactly Gaussian.

## 6.2 DNG `NoiseProfile`

**Published evidence.** DNG defines a per-plane noise function of the form

$$
N_c(x)=\sqrt{S_c x+O_c},
\qquad
0\le x\le 1,
$$

and notes that this model represents shot and read noise while ignoring some effects such as fixed-pattern, thermal, and pixel-response nonuniformity. ([Adobe DNG 1.7.1.0 specification](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf))

Treat `R`, `G0`, `G1`, and `B` independently when profiles or calibration support it. Do not automatically average the two green-site models.

## 6.3 Quantization

For a uniform one-DN quantizer, normalized quantization variance is approximately

$$
v_q
=
\frac{1}{12D_{i,c}^2}.
$$

Add it to the offset term when it is not already included in the calibrated noise profile:

$$
O_{i,c}
\leftarrow
O_{i,c}+v_q.
$$

Avoid double counting when the empirical profile was fitted from quantized samples.

## 6.4 Gain propagation

Suppose a normalized measurement `q` has

$$
\operatorname{Var}(q\mid\mu_q)
=
S\mu_q+O
$$

and is multiplied by a known positive gain `k`:

$$
z=kq,
\qquad
\nu=\mathbb E[z]=k\mu_q.
$$

Then

$$
\operatorname{Var}(z\mid\nu)
=
k^2(S\mu_q+O)
=
kS\nu+k^2O.
$$

This is the required formula for exposure matching and lens shading. It is not correct to multiply both `S` and `O` by `k^2` while continuing to express the variance as a function of the gain-corrected signal `nu`; the shot coefficient becomes `kS`.

For frame `i`, site `c`, and source coordinate `s`:

$$
v^{\mathrm{sample}}_{i,c}(\mathbf s;\nu)
=
k_i(\mathbf s,c)S_{i,c}\nu
+
k_i^2(\mathbf s,c)O_{i,c}.
$$

The common expected signal `nu` should come from a shared reference-domain pilot, not from each candidate's noisy observation. Section 15.3 demonstrates why observation-dependent inverse-variance weights are biased.

**V1 decision.** At output site `p`, use

$$
\nu_p=\max\left(z_0(\mathbf p),0\right)
$$

as the common shot-noise pilot for all frames. An optional reference-only same-CFA pilot smoother may be evaluated later, but it is not part of the initial contract.

## 6.5 Interpolation variance

A same-CFA interpolated source value is

$$
y_i
=
\sum_{j=1}^{K} h_j z_{i,j},
\qquad
\sum_j h_j\approx1,
$$

where `h_j` are interpolation coefficients and `K=16` for a 4-by-4 bicubic footprint.

Under the V1 independent-sample approximation,

$$
v^{\mathrm{interp}}_i
=
\sum_{j=1}^{K} h_j^2
v^{\mathrm{sample}}_{i,j}.
$$

Negative bicubic coefficients are valid; variance uses `h_j^2`, not `h_j`. If the decoder has already introduced spatial noise correlation, this formula is optimistic and the frame should either be rejected or use a calibrated covariance correction.

## 6.6 Exposure-scale uncertainty

Let the fitted scale have variance `sigma_a_i^2`. A first-order contribution is

$$
v^{\mathrm{scale}}_i
=
\left(
\frac{\sigma_{a_i}}{a_i}
\right)^2
y_i^2.
$$

This term is usually small for a well-constrained fit but becomes useful for downgrading frames with weak radiometric evidence. The scale error is correlated across the image; it should reduce a candidate's fusion weight, but it must not widen the motion/outlier acceptance gate and thereby make a local scene change look statistically valid.

## 6.7 Registration uncertainty

Let the source-coordinate error be a zero-mean vector with covariance

$$
\boldsymbol\Sigma_{W,i}
\quad
[\text{raw pixels}^2].
$$

Using the first-order Taylor approximation

$$
I(\mathbf s+\delta\mathbf s)
\approx
I(\mathbf s)
+
\nabla I(\mathbf s)^\mathsf T\delta\mathbf s,
$$

the variance caused by coordinate uncertainty is

$$
v^{\mathrm{reg}}_i
=
\nabla I^\mathsf T
\boldsymbol\Sigma_{W,i}
\nabla I.
$$

The gradient must be in **signal per raw pixel**. If it is computed on a half-resolution CFA plane, divide the plane-coordinate derivative by two.

This term is central to detail preservation:

- in a flat patch, motion is unobservable but the gradient is small, so a large coordinate covariance may still have little photometric cost;
- on an edge or fine texture, the same positional uncertainty produces a large effective variance and sharply reduces fusion weight.

## 6.8 Effective variance used by fusion

Use two related variances with different safety meanings. The alternate **gate variance** is

$$
v^{\mathrm{gate}}_i
=
v^{\mathrm{interp}}_i
+
v^{\mathrm{reg}}_i
+
v^{\mathrm{model}}_i
+
\epsilon_{\mathrm{num}},
$$

and the **fusion variance** is

$$
v^{\mathrm{fuse}}_i
=
v^{\mathrm{gate}}_i
+
v^{\mathrm{scale}}_i.
$$

The reference has no fitted exposure-scale uncertainty because `a_0=1`, so

$$
v^{\mathrm{gate}}_0
=
v^{\mathrm{fuse}}_0
=
k_0S_{0,c}\nu_p
+
k_0^2O_{0,c}
+
v^{\mathrm{model}}_0
+
\epsilon_{\mathrm{num}}.
$$

Registration/patch/pixel residuals use `v_gate`; inverse-variance fusion uses `v_fuse`. This separation prevents a poorly constrained global exposure scalar from making local motion easier to accept while still reducing that frame's numeric influence.

Use a small arithmetic safeguard such as

$$
\epsilon_{\mathrm{num}}=10^{-12}
$$

in comparison-domain signal-squared units. Quantization is **not** added again here: it must appear exactly once, either inside `O` as described in Section 6.3 or in an explicitly separated propagated quantization term. The larger quantization-aware lower bounds used when dividing by a variance are defined in Section 9.2; taking a maximum there does not add a second noise source.

A calibrated residual model may use

$$
v^{\mathrm{model}}_i
=
\tau_{0,i,c}^2
+
\left(\tau_{1,i,c}\nu_p\right)^2,
$$

where `tau_0` captures unmodeled signal-independent variance and `tau_1` captures residual proportional response variation. Both default to zero until measured. Do not inflate them merely to make alignment residuals look acceptable; overestimated noise makes true motion easier to fuse and therefore increases ghost risk.

## 6.9 Correlated noise and fixed-pattern limits

The independent Poisson-Gaussian model does not capture:

- row/column banding;
- frame-wise black drift;
- pixel-response nonuniformity;
- thermal patterns;
- electronic interference;
- covariance introduced by an upstream repair or denoiser.

A per-pixel variance cannot make correlated row noise average like independent noise. Preferred handling order:

1. correct measurable frame-wise black/row offsets from optical-black data;
2. calibrate residual fixed-pattern terms by camera/mode/ISO;
3. report correlated-noise diagnostics;
4. avoid promising `1/sqrt(N)` reduction for the correlated component.

## 6.10 Noise model source and quality

Use the following hierarchy:

| Quality | Source | Fusion behavior |
|---|---|---|
| `TrustedMetadata` | valid DNG/manufacturer profile for this exact frame/mode | normal thresholds |
| `CalibratedCamera` | offline flat/dark calibration keyed by camera, mode, ISO/gain, temperature range | normal thresholds |
| `EstimatedBurst` | conservative same-CFA estimate that passes diagnostics | stricter gates and weight caps |
| `GenericLowConfidence` | rough format/specification fallback | very strict gates; may return reference |
| `Unavailable` | no defensible model | reference-only success |

### Offline calibration

A practical camera calibration should use pairs of flat fields across signal levels and dark frames. For two independent flat captures of the same level,

$$
\widehat v
=
\frac{1}{2}
\operatorname{Var}(Y_1-Y_2),
$$

which removes the fixed flat-field scene pattern. Regress `v` against mean signal per CFA site:

$$
\widehat v(\mu)=S_c\mu+O_c.
$$

Dark pairs constrain `O_c` and expose row/column covariance. The EMVA 1288 photon-transfer framework is a useful calibration reference, and Bayer sites should be analyzed separately. ([EMVA 1288 standard resources](https://www.emva.org/standards-technology/emva-1288/))

### Burst estimate fallback

When metadata/profile lookup fails, V1 may attempt a low-confidence estimate on the reference:

1. split into four CFA planes;
2. divide each plane into deterministic blocks;
3. exclude clipped blocks and blocks with strong gradients/edges;
4. estimate local mean and a robust high-pass noise variance;
5. fit the **lower envelope** of variance versus mean so scene texture, which increases variance, is less likely to be mistaken for noise;
6. constrain `S>=0`, `O>=quantizationVariance`;
7. require broad signal coverage, enough blocks, and stable per-green estimates.

This estimate is intentionally conservative. If diagnostics fail, return the reference rather than accepting motion under an invented noise scale.

### Why missing-noise handling must be conservative

An underestimated noise model rejects some valid alternates and falls back toward the reference. An overestimated model treats real scene differences as plausible noise and can create ghosts. Therefore low-confidence operation tightens rejection thresholds and caps alternate influence rather than widening the noise model until frames pass.

---


# 7. CFA-safe registration

Registration is the dominant ghost-risk component. V1 therefore treats a warp as an estimate plus uncertainty, not as an unquestioned coordinate map.

## 7.1 Four-plane representation

The packed Bayer mosaic is split into four half-resolution planes. Every registration residual compares a plane only with the same plane:

$$
R \leftrightarrow R,\quad
G_0 \leftrightarrow G_0,\quad
G_1 \leftrightarrow G_1,\quad
B \leftrightarrow B.
$$

This avoids the classic error of comparing a red sample in one frame to a green sample in another after a fractional raw-pixel shift.

For registration proxies, each plane contains

$$
X_i^c(u,v)
=
g_i(2(u,v)^\mathsf T+\mathbf o_c,c)
P_i^c(u,v).
$$

The scalar exposure factor `a_i` is applied on demand in the cost. Defects may be proxy-filled from the same plane, but their validity remains zero.

## 7.2 Gaussian pyramids and variance pyramids

Build four Gaussian pyramids per frame with levels `ell=0,1,2,3`, where level 0 is the original half-resolution CFA plane. A recommended separable five-tap kernel is

$$
\mathbf h
=
\frac{1}{16}[1,4,6,4,1].
$$

Each next level is filtered and decimated by two. One level-`ell` pixel spans

$$
2^{\ell+1}
$$

raw pixels on each axis.

Propagate a variance proxy through the same linear filter using squared coefficients:

$$
V_{\ell+1}
=
\downarrow_2
\left(
(\mathbf h^2\otimes\mathbf h^2) * V_\ell
\right).
$$

This ignores covariance between neighboring pyramid pixels created by overlapping filters, but it is preferable to pretending that the finest-level variance still applies after averaging.

Pyramid convolution may use reflection internally, but registration-valid masks are eroded so reflected values never create confident correspondences near the boundary.

## 7.3 Global translation by multichannel phase correlation

**Published evidence.** Phase correlation is a standard translation initializer, and HDR+ uses FFT-based burst alignment. Subpixel phase-correlation methods can refine the peak efficiently. ([Hasinoff et al., 2016](https://hdrplusdata.org/hdrplus.pdf); [Guizar-Sicairos, Thurman, and Fienup, *Efficient subpixel image registration algorithms*, Optics Letters 2008](https://opg.optica.org/ol/fulltext.cfm?uri=ol-33-2-156))

At the coarsest usable pyramid level:

1. subtract a robust mean from each valid CFA plane;
2. apply a Hann window over the common valid region;
3. optionally suppress only the DC bin;
4. compute one cross-power spectrum per matching CFA site;
5. combine the normalized spectra.

Let `F_0,c(k)` and `F_i,c(k)` be the Fourier transforms. Define

$$
C_i(\mathbf k)
=
\sum_{c\in\mathcal C}
\eta_c
\frac{
F_{0,c}(\mathbf k)
F_{i,c}^*(\mathbf k)
}{
\left|
F_{0,c}(\mathbf k)
F_{i,c}^*(\mathbf k)
\right|
+
\epsilon_{\mathrm{fft},c}
},
$$

where `eta_c>=0` weights a plane by valid coverage and coarse SNR. The inverse FFT peak gives the translation in coarsest **plane pixels**, which is converted to raw pixels.

Use a local quadratic peak fit or localized upsampled DFT for subpixel initialization. Reject the phase result when:

- peak-to-sidelobe ratio is below a calibrated threshold;
- the peak lies outside the allowed overlap;
- different CFA planes produce irreconcilable peaks;
- the common valid region is too small.

**V1 decision.** Phase correlation is an initializer only. It does not determine local fusion eligibility.

## 7.4 Global affine refinement

Small handheld rotation, scale variation from focus/OIS behavior, and residual shear are better represented by an affine seed than by translation alone. A homography is deferred because its extra degrees of freedom can overfit weak RAW structure and still cannot explain depth parallax.

Parameterize the affine warp around the image center `pc`:

$$
\mathbf W_i^{\mathrm{aff}}(\mathbf p)
=
\mathbf p_c
+
(\mathbf I+\mathbf M_i)(\mathbf p-\mathbf p_c)
+
\mathbf t_i,
$$

with six parameters

$$
\boldsymbol\theta_i
=
(m_{00},m_{01},m_{10},m_{11},t_x,t_y)^\mathsf T.
$$

At each pyramid level, minimize the multichannel robust standardized cost

$$
E(\boldsymbol\theta)
=
\sum_{c,\mathbf u}
m_{c,\mathbf u}
\rho_\delta
\left(
\frac{
X_0^c(\mathbf u)
-
a_i X_i^c(\boldsymbol\xi_i^c(\mathbf u;\boldsymbol\theta))
}{
\sqrt{
V_0^c(\mathbf u)
+
a_i^2V_i^c(\boldsymbol\xi_i^c)
+
\epsilon_v
}}
\right),
$$

where `m` is a validity mask. Coordinates are converted using Section 3.3.

For residual `r_j`, the Gauss-Newton Jacobian is

$$
\mathbf J_j
=
-\frac{
a_i
\nabla X_i^c
\frac{\partial\boldsymbol\xi_i^c}{\partial\boldsymbol\theta}
}{
\sqrt{V_0+a_i^2V_i+\epsilon_v}
}.
$$

A robust update solves

$$
\left(
\mathbf J^\mathsf T\mathbf W_\rho\mathbf J
+
\lambda
\operatorname{diag}
(\mathbf J^\mathsf T\mathbf W_\rho\mathbf J)
+
\lambda_0\mathbf I
\right)
\Delta\boldsymbol\theta
=
-\mathbf J^\mathsf T\mathbf W_\rho\mathbf r.
$$

Use double precision for the 6-by-6 normal equations, center/scale image coordinates, and reject a step that increases the robust objective. Five to ten iterations per level are sufficient as an initial range.

Retain an approximate covariance for the final affine parameters. If `H_theta` is the robust, standardized 6-by-6 normal matrix and `s_theta` is a robust residual scale, use

$$
\boldsymbol\Sigma_{\theta,i}
\approx
\max(1,s_{\theta}^2)
\left(
\mathbf H_{\theta,i}
+
\lambda_{\theta,\Sigma}\mathbf I
\right)^{-1}.
$$

Transform this covariance back if centered/scaled fitting coordinates were used. For centered raw coordinate

$$
\bar{\mathbf p}
=
(x-p_{c,x},\;y-p_{c,y})^\mathsf T,
$$

the 2-by-6 positional Jacobian is

$$
\mathbf J_{\theta}(\mathbf p)
=
\begin{bmatrix}
\bar p_x & \bar p_y & 0 & 0 & 1 & 0\\
0 & 0 & \bar p_x & \bar p_y & 0 & 1
\end{bmatrix},
$$

and the affine contribution to source-coordinate uncertainty is

$$
\boldsymbol\Sigma_{\mathrm{aff},i}(\mathbf p)
=
\mathbf J_{\theta}(\mathbf p)
\boldsymbol\Sigma_{\theta,i}
\mathbf J_{\theta}(\mathbf p)^\mathsf T.
$$

This covariance is a local linear approximation and must be clamped and calibrated just like the tile covariance. A translation-only fallback uses the corresponding reduced Jacobian/covariance rather than pretending its global seed is exact.

Because exposure and affine alignment are coupled, the practical global sequence is:

```text
phase-correlation translation
    -> provisional metadata/median exposure scale
    -> coarse affine refinement
    -> robust scalar exposure refinement
    -> one final affine pass
```

The stage table in Section 4 denotes cache boundaries, not a prohibition on this small interleaving.

Initial affine plausibility guards:

- singular values of the linear part in `[0.95,1.05]`;
- absolute rotation below `5 degrees`;
- determinant in `[0.90,1.10]`;
- final common overlap at least 50%;
- finite, sufficiently conditioned normal matrix;
- robust standardized residual improved over translation-only.

These are corpus-tuning values. On failure, fall back in order: affine to translation, translation to identity only if the photometric evidence is still noise-consistent, otherwise reject the frame.

## 7.5 Local tile motion

The local field represents residual motion after the global affine warp.

### Grid and patches

At the finest plane level:

- match patch: `32 x 32` plane pixels = `64 x 64` raw pixels;
- grid stride: `16` plane pixels = `32` raw pixels;
- four CFA planes contribute to each tile cost;
- require at least 70% valid comparison samples.

At coarser levels, use `16 x 16` level pixels with 50% overlap. These physical supports become larger toward the coarsest level.

### Discrete search

At level `ell`, a one-pixel displacement equals `2^(ell+1)` raw pixels. Recommended residual search radii from fine to coarse are:

$$
R_0=1,\quad R_1=2,\quad R_2=2,\quad R_3=4
$$

level pixels per axis.

At the coarsest level, search the full `(2R_3+1)^2` window around the global prediction. At each finer level:

1. upsample the best parent displacement;
2. include up to the three best distinct parent/neighbor candidate shifts;
3. search the local radius around each seed;
4. deduplicate candidates;
5. retain the best and the best well-separated second candidate.

This follows the coarse-to-fine candidate-propagation spirit of the public HDR+ implementation while using a same-CFA multichannel cost.

### Noise-normalized robust block cost

For tile `t` and candidate residual displacement `d`, define

$$
r_{j,c}(\mathbf d)
=
\frac{
X_0^c(\mathbf u_j)
-
a_i X_i^c(
\boldsymbol\xi_i^c(\mathbf u_j;\mathbf d)
)
}{
\sqrt{
V_0^c(\mathbf u_j)
+
a_i^2V_i^c(\boldsymbol\xi_i^c)
+
\epsilon_v
}}.
$$

The tile cost is

$$
E_t(\mathbf d)
=
\frac{
\sum_{j,c}m_{j,c}
\rho_{2.5}\!\left(r_{j,c}(\mathbf d)\right)
}{
\sum_{j,c}m_{j,c}+\epsilon_m
}.
$$

Use a Huber loss for registration because the optimizer needs a stable gradient. Redescending rejection is applied later, after a candidate warp exists.

## 7.6 Subpixel refinement

**Published evidence.** The public HDR+ analysis refines a local displacement by fitting a quadratic to the cost neighborhood. HFSR's public description/implementation uses a small number of inverse-compositional alignment iterations. Classical Lucas-Kanade and inverse-compositional analysis provide the underlying local least-squares framework. ([Lucas and Kanade, 1981](https://publications.ri.cmu.edu/storage/publications/pub_files/pub3/lucas_bruce_d_1981_1/lucas_bruce_d_1981_1.pdf); [Baker and Matthews, 2002](https://publications.ri.cmu.edu/storage/publications/pub_files/pub3/baker_simon_2002_3/baker_simon_2002_3.pdf))

**V1 decision.** Use translation-only robust Gauss-Newton at the finest plane level, initialized by discrete block matching. Template gradients may be precomputed, but robust/noise weights and the 2-by-2 Hessian are recomputed each iteration.

For displacement `d` in plane pixels,

$$
r_j(\mathbf d)
=
\frac{
T_j-I_j(\mathbf u_j+\mathbf d)
}{
s_j
},
$$

$$
\mathbf J_j
=
-\frac{\nabla I_j}{s_j}.
$$

Solve

$$
\left(
\sum_j w_j\mathbf J_j^\mathsf T\mathbf J_j
+\lambda\mathbf I
\right)\Delta\mathbf d
=
-\sum_j w_j\mathbf J_j^\mathsf T r_j,
$$

where `w_j` is the Huber IRLS weight. Apply three iterations initially.

Safeguards:

- reject non-finite steps;
- cap one iteration to `0.75` finest plane pixel;
- reject a final refinement farther than `1.0` plane pixel from the discrete seed;
- stop when `||Delta d|| < 0.01` plane pixel;
- reject if the robust cost increases twice;
- require enough valid samples after every warp.

The resulting displacement is converted to raw pixels by multiplying by two.

## 7.7 Tile covariance

Let the final standardized robust Hessian be

$$
\mathbf H_t
=
\sum_j w_j
\mathbf J_j^\mathsf T\mathbf J_j.
$$

Estimate residual scale by

$$
s_{r,t}
=
1.4826
\operatorname{median}_j
\left|
r_j-\operatorname{median}(r)
\right|,
$$

and use

$$
\widetilde s_{r,t}^2
=
\max(1,s_{r,t}^2).
$$

An approximate finest-plane covariance is

$$
\boldsymbol\Sigma^{\mathrm{plane}}_t
=
\widetilde s_{r,t}^2
\left(
\mathbf H_t+\lambda_\Sigma\mathbf I
\right)^{-1}.
$$

Convert to raw-pixel covariance:

$$
\boldsymbol\Sigma^{\mathrm{local}}_t
=
4\boldsymbol\Sigma^{\mathrm{plane}}_t.
$$

At node center `p_t`, form the source-coordinate covariance carried by the warp:

$$
\boldsymbol\Sigma_t
=
\boldsymbol\Sigma^{\mathrm{local}}_t
+
\boldsymbol\Sigma_{\mathrm{aff},i}(\mathbf p_t)
+
\sigma_{\mathrm{warp},0}^2\mathbf I,
$$

with `sigma_warp,0=0.05` raw pixel initially. For a `FlatSafe` node, replace the locally estimated term by `Sigma_flat` from Section 7.8. The sum treats global and local fit errors as independent; this is intentionally conservative and is an engineering approximation, not a published identity. Calibrate it against known synthetic displacement error, and reduce a demonstrated double count through an explicit versioned covariance-calibration factor rather than silently dropping the global term.

Clamp covariance eigenvalues to a finite range such as `[0.01^2, 4^2]` raw-pixel-squared before later arithmetic. The covariance is an approximation: it captures local photometric information but not all model error, which is why the model floor, forward/backward test, and inter-node disagreement are all retained.

## 7.8 Tile confidence

### Residual confidence

Use a capped standardized residual statistic

$$
\chi_t^2
=
\operatorname{mean}_j
\left[
\min(r_j^2,16)
\right],
$$

and

$$
c_{\mathrm{res},t}
=
\exp
\left(
-\frac{1}{2}\max(\chi_t^2-1,0)
\right).
$$

A correct noise model and static alignment should produce a value near 1 before robust selection.

### Positional precision

Define

$$
\sigma_{\mathrm{pos},t}
=
\sqrt{
\frac{1}{2}
\operatorname{tr}
\left(
\boldsymbol\Sigma_t
\right)
}
$$

and

$$
c_{\mathrm{prec},t}
=
\frac{
1
}{
1+
\left(
\sigma_{\mathrm{pos},t}/0.35
\right)^2
}.
$$

The `0.35` scale is in raw pixels and requires calibration.

### Match uniqueness

Let `E_1` be the best discrete cost and `E_2` the best candidate at least one finest plane pixel away. Define

$$
u_t
=
\frac{E_2-E_1}{E_1+\epsilon_E}.
$$

With the quintic smootherstep

$$
S_5(x)=6x^5-15x^4+10x^3,
\qquad 0\le x\le1,
$$

define

$$
c_{\mathrm{unique},t}
=
S_5
\left(
\operatorname{clamp}
\left(
\frac{u_t-0.02}{0.15-0.02},
0,1
\right)
\right).
$$

### Flat-safe exception

A perfectly flat patch has no identifiable shift, but shifting it does not change its value. Treating all unobservable motion as invalid would prevent denoising where it is most useful.

Call a tile locally unobservable when either:

$$
\sigma_{\mathrm{pos},t}>1.0
\quad\text{raw pixel}
$$

under the ordinary Hessian covariance, or the 2-by-2 Hessian condition number exceeds `10^4`.

For the parent/global predicted displacement, assign a conservative trial covariance

$$
\boldsymbol\Sigma_{\mathrm{flat}}
=
1.0^2\mathbf I
\quad[\text{raw pixels}^2].
$$

Over the patch, compute

$$
\gamma_{\mathrm{flat}}
=
\operatorname{median}_j
\frac{
\nabla I_j^\mathsf T
\boldsymbol\Sigma_{\mathrm{flat}}
\nabla I_j
}{
v^{\mathrm{sensor}}_{0,j}
+
v^{\mathrm{sensor}}_{i,j}
+
\epsilon_v
}.
$$

A tile may enter `FlatSafe` only when all are true:

- it is locally unobservable by the criterion above;
- the parent/global prediction gives `chi_t^2 <= 1.5`;
- `gamma_flat <= 0.25`;
- valid comparison coverage is at least 80%;
- every structured node in the immediate grid neighborhood differs from the prediction by at most `0.5` raw pixel; if no structured neighbor exists, this test is neutral;
- the source support is fully valid.

For `FlatSafe`:

- use the parent/global predicted displacement;
- store `Sigma_flat`;
- set uniqueness and forward/backward factors to 1;
- set tile confidence to residual confidence times coverage confidence;
- let `grad^T Sigma grad` reduce final fusion weight if the patch was not actually harmless;
- retain all later patch, neighborhood, and per-pixel rejection.

If a low-information tile is not noise-consistent, reject it. The `1.0`, `0.25`, `80%`, and `0.5` thresholds are **Needs experiment** values.

### Coverage confidence

Let `f_valid` be the valid comparison fraction. Define

$$
c_{\mathrm{coverage},t}
=
S_5
\left(
\operatorname{clamp}
\left(
\frac{f_{\mathrm{valid}}-0.70}{0.95-0.70},
0,1
\right)
\right).
$$

## 7.9 Forward/backward consistency

Estimate a reverse field from alternate frame `i` to the reference using the same machinery. For reference coordinate `p`:

$$
\mathbf s
=
\mathbf W_{0\rightarrow i}(\mathbf p),
$$

$$
\mathbf e_{\mathrm{fb}}
=
\mathbf W_{i\rightarrow0}(\mathbf s)-\mathbf p.
$$

Combine forward and reverse covariance:

$$
\boldsymbol\Sigma_{\mathrm{fb}}
=
\boldsymbol\Sigma_f
+
\boldsymbol\Sigma_b
+
\sigma_{\mathrm{fb},0}^2\mathbf I,
$$

with `sigma_fb,0=0.1` raw pixel initially. The Mahalanobis closure error is

$$
m_{\mathrm{fb}}
=
\mathbf e_{\mathrm{fb}}^\mathsf T
\boldsymbol\Sigma_{\mathrm{fb}}^{-1}
\mathbf e_{\mathrm{fb}}.
$$

Define

$$
c_{\mathrm{fb}}
=
\exp
\left[
-\frac{1}{2}\max(m_{\mathrm{fb}}-2,0)
\right].
$$

Hard reject when either:

$$
m_{\mathrm{fb}}>9.21
$$

(the 99% quantile of a two-degree-of-freedom chi-square distribution) or

$$
\|\mathbf e_{\mathrm{fb}}\|_2>2
$$

raw pixels.

The statistical threshold is only meaningful if covariance is calibrated. The Euclidean cap prevents a badly inflated covariance from accepting an obviously inconsistent field.

## 7.10 Normal tile confidence

For a structured, observable tile:

$$
c_t
=
c_{\mathrm{res},t}
c_{\mathrm{prec},t}
c_{\mathrm{unique},t}
c_{\mathrm{fb},t}
c_{\mathrm{coverage},t}.
$$

For `FlatSafe`, omit precision/uniqueness/forward-backward from this product and rely on residual, coverage, covariance propagation, neighborhood protection, and later photometric gates.

A product is intentionally conservative. A geometric mean is a possible later tuning alternative, not part of V1.

## 7.11 Interpolating the local motion field

Let the four grid nodes surrounding output coordinate `p` have bilinear tent coefficients `b_t(p)`, displacement `d_t`, confidence `c_t`, and total source-coordinate covariance `Sigma_t` from Section 7.7. Set

$$
\alpha_t
=
b_t(\mathbf p)c_t,
$$

$$
\boldsymbol\mu_d
=
\frac{
\sum_t\alpha_t\mathbf d_t
}{
\sum_t\alpha_t+\epsilon_\alpha
}.
$$

The interpolated covariance includes within-node uncertainty and between-node disagreement:

$$
\boldsymbol\Sigma_d
=
\frac{
\sum_t
\alpha_t
\left[
\boldsymbol\Sigma_t
+
(\mathbf d_t-\boldsymbol\mu_d)
(\mathbf d_t-\boldsymbol\mu_d)^\mathsf T
\right]
}{
\sum_t\alpha_t+\epsilon_\alpha
}.
$$

The scatter term is essential near parallax and motion boundaries. Define the between-node part `Sigma_between` from the outer-product term alone. Mark the warp ambiguous when

$$
\sqrt{
\lambda_{\max}
(\boldsymbol\Sigma_{\mathrm{between}})
}
>
0.75
$$

raw pixel.

In an ambiguous region, do not average the competing flows and hope the photometric gate repairs the result. Mark the alternate invalid for fusion at that location. The threshold is a **Needs experiment** value.

## 7.12 Same-CFA subpixel sampler

Use separable Keys bicubic interpolation with parameter `a=-0.5`. For `t=|x|`,

$$
\kappa_a(x)
=
\begin{cases}
(a+2)t^3-(a+3)t^2+1, & 0\le t\le1,\\
at^3-5at^2+8at-4a, & 1<t<2,\\
0, & t\ge2.
\end{cases}
$$

For plane coordinate `xi=(xi_x,xi_y)`, the 4-by-4 coefficient is

$$
h_{mn}
=
\kappa_a(\xi_x-m)
\kappa_a(\xi_y-n).
$$

([Keys, *Cubic Convolution Interpolation for Digital Image Processing*, IEEE 1981](https://doi.org/10.1109/TASSP.1981.1163711))

Reasons for the choice:

- materially less blur than bilinear interpolation;
- fixed compact support;
- analytic derivatives;
- exact interpolation at integer positions;
- straightforward variance propagation;
- no cross-CFA mixing.

**Needs experiment.** Bicubic negative lobes can ring around extreme clipped edges. Compare `a=-0.5` with a less sharp cubic and with Lanczos-2 on the validation corpus, but ship one sampler, not per-image switching.

Do not clamp bicubic overshoot before fusion. Reject non-finite results and rely on clipping masks/gates; clamp only at a later format/render boundary if required.

## 7.13 Boundary handling

For final fusion:

- the entire 4-by-4 same-plane footprint must lie inside the valid active area;
- every footprint sample must be nondefective and nonsaturated;
- no edge replication, reflection, wrap, or constant padding is allowed;
- if support is incomplete, that alternate contributes zero;
- the output still covers the full reference active area because the reference sample is always available.

This produces a natural reference-only border rather than invented data.

## 7.14 Local registration pseudocode

```text
estimate_local_motion(ref, alt, global, exposure):
    forward = register_direction(ref, alt, global, exposure)
    reverse = register_direction(alt, ref, inverse(global), 1/exposure)

    for each forward tile/node:
        if node is structured:
            compute reverse closure and c_fb
            reject hard forward/backward failures
        else if node is FlatSafe:
            keep predicted displacement with broad covariance
        else:
            reject node

    return grid_with_displacement_covariance_confidence(forward)

register_direction(template, source, global, exposure):
    parentCandidates = initialize_from_global_at_coarsest()

    for level from coarsest to finest:
        for tile in deterministic scan order:
            seeds = propagated_parent_and_neighbor_candidates(tile)
            best, second = robust_discrete_search(
                template.planes[level],
                source.planes[level],
                seeds,
                searchRadius[level],
                exposure
            )
            store best and separated second

    for finest tile:
        if enough structure:
            refine translation for 3 robust Gauss-Newton iterations
            compute Hessian, covariance, residual, uniqueness
            classify Structured or Rejected
        else:
            test FlatSafe conditions

    return tile grid
```

---


# 8. Robust rejection and ghost prevention

No single motion mask is sufficient. V1 combines independent failure evidence at four scales:

1. frame-level compatibility and radiometric checks;
2. tile-level registration confidence and covariance;
3. cell/patch-level photometric reliability;
4. per-pixel robust gating.

A candidate contributes only when all four layers permit it.

## 8.1 Hard-invalid conditions

Set candidate contribution to zero when any of the following is true:

- incompatible frame metadata or preprocessing;
- failed global or local registration;
- invalid/interpolated-outside source support;
- clipped or near-clipped source sample;
- defect or repaired-without-provenance sample in the interpolation support;
- non-finite signal, gain, variance, displacement, covariance, or coefficient;
- nonpositive gain or variance;
- forward/backward hard failure;
- motion-field disagreement above the Section 7.11 limit;
- unsupported required RAW operation;
- reference site is clipped under the V1 no-highlight-recovery policy.

Hard validity is binary. Confidence and photometric disagreement are handled smoothly after this mask.

## 8.2 Interpolated alignment confidence

At output coordinate `p`, use the four surrounding motion-grid nodes. Before the ambiguity hard test, define

$$
c_{\mathrm{grid}}(\mathbf p)
=
\sum_t b_t(\mathbf p)c_t,
$$

where the bilinear coefficients sum to one.

Let

$$
\sigma_{\mathrm{between}}
=
\sqrt{
\lambda_{\max}
(\boldsymbol\Sigma_{\mathrm{between}})
}.
$$

For nonambiguous motion, define

$$
c_{\mathrm{disagree}}
=
\exp
\left[
-\frac{1}{2}
\left(
\frac{\sigma_{\mathrm{between}}}{0.35}
\right)^2
\right],
$$

and

$$
c_{\mathrm{align}}
=
c_{\mathrm{grid}}c_{\mathrm{disagree}}.
$$

All distances are raw pixels. The `0.35` confidence scale and `0.75` hard ambiguity limit must be calibrated together.

## 8.3 Cell-grid patch reliability

A Bayer cell contains the four sites in one two-by-two repeat. Build one reliability value per alternate per cell.

For each cell center `q`, collect valid standardized residuals over a `5 x 5` cell patch and all four CFA sites:

$$
r_{i,c}(\mathbf q')
=
\frac{
y_{i,c}(\mathbf q')
-
y_{0,c}(\mathbf q')
}{
\sqrt{
v_{i,c}^{\mathrm{gate}}(\mathbf q')
+
v_{0,c}^{\mathrm{gate}}(\mathbf q')
+
\epsilon_v
}}.
$$

Here `y_i,c` is the same-CFA warped alternate sample in the comparison domain. Registration uncertainty is already included in `v_i,gate`; exposure-scale uncertainty is deliberately excluded from this acceptance statistic.

Define the robust patch scale

$$
z_{\mathrm{patch}}
=
\frac{
\operatorname{median}|r_{i,c}(\mathbf q')|
}{
0.67448975
}.
$$

For zero-mean standard Gaussian residuals, the expected median absolute value is approximately `0.67448975`, so a static, correctly modeled patch gives `z_patch` near one.

Use a flat-top patch gate:

$$
C_{\mathrm{patch}}(z)
=
\begin{cases}
1, & z\le1.5,\\[4pt]
1-S_5\left(\dfrac{z-1.5}{3.5-1.5}\right),
   & 1.5<z<3.5,\\[10pt]
0, & z\ge3.5.
\end{cases}
$$

Require at least 50 valid residuals out of the nominal 100. Otherwise the patch value is zero.

This patch statistic catches coherent subject motion and parallax more reliably than a single noisy-pixel comparison. It also catches black/exposure mismatch that escaped frame-level diagnostics.

## 8.4 Noise-model confidence

Map the resolved model quality to an initial confidence:

| Noise quality | `c_noise` |
|---|---:|
| `TrustedMetadata` | 1.00 |
| `CalibratedCamera` | 0.90 |
| `EstimatedBurst` | 0.65 |
| `GenericLowConfidence` | 0.35 |
| `Unavailable` | 0.00 |

These values intentionally reduce the influence of an uncertain model; they are not probabilities.

## 8.5 Neighborhood protection

The raw cell confidence is

$$
R_i(\mathbf q)
=
m_{\mathrm{hard}}
c_{\mathrm{noise}}
c_{\mathrm{align}}
C_{\mathrm{patch}}.
$$

Apply a `3 x 3` cell minimum filter:

$$
R_i^{\min}(\mathbf q)
=
\min_{\mathbf q'\in\mathcal N_3(\mathbf q)}
R_i(\mathbf q').
$$

This erodes confidence around detected motion boundaries so a small alignment error cannot leave a thin noise halo or double edge. HFSR's public implementation similarly uses a local minimum filter on robustness; V1 starts with radius one cell rather than assuming a universal radius. ([Lafenetre et al., 2023](https://www.ipol.im/pub/art/2023/460/article_lr.pdf))

Remap to a cleaner acceptance weight:

$$
\widehat R_i
=
S_5
\left(
\operatorname{clamp}
\left(
\frac{R_i^{\min}-0.20}{0.80-0.20},
0,1
\right)
\right).
$$

**Needs experiment.** Compare a radius-one and radius-two minimum filter. Radius two is safer on fast motion but can create visibly noisy outlines around moving objects by discarding too much valid support.

## 8.6 Per-pixel redescending gate

Even a reliable patch can contain a thin moving edge, sparkle, hot pixel, or interpolation failure. For one output sample, define the difference

$$
d_i=y_i-y_0
$$

and standardized residual

$$
r_i
=
\frac{
d_i
}{
\sqrt{
v_i^{\mathrm{gate}}
+
v_0^{\mathrm{gate}}
+
\epsilon_v
}}.
$$

Use a flat-top quintic gate:

$$
G(r;k_{\mathrm{in}},k_{\mathrm{out}})
=
\begin{cases}
1, & |r|\le k_{\mathrm{in}},\\[4pt]
1-S_5\left(
\dfrac{|r|-k_{\mathrm{in}}}
{k_{\mathrm{out}}-k_{\mathrm{in}}}
\right),
& k_{\mathrm{in}}<|r|<k_{\mathrm{out}},\\[10pt]
0, & |r|\ge k_{\mathrm{out}}.
\end{cases}
$$

Initial thresholds:

- trusted/calibrated noise: `k_in=2.5`, `k_out=5.0`;
- estimated/generic noise: `k_in=2.0`, `k_out=4.0`.

This gate is intentionally flat near zero so valid Gaussian samples retain almost full statistical efficiency. It fully rejects strong outliers, unlike a Huber weight, but it is not expected to identify moderate motion by itself; patch and flow evidence must do that.

## 8.7 Absolute safety gate for uncertain noise

When noise quality is below `CalibratedCamera`, additionally require

$$
|d_i|
\le
\tau_{\mathrm{abs}}
+
\tau_{\mathrm{rel}}
\max(|y_i|,|y_0|).
$$

The absolute term must be expressed in the same **comparison-domain signal units** as `d_i`. Let `v_0^dark` and `v_i^dark` be the propagated read-plus-quantization variance components for the reference and interpolated alternate. Let `qstep_0=k_0/D_0` and `qstep_i` be the effective comparison-domain one-DN steps. Use

$$
\tau_{\mathrm{abs}}
=
\max
\left(
4q^{\mathrm{step}}_0+4q^{\mathrm{step}}_i,
\;
4\sqrt{
v_0^{\mathrm{dark}}+v_i^{\mathrm{dark}}
}
\right),
\qquad
\tau_{\mathrm{rel}}=0.10.
$$

This is a last-resort guard against a badly overestimated noise model. It should not be enabled for a trusted profile because a generic relative threshold is less physically grounded than the calibrated variance.

## 8.8 Final candidate gate

The final scalar gate is

$$
\gamma_i
=
m_{\mathrm{hard}}
\widehat R_i
G(r_i;k_{\mathrm{in}},k_{\mathrm{out}})
m_{\mathrm{abs}},
$$

where `m_abs` is one for trusted noise and is the binary absolute safety gate otherwise. Thus

$$
0\le \gamma_i\le1.
$$

Do not quantize this gate to 8-bit. Store it as float or sufficient fixed-point precision in cache/diagnostics.

## 8.9 How specific difficult content is handled

| Failure/content | Primary defenses | Expected fallback |
|---|---|---|
| Moving person/object | local flow, forward/backward, patch residual, min filter, pixel gate | reference in moving region |
| Occlusion/disocclusion | reverse closure, invalid support, patch residual | reference |
| Depth parallax | local tile motion; between-node scatter | local fusion where coherent, reference at boundaries |
| Fine foliage | local motion + 5x5 patch + pixel gate | partial fusion or reference |
| Water/sparks/flames | temporal photometric rejection | reference-biased temporal moment |
| Periodic texture | second-best uniqueness + forward/backward | reference if ambiguous |
| Flat wall/sky | `FlatSafe` prediction + low gradient registration variance | strong denoise if photometrically static |
| Clipped highlight | hard saturation mask | clipped reference preserved |
| Bright moving point | saturation/patch/pixel gate | reference, no streak averaging |
| Bad frame | global/radiometric/frame coverage tests | entire frame skipped |
| Residual black offset | exposure-fit intercept diagnostic + patch residual | frame downweighted/skipped |
| Flicker/global exposure drift | scalar fit | accepted if scalar model suffices |
| Local illumination change | patch/pixel residual | reference |
| Edge crossing due to flow blend | node-scatter covariance + ambiguity reject + min filter | reference band around boundary |
| Insufficient overlap | support mask | reference border |
| Row banding | optical-black correction/calibration; not solved by independent averaging | residual may remain |

Foliage and water are irreducibly difficult because genuine scene state changes at fine scale can resemble noise. The V1 bias is to leave reference noise rather than average incompatible temporal states.

## 8.10 Frame-level usability

After constructing the reliability map, compute the fraction of reference cells with `Rhat_i > 0.2`. Skip the frame entirely when fewer than

$$
N_{\mathrm{usable,min}}
=
\min
\left(
0.25N_{\mathrm{cells}},
\;
\max(256,\;0.01N_{\mathrm{cells}})
\right)
$$

cells are usable. This supports small test images while requiring approximately 1% coverage on ordinary photos. It is a performance and diagnostics rule; per-pixel rejection would remain safe without it.

---

# 9. The fusion estimator

## 9.1 Statistical base: inverse-variance fusion

For independent unbiased measurements `y_i` of one latent value `x`, a linear estimate is

$$
\widehat x=\sum_i\alpha_i y_i,
\qquad
\sum_i\alpha_i=1.
$$

Its variance is

$$
\operatorname{Var}(\widehat x)
=
\sum_i\alpha_i^2v_i.
$$

Minimizing this subject to weights summing to one gives

$$
\alpha_i
=
\frac{v_i^{-1}}{\sum_j v_j^{-1}}.
$$

This is the standard minimum-variance result and is consistent with the variance-aware fusion concepts described in Apple's public disclosure. ([US11189017B1](https://patents.google.com/patent/US11189017B1/en))

V1 modifies the candidate weights by robust reliability while leaving the reference mandatory.

## 9.2 Per-pixel V1 estimator

At reference RAW site `p` with CFA label `c`, define:

- `y_0 = z_0(p)` — reference comparison-domain value;
- `v_0 = v_0,fuse(p)` — reference fusion variance;
- `y_i` — same-CFA bicubic sample from alternate `i`;
- `v_i = v_i,fuse` — propagated alternate fusion variance;
- `gamma_i` — final candidate gate from Section 8; it is distinct from calibration gain `g_i(p,c)`.

Let the comparison-domain one-DN steps be

$$
q^{\mathrm{step}}_0
=
\frac{k_0(\mathbf p,c)}{D_{0,c}},
$$

and, for an interpolated alternate, let

$$
\left(q^{\mathrm{step}}_i\right)^2
=
\sum_j h_j^2
\left(
\frac{k_{i,j}}{D_{i,c}}
\right)^2.
$$

Define quantization-aware division floors

$$
v_{\min,0}
=
\max
\left(
10^{-12},
\frac{(q^{\mathrm{step}}_0)^2}{12}
\right),
\qquad
v_{\min,i}
=
\max
\left(
10^{-12},
\frac{(q^{\mathrm{step}}_i)^2}{12}
\right).
$$

Raw weights are

$$
w_0
=
\frac{1}{\max(v_0,v_{\min,0})},
$$

$$
w_i
=
\frac{\gamma_i}{\max(v_i,v_{\min,i})}.
$$

These are lower bounds for a division, not extra variance terms. Quantization itself remains included exactly once in the propagated noise model.

Cap one alternate's influence:

$$
w_i
\leftarrow
\min(w_i,4w_0).
$$

The comparison-domain result is

$$
\widehat z
=
\frac{
w_0y_0+\sum_{i=1}^{N-1}w_i y_i
}{
w_0+\sum_{i=1}^{N-1}w_i
}.
$$

The output mosaic value is

$$
\widehat q_0
=
\frac{\widehat z}{g_0(\mathbf p,c)}.
$$

No spatial smoothing is applied after this estimate.

## 9.3 Exact local fallback

Let

$$
r_w
=
\frac{\sum_{i=1}^{N-1}w_i}{w_0}.
$$

When

$$
r_w<0.05,
$$

copy the canonical reference sample exactly:

$$
\widehat q_0=q_0.
$$

Do not calculate `g_0 q_0/g_0` in this branch. The `0.05` threshold avoids tiny, visually meaningless numerical changes and makes diagnostics unambiguous.

Other exact-reference cases include:

- clipped reference;
- invalid reference gain;
- unavailable noise model;
- no valid candidate;
- cancellation/failure before atomic output commit.

## 9.4 Low-confidence-noise cap

When noise quality is `EstimatedBurst` or `GenericLowConfidence`, additionally cap total alternate weight:

$$
\sum_iw_i\le8w_0.
$$

If the uncapped sum exceeds the limit, scale all alternate weights by the same factor. This preserves their relative weights while preventing an uncertain model from overwhelming the reference.

No total cap is applied for trusted/calibrated profiles; a 32-frame static burst should be able to realize substantially more than an eight-frame gain.

## 9.5 Why candidates are compared to the reference

The reference defines the desired captured moment. Candidate gating is reference-relative so a majority of alternate frames cannot easily replace a moving reference subject with a different temporal state.

This has a cost: a noisy reference can imprint some selection behavior. The broad flat-top pixel gate and patch-level decision reduce that effect, and Section 15 quantifies one related weighting bias. A future robust consensus estimator may improve low-SNR efficiency, but V1 prioritizes temporal fidelity and predictable fallback.

## 9.6 Approximate output variance and effective sample count

With normalized coefficients

$$
\alpha_i
=
\frac{w_i}{\sum_jw_j},
$$

an approximate conditional variance is

$$
v_{\mathrm{out}}
\approx
\sum_i\alpha_i^2v_i.
$$

Because gates and weights depend on observed data and because interpolated samples can be correlated, this is a diagnostic, not a guaranteed posterior variance.

A useful effective sample count is

$$
N_{\mathrm{eff}}
=
\frac{
\left(\sum_iw_i\right)^2
}{
\sum_iw_i^2
}.
$$

For equal accepted variances and gates, `N_eff` equals the number of contributing frames. Store maps of:

- `N_eff`;
- alternate-to-reference weight ratio;
- selected frame count;
- output variance estimate;
- dominant rejection reason;
- warp uncertainty;
- reliability.

These maps are essential for debugging a visually plausible but incorrect output.

## 9.7 Reference-defect exception

If the reference sample is a known defect:

1. remove the reference measurement from the numeric estimate;
2. require at least three alternates with `gamma_i>=0.8`;
3. require pairwise standardized agreement among those alternates;
4. use inverse-variance fusion only if the consensus passes;
5. otherwise mark for the normal single-frame defect-repair stage.

For candidate set `A`, require

$$
\max_{i,j\in A}
\frac{
|y_i-y_j|
}{
\sqrt{v_i+v_j+\epsilon_v}
}
<3.5.
$$

If more than three candidates are available, form a weighted median pilot, discard candidates farther than 3.5 sigma from it, and require at least three survivors before inverse-variance fusion. These defect thresholds are **Needs experiment** values.

This is the only ordinary path that permits an output site with no reference value.

## 9.8 Fusion pseudocode

```text
fuse_sample(p, c, ref, acceptedFrames):
    if ref.saturated(p):
        return exact(ref.q(p)), diagnostics("ReferenceClipped")

    if ref.defect(p):
        return fuse_reference_defect_or_defer(p, c)

    y0 = ref.gain(p,c) * ref.q(p)
    nu = max(y0, 0)
    v0Gate = ref_noise_after_gain(p,c,nu)
           + calibratedModelResidual(ref, c, nu)
           + numericalVarianceFloor
    v0 = v0Gate

    if not finite_positive(v0) or not valid_gain(ref.gain(p,c)):
        return exact(ref.q(p)), diagnostics("InvalidReferenceModel")

    refDnStep = ref.gain(p,c) / ref.usableCodeSpan[c]
    vMin0 = max(1e-12, refDnStep*refDnStep/12)
    w0 = 1 / max(v0, vMin0)

    altNumerator = 0
    altDenominator = 0
    altWeightSquares = 0
    acceptedCount = 0

    for frame in stableFrameOrder:
        sample = sample_same_cfa_with_variance(frame, p, c, nu)
        if not sample.valid:
            continue

        R = sample_reliability(frame, p)
        r = (sample.y - y0) / sqrt(
            sample.gateVariance + v0Gate + epsV
        )
        G = flat_top_gate(r, thresholds_for_noise_quality(frame))
        A = absolute_safety_gate_if_required(sample, ref, p, c)
        gate = R * G * A

        vMinI = max(
            1e-12,
            sample.effectiveDnStep * sample.effectiveDnStep / 12
        )
        wi = gate / max(sample.variance, vMinI)
        wi = min(wi, 4*w0)
        if wi <= 0:
            continue

        altNumerator += wi * sample.y
        altDenominator += wi
        altWeightSquares += wi*wi
        acceptedCount += 1

    altScale = 1
    if low_noise_model_quality and altDenominator > 8*w0:
        altScale = (8*w0) / altDenominator

    altNumerator *= altScale
    altDenominator *= altScale
    altWeightSquares *= altScale*altScale

    if altDenominator / w0 < 0.05:
        return exact(ref.q(p)), diagnostics("ReferenceFallback")

    numerator = w0*y0 + altNumerator
    denominator = w0 + altDenominator
    weightSquares = w0*w0 + altWeightSquares

    zhat = numerator / denominator
    qhat = zhat / ref.gain(p,c)

    if not finite(qhat):
        return exact(ref.q(p)), diagnostics("NumericalFallback")

    neff = denominator*denominator / weightSquares
    return qhat, diagnostics(
        neff, acceptedCount, altDenominator/w0
    )
```

The low-confidence cap is applied to alternate accumulators before adding the reference, and the squared-weight diagnostic is scaled by the square of the same factor.

## 9.9 Same-CFA sample and variance pseudocode

```text
sample_same_cfa_with_variance(frame, pRaw, c, nu):
    sourceRaw, warpCov, alignValid = frame.warp.evaluate(pRaw)
    if not alignValid:
        return invalid

    xiPlane = (sourceRaw - cfaOffset[c]) / 2
    taps, coeffs, derivCoeffs = keys_bicubic_4x4(xiPlane)

    if any tap outside active plane:
        return invalid
    if any tap saturated, defective, or provenance-invalid:
        return invalid

    y = 0
    vInterp = 0
    vDark = 0
    dnStepSquared = 0
    gradPlane = (0,0)

    for tap, h, dh in taps:
        q = frame.plane[c](tap)
        gain = frame.exposureScale * frame.gainPlane[c](tap)
        z = gain * q

        vReadQuant = gain*gain * frame.O[c]
        vTap = gain * frame.S[c] * nu
             + vReadQuant

        oneDn = gain / frame.usableCodeSpan[c]

        y += h * z
        vInterp += h*h * vTap
        vDark += h*h * vReadQuant
        dnStepSquared += h*h * oneDn*oneDn
        gradPlane += dh * z

    gradRaw = gradPlane / 2
    vReg = transpose(gradRaw) * warpCov * gradRaw
    vScale = square(frame.exposureSigma / frame.exposureScale) * y*y
    vModel = calibratedModelResidual(frame, c, nu)

    vGate = vInterp + vReg + vModel + numericalVarianceFloor
    v = vGate + vScale
    effectiveDnStep = sqrt(dnStepSquared)

    if not finite(y) or not finite_positive(vGate) or not finite_positive(v):
        return invalid

    return {
        valid: true,
        y: y,
        v: v,
        gateVariance: vGate,
        darkVariance: vDark,
        effectiveDnStep: effectiveDnStep,
        gradient: gradRaw
    }
```

In a production implementation, gain varies within the footprint and should be evaluated per tap as above. A full corrected frame need not be materialized for fusion.

---

# 10. Output semantics

The output is:

- the same active dimensions as the reference;
- the same Bayer offsets and CFA labels;
- scene-linear;
- black-subtracted and white-normalized;
- in the reference exposure domain;
- in the selected reference pre-gain mosaic contract;
- not white balanced;
- not demosaiced;
- not tone mapped;
- not sharpened;
- accompanied by fusion provenance and diagnostics.

Values may remain slightly negative. Values above one may arise from noise or cubic overshoot, but a reference-clipped site remains identified as clipped. Downstream nodes must use the mask/metadata rather than treating every `q>1` as newly recovered highlight information.

If exported to DNG, do not simply copy the source noise metadata as though nothing changed. The fused residual noise is:

- lower in accepted static regions;
- reference-like in fallback regions;
- spatially varying;
- correlated by interpolation;
- conditional on data-dependent gates.

At minimum, write provenance, an appropriate `NoiseReductionApplied` indication where supported, and application-private processing parameters. Review consumed opcode and gain-map semantics carefully before external DNG export.

---


# 11. Initial parameter set

These are implementation starting points, not universal truths. Put every value in one versioned parameter structure and include it in cache keys.

## 11.1 RAW/radiometric parameters

| Parameter | Initial value | Defensible test range |
|---|---:|---:|
| Preserve negative normalized RAW | Yes | fixed |
| Saturation DN margin | 4 DN | 2–16 DN |
| Saturation noise margin | 2 sigma at white | 1–4 sigma |
| Exposure IRLS iterations | 3 | 2–5 |
| Exposure Huber delta | 2.5 sigma | 2–4 |
| Exposure fit signal range | `[0.01, 0.85]` | corpus dependent |
| Exposure fit minimum samples | `max(10,000, 0.1% pixels)` | 5,000–100,000 |
| Hard fitted scale range | `[0.5, 2.0]` | fixed for V1 scope |
| Metadata deviation warning | `±0.5 EV` | 0.25–1 EV |
| CFA diagnostic scale disagreement | 2% | 1–5% |
| Maximum accepted comparison gain | 16 | 4–32 |

## 11.2 Registration parameters

| Parameter | Initial value | Test range |
|---|---:|---:|
| CFA pyramid levels | 4 | 3–5 |
| Pyramid kernel | `[1,4,6,4,1]/16` | fixed initially |
| Global model | affine | translation/affine A/B |
| Affine iterations per level | 5–10 | 3–15 |
| Affine rotation guard | `±5 deg` | 2–10 deg |
| Affine singular-value guard | `[0.95,1.05]` | 0.90–1.10 |
| Minimum global overlap | 50% | 30–80% |
| Finest patch | `32 x 32` plane px | 16–48 |
| Finest stride | 16 plane px | 8–24 |
| Coarse patch | `16 x 16` level px | 12–32 |
| Search radii fine-to-coarse | `{1,2,2,4}` level px | `{1–3,1–4,2–6,3–8}` |
| Registration Huber delta | 2.5 sigma | 2–4 |
| Subpixel iterations | 3 | 2–5 |
| Max subpixel step/iteration | 0.75 plane px | 0.25–1 |
| Final distance from discrete seed | 1 plane px | 0.5–1.5 |
| Precision confidence scale | 0.35 raw px | 0.2–0.6 |
| Warp model covariance floor | `0.05^2 I` raw px² | 0.02²–0.15² |
| Uniqueness transition | 0.02–0.15 | corpus dependent |
| FB Mahalanobis hard limit | 9.21 | chi-square-calibrated |
| FB Euclidean hard limit | 2 raw px | 1–3 |
| Motion disagreement hard limit | 0.75 raw px | 0.4–1.2 |
| FlatSafe covariance | `1.0^2 I` raw px² | 0.5²–2² |

## 11.3 Reliability/fusion parameters

| Parameter | Initial value | Test range |
|---|---:|---:|
| Patch reliability support | `5 x 5` Bayer cells | 3–7 |
| Patch valid residual minimum | 50/100 | 40–80 |
| Patch full/fade thresholds | 1.5 / 3.5 sigma | 1.2–2 / 2.5–5 |
| Reliability min-filter radius | 1 cell | 1–2 |
| Reliability remap | 0.20–0.80 | 0.1–0.9 |
| Pixel full/fade, trusted noise | 2.5 / 5 sigma | 2–3.5 / 4–7 |
| Pixel full/fade, low confidence | 2 / 4 sigma | 1.5–2.5 / 3–5 |
| One-alternate weight cap | `4*w0` | 2–8 |
| Low-confidence total alt cap | `8*w0` | 4–16 |
| Exact fallback alt/ref ratio | 0.05 | 0.01–0.10 |
| Reliability storage | 16-bit UNORM or float | do not use 8-bit initially |
| Fusion accumulator | float64 | float32 compensated A/B |
| Output tile | `512 x 512` raw px | 256–1024 |

## 11.4 Parameter governance

- No hidden constants in shader/SIMD paths.
- Serialize the complete parameter object with the result.
- Give each semantic parameter-set change an algorithm version.
- A UI "strength" control, if later added, must map to a small documented subset; it must not alter geometry, CFA handling, or safety guards invisibly.
- Keep a conservative factory preset and an internal research preset. The production result must record which one was used.

---

# 12. C++ architecture, memory, performance, and caching

## 12.1 Recommended components

```text
CfaLayout
    offsets, plane dimensions, raw<->plane coordinate conversion

RawFrameDescriptor
    camera/mode identity, active area, bit depth, levels, opcode summary

RawCalibration
    linearization, black map, white span, safe gain-map evaluation, masks

NoiseModel
    per-CFA S/O, quality, gain propagation, variance floors

NormalizedMosaicTileProvider
    deterministic float mosaic tiles, masks, source metadata

CfaPlanePyramid
    four intensity pyramids, optional variance/validity tiles

GlobalRegistration
    phase-correlation seed, affine refinement, quality diagnostics

ExposureMatcher
    robust scalar, uncertainty, plane/intercept diagnostics

LocalMotionGrid
    displacement, covariance, confidence, node state, reverse consistency

ReliabilityStore
    tiled 16-bit/float cell confidence and hard-invalid reasons

SameCfaSampler
    Keys bicubic value, derivative, variance, boundary validation

RobustFusionTile
    fixed-order frame loop, double accumulation, fallback

MosaicDenoiseDiagnostics
    maps, frame skips, metrics, cache/provenance summary
```

Use strong coordinate types or tagged wrappers such as `RawPx2`, `PlanePx2`, and `PyramidPx2`. A bare `Vec2f` passed between these APIs invites factor-of-two and pyramid-scale bugs.

## 12.2 Core data contracts

A motion-grid node should contain at least:

```cpp
struct MotionNode {
    Vec2f residualRawPx;
    Symmetric2f covarianceRawPx2; // total source-coordinate covariance
    float confidence;          // [0,1]
    float robustCost;
    float uniqueness;
    float validFraction;
    MotionNodeState state;     // Structured, FlatSafe, Rejected
};
```

A sampled measurement should contain:

```cpp
struct FusionSample {
    float value;               // comparison-domain signal
    float variance;            // fusion variance, comparison-domain signal^2
    float gateVariance;        // excludes global exposure-scale uncertainty
    float darkVariance;        // read + quantization component
    float effectiveDnStep;     // comparison-domain one-DN RMS step
    Vec2f gradientPerRawPx;
    float reliability;         // [0,1]
    SampleRejectReason rejectReason;
    bool valid;
};
```

Use an explicitly symmetric covariance type. After every arithmetic combination:

1. symmetrize;
2. eigenvalue-check;
3. clamp tiny negative eigenvalues caused by roundoff;
4. reject materially non-positive-semidefinite results.

## 12.3 Asymptotic complexity

Let:

- `N` be the total frame count including reference;
- `M=W*H` be the number of raw mosaic samples;
- `T` be the number of motion-grid tiles;
- `C_l` be discrete candidates per tile at level `l`;
- `A_l` be comparison samples per tile at level `l`;
- `K=16` be bicubic taps.

Then:

- normalization/calibration: `O(NM)`;
- four-plane pyramids: `O(NM)`;
- a four-level pyramid contains

$$
M\left(1+\frac14+\frac1{16}+\frac1{64}\right)
=
1.328125M
$$

samples across all four planes;
- coarse phase correlation: approximately `O(N M_c log M_c)` on a reduced level;
- local block matching:

$$
O\left(
N\sum_l T_l C_l A_l
\right);
$$

- subpixel refinement: linear in valid tile pixels times a small fixed iteration count;
- final fusion: `O(NMK)` worst case, with early rejection avoiding many full samples.

With fixed patch, stride, pyramid depth, and search radii, practical work scales approximately linearly with frame count and image area.

## 12.4 Memory strategy

Never retain decoded full-resolution data and complete pyramids for all frames simultaneously.

Recommended residency:

1. reference normalized tile store;
2. reference pyramid;
3. one alternate pyramid during registration;
4. compact global/local motion metadata for all accepted alternates;
5. reliability stored in compressed/tiled form;
6. one output tile and the required source ROIs during fusion.

One float32 four-plane pyramid requires approximately

$$
1.328125M\times4\ \text{bytes}.
$$

For a 45-megapixel frame, that is about 239 MB for intensity alone. A full float32 variance pyramid doubles it. Reference plus one alternate intensity and variance pyramids are therefore near 1 GB before masks and working buffers. A memory-budget manager should spill pyramid levels or variance tiles to a local cache when necessary.

Do not reduce normalized RAW or motion math to float16 without a measured error study. Reliability can initially be stored as 16-bit UNORM because it is bounded, but the conversion must be deterministic and included in validation.

## 12.5 Tiled fusion

Use raw output tiles, initially `512 x 512`. For each tile:

1. fetch the reference mosaic/gain/noise/masks;
2. examine each alternate's global and local warp over the tile;
3. compute a conservative source bounding box;
4. add sampler support and motion-bound halo;
5. fetch only the required normalized source/gain/mask tiles;
6. evaluate reliability and fuse in fixed frame order;
7. write a temporary output tile and diagnostics.

A Keys footprint extends two plane pixels, or four raw pixels, around the source coordinate. The ROI bound must also include local-motion extrema. Sample the tile's motion-grid nodes and edges; if the bound is uncertain or highly nonlinear, split the output tile rather than requesting an unbounded source region.

A normalized tiled disk cache is important when the source RAW compression does not support efficient random access. Decode once into a lossless application cache rather than repeatedly reopening/decoding the original during tile fusion.

## 12.6 Reliability storage

A full per-cell float map costs about one float per four raw samples per alternate. At high resolution and large bursts this is substantial.

V1 options, in preferred order:

1. tiled 16-bit UNORM reliability plus compact rejection bits;
2. losslessly compressed float tiles when 16-bit calibration fails;
3. deterministic on-demand reconstruction from motion/noise data when disk is constrained.

Do not use 8-bit reliability initially; the two smooth gates and inverse-variance multiplier can make coarse quantization visible in low-noise gradients.

## 12.7 Cache boundaries and keys

### Cache A — decode/normalization

Depends on:

- exact source-content hash;
- decoder and camera-profile version;
- active-area/CFA/linearization/black/white semantics;
- safe opcode classification;
- defect/saturation policy.

Contains normalized mosaic tiles, masks, and source metadata.

### Cache B — calibration/noise

Depends on Cache A plus:

- gain-map/profile version;
- noise profile source and calibration version;
- optical-black correction parameters.

### Cache C — CFA pyramids

Depends on Cache B plus:

- pyramid kernel;
- number of levels;
- proxy defect-fill policy.

### Cache D — pairwise global/radiometric registration

Depends on:

- reference Cache C key;
- alternate Cache C key;
- global registration parameters;
- exposure-fit parameters.

### Cache E — local motion and reliability

Depends on Cache D plus:

- local tile/search/refinement parameters;
- affine/local covariance calibration and model floor;
- forward/backward settings;
- reliability/gate parameters;
- noise-quality policy.

### Cache F — fused result

Depends on:

- ordered accepted frame IDs;
- reference ID;
- all Cache E keys;
- fusion weights/caps/fallback parameters;
- output contract/version.

Reference selection invalidates pairwise registration, reliability, and fusion, but not frame normalization. Changing only a fusion gate should not force RAW decode or pyramid rebuild.

## 12.8 Deterministic behavior

Determinism requirements:

- stable project frame order;
- stable tile order for externally visible results;
- each output pixel accumulates frames in the same order regardless of thread scheduling;
- deterministic candidate enumeration and tie breaking;
- fixed scan order within block costs;
- no unordered atomic floating-point reductions;
- deterministic stratified exposure-fit sample selection;
- explicit NaN ordering/rejection;
- versioned math kernels and cache serialization.

Recommended precision:

- float32 for images, gains, motion maps, and ordinary per-sample math;
- float64 for exposure fits, affine normal equations, covariance eigensystems, and output numerator/denominator;
- float64 or compensated summation for metrics and frame-level reductions.

A strict reproducibility build should disable unsafe fast-math and use a consistent FMA policy. Bitwise identity can reasonably be promised for the same binary, CPU path, and thread-independent tile scheduling. Across different CPU instruction sets and compilers, promise a tight numerical tolerance unless the application deliberately forces one scalar math path.

## 12.9 Parallelism

Safe parallel axes:

- frame-pair registration;
- motion tiles within a level after parent dependencies are ready;
- output tiles;
- rows/blocks within a tile when reductions remain local and ordered.

Avoid oversubscribing frame, tile, FFT, and decoder thread pools simultaneously. A central work scheduler should own the thread budget.

Within one output tile, process frames serially in stable order while vectorizing pixels. This gives deterministic reduction and good cache locality.

## 12.10 Cancellation and atomicity

Check cancellation:

- before/after each frame decode;
- each pyramid level;
- every motion-grid row or bounded candidate batch;
- each reliability tile;
- each output tile;
- periodically inside long source ROI loops.

Write stages to temporary cache objects. Publish a cache entry only after its checksum and metadata are complete. Write the final image to a temporary result and atomically swap it into the graph output. Cancellation must leave the previous valid output intact and never expose a half-fused mosaic.

## 12.11 Failure policy

| Failure | Node result |
|---|---|
| Reference unreadable/incompatible | hard node error |
| Alternate unreadable/incompatible | skip alternate, report reason |
| One registration failure | skip alternate |
| All alternates rejected | successful exact-reference output |
| Missing noise model with no safe estimate | successful exact-reference output |
| Numerical failure at isolated pixel | exact reference at pixel |
| Numerical failure across one output tile | recompute scalar path; if still failing, reference tile + error diagnostic |
| Cancellation | no new output committed |
| Cache corruption | discard affected cache and recompute |
| Out-of-memory | reduce concurrency/spill; if still impossible, fail without modifying prior output |

"Successful reference output" must be visible in diagnostics so users do not mistake a no-op for denoising.

---

# 13. Numerical safeguards

## 13.1 Required finite/range checks

At every public stage boundary, validate:

- dimensions and strides;
- plane offsets and parity;
- positive white span `D`;
- finite black, gain, exposure, signal, variance, displacement, and covariance;
- `S>=0`, `O>=0`;
- `0<=confidence<=1`;
- positive determinant/eigenvalues after regularization;
- source footprints and integer index arithmetic;
- accumulator denominator at least `w0`;
- no overflow converting tile/plane coordinates.

## 13.2 Matrix solves

- Use pivoted LDLT or eigen-regularized solve for 2-by-2 and 6-by-6 symmetric systems.
- Scale coordinates around the image center before affine fitting.
- Add both relative diagonal damping and an absolute floor.
- Reject condition numbers above a calibrated limit rather than returning a huge displacement.
- Symmetrize covariance as `(Sigma+Sigma^T)/2`.
- Clamp only tiny negative eigenvalues attributable to roundoff; reject a materially indefinite matrix.

## 13.3 Variance arithmetic

- Never evaluate `sqrt` on an unchecked value.
- Use `max(v,v_min)` before reciprocal/square root.
- Keep the measured signal negative when it is negative; only the expected shot-noise pilot is clamped to nonnegative.
- Do not let an uncertain model grow arbitrarily to explain residuals.
- Ensure interpolated variance uses squared coefficients.
- Add the registration covariance term in raw-pixel units.
- Treat `NaN` as invalid, never as a zero residual.

## 13.4 Robust functions

Implement `S5` with an input clamped to `[0,1]`. Unit-test continuity and zero first/second derivatives at endpoints. The flat-top gates must be exactly one inside the inner threshold and exactly zero outside the outer threshold.

## 13.5 Tie breaking

When two displacement candidates have equal cost within a fixed tolerance, prefer in order:

1. smaller distance from the parent/global prediction;
2. smaller displacement magnitude;
3. lexicographically smaller `(dy,dx)`.

This prevents thread/compiler iteration order from changing motion fields.

## 13.6 Reference fallback is a separate branch

Do not rely on a tiny alternate weight to approximate fallback. The exact branch:

- copies the canonical reference sample;
- copies/reference-updates masks according to the declared output contract;
- records a fallback reason;
- skips division by the reference gain.

This avoids gain round-trip error and makes deterministic comparison straightforward.

---


# 14. Python derivation and numerical sanity checks

These experiments verify algebra and expose estimator behavior. They do not replace evaluation on real RAW bursts.

All experiments use a fixed random seed and the exact normalized simulation

$$
Y
=
S\operatorname{Poisson}\left(\frac{\mu}{S}\right)
+
\mathcal N(0,O),
$$

which has

$$
\mathbb E[Y]=\mu,
\qquad
\operatorname{Var}(Y)=S\mu+O.
$$

## 14.1 Gain-propagation check

The derived prediction is

$$
\operatorname{Var}(kY)
=
kS\nu+k^2O,
\qquad
\nu=k\mu.
$$

Reproducible code:

```python
import math
import numpy as np
import pandas as pd

rng = np.random.default_rng(20260802)
S = 2e-4
O = 4.5e-7
mus = [0.001, 0.01, 0.1, 0.5]
ks = [0.6, 1.0, 1.8, 3.0]
samples = 500_000

rows = []
for mu in mus:
    y = (
        S * rng.poisson(mu / S, size=samples)
        + rng.normal(0.0, math.sqrt(O), size=samples)
    )
    for k in ks:
        z = k * y
        nu = k * mu
        empirical = z.var(ddof=1)
        predicted = k * S * nu + k * k * O
        rows.append(
            [mu, k, nu, empirical, predicted,
             100.0 * (empirical - predicted) / predicted]
        )

df = pd.DataFrame(
    rows,
    columns=[
        "mu", "k", "nu", "empirical_var",
        "predicted_var", "relative_error_percent"
    ],
)
print(df)
print("max absolute relative error:",
      df.relative_error_percent.abs().max())
```

Selected results:

| `mu` | `k` | `nu` | empirical variance | predicted variance | relative error |
|---:|---:|---:|---:|---:|---:|
| 0.001 | 0.6 | 0.0006 | 2.343383e-7 | 2.340000e-7 | 0.145% |
| 0.001 | 3.0 | 0.0030 | 5.858458e-6 | 5.850000e-6 | 0.145% |
| 0.010 | 1.8 | 0.0180 | 7.951190e-6 | 7.938000e-6 | 0.166% |
| 0.100 | 1.0 | 0.1000 | 2.054211e-5 | 2.045000e-5 | 0.450% |
| 0.500 | 3.0 | 1.5000 | 9.069551e-4 | 9.040500e-4 | 0.321% |

The maximum absolute Monte Carlo discrepancy was about 0.451%. This verifies the gain-propagation equation used by `NoiseModel`.

## 14.2 Robust-gate behavior

The per-pixel V1 gate is compared with Tukey's biweight and Huber weighting under a standardized residual distributed as `N(delta,1)`.

```python
import numpy as np

rng = np.random.default_rng(20260802)
base = rng.standard_normal(2_000_000)

def smootherstep5(t):
    t = np.clip(t, 0.0, 1.0)
    return 6*t**5 - 15*t**4 + 10*t**3

def flat_top(r, inner=2.5, outer=5.0):
    a = np.abs(r)
    t = (a - inner) / (outer - inner)
    middle = 1.0 - smootherstep5(t)
    return np.where(a <= inner, 1.0,
                    np.where(a >= outer, 0.0, middle))

def tukey(r, c=4.685):
    a = np.abs(r)
    u = r / c
    return np.where(a < c, (1.0 - u*u)**2, 0.0)

def huber(r, c=1.345):
    a = np.abs(r)
    return np.where(a <= c, 1.0, c / np.maximum(a, 1e-30))

for delta in [0, 1, 2, 3, 4, 5, 8]:
    r = base + delta
    print(
        delta,
        flat_top(r).mean(),
        tukey(r).mean(),
        huber(r).mean(),
    )
```

Mean weights:

| mean shift `delta` | flat-top 2.5–5 | Tukey 4.685 | Huber 1.345 |
|---:|---:|---:|---:|
| 0 sigma | 0.999363 | 0.915108 | 0.959885 |
| 1 sigma | 0.993719 | 0.838665 | 0.890741 |
| 2 sigma | 0.943240 | 0.633770 | 0.704072 |
| 3 sigma | 0.750904 | 0.372363 | 0.500251 |
| 4 sigma | 0.411209 | 0.153218 | 0.362051 |
| 5 sigma | 0.129791 | 0.038525 | 0.281468 |
| 8 sigma | 0.000055 | 0.000010 | 0.170910 |

Interpretation:

- the flat-top gate preserves nearly all correctly modeled static samples;
- Tukey is more aggressive but reduces even valid Gaussian contributions;
- Huber is excellent for optimization but never fully rejects large residuals;
- a 3-sigma moving difference still retains substantial flat-top weight, confirming that the pixel gate cannot replace patch/flow rejection.

That is why V1 uses Huber for registration optimization and a redescending flat-top only at the final acceptance stage.

## 14.3 Bias from observation-dependent variance weights

A tempting implementation is to evaluate each frame's shot variance from its own noisy sample:

$$
w_i
=
\frac{1}{S\max(y_i,0)+O}.
$$

This gives darker noise excursions larger weights and creates a downward bias.

```python
import math
import numpy as np

rng = np.random.default_rng(20260802)
S = 2e-4
O = 4.5e-7
n_frames = 8
repetitions = 1_000_000
mus = [0.001, 0.003, 0.01, 0.03, 0.1, 0.3]

for mu in mus:
    y = (
        S * rng.poisson(
            mu / S, size=(repetitions, n_frames)
        )
        + rng.normal(
            0.0, math.sqrt(O),
            size=(repetitions, n_frames)
        )
    )

    ordinary = y.mean(axis=1)

    w = 1.0 / (S * np.maximum(y, 0.0) + O)
    observation_iv = (w * y).sum(axis=1) / w.sum(axis=1)

    print(
        mu,
        ordinary.mean() - mu,
        observation_iv.mean() - mu,
        100.0 * (observation_iv.mean() - mu) / mu,
        np.sqrt(np.mean((ordinary - mu)**2)),
        np.sqrt(np.mean((observation_iv - mu)**2)),
    )
```

Results:

| `mu` | ordinary-mean bias | observation-IV bias | observation-IV relative bias | ordinary RMSE | observation-IV RMSE |
|---:|---:|---:|---:|---:|---:|
| 0.001 | 3.63e-7 | -1.5239e-4 | -15.239% | 2.84e-4 | 3.23e-4 |
| 0.003 | 2.66e-7 | -1.8275e-4 | -6.092% | 3.62e-4 | 4.24e-4 |
| 0.010 | -9.67e-7 | -1.7859e-4 | -1.786% | 5.54e-4 | 5.91e-4 |
| 0.030 | 3.35e-7 | -1.7548e-4 | -0.585% | 8.97e-4 | 9.20e-4 |
| 0.100 | 3.47e-7 | -1.7496e-4 | -0.175% | 1.598e-3 | 1.611e-3 |
| 0.300 | -1.82e-6 | -1.7693e-4 | -0.059% | 2.749e-3 | 2.756e-3 |

For equal-noise frames, a common latent pilot gives equal relative variance and reduces to the unbiased mean. With different exposure/gain/noise profiles, the common pilot still permits physically meaningful unequal weights without rewarding each frame's negative noise excursion.

**V1 consequence:** evaluate every frame's shot term from the same reference-domain signal pilot.

## 14.4 Registration-uncertainty delta method

The term

$$
v_{\mathrm{reg}}
=
\nabla I^\mathsf T\Sigma_W\nabla I
$$

is a first-order approximation. The following one-dimensional test measures its accuracy for coordinate jitter on a sinusoidal signal.

```python
import numpy as np

rng = np.random.default_rng(20260802)
samples = 2_000_000
frequency = 0.15       # cycles per raw pixel
x0 = 0.37              # raw pixels
amplitude = 0.4

gradient = (
    amplitude * 2*np.pi*frequency
    * np.cos(2*np.pi*frequency*x0)
)
I0 = 0.5 + amplitude*np.sin(2*np.pi*frequency*x0)

for sigma in [0.02, 0.05, 0.10, 0.25, 0.50]:
    dx = rng.normal(0.0, sigma, size=samples)
    values = (
        0.5
        + amplitude
        * np.sin(2*np.pi*frequency*(x0 + dx))
    )
    empirical = np.var(values - I0, ddof=1)
    predicted = gradient*gradient*sigma*sigma
    print(
        sigma, empirical, predicted,
        100*(empirical-predicted)/predicted
    )
```

| position sigma (raw px) | empirical variance | first-order prediction | relative error |
|---:|---:|---:|---:|
| 0.02 | 5.02e-5 | 5.02e-5 | -0.031% |
| 0.05 | 3.13e-4 | 3.14e-4 | -0.234% |
| 0.10 | 1.245e-3 | 1.255e-3 | -0.855% |
| 0.25 | 7.458e-3 | 7.846e-3 | -4.946% |
| 0.50 | 2.572e-2 | 3.138e-2 | -18.037% |

The first-order term is excellent for small uncertainty and increasingly approximate as uncertainty spans curved signal structure. V1 should not interpret a large covariance as a precise probabilistic model; high-uncertainty structured regions should be rejected by alignment confidence and patch reliability. The term remains especially useful in flat or locally linear regions.

---

# 15. Validation plan

The validation program must prove both noise reduction and detail/temporal fidelity. A blurred image is not a successful denoise result.

## 15.1 Test harness layers

Build four independent layers:

1. **Math/unit tests** — CFA coordinates, noise propagation, interpolation, robust functions, covariance, exact fallback.
2. **Synthetic RAW renderer** — known radiance, subpixel motion, Bayer sampling, exact noise, ground truth.
3. **Controlled camera corpus** — flat/dark calibrations, tripod static scenes, programmable moving targets.
4. **Uncontrolled real bursts** — handheld low light, people, foliage, water, specular highlights, difficult lenses/corners.

Every result should be renderable through one fixed downstream demosaic/color pipeline for visual review, but RAW-domain metrics remain primary.

## 15.2 Synthetic RAW generation

Generate a high-resolution continuous scene or supersampled linear RGB radiance field. Do not add noise to an already tone-mapped sRGB image and call it RAW. When ordinary photographs are used as source material, invert the image pipeline using an unprocessing approach and clearly mark the approximation. ([Brooks et al., 2019](https://openaccess.thecvf.com/content_CVPR_2019/papers/Brooks_Unprocessing_Images_for_Learned_Raw_Denoising_CVPR_2019_paper.pdf))

For each synthetic frame:

1. define global camera motion and local object/depth motion;
2. warp the continuous scene;
3. convolve with a known optical point-spread function;
4. integrate over each sensor pixel area;
5. apply lens shading and optional PRNU;
6. sample the selected Bayer CFA;
7. apply exposure and conversion gain;
8. draw exact Poisson shot and Gaussian read noise;
9. add optional row/column components;
10. quantize, add black code, and clip to white;
11. emit metadata with controlled errors/missing fields.

Generate the noise-free reference mosaic with the same optics, integration, CFA, and reference geometry. This is the ground truth—not the pre-optical high-resolution scene.

## 15.3 Synthetic motion matrix

At minimum, cover:

### Global-only

- translations: `0.0, 0.1, 0.25, 0.5, 0.9, 1.1, 1.9, 4, 16, 48` raw pixels;
- rotations: `±0.1, ±0.5, ±2` degrees;
- scale: `±0.25%, ±1%, ±3%`;
- mild shear;
- combinations near crop boundaries.

### Local/depth

- two fronto-parallel layers at different disparity;
- thin foreground object crossing a detailed background;
- independently moving textured object;
- low-texture moving object;
- disocclusion;
- local affine deformation;
- row-dependent displacement resembling rolling shutter;
- OIS-like nonuniform global motion.

### Temporal content

- moving face/hand silhouette;
- one- to three-pixel hair/wire;
- foliage-like random local motion;
- water-like phase-changing texture;
- flickering light;
- bright moving point;
- blinking display/LED;
- transient frame corruption.

### Radiometric/noise

- equal exposure;
- scalar differences from `-0.5 EV` to `+0.5 EV`;
- stress at `±1 EV`;
- black offsets of 1–16 DN;
- wrong white level;
- per-plane gain mismatch;
- signal from deep shadow to near saturation;
- trusted, estimated, wrong, and absent noise profiles;
- read-dominated and shot-dominated regimes;
- row-correlated noise;
- defective pixels.

## 15.4 Registration metrics

With known warp ground truth, report:

- endpoint error in raw pixels: median, 90th, 95th, 99th percentile;
- error separately in flat, edge, texture, and motion-boundary regions;
- forward/backward closure;
- valid coverage;
- false-confidence rate: fraction of high-confidence nodes with large error;
- motion-discontinuity rejection width;
- runtime by level.

Preliminary quality targets, to be calibrated:

- textured static-region median endpoint error below `0.15` raw pixel;
- 95th percentile below `0.5` raw pixel;
- high-confidence (`c>0.8`) catastrophic error above 1 raw pixel below 0.1%;
- no confident warp interpolation across known depth discontinuities.

### Covariance calibration

For true error `e_t` and predicted covariance `Sigma_t`, compute

$$
m_t=e_t^\mathsf T\Sigma_t^{-1}e_t.
$$

Compare empirical coverage against chi-square two-dimensional coverage, especially near 68%, 95%, and 99%. If covariance is systematically optimistic, calibrate it with a scalar or feature-dependent correction; do not merely loosen ghost thresholds.

## 15.5 RAW fidelity and noise metrics

Per CFA site and signal bin, report:

- mean bias;
- RMSE and PSNR against noise-free reference mosaic;
- standard deviation in flat regions;
- noise power spectrum;
- row/column covariance;
- residual correlation introduced by interpolation;
- expected versus observed variance reduction;
- `N_eff` versus measured noise reduction;
- shadow black neutrality and per-green consistency.

For equal independent noise in truly static content, standard deviation should approach

$$
\sigma_{\mathrm{out}}
\approx
\frac{\sigma}{\sqrt{N}}
$$

when all frames receive equal weight. Deviations should be explained by unequal noise, rejection, correlation, or model error rather than hidden smoothing.

## 15.6 Detail metrics that prevent blur from winning

Use at least:

- slanted-edge MTF50 and MTF10;
- 10–90% edge-spread width;
- Siemens-star limiting resolution;
- checkerboard/zone-plate aliasing behavior;
- high-frequency texture contrast;
- gradient magnitude retention;
- local Fourier amplitude on known static texture.

A preliminary static high-SNR guardrail is no more than 5% MTF50 loss relative to the reference after the same fixed demosaic/render. Treat this as a tuning target, not a universal acceptance standard.

Plot noise reduction against detail retention as a Pareto curve. Never report PSNR alone.

## 15.7 Ghost and temporal-fidelity metrics

For synthetic motion masks:

- candidate acceptance precision/recall for truly static samples;
- false-merge rate in moving/occluded pixels;
- output-to-reference error inside moving regions;
- edge multiplicity/double-edge energy;
- halo width around motion boundaries;
- temporal-state leakage: fraction of output closer to an alternate state than the reference state;
- colored ghost magnitude after fixed demosaic.

For a reference-preserving denoiser, the moving-region output should be substantially closer to the noise-free reference moment than a naïve registered mean. A temporal median is a useful comparison but is not automatically the desired moment.

## 15.8 Highlight and defect tests

- clipped reference plus unclipped alternates must remain reference-clipped in V1;
- clipped alternate must not darken/bleed into a valid reference edge;
- cubic support touching a clipped sample must be rejected;
- one bad reference pixel must follow the documented three-alternate consensus rule;
- bad alternate pixels must never become colored zipper artifacts after demosaic;
- saturation masks must remain aligned with CFA/output coordinates.

## 15.9 Missing/bad-frame tests

Inject:

- wrong CFA metadata;
- one-pixel crop shift;
- wrong active area;
- wrong bit depth/white level;
- already-demosaiced DNG mislabeled as RAW;
- unsupported geometric opcode;
- corrupt tile;
- NaN gain map;
- missing noise metadata;
- gross exposure mismatch;
- all alternates incompatible.

Expected behavior is a specific skip/fallback diagnostic, never silent fusion. With all alternates unusable, the output must exactly equal the canonical reference.

## 15.10 Determinism tests

For the same binary and CPU path:

- repeat at least 100 times;
- vary worker count from 1 to maximum;
- vary tile scheduling;
- force cache hits and cache misses;
- cancel/restart at every stage;
- compare output and diagnostic hashes bitwise.

For cross-CPU/compiler builds:

- compare maximum absolute/relative RAW error;
- compare motion-node states and rejection masks;
- require no decision changes near thresholds without recording a numeric-mode difference;
- run a strict scalar/reference kernel in CI.

## 15.11 Real low-light corpus

Include:

- tripod static scenes at multiple ISO/gain levels;
- handheld bursts at indoor and near-dark illumination;
- detailed fabrics, hair, foliage, brick, text, and fine electronics;
- faces with eye/hand motion;
- water, flames, LED signs, screens, and traffic;
- bright specular highlights and clipped bulbs;
- corners with strong lens shading;
- scenes with row banding and hot pixels;
- different burst lengths: 2, 4, 8, 16, 32 frames.

The public HDR+ burst dataset is an important external baseline because it contains RAW bursts and a documented processing context. ([HDR+ dataset](https://hdrplusdata.org/)) Also capture native full-resolution bursts from every camera/mode intended for production; mobile datasets alone do not establish behavior on larger desktop-camera RAW files.

For static real scenes, capture a low-ISO tripod reference with much greater total exposure, avoiding clipping, as an approximate ground truth. For moving targets, use a programmable display/robotic target or a high-speed short-exposure reference so the desired temporal moment is known.

## 15.12 Baselines

Compare against:

1. reference frame only;
2. naïve same-CFA average after the same warp;
3. same-CFA temporal median;
4. inverse-variance mean with no robust rejection;
5. V1 with local registration disabled;
6. V1 with registration uncertainty disabled;
7. V1 with patch rejection disabled;
8. the public IPOL HDR+ implementation where input/output compatibility permits.

Ablations show which component prevents which artifact. They are more useful than comparing only against a black-box commercial camera.

## 15.13 Visual-review checklist

Review at 100%, 200%, and a normal print/view scale. Look specifically for:

- blur mistaken for smoothness;
- double/triple edges;
- translucent moving subjects;
- reference noise "islands" with hard outlines;
- noise halos around motion boundaries;
- colored zipper or maze artifacts;
- false color at high-contrast edges;
- periodic texture phase errors;
- moiré changes;
- waxy fabric/skin/hair;
- invented or erased microtexture;
- water/foliage frozen into an unnatural average;
- bicubic ringing near clipped lights;
- corner color/noise changes from lens-shading mistakes;
- tile seams;
- black-level color casts;
- residual row banding;
- highlight darkening or bloom leakage;
- inconsistent grain frequency across confidence regions.

Reviewers must see side-by-side reference, naïve mean, V1, difference image, `N_eff`, reliability, and motion confidence. A pleasant final render alone is insufficient evidence.

## 15.14 Performance acceptance

Measure:

- wall time by stage;
- peak resident memory;
- disk-cache bytes and I/O;
- cancellation latency;
- cache-hit speed;
- scaling with megapixels and frame count;
- scalar versus SIMD numerical differences.

Initial desktop goals should be set after profiling the actual repository and target hardware. Do not inherit mobile-paper timing as a desktop requirement: source decode, full-size camera resolution, cache format, and CPU vector path differ materially.

---


# 16. Actionable implementation sequence

Do not implement all stages in one branch and tune from final pictures. Each phase should introduce inspectable artifacts and pass/fail tests.

## Phase 0 — Freeze contracts and golden test data

Deliver:

- `CfaLayout` with all four Bayer patterns;
- raw/plane/pyramid coordinate types;
- canonical normalized-mosaic contract;
- output-domain contract;
- reject/fallback reason enum;
- serialized V1 parameter object and algorithm version;
- small hand-computed mosaics for unit tests.

Tests:

- raw-to-plane-to-raw round trips for every site and border;
- exact identity warp sampling;
- one-raw-pixel translation becomes half a plane pixel;
- no sampler ever reads a different CFA site;
- exact reference fallback hash.

Do not proceed until CFA parity tests are exhaustive.

## Phase 1 — RAW preparation and masks

Deliver:

- linearization/black/white normalization;
- negative-value preservation;
- saturation and defect masks;
- classification of safe/unsupported pre-demosaic operations;
- pointwise gain-map evaluation;
- normalized tiled cache.

Tests:

- DNG/specification-derived black/white cases;
- per-row/per-plane black maps;
- multiple white levels;
- saturation margins;
- malformed metadata;
- unsupported opcode rejection;
- gain round trip `q -> gq -> /g`.

## Phase 2 — Noise model

Deliver:

- per-CFA `S/O` profile;
- quality hierarchy;
- gain, quantization, interpolation, gate/fusion, scale, and registration variance helpers;
- optional offline calibration loader;
- conservative burst-estimate fallback;
- diagnostics.

Tests:

- analytic versus Monte Carlo gain propagation;
- quantization floors;
- negative sample with nonnegative pilot;
- green-site separation;
- missing/invalid profile behavior;
- flat/dark calibration regression.

## Phase 3 — Same-CFA sampler

Deliver:

- Keys bicubic value and derivatives;
- 4-by-4 footprint validation;
- per-tap gain and variance propagation;
- boundary/clipping/defect rejection;
- scalar reference implementation.

Tests:

- polynomial/constant reproduction;
- exact integer sampling;
- coefficient sum;
- derivative finite differences;
- variance versus Monte Carlo;
- all CFA patterns and borders;
- ringing stress around a clipped edge.

Keep this module independently benchmarkable; registration and fusion both depend on it.

## Phase 4 — Global registration and exposure matching

Deliver:

- multichannel phase correlation;
- translation confidence/peak diagnostics;
- provisional exposure estimate;
- robust affine refinement;
- final scalar IRLS fit and uncertainty;
- frame-level plausibility checks.

Tests:

- synthetic translations through large residual range;
- subpixel phase peak;
- low-texture and periodic texture failure;
- affine motion;
- exposure differences;
- moving-object contamination;
- black-offset diagnostic;
- deterministic candidate/tie behavior.

Exposure-fit pseudocode:

```text
fit_exposure(refSamples, altSamples, metadataScale):
    a = clamp(metadataScale or robustMedianRatio, 0.5, 2.0)

    repeat 3 times:
        num = 0 (double)
        den = 0 (double)

        for j in stable sample order:
            v = vRef[j] + a*a*vAlt[j] + epsV
            r = (yRef[j] - a*xAlt[j]) / sqrt(v)
            h = min(1, 2.5 / max(abs(r), tiny))
            weight = h / v

            num += weight * xAlt[j] * yRef[j]
            den += weight * xAlt[j] * xAlt[j]

        if den is too small:
            return invalid
        aNew = num / den
        if nonfinite or outside hard range:
            return invalid
        a = aNew

    compute uncertainty and plane/intercept diagnostics
    return a if diagnostics pass
```

## Phase 5 — Local motion and covariance

Deliver:

- four-level CFA plane pyramids;
- hierarchical tile search;
- best/second-best tracking;
- robust subpixel translation refinement;
- Hessian covariance;
- `Structured`, `FlatSafe`, and `Rejected` states;
- reverse field and closure;
- confidence-weighted interpolation with disagreement covariance.

Tests:

- synthetic flow endpoint error;
- covariance coverage;
- aperture problem;
- flat-safe sky/wall;
- periodic texture uniqueness;
- depth boundary;
- forward/backward occlusion;
- source-border invalidation;
- no tile seams in a static image.

## Phase 6 — Reliability

Deliver:

- 5-by-5 cell patch standardized residual;
- noise-quality policy;
- min-filter boundary protection;
- pixel flat-top gate;
- absolute low-confidence safety gate;
- tiled 16-bit/float reliability cache.

Reliability construction pseudocode:

```text
build_reliability_tile(ref, alt, motion, cellTile):
    for each Bayer cell q in cellTile:
        if warp hard-invalid at q:
            rawR[q] = 0
            continue

        residuals = []
        for q2 in 5x5 cells around q:
            for c in {R,G0,G1,B}:
                sample alt at q2/c with variance
                if ref and sample are valid:
                    residuals.append(
                        (sample.y - ref.y)
                        / sqrt(
                            sample.gateVariance
                            + ref.gateVariance
                            + epsV
                        )
                    )

        if residuals.count < 50:
            rawR[q] = 0
            continue

        z = median(abs(residuals)) / 0.67448975
        patch = flat_top_patch_gate(z, 1.5, 3.5)
        rawR[q] = hard * noiseConfidence
                  * motion.alignConfidence(q)
                  * patch

    eroded = min_filter_3x3(rawR)
    for q:
        reliability[q] = smootherstep_remap(
            eroded[q], 0.20, 0.80
        )

    store deterministically
```

The four RAW sites belonging to one Bayer cell use that cell's reliability value without interpolation. This avoids blending a rejected side of a motion boundary back into an accepted side.

Tests:

- known moving masks;
- one-pixel/thin features;
- boundary erosion width;
- flat static efficiency;
- wrong noise model;
- clipped/defective support;
- 16-bit storage versus float decision identity.

## Phase 7 — Fusion

Deliver:

- double-precision tile accumulators;
- inverse effective variance;
- per-alternate and low-confidence total caps;
- exact fallback;
- reference-defect exception;
- `N_eff`, output variance, and rejection diagnostics.

Tests:

- equal-noise `1/sqrt(N)` behavior;
- unequal-noise optimal weighting;
- no observation-dependent weight bias;
- candidate permutation invariance only when frame order/rounding are mathematically equivalent;
- fixed stable order across threads;
- moving candidate falls to reference;
- all-alternate failure exact copy;
- clipped reference exact policy.

## Phase 8 — Streaming, cache, cancellation, deterministic scheduling

Deliver:

- all cache stages and dependency keys;
- memory-budget manager;
- tile/source ROI planner;
- atomic result publication;
- cancellation points;
- strict scalar/reference path;
- corrupted-cache recovery.

Tests:

- low-memory spill;
- high-megapixel frame;
- 2/4/8/16/32-frame bursts;
- random cancellation injection;
- cache hit/miss equivalence;
- worker-count hash equivalence;
- stale-key invalidation.

## Phase 9 — Corpus tuning and release gates

Tune only after the ablation harness is producing:

- raw error/noise/detail metrics;
- motion-mask metrics;
- confidence/covariance calibration;
- visual diagnostics;
- performance/memory traces.

Freeze a conservative parameter set, then rerun the full corpus without hand-selected exceptions. Any scene-specific workaround must be explained by a measurable feature and added to the synthetic suite before release.

---

# 17. Future alternatives, recorded but not designed in parallel

## 17.1 Joint CFA demosaic/super-resolution

The leading classical next step is HFSR-style direct RGB reconstruction: scatter aligned CFA measurements onto a full-color grid with anisotropic kernels and robustness. It can use natural subpixel offsets to recover genuine spatial/color information. It should be a separate V2 node or mode because it changes the output contract and evaluation criteria.

## 17.2 Frequency-domain HDR+ merge

A closer HDR+ reproduction could improve frequency-selective behavior and reduce interpolation blur. It requires resolving public implementation ambiguities, careful tile/window normalization, and extensive ringing/texture tuning. It is an alternative fusion backend after V1 registration is proven.

## 17.3 Robust variational reconstruction

An `L1` data term with edge-aware regularization can jointly model sampling and reject outliers. It is attractive for an offline high-quality mode but needs iterative solvers, stopping rules, and regularization validation that do not fit the first production path.

## 17.4 Learned burst fusion

A KPN or learned alignment/fusion model can be added after a camera-diverse RAW corpus, synthetic unprocessing pipeline, model runtime, deterministic fallback, and licensing/deployment plan exist. The V1 diagnostics can provide supervision and failure labels.

## 17.5 Depth/layered motion and rolling shutter

Explicit layered motion, depth-aware warps, homographies, gyroscope priors, and row-dependent camera pose can increase usable area in parallax and rolling-shutter scenes. They should consume the same uncertainty/reliability interfaces rather than bypass them.

## 17.6 Bracketed HDR and highlight recovery

Deliberate exposure brackets require saturation-aware radiance estimation, motion decisions that differ by exposure, and a policy for which temporal state owns clipped regions. This is a distinct method, not a V1 option.

## 17.7 Burst deblurring

Frame sharpness selection, Fourier burst accumulation, or blind deconvolution addresses intra-frame motion blur. It should not be hidden inside a noise-reduction node because the artifacts and reference-moment semantics differ.

---

# 18. Known uncertainties and required corpus experiments

The literature does not settle the following choices for this application:

1. **Noise-profile availability and accuracy.** Full-size camera RAW formats often lack a directly usable per-frame `S/O` profile. Camera/mode calibration coverage may determine whether safe fusion is broadly available.
2. **Residual black and correlated row noise.** The simple model can underpredict structured sensor noise, especially after lens-shading amplification.
3. **Lens-shading contract.** The application's existing decoder/graph may already consume some gain operations. The boundary must be audited so gains are neither omitted nor doubled.
4. **Exposure fit under motion and flicker.** The scalar fit needs corpus validation for scenes dominated by moving subjects, screens, or local illumination changes.
5. **Affine versus translation-only global seed.** Affine should increase corner alignment but may overfit low-information scenes.
6. **Tile size and stride.** `64 x 64` raw support is a reasonable start, not a universal optimum across 12–100 megapixel sensors and lens detail.
7. **Keys bicubic ringing/detail tradeoff.** The selected cubic may preserve more detail than bilinear while creating small overshoots near extreme edges.
8. **Registration covariance calibration.** Hessian covariance omits model error and must be corrected against synthetic endpoint error.
9. **FlatSafe policy.** It should unlock denoising in skies/walls without allowing low-contrast motion or banding to merge.
10. **Motion-field boundary threshold.** The `0.75` raw-pixel disagreement guard controls a direct trade between usable coverage and ghost risk.
11. **Patch support and erosion radius.** Larger support is safer but leaves noisier outlines around moving content.
12. **Pixel gate thresholds.** The 2.5–5 sigma gate is statistically efficient but reference-selection behavior must be measured in deep shadows.
13. **Reference-pilot shot variance.** Using the noisy reference as a shared pilot avoids candidate-dependent bias but may be improved by a reference-only variance pilot.
14. **Many-frame behavior.** With 32 frames, small systematic registration or calibration errors can dominate random-noise gains.
15. **Reference-defect consensus.** The three-alternate exception needs dedicated false-repair tests.
16. **DNG export semantics.** Fused residual noise, consumed opcodes, gain maps, black/white levels, and provenance need a separate format-conformance review.
17. **Cross-CPU bitwise behavior.** A strict math mode may be needed if exact hashes across SIMD implementations are a product requirement.
18. **Public-method incompleteness.** The public HDR+ and HFSR papers/implementations necessarily include choices or omissions; Adobe does not publish Indigo's robust merge equations, and Apple patents do not document a shipping pipeline.

Each uncertainty should become a tracked experiment with a frozen corpus, metric, and decision record.

---

# 19. Final V1 statement

**Selected method:** **Reference-Anchored CFA-Plane Robust Inverse-Variance Fusion (RA-CFA)**. It produces an original-resolution denoised Bayer mosaic by registering four same-CFA planes, estimating uncertainty, rejecting temporal/geometric disagreement, and averaging only accepted measurements with noise-aware weights. It is designed so every unsafe region returns locally to the reference rather than inventing a compromise.

## The five most consequential decisions

1. **Return a denoised Bayer mosaic, not RGB.** Joint demosaicing/super-resolution is deferred so V1 fits the existing RAW graph and has a smaller artifact surface.
2. **Represent and resample `R/G0/G1/B` separately.** No registration comparison or interpolation crosses incompatible CFA sites.
3. **Make noise and calibration part of geometry and fusion.** Black/white normalization, lens shading, exposure scale, Poisson-Gaussian variance, interpolation variance, and registration covariance all affect confidence and weights.
4. **Use global affine initialization plus multiscale local translation with covariance and reverse consistency.** A warp is accepted only with evidence; flat regions use a special harmless-uncertainty path.
5. **Anchor every decision to the reference and provide an exact fallback.** Motion, occlusion, parallax, clipping, model uncertainty, and numerical failure reduce alternate weight to zero locally.

## Uncertainties that must be resolved on real RAW bursts

The highest-impact tests are noise-profile quality, lens-shading/black residuals, subpixel registration covariance, bicubic detail/ringing, motion-boundary erosion, deep-shadow reference-gating behavior, and many-frame systematic error. Until those pass a camera-diverse corpus, the method should default conservatively and expose when it returned reference-only regions.

---

# 20. Primary references and implementations

## Google HDR+

- Sam Hasinoff et al., [*Burst Photography for High Dynamic Range and Low-Light Imaging on Mobile Cameras*](https://hdrplusdata.org/hdrplus.pdf), SIGGRAPH Asia 2016.
- [Official Google Research publication page](https://research.google/pubs/burst-photography-for-high-dynamic-range-and-low-light-imaging-on-mobile-cameras/)
- [HDR+ supplementary material](https://hdrplusdata.org/hdrplus_supp.pdf)
- [HDR+ public burst dataset](https://hdrplusdata.org/)
- Antoine Monod, Julie Delon, and Thomas Veit, [*An Analysis and Implementation of the HDR+ Burst Denoising Method*](https://doi.org/10.5201/ipol.2021.336), IPOL 2021.
- [IPOL article PDF](https://www.ipol.im/pub/art/2021/336/article_lr.pdf)
- [Open HDR+ Python implementation](https://github.com/amonod/hdrplus-python)

## Google Handheld Multi-Frame Super-Resolution

- Bartlomiej Wronski et al., [*Handheld Multi-Frame Super-Resolution*](https://arxiv.org/abs/1905.03277), ACM TOG 2019.
- [Official Google Research publication page](https://research.google/pubs/handheld-multi-frame-super-resolution/)
- [Paper PDF](https://3dvar.com/Wronski2019Handheld.pdf)
- Jamy Lafenetre, Gabriele Facciolo, and Thomas Eboli, [*Implementing Handheld Burst Super-Resolution*](https://doi.org/10.5201/ipol.2023.460), IPOL 2023.
- [IPOL article PDF](https://www.ipol.im/pub/art/2023/460/article_lr.pdf)
- [Open nonofficial HFSR implementation](https://github.com/Jamy-L/Handheld-Multi-Frame-Super-Resolution)

## RAW standards and noise

- Adobe, [*Digital Negative (DNG) Specification 1.7.1.0*](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf), September 2023.
- [Adobe DNG resources](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html)
- Alessandro Foi et al., [*Practical Poissonian-Gaussian Noise Modeling and Fitting for Single-Image Raw-Data*](https://doi.org/10.1109/TIP.2008.2001399), IEEE TIP 2008.
- [Foi et al. paper PDF](https://webpages.tuni.fi/foi/papers/Foi-PoissonianGaussianClippedRaw-2007-IEEE_TIP.pdf)
- [EMVA 1288 standard resources](https://www.emva.org/standards-technology/emva-1288/)

## Adobe and Apple public disclosures

- Marc Levoy and Florian Kainz, [*Project Indigo — a computational photography camera app*](https://research.adobe.com/articles/indigo/indigo.html), Adobe Research 2025.
- Apple, [US11189017B1 — *Generalized fusion techniques based on minimizing variance and asymmetric distance measures*](https://patents.google.com/patent/US11189017B1/en)
- Apple, [US11094039B1 — *Fusion-adaptive noise reduction*](https://patents.google.com/patent/US11094039B1/en)
- Apple, [US9626745B2 — *Temporal multi-band noise reduction*](https://patents.google.com/patent/US9626745B2/en)
- Apple, [US11151702B1 — *Deep learning-based image fusion for noise reduction and high dynamic range*](https://patents.google.com/patent/US11151702B1/en)

## Registration, interpolation, and other reconstruction methods

- Bruce Lucas and Takeo Kanade, [*An Iterative Image Registration Technique with an Application to Stereo Vision*](https://publications.ri.cmu.edu/storage/publications/pub_files/pub3/lucas_bruce_d_1981_1/lucas_bruce_d_1981_1.pdf), 1981.
- Simon Baker and Iain Matthews, [*Lucas-Kanade 20 Years On: A Unifying Framework*](https://publications.ri.cmu.edu/storage/publications/pub_files/pub3/baker_simon_2002_3/baker_simon_2002_3.pdf), 2002.
- Manuel Guizar-Sicairos, Samuel Thurman, and James Fienup, [*Efficient subpixel image registration algorithms*](https://opg.optica.org/ol/fulltext.cfm?uri=ol-33-2-156), Optics Letters 2008.
- Robert Keys, [*Cubic Convolution Interpolation for Digital Image Processing*](https://doi.org/10.1109/TASSP.1981.1163711), IEEE 1981.
- Sina Farsiu et al., [*Fast and Robust Multiframe Super Resolution*](https://users.soe.ucsc.edu/~milanfar/publications/journal/SRfinal.pdf), IEEE TIP 2004.
- Sina Farsiu et al., [joint color demosaicing/super-resolution work](https://people.duke.edu/~sf59/tip_demos_final_color.pdf)
- Ben Mildenhall et al., [*Burst Denoising with Kernel Prediction Networks*](https://openaccess.thecvf.com/content_cvpr_2018/papers/Mildenhall_Burst_Denoising_With_CVPR_2018_paper.pdf), CVPR 2018.
- [KPN source code](https://github.com/google/burst-denoising)
- Tim Brooks et al., [*Unprocessing Images for Learned Raw Denoising*](https://openaccess.thecvf.com/content_CVPR_2019/papers/Brooks_Unprocessing_Images_for_Learned_Raw_Denoising_CVPR_2019_paper.pdf), CVPR 2019.
- [Unprocessing source code](https://github.com/timothybrooks/unprocessing)
- Mauricio Delbracio and Guillermo Sapiro, [*Burst Deblurring: Removing Camera Shake Through Fourier Burst Accumulation*](https://openaccess.thecvf.com/content_cvpr_2015/papers/Delbracio_Burst_Deblurring_Removing_2015_CVPR_paper.pdf), CVPR 2015.
- Thibaud Ehret et al., [*Joint Demosaicking and Denoising by Fine-Tuning of Bursts of Raw Images*](https://openaccess.thecvf.com/content_ICCV_2019/papers/Ehret_Joint_Demosaicking_and_Denoising_by_Fine-Tuning_of_Bursts_of_Raw_ICCV_2019_paper.pdf), ICCV 2019.

