# Auto Starting Point Solver Research

Research pass: July 1, 2026. Readability revision: July 1, 2026.

## Reading Intent

This document is the bridge between research and implementation. It should read
like a research note first: what problem each control owns, what evidence it
should read, and why the proposed math makes sense. Formula blocks and compact
tables are kept because they are the parts an implementation agent will need
when turning the design into code.

The solver described here is not a hidden "auto enhance" pass. Its job is to
choose starting values for the visible RAW controls: RAW Exposure, Local Range,
Finish Tone, and View Transform. White Balance is included as a prerequisite
because bad WB destabilizes luminance statistics, local color masks, and
perceived contrast.

For the deeper split between raw-domain evidence and post-demosaic
scene-linear evidence, read `auto-raw-processing-math-and-science.md` beside
this file. That companion note explains which automatic decisions can be made
from raw mosaic data, which decisions should wait until demosaiced RGB exists,
and how the raw safety ledger should constrain the visible manual controls.

## Research Grounding

Several external systems point in the same general direction. Adobe Camera Raw
documents Auto as an image analysis step that adjusts visible tone controls, and
warns that automatic tone is best treated as an initial approximation rather
than a final truth:
https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html

Lightroom Classic describes Auto tone as setting sliders to maximize tonal
scale while minimizing highlight and shadow clipping. That is useful because it
frames automation as slider authoring, not a separate rendering path:
https://helpx.adobe.com/lightroom-classic/help/image-tone-color.html

For local work, Lightroom's masking docs describe local Exposure corrections as
traditional dodging and burning. darktable's tone equalizer is even closer to
STACK's Local Range model: it uses a guided luminosity mask and an exposure
graph whose horizontal axis is brightness and whose vertical axis is exposure
change:
https://helpx.adobe.com/lightroom-classic/help/masking.html
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/

For global exposure and display mapping, darktable is a strong scene-referred
reference. Its exposure module works in linear, scene-referred RGB and can
automatically shift a selected histogram percentile to a target level. Its
scene-referred workflow then sets scene brightness before using filmic-style
white and black relative exposures to map scene values to display range:
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/exposure/
https://docs.darktable.org/usermanual/4.2/en/overview/workflow/process/
https://docs.darktable.org/usermanual/4.0/en/module-reference/processing-modules/filmic-rgb/

The math literature supports the same split. Reinhard et al. use log-average
luminance as an estimate of scene key before tone reproduction and optional
dodging-and-burning. Finlayson and Trezzi's Shades of Gray paper gives a
practical family of image-derived white-balance estimates, generalizing
Gray-World and Max-RGB through a Minkowski norm. LibRaw's histogram-based
auto-brightness and clipping thresholds are a useful reminder that highlight
tolerance must be explicit policy.

Sources:
https://www.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf
https://research-portal.uea.ac.uk/en/publications/shades-of-gray-and-colour-constancy/
https://www.libraw.org/docs/API-datastruct-eng.html

## STACK Interpretation

The exact solver is a STACK product decision. The external sources do not tell
us the perfect values for this codebase. They do, however, support a principled
structure: first establish scene placement, then resolve local conflicts, then
shape global tone, then fit the result to the display.

That means the candidate builder should operate in dependency order. It should
resolve WB policy first, determine a RAW Exposure candidate, render enough to
measure that raw-placement candidate, add Local Range only for unresolved local
conflicts, choose a mild Finish Tone if needed, fit View Transform from the
selected upstream candidate, and only then judge the final display output.

The following table is the compact implementation map for that chain.

| Stage | Reads | Writes | Primary Responsibility |
| --- | --- | --- | --- |
| WB prerequisite | metadata, neutral samples, residual color | WB fields when trusted | Stabilize color/luminance evidence. |
| RAW Exposure | key luminance, white anchor, headroom | `preToneExposureEv` | Place the whole scene. |
| Local Range | regional EV/color evidence | `localRange` graph/masks | Resolve local conflicts. |
| Finish Tone | pre-display contrast stats | `finishTone.layerJson` | Shape global tone, mildly. |
| View Transform | selected upstream scene stats | `viewTransform.layerJson` | Map scene to display. |

## Shared Measurements

