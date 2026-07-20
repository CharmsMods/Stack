# Export Backend And Packaging

This document captures the video export and FFmpeg concerns that must be
researched before implementation.

## User-Stated Export Intent

- Export video from Stack, not only still images.
- Let the user set frame rate.
- Let the user choose video export settings.
- Export at the current resolution of the main canvas.
- Record interpolated keyframe changes over time.
- Include FFmpeg setup and packaging in the implementation plan.
- Update `stack-tools.cmd` if needed so FFmpeg license materials are packaged
  correctly.
- Avoid FFmpeg usage that would conflict with Stack's current license or create
  copyright/legal trouble.

## First Export Model To Research

The minimal export loop likely needs these concepts, but code research must
confirm names and ownership:

1. determine project frame range and frame rate
2. for each frame, evaluate timeline state
3. render the main canvas at the current export resolution
4. hand the frame to an encoder or write an image sequence
5. surface progress, cancellation, and errors
6. restore normal editing state after export

This is not yet an implementation contract.

## Settings To Define

Initial settings candidates:

- frame rate
- start frame
- end frame or duration
- output file path
- output container/format
- codec or preset if exposed
- quality or bitrate if exposed
- alpha/background behavior
- current canvas resolution lock
- overwrite behavior
- progress/cancel behavior

Open question: the first pass may intentionally expose fewer settings if Stack
needs a simple, reliable path before advanced export control.

2026-07-06 design-intake direction:

- timeline/video settings should move toward a dedicated project popup similar
  in feel to Stack's normal settings window
- the popup should hold output resolution, timeline FPS, encoding settings,
  video settings, and related project/export options
- the timeline header should not become a crowded export-settings surface
- implementation must distinguish project-persistent timeline settings,
  export-job settings, and app preferences before persisting additional fields

## FFmpeg Research Checklist

The official FFmpeg legal page says it is not legal advice and explains that
FFmpeg is generally LGPL, while optional GPL parts make GPL apply to the whole
FFmpeg build when used. It also lists compliance checklist items such as
avoiding `--enable-gpl` and `--enable-nonfree` for an LGPL-oriented build,
using dynamic linking when linking against FFmpeg libraries, distributing
corresponding FFmpeg source, and mentioning FFmpeg in product notices.

Official source:

- https://www.ffmpeg.org/legal.html
- https://www.ffmpeg.org/general.html

Research needed for Stack:

- What is Stack's current project license?
- Which approved app-local `ffmpeg.exe` build should Stack package?
- If bundling `ffmpeg.exe`, which binary distribution would be used?
- What exact FFmpeg configure flags were used for that binary?
- Does the binary include GPL, nonfree, patent-sensitive, or otherwise
  incompatible components?
- Which license files, notices, source links, and about-box/EULA mentions are
  required for Stack's distribution model?
- Does `stack-tools.cmd` currently package third-party notices or binaries?
- Should video export be disabled when no approved FFmpeg binary is present?
- Should Stack support image-sequence export as a no-encoder fallback?

## Local Stack Dependency Context

Stack's root license is source-available proprietary. Third-party materials
remain under their own licenses.

Current local LibRaw pattern:

- `THIRD_PARTY_NOTICES.md` documents LibRaw separately from Stack.
- `docs/stack-documentation/Info/top-level/RAW_ARW_PIPELINE_PLAN.md` describes
  LibRaw as `libraw.dll` beside `Stack.exe` with notice/license files.
- `tools/create_release.ps1` copies `THIRD_PARTY_NOTICES.md`, `licenses/`, and
  `libraw.dll` when present into the release stage directory.
- `src/Raw/LibRawRuntime.cpp` checks whether `libraw.dll` is available at
  runtime and reports RAW support as unavailable if it is missing.

This pattern is useful precedent for FFmpeg: keep the dependency outside the
main executable, detect/report availability cleanly, and package notices only
for binaries actually distributed. FFmpeg is not identical to LibRaw, and the
planned FFmpeg boundary is an app-local external executable, not linked FFmpeg
DLLs. The exact notice/source/build obligations still need separate review.

## FFmpeg-Specific Cautions

- Do not assume any random Windows FFmpeg build is safe to redistribute with
  Stack.
- Do not treat `libx264` or `libx265` as safe defaults; FFmpeg's docs identify
  x264 and x265 as GPL libraries that require FFmpeg's license to become GPL
  when enabled.
- Do not use `--enable-nonfree` builds for redistribution unless legal review
  explicitly approves the exact situation.
- Do not bury FFmpeg inside Stack's executable.
- Do not make the renderer depend directly on FFmpeg APIs before the legal and
  packaging model is settled.
- Do not make system-wide `PATH` discovery the primary user experience.

## Pass 2 Implemented Provider Foundation

Pass 2 adds a manifest-gated optional FFmpeg provider foundation. It does not
encode video yet.

Runtime discovery:

- Stack probes only the app-local path
  `<Stack executable directory>\tools\ffmpeg`.
