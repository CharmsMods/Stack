# Advanced Node Graph Compositor Guide

## Purpose

This document explains how to evolve the current editor from a simple linear layer-based image editor into a more capable node-based image compositor.

The goal is not to rewrite the whole renderer immediately. The goal is to give the project a clear technical direction so future implementation passes can be planned safely, progressively, and without breaking existing project compatibility.

The current editor node graph is still close to the old layer stack model. It represents a mostly linear image-processing chain:

```text
Image -> Layer -> Layer -> Layer -> Output
```

That is a good starting point.

The long-term direction is to make the editor support more than one image input, optional masks, procedural textures, generated control maps, utility nodes, merge nodes, analysis nodes, and eventually true dependency-graph rendering.

A more advanced graph could look like this:

```text
Image A ---------> Color Grade --------\
                                        -> Mix -> Output
Image B -> Blur -----------------------/
Mask / Noise / Gradient -> Mix Factor -/
```

Or:

```text
Image -> Layer -> Layer -> Output
          ^
          |
       Mask Texture
```

The main idea is that a node graph should not only describe "what effects come next." It should describe how image data, masks, values, generated textures, and analysis data flow through the editor.

---

## Current State Assumption

The current application has recently moved from sidebar tabs into a custom node graph UI.

The current graph can represent:

- Image/source nodes
- Layer nodes
- Output node
- Scope/analysis nodes
- Explicit links
- A single derived render chain
- Asynchronous editor rendering through a render worker
- Graph-based layer selection
- Basic node movement, connection, deletion, zoom, pan, and validation

The current graph is still primarily a visual and interaction layer over the existing sequential pipeline.

Equivalent layer order should still produce equivalent image output.

Existing project files should keep loading.

The old linear pipeline behavior should not be broken while the graph evolves.

---

## Main Concept: Nodes Are Data Processors

A node is a processing unit.

A node can:

- Produce image data
- Modify image data
- Generate a mask
- Modify a mask
- Generate values
- Combine inputs
- Analyze an image
- Output final render data

The important shift is that nodes should not be thought of only as "layers."

A layer is one type of node.

Other node types can include:

- Image input nodes
- Procedural texture nodes
- Mask generator nodes
- Utility nodes
- Mix/merge nodes
- Preview nodes
- Scope nodes
- Value nodes
- Coordinate/mapping nodes

A node graph becomes powerful when each node has typed inputs and outputs.

---

## Main Concept: Typed Sockets

A socket is a connection point on a node.

Sockets should have types.

The socket type determines what kind of data is allowed to flow through the connection.

Suggested socket types:

```text
Image / Color      RGBA image texture
Mask / Factor      grayscale 0.0 to 1.0 area-control texture
Value              one scalar number
Vector             2D, 3D, or 4D numeric vector
UV / Coordinates   texture lookup coordinates
Depth              distance/depth map
Normal             surface normal map
ID / Selection     object/material/region selection map
Analysis           special non-render output for scopes or diagnostics
```

Early implementation should start with only the most useful socket types:

```text
Image
Mask
Value
Analysis
```

The graph UI should visually distinguish socket types.

Examples:

```text
Image socket:
- Carries full RGBA texture data.
- Used for main image processing.

Mask socket:
- Carries grayscale area-control data.
- Used to control where effects apply.

Value socket:
- Carries a single number or parameter.
- Later useful for connecting procedural controls.

Analysis socket:
- Used for scope nodes or diagnostics.
- Should not affect final render output.
```

Typed sockets allow the graph to validate connections.

Valid examples:

```text
Image output -> Layer image input
Image output -> Output image input
Mask output  -> Layer mask input
Image output -> Scope input
Value output -> Layer strength input
```

Invalid examples:

```text
Output image output -> anything
Mask output -> Image input
Analysis output -> render chain
Layer image output -> Value input
```

The graph should reject invalid links immediately or show a clear invalid-link state.

---

## Main Concept: Image Data Versus Control Data

A major design change is separating image data from control data.

Image data is what the user sees as a visual result.

Control data is used to decide how an operation behaves.

Examples of control data:

- Mask texture
- Strength value
- Blur radius map
- Distortion direction map
- Depth map
- Object selection map
- Noise texture
- Gradient texture
- Luminance-derived selection

Control data can be visible if previewed, but its main purpose is to influence other nodes.

Example:

```text
Image -> Blur -> Mix -> Output
Image --------> Mix
Mask ---------> Mix Factor
```

In this example, the mask controls where the blurred version appears.

---

## Main Concept: Masks And Area Control

A mask is usually a grayscale texture.

Typical interpretation:

```text
White = full effect
Black = no effect
Gray  = partial effect
```

In normalized shader terms:

```text
0.0 = no effect
1.0 = full effect
0.5 = half effect
```

A mask allows an effect to apply only to certain parts of an image.

Without masks:

```text
Image -> Color Grade -> Output
```

The whole image is color graded.

