# RAW Tab UI Implementation Contract

## Purpose

This is the controlling document for the first RAW tab UI overhaul implementation passes.

The design docs in this folder explain the reasoning. This document turns that reasoning into defaults that an implementation agent should follow unless the user explicitly changes direction.

The first implementation should make progress without trying to solve every future interaction at once. Build the structure, state language, and safe layout first. Leave advanced temporary-preview behavior for a later pass unless the necessary preview state is already safe and easy to isolate.

## Implementation Status

The folder is now ready for implementation passes with these constraints:

- Implement the selected defaults below.
- Preserve the technical behavior from `docs/auto-raw-base`.
- Do not convert Auto Base into hidden auto-enhance.
- Do not implement every possible idea from `interaction-ideas.md` in the first pass.
- If a detail conflicts between documents, follow this implementation contract first, then `human-workflow-notes.md`, then `side-panel-redesign-spec.md`, then the exploratory notes.

## First Implementation Scope

The first UI rollout should include:

- resizable/adaptive left controls panel
- compact RAW top area
- compact Auto Base / readiness strip
- suggestions popout/expander shell
- Main Controls group
- `Base Light` section with `RAW Exposure` and `Display Fit / View Transform`
- White Balance section compacted into Main Controls
- Graph Controls group containing Local Range and Finish Tone
- Diagnostics drawer replacing the always-inline Image Analysis block
- project/source action compaction into a small action row plus overflow menu
- icon-first command rows where actions are familiar enough
- center viewport view-mode toolbar shell using existing preview/overlay data where possible

The first UI rollout should not require:

- hover-to-preview image-changing suggestions
- pinned suggestion comparison
- persisted Auto Base provenance metadata
- new RAW analysis algorithms
- new Local Range math
- new denoise/detail controls
- OKLCh color qualification migration
- batch suggestion application across multiple gallery images

Those can be implemented later once the structure and state rules are stable.

## Canonical Workflow

The UI should support this human workflow:

```text
Pick the RAW.
Stack makes it readable.
You set the base light.
You fix parts of the image.
You shape the tone.
You inspect masks/info layers when needed.
You save or move on.
```

Use the plain language model from `human-workflow-notes.md`:

- `Auto`: what Stack already did for you.
- `Manual`: what you are steering.
- `Suggested`: what Stack thinks might help.
- `Advisory`: what Stack wants you to know, but cannot safely apply.
- `Live`: what follows your hand while editing.
- `Settled`: what updates after you pause.

## Layout Defaults

### Left Controls Panel

Implement a wider, more flexible controls panel.

Defaults:

- default width: `420 px`
- minimum width: `340 px`
- maximum width: `520 px`
- persist width in app state if the existing UI state system makes this straightforward
- if persistence is risky, keep the runtime resizable behavior and add persistence later

Responsive behavior:

- collapse or reduce the gallery before making the preview or graph controls unusable
- preserve graph minimum heights
- avoid long wrapped text in the compact top area

### Root Spacing

Use the design target from the side-panel spec:

- root RAW tab padding: `12-16 px`
- pane gutter: `10-14 px`
- icon row gaps: `4-8 px`
- icon buttons: stable square hit targets around `28-34 px`

Exact constants may follow existing theme scale, but the implementation should not keep the old feeling of controls touching edges or floating too far away from the content.

### Sticky Top Area

Preferred behavior:

- keep the compact RAW top area visible while the left panel scrolls if Dear ImGui makes that maintainable

Fallback behavior:

- if sticky behavior is awkward or fragile, mirror the selected source, warning count, suggestion count, and save/render state in the program header bar

Do not block the first pass on perfect sticky behavior.

## Program Header Bar

The header bar should orient and alert, not become another controls panel.

Show compact state when available:

- selected RAW filename or project name
- save/load/render state
- shortened workspace path/name
- warning count
- suggestion count
- compact `Auto Base` or `User edited` badge when relevant

Do not put detailed editing controls, long rationale, or project management menus in the header.

## Compact RAW Top Area

This area is always visible at the top of the RAW controls panel or mirrored by the header fallback.

Required content:

