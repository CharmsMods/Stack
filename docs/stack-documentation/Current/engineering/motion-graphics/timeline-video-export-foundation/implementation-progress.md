# Timeline Video Export Implementation Progress

This file is the active pass ledger for timeline/video-export implementation.
Update it as each implementation chunk lands so future agents can resume
without repeating completed work.

## Current Pass

- Pass: Pass 16 - Timeline Row Hierarchy Foundation
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-07
- Scope: replace the flat one-row-per-output timeline body with a hierarchical
  display model that separates output-chain sections, animatable node rows,
  and registered property rows.
- Planned coverage: chain rows remain anchored to completed output nodes; node
  rows appear under each expanded chain for nodes that expose registered
  animatable parameters; property rows appear under expanded node rows; actual
  keyframe diamonds render on property rows; chain and node rows render summary
  marks only; chain and node rows can be expanded/collapsed at runtime.
- Explicitly out of scope: keyframe dragging, curve editing, keyframe color
  system, per-row context menus, timeline zoom/windowing, row virtualization,
  persisted expansion state, broad auto-keyframing, unsupported/non-scalar
  value rows, RAW/develop controls, image-sequence export, FFmpeg invocation,
  and full FPS retiming/locking implementation.

## Pass 16 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 16 scope before source edits. |
| Hierarchical rows implemented | Done | Added runtime chain, node, and property rows in the timeline body with expand/collapse for chain and node rows. |
| Summary and property keyframe marks implemented | Done | Actual target marks render on property rows; chain and node rows render dim summary marks. |
| Focused validation completed | Done | Build, graph tests, validators, scoped diff check, and touched-file whitespace scan passed. No new pure graph test was added because this pass is UI row-structure rendering over already tested chain/target data. |
| Documentation updated | Done | README, keyframing UX, code research, open questions, research backlog, and agent context now reflect Pass 16 hierarchy coverage. |

## Previous Passes

- Pass: Pass 15 - Background Patcher Scalar Node Coverage
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-07
- Scope: cover the remaining visible non-RAW layer registry node that still has
  straightforward serialized scalar controls by adding Background Patcher
  float/bool values to the existing layer-parameter animation registry.
- Planned coverage: Background Patcher removed-area opacity, color tolerance,
  edge smoothing, edge shift, defringe, keep-selected-range toggle, and debug
  visualizer toggle.
- Explicitly out of scope: Background Patcher target color, sampled color
  picker state, gradient/range visualization, brush/flood masks, patching state,
  anti-aliasing placeholder state, RAW/develop controls, Tone Curve/View
  Transform scene workflow controls, external-model/action/cache denoise nodes,
  hidden or deprecated nodes, color/palette arrays, randomize/action buttons,
  text/file/path values, curve editors, broad auto-keyframing, hierarchical
  chain/node/property rows, image-sequence export, FFmpeg invocation, timeline
  zoom/windowing, and full FPS retiming/locking implementation.

## Pass 15 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 15 scope before source edits. |
| Remaining visible-node audit completed | Done | Compared visible layer registry types against timeline scalar registry; Background Patcher is the only visible non-RAW registry node left with simple serialized scalar controls. |
| Background Patcher registry coverage added | Done | Added float/bool definitions for simple serialized controls only. |
| Focused tests added | Done | Covered parameter collection, current-value readback, clamping, boolean writes, and target-color non-coverage. |
| Documentation updated | Done | README, code research, open questions, research backlog, realtime followups, and agent context now reflect Pass 15 coverage. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, scoped diff check, and touched-file whitespace scan passed. |

- Pass: Pass 14 - Typed Bool Enum Layer Parameter Foundation
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-07
- Scope: add the first typed/discrete animatable parameter foundation by
  supporting serialized bool and enum/combo scalar values through the existing
  timeline registry, persistence, live-edit, and frame-evaluation paths.
- Planned coverage: start with normal non-RAW layer nodes that were already
  partially covered by numeric passes, such as simple blur type, crop flips,
  bilateral filter kernel/edge mode, compression/corruption/dither/denoise
  methods, chromatic falloff link, cell shading mode toggles, and similar
  simple serialized scalar controls.
- Explicitly out of scope: RAW/develop controls, Tone Curve/View Transform
  scene workflow controls, external-model/action/cache denoise nodes, hidden
  or deprecated nodes, color/palette arrays, randomize/action buttons,
  text/file/path values, curve editors, broad auto-keyframing, hierarchical
  chain/node/property rows, image-sequence export, FFmpeg invocation,
  timeline zoom/windowing, and full FPS retiming/locking implementation.

