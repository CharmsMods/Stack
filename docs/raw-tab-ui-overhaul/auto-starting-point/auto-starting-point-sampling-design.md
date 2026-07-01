# RAW Auto Starting Point Sampling Design

Research pass: June 30 / July 1, 2026.

## Purpose

This note answers the follow-up question: if STACK adds a button that applies
manual settings to create a good starting point, what image state should it
sample?

The short answer is: **not only the final displayed image**. A useful automatic
starting point needs staged evidence from the RAW/scene pipeline and from the
final display result. It should then write the chosen edits into the existing
manual recipe controls.

## Short Answer

The button should behave like **Build Starting Point**, not like a hidden
"auto enhance" layer.

It should:

1. Inspect raw/technical information before user-facing tone and display edits.
2. Render a neutral or near-neutral scene-linear baseline.
3. Try one or more bounded candidate recipes.
4. Measure both pre-finish/pre-display and final display outcomes.
5. Apply the selected result into visible controls:
   - `preToneExposureEv` for RAW Exposure.
   - WB recipe fields for White Balance.
   - `localRange` graph/mask fields for Local Range.
   - `finishTone.layerJson` for Finish Tone.
   - `viewTransform.layerJson` for Display Fit / View Transform.

That keeps the automatic action inspectable and editable.

## External Research Anchors

### Adobe

Adobe Camera Raw says Auto analyzes the image and adjusts tone controls, and
also says automatic tone should be applied first if used because it ignores
previous adjustments in other tabs:
https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html

Lightroom Classic describes Auto tone as setting sliders to maximize tonal
scale and minimize highlight/shadow clipping. The same page defines Exposure as
overall brightness in stop-like increments, and describes Highlights, Shadows,
Whites, and Blacks as targeted tonal ranges:
https://helpx.adobe.com/lightroom-classic/help/image-tone-color.html

Adobe's tone-control workflow guidance recommends thinking top-down through
Basic tone controls, starting with midtone Exposure, then highlights, shadows,
white point, black point, contrast, and a final exposure touch-up:
https://helpx.adobe.com/lightroom-classic/help/tone-control-adjustment.html

Implication for STACK: a one-click starting point is legitimate, but it should
author actual controls. Adobe's "Auto" is not a separate invisible output pass;
it changes sliders.

### darktable

darktable's scene-referred workflow keeps RAW data on an unbounded linear scale
and only compresses to the display range after image processing:
https://docs.darktable.org/usermanual/4.2/en/overview/workflow/process/

darktable's exposure module is linear RGB, scene-referred. Its automatic RAW
mode analyzes the histogram and chooses an exposure compensation that shifts a
selected percentile to a selected target level:
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/exposure/

darktable's tone equalizer is a scene-referred dodge/burn tool. It uses a guided
luminosity mask and a graph whose vertical axis is an exposure adjustment for
matching brightness zones:
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/

darktable filmic maps scene EV ranges into display ranges; middle gray maps to
18 percent and white/black relative exposure define which scene range fits on
the display:
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/filmic-rgb/

Implication for STACK: RAW Exposure, Local Range, Finish Tone, and View
Transform should stay mentally separate. Some are scene-placement tools; some
are local scene edits; some are global tone shaping; some are display mapping.

### View Transform / Color Management

OpenColorIO distinguishes display-referred transforms from scene-referred
transforms in its display color space model:
https://opencolorio.readthedocs.io/en/latest/guides/authoring/colorspaces.html

Implication for STACK: "View Transform" is a real technical concept, but the UI
should pair it with the plain-language job label **Display Fit**.

## What Current STACK Samples

The compact RAW development renderer currently follows this broad order in
`src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp`:

1. RAW GPU render: demosaic, WB, camera transform, RAW Exposure.
2. Legacy Local Exposure, if enabled.
3. Local suggestion image and Local Range target sampling.
4. Local Range.
5. Finish Tone.
6. Capture View Transform input stats.
7. View Transform.

The current RAW workspace analysis is built in
`src/Raw/RawImageAnalysis.cpp` from the stats captured immediately before View
Transform. The status text is explicit: technical RAW analysis is unavailable,
and current-frame diagnostics are safe for View Transform fitting only.

That is good enough for the current **Fit Display** behavior. It is not enough
for a stronger one-click starting point, because:

- final or near-final stats can hide bad upstream exposure placement;
- Local Range suggestions need to know whether a problem is global or regional;
- Finish Tone and View Transform can make different upstream candidates look
  deceptively similar.

