# Implementation Pass Readiness

Research pass: July 1, 2026.

## Reading Intent

This note answers a narrow question: after the existing logic, workflow, and
math research, what is still missing before implementation can proceed through
multiple passes?

The short answer is that the high-level theory is ready. The remaining gaps are
mostly implementation-friction gaps: normalized scoring, confidence flags, stage
stats shape, validation protocol, and pass order. This document closes the parts
that can be closed with research and marks the parts that only real RAW-image
validation can settle.

Read this after `implementation-contract.md` when starting implementation. It is
not a replacement for the deeper math notes. Also read
`implementation-progress.md` before editing code; it records the active pass,
last completed work, next allowed work, and stop conditions.

## Scope Note: Multi-Update Program

This document is deliberately not a one-pass checklist. It gives the staged
path for a larger RAW-tab update that will happen across multiple code and
validation passes. The early passes exist to make the evidence trustworthy
before the solver writes values; they are not meant to define the final ceiling
of the feature.

`implementation-progress.md` is the active ledger for this staged path. Keep
that file short and update it before and after each implementation pass so a
future agent can resume cold without rereading every research note.

When this file says "first implementation," read it as "the earliest safe
implementation step." Later passes are expected to expand from data structures
and diagnostics into candidate reports, visible Base application, conservative
Balanced Local Range authoring, optional Finish Tone authoring, and
validation-tuned constants.

## Sources Added In This Pass

The new research reinforces five implementation rules.

Adobe Camera Raw's current documentation says Auto analyzes the image and
adjusts tone controls, not a hidden rendering path. The same page separates
As Shot WB, which uses camera metadata, from Auto WB, which calculates from
image data. It also warns that automatic tone should be treated as an initial
approximation and can be undone:

https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html

Lightroom Classic describes Auto tone as setting sliders to maximize tonal
scale while minimizing highlight and shadow clipping. Its tone-control notes
also map broad tonal ranges to user controls: Exposure and Contrast mostly
affect midtones, Highlights and Shadows affect adjacent ranges, and Whites and
Blacks affect the endpoints:

https://helpx.adobe.com/lightroom-classic/help/image-tone-color.html

https://helpx.adobe.com/lightroom-classic/help/tone-control-adjustment.html

darktable provides the most directly implementable precedent for RAW Exposure:
its automatic exposure mode shifts a selected histogram percentile to a selected
target EV relative to camera white. Its scene-referred workflow also gives the
editing sequence: set overall brightness with exposure, then set display white
and black mapping, then tune contrast:

https://docs.darktable.org/usermanual/4.0/en/module-reference/processing-modules/exposure/

https://docs.darktable.org/usermanual/development/en/overview/workflow/process/

darktable's tone equalizer and the guided image filter paper together tighten
the Local Range policy. The editor precedent is a guided luminosity mask whose
graph applies exposure changes by EV zone while preserving local contrast. The
paper precedent is an edge-preserving filter with an exact linear-time
algorithm, useful when smoothing masks without washing over edges:

https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/

https://people.csail.mit.edu/kaiming/publications/eccv10guidedfilter.pdf

Adobe's DNG specification remains the strongest source for raw safety. It gives
the raw-to-linear sequence, says early negative values may be worth preserving,
and defines the two-term NoiseProfile model for shot noise plus readout noise:

https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf

The MIT-Adobe FiveK paper is useful for validation. It is not a rulebook for
STACK, but it explains why one-size-fits-all histogram heuristics fail on
high-key, low-key, and backlit scenes. It also gives a practical feature list
for photographic adjustment research: log-intensity percentiles, smoothed
histogram features, detail-weighted CDFs, highlight clipping fractions, spatial
tone distributions, and face/subject features:

https://people.csail.mit.edu/vladb/photoadjust/db_imageadjust.pdf

## Gap Status

The table below separates gaps that are now closed well enough for
implementation from gaps that must remain validation tasks.

