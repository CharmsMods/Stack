# Local EV / Light: extreme adjustments and spatial behavior

**Research/design pass, iteration 2: control semantics, constrained response, and spatial contracts.**

Audited against Stack `aa972c3fcdf45470d490ce9a1f043431dfbb05ea` (1.2.167), on November 20, 2026. Deliverables are this decision record, the [panel concept](panel-concept.html), and a working [scalar response study](response-study.html). Merging this PR does not change native image processing, saved recipes, or controls. A nearby Guidance correction describes the slider that already exists.

## 1. Recommended direction

Improve the existing spatial engine rather than treating Local EV as a new feature. Give Shadows and Highlights the job of changing broad dark and bright regions while retaining their internal detail. Give Blacks and Whites distinct lower-tail and upper-tail behavior. Keep Midtones as a central brightness control. Make extreme settings predictable by constraining the resulting transfer, not by choosing a smaller arbitrary slider maximum.

The immediate priorities for a later implementation are:

1. Resolve the visible **Detail protection** control's disconnected GPU behavior.
2. Prevent accidental tonal reversals, preserve each amount control's direction, and reveal when combined adjustments are limited.
3. Separate **region size** from **detail retention**; Smoothness currently couples region size with pixel/guide interpolation.
4. Share the processing contract with the panel, floating graph, diagnostics, and export.
5. Version the new behavior so existing projects retain their appearance.

Keep the familiar six controls and their current visible ranges. The five tonal numbers remain normalized amounts, not exact stops. Exposure continues to follow Light; positive Exposure retains the existing highlight shoulder. New processing should be an explicit version, with an opt-in upgrade for old nodes.

This is a design decision record, not a claim that the proposed filter or numerical defaults have passed photographic validation. **Verified** below means source inspection or the stated arithmetic. **Proposed** means a Stack design choice. Research sources establish general methods and documented product behavior, not unpublished Adobe code.

The second iteration selects a concrete scalar reference: fixed exposure samples, isotonic fitting, constant-gain tails, and positive smoothing of the one-dimensional correction. It also distinguishes a guide's characteristic scale from its full dependency support, input-tone statistics from regional tone membership, and first-build memory from replacement memory. These decisions make the next implementation slice smaller and reviewable; they do not select a new spatial filter prematurely.

## 2. What Stack actually does today

### The active path

Local EV already measures incoming luminance, fits tonal targets, and builds a multiscale edge-aware log-luminance guide. Smoothness blends each pixel's own exposure value toward that guide. The five bands and the advanced curve select an exposure correction using this control signal. Processing multiplies the original RGB by one gain; it does not directly blur RGB. Saved area/gradient operations can then run, Exposure follows, and the outer node mask blends the result. [C1][C3][C4][C5]

The current guide is a repeated, separable five-tap filter over up to eight increasing scales, with edge weights derived from original luminance. It is not a Local Laplacian pyramid reconstruction. It normalizes interpolation over samples admitted by its positive-luminance and alpha tests. Unlike the histogram, this path does not explicitly establish a shared NaN/Inf policy; the redesign must define that boundary. [C4][C14]

| Control or mechanism | Current behavior | Consequence for the redesign |
| --- | --- | --- |
| Exposure | Panel -16 to +16 EV; renderer accepts finite -32 to +32. Positive values use a protected shoulder; negative values use uniform gain. | The displayed EV is a requested exposure change, not an identical change to every bright pixel. Preserve ordering and make that distinction clear. |
| Five tonal amounts | -100 to +100, initially zero. Each anchor requests up to +/-4 EV. Raised-cosine bases interpolate adjacent anchors. | These are five locations in one correction field, not five independently defined photographic operators. |
| Tonal targeting | Input histogram spans -32 to +32 EV in 1/16-stop bins. Fit uses the 1st/99th percentiles and at least a four-stop span. | Small bright populations do not set the fit; they still receive the outer band's correction. |
| Final main correction | Bands, advanced curve and targeted contributions ultimately clamp to +/-4 EV. | Strong combinations can reach an invisible ceiling. The individual controls may keep moving. |
| Smoothness | 0 to 1, default 0.65. Largest pass offset is `16 + 240 * smoothness` source pixels; the same value controls pixel/guide interpolation. Zero bypasses guide construction. | Default offset is 172 pixels; the cascade reaches farther. Scale and amount are coupled. |
| Edge protection | 0 to 1, default 0.75; affects guide range weighting. Its default range-weight floor is 0.25. | Strong edges can still contribute across a boundary; this is not a hard barrier or a halo guarantee. |
| Detail protection | 0 to 1, default 0.80; serialized and shown. | Inert in the current main GPU tonal-guide image, overlay and scope paths; see below. |
| Highlight protection | 0 to 1, default 0.50; reduces positive Local EV correction. | Separate from the later Exposure shoulder. Its transition uses the advanced-curve endpoint, not the fitted Light range. |
| Strength | Recipe sanitizer and floating control allow 0 to 1; graph metadata advertises 0 to 2. | Values above 1 are discarded by sanitization. Unify metadata and engine limits. |
| Advanced correction | Default domain -8 to +6 EV; bounds can reach -16 to +16. Up to 12 points, each +/-4 EV. | Its sum with the five controls must participate in constraints and effective-response feedback. |
| Saved behavior | No dedicated Local EV algorithm version. Graph read/write forces automatic targeting and protected Exposure after Light. | A semantic change requires a new version; old switches are not an available migration mechanism. |

Sources: parameters and defaults [C1][C2][C6][C7]; actual correction [C5]; Exposure [C8].

Five +100 bands do **not** add to +20 EV: adjacent basis weights form a partition of unity, so together they request +4 EV before other contributions and protections. Blacks/Whites extend as constant gains outside the fitted interval. Exact zero remains zero. Neither is currently an output black/white point control. [C1][C5]

### Verified control mismatch: Detail protection

The image and overlay shaders still define an old `edgeAwareWeight()` that reads `uDetailProtection`, and `accumulateSceneEv()` calls it. However, nothing calls `accumulateSceneEv()`. The live `edgeAwareSceneEv()` samples the prepared guide. That guide receives Smoothness and Edge protection, but no Detail protection. The scope uses the same active image shader and guide. [C3][C4][C5]

The CPU helper still uses Detail protection, but its inspected callers are tests, not a CPU image-rendering fallback. Passing that helper's tests would not verify the GPU control. This is a static call-path finding; no running-image test was performed. Legacy saved area processing is separate and is not covered by this claim. [C9]