Every stage should share the same basic measurements so candidate scores are
comparable. Luminance should use the standard Rec. 709 coefficients currently
used in STACK code:

```text
Y = 0.2126 * R + 0.7152 * G + 0.0722 * B
ev(y, ref) = log2(max(epsilon, y) / max(epsilon, ref))
sceneEv(y) = log2(max(epsilon, y) / middleGrey)
scaleEv(deltaEv) = exp2(deltaEv)
```

For each sampled stage, compute robust percentiles such as p01, p05, p25, p50,
p75, p95, p99, and p999 in both luma and EV. Also record log-average luma,
valid-pixel percentage, display clipping when the stage is display-referred,
sensor or channel clipping when raw-domain evidence exists, and any local
component summaries needed by Local Range. Percentiles should be treated as
robust evidence, not as a claim that the image has one correct mathematical
answer.

## White Balance As A Prerequisite

White Balance should be solved or at least classified before the four main
controls because it changes the evidence those controls read. If WB is wildly
wrong, a luminance histogram can still look plausible, but color-qualified
Local Range masks and perceived tone become unreliable.

The safest policy is to prefer camera/as-shot metadata when available. That
matches common RAW-editor practice and respects capture intent. Image-derived
WB should still be computed as evidence, but it should normally appear as a
visible suggestion unless metadata is missing or confidence is extremely high.
Mixed-light and stylized scenes should not receive a global image-derived WB
automatically.

The formula to compute an image-derived candidate can follow Shades of Gray:

```text
I_R = mean(R^p)^(1/p)
I_G = mean(G^p)^(1/p)
I_B = mean(B^p)^(1/p)

target = geometric_mean(I_R, I_G, I_B)
gain_R = target / I_R
gain_G = target / I_G
gain_B = target / I_B

normalize gains so gain_G = 1.0
```

Gray World is the same family with `p = 1`; STACK's current Shades-of-Gray path
uses `p = 6`. The neutral sample pool should exclude clipped pixels, very dark
or very bright extremes, and highly saturated pixels. Confidence should fall
when too few pixels are eligible, when candidate gains are extreme, or when the
scene appears intentionally colored.

Implementation anchor:

| Condition | Action |
| --- | --- |
| Camera/as-shot exists | Keep as default; show image-derived WB as suggestion only. |
| No metadata and strong neutral evidence | May apply visible WB multipliers. |
| Mixed/stylized scene | Do not apply global image-derived WB automatically. |

## RAW Exposure Solver

RAW Exposure owns global scene placement. It should answer whether the whole
scene is under- or over-placed before local or display mapping tries to make it
look readable. It should not brighten a shadow subject at the cost of blowing
out a bright sky; that is a Local Range or View Transform problem.

The base method should be percentile-to-target exposure. This follows the same
family as darktable's automatic exposure, where a selected histogram percentile
is shifted to a target level. STACK can use p50, log-average luminance, and
possibly subject/center median as the scene key, then compare that key to a
target relative to the white anchor.

```text
observedKeyEv = log2(max(epsilon, keyY))
targetKeyEv = whiteAnchorEv - targetMedianRelativeToWhiteEv
rawDeltaEv = targetKeyEv - observedKeyEv
```

Current STACK code already uses a conservative median-relative-to-white idea:

```text
targetMedianRelativeToWhiteEv = -2.7
rawDeltaEv = clamp(targetMedianEv - subjectMedianEv, -0.5, +1.0)
```

A stronger staged solver should explicitly limit positive exposure by highlight
headroom:

```text
highlightHeadroomEv = log2(clipLimit / max(epsilon, p999Y))
maxPositiveDeltaEv = highlightHeadroomEv - highlightMarginEv
safeDeltaEv = min(rawDeltaEv, maxPositiveDeltaEv)
```

Early implementation passes should keep these limits conservative. A reasonable
starting point is `highlightMarginEv = 0.25` to `0.50`, Base clamped to about
`[-0.75, +0.75]`, Balanced to about `[-1.00, +1.00]`, and Farther to about
`[-1.50, +1.25]`. These are not final truths; they are validation targets.

The key statistic should be a weighted choice rather than a permanent single
number. General scenes can use p50/log-average. A center or subject median
should only influence the key when subject confidence is high. Very high
percentiles should anchor headroom, not define the scene key.