## Pass 14 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 14 scope before source edits. |
| Typed value metadata added | Done | Added bool and enum/combo metadata without changing project schema. |
| Typed node registry coverage added | Done | Covered focused non-RAW serialized scalar controls without touching RAW/develop nodes. |
| Frame evaluation support added | Done | Bool/enum values evaluate as hold/stepped values and write typed JSON scalars. |
| Focused tests added | Done | Added coverage for parameter collection, current-value readback, stepped evaluation, and frame JSON overrides. |
| Documentation updated | Done | README, keyframing UX, open questions, research backlog, code research, realtime followups, and agent context now reflect Pass 14 coverage. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, scoped diff check, and touched-file whitespace scan passed. |

- Pass: Pass 13 - Effects Generate Stylize Numeric Node Coverage
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-07
- Scope: continue broad non-RAW node support by covering another large batch
  of normal layer nodes with serialized numeric sliders/drag values.
- Planned coverage: Bilateral Filter, Noise numeric sliders, split dither
  numeric sliders, HDR, Color Grade strength, Vignette numeric sliders,
  Chromatic Aberration numeric sliders and center coordinates, Lens
  Distortion, Glare Rays, Airy Bloom, Halftoning numeric sliders, Cell Shading
  numeric sliders, Image Breaks, Analog Video, Expander padding, and Palette
  Reconstructor blend/smoothing.
- Explicitly out of scope: RAW/develop controls, Tone Curve/View Transform
  scene workflow controls, external-model/action/cache denoise nodes, hidden
  or deprecated nodes, bool controls, enum/combo controls, color/palette
  arrays, randomize/action buttons, text/file/path values, curve editors,
  broad auto-keyframing, hierarchical chain/node/property rows,
  image-sequence export, FFmpeg invocation, timeline zoom/windowing, and full
  FPS retiming/locking implementation.

## Pass 13 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 13 scope before source edits. |
| Array-element numeric support added | Done | Added numeric array-element read/write support for registered values such as Chromatic Aberration center X/Y. |
| Numeric node registry coverage added | Done | Covered the planned non-RAW numeric layer nodes without touching RAW/develop controls. |
| Frame evaluation support added | Done | Numeric animated values apply through existing layer JSON overrides, including supported numeric array elements. |
| Focused tests added | Done | Added coverage for parameter collection, current-value readback, and frame JSON override for the new numeric groups. |
| Documentation updated | Done | README, keyframing UX, open questions, research backlog, code research, and agent context now reflect Pass 13 coverage. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, and scoped whitespace checks passed. |

- Pass: Pass 12 - Non-RAW Numeric Layer Node Coverage
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-07
- Scope: broaden timeline keyframing coverage across normal non-RAW layer
  nodes by adding integer-slider support to the registered numeric parameter
  path and covering another sweep of float/int node controls.
- Planned coverage: split corruption nodes, split compression nodes, split
  denoising nodes, split edge-effect nodes, crop/rotate numeric transform
  controls, and heat/ripple distortion numeric controls.
- Explicitly out of scope: RAW/develop controls, bool controls, enum/combo
  controls, color/palette controls, randomize/action buttons, curve editors,
  text/file/path values, broad auto-keyframing, hierarchical
  chain/node/property rows, image-sequence export, FFmpeg invocation, timeline
  zoom/windowing, and full FPS retiming/locking implementation.

## Pass 12 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 12 scope before source edits. |
| Integer numeric parameter support added | Done | Added integer metadata and rounded/clamped JSON writes while keeping stored keyframe values numeric. |
| Numeric node registry coverage added | Done | Covered the planned non-RAW numeric layer nodes without touching RAW/develop controls. |
| Frame evaluation support added | Done | Numeric animated values apply through existing layer JSON overrides; integer values are rounded/clamped before JSON writes. |
| Focused tests added | Done | Added coverage for parameter collection, current-value readback, and frame JSON override for the new numeric groups. |
| Documentation updated | Done | README, keyframing UX, open questions, research backlog, code research, and agent context now reflect Pass 12 coverage. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, and scoped whitespace checks passed. |

- Pass: Pass 11 - Blur Family Animatable Float Coverage
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-07
- Scope: add complete float-slider timeline coverage for the first blur/focus
  node group: Box Blur, Gaussian Blur, Optical/Hankel Blur, and Tilt-Shift Blur.
- Explicitly out of scope: hierarchical chain/node/property rows, broad node
  coverage outside this group, discrete/enum combo keyframing such as blur
  filter type, draggable keyframes, broad auto-keyframing, image-sequence
  export, FFmpeg invocation, timeline zoom/windowing, and full FPS
  retiming/locking implementation.

