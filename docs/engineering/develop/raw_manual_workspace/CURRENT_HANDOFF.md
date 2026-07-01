# RAW Workspace Current Handoff

Last updated: 2026-06-29

This file is the mutable resume point for implementation, validation, and documentation agents. Use it to understand where the last pass stopped, then verify against the codebase and recorded evidence before acting.

## Cold Resume Rule

If you are starting in a new conversation with no prior context:

1. Read `README.md`.
2. Read this file.
3. Read `RAW_MANUAL_WORKSPACE_PLAN.md`.
4. Read `implementation_phases/00_IMPLEMENTATION_GUIDE.md`.
5. Read `implementation_phases/00_MODE_CONTRACT.md` and `implementation_phases/00_STORAGE_LAYOUT.md`.
6. If the active task is native/manual RAW UI smoke, read `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` and `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
7. Verify the active phase, validation task, or documentation task against the codebase and recorded evidence.
8. Read the active phase document, task document, or validation protocol completely.
9. Do only that phase's owned scope, or do only the requested validation/documentation task when the handoff identifies a task as the gate.

This file is guidance, not proof. The codebase, recorded evidence, phase `Done When` sections, and active task protocol decide what is actually complete.

## Current Resume State

Current recommended active task: Execute `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 for Phase 7 and Scenario 1 plus Scenario 6 through Scenario 9 for Phase 8A against the native app, record results in `NATIVE_RAW_UI_SMOKE_RESULTS.md`, then update this handoff before marking Phase 7 complete or calling the Phase 8A Local Range branch natively verified. Automated preflight for that checklist was rerun and passed on 2026-06-29. Do not proceed to broader RAW Manual Workspace Phase 8 work automatically.

Reason:

- Phase 1 folder/catalog foundation is complete in code and validated.
- The RAW tab now has folder-backed Workspace root state, recursive source records, managed folders, catalog skeletons, recent Workspace persistence, and preview-only selection.
- Phase 2 thumbnail pipeline is complete in code and validated.
- Phase 3 gallery and Library integration is complete in code and validated.
- Phase 4 recipe and compact-node foundation is complete in code and validated.
- Phase 5 per-image project lifecycle is complete in code and validated.
- Phase 6 dedicated RAW workspace panels are implemented and audit-hardened in code.
- The Phase 6 audit fixed metadata round-tripping, unsupported mode handling, first-edit graph staging, RAW-tab focus, recipe preview metadata caching, and the close-time smoke path.
- The older Phase 6 selected-project preview gap has been superseded by later stabilization: selecting a RAW now queues active preview staging, existing stored projects load through the deferred project-load path, and preview-only selections stage a neutral same-source RAW preview. Browser/gallery tiles can still use neutral thumbnails/placeholders until later edited/after thumbnail work.
- The user explicitly moved to Phase 7 before later preview stabilization; current selected-source preview behavior should be verified through native/manual smoke rather than treated as the old non-active preview limitation.
- Phase 7 V1 is implemented in code and validated at the model/build level.
- Branch-off usability passes stabilized the active RAW tab edit/render loop: selecting a RAW now stages a real active RAW render graph immediately, RAW recipe controls no longer synchronously save on every slider tick, the RAW tab pumps editor renders while visible, active RAW previews draw only explicit same-source render outputs, same-source live previews stay visible while new renders are pending, cross-source stale render results are ignored, capped drag previews are followed by an uncapped current-source render, the compact RAW recipe tone curve now affects rendering, and RAW project saves now happen at lifecycle boundaries instead of idle/per-control saves.
- Branch-off loading/crash stabilization is implemented through the current loading and RAW-tab debug passes: scan classify/discover work moved off the UI thread and now cooperatively cancels when superseded, thumbnail status apply callbacks are batched and now apply by source index, catalog and app-state writes are asynchronous/coalesced/debounced with superseding revision fences and temp-file commits plus compact catalog snapshots and a synchronous latest-state flush during editor shutdown, thumbnail PNG decode/upload no longer happens inline in tile draw, thumbnail texture deletion is amortized per frame during runtime cache invalidation, non-explicit RAW project saves use a compact no-render/no-PNG/no-node-thumbnail packaging path and write on a dedicated close-drained save worker, RAW project save jobs now use per-source revisions and serialized project-file writes so stale lifecycle saves cannot clean newer edits or overwrite later explicit saves, explicit single-project Save/Relink/Embed refresh only the affected source instead of rediscovering every project, RAW scan/project-load/save busy overlays are visible in the RAW tab and Library RAW view, existing RAW project selection/Open In Graph now run through the deferred project-load path instead of synchronously blocking selection, normal RAW project selection no longer reads/decodes stored source PNGs or node-browser thumbnail blobs, preview-only/default graph staging is queued out of the click handler and now applies through the deferred project-apply pump instead of direct live graph mutation, startup RAW Workspace restore no longer probes the last workspace folder on the UI thread before queueing the scan, async scan apply now preserves the user's latest current selection when possible instead of snapping back to the selection captured at scan start, capped RAW preview renders now use renderer-owned downsampled RAW proxy data instead of always uploading/prepping the full RAW input, the RAW proxy builder is factored into a CPU-testable helper with coverage for capped mosaic/linear proxy sizing and cache identity, thumbnail RAW decode now passes cancellation into `RawLoader`/`LibRawDecoder` and can stop between LibRaw stages plus large copy/stat passes, thumbnail PNG decode workers now observe a bulk reset generation before decode, after decode, and before posting upload work so folder switch/clear can abandon obsolete thumbnail texture decode work earlier, stored RAW project load generation is now atomic and worker loads check cancellation before/after project file read and before posting apply work, batched thumbnail updates now bump the catalog persistence generation only when source thumbnail state actually changes so in-flight stale catalog writes cannot be accepted as current, stored-project controls no longer synchronously check project file mtimes during draw, RAW tab/Library gallery presentation is cached and invalidated on workspace/source/selection/project/thumbnail changes, RAW tab and Library RAW galleries now cull off-screen tiles before requesting thumbnail textures, thumbnail texture upload work uses an explicit pending queue instead of rescanning the whole thumbnail cache, abandoned viewport render tiles are released through a per-frame GL delete queue, abandoned single-texture render outputs now use the same fence-polled deferred release path, RAW tab render-result adoption no longer runs after the preview panel has queued ImGui draw commands, first edits on stored projects request the async project-load path instead of blocking, RAW decode/develop stage fingerprints now include preview cap so capped previews cannot satisfy full-resolution renders, LibRaw runtime status is preloaded during editor initialization, RAW GPU fullscreen geometry is now per-pipeline/context instead of function-static, RAW graph caches now own cloned textures instead of aliasing RAW GPU/stage-cache outputs, incomplete superseded tiled renders are released on the worker before crossing to the UI thread, active preview texture clearing defers GL deletion past the current ImGui frame, RAW source file stat data participates in renderer cache identity, invalid/non-finite tone-curve points are sanitized, deferred project apply no longer half-commits active RAW state on failure, and RAW-tab state pumping is restored pre-draw with a per-frame guard so RAW renders advance while the RAW tab is visible without duplicate same-frame adoption.
- Latest startup crash diagnosis found deferred RAW project apply entering node-browser thumbnail seed generation with a tiny placeholder source-pixel buffer and full image dimensions. The thumbnail resize/encode helpers now validate byte counts, normalize 1/2/3/4-channel inputs to RGBA, and fall back to blank/fallback thumbnail seeds instead of reading past the buffer.
- Latest broad RAW audit tightened RAW render/cache identity and lifecycle edges: RAW GPU corrected/linear upload cache keys now include render-affecting metadata/settings, render-graph RAW source fingerprints now include crop/layout/color/gain-map metadata, DNG gain-map RAWs bypass downsampled preview proxies that would strip correction data, small RAW develop stage cache capacity was raised to 8 entries to preserve same-source pre-finish reuse under stricter metadata identity, managed RAW validation now rejects source path/relative-key/fingerprint drift, and failed async RAW save jobs restore the pre-save source project metadata before marking the source dirty.
- Branch-off local exposure V1 is implemented for recipe-backed RAW Workspace projects as compatibility/supporting state: recipes still serialize a default-disabled `localExposure` block, compact RAW rendering can preserve active legacy Local Exposure before Local Range and Finish Tone/View Transform, but the later Local Range pass made graph-first `Local Range` the primary local balancing UI for new/default projects. Managed decomposition explicitly rejects active local-exposure recipes until an exact managed stage mapping exists.
- Branch-off graph-quality finish is implemented for compact recipe-backed RAW Workspace projects: schema v3 introduced serialized `finishTone` and `viewTransform` layer state, legacy v1/v2 tone curves migrate into the finish stack, and the RAW tab exposes `Finish Tone` and `View Transform` sections with labeled helper controls. At schema v3 time, compact finish rendering ran RAW develop/decode -> legacy `localExposure` when active -> `ToneCurveLayer` finish tone -> `ViewTransformLayer`; after Local Range, the current compact order is RAW develop/decode -> legacy `localExposure` when active -> Local Range -> `ToneCurveLayer` finish tone -> `ViewTransformLayer`. Managed decomposition/validation now round-trips Tone Curve/View Transform layer state, while active/non-default Local Range or legacy `localExposure` still blocks managed decomposition until exact stage mapping exists. The current recipe schema is v5 after Local Range Pass 7.
- Local Range/tone equalizer implementation is documented in `implementation_phases/08A_local_range_tone_equalizer.md`: the compact order is RAW Decode/WB/RAW Exposure -> Local Range -> Finish Tone -> View Transform, with completed passes for contract/recipe, renderer math, graph UI, trust overlays, image target tool, edge-aware map quality, manual region masks, and compatibility/deprecation.
- Local Range Pass 1 is implemented at the recipe/identity layer: `RawDevelopmentRecipe` now writes schema v4 with a default-disabled `localRange` block, sanitizes/clamps Local Range fields and EV points, keeps legacy `localExposure` readable, normalizes stage order with `local-range` before finish tone, exposes Local Range default/sanitize/equality/hash helpers, includes `localRange` in compact RAW render identity through recipe serialization, and rejects active/non-default Local Range recipes from managed graph decomposition until a managed stage mapping exists.
- Local Range Pass 2 is implemented in the compact RAW renderer: recipe helpers now evaluate scene EV and local EV deltas, synthetic tests cover predictable shadow lift/highlight compression/default no-op behavior, and compact recipe-backed RAW rendering applies a fullscreen Local Range pass after legacy Local Exposure and before Finish Tone/View Transform. Pass 2 originally used direct per-pixel scene-EV mapping; edge-aware smoothing, overlays, target sampling, and masks now belong to the implemented Pass 4 through Pass 7 scopes below.
- Local Range Pass 3 is implemented in the RAW tab UI: the main local-range editor is now a graph-first `Local Range` section backed by `RawDevelopmentRecipe.localRange`, with add/drag/delete/reset EV points, axis labels, selected point readout, labeled plain controls, and the old `localExposure` sliders moved to a collapsed legacy compatibility drawer.
- Local Range Pass 4 is implemented for trust overlays: the RAW tab now has transient preview-only `Trust Overlay` modes for `Affected Tones` and `EV Delta Map`, renderer-side overlay artifacts are generated from the pre-finish texture immediately before Local Range is applied, positive EV/lift zones draw teal, negative EV/compression zones draw pink/red, overlay textures are source/mode/render-generation checked before drawing, and turning overlays off clears the overlay without dirtying the recipe or altering export output. Scenario 8 native/manual RAW UI smoke is still required; the optional before/after split remains deferred.
- Local Range Pass 5 is implemented for image targeting: the RAW tab now has a transient `Target From Image` mode, accepted live RAW previews can be clicked to request a small robust pre-finish scene-EV patch sample from the texture immediately before Local Range is applied, the graph draws a marker for the sampled EV, and click-drag up/down writes a normal Local Range point at that EV through the same recipe/render path as direct graph edits. Scenario 7 native/manual RAW UI smoke is still required.
- Local Range Pass 6 is implemented for edge-aware map quality: compact RAW Local Range rendering now evaluates the graph from a broader edge-aware log-luma scene-EV map, `Smoothness`/`Edge Protection`/`Detail Protection`/`Highlight Protection` affect the output and trust overlays, and synthetic CPU coverage checks cross-edge protection plus reduced texture-driven EV variation. Scenario 7 and Scenario 8 native/manual RAW UI smoke is still required.
- Local Range Pass 7 is implemented for manual region masks: schema v5 persists optional Local Range region-mask state, compact RAW rendering gates the Local Range EV delta with linear-gradient/radial-gradient/luminance-range masks, the RAW tab exposes labeled Region Mask controls plus a preview-only `Region Mask` overlay, and synthetic coverage verifies mask round-trip, sanitization, render-identity participation, and mask math. Brush painting and edge-refinement masks were not implemented in this V1.
- Local Range Pass 8 is implemented for compatibility, presets, and deprecation: old `localExposure` remains readable/renderable for existing projects but is hidden for untouched/default projects, old projects with legacy state get a compatibility drawer and explicit `Convert To Local Range` action, the RAW tab now exposes graph-backed `Open Shadows`/`Hold Highlights`/`Compress Range`/`Reset` Local Range presets, and synthetic coverage verifies preset/conversion helpers.
- Phase 7 is not complete yet only because `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 with a real RAW Workspace are still needed. Flexible reordering is intentionally disabled and accepted for V1, repair is intentionally limited to missing-link mechanical repair rather than broad node/metadata/topology repair, project reload preservation has dedicated file-level regression coverage for recipe-backed/managed-decomposed/custom graph ownership, graph-breaking pre-confirmation warning boundaries have model coverage and editor popup wiring, and V1 reorder rejection has explicit regression coverage.
- Native/manual RAW UI smoke now has a durable protocol in `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md`. Use it for Phase 7 Scenario 2 through Scenario 5 and Phase 8A Local Range Scenario 1 plus Scenario 6 through Scenario 9.
- Automated checklist preflight was rerun and passed on 2026-06-29 against `D:\Program Development\Stack\build\Stack.exe` rebuilt at 6:58:03 AM and the real RAW workspace `C:\Users\djhbi\Downloads\all in extract\all copied images`. The workspace loading smoke found 178 RAW sources, 1 group, 178 valid thumbnails, 0 queued thumbnails, 0 failed thumbnails, and 18 existing projects. Native interactive checklist scenarios have not been executed yet.
- Native/manual RAW UI smoke evidence is tracked in `NATIVE_RAW_UI_SMOKE_RESULTS.md`. It currently records automated preflight as passed and all interactive scenarios as pending.
- Phase 8A Local Range docs have been cleaned up so they no longer instruct agents to start at Pass 1. They now state that Passes 1-8 are implemented, the current recipe schema is version `5`, and the next Local Range work is running `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 plus Scenario 6 through Scenario 9 and recording results in `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- The README and implementation guide now route native/manual RAW UI smoke through `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` plus `NATIVE_RAW_UI_SMOKE_RESULTS.md`; final responses may summarize native smoke, but the results log is the durable evidence store.
- The durable `RAW_MANUAL_WORKSPACE_PLAN.md` has been refreshed so its current-status, checklist, completed-work, and validation sections align with the current handoff instead of the early scaffold state.
- Phase 6, Phase 7, Phase 8, and Phase 8A docs now consistently point native/manual RAW UI smoke evidence to `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- The mode/storage contracts now document current V1 constraints: no reorderable managed-stage slots yet, graph-quality Finish Tone/View Transform can round-trip through managed nodes, active/non-default Local Range or active legacy `localExposure` recipes block managed decomposition until exact round-trip mapping exists, recipe schema v5 owns Local Range region-mask fields, explicit conversion owns legacy `localExposure` migration, and project files remain authority for RAW recipe schema/mode state.
- `NATIVE_RAW_UI_SMOKE_RESULTS.md` now includes an interactive run sheet with suggested scenario order and minimum evidence required per scenario.
- Phase 1-5 docs now include current notes for later UI replacement, thumbnail/gallery evolution, project-status authority, recipe schema v5 evolution, and active RAW lifecycle-save behavior.

