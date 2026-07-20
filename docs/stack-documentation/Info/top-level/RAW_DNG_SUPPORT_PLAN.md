# RAW DNG Support Plan

## Summary

DNG support should extend the existing RAW node and decoder backend structure. Do not create a separate DNG node or a second GPU pipeline unless a future native decoder requires it.

Targets for first validation:

- Samsung Galaxy S24 base model DNG files.
- Samsung Expert RAW / Pro mode DNG files.
- MotionCam Pro exported DNG stills or per-frame DNG exports.

V1 now targets exported `.dng` files only. MotionCam `.mcraw` containers, clip-directory import, image-sequence workflows, embedded XMP provenance display, and native TIFF/DNG parsing are out of scope for this pass.

## DNG Layout Detection

Stack must classify DNG input before rendering:

- Mosaic Bayer DNG: use the existing RAW mosaic upload, normalization, WB, demosaic, camera transform, and exposure path.
- Linear RGB DNG: copy LibRaw's full-color buffer into Stack-owned RGB/RGBA data, skip CFA/demosaic, upload as high-precision RGB, then apply baseline exposure and the available camera transform where meaningful.
- Unknown/unsupported DNG: fail gracefully with a clear RAW node status.

LibRaw remains the first decoder backend. `open_file()` and `unpack()` are the only LibRaw production decode operations; Stack does not call `dcraw_process()`. Mosaiced DNG data is read from `raw_image`. Linear/full-color DNG data is read from `color3_image`, `color4_image`, `float3_image`, or `float4_image` when LibRaw exposes those buffers.

The MotionCam research report recommends classic single-frame TIFF/DNG exports where raw Bayer frames are represented as CFA mosaic DNGs (`SamplesPerPixel = 1`) and already demosaiced/enhanced outputs use LinearRaw semantics. Stack follows that distinction by detecting the LibRaw output layout instead of assuming every `.dng` is Bayer. MotionCam metadata is device- and camera-instance-specific, so CFA pattern, black/white levels, active area, WB, and color matrices must come from the DNG/LibRaw metadata rather than hardcoded phone or app defaults.

## Metadata To Preserve

For DNG, preserve the same Stack-owned metadata as ARW:

- file path, make/model
- raw and visible dimensions
- crop/margins/active area where available
- orientation
- bit depth
- pixel layout and CFA pattern
- black and white levels
- white balance
- camera/color matrix metadata
- DNG type/status text
- Linear RGB channel count and sample format when applicable
- LibRaw warnings/errors

Color matrix handling is especially important for DNG. Stack's current camera transform is approximate and should not be presented as Lightroom/darktable-equivalent color.

## UI Behavior

The `RAW` node remains the user-facing node.

- Mosaic RAW: show normal RAW controls.
- Linear RGB DNG: show file/camera metadata, report `DNG type: Linear RGB / demosaic skipped`, keep exposure/color/tone compatibility, and disable or hide CFA/demosaic/highlight-reconstruction debug controls.
- Unsupported/unknown DNG: show a clear status error.

The File / Camera group should identify the layout as `Mosaic RAW`, `Linear RGB`, or `Unknown`.

## Backend Roadmap

```text
RawLoader
  LibRawDecoder          working backend for ARW, mosaiced DNG, and Linear RGB DNG
  NativeExperimental     future placeholder
  CompareDebug           future reference/diagnostic mode
```

Future native decoders must still output Stack-owned `RawImageData`, `RawMetadata`, and compatible decode status/warnings. UI and graph code should not depend on decoder-specific types.

## Testing Checklist

- Build normally.
- Build with `STACK_ENABLE_LIBRAW=OFF`.
- Confirm package output contains `Stack.exe`, `libraw.dll`, notices, and licenses.
- Import the known Sony ARW sample and confirm output still works.
- Import PNG/JPEG and confirm existing image import still works.
- Try a corrupt or renamed fake `.dng` and confirm a clean error.
- Try Samsung S24 DNG and MotionCam Pro DNG samples when available.
- Confirm MotionCam mosaiced DNG renders through the existing RAW mosaic controls.
- Confirm MotionCam Linear RGB DNG renders without Bayer demosaic and reports `Linear RGB / demosaic skipped`.
- Confirm Tone Curve, Tone Mapper, and Shadows / Highlights work after both DNG layouts.
- Confirm project save/load restores the DNG source path/settings and reloads the file rather than serializing large raw buffers.
- Confirm project save/load with a RAW node still works.

Do not commit large ARW/DNG samples unless Stack adopts an explicit large-binary test asset policy.
