# Automatic RAW Processing Math And Science

Research pass: July 1, 2026.

## Reading Intent

This note extends the starting-point solver research with the actual
image-processing split behind automatic RAW decisions. It is written for two
readers at once: a human trying to understand why the four RAW controls are
separate, and an implementation agent that needs formulas and guardrails.

The main answer is that most user-facing automatic decisions should happen
after demosaicing, white balance, and camera-to-working-color conversion, because
that is the first point where the solver can reason about RGB color, luminance,
regions, and scene appearance. But the solver must still read raw-domain
evidence before demosaicing. The raw mosaic knows things the pretty RGB image
can hide: black level, white level, clipped sensor channels, defective pixels,
and noise/headroom risk.

A good **Build Starting Point** system should therefore keep two ledgers:

```text
Raw safety ledger      = sensor facts before/around demosaic
Scene appearance ledger = demosaiced scene-linear facts after WB/color transform
```

The raw safety ledger should constrain the automatic edit. The scene appearance
ledger should choose the visible manual values.

## Research Grounding

The DNG 1.7.1.0 specification gives the cleanest formal description of the raw
front end. It describes raw-to-linear mapping as linearization, black
subtraction, rescaling, and clipping; it also says early negative values may be
worth preserving for shadow noise reduction. It defines black level, white
level, linear response limit, AsShotNeutral, BaselineExposure, BaselineNoise,
NoiseProfile, and opcode lists that can run before linearization, after
linearization, or after demosaicing:

https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf

darktable's manual is useful because it names the same conceptual boundaries in
an editor. Its raw black/white point and hot pixels modules are raw-domain
technical steps; raw denoise is also before demosaic; exposure is scene-referred
linear RGB; color calibration separates technical WB from later chromatic
adaptation; tone equalizer is scene-referred dodge/burn; and the scene-referred
workflow keeps display compression late:

https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/raw-black-white-point/

https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/hot-pixels/

https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/raw-denoise/

https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/exposure/

https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/color-calibration/

https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/

https://docs.darktable.org/usermanual/development/en/overview/workflow/process/

Adobe's Camera Raw and Lightroom documentation confirms the product rule that
automation should author ordinary controls, not become an invisible output pass.
Camera Raw's Auto analyzes the image and adjusts tone controls; Lightroom
describes Auto tone as setting sliders to maximize tonal scale while minimizing
shadow and highlight clipping:

https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html

https://helpx.adobe.com/lightroom-classic/help/image-tone-color.html

The academic references support the math. Foi et al. model raw sensor noise as a
Poissonian-Gaussian process, which matches DNG's two-term NoiseProfile idea.
Malvar-He-Cutler and Hirakawa-Parks explain why demosaicing is interpolation,
why green carries much luminance structure, and why edge-aware interpolation is
needed to avoid color artifacts. Finlayson and Trezzi's Shades of Gray gives a
practical image-derived WB family. Reinhard et al. explain log-average
luminance, scene key, middle gray, display tone mapping, and local
dodging/burning.

https://webpages.tuni.fi/foi/papers/Foi-PoissonianGaussianClippedRaw-2007-IEEE_TIP.pdf

https://web.stanford.edu/class/ee367/reading/Demosaicing_ICASSP04.pdf

https://www.photo-lovers.org/pdf/hirakawa05mndemosaictip.pdf

https://research-portal.uea.ac.uk/en/publications/shades-of-gray-and-colour-constancy/

https://www.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf

Finally, OpenColorIO gives the naming anchor for Display Fit / View Transform:
a view transform maps between scene-referred and display-referred reference
spaces.

https://opencolorio.readthedocs.io/en/latest/guides/authoring/displays_views.html

The July 1 math gap check adds three more implementation anchors. DNG's raw
mapping and camera-color chapters make clear that raw values, calibration
matrices, and opcode stages are distinct pieces of the pipeline; display
standards make clear that "display luma" is only meaningful if the transfer
function is known; and CIEDE2000 is a useful optional validation metric for
white-balance/color shifts, but too heavy to make the first live statistic.

Sources:
https://www.w3.org/Graphics/Color/sRGB.html

https://registry.color.org/rgb-registry/bt709

https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-3-202502-I%21%21PDF-E.pdf

https://hajim.rochester.edu/ece/sites/gsharma/papers/CIEDE2000CRNAFeb05.pdf

## The Pipeline Split

Before demosaicing, a Bayer RAW image is not an RGB image. Each sensel has one
filtered measurement. A red sensel does not know its own green or blue value,
and a green sensel does not know whether the surrounding red/blue structure is
an edge, a texture, or noise. That means pre-demosaic analysis is excellent for
sensor facts but weak for visual judgments.

After demosaicing, the solver finally has full RGB values at each pixel. After
white balance and camera-to-working-color conversion, those values become a much
better basis for luma, local masks, tone zones, and display fitting. This is why
the four user-facing RAW controls belong mostly on the post-demosaic,
scene-linear side:

```text
raw mosaic facts
  -> linearize / black subtract / normalize
  -> defect and noise correction
  -> demosaic
  -> white balance
  -> camera to working RGB
  -> RAW Exposure
  -> Local Range
  -> Finish Tone
  -> View Transform / Display Fit
```

The practical rule is: use raw-domain evidence to say what is physically safe,
then use scene-linear RGB evidence to decide what looks like a good starting
point.

## Raw Safety Ledger

The raw safety ledger should be built before aesthetic correction and should be
kept separate from the final displayed histogram. Its job is not to decide that
the photo should look brighter, moodier, warmer, or flatter. Its job is to
answer: what did the sensor actually record, what data is already clipped, and
how risky would a proposed edit be?

### Linearization And Normalization

The first formula is the raw-to-linear normalization model. In DNG language, a
stored raw value may first pass through a linearization table, then black level
is subtracted, then the value is scaled by the distance between black and white
level.

```text
linearStored = LinearizationTable(rawStored)
black = blackLevel(cfaColor, x, y)
white = whiteLevel(cfaColor)
rawNorm = (linearStored - black) / max(epsilon, white - black)
```

For ordinary normalized analysis, `rawNorm = 0` means zero light after black
subtraction, and `rawNorm = 1` means the sensor sample has reached the useful
white level. A renderer may clip to `[0, 1]` for display, but the analysis path
should preserve slightly negative values early when possible because they are
useful for shadow-noise estimation and black-level diagnostics.

STACK already has this model in the current GPU raw path. `RawGpuPipeline.cpp`
computes `blackForColor(color)` and returns `(v - black) / (white - black)` in
`rawNormalizedAt`. `LibRawDecoder.cpp` parses DNG black level, white level, and
AsShotNeutral metadata, and it also stores a simple raw clip percentage by
counting raw values at or above the metadata white level.

### Raw Validity Mask And Black Geometry

The previous formula is directionally right, but implementation should not
treat the full stored raster as one uniform statistical field. DNG raw files can
carry active areas, masked areas, default crop regions, per-CFA black patterns,
and horizontal/vertical black deltas. Those details matter because edge pixels,
masked optical-black pixels, and cropped-off margins can bias percentiles if
they are mixed into the scene evidence.

The raw safety pass should produce a validity mask before computing raw
percentiles:

```text
active = inside ActiveArea or decoder active crop
notMasked = not inside MaskedAreas / optical black regions
notMargin = inside default crop or preview-safe active region when measuring appearance
rawValid = active and notMasked and not knownBadPixel
```

Black should be computed in the same geometry:

```text
black(x, y, c) =
  BlackLevelPattern((y - activeTop) mod repeatRows,
                    (x - activeLeft) mod repeatCols,
                    c)
  + BlackLevelDeltaH[x - activeLeft]
  + BlackLevelDeltaV[y - activeTop]
```

When masked areas are present, they can be used as a diagnostic for black-level
drift, but they should not become normal scene pixels. A future solver should
record `activeValidFraction`, `maskedBlackMean`, `maskedBlackStd`, and
`blackDriftWarning` if the observed optical-black level disagrees materially
with metadata. That warning should constrain confidence rather than secretly
rewriting RAW Exposure.

### Lens Shading And Gain Map Awareness

Another pre-demosaic gap is lens shading. Vignetting, microlens falloff, and
profile gain maps can make the raw corners darker than the center even when the
scene itself is evenly lit. If the starting-point solver ignores that, it may
mistake optical falloff for a Local Range shadow problem.

DNG can describe correction stages through opcode lists and newer profile gain
table metadata. STACK does not need the starting-point solver to implement a
full lens-correction engine, but the evidence layer should know whether such
corrections have already been applied or are unavailable.

When a gain map is available and applied by the raw pipeline, record it:

```text
rawFlatCorrected(x, y, c) = rawNorm(x, y, c) * lensGainMap(x, y, c)
lensGainEv(x, y, c) = log2(max(epsilon, lensGainMap(x, y, c)))
```

When gain information is missing, record a conservative falloff diagnostic
instead of automatically fixing it:

```text
centerEv = median(sceneEv(Y) over center region)
cornerEv = median(sceneEv(Y) over corner regions)
cornerFalloffEv = centerEv - cornerEv
lensShadingConfidence =
  hasAppliedGainMap ? high :
  hasLensProfileButNotApplied ? low :
  unknown
```

If `cornerFalloffEv` is high and lens shading confidence is low, Local Range
corner/shadow suggestions should become weaker or advisory. The user may want
the vignette, or the correct fix may belong to a future lens/profile control
rather than the Local Range graph.

### Defective Pixels And Hot Pixels