With a mask:

```text
Image -> Color Grade -> Output
Mask  -> Color Grade mask input
```

Only the masked region is color graded.

Basic shader behavior:

```glsl
vec4 original = texture(inputImage, uv);
vec4 processed = ApplyLayerEffect(original);

float mask = texture(maskTexture, uv).r;
float amount = clamp(mask * strength, 0.0, 1.0);

vec4 result = mix(original, processed, amount);
```

If no mask is connected, the layer should behave exactly as it does now.

Equivalent logic:

```glsl
float mask = hasMask ? texture(maskTexture, uv).r : 1.0;
```

This preserves old behavior.

---

## Why Mask Inputs Are The Best Next Step

The safest next step toward a powerful graph is adding optional mask sockets to layer nodes.

This gives the editor a large capability increase without requiring full arbitrary branching yet.

Current layer node:

```text
[Image In] -> Layer -> [Image Out]
```

Recommended next version:

```text
[Image In] -> Layer -> [Image Out]
[Mask In] --------^
```

The mask socket is optional.

If no mask is connected:

```text
Layer behaves exactly like before.
```

If a mask is connected:

```text
The layer blends between original and processed result based on the mask.
```

This is a good bridge between:

```text
simple layer stack
```

and:

```text
true compositor graph
```

because it introduces typed secondary inputs while keeping the render chain mostly linear.

---

## Factor Inputs

A factor is a general-purpose strength controller.

A mask is often a factor texture.

A factor can be:

- A constant slider value
- A grayscale texture
- A generated procedural texture
- A derived map from the image
- A connected value node

Examples:

```text
Mix factor
Blur strength
Distortion amount
Bloom visibility
Color grade strength
Sharpen amount
Glitch intensity
```

A basic UI may expose this as:

```text
Strength: 0.75
Mask input: optional
```

Then the actual amount becomes:

```glsl
float amount = strength;

if (hasMask)
{
    amount *= texture(maskTexture, uv).r;
}
```

Later, a value socket can override or multiply the slider.

---

## Procedural Textures

A procedural texture is generated by code rather than loaded from an image file.

Procedural texture nodes are extremely useful because they let the user create masks and patterns without importing files.

Useful procedural texture nodes:

```text
Solid Mask
Gradient Mask
Radial Gradient
Noise Texture
Voronoi Texture
Checker Texture
Wave Texture
Cloud Texture
Scratch Texture
Film Grain Texture
Edge Mask
Luminance Mask
Hue Range Mask
Saturation Mask
Alpha Mask
```

Procedural textures can be used directly as images or as masks.

Example:

```text
Noise Texture -> ColorRamp -> Layer Mask
Image -> Glare Layer -> Output
```

The glare appears only where the noise/color ramp allows it.

Example:

```text
Radial Gradient -> Vignette Mask
Image -> Color Grade -> Output
```

The color grade is strongest near the center or edges depending on the gradient.

---

## Generated Mask Nodes

Mask nodes should output Mask-type textures.

Useful mask generators:

### Solid Mask

Outputs a constant grayscale mask.

Settings:

```text
Value: 0.0 to 1.0
```

Use cases:

- Testing mask connections
- Turning a masked effect fully on or off
- Creating a simple factor source

### Linear Gradient Mask

Outputs a linear black-to-white gradient.

Settings:

```text
Angle
Start position
End position
Invert
Softness
Repeat / clamp mode
```

Use cases:

- Fade effects across the image
- Apply blur only to one side
- Make color grading stronger at the top/bottom

### Radial Gradient Mask

Outputs a circular or elliptical gradient.

Settings:

```text
Center X/Y
Radius
Feather
Aspect ratio
Invert
```

Use cases:

- Vignettes
- Center-focused effects
- Spotlight-style masks
- Radial blur control

### Noise Mask

Outputs grayscale procedural noise.

Settings:

```text
Scale
Detail/octaves
Roughness
Seed
Contrast
Brightness
Animation offset later if needed
```

Use cases:

- Organic effect breakup
- Film damage
- Uneven bloom
- Glitch masks
- Texture overlays

### Luminance Mask

Generates a mask based on brightness of an input image.

Settings:

```text
Low threshold
High threshold
Softness
Invert
```

Use cases:

- Select bright regions for bloom
- Select shadows for color grading
- Isolate highlights

Example:

```text
Image -> Luminance Mask -> Bloom Mask
Image -> Bloom -> Output
```

### Hue Range Mask

Generates a mask based on color hue.

Settings:

```text
Target hue
Hue width
Softness
Saturation minimum
Invert
```

Use cases:

- Select blue sky
- Select red lights
- Select green foliage
- Color-targeted effects

### Alpha Mask

Uses image alpha as mask.

Settings:

```text
Invert
Threshold
Softness
```

Use cases:

- Respect transparent regions
- Mask based on cutout images
- Composite assets cleanly

---

## Utility Mask Nodes