**Proposed disposition:** preserve old rendering under the legacy version. In the new version, replace the ambiguous setting with Detail retention, with a defined pixel/region exposure-field blend. Do not simply reconnect an obsolete kernel and silently alter old photos.

### Verified extreme: the requested transfer can reverse tones

For neighboring anchors separated by `h` stops, with requested corrections `a0` and `a1`, the existing cosine interpolation gives, at the interval midpoint:

```text
F(E) = E + correction(E)
F'(midpoint) = 1 + (a1 - a0) * pi / (2*h)
```

For a 14-stop fit, `h = 3.5`. Shadows +100 followed by Midtones 0 changes the correction from +4 EV to 0 EV; the minimum slope is about **-0.795**. A negative slope means brighter inputs can become darker outputs.

Consider a fitted range centered like -8 to +6 EV, Smoothness 0, Strength 1, neutral advanced curve, no masks/targets and Exposure 0:

| Input position | Input EV | Requested correction | Output EV |
| --- | ---: | ---: | ---: |
| Shadows anchor | -4.5 | +4 | -0.5 |
| Midtones anchor | -1.0 | 0 | -1.0 |

The darker sample becomes brighter. Highlight protection is inactive at these positions, and the +/-4 clamp does not prevent the reversal. Actual histogram fits are quantized to bin centers, so exact endpoints shift; the same example holds for a shifted 14-stop fit. This is an analytic consequence of the executed formula under the stated settings, not a screenshot from Stack. [C1][C5]

At the minimum four-stop span, adjacent anchors are only one stop apart. Reducing every slider's maximum would restrict useful edits without providing a general solution. The constraint belongs on the combined transfer.

### Presentation and performance findings

The floating graph plots the requested bands plus advanced curve. It omits Strength, highlight attenuation, final clamp, masks, spatial variation and later Exposure. Label it as requested correction; it cannot be an authoritative before/after image curve. [C10]

The panel already has thin tracks, hollow handles, exact entry, arrow keys, Shift precision, reset, and Escape/focus-loss restoration. Finite-range dragging uses the full range divided by track width; a narrower panel therefore changes values faster per pixel. The caller's `speed` affects keyboard steps, not this finite-range drag. Improve precision without removing these existing interactions. [C11]

Analysis is already cached from upstream content, geometry and working space. Guide identity also includes spatial settings and native dimensions; band-amount edits reuse it. Four scalar fits can be retained, but only one full-resolution guide. Active Local EV requires complete input evidence in viewport planning. Preserve these boundaries; do not add per-drag full-frame scans or fit the visible viewport. [C3][C4][C12]

### Spatial reach, proxy behavior and allocation limits

For largest pass offset `M`, the eight scales are `L_k = 2 * (M/2)^(k/7)`, for `k = 0...7`. Each horizontal/vertical five-tap pass reaches `L_k`, and each band consumes the previous band's result. Possible dependency therefore accumulates across the cascade. [C4][C15]

| Smoothness | Largest pass offset | Continuous cascade radius per axis | Ideal native-grid bound including interpolation |
| --- | ---: | ---: | ---: |
| 0.65, default | 172 source pixels | 363.11 source pixels | 367 pixels |
| 1.00 | 256 source pixels | 510 source pixels | 510 pixels |

These are support calculations, not measured influence or halo width. The continuous bound is `sum(L_k)`; the ideal center-aligned native-grid bound is `sum(ceil(L_k))`. A production planner must use actual sample geometry, numerical margins and upstream resampling footprints. A future Region size value must never double as the tile-padding distance.

At proxy scale `(qx,qy)`, a band is omitted when `L_k * max(qx,qy) < 1`. The ideal processed-grid bounds sum `ceil(L_k*qx)` and `ceil(L_k*qy)` over active bands. At one-quarter linear resolution (`qx = qy = 0.25`) and default settings, two bands are omitted; the remaining ideal radius is 92 processed pixels, equivalent to 368 source pixels. Matching the largest source-pixel offset does not make proxy and native processing identical. Small-scale omission and interpolated original-color guidance differ. [C4]

The edge range weight is `mix(1, gaussian(difference,sigma), edgeProtection)`. Its default 0.25 floor admits cross-edge influence before spatial and alpha weighting. Similar-luminance color boundaries also lack a strong luminance cue. Neither observation proves a visible artifact in a particular photograph. [C4]

Guide construction uses three RG32F fields, **24 bytes per processed pixel**. During replacement after a spatial-setting change, the old 8-byte-per-pixel guide remains alive until the new result succeeds: up to **32 bytes per pixel** for guide-related allocations, then 8 retained. At 7,952 x 5,304 pixels, these are approximately 965 MiB for a first build, 1,287 MiB during replacement, and 322 MiB retained. Input/output and other graph allocations are additional. These are allocation arithmetic, not measured total VRAM. Existing graph planning already reserves RAW scratch surfaces and counts retained guides; refine its accounting rather than claiming no budget exists. [C3][C4][C12][C16]

### Input tones and regional control tones are different signals

The fit counts finite, positive-luminance input pixels with positive alpha; accepted pixels count equally, including fractional alpha. The correction is evaluated using `E_control = mix(E_input, E_guide, smoothness)`. A dark crease in a brighter surface can therefore receive a different correction from an equally dark broad area. The original histogram is not a histogram of the actual control signal. [C3][C5][C14]

For the first new-version comparison, keep fitting in the original-input domain and interpret the guide against those same anchors. Diagnostics should distinguish **Input tones** from **Regional control tones**. Do not refit merely because Detail retention moves. Today's fit identity includes processed dimensions, so proxy/native fits can differ. The proposed shared anchors require one canonical full-frame analysis identity and an explicit sampling policy; whole-image coverage does not necessarily require native-resolution analysis. [C3]

## 3. What the Lightroom evidence supports

