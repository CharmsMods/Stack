# RAW Node Technical Notes

## Ownership

`RawLoader` is the stable Stack-facing facade. `LibRawDecoder` is the only working backend and the only module that includes LibRaw headers. UI and graph code use Stack-owned `RawMetadata`, `RawImageData`, and `RawDevelopSettings`.

`RawSource` nodes serialize the source path, metadata snapshot, and develop settings. They do not serialize the raw sensor buffer. On render, the worker reloads/unpacks the RAW file through the decoder backend and `RawGpuPipeline` caches the GPU texture for the node.

## LibRaw Calls

The LibRaw backend calls:

- `LibRaw::open_file(path)`
- `LibRaw::unpack()`
- reads `imgdata.rawdata.raw_image`
- reads `imgdata.sizes`, `imgdata.idata`, and `imgdata.color`

It does not call `dcraw_process()` for the production path.

## Backend Seam

`RawDecoderBackend` currently defines `LibRaw`, `NativeExperimental`, and `CompareDebug`. Only LibRaw is implemented. Future native or compare/debug decoders must output the same Stack-owned `RawImageData` and metadata types.

## DNG Readiness

The loader recognizes `.dng` as a RAW path. Mosaiced Bayer DNG can use the current GPU RAW path. Linear RGB DNG is detected as a separate pixel layout and fails gracefully until Stack has a high-bit-depth RGB/linear import path.

## CFA Handling

The loader infers the 2x2 Bayer tile using LibRaw's `COLOR(row, col)` and `idata.cdesc`, offset by `top_margin` and `left_margin`. Supported patterns are `RGGB`, `BGGR`, `GBRG`, and `GRBG`. Unknown or non-Bayer data should keep `metadata.error` populated and render no texture.

## GPU Contract

`RawGpuPipeline` uploads the raw mosaic as `GL_R16UI`/`GL_RED_INTEGER`/`GL_UNSIGNED_SHORT`. The develop shader samples with `texelFetch`, normalizes using black/white levels, applies WB, performs bilinear demosaic, optionally applies the camera matrix, and writes `GL_RGBA16F`.

The CFA phase is defined at the visible image origin. Raw samples are fetched from absolute raw coordinates (`cropOrigin + visiblePixel`), but CFA classification is done in visible/cropped coordinates. This keeps LibRaw's uncropped `raw_image` buffer compatible with a Bayer pattern extracted at `left_margin/top_margin`.

Normalization uses the global black level by default and uses LibRaw per-channel black levels when present. Manual black override intentionally disables per-channel black subtraction so the override is easy to reason about.

Debug views are part of the RAW settings and render through the same GPU path:

- Final output
- Normalized mosaic
- CFA false color
- Demosaiced camera RGB
- White-balanced camera RGB
- Camera-transformed RGB
- Clipped RAW channels

Highlight reconstruction is a conservative artifact-reduction stage in `RawGpuPipeline`. It uses clipped RAW channel masks derived from the RAW white level and threshold setting, then neutralizes/desaturates fully clipped regions and blends one/two-channel clipped regions toward luminance-preserving repaired color. Higher-quality neighborhood reconstruction is still future work.

The current shader uses clamp-to-edge behavior. Alpha is always `1.0` for RAW output.

## Graph Integration

`RawSource` behaves like `Image` as a render-chain source:

- output socket: `imageOut`
- no input sockets
- can connect to layers, mix inputs, channel split, luminance mask, preview, scope, and output
- can be selected as the active source for auto-linked chains

The render worker still needs a reference canvas before graph evaluation. For RAW chains, Stack supplies transparent placeholder pixels sized to the RAW visible dimensions; the RAW node then produces the real GPU output texture during graph evaluation.

## Future Work

- Apply orientation consistently.
- Add crop-after-demosaic policy for all camera margins.
- Replace approximate camera matrix handling with a real working-space transform.
- Add Malvar-He-Cutler or another higher-quality demosaic.
- Add tile/cached decoding for very large files if needed.
- Add a debug metadata dump/export action.