## Pass 11 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 11 scope before source edits. |
| Blur family parameter registry added | Done | Added float slider definitions for Box Blur, Gaussian Blur, Hankel Blur, and Tilt-Shift Blur. |
| Frame evaluation support added | Done | Blur/focus animated values apply through existing layer JSON overrides. |
| Focused tests added | Done | Added coverage for parameter collection, current-value readback, and frame JSON override for the blur/focus group. |
| Documentation updated | Done | README, keyframing UX, open questions, research backlog, code research, and agent context now reflect Pass 11 coverage. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, and scoped whitespace checks passed. |

- Pass: Pass 10 - Off-Keyframe Live Edit Preview
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-07
- Scope: when the user edits a registered animatable layer parameter while the
  playhead is not on a matching keyframe, show the live node edit normally
  without saving a keyframe; reapply timeline evaluation when the playhead
  moves or playback starts.
- Explicitly out of scope: hierarchical chain/node/property rows, broad node
  coverage, draggable keyframes, broad auto-keyframing, creating new keyframes
  from value edits, image-sequence export, FFmpeg invocation, timeline
  zoom/windowing, and full FPS retiming/locking implementation.

## Pass 10 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 10 scope before source edits. |
| Live edit preview state added | Done | Tracks edited animatable targets whose timeline override should be temporarily bypassed. |
| Frame evaluation bypass added | Done | Removes live-edited targets from the current frame evaluation context only while the playhead stays on that frame. |
| Playhead clear behavior added | Done | Clears live edit preview when the timeline frame changes, playback starts, project timeline state resets, or `+ Key` writes the value. |
| Focused tests added | Done | Added coverage for removing one target from a frame evaluation context without affecting others. |
| Documentation updated | Done | README, keyframing UX, open questions, research backlog, code research, and agent context now reflect Pass 10. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, and touched-file whitespace scan passed. |

- Pass: Pass 9 - Realtime Existing-Keyframe Editing Foundation
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-06
- Scope: update an existing matching keyframe when the user edits a registered
  animatable layer parameter at the current timeline frame, then refresh the
  frame preview.
- Explicitly out of scope: hierarchical chain/node/property rows, broad node
  coverage, draggable keyframes, broad auto-keyframing, creating new keyframes
  from value edits, image-sequence export, FFmpeg invocation, timeline
  zoom/windowing, and full FPS retiming/locking implementation.

## Pass 9 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 9 scope before source edits. |
| Existing-keyframe update helper added | Done | Added a timeline helper that updates an existing keyframe value without creating a track/keyframe. |
| Layer edit hook added | Done | Dirty-tracked layer edits detect changed registered animatable parameters while the timeline is open. |
| Realtime preview refresh added | Done | After updating a matching keyframe, the timeline frame/composite output preview is refreshed. |
| Focused tests added | Done | Added coverage that existing keyframes update and missing keyframes are not created. |
| Documentation updated | Done | README, code research, open questions, keyframing UX, research backlog, and agent context now reflect Pass 8/9. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, and touched-file whitespace scan passed. |

- Pass: Pass 8 - Shortcut Stop Priority And Frame-Step Wraparound
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-06
- Scope: make `Space` stop/pause active timeline playback before viewport/split
  resize handling, keep `Q` as previous/left and `E` as next/right, and make
  previous/next frame stepping wrap within the current playback range.
- Explicitly out of scope: hierarchical chain/node/property rows, broad node
  coverage, draggable keyframes, auto-keyframing, realtime existing-keyframe
  editing, image-sequence export, FFmpeg invocation, timeline zoom/windowing,
  and full FPS retiming/locking implementation.

## Pass 8 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 8 scope before source edits. |
| Space stop-priority added | Done | `Space` pauses active timeline playback before existing viewport/split resize handling can start. |
| Frame-step wraparound added | Done | `Q`/`E` wrap across the playback range; exact visible-window wrap waits for future timeline zoom/windowing. |
| Focused tests added | Done | Added coverage for the pure frame-step wrap helper. |
| Documentation updated | Done | Progress docs captured the implemented shortcut behavior and kept visible-window wrap deferred. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, and touched-file whitespace scan passed. |

- Pass: Pass 7 - Timeline Header Polish And Shortcuts
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-06
- Scope: fixed/non-scrolling timeline header polish, compact icon-style
  transport controls with tooltips, simple timeline settings popup, guarded
  timeline keyboard shortcuts, loop playback defaulting on, and first
  loop-to-last-keyframe playback behavior.
- Explicitly out of scope: hierarchical chain/node/property rows, broad node
  coverage, curve editor UI, auto-keyframing, image-sequence export, FFmpeg
  invocation, and full FPS retiming/locking implementation.

## Pass 7 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 7 scope before source edits. |
| Header polish added | Done | Timeline controls now render in a fixed header child above the ruler/scrolling row list. |
| Compact controls added | Done | Transport controls use compact symbolic buttons with tooltips; duration/FPS moved out of the header. |
| Settings popup added | Done | Added a simple timeline settings popup for current frame, duration, FPS, and loop playback. |
| Keyboard shortcuts added | Done | `Q`/`E` step frames whenever the timeline is open and no item is active; `Space` toggles playback only while hovering the timeline. |
| Loop default updated | Done | Runtime/reset/load timeline state defaults loop playback on. |
| Loop-to-last-keyframe behavior added | Done | Looping playback uses the last distinct keyframe range when at least two distinct keyed frames exist; zero/one-keyframe timelines continue using full duration. |
| Focused tests added | Done | Graph behavior tests cover last-keyframe and distinct-keyframe-frame helper behavior. |
| Documentation updated | Done | README, code research, open questions, technical decisions/explanations, keyframing UX, and agent context now reflect Pass 7. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, scoped diff check, and touched-file whitespace scan passed. |

- Pass: Pass 6 - Timeline Persistence Foundation
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-06
- Scope: project save/load for timeline duration, FPS, current frame, and
  runtime animation tracks/keyframes; schema/version guardrails; load-time
  repair for missing/unsupported targets; and focused round-trip validation.
- Explicitly out of scope: video or image-sequence export UI, FFmpeg
  invocation, encoder settings, curve editor UI, auto-keyframing, broad
  all-node coverage, and persisting transient playback/open-panel state.

## Planning Intake After Pass 6

- Date: 2026-07-06 21:58
- Status: documentation-only; no code changes
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-2158-timeline-ux-keyframe-controls.md`
- Routed note:
  `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/2026-07-06-timeline-ux-keyframe-controls.md`
- Research backlog:
  `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/research-backlog.md`
- Summary: captured future timeline header polish, icon-control direction,
  hierarchical chain/node/property rows, keyframe color differentiation,
  dedicated timeline/video settings popup, FPS locking/retiming questions,
  timeline keyboard shortcut ideas, playback loop defaults, dense-row visual
  design concerns, and a documentation rule for preserving research-needed
  topics separately from implementation guidance.

## Planning Intake After Pass 7

- Date: 2026-07-06 22:50
- Status: documentation-only; no code changes
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-2250-timeline-realtime-editing-followups.md`
- Routed note:
  `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/2026-07-06-timeline-realtime-editing-followups.md`
- Summary: captured `Space` stop-priority behavior while playback is active,
  frame-step wraparound ideas, draggable keyframes, and the current realtime
  editing limitation where node/property edits do not update existing matching
  keyframes or viewport preview unless the user explicitly writes a keyframe.

## Scoping Note After Shortcut Clarification

- Date: 2026-07-06
- Status: planning/scoping only; no source implementation from this note yet
- The user confirmed that the earlier "E to go left" wording was a slip.
  Shortcut direction remains `Q` for previous/left and `E` for next/right.
- Small, implementation-ready candidate: make `Space` stop/pause active
  timeline playback before viewport/split resize handling, and add
  previous/next frame wraparound for `Q`/`E`. Because there is no timeline zoom
  or visible window model yet, any near-term wraparound must use a temporary
  range such as the playback loop range rather than the future visible range.
- Larger, focused candidate: add update-existing-keyframe behavior for the
  first registered animatable layer family. Use
  `EditorModule::RenderLayerControlsWithDirtyTracking(...)` to detect changed
  registered layer parameters, update only a matching keyframe at the current
  playhead frame, and refresh the frame preview. This should not create new
  keyframes when no matching keyframe exists.
- Historical note: at this point, draggable keyframes on the then-current flat
  chain rows were deferred. Pass 16 later added the first property/value row
  hierarchy; dragging still needs its own target hit-test and interaction pass.

- Pass: Pass 5 - Timeline Playback And Frame Producer Foundation
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-06
- Scope: runtime timeline playback controls, FPS-based playhead advancement,
  reusable frame-specific graph/raster entry points for future export, and
  focused playback/frame-producer validation.
- Explicitly out of scope: project serialization, video or image-sequence
  export UI, FFmpeg invocation, encoder settings, curve editor UI,
  auto-keyframing, and broad all-node coverage.