After generator nodes, utility nodes become important.

Utility nodes take one mask or image and transform it into another useful mask/image.

Useful utility nodes:

```text
Invert
Levels
Threshold
Clamp
Blur Mask
Sharpen Mask
Dilate
Erode
ColorRamp
Channel Extract
Channel Combine
Normalize
Remap
```

### Invert

```text
input = 0.2
output = 0.8
```

Use cases:

- Reverse mask area
- Apply effect everywhere except selected region

### Threshold

Turns soft grayscale into harder black/white.

```text
if input > threshold:
    output = 1.0
else:
    output = 0.0
```

Use cases:

- Hard selections
- Cutoff masks
- Posterized control maps

### Levels / Remap

Adjusts black point, white point, gamma, and contrast.

Use cases:

- Make masks stronger
- Make masks softer
- Expand narrow luminance ranges

### ColorRamp

Maps grayscale values to custom colors or control values.

For masks, it can remap grayscale to a more controlled falloff.

Use cases:

- Fine mask shaping
- Procedural texture control
- Stylized ramps

### Blur Mask

Blurs only the mask/control texture, not the image.

Use cases:

- Feather selections
- Smooth rough masks
- Create soft transitions

---

## Multiple Image Inputs

The editor should eventually support more than one image input.

Possible image nodes:

```text
Image Input
Texture Input
Generated Image
Solid Color
Transparent Image
Render Result
Previous Output / Cached Output
```

A multi-image graph allows compositing:

```text
Image A -> Mix A
Image B -> Mix B
Mask    -> Factor
Mix     -> Output
```

Important distinction:

- Multiple image nodes can exist in the graph.
- Not every image node has to be connected to the final output.
- Disconnected nodes should be allowed.
- A graph should only render what is connected to the output or needed by connected scopes/previews.

---

## Branching

Branching means one node output feeds multiple destinations.

Example:

```text
Image -> Blur -> Mix A
      -> Edge Detect -> Mix B
```

Branching is powerful but increases renderer complexity.

It requires:

- Dependency graph traversal
- Intermediate texture caching
- Cycle detection
- Correct execution order
- Validation
- Better memory management

Branching should be introduced carefully.

A good first form of branching is analysis-only branching:

```text
Image -> Layer -> Output
             \
              -> Scope
```

This does not affect final output.

A good second form of branching is mask-only branching:

```text
Image -> Luminance Mask -> Layer Mask
Image -> Layer -> Output
```

This affects the layer but still keeps the main image chain mostly linear.

Full image branching should wait until merge nodes exist.

---

## Merge / Mix Nodes

A merge node combines two or more image streams.

This is what turns the editor into a true compositor.

Basic Mix node:

```text
Image A -> A input
Image B -> B input
Mask    -> Factor input
Mix     -> Image output
```

Common blend modes:

```text
Normal
Alpha Over
Add
Subtract
Multiply
Screen
Overlay
Soft Light
Hard Light
Difference
Darken
Lighten
Min
Max
Mask Mix
```

Basic shader mix:

```glsl
vec4 a = texture(imageA, uv);
vec4 b = texture(imageB, uv);
float factor = texture(maskTexture, uv).r;

vec4 result = mix(a, b, factor);
```

Multiply example:

```glsl
vec4 blended = a * b;
vec4 result = mix(a, blended, factor);
```

Add example:

```glsl
vec4 blended = a + b;
vec4 result = mix(a, blended, factor);
```

Alpha-over example:

```glsl
vec3 rgb = b.rgb * b.a + a.rgb * (1.0 - b.a);
float alpha = b.a + a.a * (1.0 - b.a);
vec4 blended = vec4(rgb, alpha);
vec4 result = mix(a, blended, factor);
```

Merge nodes should be delayed until the graph model and renderer can evaluate non-linear dependencies.

---

## Coordinate And Mapping Nodes

Textures need coordinates.

For a 2D editor, the basic coordinate is UV:

```text
uv.x = 0.0 left, 1.0 right
uv.y = 0.0 bottom/top depending on convention
```

A mapping node modifies coordinates before they are used by another texture.

Possible coordinate/mapping controls:

```text
Translate X/Y
Scale X/Y
Rotate
Tile
Mirror
Clamp
Distort
Offset
Aspect correction
```

Example:

```text
UV -> Mapping -> Noise Texture -> Mask input
```

This lets the user scale or rotate the noise mask without changing the image itself.

Coordinate nodes are not required for the first mask implementation, but the socket system should leave room for them later.

---

## Scope / Analysis Nodes

Scope nodes are not render-effect nodes.

They observe image data.

Examples:

```text
Histogram
RGB Parade
Vectorscope
Waveform
Alpha viewer
Mask viewer
Channel viewer
Debug preview
```

Scope nodes should be allowed to connect to image outputs.

Example:

```text
Image -> Color Grade -> Output
                  \
                   -> Histogram
```

