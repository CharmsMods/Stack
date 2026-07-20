#import "../style.typ": stack-muted, stack-accent

#let comparison-label(body) = align(center)[
  #text(size: 8.5pt, fill: stack-muted)[#body]
]

#let pipeline-stage(body) = align(center)[
  #text(size: 9.5pt, weight: 700)[#body]
]

#let pipeline-arrow = align(center)[
  #text(size: 13pt, fill: stack-accent)[→]
]

#let pipeline-result(body) = align(center)[
  #text(size: 9.5pt, fill: stack-muted)[#body]
]

= Introduction <ch-introduction>

I go online by "Charm?", and you are reading the explanation, manual, thesis,
and personal passion that has converged and formed over months of work, design,
and learning.

#figure(
  image(
    "/assets/images/stack editor empty.png",
    width: 100%,
    alt: "An empty Stack editor with a dark teal node workspace and the message, Drop an image into the graph or press Tab to start.",
  ),
  caption: [The empty Stack editor.],
) <fig-stack-editor-empty>

#v(0.7em)

Stack was born after a thought hit me extremely hard in the shower (of all
places): think about many common image editors on many platforms; what order do
they apply low-level adjustments to the image in?

#v(0.7em)

#figure(
  align(center)[
    #grid(
      columns: (1.1fr, auto, 1.25fr, auto, 1.25fr, auto, 1.1fr),
      row-gutter: 0.82em,
      pipeline-stage([Image]), pipeline-arrow, pipeline-stage([Contrast]), pipeline-arrow, pipeline-stage([Brightness]), pipeline-arrow, pipeline-result([Result A]),
      pipeline-stage([Image]), pipeline-arrow, pipeline-stage([Brightness]), pipeline-arrow, pipeline-stage([Contrast]), pipeline-arrow, pipeline-result([Result B]),
    )
  ],
  caption: [The same two adjustments can be assembled in different sequences before reaching separate results.],
) <fig-operation-order>

#v(0.75em)

Let's imagine you open your phone and begin to edit a photo. You adjust your
exposure and your contrast.
Fundamentally these operations involve performing math on pixels. Crucially,
how do you know which order you are applying these specific chunks of math
operations in?

#figure(
  grid(
    columns: (1fr, 1fr),
    gutter: 0.18in,
    image(
      "/assets/images/contrast-then-brightness-print.png",
      width: 100%,
      alt: "Portrait comparison render with contrast increased by fifty percent before brightness is increased by fifty percent.",
    ),
    image(
      "/assets/images/brightness-then-contrast-print.png",
      width: 100%,
      alt: "Portrait comparison render with brightness increased by fifty percent before contrast is increased by fifty percent.",
    ),
    comparison-label([Contrast +50%, then Brightness +50%]),
    comparison-label([Brightness +50%, then Contrast +50%]),
  ),
) <fig-order-comparison>

Looking at these images, you can immediately tell they're different, but the
same amount of contrast and brightness adjustments have been applied equally to
both of them, so why do they look inherently different?

Let's visualize it like this.

#figure(
  align(center)[
    #text(size: 18pt, fill: stack-accent)[$ (2 + 3) dot 4 = 20 $]
    #v(0.5em)
    #text(size: 18pt, fill: stack-accent)[$ 2 + 3 dot 4 = 14 $]
  ],
  caption: [The same values and operations produce a different result when their grouping changes.],
) <fig-order-algebra>

This comes down to the reality that our outcome is informationally lossy at each
operational stage in relation to the original image and/or the stage before it.

// Bodoni MT Poster has one stable, requested role: the Introduction's display interlude.
#align(center)[
  #text(font: "Bodoni MT Poster", size: 28pt, fill: stack-accent)[Why not change that?]
]

#v(1.5em)

#align(center)[
  #text(size: 13pt)[I welcome you to the Mindset of Stack.]
]
