# RAW Manual Workspace Plan

Date: 2026-06-25
Last updated: 2026-06-29

## Read This First

If you opened this file directly while starting implementation, validation, or documentation work, first read `README.md`, `CURRENT_HANDOFF.md`, and, when native/manual RAW UI smoke is involved, `NATIVE_RAW_UI_SMOKE_RESULTS.md`. They define the folder read order, latest resume state, phase/task selection rule, verification evidence, and first work note. Then return here.

This document is intended to survive multiple implementation, validation, and documentation passes across multiple conversations. Before changing code, running validation, or updating docs, preserve these invariants:

- There is one project truth: the per-image `.stack` project. That project should contain the RAW development recipe and the downstream editor graph.
- The RAW tab owns supported RAW development as a structured recipe, not as a live mirror of arbitrary graph nodes.
- The Editor graph consumes the RAW development result through a compact `RAW Development` node by default.
- The compact recipe-backed `RAW Development` node is the default graph representation. Managed decomposed chains are allowed, but only while Stack can validate their structure.
- RAW tab controls may sync to a decomposed graph chain only in `Managed Decomposed Mode`: locked RAW foundation stages stay in required order, and only supported development stages may be reordered inside validated slots.
- If the user deletes required nodes, duplicates unsupported stages, inserts custom nodes inside the managed RAW section, or otherwise breaks validation, that image enters `Custom Graph Mode`; RAW tab editing becomes read-only until the chain is repaired or explicitly re-adopted.
- Folder gallery selection is not the same as project editing. Selecting a RAW can be preview-only and must not create a `.stack` project.
- A per-image `.stack` project is created only on the first image-affecting edit.
- Each edited RAW gets one project. Do not build one giant project graph for the whole folder.
- RAW files are linked by default. Do not embed RAW files unless the user explicitly runs a bake/embed action.
- Neutral thumbnails are cache/preview assets, not edit state.
- Ratings, flags, labels, and folder-gallery metadata are not image edits and should not force per-image project creation.
- `Develop (Advanced Auto)` remains loadable for compatibility and advanced graph/debug surfaces only. Do not delete or rename serialized `RawDevelop` kinds as part of this plan.
- Do not reintroduce the old automatic RAW Develop workflow as normal RAW workspace behavior. Future automation, if it ever returns, may suggest manual recipe settings, but it must not own, hide, or duplicate the RAW pipeline.
- The implemented `Decompose To Nodes` action creates `Managed Decomposed Mode` by default and keeps a validated live link to the RAW tab while the chain remains structurally valid and representable by the current managed stage set.
- User-facing RAW tab copy should call the opened RAW folder the `Workspace`. The primary empty-state action should be `Open RAW Folder`.
- Base Phase 6 used `Preview & Output` copy for output/display mapping. The later graph-quality finish pass deliberately exposed explicit `View Transform` controls in the RAW tab so compact recipes can match the proven graph workflow; keep future copy aligned with that implemented recipe state.
- The original first RAW control set was deliberately small: `RAW Exposure EV`, white balance, tone curve, crop, rotate, and preview/output behavior. Current compact recipe controls have expanded to graph-quality Finish Tone, View Transform, Local Range, and region masks.
- The final `White Balance` panel should include `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking. Implementation may stage these features, but the planned end-state is the full set.

After following the README read order, identify the active task/phase from `CURRENT_HANDOFF.md` and do only that slice. If no validation or documentation task is active, use the phase list below to find the earliest incomplete phase whose prerequisites are satisfied. Do not skip straight to final Lightroom-style behavior by inventing parallel state.

Detailed phase/task handoff documents live in `implementation_phases/`. Start with `implementation_phases/00_IMPLEMENTATION_GUIDE.md`, then read the specific phase document, task document, or validation protocol before editing code, running validation, or updating docs.

## Current Status

`CURRENT_HANDOFF.md` is the authoritative resume state. This summary exists so the durable product plan does not mislead a cold-start reader.

As of 2026-06-29:

- Phases 1-5 are implemented and validated in code: folder/catalog foundation, thumbnail pipeline, RAW gallery/Library integration, recipe/compact node, and per-image project lifecycle.
- Phase 6 RAW workspace panels are implemented and audit-hardened. Later stabilization superseded the old selected-project preview limitation: selected sources now queue active preview staging, stored projects use deferred project load/apply, and preview-only selections stage neutral same-source RAW previews. `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 native RAW UI smoke is still required for active preview/startup/source-switch confidence, and edited/after or recipe-rendered gallery thumbnails remain later Phase 8 work.
- Phase 7 Managed Decomposition V1 is implemented at model/build level: managed metadata, decomposition, conservative validation, supported sync, custom mode transition, repair/re-adopt/detach/adoption surfaces, missing-link repair, warning boundaries, V1 reorder rejection, and project reload ownership coverage. Phase 7 is not complete until `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 pass on a real RAW Workspace and are recorded in `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Phase 8A Local Range branch-off passes 1-8 are implemented at model/build level: recipe/identity, renderer math, graph UI, trust overlays, image target tool, edge-aware map quality, manual region masks, presets, and legacy compatibility/deprecation. It is not natively verified until `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 and Scenario 6 through Scenario 9 pass on real RAW files and are recorded in `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Automated native-smoke preflight passed against a real RAW Workspace on 2026-06-29 and is recorded in `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Current recommended task: run `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 for Phase 7 and Scenario 1 plus Scenario 6 through Scenario 9 for Phase 8A, record scenario rows in `NATIVE_RAW_UI_SMOKE_RESULTS.md`, then summarize results in `CURRENT_HANDOFF.md`.