- Pass: Pass 4 - Frame Evaluation Context Foundation
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-06
- Scope: runtime timeline track sampling, a temporary frame evaluation context,
  non-mutating frame-specific layer JSON overrides for the first covered split
  adjustment layer parameters, and current-frame preview/render refresh where
  feasible.
- Explicitly out of scope: project serialization, playback transport, video or
  image-sequence export, curve editor UI, broad all-node coverage, and FFmpeg
  invocation.

- Pass: Pass 3 - Runtime Keyframe Model And First Animatable Registry
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Started: 2026-07-06
- Scope: runtime-only keyframe data model, stable parameter target IDs, first
  node-group-complete animatable coverage for split adjustment layer nodes,
  timeline target selection, Add Keyframe command, and keyframe marks on rows.
- Explicitly out of scope: project serialization, frame evaluation,
  interpolation execution, playback rendering, video/image-sequence export,
  curve editing, auto-keyframing, and broad all-node coverage.

- Pass: Pass 2 - Optional FFmpeg Provider And Packaging Foundation
- Status: code complete; automated validation complete
- Started: 2026-07-06
- Scope: app-local FFmpeg provider discovery, manifest-gated release packaging,
  stack-tools visibility, legal/package documentation, and validation that
  missing FFmpeg is a supported state.
- Explicitly out of scope: video export UI, frame production, encoder process
  invocation, downloading FFmpeg automatically, choosing a specific third-party
  FFmpeg build, and declaring final legal approval.

## Previous Pass

- Pass: Pass 1 - Visible Timeline Foundation
- Status: code complete; automated validation complete; native/manual UI smoke
  pending
- Scope: runtime-only bottom timeline panel, output-chain rows, playhead shell,
  duration/FPS controls, hotkey, layout reservation, focused graph behavior
  test, and documentation updates.
- Explicitly out of scope: keyframe storage, parameter registry, interpolation,
  frame evaluation, playback rendering, video export, FFmpeg discovery, FFmpeg
  packaging, and project persistence.

## Pass 1 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger created | Done | Created this file before source edits. |
| Runtime timeline UI state added | Done | Added runtime-only `TimelineUiState` with defaults: closed, 220 px height, frame 0, 120 frames, 30 FPS. |
| Timeline hotkey added | Done | Added `Ctrl+Shift+Tab`; guarded existing `Ctrl+Tab` presets toggle with `!KeyShift`. |
| Layout reserves bottom timeline height | Done | Workspace computes timeline height and gives graph, viewport, and split handle the reduced pane height. |
| Timeline panel UI added | Done | Added bottom timeline panel with resize grip, header controls, ruler/playhead shell, and scrollable completed-chain rows. |
| Graph behavior test added | Done | Added graph behavior coverage for one shared upstream branch feeding two output-anchored completed chains. |
| Documentation updated | Done | README, research plan, open questions, and agent context now reflect Pass 1 implementation and verified paths. |
| Validation completed | Done | Automated checks completed. Native/manual UI smoke was not run in this pass. |

## Validation Log

- 2026-07-06: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed.
- 2026-07-06: full `git diff --check` reported CRLF conversion warnings across
  the dirty worktree and an unrelated `cmake/StackVersion.cmake:7: new blank
  line at EOF` issue. This pass did not touch `cmake/StackVersion.cmake`.
- 2026-07-06: scoped `git diff --check -- <Pass 1 files>` passed with CRLF
  conversion warnings only.
- Native/manual UI smoke for opening the app, pressing `Ctrl+Shift+Tab`, row
  scrolling, and resize behavior is still pending.

## Pass 2 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 2 before source edits. |
| Runtime FFmpeg provider discovery added | Done | Added `Stack::Video::ProbeAppLocalFFmpegProvider()` for app-local `tools\ffmpeg` manifest discovery. Missing FFmpeg is a supported unavailable state. |
| Validation command added | Done | Added `Stack.exe --validate-ffmpeg-provider`; it should pass when the provider is missing and fail for malformed/unsafe provider manifests. |
| Release packaging support added | Done | `create_release.ps1` now copies `_workspace\ffmpeg-provider` to packaged `tools\ffmpeg` only when manifest checks pass. Missing provider continues packaging without FFmpeg. |
| stack-tools visibility added | Done | `stack-tools.cmd` validation now includes `--validate-ffmpeg-provider`, and the menu can show optional provider status. |
| Legal/package docs updated | Done | Updated provider policy, app-local paths, manifest gate, official FFmpeg caution points, and optional-provider notices. |
| Validation completed | Done | Build, provider validation, graph tests, no-provider packaging smoke, scoped diff check, and touched-file whitespace check passed. |

## Pass 4 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 4 scope before source edits. |
| Frame sampling model added | Done | Runtime tracks evaluate deterministically at a requested frame with exact-key, linear, hold, and range-clamp behavior. |
| Frame evaluation context added | Done | Context holds temporary per-node/per-parameter float values for the requested frame. |
| Layer JSON override path added | Done | Covered split adjustment parameters apply only to render snapshot layer JSON via the frame evaluation context. |
| Current-frame preview refresh wired | Done | Scrubbing with timeline tracks and adding keyframes request render refreshes; affected composite output rows are marked dirty. |
| Focused tests added | Done | Graph behavior tests cover interpolation/hold sampling, frame context lookup, and non-mutating layer JSON overrides. |
| Documentation updated | Done | README, research plan, open questions, technical decisions/explanations, keyframing UX, chain model, and agent context now reflect Pass 4. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, scoped diff check, and touched-file whitespace scan passed. |

## Pass 5 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 5 scope before source edits. |
| Runtime playback state added | Done | Added runtime-only playing, loop, and frame-accumulator state to `TimelineUiState`. |
| Playback controls added | Done | Timeline header exposes play/pause, stop, previous/next frame, and loop controls. |
| FPS-based advancement added | Done | Playback advances `currentFrame` from `framesPerSecond` and requests render refreshes on frame changes. |
| Frame-specific graph snapshot added | Done | Added `BuildGraphSnapshotForTimelineFrame(...)` so future export can build frame N without moving the UI playhead. |
| Frame raster entry point added | Done | Added `BuildSingleOutputTimelineFrameRaster(...)` as the first reusable single-output frame raster path. |
| Focused tests added | Done | Graph behavior tests cover playback advancement, stop/loop behavior, and frame request normalization/context sampling. |
| Documentation updated | Done | README, research plan, open questions, technical decisions/explanations, keyframing UX, and agent context now reflect Pass 5. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, scoped diff check, and touched-file whitespace scan passed. |

## Pass 6 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 6 scope before source edits. |
| Timeline schema added | Done | Added `editorTimeline` schema version 1 through `TimelinePersistence.*`. |
| Timeline save/load wired | Done | Pipeline serialization now writes `editorTimeline`; deserialization restores duration, FPS, current frame, and animation tracks. |
| Load-time repair added | Done | Deserialization validates targets against the loaded graph and animatable registry, dropping missing/unsupported targets safely. |
| Transient state excluded | Done | Playback/open-panel state remains runtime-only; load resets playback state. |
| Focused tests added | Done | Graph behavior tests cover timeline document round-trip, invalid target repair, frame clamping, and unsupported future schema safety. |
| Documentation updated | Done | README, code research, open questions, chain model, technical decisions/explanations, keyframing UX, and agent context now reflect Pass 6. |
| Validation completed | Done | Build, graph tests, layer registry validation, FFmpeg provider validation, scoped diff check, and touched-file whitespace scan passed. |

## Pass 3 Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Progress ledger updated | Done | Added Pass 3 scope before source edits. |
| Runtime keyframe model added | Done | Added runtime tracks, targets, float keyframes, interpolation placeholder, sorted insert, and replace-at-frame behavior in `TimelineAnimation`. |
| First animatable registry added | Done | Covered split adjustment layer nodes completely: Brightness, Contrast, Saturation, Warmth, and Sharpen. |
| Timeline keyframe UI added | Done | Selected output chain now exposes supported parameters and can add runtime keyframes at the current frame. |
| Keyframe marks rendered | Done | Runtime keyframe marks render on every output row whose completed chain contains the keyed node. |
| Focused tests added | Done | Added graph behavior tests for registry group completeness and keyframe storage/affected-chain behavior. |
| Documentation updated | Done | Updated README, code research, keyframing UX, open questions, technical decisions, and implementation agent context. |
| Validation completed | Done | Build, graph behavior tests, layer registry validation, FFmpeg provider validation, scoped diff check, and touched-file whitespace check passed. Native/manual UI smoke is pending. |

## Resume Notes

Use existing completed-chain discovery as the row source. Do not add a parallel
chain detector in Pass 1.

Pass 2 established the optional FFmpeg provider foundation. Do not add a
second provider discovery path before extending the existing provider contract.
Missing FFmpeg must remain a supported state.

Pass 4 established the first non-mutating frame evaluation path. Future passes
should reuse `Stack::Timeline::FrameEvaluationContext` and render snapshot
overrides instead of mutating live layer/node state during playback or export.