| Evidence | Finding and limits | Design consequence |
| --- | --- | --- |
| Adobe tone/color help [R1] | Exposure, Highlights/Shadows and Whites/Blacks have distinct documented jobs. The latter explicitly adjust clipping endpoints. | Avoid five differently named copies of the same control. Stack's scene-linear tails need their own contract rather than copying output clipping. |
| Adobe's adjustment procedure [R2] | Describes PV3-PV5 controls as image-adaptive and interacting. Its percentage regions are teaching aids, not published mask equations. | Research content adaptation and combinations; do not hard-code those percentages as recovered Adobe behavior. |
| Hasinoff's author statement [R3] | Lightroom 4 / Photoshop CS6 Shadows, Highlights and Clarity were inspired by Local Laplacian research. | Strong historical motivation for multiscale comparison; it does not reveal the current shipping implementation. |
| Adobe engineer Eric Chan, May 30, 2012 [R4] | Described edge-aware controls and expensive halo-suppression masking; cited strong settings and branches against bright sky as difficult. | Include thin boundaries and strong settings in later checks. This is historical evidence, not a claim about a current Lightroom bug. |
| Adobe process-version reference [R5] | Documents appearance-affecting processing generations, including the 2012 tone redesign and later low-light improvements. | Preserve old Stack rendering explicitly. |
| Eric Chan's HDR explanation, 2023 [R6] | HDR editing extends beyond SDR white; display capability and SDR rendering matter separately. | Separate captured clipping, scene headroom, and output/display limits. |
| Eric Chan's first-person design account, July 2013 [R14] | The PV2012 redesign reconsidered channel clipping, highlight color blending, strong edges, names, order, defaults and ranges over many photographic iterations. | A familiar -100 to +100 scale does not define the operator. Evaluate photographic jobs and interaction together. |

The evidence supports **image-adaptive, edge-aware tone redistribution**, not an exact Lightroom clone. There is no verified universal Lightroom EV equivalent for +100, fixed neighborhood radius, or current internal threshold at +/-50 in this research. Do not turn repeated forum explanations into implementation facts.

Two reading cautions: [R2] contains an inconsistent process-version heading; [R5] is authoritative for PV3 = 2012. Top-down adjustment advice describes a workflow, not proof that identical final settings depend on the chronology of slider gestures. In [R4], the engineer also says a suggested progressive rendering system would not solve that particular artifact; progressive preview is not a halo cure.

### Adjacent tools: useful evidence, separate algorithms

| Primary documentation | Useful distinction | What it does not establish |
| --- | --- | --- |
| Photoshop Shadow/Highlight [R15] | Amount, tonal width and neighborhood radius are separate. Its manual explicitly warns that extreme amounts can reverse brightness relationships. | This command is not Lightroom's Basic panel; its radius, limits and clipping options do not reveal Lightroom internals. |
| darktable filmic rgb [R16] | Scene black/white exposure anchors, toe/shoulder shape, output luminance endpoints and clipped-area reconstruction are separate jobs. The black picker is sensitive to noise. | Its scene-to-display contract is not appropriate wholesale for scene-linear Local EV. |
| darktable shadows/highlights [R17] | Larger radius softens transitions but can create halos; smaller radius can look artificial. Midtone reach is separately controlled. | This older Lab/display-referred module is not evidence that every bilateral or guided method behaves identically. Its manual recommends tone equalizer instead. |
| RawTherapee Shadows/Highlights [R18] | The documented engine uses a fast guided filter and separates tonal width from radius. RGB behavior can still oversaturate shadows. | Edge awareness and RGB-ratio preservation do not guarantee appearance under strong edits. |
| RawTherapee Dynamic Range Compression [R19] | Amount, detail and anchor are different controls. Histogram adaptation can cause brightness discontinuities between panorama source frames with identical settings. | Holding five target positions cannot freeze all spatial/image-adaptive behavior. |
| RawTherapee Tone Mapping [R20] | Its documented base/detail method has scale-dependent preview/export differences. | A responsive proxy is not evidence of semantic agreement with full-resolution output. |

The practical conclusion is to separate **amount**, **tonal reach**, **spatial scale** and **detail behavior** in the processing contract. That does not require exposing four controls beside every slider. Begin with the six main controls, explain their jobs on selection, and keep spatial refinements in Settings.

## 4. Proposed meaning of each control

The aim is **changing illumination relationships while retaining useful structure**. Equal-luminance pixels can deserve different changes: a dark crease on a bright shirt should often move with the shirt, while a broad dark wall should respond to Shadows. A neighborhood guide can help make this distinction; it does not identify objects or understand materials.

| Control | Proposed intent | Positive / negative behavior | Boundary |
| --- | --- | --- | --- |
| Exposure | Overall brightness, retaining Stack's existing protected positive response | Brighter / darker; use the current monotone shoulder after Light | Requested EV, not a promise of uniform highlight gain. Pure uniform exposure remains a separate graph operation. |
| Blacks | Placement of the deep nonzero tail | Open / deepen that tail, with a constrained transition into Shadows | Preserve exact zero; do not alter sensor black calibration or silently add a gray output floor. |
| Shadows | Illumination of dark regions | Reveal / deepen broad dark material while retaining its texture | Do not target every tiny dark edge in a bright region equally. Lifting noise is not recovering signal. |
| Midtones | Central regional brightness | Raise / lower central tones with smooth joins to both tails | This is not Contrast; avoid adding a second contrast or clarity control in this pass. |
| Highlights | Shape and separation of broad bright regions | Open / compress bright regions while preserving visible variation | Do not promise reconstruction of clipped camera channels. |
| Whites | Placement of the high tail | Extend / restrain that tail, with a constrained transition from Highlights | Preserve HDR separation; do not clamp scene RGB at 1 or assume every bright feature is diffuse white. |

**Important distinction from Lightroom:** Adobe documents clipping-oriented Blacks/Whites. Stack's proposed controls remain scene-linear tail shapers; actual output clipping belongs to the View/output stage. Names can remain familiar if the selected-control explanation and diagnostics make the behavior clear.

Implement these as coordinated targets for one transfer. Zero must be identity. Increasing an amount should never lower the shared scalar output elsewhere when the input/targets are fixed. This is a nondecreasing response, not guaranteed visible movement at every setting: conflicting requests can pool and budget limits can plateau. The panel must be able to show the effective result.

The current outer bands already extend as constant gains. The new contribution is **coordinated tail placement, constrained joins and realized-response feedback**, not merely adding tails. A bounded EV gain cannot produce a permanent finite display-white shoulder as input grows without bound, or lift exact zero to a nonzero gray floor. Constant-gain outer tails preserve extreme HDR differences; shaping occurs over a finite transition. A four-stop input span with Blacks requesting +4 EV and Whites -4 EV has reversed requested endpoints, so both cannot be hard constraints.

The first implementation candidate should keep normalized amounts in [-100,100], map them to requested regional/tail targets, and solve those targets jointly. Do not advertise -100 as black, +100 as white, or +50 as a fixed number of recovered stops. Endpoint roles, transition width and shoulder/toe shapes require comparison on photographs before becoming defaults.

## 5. Processing contract and algorithm candidates

### A. Constrain the transfer before improving the filter

