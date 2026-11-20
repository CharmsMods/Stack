# Local EV / Light: extreme adjustments and spatial behavior

**First pass: research, design decisions, and an interactive panel concept.**

Audited against Stack `aa972c3fcdf45470d490ce9a1f043431dfbb05ea` (1.2.167), on November 20, 2026. This record and [panel concept](panel-concept.html) are the deliverables. Merging this PR does not change image processing, saved recipes, or the native controls. A nearby Guidance correction describes the slider that already exists.

## 1. Recommended direction

Improve the existing spatial engine rather than treating Local EV as a new feature. Give Shadows and Highlights the job of changing broad dark and bright regions while retaining their internal detail. Give Blacks and Whites distinct lower-tail and upper-tail behavior. Keep Midtones as a central brightness control. Make extreme settings predictable by constraining the resulting transfer, not by choosing a smaller arbitrary slider maximum.

The immediate priorities for a later implementation are:

1. Resolve the visible **Detail protection** control's disconnected GPU behavior.
2. Prevent accidental tonal reversals and reveal when combined adjustments are limited.
3. Separate **region size** from **detail retention**; Smoothness currently couples region size with pixel/guide interpolation.
4. Share the processing contract with the panel, floating graph, diagnostics, and export.
5. Version the new behavior so existing projects retain their appearance.

Keep the familiar six controls and their current visible ranges. The five tonal numbers remain normalized amounts, not exact stops. Exposure continues to follow Light; positive Exposure retains the existing highlight shoulder. New processing should be an explicit version, with an opt-in upgrade for old nodes.

This is a design decision record, not a claim that the proposed filter or numerical defaults have passed photographic validation. **Verified** below means source inspection or the stated arithmetic. **Proposed** means a Stack design choice. Research sources establish general methods and documented product behavior, not unpublished Adobe code.

## 2. What Stack actually does today

### The active path

Local EV already measures incoming luminance, fits tonal targets, and builds a multiscale edge-aware log-luminance guide. Smoothness blends each pixel's own exposure value toward that guide. The five bands and the advanced curve select an exposure correction using this control signal. Processing multiplies the original RGB by one gain; it does not directly blur RGB. Saved area/gradient operations can then run, Exposure follows, and the outer node mask blends the result. [C1][C3][C4][C5]

The current guide is a repeated, separable five-tap filter over up to eight increasing scales, with edge weights derived from original luminance. It is not a Local Laplacian pyramid reconstruction. The guide normalizes valid signal and rejects nonpositive or alpha-zero samples. Preserve these useful contracts. [C4]

| Control or mechanism | Current behavior | Consequence for the redesign |
| --- | --- | --- |
| Exposure | Panel -16 to +16 EV; renderer accepts finite -32 to +32. Positive values use a protected shoulder; negative values use uniform gain. | The displayed EV is a requested exposure change, not an identical change to every bright pixel. Preserve ordering and make that distinction clear. |
| Five tonal amounts | -100 to +100, initially zero. Each anchor requests up to +/-4 EV. Raised-cosine bases interpolate adjacent anchors. | These are five locations in one correction field, not five independently defined photographic operators. |
| Tonal targeting | Input histogram spans -32 to +32 EV in 1/16-stop bins. Fit uses the 1st/99th percentiles and at least a four-stop span. | Small bright populations do not set the fit; they still receive the outer band's correction. |
| Final main correction | Bands, advanced curve and targeted contributions ultimately clamp to +/-4 EV. | Strong combinations can reach an invisible ceiling. The individual controls may keep moving. |
| Smoothness | 0 to 1, default 0.65. Maximum source-pixel filter scale is `16 + 240 * smoothness`; the same value controls pixel/guide interpolation. | At the default, scale is 172 source pixels. Scale and amount are coupled. |
| Edge protection | 0 to 1, default 0.75; affects guide range weighting. | Keep boundary preservation separate from spatial extent. |
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

Analysis is already cached from upstream content, geometry and working space. Guide identity also includes spatial settings and native dimensions; band-amount edits reuse it. Four scalar fits can be retained, but only one full-resolution guide. Guide construction uses three RG32F fields, approximately **24 bytes per processed pixel**, excluding input/output and other allocations. Active Local EV requires complete input evidence in viewport planning. Preserve these boundaries; do not add per-drag full-frame scans or fit the visible viewport. [C3][C4][C12]

## 3. What the Lightroom evidence supports

