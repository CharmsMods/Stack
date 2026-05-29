# RAW LibRaw Production Audit

## Current Status

- LibRaw version: `0.22.1`
- LibRaw commit: `b860248a89d9082b8e0a1e202e516f46af9adb29`
- Build mode: dynamically linked `libraw.dll` when `STACK_ENABLE_LIBRAW=ON`
- Stack-facing RAW API: `RawLoader`, `RawImageData`, `RawMetadata`, and `RawDevelopSettings`
- Direct LibRaw include location: `src/Raw/LibRawDecoder.cpp`

LibRaw is used for `open_file()`, `unpack()`, raw buffer access, and metadata extraction. Stack copies the unpacked mosaic data into `RawImageData` and performs RAW development in `RawGpuPipeline`.

## Boundary Check

- UI, graph, and GPU pipeline code do not include LibRaw headers directly.
- Production Stack code does not call `dcraw_process()`.
- The RAW path does not bake a CPU-side final RGB bitmap before GPU processing.
- `RawLoader` is a stable facade; `LibRawDecoder` is the only working backend in this pass.
- RawSpeed, Adobe DNG SDK, and other optional LibRaw accelerators/extra decoders are not enabled.

## Build And Package Layout

Expected Windows development/package output:

```text
Stack/
  Stack.exe
  libraw.dll
  THIRD_PARTY_NOTICES.md
  licenses/
    LibRaw-LGPL-2.1.txt
    LibRaw-CDDL-1.0.txt
    LibRaw-COPYRIGHT.txt
```

CMake copies the notices and license files after building `Stack.exe`. `libraw.dll` is produced by the `stack_libraw` shared target in the runtime output directory.

`STACK_ENABLE_LIBRAW=OFF` should still configure and build. In that mode RAW import routing is hidden and `RawLoader` reports LibRaw support as unavailable.

## Licensing Notes

LibRaw is dual licensed under LGPL 2.1 or CDDL 1.0. Stack's current engineering plan is to ship LibRaw as a separate DLL with copied license/copyright notices. This is not legal advice and does not remove the need for final legal review before public or commercial distribution.

## Cleanup Still Needed Before Public Release

- Verify the exact DLL and license payload in the final installer/package job.
- Decide whether to use upstream LibRaw build artifacts or this local CMake shared target for releases.
- Add automated package checks for `libraw.dll`, notices, and license files.
- Add real ARW/DNG sample validation outside the git repo.
- Revisit color management before claiming profile-quality RAW output.