- The expected manifest name is `ffmpeg-provider.json`.
- The expected executable path is read from the manifest's `binaryFile` field.
- Missing provider directory or missing executable is a normal unavailable
  state; Stack should continue running and video encoding can remain disabled.
- If a provider directory exists but the manifest is missing, malformed,
  unapproved, or advertises unsafe build flags, validation should fail.

Release packaging:

- Local reviewed provider source lives outside tracked source at
  `_workspace\ffmpeg-provider`.
- `tools/create_release.ps1` copies that directory into release artifacts at
  `tools\ffmpeg` only after manifest checks pass.
- A missing local provider source is allowed; releases package without FFmpeg.
- If a provider directory exists but fails checks, release packaging stops so
  Stack does not silently ship an unreviewed encoder.
- `tools/ffmpeg-provider.example.json` documents the manifest shape and keeps
  `approvedForRedistribution` false by default.

Required manifest fields for packaging:

- `schema`: `stack.ffmpegProvider`
- `schemaVersion`: `1`
- `binaryFile`: relative path, normally `ffmpeg.exe`
- `license`: expected Stack-approved marker such as `LGPL-2.1-or-later`
- `approvedForRedistribution`: `true` only after review
- `configureLine`: must not include `--enable-gpl` or `--enable-nonfree`
- `licenseFiles`: relative paths that exist inside the provider directory
- `sourceReferenceFiles`: relative paths that exist inside the provider
  directory

Current policy:

- Do not use FFmpeg DLL/library integration for the initial architecture.
- Do not auto-download FFmpeg in Stack or `stack-tools.cmd` yet.
- Do not package a GPL or nonfree FFmpeg build as Stack's approved provider.
- User-configured and `PATH` FFmpeg providers remain future fallback research,
  not Pass 2 implementation.

## Preferred Architecture To Research

Use an encoder-provider boundary:

- Stack owns frame-at-time rendering.
- Stack can export image sequences without FFmpeg.
- Stack's normal packaged experience should use an app-local approved
  `ffmpeg.exe`.
- Stack can fall back to a configured user path or optional `PATH` discovery
  when the app-local encoder is missing.
- `stack-tools.cmd` can later help install or refresh an approved FFmpeg build,
  but only after the exact build, license, notices, source availability, and
  release packaging are approved.

This keeps export implementation from depending on one legally fragile binary
choice while also avoiding a bad user experience where video export requires
manual system-wide FFmpeg setup.

Do not treat this file as legal advice. Before release, verify the selected
distribution approach against official FFmpeg docs and any project-specific
legal requirements.

## Backend Options

### External FFmpeg Executable

Stack renders raw frames or temporary image frames, then calls `ffmpeg.exe` as a
separate process.

This is the planned FFmpeg integration boundary. The preferred provider is an
approved app-local executable packaged with Stack, not a system dependency.

Questions:

- Is an external executable simpler for Stack's license and build system than
  linking libraries?
- What app-local path should Stack use for the packaged encoder?
- What provider priority should Stack use when the packaged encoder is missing?
- Should `stack-tools.cmd` support installing or refreshing the approved build
  after the packaged-provider path is defined?
- How should stdout/stderr, progress, and failures be surfaced?
- If Stack distributes or downloads it, what exact source, license, configure
  flags, notices, and matching-source obligations apply?

### FFmpeg Library Integration

Stack links against FFmpeg libraries and encodes directly.

This is not the planned initial architecture. It remains a later research topic
only if process-based encoding proves insufficient and legal/build review
supports direct library integration.

Questions:

- Does this fit Stack's current build system?
- How much license/compliance burden does linking introduce?
- Is the added control worth the implementation and distribution complexity?
- Would dynamic linking plus notices/source distribution be enough for the
  selected build, or would enabled GPL/nonfree components conflict with Stack's
  proprietary release model?

### Image Sequence First

Stack exports a numbered frame sequence first, then video encoding is a later
or optional step.

Questions:

- How should image-sequence export appear as a permanent supported fallback?
- Does it help validate frame-at-time rendering before encoder integration?
- How large are temporary frame outputs for expected project sizes?
- Should image-sequence export remain as a permanent fallback when FFmpeg is not
  available?
- Which export settings should live in the dedicated timeline/video settings
  popup, and which should remain per-export choices?
- How should timeline FPS editing behave after keyframes exist?

## Code Areas To Inspect

Update after code research:

- still-image export path:
- current export resolution owner:
- main canvas render/readback path:
- background export/task framework:
- build/package script:
- third-party notice location:
- existing FFmpeg references:
- app-local encoder provider path: Pass 2 uses
  `<Stack executable directory>\tools\ffmpeg`.
- approved FFmpeg build manifest location: Pass 2 uses
  `tools\ffmpeg\ffmpeg-provider.json` in packaged/runtime artifacts and
  `_workspace\ffmpeg-provider\ffmpeg-provider.json` as the local packaging
  source.
- validation command candidates: Pass 2 adds
  `Stack.exe --validate-ffmpeg-provider`.
- Stack root license and release EULA/about surfaces:
- existing optional-runtime dependency patterns:
