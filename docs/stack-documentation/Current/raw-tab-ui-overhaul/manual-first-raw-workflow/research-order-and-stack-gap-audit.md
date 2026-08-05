# RAW Order Research and Stack Gap Audit

Last verified: July 23, 2026.

## What the External Guidance Actually Says

Adobe's public editing guidance is a user workflow, not a disclosure of Camera
Raw's internal pixel pipeline. Lightroom Classic recommends evaluating the
image and histogram first, then starting with global Basic controls such as
white balance and tonal scale, refining with Tone Curve and color panels,
handling detail/lens correction, retouching and local adjustments, and finally
soft proofing where relevant. Adobe also says Camera Raw interprets sensor data
and metadata to construct a color image and that the default Adobe Color
profile supplies a color-and-contrast rendering foundation. Stack should borrow
the legible workflow and nondestructive recipe model, not pretend that Adobe's
default profile is a neutral sensor rendering.

darktable publishes a much more explicit processing model. Its recommended
scene-referred workflow keeps most work in linear RGB and places the
display-referred transition late. Its documented dependencies include raw
black/white correction, technical white-balance scaling in RAW space,
highlight reconstruction before demosaic, demosaic before an input color
profile, scene exposure and tone work, and a late filmic/sigmoid-style display
mapping. darktable also warns that moving modules changes the actual math and
that many modules only make sense in their designed domain.

The Adobe DNG specification is the more useful Adobe source for Stack's
technical import order. It makes the linearization table, spatial/repeating
black model, white levels, active/masked areas, CFA description, camera
calibration, illuminants, baseline exposure, and ordered opcode lists part of
the interpretation contract. These are not interchangeable creative sliders.

Primary sources:

- [Adobe Lightroom Classic editing workflow](https://helpx.adobe.com/lightroom-classic/desktop/help/applying-adjustments-develop-module-basic.html)
- [Adobe Camera Raw introduction](https://helpx.adobe.com/camera-raw/using/introduction-camera-raw.html)
- [Adobe Camera Raw profiles](https://helpx.adobe.com/camera-raw/using/adjust-color-rendering-camera-camera.html)
- [Adobe DNG Specification 1.7.1.0](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf)
- [darktable pixelpipe and module order](https://docs.darktable.org/usermanual/development/en/darkroom/pixelpipe/the-pixelpipe-and-module-order/)
- [darktable white balance](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/white-balance/)
- [darktable color calibration](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/color-calibration/)
- [Malvar, He, and Cutler demosaicing paper](https://www.microsoft.com/en-us/research/publication/high-quality-linear-interpolation-for-demosaicing-of-bayer-patterned-color-images/)
- [W3C sRGB specification background](https://www.w3.org/Graphics/Color/sRGB)
- [Little CMS integration tutorial](https://www.littlecms.com/LittleCMS2.17%20tutorial.pdf)

## Stack's Truthful V1 Order

New schema-7 RAW recipes now use this explicit baseline:

```text
decode sensor mosaic and metadata
-> resolve active area and CFA planes
-> DNG LinearizationTable
-> repeating/per-plane/spatial black subtraction
-> per-plane white normalization
-> supported OpcodeList2 gain maps in ActiveArea coordinates, clipping after each opcode
-> camera/as-shot or authored RAW-space WB multipliers
-> named MHC 5x5 Bayer demosaic
-> camera matrix and illuminant interpolation
-> linear Rec. 2020 / D65 working RGB
-> disclosed DNG BaselineExposure plus authored RAW Exposure
-> optional manual Local Range
-> optional manual Finish Tone
-> explicit View Transform to display-mapped linear sRGB
-> exact sRGB transfer encoding
-> direct preview/export result
```

Necessary interpretation and creative edits are represented separately.
Truthful V1 does not silently enable mosaic denoise, highlight reconstruction,
false-color suppression, defringe, local exposure, Local Range, or Finish Tone.
The View Transform is necessary to show scene-linear values on a finite display,
but its controls and output encoding are explicit.

## Critical Comparison

| Area | Stack before this pass | Truthful V1 now | Still missing or deliberately deferred |
| --- | --- | --- | --- |
| Compatibility | One mutable behavior changed old projects implicitly | Schema-7 processing contract; older recipes migrate explicitly to Legacy V1 | A user-facing legacy-to-truthful comparison/migration tool |
| Sensor normalization | Mostly scalar/per-channel GPU black and white handling | CPU reference and render path apply LUT, repeating/spatial black, per-plane white, preserve below-black values, and cap at sensor white | Full truthful handling for every Linear DNG variation |
| DNG corrections | Gain maps were partial and the processing identity was unclear | Supported OpcodeList2 gain maps use ActiveArea-relative, pixel-center coordinates after normalization and clip after each opcode; unsupported-list counts are visible | OpcodeList1, non-gain OpcodeList2 operations, OpcodeList3, ProfileGainTableMap |
| White balance | Controls mixed unsupported temperature/tint and inert sampled coordinates; render multiplied after demosaic | As Shot or canonical camera-plane multipliers; Truthful V1 scales CFA samples before demosaic | Calibrated camera-specific temperature/tint round trip and a real neutral picker |
| Demosaic | UI exposed a quality choice but render effectively forced bilinear or used an unnamed placeholder | Named, paper-derived Malvar-He-Cutler 5x5 path plus explicit bilinear preview option | X-Trans/quad/non-Bayer support and a higher-end artifact-adaptive final algorithm |
| Highlight handling | Heuristic post-demosaic reconstruction could look plausible without being physically well placed | Reconstruction is Off by default in Truthful V1 | A named, tested pre-demosaic reconstruction method |
| Camera color | Matrix path primarily targeted linear sRGB and used a rough dual-illuminant heuristic | D50-to-D65 adaptation, iterative neutral/CCT interpolation, and linear Rec. 2020 working output | Full DCP/ICC LUT/profile pipeline, ForwardMatrix edge cases, spectral accuracy |
| Exposure | Global scene exposure and display fit were easy to confuse | RAW Exposure is scene-linear; DNG BaselineExposure is disclosed separately; View Transform says it is display mapping | Reference-image validation across cameras |
| Local Range | Custom sparse edge-aware/bilateral-like approximation was presented with stronger implications than its evidence supports | Manual graph is isolated in its own window; identity defaults remain off | Replace or validate against a proper guided-filter/multiscale design with halo and noise fixtures |
| Finish Tone | Buried in a long dropdown list | Persistent graph window and identity default | Perceptual/color-science validation for extreme curves |
| Display/output | View Transform returned clamped linear display RGB while preview/export semantics were vague | Rec. 2020-to-sRGB primaries, exact sRGB encoding, and graph descriptors for linear versus encoded output | Operating-system monitor ICC transform and wider-gamut output contracts |
| Workspace | One long vertical inspector mixed diagnostics, automation, graphs, and manual controls | Docked Controls, Image, and Tone Graphs windows with resettable layout | Native visual-density review, persistent user layout polish, dedicated scopes window |

## Product Workflow Groundwork

The intended manual flow is now:

1. Inspect source identity, metadata warnings, CFA/clipping evidence, and the
   declared processing version.
2. Choose As Shot or authored camera-plane white-balance multipliers.
3. Place scene brightness with RAW Exposure.
4. Use Local Range only when a local/tonal-zone correction is intended.
5. Shape global tone relationships with Finish Tone.
6. Set the View Transform's black, middle-gray, white, toe, and shoulder
   explicitly.
7. Inspect output color/transfer assumptions before export.

This is Adobe-like in discoverability and sequencing, and darktable-like in
domain precision and inspectability. It is intentionally not a clone of either.

## Next Technical Gates

The next pass should be evidence-driven and should not reactivate automation:

1. Build real-camera fixtures covering at least two Bayer CFA layouts, a DNG
   linearization table, spatial black deltas, a gain map, dual illuminants, and
   a Linear DNG.
2. Compare full-resolution Truthful V1 output to dcraw/LibRaw metadata
   readback, darktable's neutral scene pipeline, and one independently computed
   reference patch set. Compare stages, not visual similarity alone.
3. Implement a pre-demosaic highlight method or keep reconstruction explicitly
   unavailable.
4. Replace Local Range's sparse approximation with a documented guided or
   multiscale filter and require identity, constant-field, edge-halo,
   monotonicity, finite-value, and tiled/full-frame tests.
5. Add monitor ICC conversion at the presentation boundary; Little CMS is a
   suitable implementation candidate, not an automatic product decision.
6. Add native visual review for dock persistence, minimum widths, graph
   interaction, keyboard focus, and small-window behavior.
