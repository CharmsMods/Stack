# Unified Workspace Architecture

## 1. Current State

### Editor Tab
The Editor tab is currently a node-based image compositor that evolved from a linear layer stack. It utilizes a `RenderPipeline` backed by ping-pong FBOs and an `EditorRenderWorker` for asynchronous execution. The state is maintained in a node graph (`EditorNodeGraph::Graph`), which supports Typed Sockets (Image, Mask, Value, Analysis). It heavily focuses on pixel processing, masks, channel operations, and deep effects, but conceptually treats the result as a single output image filling the viewport.

### Composite Tab
The Composite tab acts as a 2D spatial arrangement canvas. It maintains a list of `CompositeLayer` objects (images, shapes, text, nested editor projects). These objects have spatial transforms (position, scale, rotation), Z-index ordering, blend modes, and opacity. Rendering is immediate and hardware-accelerated, focusing on layout, bounding boxes, and object manipulation rather than deep pixel processing.

### Duplicated Functionality
- **Image/Project Loading:** Both tabs have systems for loading images, decoding PNGs, and reading project files.
- **Blending & Opacity:** Both handle blend modes (Multiply, Screen, etc.) and opacity, though Editor does it via shaders and Composite does it via ImGui/OpenGL draw calls.
- **Canvas/Viewport Pan & Zoom:** Both implement viewport navigation.
- **Serialization:** Both have their own save/load formats and state definitions.

## 2. Unification Goal

The goal is to merge the deep pixel-processing logic of the Editor with the spatial 2D arrangement freedom of the Composite tab. 

- **Single-Image Editing Mode:** When only one image/object exists, the workspace behaves like a traditional photo editor. The canvas fits the image, and the graph processes that image's pixels.
- **Multi-Object Compositing Mode:** When multiple objects are added, the workspace seamlessly expands into a 2D canvas. Users can freely move objects around, layer them, and apply different node graphs to different objects.
- **One Shared Core:** This should be one unified module (`WorkspaceModule`), replacing both `EditorModule` and `CompositeModule`. The UI can offer different "views" or "workspaces" (e.g., hiding the node graph for simple layout tasks), but the underlying data model remains the same.

## 3. Canvas/Object Model

To unify the visual and logical layers, we need a robust **2D Canvas Object Model**.

### Object Properties
Every visual entity on the canvas is a `CanvasObject` with the following properties:
- **Transform:** Position (X,Y), Scale (X,Y), Rotation, Size (Width/Height)
- **Visibility & Rendering:** Opacity, Visibility toggle, Z-index (render order), Blend/Composite mode.
- **Source Reference:** A pointer/ID linking the object to a specific output from the Node Graph.
- **Masks/Effects:** Optional linkage to node graph masking or channel operations.
- **Metadata:** Name, locked state, snapping behavior.