## Goal

Make RAW editing in Stack manual-first, understandable, and UI-native.

The normal RAW workflow should be a dedicated RAW tab, closer in spirit to Lightroom or darktable than to a node graph. That RAW tab owns a stable default-order RAW development recipe for the active image. The recipe should be saved in the per-image `.stack` project and rendered as the developed image result. If an image later enters `Custom Graph Mode`, the custom graph section owns that render until repaired, re-adopted, or detached.

The main Editor graph should consume that developed result through a compact `RAW Development` node. Users then continue ordinary Stack editing downstream from that node.

The advanced graph decomposition form is still useful:

`RAW Source -> RAW Decode -> Tone Curve -> View Transform -> Output`

Here `Output` means the normal downstream graph/output endpoint. A managed RAW section may stop at `View Transform` and feed whatever downstream graph/output structure the current project uses.

However, that chain should be treated as an advanced/decomposed representation, not the default RAW tab architecture. If Stack creates it through `Decompose To Nodes`, it can remain connected to the RAW tab as a managed decomposed chain while it stays structurally valid. The RAW tab should not become a second node graph that understands arbitrary graph mutations; it should understand a bounded, validated RAW-chain contract.

The RAW tab should also be workspace-centered. A user opens a RAW folder as the Workspace, Stack scans that workspace root, generates thumbnails, presents the RAW files as a gallery, and creates per-image Stack project files only when the user actually starts editing an image.

In user-facing copy, this opened folder should be called the RAW `Workspace`. The filesystem implementation can still refer to it as the workspace root when that is clearer for code.

## Problem

The old default RAW path treated `Develop` / Auto RAW as the primary workflow. That node tries to combine too many jobs:

- RAW decode and pre-exposure placement.
- Tone shaping.
- Automatic scene prep.
- Local exposure/tone behavior.
- Candidate renders and scoring.
- Denoise/detail-related guidance.

Those are all plausible tools, but combining them into one automatic node made the controls redundant and hard to reason about. The user could get a poor result without knowing which stage caused it.

This plan does not bring that old automatic RAW workflow back into the normal RAW workspace. Compatibility can remain, and expert/debug graph access can remain, but the designed RAW workspace should be manual, explicit, and recipe-owned.

There is a second risk in the opposite direction: making the RAW tab and the graph equally capable editors for the same live RAW chain. If the RAW tab directly controls graph nodes such as `RAW Decode`, `Tone Curve`, and `View Transform`, then graph edits can break the RAW tab's assumptions. Users could delete a node, add a second tone curve, insert custom nodes, or reorder stages. Supporting every possible graph mutation would turn the RAW tab into another node graph, which defeats the purpose of a designed RAW workspace.

The intended middle ground is bounded two-way integration. The RAW tab can stay editable while a decomposed graph still matches a known RAW-chain structure. If the graph becomes arbitrary, Stack should say so plainly and stop pretending the RAW tab can safely own it.

## Decisions

