# Building and packaging Stack

Run these commands from the repository root on Windows.

## Build

Use `.\build.cmd` for a direct full-product build. It closes running Stack workers and app instances, configures a private CMake tree in `_workspace/build-tree/stack`, builds the explicit `Stack`, `StackBurst`, and `StackBurstShell` targets with 14 parallel jobs, and places the executables at `build/Stack.exe` and `build/StackBurst.exe`. The native Explorer adapter goes to `build/Stack/App/Runtime/StackBurstShell.dll`. Ordinary builds keep the version unchanged. The script repairs common local tool-path problems. A fresh configure may need network access to fetch C++ dependencies.

To build only the Burst component, use `.\tools\build\build_stack.ps1 -Product Burst`. This selects `StackBurst`, its native shell adapter, and its declared runtime dependencies, without building the full application. Both products use the same engine executable implementation and version. `StackBurst.exe --version` reports its identity; `--self-check` checks the worker runtime. Launching StackBurst without arguments opens its file picker and compact grouping/output/progress window; `--selection <manifest>` receives the Explorer selection. Both installer profiles include the native x64 Explorer command (Windows 11: Show more options); portable ZIPs do not register it. The shell adapter is a separate thin DLL. For scripted checks, wait for this GUI executable and capture its exit code/output explicitly.

The optional graph-routing backend uses the pinned native dependency in `cmake/StackGraphRouting.cmake`. The native canvas currently draws cached endpoint-based cubic wires without submitting obstacle-routing jobs. Full Stack ships the backend's replaceable `Stack/App/Runtime/libavoid.dll`, with corresponding LGPL source and standalone rebuild instructions in `Stack/App/Legal/ThirdParty/LibavoidSource/`. Keep the runtime and source payload together when packaging. The Burst-only product does not require this DLL.

Private in-process Bracketing algorithm diagnostics require a separate CMake tree configured with `-DSTACK_BUILD_BURST_DIAGNOSTICS=ON`. Building its `Stack` target produces `diagnostics/StackBurstDiagnostics.exe` inside that tree, outside the packageable deployment. Normal build scripts explicitly disable this option. The shipping application's `--validate-burst-worker <inputs-folder> <new-output-folder>` is a bounded check of the real process client using two to eight existing RAW captures; it does not need internal algorithm linkage. Follow the repository's validation-scope agreement before running it. `Stack.exe --validate-burst-parity <inputs-folder> <standalone.dng> <new-output-folder> [standalone-report.json]` compares a known burst through the shipping client against every calibrated CFA sample in a standalone DNG. Supply the `measurement` JSON emitted by `StackBurst.exe --batch` to verify source order/settings and retain stage timing/resource evidence. It runs one complete processing job; use only within the agreed validation scope.

Burst reconstruction requires a working OpenGL compute backend. `--self-check` reports `gpuRequired`, `gpuAvailable`, `gpuCombiningAvailable` and `gpuMessage`; failure to initialize the required backend makes the check fail. A `--batch` manifest may omit `captureCombining` or set it to `"auto"`/`"gpu"`; legacy `"cpu"` requests are rejected rather than silently substituted. GPU initialization, shader, verification, resource and execution failures stop the job with diagnostics. CPU decoding, orchestration and scalar verification are still parts of the hybrid algorithm, not replacement GPU backends. Evidence-based selection of original-frame pixels for moving/unsupported regions remains distinct from backend failure. The measurement report records actual GPU tile counts and bounded GPU memory reservation. `Stack.exe --validate-burst-combining-cancellation <inputs-folder> <new-output-folder>` uses two to eight real captures and cancels through the normal client after the first combining tile completes, without running the larger worker-validation suite.

For focused Color Calibration checks, run `.\build\Stack.exe --validate-color-calibration`. Optional RAW paths add real-image checks and write comparison previews to `outputs/validation/color-calibration/`. The first RAW also checks native preview against Editor export. The command uses a hidden graphics context and does not open or edit a user project.

