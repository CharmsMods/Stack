# Third Party Notices

Stack is proprietary software. The third-party components listed below remain
licensed by their original authors under their own separate license terms.

## Dear ImGui

- Component: Dear ImGui
- Upstream: https://github.com/ocornut/imgui
- License: MIT
- Copyright: Copyright (c) 2014-2026 Omar Cornut

Stack statically links Dear ImGui. The Dear ImGui license text should be
distributed with Stack release artifacts.

## GLFW

- Component: GLFW
- Upstream: https://www.glfw.org/
- License: zlib/libpng
- Copyright: Copyright (c) 2002-2006 Marcus Geelnard; Copyright (c) 2006-2019 Camilla Lowy

Stack links GLFW as part of its application runtime. The GLFW license text
should be distributed with Stack release artifacts.

## LibRaw

- Component: LibRaw
- Version: `0.22.1`
- Git tag: `0.22.1`
- Git commit: `b860248a89d9082b8e0a1e202e516f46af9adb29`
- Upstream: https://github.com/LibRaw/LibRaw
- Copyright: Copyright (C) 2008-2026 LibRaw LLC
- Stack distribution path: GNU LGPL 2.1 option
- Alternate upstream option: CDDL 1.0

Stack uses LibRaw as an isolated, dynamically linked RAW decoder backend for file reading, unpacking, and metadata extraction. Stack's main RAW rendering path performs RAW development in Stack GPU code and does not use `dcraw_process()` as the final image processor.

License texts that should be distributed with Stack release artifacts:

- LGPL 2.1: include `LICENSE.LGPL` from the LibRaw source distribution, or another unmodified copy of the GNU Lesser General Public License version 2.1.

LibRaw may include additional upstream notices in its source distribution. Release packaging should preserve and ship the license and notice files included with the exact pinned LibRaw source.

Distribution assumption: Stack's current Windows production-readiness layout ships `libraw.dll` beside `Stack.exe`, plus this notices file and the LibRaw license/copyright files. This is a practical engineering note, not legal advice or legal certainty. Final legal review is still needed before public or commercial distribution.

Optional acceleration/extra decoder note: RawSpeed, Adobe DNG SDK, and other optional LibRaw-adjacent acceleration/extra decoder components are not enabled in Stack RAW V1. Do not enable them without separately reviewing and documenting their licenses and notice requirements.
