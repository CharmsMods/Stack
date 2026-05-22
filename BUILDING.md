# Building Stack

This repo is a CMake-based C++ desktop app. The easiest supported path today is the Windows helper script in the repo root.

## Prerequisites

Install:

- Visual Studio 2022 Build Tools or Visual Studio with C++ desktop development support.
- CMake 3.20 or newer.
- Python 3.
- Git.

The first configure/build may need network access because CMake uses `FetchContent` for GLFW and Dear ImGui.

## Recommended Windows Build

From the repo root:

```powershell
.\build.cmd
```

The script:

- closes a running `Stack.exe` so linking can succeed
- repairs common local `PATH` issues before invoking tools
- configures `build` if needed
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

`build.cmd` delegates the configure/build work to `tools\build_release.ps1`.

Source assets live in `Assets/`, `Icons/`, and shader folders under `src/RenderTab/Shaders/`.

Generated files under build directories are ignored. Generated headers under `src/` are committed when source files include them directly.

## Common Build Issues

- If linking fails with `LNK1104` for `Stack.exe`, close the running app and run `.\build.cmd` again.
- If `cmake` or `python` is not found, use `.\build.cmd` or `.\dev-shell.cmd`.
- If dependency fetch fails on a fresh machine, confirm Git and network access are available.
