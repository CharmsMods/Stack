# Naming And Industry Terms — Findings

- Research date: 2026-07-21
- Addresses: R2 and the former broad Q10
- Scope: user-facing data names, Channel Set, descriptor vocabulary,
  partial-Image wording, mixed Value/Channel pins, spatial constants, and
  Vector/Coordinate labels
- Adopted: 2026-07-22
- Status: complete; R2 closed
- Product authority: `../../02-channel-based-design/accepted-product-direction.md`

This report records research evidence and recommendations. It does not change
accepted product direction or activate implementation.

## Plain-Language Result

Stack should keep **Value**, **Channel**, **Image**, **Data**, and
**Specialized**. The comparison found no clearer industry-wide replacement for
that small public model.

The narrower terms also mostly hold up:

- **Channel Set** is an established compositing term and is safer than
  **Layer**, which already means something else in Stack.
- **Constant Channel** clearly distinguishes a reusable spatial result from a
  single Value.
- `Image · R, B` is a compact and truthful partial-Image badge. Details should
  say `Present channels: R, B`, while a failed RGB operation should say
  `Missing G`.
- Component, role, units/range, spatial state, and preview choice should remain
  separate facts.
- Vector remains the general numeric type. Coordinate, UV Coordinate, Pixel
  Coordinate, Direction, Offset, and Scale should be purpose-specific socket
  labels where the promise matters.

The user confirmed that normal pins and accessible text should say
`Value or Channel` instead of the compact technical shorthand
`Value/Channel`.

## What The Sources Establish

### Channels And Sets

- OpenEXR permits arbitrary combinations of channels and explicitly supports
  common RGBA subsets. It also groups logically related channels into named
  layers by convention.
- Nuke calls a layer a **channel set** and uses it for RGBA, depth, motion,
  masks, and other related channels. This directly validates **Channel Set**
  as recognizable professional terminology.
- Nuke's layer cannot be copied as Stack's normal word because Stack already
  uses layers for document/canvas organization and because an OpenEXR/Nuke
  layer does not necessarily imply compositing order.

### Shape, Meaning, And Metadata

- Adobe Photoshop describes color, alpha, and spot channels as the same broad
  grayscale-image shape carrying different kinds of information. After
  Effects separately warns that the fourth component and transparency purpose
  are commonly associated but not logically identical.
- MaterialX separates numerical type from color space, units, semantic hints,
  and whether a value is uniform or spatially varying. It also distinguishes
  color vectors from ordinary vectors even when their storage shapes match.
- OpenEXR separately records a channel's name, numeric type, and sampling rate.

These precedents support Stack's choice to keep Channel shape separate from
component assignment, semantic role, units/range, spatial state, and preview
interpretation.

### Values, Spatial Inputs, And Constants

- MaterialX distinguishes uniform inputs from spatially varying streams.
- OpenImageIO explicitly supports operation inputs that may be an image or a
  constant. Its generated patterns still require an output region or existing
  destination to establish spatial size.
- Nuke can assign constant zero or one to channels, including making Alpha
  solid. Adobe Substance uses **Uniform Color** for a solid image-like result.
- OpenUSD uses **uniform** with a different domain-specific meaning—one value
  per surface segment rather than necessarily one value for an entire graph.

The shared concept is real, but **uniform** is too overloaded for Stack's main
UI. **Value** and **Constant Channel** keep the spatial distinction visible.

### Accessible Presentation

Blender and other node systems commonly use socket color, but WCAG guidance
states that color should not be the only carrier of meaning. Stack should
therefore keep readable type or role text available on the node, wire detail,
tooltip, and accessible name. Color and icons may reinforce the text.

## Recommended Stack Vocabulary

