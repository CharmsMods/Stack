# Code And Web Research: Readbacks, Display Domain, And DNG Handling

Research pass: July 1, 2026.
Current code recheck: July 9, 2026, after iterative solver Phase 01.

## Reading Intent

Read this when the implementation question is about current STACK readback
boundaries, whether a statistic is linear or encoded, or which DNG
metadata/correction facts are already represented in the code. This is a
factual bridge between the broad math docs and the current implementation. It
does not replace code inspection. It records the current snapshot so a later
agent can reread the right files quickly.

The practical split is important:

```text
web/standards research = what DNG or display terms mean
code research          = what STACK currently parses, applies, and measures
validation research    = what constants feel right on real RAW images
```

External research can clarify definitions and plausible formulas, but it cannot
decide current STACK behavior or final product constants by itself.

## Current Readback Boundaries In STACK

The RAW Development render path currently has named Raw Technical, Neutral
Scene, Raw Placement, Local Candidate, pre-display, and final-display evidence,
plus the local-suggestion image boundary.

`src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp` captures a
Raw Technical safety ledger from active RAW/linear source samples and metadata
before demosaic or display transforms. It estimates channel clipping,
near-clipping, p999 raw white placement, WB-scaled headroom, hot-pixel fraction,
and limited metadata confidence. This is useful safety evidence, but still
partial: masked areas, true linear response limits, noise profiles, and full
DNG profile/gain metadata are not parsed yet.

`src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp` first captures
a Neutral Scene analysis readback from a 0 EV RAW render using the current WB
policy before legacy Local Exposure, Local Range, Finish Tone, and View
Transform. This is analysis evidence only; the normal current recipe render is
still rendered afterward and remains the displayed output.

`src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp` renders the
RAW GPU result, then captures a complete Raw Placement readback from that
texture before legacy Local Exposure, Local Range, Finish Tone, and View
Transform touch it. This is the current recipe's RAW Exposure/WB placement,
not yet a separate proposed Base-candidate render.

The renderer then captures local-suggestion evidence before Local Range is
applied. In the July 8 code recheck, `ReadLocalSuggestionAnalysisImage(...)`
and `CaptureRawDevelopmentLocalRangeTargetSample(...)` are still called after
the raw GPU stage and before the Local Range overlay/range render. That local
image is therefore useful for "what local conflicts exist before Local Range
touches the image?" It is not a final display sample. The renderer now also
labels that pre-Local-Range texture as Local Candidate fallback evidence when
there is no accepted Local Range output for the current recipe.

When the current RAW recipe actually enables Local Range and the rendered
Local Range output is accepted rather than rejected as blank, the renderer
captures a complete Local Candidate readback from the post-Local-Range texture
before Finish Tone and View Transform. This is a current-render stage
readback. It is not yet the full bounded candidate-batch renderer that would
render proposed Local Range points before applying them.

The same file captures `m_RawDevelopmentViewTransformInputStats` after Local
Range and Finish Tone have been rendered, but before View Transform is rendered.
It also captures `m_RawDevelopmentFinalDisplayStats` after View Transform and
records it as the `DisplayCandidate` stage. `src/Editor/EditorRenderWorker.cpp`
then publishes both stats objects; current-frame analysis is still built from
the pre-View-Transform stats. `src/Raw/RawImageAnalysis.cpp` says that technical
stage is unavailable for sensor truth and that the current-frame stats are safe
for View Transform fitting only.

The stage-diagnostics list now includes measured `RawTechnical`,
`NeutralScene`, and `RawPlacement` entries. `RawTechnical` is a partial source
safety ledger, `NeutralScene` is a separate 0 EV analysis render with the
current WB policy, and `RawPlacement` is the current recipe's RAW GPU output
before legacy Local Exposure, Local Range, Finish Tone, and View Transform.

That means the current RAW Workspace analysis is best described as:

```text
raw technical:              active RAW/linear source samples plus metadata safety ledger
neutral scene:              0 EV RAW analysis render/current WB, pre-local/tone/display
raw placement:              post-RAW GPU/current RAW Exposure+WB, pre-local/tone/display
local suggestion image:     post-RAW GPU, pre-Local-Range
local candidate fallback:   pre-Local-Range texture if no accepted Local Range output exists
local candidate complete:   post-Local-Range texture when Local Range output is accepted
current frame stats:        post-Local-Range, post-Finish-Tone, pre-View-Transform
finish tone candidate:      named pre-View-Transform texture readback
display candidate:          named post-View-Transform display-mapped readback
```

This confirms the earlier docs: current Auto Base / Fit Display now has useful
Raw Technical, Neutral Scene, Raw Placement, Local Candidate,
pre-View-Transform, and final display readbacks, but a full Build Starting
Point solver still needs per-candidate renders and stronger RAW metadata
coverage before it can claim complete staged solving.

## What ReadTextureStats Actually Measures

