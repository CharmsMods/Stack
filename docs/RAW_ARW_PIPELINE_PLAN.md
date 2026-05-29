# Sony ARW RAW Pipeline Plan

## Intended Pipeline

Stack's RAW path is a source-node pipeline:

`ARW file -> LibRaw open_file() -> LibRaw unpack() -> RawImageData -> GPU raw texture -> black/white normalization -> white balance -> demosaic -> camera color transform -> linear RGB texture -> normal node graph`

The RAW node outputs a normal image socket. Downstream layers, mix nodes, previews, scopes, and output nodes should not need to know that the source began as sensor mosaic data.

## LibRaw Boundary

LibRaw is used only for file parsing, decoder support, unpacking, and metadata extraction. Stack copies the unpacked `raw_image` mosaic buffer and selected metadata into `RawImageData`.

The main Stack path must not call `dcraw_process()`. That function performs LibRaw/dcraw-style CPU development into a finished image, which would bypass Stack's GPU RAW controls and bake decisions too early. A future reference/debug preview may use it only behind an isolated debug path that is clearly not the production output path.

## GPU Processing Stages

V1 uses a direct GPU develop shader:

- Upload unpacked Bayer mosaic as `R16UI`.
- Normalize each raw value with `clamp((rawValue - blackLevel) / (whiteLevel - blackLevel), 0, 1)`.
- Apply as-shot, neutral, or manual RGB white-balance multipliers.
- Demosaic with bilinear interpolation.
- Use clamp-to-edge sampling at image borders.
- Apply LibRaw camera matrix metadata when available. This is currently approximate because Stack does not yet have full ICC/DCP color management.
- Apply a simple exposure multiplier and output `RGBA16F` with alpha `1.0`.
- Optionally apply conservative highlight reconstruction to reduce clipped RAW channel color artifacts before final output. This is not a substitute for graph-level tone mapping.

The shader treats the visible crop origin as the CFA phase origin while still fetching samples from the uncropped LibRaw raw buffer. This avoids Bayer phase shifts when `left_margin` or `top_margin` are odd.

The first version keeps the shader compact. Later passes can split this into explicit normalize, demosaic, and color transform passes if profiling or debugging requires it.

## RAW Node UI

`RawSource` is a source-like graph node with one image output. Double-clicking the node header opens the existing complex-node/overlay settings surface.

Controls are grouped as:

- File / Camera: path, make/model, raw and visible dimensions, CFA, bit depth, status/errors.
- Basic RAW: exposure, WB mode, manual RGB multipliers, black/white level overrides.
- Demosaic: method selector and edge behavior status.
- Color: camera transform toggle and transform status.
- Highlight Reconstruction: conservative clipped-channel repair controls.
- Debug: stage view selector for final output, normalized mosaic, CFA false color, demosaiced camera RGB, white-balanced camera RGB, camera-transformed RGB, clipped RAW channels, and warnings.

Inline graph UI stays compact. The larger settings surface is where detailed RAW controls live.

## Required Metadata

`RawMetadata` stores source path, camera make/model, raw dimensions, visible dimensions, crop/margins, orientation, bit depth, CFA pattern, mosaiced/linear status, global/per-channel black levels, white/saturation level, as-shot WB, daylight/default WB, camera matrix, upload format, warnings, and error status.

V1 supports Bayer CFA patterns: `RGGB`, `BGGR`, `GBRG`, and `GRBG`. Unknown patterns fail gracefully with a status error.

## Known V1 Limitations

- Color will not match Lightroom exactly.
- Full ICC, DCP, and camera-profile support is not implemented.
- Highlight recovery, lens correction, noise reduction, chromatic aberration correction, and advanced demosaic are deferred.
- Malvar-He-Cutler is exposed only as a placeholder option; bilinear is the implemented demosaic.
- Orientation handling is applied for common LibRaw orientation codes, but crop/rotation behavior still needs broader camera validation.
- Sony compressed ARW variants depend on LibRaw support and need real-camera testing.
- Non-Bayer RAW, pixel-shift RAW, and already-linear RGB RAW are not supported in V1.

## Licensing And Distribution Notes

Stack's current Windows production-readiness layout ships LibRaw as `libraw.dll` beside `Stack.exe`, with `THIRD_PARTY_NOTICES.md` and copied LibRaw license/copyright files in `licenses/`. This is a practical engineering plan, not legal certainty. Final legal review is still required before public or commercial release.

V1 pins LibRaw `0.22.1` / commit `b860248a89d9082b8e0a1e202e516f46af9adb29`. RawSpeed, Adobe DNG SDK, and optional accelerator/extra decoder components are not enabled; if considered later, their separate licensing and notice obligations must be reviewed first.

## Phased Checklist

- [x] Add RAW architecture docs and third-party notices.
- [x] Add `STACK_ENABLE_LIBRAW` build gate and isolated LibRaw integration.
- [x] Add `RawMetadata`, `RawImageData`, `RawDevelopSettings`, and `RawLoader`.
- [x] Add `RawSource` graph node, serialization, and import routing for `.ARW`.
- [x] Add GPU RAW rendering path with R16UI upload and bilinear demosaic.
- [x] Add compact and complex RAW controls.
- [ ] Improve orientation/crop handling and color management.
- [ ] Add higher-quality demosaic.
- [ ] Add automated image regression fixtures when test assets are available.

## Testing Checklist

- Build with `.\build.cmd`.
- Build with `-DSTACK_ENABLE_LIBRAW=OFF`.
- Import PNG/JPEG and confirm existing image nodes still render.
- Import `.ARW` and confirm a RAW node and output chain are created.
- Confirm corrupt/unsupported RAW shows an error instead of crashing.
- Confirm metadata fields look sane for a Sony ARW.
- Toggle normalized mosaic debug view.
- Confirm bilinear RGB output renders and connects downstream.
- Double-click RAW node and edit exposure/WB without reloading the file.
- Save/load a project with a RAW node.
- Rename or remove the source RAW file and confirm the project does not crash.
- Compare against camera JPEG, RawTherapee, darktable, or Lightroom for major Bayer/color/crop/orientation mistakes.
