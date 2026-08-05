# Manual-First RAW Workflow Progress

Last updated: August 4, 2026.

Keep this file short. It owns the active pass, verification, next allowed work,
and stop rules.

## Current State

```text
Phase: 04 - Local Exposure direct targeting
Status: schema, CPU/GPU math, image gestures, connected masks, shared overlay delivery, and automated GL verification complete; native interaction tuning remains
Product direction: manual-first RAW editing
Automatic solver status: preserved backend, retired product surface
```

## Phase 01 Active Slice

Implement a versioned `truthful-v1` path without changing the rendering of
existing recipes:

1. establish CPU reference math and focused tests;
2. make decode/normalization metadata and omissions explicit;
3. replace bilinear-only final quality with a named higher-quality Bayer path;
4. make preview proxies CFA-aware and visibly identify proxy quality;
5. connect manual white-balance controls to canonical render multipliers;
6. separate scene/view rendering from output transfer encoding; and
7. introduce the dockable manual workspace shell only after the pixel gates
   above pass.

New recipes may opt into `truthful-v1` only after its focused validation passes.
Recipes written before this phase remain on `legacy-v1`.

## Phase 00 Scope

Archive these live UI entry points without deleting historical backend code:

- Develop node Auto mode, Return to Auto, and Recalibrate Auto
- new Develop nodes defaulting to Auto and applying an automatic solve
- RAW workspace Build Starting Point / Precise / Fast panel
- automatic suggestion popout and apply actions
- Analyze/Fit Display/Refit Display automatic write actions
- automatic per-control ownership/readout markers
- automatic candidate, suggestion, and undo diagnostics
- selectable Auto White Balance in the active manual editing UI

Preserve:

- existing authored recipe values
- loading compatibility for old project data
- non-mutating technical RAW diagnostics and current-frame statistics
- Local Range and Finish Tone manual graphs
- archived solver source and focused regression tests

## Completed Behavior

- Root navigation is an experimental centered floating island. It retracts on
  editing surfaces, remains visible in Library, and preserves the existing
  project-session tab transitions. RAW and RAW Lab keep their command rows
  below native caption controls; RAW Lab uses an 18-pixel outer-left inset.
- The floating island uses icon-only active/hover feedback and a broad,
  low-density Gaussian-falloff shadow rather than local rounded button fills.
- Permanently visible text-only `File` and `Settings` controls now live at the
  top-left independently of the retracting island. Settings reuses the existing
  centered window; the island gear and its native-input bridge were removed.
- The File menu now owns New/Open/Save/Save As/Close/Exit and their shortcuts
  through one state-aware dispatcher. Save As adopts an absolute destination,
  embeds linked single-RAW originals, preserves MFD storage kind, and leaves
  storage conversion as a separate command.
- Ordinary project Save/Save As duplicates were removed from RAW Lab, MFD, and
  graph menus. Externally opened Editor projects save back to their absolute
  path without publishing managed-Library sidecars, and graph-only projects
  can be persisted before they have a rendered output.
- RAW Lab is the only visible RAW root-navigation icon. A double right-click
  on it opens the retained legacy RAW presentation; all ordinary RAW routing
  opens RAW Lab. The former Tools root tab and its dedicated module were
  removed rather than retained as a hidden surface.
- New Develop nodes default to Manual and do not run an initial automatic solve.
- Serialized legacy Auto Develop nodes load into Manual with their authored
  settings preserved.
- Automatic RAW workspace panels, suggestions, write actions, ownership
  readouts, advisories, and candidate diagnostics are no longer rendered.
- Pending Starting Point continuation is canceled instead of applying edits.
- Auto WB is unavailable for new manual edits; legacy recipes retain a labeled
  migration path.
- Technical RAW, Highlight Signals, Current Frame Stats, Local Range, Finish
  Tone, and explicit View Transform controls remain available.
- Recipe schema 7 stores an explicit processing version, demosaic algorithm,
  working space, DNG baseline-exposure policy, and output-transfer policy.
- Pre-schema-7 recipes retain Legacy V1 behavior; new recipes default to
  Truthful V1.
