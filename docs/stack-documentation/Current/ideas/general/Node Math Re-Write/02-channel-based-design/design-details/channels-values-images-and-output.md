# Channel-First Data Model And User Language

- Started: 2026-07-17
- Consolidated: 2026-07-20
- Authority: `../accepted-product-direction.md`
- Verification: current value, socket, Combine, Output, and shader paths inspected on 2026-07-20

This file owns the user-facing data model, partial-Image semantics, Channel
inspection/export boundary, and imported-image channel access. It no longer
stores conversational option history.

## Primary Mental Model

| User-facing family | Meaning | Examples |
| --- | --- | --- |
| **Value** | One uniform value | Boolean, Number, Vector, Coordinate, Matrix |
| **Channel** | One value at every pixel location | R, G, B, Alpha, Mask, Luminance, EV, Depth |
| **Image** | Spatial Channels with one image/display relationship | RGB, RGBA, partial R+B Image |
| **Data** | Structured non-pixel-grid resources | Curve, LUT, Histogram, Statistics, Metadata |
| **Specialized** | A domain with dedicated storage or execution | RAW, spectra, frame collections, model handles |

`Scalar`, `uniform`, and `field` remain valid internal or advanced mathematical
terms. The normal UI teaches Value and Channel first.

Value subtypes stay distinct when their promises matter. A Coordinate is a
location; a 2D Vector is a direction, offset, or scale. They may store the same
number of components without being interchangeable.

## Channel Shape And Meaning

A Channel's mathematical shape does not decide its role. Keep these concerns
separate:

- semantic purpose: Neutral, Mask, Alpha, Luminance, EV, Depth;
- Image component assignment: R, G, B, A;
- units and expected range;
- extent and sampling; and
- preview interpretation.

The canvas may summarize them with a badge, but the descriptor must not merge
them. An extracted R Channel may later be used as a Mask without confusing its
component origin, receiving role, units, or view mode.

Mask and Alpha are ordinary Channels. Their roles affect formula expectations,
labels, diagnostics, and previews; they do not remove normal branching or
Channel editing.

## Images, Partial Images, And Channel Sets

An Image is defined by an image/display relationship, not by fixed RGBA
storage. It may contain any subset of R/G/B/A:

```text
R + B          -> Image · R, B
R + G + B      -> complete RGB Image
R + G + B + A  -> complete RGBA Image
```

An unconnected component is absent. A connected Channel filled with zero is
present. A renderer may use zero to display an absent color component, but that
fallback never changes the descriptor.

An arbitrary bundle without one image/display relationship is a Channel Set.
For example, depth + temperature + mask is not mislabeled as a color Image.
Channel Set remains an advanced structural term rather than a sixth primary
browser category.

## Image Combine

Image Combine exposes R, G, B, and A Channel inputs. Socket position assigns
component role inside the result; an upstream Channel does not need a permanent
R/G/B/A role first.

- Unconnected color inputs remain absent.
- Present inputs must have compatible extents.
- A complete-RGB operation can detect a missing component even if fixed RGBA
  storage samples it as zero.
- Opaque Alpha creation is owned by the alpha topic.

## Standalone Channel Inspection

A standalone Channel connected to the inspection boundary defaults to Neutral:

```text
Neutral -> value copied to display R, G, and B
Red     -> value in display R; G and B are zero
Green   -> value in display G; R and B are zero
Blue    -> value in display B; R and G are zero
```

Output owns the saved normal inspection mode. A temporary viewport override may
be used for debugging. Neither changes Channel data or lives on the wire.

Inspection is not Image construction. A Channel entering Image R is red. A
grayscale RGB Image is authored by wiring the same Channel to R, G, and B.
Ordinary RGB/RGBA export is disabled while Output is only inspecting a Channel.
A future explicit single-channel format is a separate export mode.

## Value Broadcast

A port declared technically as `Value/Channel` accepts either:

- a Value, reused uniformly at every pixel; or
- a Channel, sampled independently at each location.

Normal UI and accessible text spell this out, for example
`Amount · Value or Channel`. Compact technical surfaces may retain the slash
shorthand. Color and icons supplement the readable label rather than replacing
it.

This is both a semantic rule and a likely optimization. It does not create a
standalone Channel texture. Reusable spatial constants use Constant Channel and
an explicit extent contract.

An Amount Channel changes a parameter per pixel. A Mask Channel blends the
original and processed results. Their underlying shape is the same; their
formula position is not.

## Imported Images

Import remains compact. **Dissolve Into Channels** is a selected-source,
undoable action that creates ordinary Split/Combine structure and reconnects
former Image consumers through the Combine result. It includes Alpha handling,
preserves absent source components, and must preserve fan-out and exact graph
meaning. Visible wires do not require physically split textures.

## Current Code Gap

Current Stack still:

- exposes Channel Split/Combine components as `ScalarField` sockets;
- marks every Combine input optional;
- writes fixed RGBA output with zero RGB/one Alpha defaults;
- routes an extracted component dropped on Output to a matching component
  socket rather than a general Neutral inspection boundary; and
- has no serialized partial-Image presence set or general source-dissolve
  mutation matching this design.

The exact implementation work is C1, C2, C4, C5, and C7 in
`../work-still-to-define.md`.

## Related Topics

- `connections-errors-and-visual-feedback.md`
- `alpha-masks-and-math.md`
- `execution-and-performance.md`
- `compound-nodes-and-unpacking.md`
