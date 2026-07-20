# Technical Explanations

This file explains the settled technical direction in plainer language, plus
the reasons the rejected alternatives are risky.

## Frame-At-Time Evaluation

Plain-language version:

When the playhead is on frame `48`, Stack has to answer: what does the whole
graph look like at frame `48`?

If a Brightness node is `0.0` at frame `0` and `0.5` at frame `100`, then frame
`48` needs an in-between brightness value. If that Brightness node feeds two
downstream outputs, both outputs should render with that frame-48 brightness
value.

The technical question is where those temporary frame values live while Stack
renders or exports.

### Option A - Temporarily Change The Normal Graph State

Stack could move the playhead to frame `48`, write the interpolated frame-48
values directly into the normal node settings, render, then restore the old
values afterward.

Pros:

- simplest to imagine
- may require less new render plumbing at first
- existing render code may already know how to render normal node values

Cons:

- risky for undo/redo and dirty project state
- export could accidentally leave the project at the last rendered frame
- background rendering could race with UI edits
- harder to guarantee that playback/export never corrupts the editing session
- shared nodes and multiple output rows become easier to mishandle

### Option B - Use A Frame Evaluation Overlay

Stack keeps the user's normal graph settings untouched. For frame `48`, it
computes temporary overrides such as "Brightness node amount = 0.24" and passes
those overrides into the render/evaluation path.

Pros:

- normal editing state stays clean
- playback and export do not have to dirty the project
- shared upstream nodes naturally affect every downstream output during that
  frame's render
- easier to run still export, playback, and video export from the same
  frame-evaluation contract
- better long-term fit for "keyframe almost anything"

Cons:

- requires a real evaluation context or override layer
- node parameter reads need to know how to ask for the frame value, not only the
  base value
- existing render code may need careful refactoring

### Option C - Render From A Duplicated Graph Snapshot

Stack could clone the graph, apply frame values to the clone, render from the
clone, and throw it away.

Pros:

- safer than mutating the real graph
- may fit existing snapshot/render worker concepts if they already exist

Cons:

- potentially heavy for long exports
- still needs a way to apply animated values to the clone
- can become expensive if every frame clones large graph state

### Settled Direction

Use Option B as the target architecture: a first-class frame evaluation
overlay/context.

The end-goal model should be:

- the saved graph stores base values and keyframes
- the playhead chooses a frame
- Stack computes interpolated parameter overrides for that frame
- render/export receives those overrides without permanently changing the graph

If code research shows that the current render path cannot support this
reliably right away, the fallback to research is Option C: render from a graph
snapshot or clone. The fallback should not be Option A, because mutating the
live graph during playback/export creates undo, dirty-state, save, crash
recovery, and background-render hazards.

Pass 4 code status:

- Stack now has a runtime `FrameEvaluationContext` for sampled keyframe values.
- The first covered parameters are the split adjustment layer family from Pass
  3.
- For rendering, sampled values are written into copied
  `RenderGraphNode::layerJson` payloads, not into the live graph or live layer
  objects.
- This proves the overlay approach for the first layer group, but does not yet
  add playback transport, export frame loops, persistence, or broad parameter
  coverage.

## Render Frames First Versus Tight Encoder Integration

This question is about video export shape.

### Render Frames First

Stack renders each timeline frame as a still image or raw frame, then hands the
frames to an encoder afterward or as a separate step.

Pros:

- easier to debug because individual bad frames can be inspected
- image-sequence export can work even before video encoding is final
- good for validation: frame `N` should match the visible timeline frame
- easier to recover from encoder failure without losing rendered frames
- keeps the render engine and encoder boundary clear

Cons:

- can use lots of disk space if frames are written as images
- can be slower if every frame is written and read back
- needs temporary file cleanup and overwrite rules
- the user may expect a single video file, not a folder of frames

### Tight Encoder Integration

Stack renders a frame and immediately sends it to an encoder process or encoder
library.

Pros:

- avoids large temporary image sequences
- can be faster for final video export
- progress can map directly to encoded frames
- produces the final video in one operation

Cons:

- harder to debug bad frames
- encoder failure can waste the whole export
- process piping can block if not handled carefully
- direct library integration increases dependency and licensing complexity
- cancellation and error recovery need more care

### Settled Export Shape

Use a frame producer plus export sinks:

- frame producer: evaluates and renders frame `N`
- still sink: writes one frame
- image-sequence sink: writes numbered frames
- FFmpeg process sink: sends frames to `ffmpeg.exe`
- future encoder-library sink: optional, only if licensing and build complexity
  justify it

This keeps the architecture from becoming "the FFmpeg path" too early.
Image-sequence export should remain a supported user-visible fallback/debug
export path even after direct video encoding exists.

Pass 5 code status:

- Stack now has runtime playback controls that advance the timeline frame using
  the configured FPS.
- Stack can build a graph snapshot for a requested timeline frame without
  moving the visible playhead.
- Stack can render a requested single-output timeline frame to an RGBA pixel
  buffer through `BuildSingleOutputTimelineFrameRaster(...)`.
- There is still no image-sequence writer, FFmpeg process sink, export settings
  UI, or persisted timeline data.

Pass 6 code status:

- Timeline settings and keyframe tracks now save into the project payload under
  `editorTimeline`.
- The saved timeline payload has schema version 1 and includes duration, FPS,
  current frame, tracks, node/parameter targets, keyframe values, frame
  numbers, and interpolation mode.