| Evidence | Finding and limits | Design consequence |
| --- | --- | --- |
| Adobe tone/color help [R1] | Exposure, Highlights/Shadows and Whites/Blacks have distinct documented jobs. The latter explicitly adjust clipping endpoints. | Avoid five differently named copies of the same control. Stack's scene-linear tails need their own contract rather than copying output clipping. |
| Adobe's adjustment procedure [R2] | Describes PV3-PV5 controls as image-adaptive and interacting. Its percentage regions are teaching aids, not published mask equations. | Research content adaptation and combinations; do not hard-code those percentages as recovered Adobe behavior. |
| Hasinoff's author statement [R3] | Lightroom 4 / Photoshop CS6 Shadows, Highlights and Clarity were inspired by Local Laplacian research. | Strong historical motivation for multiscale comparison; it does not reveal the current shipping implementation. |
| Adobe engineer Eric Chan, May 30, 2012 [R4] | Described edge-aware controls and expensive halo-suppression masking; cited strong settings and branches against bright sky as difficult. | Include thin boundaries and strong settings in later checks. This is historical evidence, not a claim about a current Lightroom bug. |
| Adobe process-version reference [R5] | Documents appearance-affecting processing generations, including the 2012 tone redesign and later low-light improvements. | Preserve old Stack rendering explicitly. |
| Eric Chan's HDR explanation, 2023 [R6] | HDR editing extends beyond SDR white; display capability and SDR rendering matter separately. | Separate captured clipping, scene headroom, and output/display limits. |

The evidence supports **image-adaptive, edge-aware tone redistribution**, not an exact Lightroom clone. There is no verified universal Lightroom EV equivalent for +100, fixed neighborhood radius, or current internal threshold at +/-50 in this research. Do not turn repeated forum explanations into implementation facts.

Two reading cautions: [R2] contains an inconsistent process-version heading; [R5] is authoritative for PV3 = 2012. Top-down adjustment advice describes a workflow, not proof that identical final settings depend on the chronology of slider gestures. In [R4], the engineer also says a suggested progressive rendering system would not solve that particular artifact; progressive preview is not a halo cure.

## 4. Proposed meaning of each control

The aim is **changing illumination relationships while retaining useful structure**. Equal-luminance pixels can deserve different changes: a dark crease on a bright shirt should often move with the shirt, while a broad dark wall should respond to Shadows. A neighborhood guide can help make this distinction; it does not identify objects or understand materials.

| Control | Proposed intent | Positive / negative behavior | Boundary |
| --- | --- | --- | --- |
| Exposure | Overall brightness, retaining Stack's existing protected positive response | Brighter / darker; use the current monotone shoulder after Light | Requested EV, not a promise of uniform highlight gain. Pure uniform exposure remains a separate graph operation. |
| Blacks | Deep-tone anchor and toe separation | Open the deepest nonzero tones / deepen them without collapsing ordinary shadows | Preserve exact zero; do not alter sensor black calibration or silently add a gray output floor. |
| Shadows | Illumination of dark regions | Reveal / deepen broad dark material while retaining its texture | Do not target every tiny dark edge in a bright region equally. Lifting noise is not recovering signal. |
| Midtones | Central regional brightness | Raise / lower central tones with smooth joins to both tails | This is not Contrast; avoid adding a second contrast or clarity control in this pass. |
| Highlights | Shape and separation of broad bright regions | Open / compress bright regions while preserving visible variation | Do not promise reconstruction of clipped camera channels. |
| Whites | Upper-tail placement and separation | Extend / restrain the brightest tail, preserving HDR headroom | Do not clamp scene RGB at 1. Diffuse white, reflections and emitters are not interchangeable. |

**Important distinction from Lightroom:** Adobe documents clipping-oriented Blacks/Whites. Stack's proposed controls remain scene-linear tail shapers; actual output clipping belongs to the View/output stage. Names can remain familiar if the selected-control explanation and diagnostics make the behavior clear.

Implement these as coordinated constraints/targets for one transfer, not sequential, noncommuting filters attached to each slider. Zero must be identity. Every slider must remain useful when its neighbors move; the solver may soften an impossible request, but the panel must be able to show the effective result.

The first implementation candidate should keep normalized amounts in [-100,100], map them to requested regional/tail targets, and solve those targets jointly. Do not advertise -100 as black, +100 as white, or +50 as a fixed number of recovered stops. Endpoint roles, transition width and shoulder/toe shapes require comparison on photographs before becoming defaults.

