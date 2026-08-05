# Local Exposure Direct Targeting Implementation

Last updated: July 23, 2026.

## Outcome

RAW Lab's Zones tool now supports independent, image-targeted Local Exposure
zones. A target is no longer converted into another point on the shared base
curve, so selecting a new image area cannot silently move or reset an existing
curve point.

The original Local Range curve remains intact. Target zones are additional
recipe-backed corrections evaluated in the same Local Range stage.

## Image Interaction

Entering Target mode temporarily switches to a dedicated outline overlay and
restores the user's previous overlay when Target mode ends. The filled cyan
Region Mask remains available as an explicit diagnostic preview, but is never
enabled automatically during on-image editing.

Over the displayed image Stack now draws a borderless vector cursor instead of
the OS pointer:

- drag upward or downward to raise or lower exposure after an eight-pixel
  dead zone, at 120 display pixels per EV;
- release and drag the same target again to continue from its authored EV
  value rather than from zero;
- use Ctrl-drag to create another independent target after the first;
- use the wheel to widen or narrow tonal reach;
- use Shift-wheel to change tonal feather;
- use Ctrl-wheel to change color reach when color matching is enabled;
- Shift-click to add another selected area to the active target;
- Alt-click to remove the nearest selected-area seed; and
- right-click for scope, color matching, overlap policy, reset, and delete.

Hover sampling is coalesced to at most 20 requests per second and requires
three pixels of pointer motion. The cursor readout reports sampled scene EV,
the active target's EV correction, and whether the next drag will refine an
authored zone or needs Ctrl to create a new one. Clicking or releasing inside
the dead zone selects only UI state and does not create a project, dirty the
recipe, or schedule a settled render.

The outline is transparent everywhere except its contour pixels. Motion shows
a lower-opacity provisional qualifier contour. After 120 ms of pointer dwell,
Stack replaces it with strong approximately 90-percent and soft approximately
10-percent contours from the connected selection, exposing core and feather
reach without covering the photograph.

Right-clicking empty image space may create a temporary zero-EV zone so the
context menu has something concrete to edit. Closing that menu without
changing the zone removes the temporary recipe entry.

## Recipe Contract

RAW recipe schema 8 adds a bounded list of `RawLocalRangeTargetZone` records.
Each record stores:

- stable ID and display name;
- enabled state;
- center scene EV;
- core half-width and feather in EV;
- exposure delta in EV;
- Selected Areas or All Matches scope;
- optional CIE 1976 u-prime/v-prime color target, radius, and feather; and
- normalized image-space seeds for connected selected areas.

Recipes are limited to 32 zones and 32 seeds per zone. Loading sanitizes
non-finite values, bounds numerical fields, repairs duplicate/missing IDs, and
caps oversized lists. Schema-7 and older recipes load with no target zones and
retain their previous Local Range result.

The Reset Local Range preset preserves independent target zones. This avoids a
base-curve reset unexpectedly deleting authored regional work.

## Tonal and Color Math

Each target starts with a scene-EV qualification:

```text
weight = 1 inside the core
weight = smoothstep falloff through the feather
weight = 0 outside core + feather
```

Optional color matching converts scene-linear working RGB to XYZ and then CIE
1976 u-prime/v-prime. Stack uses the correct D65 matrices for linear sRGB and
linear Rec. 2020. A chroma guard keeps near-neutral targets from accepting
saturated colors and prevents chromatic targets from spilling into unstable
near-neutral hue.

The final per-zone weight is tonal weight times color weight times selected
component membership. The target's EV delta and the existing Local Range
strength are applied after qualification. Existing highlight protection still
attenuates positive corrections near the top of the authored scene-EV range.

Overlapping target zones support:

- Add: sum weighted corrections, clamped to the Local Range EV limit;
- Strongest: use the weighted correction with greatest absolute magnitude; or
- Blend: divide the weighted sum by at least the accumulated weight.

CPU reference helpers and GLSL use the same constants and equations.

## Selected-Area Mask

Selected Areas does not mean "every pixel with the same brightness." Stack:

1. renders the target's tonal and optional color qualifier;
2. starts from each normalized image seed;
3. re-anchors locally if the exact seed is not a strong qualified pixel;
4. grows an eight-connected component through qualified neighbors; and
5. stores membership as one bit per zone in a single `R32UI` texture.

All Matches bypasses connected-component membership and applies to every
qualified pixel.

Connected-component work is capped at a 1536-pixel maximum dimension during a
full-resolution settle. The integer mask is sampled with normalized
coordinates by the full-size Local Range shader. Changing only a target's EV
delta reuses the mask; changing tonal/color reach, seeds, working space, input
stage, or dimensions invalidates it.

## Graph and Original RAW Behavior

The RAW Lab Local Exposure graph draws target zones as translucent tonal lobes
over the pre-existing base curve. Each target has a selectable center/delta
handle, and the small numbered chips switch the active target. Dragging a lobe
handle changes its center scene EV and exposure delta through the shared recipe
edit path.

The original RAW presentation is not given a second target-zone editor. It
shows a read-only count when schema-8 target zones are present and preserves
them through all legacy controls.

## Render and Ownership Behavior

Ordinary RAW workspace image delivery now publishes a shared GPU texture from
the render owner. Local Range overlays use a second shared texture and fence.
The UI adopts both textures after the fences signal; it no longer downloads
and re-uploads a full RGBA frame merely to display a hover or drag result.

Target and graph drags continue to use interactive preview resolution and
latest-wins render generations. The settled full-quality render is requested
only when the active interaction ends. A hover-only target sample is now an
auxiliary proxy request: it does not enter edit-preview mode, schedule a
settled render, replace the visible presentation texture, or replace the
accepted mask overlay.

The authored-zone connected-component qualifier still uses its bounded cached
selection texture. Prospective hover refinement is separate: it renders an
`R8` qualifier at no more than 768 pixels on the longest side, issues the
download through a two-PBO ring plus GL fence, and reads a buffer only on a
later render tick. Pointer generations are latest-wins, so stale refinement
results cannot replace the accepted outline. The provisional contour remains
visible while refinement is pending.

## July 23 Font-Atlas Regression and Fix

The first native RAW Lab test exposed rapid flashing between the image and
large upside-down text glyphs while Target was active. The glyphs were ImGui's
font atlas, not corrupted image pixels or failed Local Range shader math.

Ordinary RAW workspace renders currently execute synchronously on the main GL
context. That path rendered a new Local Range overlay texture but did not take
ownership of it from `RenderPipeline` and publish it into the UI-owned RAW
overlay state. The asynchronous worker-result path already performed the
equivalent adoption. The incomplete synchronous path allowed a stale texture
name to remain in the preview state; after OpenGL reused that name for the font
atlas, the image widget sampled the atlas as though it were the mask.

The synchronous RAW branch now:

1. takes the rendered overlay texture and dimensions from `RenderPipeline`;
2. clears and releases the previously accepted UI overlay;
3. accepts the new texture only when its dimensions and requested overlay mode
   still match the current RAW interaction;
4. tags it with the active source and render generation; and
5. queues every rejected or superseded texture for release.

The one-byte qualifier readback also temporarily sets
`GL_PACK_ALIGNMENT` to `1` and restores the previous value afterward. This is
separate hardening for selection widths that are not divisible by four and
prevents row padding from writing beyond the CPU qualifier buffer.

## July 23 Background-Color Flash Root Cause and Fix

After the font-atlas symptom was removed, user retesting showed that Target
hover still alternated between the photograph and the RAW Lab viewport
background. Sampling the supplied screenshot established that every pixel in
the failed image rectangle was exactly Stack's teal window-clear RGB value,
not black and not a faded photograph.

The remaining failure had four cooperating causes:

1. graph render targets inherited AppShell's opaque teal `glClearColor`;
2. a skipped or failed fullscreen overlay/image pass was therefore still a
   valid-looking, opaque teal texture;
3. shared-output publication accepted its blit without explicit read/draw
   buffers, framebuffer validation, or a GL error check; and
4. every observational hover sample needlessly published a replacement base
   image and alternated proxy/full preview scheduling.

The corrected contract is:

- every graph target initializes to transparent black with scissor disabled
  and all color channels writable;