- The top-level RAW tab is now a designed RAW editing workspace, not a visual copy of the Editor tab.
- The RAW tab should own a structured RAW development recipe for the active image while the image is in recipe-backed or managed decomposed mode.
- The per-image `.stack` project is the source of truth for both the RAW development recipe and the downstream editor graph.
- The main Editor graph should consume the RAW development result through a compact `RAW Development` node by default.
- The user should be able to continue editing downstream from `RAW Development` in the main Editor graph.
- Direct RAW import/open/drop paths currently create the manual baseline graph chain. This is completed transitional work and remains useful, but the folder-backed RAW tab should move toward the compact `RAW Development` node model.
- `RAW Source -> RAW Decode -> Tone Curve -> View Transform` should remain available as an advanced/decomposed graph representation, not as the default graph shape for normal RAW tab editing.
- The RAW tab should use a stable default order for beginners and for images that have never been customized in the graph.
- Some RAW/development stages are locked foundation stages and must remain in a required order. Some later development stages may be reorderable if Stack can validate that order and map it back to the recipe.
- A decomposed RAW chain can stay editable from both the RAW tab and graph while it is in `Managed Decomposed Mode` and passes structural validation.
- If a graph user customizes the RAW chain beyond the supported structure, Stack should switch that image to `Custom Graph Mode`, make RAW tab controls read-only, and offer repair/re-adopt paths when possible.
- This gives beginners a safe RAW tab workflow and gives experts a graph workflow without forcing the RAW tab to support every possible node graph.
- The RAW tab should manage a RAW Workspace root, not just a single imported file.
- User-facing RAW tab copy should call that opened folder the `Workspace`.
- The primary no-workspace action should say `Open RAW Folder`, not `Open Project`.
- RAW folder scanning should include subfolders.
- The RAW gallery should preserve and display the user's folder organization instead of flattening every RAW into one list.
- Selecting a RAW in the gallery should preview it without creating a project.
- A project should be created automatically only when the user edits that RAW.
- "First edit" means an image-affecting adjustment such as changing a slider, rotating, choosing a crop, or otherwise changing the rendered result.
- Ratings, flags, and similar gallery organization metadata are not image edits and should be stored separately from per-image edit projects.
- Per-image project files should be named from the RAW filename so the relationship is obvious outside Stack.
- RAW files should be linked by default, not embedded into project files.
- Embedding/baking the RAW file into a project should be an explicit user action with a clear project-selection UI.
- Active RAW recipe edits should not synchronously save on every control tick or use time-based idle save timers. A visible Save command, `Ctrl+S`, and lifecycle boundaries such as source switch, tab leave, clear/close, and shutdown should persist dirty projects.
- Switching to another RAW with unsaved edits should save the current project's edits before loading the next image.
- The Library should gain a RAW-folder tab/view that reads the same folder structure. The main Library remains focused on normal image/edit projects.
- `Develop` should remain loadable for existing projects and advanced graph/debug experiments only.
- `Develop (Advanced Auto)` should not appear in the normal RAW workspace UI and should not be presented as an equal normal alternative beside the RAW tab's recipe-owned `RAW Development` workflow.
- Future automation should not revive the old Auto RAW node as a normal product flow. If automation returns, it should propose settings for the manual recipe or perform a clearly named single-purpose helper action; it must not own the entire pipeline.
- UI copy should name what the control actually does, not what we hope the automatic system can infer.
- The first recipe-backed RAW control set was intentionally narrow: `RAW Exposure EV`, white balance, tone curve, crop, rotate, and preview/output behavior. Current compact recipe controls also include graph-quality Finish Tone, View Transform, Local Range, and region masks.
- The final white balance UI should include `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking. Early implementation can stage backend support, but the UI plan should not permanently narrow white balance to one slider pair.
- Phase 6 originally used `Preview & Output` as the user-facing label for output/display mapping. The current graph-quality finish branch exposes `View Transform` directly for compact recipe finishing controls; managed decomposition must continue to map that state exactly to the graph `View Transform` node.
- The `Tone` UI should be curve-first and may include useful sliders that visibly modify the curve. Future RAW UI design should explore curve graphs, point graphs, and related point-based controls where they explain relationships better than isolated sliders.
- Later copy/paste/sync settings should auto-create projects for preview-only targets, but only as an explicit batch action with progress and clear feedback.
- Experts should be able to start directly in graph mode. Stack may sync that work back to the RAW tab only when the graph chain can be validated/adopted into the supported RAW-chain contract.

## Non-Goals

- Do not delete `RawDevelop` serialization or renderer support in this pass.
- Do not rewrite, revive, or route the normal RAW workspace through the Auto RAW solver in this pass.
- Do not expose `Develop (Advanced Auto)` in the normal RAW workspace UI.
- Do not create a separate RAW document model outside the per-image `.stack` project.
- Do not embed full RAW files into `.stack` projects by default.
- Do not make the main Library become RAW-folder-only; RAW folder browsing is a separate Library view/tab.
- Do not rename serialized node kinds yet; compatibility matters.
- Do not create projects while merely scanning folders, generating thumbnails, rating/flagging images, or selecting images for preview.
- Do not block the UI while generating thumbnails for a large folder.
- Do not make the RAW tab and Editor graph unbounded equal editors for arbitrary RAW-stage graphs.
- Do not require the RAW tab to understand arbitrary graph rewiring, deleted nodes, duplicated tone curves, or inserted custom nodes.
- Do not expose arbitrary RAW foundation ordering in the normal RAW tab workflow. Only validated flexible development stages should be reorderable.

## Implementation Phases

Use these phases to keep future work coherent:

1. Folder/catalog foundation (`implementation_phases/01_folder_catalog_foundation.md`):
   RAW Workspace root state, recursive scan, source records, human-readable managed folders, folder-level catalog/index, and no-edit preview selection.

2. Thumbnail pipeline (`implementation_phases/02_thumbnail_pipeline.md`):
   neutral thumbnail generation, thumbnail reuse/staleness checks, background progress, partial gallery availability while generation continues.

3. RAW gallery and Library integration (`implementation_phases/03_gallery_and_library.md`):
   RAW tab gallery grouped by folder, Library RAW-folder view, project-status surfaces, shared Workspace state. Real project discovery is Phase 5.

4. RAW development recipe and graph output (`implementation_phases/04_recipe_and_compact_node.md`):
   structured default-order RAW recipe, compact `RAW Development` graph node, recipe rendering path, and no automatic decomposition into multiple graph nodes for the default UI.

5. Per-image project lifecycle (`implementation_phases/05_project_lifecycle.md`):
   create project on first image edit, link RAW by default, explicit and lifecycle-boundary saves, Save/`Ctrl+S`, save-before-switch, project discovery, relink/repair.

6. Dedicated RAW editing panels (`implementation_phases/06_raw_workspace_panels.md`):
   replace the editor-copy RAW tab body with recipe-owned panels such as Basic, White Balance, Exposure, Tone/Finish Tone, Crop & Rotate, current View Transform controls, preview, and Open In Graph controls.

7. Advanced graph decomposition (`implementation_phases/07_managed_decomposition.md`):
   optional `Decompose To Nodes` action that creates a managed RAW graph section for graph experts. While that section remains structurally valid, RAW tab controls and managed node parameters can sync both directions. If the user breaks the supported structure, the image switches to `Custom Graph Mode` and RAW tab editing becomes read-only until repaired or re-adopted.

8. Later workflow upgrades (`implementation_phases/08_later_workflow_upgrades.md`):
   edited/after thumbnails, before/after hover, copy/paste/sync settings, batch behavior, optional denoise/detail stages, Local Range follow-up extensions, and legacy local-exposure compatibility.

Earlier phases should not require later phases to exist. For example, phase 1 can show a gallery with missing thumbnails; phase 2 can generate neutral thumbnails without creating projects; phase 4 can define/render the RAW recipe without final polished panels; phase 5 can create projects before advanced graph decomposition exists.

## RAW Development Recipe And Graph Integration

The normal recipe-backed RAW architecture should separate RAW development ownership from downstream graph editing:

```text
RAW tab
  owns supported RAW development as a structured recipe
  renders the developed RAW result
        |
        v