Hot-pixel and defect correction belong before demosaic because a single bad
sensel can otherwise be interpolated into several RGB pixels. The robust
detector should compare a pixel only to same-CFA-color neighbors, because a red
sensel and a green sensel are not measuring the same filtered light.

One implementation-friendly detector is:

```text
N = same-CFA neighbors around pixel p
medianN = median(N)
madN = median(abs(N - medianN))
sigmaN = 1.4826 * madN

isBrightOutlier = p > medianN + max(absThreshold, k * sigmaN)
isDarkOutlier = p < medianN - max(absThreshold, k * sigmaN)
isHotOrDead = isBrightOutlier or isDarkOutlier

replacement = medianN or edge-aware same-CFA weighted mean
```

STACK's current shader already follows the right family. It computes a
same-CFA neighbor mean with spatial and range weights, then flags hot pixels
when the center is outside neighbor min/max or sufficiently far from the mean.
That gives the future solver a strong raw-domain hook: the automatic starting
point can read or request a hot-pixel count without making that count part of
RAW Exposure, Local Range, Finish Tone, or View Transform.

### Raw Clipping And Headroom

Raw clipping must be judged before tone curves and view transforms, because late
display mapping can make clipped data look visually acceptable. The raw ledger
should compute per-channel clipping, near-clipping, and headroom using the
normalized sensor values.

```text
clipThreshold = min(1.0, linearResponseLimit)
clipped_c = count(rawNorm_c >= clipThreshold) / count(rawNorm_c)
nearClipped_c = count(rawNorm_c >= clipThreshold - margin) / count(rawNorm_c)

rawWhiteP999 = percentile(rawNorm, 99.9)
headroomEv = log2(clipThreshold / max(epsilon, rawWhiteP999))
```

The DNG `LinearResponseLimit` tag is important when present: it says the sensor
may become meaningfully nonlinear before the formal white level. For automatic
processing, that means the safe "do not push beyond this" threshold might be
below 1.0.

This raw headroom number should limit positive RAW Exposure. If the solver wants
`+0.8 EV` but the raw ledger reports only `0.2 EV` of trustworthy highlight
headroom, RAW Exposure should stop early and downstream stages should handle the
remaining visual problem. Local Range can lift the subject, and View Transform
can roll off display highlights, but neither should claim that missing sensor
detail has been restored.

### Channel And WB-Scaled Headroom

The scalar `headroomEv` above is useful, but it is not enough. A RAW file can
have one channel close to clipping while the luma percentile still looks safe.
White-balance gains can also make a near-clipped channel dominate the later
scene-linear RGB even though the raw sensor ledger looked acceptable in a
combined percentile.

The raw ledger should therefore keep per-channel percentiles and a
white-balance-scaled headroom estimate:

```text
rawP999_c = percentile(rawNorm over CFA channel c, 99.9)
rawClip_c = count(rawNorm_c >= clipThreshold_c) / count(rawNorm_c)
rawNearClip_c = count(rawNorm_c >= clipThreshold_c - margin_c) / count(rawNorm_c)

channelHeadroomEv_c =
  log2(clipThreshold_c / max(epsilon, rawP999_c))
```

After choosing a WB policy, compute the processing headroom that remains after
the channel gains:

```text
wbGainNorm_c = wbGain_c / geometric_mean(wbGain_R, wbGain_G, wbGain_B)
wbScaledP999_c = rawP999_c * wbGainNorm_c
wbScaledHeadroomEv_c =
  log2(clipThreshold_c / max(epsilon, wbScaledP999_c))

rawExposurePositiveLimitEv =
  min_c(wbScaledHeadroomEv_c) - highlightMarginEv
```

This does not mean WB can recover clipped sensor data. It means a global RAW
Exposure suggestion should not ignore the channel that will run out of room
first after WB and the camera transform. Diagnostics should separate "sensor
clipped" from "processing headroom low after WB".

The ledger should also classify clipping pattern, because a small area with one
clipped channel is a different risk than large three-channel saturation:

```text
clippedChannelCountAtPixel = clipped_R + clipped_G + clipped_B
singleChannelClipFraction = count(clippedChannelCountAtPixel == 1) / valid
multiChannelClipFraction = count(clippedChannelCountAtPixel >= 2) / valid
fullClipFraction = count(clippedChannelCountAtPixel == 3) / valid
anyClipFraction = singleChannelClipFraction + multiChannelClipFraction
recoverableClipShare =
  singleChannelClipFraction / max(epsilon, anyClipFraction)

highlightRecoverabilityScore =
  highIsGood01(recoverableClipShare, recoverableShareBad, recoverableShareGood)
  * lowIsGood01(multiChannelClipFraction, goodMulti, badMulti)
  * lowIsGood01(fullClipFraction, goodFull, badFull)
```

This score should not promise actual reconstruction. It only tells the solver
whether highlight-protection language should say "display rolloff / possible
single-channel recovery" or "sensor detail is gone".