When resuming Phase 7 code work or native/manual smoke, verify the V1 implementation against the codebase, especially:

- `src/Raw/RawWorkspaceManagedGraph.*`
- `src/Editor/Internal/EditorModuleRawManagedGraph.cpp`
- project save/load hooks in `EditorModuleProjectLifecycle.cpp` and `EditorModulePersistence.cpp`
- mutation/RAW Decode validation hooks
- `tools/graph_behavior_tests.cpp`

## Recorded Completed Work

The main plan records these completed pre-Phase-1 transitional items. This list is historical only; it is not the current RAW tab state.

- Direct RAW import/open/drop paths create the manual baseline RAW graph chain.
- The initial RAW top-level tab scaffold existed and routed into the existing Editor UI before the dedicated RAW Workspace panels replaced it.
- RAW Source UI and context menus point users toward the manual RAW chain first.
- `Develop` is labeled as advanced auto in node-add surfaces.
- A graph regression test verifies the manual RAW baseline socket chain.

These are useful compatibility/transitional pieces. They do not satisfy Phase 1 by themselves.

Phase 1 folder/catalog foundation is now also complete:

- The RAW tab has folder-backed Workspace state and recent Workspace persistence.
- Recursive RAW source scanning preserves folder grouping and excludes managed folders.
- Managed folders and catalog/rating skeleton files are created under the selected Workspace.
- Selecting a RAW source is preview-only and does not create `.stack` projects.