- Truthful V1 applies DNG linearization before spatial/repeating black
  subtraction and per-plane white normalization, then supported OpcodeList2
  gain maps in ActiveArea coordinates with pixel-center interpolation and
  per-opcode clipping.
- Truthful V1 applies camera-plane white-balance multipliers before demosaic,
  uses a named Malvar-He-Cutler 5x5 Bayer path, and disables unvalidated
  reconstructive cleanup by default.
- The MHC measured center sample and its interpolated neighbors now share the
  same pre-demosaic white-balance domain. A GPU readback regression rejects
  repeating RGB differences between the four CFA phases.
- The camera matrix path targets linear Rec. 2020 / D65 with explicit
  chromatic adaptation and iterative dual-illuminant interpolation.
- The View Transform converts to display-mapped sRGB primaries and may apply
  the exact sRGB transfer. Graph descriptors carry the actual linear/encoded
  output state.
- Unsafe CFA preview decimation was replaced with same-plane averaging and is
  declined when nonlinear/spatial metadata or a larger repeating black pattern
  cannot be preserved. Proxy crops now use the declared DNG ActiveArea.
- RAW Controls, Image, and Tone Graphs are dockable windows. Local Range and
  Finish Tone no longer occupy the long control scroll.
- Unsupported gray-point coordinates, temperature/tint implications, Display
  P3 output, and monitor color management are no longer presented as if they
  work.
- Unsupported DNG opcode counts and decoder warnings are visible in the RAW
  source and workspace diagnostics instead of being silently omitted.
- Active slider/graph renders use a display-sized CFA-aware proxy and skip
  observational RAW readbacks/automatic analysis until the settled render.
- The recipe-backed RAW node caches exact RAW-placement and neutral-placement
  RGBA16F intermediates using upstream-only fingerprints.
- Ordinary manual renders no longer run archived automatic recommendations,
  dry-run diagnostics, or hidden candidate-render loops.
- The Graph Performance overlay identifies interactive/settled RAW mode,
  proxy size, analysis capture, graph time, and post-render time.
- Real-RAW validation proves analysis-on/off floating-point output identity,
  downstream stage-cache reuse, and upstream RAW Exposure invalidation.
- A separate top-level `RAW Lab` presentation shares the existing RAW session,
  selected source, recipe, preview textures, render caches, dirty state, and
  save path. Switching between `RAW` and `RAW Lab` no longer leaves and
  re-enters the RAW workspace.
- RAW Lab uses a vertically resizable borderless editing rail to the left of a
  large image preview. The original docked RAW presentation remains unchanged
  as the comparison baseline.
- A wrapping, content-sized text island switches CFA Denoise, RGB Denoise,
  Exposure, Zones, Curve, and View. The
  active surface reuses the shared recipe/edit path rather than copying the
  original Controls or Tone Graphs panels.
- Recipe schema 9 stores the experimental pre-demosaic Fast Denoise settings.
  RAW Lab exposes DNG NoiseProfile and Fixed comparison modes before Exposure,
  while new and schema-8 recipes retain disabled, pixel-identical defaults.
- Denoise mode now participates in RAW placement cache identity, so switching
  DNG versus Fixed cannot incorrectly display a cached result from the other
  mode.
- Recipe schema 10 stores a separate post-demosaic RGB Denoise contract.
  It defaults off, uses the stable `classical-multiscale-v1` method, and
  preserves separate Color Noise, Luminance Noise, and Detail Protection
  controls.
- The enabled RGB path now splits neutral RAW conversion, RGB denoise, and
  authored RAW Exposure. Neutral-base, denoised-base, and exposure-placement
  cache identities allow Exposure edits to reuse the heavier multiscale work.
- The disabled RGB path remains on the established RAW GPU render so old and
  migrated recipes do not change merely because schema 10 exists.
- Zones, Curve, and View graphs are capped near 320 logical pixels and use a
  compact 4:3 proportion instead of the vertically compressed full-width
  bottom-workbench geometry.
- Preview overlays moved into one floating menu. A closable/resizable lower
  grading placeholder occupies only the image column and adds no new grading
  math.
