# Code And Web Research: Readbacks, Display Domain, And DNG Handling

Research pass: July 1, 2026.

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

The RAW Development render path currently has one strong scene stats boundary
and one local-suggestion image boundary.

`src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp` renders the
RAW GPU result, then captures local-suggestion evidence before Local Range is
applied. In the July 1 code snapshot, `ReadLocalSuggestionAnalysisImage(...)`
and `CaptureRawDevelopmentLocalRangeTargetSample(...)` are called after the raw
GPU stage and before the Local Range overlay/range render. That local image is
therefore useful for "what local conflicts exist before Local Range touches the
image?" It is not a final display sample.

The same file captures `m_RawDevelopmentViewTransformInputStats` after Local
Range and Finish Tone have been rendered, but before View Transform is rendered.
`src/Editor/EditorRenderWorker.cpp` then publishes those stats as
`rawWorkspace.viewTransformInputStats` and builds current-frame analysis from
them. `src/Raw/RawImageAnalysis.cpp` says this technical stage is unavailable
for sensor truth and that the current-frame stats are safe for View Transform
fitting only.

That means the current RAW Workspace analysis is best described as:

```text
local suggestion image: post-RAW GPU, pre-Local-Range
current frame stats:   post-Local-Range, post-Finish-Tone, pre-View-Transform
final display stats:   not currently a named RAW Workspace readback
raw safety stats:      not currently a named RAW Workspace readback
```

This confirms the earlier docs: current Auto Base / Fit Display has useful
pre-View-Transform evidence, but a full Build Starting Point solver still needs
additional named stages.

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

The July 1 code snapshot shows a narrower implementation than the DNG spec
allows.

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

The current code should not claim full DNG lens/profile correction. A focused
`rg` pass found no current parsing for:

```text
ActiveArea tag 50829
MaskedAreas tag 50830
true LinearResponseLimit tag 50734
NoiseProfile tag 51041
OpcodeList1 tag 51008
OpcodeList3 tag 51022
ProfileGainTableMap
ProfileGainTableMap2
```

`src/Raw/RawImageAnalysis.cpp` currently marks `hasActiveArea` from available
visible/raw dimensions, not from the DNG `ActiveArea` tag. It also derives a
`linearResponseLimit`-like value from `metadata.defaultWhiteClipPercent`, not
from the actual DNG `LinearResponseLimit` tag.

## What This Closes And What Remains

This pass closes several research questions enough for implementation planning:

| Question | Status After This Pass |
| --- | --- |
| Current STACK readback boundaries | Clarified by code. Local suggestions are pre-Local-Range; current stats are pre-View-Transform after Finish Tone. |
| Current stats domain | Clarified by code. `ReadTextureStats` is GL_FLOAT texture readback with Rec.709-style luma and no raw validity model. |
| Current View Transform output domain | Clarified enough for docs. The shader writes clamped display-mapped RGB into RGBA16F without explicit sRGB/PQ/HLG transfer in the inspected path. |
| Current DNG gain/lens handling | Clarified by code. Limited OpcodeList2 GainMap is parsed/applied; full DNG profile/lens stack is not. |
| DNG tag meanings | Clarified by Adobe DNG spec. |

The remaining gaps are implementation and validation gaps:

```text
add named raw safety stats instead of relying on current-frame stats
add named final display stats if Display Fit scoring needs them
decide whether to parse ActiveArea, MaskedAreas, LinearResponseLimit, NoiseProfile, OpcodeList1/3, and ProfileGainTableMap
inspect the exact UI/output preview path before labeling user-facing histograms as encoded code values
tune constants on real RAW files
```

For the upcoming implementation passes, the key rule is simple: do not let the
new solver treat current pre-View-Transform stats as a complete image
understanding. They are one useful stage, not the raw ledger, not the neutral
scene baseline, and not the final display readback.