Editor graph
  RAW Development node -> normal Stack nodes -> Output
```

The RAW development recipe is saved inside the per-image `.stack` project. It should not be stored only in UI state, thumbnail cache, or folder catalog metadata.

`Managed Decomposed Mode` is a validated graph representation of the same recipe. `Custom Graph Mode` is the explicit exception where the custom graph section owns the rendered RAW result and the RAW tab becomes read-only for that image.

The normal Editor graph should see a compact `RAW Development` node. That node represents the developed output of the RAW tab recipe for the active image. It should expose a small, clear surface:

- RAW filename / status.
- Edited/dirty/saved/project state.
- `Edit In RAW Tab`.
- `Decompose To Nodes` for advanced graph control.
- Possibly basic read-only summary values, but not the full RAW tab UI.

The graph should not show the full RAW development internals by default. Downstream graph editing starts after the compact `RAW Development` node.

### Default Stage Order

The RAW tab should use a stable default processing order for RAW development. The exact renderer implementation can evolve, but the UI should not ask the normal user to design RAW stage order from scratch.

A reasonable mental model is:

```text
RAW file
-> load / metadata / black-white calibration / orientation
-> demosaic / camera transform / white balance
-> pre-tone RAW exposure EV
-> tone shaping
-> color/detail/crop/output adjustments as supported
-> display/output transform
-> developed image result
```

This avoids questions like whether brightness happens before contrast or whether tone mapping happens before exposure in arbitrary graph order. The RAW tab's controls are organized around a stable development recipe.

This default order is the beginner-friendly path. It is also the starting point for experts who later decompose the RAW development into graph nodes.

### Stage Ordering And Structural Validity

Stack should distinguish locked RAW foundation stages from flexible development stages.

Locked foundation stages are the stages that must remain in a required relationship for the RAW pipeline to make sense. The exact implementation can evolve, but this category includes source loading, metadata/orientation, black-white calibration, demosaic/camera transform, and any renderer-required early RAW reconstruction steps.

Flexible development stages are image-stage adjustments that happen after the image exists in a form where those operations are meaningful. Examples may include tone shaping, contrast, Local Range/local-exposure compatibility, color adjustments, detail work, crop/rotate, and preview/output behavior. Not every flexible stage is automatically reorderable; reorderability must be explicitly supported by the recipe and validator.

When a RAW recipe is decomposed into graph nodes, Stack should validate the graph section:

- Required foundation stages exist exactly as expected.
- Locked foundation stages remain in the required order.
- Supported flexible stages may be reordered only inside allowed slots.
- Node parameters still map clearly back to RAW recipe fields.
- Unknown/custom nodes inside the managed RAW section are treated as custom graph edits unless a future version explicitly supports them.

As long as validation passes, the RAW tab can remain editable and the graph can remain editable. If validation fails, Stack should stop two-way editing for that image and show:

```text
This RAW chain has been customized in the graph.
RAW tab editing is read-only for this image until the chain is repaired or re-adopted.
```

This rule is the core beginner/expert compromise: beginners get a stable RAW tab order, while experts can use the graph and preserve RAW-tab editing if they stay inside the supported RAW-chain structure.

### RAW Tab Panel Names

The RAW tab should use photographer/workflow names, not graph-stage names:

- `Basic`
- `White Balance`
- `Exposure`
- `Tone`
- `Crop & Rotate`
- `Preview & Output` in the base Phase 6 surface, now expanded by graph-quality finish into explicit `View Transform` controls where the recipe exposes output/display rolloff state.

Controls can still be mathematically honest. For example, `RAW Exposure EV` should be named clearly and can explain in a tooltip that it is applied before tone shaping and multiplies scene-linear decoded values.

The original MVP kept the active control set narrow: `RAW Exposure EV`, white balance, tone curve, crop, rotate, and preview/output behavior. Current compact recipe controls have expanded to graph-quality Finish Tone, View Transform, Local Range, and region-mask state while preserving the same recipe-owned UI principle.

The final `White Balance` panel should be planned around `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking. A first implementation may wire only the subset the renderer supports, but the UI should have a path to the full feature set instead of treating white balance as permanently minimal.