### Noise And Shadow Lift Risk

The raw noise model should be used as a penalty, not as a hidden beautification
pass. DNG's `NoiseProfile` uses the same broad shape as the Poissonian-Gaussian
model in Foi et al.: signal-dependent photon noise plus signal-independent
readout noise.

```text
noiseStd_i(x) = sqrt(S_i * x + O_i)
snr_i(x) = x / max(epsilon, noiseStd_i(x))
```

If no DNG NoiseProfile is available, a fallback can estimate noise risk from
metadata such as ISO, BaselineNoise, raw shadow variance, and the amount of
shadow lift requested by Local Range. The key conceptual point is that a
post-capture exposure scale makes shadow noise more visible; it does not improve
the original signal-to-noise ratio.

```text
displayLift = exp2(deltaEv)
visibleNoiseStd = displayLift * noiseStd_i(x)
noiseLiftPenalty = liftWeight * max(0, deltaEv - safeLiftEvAtSNR)
```

This should feed candidate scoring. A high ISO file may still need a shadow
lift, but the solver should prefer a smaller, local, visible graph point over a
large global RAW Exposure push that makes the whole frame noisy.

### Noise Propagation Through WB, Exposure, Local Edits, And Color Matrix

The existing noise formula describes raw-channel noise. The starting-point
solver also needs to know how that noise becomes visible after deterministic
gains. A gain does not improve signal-to-noise ratio; it scales both signal and
noise. For each raw channel:

```text
sigmaRaw_c(x) = sqrt(S_c * x + O_c)
gain_c = wbGain_c * exp2(rawExposureEv + localDeltaEv)
sigmaAfterGain_c = abs(gain_c) * sigmaRaw_c(x)
```

If the camera-to-working transform is a 3x3 matrix `M`, approximate RGB noise
covariance after the matrix with:

```text
CovCamera = diag(sigmaAfterGain_R^2,
                 sigmaAfterGain_G^2,
                 sigmaAfterGain_B^2)
CovWorking = M * CovCamera * transpose(M)
```

For luma-noise risk, use the same luma vector as the scene stats:

```text
L = [0.2126, 0.7152, 0.0722]
sigmaY = sqrt(L * CovWorking * transpose(L))
snrY = Y / max(epsilon, sigmaY)
```

This is an approximation because demosaic mixes neighboring sensels and real
noise is not perfectly white, but it is a better guardrail than treating shadow
lift as a single scalar. The implementation can start with a simplified
per-bucket version and add covariance later.

## Demosaic As The Boundary

Demosaicing is not just a convenience step; it changes what can be measured.
Malvar-He-Cutler describe demosaicing as reconstructing full RGB values from a
CFA, with green sampled more densely because it carries much of the luminance
structure. Hirakawa-Parks emphasize that naive interpolation can cause color
artifacts near edges and that directional methods use local structure to reduce
those errors.

For STACK's automatic starting point, the lesson is modest and practical. Use
pre-demosaic raw data for per-channel facts and defects. Use post-demosaic
scene-linear data for luma, region, and tone decisions. Do not build a sky mask,
a foliage mask, a face/subject brightness estimate, or a tone curve from the
raw mosaic alone unless there is a very specific sensor-domain reason.

There is a code caveat worth preserving. STACK currently exposes debug views
named `NormalizedMosaic`, `PreDenoiseMosaic`, `PostDenoiseMosaic`,
`HotPixelMask`, `DemosaicedCameraRgb`, `WhiteBalancedCameraRgb`, and
`CameraTransformedRgb`. However, the shader path currently applies lateral CA
and highlight reconstruction before returning the `DemosaicedCameraRgb` debug
view. So a strict "only demosaiced, no correction" sample is not quite the same
thing as today's debug view. A future starting-point renderer should add named
stage captures instead of overloading debug views.

## Scene Appearance Ledger

Once the image is demosaiced, white-balanced, and converted into working linear
RGB, the solver can measure the image in the same domain that the four manual
controls actually affect. This is the right place to determine the base visual
state.

The shared luma and EV measurements should stay simple:

```text
Y = 0.2126 * R + 0.7152 * G + 0.0722 * B
logAverageY = exp(mean(log(epsilon + Y)))
ev(y, ref) = log2(max(epsilon, y) / max(epsilon, ref))
scaleEv(deltaEv) = exp2(deltaEv)
```

For every stage, record robust percentiles:

```text
p01, p05, p25, p50, p75, p95, p99, p999
```

These percentiles should exist in both luma and EV space. They should be
computed over a valid-pixel mask that excludes transparent/invalid pixels,
obvious hot-pixel replacements if needed, and sensor-clipped samples when those
samples would distort a statistic.

### Linear Luma, Negative Channels, And Gamut Warnings

