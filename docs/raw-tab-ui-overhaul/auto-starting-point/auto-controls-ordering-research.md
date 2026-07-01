# RAW Automatic Controls Ordering Research

This note preserves the June 30, 2026 research pass on how STACK should name,
order, and visually separate the RAW tab's automatic controls. It is a synthesis
of current code behavior, existing docs in this folder, and targeted research on
Lightroom/Camera Raw, Photoshop, darktable, DNG, OpenColorIO, and tone/WB math.

Folder note: this file now lives beside `auto-manual-compute-model.md` and
`auto-starting-point-sampling-design.md` so the automatic-control research can
be reread as one packet before later RAW tab UI or implementation passes.

## Short Answer

The RAW tab should treat automation as three different products, not one thing:

1. **Display readability automation**: safe to apply visibly for new/default RAW
   recipes. In current code this is Auto Base fitting View Transform.
2. **Editable suggestions**: safe to compute and preview, but user-applied. In
   current code this covers RAW Exposure, alternate WB, highlight protection,
   and Local Range suggestions.
3. **Diagnostics/advice**: computed evidence with no direct safe edit, such as
   clipping, noise/detail warnings, and analysis rationale.

This supports a panel order like:

1. Readiness strip: source, Display Fit state, warning count, suggestion entry.
2. Base Light: RAW Exposure plus Display Fit / View Transform ownership/refit.
3. White Balance: As Shot/default first, alternate suggestions nearby.
4. Local Range: graph, Target tool, Color Target, overlay mode.
5. Finish Tone: final creative tone graph.
6. Crop/Rotate and Preview/Output.
7. Diagnostics drawer: analysis stats, Auto Base rationale, stage details.

The important naming change is that **Auto Base is not the editor**. It is the
assistant layer. The applied control is **Display Fit / View Transform**.

## Current Code Model

### RAW Workspace Auto Base

`src/Raw/RawAutoBase.cpp` currently fits View Transform from current-frame
scene-linear stats. `FitViewTransformFromAnalysis` builds `middleGrey`,
`whiteEv`, `blackEv`, contrast, shoulder, toe, and saturation; the decision
summary says the view fit changes display mapping while RAW Exposure remains
unchanged.

`src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp` applies that fit only
when the recipe is default/new, or when the user explicitly presses Apply. It
refuses automatic changes for existing/edited recipes and marks View Transform
as user-owned after manual edits.

`BuildAutoBaseRecommendations` computes recommendations for RAW Exposure, WB,
highlights, Local Range, and noise/detail. The apply handlers show the trust
boundary:

- RAW Exposure suggestions write `preToneExposureEv` only when the user applies.
- WB suggestions write custom multipliers only when the user applies.
- Highlight protection can write View Transform shoulder/white EV when applied.
- Local suggestions instantiate normal editable Local Range recipe values.
- Noise/detail is advisory in the current RAW workspace.

Conclusion: the RAW workspace Auto Base is mostly a **visible display fit plus a
suggestion engine**, not a full auto-develop solver.

### Advanced Develop Auto

The Advanced Develop node has a broader Auto mode. `src/Develop/DevelopTypes.h`
defaults `RawDevelopPayload::uiMode` to Auto and carries `AutoGuidance`.
`src/Editor/Internal/EditorModuleDevelopControls.cpp` describes Auto as solving
RAW conversion, Scene Prep, and Finish Tone together; Manual freezes the current
auto-authored result.

The apply path is intentionally broader:

- `EditorModuleDevelopAutoSolveRawApplication.cpp` can author RAW EV, WB mode,
  highlight reconstruction, false-color/defringe cleanup, and mosaic denoise.
- `EditorModuleDevelopAutoSolveScenePrepApplication.cpp` authors Scene Prep
  strength, shadow/highlight bias, noise guard, halo guard, texture guard, and
  local exposure pressure.
- `EditorModuleDevelopAutoSolveToneApplication.cpp` writes diagnostics and
  requested/authored intent fields into the integrated tone layer JSON.
