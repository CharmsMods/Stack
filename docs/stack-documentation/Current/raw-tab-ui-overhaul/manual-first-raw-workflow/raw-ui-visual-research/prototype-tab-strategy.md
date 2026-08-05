# RAW Lab Prototype Tab Strategy

Status: implemented as a text-first prototype; promotion gates remain.

## Decision

Add a temporary top-level `RAW Lab` tab as a second presentation of the same
RAW workspace. Keep the existing `RAW` tab as a frozen visual and behavioral
baseline while the new fixed, borderless layout is developed.

Do not create a second RAW engine, second project model, second recipe, second
render pipeline, or copied set of workspace services.

## Why a Separate Top-Level Tab Is Useful

- The current RAW interface remains usable throughout the redesign.
- Original and experimental presentations can be compared in one executable.
- The Lab can be incomplete without leaving the production RAW tab incomplete.
- Layout experiments do not require temporary compatibility layers inside the
  current dockspace.
- The Lab can be removed or promoted later without carrying a permanent
  Classic/Lab switch inside the final RAW product.

The cost is one more top-level tab and some tab-lifecycle work. Labeling it
`RAW Lab` and optionally hiding it behind an experimental setting prevents it
from looking like a second production workflow.

## What the Code Already Supports

The root program tabs are descriptor-driven in `AppShell`. Each descriptor has
an ID, name, icon texture, and render function. Adding a second RAW renderer is
mechanically straightforward.

The important state already lives in the single `EditorModule`:

- RAW catalog and folder scan;
- selected source;
- active project and recipe;
- thumbnail state;
- project loading and saving;
- render and preview state;
- Local Range targets and overlays;
- Finish Tone and View Transform values; and
- shared edit application through
  `ApplyRawWorkspaceRecipeEditForSelectedSource`.

This makes a second presentation possible without duplicating processing.

## Required RAW-Family Lifecycle

Stack currently treats only `RootTabRaw` as the RAW workspace. The Lab should
not cause the workspace to leave and re-enter when switching between the two
presentations.

Introduce a predicate such as:

```text
IsRawWorkspaceRootTab(tab) =
    tab == RAW or tab == RAW Lab
```

Use it consistently for:

- enter/leave lifecycle;
- dirty-project flushing;
- preview release;
- program-bar status;
- root-tab transition support;
- requests that open a RAW workspace; and
- any keyboard or file-drop path that currently checks only `RootTabRaw`.

The session should enter when crossing from a non-RAW tab into either RAW tab,
remain active when switching RAW ↔ RAW Lab, and leave only when crossing out of
the RAW family.

## Shared Model, Separate Presentation

Recommended ownership:

```text
AppShell
  RAW tab     -> RenderRawWorkspaceUI()       (existing/frozen presentation)
  RAW Lab tab -> RenderRawWorkspaceLabUI()    (new presentation)

EditorModule
  one RAW workspace/catalog
  one selected source
  one active project and recipe
  one preview/render pipeline
  one persistence lifecycle
  separate Classic and Lab layout-only UI state
```

Edits made in RAW Lab should be the same real recipe edits seen in the original
RAW tab. A separate Lab recipe would introduce synchronization, save, undo,
comparison, and migration problems. If experimentation ever needs disposable
values, use an explicit recipe snapshot or temporary version—not a second
implicit source of truth.

Hidden controls must retain their authored values. Opening RAW Lab must never
reset or reinterpret settings that the Lab does not expose.

## Do Not Chase Full Feature Parity

RAW Lab should initially expose only:

1. `Light` — global scene-linear RAW Exposure;
2. `Zones` — the current Local Range/Local Exposure model;
3. `Tone` — Finish Tone curve; and
4. `View` — View Transform/display mapping.

`Zones` is a better provisional label than `Regions` because the current tool
primarily maps scene-EV tonal zones. `Regions` implies a spatial selection even
when no region mask exists. The final name can change after the interaction is
redesigned.