For RAW layer and mask checks, run `Stack.exe --validate-raw-layers <research-output-folder>` from `build/`, or supply its path from the repository root. This uses a hidden graphics context and creates a separate save/reopen project beneath the supplied folder. In PowerShell, use `Start-Process -WindowStyle Hidden -Wait -PassThru` to wait for this GUI executable and read its exit code. The focused model/history/dependency checks are in `_workspace/build-tree/stack/artifacts/StackGraphBehaviorTests.exe --raw-layers-only`. These commands do not test mouse interactions. The same layer checks cover scene-tone math and Detail Contrast settings. For a focused real-RAW tone/detail check, use `Stack.exe --validate-tone-detail <RAW-file> <research-output-folder>`. It compares source-scale detail across preview and native sizes, checks masked native preview against export, and saves two inspection PNGs.

For focused curve/tonal-slider interactions, run `Stack.exe --validate-tonal-sliders [preview.png]`. It uses a hidden native context with the production fonts/theme and submits the actual curve and slider widgets; an optional output path saves one Local EV/Tone component preview. It does not open a user project or measure image-processing performance. The `--raw-layers-only` model checks also cover owned-point restoration, amounts, undo/redo and current-format persistence.

For focused Framing graph checks, run `Stack.exe --validate-node-math-phase6 --framing-only`. This uses a hidden graphics context to check authored geometry, settings round-trip, GPU sampling against the CPU reference, and preview/export pixel agreement. It does not test mouse interactions.

Use `.\stack-tools.cmd` for the interactive build and release menu. The visible `P` product selector switches between full Stack and Burst only; build and package actions use that selection. Option 3 remains the unsigned installer and ZIP action. More tools contains checks, version editing, and full paths (the existing numeric shortcuts still work). Ordinary build actions do not show a version prompt. At a packaging or explicit version prompt, Enter keeps the current version, `N` selects the next patch, and a typed version selects that version. The shared version is stored in [StackVersion.cmake](../cmake/StackVersion.cmake).

For C-drag capture checks on an existing RAW image, run `Stack.exe --validate-raw-asset-capture <RAW-file>` from `build/`. This uses the existing RAW interaction setup and a hidden graphics context with document saving disabled. It checks stroke submission, cancellation/undo, two completed native masks with their Gaussian Blur nodes, and edits to both receiving Local EV operations. It omits the Graph backdrop checks performed by `--validate-raw-floating-tools`.

For the existing RAW graph editing checks without the separate shared-curve interaction preflight, run `Stack.exe --validate-raw-graph-canvas`. This hidden-context check disables document saving and covers connection eligibility, creation, deletion, cycle rejection, node opening, and document reload. The full `--validate-raw-graph-editing` command retains the shared-curve preflight.

## Current Burst engine changes and verification

The October 23, 2026 source update is not a published release. It changes HDR
admission and exposure fitting to use decoded layout and registered image evidence
rather than camera/exposure labels. Unresolved captures get a local-alignment retry,
then a documented exclusion instead of an invented metadata gain. It also improves
per-capture highlight-headroom weighting, registration provenance, noise-failure
reporting and cancellation. Shared-guide registration risk now transports motion
covariance into the guide coordinate system, including composed/refined motion;
unavailable derivatives retain source-native risk without rejecting the capture.
Eligible brighter repeated exposures now choose their auxiliary alignment anchor
from comparable native image structure instead of input-order median alone. The
user's output reference and capture membership stay unchanged. Weak/incomparable
evidence retains the median; this is not per-pixel multi-source guide fusion.
GPU stages now fail explicitly rather than silently switching to CPU processing. Decoder CFA, orientation and black/white calibration
are still required; missing information does not create recovered dynamic range.

New, untouched Bracketing imports can initialize their default Local EV and Tone
Curve nodes once. This is an editable starting look, not a change to reconstructed
RAW or standalone output. It does not overwrite existing edits or run again after
undo, reopening a saved project or reprocessing. Ordinary tone edits reuse the
reconstruction and request display refinement, not another capture merge.