Historically, the RAW tab avoided `View Transform` as a normal user-facing panel name and used `Preview & Output`. The current graph-quality finish branch intentionally exposes `View Transform` controls because users need direct control over display rolloff, contrast, saturation, middle grey, and false-color inspection in the compact recipe path.

The `Tone` panel should be curve-first and may include supporting sliders. Sliders that affect tone should visibly update the curve or clearly show their relationship to the curve. Future design exploration should consider curve graphs, point graphs, and linked point controls where they make the image relationship clearer than independent sliders.

### Advanced Decomposition

Advanced users may need the graph-stage representation:

```text
RAW Source -> RAW Decode -> Tone Curve -> View Transform
```

This should be exposed as an intentional `Decompose To Nodes` action, not as the normal graph shape for the RAW tab.

By default, decomposition should create `Managed Decomposed Mode`:

- Stack creates a clearly grouped or labeled RAW graph section, for example `RAW Development: image_0001.ARW`.
- RAW tab controls update the corresponding managed graph nodes.
- Managed graph node parameter changes update the RAW recipe and RAW tab controls.
- Phase 7 V1 rejects flexible-stage reordering. A future reorderable-slot pass must update recipe stage order only for explicitly supported slots that the validator can round-trip.
- Locked foundation stages cannot be reordered without breaking managed mode.
- The user can continue downstream normal graph editing after the managed RAW section.

If the user adds a custom node, deletes a required node, duplicates a stage Stack cannot represent, or rewires the section outside the supported RAW-chain contract, Stack should switch that image to `Custom Graph Mode`. In that mode:

- The custom graph section owns the rendered result.
- RAW tab editing controls are read-only for that image.
- The RAW tab can still show preview/status and explain why editing is disabled.
- Stack can offer `Repair RAW Chain` if the issue is mechanical, such as a missing required node that can be restored.
- Stack can offer `Re-adopt Graph As RAW Recipe` only when the current graph section validates cleanly.
- Stack can offer `Detach From RAW Tab` for experts who intentionally want fully custom `Custom Graph Mode` RAW work.

Before an intentional operation breaks managed mode, Stack should warn the user in plain language. The warning should explain that the RAW tab will become read-only for that image unless the chain is later repaired or re-adopted.

## Folder Workspace Model

The RAW Workspace is rooted at a user-opened RAW folder. In the UI, call this folder the RAW `Workspace`. In implementation docs and code comments, `workspace root` means that folder's filesystem path. Stack should use human-readable managed folders inside the workspace root as long as that does not block reliable program behavior.

Stack scans the workspace root and its subfolders for RAW files, while preserving that folder organization in the gallery. If a user already organized a shoot by subfolder, for example `Day 1` and `Day 2`, Stack should show that same structure instead of flattening everything.

Example:

```text
RAW Workspace/
  Day 1/
    image_0001.ARW
    image_0002.DNG
  Day 2/
    image_0101.ARW
    image_0102.DNG
  Stack RAW Thumbnails/
    Day 1/
      image_0001.thumb.png
      image_0002.thumb.png
    Day 2/
      image_0101.thumb.png
      image_0102.thumb.png
  Stack RAW Projects/
    Day 1/
      image_0001.stack
    Day 2/
      image_0101.stack
  Stack RAW Catalog/
    catalog.json
    ratings.json
```

The folder names above are the documented implementation names. Do not rename them unless the main plan and every phase document are updated together. The structure is intentional: thumbnails, projects, and catalog metadata live inside the RAW Workspace instead of Stack's normal application/library storage.