## 5. Processing contract and algorithm candidates

### A. Constrain the transfer before improving the filter

Let `E = log2(Y / middleGrey)` for valid positive working-space luminance. If the requested exposure field is `g(E)`, the pointwise transfer is `F(E) = E + g(E)`.

Require a strictly positive derivative in the protected version, with a floor `delta` in `(0,1]`. A proposed initial contrast floor is `F'(E) >= 0.10`; **0.10 is a prototype tuning value**, not a perceptual guarantee. Include the advanced correction and brightness-dependent protection in the requested field before enforcing the constraint. Checking each band separately is insufficient.

One concrete solver candidate:

1. Prepare requested output knots `y_i = E_i + g_requested(E_i)` from all recipe terms expressible as the shared scalar transfer. Arbitrary spatial masks and legacy area operations are outside this one-dimensional solver.
2. Fit the closest output knots under `y_(i+1) - y_i >= delta * (E_(i+1) - E_i)` and the chosen anchor/tail constraints.
3. Interpolate `z_i = y_i - delta * E_i` with a monotonicity-preserving interpolant, then reconstruct `F(E) = z(E) + delta * E`. A generic cubic spline is insufficient.
4. Define continuous positive-slope tails outside the fitted interval. Never clip input coordinates and mistake the resulting constant correction for an endpoint policy.
5. If a bounded correction is required, use a continuous soft bound `B` with `B(0) = 0` and derivative between zero and one; prefer a symmetric, sign-preserving bound. For `g = B(F(E)-E)`, that derivative condition preserves the scalar slope floor. Verify the complete interpolation/tail implementation, including between knots.

Retain +/-4 EV as the first candidate **main tonal correction budget** for the shared five-band/advanced-curve transfer and its pixel/region blend, separate from Exposure. This is not an aggregate limit on subsequent legacy targeted/area/gradient operations. Do not raise it merely because a slider feels unresponsive. A soft transition toward that budget should replace invisible hard saturation in the new version. Record requested versus effective correction in prepared analysis. The exact soft transition is an experiment; its selected form and coefficients must be versioned.

This proof concerns the scalar transfer. Spatially varying guides, source confidence, region masks and outer blending can change image gradients. A monotone scalar curve alone does not prove halo-free spatial output.

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

Neutral controls must bypass unnecessary work and produce identity. Nonfinite input/parameter handling must follow the engine's declared policy; nonfinite pixels must not poison statistics or spatial neighborhoods. Evaluate permitted gains jointly with source magnitude and target precision. The renderer's finite numeric range is not a photographic quality guarantee.

Common gain preserves linear RGB ratios where defined. It does **not** prove identical perceived color after strong compression and display mapping; appearance experiments in [R13] demonstrate why those are different claims. Keep gamut mapping and SDR/HDR adaptation with the View/output owner. Do not silently desaturate Local EV to conceal an output-gamut issue.

## 6. Limits and panel design

### Proposed first prototype limits

| Setting | Proposed first-pass choice | Status |
| --- | --- | --- |
| Exposure | Keep visible -16 to +16 requested EV and existing positive shoulder | Existing range retained; no expansion. |
| Five tonal controls | Keep -100 to +100 normalized amount | Familiar range; effects constrained jointly. |
| Main tonal correction | Start with a soft +/-4 EV budget for the shared transfer/blend, excluding Exposure and legacy spatial operations | Candidate based on the existing bound, not a universal safe limit. |
| Scalar slope floor | 0.10 | Candidate to compare against local-contrast loss. |
| Region size | 0.5%-10% of the full incoming image's longer side; default 2% | Exploratory filter-scale range, not measured or copied from another product. |
| Detail retention | 0%-100%; start at 100% in the new-version prototype | Exact field-blend meaning in section 5; photograph checks may change the default. |
| Edge behavior | Automatic boundary preservation first | Keep algorithm tuning out of the main panel; expose an expert control only if it has a predictable effect. |

Region size denotes the filter's characteristic radius/support scale, not its full diameter. Convert it to source coordinates once; each candidate must document its actual effective support, including repeated passes. Never use panel width, zoom level or the resident tile size. Presets that specify a radius must retain the same semantics when used at another resolution.

Avoid a second “extreme mode” in the initial UI. A well-conditioned full range is simpler. Exact entry can reach the existing supported extremes; ordinary dragging should favor precise edits. A prototype interaction can use an explicit exposure-per-scaled-pixel sensitivity with Shift refinement, rather than coupling drag speed to the panel's width. Tune this on the native control before selecting a shipping value.