- `EditorModuleGraphProcessingNodes.cpp` gates recalculation with trigger hashes,
  so Auto does not blindly rewrite on every repaint. Full raw recalibration is
  needed when raw inputs, metadata, mode/guidance, subject importance, or an
  explicit full reanalysis change requires it.

Conclusion: the RAW tab and Advanced Develop node should not share one vague
"Auto" label. Suggested language:

- RAW tab: **Auto Base** or **Base Assist**, with applied state named
  **Display Fit / View Transform**.
- Advanced Develop: **Auto Develop Solve**, because it really can author a
  merged RAW + prep + tone solution.

## Pipeline And Mental Model

The compact RAW development render path in
`src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp` is:

1. RAW decode, WB, camera transform, RAW Exposure.
2. Legacy Local Exposure if enabled.
3. Local suggestion image and Local Range target sample capture.
4. Local Range render/overlay.
5. Finish Tone.
6. View Transform.

The UI does not need to be strictly pipeline-ordered, but it must not lie about
pipeline order. The practical wording is:

- **RAW Exposure**: moves scene-linear image placement before local/tone/display.
- **Local Range**: changes selected scene-EV regions before Finish Tone and View
  Transform.
- **Finish Tone**: creative tone shaping after Local Range.
- **Display Fit / View Transform**: maps the current scene-linear image to the
  screen so it is readable.

That is why Display Fit belongs near the top for usability, but its section
should visibly say that it happens late in the render pipeline.

## Follow-Up: Four Foundational RAW Controls

This section preserves the June 30 follow-up question about the four controls
that need to become mentally unified without becoming one giant control:

1. Global RAW Exposure.
2. Local Exposure / Local Range graph.
3. Finish Tone / tone graph.
4. Display Fit / View Transform.

The important split is that STACK has two valid orders:

1. **Processing order**, which is what the renderer actually does.
2. **Teaching/workflow order**, which is what a human should usually think about
   while editing.

### Processing Order

In current code the recipe stage order is:

`source -> raw-decode -> white-balance -> pre-tone-exposure -> local-exposure -> local-range -> tone-curve -> view-transform -> crop-rotation -> output`

That is stored by `DefaultStageOrder()` in
`src/Raw/RawDevelopmentRecipe.cpp`. The compact RAW renderer follows the same
mental order:

1. RAW decode, WB, camera transform, and RAW Exposure are rendered through the
   RAW GPU path.
2. Legacy Local Exposure runs next if enabled.
3. Local Range suggestions sample the pre-Local-Range scene-linear image.
4. Local Range applies its scene-EV graph and optional masks.
5. Finish Tone renders the tone curve layer.
6. View Transform renders last, after stats are captured from its input.

This means the four controls are not peers in the pixel pipeline:

- **RAW Exposure** is an early scene-linear multiplier.
- **Local Range** is a scene-linear regional/tone-zone exposure graph.
- **Finish Tone** is a later global tone-shaping graph.
- **View Transform** is the final scene-to-display mapping.

### Human Workflow Order

For a user, Display Fit is useful twice:

1. At the beginning, as a readability lens so the RAW is not too dark, flat, or
   outside the display range to judge.
2. At the end, as a final display/output mapping after the image has been placed,
   balanced, locally corrected, and tone-shaped.

So the teaching order should be:

1. **Initial Display Fit**: get a readable preview. This is a viewing helper,
   even though it renders last.
2. **Global RAW Exposure**: place the whole scene's middle brightness. Use it
   for overall scene placement, not for rescuing one face under a bright sky.
3. **Local Range**: fix parts of the image by scene EV, color, or region. Use it
   when the global exposure would damage another part of the frame.
4. **Finish Tone**: shape the global tone relationship and contrast after the
   local problems are no longer forcing the curve to do local work.
5. **Final Display Fit / View Transform**: refit or polish the display mapping,
   especially white EV, black EV, shoulder, toe, and display contrast.

This is why the UI can show Display Fit near the top but should visually explain
that it is an output lens, not an early RAW exposure edit. A good label pattern
is:

`Base Light: RAW Exposure + Display Fit status`

with a small stage badge such as:

`RAW Exposure: early scene placement` and `Display Fit: final display mapping`

