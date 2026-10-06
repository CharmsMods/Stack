# Repository layout

Keep the root for the CMake entry point, command launchers, Git configuration and public repository documents. Build commands and package paths are in [BUILDING.md](BUILDING.md).

| Folder | Contents |
| --- | --- |
| `src/` | Application code, shaders and third-party source dependencies |
| `Assets/` | Source fonts, icons and splash images |
| `cmake/` | Source lists, version information and focused build targets |
| `tools/assets/` | Scripts that bake source assets into embedded headers |
| `tools/build/` | Build scripts |
| `tools/dev/` | Build menu, environment helpers and development maintenance |
| `tools/release/` | Packaging, compliance and runtime staging scripts |
| `tools/tests/` | Focused C++ checks and their shared harness |
| `tools/tests/fixtures/` | Existing development image generators |
| `tools/restormer/` | Optional model-service source and export tools |
| `installer/` | Installer source and configuration |
| `legal/` | Legal source documents, dependency licenses and release manifests required by the build |
| `website/` | Local website source and images, ignored by Git |
| `Guidance/` | Current repository instructions |
| `RESEARCH/` | Local topic-specific designs, findings and prior work records, ignored by Git |

## Generated and local files

`.gitignore` excludes the runnable `build/` directory, private `_workspace/` build trees, `outputs/` packages and validation output, `_tmp/` scratch work, and `_local_archive/` local archives. Put new local scratch files in those existing folders rather than the repository root. Optional training and LUT workspaces retain their specific ignore rules.

The splash, tab-icon, RAW-sidebar-icon and composite-font headers are generated and ignored. The normal build recreates them before compiling Stack. Keep their source assets and bake scripts in Git. `Embedded8BitFont.h` remains a source dependency because the build has no generator for it.

The whole `RESEARCH/` and `website/` folders stay local. This includes downloaded papers, validation results and website drafts. The app's source icon is `Assets/Icons/Stack.png`; its bake script and source-image loading path do not require `website/`.

Original RAW inputs in the local `bike test set/` and `denoise sets/` directories stay where they are. Those directories and project backup files are ignored. Follow the original-source safeguards in [Implementation/README.md](Implementation/README.md) before changing any image-data location.

Ignore generated files by their output directory or exact name. Avoid blanket rules for `.txt` or `.cmake`, which can hide required CMake and legal sources. Check a new rule against both its intended output and a source file that must remain visible to Git.
