# Native RAW UI Smoke Checklist

This checklist is the required native UI gate for RAW Workspace work that cannot be honestly verified by model tests alone. Use it when a pass says "native smoke", "real RAW visual smoke", or "hands-on RAW UI smoke" is still pending.

It is not a replacement for automated tests. Run the model/build validations first, then use this checklist against the real app.

## Scope

This checklist currently closes the remaining native verification gaps for:

- Phase 6 active RAW preview/render behavior, including Scenario 1 startup/loading/source-switch coverage that is also required before Phase 8A Local Range can be called natively verified.
- Phase 7 managed decomposition, repair, re-adopt, detach, and custom graph transitions.
- Phase 8A Local Range native behavior: graph-first Local Range UI, trust overlays, target tool, edge-aware map quality, region masks, presets, legacy compatibility display, and Finish Tone/View Transform independence.

Do not use this checklist as permission to add broader Phase 8 batch workflow features. It is a verification pass for already implemented behavior.

## App Under Test

Use the official local build path:

```text
D:\Program Development\Stack\build\Stack.exe
```

If the executable was rebuilt or differs from the build recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`, rerun automated preflight and update the results file before launching interactive smoke. The interactive scenario rows must describe the same build they are testing.

## Preconditions

- Build the current app and graph tests before native smoke.
- Use a real RAW Workspace folder with several RAW files.
- Include at least:
  - one untouched RAW with no `.stack` project,
  - one recipe-backed RAW project,
  - one project that has been decomposed to managed nodes,
  - one intentionally customized graph project if testing reload ownership,
  - one high-dynamic-range image with bright highlights and dark foreground/shadow detail.
- Record the workspace folder and RAW filenames used.
- If the smoke fails, preserve the failing `.stack` project and capture the exact steps.

## Automated Preflight

Run the relevant subset before native UI smoke:

```text
cmake --build build --config Debug --target StackGraphBehaviorTests --parallel 1
build\StackGraphBehaviorTests.exe
cmake --build build --config Debug --target Stack --parallel 1
build\Stack.exe --validate-layer-registry
build\Stack.exe --validate-develop-node-smoke
build\Stack.exe --validate-raw-workspace-loading-smoke "<workspace-folder>" --expect-min-sources 1
```

Optional, when representative RAW paths are available:

```text
build\Stack.exe --validate-develop-real-raw-smoke "<raw-file-1>" "<raw-file-2>"
```

Passing these commands does not complete native smoke by itself; they only prove the non-interactive paths are healthy enough to test manually.

## Evidence Template

Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`, then summarize the latest state in `../CURRENT_HANDOFF.md`. Use this shape:

```text
Native RAW UI smoke date:
Build path:
Build timestamp:
Workspace folder:
RAW files used:
GPU / display scale, if relevant:

Scenario results:
- Startup/loading:
- Active preview:
- Phase 7 managed decomposition:
- Phase 7 warning/custom-mode transitions:
- Phase 7 repair/re-adopt/detach:
- Save/reopen ownership:
- Local Range presets and legacy compatibility:
- Local Range target tool:
- Local Range overlays:
- Local Range region masks:
- Finish Tone / View Transform independence:
- Responsiveness/full-resolution settle:

Failures or screenshots/logs:
Residual risks:
```

## Scenario 1 - Startup, Loading, And Active Preview

1. Launch `D:\Program Development\Stack\build\Stack.exe`.
2. Open the RAW tab.
3. If a previous workspace auto-restores, wait for scan/status work to settle.
4. If no workspace restores, open a real RAW folder.
5. Select an untouched RAW.
6. Wait for the active render to settle.
7. Switch between at least three RAW files, including one with an existing project.
8. Return to the first RAW.

Pass criteria:

- The UI remains responsive during scan, project discovery, thumbnail work, project load, and active preview render.
- The main preview does not get stuck on a neutral thumbnail when the active RAW should render.
- The active preview has correct orientation after the first render without needing a dummy edit.
- Capped preview renders settle into a full-resolution render after interaction quiets.
- Switching sources clears cross-image stale previews while preserving same-source stale tiles during edits.
- No red notification appears unless a real recoverable failure occurred.
- No black/white character texture or placeholder garbage flashes in the viewport.

