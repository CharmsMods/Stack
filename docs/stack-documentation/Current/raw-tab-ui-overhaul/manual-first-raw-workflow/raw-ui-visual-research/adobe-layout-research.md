# Adobe Layout Research

Status: first official-source pass complete.

## Products Considered Separately

Adobe does not have one single RAW-editor shell:

- Lightroom Classic uses a module workspace with left and right panel stacks.
- Current Lightroom desktop uses a simpler Detail view, a central image, a
  right-side tool rail, and a contextual right panel.
- Camera Raw is a focused dialog with a large left preview, right editing
  surface, optional filmstrip, and a compact header.

They share processing concepts, but their information architecture differs.
For Stack's visual direction, current Lightroom and Camera Raw are more useful
than Lightroom Classic's long accordion stack.

## Lightroom Classic: Useful Structure, Wrong Control Container

Adobe describes the Develop module as:

```text
left: navigation/presets/history
center: image work area
right: histogram, tool strip, adjustment panels
bottom: filmstrip and image-view toolbar
```

The separation of selection/history, image, and editing is clear. The image
remains the center of gravity. The right side combines a persistent measurement
surface (histogram), a compact tool strip for mode-like operations, and the
current adjustment panels.

However, Lightroom Classic's Develop controls are explicitly a reorderable,
hideable stack of panels. This is the accordion/long-scroll pattern Stack is
trying to escape. It is evidence that a broad photo toolset needs hierarchy,
but not evidence that Stack should repeat Adobe's container.

Useful details:

- the histogram remains close to adjustments and exposes clipping;
- tool-strip actions such as crop, remove, and masking are separated from
  continuous tone/color parameters;
- before/after, reference, and zoom are image-view modes in a toolbar rather
  than editing sections;
- every adjustment panel has an eye indicator for temporary effect bypass;
- used tools receive a small dot in the tool strip;
- panels can be hidden so the application does not assume every user wants
  every tool present all the time.

## Current Lightroom Desktop: Tool Rail Then Context

Current Lightroom enters editing from a single-image Detail view. The Edit
panel is opened from an icon at the upper right; Masking and other operations
are selected from the right-hand toolbar. The filmstrip remains a separate
image-navigation layer at the bottom.

This creates two levels without a movable window:

1. a narrow icon rail chooses the editing context;
2. one contextual panel shows the controls for that context.

That pattern is directly relevant to Stack. An icon can choose `Light`,
`Local`, `Curve`, or `View`; the adjacent tool surface can then devote its full
area to that tool instead of keeping every family vertically expanded.

Adobe's masking workspace also demonstrates a temporary context shift:
choosing Masking changes the right panel to mask creation/list/refinement, while
the image becomes the direct-manipulation surface through overlays. The tool
rail remains the stable way back to global editing.

## Camera Raw: The Most Comparable Adobe Shell

Adobe's current Camera Raw overview shows:

- a compact header for save/settings/presentation/full-screen actions;
- the selected image occupying the large left preview;
- Edit panels on the right;
- a right-hand tool model for special tasks such as masking;
- a bottom filmstrip that can be hidden or moved vertically;
- before/after in the preview area;
- zoom/hand near the preview rather than mixed into tone controls.

Inside Edit, Adobe keeps the main adjustment families recognizable: Basic,
Curve, Detail, Color Mixer, Color Grading, Optics, Geometry, Effects, and
Calibration. The Basic panel is the normal starting place; Curve is a
fine-tuning context after broad tone work. The Curve surface switches between
parametric, point, and RGB-channel interpretations.

Camera Raw still uses collapsible panels. Stack should borrow the stable
preview/tool relationship and contextual tool switching, not the accordion
implementation.

## Adobe Patterns Worth Borrowing

### 1. The image owns the workspace

Panels flank the image; they do not become equal window peers. Image navigation,
zoom, comparison, and overlay controls stay attached to the preview context.

### 2. Separate modes from parameters

Crop, remove, mask, edit, and view/compare are modes selected by compact tools.
Exposure and curve points are parameters displayed after a mode is selected.
Stack's icon row should select a workspace, not turn every numeric property
into an unexplained icon.

### 3. Keep measurement persistent

The histogram and clipping indicators remain visible while users switch among
adjustment families. Stack can apply this to a compact histogram/EV distribution
or clipping strip that stays present while Local, Tone, and View tools change.

### 4. Show state without boxes

Adobe's edit dots and eye indicators are low-chrome answers to two important
questions: “Has this tool changed the image?” and “What happens if I bypass
it?” Stack can use a small accent dot, changed-value tint, or press-and-hold
bypass on each tool icon.

### 5. Treat target-on-image operations as temporary workspaces

White-balance sampling, masking, crop, and targeted adjustment change what
dragging on the image means. Their controls follow the active tool rather than
remaining permanently mixed into global sliders.

### 6. Keep comparison close to the image

Before/after and reference views change the preview layout, not the adjustment
hierarchy. Stack's current Final/Compare/Affected/Delta/Mask concepts should be
reconsidered as a compact preview-state strip or momentary actions.

## Adobe Patterns to Reject or Adapt

- Do not copy Lightroom Classic's full-height panel accordion.
- Do not put explanatory paragraphs into the everyday adjustment path.
- Do not turn every low-frequency technical choice into a persistent panel.
- Do not copy unlabeled icons without strong tooltips, active treatment, and a
  discoverable relationship to the panel they control.
- Do not combine tool-family navigation and channel/domain switching in the
  same visual tier; one changes the workspace, the other changes the active
  graph.

## Initial Stack Translation

An Adobe-influenced Stack shell could use:

```text
large preview | narrow tool rail | one contextual tool surface
```

The tool rail might contain:

- Light — RAW Exposure and essential placement;
- Local — Local Exposure graph and target mode;
- Tone — Finish Tone graph;
- View — Display Transform;
- Inspect — clipping, histogram/scopes, metadata;
- Setup — WB, demosaic, processing/profile details.

The active state, non-default state, and bypass state can be visible on the icon
itself. This preserves Adobe's discoverability while avoiding its long panel
stack.

## Official Visual References

- [Adobe Camera Raw overview and workspace](https://helpx.adobe.com/camera-raw/using/introduction-camera-raw.html)
- [Lightroom Classic Develop module tools and layout](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/develop-module-tools.html)
- [Lightroom desktop editing surface](https://helpx.adobe.com/lightroom-cc/using/edit-photos.html)
- [Lightroom desktop masking workspace](https://helpx.adobe.com/lightroom/desktop/edit-photos/masking.html)
- [Camera Raw masking workspace](https://helpx.adobe.com/camera-raw/using/masking.html)
- [Camera Raw tonal and curve controls](https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html)
