# Stack

Stack is a C++ desktop image and compositing workspace built with CMake, OpenGL, GLFW, and Dear ImGui. It includes an editor with a custom node graph, image/layer processing tools, a library workspace, and rendering/composite experiments that are being folded into the main app.

## What Is In This Repo

- `src/` - application source code.
- `Assets/` - source assets baked into generated headers during the build.
- `Icons/` - source tab icon images baked into `src/App/Resources/EmbeddedTabIcons.h`.
- `tools/` - asset bake scripts, the release build helper, and local environment helpers.
- `CMakeLists.txt` - main CMake project.
- `build.cmd` - recommended Windows build entrypoint.
- `BUILDING.md` - detailed Windows build notes.
- `NODE_GRAPH_INTERACTION_GUIDE.md` - current guidance for the custom Dear ImGui node graph.

Local app data, build outputs, old planning notes, and migration scratch files are intentionally ignored.

## Prerequisites

On Windows, install:

- Visual Studio 2022 Build Tools or Visual Studio with C++ desktop development support.
- CMake 3.20 or newer.
- Python 3.
- Git.

CMake fetches GLFW and Dear ImGui during configure/build. Network access is needed the first time dependencies are populated unless the build directory already has them cached.

## Build

From the repo root:

```powershell
.\build.cmd
```

The script repairs common local `PATH` issues, closes a running `Stack.exe` if needed, and builds the Release target.

Expected output:

```text
build_codex\bin\Release\Stack.exe
build_codex\portable\Stack.exe
```

You can also use standard CMake commands from a healthy developer shell:

```powershell
cmake -S . -B build_codex
cmake --build build_codex --config Release
```

## Generated Files

Some headers are generated from source assets:

- `src/App/Resources/EmbeddedSplash.h` from `Assets/Splash/splash_banner.png`
- `src/App/Resources/EmbeddedTabIcons.h` from `Icons/*.png`
- `src/Composite/EmbeddedCompositeFont.h` from `Assets/Fonts/Roboto-Medium.ttf`
- `build_codex/generated/src/RenderTab/Shaders/EmbeddedShaders.h` from shader files

The generated headers that live under `src/` are committed when the source includes require them. Build-directory generated files remain ignored.

## Repo Hygiene

The root should stay focused on source, build, and current documentation. Old plans, research notes, local experiments, app library data, build outputs, and logs belong outside Git or in the ignored `_local_archive/` folder.

Before pushing, a useful sanity check is:

```powershell
git status --short
```

Expected visible changes should be source code, current docs, build scripts, source assets, and required generated headers. You should not see build folders, `Library/`, `imgui.ini`, logs, or old planning folders.