Phase 2 thumbnail pipeline is now also complete:

- Source records classify neutral thumbnails as valid, missing, or stale using sidecar cache signatures.
- Missing/stale/failed thumbnails queue for asynchronous neutral generation after scan.
- Thumbnail progress reports total discovered RAWs plus valid, queued, completed, and failed counts.
- Existing valid thumbnail files are reused without regeneration.
- Thumbnail status and cache paths are serialized into the catalog skeleton.
- Thumbnail generation failure is per-file and does not create `.stack` projects or block folder browsing.

Phase 3 gallery and Library integration is now also complete:

- The RAW tab renders an opened Workspace with grid/list browsing modes.
- The RAW tab supports mutually exclusive right-gallery and bottom-filmstrip placements; the right gallery can collapse/expand.
- Grid and filmstrip tiles show generated thumbnails when available and RAW placeholders otherwise.
- Folder grouping, scan status, thumbnail progress, file labels, and placeholder project status are visible in the browsing structure.
- The Library has a separate `RAW Workspace` view backed by the same `EditorModule` Workspace state.
- Library RAW tiles show grouped thumbnails and placeholder project status, and the selected RAW can be opened in the RAW tab.
- Phase 3 does not scan `Stack RAW Projects`, create projects, infer project status, or add edit/recipe controls.