### What Each Control Touches

#### 1. Global RAW Exposure

STACK field: `RawDevelopmentRecipe::preToneExposureEv`.

Renderer position: before Local Exposure, Local Range, Finish Tone, and View
Transform.

What it does: applies a uniform scene-linear EV scale to the image. A +1 EV
change doubles scene-linear values before later tone and display mapping. This
changes the whole frame, including the values seen by downstream Local Range,
Finish Tone, and View Transform analysis.

Good use:

- Set the image's global scene placement.
- Put the main subject or average scene near a sensible middle brightness.
- Correct genuinely under- or over-placed RAW data when highlight headroom allows.

Bad use:

- Do not use it to brighten a dark subject when the sky/highlights are already
  near risk. Local Range or View Transform should carry more of that job.
- Do not let automatic display refits hide the effect of moving RAW Exposure.

External anchor: darktable's exposure module is explicitly linear RGB,
scene-referred, and is used to set overall image brightness/middle-gray before
filmic/view compression. Adobe's Basic Exposure control is also described as
overall image brightness in stop-like increments.

#### 2. Local Exposure / Local Range Graph

STACK field: `RawDevelopmentRecipe::localRange`, plus the older
`localExposure` block that can be converted into Local Range graph points.

Renderer position: after RAW Exposure, before Finish Tone and View Transform.

What it does: computes a scene EV from scene luma and uses the graph to apply an
EV delta to matching tones. Region and color masks can narrow the effect. In
code, `EvaluateLocalRangeDeltaEv()` evaluates the graph and
`LocalRangeExposureScaleForLuma()` converts the delta back to a multiplier with
`exp2(deltaEv)`.

Good use:

- Lift shadow subjects without raising the entire RAW exposure.
- Hold bright sky or display highlights without lowering the whole scene.
- Make tone-zone corrections before the Finish Tone curve decides global
  contrast.

Bad use:

- Do not treat it as the final global contrast curve.
- Do not hide automatic local suggestions away from the graph. Applying a local
  suggestion should create visible graph points, masks, or ghost markers.

External anchor: darktable's tone equalizer is a scene-referred dodge/burn style
tool that uses a guided luminosity mask and an exposure graph while preserving
local contrast. Adobe frames local Exposure as a correction similar to
traditional dodging and burning, with local masks and range refinement.

#### 3. Finish Tone / Tone Graph

STACK field: `RawDevelopmentRecipe::finishTone.layerJson`; legacy
`toneCurve.points` still serializes but the active compact RAW path renders the
Finish Tone layer JSON.

Renderer position: after Local Range, before View Transform.

What it does: maps tonal inputs to tonal outputs. In a simple point curve, moving
a point up makes that input tone lighter and moving it down makes it darker.
STACK's ToneCurve layer also has richer auto/prepared state, local baseline, and
foundation controls when used as a full graph node or integrated Develop tone
layer.

Good use:

- Shape global tonal relationship after exposure and local problems are solved.
- Add or soften contrast.
- Decide how midtones, darks, lights, and highlights relate creatively.

Bad use:

- Do not make the tone graph carry the whole job of display mapping. That is the
  View Transform's job.
- Do not use the global curve to fix one local object if Local Range can do it
  more directly.

External anchor: Adobe says the Tone Curve fine-tunes the tonal scale after
Basic tone adjustments. darktable filmic is more display-transform-like than
STACK's Finish Tone, but it is still useful conceptually: it shows how a curve
redistributes scene dynamic range and makes contrast tradeoffs.

#### 4. Display Fit / View Transform

STACK field: `RawDevelopmentRecipe::viewTransform.layerJson`.

Renderer position: last among the four controls.

What it does: maps the current scene-linear/tone-shaped image into display
space. It owns middle grey, black EV, white EV, shoulder, toe, display contrast,
saturation, preserve hue, and false color. Auto Base currently writes this field
when it fits the preview from analysis.

Good use:

- Make a RAW file readable without changing RAW Exposure.
- Set display black/white bounds and highlight rolloff.
- Refit after major RAW Exposure, Local Range, or Finish Tone changes.

Bad use:

- Do not present it as if it changed the captured exposure.
- Do not continuously auto-refit in a way that makes earlier controls feel like
  they do nothing.

External anchor: OpenColorIO defines view transforms as mappings between
scene-referred and display-referred reference spaces. darktable's filmic/AgX
family plays a similar user-facing role: scene values are compressed into the
display range after scene-referred processing.

### Code Check: Are Automatic Edits Visible And Manual?

The RAW workspace is mostly aligned with the rule that automatic work should
land in existing visible controls:

- Auto Base View Fit writes `recipe.viewTransform.layerJson` through
  `ApplyViewTransformFitToRecipe()`. That is the same View Transform data exposed
  in Base Light advanced controls.
- Automatic View Fit is only applied automatically for new/default recipes.
  Existing or edited RAW recipes are not changed automatically unless the user
  explicitly applies/refits.
- RAW Exposure suggestions write `recipe.preToneExposureEv` only when applied.
  That is the same RAW Exposure slider the user sees.
- WB suggestions write the visible WB recipe fields only when applied.
- Highlight protection writes View Transform shoulder/white EV when applied,
  and the status text says RAW Exposure is unchanged.
- Local suggestions call `ApplySuggestedLocalAdjustment()` and create normal
  editable `recipe.localRange` points and color mask values.
- Tone Curve auto calibration in the graph/develop path serializes an authored
  ToneCurve layer JSON and applies it back to the actual ToneCurve layer or
  integrated Develop tone JSON. It is not a separate hidden image pass.

The main caveats are UI/readability caveats, not a separate hidden RAW pipeline:

- The compact RAW tab Finish Tone panel does not expose every advanced
  ToneCurve auto/prepared/foundation field that the full ToneCurve layer can
  serialize. If RAW tab Auto ever starts changing Finish Tone automatically, the
  applied result should either show as editable graph points/controls in the RAW
  tab or provide a clear "open full tone controls" bridge.
- The label "Apply" in Auto Base is still too vague. Since the code applies a
  View Transform fit, the action should read **Fit Display** or **Refit
  Display**.
- "View Transform" is technically accurate but should be paired with
  **Display Fit** so users know its job.

### Classroom Explanation

Think of the image as a sentence being translated:

1. **RAW Exposure** chooses how loud the whole sentence is before editing. It
   changes everything.
2. **Local Range** changes certain words or phrases because they need local
   emphasis or restraint.
3. **Finish Tone** edits the rhythm of the whole sentence: contrast, weight,
   shape, mood.
4. **View Transform** chooses how the sentence is printed on the page or shown
   on the screen.

That analogy is imperfect, but the direction is right: early controls change the
scene data that later controls receive; late controls change how the already
edited scene is shaped for display.

The most useful beginner rule is:

`Fit to see -> expose the scene -> fix regions -> shape contrast -> refit display`

This rule matches current STACK code and also aligns with Adobe and darktable
workflow guidance.

## External Research Notes

### Adobe Lightroom and Camera Raw

Adobe's current Camera Raw docs say As Shot uses camera WB metadata and Auto WB
calculates from image data. The same page says the Auto tone button analyzes the
image and adjusts tone controls, and recommends applying automatic tone first if
used at all:
https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html

Lightroom Classic's WB docs make the same distinction: As Shot uses camera WB
settings when available, Auto computes WB from image data, and a neutral picker
adjusts Temp/Tint from a selected gray/white area:
https://helpx.adobe.com/lightroom-classic/help/image-tone-color.html

Adobe's tone-control workflow doc recommends a top-down manual order:
Exposure for midtones, Highlights, Shadows, Whites, Blacks, then Contrast and a
final Exposure touch-up. It also notes that the Basic tone controls are image
adaptive and interact:
https://helpx.adobe.com/lightroom-classic/help/tone-control-adjustment.html

Lightroom local adjustment docs are useful for Local Range language. They frame
local Exposure as dodging/burning, and Range Mask as a refinement by sampled
color or luminance:
https://helpx.adobe.com/lightroom-classic/help/apply-local-adjustments.html

