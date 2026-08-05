# 10 — Metadata, Calibration, Camera, Lens, and Scene Operations

> **Reference status:** Formulas and sources are technical reference. Any Stack
> status in this file is a 2026-07-12 snapshot, not current state or a release
> backlog.

## Scope

These operations need information that ordinary RGB pixels cannot safely supply. A matrix applied to RGB may be simple arithmetic, but choosing the correct matrix can require a camera model, illuminant, profile, or measured chart. A radial warp may be simple math, but correcting a real lens needs calibrated coefficients, focal length, focus distance, aperture, and sometimes a gain map.

This section is the clearest proof that “everything is math” does not mean “everything can be inferred from the current pixel.” Metadata and calibration must remain attached to the data until every dependent operation has consumed it.

The Adobe DNG processing order is especially useful: linearize stored code values, subtract the applicable black model, normalize against applicable white levels, and only then proceed through demosaic and color processing. Black level may vary by CFA plane, repeating pattern, row, or column; it is not necessarily one scalar.

## Canonical operation table

| ID | Operation | Canonical formula or staged algorithm | Scope | Required external data | What it does/common uses | Cautions | Stack status, 2026-07-12 |
|---|---|---|---|---|---|---|---|
| `CAL.RAW_LINEARIZE` | RAW linearization table | $L=\operatorname{LUT}(code)$ before black subtraction | `X+P` | DNG LinearizationTable or camera response mapping | Converts stored sensor codes to a linearized sensor domain | Order is semantic; interpolation/bit depth and missing table identity required | **Specialized/partial:** LibRaw path; not a public primitive |
| `CAL.BLACK` | RAW black-level subtraction | $S(p,c)=L(p,c)-B(p,c)$ | `X+P/C` | Per-plane/repeating black, row/column deltas, masked pixels | Removes sensor/electronics offset | Not one scalar; preserve negative values early for truthful noise behavior | **Specialized:** RAW normalization uses captured levels |
| `CAL.WHITE_LEVEL` | RAW white/saturation normalization | $N=(L-B)/\max(W_c-B,\epsilon)$ | `X+P` | Per-plane WhiteLevel/saturation | Normalizes sensor range | White level is not display white; clipping can destroy reconstruction data | **Specialized:** RAW path |
| `CAL.CFA` | CFA interpretation | Map sensor location to color plane using declared pattern/layout | `X+C` | CFARepeatPatternDim, CFAPattern, plane colors/layout | Makes mosaic samples semantically meaningful | Bayer, X-Trans, quad, and linear DNGs differ | **Specialized:** Raw socket retains CFA metadata until decode |
| `CAL.ACTIVE_AREA` | Active/masked area | Apply declared active rectangle and masked calibration regions | `X+C` | ActiveArea, MaskedAreas, crop tags | Excludes optical black/borders and establishes valid sensor image | Do not confuse sensor active area with creative crop | **Specialized/partial** |
| `CAL.ORIENTATION` | Orientation metadata | Apply EXIF/DNG orientation as an exact discrete transform | `X+C` | Orientation tag | Presents camera image upright | Should be metadata-normalized once or preserved explicitly, not repeatedly guessed | **Partial/unknown** in generic descriptor |
| `CAL.PIXEL_ASPECT` | Pixel aspect / default scale | Interpret pixels with declared aspect/scale | `X+C` | PixelAspectRatio, DefaultScale | Correct geometry and display size | Pixel dimensions alone are insufficient | **Missing** generic edge metadata |
| `CAL.AS_SHOT_WB` | As-shot white balance gains | From neutral coordinates, relative gains commonly proportional to $1/n_c$ | `X+V+P` | AsShotNeutral/AsShotWhiteXY, analog balance | Reproduces selected camera white balance | As-shot is a capture choice/estimate, not guaranteed “accurate” illumination | **Specialized:** RAW recipes/metadata |
| `CAL.NEUTRAL_WB` | User-neutral white balance | Estimate gains so a selected neutral becomes achromatic | `X+M/V+P` | Sampled neutral patch and camera/working model | Manual accurate neutralization | Clipped/noisy patch and spatially mixed light limit accuracy | **Specialized UI**, not a general graph sample/value contract |
| `CAL.ANALOG_BAL` | Analog balance | Apply camera-provided per-channel scaling in the prescribed profile pipeline | `X+V+P` | AnalogBalance | Accounts for camera channel scaling/calibration | Must follow the DNG profile equations/order, not be treated as creative RGB gain | **Specialized via LibRaw/DNG data** |
| `CAL.CAMERA_COLOR` | Camera-to-colorimetric transform | Use the applicable DNG/ICC/DCP matrix/profile sequence for chosen illuminant | `X+V+P` | ColorMatrix, CameraCalibration, ForwardMatrix, illuminants/profile | Maps camera-native responses toward XYZ/working RGB | Matrix direction and profile pipeline are easy to invert incorrectly; profiles may be nonlinear/LUT based | **Specialized:** RAW matrix path toward linear-sRGB-like primaries |
| `CAL.ILLUM_INTERP` | Multi-illuminant profile interpolation | Interpolate calibrated transforms according to selected scene white/illuminant using the profile specification | `X+V` | Dual/triple illuminant matrices and calibration temperatures | Makes camera transform adapt to illumination | Matrix interpolation rules are profile-specific; CCT is not enough under arbitrary spectra | **Specialized/partial** |
| `CAL.BASELINE_EV` | Baseline exposure | $RGB'=RGB\,2^{EV_{base}}$ at the defined RAW stage | `X+P` | BaselineExposure/profile policy | Normalizes camera default brightness | Metadata value is a pipeline convention, not user exposure | **Specialized/unknown exact coverage** |
| `CAL.NOISE_PROFILE` | Sensor noise profile | Common variance model $\sigma^2(x)=a x+b$ per channel/ISO | `X` | NoiseProfile, ISO/read/shot calibration | Drives denoise, uncertainty, merge weights | Signal scale and RAW stage must match calibration | **Specialized/partial:** denoise has private models, no shared descriptor |
| `CAL.BAD_PIXEL` | Dead/hot pixel correction | Detect/use map; replace from same-color CFA neighbors or robust neighborhood | `X+N` | Defect map and/or detection thresholds | Removes sensor defects | Must operate before/with demosaic and preserve real small highlights | **Specialized/unknown** |
| `CAL.BAD_COLUMN` | Bad row/column correction | Detect/map defective line and interpolate from valid neighbors | `X+N` | Sensor defect calibration | Removes line defects | Pattern and CFA plane alignment matter | **Missing/general unknown** |
| `CAL.DARK_FRAME` | Dark-frame subtraction | $O=I-D$ after matching black/exposure/temperature scale | `X+M+P` | Matched dark frame and capture metadata | Removes fixed-pattern/amp glow | Mismatched temperature/exposure adds artifacts; may require scaling | **Missing** as graph operation |
| `CAL.FLAT_FIELD` | Flat-field correction | $O=(I-D)/(F-D)$ with guarded normalization | `X+M+P` | Flat and optional dark calibration frames | Corrects pixel response and illumination nonuniformity | Zero/near-zero policy and channel/RAW stage matter | **Missing** |
| `CAL.GAIN_MAP` | Gain-map correction | $O(p,c)=I(p,c)G(p,c)$ with specified interpolation/domain | `X+N/P` | DNG gain map/opcode or lens shading map | Corrects spatial sensitivity/vignetting | Gain map coordinate system, stage, plane and interpolation required | **Specialized/partial** |
| `CAL.DNG_OPCODE` | DNG opcode processing | Execute ordered OpcodeList1/2/3 operations at their specified stages | `X+N/C/I` | Opcode lists and version support | Standardized camera/lens corrections | Order and unsupported-opcode behavior must be visible | **LibRaw-dependent/specialized** |
| `CAL.LENS_DISTORT` | Calibrated geometric lens correction | Apply inverse calibrated radial/tangential model for capture parameters | `X+C+N` | Lens profile, focal length, focus/aperture where needed | Straightens barrel/pincushion distortion | Different from creative amount slider; changes valid extent | **Partial:** creative radial Lens Distortion only |
| `CAL.LENS_VIGNETTE` | Calibrated lens shading correction | Multiply by reciprocal measured radial/spatial attenuation/gain map | `X+C+P/N` | Lens/profile/capture parameters | Corrects optical falloff | Can amplify noise/corners; not the same as creative vignette | **Partial:** creative Vignette exists, not calibrated correction |
| `CAL.CHROM_AB` | Chromatic-aberration correction | Apply channel-dependent radial/lateral coordinate corrections or estimate fringes | `X+C+N`, sometimes `G` | Lens profile or image estimate | Aligns color channels at edges | Lateral and longitudinal CA differ; creative offset effect is not calibration | **Partial:** Chromatic Aberration is a creative effect |
| `CAL.COLOR_CHART` | Color-chart calibration | Fit constrained matrix/profile minimizing error between measured patches and references | `X+M+G/V` | Captured chart, reference values, illuminant/viewing data | Builds camera/input correction | Objective, color space, regularization and illuminant matter | **Missing** |
| `CAL.ICC_INPUT` | ICC input transform | Apply profile device→PCS transform with intent/state, then PCS→working | `X+V/P` | Embedded/assigned ICC profile | Correctly imports tagged RGB/device images | Profile may be matrix/TRC or LUT; do not reduce blindly to “gamma” | **Missing:** current ordinary import discards ICC data |
| `CAL.ICC_OUTPUT` | ICC output/proof transform | Working→PCS→output profile using selected rendering intent, optional proof simulation | `X+V/P` | Destination/proof ICC profile, intent, black-point policy | Display/print proof and export | Output-referred operation; soft proof should normally remain a view, not bake accidentally | **Missing** |
| `CAL.PHYSICAL_COLOR` | Physical/colorimetric conversion | Transform characterized source through XYZ/PCS to characterized destination | `X+V/P` | Source/destination characterization and viewing assumptions | Preserves color appearance/measurement | Numbers without characterization are insufficient | **Missing** general transform facility |
| `CAL.ABS_LUMINANCE` | Absolute luminance conversion | Relate code/linear values to $cd/m^2$ using calibrated scale/EOTF | `X+P` | Display/capture calibration, reference white/peak | HDR mastering, measurement, photometric workflows | Relative scene-linear 1.0 is not automatically 1 nit or display white | **Missing** |
| `CAL.SCENE_LINEAR` | Scene-linear reconstruction | RAW normalization + demosaic + WB + calibrated camera transform + exposure convention | `X+N+V` | RAW sensor data/metadata/profile | Produces scene-referred working RGB | Not recoverable exactly from an arbitrary rendered JPEG | **Specialized:** implemented RAW path, semantics lost on generic Image wire |
| `CAL.HIGHLIGHT_REC` | RAW highlight reconstruction | Use unclipped channels/spatial/color ratios to infer clipped channel(s) | `X+N/V`, sometimes `I` | RAW planes and clipping levels | Repairs single-channel clipping | Cannot restore detail when all relevant channels are clipped; algorithm is heuristic | **Specialized/partial** |
| `CAL.RESPONSE_REC` | Camera response recovery | Solve response curve from aligned exposures and known exposure ratios | `X+M+G/I` | Exposure stack/times and static scene | HDR radiance reconstruction | Cross-references multi-image HDR; response can be nonunique without smoothness constraints | **Missing** |
| `CAL.DEPTH_BLUR` | Calibrated depth-aware blur | Convert depth/camera/aperture to circle of confusion, then depth-aware filter | `X+M+N/I` | Depth, focal parameters, aperture, focus distance | Physically guided synthetic DOF | Requires occlusion model and depth units | **Missing** |
| `CAL.DISPLAY_VIEW` | Display/view transform | Scene working state → rendering/tone/gamut stage → target display encoding | `X+P/V`, often `G` | Working state, display primaries, white, EOTF/ICC/OCIO view | Truthful viewport | Creative look, rendering transform, and display encoding are separate | **Needs fix:** current View Transform omits final OETF; viewport unmanaged |
| `CAL.PRINT_PROOF` | Print simulation | Apply source→proof profile, paper/ink black/white and rendering intent, then display visualization | `X+P/V` | Printer/ink/paper profile and monitor profile | Soft proof and print prediction | Not one LUT unless the transform is fully characterized/versioned | **Missing** |