The Advanced Develop candidate feedback path already recognizes this problem.
`src/Editor/Internal/EditorRenderWorkerCandidateRendering.cpp` can measure both
final metrics and pre-finish metrics. `EditorModuleDevelopRenderedFeedbackAnalysis.cpp`
keeps a candidate alive when final tone masks an upstream difference.

That is the strongest local precedent for RAW tab starting-point sampling.

## Which Image State To Sample

### 1. Raw / Technical Stage

Question answered: what did the sensor and metadata give us?

Use this for:

- sensor clipping/headroom;
- black/white level confidence;
- ISO/noise risk;
- baseline exposure metadata;
- as-shot WB availability;
- camera profile and CFA facts.

This should not be a pretty preview. It is diagnostic evidence.

Implementation note: some data can come from `RawImageData` metadata and raw
buffers. GPU debug views already exist for normalized mosaic, CFA, demosaiced
camera RGB, clipped raw channels, and denoise/debug masks. However,
`RawDebugView::DemosaicedCameraRgb` currently comes after highlight
reconstruction in the shader, so a true "only demosaiced, no corrections" stage
would need either neutral settings or a dedicated analysis path.

### 2. Neutral Scene Stage

Question answered: where does the unstyled scene sit in linear brightness and
color after a minimal RAW conversion?

Suggested contents:

- demosaic;
- as-shot or explicit neutral WB policy;
- camera transform into working RGB;
- RAW Exposure at 0 EV;
- no Local Range;
- no Finish Tone;
- no View Transform for analysis.

This is the best place to estimate the first global RAW Exposure/WB proposal.
It is more useful than raw mosaic data for user-facing EV decisions because it
is closer to the actual working scene image.

### 3. Raw Placement Candidate Stage

Question answered: after proposed RAW Exposure and WB, is the scene placed
sensibly before local correction?

Sample after:

- proposed RAW Exposure;
- proposed WB.

Sample before:

- Local Range;
- Finish Tone;
- View Transform.

Use this to decide whether global exposure solved the image or merely created a
new problem. For example, if a face is still dark but highlights are now near
risk, stop pushing RAW Exposure and hand the problem to Local Range.

### 4. Local Candidate Stage

Question answered: do Local Range graph points improve the scene before global
tone and display compression?

Sample after:

- proposed RAW Exposure/WB;
- proposed Local Range.

Sample before:

- Finish Tone;
- View Transform.

Use this to evaluate lifted shadows, held highlights, backlit subjects, and
local contrast preservation. This is also the right place to decide whether a
local suggestion should become visible graph points, ghost points, or no edit.

### 5. Finish Tone Candidate Stage

Question answered: does the global tone graph produce a useful tonal
relationship without doing work that should belong to Local Range or View
Transform?

Sample after:

- RAW Exposure/WB;
- Local Range;
- Finish Tone.

Sample before:

- View Transform.

This should not be judged only by final display pixels, because the View
Transform can hide excessive or weak pre-display contrast.

### 6. Display Candidate Stage

Question answered: does the displayed image look readable and safe on the
target display?

Sample after:

- all proposed visible controls;
- View Transform.

Use this for display clipping, perceived brightness, final histogram spread,
and preview/readability confidence.

## Candidate Strategy

The starting-point builder should use a small candidate budget, not an
unbounded auto loop.

Recommended early implementation behavior:

- Build one conservative candidate and one slightly farther candidate.
- Render at preview analysis size, such as 512 or 768 max dimension.
- Record stage stats for neutral, pre-local, pre-finish, and final display.
- Score the candidates.
- Apply the best candidate into the visible recipe.
- Store one revert snapshot, like current Auto Base.

Candidate scoring should prefer:

- valid scene signal;
- sensible middle placement;
- limited display clipping;
- no hidden highlight damage;
- no extreme Local Range deltas when a small RAW Exposure correction would do;
- pre-finish improvement that remains visible after View Transform;
- conservative strength unless the user chose a farther mode.

Avoid continuous live iteration while the user is editing. Recompute after a
settled render or on explicit button press.

## UI Proposal

Rename the current Auto Base action:

- `Analyze`: no recipe change.
- `Fit Display`: current behavior; only refits View Transform.
- `Build Starting Point`: new behavior; applies a visible multi-control recipe.
- `Undo`: restores the recipe before the automatic action.

Optional strength menu:

- `Base`: conservative scene placement plus display fit.
- `Balanced`: may add mild Local Range and Finish Tone.
- `Farther`: may apply stronger local/tone shaping, still visible and editable.