Older saved finished results reopen as saved. **Explicitly process the bracket
again to use the updated engine**; merely opening it or changing a tone node is
not a reconstruction upgrade. Use a project copy when retaining an older result
for comparison, and keep the original RAW inputs unchanged.

### Checks completed and remaining limits

- Cloud C++17 checks passed for changed engine/editor translation units. Bounded
  executable checks passed for exposure graphs/cohorts, metadata admission, image
  search seeds, calibration fallback/publication, motion/covariance, border/cache
  behavior, noise cancellation and precedence, detail fusion and starting-look math.
  Scalar/noise AddressSanitizer and UndefinedBehaviorSanitizer runs passed; leak
  checking was unavailable in that execution environment.
- All five temporal compute shaders compiled and linked on software Mesa. Actual
  Accumulate/Finish dispatch passed 26 small arithmetic/flag cases. Later-group
  Prepare reset and failure propagation were checked separately. The actual
  temporal GPU wrapper also passed bounded multi-group success, weighted-output,
  disabled-source and late-failure publication checks. GPU executor refusal,
  cancellation and malformed-score checks passed. These are component checks,
  not full spatial-detail, Windows or hardware-driver validation.
  Independent affine-guide GPU fixtures pass at two tile sizes, including border
  and seam probes; singular/missing-guide cases retain valid source support.
  Analytic derivative tests preserve original motion fields bit-for-bit. The
  optional derivative has a measured mapping-only CPU cost; an attempted fused
  reduction that regressed existing evaluation was removed.
- Anchor-ranking checks cover blur/noise, translation/rotation, sparse clipping,
  localized foreground, unavailable evidence and transactional read/cancel errors.
  A real image-calibration/local-alignment/GPU-combining component chain improved
  synthetic strong/modest-blur error, with less averaging in the strong case;
  equal-sharpness controls were unchanged. Sparse optional exposure refits retained
  initial measured gains without new exclusions. This is not full `Process` or
  real-camera validation. Ranking adds I/O: one OS-cache-warm 24 MP, three-candidate
  fixture took about 0.72 s and read about 460 MiB of prepared tiles. Cold-disk cost
  and broad image-quality benefit remain unmeasured.
- Legacy MFD/HDR processor checks confirmed GPU failures and missing executors
  return failure without publishing a CPU replacement; cancellation stays canceled.
  RAW display preprocessing also reports GPU failure instead of CPU retry. Explicit
  CPU reference/interactive algorithms remain distinct primary paths, not recovery.
- A measured pyramid-index optimization regression was removed. Residual-buffer
  reuse reduced allocation calls in one controlled fixture, but timing overlapped.
  Redundant later-group GPU status readbacks and reference staging were removed.
  Operation-scoped exact-layout GPU buffer reuse passed fresh-data, bounded-memory,
  concurrent-worker, cancellation and cleanup-failure checks on software Mesa.
  It reduces allocation calls, not intermediate image transfers or disk caching.
  Non-detail-band sampling and accumulation now share one GPU dispatch, avoiding
  an intermediate observation-buffer write/read. Detail-band neighborhood work
  retains separate dispatches. Original/split/fused synthetic outputs matched on
  software Mesa; bounded dispatch microcases showed no clear regression, but
  exclude uploads, initialization and final readback. The added shader increases
  initialization work: a cache-disabled software-Mesa compile/link comparison
  measured about 20 ms extra (38 ms to 58 ms), excluding context creation. Hardware
  startup and full-pipeline timing remain unmeasured.
  Completed canonicalized group tiles now feed neutral-guide math immediately,
  avoiding one group-cache and one origin-cache read pass. Exact old-loop/cache
  parity and actual tile-driver cancellation/publication checks passed; single
  inputs keep their original GPU doubles. The later guide stage only finalizes
  presentation and ordered histogram reduction. `temporal_groups` now includes
  this guide computation; `group_neutral_guide_worker_seconds` records overlapping
  worker time, while `neutral_guide` measures finalization.
  Group-cache decoding no longer creates a full template Observation tile: a
  512-square, four-group check reduced tracked peak allocation by exactly 20 MiB.
  Decoded source ownership is released before noise/overview scans, and native
  output aliases no longer pin replaced buffers through later depth work.
  A noise-scratch reuse experiment was removed because paired timings showed no
  useful speed gain despite fewer allocations.
  No end-to-end speedup or real-camera image-quality improvement is established.
