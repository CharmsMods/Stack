# DaVinci Resolve Layout Research

Status: official product and Resolve 20 Colorist Guide pass complete.

## Default Color Page Composition

Blackmagic's Resolve 20 Colorist Guide labels the default Color page as a fixed
composition:

```text
top left:    Gallery
top middle:  Viewer
top right:   Node Editor
middle:      Thumbnail timeline
bottom left: Primaries color wheels
bottom mid:  Curves
bottom right: Keyframe Editor / Scopes / Info
lower strip: Mini-timeline
```

These are panels, but they behave as assigned regions in a page rather than
free-floating windows. The top interface toolbar shows and hides optional
regions. Hiding Gallery or Mini-timeline gives their area back to the viewer
and remaining panels. At resolutions below 1920x1080, Resolve merges palette
groups instead of relying on arbitrary docking.

This is the strongest evidence for Stack's desired fixed-but-adaptive model:
regions have known roles; users decide which roles are present; the application
owns how the remaining regions reflow.

## Palette Navigation

Resolve exposes its deep toolset with compact button rows under the timelines.
The official guide divides them into:

- Left palettes: Camera Raw, Color Match, Primaries, HDR, RGB Mixer, Motion
  Effects.
- Central palettes: Curves, ColorSlice, Color Warper, Qualifier, Window,
  Tracker, Magic Mask, Blur, Key, Sizing, 3D.
- Right region: Keyframes, Scopes, Info.

The button row selects which tool occupies a known palette region. The active
palette's name appears at the upper left of the region, and hovering a button
shows its name.

This is extremely close to the user's icon-row idea. Resolve proves that icons
can navigate a professional toolset if:

- their spatial order is stable;
- they switch a clearly bounded content region;
- the active palette is named;
- every icon has a hover label; and
- related tools occupy the same tier.

Resolve does not make every parameter an icon. The icon row selects a palette;
the palette then uses the interaction best suited to the task: wheels, curves,
bars, point graphs, toggles, or on-image controls.

## Simultaneous Context, Not One Giant Inspector

Resolve can show a broad primary tool and a precision tool at the same time.
The default layout puts Primaries and Curves side by side, with Scopes available
on the right. This is important for Stack: it may be better to keep RAW
Exposure immediately reachable while a Local or Finish graph is active than to
make every family mutually exclusive.

The useful abstraction is not “one palette at a time across the whole app.” It
is “one active palette per fixed region.” Stack could have:

- a narrow persistent primary strip for RAW Exposure and WB;
- one large active graph/tool region for Local, Tone, or View;
- an optional measurement region for histogram/waveform/clipping.

## Viewer as an Interaction Surface

Resolve's Viewer includes:

- temporary grade bypass;
- highlight/matte inspection;
- image wipe;
- split-screen comparison;
- an onscreen-control menu whose content follows relevant palettes/effects;
- navigation and transport controls.

This mirrors Adobe's behavior: preview modes and tool-specific overlays remain
attached to the image. For Stack, Local targeting, Affected/Delta/Mask, neutral
comparison, clipping, and before/after should be preview states rather than
separate inspector sections.

## Graph and Scope Treatment

Resolve's Curves are a dedicated central palette rather than a small widget
inside a long settings form. The custom curve:

- can adjust luminance and RGB channels independently;
- displays a live histogram behind the curve;
- uses a compact control strip to switch curve type/channel;
- can select image values directly for targeted editing.

Professional scopes occupy a dedicated bottom-right palette and can switch
among waveform, parade, vectorscope, histogram, and chromaticity. The important
principle is that measurement is a peer to Keyframes and Info, not explanatory
text inserted between sliders.

Stack does not need every Resolve scope. It can borrow the spatial contract:
an optional persistent measurement tile whose mode changes through a tiny
icon/text row.

## RAW Controls in Resolve

Resolve places camera decoding controls in a dedicated Camera Raw palette,
selected from the left palette row. Blackmagic describes this palette as
pre-node processing with exposure, white balance, gamma, highlight recovery,
temperature, tint, sharpness, and related camera controls.

The useful Stack translation is to give technical input interpretation and
camera-stage choices a dedicated Setup/Input context. Demosaic and processing
version should not consume permanent space beside creative tone tools merely
because they occur early in the pipeline.

## Low-Chrome Lessons

- Palette selectors are compact icon rows, not boxed tabs with long names.
- The selected palette name appears once in the content region.
- Tooltips provide icon names on hover.
- The image, node graph, curves, and scopes carry most of the visual structure;
  excessive group boxes are unnecessary.
- A small number of persistent region boundaries can be justified by strong
  functional differences, but adding frames inside each region would create
  needless nesting.
- Optional regions disappear to increase the size of what remains.

## Resolve Patterns Worth Borrowing

1. Fixed semantic regions with application-managed resizing/reflow.
2. Palette icon rows as navigation into dense tools.
3. One active palette per region, not one global long inspector.
4. Persistent primary adjustment access alongside a precision graph.
5. Scopes as an optional, dedicated measurement surface.
6. On-image interaction whose controls follow the selected palette.
7. Responsive merging of palette groups at lower width.
8. Hide/show optional regions instead of draggable window rearrangement.

## Resolve Patterns to Reject or Simplify

- Stack does not need Resolve's always-visible node graph in the RAW tab; Stack
  already has an Editor tab for graph-level decomposition.
- A still-photo RAW workspace does not need the thumbnail timeline,
  mini-timeline, transport, keyframe editor, or motion tools.
- Resolve's density is appropriate for video grading but too high for Stack's
  requested minimalism.
- Multiple simultaneous button rows would be too much for Stack's current four
  core tools. Start with one primary rail and, only when needed, a small
  graph-specific mode row.
- A fixed region should not automatically receive a border. Background
  continuity, spacing, and alignment can establish regions more quietly.

## Initial Stack Translation

The most relevant Resolve-inspired shell is:

```text
top:    quiet global action strip
center: large preview
right:  tool rail + active tool region
bottom/right optional: compact scopes
```

Within the active tool region, a Local/Tone/View graph should be large and
stable. RAW Exposure can remain a persistent floating control near the preview
or at the top of the tool region, reflecting Resolve's simultaneous Primaries
plus Curves arrangement.

## Official Visual References

- [DaVinci Resolve Color product page](https://www.blackmagicdesign.com/products/davinciresolve/color)
- [The Colorist Guide to DaVinci Resolve 20 (PDF)](https://documents.blackmagicdesign.com/UserManuals/DaVinci-Resolve-20-Colorist-Guide.pdf)
- [Official Resolve training hub](https://www.blackmagicdesign.com/products/davinciresolve/training)
- [Resolve panels and palette-navigation explanation](https://www.blackmagicdesign.com/products/davinciresolve/panels)
