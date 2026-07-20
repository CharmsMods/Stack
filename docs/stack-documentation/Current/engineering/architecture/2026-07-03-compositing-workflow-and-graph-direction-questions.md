# Compositing Workflow, Canvas Mode, And Graph Direction Questions

- Captured: 2026-07-03 01:33
- Source: docs/stack-documentation/Archived/source-notes/2026/2026-07-03-0133-slice-video-graph-notes.md
- Type: question
- Topic: architecture
- Verification: partially-verified

There is an open direction question around image/video slices, graph directionality, and multi-image work.

Right now the mental model falls into two broad modes:

1. A single-image edit, where multiple images or inputs feed into one final output.
2. Multiple images, where each image has its own separate output.

I want to explore layering images on top of each other, setting opacity per layer, reordering them, and treating the result more like a layered composite. This has traits from both existing modes.

One possibility is that canvas mode becomes the main default mode. If two images are the same image, have the same dimensions, or at least share the same aspect ratio, maybe they should automatically overlay and behave more like stacked layers. That may not be the best option, though.

The bigger question is whether there should be a dedicated multi-image compositing workflow for creating one final image output, rather than using the canvas as a general free-layout space where many images can sit side by side. In other words, there may need to be a distinction between a compositing workflow and a graphic-design/canvas workflow.

A separate but related thought is whether a given object should own its node graph for how it looks, while canvas placement owns transforms and animation such as moving, rotating, scaling, and perspective-style adjustments.

## Research Needed

- Compare a single global graph plus canvas-object nodes against per-object node graphs.
- Define what should happen by default when imported images share dimensions or aspect ratio.
- Decide whether compositing and free-layout canvas work should be one mode with strong defaults or two distinct workflows.

## Related Docs

- docs/stack-documentation/Current/engineering/architecture/UNIFIED_WORKSPACE_ARCHITECTURE.md
- docs/stack-documentation/Current/engineering/editor/ADVANCED_NODE_GRAPH_COMPOSITOR_GUIDE.md