### The panel concept

Open [panel-concept.html](panel-concept.html) locally. It shows the six controls, exact numeric entry, a selected control's intended job, and collapsed spatial settings. The range strip is **illustrative tonal reach**, not measured image analysis. Its controls do not process an image, validate a tone curve, or simulate the proposed renderer. This keeps the UI discussion inspectable without pretending image-quality work is finished.

The intended native design retains quiet tracks, center ticks, numeric values and existing reset/cancel behavior. Primary controls stay compact. Settings hold Region size, Detail retention and optional Hold tone targets. The latter freezes accepted target anchors, not a raster guide; changing the actual source or geometry still invalidates analysis. Persisting a held target must use normal node edits/history, including its reference domain, so save/load and export agree.

Native follow-up diagnostics should offer **Affected regions**, **Actual gain in stops**, and explicitly named **SDR output / HDR display limits** through existing inspection surfaces. Do not report unavailable source confidence as zero or invent a percentage of “recoverable detail.” Requested correction, realized gain and clipped input evidence are different things. These diagnostics are proposed; the HTML does not fabricate them.

## 7. Source awareness and ideas worth keeping

**Noise-aware lifting:** a future optional protection could temper shadow gain using upstream variance/support evidence. Multiplying signal and noise does not improve capture SNR. Missing evidence must yield an honest luminance-only fallback. Do not classify faint texture or stars as noise from contrast alone. The current `SceneColorGuide` has reliability/variance/support contracts but is not wired into the ordinary five-band path; its reliability is not a calibrated probability. [C13]

**Small bright lights:** first preserve a sensible upper-tail slope. Later compare a small-bright-feature protection for headlights, chrome and stars. It is a heuristic, not material recognition; white texture and bright sources can overlap in these statistics. Make its influence inspectable and disableable.

**Color boundaries:** compare a color-aware guide where different colors have similar luminance. Use reliable upstream evidence when available, preserve boundary provenance, and fall back explicitly when the graph loses that evidence. Do not treat chroma noise as a trusted edge. Guided-filter research illustrates the limits of gray guidance. [R8]

**Consistent sequences:** Hold tone targets could help edits remain consistent across related frames. Do not derive analysis from the node's own output or from the visible crop. Automatic changes in a histogram are not scene understanding. A future sequence reference needs explicit source/geometry ownership; this PR creates no batch system.

**Spatial intent without manual painting:** an on-image sample could select the region/tonal target and show its affected area. Reuse Stack's existing mask/selection authoring instead of reviving the retired Local EV painting editor. Keep texture/clarity enhancement in its own operation; preservation and enhancement have different goals.

## 8. Architecture and compatibility plan

Create the new transfer/parameter preparation under the processing owner, separate from ImGui. Keep a small supported contract that produces immutable prepared data: version, input identity/domain, fitted/held targets, transfer coefficients, spatial settings, and any constraints applied. The renderer owns textures and context-bound lifetime; the editor owns draft gestures and display selection.

| Boundary | Required behavior |
| --- | --- |
| Recipe / graph | Add a dedicated processing version. Missing version means legacy behavior, including old protection semantics. Unknown versions must not silently select the newest renderer. |
| Upgrade | Explicit per-node action through normal project edits and undo. Retain all authored legacy values and saved area/gradient data; avoid irreversible reinterpretation. Nodes with legacy targeted/area/gradient operations remain legacy until their combined migration is specified; explain an unavailable upgrade. New nodes can adopt the new version after validation. |
| Panel / floating graph | Both author the same draft and use the same limits/units. Respect graph-driven values. No per-frame image scanning or duplicate processing formulas in widgets. |
| Reference / GPU | Establish one authoritative scalar transfer and intentional CPU/GPU parity checks. The present sample helper is not a full reference renderer. Share generated coefficients where possible. |
| Analysis / caching | Reuse upstream-only fits and guides across tonal-amount edits. Include version, working space, full image geometry, spatial settings and evidence identity in relevant keys. |
| Viewport | Use complete incoming evidence and full-image coordinates. Crops consume consistent results; any bounded/tiled algorithm must derive its required support, not guess a one-radius halo. |
| Scheduling | Keep processing on existing workers, coalesce drag edits, reject stale project/node/revision results, and retain accepted imagery. No new UI-thread waits or full-frame readbacks. |
| Memory | Account for all guide work fields and accepted caches using existing admission. Do not retain a guide per slider. Memory deferral preserves accepted output and cannot silently change the authored operator. |
| Preview / export | Allow declared sampling approximations while preserving operator semantics, guide scale and target anchors. Native/export use the same version and constraints. |
| Diagnostics | Request only when visible/needed through existing analysis. Preview overlays remain presentation, absent from saved/exported pixels. |