| Area | Before This Pass | Status Now |
| --- | --- | --- |
| Stage metric schema | The docs said to measure stages, but not exactly what every stage should output. | Closed enough for Pass 1. Use `RawSafetyStats`, `SceneStageStats`, `DisplayStageStats`, and candidate summaries below. |
| Score normalization | The docs had weighted scores, but not normalized subscore formulas. | Closed enough for early implementation passes. Use explicit `0..1` score helpers and keep weights visible. |
| Local Range mask reasoning | The docs said guided/local masks but did not anchor the smoothing math strongly. | Closed enough. Guided masks are justified by darktable's tone equalizer and guided filtering literature. |
| Validation protocol | The docs said real RAW validation is needed, but did not define what to log. | Closed enough to build a harness and review sheets. Constants still need image validation. |
| Implementation pass order | The docs had first-pass hints, but not a multi-pass sequence. | Closed in this doc. |
| Final constants | Target key EV, clipping tolerances, tone strength caps, and local lift limits. | Not closable by web research alone. These require STACK RAW validation images. |
| Subject detection | Center/foreground heuristics exist, but true subject/face priority is not fully designed. | Partially open. Base can avoid face dependency; Balanced can start with center/region heuristics. |
| Finish Tone compact UI fidelity | The docs warn not to author hidden advanced fields. | Still implementation-dependent. Do not auto-author fields the RAW tab cannot show or bridge. |

## Code/Web Snapshot To Read Before Pass 1

`code-web-research-readbacks-and-dng.md` should be read before implementing
stage readbacks or raw safety stats. It establishes the current code boundary:
local-suggestion evidence is captured before Local Range, current-frame stats
are captured after Finish Tone but before View Transform, and there is no named
final display or raw safety readback for RAW Workspace yet.

It also records that `ReadTextureStats` is a GL_FLOAT texture readback with
Rec.709-style luma, not a raw safety statistic. The inspected View Transform
shader writes display-mapped linear RGB into an RGBA16F texture without an
explicit display transfer encode in that shader path. Finally, the current DNG
implementation applies a limited `OpcodeList2` GainMap before demosaic, but it
does not parse full DNG active/masked area, true linear response limit, noise
profile, opcode list 1/3, or ProfileGainTableMap support.

## Required Stage Stats

Implementation should not start with a single generic stats blob. The solver
needs three related but distinct structs.

```text
RawSafetyStats
  valid
  activeValidFraction
  blackLevelSource
  whiteLevelSource
  maskedBlackMean
  maskedBlackStd
  blackDriftWarning
  lensShadingConfidence
  cornerFalloffEv
  perChannelClippedFraction
  perChannelNearClippedFraction
  perChannelP999
  rawWhiteP999
  linearResponseLimit
  headroomEv
  wbScaledHeadroomEv
  singleChannelClipFraction
  multiChannelClipFraction
  fullClipFraction
  highlightRecoverabilityScore
  hotPixelFraction
  noiseProfileAvailable
  shadowSnrByEvBucket
  baselineExposureEv
  colorMatrixConfidence
  asShotWbAvailable
```

```text
SceneStageStats
  stage
  validPixelFraction
  lumaPercentiles: p01, p05, p10, p25, p50, p75, p90, p95, p99, p999
  evPercentiles: same set, relative to middleGrey
  logAverageY
  midSpreadEv = p75Ev - p25Ev
  wideSpreadEv = p95Ev - p05Ev
  shadowMassFraction
  highlightMassFraction
  centerMedianEv
  topBandMedianEv
  gradientWeightedPercentiles
  toneBucketSpatialSummaries
  neutralSampleSummary
  negativeChannelFraction
  wideGamutPressure
  matrixGamutConfidence
  localMaskHaloRisk
  noiseAfterGainByEvBucket
```

```text
DisplayStageStats
  transferFamily
  displayClipHighFraction
  displayClipLowFraction
  displayLinearP05
  displayLinearP50
  displayLinearP95
  displayP05
  displayP50
  displayP95
  displaySpread = displayP95 - displayP05
  readabilityScore
```

The FiveK paper is the useful research anchor here. It found that practical
photographic adjustment features include log-intensity percentiles, smoothed
percentiles, gradient-weighted intensity distributions, clipping fractions,
spatial distribution of tone ranges, and face/subject features. STACK does not
need to copy that system, but those features are a strong checklist for what a
candidate solver should record before it claims to understand a RAW image.

The later math audit adds a practical warning: do not collapse per-channel raw
facts into one luma number too early. RAW safety should keep active-area/black
diagnostics, per-channel clipping, WB-scaled headroom, clipping-pattern
fractions, and rough noise propagation. Scene stats should record negative or
out-of-gamut pressure after the camera matrix. Display stats should say whether
their percentiles are measured in linear display light or encoded display code
values.

## Score Helpers

The main implementation gap in the previous docs was score normalization. Every
subscore should return `0..1`, and every penalty should be easy to inspect in
Diagnostics.

Use small helper functions like these:

```text
smoothstep(edge0, edge1, x):
  t = clamp((x - edge0) / max(epsilon, edge1 - edge0), 0, 1)
  return t * t * (3 - 2 * t)

closeness01(errorAbs, goodAbs, badAbs):
  return 1 - smoothstep(goodAbs, badAbs, errorAbs)

lowIsGood01(value, goodMax, badMin):
  return 1 - smoothstep(goodMax, badMin, value)

highIsGood01(value, badMax, goodMin):
  return smoothstep(badMax, goodMin, value)
```

The rule is simple: a subscore should explain one reason a candidate is good. A
penalty should explain one reason it is risky. Avoid a clever single number that
cannot tell the user why the result was chosen.

## RAW Exposure Subscores

RAW Exposure should be scored from the raw-placement stage, not from the final
display result.

```text
targetKeyY = whiteAnchorY * exp2(-targetKeyBelowWhiteEv)
wantedDeltaEv = log2(targetKeyY / max(epsilon, keyY))
maxPositiveDeltaEv = rawSafety.headroomEv - highlightMarginEv
safeDeltaEv = clamp(min(wantedDeltaEv, maxPositiveDeltaEv), modeMinEv, modeMaxEv)
```

After rendering the raw-placement candidate:

```text
keyAfterEv = log2(max(epsilon, candidateKeyY) / max(epsilon, targetKeyY))
scenePlacementScore = closeness01(abs(keyAfterEv), 0.20, 1.00)

highlightSafetyScore =
  lowIsGood01(rawSafety.perChannelNearClippedFractionMax, 0.001, 0.020)

rawExposureConservatism =
  1 - smoothstep(modeSoftLimitEv, modeHardLimitEv, abs(safeDeltaEv))
```

When WB evidence is available, also clamp positive RAW Exposure by
`rawSafety.wbScaledHeadroomEv` rather than only scalar raw headroom. This keeps
one WB-amplified channel from silently becoming the limiting highlight.

The `0.20 EV`, `1.00 EV`, `0.001`, and `0.020` values are initial engineering
targets, not final product constants. They are meant to make the early solver
debuggable. Validation should tune them.

High-key and low-key ambiguity should weaken the exposure move rather than
force a normal histogram:

```text
highKeyEvidence =
  highIsGood01(p50Y, highKeyP50Low, highKeyP50High)
  * lowIsGood01(shadowMassFraction, highKeyShadowGood, highKeyShadowBad)
  * lowIsGood01(rawClipFraction, rawClipGood, rawClipBad)

lowKeyEvidence =
  highIsGood01(shadowMassFraction, lowKeyShadowLow, lowKeyShadowHigh)
  * lowIsGood01(highlightMassFraction, lowKeyHighlightGood, lowKeyHighlightBad)
  * lowSubjectConfidence

keyConfidence = 1 - max(highKeyEvidence, lowKeyEvidence, colorConstancyRisk)
safeDeltaEv *= lerp(0.35, 1.0, keyConfidence)
```

This matches the research lesson from Reinhard and FiveK: scene key is not a
pure histogram fact. A snow scene and a night scene can both be correct while
breaking a normal exposure target.

## White Balance Confidence

White Balance should still be treated as a prerequisite, not one of the four
main controls. The confidence score should be explicit because WB can
destabilize all later measurements.

```text
eligibleNeutralFraction =
  neutralSampleCount / max(1, validPixelCount)

gainDistanceEv =
  max(abs(log2(gainR)), abs(log2(gainB)))

grayVsShadesAgreement =
  1 - smoothstep(0.10, 0.45, maxChannelAbsLog2(grayWorldGain / shadesGain))

neutralEvidenceScore =
  highIsGood01(eligibleNeutralFraction, 0.01, 0.08)
  * lowIsGood01(medianNeutralChroma, 0.08, 0.22)
  * lowIsGood01(gainDistanceEv, 0.75, 1.75)
  * grayVsShadesAgreement
```

Policy:

```text
if cameraAsShotAvailable:
  keep As Shot by default
  expose image-derived WB as a suggestion
else if neutralEvidenceScore > 0.85:
  Base may apply visible WB multipliers
else:
  leave WB unchanged and show diagnostics
```

This follows Adobe's distinction between As Shot metadata and image-data Auto
WB, darktable's warning that gray-world sampling can fail on artificial/stylized
scenes, and Shades of Gray's point that Gray World and Max-RGB are assumptions
within a Minkowski-norm family.