`src/Renderer/Internal/RenderPipelineReadback.cpp` implements
`RenderPipeline::ReadTextureStats(...)` as a generic texture readback. It
downsamples to a maximum probe edge of 512 pixels, blits with linear filtering
when needed, and reads `GL_RGBA` as `GL_FLOAT`. It then computes luma using the
Rec.709/sRGB coefficient family:

```text
Y = max(0, 0.2126 * R + 0.7152 * G + 0.0722 * B)
```

The current implementation records luma percentiles, log-average luma, dynamic
range in EV, min/max RGB, a count of pixels where any channel is above `1.0`,
and a `displayClipPercent` count where the maximum channel is at least `0.999`
or the minimum channel is at most `0.001`.

There are three important limits:

```text
all pixels in the probe are counted as valid
alpha is not used as a validity mask
the function only knows the texture domain it was handed
```

So `ReadTextureStats` is a useful stage-texture statistic, but it is not raw
safety evidence. It does not know active areas, masked optical black regions,
CFA channels, WB-scaled headroom, or sensor clipping patterns.

## Current View Transform Output Domain

`src/Editor/Layers/ToneLayerRendering.cpp` implements the current View
Transform shader. It reads a texture, clamps negative input channels to zero for
the transform, maps luma through a filmic curve, optionally preserves hue,
compresses display gamut, applies saturation, and writes:

```text
FragColor = vec4(clamp(rgb, 0.0, 1.0), alpha)
```

The shader output texture is allocated as `GL_RGBA16F` in the render pipeline.
The shader does not apply an explicit sRGB, Rec.709, PQ, or HLG transfer
function in the inspected path. Therefore, a readback of this texture with
`ReadTextureStats` should be labeled as linear normalized display-mapped RGB,
not as encoded sRGB code values, unless a later UI/output path applies an
encoding before the readback.

This distinction matters because display standards define a transfer function
separately from RGB primaries and white point. The ICC sRGB registry defines
sRGB as IEC 61966-2-1:1999 and gives a piecewise color-component transfer from
linear RGB to encoded values:

https://registry.color.org/rgb-registry/srgb

OpenColorIO uses the same conceptual separation for modern color pipelines: a
View Transform can map scene-referred reference values to display-referred
reference values, and a display color space can then map those values to a
specific display:

https://opencolorio.readthedocs.io/en/latest/guides/authoring/displays_views.html

For STACK docs and diagnostics, the safe wording is:

```text
pre-View-Transform stats: scene-linear / pre-display stats
post-View-Transform texture stats, if added: display-mapped linear RGB unless an explicit transfer encode is added
UI/output code values: unknown until the exact preview/output readback path is inspected
```

## DNG Metadata And Corrections: What The Spec Clarifies

Adobe's DNG page identifies DNG as a public raw format and links the current
DNG 1.7.1.0 specification:

https://helpx.adobe.com/camera-raw/digital-negative.html

The DNG 1.7.1.0 specification is the source of truth for tag meaning:

https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf

The most relevant confirmed definitions are:

| DNG concept | Research clarification | Implementation meaning |
| --- | --- | --- |
| `ActiveArea` | The active non-masked sensor rectangle, ordered top, left, bottom, right. | Raw percentiles should avoid assuming the full stored raster is scene evidence. |
| `MaskedAreas` | Fully masked rectangles that can be used to measure black encoding level. | Masked pixels can support black diagnostics but should not become normal scene pixels. |
| `LinearResponseLimit` | Fraction of the encoding range above which response may become significantly nonlinear. | Positive RAW Exposure should use this when present, not only formal white level. |
| `OpcodeList1` | Opcodes applied to raw image as read from file. | Before-linearization correction stage. |
| `OpcodeList2` | Opcodes applied just after mapping to linear reference values. | STACK's current parsed DNG GainMap lives here. |
| `OpcodeList3` | Opcodes applied just after demosaicing. | Post-demosaic correction stage, not currently parsed by STACK. |
| `NoiseProfile` | Two-term raw noise model: signal-dependent photon noise plus signal-independent readout noise. | Raw safety can use it for shadow-lift risk when parsed. |
| `ProfileGainTableMap` / `ProfileGainTableMap2` | Spatial gain tables applied in linear color space, with DNG-defined ordering and precedence. | This is not the same as STACK's current OpcodeList2 GainMap support. |

The spec also matters for pipeline placement. `OpcodeList2` is defined after
linear mapping; `OpcodeList3` is after demosaic. `ProfileGainTableMap` is
applied after BaselineExposure and after opcodes, and DNG 1.7 defines
`ProfileGainTableMap2` precedence. Those details prevent a future solver from
collapsing every "gain map" concept into one generic lens correction flag.

## DNG Metadata And Corrections: What STACK Currently Does

The July 1/8 Pass 94 render path remains narrower than the DNG spec allows, but
Phase 01 added a separate non-mutating raw-evidence path on July 9.

`src/Raw/LibRawDecoder.cpp` parses these DNG tags in the supplement path:

```text
BlackLevelRepeatDim
BlackLevel
WhiteLevel
ColorMatrix1 / ColorMatrix2
CameraCalibration1 / CameraCalibration2
AnalogBalance
AsShotNeutral
BaselineExposure
CalibrationIlluminant1 / CalibrationIlluminant2
ForwardMatrix1 / ForwardMatrix2
OpcodeList2
CFA repeat / CFA pattern
```