Let `E = log2(Y / middleGrey)` for valid positive working-space luminance. If the requested exposure field is `g(E)`, the pointwise transfer is `F(E) = E + g(E)`.

Require a strictly positive derivative in the protected version, with a floor `delta` in `(0,1]`. A proposed initial contrast floor is `F'(E) >= 0.10`; **0.10 is a prototype tuning value**, not a perceptual guarantee. Include the advanced correction and brightness-dependent protection in the requested field before enforcing the constraint. Checking each band separately is insufficient.

#### Two different guarantees

1. Brighter scalar input stays brighter: `dF/dE >= delta`.
2. Raising one amount cannot darken any scalar output: `F_new(E) >= F_old(E)` when that amount increases with other inputs fixed.

A generic monotone interpolator only addresses the first. For example, PCHIP on input locations `[0,1,2,3]` and output samples `[0,1,2,3]` yields 1.5 at input 1.5. Raising only the first output to 0.1 yields approximately **1.493421** there, despite both curves remaining increasing. This bounded arithmetic example follows PCHIP's data-dependent tangents; it is not an implementation defect in the interpolator. [R23]

#### Selected reference construction

Use fixed, increasing exposure samples `E_i`, fixed positive weights, requested gains `r_i` in `[-G,G]`, and `0 < delta <= 1`:

```text
q_i = (1-delta) * E_i
z = weighted_isotonic_fit(q + r)  // z_0 <= z_1 <= ...
c_i = z_i - q_i
```

The pool adjacent violators algorithm (PAVA) minimizes weighted squared displacement under the ordering constraint. [R21] Start with equal weights on uniform EV samples: this weights departure across exposure, not the number of pixels in a histogram bin. Histogram weighting could sacrifice rare lights and deep tails more readily; any future priorities must be explicit and fixed during an amount gesture.

Linearly interpolate `c_i` to obtain `c_PL(E)`, with constant correction outside the sample domain. Let `A` be its exact piecewise-quadratic antiderivative, extended linearly in those tails. For a fixed half-width `h > 0`:

```text
g(E) = [A(E+h) - A(E-h)] / (2*h)
F(E) = E + g(E)
F'(E) = 1 + [c_PL(E+h) - c_PL(E-h)] / (2*h)
```

This positive symmetric average smooths the **one-dimensional correction function**, not the photograph. It gives a continuous derivative; outside the sample domain plus `h`, the correction is constant and slope is 1. The [response study](response-study.html) uses 257 uniform samples, `delta = 0.10`, `h = 0.25 EV`, and the existing five-band request basis. Those are a declared comparison configuration, not final photographic semantics or shipping defaults.

The guarantees follow as an original application of the isotonic projection's coordinatewise order preservation [R22]:

- `q-G` and `q+G` are already increasing, hence fixed by the projection. Since they bound `q+r`, they also bound fitted `z`. Therefore `|c_i| <= G`; positive interpolation and averaging retain that bound.
- `F_PL(E) = E+c_PL(E)` has segment slopes at least `delta`. Its symmetric secant slope is the derivative above, so the smoothed result retains the same floor, including joins and tails.
- Zero request remains zero, and every uniform gain `c` in `[-G,G]` remains exactly `c` in real arithmetic. Use ordinary rounding tolerances or explicit neutral/uniform fast paths in native code.
- Raising a control with nonnegative fixed influence weights raises or preserves every requested sample. Projection, interpolation and positive averaging preserve that order. Exposure positions, fit weights, smoothing width, guide and target identity must remain fixed for this guarantee; any request-budget mapping must also be nondecreasing.

This replaces the earlier generic monotone-interpolator/soft-bound proposal. An always-active post-fit bound such as `4*tanh(g/4)` would turn a feasible uniform +4 EV request into about +3.0464 EV. There is no need to weaken that case: the selected construction preserves the supplied gain bound directly.

#### Budget and scope

Retain +/-4 EV as the first candidate **main tonal correction budget** for the shared five-band/advanced-curve transfer and its pixel/region blend, separate from Exposure. The five nonnegative basis weights already sum to one, so bands alone satisfy it. Adding an independent +/-4 EV advanced curve can request +/-8 EV before protection/Strength; the budget policy must explicitly bound that combined request before fitting. The reference experiment can use a declared pointwise clamp and show requested, budgeted and realized values. Whether a later smooth request limiter is worth its loss of directness remains a product decision; do not silently rescale every request. Arbitrary spatial masks and legacy targeted/area/gradient operations are outside this one-dimensional fit and this budget.

For an input interval of width `D`, the output span is at least `max(delta*D, D-2*G)`. With `delta=.10` and `G=4`, a four-stop span can shrink to 0.4 stops, while a 14-stop span retains at least 6 stops. Order preservation can still remove substantial contrast; it is not a perceptual quality guarantee. +/-4 EV also means gains from 1/16 to 16, not a promise of four stops of recoverable detail.

With fixed image/guide inputs, evaluating this transfer in the proposed pixel/region blend preserves the direction of amount edits. Spatially varying guides, source confidence, masks and later operations still need their own analysis. A monotone scalar curve alone does not prove halo-free spatial output.

### B. Separate the region guide from detail behavior

For edge-aware base `b = EdgeAware(E)`, the residual `d = E-b` describes variation around the regional brightness. A region-based correction is:

```text
E_out = F(b) + d
      = E + (F(b) - b)
RGB_out = RGB_in * exp2(F(b) - b)
```

When the base is locally constant, fine log-contrast is retained. This motivates preserving detail rather than directly sharpening it. Base/detail processing is established in [R7]; guided filtering and multiscale alternatives are discussed in [R8]-[R11].

For the proposed Detail retention control `s` in [0,1], compare:

```text
g_pixel  = F(E) - E
g_region = F(b) - b
g_final  = (1-s) * g_pixel + s * g_region
```

This differs from today's `g((1-s)*E + s*b)`. The proposed endpoints have explicit meanings: 0 uses each pixel's tonal position; 1 uses the regional correction. Region size independently controls `b`. At 1 this is still not a guarantee that all texture is retained near boundaries, because `b` itself varies. Do not call this control sharpening or noise reduction.

At 100%, a dark crease with a midtone base receives the midtone-region correction. Preserving local variation therefore changes tonal membership; it does not promise that the same original-histogram shadow pixels receive the same correction. Keep input-fit and regional-control diagnostics distinct. Detail retention can reuse the same guide and must not trigger a rebuild solely to blend these two fields.

### Candidate comparison