### Rendering and Z-Order
The Canvas renderer will draw objects back-to-front based on Z-index. 
- **Texture Processing:** The texture applied to a `CanvasObject` is the fully resolved output of its associated Node Graph chain. 
- **Transforms vs. Processing:** Node graph processing (blurs, color grading, generated textures) happens in *texture space* (local to the object's pixel dimensions). Canvas transforms (position, rotation, scale) happen in *world/canvas space* and are applied during the final draw call to the screen.
- **Viewport:** The canvas supports infinite panning and zooming. Selection is performed via raycasting/point-in-bounds testing on the transformed objects.

## 4. Node Graph Relationship

**Recommendation: A Global Graph with "Canvas Object" Nodes.**

Instead of every object having a hidden, isolated graph, there should be **one global node graph** per project. 
To bridge the graph and the canvas, we introduce a specific node type: the **Canvas Object Node**.

- **How it works:** The node graph handles all pixel generation, mixing, and processing. When you want something to appear on the screen, you route its Image data into a `Canvas Object Node`.
- **Canvas as a View:** The 2D Canvas is simply a visual representation of all `Canvas Object Nodes` currently in the graph, sorted by their Z-index property.
- **Why this is best:** It allows true compositor workflows. You can take one `Image Input Node`, process it, and split it into two `Canvas Object Nodes` (e.g., one serving as a background, one scaled down as an inset). You can use a `Generated Texture Node` to drive both a mask in the graph and a visual overlay on the canvas. 

**Graph = Data/Processing Logic.**
**Canvas = Spatial Arrangement/Layout.**

## 5. Channel And Texture System

The Node Graph will be expanded to support channel-level manipulation.

- **Internal Representation:** Image sockets carry RGBA data. Mask/Value sockets carry single-channel (grayscale) data. When converting RGBA to Mask, luminance is used by default.
- **Split/Combine:** `Channel Split` node takes RGBA and outputs 4 Mask sockets (R, G, B, A). `Channel Combine` takes 4 Mask sockets and outputs an RGBA Image.
- **Channel Operations:**
  - **Swap/Remap:** Route the R output into the B input of a Combine node.
  - **Alpha Management:** `Remove Alpha`, `Premultiply`, `Unpremultiply`.
  - **Masking:** Use the Alpha channel socket directly as a Mask input for a Mix node.
- **UI:** Sockets should be color-coded (e.g., Yellow for RGBA, Gray for Mask/Grayscale, Green for Data/Channels).

## 6. Mask System

Masks are single-channel data streams used to control factors, opacities, and effect strengths.

- **Generators:** Procedural nodes like `Noise`, `Linear Gradient`, `Radial Gradient`, `Solid Value`.
- **Image-to-Mask:** `Luminance Key`, `Hue Key`, `Alpha Extract`.
- **Modifiers (Utility Nodes):** `Invert`, `Levels/Remap`, `Threshold`, `Blur/Feather`, `Transform/UV Map`.
- **Usage:** A mask output connects to the `Mask In` socket of a Layer/Effect node, or the `Factor` socket of a Mix node. 
- **Painting:** Later, a `Paint Mask` node can store internal bitmap data allowing direct brush strokes on the canvas, outputting the result as a mask stream.

## 7. Generated Textures

Generated textures are procedural sources that live inside the project file, avoiding external dependencies.

- **Generators:** `Solid Color`, `Checker/Pattern`, `Noise`, `Scanlines`, `Film Grain`, `Light Leaks`, `Gradients`.
- **Usage:** They output RGBA or Mask data. They can be plugged directly into a `Canvas Object Node` to be arranged spatially, or into a `Mix Node` to act as an overlay (e.g., blending film grain over a photo).
- **Storage:** Generator configurations are serialized in the JSON graph state. No external files are saved.

## 8. Composite Panel Role

With the tabs unified, the old "Composite Tab" disappears, but its essential UI components are redistributed.

**Recommendation: The Layer/Object Stack Panel.**
Finding specific objects on a busy 2D canvas or a massive node graph can be difficult. The workspace should feature a **Layer Stack Panel** (similar to standard image editors).
- This panel lists all `Canvas Object Nodes` in their Z-order.
- It allows quick selection, renaming, locking, hiding, and dragging to change Z-index.
- Selecting an object in this panel selects it on the Canvas *and* highlights its node in the Graph.
- It acts as a bridge for users who are intimidated by nodes but comfortable with layer stacks.

## 9. Recipes / Templates Later

Recipes are pre-configured, serialized node groups that can be dropped into the graph.

- **Concept:** A user selects "Add Glow". Instead of a black-box "Glow" node, the system spawns a `Luminance Key` -> `Blur` -> `Screen Mix` setup and connects it.
- **Implementation:** Recipes are stored as JSON snippets of partial graphs. When applied, the nodes are instantiated, given fresh IDs, and auto-connected to the currently selected node.
- **Examples:** Highlight Glow, Frequency Separation, Channel Glitch, Film Grain Overlay with Vignette.

## 10. UI Organization

A unified but powerful UI layout:

- **Center / Main View:** The 2D Viewport/Canvas. Infinite pan/zoom, visual transform gizmos.
- **Bottom Panel:** The Node Graph. Can be collapsed or expanded. Handles all logic routing.
- **Right Panel (Top):** The Object/Layer Stack. Manages Z-index, visibility, locking.
- **Right Panel (Bottom):** Selected Properties. Context-sensitive. If a Canvas Object is selected, shows X/Y/Scale/Rotation. If a Blur Node is selected, shows Blur Radius.
- **Left Panel:** Library / Assets / Scopes / Previews.

## 11. Implementation Phases

**Phase 1: Foundation (Canvas Object Model in Editor)**
- **Goal:** Introduce `CanvasObject` logic to the Editor without breaking current rendering.
- **Changes:** Add a `Canvas Object Node` type to the graph. The output of the graph must route to this node. 
- **Must still work:** Loading old projects, linear rendering.

**Phase 2: Viewport Transforms**
- **Goal:** Allow the Canvas Object to be moved, scaled, and rotated in the Editor Viewport.
- **Changes:** Update `EditorViewport` to handle spatial transforms and ImGuizmo logic. Apply transforms in the final screen draw call.
- **Must still work:** Pixel-perfect rendering at 1:1 scale when transforms are reset.

**Phase 3: Multi-Object Rendering**
- **Goal:** Support multiple `Canvas Object Nodes` rendering to the viewport.
- **Changes:** Update `RenderPipeline` to evaluate the graph and draw multiple output targets sorted by Z-index. Add the "Object Stack" UI panel.
- **Test:** Two distinct images processed differently, scaled, and arranged side-by-side.

**Phase 4: Advanced Graph Capabilities**
- **Goal:** Add Channel splitting, utility nodes, and generators.
- **Changes:** Add new node definitions and shader programs for Noise, Gradients, Levels, Threshold.
- **Test:** Generate a procedural noise mask and use it to control a blur effect.

**Phase 5: Composite Tab Retirement**
- **Goal:** Remove `CompositeModule` entirely.
- **Changes:** Delete `CompositeModule.cpp/h`. Migrate any missing features (shapes, text) as new Node types in the unified graph. Update AppShell routing.

## 12. Risks And Architecture Choices

- **Risk: Combining too early.** Tearing out the Composite tab before the Editor supports multiple spatial objects will cause feature regression.
  - *Mitigation:* Keep the Composite tab alive until Phase 5 is fully complete and validated.
- **Risk: Confusing Texture Transforms vs Canvas Transforms.** Users might get confused about whether they are scaling an image *before* processing or *after* on the canvas.
  - *Mitigation:* Keep `Canvas Object` properties strictly separated in the UI from a `Transform Node` (which alters UVs during processing).
- **Risk: Graph Complexity.** A single global graph for 50 canvas objects might become an unreadable spiderweb.
  - *Mitigation:* Allow "Grouping" nodes or collapsing sections of the graph. Implement double-click to isolate the graph view to only nodes affecting the selected Canvas Object.

## 13. Final Recommended Architecture

**Recommendation:** 
Unify the system into a single **WorkspaceModule**. 
- **Unify:** Viewport interaction, project serialization, image loading, and the final output stage.
- **Separate Internally:** Keep the `RenderPipeline` (GPU execution) strictly separated from the `NodeGraph` (logical state) and the `CanvasManager` (spatial layout).
- **First Step (Next for Codex):** Implement the `Canvas Object Node` concept within the existing Editor graph, allowing the Editor Viewport to render an image with 2D scale/translation transforms, paving the way for multi-object support. Delay removing the Composite tab until the unified workspace can fully replicate its spatial layout capabilities.