Pass 5 established runtime playback helpers and frame-specific graph/raster
entry points. Future image-sequence and video export work should reuse
`BuildGraphSnapshotForTimelineFrame(...)` and
`BuildSingleOutputTimelineFrameRaster(...)` instead of moving the UI playhead
or mutating live graph state.

Pass 6 established project persistence for timeline data under
`editorTimeline` schema version 1. Future timeline persistence work should
extend that schema with explicit version handling and preserve the rule that
playback/open-panel UI state is transient unless a later app-settings decision
changes it.

## Pass 2 Validation Log

- 2026-07-06: `.\build.cmd` passed after adding the FFmpeg provider module and
  validation suite.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present. Output confirmed video encoding can remain
  disabled while Stack continues normally.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed.
- 2026-07-06: no-provider release-staging smoke passed with
  `tools\create_release.ps1 -SkipBuild -SkipInstaller -SkipPortableZip
  -SkipHashes`; packaging continued without `_workspace\ffmpeg-provider`.
- 2026-07-06: scoped `git diff --check -- <Pass 2 tracked files>` passed with
  CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed, including new
  untracked Pass 2 files.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.

## Pass 3 Validation Log

- 2026-07-06: first `.\build.cmd` attempt found a new include path issue in
  `TimelineAnimation.cpp`; fixed by using `Editor/Layers/LayerBase.h`.
