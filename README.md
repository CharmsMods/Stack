
# Stack

If you just want the simplest way to build or package Stack on Windows, run:

```powershell
.\stack-tools.cmd
```

That opens a plain-text menu for the common tasks:

- build the app
- build and launch the app
- package a full release
- package a release without the installer
- validate the current build
- open the current build or release folders
- archive extra old build folders

Official day-to-day paths:

- app build: `build\Stack.exe`
- current packaged release: `outputs\releases\current\`
- older packaged releases: `outputs\releases\archive\`

Root-level files that are meant to be used directly:

- `stack-tools.cmd` for the interactive build/package menu
- `build.cmd` for a direct app build
- `dev-shell.cmd` for a repaired developer PowerShell session
- `BUILDING.md` for build and packaging instructions
- `LICENSE` for the Stack proprietary license
- `THIRD_PARTY_NOTICES.md` for third-party license notices

Everything else in the repo root should be treated as internal project files,
generated files, or local-only data unless documentation explicitly says
otherwise.

More detail is in [BUILDING.md](/D:/Program%20Development/Stack/BUILDING.md).