Photoshop's Shadow/Highlight docs emphasize local-neighborhood behavior and warn
that extreme corrections can look unnatural:
https://helpx.adobe.com/photoshop/using/adjust-shadow-highlight-detail.html

Product implication for STACK: "Auto" can be a deliberate one-click action, but
automatic tone/WB should be visible, reversible, and usually early in the user's
decision flow. Local range/subject-region edits should feel like targeted edits,
not hidden base corrections.

### darktable

darktable's current manual strongly supports the scene-referred separation:
scene-linear editing remains unbounded as long as possible, and display mapping
compresses to the device late in the pipeline:
https://docs.darktable.org/usermanual/development/en/overview/workflow/process/

The pixelpipe/module-order docs say scene-referred workflows place tone mapping
late and do most work in linear RGB. They also warn that some modules must stay
early, such as highlight reconstruction before demosaic:
https://docs.darktable.org/usermanual/development/en/darkroom/pixelpipe/the-pixelpipe-and-module-order/

The processing preferences docs say scene-referred defaults can automatically
enable exposure, color calibration, and filmic/AgX/sigmoid style tone mapping as
reasonable starting points:
https://docs.darktable.org/usermanual/development/en/preferences-settings/processing/

The exposure docs specify that exposure operates in the scene-referred, linear,
camera RGB part of the pipeline:
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/exposure/

The color calibration docs distinguish technical white balancing from chromatic
adaptation, start from camera Exif/as-shot metadata by default, and warn that
gray-world sampling can fail on artificial/stylized scenes:
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/color-calibration/

The tone equalizer docs describe scene-referred dodge/burn by similar luminosity
while preserving local contrast:
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/

Product implication for STACK: keep early technical controls, local scene-EV
edits, and late display mapping conceptually distinct. The user's local targeting
tool is closer to darktable's scene-referred dodge/burn/tone-equalizer family
than to a generic global "Auto" feature.

### DNG And Metadata

Adobe's DNG page identifies DNG as a public raw format and links to the current
DNG 1.7.1.0 specification:
https://helpx.adobe.com/camera-raw/digital-negative.html

The DNG spec says `AsShotNeutral` records the selected capture white balance as
a neutral color in linear reference space. It says `BaselineExposure` moves the
zero point of the raw converter's exposure compensation in EV units because
camera models trade highlight headroom against shadow noise differently:
https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf

Product implication for STACK: metadata-backed defaults are legitimate
technical defaults. Content-derived RAW exposure and WB are more interpretive,
so the UI should present them as suggestions unless confidence and policy are
very explicit.

### View Transforms

OpenColorIO's config docs define view transforms as mappings between
scene-referred and display-referred reference spaces, and note that a view may
combine a view transform plus a display color space:
https://opencolorio.readthedocs.io/en/latest/guides/authoring/authoring.html

Adobe's OCIO/ACES docs for After Effects use display color space and view
transform as viewport/display choices:
https://helpx.adobe.com/after-effects/using/opencolorio-aces-color-management.html

Product implication for STACK: "View Transform" is the correct technical term,
but users benefit from "Display Fit" as the visible job label. Use both:
**Display Fit / View Transform**.

### Tone And WB Math

Reinhard et al. frame tone reproduction as mapping high dynamic range scene
luminance to low dynamic range print/screen output, with exposure-like initial
scaling and optional dodge/burn style compression:
https://www.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf

Finlayson and Trezzi's "Shades of Gray and Colour Constancy" generalizes
gray-world and max-RGB style illuminant estimation as Minkowski-norm methods:
https://research-portal.uea.ac.uk/en/publications/shades-of-gray-and-colour-constancy/

Product implication for STACK: percentile/log/EV tone fitting and gray-world WB
are valid analysis tools, but they are assumptions. Good UI wording should make
the assumption visible: "Suggested WB" or "Neutral estimate", not "correct WB".

## Naming Recommendations