The luma formula above assumes scene-linear working RGB. It should not be mixed
with gamma-encoded display values unless the display transfer has been inverted
or the metric is explicitly a display-code metric.

Camera-to-working matrices can produce negative channel values or values outside
the nominal working/display gamut. Those values are not automatically wrong:
negative components can occur near saturated colors or after a matrix maps a
camera color outside the target space. But log statistics cannot accept
negative luminance, and local masks should not mistake matrix overshoot for real
scene darkness.

For scene statistics:

```text
rgbForStats = max(rgb, 0)
Y = dot(rgbForStats, lumaCoefficients)
logY = log(max(epsilon, Y))
negativeChannelFraction = count(any(rgb < -negativeTol)) / valid
wideGamutPressure = count(any(rgb > gamutSoftLimit) or any(rgb < -negativeTol)) / valid
```

The solver should keep these as warnings and confidence modifiers. It should
not simply clamp the image and forget that clamping occurred.

### White Balance Evidence

White Balance is not one of the four controls the user is trying to visually
structure, but it is a prerequisite for the four. A bad WB changes luminance,
color-qualified masks, and perceived contrast. The safest product policy
remains: prefer camera/as-shot metadata when available, compute image-derived WB
as evidence, and apply it automatically only when metadata is missing or the
confidence is unusually strong.

For image-derived evidence, use a neutral candidate mask:

```text
valid = not rawClipped
valid = valid and Y between p20 and p85
valid = valid and saturation < saturationThreshold
valid = valid and localGradient not extreme
valid = valid and sampleCount >= minimumNeutralPixels
```

Then compute a Shades-of-Gray estimate:

```text
I_R = mean(R^p)^(1/p)
I_G = mean(G^p)^(1/p)
I_B = mean(B^p)^(1/p)

target = geometric_mean(I_R, I_G, I_B)
gain_R = target / I_R
gain_G = target / I_G
gain_B = target / I_B

normalize so gain_G = 1.0
```

`p = 1` behaves like Gray World. Higher values move toward Max-RGB behavior;
the Shades of Gray paper reports strong results around an L6 norm on its
dataset. The important UI consequence is that this is an assumption, not a
truth. A scene full of red curtains or blue stage light can defeat global
image-derived WB. That is why the automatic starting point should treat
image-derived WB as a suggestion by default.

For stronger validation, candidate WB can also be judged in a perceptual color
space on carefully chosen neutral samples. A cheap live metric can stay in
log-channel space:

```text
neutralLogError =
  median(abs(log2(R/G))) + median(abs(log2(B/G)))
```

An offline validation harness can additionally compute CIEDE2000 between
neutral samples before and after the candidate. That metric is useful for human
review because it is perceptual, but the implementation notes by Sharma, Wu,
and Dalal warn that CIEDE2000 is easy to implement incorrectly and needs test
data. Treat it as validation support, not the first live solver dependency.

### Camera Matrix And Gamut Confidence

WB confidence should include the color transform path. DNG may provide
ForwardMatrix tags, ColorMatrix tags, camera calibration matrices, analog
balance, and multiple illuminant calibrations. A future implementation does not
need to solve full color science in the starting-point module, but the solver
should know whether the scene stats came from a strong or weak color pipeline.

```text
matrixConfidence =
  hasForwardMatrix ? high :
  hasColorMatrix ? medium :
  fallbackMatrix ? low :
  none

matrixGamutConfidence =
  lowIsGood01(negativeChannelFraction, negativeGood, negativeBad)
  * lowIsGood01(wideGamutPressure, gamutGood, gamutBad)
```

If `matrixConfidence` is low or gamut pressure is high, reduce confidence in
image-derived WB, color-qualified Local Range suggestions, and color-based sky
classification. RAW Exposure and View Transform can still proceed from luma
with conservative limits.

### RAW Exposure Evidence

RAW Exposure owns the global placement of the scene. It should use the
demosaiced scene-linear ledger for the visual key, but use the raw safety ledger
for highlight limits.

Reinhard et al. use log-average luminance as a scene-key estimate and map
normal-key scenes toward 18 percent middle gray. In EV terms, 0.18 is about
`2.47 EV` below 1.0:

```text
log2(0.18 / 1.0) = -2.47 EV
```

STACK already uses a nearby policy in the current Auto Base exposure suggestion:
it targets a subject/median value about `2.7 EV` below a white anchor and clamps
the recommendation conservatively. A stronger solver can keep that logic but
make the evidence explicit.

```text
keyY = robustBlend(logAverageY, p50Y, subjectMedianY)
whiteAnchorY = p999Y or raw-safe white anchor
targetKeyBelowWhiteEv = 2.5 to 3.0

targetKeyY = whiteAnchorY * exp2(-targetKeyBelowWhiteEv)
wantedDeltaEv = log2(targetKeyY / max(epsilon, keyY))

maxPositiveDeltaEv = rawHeadroomEv - highlightMarginEv
safeDeltaEv = min(wantedDeltaEv, maxPositiveDeltaEv)
safeDeltaEv = clamp(safeDeltaEv, modeMinEv, modeMaxEv)
```