Phase 4 recipe and compact node is now also complete:

- `Stack::RawRecipe::RawDevelopmentRecipe` is versioned and serializes source reference, stage order, exposure EV, white balance, historical `toneCurve` compatibility state, crop/rotation state, and preview/output mapping. Later graph-quality finish work made `finishTone` and `viewTransform` the active compact finishing state.
- The recipe maps to the existing RAW GPU renderer path for developed preview/graph output.
- The Editor graph has a compact `RAW Development` node with image output, recipe payload serialization, render snapshot support, and render graph execution.
- Downstream image nodes can connect after `RAW Development`; legacy RAW sockets remain limited to the existing manual RAW chain.
- The compact node exposes source/status summary and `Edit In RAW Tab`; Phase 7 now adds the visible `Decompose To Nodes` path.
- Phase 4 does not create projects, own project save lifecycle, discover project status, or build final RAW panels.

Phase 5 per-image project lifecycle is now also complete:

- Project discovery scans `Stack RAW Projects`, associates `.stack` files with RAW source records, and surfaces project status/path/mode in source records, catalog data, and RAW browsing UI.
- The editor exposes lifecycle APIs for Phase 6 controls to ensure a project exists before applying image-affecting recipe edits.
- First image-affecting edit creates or loads one per-image project under the RAW Workspace project tree and mirrors source subfolders.
- Project files persist RAW source metadata, linked/embedded source state, recipe data, downstream graph data, ownership mode payload slots, managed/custom state where applicable, and read-only state in a RAW Workspace project section.
- RAW Workspace projects route through the save lifecycle: explicit Save, `Ctrl+S`, save-before-export, save-before-load, save-before-switch, save-before-tab-leave, clear/close protection, and close-drained save worker paths. Later usability stabilization disables active RAW per-control and idle-timer saves.
- Minimal relink-to-selected-source and bake/embed actions exist for the selected active project.
- Phase 5 does not build final RAW edit panels or managed/custom graph validation.

Phase 6 dedicated RAW workspace panels are implemented and audit-hardened, with selected-source preview stabilization documented and Scenario 1 native active preview/startup/source-switch smoke still pending:

- The RAW tab body uses a dedicated development layout with controls on the left, preview centered, and the existing right gallery or bottom filmstrip embedded around it.
- Recipe panels exist for `Basic`, `White Balance`, `Exposure`, `Tone`, `Crop & Rotate`, and output/display controls. Later graph-quality finish work expanded the active RAW tab with `Finish Tone`, `View Transform`, and Local Range controls.
- Controls mutate RAW development recipes through the Phase 5 lifecycle API, so preview-only images create projects only on first image-affecting edit.
- `Open In Graph` is disabled for preview-only selections with the required tooltip, and existing project opens/focuses request the Editor tab.
- The RAW preview uses explicit same-source active render outputs when available; selected stored projects route through deferred project load/apply, selected preview-only sources stage a neutral same-source RAW preview, and neutral thumbnail/placeholder fallback is limited to browser/gallery, unavailable-render, or short pending states.
- RAW panel state covers preview-only, recipe-backed, embedded, invalid, and `Custom Graph Mode` read-only behavior.
- Non-selected browser/gallery tiles still use neutral thumbnail-style presentation until later edited/after thumbnail work.
- Phase 6 does not implement managed graph decomposition, structural validation, graph sync, repair/re-adopt, or detach semantics.

## Phase Status Snapshot

| Phase | Status | Resume Note |
| --- | --- | --- |
| Phase 1 - Folder/catalog foundation | Complete | Validated with `StackGraphBehaviorTests` and `Stack` Debug build on 2026-06-26. |
| Phase 2 - Thumbnail pipeline | Complete | Validated with `StackGraphBehaviorTests` and `Stack` Debug build on 2026-06-26. |
| Phase 3 - Gallery and Library | Complete | Validated with gallery presentation tests, `StackGraphBehaviorTests`, `Stack` Debug build, and launch/close smoke on 2026-06-26. |
| Phase 4 - Recipe and compact node | Complete | Validated with recipe/compact-node graph tests, `StackGraphBehaviorTests`, `Stack` Debug build, and launch/close smoke on 2026-06-26. |
| Phase 5 - Project lifecycle | Complete | Validated with lifecycle model tests, `StackGraphBehaviorTests`, `Stack` Debug build, and launch/close smoke on 2026-06-26. |
| Phase 6 - RAW workspace panels | In progress | Audit hardening validated with panel/project lifecycle tests, `StackGraphBehaviorTests`, `Stack` Debug build, and launch/close smoke on 2026-06-27. Active-project RAW tab edit/render usability stabilization and loading-freeze/project-load/proxy-preview tranche completed on 2026-06-27; loading audit/proxy-validation, command-line RAW Workspace loading smoke, gallery culling, preview/cache cleanup, RAW adjustment crash-lifetime follow-up, broad RAW render/cache/lifecycle crash stabilization, and final RAW render/pipeline/editor/memory audit hardening completed on 2026-06-28. Follow-up needed: `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 hands-on real-RAW smoke for active preview/startup/source switching; edited/after or recipe-rendered gallery thumbnails remain later Phase 8 work. |
| Phase 7 - Managed decomposition | In progress | V1 is implemented at model/build level: managed section metadata, decomposition, conservative validation, sync, custom mode transition, repair/re-adopt/detach/adoption surfaces, conservative missing-link mechanical repair, pre-confirmation warning boundaries, V1 reorder rejection, and file-level project reload ownership regression coverage are implemented and build/test validated. Automated native-smoke preflight passed on 2026-06-29. Follow-up: run `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 before marking complete. |
| Phase 8 - Later workflow upgrades | In progress | Branch-off work is implemented at model/build level: local exposure V1 and graph-quality compact finish state are implemented for compact recipe-backed RAW Workspace projects and validated at build/model level. Local Range Passes 1-8 recipe/identity contract, renderer V1, graph UI, trust overlays, target tool, edge-aware map quality, manual region masks, presets, and legacy compatibility/deprecation are implemented and validated at build/model level. Automated native-smoke preflight passed on 2026-06-29. Follow-up: run `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 and Scenario 6 through Scenario 9 for native Local Range verification. Other upgrades remain later. |

## When Finishing A Pass

Before ending an implementation, validation, or documentation pass, update this file. Do not rely on the chat transcript as the only record.

Update procedure:

1. Change `Last updated`.
2. Update `Current recommended active task` or `Current recommended active phase`, depending on whether the next action is implementation, validation, or documentation.
3. Update the `Phase Status Snapshot` table.
4. Replace the `Latest Pass` section with the newest pass.
5. Update the relevant phase document if its scope, status, validation, or deferrals changed.
6. In the final response, mention that `CURRENT_HANDOFF.md` was updated.

The newest handoff entry must include:

- the phase or task worked on,
- files changed,
- what is complete,
- what is intentionally deferred,
- validation run and result,
- known risks,
- the recommended next active phase or task.

Also update the relevant phase or task document when:

- a `Done When` item is now complete,
- a decision changed,
- scope was intentionally deferred,
- validation expectations changed,
- the next agent needs specific local context.

Do not mark a phase complete unless every `Done When` item is satisfied and relevant validation has passed or the missing validation is explicitly documented.

If a phase is complete or a validation/documentation task is satisfied:

- mark that phase `Complete` in the snapshot table when a phase, not only a task, is complete,
- set the current recommended active task/phase to the next incomplete phase or required validation/documentation task,
- list the validation that proves completion,
- record any follow-up work under the later phase that owns it.

If a phase or validation/documentation task is partially complete:

- keep that phase or validation/documentation task as the current recommended active task,
- list which `Done When` items or task requirements remain open,
- list any known partial implementation/docs files or tests,
- do not move the next agent to a later phase.

If a phase is blocked:

- keep the blocked phase or task visible as the current active task,
- explain the blocker,
- say exactly what user decision, dependency, test failure, or code issue must be resolved,
- do not skip to a later phase unless the user explicitly changes the plan.

## Handoff Entry Template

Use this format for the newest handoff entry:

```text
## Latest Pass