- Full Windows build and user-selected capture testing remain outstanding. The
  full `Process` integration check was blocked by unavailable OpenCV development
  headers; component checks do not replace it. Shared exposure-error correlations and very dark reference-guide observability remain
  modeling limitations; reported uncertainty is not a complete covariance model.

### Current memory and transfer flow

These are code-derived payload sizes, not physical-disk or GPU timing claims.
Let N be sensor pixels and G the recipe's exposure-group count; halos and repeated
passes add traffic beyond nominal full-coverage figures.

- Decoding owns sensor buffers in RAM. Prepared sample, comparison-gain and flag
  tiles occupy 9N bytes on disk, plus headers. Image-noise estimation (when used),
  overview generation and alignment pyramids read those prepared tiles separately.
- Temporal processing uploads 16-byte source taps and, when aligned, 72-byte
  per-output-pixel mappings. Each group returns 72-byte GPU observations to CPU;
  the checked 64-bit CPU Observation representation is 80 bytes. Local detail and
  later consumers still prevent a wholly GPU-resident pipeline.
- Canonical group-cache payload is 36GN bytes. A fresh multi-input run writes it
  once, builds its guide from the resident tile, and reads it for final blending.
  At 24 MP and three groups, removing the former guide reread avoids 2.592 GB of
  group payload plus 216 MB of origin payload. Header/checksum safety remains.
- Native output and measurement sidecars total 20N bytes; the cell guide adds N
  bytes. Prepared analysis also retains motion/reliability and bounded overviews.
  Replacing native output during SR no longer leaves redundant local owners alive
  into depth processing; required native data still remains alive during SR.
- GPU workspaces remain bounded and operation-scoped, not whole-frame allocations.
  CPU/GPU budgets are admission estimates, not a measured universal peak: the
  two-capture local-alignment pyramid/scratch preflight remains a known accounting
  gap near the minimum memory allowance. Full Windows/hardware profiling is pending.

### First checks after pulling and building

1. Preserve local edits, pull the latest source without overwriting them, and use
   the Windows build entry point above. Confirm the built worker with
   `build/StackBurst.exe --version` and `--self-check`.
2. Choose a small familiar set, plus a dark/noisy bracket and a highlight-heavy
   bracket. Process explicitly into new outputs. Inspect fine edges, moving
   objects, shadow noise, highlight color and any capture-exclusion diagnostics.
   These are suggested checks, not results already verified on your images.
3. For the automatic starting look, use a newly imported untouched default draft.
   Check its editable Local EV/Tone Curve settings and undo. An older project is
   expected to retain its authored look rather than automatically change.
4. Confirm that an older finished project only gains the new reconstruction after
   explicit processing. Retain the actual GPU tile-count and failure diagnostics;
   there is no production CPU fallback to hide an unavailable GPU backend.

The durable scalar, noise and exposure regressions are part of the existing
`--validate-bracketing` suite in the optional diagnostics build described above.
That command runs more than the focused cloud checks and is not available in the
normal shipping executable. Follow the agreed validation scope before running it.

## Optional AI alignment package

Stack's Bracketing reconstruction controls and the standalone Burst window expose **AI-assisted alignment (experimental)**, off by default. The standalone batch manifest accepts `"aiAlignment": true`. The saved recipe keeps the choice; AI requires Automatic local alignment and is not used for panorama. It proposes motion with SEA-RAFT-M, then keeps the shared native RAW refinement, verification and reconstruction rules. Turning it off starts no model process.