## Local Range Subscores

Local Range should only score highly when it solves a conflict that RAW
Exposure should not solve globally.

Use the raw-placement candidate as input:

```text
globalEv = median(sceneEv(Y) over valid pixels)
centerEv = median(sceneEv(Y) over center/subject region)
brightTopEv = median(sceneEv(Y) over bright top/sky candidate)
shadowMedianEv = median(sceneEv(Y) over shadowMask)
```

Backlit subject:

```text
backlightContrastEv = brightBackgroundEv - centerEv
backlitConfidence =
  highIsGood01(backlightContrastEv, 1.5, 3.0)
  * highIsGood01(centerAreaFraction, 0.03, 0.12)
  * lowIsGood01(rawHighlightRisk, 0.05, 0.35)

subjectLiftEv = clamp(targetCenterEv - centerEv, 0.0, maxSubjectLiftEv)
subjectLiftEv *= backlitConfidence
```

Open shadows:

```text
shadowMask = sceneEv(Y) < globalEv - 2.0
shadowMass = area(shadowMask) / validArea
shadowLiftEv = clamp(targetShadowEv - shadowMedianEv, 0.0, maxShadowLiftEv)
shadowLiftEv *= noiseSafetyFactor
shadowLiftEv *= highlightSafetyFactor
```

Sky/highlight hold:

```text
skyHoldEv = -clamp(brightTopEv - targetSkyEv, 0.0, maxSkyHoldEv)
skyConfidence =
  highIsGood01(topBandBlueOrCyanScore, 0.25, 0.60)
  * highIsGood01(topBandAreaFraction, 0.05, 0.20)
  * highIsGood01(brightTopEv - globalEv, 1.5, 3.0)
```

Write graph points only when the confidence is high enough for the selected
mode. The suggested first thresholds are:

```text
Base:     localConfidence >= 0.90 and abs(deltaEv) <= 0.60
Balanced: localConfidence >= 0.70 and abs(deltaEv) <= 1.00
Farther:  localConfidence >= 0.55 and abs(deltaEv) <= 1.50
```

These values are intentionally conservative. A wrong local graph point feels
more invasive than a wrong Display Fit.

Local confidence should also be reduced by `localMaskHaloRisk` once mask
readbacks exist. A Local Range point that requires a floating mask transition in
a smooth area should remain a ghost suggestion or diagnostic until the user
accepts it.

## Finish Tone Subscores

Finish Tone should score pre-display tone, not final display attractiveness.
Base should generally keep a neutral curve.

```text
flatness =
  clamp((targetMidSpreadEv - midSpreadEv) / max(epsilon, targetMidSpreadEv), 0, 1)

endpointPressure =
  max(0, p99Ev - desiredWhiteEv) +
  max(0, desiredBlackEv - p01Ev)

toneStrength =
  min(modeToneLimit, 0.35 * flatness)
toneStrength *= lowIsGood01(endpointPressure, 0.25, 1.50)
```

A candidate's tone subscore can be:

```text
preDisplayToneScore =
  0.45 * highIsGood01(midSpreadEv, 0.75, 1.75) +
  0.35 * lowIsGood01(endpointPressure, 0.25, 1.50) +
  0.20 * monotonicCurveScore
```

`monotonicCurveScore` should be `1` only if every authored tone point preserves
curve order and does not create a slope spike. This is a boring check, but it
prevents automatic tone from creating a fragile curve the user cannot reason
about.

## Display Fit Subscores

Display Fit should be fit after the selected upstream candidate, then judged in
display space.

```text
displayReadabilityScore =
  0.35 * closeness01(abs(displayP50 - targetDisplayMid), 0.08, 0.28) +
  0.25 * highIsGood01(displaySpread, 0.35, 0.70) +
  0.20 * lowIsGood01(displayClipHighFraction, 0.001, 0.020) +
  0.20 * lowIsGood01(displayClipLowFraction, 0.001, 0.030)
```

The implementation must label whether `displayP50` and `displaySpread` are
linear-display metrics or display-code metrics. For SDR UI histograms, both can
be useful; for scoring physical clipping and cross-display behavior,
linear-display metrics are the safer default.

Display Fit can reject a candidate that looks unreadable on screen, but it
should not make the candidate win by hiding poor raw placement or poor
pre-display tone. Keep the hidden compensation penalty:

```text
hiddenCompensationPenalty =
  max(0, displayReadabilityScore - rawPlacementScore) * 0.5 +
  max(0, displayReadabilityScore - preDisplayToneScore) * 0.5
```