| Candidate | Why compare it | Limitation / decision |
| --- | --- | --- |
| Existing eight-scale guide + constrained transfer | Smallest change, known owner/caches, isolates benefits of better curve behavior | Keep as the baseline. Current luminance guide and coupled controls still need evaluation. |
| Exposure-stable guided base/detail | Clear regional interpretation; guided filtering has a practical linear-time formulation [R8] | Preferred challenger. Log/exposure-stable guidance must be specified; the original method can still show halos and miss color-only edges. |
| Fast Local Laplacian | Multiple detail scales and a documented historical Lightroom connection [R3][R9][R10] | Quality reference, then possible replacement if it wins on evidence. More implementation and memory work; no universal artifact guarantee. |
| Weighted least squares decomposition | Useful quality reference for progressive, multiscale separation [R11] | Defer unless the simpler candidates fail the chosen scenes; solving and integration add complexity. |
| Exposure fusion | Can create attractive compression through multiscale blending | Defer: synthetic exposure stacking would complicate scene-linear semantics and slider predictability. No fusion implementation or benchmark is proposed here. |

The darktable tone-equalizer manual is a useful product precedent: it distinguishes a region guide from image brightness, uses an image-relative spatial size, and documents exposure-sensitive versus exposure-independent guidance. Its numbers and implementation are not Stack defaults. [R12]

### C. Preserve color, alpha and numerical meaning

Use the incoming working space's luminance coefficients and one positive scalar RGB gain. Preserve alpha and valid signed RGB components; do not independently clamp negative channels merely to make the logarithm convenient. For nonpositive luminance, use an explicitly documented fallback or leave the local correction neutral; define this consistently across GPU, numerical reference and diagnostics. A log floor belongs to guide construction, not a blanket rewrite of source pixels.

The present histogram explicitly rejects nonfinite RGBA, while guide construction does not share those checks. The application computes floored EV before its alpha/exact-zero bypass; nonzero signed RGB with nonpositive luminance can still receive a gain despite being excluded from positive-luminance fitting. No universal finite-pixel guarantee was established at the incoming texture boundary. This is a source-level contract gap, not a reproduced image failure. Prefer neutral local correction for finite nonpositive-luminance input in the new version, and use the engine's declared invalid-pixel fallback consistently for nonfinite input. Fold validity into existing per-pixel processing/analysis, not a new full-frame scan on every gesture. [C4][C5][C8][C14]

Neutral controls must bypass unnecessary work and produce identity. Nonfinite input/parameter handling must follow the engine's declared policy; nonfinite pixels must not poison statistics or spatial neighborhoods. Evaluate permitted gains jointly with source magnitude and target precision. The renderer's finite numeric range is not a photographic quality guarantee.

Common gain preserves linear RGB ratios where defined. It does **not** prove identical perceived color after strong compression and display mapping; appearance experiments in [R13] demonstrate why those are different claims. Keep gamut mapping and SDR/HDR adaptation with the View/output owner. Do not silently desaturate Local EV to conceal an output-gamut issue.

## 6. Limits and panel design

### Proposed first prototype limits

| Setting | Proposed first-pass choice | Status |
| --- | --- | --- |
| Exposure | Keep visible -16 to +16 requested EV and existing positive shoulder | Existing range retained; no expansion. |
| Five tonal controls | Keep -100 to +100 normalized amount | Familiar range; effects constrained jointly. |
| Main tonal correction | Start with a declared +/-4 EV request budget for the shared transfer/blend, excluding Exposure and legacy spatial operations | Bands already satisfy it; combined advanced requests need explicit limiting and feedback. |
| Scalar slope floor | 0.10 | Candidate to compare against local-contrast loss. |
| Scalar smoothing | Fixed 0.25 EV half-width, 257 uniform knots in the bands-only study | Reference configuration; native sample domain must also cover the advanced curve and protection transitions. |
| Region size | 0.5%-10% of the full incoming image's longer side; default 2% | Exploratory filter-scale range, not measured or copied from another product. |
| Detail retention | 0%-100%; start at 100% in the new-version prototype | Exact field-blend meaning in section 5; photograph checks may change the default. |
| Edge behavior | Automatic boundary preservation first | Keep algorithm tuning out of the main panel; expose an expert control only if it has a predictable effect. |

Region size denotes an algorithm-specific **characteristic scale**, whose precise interpretation must be selected with the filter. It is not currently a promised radius, diameter or dependency support. Convert it to full-image source coordinates once; expose actual support separately in the prepared plan, including every repeated pass and sampling footprint. Never use panel width, zoom level or resident tile size. A preset must retain the same geometric meaning at another resolution.

Avoid a second “extreme mode” in the initial UI. A well-conditioned full range is simpler. Exact entry can reach the existing supported extremes; ordinary dragging should favor precise edits. A prototype interaction can use an explicit exposure-per-scaled-pixel sensitivity with Shift refinement, rather than coupling drag speed to the panel's width. Tune this on the native control before selecting a shipping value.

### The panel concept

Open [panel-concept.html](panel-concept.html) locally. It shows the six controls, exact numeric entry, a selected control's intended job, and collapsed spatial settings. The range strip is **illustrative tonal reach**, not measured image analysis. Its controls do not process an image, validate a tone curve, or simulate the proposed renderer. This keeps the UI discussion inspectable without pretending image-quality work is finished.

The intended native design retains quiet tracks, center ticks, numeric values and existing reset/cancel behavior. Primary controls stay compact. Settings hold Region size, Detail retention and optional Hold tone targets. The latter freezes accepted target anchors, not a raster guide; changing the actual source or geometry still invalidates analysis. Persisting a held target must use normal node edits/history, including its reference domain, so save/load and export agree.

### The scalar response study

Open [response-study.html](response-study.html) locally. It executes the current five-band scalar formula and the selected constrained reference using the same requests. Compare four-stop and 14-stop fitted spans, move individual amounts, or select Neutral, Shadows +100, Opposing amounts and All +100. The plot shows input EV against output EV, the identity guide, minimum slopes and inspected outputs.

The study deliberately isolates the mathematical decision: **bands only, no image processing**. Exposure, highlight protection, Strength changes, advanced curves, spatial guides and masks are excluded. It is not a Lightroom emulator, photograph comparison, final native solver or validation of the proposed tail semantics. All +100 remains a uniform +4 EV shift in both displayed models; conflicting neighboring amounts show why constrained fitting changes the requested response.