- Zones and Finish Tone use direct-manipulation frameless graphs. View draws
  the existing display mapping through a shared CPU scalar evaluator rather
  than introducing new View Transform math.
- The View scalar evaluator is checked against an independent transcription of
  the current shader equation to a `2e-6` floating-point tolerance.
- Gallery has one shared content model with bottom-filmstrip, full-workspace,
  and native pop-out hosts. Grid/List preference, active tool, rail width,
  lower-shelf state/height, filmstrip height, and last Gallery host are
  optional app-state fields; Gallery still starts closed.
- Detached platform-window handling now identifies both Editor Preview and RAW
  Gallery surfaces, so the native Gallery can coexist with the existing
  detached Editor preview mechanism.
- The icon baker accepts the planned optional files under
  `Icons/Raw Tab/Lab/`. Missing assets produce zero-byte embedded fallbacks and
  the prototype continues to show usable text.
- Recipe schema 8 stores independent Local Exposure target zones with scene-EV
  core/feather bounds, exposure delta, Selected Areas or All Matches scope,
  optional CIE u-prime/v-prime color qualification, and normalized area seeds.
- RAW Lab Target mode uses a custom vector cursor. Vertical drag edits EV from
  the zone's existing value; wheel, Shift-wheel, and Ctrl-wheel edit tonal
  reach, tonal feather, and enabled color reach. Shift-click and Alt-click add
  or remove selected-area seeds.
- Target zones are additional Local Range corrections rather than mutations of
  the shared base curve. Their Add, Strongest, and Blend overlap rules have CPU
  reference tests and matching GLSL.
- Selected-area qualification grows eight-connected components from authored
  seeds and packs membership for up to 32 zones into one `R32UI` texture.
  Full-resolution settles bound this topology pass to a 1536-pixel maximum
  dimension, and delta-only edits reuse the mask.
- The Local Exposure graph renders recipe-backed target lobes and handles.
  Original RAW preserves schema-8 zones and reports their count without
  duplicating the Lab editor.
- RAW Lab Zones now separates Overall Tones from Targeted Areas. The target
  view exposes the existing schema-8 fields as a conventional list and
  inspector, keeps image gestures synchronized with the same recipe values,
  and places Final, Affected, Delta, and combined Mask previews beside the
  target controls. This is a UI-only pass with no schema or shader change.
- RAW workspace image and Local Range overlay delivery now use fenced shared
  GPU textures. Ordinary interactive delivery no longer downloads and
  re-uploads the full RGBA viewport frame.
- RAW Lab Zones and Curve graphs now draw a logarithmically scaled tonal or
  RGB histogram behind the authored graph. Zones captures source RGB plus the
  exact edge-aware scene-EV control signal used by Overall Tones in one
  bounded pass; Curve samples immediately before Finish Tone and remaps the
  bins when Scene/Log changes.
- Graph scopes use one bounded 192-pixel settled-render readback. Interactive
  proxy renders skip the readback and retain the last settled histogram until
  the next full-quality result is accepted.
- The Zones graph labels its photographic EV input range and -4/0/+4 EV
  correction axis inside a restrained margin. Its primary histogram choice is
  named Tones because it is the guided tonal map rather than ordinary source
  luminance; RGB remains an optional informational overlay.
- Editor, RAW, and RAW Lab now share one active project session. Normal Editor
  projects remain intact behind disabled RAW surfaces until the title-bar
  switch action saves or discards them; RAW previews/projects use the same
  graph in Editor without `Open in Graph` or tab-transition serialization.
- Deferred project apply validates before reset, snapshots the prior graph and
  shared source for rollback, and restores it on failure. Source switching
  durably saves the current RAW work before candidate staging, while external
  RAW projects remain session-pinned instead of replacing the scanned folder.
- RAW project schema v2 uses `projectKind: raw` and canonical `pipelineData`.
  The old `rawWorkspaceData.downstreamGraph` is read-only fallback data. Exact
  managed projects migrate to an untouched sibling copy; custom and unknown
  modes are refused before the live session is reset.
- Compact RAW Development is a protected project owner. Its built-in View can
  be disabled for scene-linear graph editing; enabled output prevents a second
  transform, while disabled output inherits exactly one View before Output.