## Recommended metadata lifetime

Metadata should be divided into three groups:

1. **Required semantics** that travel on the edge: color encoding, scene/display state, alpha, range, extent, channel meaning.
2. **Source metadata** retained for provenance: camera/lens/exposure/ISO/profile identities.
3. **Consumable calibration payloads** retained while needed: CFA, black/white levels, matrices, noise profile, opcodes, gain maps.

A RAW Decode node may intentionally convert `Raw` to `ColorImage`, but its output descriptor must record the resulting primaries, white/reference convention, transfer, range, and provenance. “Image” alone is not sufficient.

## Stack-specific consequence

Stack’s separate `Raw` socket is a good boundary. The failure occurs when RAW becomes the same untyped `Image` used by encoded PNG/JPEG. The future contract should preserve source/calibration provenance while giving the new RGB image an explicit semantic descriptor. It should not force ordinary math nodes to understand every camera tag.

## Primary sources

- [Adobe Digital Negative Specification 1.7.1.0](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf)
- [Adobe Digital Negative landing page and SDK](https://helpx.adobe.com/camera-raw/digital-negative.html)
- [LibRaw data structures](https://www.libraw.org/docs/API-datastruct.html)
- [ICC.1:2022 profile specification](https://www.color.org/specification/ICC.1-2022-05.pdf)
- [OpenColorIO authoring/config concepts](https://opencolorio.readthedocs.io/en/latest/guides/authoring/authoring.html)
- [ACES system overview](https://docs.acescentral.com/background/overview/)
- [ACES Output Transforms](https://docs.acescentral.com/system-components/output-transforms/)
