# Standards And Research Source Ledger

> **Source ledger:** Verify current primary sources when a claim affects an
> implementation decision. Old workflow commands in this ledger do not override
> the current workstream entry files.

Original package research date: 2026-07-12
Latest focused research date: 2026-07-21

## Source policy

This package prioritizes standards, official project documentation, official specifications, and original papers. Product documentation is used for architecture precedents, not as proof that Stack should copy a product’s UI or formula.

The returned Codex audit is the authority for current Stack implementation facts. The sources below support definitions, algorithms, interoperability rules, and future architecture proposals.

## Naming and node-language review — 2026-07-21

| Source | Type | Used for |
|---|---|---|
| [OpenEXR Technical Introduction](https://openexr.com/en/latest/TechnicalIntroduction.html) | ASWF official documentation | Arbitrary channel combinations, RGBA subsets, channel names/types/sampling, and logical layer grouping |
| [Nuke: Understanding Channels and Layers](https://learn.foundry.com/nuke/15.0/content/comp_environment/channels/understanding_channels.html) | Foundry official documentation | Layer/channel-set terminology and qualified channel names |
| [Nuke: Shuffling Channels](https://learn.foundry.com/nuke/content/comp_environment/channels/swapping_channels.html) | Foundry official documentation | Channel reassignment, channel-set presentation, and constant black/white channels |
| [Adobe Photoshop Channel Basics](https://helpx.adobe.com/photoshop/using/channel-basics.html) | Adobe official documentation | Color, alpha, and spot channel purposes and inspection |
| [Adobe After Effects: Alpha Channels, Masks, and Mattes](https://helpx.adobe.com/after-effects/desktop/work-with-transparency-and-compositing/work-with-alpha-channels-and-masks/alpha-channels-masks-mattes.html) | Adobe official documentation | Distinction between the fourth component and transparency purpose |
| [Adobe Substance 3D Designer: Nodes Overview](https://helpx.adobe.com/substance-3d-designer/using/nodes-overview.html) | Adobe official documentation | Uniform Color and adaptive color/grayscale graph inputs |
| [MaterialX Specification](https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/documents/Specification/MaterialX.Specification.md) | ASWF standard | Typed streams, uniform inputs, color/vector distinction, colorspaces, units, and UI names |
| [OpenImageIO ImageBufAlgo](https://openimageio.readthedocs.io/en/stable/imagebufalgo.html) | ASWF official documentation | Channel ranges, image-or-constant inputs, and spatial requirements for generated patterns |
| [OpenUSD Primvars](https://openusd.org/release/user_guides/primvars.html) | Alliance for OpenUSD official documentation | Domain-specific constant, uniform, and varying meanings |
| [Blender Node Parts](https://docs.blender.org/manual/en/5.0/interface/controls/nodes/parts.html) | Blender official manual | Visible socket types and Value/Vector/Color conventions |
| [W3C: Understanding Use of Color](https://www.w3.org/WAI/WCAG22/Understanding/use-of-color) | W3C accessibility guidance | Requirement to supplement color with readable meaning |

Accessed 2026-07-21. Findings:
`../channel-system-findings/2026-07-21-naming-and-industry-terms.md`.

## Color, image state, profiles, and display

| Source | Type | Used for |
|---|---|---|
| [W3C CSS Color Module Level 4](https://www.w3.org/TR/css-color-4/) | W3C specification | sRGB/linear-sRGB transfer functions; RGB↔XYZ; Lab/LCh and Oklab/OkLCh; chromatic adaptation sequence; extended-range color; gamut mapping; premultiplied interpolation |
| [W3C Compositing and Blending Level 1](https://www.w3.org/TR/compositing-1/) | W3C specification | General compositing equation; Porter–Duff factors; separable/nonseparable blend modes; standard soft-light/dodge/burn/overlay behavior; group isolation |
| [PNG Third Edition](https://www.w3.org/TR/png-3/) | W3C/PNG specification | 8/16-bit PNG; color-chunk precedence; ICC/sRGB/gAMA/cHRM/cICP; unassociated alpha |
| [ICC.1:2022 v4.4 Profile Specification](https://www.color.org/specification/ICC.1-2022-05.pdf) | ICC specification | Profile Connection Space, image state, rendering intents, matrix/TRC/LUT transforms, D50 PCS, profile metadata |
| [ICC profile embedding guidance](https://www.color.org/profile_embedding.xalter) | ICC official guidance | Embedding and identifying profiles in common image formats |
| [ACES system overview](https://docs.acescentral.com/background/overview/) | Academy/ACES documentation | Separation of input, working/interchange, look, rendering, and output concepts |
| [ACES encoding overview](https://docs.acescentral.com/encodings/introduction/) | Academy/ACES documentation | Working/interchange encodings and intended roles |
| [ACEScg](https://docs.acescentral.com/encodings/acescg/) | Academy/ACES documentation | Example of a linear working encoding distinct from archive and grading encodings |
| [ACES Output Transforms](https://docs.acescentral.com/system-components/output-transforms/) | Academy/ACES documentation | Separation of rendering transform from target display encoding |
| [ACES Common LUT Format specification](https://docs.acescentral.com/clf/specification/) | Academy specification | Versioned ordered ProcessList, 1D/3D LUTs, matrices, ranges, log/exponent nodes, bit depth, descriptors and IDs |
| [OpenColorIO overview](https://opencolorio.readthedocs.io/en/latest/) | ASWF official documentation | Production color-management goals and processor/config model |
| [OpenColorIO configuration authoring](https://opencolorio.readthedocs.io/en/latest/guides/authoring/authoring.html) | ASWF official documentation | Roles, file rules, scene/display spaces, data role, displays/views, looks, config validation |
| [OpenColorIO ColorSpace API](https://opencolorio.readthedocs.io/en/latest/api/colorspace.html) | ASWF API documentation | Structured color-space state, reference-space and data semantics |
| [OpenColorIO ViewTransform API](https://opencolorio.readthedocs.io/en/latest/api/viewtransform.html) | ASWF API documentation | Scene-reference to display-reference transforms |
| [OpenEXR Scene Linear](https://openexr.com/en/latest/SceneLinear.html) | ASWF official documentation | Scene-linear convention, HDR values and display separation |
| [OpenEXR Technical Introduction](https://openexr.com/en/latest/TechnicalIntroduction.html) | ASWF technical documentation | HALF/FLOAT/UINT channels, arbitrary channels, data/display windows, premultiplied convention |
| [OpenEXR Standard Attributes](https://openexr.com/en/latest/StandardAttributes.html) | ASWF official documentation | Chromaticities, white luminance, adopted neutral and metadata conventions |
| [OpenGL 4.6 Core Specification](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf) | Khronos specification | Texture formats, sRGB texture conversion, framebuffer conversion, pixel transfer/readback semantics |
| [ASC Color Decision List](https://theasc.com/asc/asc-cdl) | ASC official reference | Named Slope/Offset/Power and saturation grading model; supports keeping ASC CDL distinct from ambiguous LGG |

## Node graphs, compounds, evaluation, and image-processing APIs

| Source | Type | Used for |
|---|---|---|
| [MaterialX Specification](https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/documents/Specification/MaterialX.Specification.md) | ASWF standard | Typed node interfaces, definitions, implementations, versions, target-specific behavior and graph representation |
| [MaterialX NodeDef API](https://materialx.org/docs/api/class_node_def.html) | ASWF API documentation | Separating a public node definition from its implementation |
| [MaterialX NodeGraph API](https://materialx.org/docs/api/class_node_graph.html) | ASWF API documentation | Graph-defined implementations, typed inputs/outputs, validation, instances, topological ordering and recursive subgraph flattening |
| [OpenFX Image Effect Actions](https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectActions.html) | Open Effects Association specification | Region of definition, ROI, frames needed, render window, identity/bypass and clip preferences |
| [OpenFX Image Effect Reference](https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectReference.html) | Open Effects Association specification | Clip components/depth, premultiplication, render scale, project extent and host/effect contracts |
| [Halide paper: Decoupling Algorithms from Schedules](https://people.csail.mit.edu/jrk/halide12/halide12.pdf) | Original systems paper | Separation of mathematical meaning from execution schedule; fusion/materialization/locality as performance decisions |
| [Halide scheduling lesson 1](https://halide-lang.org/tutorials/tutorial_lesson_05_scheduling_1.html) | Official tutorial | Compute/store placement, tiling and locality |
| [Halide scheduling lesson 2](https://halide-lang.org/tutorials/tutorial_lesson_08_scheduling_2.html) | Official tutorial | Scheduling multi-stage pipelines without changing algorithm meaning |
| [OpenImageIO ImageBuf](https://openimageio.readthedocs.io/en/latest/imagebuf.html) | ASWF official documentation | ImageSpec, data/full windows, channel and pixel data model |
| [OpenImageIO ImageBufAlgo](https://openimageio.readthedocs.io/en/latest/imagebufalgo.html) | ASWF official documentation | Explicit ROI/channel spans, image-or-constant arguments, output allocation, many reference operations |
| [OpenImageIO Image I/O API](https://openimageio.readthedocs.io/en/latest/imageioapi.html) | ASWF official documentation | File metadata, format capabilities and typed image I/O |
| [Blender Node Groups](https://docs.blender.org/manual/en/latest/interface/controls/nodes/groups.html) | Blender official manual | Shared group definitions and deliberate group interfaces |
| [Blender NodeTreeInterface API](https://docs.blender.org/api/current/bpy.types.NodeTreeInterface.html) | Blender API documentation | Programmatic group socket/interface representation |
| [Houdini asset namespaces and versions](https://www.sidefx.com/docs/houdini/assets/namespaces.html) | SideFX official documentation | Namespaced, versioned reusable digital-asset definitions |
| [Nuke Gizmos](https://learn.foundry.com/nuke/content/comp_environment/configuring_nuke/creating_gizmos.html) | Foundry official documentation | Promoted controls around an internal group and reusable packaged tools |

## Filtering, morphology, and local operations

| Source | Type | Used for |
|---|---|---|
| [OpenCV image filtering reference](https://docs.opencv.org/4.x/d4/d86/group__imgproc__filter.html) | Official API documentation | Border policy, filtering, morphology, derivatives, Gaussian/box/median/bilateral and correlation convention |
| [Tomasi and Manduchi, Bilateral Filtering for Gray and Color Images](https://doi.org/10.1109/ICCV.1998.710815) | Original paper | Bilateral spatial/range weighting and perceptual color-distance discussion |
| [He, Sun, and Tang, Guided Image Filtering](https://doi.org/10.1109/TPAMI.2012.213) | Original paper | Guided filter’s local linear model and parameters |
| [Canny, A Computational Approach to Edge Detection](https://doi.org/10.1109/TPAMI.1986.4767851) | Original paper | Multistage edge detector and optimality criteria |
| [Burt and Adelson, The Laplacian Pyramid as a Compact Image Code](https://doi.org/10.1109/TCOM.1983.1095851) | Original paper | Gaussian/Laplacian pyramids and reconstruction |
| [Floyd and Steinberg, An Adaptive Algorithm for Spatial Grey Scale](https://www.visgraf.impa.br/Courses/ip00/proj/Dithering1/floyd-steinberg.pdf) | Original algorithm paper copy | Sequential quantization-error diffusion |

## Geometry and reconstruction

| Source | Type | Used for |
|---|---|---|
| [OpenCV geometric transform reference](https://docs.opencv.org/4.x/da/d54/group__imgproc__transform.html) | Official API documentation | Inverse mapping, affine/projective transforms, interpolation and border modes |
| [Mitchell and Netravali, Reconstruction Filters in Computer Graphics](https://doi.org/10.1145/378456.378514) | Original paper | Bicubic reconstruction as a parameterized family rather than one kernel |
| [Duchon, Lanczos Filtering in One and Two Dimensions](https://doi.org/10.1175/1520-0450(1979)018%3C1016:LFIOAT%3E2.0.CO;2) | Original paper | Windowed-sinc/Lanczos reconstruction |
| [Avidan and Shamir, Seam Carving for Content-Aware Image Resizing](https://doi.org/10.1145/1275808.1276390) | Original paper | Energy map and dynamic-program seam removal/insertion |

## Global statistics, tone, and analysis

| Source | Type | Used for |
|---|---|---|
| [OpenCV histogram reference](https://docs.opencv.org/4.x/d6/dc7/group__imgproc__hist.html) | Official API documentation | Histograms, CDF-based equalization, comparison and backprojection |
| [Pizer et al., Adaptive Histogram Equalization and Its Variations](https://doi.org/10.1016/S0734-189X(87)80186-X) | Original paper | Local adaptive histogram methods |
| [Zuiderveld CLAHE reference implementation](https://www.realtimerendering.com/resources/GraphicsGems/gemsiv/clahe.c) | Original Graphics Gems reference code | Tile clip/excess redistribution and mapping interpolation |
| [Otsu, A Threshold Selection Method from Gray-Level Histograms](https://doi.org/10.1109/TSMC.1979.4310076) | Original paper | Automatic threshold from between-class variance |
| [Reinhard et al., Photographic Tone Reproduction for Digital Images](https://doi.org/10.1145/566570.566575) | Original paper | Global/local photographic tone mapping and log-average concepts |
| [Reinhard et al., Color Transfer between Images](https://doi.org/10.1109/38.946629) | Original paper | One named mean/std color-transfer method; evidence that “match color” is a family |
| [He, Sun, and Tang, Dark Channel Prior Dehazing](https://doi.org/10.1109/TPAMI.2010.168) | Original paper | Atmospheric light/transmission estimation and image recovery |

## Multiple-image and reference operations

| Source | Type | Used for |
|---|---|---|
| [Debevec and Malik, Recovering High Dynamic Range Radiance Maps from Photographs](https://www.pauldebevec.com/Research/HDR/debevec-siggraph97.pdf) | Original paper | Response recovery and radiance HDR merge from exposure brackets |
| [Mertens, Kautz, and Van Reeth, Exposure Fusion](https://doi.org/10.1109/PG.2007.23) | Original paper | Contrast/saturation/well-exposedness weighting and pyramid fusion; distinction from radiance HDR |
| [Brown and Lowe, Automatic Panoramic Image Stitching](https://doi.org/10.1007/s11263-006-0002-3) | Original paper | Feature matching, robust model estimation and panorama pipeline |
| [Burt and Adelson, A Multiresolution Spline with Application to Image Mosaics](https://doi.org/10.1145/800059.801126) | Original paper | Multiband blending |
| [Pérez, Gangnet, and Blake, Poisson Image Editing](https://doi.org/10.1145/882262.882269) | Original paper | Gradient-domain seamless cloning/compositing |

## RAW, calibration, and camera processing

| Source | Type | Used for |
|---|---|---|
| [Adobe DNG Specification 1.7.1.0](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf) | Adobe specification | RAW linearization, black/white levels, CFA, active area, WB, camera/profile matrices, illuminants, noise, opcodes and gain maps |
| [Adobe Digital Negative and SDK](https://helpx.adobe.com/camera-raw/digital-negative.html) | Adobe official page | Current DNG specification/SDK landing page |
| [LibRaw API data structures](https://www.libraw.org/docs/API-datastruct.html) | LibRaw official documentation | Camera RAW metadata structures used by a practical decoder |

## Model-runtime boundary

| Source | Type | Used for |
|---|---|---|
| [ONNX introduction](https://onnx.ai/onnx/intro/) | ONNX official documentation | Versioned model graph and tensor contract |
| [ONNX Runtime execution providers](https://onnxruntime.ai/docs/execution-providers/) | Microsoft/ONNX Runtime documentation | Provider/runtime identity and portability considerations |

## Stack-specific evidence

The following claims in this package come from the returned Codex audit rather than outside sources:

- exact node inventory and registration state;
- actual shader formulas;
- buffer/texture formats;
- preview/export paths;
- current alpha defects;
- current graph flatness and preset/group semantics;
- missing fusion, types, target budgeting and tests;
- RAW pipeline and current View Transform behavior.

For present-day Stack status, inspect current code and tests. Create a new dated
audit or focused audit delta when a broad snapshot would be useful; do not
rewrite the preserved 2026-07-12 audit or its historical model map to resemble
newer code.
