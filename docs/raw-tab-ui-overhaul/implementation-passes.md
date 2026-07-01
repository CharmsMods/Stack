# RAW Tab UI Implementation Passes

## Purpose

This document breaks the RAW tab UI overhaul into implementation passes. Each pass should be small enough to review and validate without trying to rebuild the entire RAW workspace at once.

Follow `implementation-contract.md` for default decisions.

## Pass Rules

For every pass:

- keep Auto Base visible, editable, and reversible
- do not add algorithm/math code to ImGui files
- do not silently change recipe values outside explicit existing Auto Base behavior
- do not increase `EditorModuleRawWorkspace.cpp` responsibility unless the pass is purely transitional
- preserve existing RAW editing behavior unless the pass explicitly changes it
- validate with at least a build or targeted tests if the pass touches behavior

If a pass reveals that a required state model is missing, implement the smallest grouped state needed rather than adding loose fields across `EditorModule`.

## Pass 00: Orientation And Guardrails

Goal:

- confirm current file organization, entry points, and existing helper functions before moving UI.

Tasks:

- reread `current-ui-map.md`
- inspect current RAW workspace files listed in `README.md`
- identify the current left panel width constants and layout split code
- identify current Auto Base, Local Range, View Transform, Image Analysis, and project action render functions
- confirm whether the controls panel, preview panel, and Local Range code have already been split as the Auto Base docs intended

Acceptance:

- implementation agent can name the exact functions/files they will touch
- no code changes unless small comments or compile fixes are required
- no UI behavior changes

## Pass 01: Left Panel Layout Shell

Goal:

- create the spatial foundation for the new RAW controls panel without changing recipe behavior.

Tasks:

- make the controls panel wider by default
- implement adaptive/resizable width if practical
- use default/min/max targets from `implementation-contract.md`
- preserve right gallery and bottom filmstrip modes
- collapse/reduce gallery before graphs or preview become unusable
- tighten root padding and pane gutters toward the new spacing rules
- keep graph surfaces from shrinking below useful heights

Acceptance:

- RAW tab still opens with existing behavior
- controls panel no longer maxes out at the old cramped width
- preview remains usable
- gallery modes still work
- no recipe values change because of this pass

## Pass 02: Compact RAW Top Area And Project Actions

Goal:

- move source/project state and project actions out of the long main editing stack.

Tasks:

- add compact top area with filename, project state badges, recipe/graph state, warning/suggestion count where available
- show Save directly only when relevant
- move structural actions into a More menu or compact management drawer
- shorten labels where approved:
  - `Decompose To Nodes` -> `Convert to Nodes`
  - `Re-adopt Graph As RAW Recipe` -> `Use Graph as Recipe`
  - `Bake / Embed` -> `Embed` if no meaning is lost
- keep critical errors/loading/read-only states visible
- mirror selected RAW/project state in program header if top area is not sticky

Acceptance:

- normal project actions remain reachable
- long source/path/project text no longer dominates the panel
- dangerous or structural actions are not presented as casual primary buttons
- empty/read-only/error states remain understandable

## Pass 03: Auto Base / Readiness Strip

Goal:

- replace the verbose Auto Base block with a compact status-and-action strip.

Tasks:

- render status line:
  - `Readable: Auto fit`
  - `Readable: pending analysis`
  - `Readable: user edited`
  - `Existing project: Auto Base not applied`
- render compact badges:
  - `RAW Exposure unchanged`
  - `WB as shot`
  - suggestion count
  - highlight/warning state
- keep Analyze, Apply, Revert commands available
- move long rationale and percentile details to Diagnostics
- preserve existing Auto Base behavior and revert behavior
- do not add hover preview

Acceptance:

- Auto Base remains visible and reversible
- the UI still clearly says RAW Exposure was not changed by View Fit when relevant
- verbose repeated unchanged lines are compressed into badges/details
- existing saved recipes are not visually treated as silently auto-applied

## Pass 04: Main Controls And Base Light

Goal:

- create the workflow-first Main Controls group and pair RAW Exposure with Display Fit / View Transform.

Tasks:

- create `Main Controls` group
- create `Base Light` section
- move/duplicate-without-conflict RAW Exposure into Base Light as the primary manual control
- move Display Fit / View Transform summary into Base Light
- expose `Refit Display`
- show Display Fit owner/state:
  - `Auto`
  - `Manual`
  - `Locked`
  - `Needs Refit`
- move advanced View Transform controls into a section-level `Advanced` drawer
- keep White Balance compact/open below Base Light
- show Detail/Noise advisory only when relevant

Behavior rules:

- do not continuously refit Display Fit during RAW Exposure drags
- mark `Needs Refit` only after settled analysis and meaningful stats movement
- direct View Transform edits mark it Manual/Locked

Acceptance:

- user can follow exposure-first workflow from the top of the panel
- View Transform is visible as technical Display Fit, not hidden magic
- Display Fit does not fight RAW Exposure
- advanced controls remain reachable

## Pass 05: Graph Controls Group

Goal:

- make Local Range and Finish Tone feel like the primary graph-backed editing surfaces.

Tasks:

- create `Graph Controls` group
- place Local Range and Finish Tone together
- keep both graphs visually dominant
- convert surrounding full-width button rows to compact icon or icon+text rows where clear
- move graph internals and rare controls into per-section `Advanced` drawers
- keep reset as a secondary/overflow action
- keep legacy Local Exposure hidden unless existing recipes require it

