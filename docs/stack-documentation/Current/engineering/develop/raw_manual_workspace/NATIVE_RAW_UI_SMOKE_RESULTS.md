# Native RAW UI Smoke Results

This file records evidence for `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md`.

Use it as the durable run log for native/manual RAW Workspace smoke results. The checklist defines what must be tested; this file records what actually happened.

## Current Run

Date: 2026-06-29

Status:

- Automated preflight: Passed.
- Interactive native scenarios: Pending.
- Phase 7 completion: Not proven.
- Phase 8A Local Range native verification: Not proven.

Build under test:

- Executable: `D:\Program Development\Stack\build\Stack.exe`
- Rebuilt timestamp recorded by handoff: 2026-06-29 6:58:03 AM
- Executable size recorded by handoff: 21611008 bytes

Workspace under test:

- Path: `C:\Users\djhbi\Downloads\all in extract\all copied images`
- Source count from loading smoke: 178
- Groups from loading smoke: 1
- Neutral thumbnails: 178 valid, 0 queued, 0 failed
- Existing projects from loading smoke: 18
- Optional real RAW smoke files:
  - `DSC00122.ARW`
  - `DSC00123.ARW`

## Interactive Run Sheet

Before running the interactive scenarios:

- Confirm `D:\Program Development\Stack\build\Stack.exe` still has the expected timestamp and size. If the executable differs, treat the automated preflight evidence below as stale: rebuild as needed, rerun automated preflight, and replace the `Build under test` plus `Automated Preflight Evidence` sections before recording interactive rows.
- Use the workspace above unless deliberately testing another real RAW workspace.
- Record the RAW filenames used for each scenario.
- Use disposable or copied `.stack` projects for graph-breaking scenarios when possible, especially Scenario 3 and Scenario 4.
- Do not mark a scenario `Passed` from memory. Record the observed behavior in the row or in a dated note below the table.
- If a scenario cannot be run because the needed project type is missing, mark it `Blocked` and state the missing prerequisite.

Suggested execution order:

1. Scenario 1 first, because it verifies startup, loading, active preview, and source switching.
2. Scenario 2, then save the managed-decomposed project state for Scenario 3 through Scenario 5.
3. Scenario 3 and Scenario 4 on disposable managed graph projects, because they intentionally break graph ownership.
4. Scenario 5 after at least one recipe-backed, managed-decomposed, and custom graph project exists.
5. Scenario 6 through Scenario 9 after Scenario 1 passes for the same build/workspace and the active preview/render path is stable.

Minimum evidence per scenario:

| Scenario | Required Evidence |
| --- | --- |
| Scenario 1 | RAW filenames selected, whether restored/opened workspace loaded without freezing, preview orientation/quality result, any notification text. |
| Scenario 2 | Source RAW, project path if known, managed node chain observed, RAW-tab-to-graph edit result, graph-to-RAW-tab edit result. |
| Scenario 3 | Mutation type, whether warning appeared before mutation, Cancel result, Continue result, final project mode/read-only state. |
| Scenario 4 | Broken-link repair result, unsupported custom edit repair refusal result, re-adopt result, detach result. |
| Scenario 5 | One RAW/project path per ownership mode, save/switch/tab/relaunch result for each, final mode after reopen. |
| Scenario 6 | New/default project result, preset results, reset result, legacy drawer/conversion result if a legacy project is available. |
| Scenario 7 | High-dynamic-range RAW filename, dark target result, bright target result, graph marker behavior, full-resolution settle result. |
| Scenario 8 | Overlay modes tested, region mask types tested, source-switch behavior, halo/texture observations. |
| Scenario 9 | Local Range setup, Finish Tone changes, View Transform changes, Local Range toggle comparison. |

## Automated Preflight Evidence

Recorded from the 2026-06-29 native-smoke gate preflight rerun.

| Check | Result | Evidence |
| --- | --- | --- |
| Build graph behavior tests | Passed | `cmake --build build --config Debug --target StackGraphBehaviorTests --parallel 1` |
| Run graph behavior tests | Passed | `build\StackGraphBehaviorTests.exe` returned `Stack graph behavior tests passed.` |
| Build app | Passed | `cmake --build build --config Debug --target Stack --parallel 1` produced `D:\Program Development\Stack\build\Stack.exe` |
| Layer registry validation | Passed | `build\Stack.exe --validate-layer-registry` returned `LayerRegistry validation passed.` |
| Develop node smoke | Passed | `build\Stack.exe --validate-develop-node-smoke` returned `Develop node smoke validation passed.` |
| RAW Workspace loading smoke | Passed | `build\Stack.exe --validate-raw-workspace-loading-smoke "C:\Users\djhbi\Downloads\all in extract\all copied images" --expect-min-sources 1` returned 178 sources, 1 group, 178 valid thumbnails, 0 queued, 0 failed, and 18 existing projects. |
| Optional real RAW smoke | Passed | `build\Stack.exe --validate-develop-real-raw-smoke "C:\Users\djhbi\Downloads\all in extract\all copied images\DSC00122.ARW" "C:\Users\djhbi\Downloads\all in extract\all copied images\DSC00123.ARW"` returned `Develop real RAW smoke validation passed.` |

## Interactive Scenario Results

Use these statuses:

- `Pending`: not run yet.
- `Passed`: run on native app and met checklist criteria.
- `Failed`: run on native app and failed; record exact steps and evidence.
- `Blocked`: could not run because a prerequisite was missing.

| Scenario | Status | Evidence / Notes |
| --- | --- | --- |
| Scenario 1 - Startup, Loading, And Active Preview | Pending | Required for Phase 6 active-preview follow-up and Phase 8A Local Range native verification. |
| Scenario 2 - Phase 7 Managed Decomposition | Pending | Required before Phase 7 can be marked complete. |
| Scenario 3 - Graph-Breaking Warning And Custom Mode | Pending | Required before Phase 7 can be marked complete. |
| Scenario 4 - Repair, Re-Adopt, Detach | Pending | Required before Phase 7 can be marked complete. |
| Scenario 5 - Save, Reopen, And Ownership Reload | Pending | Required before Phase 7 can be marked complete. |
| Scenario 6 - Local Range Presets And Legacy Compatibility | Pending | Required before Phase 8A Local Range can be called natively verified. |
| Scenario 7 - Local Range Target Tool | Pending | Required before Phase 8A Local Range can be called natively verified. |
| Scenario 8 - Local Range Overlays And Region Masks | Pending | Required before Phase 8A Local Range can be called natively verified. |
| Scenario 9 - Finish Tone And View Transform Independence | Pending | Required before Phase 8A Local Range can be called natively verified. |

## Failure Log

No interactive failures have been recorded yet because the interactive scenarios have not been executed.

When a failure occurs, add a dated entry with:

- scenario number,
- selected RAW filename,
- project mode,
- exact interaction,
- visible notification or status text,
- whether the app froze, crashed, or recovered,
- screenshot/log path if available,
- preserved `.stack` project path if ownership or persistence is involved.

## Completion Notes

Phase 7 can be marked complete only after Scenario 2 through Scenario 5 pass on a real RAW Workspace.

Phase 8A Local Range can be called natively verified only after Scenario 1 and Scenario 6 through Scenario 9 pass on real RAW files.

Until those interactive rows are filled with passing evidence, the native UI smoke gate is still open.