Phase 01 extends that parser across classic-TIFF IFD/SubIFD trees with:

```text
LinearizationTable
full BlackLevel values and BlackLevelDeltaH/V
WhiteLevel values
ActiveArea and MaskedAreas
true LinearResponseLimit
BaselineNoise and NoiseProfile
OpcodeList1/2/3 counts and support state
ProfileGainTableMap / ProfileGainTableMap2 presence
```

`ParseDngOpcodeList2(...)` only handles opcode id `9`, stored as
`DngGainMapOpcode`, and increments an unsupported-opcode count for other
opcodes or unsupported map shapes. `RawGpuPipeline::UploadCorrectedRawTexture`
then normalizes raw mosaic samples with black/white levels and multiplies the
normalized values by the parsed DNG gain maps for visible coordinates. The RAW
render path binds that corrected raw texture when available before demosaic,
white balance, camera transform, RAW Exposure, Local Range, Finish Tone, and
View Transform.

So the current code can truthfully say:

```text
STACK applies a limited DNG OpcodeList2 GainMap correction before demosaic when parsed successfully.
STACK uses parsed DNG black/white/WB/matrix/BaselineExposure metadata in the raw path.
```

The current code should not claim full DNG lens/profile correction. Phase 01
reports OpcodeList1/3 and profile gain maps but does not apply them. The only
implemented DNG opcode correction remains the bounded OpcodeList2 GainMap path.

`src/Raw/RawTechnicalEvidence.cpp` now owns the versioned
`raw-technical-evidence-v1` diagnostic record. It uses content SHA-256 and a
decode identity, normalizes in sensor coordinates, retains per-plane evidence,
classifies CFA-aligned clipping, computes technical-WB headroom and DNG
NoiseProfile SNR buckets, and serializes units/provenance/uncertainty. The
Phase 01 checkpoint and 186-source report are in
`iterative-raw-solver-phases/phase-01/`.

This record is deliberately not consumed by Pass 94. The legacy renderer Raw
Technical payload and `src/Raw/RawImageAnalysis.cpp` remain unchanged for
baseline comparison; that older path still infers `hasActiveArea` from decoded
dimensions and a response-limit-like value from `defaultWhiteClipPercent`.

`src/Raw/RawImageAnalysis.cpp` currently marks `hasActiveArea` from available
visible/raw dimensions, not from the DNG `ActiveArea` tag. It also derives a
`linearResponseLimit`-like value from `metadata.defaultWhiteClipPercent`, not
from the actual DNG `LinearResponseLimit` tag.

## What This Closes And What Remains

This pass closes several research questions enough for implementation planning:

| Question | Status After This Pass |
| --- | --- |
| Current STACK readback boundaries | Clarified by code. Raw Technical is a partial active-sample safety ledger; Neutral Scene is a 0 EV analysis render; Raw Placement is captured after RAW GPU render before local/tone/display; Local suggestions are pre-Local-Range; Local Candidate is fallback pre-local or complete post-local for the current render; current stats are pre-View-Transform after Finish Tone; final display stats are post-View-Transform display-mapped texture stats. |
| Current stats domain | Clarified by code. `ReadTextureStats` is GL_FLOAT texture readback with Rec.709-style luma and no raw validity model. |
| Current View Transform output domain | Clarified enough for docs. The shader writes clamped display-mapped RGB into RGBA16F without explicit sRGB/PQ/HLG transfer in the inspected path. |
| Current DNG gain/lens handling | Clarified by code. Limited OpcodeList2 GainMap is parsed/applied; Phase 01 reports other opcode/profile coverage without applying it; full DNG profile/lens stack is not implemented. |
| Phase 01 raw evidence | Implemented and frozen as `raw-technical-evidence-v1`; 178 ARWs and eight DNGs produced complete records without recipe mutation. |
| DNG tag meanings | Clarified by Adobe DNG spec. |

The remaining gaps are implementation and validation gaps:

```text
keep the legacy Pass 94 Raw Technical payload frozen while later phases consume the separate versioned Phase 01 record
acquire real DNG fixtures containing explicit ActiveArea/MaskedAreas; current local DNGs contain neither tag
retain BigTIFF supplement parsing and unsupported opcode/profile application as explicit gaps
add true per-candidate Local Candidate renders if Balanced scoring depends on proposed Local Range edits before apply
decide how Neutral Scene stats should influence visible WB proposals and future per-candidate RAW Exposure/WB renders
inspect the exact UI/output preview path before labeling user-facing histograms as encoded code values
tune constants on real RAW files
```

For the upcoming implementation passes, the key rule is simple: do not let the
new solver treat current texture stats as complete image understanding. The
current Raw Technical, Neutral Scene, Raw Placement, pre-View-Transform, and
final display readbacks are useful stages, but Raw Technical is still partial
and the render readbacks are not per-candidate evidence for proposed edits
before apply.