Scope nodes should not affect final render output.

They should be treated as analysis consumers.

Important rules:

```text
Scope nodes can consume Image or Mask data.
Scope nodes should not output render-chain data.
Scope nodes should not create cycles.
Scope nodes should not change layer order.
Scope nodes should not trigger expensive full graph recomputation more often than needed.
```

A scope connected to an intermediate layer result may require rendering up to that point.

That should be cached and throttled.

---

## Preview Nodes

A Preview node is similar to a Scope node but shows a small image preview.

Possible behavior:

```text
Image/Mask input -> Preview node
```

Preview nodes should not affect final output.

They are useful for debugging masks and procedural textures.

Preview nodes are especially important once masks and generated textures exist.

Examples:

```text
Noise Mask -> Preview
Luminance Mask -> Preview
Layer Output -> Preview
```

Preview nodes should be optional and throttled.

---

## Layer Nodes With Optional Masks

This is the recommended near-term implementation target.

Each layer node should have:

```text
Image input
Optional Mask input
Image output
Existing settings UI
```

The existing RenderUI() function can remain the settings UI.

The renderer should apply the mask after the layer computes its processed output.

Conceptually:

```text
original image -> layer effect -> processed image
original image + processed image + mask -> final layer output
```

Shader behavior:

```glsl
vec4 original = texture(inputImage, uv);
vec4 processed = ApplyEffect(original);

float mask = 1.0;

if (hasMask)
{
    mask = texture(maskTexture, uv).r;
}

float amount = clamp(mask * layerStrength, 0.0, 1.0);
vec4 outputColor = mix(original, processed, amount);
```

This may be implemented as:

1. Per-layer shader support
2. A generic post-layer mask blend pass
3. A wrapper around existing layer execution
4. A common layer base helper

The safest migration path is usually a generic wrapper/blend pass so every old layer does not need to be rewritten immediately.

---

## Generic Mask Blend Pass

A generic mask blend pass can preserve old layer behavior.

Current layer execution:

```text
inputTexture -> layer shader -> outputTexture
```

New masked execution:

```text
inputTexture -> layer shader -> processedTexture
inputTexture + processedTexture + maskTexture -> mask blend pass -> outputTexture
```

If no mask is connected:

```text
processedTexture becomes outputTexture directly
```

or:

```text
mask is treated as 1.0
```

This avoids rewriting every effect shader.

Possible generic mask blend shader:

```glsl
uniform sampler2D uOriginal;
uniform sampler2D uProcessed;
uniform sampler2D uMask;
uniform bool uHasMask;
uniform float uStrength;

in vec2 vUV;
out vec4 fragColor;

void main()
{
    vec4 original = texture(uOriginal, vUV);
    vec4 processed = texture(uProcessed, vUV);

    float mask = 1.0;

    if (uHasMask)
    {
        mask = texture(uMask, vUV).r;
    }

    float amount = clamp(mask * uStrength, 0.0, 1.0);
    fragColor = mix(original, processed, amount);
}
```

This is one of the cleanest ways to add masks without making every existing layer shader understand mask logic.

---

## Renderer Direction: From Sequential Chain To Graph Evaluation

The old renderer likely thinks like this:

```text
for each layer in layer order:
    run layer
```

That works for linear chains.

A full node graph renderer should eventually think like this:

```text
Evaluate Output node
    Evaluate whatever is connected to Output
        Evaluate that node's inputs
            Evaluate those nodes' inputs
Cache results as needed
Return final texture
```

This is dependency-graph evaluation.

Example:

```text
Output
  needs Mix
    Mix needs Image A
    Mix needs Image B after Blur
      Blur needs Image B
    Mix needs Mask
      Mask needs Noise
```

The renderer computes only what is needed.

Important features required for full graph evaluation:

```text
Topological sorting
Cycle detection
Socket type validation
Intermediate texture cache
Dirty tracking
Per-node render functions
Consistent resolution handling
Error fallback textures
Graph serialization
```

This should not be attempted all at once.

---

## Intermediate Texture Caching

Once branching exists, multiple nodes may need the same upstream result.

Example:

```text
Image -> Blur -> Mix A
             -> Scope
             -> Preview
```

The Blur result should be rendered once, then reused.

A render cache can use keys like:

```text
Node ID
Output socket ID
Graph generation ID
Input generation IDs
Node settings hash
Resolution
Color format
```

Early simplified cache:

```text
nodeId -> texture
```

Better cache later:

```text
(nodeId, outputSocketId, renderGeneration, resolution) -> texture
```

Cache invalidation matters.

If a node setting changes, that node and downstream nodes become dirty.

If an input image changes, everything downstream becomes dirty.

If only a scope moves on the graph, no image render should be dirty.

---

## Dirty Tracking

The app should not re-render everything every UI frame.

Graph changes should mark render state dirty only when they affect output.

Dirty events:

```text
Image content changed
Active render connection changed
Layer setting changed
Layer visibility changed
Layer order changed
Mask connection changed
Mask generator setting changed
Merge node setting changed
Output connection changed
Canvas/render size changed
```

Non-render dirty events:

```text
Node position changed
Node selected
Node expanded/collapsed
Graph pan/zoom changed
Context menu opened
Scope node moved
Search text changed
```

Non-render dirty events should not trigger image re-render.

This matters for keeping UI fast.

---

## Render Worker Compatibility

The render worker should receive immutable snapshots.

A render snapshot should describe what to render without depending on live UI pointers.

A snapshot may contain:

```text
Graph generation ID
Render resolution
Output node ID
Node list needed for render
Socket/link list needed for render
Serialized layer settings
Source image bytes or uploaded texture references
Mask node settings
Procedural generator settings
Output-connected flag
```

The UI thread owns interaction state.

The worker owns render execution.

Important rule:

```text
Do not let the worker read mutable UI graph structures directly.
```

Instead:

```text
UI graph -> immutable render snapshot -> worker
```

The worker should drop stale jobs.

Example:

```text
generation 101 starts
generation 102 is submitted
generation 103 is submitted
worker finishes 101
main thread ignores it because 103 is newer
worker renders latest available generation
```

This keeps slider dragging responsive.

---

## Graph Validation

The graph should validate itself before rendering.

Validation should check:

```text
Missing Output node
Multiple Output nodes if only one is allowed
Invalid socket type connections
Links referencing deleted nodes
Links referencing deleted sockets
Duplicate links
Cycles in render graph
Output disconnected
Layer node missing layer reference
Image node missing image data
Mask input connected to non-mask output
Image input connected to non-image output
Scope node connected in a way that affects render output
Protected node deleted
Unconnected nodes allowed but ignored
```

Validation should separate errors from warnings.

Errors:

```text
Cannot render output.
```

Warnings:

```text
Node is unconnected.
Mask resolution differs and will be resampled.
Preview node is stale.
Image node has no loaded data.
```

The graph should allow disconnected exploratory nodes.

Disconnected nodes should not break the entire project.

Only the path required by Output should matter for final render.

---

## Node Categories

Recommended node categories for the add menu:

```text
Input
Layer
Mask
Generator
Utility
Merge
Analysis
Output / Debug
```

Near-term version:

```text
Input
Layer
Mask
Analysis
```

Later version:

```text
Input
Layer
Mask
Generator
Utility
Merge
Coordinate
Value
Analysis
Debug
```

The menu should say "Layer" rather than "Effect" if that is the naming convention for this app.

Search should filter all visible node types.

Search examples:

```text
blur
mask
noise
grade
scope
gradient
mix
```

---

## Recommended Node Type List

### Input Nodes

```text
Image Input
Texture Input
Solid Color
Transparent Image
```

### Layer Nodes

These are existing layer/effect operations exposed as graph nodes.

```text
Adjustments
Color Grade
Blur
Sharpen
Denoise
Bloom
Glare
Chromatic Aberration
Lens Distortion
Vignette
Dither
Halftone
Compression
Corruption
Analog Video
Edge Effects
Palette Reconstructor
```

### Mask Nodes

```text
Solid Mask
Gradient Mask
Radial Mask
Noise Mask
Luminance Mask
Hue Range Mask
Alpha Mask
Edge Mask
Shape Mask
Painted Mask
```

### Utility Nodes

```text
Invert
Levels
Threshold
Clamp
Blur Mask
ColorRamp
Channel Split
Channel Combine
Transform
Crop
Resize
Normalize
Remap
```

### Merge Nodes

```text
Mix
Alpha Over
Add
Multiply
Screen
Overlay
Difference
Min
Max
Mask Merge
```

### Coordinate Nodes

```text
UV Coordinates
Mapping
Transform Coordinates
Distort Coordinates
Tile Coordinates
```

### Value Nodes

```text
Float Value
Integer Value
Vector2 Value
Color Value
Slider Control
Random Value
Time Value
```

### Analysis Nodes

```text
Histogram
RGB Parade
Vectorscope
Waveform
Preview
Mask Preview
Channel Viewer
Alpha Viewer
```

---

## Graph UI Rules

The graph UI should support exploration without forcing immediate render-chain changes.

Important UI rules:

```text
Adding a node should not automatically connect it unless explicitly requested.
Dropped images should appear as unconnected image nodes.
The user should connect nodes manually.
The user should be able to connect either direction for convenience:
    output pin -> input pin
    input pin -> output pin
Both should create the same final link.
Invalid link attempts should provide clear feedback.
```

Node selection:

```text
Click a node to select it.
Click empty graph space to deselect.
Drag empty space to box-select.
Delete removes selected nodes if they are not protected.
Output node should usually be protected.
```

Node movement:

```text
Dragging from any non-control region of a node should move it.
Dragging sliders, checkboxes, color pickers, text inputs, or combo boxes should not move the node.
Once a control captures the mouse, node movement must remain disabled until mouse release.
```

