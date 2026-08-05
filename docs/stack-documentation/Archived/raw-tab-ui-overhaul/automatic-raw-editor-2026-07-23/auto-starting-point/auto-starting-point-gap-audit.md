# Auto Starting Point Gap Audit

Research pass: July 1, 2026. Readability revision: July 1, 2026.

## Reading Intent

This note is deliberately more prose-like than the implementation contract
documents. Its job is to explain the research gap in a way a human can hold in
their head before turning the work into code. Short tables remain where they
help an implementation agent preserve decisions.

## Central Gap

The first research pass clarified where the four foundational RAW controls sit
in the image pipeline. It also established an important product rule: automatic
processing must not live as an invisible edit. If STACK suggests or applies a
change, that change should land in the same manual controls the user can edit
afterward.

What was still missing was the solver policy. We had not yet explained, in a
coherent chain, how STACK should decide the actual starting values for RAW
Exposure, Local Range, Finish Tone, and View Transform. Without that chain, a
**Build Starting Point** button could become a collection of unrelated
heuristics. It might brighten the image, add a local graph point, and refit the
display, but without a clear reason for which stage owned which problem.

The next research layer therefore needs to answer a more precise question: when
an image is under-balanced, which control should carry the correction first, and
when should that control stop and pass the problem downstream?

The July 1 math/science pass in `auto-raw-processing-math-and-science.md`
fills part of this gap by separating the solver into a raw safety ledger and a
post-demosaic scene appearance ledger. The remaining gap is now narrower:
STACK still needs named stage readbacks in the render path and real RAW-image
validation to tune the constants.

The later July 1 implementation-readiness pass in
`implementation-pass-readiness.md` closes more of the implementation-facing
gap: it defines the expected stage stats, normalized score helpers, candidate
subscores, validation record, and multi-pass implementation order. What remains
open is no longer basic math or workflow; it is tuning constants and validating
product behavior on real RAW files.

The subsequent math gap check found that the broad math was sound but a few
implementation details were still too scalar. The docs now call out
per-channel/WB-scaled headroom, active-area and masked-black handling,
clipping-pattern fractions, noise propagation through gains and color matrices,
negative/out-of-gamut scene stats, Local Range mask halo risk, and display
transfer labeling. These are not a new hidden pipeline; they are evidence
fields that keep the visible controls honest.

The July 1 code/web readback pass in
`code-web-research-readbacks-and-dng.md` closed another part of the selected
gap. Current STACK code has a local-suggestion image before Local Range and a
current-frame stats boundary after Finish Tone but before View Transform.
`ReadTextureStats` is a GL_FLOAT texture readback with Rec.709-style luma; it
is not a raw safety ledger. The inspected View Transform shader writes clamped
display-mapped RGB to an RGBA16F texture without an explicit sRGB/PQ/HLG
transfer function in that shader path. STACK currently parses and applies a
limited DNG `OpcodeList2` GainMap correction, but does not parse the full set
of DNG tags the math docs discuss, including true `ActiveArea`, `MaskedAreas`,
`LinearResponseLimit`, `NoiseProfile`, `OpcodeList1`, `OpcodeList3`, or
`ProfileGainTableMap`.

## What The Previous Pass Solved

The previous documentation established the mental and pipeline order. RAW
Exposure is the early scene-linear placement control. Local Range is the
scene-EV local correction layer. Finish Tone is global tone relationship and
contrast. View Transform is the final scene-to-display mapping. That is a
strong foundation for UI naming, because it lets the interface say "Fit Display"
without implying that View Transform changed the captured exposure.

It also established the manual-control fidelity rule. Auto Base, suggestions,
and future starting-point automation should write to visible recipe fields:
`preToneExposureEv`, WB recipe values, `localRange`, `finishTone.layerJson`, and
`viewTransform.layerJson`. That part of the product philosophy is mostly in
place.

## What Still Needed Research

The missing research is less about order and more about evidence. A solver
needs to know what image state it is reading before it can responsibly choose a
manual value. Current RAW workspace analysis mostly samples the scene-linear
image immediately before View Transform. That is enough for today's display fit,
but it is not enough for a real starting-point builder. A final or near-final
sample can make upstream mistakes look acceptable because View Transform and
Finish Tone can compress, brighten, or hide the evidence.