Stack should exclude its own managed folders from source RAW scanning. `Stack RAW Thumbnails`, `Stack RAW Projects`, and `Stack RAW Catalog` are read by their dedicated thumbnail/project/catalog systems, not treated as source image folders.

The normal Stack Library stores normal image/edit projects in Stack-managed application/library storage. RAW-folder projects are different: they live next to the user's RAW files in whichever folder or drive the user selects.

Thumbnail behavior:

- Stack scans the workspace root and subfolders for supported RAW formats such as DNG, ARW, RAW, and any other extension supported by `RawLoader`.
- Stack generates neutral thumbnails for discovered RAW files and stores them in `Stack RAW Thumbnails`, mirroring the source folder organization.
- A neutral thumbnail means decoded/rendered RAW data without user edits applied.
- Thumbnails should be reused on future scans when the RAW file and thumbnail are still valid.
- Stack must check that every discovered RAW file has a valid thumbnail.
- Missing or stale thumbnails should be generated in the background with visible progress.
- Thumbnail generation can be resource-intensive and must be treated as asynchronous work, not an instant UI operation.
- The RAW tab and Library RAW-folder view should show scan/thumbnail progress, including partial progress when some thumbnails already exist.
- Later, once the neutral thumbnail system is stable, Stack can add edited/after thumbnails for RAW files with projects. The gallery can default to showing the edited/after thumbnail and reveal the neutral/before thumbnail on hover.

Project behavior:

- Clicking a RAW file in the RAW tab selects/previews it.
- Selecting or previewing a RAW file does not create a project.
- The first image edit creates a `.stack` project in `Stack RAW Projects`.
- Image edits include slider/control changes, rotation, crop selection, and any other rendered-image adjustment.
- Ratings, flags, labels, and gallery organization changes do not create per-image edit projects.
- The project filename should match the RAW filename stem, for example `image_0001.ARW` -> `image_0001.stack`.
- The project stores the RAW development recipe and downstream editor graph, but links to the RAW file by default.
- If the image is in `Managed Decomposed Mode` or `Custom Graph Mode`, the project should also persist enough mode/graph-section state to reopen that image without silently flattening it back into compact recipe mode.
- Each RAW image gets one project. The RAW tab should switch between per-image projects, not build one huge multi-RAW graph for the whole folder.
- The main Editor graph represents the downstream graph for the currently open per-image project, not the entire RAW folder.
- By default, that graph should consume the RAW tab result through a compact `RAW Development` node.
- Active RAW recipe edits should mark the project dirty without synchronously saving on every control tick or using time-based idle save timers.
- A visible Save command and `Ctrl+S` should explicitly save the current RAW project.
- Switching from one RAW to another, leaving the RAW tab, clearing/closing the Workspace, or shutting down should save any unsaved edits before the lifecycle transition completes.
- If the user wants the RAW file embedded, they use an explicit bake/embed action.

Gallery metadata behavior:

- Ratings, flags, labels, and similar gallery metadata should be stored separately from the per-image `.stack` edit projects.
- Default storage split: `catalog.json` is the structural/index cache for source records, thumbnail status, project associations, and scan recovery; `ratings.json` is the user gallery metadata file for ratings, flags, and labels.
- `catalog.json` may cache derived metadata summaries for speed, but `ratings.json` should be treated as the user-editable gallery metadata source unless a future migration intentionally changes the storage split.
- A folder-level catalog/index is allowed and likely desirable for reliability and speed.
- The catalog/index can track discovered RAW files, relative paths, thumbnail status, project associations, source fingerprints, and the last selected image.
- The catalog/index should be treated as cache/metadata, not the only source of truth. Stack should be able to rescan the folder and recover if the catalog is missing or stale.

Rename/relink behavior:

- If a RAW file is renamed or moved, Stack should try to repair the project association using stored metadata and/or a source fingerprint/hash.
- If automatic repair is uncertain or fails, Stack should offer a relink UI.
- The relink UI should let the user choose the RAW file that belongs to an existing project.

Bake/embed behavior:

- The bake/embed UI should let the user choose which project receives the embedded RAW data.
- The UI must make the storage consequence clear: linked projects stay smaller and depend on the source RAW path; baked projects are larger but self-contained.
- Baking should not silently change every project in the folder.

## Library Integration

The Library should have a RAW-folder view/tab that uses the current RAW Workspace. This view should:

- Show RAW thumbnails from `Stack RAW Thumbnails`.
- Preserve the Workspace's subfolder organization.
- Show which RAW files already have project files in `Stack RAW Projects`.
- Let the user open an existing RAW project.
- Let the user select a RAW that has no project yet and send it to the RAW tab for preview/editing.
- Keep the main Library's normal project/image-edit browsing separate from this RAW-folder workflow.

The RAW tab and Library RAW-folder view should share the same Workspace state so the user does not have to reopen the RAW folder in both places.