## Scenario 2 - Phase 7 Managed Decomposition

1. Select a recipe-backed RAW project with representable settings.
2. Use `Decompose To Nodes`.
3. Open the project in the graph.
4. Verify the managed section contains the expected RAW chain:
   - `RAW Source`
   - `RAW Decode`
   - `Tone Curve`
   - `View Transform`
5. Return to the RAW tab.
6. Edit representable RAW controls, such as exposure, white balance mode/multipliers, rotation, Finish Tone, and supported View Transform fields.
7. Return to the graph and verify the managed nodes reflect the edits.
8. Edit supported managed node parameters in the graph.
9. Return to the RAW tab and verify the recipe controls reflect those graph edits.

Pass criteria:

- Decomposition is explicit and does not silently drop active recipe fields.
- The RAW tab remains editable while the managed graph is structurally valid.
- Supported RAW tab edits and managed graph edits round-trip without requiring a restart.
- Active preview rendering still follows fast-then-full behavior after decomposition.

## Scenario 3 - Graph-Breaking Warning And Custom Mode

Run each mutation from an active managed decomposed project:

1. Attempt to remove a required managed-chain link.
2. Attempt to delete a required managed-chain node.
3. Attempt to connect sockets in a way that would change the managed chain ownership.
4. Exercise the mutation through mouse interaction, context menu actions, and keyboard deletion where available.

For each mutation:

1. Confirm the warning appears before the graph changes.
2. Choose Cancel and verify the graph is unchanged.
3. Repeat, choose Continue, and verify the project enters `Custom Graph Mode`.

Pass criteria:

- The popup is visible, focused enough to operate, and understandable.
- Cancel leaves the graph and RAW ownership unchanged.
- Continue performs the requested graph mutation and makes RAW tab editing read-only for that image.
- The RAW tab shows the customized-chain message:

```text
This RAW chain has been customized in the graph.
RAW tab editing is read-only for this image until the chain is repaired or re-adopted.
```

- No arbitrary graph mutation remains silently RAW-tab editable.

## Scenario 4 - Repair, Re-Adopt, Detach

1. From a managed decomposed project, remove only a required link while leaving owned nodes and settings intact.
2. Run `Repair RAW Chain`.
3. Verify the missing-link repair succeeds and the project returns to valid managed ownership.
4. Create a custom internal edit that repair should not bypass, such as inserting an unsupported node in the managed section.
5. Run `Repair RAW Chain`.
6. Verify repair refuses or leaves the project custom/read-only.
7. Restore or create a valid graph-first RAW chain and run `Re-adopt Graph As RAW Recipe`.
8. Verify valid adoption returns to managed RAW ownership.
9. Run `Detach From RAW Tab`.

Pass criteria:

- Repair only fixes the narrow missing-link case.
- Repair does not recreate missing nodes, rewrite metadata, remove custom nodes, or bypass unsupported internal graph edits.
- Re-adopt succeeds only for a valid managed-compatible chain.
- Detach intentionally transfers ownership to the graph and leaves RAW tab editing read-only.

## Scenario 5 - Save, Reopen, And Ownership Reload

Create or reuse one project in each mode:

- `RecipeBacked`
- `ManagedDecomposed`
- `CustomGraph`

For each:

1. Save explicitly.
2. Switch to another RAW and back.
3. Leave and return to the RAW tab.
4. Close and relaunch Stack.
5. Reopen the workspace.
6. Select the project again.

Pass criteria:

- Recipe-backed projects reopen editable in the RAW tab.
- Managed decomposed projects preserve managed graph metadata and remain editable only while validation passes.
- Custom graph projects reopen read-only in the RAW tab with the customized-chain message.
- Existing project selection does not synchronously freeze the UI.
- No stale project-load result overwrites a newer selection.

## Scenario 6 - Local Range Presets And Legacy Compatibility