High-key and low-key scenes are the hard part. A snow field and a night street
can both be "correct" while violating a generic histogram target. The starting
point should therefore score the confidence of the key estimate. If the entire
image is high-luma with low raw clipping, it may be high-key and should not be
dragged down to ordinary middle gray. If the entire image is dark but contains
small bright highlights, it may be low-key and should not be forced into a flat
daylight histogram.

```text
highKeyEvidence = p50Y high and rawClipLow and shadowMassLow
lowKeyEvidence = p50Y low and highlightAreaSmall and subjectConfidenceLow
keyConfidence = 1 - max(highKeyAmbiguity, lowKeyAmbiguity, colorCastRisk)
```

When key confidence is low, the solver should reduce RAW Exposure strength and
lean more on Display Fit for readability.

### Local Range Evidence

Local Range owns conflicts. A conflict exists when the whole image cannot be
placed well with one exposure multiplier. Backlit subjects, bright skies, and
large shadow masses are the usual examples.

The local solver should start from the raw-placement candidate. It should ask:
after RAW Exposure and WB, what regions still disagree with the global answer?

```text
regionEv = median(sceneEv(Y) over regionMask)
globalEv = median(sceneEv(Y) over valid image)
contrastToGlobalEv = regionEv - globalEv
```

For an "open shadows" suggestion, the solver might estimate:

```text
shadowMask = Y < exp2(globalEv - 2.0) * middleGrey
shadowMass = area(shadowMask)
shadowMedianEv = median(sceneEv(Y) over shadowMask)

wantedLiftEv = targetShadowEv - shadowMedianEv
localDeltaEv = clamp(wantedLiftEv, 0.0, maxShadowLiftEv)
localDeltaEv *= highlightSafetyFactor
localDeltaEv *= noiseSafetyFactor
```

For a sky or highlight hold, the sign flips:

```text
brightRegionEv = median(sceneEv(Y) over skyOrHighlightMask)
wantedHoldEv = brightRegionEv - targetBrightRegionEv
localDeltaEv = -clamp(wantedHoldEv, 0.0, maxHoldEv)
```

The authored result should be a visible graph point with anchors:

```text
point(targetEv, deltaEv)
anchor(targetEv - widthEv / 2, 0)
anchor(targetEv + widthEv / 2, 0)
localScale = exp2(deltaEv)
```

That equation matters for UI honesty. The automatic solver is not "improving
the image" in the abstract. It is placing explicit points on the Local Range
graph, and those points multiply the selected scene-EV zones by exposure
factors.

### Local Mask Smoothness And Halo Risk

The Local Range math still needs one more guardrail: a local correction is only
trustworthy if its mask transitions are visually plausible. Guided or
edge-aware filtering is appropriate because it can smooth noisy masks while
respecting image edges, but the solver should still score the result for halos
or leakage.

Implementation can start with simple mask diagnostics:

```text
guideGradient = length(gradient(logY))
maskGradient = length(gradient(mask))

edgeAlignedTransition =
  mean(maskGradient where guideGradient > edgeThreshold)

floatingTransition =
  mean(maskGradient where guideGradient <= flatThreshold)

maskTransitionConfidence =
  highIsGood01(edgeAlignedTransition, edgeBad, edgeGood)
  * lowIsGood01(floatingTransition, floatingGood, floatingBad)

haloRisk = 1 - maskTransitionConfidence
```

Strong mask transitions are safer when they coincide with real image edges and
riskier when they float through smooth areas. This score should reduce Local
Range confidence or force the suggestion to remain a ghost marker instead of an
applied graph point.

### Finish Tone Evidence

Finish Tone owns global relationship, not rescue. It should not perform hidden
display compression, and it should not solve a local subject problem that Local
Range can express more directly.

The useful evidence is the pre-display tone distribution after RAW Exposure and
Local Range:

```text
midSpreadEv = p75Ev - p25Ev
wideSpreadEv = p95Ev - p05Ev
endpointPressure = max(0, p99Ev - desiredWhiteEv) + max(0, desiredBlackEv - p01Ev)
```

A mild automatic tone curve is reasonable only when the image is flat after the
earlier stages have done their jobs. In Base mode, Finish Tone can stay neutral.
In Balanced mode, use visible points and keep strength modest:

```text
logMinEv = floor(p01Ev - 0.5)
logMaxEv = ceil(p99Ev + 0.5)
x(ev) = clamp((ev - logMinEv) / max(epsilon, logMaxEv - logMinEv), 0, 1)

flatness = clamp((targetMidSpreadEv - midSpreadEv) / targetMidSpreadEv, 0, 1)
strength = min(0.35, flatness * modeToneLimit)

points = [
  (0.00, 0.00),
  (0.25, 0.25 - 0.07 * strength),
  (0.50, 0.50),
  (0.75, 0.75 + 0.07 * strength),
  (1.00, 1.00)
]
```