Node expansion:

```text
Double-click a layer node body/header to expand or collapse.
Double-click controls should not accidentally expand/collapse.
Expanded settings should be clipped inside the node bounds.
Expanded controls should not overlap pins.
```

Viewport:

```text
Rendering status should be an overlay.
Rendering status should not change layout size.
The image should not bounce or resize when status appears/disappears.
```

Pan/zoom:

```text
Scroll wheel zooms around the cursor.
Middle mouse drag pans.
Pan should be clamped enough that users cannot lose the graph far away from nodes.
Clamp should still leave comfortable overscroll margin.
```

---

## Serialization And Compatibility

Project compatibility is critical.

Old projects should load.

New graph metadata should be optional.

A project with no graph metadata should auto-generate a graph from the existing linear pipeline.

Possible project structure:

```json
{
  "pipelineData": {
    "layers": [],
    "nodeGraph": {
      "version": 1,
      "nodes": [],
      "links": [],
      "activeOutputNode": 1
    }
  }
}
```

Old format might be:

```json
{
  "pipelineData": []
}
```

The loader should accept both.

Recommended rules:

```text
If pipelineData is an array:
    treat it as old layer array
    load layers
    auto-generate graph

If pipelineData is an object:
    load layers from pipelineData.layers
    load graph from pipelineData.nodeGraph if available
    repair graph if needed
```

Graph metadata should include:

```text
Node ID
Node kind
Node position
Node expanded/collapsed state
Socket info or socket references
Layer reference/index/ID
Image metadata
Embedded image data if required
Links
Selected node optional
Graph pan/zoom optional
Version number
```

Avoid saving runtime-only GPU handles.

Do not save raw OpenGL texture IDs.

Use image bytes, metadata, settings, and graph structure.

---

## Resolution Handling

Different inputs may have different sizes.

The renderer must define resolution behavior.

Early rule:

```text
Final output resolution comes from the connected main image or canvas settings.
Masks are sampled/resized to match the current render resolution.
Procedural textures generate at render resolution.
```

Possible future options:

```text
Use source image resolution
Use explicit canvas resolution
Use output node resolution
Use project resolution
Allow per-node resolution later
```

Mask resolution mismatch should not break the graph.

It should resample.

---

## Color And Channel Handling

Image data and mask data should be handled differently.

Image data:

```text
RGBA
Color-managed later if needed
May contain alpha
```

Mask data:

```text
Usually single channel
Can be stored as R8, R16F, or RGBA depending on implementation simplicity
Sample .r channel by convention
```

Important rule:

```text
Mask outputs should have a clear convention:
mask value = red channel
```

If masks are stored as RGBA for simplicity:

```text
R = G = B = mask
A = 1.0
```

This keeps previewing easy.

---

## Error Fallbacks

The graph should fail gracefully.

Examples:

```text
Missing image input:
    show checker/black/error preview
    output invalid or clear

Invalid mask:
    treat mask as 1.0 or show warning depending on severity

Disconnected output:
    clear output and show "Output disconnected"

Shader compile failure:
    keep previous output if possible
    show node error state

Node render failure:
    mark node red/error
    do not crash editor

Cycle detected:
    reject connection or disable render until fixed
```

Graph systems must be robust because users will create incomplete graphs while working.

---

## Recommended Implementation Phases

### Phase 1: Socket Type Foundation

Goal:

Add typed sockets internally without changing final render behavior much.

Implement:

```text
Socket model
Socket IDs
Socket types: Image, Mask, Value, Analysis
Input/output socket definitions per node type
Connection validation by socket type
Graph UI display for typed sockets
Graph serializer support for links by socket ID
```

Current behavior remains:

```text
Image -> Layer -> Output
```

Layer nodes initially only use Image input/output.

Scope nodes use Analysis/Image input.

Do not add full merge nodes yet.

Do not add arbitrary render branching yet.

---

### Phase 2: Optional Mask Inputs On Layer Nodes

Goal:

Every layer node gains an optional Mask input.

Implement:

```text
Layer node sockets:
    Image In
    Mask In optional
    Image Out

Graph validation:
    Mask input accepts Mask output
    Optionally allow Image output into Mask input through implicit luminance conversion only if deliberately supported

Renderer:
    Existing layer processing stays mostly the same
    Add generic mask blend pass after layer effect
    No mask = old behavior
```

Add basic mask generators if safe:

```text
Solid Mask
Gradient Mask
Radial Mask
Noise Mask
Luminance Mask
```

Best first generators:

```text
Solid Mask
Gradient Mask
Radial Mask
```

These do not require analyzing image input.

Then add:

```text
Noise Mask
Luminance Mask
```

---

### Phase 3: Mask Utility Nodes

Goal:

Make masks useful and editable.

Implement:

```text
Invert
Levels
Threshold
Blur Mask
ColorRamp
```

