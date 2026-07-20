# Stack Image Math Research Package

Status: architecture and reference research, not an implementation specification  
Stack ground-truth date: 2026-07-12  
External-research date: 2026-07-12

## Main conclusion

The original intuition is correct, with one major qualification: a very large share of image editing can be described as small mathematical operations, but those operations do not all have the same input requirements or the same execution cost.

Some operations are genuinely pointwise: one output pixel can be calculated from one input pixel and a few constants. Others need neighboring pixels, coordinates, another image, whole-image statistics, repeated passes, camera metadata, a display profile, or a learned model. They are all mathematical, but they are not interchangeable pieces of execution machinery.

Stack should therefore use two layers:

1. **Explicit primitives** with precise formulas, declared input/output meanings, and predictable behavior.
2. **Convenience or compound nodes** that expose familiar editing tools while retaining a documented decomposition and, where practical, an editable internal graph.

The current Stack audit adds a practical constraint: ordinary graph nodes presently render as separate full-canvas `GL_RGBA16F` passes, and the graph has no general fusion compiler. Publishing hundreds of tiny operations as ordinary nodes before adding semantic types and pointwise fusion would make the graph more transparent but also much more expensive and still semantically ambiguous.

The correct order is therefore:

1. Define the image/value contract.
2. Separate working-buffer meaning from view/output behavior.
3. Define alpha and compositing behavior.
4. Add semantic wire descriptors and validation.
5. Add first-class scalar/vector/matrix/curve values.
6. Add a pointwise intermediate representation that can fuse compatible primitives.
7. Add executable compound definitions with stable interfaces and versioning.
8. Expand the public primitive library.

## Documents

### Operation references

- [01 — Fundamental per-pixel math](01_Fundamental_Per_Pixel_Math.md)
- [02 — Channel and vector operations](02_Channel_and_Vector_Operations.md)
- [03 — Tone, exposure, and curves](03_Tone_Exposure_and_Curves.md)
- [04 — Color-grading operations](04_Color_Grading_Operations.md)
- [05 — Alpha, masks, and compositing](05_Alpha_Masks_and_Compositing.md)
- [06 — Neighborhood filtering and morphology](06_Neighborhood_Filtering_and_Morphology.md)
- [07 — Geometry, transforms, and resampling](07_Geometry_Transforms_and_Resampling.md)
- [08 — Global and statistical operations](08_Global_and_Statistical_Operations.md)
- [09 — Multi-image and reference operations](09_Multi_Image_and_Reference_Operations.md)
- [10 — Metadata, calibration, and scene operations](10_Metadata_Calibration_and_Scene_Operations.md)
- [11 — Model- and intent-based operations](11_Model_and_Intent_Operations_Scope_Boundary.md)

### Architecture and Stack-specific documents

- [12 — Proposed image-buffer contract](12_Proposed_Image_Buffer_Contract.md)
- [13 — Primitive-node taxonomy](13_Primitive_Node_Taxonomy.md)
- [14 — Composition and validation rules](14_Composition_and_Validation_Rules.md)
- [15 — High-level node decompositions](15_High_Level_Node_Decompositions.md)
- [16 — Current Stack implementation mapping](16_Current_Stack_Implementation_Mapping.md)
- [17 — Composable node architecture research](17_Composable_Node_Architecture_Research.md)
- [18 — Research sources](18_Research_Sources.md)

## How to read the operation tables

Every operation table separates five questions that normal editing software often collapses into one label:

1. **What is the formula or staged algorithm?**
2. **What information does it need?**
3. **What representation gives the numbers meaning?**
4. **What is the user-facing purpose?**
5. **What does Stack currently support?**

The formulas are reference definitions, not claims that every friendly control has one universal formula. Names such as Brightness, Contrast, Saturation, Lift, Temperature, Vibrance, Clarity, and Dehaze refer to families of behavior. A production node must choose and document a particular definition.

### Scope codes

| Code | Information required |
|---|---|
| `P` | Current pixel or scalar only |
| `V` | Multiple channels/components of the current pixel |
| `N` | Neighboring samples or a spatial kernel |
| `C` | Coordinates, output extent, or resampling policy |
| `G` | Whole-image or regional statistics/reduction |
| `M` | Another image, mask, frame, or reference |
| `X` | Metadata, profiles, calibration, camera, lens, or display information |
| `I` | Iteration, recurrence, state, pyramid, or multi-resolution processing |
| `L` | Learned parameters or semantic interpretation |

