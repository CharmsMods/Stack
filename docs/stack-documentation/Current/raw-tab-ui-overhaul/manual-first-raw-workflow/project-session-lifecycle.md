# Project Session And RAW/Editor Lifecycle

Last updated: August 2, 2026.

## Outcome

Stack's editing surfaces now operate on one active project session. `Editor`,
`RAW`, and `RAW Lab` are views over the same canonical graph rather than owners
of hidden graphs that are swapped on tab changes.

The runtime session kinds are:

```text
Empty
EditorProject
RawPreview
RawProject
```

A normal Editor project visibly locks both RAW presentations. The RAW body is
disabled without destroying the Editor graph, and the title bar exposes one
`Switch to RAW Workspace` action. A clean project closes directly. A dirty
project offers Save & Switch, Discard & Switch, and Cancel; the switch occurs
only after a successful save callback.

A RAW preview or project remains active when the user enters Editor. The same
graph, render pipeline, stage caches, preview state, dirty flag, and save path
remain authoritative, so no `Open in Graph` action or tab-transition render is
required.

## Project Loading Contract

Library and direct-path loaders retain `rawWorkspaceData` and classify RAW
documents even when an older file still says `projectKind: editor`. RAW files
use a one-pixel placeholder source because their canonical source and recipe
live in the RAW metadata and compact graph.

Before destructive deferred-apply steps, Stack validates RAW mode/schema and
the serialized layer array. The previous project then receives a rollback
snapshot containing its canonical graph, immutable shared source buffer,
project identity, thumbnails, dirty state, and RAW recipe metadata. Source
storage is shared rather than copied. A failure during deserialization or
legacy migration restores this snapshot.

Without an active project, RAW source selection may stage a browsing preview.
With an active project, gallery selection and multi-selection are browsing
state only: they never save, unload, decode over, or replace the active project.
Opening a different project remains an explicit staged replacement. Failed
candidates leave the previous project and selection authority intact.

Remembered gallery selection is also browsing state only. Entering RAW or RAW
Lab no longer reopens the selected image's saved project. One click selects,
while double-click or `Open in RAW` explicitly stages the image/project as a
replacement. Clean sessions switch directly; dirty sessions require Save,
Discard, or Cancel. Loading and validation happen before authority moves, and a
failed replacement restores the prior in-memory project. Both RAW presentations
also expose `Close Project`; the folder and multi-selection survive close so
MFD creation remains reachable.

An externally loaded RAW project is pinned to the current session. Its source
is available to RAW controls without replacing the scanned folder, recent
folder, catalog selection, or persisted last-selected source.

## Persistence Contract

Legacy single-RAW saves use:

```text
metadata.projectKind = "raw"
rawWorkspaceSchemaVersion = 2
pipelineData = canonical complete graph
```

`rawWorkspaceData.downstreamGraph` is no longer written. It is read only as a
legacy fallback when canonical `pipelineData` is empty. Unknown nested RAW
metadata remains preserved during save.

New source-set RAW projects use schema 3 with
`rawProjectModel = "source-sets"` and a lazy transactional store. The default
is a `.stackbundle` directory; portable `.stack` v3 is available through Save
As. Graph and source-set state commit as one expected-revision generation.
Exact originals are content-addressed and streamed, not loaded into
`LoadedProjectData`. See `multi-image-raw-project-foundation.md`.

Closing Stack synchronously flushes dirty RAW work. A named normal Editor
project is autosaved and Stack closes only from the successful completion
callback. An unnamed normal project prompts Save & Close, Discard & Close, or
Cancel. Opening another Library project similarly requires Save, Discard, or
Cancel for dirty normal work; dirty RAW work is flushed first.

## Graph Ownership And View Transform

Recipe-backed RAW projects own one compact `RAW Development` node. The owner
node cannot be deleted, while downstream nodes, connections, images, geometry,
and output choices remain ordinary graph content.

Source-set RAW projects own one compact protected `RAW Project Source Set`
node per set. The node stores only its set binding and presentation status. It
has one image output that is explicitly unavailable in this foundation pass;
downstream links serialize and fail clearly without reference pass-through.

The RAW menus no longer expose `Open in Graph`, decomposed-node conversion,
detach, repair, or re-adopt actions. The Editor view already exposes the live
graph.

The built-in View Transform is explicitly switchable. When enabled, it
satisfies the display transform requirement through downstream identity or
geometry nodes, so connecting to Output does not create a duplicate transform.
When disabled, the compact node is scene-linear and Output may insert one
external View Transform using the authored RAW View settings.

## Legacy RAW Modes

`custom-graph` and unknown RAW modes are rejected before the live project is
reset because an exact conversion cannot be guaranteed.

An exact `managed-decomposed` project is converted once to a recipe-backed
compact graph. Stack validates the managed contract, copies RAW Decode plus
Tone/View layer state into the recipe, reconnects every downstream View output,
and writes a new file under the sibling `Stack Migrated Projects` directory:

```text
<original stem> (Compact).stack
<original stem> (Compact) 2.stack
...
```

The original file is never modified. Migration uses a temporary file plus
rename and refuses to open when validation or reconnection cannot preserve the
graph exactly.

## Render And Cache Boundary

Tab changes are presentation-only. They do not serialize/deserialize the
pipeline, clear RAW stage caches, release the accepted image, or schedule a
render. Zones and Curve continue to reuse their settled, fingerprinted graph
scope readbacks; View remains a CPU graph presentation until an authored value
changes. Existing dependency-aware RGBA16F stage caches retain the settled
quality contract described in `interactive-preview-performance-pass.md`.

## Verification

Passed on August 2, 2026:

```text
.\build.cmd
.\build\StackGraphBehaviorTests.exe
.\build\StackNodeMathPhase6Tests.exe
.\build\StackRawEvidenceTests.exe
.\build\Stack.exe --validate-node-math-phase6
.\build\Stack.exe --validate-develop-node-smoke
```

The live graph transaction suite covers clean and dirty normal-project RAW
locks, one RAW graph and identity across repeated Editor/RAW transitions,
compact owner deletion protection, both sides of the built-in View contract
through Flip, durable RAW v2 save/reload, absence of a written
`downstreamGraph`, and preservation of nested future metadata.

Native follow-up remains for pointer-driven Editor/RAW/RAW Lab switching, save
failure and close-during-save dialogs, failed real-RAW loads, external pinned
projects, and one exact legacy managed migration with original/copy inspection.