## RAW Workspace UX Target

The RAW tab should present the RAW development recipe as a normal photo-development interface:

- A large preview. Before a project exists, browser/gallery tiles may show neutral thumbnails or placeholders while the active preview stages a neutral same-source RAW render when available. After edits exist, show the active per-image project output.
- A workspace picker for the active RAW workspace, with the primary action labeled `Open RAW Folder`.
- A gallery/filmstrip/grid of RAW thumbnails from the Workspace and its subfolders, grouped by folder.
- A scan/thumbnail progress display for resource-intensive thumbnail generation.
- A selected-image state that can be preview-only until the first edit.
- MVP workflow panels such as `Basic`, `White Balance`, `Exposure`, `Tone`, `Crop & Rotate`, and output/display controls. Current compact recipe controls include graph-quality `Finish Tone`, `View Transform`, and Local Range branch-off controls.
- Controls should edit the active image's RAW development recipe. In `Managed Decomposed Mode`, that recipe can sync to managed graph nodes; controls should not edit arbitrary live graph nodes.
- Denoise can become an explicit recipe-backed panel or deliberate advanced graph stage when its behavior is clear. Local Range is now the explicit recipe-backed local balancing branch; legacy `localExposure` remains compatibility/supporting state and should not be revived as the primary local UI. Neither should be a hidden side effect of an automatic RAW path.
- `Develop (Advanced Auto)` must not appear in the normal RAW workspace UI. It remains compatibility/advanced graph/debug functionality outside the recipe-owned RAW workspace flow.
- A clear command to open the current per-image project in the Editor graph.
- A clear advanced command to decompose the RAW development recipe into a managed graph section when needed.

The RAW tab may create or repair the compact `RAW Development` graph entry for an existing or newly-created per-image project, but it must not create a project merely because the user selected an image. It must not store duplicate RAW edit settings outside the per-image project recipe. When the RAW tab is editable, changing a slider must update the active image's RAW development recipe.

The Editor graph should expose the RAW development result as a compact node by default. Users can connect downstream from that node and continue regular editing there. A full `RAW Source -> RAW Decode -> Tone Curve -> View Transform` node chain is reserved for advanced managed decomposition or fully custom `Custom Graph Mode` RAW work.

## Implementation Checklist

- [x] Route RAW imports through the manual baseline chain.
- [x] Add the initial RAW top-level tab scaffold and focus the existing graph on RAW decode work.
- [x] Make RAW Source copy and actions point users at the manual chain first.
- [x] Mark `Develop` as advanced/legacy where it appears in node-add surfaces.
- [x] Add a regression check for the manual RAW baseline chain shape.
- [x] Build and run the relevant validation commands.

## Implementation Status Checklist

This checklist is a durable orientation aid, not the resume authority. Use `CURRENT_HANDOFF.md`, the phase docs, and `NATIVE_RAW_UI_SMOKE_RESULTS.md` for exact current state and verification evidence.

- [x] Replace the RAW tab's current editor-copy body with a dedicated UI workspace.
- [x] Add selected RAW-folder state shared by RAW tab and Library RAW-folder view.
- [x] Add recursive RAW folder scanning for supported RAW files while preserving subfolder organization in the UI.
- [x] Add thumbnail generation/reuse into the Workspace thumbnail subfolder, with background progress reporting.
- [x] Add neutral thumbnail generation first; defer edited/after thumbnails until the neutral path is stable.
- [x] Add per-RAW project discovery in the Workspace project subfolder, mirroring source subfolders.
- [x] Add preview-only selection for RAW files that have not been edited yet.
- [x] Add automatic project creation on first image edit, named from the RAW filename stem.
- [x] Store ratings/flags/labels outside per-image edit projects.
- [x] Add a folder-level catalog/index for reliable scan, metadata, thumbnail, and project association tracking.
- [x] Store RAW files as linked external sources by default in RAW-folder projects.
- [x] Add project save lifecycle support, explicit Save, `Ctrl+S`, save-before-switch, and close/clear protection. Active RAW per-control/idle saves are intentionally avoided after the usability stabilization pass.
- [x] Save the current RAW project before switching to another image.
- [x] Add RAW relink/repair support by selected source, with metadata/fingerprint safety checks.
- [x] Add explicit bake/embed RAW action with project selection.
- [x] Add the Library RAW-folder tab/view that reads the same folder structure.
- [x] Add a structured RAW development recipe model saved inside per-image projects.
- [x] Add rendering support for the RAW development recipe.
- [x] Add a compact `RAW Development` graph node that outputs the recipe result to the Editor graph.
- [x] Add RAW tab controls that edit the active image's RAW development recipe.
- [x] Add and then expand the MVP RAW controls. Current compact recipe controls include RAW exposure, white balance, Finish Tone, Local Range, region masks, crop controls, quarter-turn rotation, and View Transform state. Managed decomposition currently maps supported rotation values and still blocks enabled crop until an exact crop-node mapping exists.
- [x] Plan the final white balance panel around `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking, even if the implementation wires those features in stages.
- [x] Add a way to jump from the RAW tab to the compact `RAW Development` node in the Editor graph.
- [x] Add explicit advanced `Decompose To Nodes` support that creates a managed RAW graph section from the recipe.
- [x] Add structural validation for managed decomposed RAW chains.
- [x] Add two-way sync between RAW tab controls and managed graph node parameters while validation passes for currently representable fields.
- [ ] Add support for explicitly reorderable flexible development stages inside validated slots. Phase 7 V1 intentionally rejects flexible reordering; future support must expand recipe, UI explanation, serialization, and validator together.
- [x] Add `Custom Graph Mode` when the decomposed RAW chain no longer validates.
- [x] Add read-only RAW tab messaging for customized graph chains.
- [x] Add `Repair RAW Chain`, `Re-adopt Graph As RAW Recipe`, and `Detach From RAW Tab` actions.
- [x] Add UI/model and graph regression checks for recipe persistence, compact node output, managed decomposition, custom mode, repair/re-adopt/detach, V1 reorder rejection, and ownership reload.
- [ ] Execute the required native/manual RAW UI smoke scenarios from `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` and record results in `NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- [ ] Later: add copy/paste or sync settings between RAW images from the gallery as explicit batch actions with progress and project creation for preview-only targets.