Date:
Phase/task worked on:
Files changed:
Completed:
- TBD
Deferred:
- TBD
Validation:
- TBD
Known risks:
- TBD
Recommended next phase/task:
- TBD
```

## Latest Pass

Date: 2026-06-29
Phase/task worked on: RAW Manual Workspace docs - Local Range scenario-specific smoke mapping cleanup
Files changed:
- `docs/engineering/develop/raw_manual_workspace/CURRENT_HANDOFF.md`
- `docs/engineering/develop/raw_manual_workspace/implementation_phases/08A_local_range_tone_equalizer.md`
Completed:
- Replaced scattered old Local Range smoke-test labels with scenario-specific `native/manual RAW UI smoke` requirements in `08A_local_range_tone_equalizer.md`.
- Mapped the implemented Local Range follow-ups to their durable checklist rows: Scenario 8 for trust overlays and region masks, Scenario 7 for the image target tool, Scenario 7 plus Scenario 8 for edge-aware quality, and Scenario 1 plus Scenario 6 through Scenario 9 for presets/legacy compatibility and full branch verification.
- Updated the main handoff Local Range Pass 4 through Pass 6 bullets to use the same scenario-specific native/manual RAW UI smoke wording.
Deferred:
- Interactive native/manual RAW UI smoke with real RAW projects remains required before Phase 7 can be called complete or Phase 8A Local Range can be considered natively verified.
- Scenario 1 through Scenario 9 remain pending in `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Broader Phase 8 workflow upgrades remain gated until native smoke rows are recorded and this handoff is updated.
Validation:
- Passed focused stale searches for old Local Range smoke-test labels in the Local Range doc.
- Passed focused presence searches for scenario-specific native/manual RAW UI smoke wording in the Local Range doc and handoff.
- Ran `git diff --check` on touched docs; command returned clean, though these docs remain untracked in this worktree.
- Passed direct trailing whitespace scan on the touched files.
Known risks:
- The interactive checklist scenarios have not been executed yet.
- This was a docs-only cleanup; no code build/tests were needed. The latest automated preflight remains recorded in `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- The repository/worktree remains broadly dirty/untracked from prior phases; do not revert unrelated files.
Recommended next phase/task:
- Execute `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 for Phase 7 and Scenario 1 plus Scenario 6 through Scenario 9 for Phase 8A against `D:\Program Development\Stack\build\Stack.exe` with the real RAW Workspace `C:\Users\djhbi\Downloads\all in extract\all copied images`. Record scenario rows in `NATIVE_RAW_UI_SMOKE_RESULTS.md`, then summarize results in this handoff before marking Phase 7 complete, calling Phase 8A Local Range natively verified, or starting broader Phase 8 workflow upgrades.