An operation may carry several codes. `P+V`, for example, is still pointwise but uses the RGB vector rather than treating each channel independently. `N+I` means that it samples a neighborhood and repeats or propagates work.

### Stack status terms

| Status | Meaning |
|---|---|
| **Existing** | A live Stack node or backend path performs substantially this operation. |
| **Partial** | Relevant machinery exists, but it is fixed inside a node, semantically incomplete, or not reusable. |
| **Needs fix** | A visible implementation exists but the audit found a formula, naming, serialization, alpha, or color-state defect. |
| **Missing** | No reusable live implementation was found. |
| **Specialized** | Stack performs the work only inside a higher-level RAW, denoise, frequency, export, or other special path. |
| **Out of scope** | Deliberately not part of the recommended conventional primitive system. |

These labels describe the audited working tree, not a promise about clean `HEAD` or a later version.

## Terms that must remain separate

| Term | Precise meaning in this package |
|---|---|
| **Color space** | A defined color representation. For an RGB space this normally requires primaries, a white point, and a transfer/encoding definition or an explicit statement that values are linear. |
| **Primaries** | Chromaticities defining what the R, G, and B axes mean. |
| **Transfer function** | The mapping between stored code values and linear-light values. |
| **Linear light** | Values proportional to represented light. It does not by itself identify the RGB primaries. |
| **Scene-referred** | Values interpreted relative to the captured or synthesized scene rather than a specific display. |
| **Display-referred** | Values already interpreted for a display/output condition. |
| **View transform** | A non-destructive viewing path from the working representation toward a display representation. |
| **Output transform** | The transform used to produce a particular encoded deliverable or display signal. |
| **Look transform** | A creative transform, distinct from the technical viewing/output transform. |
| **Luma** | A weighted signal computed in a named encoded RGB/video system. |
| **Luminance** | A colorimetric or physical light quantity, commonly the `Y` component of XYZ. |
| **Lightness** | A perceptual coordinate such as `L*` or Oklab `L`. |
| **Chroma** | Distance from a neutral axis in an appropriate color space. |
| **Saturation** | A context-dependent measure of colorfulness relative to brightness/lightness; it has no single universal formula. |
| **Straight alpha** | RGB is stored independently of alpha. Fully transparent pixels may still contain color. |
| **Premultiplied alpha** | Stored RGB has already been multiplied by alpha. |

## What the returned Codex audit established

The returned `STACK_IMAGE_MATH_AND_PIPELINE_AUDIT as of 7-12-26.md` is the authority for statements about the current program. Its most consequential verified findings are:

- Stack is C++17 with GLFW, Dear ImGui, OpenGL 4.3, stb image I/O, and LibRaw.
- Ordinary PNG/JPEG data is decoded to untagged RGBA8 and is not automatically linearized.
- RAW data is processed toward scene-linear, linear-sRGB-like RGB before becoming a generic `Image` wire.
- Generic `Image` wires do not record color, transfer, range, alpha, resolution, or scene/display state.
- Normal node outputs are full-canvas RGBA16F passes.
- There is no general pointwise pass fusion or cache memory budget.
- The viewport does not perform a Stack-managed display transform.
- Export independently rerenders, reads RGBA8, and writes an untagged 8-bit PNG.
- The graph is flat; groups are visual and presets are copied selections, not executable compounds.
- Current structural tests do not prove GPU formula, color, alpha, or import/export correctness.

Those findings are not arguments against the math-building-block direction. They identify the support system that must exist before that direction can be reliable.

## Research basis

The package uses standards and primary technical material from W3C, Khronos, the International Color Consortium, the Academy Color Encoding System, OpenColorIO, OpenEXR, MaterialX, OpenFX, OpenImageIO, Adobe DNG, and original image-processing papers. The detailed bibliography and direct URLs are in [18_Research_Sources.md](18_Research_Sources.md).

The outside sources are used for definitions, algorithms, and architecture lessons. They do not override verified facts about Stack’s current code.

## Intended use

These files are designed to be edited as Stack evolves. When a node is implemented or corrected:

1. Update its Stack status.
2. Record the exact implemented formula, domain, range, and alpha behavior.
3. Add a golden/reference test identifier.
4. Add the node or compound definition version.
5. Record any compatible fused implementation.

The operation catalog should remain an encyclopedia of capability. The public node browser should remain much smaller.