Native follow-up diagnostics should offer **Affected regions**, **Actual gain in stops**, and explicitly named **SDR output / HDR display limits** through existing inspection surfaces. Do not report unavailable source confidence as zero or invent a percentage of “recoverable detail.” Requested correction, realized gain and clipped input evidence are different things. These diagnostics are proposed; the HTML does not fabricate them.

## 7. Source awareness and ideas worth keeping

**Noise-aware lifting:** a future optional protection could temper shadow gain using upstream variance/support evidence. Multiplying signal and noise does not improve capture SNR. Missing evidence must yield an honest luminance-only fallback. Do not classify faint texture or stars as noise from contrast alone. The current `SceneColorGuide` has reliability/variance/support contracts but is not wired into the ordinary five-band path; its reliability is not a calibrated probability. [C13]

**Small bright lights:** first preserve a sensible upper-tail slope. Later compare a small-bright-feature protection for headlights, chrome and stars. It is a heuristic, not material recognition; white texture and bright sources can overlap in these statistics. Make its influence inspectable and disableable.

**Color boundaries:** compare a color-aware guide where different colors have similar luminance. Use reliable upstream evidence when available, preserve boundary provenance, and fall back explicitly when the graph loses that evidence. Do not treat chroma noise as a trusted edge. Guided-filter research illustrates the limits of gray guidance. [R8]

**Consistent sequences:** Hold tone targets freezes one classification reference, not the complete spatial operator or all histogram-dependent behavior. RawTherapee explicitly documents panorama seams from image-adaptive compression with identical settings. [R19] Do not promise sequence consistency from five held anchors. Do not derive analysis from the node's own output or visible crop. A future shared sequence reference needs explicit source/geometry ownership; this PR creates no batch system.

**Spatial intent without manual painting:** an on-image sample could select the region/tonal target and show its affected area. Reuse Stack's existing mask/selection authoring instead of reviving the retired Local EV painting editor. Keep texture/clarity enhancement in its own operation; preservation and enhancement have different goals.

## 8. Architecture and compatibility plan

Create the new transfer/parameter preparation under the processing owner, separate from ImGui. Keep a small supported contract that produces immutable prepared data: version, input identity/domain, fitted/held targets, transfer coefficients, spatial settings, and any constraints applied. The renderer owns textures and context-bound lifetime; the editor owns draft gestures and display selection.

Separate four prepared products inside the existing owners:

| Product | Immutable content and reuse boundary |
| --- | --- |
| Input statistics | Upstream identity, working-space/luminance and validity policy, EV reference/middle grey, full geometry, canonical sampling policy, accepted/invalid counts, fitted or held targets. Reuse across amount and detail-retention edits. |
| Spatial guide plan/result | Input identity, algorithm/version, source-coordinate scale, processed grid/origin, edge settings, optional evidence identity, actual dependency support, working/retained bytes and quality. Reuse across amount edits and an after-guide detail blend. |
| Prepared tonal transfer | Version, target identity, fixed sample domain/weights, authored amounts/advanced curve/Strength/protection, request budget, fitted coefficients, tails and requested/effective differences. Rebuild cheaply when tonal intent changes. |
| Image result | Relevant preceding identities, Detail retention, later Exposure policy/value, masks, geometry and declared quality. Invalidate for every pixel-affecting change. |

The current graph fingerprint serializes the complete operation. Preserve correct end-to-end invalidation while improving internal reuse; do not omit a setting merely because it does not rebuild a guide. UI diagnostics consume prepared engine data with normal project/node/revision checks. The standalone research formula must not become a second production formula in widgets. [C16]

| Boundary | Required behavior |
| --- | --- |
| Recipe / graph | Add a dedicated version to the relevant `RawLocalRangeRecipe` representation, `LocalRangeJson` and deserialization, covering nested recipes that can execute it. Missing means legacy behavior, including old protection semantics. Unknown versions fail explicitly in the new reader. |
| Upgrade | Explicit per-node action through normal project edits and undo. Retain all authored legacy values and saved area/gradient data; avoid irreversible reinterpretation. Nodes with legacy targeted/area/gradient operations remain legacy until their combined migration is specified; explain an unavailable upgrade. New nodes can adopt the new version after validation. |
| Panel / floating graph | Both author the same draft and use the same limits/units. Respect graph-driven values. No per-frame image scanning or duplicate processing formulas in widgets. |
| Reference / GPU | Establish one authoritative scalar transfer and intentional CPU/GPU parity checks. The present sample helper is not a full reference renderer. Share generated coefficients where possible. |
| Analysis / caching | Reuse upstream-only fits and guides across tonal-amount edits. Include version, working space, full image geometry, spatial settings and evidence identity in relevant keys. |
| Viewport | Use complete incoming evidence and full-image coordinates. Any future tiles need a shared full-frame fit, full-cascade padding, original guidance over that padding and edge extension only at true image boundaries. Crop after the necessary passes, with alpha/validity and upstream resampling accounted for. |
| Scheduling | Keep processing on existing workers, coalesce drag edits, reject stale project/node/revision results, and retain accepted imagery. No new UI-thread waits or full-frame readbacks. |
| Memory | Account for all guide work fields and the old guide during replacement using existing admission. Do not retain a guide per slider. Memory deferral preserves accepted output and cannot silently change the authored operator. |
| Preview / export | Allow declared sampling approximations while preserving operator semantics, guide scale and target anchors. Native/export use the same version and constraints. |
| Diagnostics | Request only when visible/needed through existing analysis. Preview overlays remain presentation, absent from saved/exported pixels. |

The relevant owners are the existing Raw recipe/graph parameter preparation, Renderer Light analysis/spatial field, `PhotographicPanelEditors`, `LightBandEditor`, and node-edit host. Avoid placing a new solver in either editor or expanding the large shader source file with another unrelated subsystem. Follow [implementation guidance](../../Guidance/Implementation/README.md) for the eventual code change.

Do not repurpose `areasVersion` or the RAW pipeline's `technical.processingVersion` for this contract. Upgrading a main Local EV recipe must not silently upgrade a nested gradient recipe. Missing-version compatibility requires preserving the complete legacy path, including the currently inert setting. Older binaries ignore unfamiliar Local EV fields, so adding a new field alone cannot make those older readers reject new semantics: the implementation also needs the application's forward-compatibility policy. [C6][C7]

## 9. Bounded next pass and acceptance questions

### Implementation sequence