The graph can now support:

```text
Image -> Luminance Mask -> Levels -> Layer Mask
Image -> Layer -> Output
```

or:

```text
Noise -> ColorRamp -> Layer Mask
Image -> Glitch -> Output
```

---

### Phase 4: Preview Nodes

Goal:

Let users inspect generated masks and intermediate outputs.

Implement:

```text
Preview node
Mask Preview node
Channel Viewer
```

These should be analysis/debug nodes.

They should not affect final render output.

---

### Phase 5: Merge / Mix Nodes

Goal:

Support multiple image streams.

Implement:

```text
Mix node
Alpha Over node
Add node
Multiply node
Screen node
```

This requires real dependency graph rendering.

A Mix node needs:

```text
Image A input
Image B input
Factor/Mask input
Image output
Blend mode setting
```

At this point, the renderer cannot be just a linear layer vector.

It must evaluate from Output backward.

---

### Phase 6: Full Dependency Graph Renderer

Goal:

Render arbitrary acyclic image graphs.

Implement:

```text
Topological sort
Recursive output evaluation
Intermediate texture cache
Cycle rejection
Dirty propagation
Node-level render functions
Resolution policy
Graph-level render snapshot
```

This is the point where the editor becomes a true compositor.

---

### Phase 7: Node Groups

Goal:

Allow complex graph setups to be packaged as reusable nodes.

Implement:

```text
Group node
Group input sockets
Group output sockets
Nested graph storage
Exposed controls
Group preview
```

Example group:

```text
Cinematic Bloom
    threshold
    blur
    glare
    chromatic split
    mix back
```

The user sees one node, but internally it contains multiple nodes.

---

## Suggested Internal Data Model

A possible graph model:

```cpp
enum class NodeKind
{
    Image,
    Layer,
    Output,
    Scope,
    MaskGenerator,
    Utility,
    Merge,
    Value,
    Preview
};

enum class SocketDirection
{
    Input,
    Output
};

enum class SocketType
{
    Image,
    Mask,
    Value,
    Vector,
    Color,
    Coordinates,
    Analysis
};

struct SocketId
{
    uint64_t value;
};

struct NodeId
{
    uint64_t value;
};

struct GraphSocket
{
    SocketId id;
    NodeId nodeId;
    SocketDirection direction;
    SocketType type;
    std::string name;
    bool optional = false;
    bool multiInput = false;
};

struct GraphNode
{
    NodeId id;
    NodeKind kind;
    std::string displayName;
    ImVec2 position;
    bool expanded = false;
    std::vector<GraphSocket> inputs;
    std::vector<GraphSocket> outputs;

    // Optional references:
    int layerIndex = -1;
    std::string layerTypeId;
    std::string imageAssetId;
    std::string generatorTypeId;
};

struct GraphLink
{
    uint64_t id;
    SocketId fromOutput;
    SocketId toInput;
};
```

Important:

```text
Links should connect sockets, not just nodes.
```

Node-to-node links are too limiting once nodes have multiple inputs.

For example, a Mix node has multiple inputs:

```text
Image A
Image B
Factor
```

A link must know exactly which input it connects to.

---

## Suggested Renderer Data Model

The UI graph should be converted into a render graph or render snapshot.

Possible snapshot structure:

```cpp
struct RenderNodeSnapshot
{
    NodeId nodeId;
    NodeKind kind;
    std::string typeId;
    std::vector<SocketSnapshot> inputs;
    std::vector<SocketSnapshot> outputs;
    Json settings;
};

struct RenderLinkSnapshot
{
    SocketId fromOutput;
    SocketId toInput;
};

struct EditorRenderSnapshot
{
    uint64_t generation;
    int width;
    int height;
    NodeId outputNode;
    std::vector<RenderNodeSnapshot> nodes;
    std::vector<RenderLinkSnapshot> links;
    std::vector<ImagePayload> images;
};
```

The render worker should receive this immutable snapshot.

The worker should not read live node UI state.

---

## How To Add Masks Without Rewriting Every Layer

The safest method is a generic wrapper.

Current layer interface likely does something like:

```text
Render(inputTexture) -> outputTexture
```

New masked path:

```text
originalTexture = inputTexture
processedTexture = RenderLayer(originalTexture)

if mask connected:
    outputTexture = MaskBlend(originalTexture, processedTexture, maskTexture, strength)
else:
    outputTexture = processedTexture
```

This means the layer shader does not need to know about masks.

Only the pipeline needs to know whether a mask texture is connected.

Later, some advanced layers may use masks internally for better quality or performance, but this is not necessary for the first implementation.

---

## What Codex Should Avoid

Do not ask Codex to do all of this at once.

Avoid:

```text
Rewriting the full renderer immediately
Adding merge nodes before typed sockets exist
Adding arbitrary branching before graph evaluation exists
Letting the worker read mutable UI graph state directly
Breaking old projects
Replacing every shader just to add mask support
Mixing graph UI changes with major render architecture changes in the same pass
Adding a huge undo/redo system during socket implementation
Changing unrelated tabs/modules
Changing visual theme outside node graph
```