## Completed Work

The full up-to-date resume state lives in `CURRENT_HANDOFF.md`. Highlights as of 2026-06-29:

- `AddGraphRawChainFromFile` now builds the manual baseline chain by reusing `AddFullRawTreeToSource`. This is useful transitional behavior and an advanced decomposition template, not the final RAW tab backing model.
- RAW paths through source-image load, graph drag/drop, and add-image-node import share the same manual baseline behavior today.
- RAW Source controls now expose `Add Manual RAW Chain` first and keep `Add Advanced Develop` as an explicit advanced option.
- The RAW Source context menu now says `Add Manual RAW Chain` instead of `Add Full Tree`.
- The node browser labels `Develop` as `Develop (Advanced Auto)` under `Advanced RAW`.
- `StackGraphBehaviorTests` now verifies the manual RAW baseline chain socket contract.
- The RAW tab is now a folder-backed RAW Workspace with scan/catalog state, neutral thumbnails, gallery/list/filmstrip presentation, and Library RAW-folder integration.
- Per-image `.stack` RAW Workspace projects persist recipe, linked/embedded source state, downstream graph data, project ownership mode, and lifecycle save behavior.
- The compact `RAW Development` node and recipe renderer are implemented for recipe-backed RAW projects.
- Active RAW tab preview/edit rendering has been stabilized for non-blocking recipe edits, capped drag previews, uncapped full-resolution settle renders, explicit preview state, and same-source stale-tile retention.
- Managed decomposition V1 is implemented and model/build validated, with Scenario 2 through Scenario 5 native UI smoke still required before marking Phase 7 complete.
- Local Range Passes 1-8 are implemented and model/build validated, with Scenario 1 and Scenario 6 through Scenario 9 native UI smoke still required before calling the branch natively verified.
- Native smoke protocol and result logging now live in `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` and `NATIVE_RAW_UI_SMOKE_RESULTS.md`.

## Validation

Initial baseline validation passed on 2026-06-25:

```text
cmake --build build --config Debug --target StackGraphBehaviorTests --parallel 1
build\StackGraphBehaviorTests.exe
cmake --build build --config Debug --target Stack --parallel 1
build\Stack.exe --validate-develop-node-smoke
build\Stack.exe --validate-tone-curve-auto
```

Latest automated native-smoke preflight passed on 2026-06-29 and is recorded in `NATIVE_RAW_UI_SMOKE_RESULTS.md`. Do not treat that preflight as native/manual UI completion; the interactive checklist scenarios remain pending until recorded there.

## Notes For Future Cleanup

- RAW exposure in the recipe is a linear pre-tone exposure multiplier. Positive EV multiplies scene-linear decoded values before tone shaping.
- Tone shaping in the recipe is the main manual step after decode. It is where the user makes the image look decent without hiding the math.
- Output/display transform is the final display/output mapping step in the recipe. In the graph and current graph-quality RAW tab this maps to `View Transform`; older Phase 6 copy called the same user-facing area `Preview & Output`.
- Denoise should become an explicit recipe capability or deliberate advanced graph stage when ready, not a hidden automatic side effect of the default RAW path. Local Range is already the explicit local balancing recipe branch; legacy `localExposure` should stay compatibility/supporting state unless a later migration deliberately removes it.
- Do not bring back the old automatic RAW Develop workflow as a normal RAW workspace feature. If automation returns later, it should only suggest manual recipe settings or run a clearly named helper action that the user can inspect and undo; it must not own or hide the full pipeline.