1. **Contract cleanup:** version behavior; reconcile Strength limits and visible settings; establish the shared effective-transfer representation. Preserve old projects.
2. **Constrained transfer:** prepare the versioned scalar transfer on the existing guide. Include advanced/protection requests in the same preparation, expose requested/effective differences, and verify both tone order and amount direction. This is the smallest meaningful native slice.
3. **Spatial comparison:** separate region size/detail retention and compare the existing guide with one exposure-stable guided candidate. Bring in fast Local Laplacian only if that comparison exposes a concrete quality gap.
4. **Native panel:** connect the approved controls and real diagnostics to the existing draft/history/worker paths. Select defaults using the limited photo comparison.

Before extensive image validation, agree on its scope under [AGENTS.md](../../AGENTS.md). Suggested initial scope is the existing scalar/serialization checks plus **six supplied photographs**, one pass each at ordinary and strong settings. No new broad synthetic-data framework or repeated benchmark sweep is implied.

| Focused case | Evidence to inspect |
| --- | --- |
| Scalar transfer and adjacent opposing controls | Neutral/uniform behavior; positive slope including joins/tails; nondecreasing amount response with fixed targets; finite extremes; combined advanced curve/protection and declared budget. |
| Bright window and dark frame | Halo/overshoot, edge width, preserved interior/exterior texture. |
| Fine branches against sky | Thin-feature leakage, halos, guide-scale behavior. |
| Deep noisy shadow with real texture | Visibility of noise versus genuine detail; no invented recovered information. |
| Saturated lights, skin or colored reflections | Signed-channel behavior, RGB ratio continuity, perceived color after the actual View transform. |
| Fog or another low-contrast scene | Preserve intentional softness; avoid forcing every image to full contrast. |
| Broad HDR cloud plus a small bright source | Bright-region separation, upper-tail continuity, values above SDR white. |

Use the same selected image to check proxy/native agreement and overlapping viewport crops; do not expand this into a screenshot matrix. Review the existing cancellation/history/save tests that exercise the changed contracts. A bounded Windows build and an actual GPU-path check are needed before claiming a native implementation works; research math alone cannot establish that.

Still to decide from evidence: finite tail-transition shape, contrast floor, whether a smooth request-budget transition improves interaction, default spatial scale, whether one guide is sufficient, and whether optional source confidence improves shadows without suppressing valid content. The scalar reference resolves a mathematical contract; it cannot choose photographic defaults. These are implementation experiments, not unfinished work in this research PR.

## 10. Verification record and limits

- Read current model, sanitization, panel/floating controls, CPU helper, GPU guide/application, Exposure placement and analysis ownership.
- Distinguished active paths from unused shader helpers and legacy saved-area behavior.
- Derived the extreme-tone example directly from the current cosine basis; did not claim a running-image reproduction.
- Reviewed primary Adobe documentation, a firsthand engineer statement, author-hosted papers, and official darktable documentation.
- Independent source and design reviews covered active paths, the revised scalar construction, tail semantics, cumulative support, allocation lifetime and input/guide domains. Their clarifications are incorporated.
- Concept JavaScript parses; static selectors/IDs and six control ranges were checked. Its actual script passed an in-memory DOM event-handler smoke check for slider/number synchronization, exact-entry bounds, selected-control intent/reach, reset and spatial settings. This is not a browser rendering test.
- The response study's own pure model supplies the plotted curves and arithmetic checks. Four presets at each of the two spans passed finite-value, +/-4 EV bound, neutral-identity, uniform-shift and minimum-slope checks within `1e-10` tolerance. The reference minimum is computed at every derivative breakpoint plus the exterior slope, not estimated from chart samples.
- The study's actual script passed in-memory event-handler checks for sliders, presets, fitted spans, exact EV inspection, hover coordinates and series visibility. Focused out-of-range entry reflects the clamped value, live status distinguishes reversal/identity/uniform shifts, and Escape dismisses and unpins the tooltip. Fragment and standalone content match; both are self-contained. This verifies script behavior, not browser layout or accessibility in a running browser.
- An independent check raised Midtones from -100 to -99 in the 14-stop opposing case. Across 257 inspected positions including tails, output change ranged from 0 to +0.02284576 EV; no value darkened. The two reference minima remained 0.10 within floating-point tolerance. This is a bounded example supporting the stated construction, not an exhaustive proof by testing.
- All 39 evidence IDs, 22 unique pinned code paths, relative destinations and Markdown fences checked. No unresolved references found.
- Optional headless layout verification was blocked by the environment's browser socket restriction; the requested escalation was automatically rejected. No browser workaround or computer-use tool was used. Rendered layout and native browser interaction remain unverified.
- No C++/GLSL, processing defaults, build configuration or persistence code changed. No Windows build, GPU image run, photographic benchmark or new automated image suite was performed for this design pass.

Scalar study results, rounded; negative slope means reversed tone order:

| Preset | Fitted span | Current minimum slope | Reference minimum slope |
| --- | ---: | ---: | ---: |
| Neutral | 4 / 14 stops | 1.000 / 1.000 | 1.000 / 1.000 |
| Shadows +100 | 4 / 14 stops | -5.283 / -0.795 | 0.100 / 0.100 |
| Shadows +100, Midtones -100 | 4 / 14 stops | -11.566 / -2.590 | 0.100 / 0.100 |
| All +100 | 4 / 14 stops | 1.000 / 1.000 | 1.000 / 1.000 |

Neutral is identity and All +100 retains +4 EV uniformly in this bands-only configuration. These are scalar results, not measured image quality or a claim that the complete current native pipeline always has these slopes.

## Sources

All web sources accessed November 20, 2026. Product documentation and first-person statements support the scoped product claims above. Algorithm references support methods and caveats; the scalar construction, proposed Stack settings and architecture are our design conclusions. Documentation for a named product/version is not proof of another product's implementation.

### Current Stack source, pinned to the audited commit

