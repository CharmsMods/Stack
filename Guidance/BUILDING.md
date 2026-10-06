# Building and packaging Stack

Run these commands from the repository root on Windows.

## Build

Use `.\build.cmd` for a direct build. It closes a running Stack instance, configures a private CMake tree in `_workspace/build-tree/stack`, builds with 14 parallel jobs, and places the runnable app at `build/Stack.exe`. The script repairs common local tool-path problems. A fresh configure may need network access to fetch C++ dependencies.

For focused Color Calibration checks, run `.\build\Stack.exe --validate-color-calibration`. Optional RAW paths add real-image checks and write comparison previews to `outputs/validation/color-calibration/`. The first RAW also checks native preview against Editor export. The command uses a hidden graphics context and does not open or edit a user project.

For RAW layer and mask checks, run `Stack.exe --validate-raw-layers <research-output-folder>` from `build/`, or supply its path from the repository root. This uses a hidden graphics context and creates a separate save/reopen project beneath the supplied folder. In PowerShell, use `Start-Process -WindowStyle Hidden -Wait -PassThru` to wait for this GUI executable and read its exit code. The focused model/history/dependency checks are in `_workspace/build-tree/stack/artifacts/StackGraphBehaviorTests.exe --raw-layers-only`. These commands do not test mouse interactions. The same layer checks cover scene-tone math and Detail Contrast settings. For a focused real-RAW tone/detail check, use `Stack.exe --validate-tone-detail <RAW-file> <research-output-folder>`. It compares source-scale detail across preview and native sizes, checks masked native preview against export, and saves two inspection PNGs.

Use `.\stack-tools.cmd` for the interactive build and release menu. Option 12 shows the full build and output paths. **At its version prompt, Enter accepts the suggested next patch version even for an ordinary build; type `K` to keep the current version.** The version is stored in [StackVersion.cmake](../cmake/StackVersion.cmake).

## Packages

Menu option 3 creates an unsigned installer and portable ZIP. It checks the legal-file hashes, but can package draft legal text, so do not publish it while the legal approval state is `draft`. Option 4 requires approved legal files, code signing, and final hashes for a public release. Option 5 creates an isolated, unsigned local test installer. Installer creation requires Inno Setup 6. To create a portable package without an installer, use `.\tools\release\create_release.ps1 -SkipInstaller`.

Current packages go to `outputs/releases/current/`; the prior current package is archived under `outputs/releases/archive/`. Local test packages go to `outputs/releases/local-test/`.

## Source assets

The [repository layout](REPOSITORY.md) lists the source, tool and local-output folders. Generated splash, icon and composite-font headers are ignored; their source assets and bake scripts belong in Git. The program icon source is `Assets/Icons/Stack.png`. The whole `website/` folder is local and is not required to build the app.

CMake runs the asset bake scripts as part of the app build. Raster UI icons are read from `Assets/Icons/` by [bake_tab_icons.py](../tools/assets/bake_tab_icons.py) and embedded in generated C++ headers; the packaging script does not copy that source folder. The icon baker needs Python with Pillow installed. When moving source assets, update their bake-script paths before building. The navigation rail's vector glyphs are drawn by [NavigationGlyphs.cpp](../src/App/NavigationGlyphs.cpp) and compile directly into the app without a raster bake.