## Verification

Passed on July 23, 2026:

```text
.\build.cmd
cmake --build build --config Release --target Stack
.\build\Stack.exe --validate-develop-node-smoke
.\build\Stack.exe --validate-develop-real-raw-smoke <real-dng-path>
.\build\StackGraphBehaviorTests.exe
.\build\StackRawEvidenceTests.exe
.\build\StackNodeMathPhase2Tests.exe
```

The Develop smoke validation compiles and executes the RAW shaders. Focused
reference tests cover processing-version migration, DNG normalization order,
CFA-aware proxy behavior, MHC constant fields across all Bayer layouts,
RAW-space white-balance plane scaling, non-neutral MHC/WB phase stability,
exact sRGB encoding, and declared View Transform output semantics. The live
shader smoke suite also measures four-phase MHC output on a non-unity-WB flat
Bayer fixture.

Passed on August 2, 2026 for the single-session project lifecycle pass:

```text
.\build.cmd
.\build\StackGraphBehaviorTests.exe
.\build\StackNodeMathPhase6Tests.exe
.\build\StackRawEvidenceTests.exe
.\build\Stack.exe --validate-node-math-phase6
.\build\Stack.exe --validate-develop-node-smoke
```

The live graph transaction coverage includes clean and dirty normal-project
RAW locks, one RAW graph across repeated Editor/RAW transitions, protected RAW
ownership, enabled/disabled View singularity through Flip, canonical RAW v2
durable save/reload, and preservation of nested future metadata.

The repository-preferred `.\build.cmd` passed after the final correctness
changes. Native 1920x1080 smoke passes verified the RAW Lab command strip,
borderless preview, all four tool switches, compact Zones/Curve graphs,
floating Preview menu, open/closed lower shelf, filmstrip host, full-workspace
Gallery, and native Gallery creation/closure while the main editor remained
usable.

The native passes did not alter recipe values. The final pass restored Exposure
as the active tool and closed the lower shelf before shutdown.

The Phase 02 implementation and research record is
`interactive-preview-performance-pass.md`. Its real-RAW validation reports
timings as evidence without imposing hardware-specific pass thresholds.

The Phase 03 implementation record is
`raw-lab-left-rail-implementation.md`.

The Phase 04 implementation and verification record is
`local-exposure-direct-targeting-implementation.md`.

The July 24 experimental Denoise surface passed the graph-behavior suite,
Develop OpenGL smoke validation, and the real-DNG RAW workspace smoke. Focused
coverage includes schema-9 round trip and clamping, schema-8 disabled
migration, RAW Lab active-tool persistence, and managed-graph denoise
round-trip.

The July 24 RGB Denoise foundation also passed schema-10 graph behavior tests,
the Release build, and a live real-DNG A/B/A render on
`IMG_260608_204838.dng`. The trace proved finite nonblank output, a visible
denoise result, deterministic repetition, visible authored-exposure changes,
and denoised-base cache reuse. See the detailed method and open corpus gates in
`../../engineering/denoise-rewrite/classical-post-demosaic-foundation.md`.

The July 31 first Zones usability pass passed the Release `Stack` build,
`StackGraphBehaviorTests`, `--validate-develop-node-smoke`, and the real-DNG
smoke for `IMG_260608_204838.dng`. The real-DNG run covered target edit
publication, repeat hover sampling, authored-zone hit detection, connected
outline refinement, overlay validity, stable published output, and interactive
cache behavior. Native pointer feel and compact-window review remain open.

The July 31 graph-scope pass passed the Release build,
`StackGraphBehaviorTests`, `--validate-develop-node-smoke`, and the real-DNG
smoke for `IMG_260608_204838.dng`. The real-DNG contract captured finite
48-by-64 Local Range input and Finish Tone input scopes from the requested
scene-linear boundaries, and proved that the analysis-free interactive pass
produced no scope readback.

