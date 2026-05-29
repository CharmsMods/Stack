# Third Party Notices

## LibRaw

- Component: LibRaw
- Version: `0.22.1`
- Git tag: `0.22.1`
- Git commit: `b860248a89d9082b8e0a1e202e516f46af9adb29`
- Upstream: https://github.com/LibRaw/LibRaw
- Copyright: Copyright (C) 2008-2026 LibRaw LLC
- License options: GNU LGPL 2.1 or CDDL 1.0

Stack uses LibRaw as an isolated, dynamically linked RAW decoder backend for file reading, unpacking, and metadata extraction. Stack's main RAW rendering path performs RAW development in Stack GPU code and does not use `dcraw_process()` as the final image processor.

License texts should be distributed with release artifacts:

- LGPL 2.1: include `LICENSE.LGPL` from the LibRaw source distribution, or another unmodified copy of the GNU Lesser General Public License version 2.1.
- CDDL 1.0: include `LICENSE.CDDL` from the LibRaw source distribution, or another unmodified copy of the Common Development and Distribution License version 1.0.

LibRaw may include additional upstream notices in its source distribution. Release packaging should preserve and ship the license and notice files included with the exact pinned LibRaw source.

Distribution assumption: Stack's current Windows production-readiness layout ships `libraw.dll` beside `Stack.exe`, plus this notices file and the LibRaw license/copyright files. This is a practical engineering note, not legal advice or legal certainty. Final legal review is still needed before public or commercial distribution.

Optional acceleration/extra decoder note: RawSpeed, Adobe DNG SDK, and other optional LibRaw-adjacent acceleration/extra decoder components are not enabled in Stack RAW V1. Do not enable them without separately reviewing and documenting their licenses and notice requirements.