## Local Range Solver

Local Range owns conflicts where different regions need different exposure
answers. It should be used when a global RAW Exposure move would damage another
part of the frame. This is conceptually close to dodging and burning, and
technically close to darktable's tone equalizer model of using a luminosity
mask plus an EV graph.

STACK's current local suggestion code already points in the right direction. It
can analyze a scene-linear pre-Local-Range image, estimate sky and foliage
areas, measure shadow mass, check center/foreground median EV, and detect
backlit contrast. The solver policy should decide which of those suggestions
are safe to apply automatically.

The graph-writing model should stay simple and visible. A local adjustment is a
target EV with a delta EV, plus neutral anchor points around it:

```text
point(targetEv, deltaEv)
anchor(targetEv - widthEv / 2, 0)
anchor(targetEv + widthEv / 2, 0)
localScale = exp2(deltaEv)
```

The following table keeps the current detector logic in implementation-friendly
form.

| Problem | Trigger Sketch | Authored Graph |
| --- | --- | --- |
| Backlit subject | bright background minus center median exceeds about 2.5 EV | Broad lift around center/foreground EV, about +0.5 to +1.0 EV. |
| Protect sky | sky area is meaningful and brighter than foreground | Hold around sky P70 EV, color-qualified to blue/cyan. |
| Open shadows | shadow mass below p50 - 2 EV is large | Lift around shadow median EV, reduced when highlight risk exists. |
| Display highlight pressure | display clip exists but sensor/channel clip is low | Small high-luma hold, labeled as display protection. |

Base mode should normally not apply Local Range unless the evidence is very
strong. Balanced mode may apply one or two conservative visible graph points.
Farther mode may apply more, but still should not hide the result away from the
Local Range graph. High ISO shadow lifts should be penalized because they can
make noise more visible.

## Finish Tone Solver

Finish Tone owns global tonal relationship and contrast. This is the easiest
stage to overuse. If it rescues bad RAW Exposure, brightens one local subject,
or performs final display compression, it becomes confusing because the user no
longer knows which control is responsible for the result.

The first starting-point implementation should therefore treat Finish Tone as
mild. Base mode can leave it neutral. Balanced mode can apply a visible, modest
S-curve only when the image is flat after RAW Exposure and Local Range have had
their chance. Farther mode can explore stronger shaping later, but only after
the compact RAW tab can clearly show every authored point or provide a bridge to
full tone controls.

The proposed first-pass curve works in log scene domain. It maps scene EV into
curve x coordinates using the sampled pre-display range:

```text
logMinEv = floor(p01Ev - 0.5)
logMaxEv = ceil(p99Ev + 0.5)
x(ev) = clamp((ev - logMinEv) / (logMaxEv - logMinEv), 0, 1)
```

For Base, use a neutral line:

```text
points = [(0,0), (1,1)]
```

For a mild Balanced curve, use visible points:

```text
strength = clamp(remap(midSpreadEv, 1.0, 3.0, 0.35, 0.0), 0, 0.35)
shadowY = 0.25 - 0.07 * strength
lightY = 0.75 + 0.07 * strength
points = [(0,0), (0.25, shadowY), (0.50,0.50), (0.75, lightY), (1,1)]
```

If p99 minus p01 is already very large, reduce Finish Tone strength and let View
Transform shoulder/toe handle display compression. That keeps Finish Tone from
becoming a second, hidden display mapper.

## View Transform / Display Fit Solver

View Transform owns final scene-to-display mapping. It is the safest current
automatic edit because it can make the image readable without changing RAW
Exposure. In a Build Starting Point candidate, View Transform should always be
computed after the upstream candidate has been chosen. If the user later edits
RAW Exposure, Local Range, or Finish Tone, Display Fit should become stale or
available for refit rather than silently changing every frame.

Current STACK code is already close to the needed fit. It uses current-frame
percentiles to choose middle grey, white EV, black EV, shoulder, toe, and
contrast:

```text
middleGrey = clamp(p50Y, 0.01, 1.0)
whiteAnchorEv = ev(p999Y, middleGrey) or ev(p99Y, middleGrey)
blackAnchorEv = ev(p01Y, middleGrey)

whiteEv = clamp(whiteAnchorEv + whiteMarginEv, 2.5, 10.0)
blackEv = -clamp(abs(blackAnchorEv) + blackMarginEv, 4.0, 14.0)

shoulder = lerp(0.20, 0.60, highlightRisk)
toe = lerp(0.15, 0.45, shadowCompressionRisk)
contrast = clamp(1.15 - 0.04 * ((whiteEv + abs(blackEv)) - 10.0), 0.90, 1.20)
```

The shader-side mental model is:

```text
black = middleGrey * exp2(blackEv)
white = middleGrey * exp2(whiteEv)
norm = max(0, input * exp2(exposure) - black) / max(epsilon, white - black)
norm = pow(norm, contrast)
mapped = shoulder_curve_with_toe(norm)
```

This is where display clipping and readability should be judged. It should not
be used to prove that upstream placement was good; that is why pre-display
candidate metrics matter.

## Candidate Generation And Scoring

The solver should compare a small number of candidates, not run an unbounded
optimization loop. A practical first set is:

| Candidate | Contents |
| --- | --- |
| Current fit | Current/default recipe plus Fit Display only. |
| Base | RAW Exposure/WB policy plus Fit Display. |
| Balanced | Base plus conservative Local Range, mild Finish Tone, and Fit Display. |
| Farther | Stronger opt-in variant, still fully visible and reversible. |

Each candidate should be rendered at analysis preview size and measured at
multiple stages: raw placement, local candidate, pre-View-Transform, and final
display. This mirrors the lesson from the Advanced Develop candidate feedback
path: final tone can hide upstream differences, so final display metrics are
not enough.

The scoring model should remain understandable:

```text
score =
  30 * rawPlacementScore +
  20 * highlightSafetyScore +
  15 * localProblemScore +
  15 * preDisplayToneScore +
  15 * finalDisplayScore +
   5 * editConservatismScore
  - noisePenalty
  - hiddenCompensationPenalty
```

Raw placement score rewards a sensible scene key and no new clipping. Local
problem score rewards actual improvement in backlit subjects, shadow mass, or
sky protection. Pre-display tone score rewards useful global contrast without
endpoint crushing. Final display score rewards readability, low display
clipping, and highlight rolloff. The hidden compensation penalty is important:
a candidate should not win merely because View Transform hid poor upstream
placement.

## Mode Policy

Base mode should be conservative and trust-building. It may apply WB metadata,
a modest RAW Exposure correction, and View Transform fit. It should avoid
image-derived WB when metadata exists, avoid strong Finish Tone, and normally
avoid Local Range.

Balanced mode is the likely future default after validation. It may apply one
or two conservative Local Range points, a mild visible S-curve, and a final
Display Fit. It should still feel like a starting point, not a finished image.

Farther mode is opt-in. It can use stronger local graph points and stronger
tone shaping, but it must still write visible manual controls and preserve a
clear undo path.

## Implementation Contract

This final table is for implementation agents. It compresses the prose above
into guardrails.

| Step | Must Do | Must Not Do |
| --- | --- | --- |
| WB | Prefer camera/as-shot; compute image-derived evidence. | Apply global image WB in mixed/stylized scenes. |
| RAW Exposure | Use robust key plus highlight headroom. | Brighten globally through highlight risk. |
| Local Range | Write visible graph/mask values. | Hide subject/local edits outside the graph. |
| Finish Tone | Start neutral or mild with visible points. | Use hidden advanced tone fields the RAW tab cannot explain. |
| View Transform | Fit after selected upstream candidate. | Continuously refit while the user drags upstream controls. |
| Scoring | Compare staged metrics and final display. | Let final display alone decide the winner. |

## Open Decisions

The next unresolved questions are validation questions, not basic theory. STACK
still needs an image set to tune the key luminance target, decide whether Base
mode ever applies Local Range, choose a display-clipping threshold that feels
like a starting point rather than a finished edit, and decide how much to use
camera embedded JPEG rendering as reference. RawTherapee-style embedded JPEG
matching remains useful context, but RawPedia was unavailable during the pass
and the usable notes were secondary/community sources, so this solver should
not yet depend on that approach.