The August 1 Zones accuracy follow-up keeps that settled-only bound while
packing the renderer's edge-aware scene-EV map beside source RGB. The
real-RAW scope contract now requires the guided signal's domain, dimensions,
and finite samples independently from the Finish Tone RGB scope. The focused
Release build, `StackGraphBehaviorTests`, `--validate-develop-node-smoke`, and
the real-DNG smoke for `IMG_260608_204838.dng` passed. The real-DNG trace
reported `local=48x64 guided=1`, `interactiveSkipped=1`, and `passed=1`.

### August 1 RAW Lab Performance And Lifecycle Follow-Up

- Exact, dependency-aware RAW stage caches now retain the post-Local Exposure,
  post-Local Range, and post-Finish Tone RGBA16F boundaries. Together with the
  neutral/placement caches, Exposure, Zones, Curve, and View edits reuse only
  upstream work that their recipe dependencies prove unchanged; settled image
  math and output quality are not approximated.
- Tone Curve and View Transform keep per-node GL layer runtimes instead of
  compiling fresh programs on every RAW render. Each invocation reapplies a
  fresh authored state, and deserialization/reset still tears the runtime down,
  so reuse cannot leak recipe state between renders.
- Suspended Editor and RAW Lab graph snapshots share immutable source-pixel
  storage rather than duplicating full source buffers. Explicit RAW pipeline
  ownership now distinguishes a Lab project from an ordinary Editor graph.
  Switching in either direction snapshots the correct graph, cancels deferred
  RAW loads and preview staging before restoration, and closes RAW-only native
  surfaces before the Editor graph becomes active.
- Leaving RAW Lab or closing Stack performs a synchronous lightweight project
  flush. Save revisions are rechecked while holding the file lock so an older
  queued save cannot replace a newer flush; existing `rawWorkspaceData` is
  merged rather than discarded. Windows replacement uses write-through atomic
  replacement, and unsigned binary metadata now round-trips while accepting
  older nonnegative signed encodings.

Focused coverage was added for stage-cache dependency hits/invalidation,
settled scope reuse, persistent-layer fresh-state/reset behavior, repeated RAW
Lab/Editor transitions, deferred-load cancellation, save ordering, unknown RAW
workspace-field preservation, and durable replacement. The final consolidated
Release verification passed: `.\build.cmd`, `StackGraphBehaviorTests`,
`Stack.exe --validate-node-math-phase6`,
`Stack.exe --validate-develop-node-smoke`, and the real-DNG smoke for
`IMG_260608_204838.dng`. The graph-behavior run included 24 consecutive
overwrite cycles and verified that no `.tmp` file remained. The phase-6
integration run included four exact dirty Editor/RAW graph round trips plus an
immediate durable-save reload that retained nested future metadata. The real
RAW trace kept rendered pixels identical for downstream-only interaction,
reported cache hits on the repeat path, and passed all workspace scope,
targeting, outline, and interactive-performance checks.

### August 1 RAW Lab / Graph View Transform Contract

- The RAW Lab View surface now has an explicit persisted
  `Apply View Transform in RAW Development` switch. It defaults on when the
  field is absent, so existing projects keep their prior pixels. Turning it
  off preserves the authored View settings but disables their controls and
  returns the post-Finish-Tone scene-linear working-space image from the
  compact RAW Development node.
- Graph scene-path and semantic descriptors now agree with the renderer.
  Enabled compact RAW Development output is already display-mapped, including
  through identity/geometry layers such as Flip, so connecting it to Output no
  longer creates a second View Transform. Disabled output is declared
  scene-linear and still requires exactly one downstream View Transform.
- When Output auto-inserts that external View Transform, it copies the
  disabled RAW Lab transform's authored exposure, curve, color, working-space,
  and output-transfer state. The graph View Transform also exposes its input
  working space directly. This keeps moving the display stage into the graph
  from silently changing Rec. 2020 interpretation or the authored look.
- Compact RAW Development was added to main-chain traversal. Its omission had
  prevented the inherited-state lookup from crossing Flip and could also make
  graph navigation/connection behavior disagree with the visible pipeline.

Focused verification passed `.\build.cmd`, `StackGraphBehaviorTests`,
`Stack.exe --validate-node-math-phase6`, and
`Stack.exe --validate-develop-node-smoke`. Coverage includes old recipes with
no enable field, disabled-state round trip, enabled and disabled
RAW Development -> Flip scene classification, one real editor auto-insertion,
inherited Rec. 2020/sRGB state, and placement immediately before Output.