The stronger model is staged. The solver needs raw or sensor evidence for clip
and headroom risk; a neutral scene-linear pass for initial exposure and color
placement; a RAW Exposure/WB candidate pass to check whether the scene is now
globally sane; a Local Range candidate pass to see whether regional conflicts
improve; a pre-display tone pass to judge global tone before View Transform; and
a final display pass to make sure the image is actually readable.

The table below keeps the implementation version compact.

| Area | Missing Decision | Current Recommendation |
| --- | --- | --- |
| Stage evidence | Which render states should be sampled? | Use raw/technical, neutral scene, raw-placement, local candidate, pre-display, and final display stages. |
| RAW Exposure | Which statistic defines the key of the image? | Start with p50/log-average blend; use center/subject median only with confidence. |
| White Balance | Should image-derived WB apply automatically? | Keep camera/as-shot when available; show image-derived WB as a suggestion unless metadata is missing and confidence is high. |
| Local Range | Which local edits are safe for one click? | Base mode should be conservative; Balanced may apply one or two visible graph points. |
| Finish Tone | Should the starting point author a tone curve? | Base should stay neutral or very mild; stronger tone belongs in opt-in modes. |
| View Transform | When should display fitting run? | Fit after the selected upstream candidate, then mark stale after later upstream edits. |
| Candidate scoring | What makes one candidate better than another? | Score staged behavior, not just final display pixels. |

## Control Ownership Is The Core Design Constraint

The solver should treat each control as owning a different kind of problem.
RAW Exposure owns the question "is the whole scene placed too low or too high?"
It should not be used to brighten a dark subject when a bright sky would be
damaged. Local Range owns the cases where a region or tone zone needs a
different exposure answer than the whole frame. Finish Tone owns the shape of
the global tonal relationship after exposure and local conflicts are no longer
forcing the curve to do local work. View Transform owns the final display fit,
including white/black display bounds, shoulder, toe, and display readability.

This ownership model gives the solver a stopping rule. For example, if a
positive RAW Exposure move would threaten highlights, the solver should not
pretend the exposure answer is "just brighten anyway." It should leave RAW
Exposure conservative, use Local Range if there is a shadow or subject conflict,
and let View Transform handle display compression.

## Algorithm Gaps By Control

RAW Exposure needed a real target policy. darktable provides a useful anchor:
automatic exposure can shift a selected histogram percentile to a target level.
Reinhard's tone reproduction work provides another anchor: log-average
luminance can act as a scene key. LibRaw/dcraw-style auto brightness adds a
third lesson: highlight clipping tolerance is a policy choice, not a free
mathematical truth. For STACK, this means we need to decide whether the exposure
key is p50, log average, subject/center median, or a weighted blend, and how
much positive movement is allowed before highlight headroom blocks it.

White Balance is not one of the four controls the user is trying to unify, but
it affects all four. A wrong WB changes luminance, color masks, and tone
perception. Adobe distinguishes camera/as-shot WB from image-data Auto WB, while
darktable separates technical white balance from later chromatic adaptation.
Shades of Gray gives us a mathematically grounded family of image-derived WB
estimates. The product question is not whether image-derived WB exists; it is
when STACK should trust it enough to apply rather than merely suggest it.

Local Range already has useful code-level beginnings. STACK can detect sky,
foliage, shadow mass, bright top areas, and center/backlit contrast. The gap is
policy: which of those inferred local problems are safe enough for a one-click
starting point? This matters because a local graph point feels more opinionated
than a display fit. The current recommendation is that Base mode should avoid
local edits unless confidence is very high, while Balanced mode can apply a
small number of conservative visible graph points.

Finish Tone remains the least complete area. Research gives us principles, not
a single obvious answer. Adobe frames Tone Curve as a fine tuning stage after
basic tonal adjustments. Reinhard-style tone mapping separates scene key from
tone reproduction. darktable filmic treats white/black scene bounds and
contrast as display-mapping concerns. For STACK, the safe first answer is to
keep Base mode neutral or nearly neutral and only apply visible, mild curve
points in Balanced mode. Hidden advanced ToneCurve auto fields should not be
used until the compact RAW tab can clearly show what was authored.

View Transform is already the most mature automatic piece. It is safe because
it maps scene values to the display without changing RAW Exposure. The gap is
coupling: a starting-point candidate should always fit View Transform after the
selected upstream recipe is chosen, but later user edits to RAW Exposure, Local
Range, or Finish Tone should make Display Fit stale or refit-on-request. It
should not silently chase every edit.