White balance, demosaic, processing version, source warnings, project actions,
and other technical settings continue to affect the shared recipe but do not
need full Lab editors at first. The Lab can provide a small read-only input
status and a direct route to the original RAW tab or future Setup tool.

The goal is workflow validation, not a second checklist implementation.

## Avoiding UI Code Duplication

Do not reuse the existing whole Controls or Tone Graphs panels; that would
import the layout being replaced. Do not copy those large functions either.

Share below the presentation level:

- current source/project resolution;
- editable recipe acquisition and commit;
- RAW Exposure binding;
- Local Range recipe math and graph widget;
- Finish Tone point conversion and graph widget;
- View Transform JSON/default/math helpers;
- target/overlay actions;
- preview texture and viewer-state logic; and
- save/load/error state.

The ideal reusable seam is a small edit context:

```text
Begin RAW edit
  -> selected source
  -> editable recipe copy
  -> can-edit/read-only state

Classic renderer or Lab renderer edits that copy

Commit RAW edit
  -> ApplyRawWorkspaceRecipeEditForSelectedSource
  -> interactive/settled render scheduling
```

Presentation-specific code owns spacing, icon rails, graph sizing, responsive
layout, and advanced-sheet behavior.

## Safe Implementation Sequence

### Phase 1 — Tab shell and shared lifecycle

- Add `RAW Lab` to the program tab bar.
- Reuse the current RAW tab icon initially with an experimental marker or use a
  distinct lab icon.
- Add the RAW-family predicate.
- Add an empty Lab renderer that proves the same folder, selected image, and
  preview remain alive when switching tabs.
- Do not refactor the original controls yet.

### Phase 2 — Preview and four-tool rail

- Build the fixed borderless preview/tool split from the Editor-tab child-pane
  pattern.
- Add the Light/Zones/Tone/View rail.
- Keep Gallery, Setup, and advanced controls out of the first slice.

### Phase 3 — Share edit bindings

- Extract only the small binding/commit seams needed by the Lab.
- Reuse the current graph math/widgets, but give the Lab new presentation
  wrappers.
- Prove that an edit made in one tab appears identically in the other.

### Phase 4 — Lab-specific graph presentation

- Introduce frameless graph rendering, persistent sizing, and contextual
  controls.
- Preserve the underlying recipe contract so both tabs render the same result.

### Phase 5 — Decide promotion

- Keep Classic RAW as the fallback until the Lab covers the intended workflow,
  save/load behavior, comparison states, and performance gates.
- Then either promote the Lab renderer to `RAW` and archive the old renderer,
  or retain the Lab longer if important workflows remain unresolved.

Do not maintain two production RAW interfaces indefinitely.

## Implementation Result

The architecture in this record is now implemented:

- `RootTabRawLab` and `RenderRawWorkspaceLabUI()` provide the second
  presentation.
- `IsRawWorkspaceRootTab()` keeps RAW and RAW Lab inside one workspace
  lifecycle.
- a small shared edit context resolves and commits the complete existing
  recipe without resetting hidden fields;
- Light, Zones, Tone, and View use new presentation code over the same recipe
  contracts;
- Lab-only rail, lower-shelf, tool, secondary-sheet, and Gallery state is separate
  from the original dock state; and
- Gallery can live as a filmstrip, full workspace, or native platform window.

See `../raw-lab-left-rail-implementation.md` for verification and
remaining gates. The implementation does not mean RAW Lab is ready to replace
the original RAW presentation.

## Acceptance Tests

- Switching RAW ↔ RAW Lab does not reload the project or release the preview.
- Both tabs show the same selected source and active recipe.
- RAW Exposure, Zones, Tone, and View edits match numerically and visually
  between tabs.
- Hidden authored values survive entering, editing in, and leaving RAW Lab.
- Save from either presentation writes one project state.
- The original RAW presentation remains visually unchanged during early Lab
  work.
- Lab layout state does not pollute Classic dock/layout state.
- Removing or disabling RAW Lab leaves project data fully compatible.
