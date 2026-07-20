#import "../style.typ": stack-accent, stack-muted, stack-paper

// Mask-socket language checked against the current implementation on
// 2026-07-13: EditorNodeGraphDefinitions.cpp and EditorNodeGraph.cpp distinguish
// image sockets from mask/scalar sockets and expose optional mask inputs on
// image-processing nodes.

#let render-label(body) = align(center)[
  #text(size: 8pt, fill: stack-muted)[#body]
]

#heading(level: 2, numbering: none)[The node graph] <overview-node-graph>

Stack is set apart from any mainstream image editor because of its mostly
unique approach to its workspace.

#align(center)[
  #text(size: 17pt, weight: 700, fill: stack-accent)[Welcome to the Node Graph!]
]

One of Stack's ambitious goals is to allow the user to place things wherever
they want, in addition to connecting things _almost_ wherever they want. Graph
organization is key to visualizing a large project, its masking, and its edits
in one place, with a coherent "chain of events," so to speak. A graph lets you
organize things on two axes across a plane, rather than along the single axis
of a vertically scrolling panel.

Stack's node system dissolves to a simple set of input and output connection
points on each node. Let's walk through a simple single-operation adjustment
chain.

#figure(
  image(
    "/assets/images/blurred graph chain.png",
    width: 100%,
    alt: "A Stack graph with an input image connected to a Blur Radius node, which is connected to a blurred output image.",
  ),
  caption: [A single image-processing chain on the Stack node graph.],
) <fig-overview-simple-blur-chain>

#pagebreak()

#heading(level: 2, numbering: none)[A simple adjustment chain] <overview-simple-adjustment>

Here, an input image has been loaded onto the graph and connected to a Gaussian
blur node, which is in turn connected to a final output node to be displayed in
the viewport.

#v(0.55in)

#figure(
  grid(
    columns: (1fr, 1fr),
    gutter: 0.3in,
    image(
      "/assets/images/graph-chain-output-render-print.png",
      width: 100%,
      alt: "The unblurred Charm question-mark artwork used as the input to the example graph.",
    ),
    image(
      "/assets/images/blurred-graph-chain-output-render-print.png",
      width: 100%,
      alt: "The Charm question-mark artwork after a strong Gaussian blur is applied.",
    ),
    render-label([Input image]),
    render-label([After Gaussian blur]),
  ),
  caption: [The render before and after the Gaussian blur operation.],
) <fig-overview-blur-before-after>

#pagebreak()

#heading(level: 2, numbering: none)[Mask inputs] <overview-mask-inputs>

Stack also allows the use of single-channel, or mask, inputs on many of its
nodes. These inputs generally appear lower on the left side of a node and
accept mask or other scalar data rather than a full 3- or 4-channel RGB or RGBA
image.

#figure(
  image(
    "/assets/images/First mask Graph Example.png",
    width: 100%,
    alt: "A Stack graph where a linear-gradient mask feeds the mask socket of a Blur Radius node, limiting a strong blur to part of the output image.",
  ),
  caption: [A linear-gradient mask limits the blur to part of the image.],
) <fig-overview-linear-gradient-mask>

Stack hopes to achieve masking complex shapes and objects without primarily
relying on drawing or AI: by clamping luma and color, detecting edges, and
letting the user create a mask from an input image or another set of edited
channels.

#page(
  paper: "us-letter",
  flipped: true,
  fill: stack-paper,
  margin: (x: 0.7in, top: 0.62in, bottom: 0.58in),
  header: none,
  footer: context [
    #set text(font: "New Computer Modern", size: 9pt, fill: stack-muted)
    #align(center)[#counter(page).display("1")]
  ],
)[
  #set text(font: "New Computer Modern", size: 10.5pt, fill: white)
  #heading(level: 2, numbering: none)[A graph at scale] <overview-graph-at-scale>

  Below is a much more complex graph project.

  #v(0.75in)

  #figure(
    image(
      "/assets/images/complex-graph-full-print.png",
      width: 100%,
      alt: "A wide Stack node graph containing many image, mask, analysis, and adjustment nodes connected across a large project.",
    ),
    caption: [A larger Stack project arranged as an explicit graph.],
  ) <fig-overview-complex-graph>
]