The optional package lives at `Stack/Models/SeaRaft` beside the owning `StackBurst.exe`. It is not included by ordinary product builds or release manifests. `tools/models/prepare_sea_raft.py --deployment <executable-folder> --python <python.exe>` prepares the pinned, checksum-verified code and checkpoint with notices. The explicitly supplied interpreter needs compatible PyTorch, NumPy and safetensors; the tool does not install globally. `--source-pack <folder>` reuses a retained artifact pack instead of downloading. The adapter runs offline, uses CUDA when available or CPU otherwise, and exits before native registration. A missing/incomplete package or inference failure is reported rather than silently disabling the requested setting.

A development `runtime.json` can point to an existing interpreter. A distributable optional package must supply its own compatible runtime, preserve dependency notices, and use the relative `python/python.exe` location or another package-relative interpreter path. Copying a development manifest with an absolute interpreter path does not create a portable package. Both hosts resolve the same package relative to their own worker; no research-folder path is compiled into the application. Packaging/clean-machine runtime validation is separate from a developer build.

## Packages

Menu option 3 creates an unsigned installer and portable ZIP for the selected product. It checks the embedded versions of the product's executables and required third-party notices and licenses. Signed public packaging additionally requires code signing, final hashes and the explicit matching-build/source review described in [the source-release checklist](../legal/SOURCE_RELEASE.md). Use the direct release command with `-PublicRelease -ReviewedBuildCommit <full SHA> -CorrespondingSourceReviewed` after that review; the menu does not supply those attestations automatically. Option 5 creates an isolated, unsigned local test installer. Stack's custom draft agreement and acceptance plumbing are archived under `legal/archive/stack-agreement/`; they are not part of current binary packages or a release-approval gate. The source-code license remains at the repository root. Installer creation requires Inno Setup 6. Direct equivalents are `.\tools\release\create_release.ps1 -Product Stack` and `.\tools\release\create_release.ps1 -Product Burst`; omitting `-Product` preserves the full-Stack default. `package_release.ps1` remains a compatible forwarding entry point. Add `-SkipInstaller` for a ZIP-only run or `-SkipBuild` to package already-built binaries whose embedded versions match the requested release. Matching version numbers alone do not prove matching source. Standard/public packaging now requires a clean Git checkout and Python 3.9+, captures the exact source revision before the build and checks it again afterward. Commit intended version/input changes before packaging. `-LocalTestPackage` preserves dirty-worktree development packaging and is not distributable.

Current packages go to `outputs/releases/current/Stack/` and `outputs/releases/current/Burst/`. A run archives only that product's preceding output under `outputs/releases/archive/<product>/`; local test outputs and their archives are similarly isolated. A custom `-OutputDir` is the parent of these product subfolders. Full installers are named `StackSetup-<tag>-win-x64.exe`; Burst installers are named `StackBurstSetup-<tag>-win-x64.exe`. Each product has its own ZIP, SBOM, and `<artifact-name>-SHA256SUMS.txt`. Standard/public outputs also contain `<artifact-name>-source/` with the tracked-source ZIP, provenance, dependency evidence and a separate checksum list, plus a `<artifact-name>-<tag>-source-bundle.zip` included in the main release checksums. Distribute that source-bundle ZIP with the matching binaries, plus any additional corresponding source required by review.

Both products retain dependency license texts, the Adobe DNG notice and a matching LibRaw source ZIP. Installers show no Stack agreement or disclosure page, and silent installation does not require `/ACCEPTEULA`. Full Stack starts without an agreement gate; Settings > Legal opens the retained third-party documents. On upgrade the installer removes only the four obsolete agreement documents from `Stack/App/Legal`; existing user preferences and historical acceptance records are not used or rewritten. A normal incremental build likewise removes only those obsolete documents from the developer payload, and package validation rejects them if stale files are found.