## Multi-Image Project Foundation

Implemented August 2, 2026:

- RAW Workspace schema 3 `source-sets` snapshots and 64-bit logical types
- content-addressed exact-original embedding and within-project deduplication
- lazy transactional `.stackbundle` and append-generation portable `.stack`
  v3 stores, conversion, verification, conflict detection, recovery, and
  portable optimize
- one project-session lifecycle authority for v3 load/import/save/conflict and
  stale-completion fencing
- independent project catalog, many-to-many source memberships, and gallery
  multi-selection actions
- RAW Lab `Multi-Frame` organization UI and protected managed graph bindings
- explicit unavailable-result propagation with no fake reference output
- explicit legacy `Save As Upgraded Project` into a separate embedded bundle
- dedicated `--validate-multi-source-projects` coverage

The first MFD organization pass is also implemented:

- plain-click, Ctrl-toggle, Shift-range, and double-click-to-open gallery
  semantics shared by RAW, RAW Lab, and Library
- a two-or-more-RAW `Create Multi-Frame Denoise Project` action with project,
  location, and reference-frame choices
- header-only RAW preflight before staging any originals
- a mosaiced Bayer/CFA-only input boundary with explicit future-work rejection
  for demosaiced/Linear RGB RAWs
- structural compatibility checks for camera model, CFA, bit depth, RAW and
  visible geometry, and sensor margins
- project-frame selection, ordering, inclusion, reference selection, removal,
  and CFA-preserving orientation metadata in RAW Lab
- one protected RAW frame node per project frame, permanent managed links into
  one MFD node, and a serializable typed-unavailable MFD output
- internal View Transform by default, with a scene-linear graph placement
  option that maintains exactly one View Transform before Output
- separate MFD input and post-recipe revisions for future cache invalidation
- dedicated `--validate-mfd-project-foundation` coverage

Registration, motion/alignment estimation, merge math, denoising, demosaic
placement inside the eventual processor, derived caches, and final result
contracts remain deferred. The full boundary lives in
`multi-image-raw-project-foundation.md`.

### August 3 RAW Lab Lightroom-Style Point Curves

- Curve, not Zones, now owns four cumulative Finish Tone surfaces: composite
  Point, Red, Green, and Blue. The selectors are UI/app state only; changing
  surfaces or histogram presentation does not touch the recipe or render
  fingerprint.
- RAW recipe schema 13 adds `pointCurveSetVersion: 1` and canonical
  `pointCurves.composite/red/green/blue` components. New components use
  `monotone-cubic-v1`, keep endpoint inputs pinned while allowing matte output
  endpoints, sanitize to at most 12 points, and evaluate without per-segment
  overshoot even when the authored curve descends.
- The shared Tone Curve renderer uploads one 4096-sample RGBA32F LUT. RGB store
  composite-then-channel results for the corresponding channel. Alpha carries
  the compatibility-only legacy luminance response when required. Scene/Log
  coordinate conversion remains around that LUT and the stage remains after
  Zones and before View.
- Schema-12 `RGB/R/G/B` states migrate into their corresponding component with
  `preparedPoints` retained as compatibility `basePoints`. Legacy `Y` becomes
  an explicit pre-curve `legacyLuma` stage with a visible reset notice; new
  recipes do not author Y. Unknown Finish Tone and archived automatic-backend
  fields remain intact.
- RAW Lab now draws the sampled response, independent channel histogram
  emphasis, subtle red/cyan, green/magenta, or blue/yellow semantic tints,
  active points, edited dots, 0-255 Input/Output fields, Log EV equivalents,
  right-click point deletion, and active-curve reset. The top-level Curve reset
  clears all four curves and legacy Y while preserving domain, EV bounds,
  middle grey, and unrelated Finish Tone fields.
- Native review found that a selection click could nudge a point by one 0-255
  step. Point motion now begins only after a 1.5-pixel drag threshold.

Verification passed on August 3, 2026:

```text
.\build.cmd
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-develop-node-smoke
.\build\Stack.exe --validate-develop-real-raw-smoke C:\Users\djhbi\Downloads\Tennis\IMG_260608_204838.dng
```

The real-DNG point-curve trace reported selected-channel maximum differences
of `6.12695, 5.35059, 6.13086`, zero difference in both unedited channels for
each R/G/B render, and two upstream RAW-stage cache hits for every curve edit.
The existing settled Finish Tone scope, interactive proxy publication, target
outline, and full-quality completion checks also passed.

Native visual review passed at 1920x1080 and the application's current compact
minimum of 1282x832. The graph, tint, selector row, histogram, and numeric/EV
rows remained legible without clipping. A true 1280x720 window is currently
prevented by that minimum, and no high-DPI monitor was available; those two
visual gates remain open rather than being inferred from scaling.

### August 4 Shared Orientation Controls

- RAW recipe schema 14 persists horizontal and vertical flips alongside the
  existing quarter-turn rotation, with pre-schema-14 projects remaining
  unflipped.
- RAW Lab exposes Rotate Left, Rotate Right, Flip H, and Flip V in its command
  strip. They edit the same saved recipe for single-RAW and post-MFD workflows,
  invalidate the affected RAW stages, and do not rerun MFD fusion.
- Managed RAW graph conversion now round-trips those flips through RAW Decode;
  the original RAW Crop & Rotate panel exposes the same state.

## Next Allowed Work

1. Run a native Local Exposure targeting pass covering repeat drags, wheel
   reach, Shift/Alt seeds, context actions, mask opacity, 1280x720, and high
   DPI. Tune interaction constants from evidence.
2. Profile hover sampling and connected-mask rebuilds during real pointer
   traces. The bounded readbacks run on the RAW render owner but are not yet
   asynchronous PBO/reduction work.
3. Decide whether an unsaved prospective connected mask before the first drag
   is worth its extra render and transient-state complexity.
4. Compare authored edits made in original RAW and RAW Lab at the serialized
   recipe and rendered-pixel levels, including save/reload and hidden-field
   preservation.
5. Lower or explicitly retain the current 1282x832 native minimum, then
   exercise true 1280x720, high-DPI, and multi-monitor Gallery placement.
6. Complete Curve graph review on a real high-DPI monitor and, if the minimum
   is lowered, at true 1280x720. The 1920x1080 and 1282x832 passes are complete;
   tune only opacity and spacing from new evidence.
7. Decide whether graph-edge clipping markers, pixel-under-pointer values, and
   a separate spatial scope belong in Inspect rather than overloading the
   aligned histogram backdrop.
8. Reprofile RAW Exposure, Zones, Curve, and View interaction from RAW Lab and
   record Graph Performance overlay traces.
9. Consolidate the new shared image/overlay handoff into the single-owner RAW
   render service described in the performance record; do not enable the
   existing unsafe overlapping worker.
10. Add more real-camera and Linear DNG fixtures for the remaining Phase 01
   gates.
11. Implement or explicitly reject unsupported DNG opcode/profile cases.
12. Design a tested pre-demosaic highlight path.
13. Replace/validate Local Range smoothing with a documented guided or
   multiscale algorithm.
14. Implement the monitor ICC presentation seam.
15. Review the text-first layout before sourcing/replacing the complete icon
   family.
16. Re-audit latency after the UI/control redesign.

## Stop Rules

Do not:

- expose or reactivate automatic recipe-writing actions
- describe Truthful V1 as a complete production camera pipeline
- delete solver/backend code as part of this UI-only archive pass
- reinterpret an existing recipe with the new processing path
- add an undocumented camera/profile guess when metadata is incomplete
- silently fall back from an unsupported Truthful V1 metadata/profile case
- mix unrelated working-tree changes into this pass
- re-enable overlapping UI/worker RAW rendering without replacing the
  readback/re-upload ownership model
- place synchronous diagnostic readbacks back in the drag path
- remove or promote the original RAW presentation before Lab recipe, pixel,
  persistence, performance, and layout gates pass
- treat the optional Lab icon list as a reason to make missing artwork produce
  invisible controls