- selected RAW filename, ellipsized
- project state badge:
  - `New project`
  - `Existing project`
  - `Embedded`
  - `Read-only`
  - `Saving`
  - `Loading`
- recipe/graph state badge where known:
  - `Recipe`
  - `Managed graph`
  - `Custom graph`
- primary suggestion badge/button:
  - `No suggestions`
  - `1 suggestion`
  - `3 suggestions`
  - `1 warning`
- compact actions:
  - Save when relevant
  - More menu

Project management actions go in the More menu unless they are urgent:

- Open In Graph
- Save
- Decompose To Nodes / Convert to Nodes
- Validate RAW Chain
- Re-adopt Graph As RAW Recipe / Use Graph as Recipe
- Detach From RAW Tab
- Repair RAW Chain
- Relink
- Bake / Embed / Embed

## Suggestions Popout / Expander

### Placement

Default:

- primary entry point is the suggestion badge/button in the compact RAW top area
- the program header may mirror the count but should not be the primary interaction anchor
- open the suggestions UI as an anchored popout or inline expander from the RAW top area

V1 behavior:

- closable popout/expander
- not required to be pinnable
- design the code/state so a pinned mode can be added later

### Suggestion Item Content

Each suggestion item should show:

- short action label:
  - `Open shadows`
  - `Protect sky`
  - `Brighten foliage`
  - `Suggested WB`
  - `Raise RAW Exposure +0.3 EV`
- affected section:
  - `Base Light`
  - `White Balance`
  - `Local Range`
  - `Display Fit`
  - `Detail`
- apply action
- short caution or confidence only when it changes the decision
- short rationale in the item or tooltip
- optional affordance to inspect a related mask/info view

Do not duplicate the full rationale beside the owning control.

### V1 Suggestion Behavior

Implement click-to-apply first.

Rules:

- clicking applies normal visible recipe values
- applying a suggestion creates one normal undoable edit if the current undo/edit system supports it
- the owning control section must show the resulting editable value
- applied suggestions may remain listed as `Applied` until source or analysis changes
- dismissed suggestions may be hidden for the current source/analysis hash if dismissal state is easy to keep

Do not implement image-changing hover preview in v1 unless temporary recipe/render state is already safe.

### Future Hover Preview Contract

When hover preview is implemented:

- image-changing previews start after a short delay, around `150-250 ms`
- graph ghost markers may appear instantly
- hover preview must not dirty the project
- hover preview must not permanently mutate the recipe
- moving away, pressing Esc, closing the popout, switching sources, or switching suggestions cancels the preview
- late render results must be ignored if their preview token no longer matches the active preview
- click-to-apply converts the preview into normal recipe values
- pinned preview remains temporary until applied

## Main Controls Group

Main Controls contains global setup controls:

1. `Base Light`
2. `White Balance`
3. `Detail / Noise` advisory only when relevant

## Base Light

This is the default top manual editing section.

Use this section name:

```text
Base Light
```

It contains:

- `RAW Exposure`
- `Display Fit / View Transform`
- compact display-fit state
- `Refit Display`
- lock/manual state if available
- advanced View Transform controls in a section-level `Advanced` drawer

Human meaning:

- RAW Exposure is what the user steers.
- Display Fit / View Transform is how Stack keeps the preview readable.
- Display Fit does not replace RAW Exposure.
- Display Fit should not hide the visible effect of RAW Exposure by constantly refitting.

Default summary row example:

```text
RAW Exposure +0.00 EV    Display Fit: Auto
```

If stale:

```text
RAW Exposure +0.35 EV    Display Fit: Needs Refit
```

### Display Fit Behavior

Do not continuously refit Display Fit while the user edits.

Allowed automatic behavior:

- Auto Base may apply an initial Display Fit on new/default RAW recipes.
- Auto Base may mark Display Fit as auto-owned.
- Stack may update analysis after settled renders.

Manual/user behavior:

- user can click `Refit Display`
- user can edit View Transform controls directly
- direct user edits mark Display Fit / View Transform as `Manual` or `Locked`

Staleness behavior:

- do not show `Needs Refit` during live drag
- after a settled render, mark `Needs Refit` if current-frame stats have moved meaningfully from the stats used for the current fit
- starting thresholds:
  - median EV shifted by about `0.33 EV` or more
  - white/high percentile EV shifted by about `0.50 EV` or more
  - display clipping/readability changed enough to produce a warning
- keep these thresholds adjustable in code; they are UI behavior defaults, not color-science absolutes

If Display Fit is `Manual` or `Locked`, do not nag. Put stale-fit details in Diagnostics unless clipping/readability is severe.

## White Balance

Default:

- compact/open in Main Controls
- camera/as-shot remains the default when available
- alternate WB appears as a suggestion item in the suggestions popout
- manual/custom WB wins

Compact labels:

- use `WB` only in badges or tight summaries
- use `White Balance` as the section title
- use `As Shot` for camera metadata default

## Detail / Noise

Default:

- do not show a permanent placeholder section unless real editable controls exist
- show relevant advisory rows only when high ISO, shadow lift, or recommendation data makes it useful
- never show disabled denoise buttons that look broken
- if no visible editable controls exist, use `Advisory`, not `Apply`

## Graph Controls Group

Graph Controls contains:

1. `Local Range`
2. `Finish Tone`

These should be visually grouped because both are graph-backed editing surfaces.

Keep both graphs first-class. Do not bury either graph behind an advanced disclosure.

## Local Range

Required v1 structure:

- Local Range graph visible when active, suggested, or open by default
- compact section header with On/Off state and main actions
- `Target` / `Stop Target` icon command
- overlay/view mode control
- selected point or target sample compact row when relevant
- Color Target compact row when sampled/enabled
- Region Mask in `Advanced` unless already active
- legacy Local Exposure only when an existing recipe requires it

### Targeting And Color

When targeting from the image:

- sample luminance and color together
- drag up/down primarily changes Local Range delta EV
- keep sampled color even when Color Target is off
- let the user toggle `Use Color Target` without losing the sample
- show a compact sample row when there is an active/recent target

Example:

```text
Target EV -2.1    Delta +0.5    Color Target [swatch] On
```

Manual targeting default:

- sample color always
- do not force color limiting on unless the user enabled `Use Color Target`

Suggestion default:

- suggestions like `Brighten foliage` and `Protect sky` may enable Color Target by default because color separation is the purpose of the suggestion

Swatch rule:

- show a display-mapped swatch for usability
- do not imply it is exact scene-linear color
- numeric scene-linear RGB belongs in tooltip/details/Diagnostics

## Finish Tone

Required v1 structure:

- graph always visible when the section is open
- compact channel/mode row
- domain selector near the graph title or graph controls
- reset as a small secondary action or overflow action
- graph bounds and curve internals in `Advanced` unless needed

Keep `Finish Tone` as the title for now. `Tone Graph` may appear as a subtitle or tooltip if useful, but do not rename the persisted concept in this UI pass.

## Center Visual Workspace

The center viewport should become a mode-based visual workspace.

V1 modes to expose where existing data supports them:

- `Final`
- `Compare`
- `Affected`
- `Delta`
- `Mask`
- `Highlight Risk`

Rules:

- the active view mode must be visible
- mode controls should be compact icon buttons with tooltips
- Local Range overlay modes should be bridged into this mode system rather than remaining a separate confusing overlay concept
- if a mode is unavailable, hide it or show it disabled with a tooltip explaining the missing data
- changing center view mode should not mutate the recipe
- a future suggestion hover may temporarily switch/augment the view, but v1 does not need that behavior

If `Compare` is not yet supported by existing preview state, provide the toolbar slot only if it can be disabled cleanly. Do not fake a comparison view.

## Diagnostics

Default:

- one collapsed `Diagnostics` drawer in the side panel
- warnings may link/open the drawer
- Auto Base details may also open it

Diagnostics owns:

- Image Analysis details
- technical/current-frame stats
- highlight signals
- missing metadata
- fallback analysis notes
- suppressed/withheld recommendation rationale
- full Auto Base rationale

Do not keep percentile tables and long rationale in the main editing stack.