- an incomplete framebuffer or GL draw error fails the pass and deletes its
  target instead of caching/publishing the clear texture;
- shared-output publication uses complete FBOs, explicit
  `GL_COLOR_ATTACHMENT0` read/draw buffers, and rejects failed blits;
- pass-through Output nodes no longer persist a borrowed OpenGL texture name
  that can dangle after an upstream RAW cache replacement;
- target sample and overlay side effects explicitly execute RAW Development
  even when visible output pixels are otherwise cacheable; and
- hover-only sampling preserves the last accepted base image and overlay.

This is the first fix in the sequence that addresses the exact teal pixels in
the reported blank frame. The earlier alpha and font-atlas guards remain
useful ownership hardening, but they were not sufficient to fix this symptom.

## Verification

Passed on July 23, 2026:

```text
.\build.cmd
cmake --build build --config Release --target Stack StackGraphBehaviorTests --parallel 4
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-develop-node-smoke
.\build\Stack.exe --validate-develop-real-raw-smoke C:\Users\djhbi\Downloads\Tennis\IMG_260608_203532.dng
.\build\Stack.exe --validate-develop-real-raw-smoke C:\Users\djhbi\Downloads\Tennis\IMG_260608_205149.dng
.\build\Stack.exe --validate-develop-real-raw-smoke C:\Users\djhbi\Downloads\Tennis\IMG_260608_204838.dng
```

The behavior suite covers:

- schema-8 JSON round trip;
- schema-7 empty-zone migration;
- Local Range fingerprint changes from zone edits;
- tonal core and feather behavior;
- working-space RGB-to-u-prime/v-prime color matching;
- neutral/chromatic rejection behavior; and
- Add, Strongest, and Blend overlap rules.

The hidden real-RAW OpenGL validation now authors a selected-area target zone,
which compiles and executes the main Local Range shader, qualifier shader,
connected-component mask, and stage-cache path. Its hover regression requests
sample A, sample B, and sample A again without changing the recipe. It requires
all three returned UVs to be current, every overlay and published texture to
be valid and nonblank, and all visible output pixels to remain identical.

The previous automated native interaction pass was not sufficient evidence:
user retesting still reproduced the background-color flash. The deterministic
real-DNG regression now covers the renderer and cache failure, but the rebuilt
application still requires user confirmation of the actual pointer interaction
before the native symptom can be marked closed.

For `IMG_260608_203532.dng`, the measured validation trace was:

```text
warm:       1486.76 ms
analyzed:    149.86 ms
interactive:   6.59 ms
downstream:    5.91 ms
upstream:     65.89 ms
analysis-on/off pixels identical: yes
target samples A/B/A current: yes
target hover output pixels stable: yes
target overlays and published textures valid: yes
```

These are evidence from one machine and image, not universal pass thresholds.
The upstream case intentionally invalidates more RAW work and the selection
mask.

## July 23 Outline And Refinement Pass

The first filled-mask interaction made a valid selection look like a cyan
color cast because the diagnostic shader applied 18-percent dark teal even to
unqualified pixels and up to 66-percent cyan to qualified pixels. Target mode
now uses `target-outline`; the explicit `region-mask` preview is unchanged.

Authored-zone reuse now comes from the rendered 32-bit connected-membership
texture with twelve logical pixels of hit slop. It no longer depends primarily
on being within five percent of the original seed. The strongest qualified
zone wins, with a 0.05 active-zone hysteresis. A normal drag over empty space
does not create a surprise zone; Ctrl-drag or the `+` action explicitly arms
creation.

The real-DNG validation for `IMG_260608_204838.dng` exercised the transient
outline request, its two-PBO/fence readback, connected-component refinement,
and sparse overlay publication:

```text
refined:              yes
proxy longest side:   768 pixels
visible contour:      7,734 pixels
transparent overlay: 778,698 pixels
qualifier issue:        1.94 ms
readback copy:          0.05 ms
connected growth:       7.71 ms
texture upload:         0.31 ms
accepted image stable: yes
```

The qualifier issue measured 1.94 ms in the final worker-backed run. Connected
growth begins only after the GPU fence and runs on a CPU worker rather than
blocking the render-owner tick. These numbers are one diagnostic sample, not
portable performance thresholds.