[products.json](../tools/release/products.json) declares the common burst payload, build targets, signing ownership, shell CLSIDs, and each product's required files. Full Stack includes `Stack.exe`, `StackBurst.exe`, and its application resources. Burst only includes `StackBurst.exe`, the shell adapter, its runtime and notices, and a capability README. The installers have separate identities and folders, and reject an overlapping product folder. If both products are installed, each owns a private copy of the same engine binary so updates and uninstall do not remove another installation's files. The application always uses its own bundled worker. Neither installer implicitly owns the other product's writable data. `finalize_runtime_layout.ps1` remains a developer migration cleanup tool for a full deployment containing existing user state; package validation uses the product manifest and does not require developer settings or caches.

Both installers automatically register the native x64 Explorer command; there is no optional integration checkbox. On Windows 11 it appears under **Show more options**; modern top-level integration needs a signed app-identity package and is not supplied. ARM64 installations can use the x64 standalone window through supported emulation, but do not register this x64 in-process DLL in native ARM64 Explorer. ZIP extraction does not register shell integration. [BurstShellRegistration.iss](../installer/BurstShellRegistration.iss) owns one shared visible verb and distinct installation CLSIDs. It prefers standalone Burst, then full Stack, then their local-test variants; install/uninstall recomputes the provider without sharing uninstall-owned files. A common installation mutex prevents concurrent family installers from racing the registration. Locked shell DLL replacement/removal may require a restart.

The adapter only transports the complete selected path array to its own sibling engine. The UTF-8 selection contract is `{"schemaVersion":1,"files":["absolute path", "..."]}`, limited to 100,000 files and 16 MiB, in `%TEMP%/StackBurst/Selections/<GUID>/selection.json`. The GUI reads and validates it, then removes only this owned manifest and empty directory. Menu construction does no file enumeration or processing; decoding, grouping and output stay outside Explorer. The standalone window supplies editable DNG and finished TIFF/JPEG/PNG output beside originals or in a new subfolder.

## First-run settings and reinstall comparisons

Portable and installed Full Stack use the same executable and first-run profile. `src/App/settings/AppearanceTheme.cpp` owns `MakeFirstRunAppearanceLibrary`; `src/Raw/RawViewportSettings.h` and `RawViewportPreferences` own image-preview defaults. Package staging excludes developer user data. Put shared product defaults in those owners, rather than copying a developer settings file into an installer or maintaining a second installer-only profile.

Portable preferences live at `Stack/User/Settings` beside the executable. Installed preferences live at `%APPDATA%/Stack/User/Settings`; caches use `%LOCALAPPDATA%/Stack`. An upgrade or reinstall normally preserves these preferences, so an existing selection can differ from a changed first-run default. Back up and change only the relevant preference files when the user requests a reset/comparison; do not remove their library, project, palette or background data to test defaults.

Within an agreed validation scope, `Stack.exe --validate-appearance-settings` checks the shared profile and appearance persistence/migrations. `Stack.exe --validate-settings-defaults <new-output-folder>` exercises actual appearance and viewport settings file I/O through both runtime layouts beneath that isolated folder. Run it again with `--reload` in a new process to verify the saved files. It does not modify the user's settings, install the program, or establish rendered appearance. `StackAppPathsTests` covers portable/installed directory and migration contracts.

## Source assets

The [repository layout](REPOSITORY.md) lists the source, tool and local-output folders. Generated splash, icon and composite-font headers are ignored; their source assets and bake scripts belong in Git. The program icon source is `Assets/Icons/Stack.png`. The whole `website/` folder is local and is not required to build the app.

CMake runs the asset bake scripts as part of the app build. Raster UI icons are read from `Assets/Icons/` by [bake_tab_icons.py](../tools/assets/bake_tab_icons.py) and embedded in generated C++ headers; the packaging script does not copy that source folder. The icon baker needs Python with Pillow installed. When moving source assets, update their bake-script paths before building. The navigation rail's vector glyphs are drawn by [NavigationGlyphs.cpp](../src/App/NavigationGlyphs.cpp) and compile directly into the app without a raster bake.