If endpoint pressure is high, reduce Finish Tone strength. That pressure should
belong to View Transform shoulder/toe and display bounds, not to a hidden
global contrast curve.

### View Transform Evidence

View Transform owns final display mapping. It should be fit after the upstream
candidate is chosen, because RAW Exposure, Local Range, and Finish Tone all
change its input.

The current STACK fit is already in the right family: set middle gray from
current-frame median, derive white/black EV distances from high/low
percentiles, then choose shoulder, toe, contrast, and margins. A staged solver
should make that dependency explicit:

```text
middleGrey = clamp(p50Y, 0.01, 1.0)
whiteAnchorEv = ev(p999Y, middleGrey)
blackAnchorEv = ev(p01Y, middleGrey)

whiteEv = clamp(whiteAnchorEv + whiteMarginEv, minWhiteEv, maxWhiteEv)
blackEv = -clamp(abs(blackAnchorEv) + blackMarginEv, minBlackDepthEv, maxBlackDepthEv)

shoulder = baseShoulder + shoulderGain * displayHighlightPressure
toe = baseToe + toeGain * shadowCompressionPressure
```

The display candidate should then be measured in display space:

```text
displayClipHigh = count(displayY >= 1.0 - displayEps) / validCount
displayClipLow = count(displayY <= displayEps) / validCount
displayMid = percentile(displayY, 50)
displaySpread = percentile(displayY, 95) - percentile(displayY, 5)
```

These metrics can reject a candidate that is unreadable on screen, but they
should not be the only score. A candidate should not win only because the View
Transform hid poor upstream placement.

### Display Transfer And Code-Value Metrics

Display Fit math needs one explicit domain label. A renderer may hold final
display pixels as linear display RGB, sRGB-like code values, or future HDR
values such as PQ/HLG. Percentiles mean different things in each domain.

For SDR/sRGB-like output, keep two families of metrics when possible:

```text
displayLinearRgb = inverseTransfer(displayCodeRgb)
displayLinearY = dot(max(displayLinearRgb, 0), lumaCoefficients)
displayCodeY = dot(clamp(displayCodeRgb, 0, 1), lumaCoefficients)
```

Use `displayLinearY` for physical clipping/readability checks and `displayCodeY`
for UI-facing histogram/readout behavior if the UI histogram is code-value
based. If STACK later supports HDR output, the view transform must declare the
transfer family and peak/reference luminance before display clipping thresholds
can be compared across SDR, PQ, or HLG.

## One-Time Versus Iterative Automation

A one-time automatic action should be bounded and inspectable. It should gather
raw safety evidence, render a neutral scene baseline, generate a small set of
candidates, score them at multiple stages, and write the selected values into
manual controls.

```text
1. Build raw safety ledger from raw buffer and metadata.
2. Render neutral scene-linear baseline.
3. Estimate WB policy and RAW Exposure.
4. Render raw-placement candidate.
5. Add Local Range only for unresolved conflicts.
6. Add mild Finish Tone only when pre-display tone is flat.
7. Fit View Transform after upstream edits are selected.
8. Score pre-display and final-display results.
9. Apply selected visible recipe values.
```

Iterative processing should not mean an endless loop that keeps changing sliders
until the final histogram looks nice. It should mean a bounded candidate search
where later evidence can reject or reduce an earlier idea. For example, the
solver can try a `+0.6 EV` RAW Exposure, discover that raw headroom or display
highlight pressure is too high, and produce a second candidate that uses
`+0.25 EV` RAW Exposure plus a local shadow lift instead.

The important dependency is:

```text
raw evidence constrains exposure
exposure candidate reveals local conflicts
local candidate reveals pre-display tone
pre-display tone informs finish curve
selected upstream result informs display fit
final display checks readability
```

The solver should store the measurements that caused the selected result so the
Diagnostics drawer can explain the decision in human language.

## Candidate Score

The scoring model should stay understandable enough that it can be debugged.
This is more valuable than a clever opaque optimizer.

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

`rawSafetyScore` is mostly raw clipping, headroom, and noise risk. It is a
guardrail. `scenePlacementScore` asks whether the scene key is plausibly placed
after RAW Exposure. `localConflictScore` asks whether local problems improved
without excessive graph deltas. `toneShapeScore` asks whether pre-display
contrast is useful without endpoint crushing. `displayReadabilityScore` asks
whether the final screen result is legible. `editConservatismScore` rewards a
starting point over a finished look.

The hidden compensation penalty is the philosophical center:

```text
hiddenCompensationPenalty =
  penalty if final display score is good
  but raw placement or pre-display tone score is poor
```