The safe rule:

```text
Introduce data model first.
Then validation.
Then minimal rendering behavior.
Then UI polish.
Then advanced node types.
```

---

## Recommended Next Codex Task

The best next task is not "build a full compositor."

The best next task is:

```text
Add typed sockets and optional mask inputs to layer nodes.
```

This gives the app a strong foundation while keeping risk manageable.

A good implementation should:

```text
Preserve Image -> Layer -> Output behavior.
Add socket types internally.
Add Mask input sockets to layer nodes.
Reject invalid links.
Allow disconnected mask nodes.
Add a small number of simple mask generator nodes if safe.
Add generic post-layer mask blending if feasible.
Keep old projects loading.
Keep async render worker snapshot-safe.
```

---

## Suggested Codex Prompt

```text
I want to evolve the Editor node graph from a mostly linear layer graph into a more capable compositor-style graph, but conservatively.

Do not implement full arbitrary branching yet.
Do not implement Mix/Merge nodes yet.
Do not rewrite the whole renderer.
Do not break existing project files.
Do not change unrelated modules.

First inspect:
- Editor node graph model and UI
- EditorRenderWorker and render snapshot path
- RenderPipeline
- LayerRegistry
- Layer base classes and RenderUI flow
- Current project serialization/deserialization

Goal for this pass:
1. Add typed sockets to the node graph model.
2. Supported socket types for now:
   - Image
   - Mask
   - Value
   - Analysis
3. Links should connect sockets, not only nodes.
4. Existing Image -> Layer -> Output workflow must keep working.
5. Layer nodes should expose:
   - Image input
   - Optional Mask input
   - Image output
6. Scope/analysis nodes should consume Image or Mask data without affecting final output.
7. Add graph validation for socket type mismatches.
8. Do not auto-connect newly added nodes.
9. Do not add full merge/mix nodes yet.
10. If feasible and safe, add simple mask generator nodes:
    - Solid Mask
    - Linear Gradient Mask
    - Radial Gradient Mask
11. A connected mask should control where a layer applies.
12. If no mask is connected, the layer must behave exactly as before.
13. Prefer a generic post-layer mask blend pass instead of rewriting every layer shader.
14. Keep project compatibility:
    - Old projects load.
    - New socket metadata is optional or safely generated.
15. Keep render worker usage safe:
    - UI graph creates immutable render snapshots.
    - Worker does not read mutable UI state directly.

Before editing files:
- Write a concise implementation plan.
- Identify the exact files/classes to change.
- Explain how typed sockets will map onto the existing sequential layer pipeline.
- Explain how optional mask input will be rendered without rewriting every layer.
- Explain how old projects will be upgraded or auto-generated.

After implementation:
- List changed files.
- Explain what works now.
- Explain what is intentionally left for later phases.
- Run the build script and layer registry validation.
```

---

## Long-Term Vision

The editor can evolve through these levels:

### Level 1: Linear Layer Graph

```text
Image -> Layer -> Layer -> Output
```

This is the current foundation.

### Level 2: Linear Graph With Masks

```text
Image -> Layer -> Layer -> Output
          ^        ^
          |        |
        Mask     Mask
```

This is the recommended next major capability.

### Level 3: Mask And Texture Generation

```text
Noise -> ColorRamp -> Layer Mask
Gradient -> Vignette Mask
Image -> Layer -> Output
```

Now the user can create procedural control data.

### Level 4: Utility Processing

```text
Image -> Luminance Mask -> Levels -> Blur Mask -> Layer Mask
```

Now masks can be shaped and refined.

### Level 5: Multi-Image Compositing

```text
Image A -> Mix A
Image B -> Mix B
Mask -> Factor
Mix -> Output
```

Now the editor supports true image compositing.

### Level 6: Full Graph Evaluation

```text
Output pulls from whatever graph is connected to it.
Nodes render only when needed.
Intermediate results are cached.
Branches are allowed.
Cycles are rejected.
```

This is the full compositor model.

### Level 7: Groups And Reusable Tools

```text
Complex node setups can be collapsed into reusable nodes.
```

This makes the system powerful without overwhelming the user.

---

## Final Guidance

The next step should be focused and foundational.

The project should not jump straight to arbitrary branching, merge nodes, node groups, or a full renderer rewrite.

The correct next architecture move is:

```text
Typed sockets + optional layer masks.
```

This step introduces the most important concepts:

```text
Different data types
Area control
Control textures
Socket validation
Future-ready graph model
```

while preserving:

```text
Existing layers
Existing project compatibility
Existing render behavior when no masks are connected
Existing UI workflow
Existing async render direction
```

Once that is stable, the project can safely grow into procedural masks, utility nodes, previews, mix nodes, and a true graph compositor.