1. Select a new/default recipe-backed RAW.
2. Open the `Local Range` section.
3. Verify the legacy `Local Exposure` slider drawer is not shown as the primary editing surface.
4. Apply `Open Shadows`.
5. Apply `Hold Highlights`.
6. Apply `Compress Range`.
7. Apply `Reset`.
8. Save, switch sources, return, and reopen.
9. If a project with legacy `localExposure` state is available, select it and verify the compatibility drawer appears with `Convert To Local Range`.
10. Use `Convert To Local Range`, save, and reopen.

Pass criteria:

- New/default projects use the graph-first Local Range workflow.
- Presets write normal graph points and leave the graph editable.
- Reset returns Local Range to disabled identity behavior.
- Legacy Local Exposure remains readable/renderable but does not compete with Local Range for new projects.
- Conversion is explicit and does not silently change old project output before the user asks.

## Scenario 7 - Local Range Target Tool

Use a high-dynamic-range RAW:

1. Set RAW Exposure so important highlights retain headroom.
2. Enable `Target From Image`.
3. Click a dark foreground/shadow area and drag upward.
4. Verify a Local Range point appears near the sampled scene EV and the dark area lifts.
5. Click a bright sky/cloud area and drag downward.
6. Verify a Local Range point appears near the sampled scene EV and the bright area compresses.
7. Disable targeting and continue editing the graph directly.

Pass criteria:

- The target marker appears on the Local Range graph for the sampled tone.
- Drag-up on a dark patch lifts that tone zone before Finish Tone.
- Drag-down on a bright patch compresses that tone zone without globally dimming unrelated midtones by the same amount.
- The interaction uses capped previews while dragging and settles to uncapped full render after quiet.
- The tool does not feel like hidden automatic look generation; it creates normal editable graph points.

## Scenario 8 - Local Range Overlays And Region Masks

1. Enable `Affected Tones` overlay.
2. Enable `EV Delta Map` overlay.
3. Enable `Region Mask` overlay.
4. Switch sources while an overlay is visible.
5. Return to the original source.
6. Test each V1 region mask type:
   - linear gradient,
   - radial gradient,
   - luminance range.
7. Adjust mask center/angle/size/feather or EV range while Local Range has visible non-zero points.
8. Toggle mask inversion.

Pass criteria:

- Overlays are preview-only and do not dirty or alter the saved recipe by themselves.
- Overlay textures are accepted only for the current source/mode/render generation.
- Region masks visibly gate Local Range rather than changing the global Local Range graph.
- Linear/radial/luminance masks compile and render on real RAW files.
- No obvious halos appear around dark/bright edges at normal settings.
- Fine texture does not become flattened or smeared by the edge-aware map.

## Scenario 9 - Finish Tone And View Transform Independence

1. Use Local Range to bring a dark and bright area closer together.
2. Adjust Finish Tone in `RGB` + `Log Scene`.
3. Adjust View Transform rolloff and contrast, or the equivalent output/display controls if older copy is still visible.
4. Toggle Local Range off and on to compare.

Pass criteria:

- Finish Tone remains the main photographic look control.
- Local Range behaves like pre-finish tonal balancing, not a second competing final tone curve.
- View Transform still owns display rolloff and saturation/contrast taste.

## Failure Handling

If any scenario fails:

- Do not mark Phase 7 complete.
- Do not mark the relevant Phase 8A pass natively verified.
- Record the exact action, selected RAW, project mode, visible status text, and any notification text.
- Preserve the failing `.stack` file if the failure involves project ownership, graph validation, save/reload, or Local Range persistence.
- Prefer a small targeted fix over broad changes to renderer ownership, graph ownership, or project persistence.

## Completion Rule

Phase 7 may be marked complete only after Scenario 2 through Scenario 5 pass on a real RAW Workspace.

Phase 8A Local Range may be called natively verified only after Scenario 1 and Scenario 6 through Scenario 9 pass on real RAW files.

Record the completed scenario rows in `../NATIVE_RAW_UI_SMOKE_RESULTS.md` before changing phase status in `../CURRENT_HANDOFF.md`.