| Technical concern | Stack's friendly label | Exact meaning | Ambiguity to avoid | Example presentation |
| --- | --- | --- | --- | --- |
| Uniform scalar/vector value | **Value** | One Boolean, number, vector, coordinate, matrix, or similar value reused without a pixel grid | `Uniform` has different meanings across MaterialX, USD, and image tools | `Exposure · Value` |
| Spatial scalar field | **Channel** | One value at each pixel location with a declared spatial state | Do not imply that every Channel is color, Alpha, or a Mask | `Channel · Luminance` |
| Related display components | **Image** | Spatial Channels sharing one image/display relationship | Do not infer missing components from fixed RGBA storage | `Image · R, B` |
| Arbitrary related spatial fields | **Channel Set** | A named bundle without one image/display relationship | Avoid **Layer**, which conflicts with Stack document layers and can imply order | `Channel Set · Depth, Mask, Temperature` |
| Membership in an Image | **Component** | R, G, B, or A assignment inside a particular Image | Component origin is not the Channel's permanent purpose | `Component: R` |
| Intended downstream meaning | **Channel Role** | Neutral, Mask, Alpha, Luminance, EV, or another defined purpose | A role must not silently remap or clamp numbers | `Channel · Mask` |
| Measurement interpretation | **Units / Expected Range** | EV, normalized coverage, depth units, or another declared measurement promise | Do not merge units with role or storage type | `Units: EV` |
| Inspection mapping | **View As** | Neutral, Red, Green, Blue, or another non-mutating preview interpretation | Preview must not assign components or change export data | `View As: Neutral` |
| Spatially reusable constant | **Constant Channel** | One constant value over an explicit or referenced extent, with an ordinary Channel output | Do not confuse it with a Value that has no extent | `Constant Channel · Alpha · 1.0` |
| Generic tuple | **Vector** | Two, three, or four numeric components without an implied location | Do not use it when location, direction, or units are required promises | `Offset · Vector2` |
| Location or sampling position | **Coordinate** with a domain alias | A position in a named space such as UV or pixels | Do not make every Vector interchangeable with a Coordinate | `UV Coordinate · Value` |

## Recommended Pin And Wire Pattern

Use the purpose as the primary pin label and provide the accepted shape in
readable secondary text:

```text
Amount · Value or Channel
Mask · Channel
Image · R, G, B
Channel · Mask
```

For very compact inspection surfaces, `Value | Channel` may be used as a type
badge, but the accessible name and expanded detail should say
`Value or Channel`. Color must remain supplemental.

A friendly quick action such as `Use as Mask` may insert the explicit
`Set Channel Role` operation. The authored node should keep the exact name
`Set Channel Role` because it describes a branch-local semantic change rather
than a numerical conversion.

## Adopted User Decision

Confirmed 2026-07-22: ordinary pin and wire text uses the spoken wording
`Amount · Value or Channel`. Contracts and compact internal notation may
continue to use `Value/Channel`.

The word **or** is easier to read aloud, clearer in an accessible name, and
does not look like arithmetic or a filesystem path. No other naming question
needs user input from this research pass.

## Primary And Official Sources

All sources were accessed 2026-07-21.

- [OpenEXR Technical Introduction](https://openexr.com/en/latest/TechnicalIntroduction.html)
  — arbitrary channel combinations, RGBA subsets, channel names/types/sampling,
  and logical layer grouping.
- [Nuke: Understanding Channels and Layers](https://learn.foundry.com/nuke/15.0/content/comp_environment/channels/understanding_channels.html)
  — layer/channel-set terminology and qualified channel names.
- [Nuke: Shuffling Channels](https://learn.foundry.com/nuke/content/comp_environment/channels/swapping_channels.html)
  — channel reassignment, set presentation, and constant black/white channels.
- [Adobe Photoshop Channel Basics](https://helpx.adobe.com/photoshop/using/channel-basics.html)
  — color, alpha, and spot channels and channel inspection.
- [Adobe After Effects: Alpha Channels, Masks, and Mattes](https://helpx.adobe.com/after-effects/desktop/work-with-transparency-and-compositing/work-with-alpha-channels-and-masks/alpha-channels-masks-mattes.html)
  — distinction between the fourth component and transparency purpose.
- [Adobe Substance 3D Designer: Nodes Overview](https://helpx.adobe.com/substance-3d-designer/using/nodes-overview.html)
  — Uniform Color and adaptive color/grayscale graph inputs.
- [MaterialX Specification](https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/documents/Specification/MaterialX.Specification.md)
  — typed streams, uniform inputs, color/vector distinction, colorspaces,
  units, and UI names.
- [OpenImageIO ImageBufAlgo](https://openimageio.readthedocs.io/en/stable/imagebufalgo.html)
  — channel ranges, image-or-constant inputs, and spatial requirements for
  generated patterns.
- [OpenUSD Primvars](https://openusd.org/release/user_guides/primvars.html)
  — domain-specific constant, uniform, and varying interpolation meanings.
- [Blender Node Parts](https://docs.blender.org/manual/en/5.0/interface/controls/nodes/parts.html)
  — visible socket types and Value/Vector/Color conventions.
- [W3C: Understanding Use of Color](https://www.w3.org/WAI/WCAG22/Understanding/use-of-color)
  — text or shape must supplement color when color carries meaning.

## Adoption Result

The source-backed result keeps the accepted primary model unchanged. NMR-140
records the user-facing wording, the accepted direction is synchronized, and
R2 is closed. The future C5 contract must carry this wording into UI
accessibility and test expectations.