## Validation Is Still Real Research

Even with better formulas, the constants need image-based validation. A value
like "target median is 2.7 EV below scene white" can be a good starting guess,
but it becomes a product decision only after looking at real RAW files. The
validation set should include normal daylight, high-key snow or beach scenes,
low-key night or interior scenes, backlit subjects, bright sky landscapes, high
ISO shadow lifts, mixed light, intentionally warm or cool scenes, severe clipped
highlights, flat overcast low-contrast files, and camera JPEGs with strong
embedded looks.

For each image, the validation record should capture the chosen candidate
values, the before/after stage stats, whether the result feels like a starting
point rather than a finished edit, and which manual control the user would
naturally tweak next. That last question matters because the point is not to
finish the image for the user; it is to hand them a balanced place to begin.

## What Research Can Still Clarify

The remaining gaps should not be treated as one kind of unknown. Some can be
clarified by external standards or image-processing research, some by reading
STACK's code more carefully, and some require a validation set of real RAW
files.

External standards and literature research can clarify definitions and safe
starting ranges. It can tell us how DNG defines black/white levels, active and
masked areas, linear response limits, noise profiles, opcode stages, color
matrices, and profile gain maps. It can also clarify display transfer families,
such as SDR/sRGB-like code values versus linear display light or future HDR
PQ/HLG behavior. This research can narrow formulas and terminology, but it
cannot choose STACK's final product thresholds.

Code research can clarify what STACK actually does today. It should answer
where readbacks are captured, whether a readback is pre-View-Transform or final
display, whether final display buffers are linear or encoded, which DNG tags are
parsed, which lens/profile corrections are applied before statistics, and which
recipe fields can be written visibly. These are not product choices; they are
facts in the current implementation and should be checked before each code
pass.

Validation research can tune constants. Margins, clip tolerances, Local Range
lift caps, halo thresholds, Finish Tone strength, display readability targets,
and default mode behavior all need real RAW images. External research can give
reasonable initial ranges, but only STACK's renderer, UI, and review images can
decide whether the result feels like a starting point instead of a finished or
over-eager edit.

The practical classification is:

| Remaining Gap | Clarify With | What Research Can Decide |
| --- | --- | --- |
| Exact constants for margins and clip tolerances | External research plus validation | External sources define concepts and plausible ranges; validation sets final values. |
| Local Range lift caps and halo thresholds | Literature plus validation | Literature defines mask/halo risks; validation decides acceptable strength. |
| Display thresholds | Display standards plus code research plus validation | Standards define transfer domains; code reveals current buffer domain; validation tunes readability. |
| STACK readback boundaries | Code research | Exact current capture points and missing stage APIs. |
| Linear versus encoded display readbacks | Code research, with display-standard reference | Current View Transform shader output is display-mapped linear RGB in RGBA16F; inspect UI/output paths before labeling user-facing histograms as encoded code values. |
| DNG lens/profile correction before stats | Code research, with DNG reference | Current code applies limited DNG `OpcodeList2` GainMap before demosaic; full DNG profile/lens correction is not implemented. |
| Tuning on real RAW images | Validation research | Final constants, defaults, and whether Base/Balanced behavior feels right. |

## Implementation Anchors

These anchors are intentionally concise so an implementation agent can scan
them quickly after reading the prose.

| Implementation Anchor | Meaning |
| --- | --- |
| `Build Starting Point` | Multi-control candidate authoring, not a hidden image process. |
| `Fit Display` | Current View Transform-only behavior. |
| Base mode | Conservative, trust-building, mostly RAW Exposure + View Transform. |
| Balanced mode | May apply conservative Local Range and mild visible Finish Tone. |
| Farther mode | Opt-in stronger edit, still visible and reversible. |
| Staged scoring | Compare candidates before final display as well as after final display. |

## Product Policy Still Open

Some choices are not purely mathematical. STACK still needs to decide how much
it should preserve photographer intent versus optimize histogram spread, whether
the default button should ever apply local edits, and how much it should imitate
camera JPEG rendering. My current recommendation is conservative: Base mode
should be the first shippable behavior, Balanced should become the default only
after validation, and Farther should remain opt-in.