The relevant owners are the existing Raw recipe/graph parameter preparation, Renderer Light analysis/spatial field, `PhotographicPanelEditors`, `LightBandEditor`, and node-edit host. Avoid placing a new solver in either editor or expanding the large shader source file with another unrelated subsystem. Follow [implementation guidance](../../Guidance/Implementation/README.md) for the eventual code change.

## 9. Bounded next pass and acceptance questions

### Implementation sequence

1. **Contract cleanup:** version behavior; reconcile Strength limits and visible settings; establish the shared effective-transfer representation. Preserve old projects.
2. **Constrained transfer:** implement scalar limits and tail roles on the existing guide, making the source of improvement easy to assess.
3. **Spatial comparison:** separate region size/detail retention and compare the existing guide with one exposure-stable guided candidate. Bring in fast Local Laplacian only if that comparison exposes a concrete quality gap.
4. **Native panel:** connect the approved controls and real diagnostics to the existing draft/history/worker paths. Select defaults using the limited photo comparison.

Before extensive image validation, agree on its scope under [AGENTS.md](../../AGENTS.md). Suggested initial scope is the existing scalar/serialization checks plus **six supplied photographs**, one pass each at ordinary and strong settings. No new broad synthetic-data framework or repeated benchmark sweep is implied.

| Focused case | Evidence to inspect |
| --- | --- |
| Scalar transfer and adjacent opposing controls | Exact neutral behavior; positive slope including between knots/tails; finite extremes; combined advanced curve/protection. |
| Bright window and dark frame | Halo/overshoot, edge width, preserved interior/exterior texture. |
| Fine branches against sky | Thin-feature leakage, halos, guide-scale behavior. |
| Deep noisy shadow with real texture | Visibility of noise versus genuine detail; no invented recovered information. |
| Saturated lights, skin or colored reflections | Signed-channel behavior, RGB ratio continuity, perceived color after the actual View transform. |
| Fog or another low-contrast scene | Preserve intentional softness; avoid forcing every image to full contrast. |
| Broad HDR cloud plus a small bright source | Bright-region separation, upper-tail continuity, values above SDR white. |

Use the same selected image to check proxy/native agreement and overlapping viewport crops; do not expand this into a screenshot matrix. Review the existing cancellation/history/save tests that exercise the changed contracts. A bounded Windows build and an actual GPU-path check are needed before claiming a native implementation works; research math alone cannot establish that.

Still to decide from evidence: exact tail shape, contrast floor, soft gain budget transition, default spatial scale, whether one guide is sufficient, and whether optional source confidence improves shadows without suppressing valid content. These are implementation experiments, not unfinished work in this research PR.

## 10. First-pass verification record

- Read current model, sanitization, panel/floating controls, CPU helper, GPU guide/application, Exposure placement and analysis ownership.
- Distinguished active paths from unused shader helpers and legacy saved-area behavior.
- Derived the extreme-tone example directly from the current cosine basis; did not claim a running-image reproduction.
- Reviewed primary Adobe documentation, a firsthand engineer statement, author-hosted papers, and official darktable documentation.
- Independent source and design reviews completed; their scalar-bound and scope clarifications are incorporated. All 26 evidence IDs, 20 pinned code-file links, relative links and code fences were checked.
- Concept JavaScript parses; static selectors/IDs and six control ranges were checked. Its actual script passed an in-memory DOM event-handler smoke check for slider/number synchronization, exact-entry bounds, selected-control intent/reach, reset and spatial settings. This is not a browser rendering test.
- Optional headless layout verification was blocked by the environment's browser socket restriction; the requested escalation was automatically rejected. No browser workaround or computer-use tool was used. Rendered layout and native browser interaction remain unverified.
- No C++/GLSL, processing defaults, build configuration or persistence code changed. No Windows build, GPU image run, photographic benchmark or new automated image suite was performed for this design pass.

## Sources

All web sources accessed November 20, 2026. Sources [R1]-[R6] support the scoped product claims above. The algorithm papers support candidate mechanisms and caveats; proposed Stack settings and architecture are our design conclusions.

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