This prevents View Transform from disguising an upstream failure.

## Current STACK Hooks

The current codebase already contains more raw-domain infrastructure than the
current Auto Base UI exposes.

| Code Area | Existing Hook | Solver Meaning |
| --- | --- | --- |
| `src/Raw/LibRawDecoder.cpp` | Parses DNG black level, white level, AsShotNeutral, color matrices, and LibRaw fallbacks. | The raw safety ledger can be built from real metadata. |
| `src/Raw/LibRawDecoder.cpp` | Computes raw min/max and default white clip percent. | Starting-point scoring can distinguish sensor clipping from display clipping. |
| `src/Raw/LibRawDecoder.cpp` | Parses only a limited DNG `OpcodeList2` GainMap path from the DNG supplement parser. | Do not treat STACK as having full DNG opcode/profile correction. |
| `src/Raw/RawGpuPipeline.cpp` | Normalizes raw values using per-channel black and white levels. | Matches the DNG raw-to-linear model. |
| `src/Raw/RawGpuPipeline.cpp` | Applies parsed DNG `OpcodeList2` GainMap values to normalized raw mosaic samples before demosaic when available. | Lens/gain-map awareness can record "limited OpcodeList2 GainMap applied" as higher confidence than "unknown", but it should not claim `ProfileGainTableMap` support. |
| `src/Raw/RawGpuPipeline.cpp` | Has same-CFA hot-pixel suppression and mosaic denoise settings. | Defect/noise correction is already pre-demosaic in spirit. |
| `src/Raw/RawImageData.h` | Has debug views for normalized mosaic, pre/post denoise, hot pixel mask, demosaiced camera RGB, WB camera RGB, transformed RGB, and clipped raw channels. | These are close to the needed stage captures, but debug views should not be treated as stable solver readback stages. |
| `src/Raw/RawImageAnalysis.cpp` | Current frame analysis is built from stats before View Transform. `hasActiveArea` is inferred from dimensions, and the linear response limit is approximated from `defaultWhiteClipPercent`, not parsed from the DNG `LinearResponseLimit` tag. | Good for Fit Display, insufficient for full starting-point solving or true raw safety. |
| `src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp` | Renderer order is RAW GPU, local, Finish Tone, stats before View Transform, then View Transform. | Future start-point readbacks need named boundaries before final display. |
| `src/Renderer/Internal/RenderPipelineReadback.cpp` | `ReadTextureStats` reads GL_FLOAT texture data, computes Rec.709-style luma, and counts simple over-1.0 / near-edge display percentages. | Useful as stage texture stats; not a raw-domain validity, active-area, or CFA-channel statistic. |

The biggest implementation gap is not the math; it is stage capture. The future
solver needs explicit readbacks for raw safety, neutral scene, raw placement,
local candidate, pre-display tone, and final display. Without those boundaries,
the candidate score will keep confusing "looks fine after display mapping" with
"the upstream manual controls are well placed."

## Implementation Contract

The following contract should guide future code work.

Use pre-demosaic/raw-domain evidence for:

```text
black and white level normalization
masked/active area facts
raw clipping and near-clipping
linear response limit
hot/dead pixel detection
raw denoise confidence
noise/headroom estimates
camera/as-shot WB metadata
```

Use post-demosaic scene-linear evidence for:

```text
luminance percentiles
log-average scene key
subject/center brightness
sky/foliage/shadow/local masks
image-derived WB evidence
RAW Exposure target selection
Local Range graph targets
Finish Tone curve strength
View Transform input fitting
```

Use final display evidence only for:

```text
display clipping
readability
preview brightness
final contrast sanity
whether View Transform needs refit
```

Do not use final display evidence to prove that RAW Exposure, Local Range, or
Finish Tone were correct. That is the mistake that creates nice-looking
automatic results that fall apart when the user edits upstream controls.

## Open Research And Validation Questions

The constants still need image-based validation. The formulas above give the
shape of the solver, but not the final product values. STACK still needs a RAW
validation set that includes high-key snow/beach images, low-key night scenes,
backlit portraits, bright sky landscapes, interiors with windows, high ISO
shadow lifts, mixed light, intentionally colored scenes, clipped speculars,
flat overcast scenes, and camera JPEG previews.

The key values to tune are:

```text
highlightMarginEv
targetKeyBelowWhiteEv
per-channel near-clip margins
WB-scaled headroom margin
minimum neutral sample count
neutral saturation threshold
safe shadow lift by SNR
Local Range max lift/hold per mode
Local Range halo/leakage thresholds
Finish Tone strength cap
display clipping tolerance
display transfer / code-value histogram policy
hidden compensation penalty weight
```

The product choice is equally important: Base mode should probably stay
conservative until validation proves that automatic local and tone edits are
trustworthy. Balanced mode can be more useful later, but only if every authored
change remains visible in the manual RAW controls.