| Current / likely label | Recommended visible label | Reason |
| --- | --- | --- |
| Auto Base panel | Base Assist or Auto Base | Keep as the assistant/status layer, not the edit itself. |
| Apply Auto Base | Fit Display or Refit Display | Current applied behavior is View Transform fitting, not full auto edit. |
| View Transform | Display Fit / View Transform | Plain job first, technical term retained. |
| RAW Exposure | RAW Exposure | Accurate. Tooltip should say scene-linear EV scale before local/tone/display. |
| WB Auto | Daylight or Metadata Auto unless true image AWB is implemented | Current GPU `Raw::WhiteBalanceMode::Auto` uses daylight WB metadata, not an image-data AWB solve. |
| Suggested WB | Suggested WB / Neutral Estimate | Makes the inferential nature explicit. |
| Target | Target | Current label is good. Tooltip must say it samples scene EV and optional color, then drag sets Local Range. |
| Color Target | Color Target | Good. Keep separate from Local Range Target. |
| Overlay | Overlay: Affected / Delta / Mask | Good. Say preview-only. |
| Image Analysis | Diagnostics | Keeps evidence out of the main edit path. |
| Advanced Develop Auto | Auto Develop Solve | Distinguishes full merged solver from RAW tab Base Assist. |

## Ordering Recommendation

Recommended default RAW tab structure:

1. **Top Readiness Strip**
   - Source/project state.
   - Display Fit state: Auto, Manual, Stale, Refit available.
   - RAW Exposure unchanged note when Auto Base only fit display.
   - Suggestion count and warning count.

2. **Base Light**
   - RAW Exposure as the primary manual scene-linear placement control.
   - Display Fit / View Transform status and Refit button paired beside it.
   - Exposure suggestion marker here; full details in suggestion popout.

3. **White Balance**
   - As Shot/default first.
   - Daylight/metadata mode if kept.
   - Custom multipliers and Gray Point.
   - Suggested WB marker here; full details in suggestion popout.

4. **Suggestions Popout / Expander**
   - Primary entry from the readiness strip.
   - Group suggestions by affected section: Base Light, White Balance, Display
     Fit, Local Range, Detail/Noise.
   - Applying a suggestion creates normal visible recipe values.

5. **Local Range**
   - Enable, graph, Target, presets.
   - Color Target and Region Mask as compact subareas.
   - Overlay modes near the graph.
   - Suggested graph points/ghost markers next to the graph.

6. **Finish Tone**
   - Creative graph and tone controls.
   - Keep visually distinct from Display Fit.

7. **Crop/Rotate and Preview/Output**
   - Routine finish/export concerns.

8. **Diagnostics**
   - Full Auto Base rationale.
   - Technical stats, clipping, highlight risk, noise/detail.
   - Stage/pipeline explanation.

## Beginner-Friendly Explanation

If a beginner asks "what should I touch first?", the answer should be:

1. **Display Fit** makes the RAW visible on your screen. It does not change the
   captured exposure.
2. **RAW Exposure** changes the scene-linear light level before the rest of the
   RAW recipe. Use it carefully because highlights can clip.
3. **White Balance** decides what color the light source is. Camera/as-shot is
   a good default; suggested WB is an estimate.
4. **Local Range** is for parts of the image: open shadows, hold skies, or lift
   a subject without moving the whole RAW exposure.
5. **Finish Tone** is the final creative contrast/curve.
6. **Diagnostics** explain why STACK suggested something.

That explanation matches the code and aligns with Lightroom, Camera Raw, and
darktable patterns without copying any one application's UI.

## Open Design Risks

- If "Auto Base" remains the main button label, users may assume it rewrites
  exposure/WB/local/tone. Prefer **Fit Display** for the primary action and
  keep Auto Base/Base Assist as the status system.
- If Display Fit refits too often, RAW Exposure feedback becomes invisible.
  Use visible states: Auto, Manual, Stale, Refit.
- If WB mode "Auto" remains metadata daylight WB, it can mislead users because
  Lightroom/Camera Raw use Auto to mean image-data AWB. Rename or clarify.
- If Local Range suggestions live only in the global suggestion list, users may
  miss that they create editable graph points. Mirror them as ghost/marker state
  in the graph.
- If diagnostics stay inline, the panel will feel like a lab report. Keep the
  top path edit-focused and move evidence into a drawer.
