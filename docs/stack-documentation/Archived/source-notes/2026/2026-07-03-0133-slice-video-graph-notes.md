# Source Note: slice video graph notes

- Session ID: idea-20260703-0133-slice-video-graph-notes
- Received: 2026-07-03 01:33
- Source Kind: pasted-text

## Original Text

Being able to export a still frame or a video
Imported images should become slices instead of images. The terminology should become import slice, and each image should be textually or verbally referred to as a slice.
We should be adding support for this. First, we need to make sure that our environment and graph are set up for the implementation of allowing video to be imported as a slice, as well as image. We also need to be able to double-click on an image to reveal its basic settings.
We need the addition of a timeline in order to have keyframe markers. We can create a new track, then select a dropdown box for each track that has any slice on it, and then we can add keyframes. Right-clicking a keyframe opens a window on the UI that will expand like an inspector panel, just like an image would have. That allows us to select and auto-keyframe exactly what values or aspects of the video or image we are changing.

We very likely now need to start the implementation of a settings/debug menu where I can adjust the way things look parameter-wise in the UI. That way, along with actually implementing different features, I can choose what they look like with some different parameters. When I set them to something specific, they can be updated to a fig file that Adrian can read and set them permanently to that value.

As an organizational strategy, I want to separate documents for these notes into two categories:
1. What we already know
2. Things that we need to research further and then make better detailed documents on


The first example of which would be whether we do timing on the timeline for keyframes per frame or per second. I'm going to lean towards per frame, but we want some kind of conversion to per second as well. We want to be able to take care of rounding issues for second measurements, not necessarily landing exactly on our set frame rate, and ensure that those points are rounded to the nearest frame for every keyframe.


As a separate thought that still ties into the work around image/video slices, renaming, graph inputs, and the overall graph mindset, I want to think more about the directionality of the graph. Right now, the graph falls into one of two modes:
1. A single-image edit, where multiple images or inputs feed into one final output.
2. Multiple images, where each image has its own separate output.
Both of those modes are already supported by the current graph. Either the graph is displaying a single image output/composite, or it is displaying a canvas that can be rendered.
What I want to explore is the ability to layer images on top of each other, set opacity for different layers, reorder them, and treat the result more like a layered composite. This idea has traits from both existing modes.
I need to think about whether canvas mode should become the main default mode. For example, if two images are the same image, have the same dimensions, or at least share the same aspect ratio, maybe they should automatically be overlaid on top of each other. Then the user could reorder them like layers. This is technically somewhat possible already, since images can be moved around visually on the canvas, but they do not automatically align or behave like stacked layers.
That may not be the best option, though.
The bigger question is whether there should be a dedicated multi-image compositing mode specifically for creating one final image output, rather than using the canvas as a general space where many images can sit side by side. In other words, there may need to be a distinction between a compositing workflow and a graphic design/canvas workflow.
A compositing workflow would be for combining multiple images into one final output. A canvas workflow would be for arranging multiple images freely, like a design board or graphic layout.

Another somewhat disconnected thought, when processing the video, we are asking:
1. What input do we have
2. What nodes are connected to it
3. What kind of output is it giving (might be more options that a 2D raster at some point)

We will basically be processing the video every frame so we're going to ask what each node will do every frame on the video


Okay actually as I'm thinking about this maybe it's better to instead have, on the graph for a given object, the node graph. You'll have all the nodes that make up the way it looks and they all have a sequence. Then for that object, when it's put on a canvas somewhere, you can have all the normal animations and transforms (I guess you would call them) for it, like moving, rotating, and scaling and perspective stuff, but then every value