After applying, show a compact summary such as:

`Starting point: RAW Exposure +0.35 EV, WB As Shot, Local Range 2 points, mild Tone Graph, Display Fit refreshed.`

The important UI rule: every applied value must be visible in the same manual
section the user would use to change it.

## Implementation Map

### Data Model

Create a focused RAW starting-point module instead of overloading Auto Base:

- `src/Raw/RawAutoStartPoint.h`
- `src/Raw/RawAutoStartPoint.cpp`

Suggested structs:

- `RawAutoStartPointIntent`: Base, Balanced, Farther.
- `RawAutoStartPointStage`: RawTechnical, NeutralScene, RawPlacement,
  LocalCandidate, FinishToneCandidate, DisplayCandidate.
- `RawAutoStartPointStageStats`: stage name, render stats, confidence,
  warnings.
- `RawAutoStartPointCandidate`: candidate recipe plus stage stats.
- `RawAutoStartPointResult`: selected recipe, applied summary, confidence,
  candidate diagnostics.

### Render Worker

Extend `EditorRenderWorker::RawWorkspaceSnapshot` and `RawWorkspaceResult` with
an explicit start-point request/result. Keep it separate from the normal
per-frame RAW workspace analysis so the expensive pass only runs when requested.

The current `RawWorkspaceResult` has:

- one `viewTransformInputStats`;
- one `RawImageAnalysis`;
- one `AutoBaseRecommendations`;
- one `localRangeTargetSample`.

The new path needs per-candidate stage stats.

### Renderer / Readbacks

Add a way to read stats at multiple RAW development boundaries:

- neutral scene or raw placement boundary;
- pre-Local-Range boundary;
- pre-Finish-Tone boundary;
- pre-View-Transform boundary;
- final display boundary.

This can be implemented by either:

1. adding named stage texture captures to the compact RAW development renderer;
2. or rendering synthetic candidate sockets the way Develop candidate feedback
   already renders final and pre-finish outputs.

The second option is attractive because it mirrors existing candidate feedback
logic and naturally supports a bounded candidate batch.

### Apply Path

Add a RAW workspace method near the current Auto Base apply code:

- `ApplyRawWorkspaceAutoStartingPointForSource(...)`

It should:

1. verify the selected RAW source;
2. store a revert snapshot;
3. request or consume the start-point result;
4. call `ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)`;
5. refresh recommendations and overlays;
6. mark ownership states for the sections it touched.

Do not put the core solving logic in the ImGui panel file. The panel should
only render buttons, state, and summaries.

### Manual-Control Fidelity

Allowed automatic writes:

- RAW Exposure suggestion -> `recipe.preToneExposureEv`.
- WB suggestion -> visible WB recipe mode/multipliers.
- Local suggestion -> `recipe.localRange` points/mask fields.
- Finish Tone -> visible tone graph layer JSON, with enough UI exposure to edit
  the authored result.
- Display Fit -> `recipe.viewTransform.layerJson`.

Not allowed:

- an invisible "auto result" texture;
- a post-process that makes the preview look better without recipe controls;
- a continuously changing view transform that hides RAW Exposure feedback;
- applying Finish Tone fields that the compact RAW tab cannot meaningfully show
  or bridge to full controls.

## Suggested Early Pass Scope

The earliest pass should be a design-safe skeleton:

1. Rename current `Apply` button to `Fit Display`.
2. Add disabled or experimental `Build Starting Point` UI state only if needed.
3. Implement the result data model and request plumbing.
4. Measure two stages first: pre-View-Transform and final display.
5. Add neutral/raw-placement stage capture next.
6. Only then apply multi-control recipes.

This prevents the button from becoming a black box before the analysis evidence
is trustworthy.

This section is not the full project scope. It is only the first safe slice of a
larger implementation sequence. Later updates should continue into named stage
readbacks, dry-run candidate reports, visible Base application, conservative
Balanced Local Range, and Finish Tone authoring once the compact RAW tab can
show or bridge the authored values.

## Open Questions

- Should `Build Starting Point` default to `Base` or `Balanced`? My current
  recommendation is `Base` for the earliest public behavior and a hold/menu for
  farther modes.
- Should WB be included in the first automatic recipe, or stay as a separate
  suggestion until image-data WB confidence is stronger?
- Should Finish Tone changes be applied in the compact RAW tab before every
  advanced ToneCurve auto field is exposed there?
- Should candidate stage stats be visible in Diagnostics, or only summarized
  unless a debug flag is enabled?