- [C1] [Light bands, interpolation and percentile fit](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/Curves/LightBands.h).
- [C2] [Panel controls and settings](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Editor/NodeEditors/PhotographicPanelEditors.cpp), `DrawLightPanel` / `DrawLightPanelSettings`; [floating settings](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Editor/Internal/RawLab/EditorModuleRawLabZones.cpp).
- [C3] [Light analysis and guide cache](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelineLightAnalysis.cpp), `PrepareLightAnalysis`.
- [C4] [Spatial field execution](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelineSceneDetail.cpp) and [shaders](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/SceneDetailShaders.h).
- [C5] [Local EV shader and scope](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelinePrograms.cpp), `edgeAwareSceneEv`, `protectedLocalDeltaEv`, `EnsureRawDevelopmentLocalRangeProgram` and scope use.
- [C6] [Recipe defaults](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/RawDevelopmentRecipe.h), `RawLocalRangeRecipe`; [serialization and sanitization](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/RawDevelopmentRecipe.cpp), `LocalRangeJson` / `SanitizeLocalRangeRecipe`.
- [C7] [Graph parameter limits](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/RawGraphParameters.cpp) and [enforced operation policies](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/RawGraphOperation.cpp).
- [C8] [Exposure shoulder](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelineRawPlacement.cpp) and [Local EV orchestration](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelineRawOperation.cpp).
- [C9] [CPU sample helper](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/RawDevelopmentRecipe.cpp), `LocalRangeEdgeAwareWeight` / `EdgeAwareLocalRangeDeltaEvForSamples`.
- [C10] [Requested curve plot](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Editor/NodeEditors/LightBandEditor.cpp), `DrawLightBandGraph`.
- [C11] [Panel interaction](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Editor/NodeEditors/PanelRoller.cpp).
- [C12] [Viewport dependency planning](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/GraphViewportPlanning.cpp) and [analysis cache storage](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/RenderPipeline.h).
- [C13] [Source guide contract](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/SceneColorGuide.h) and [current area-path integration](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelineRawOperation.cpp).
- [C14] [Histogram admission and EV bins](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/LightRangeAnalysis.h), finite/alpha/luminance checks.
- [C15] [Eight-scale sequence](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Raw/Detail/DetailContrast.cpp), `DetailBandScale`.
- [C16] [Image execution fingerprint](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelineGraphExecutionFingerprintImage.cpp) and [retained texture accounting](https://github.com/CharmsMods/Stack/blob/aa972c3fcdf45470d490ce9a1f043431dfbb05ea/src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp).

### Primary research and product sources

- [R1] Adobe, [Image tone and color](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/image-tone-color.html), documented control roles, including PV2012 behavior.
- [R2] Adobe, [Tone Control Adjustment Procedure](https://helpx.adobe.com/lightroom-classic/desktop/help/tone-control-adjustment.html), image-adaptive controls; historical PV3-PV5 scope.
- [R3] Samuel W. Hasinoff, [author/product page](https://people.csail.mit.edu/hasinoff/), Lightroom 4 / Photoshop CS6 research lineage.
- [R4] Eric Chan / MadManChan2000, Adobe Employee, [May 30, 2012 engineering reply](https://community.adobe.com/bug-reports-562/p-artefacts-when-using-shadows-clarity-and-image-warp-lens-corrections-666759), historical edge-aware behavior and tradeoffs.
- [R5] Adobe, [Process versions in Camera Raw](https://helpx.adobe.com/camera-raw/desktop/get-started/overview-and-setup/process-versions.html), November 3, 2023.
- [R6] Eric Chan, [High Dynamic Range Explained](https://blog.adobe.com/en/publish/2023/10/10/hdr-explained), October 10, 2023.
- [R7] Durand and Dorsey, [Fast Bilateral Filtering for the Display of High-Dynamic-Range Images](https://people.csail.mit.edu/fredo/PUBLI/Siggraph2002/), SIGGRAPH 2002. Its display normalization is not proposed for Local EV.
- [R8] He, Sun and Tang, [Guided Image Filtering](https://people.csail.mit.edu/kaiming/publications/pami12guidedfilter.pdf), TPAMI 2013; sections 3.4 and 5 include relevant behavior and limitations.
- [R9] Paris, Hasinoff and Kautz, [Local Laplacian Filters](https://people.csail.mit.edu/hasinoff/pubs/ParisEtAl11-lapfilters-lowres.pdf), SIGGRAPH 2011; sections 5.2-5.4 discuss noise, tone mapping and failure cases.
- [R10] Aubry et al., [Fast Local Laplacian Filters: Theory and Applications](https://imagine.enpc.fr/~aubrym/projects/llf/index.html), TOG 2014. Reported acceleration is paper-specific, not a Stack performance estimate.
- [R11] Farbman et al., [Edge-Preserving Decompositions for Multi-Scale Tone and Detail Manipulation](https://pages.cs.huji.ac.il/danix/epd/index.html), SIGGRAPH 2008.
- [R12] darktable, [tone equalizer manual](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/), development documentation as accessed; guide semantics, scale and exposure sensitivity.
- [R13] Mantiuk et al., [Color Correction for Tone Mapping](https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk09cctm.pdf), Eurographics 2009; linear ratios and perceptual appearance are different constraints.
- [R14] Adobe, [Eric Chan discusses his role in improving Photoshop](https://blog.adobe.com/en/publish/2013/07/12/principal-scientist-and-mad-man-eric-chan-discusses-his-role-in-improving-photoshop), July 12, 2013; firsthand PV2012 control-design account.
- [R15] Adobe, [Adjust shadow and highlight detail](https://helpx.adobe.com/nz/photoshop/using/adjust-shadow-highlight-detail.html), November 15, 2022; Photoshop command, neighborhood/range distinction and extreme-value crossover warning.
- [R16] darktable 5.6 manual, [filmic rgb](https://docs.darktable.org/usermanual/5.6/en/module-reference/processing-modules/filmic-rgb/); scene/display endpoints, reconstruction and curve-shape controls.
- [R17] darktable 5.6 manual, [shadows and highlights](https://docs.darktable.org/usermanual/5.6/en/module-reference/processing-modules/shadows-and-highlights/); older Lab/display module's radius, reach and artifact tradeoffs.
- [R18] RawTherapee, [Shadows/Highlights](https://rawpedia.pixls.us/shadows--highlights/), modified February 8, 2026; documented engine introduced in 5.5, tonal width and radius.
- [R19] RawTherapee, [Dynamic Range Compression](https://rawpedia.pixls.us/dynamic_range_compression/), modified February 28, 2026; amount/detail/anchor and panorama consistency caveat.
- [R20] RawTherapee, [Tone Mapping](https://rawpedia.pixls.us/tone_mapping/), modified October 25, 2026; documented WLS-derived operator and preview/export dependence. Some behavior is explicitly historical, since 4.2.156.
- [R21] SciPy, [isotonic_regression](https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.isotonic_regression.html); weighted least-squares objective, PAVA and pooled blocks.
- [R22] A. B. Nemeth and S. Z. Nemeth, [Isotonic regression and isotonic projection](https://arxiv.org/pdf/1503.08458), June 2, 2015 version; coordinatewise projection property, including weighted regression.
- [R23] SciPy, [PchipInterpolator](https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.PchipInterpolator.html); shape-preserving interpolation and data-dependent derivative construction.
