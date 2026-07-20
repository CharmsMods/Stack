# Stack Installer, Updater, and Release Packaging

## Source Layout

- Stack proprietary license file: [LICENSE](/D:/Program%20Development/Stack/LICENSE)
- App version source of truth: [cmake/StackVersion.cmake](/D:/Program%20Development/Stack/cmake/StackVersion.cmake)
- Generated runtime version header: `build*/generated/src/App/AppVersion.h`
- Install-mode and user-data paths: [src/App/AppPaths.cpp](/D:/Program%20Development/Stack/src/App/AppPaths.cpp)
- In-app updater logic: [src/App/UpdateManager.cpp](/D:/Program%20Development/Stack/src/App/UpdateManager.cpp)
- URL and installer launch helpers: [src/App/PlatformHelpers.cpp](/D:/Program%20Development/Stack/src/App/PlatformHelpers.cpp)
- Settings UI surface: [src/App/AppSettingsPopup.cpp](/D:/Program%20Development/Stack/src/App/AppSettingsPopup.cpp)
- Windows installer script: [installer/StackInstaller.iss](/D:/Program%20Development/Stack/installer/StackInstaller.iss)
- Interactive menu launcher: [stack-tools.cmd](/D:/Program%20Development/Stack/stack-tools.cmd)
- Build script: [tools/build_stack.ps1](/D:/Program%20Development/Stack/tools/build_stack.ps1)
- Release packaging script: [tools/create_release.ps1](/D:/Program%20Development/Stack/tools/create_release.ps1)
- Compatibility wrapper: [tools/package_release.ps1](/D:/Program%20Development/Stack/tools/package_release.ps1)

## Versioning

`cmake/StackVersion.cmake` defines:

- `STACK_VERSION_MAJOR`
- `STACK_VERSION_MINOR`
- `STACK_VERSION_PATCH`
- `STACK_VERSION`
- `STACK_VERSION_TAG`
- `STACK_PUBLISHER`

Update this file first when preparing a release.

If the running app shows `1.0.0`, it is because `cmake/StackVersion.cmake` still says `1.0.0`.

Recommended release habit:

- bump the version in `cmake/StackVersion.cmake` before you build release artifacts
- use patch versions like `1.0.1` for smaller fixes
- use minor versions like `1.1.0` for larger feature releases
- make the GitHub Release tag and uploaded asset filenames match that same version

If you use [stack-tools.cmd](/D:/Program%20Development/Stack/stack-tools.cmd), the menu shows the current version, suggests the next patch version automatically, and lets you:

- press Enter to use the suggested next patch version
- type `K` to keep the current version
- type a full version like `1.1.0` to override it

## Build and Package a Release

1. If you want the simplest path, open the menu:

```powershell
.\stack-tools.cmd
```

1. Or build Stack directly:

```powershell
.\build.cmd
```

1. Package the release artifacts directly:

```powershell
.\tools\create_release.ps1
```

1. If Inno Setup 6 is not installed locally, you can still build the portable ZIP and hashes:

```powershell
.\tools\create_release.ps1 -SkipInstaller
```

Default outputs now go to `outputs/releases/current/`.

Stack's release flow now assumes:

- Stack itself is proprietary software under the top-level `LICENSE`
- third-party components remain under their own licenses
- release packaging should include the third-party notices and shipped license files

## Expected Release Assets

The packaging flow is built around these release filenames:

- `StackSetup-vX.Y.Z-win-x64.exe`
- `Stack-vX.Y.Z-win-x64.zip`
- `SHA256SUMS.txt`

The staging folder before zipping is `outputs/releases/current/Stack-vX.Y.Z-win-x64/`.

Each new package run archives the previous contents of `outputs/releases/current/` into `outputs/releases/archive/` before writing the next release.

## What the Packaging Script Includes

The portable ZIP and installer stage include:

- `Stack.exe`
- `libraw.dll` when present in the build output
- `Microsoft.WindowsAppRuntime.Bootstrap.dll` when the Windows App SDK titlebar bridge is present in the build output
- `LICENSE`
- `THIRD_PARTY_NOTICES.md`
- the `licenses/` folder copied from the build output

The installer also adds `StackInstalledBuild.marker` so the app can distinguish installed builds from portable ZIP runs.

## Installer Behavior

The Inno Setup script is designed for:

- all-users install under `C:\Program Files\Stack`
- upgrade-in-place via a stable `AppId`
- Start Menu and desktop shortcuts enabled by default
- launch-after-install enabled by default
- uninstall entry in Windows Installed Apps
- default uninstall behavior that preserves user data
- optional uninstall removal of known Stack data in `%AppData%\Stack` and `%LocalAppData%\Stack`

The installer uses a generated license/notice page file built from the top-level Stack `LICENSE` file plus `THIRD_PARTY_NOTICES.md`.

## User Data and Migration

Installed builds now use:

- `%AppData%\Stack` for settings, presets, and the Stack library
- `%LocalAppData%\Stack` for logs, cache, and downloaded updates

Portable builds keep using the executable folder.

On installed builds, startup performs a conservative one-way migration by copying legacy `StackSettings.json`, `Library/`, logs, cached updates, and managed background image files from the executable folder into the new user-data locations if those files already exist there. The old files are not deleted automatically.

## Updater Behavior

The updater:

- queries `https://api.github.com/repos/CharmsMods/Stack/releases/latest`
- ignores drafts and prereleases
- parses semantic versions with or without a leading `v`
- compares versions semantically, not lexically
- prefers installer assets named like `StackSetup-vX.Y.Z-win-x64.exe`
- never uses GitHub source archives as app updates
- downloads into `%LocalAppData%\Stack\Updates` for installed builds
- launches the downloaded installer with elevation instead of trying to overwrite the running EXE

Portable builds stay conservative. They can detect releases and use the installer flow, but they do not overwrite the portable folder in place.

## Verification

`SHA256SUMS.txt` is a checksum list for the release files. It records the SHA-256 hash for each packaged asset so anyone can verify that the downloaded file matches the one you published.

This is a normal, professional thing to publish with release assets. It does not expose secrets. It only contains hashes and filenames for the files you already chose to publish publicly.

For Stack, this file is useful for two reasons:

- people can manually verify release downloads if needed
- the in-app updater can use it to verify that the downloaded installer matches the published hash when GitHub does not provide a usable asset digest

Recommended release habit:

- upload `SHA256SUMS.txt` to the GitHub Release alongside the installer and portable ZIP
- keep the filenames inside `SHA256SUMS.txt` matching the uploaded asset filenames exactly

The updater verifies downloads when either of these is available:

- a release-asset `digest` field
- a `SHA256SUMS.txt` style asset that includes the installer filename

If neither is published, the updater still downloads over HTTPS but reports that verification was unavailable instead of pretending the file was verified.

## Website Download Behavior

[website/index.html](/D:/Program%20Development/Stack/website/index.html) now prefers installer assets for the primary download button and keeps the portable ZIP as a secondary link.

The asset selection logic looks at real uploaded release assets and prefers:

1. `StackSetup-vX.Y.Z-win-x64.exe`
2. `Stack-vX.Y.Z-win-x64.zip`

## Publish Checklist

1. Update `cmake/StackVersion.cmake`.
1. Build Stack.
1. Run `.\tools\create_release.ps1`.
1. Create a normal GitHub Release with tag `vX.Y.Z`.
1. Upload:
   - `StackSetup-vX.Y.Z-win-x64.exe`
   - `Stack-vX.Y.Z-win-x64.zip`
   - `SHA256SUMS.txt`
1. Publish the release as a normal release, not a prerelease.

## Current Limitations

- Inno Setup 6 is required locally to compile the installer.
- The app and installer are not code signed yet, so Windows may show `Unknown Publisher`.
- Silent updater execution is not implemented; the installer UI is shown during updates.
