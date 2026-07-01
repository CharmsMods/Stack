# Pass 11 Validation Notes

Date: 2026-06-30

## Automated Evidence

The following checks were run after the Pass 10 density polish and before closing the first RAW tab UI overhaul implementation set:

- `.\tools\build_stack.ps1 -Root 'D:\Program Development\Stack' -BuildDir 'D:\Program Development\Stack\build'`
  - Passed. Produced `D:\Program Development\Stack\build\Stack.exe` and `D:\Program Development\Stack\build\StackGraphBehaviorTests.exe`.
- `.\build\StackGraphBehaviorTests.exe`
  - Passed with `Stack graph behavior tests passed.`
- `.\build\Stack.exe --validate-layer-registry`
  - Passed with `LayerRegistry validation passed.`
- `.\build\Stack.exe --validate-develop-node-smoke`
  - Passed with `Develop node smoke validation passed.`
- `.\build\Stack.exe --validate-raw-workspace-loading-smoke 'C:\Users\djhbi\Downloads\all in extract\all copied images' --expect-min-sources 1`
  - Passed with 178 sources, 1 group, 178 valid thumbnails, 0 queued thumbnails, 0 failed thumbnails, and 29 existing projects.
- `.\build\Stack.exe --validate-develop-real-raw-smoke 'C:\Users\djhbi\Downloads\all in extract\all copied images\DSC00122.ARW' 'C:\Users\djhbi\Downloads\all in extract\all copied images\DSC00123.ARW'`
  - Passed for both real RAW files with stable repeated solves.

## Manual Visual Validation Status

The native Dear ImGui RAW workspace does not currently have a repo-local screenshot or UI automation harness for the manual visual states required by `implementation-contract.md`. The app builds and the available command-line validation suite passes, but the following checks still need hands-on verification in the native app before the UI can be called fully visually validated:

- narrow, normal, and wide window views
- long RAW filenames and long workspace paths
- no selected RAW empty state
- new/default RAW with Auto Base
- existing saved recipe
- read-only/error/custom graph state
- right gallery expanded/collapsed
- bottom filmstrip mode
- active Local Range, Color Target, and Finish Tone
- suggestions available, no suggestions, and warnings/advisories
- Diagnostics drawer collapsed/opened/from warning link
- center view modes and disabled unavailable modes

Use the existing local RAW workspace from the automated smoke test when running the manual pass:

```text
C:\Users\djhbi\Downloads\all in extract\all copied images
```

## Known Deferred Behavior

Passes 00 through 10 kept behavior changes scoped to the first layout overhaul. These items remain intentionally deferred:

- Center `Compare` mode is not implemented yet. The toolbar exposes `Final`, `Affected`, `Delta`, and `Mask`; unavailable risk overlay remains disabled with a tooltip.
- Highlight Risk remains a Diagnostics item until there is a viewport overlay backend for it.
- Suggestions support click-to-apply from the RAW top expander, but hover preview, pinned compare, and batch suggestion review remain deferred.
- Pass 10 used compact text/ellipsis affordances rather than adding a new icon font or command icon asset system. Structural, destructive, and ambiguous project actions remain textual.

These limitations are accepted for the first UI structure pass because adding preview-state compare, highlight-risk overlays, hover preview tokens, batch review, or a new shared icon system would expand the scope beyond the layout and clarity goals of this implementation set.
