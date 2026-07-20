# Codex Prompt: First Usable Implementation Pass For Typed Sockets + Layer Masks

Read this guide first from the project root:

`ADVANCED\_NODE\_GRAPH\_COMPOSITOR\_GUIDE.md`

Use it as the high-level architecture guide for evolving the Editor node graph, but do not try to implement the whole guide at once.

## Main Goal

Start the first conservative implementation pass toward a more advanced compositor-style node graph.

The app must remain usable after this pass.

Do not leave fragmented functionality where the UI shows features that do not actually work. If something cannot be fully connected and usable in this pass, keep it hidden, disabled, or leave it for a later phase.

## Current Context

The Editor has already moved from the old sidebar tabs into a custom ImGui node graph.

The graph currently supports image/source nodes, layer nodes, output, scopes, links, selection, zoom/pan, graph interaction, and a render worker for keeping the UI responsive.

The current renderer still mostly behaves like a linear image-processing chain:

`Image -> Layer -> Layer -> Output`

That behavior must continue working.

## First Implementation Target

Implement the foundation for typed sockets and prepare the graph for optional layer masks.

This pass should focus on the graph/data model and validation first, then only add render behavior where it can be made complete and stable.

Required socket types for this pass:

* Image
* Mask
* Value
* Analysis

Layer nodes should conceptually support:

* Image input
* Optional Mask input
* Image output

Scope/analysis nodes should consume Image or Mask data without affecting final output.

Output should consume Image data.

Image/source nodes should produce Image data.

Mask generator nodes should only be added if they are fully usable in this pass. If implemented, start with simple, complete nodes only:

* Solid Mask
* Linear Gradient Mask
* Radial Gradient Mask

Do not add Mix/Merge nodes yet.

Do not add full arbitrary branching yet.

Do not rewrite the whole renderer.

## Usability Requirement

Every implementation pass must leave the editor in a usable state.

This means:

1. Existing projects still load.
2. Existing Image -> Layer -> Output graphs still render.
3. Existing layer settings still work.
4. New graph links must validate correctly.
5. Newly shown UI features must actually work.
6. If masks are shown in the UI, they must affect layer output correctly.
7. If mask rendering cannot be completed safely in this pass, implement typed sockets internally but keep mask sockets hidden or visibly disabled until the next pass.
8. The app should build and launch.
9. The render worker should remain stable.
10. No unrelated tabs/modules should be rewritten.

Do not expose half-finished mask nodes, broken sockets, inactive controls, or placeholder behavior as if they are working features.

## Implementation Strategy

First inspect the current code before editing.

Inspect:

* Editor node graph model
* Editor node graph UI
* Editor node graph serialization
* EditorModule
* EditorRenderWorker
* RenderPipeline
* LayerRegistry
* Layer base classes
* Existing layer RenderUI flow
* Scope/analysis node handling
* Project save/load paths

Then write a short implementation plan before editing.

## Expected Architecture Direction

Move from node-to-node links toward socket-to-socket links.

If the current graph links are still node-level, migrate carefully so links reference specific output and input sockets.

Each node should define its sockets.

Example:

Image node:

* Output: Image

Layer node:

* Input: Image
* Optional input: Mask
* Output: Image

Output node:

* Input: Image

Scope node:

* Input: Image or Mask
* No render-chain output

Solid Mask node, if implemented:

* Output: Mask

Gradient Mask node, if implemented:

* Output: Mask

Socket validation should prevent invalid links.

Examples of valid links:

* Image output -> Layer Image input
* Image output -> Output Image input
* Layer Image output -> Layer Image input
* Layer Image output -> Output Image input
* Mask output -> Layer Mask input
* Image output -> Scope input
* Mask output -> Scope input

Examples of invalid links:

* Mask output -> Image input
* Analysis output -> render-chain input
* Output node -> anything
* Image output -> Mask input unless a deliberate conversion node exists
* Value output -> Image input

## Rendering Rules For This Pass

Preserve the current linear render behavior.

The app should still be able to derive a renderable linear chain from the graph:

`Image -> Layer -> Layer -> Output`

If optional mask sockets are implemented fully:

* A layer with no connected mask must behave exactly as before.
* A layer with a connected mask should apply the layer only where the mask is white/bright.
* Black mask areas should preserve the original input image for that layer.
* Gray areas should partially blend between original and processed layer output.

Prefer a generic post-layer mask blend pass instead of rewriting every layer shader.

Conceptual behavior:

1. Keep original input texture.
2. Run the existing layer shader/effect to get processed texture.
3. If the layer has a connected mask:

   * Blend original and processed using mask.
4. If no mask is connected:

   * Use processed texture directly.

Do not rewrite every existing layer shader unless absolutely necessary.

If generic mask blending cannot be completed safely in this pass, do not expose mask sockets as active usable features yet. Implement the typed socket foundation and validation only.

## Render Worker Rules

Keep the render worker snapshot-based.

The worker should not read mutable UI graph state directly.

The UI/main thread should produce an immutable render snapshot.

The worker should render the newest valid snapshot and drop stale jobs.

Node position, selection, expansion, graph zoom, graph pan, and menu state should not trigger image re-render.

Only render-affecting changes should mark render dirty.

Render-affecting changes include:

* Source image changed
* Output connection changed
* Layer order changed
* Layer settings changed
* Layer visibility changed
* Mask connection changed
* Mask generator setting changed
* Canvas/render size changed

## Save/Load Compatibility

Do not break old projects.

Support old project formats.

If old projects do not contain socket metadata, auto-generate default sockets and valid links from the existing layer pipeline.

New socket metadata should be saved safely and optionally.

If loading a graph with missing/old socket data:

* Repair it where possible.
* Auto-generate sockets from node type.
* Drop invalid links safely.
* Keep the project loadable.

Do not save runtime-only GPU objects.

Do not save OpenGL texture IDs.

## UI Requirements

Keep the current graph usable.

Do not make interaction worse.

Do not auto-connect newly added nodes unless the existing app already does so intentionally. Prefer unconnected new nodes.

Right-click Add menu naming should use "Layer" rather than "Effect."

If mask nodes are implemented:

* Put them under an Add -> Mask or Add -> Generator menu.
* Make them visibly distinct from Image/Layer nodes.
* Allow them to connect only to compatible sockets.
* Provide a way to preview or scope them if practical.

Do not add Mix/Merge nodes in this pass.

Do not add node groups in this pass.

Do not add arbitrary multi-image compositing in this pass.

## Validation Requirements

Add or update graph validation for:

* Missing output node
* Output disconnected
* Links referencing missing nodes
* Links referencing missing sockets
* Invalid socket type connections
* Duplicate links
* Cycles in render chain
* Layer nodes missing layer references
* Mask sockets connected to non-mask outputs
* Scope links accidentally affecting final output
* Deleted nodes leaving stale links behind

Validation should allow disconnected exploratory nodes.

Disconnected nodes should not break the whole project.

Only the graph path required by Output should decide whether final render is valid.

## Deliverables

Before editing files:

1. Read `ADVANCED\_NODE\_GRAPH\_COMPOSITOR\_GUIDE.md`.
2. Inspect the current graph/render/serialization code.
3. Write a implementation plan.
4. Clearly state whether this pass will:

   * Only implement typed socket infrastructure, or
   * Implement typed sockets plus fully usable mask rendering.
5. If mask rendering is not completed, keep mask UI hidden/disabled and explain why.

After editing files:

1. List changed files.
2. Explain how the graph now models sockets.
3. Explain how old projects are handled.
4. Explain how Image -> Layer -> Output still works.
5. Explain what validation was added.
6. Explain whether masks are usable now or only prepared internally.
7. Explain what is intentionally left for the next pass.
8. Run:

`C:\\Windows\\System32\\cmd.exe /c build.bat`

9. Run the existing layer registry validation command if available.
10. Report build result and warnings.

## Hard Restrictions

Do not implement the whole advanced compositor yet.

Do not implement Mix/Merge nodes.

Do not implement arbitrary full branching.

Do not rewrite the whole renderer.

Do not replace ImGui.

Do not change unrelated modules.

Do not remove the render worker.

Do not let UI state be read directly by the worker.

Do not expose non-working UI as if it is complete.

Do not leave the app in a state where normal Image -> Layer -> Output editing is broken.

## Preferred First-Pass Outcome

Best outcome:

* Typed sockets are real in the graph model.
* Links connect sockets instead of just nodes.
* Existing linear render chains still work.
* Socket validation is in place.
* Old projects auto-upgrade/repair cleanly.
* Layer mask sockets are either fully usable or hidden until the next pass.
* The app builds and remains usable.

If there is enough time and risk is low, also implement:

* Solid Mask node
* Linear Gradient Mask node
* Radial Gradient Mask node
* Generic post-layer mask blend pass

Only expose those nodes if the mask actually affects layer output correctly.