Acceptance:

- Local Range graph and Finish Tone graph are easy to find and use
- neither graph is buried below long status text
- existing graph behavior is unchanged
- advanced controls remain reachable

## Pass 06: Local Range Targeting And Color Target UI

Goal:

- make the original tone-plus-color targeting goal visible and understandable.

Tasks:

- rename target controls in UI to `Target` and `Stop Target`
- sample luminance and color together when targeting
- show compact target row when a target exists:

```text
Target EV -2.1    Delta +0.5    Color Target [swatch] On
```

- keep sampled color even when Color Target is off
- allow `Use Color Target` toggle without losing sample
- show display-mapped swatch
- move numeric scene-linear RGB to tooltip/details
- keep Region Mask collapsed unless active
- align overlay names with center view names:
  - `Affected`
  - `Delta`
  - `Mask`

Acceptance:

- user can understand whether Local Range is tone-only or tone-plus-color
- sampled color target is visible without raw RGB clutter
- bright green grass can be separated conceptually from bright sky through Color Target
- existing Local Range recipes still load correctly

## Pass 07: Suggestions Popout Shell

Goal:

- create the dedicated suggestions surface without implementing risky hover previews.

Tasks:

- add suggestion badge/button to compact RAW top area
- open suggestions as an anchored popout or inline expander from that badge
- list active applyable suggestions
- show affected section, short rationale, and apply action
- show compact markers/ghost points near owning controls where existing data supports it
- keep long rationale in popout/Diagnostics, not duplicated in each section
- make applied suggestions show `Applied` until source/analysis changes if state is straightforward
- make closing the popout non-mutating

Do not implement:

- image-changing hover preview
- pinned preview
- batch apply

Acceptance:

- suggestions no longer live as a long inline Auto Base list
- applying a suggestion creates normal visible recipe changes
- owning controls show the result
- suggestion count in top area matches applyable suggestions

## Pass 08: Diagnostics Drawer

Goal:

- move developer-style analysis text out of the normal editing path while preserving transparency.

Tasks:

- create one collapsed `Diagnostics` drawer
- move Image Analysis content into it
- move Auto Base full rationale into it or link from Auto Base details
- show technical/current-frame stats separately
- show fallback/unavailable analysis states
- show withheld/suppressed recommendations as Diagnostics, not disabled broken actions
- allow warning badges to open Diagnostics

Acceptance:

- percentile tables and long rationale no longer dominate the main panel
- all technical transparency remains available
- warnings remain discoverable
- unavailable/fallback analysis does not look like a broken control

## Pass 09: Center Visual Workspace Toolbar

Goal:

- make mask/info layers visible as first-class editor views.

Tasks:

- add compact center viewport mode toolbar
- implement or expose modes backed by existing data:
  - `Final`
  - `Affected`
  - `Delta`
  - `Mask`
  - `Highlight Risk`
- include `Compare` only if existing preview state supports it cleanly; otherwise make it disabled with a tooltip or defer it
- bridge Local Range overlay modes into this toolbar
- show active mode clearly
- ensure changing view mode does not mutate recipe values

Acceptance:

- user can tell what they are looking at
- Local Range masks/overlays feel like editor views, not hidden debug toggles
- unavailable modes are hidden or clearly disabled
- final output view remains the default

## Pass 10: Icon Vocabulary And Density Polish

Goal:

- compact the UI without making it cryptic.

Tasks:

- standardize icons for:
  - Analyze
  - Apply
  - Revert/Undo
  - Refit Display
  - Lock/Manual
  - Target
  - Stop Target
  - Overlay/View
  - More
  - Pin
  - Compare
  - Warning
  - Diagnostics
  - Save
  - Open In Graph
  - Reset
- add tooltips to every icon command
- use visible active/inactive states beyond color alone
- keep text for ambiguous, destructive, or structural actions
- check compact labels at narrow widths

Acceptance:

- icon rows reclaim space
- actions remain understandable
- every icon has a tooltip
- destructive/structural actions are still clear

## Pass 11: UI Validation And Cleanup

Goal:

- verify the new RAW tab is usable before deeper functionality testing resumes.

Tasks:

- run build/tests appropriate for touched code
- capture/check screenshots or manual views at narrow, normal, and wide widths
- validate empty state, new RAW, existing recipe, read-only/error state
- validate right gallery and bottom filmstrip
- validate long filenames and paths
- validate active Local Range, Color Target, and Finish Tone
- validate suggestions available/no suggestions/warnings
- validate Diagnostics drawer
- validate center view modes
- record known limitations in this folder if any pass intentionally defers behavior

Acceptance:

- app builds
- no obvious overlapping text/buttons
- graph surfaces remain usable
- no hidden recipe mutation from layout/view-mode actions
- remaining limitations are documented

## Deferred Passes

These are intentionally after the first UI structure pass:

- hover-to-preview suggestions with temporary recipe/render tokens
- pinned suggestion compare
- persisted Auto Base provenance metadata
- batch suggestion review across gallery images
- richer center workspace layers for WB/noise/detail
- OKLCh color qualifier migration
- new Detail/Noise editable controls

Do not pull these into the first layout overhaul unless the user explicitly asks.