This penalty is the practical guardrail against an automatic result that looks
good only until the user touches an upstream control.

## Candidate Selection

The early candidate implementation should use a small candidate set:

```text
CurrentFit:
  current recipe + Display Fit only

Base:
  WB policy + RAW Exposure + Display Fit

Balanced:
  Base + conservative Local Range + mild Finish Tone + Display Fit

Farther:
  opt-in stronger Local Range / Finish Tone, still visible and reversible
```

Use the weighted score from the existing docs, but keep every subscore in the
diagnostic payload:

```text
score =
  25 * rawSafetyScore +
  25 * scenePlacementScore +
  15 * localConflictScore +
  10 * toneShapeScore +
  15 * displayReadabilityScore +
  10 * editConservatismScore
  - noisePenalty
  - colorConstancyPenalty
  - hiddenCompensationPenalty
```

Do not choose a candidate only because its final display histogram is pleasing.
The score should reward a starting point that remains understandable after the
user edits RAW Exposure, Local Range, Finish Tone, or Display Fit manually.

## Validation Protocol

The web research cannot decide the final constants. It can, however, define the
validation record that implementation should produce.

For each RAW validation image, log:

```text
source id / camera / ISO / exposure metadata
image category tags
current recipe summary
raw safety stats
neutral scene stats
candidate recipes
candidate stage stats
candidate subscores and penalties
selected candidate and reason
visible fields changed
human review: too dark / too bright / too local / too flat / too finished
next manual control the reviewer would touch
```

Use categories at least like:

```text
normal daylight
high-key snow/beach/interior white room
low-key night or stage
backlit person/object
bright sky landscape
interior with bright window
high ISO shadow lift
mixed/artificial light
intentionally warm or cool scene
flat overcast / low contrast
clipped specular highlights
camera JPEG with strong embedded look
```

FiveK is useful as a broad validation source because it contains RAW inputs and
multiple expert retouches, but STACK should not train toward a single expert
look. Use it to find failure modes and tune starting-point conservatism, not to
declare one "correct" rendering.

## Multi-Pass Implementation Plan

Pass 0 should add the structures and diagnostics without changing behavior.
Define `RawAutoStartPoint` data types, stage enums, subscore structs, and JSON
or UI-readable diagnostics. This pass should compile and expose no new automatic
image change.

Pass 1 should add stage stats readbacks. Start with pre-View-Transform and final
display because the current renderer already has nearby hooks. Then add neutral
scene and raw-placement boundaries. This pass should prove the solver is reading
the right image states before it writes values.

Pass 2 should rename or separate the current View Transform-only action as
`Fit Display` / `Refit Display`. This keeps the existing safe automation clear
before a multi-control starting point arrives.

Pass 3 should implement a dry-run `Build Starting Point` candidate report. It
should generate CurrentFit and Base candidates, score them, and show the summary
in Diagnostics without applying the selected recipe.

Pass 4 should allow Base mode to apply visible RAW Exposure and Display Fit,
with one undo snapshot. Image-derived WB should remain a suggestion unless
metadata is missing and confidence is high.

Pass 5 should add Balanced Local Range authoring. Limit this to one or two
visible graph points with confidence and delta caps. Mirror the result as graph
markers and include the reason in Diagnostics.

Pass 6 should add mild Finish Tone authoring, but only if the compact RAW tab
can show the authored points or bridge clearly to full tone controls. Keep Base
neutral.

Pass 7 should tune constants against the validation set and only then consider
making Balanced more prominent. Do not make Balanced the default until repeated
validation shows that local/tone edits feel like a starting point rather than a
finished look.

## Remaining True Gaps

Three things remain unresolved because they require product validation, not more
web research.

First, final thresholds need real images. The formulas above make the first
implementation debuggable, but values like `targetKeyBelowWhiteEv`,
`highlightMarginEv`, Local Range max lift, and display clipping tolerance should
be tuned with STACK's actual renderer and UI.

Second, subject priority needs a choice. Center/foreground heuristics can carry
the early implementation passes. Face or semantic subject detection would improve some
images, but it is a larger feature and should not block Base mode.

Third, Finish Tone authoring depends on UI fidelity. If the compact RAW tab
cannot clearly expose an automatically authored tone curve, automatic Finish
Tone should remain off in Base and conservative in Balanced.

Everything else is ready enough to begin careful, staged implementation.