- 2026-07-06: rerun `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  split-adjustment registry and timeline keyframe affected-row tests.
- 2026-07-06: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-06: scoped `git diff --check -- <Pass 3 tracked files>` passed with
  CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed, including new
  untracked Pass 3 files.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.

## Pass 4 Validation Log

- 2026-07-06: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  timeline frame sampling and non-mutating layer JSON override tests.
- 2026-07-06: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-06: scoped `git diff --check -- <Pass 4 touched files>` passed with
  CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for timeline scrubbing render refresh is still
  pending.

## Pass 5 Validation Log

- 2026-07-06: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  timeline playback and frame-producer normalization tests.
- 2026-07-06: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-06: scoped `git diff --check -- <Pass 5 touched files>` passed with
  CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed, including new
  Pass 5 files.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for timeline playback controls is still pending.

## Pass 6 Validation Log

- 2026-07-06: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  timeline persistence round-trip and repair tests.
- 2026-07-06: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-06: scoped `git diff --check -- <Pass 6 touched tracked files>`
  passed with CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed, including new
  Pass 6 files.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for save/load of timeline keyframes is still pending.

## Pass 7 Validation Log

- 2026-07-06: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  timeline last-keyframe/distinct-frame helper coverage.
- 2026-07-06: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-06: scoped `git diff --check -- <Pass 7 touched tracked files>`
  passed with CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for the header, settings popup, shortcuts, and
  loop-to-last-keyframe behavior is still pending.

## Pass 8 Validation Log

- 2026-07-06: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  timeline step-wrap helper coverage.
- 2026-07-06: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-06: scoped `git diff --check -- <Pass 8/9 tracked source files>`
  passed with CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for Space stop-priority and Q/E wraparound is still
  pending.

## Pass 9 Validation Log

- 2026-07-06: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-06: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  update-existing-keyframe helper coverage.
- 2026-07-06: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-06: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-06: scoped `git diff --check -- <Pass 8/9 tracked source files>`
  passed with CRLF conversion warnings only.
- 2026-07-06: touched-file trailing-whitespace scan passed.
- 2026-07-06: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for realtime existing-keyframe editing is still
  pending.

## Pass 10 Validation Log

- 2026-07-07: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-07: `.\build\StackGraphBehaviorTests.exe` passed, including the new
  frame-evaluation live-edit target removal coverage.
- 2026-07-07: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-07: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-07: scoped `git diff --check -- <Pass 10 tracked source files>`
  passed with CRLF conversion warnings only.
- 2026-07-07: touched-file trailing-whitespace scan passed.
- 2026-07-07: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for off-keyframe live edit preview is still pending.

## Pass 11 Validation Log

- 2026-07-07: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-07: `.\build\StackGraphBehaviorTests.exe` passed, including blur
  family animatable registry coverage and blur/focus frame JSON override
  coverage.
- 2026-07-07: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-07: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-07: scoped `git diff --check -- <Pass 11 tracked source files>`
  passed with CRLF conversion warnings only. Some timeline workstream files are
  untracked in this dirty worktree, so they are covered by the direct
  whitespace scan below rather than by Git's tracked-file diff check.
- 2026-07-07: touched-file trailing-whitespace scan passed.
- 2026-07-07: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for blur-family keyframing is still pending.

## Pass 12 Validation Log

- 2026-07-07: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-07: `.\build\StackGraphBehaviorTests.exe` passed, including non-RAW
  numeric layer registry coverage and rounded/clamped integer frame JSON
  override coverage.
- 2026-07-07: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-07: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-07: scoped `git diff --check -- <Pass 12 tracked source files>`
  passed with CRLF conversion warnings only. Some timeline workstream files are
  untracked in this dirty worktree, so they are covered by the direct
  whitespace scan below rather than by Git's tracked-file diff check.
- 2026-07-07: touched-file trailing-whitespace scan passed.
- 2026-07-07: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for Pass 12 node keyframing is still pending.

## Pass 13 Validation Log

- 2026-07-07: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-07: `.\build\StackGraphBehaviorTests.exe` passed, including effects,
  generate, stylize, optics, and damage numeric registry coverage plus
  array-element frame JSON override coverage.
- 2026-07-07: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-07: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-07: scoped `git diff --check -- <Pass 13 tracked source files>`
  passed with CRLF conversion warnings only. Some timeline workstream files are
  untracked in this dirty worktree, so they are covered by the direct
  whitespace scan below rather than by Git's tracked-file diff check.
- 2026-07-07: touched-file trailing-whitespace scan passed.
- 2026-07-07: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for Pass 13 node keyframing is still pending.

## Pass 14 Validation Log

- 2026-07-07: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-07: `.\build\StackGraphBehaviorTests.exe` passed, including typed
  bool/enum scalar registry coverage, hold/stepped discrete evaluation, and
  typed JSON override coverage.
- 2026-07-07: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-07: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-07: scoped `git diff --check -- <Pass 14 tracked source files>`
  passed with CRLF conversion warnings only. Some timeline workstream files are
  untracked in this dirty worktree, so they are covered by the direct
  whitespace scan below rather than by Git's tracked-file diff check.
- 2026-07-07: touched-file trailing-whitespace scan passed.
- 2026-07-07: full `git diff --check` still fails on the unrelated
  `cmake/StackVersion.cmake:7: new blank line at EOF` issue already present in
  the broader dirty worktree.
- Native/manual UI smoke for Pass 14 node keyframing is still pending.

## Pass 15 Validation Log

- 2026-07-07: `.\build.cmd` passed and produced `build\Stack.exe` plus
  `build\StackGraphBehaviorTests.exe`.
- 2026-07-07: `.\build\StackGraphBehaviorTests.exe` passed, including
  Background Patcher scalar registry coverage, current-value readback,
  clamping, boolean JSON override coverage, and target-color non-coverage.
- 2026-07-07: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-07: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-07: scoped `git diff --check -- <Pass 15 touched files>` passed
  with CRLF conversion warnings only.
- 2026-07-07: touched-file trailing-whitespace scan passed.
- 2026-07-07: post-pass registry comparison showed the only layer registry
  types without timeline scalar entries are deferred edge-case families:
  `AlphaHandling`, `ClassicalRgbDenoise`, `LinearRgbNeuralDenoise`,
  `SceneDenoise`, `TextOverlay`, `ToneCurve`, `ToneEqualizer`, and
  `ViewTransform`.
- Native/manual UI smoke for Pass 15 Background Patcher keyframing is still
  pending.

## Pass 16 Validation Log

- 2026-07-07: initial `.\build.cmd` failed because the timeline UI file
  referenced a node-label helper local to the timeline animation file.
- 2026-07-07: added a local timeline node label resolver in
  `EditorModuleTimeline.cpp`; reran `.\build.cmd`, which passed and produced
  `build\Stack.exe` plus `build\StackGraphBehaviorTests.exe`.
- 2026-07-07: `.\build\StackGraphBehaviorTests.exe` passed.
- 2026-07-07: `.\build\Stack.exe --validate-layer-registry` passed.
- 2026-07-07: `.\build\Stack.exe --validate-ffmpeg-provider` passed with no
  app-local provider present.
- 2026-07-07: scoped `git diff --check -- <Pass 16 touched files>` passed
  with CRLF conversion warnings only. Some timeline workstream files are
  untracked in this dirty worktree, so they are covered by the direct
  whitespace scan below rather than by Git's tracked-file diff check.
- 2026-07-07: touched-file trailing-whitespace scan passed.
- Native/manual UI smoke for Pass 16 hierarchical timeline rows is still
  pending.