## Error, Pending, and Withheld States

Use these states where relevant:

- `Analyzing`
- `Analysis unavailable`
- `Using fallback stats`
- `Metadata incomplete`
- `Preview pending`
- `Suggestion stale`
- `Source mismatch`
- `Render failed`
- `Withheld`

Rules:

- unsafe suggestions should be suppressed when analysis is unavailable or from the wrong stage
- suppressed items may appear in Diagnostics as `Withheld`
- the suggestion badge should count only applyable suggestions
- warnings/advisories should be counted separately from applyable suggestions when possible

## Undo, Revert, and Provenance

Separate these concepts:

- normal undo
- applied suggestion
- dismissed suggestion
- Auto Base revert
- full recipe reset

Rules:

- applying a suggestion is a normal user edit
- Auto Base revert restores only values Auto Base actually auto-set
- Auto Base revert must not overwrite later manual values unless the user explicitly asks for a full recipe restore
- if this exact selective revert is not safely implementable yet, keep the existing broader revert behavior but label it honestly and document the limitation in code/UI
- after reload, do not claim `Auto Base applied` unless provenance is known
- use neutral reload language such as `Existing recipe`, `Display Fit set`, or `Recipe loaded`

Persisted Auto Base provenance is deferred for the first UI pass.

## Default Open Sections

Initial default:

- Compact RAW Top Area: always visible
- Auto Base / Readiness: always visible
- Base Light: open
- White Balance: compact/open
- Local Range: open when active or suggested, otherwise compact
- Finish Tone: open
- Diagnostics: collapsed
- Crop / Rotate: collapsed unless active
- Output: collapsed unless active

If vertical space is short, Local Range and Finish Tone should keep useful graph sizes before optional text expands.

## Naming Defaults

Use these names:

| Concept | UI Name |
|---|---|
| Combined exposure/readability section | `Base Light` |
| Scene-linear exposure | `RAW Exposure` |
| View mapping | `Display Fit / View Transform` |
| Auto fit current frame | `Refit Display` |
| Target from image | `Target` |
| Stop image target | `Stop Target` |
| Trust overlay | `Overlay` or center view mode |
| Affected tones overlay | `Affected` |
| EV delta map | `Delta` |
| Range/color mask | `Mask` |
| Color qualification | `Color Target` |
| Finish tone curve | `Finish Tone` |
| Image analysis | `Diagnostics` |

Keep technical terms visible in tooltips/details where a short UI name is used.

## Icon Vocabulary

Define and reuse one icon vocabulary.

Actions that should be icon-first:

- Analyze
- Apply
- Revert / Undo
- Refit Display
- Lock / Manual
- Target
- Stop Target
- Overlay / View
- More
- Pin
- Compare
- Warning
- Diagnostics
- Save
- Open In Graph
- Reset

Rules:

- every icon button needs a tooltip
- active states must be visible beyond color alone
- destructive or structural actions need text in menus or confirmation
- if an icon is ambiguous after one tooltip, use icon plus text or text

## Validation Required Before Calling The UI Pass Done

Manual validation:

- normal width, narrow width, and wide width screenshots
- long filename and long workspace path
- no selected RAW empty state
- new default RAW with Auto Base
- existing saved recipe
- local range active
- local range color target active
- suggestions available
- no suggestions available
- warnings/advisories available
- diagnostics unavailable/fallback state
- right gallery expanded/collapsed
- bottom filmstrip mode

Interaction validation:

- panel resizing does not crush graphs
- gallery collapses before preview/graphs become unusable
- suggestion popout opens from the RAW top area and stays onscreen
- clicking a suggestion applies a visible recipe value
- closing suggestions does not mutate recipe values
- Refit Display does not happen continuously while dragging RAW Exposure
- Local Range Target samples EV/color and shows a compact row
- Diagnostics drawer replaces long inline Image Analysis text

Automated tests:

- run existing build/tests that are normally used for RAW workspace changes
- add tests only where the UI pass changes recipe state, serialization, ownership, or suggestion application behavior

At minimum, verify the app builds. If behavior tests exist for graph/recipe changes touched by the pass, run them.
