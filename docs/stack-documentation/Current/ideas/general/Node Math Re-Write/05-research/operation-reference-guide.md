# How To Read The Image-Math Reference

- Research date: 2026-07-12
- Stack status date in the original tables: 2026-07-12
- Purpose: formula, algorithm, terminology, and source reference
- Authority: research—not an implementation contract or automatic backlog

## Main Conclusion

A large share of image editing can be described as smaller mathematical
operations, but those operations do not all need the same information or cost
the same amount to execute. Some use one pixel. Others need neighboring pixels,
coordinates, another image, full-image statistics, repeated passes, camera
metadata, display information, or a learned model.

Stack can therefore support both exact lower-level operations and friendly
higher-level tools. A friendly tool may expose an editable graph only when
that graph truthfully defines its result. The visible graph does not have to
become one full-image GPU pass per visible node.

## What Each Operation Entry Tries To Separate

1. The formula or staged algorithm.
2. The information and neighboring data it needs.
3. The representation that gives its numbers meaning.
4. Its user-facing purpose.
5. Stack's status as observed in the dated audit.

Names such as Brightness, Contrast, Saturation, Vibrance, Clarity, and Dehaze
describe families of behavior, not one universal formula. A production node
must choose and test one exact definition.

## Scope Codes

| Code | Information required |
| --- | --- |
| `P` | Current pixel or scalar only |
| `V` | Several channels/components of the current pixel |
| `N` | Neighboring samples or a spatial kernel |
| `C` | Coordinates, output extent, or resampling policy |
| `G` | Whole-image or regional statistics/reduction |
| `M` | Another image, mask, frame, or reference |
| `X` | Metadata, profiles, calibration, camera, lens, or display information |
| `I` | Iteration, recurrence, state, pyramid, or multi-resolution processing |
| `L` | Learned parameters or semantic interpretation |

An operation may use several codes. The codes describe information needs, not
what must appear as top-level user-facing node categories.

## Dated Stack-Status Labels

The labels below describe the audited 2026-07-12 tree. They are not current
status and must be rechecked before implementation.

| Label | Meaning at the audit date |
| --- | --- |
| **Existing** | A live node or backend path substantially performed the operation |
| **Partial** | Related machinery existed but was fixed inside another path or semantically incomplete |
| **Needs fix** | A visible path had a formula, naming, serialization, alpha, or color-state problem |
| **Missing** | No reusable live implementation was found |
| **Specialized** | The work existed only inside a higher-level RAW, frequency, denoise, export, or similar path |
| **Out of scope** | Not recommended as part of the conventional primitive system |

Phases 1–6 changed several of these statuses, including values, fusion,
regions/tiling, Field Mean, Reformat, and specialized planning.

## Terms That Must Stay Distinct

| Term | Meaning in this research |
| --- | --- |
| **Color space** | A defined color representation; for RGB this normally includes primaries, white point, and transfer/encoding information |
| **Primaries** | Chromaticities defining what the RGB axes mean |
| **Transfer function** | Mapping between stored code values and linear-light values |
| **Linear light** | Values proportional to represented light; this alone does not identify RGB primaries |
| **Scene-referred** | Values interpreted relative to a captured or synthesized scene |
| **Display-referred** | Values interpreted for an output/display condition |
| **View/presentation transform** | A viewing path toward a display representation; product policy remains explicit |
| **Output transform** | A transform used to make a particular encoded deliverable or display signal |
| **Look transform** | A creative transform, separate from technical presentation/output behavior |
| **Luma** | A weighted signal in a named encoded RGB/video system |
| **Luminance** | A colorimetric or physical light quantity, often `Y` in XYZ |
| **Lightness** | A perceptual coordinate such as `L*` or Oklab `L` |
| **Chroma** | Distance from a neutral axis in an appropriate color space |
| **Saturation** | A context-dependent measure with no single universal formula |
| **Straight alpha** | RGB is stored independently of alpha |
| **Premultiplied alpha** | Stored RGB has already been multiplied by alpha |

## Sources And Current Use

The library uses standards and primary material from organizations and projects
including W3C, Khronos, ICC, ACES, OpenColorIO, OpenEXR, MaterialX, OpenFX,
OpenImageIO, Adobe DNG, and original image-processing papers. Follow the exact
entries in the [source ledger](sources/standards-and-research-source-ledger.md)
and verify the current primary source whenever an implementation choice
depends on it.
