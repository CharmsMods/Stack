# Building Stack

This repo is a CMake-based C++ desktop app. The easiest supported path on Windows is the menu launcher in the repo root.

## Prerequisites

Install:

- Visual Studio 2022 Build Tools or Visual Studio with C++ desktop development support.
- CMake 3.20 or newer.
- Python 3.
- Git.

The first configure/build may need network access because CMake uses `FetchContent` for GLFW and Dear ImGui.

## Easiest Windows Workflow

From the repo root:

```powershell
.\stack-tools.cmd
```

That opens a simple text menu for:

- building Stack
- building and launching Stack
- packaging a full release
- packaging a release without the installer
- validating the current build
- opening the important folders
- archiving extra old `build_*` folders

Before build and packaging actions, the menu now shows the current version from [cmake/StackVersion.cmake](/D:/Program%20Development/Stack/cmake/StackVersion.cmake), suggests the next patch version automatically, and lets you:

- press Enter to use the suggested next patch version
- type `K` to keep the current version
- type a full version like `1.1.0` to override it

Official locations used by that workflow:

- app build: `build\Stack.exe`
- current packaged release: `outputs\releases\current\`
- older packaged releases: `outputs\releases\archive\`

## Direct Windows Build

From the repo root:

```powershell
.\build.cmd
```

The build script:

- closes a running `Stack.exe` so linking can succeed
- repairs common local `PATH` issues before invoking tools
- configures `build` if needed
- enables the Windows App SDK titlebar bridge on Windows when the local package is available
- builds the Release target in `build`

Expected output:

```text
build\Stack.exe
```

## Standard CMake Build

From a healthy developer shell where `cmake` and `python` are on `PATH`:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

On Windows, `STACK_ENABLE_APPWINDOW_TITLEBAR` defaults to `ON`. CMake searches
for the local Microsoft.WindowsAppSDK package under `_workspace\deps`, the
`STACK_WINDOWS_APP_SDK_ROOT` cache/env value, and the user's NuGet cache. If the
package is not available, Stack builds with the safe native titlebar fallback.

The window chrome decision and failure history are documented in
[docs/engineering/WINDOW_CHROME_POSTMORTEM.md](/D:/Program%20Development/Stack/docs/engineering/WINDOW_CHROME_POSTMORTEM.md).

## Release Packaging

The clearer direct script names are:

```powershell
.\tools\build_stack.ps1
.\tools\create_release.ps1
```

The older compatibility entry points still work:

```powershell
.\tools\package_release.ps1
```

Useful options:

- `-BuildDir <path>` to package an existing build folder
- `-SkipBuild` to reuse an already-built `Stack.exe`
- `-SkipInstaller` to package only the portable ZIP and hashes when Inno Setup is not installed locally

Installer builds require Inno Setup 6 (`ISCC.exe`) on Windows.

Stack is now packaged as proprietary software. The release flow expects a real
top-level [LICENSE](/D:/Program%20Development/Stack/LICENSE) file to exist, and
the packaging step will stop if that file is missing.

Default packaged release output now goes to:

```text
outputs\releases\current\
```

Before a new package run writes there, the previous packaged release is moved into:

```text
outputs\releases\archive\
```

## Version Numbers

Stack's version number comes from [cmake/StackVersion.cmake](/D:/Program%20Development/Stack/cmake/StackVersion.cmake).

If the app still says `1.0.0`, that means this file still says `1.0.0`.

Typical workflow:

- change `STACK_VERSION_MAJOR`, `STACK_VERSION_MINOR`, and `STACK_VERSION_PATCH`
- rebuild the app
- package the release
- upload the matching installer and ZIP to the GitHub Release for that same version

If you use `.\stack-tools.cmd`, it prompts for the version before build and packaging actions so you do not have to edit the file manually every time.

Simple rule of thumb:

- use `1.0.1`, `1.0.2`, and so on for small fixes
- use `1.1.0`, `1.2.0`, and so on for bigger feature releases
- do not package or publish a release until the version file matches the version you want users to see

Release packaging details, updater behavior, asset naming, and publish steps are documented in [docs/INSTALLER_UPDATER_RELEASES.md](/D:/Program%20Development/Stack/docs/INSTALLER_UPDATER_RELEASES.md).

## Developer Shell Helper

If a terminal launches with a broken or empty `PATH`, use:

```powershell
.\dev-shell.cmd
```

That opens PowerShell with the process environment repaired by `tools\use_fixed_env.ps1`.

## Generated Assets

The build runs asset bake scripts automatically:

- `tools\bake_splash.py`
- `tools\bake_tab_icons.py`
- `tools\bake_fonts.py`
- `tools\bake_shaders.py`

`build.cmd` delegates the configure/build work to `tools\build_stack.ps1`.

Source assets live in `Assets/`, `Icons/`, and shader folders under `src/RenderTab/Shaders/`.

Generated files under build directories are ignored. Generated headers under `src/` are committed when source files include them directly.

## Common Build Issues

- If linking fails with `LNK1104` for `Stack.exe`, close the running app and run `.\build.cmd` again.
- If you are not sure which script to use, start with `.\stack-tools.cmd`.
- If `cmake` or `python` is not found, use `.\stack-tools.cmd`, `.\build.cmd`, or `.\dev-shell.cmd`.
- If dependency fetch fails on a fresh machine, confirm Git and network access are available.