- Loading a project validates timeline targets against the loaded graph and the
  current animatable registry, then drops missing or unsupported targets.
- Invalid negative keyframes are ignored, keyframes beyond the loaded duration
  are clamped, and unsupported future schema versions fall back to default
  timeline state.
- The open timeline panel, panel height, play/pause state, loop state, and
  playback accumulator remain runtime-only rather than project data.
- There is still no image-sequence writer, FFmpeg process sink, export settings
  UI, curve editor, auto-keyframing, or broad node coverage.

## Timeline Row Hierarchy

Plain-language version:

A chain row is useful for showing that an output exists, but it is too broad to
be the final home for every keyframe. If a chain contains Brightness, Contrast,
and Sharpen nodes, putting all of their keyframes on one horizontal row makes
it hard to know which keyframe controls which value.

The end-goal timeline should have more structure:

- a chain section for the output chain
- a node row for each node in the chain
- property rows under a node for each animatable value

That lets the user see time at several levels. The chain can show summary
activity, the node row can group a node's animation, and the property row can
hold the actual keyframes for one value.

This does not mean every row has to be visible all the time. Collapsing and
expanding are expected. The important rule is that keyframes for different
nodes should not end up sharing the same final horizontal editing row.

## Frame Rate Lock And Retiming

Plain-language version:

Once a project has keyframes, the timeline frame rate is no longer just a
display preference. It is part of how time is divided.

If a keyframe is on frame `30`, that means something different at `30 FPS`
than it does at `60 FPS`. Changing the FPS after animation exists can shift
timing, change interpolation, or make held values behave differently.

The design direction is:

- warn the user when the first keyframe commits the timeline's frame rate
- do not let FPS changes after keyframes exist behave like a casual text edit
- research explicit retiming flows before implementation

Possible future retiming choices include preserving visual timing by moving
keyframes, multiplying FPS and interpolating between existing frames, or
materializing extra keyframes to avoid interpolation changes. Those choices
need research before they become code requirements.

Pass 7 code status:

- Stack now defaults timeline loop playback to on.
- When looping, playback uses the last distinct keyframe frame as the loop end
  if there are at least two distinct keyed frames.
- If there are zero or one distinct keyed frames, playback still uses the full
  timeline duration so it does not snap around a single keyframe.
- This is playback behavior only; it does not implement FPS locking, retiming,
  or export sampling changes.

## Timeline Project Persistence

The project file should remember the timeline data that changes what a project
means, but it should not remember momentary editor state that only describes
what the user interface was doing.

Saved project data:

- timeline duration
- frames per second
- current frame
- animation tracks
- each track's target node and parameter
- keyframe frame numbers, values, and interpolation modes

Runtime-only editor state:

- whether the bottom timeline panel is open
- the current panel height
- whether playback is currently running
- loop-playback state
- fractional playback accumulator state

That split keeps projects portable and avoids reopening a file into an
unexpected playback state. If Stack later wants to remember panel height or
open/closed state, that should likely be an app-window preference rather than
timeline project data.

## FFmpeg Dependency Strategy

Stack's current root license is proprietary source-available. Its existing
LibRaw pattern keeps third-party runtime code outside the main executable and
packages notices beside release artifacts. FFmpeg should follow the same
boundary principle, but as an app-local external executable rather than linked
FFmpeg libraries.

Official source context:

- FFmpeg legal page: https://www.ffmpeg.org/legal.html
- FFmpeg external library notes: https://www.ffmpeg.org/general.html
- LibRaw about/licensing page: https://www.libraw.org/about

Important findings:

- FFmpeg's official legal page says FFmpeg is LGPL 2.1-or-later by default,
  but optional GPL parts make GPL apply to the whole FFmpeg build when used.
- FFmpeg's compliance checklist says an LGPL-oriented library integration
  should avoid `--enable-gpl` and `--enable-nonfree`, use dynamic linking on
  Windows, distribute matching FFmpeg source, explain the configure line, and
  add notices in product/download/EULA/about surfaces.
- FFmpeg's docs state that x264 and x265 are GPL libraries and require FFmpeg's
  license to be upgraded to GPL when enabled.
- FFmpeg is not available from FFmpeg under proprietary/commercial terms.
- LibRaw is dual-licensed under LGPL 2.1 and CDDL 1.0, letting applications
  choose the better-fitting license for their use.

Implications for Stack:

- Do not treat a `libx264` command example as a safe default for Stack.
- Do not link FFmpeg libraries until there is a clear legal and build-system
  decision.
- Prefer an app-local external executable boundary first, similar in spirit to
  Stack's optional external LibRaw runtime boundary, but using `ffmpeg.exe` as a
  separate process rather than linked FFmpeg DLLs.
- The normal user experience should use Stack's packaged approved
  `ffmpeg.exe`, so the user does not need to install or configure FFmpeg
  system-wide.
- A user-selected `ffmpeg.exe` path and `PATH` discovery are useful fallback
  providers, not the primary product experience.
- If `stack-tools.cmd` downloads or installs FFmpeg, the script must only use an
  approved source/build and must document exactly what was installed.
- If Stack bundles or redistributes FFmpeg, the release process must include the
  exact binary, matching source/source link, license texts, configure flags,
  notices, and any required website/about/EULA language.

This is not legal advice. It is a technical planning constraint: the
implementation should keep FFmpeg behind a replaceable encoder-provider
boundary, with app-local approved `ffmpeg.exe` as the planned normal provider
once the exact build is approved.