## Remaining Boundaries

- Connected masks are topology selections at bounded resolution; they do not
  yet include semantic subject recognition or learned edge refinement.
- The current scope is scene brightness plus optional chromatic qualification.
  Texture, depth, object identity, and motion are not target dimensions.
- A native interaction pass is still required for subjective cursor feel,
  contour density, wheel sensitivity, seed removal, menu placement, and
  repeated click-drag behavior at multiple display scales.

## July 24 RAW Lab Integration Correction

Native RAW Lab testing found that valid outline textures were not being drawn
and existing zones could not be refined through the synchronous RAW render
path.

The outline was incorrectly required to share the base viewport's render
generation. Hover renders intentionally preserve that base photograph, so the
auxiliary outline necessarily has a newer render generation. Target outlines
now validate against their transient target-preview generation; ordinary
Affected, Delta, and Mask overlays continue validating against the base
viewport generation.

The synchronous target-sample adoption path also copied scene EV and RGB but
dropped the authored-zone hit bits and strongest hit weight. It now carries
the same complete sample contract as the background worker path. The real-DNG
regression samples the authored zone seed and requires hit bit `0x1` plus a
positive effective weight.

The next native screenshots exposed two additional presentation problems. A
continuous tonal isoband could become broad or fragment around RAW noise until
thousands of amber or cyan fragments resembled a filled mask. The two colors
were provisional/refined states, but that distinction was not useful because
both states obscured the photograph and flashed whenever hover generations
changed.

Target presentation now has a simpler contract:

- pointer motion is represented only by the circular cursor and its numeric
  readout; no moving pixel mask is rendered;
- after 120 ms of dwell, the GPU qualifier is read back asynchronously and
  connected growth isolates only the component under the pointer;
- the transient proxy qualifier receives a five-by-five box stabilization and
  the resulting membership receives one majority cleanup pass; these affect
  only the UI outline, never the authored recipe or final exposure math;
- the shader compares neighboring binary membership samples and draws only a
  cyan component boundary, not tonal isobands or selected interiors;
- the settled outline is cleared on mouse-down so active dragging leaves the
  photograph unobstructed; and
- the real-DNG overlay must remain at least 95 percent transparent and at
  least 80 percent of its visible pixels must touch another visible contour
  pixel, rejecting a salt-and-pepper field.

The same screenshot showed `+1.77 EV` after a normal drag whose pointer was
outside every authored mask. That drag was correctly a no-op under the locked
Ctrl-drag creation rule, but its HUD was falsely reporting physical mouse
distance as an accepted adjustment. Non-editable drags now say
`No zone here - Ctrl-drag to create`, omit the correction readout, and reset
their transient distance without changing the recipe. Fast drags that begin
before their click sample arrives retain their distance until sampling decides
whether they refine, create, or remain a no-op.

The same render scheduling path had classified every `target-outline` request
as observational hover work. That was correct while only the pointer moved,
but incorrect after a drag changed a zone EV: the graph/recipe could update
while presentation logic continued preserving the pre-drag photograph.
`RawLocalRangeTargetPreviewRequest::interactionEditing` now distinguishes
those cases. Hover sampling and settled outline work preserve the accepted
photo; an active recipe edit publishes the new interactive photo. A pure
scheduling helper and graph-behavior regression lock that distinction.

The July 24 real-DNG A/B check renders the same authored connected area at
`+0.20 EV` and `+1.00 EV`. For `IMG_260608_204838.dng`, changing only that
zone changed 2,342,565 output bytes. The refined boundary check measured:

```text
refined:                       yes
proxy longest side:            768 pixels
visible contour:            38,978 pixels
visible pixels with neighbor: 38,978 pixels
transparent overlay:        747,454 pixels
qualifier issue:                3.00 ms
readback copy:                  0.05 ms
connected growth/cleanup:       9.11 ms
texture upload:                 0.77 ms
```

`StackGraphBehaviorTests`, Develop node smoke validation, and the real-DNG
smoke validation passed against an alternate validation executable. The
ordinary `build\Stack.exe` link was not replaced during this pass because the
user's currently running Stack process held it open.
