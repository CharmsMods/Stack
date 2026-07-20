#import "../style.typ": stack-muted, stack-accent, stack-soft, stack-line

#let screen-cell(color) = rect(
  width: 0.46in,
  height: 0.36in,
  fill: color,
)

#let screen-row(a, b, c, d, e, f, g, h, i, j) = grid(
  columns: (0.46in, 0.46in, 0.46in, 0.46in, 0.46in, 0.46in, 0.46in, 0.46in, 0.46in, 0.46in),
  gutter: 1pt,
  screen-cell(a), screen-cell(b), screen-cell(c), screen-cell(d), screen-cell(e),
  screen-cell(f), screen-cell(g), screen-cell(h), screen-cell(i), screen-cell(j),
)

#let subpixel(color, label) = align(center)[
  #rect(
    width: 0.78in,
    height: 1.45in,
    fill: color,
    stroke: (paint: stack-line, thickness: 0.35pt),
  )
  #v(0.32em)
  #text(size: 8pt, fill: stack-muted)[#label]
]

#let color-card(color, label) = align(center)[
  #rect(
    width: 2.5in,
    height: 1.406in,
    fill: color,
    stroke: (paint: stack-line, thickness: 0.45pt),
  )
  #v(0.35em)
  #text(size: 8pt, fill: stack-muted)[#label]
]

#let alpha-label(body) = align(center)[
  #text(size: 8pt, fill: stack-muted)[#body]
]

It's generally common knowledge that an image is a group of pixels laid out in
a 2-D plane. You generally view this grouping of colors from far enough away
that your brain blends the gaps together into something you can perceive as a
representation on the screen.

#figure(
  align(center)[
    #block(
      fill: stack-soft,
      inset: 6pt,
      radius: 2pt,
      stroke: (paint: stack-line, thickness: 0.45pt),
    )[
      #screen-row(
        rgb("#16313c"), rgb("#20475a"), rgb("#315a70"), rgb("#42697c"), rgb("#68818a"),
        rgb("#88969b"), rgb("#adad9e"), rgb("#d4ae6e"), rgb("#dc8066"), rgb("#d2535a"),
      )
      #v(1pt)
      #screen-row(
        rgb("#1d3d4a"), rgb("#285166"), rgb("#3a6680"), rgb("#4c758a"), rgb("#77908e"),
        rgb("#9ca395"), rgb("#c5af83"), rgb("#e0a66c"), rgb("#e67564"), rgb("#cf4d58"),
      )
      #v(1pt)
      #screen-row(
        rgb("#254a54"), rgb("#2d5e70"), rgb("#3e7285"), rgb("#59858e"), rgb("#8fa28c"),
        rgb("#b7b47d"), rgb("#dda96f"), rgb("#e77d61"), rgb("#d9565c"), rgb("#b44156"),
      )
      #v(1pt)
      #screen-row(
        rgb("#31575d"), rgb("#3c6d78"), rgb("#51818a"), rgb("#78a08c"), rgb("#b0b97e"),
        rgb("#e0b16e"), rgb("#e88060"), rgb("#d95a5c"), rgb("#b44758"), rgb("#8e3b52"),
      )
      #v(1pt)
      #screen-row(
        rgb("#3b6365"), rgb("#4c7b7e"), rgb("#6b9587"), rgb("#a1b87e"), rgb("#d4b66e"),
        rgb("#e8885f"), rgb("#d95d5c"), rgb("#b44758"), rgb("#8f3d55"), rgb("#6f3450"),
      )
      #v(1pt)
      #screen-row(
        rgb("#456b69"), rgb("#5d8980"), rgb("#8dab7f"), rgb("#c4bb70"), rgb("#e0a967"),
        rgb("#e2765d"), rgb("#c94e59"), rgb("#973d56"), rgb("#71344f"), rgb("#552d48"),
      )
    ]
  ],
  caption: [A screen-like field with its individual pixels made visible.],
) <fig-visible-pixels>

Let's zoom in a little bit farther.

#figure(
  align(center)[
    #grid(
      columns: (0.78in, 0.78in, 0.78in),
      gutter: 0.05in,
      subpixel(rgb("#df4f50"), [red]),
      subpixel(rgb("#5cb56e"), [green]),
      subpixel(rgb("#527fda"), [blue]),
    )
  ],
  caption: [A conventional RGB stripe: red, green, then blue. Display layouts can vary, but this is the standard RGB ordering used here.],
) <fig-rgb-subpixels>

On a conventional RGB display, a pixel is generally composed of three
subpixels. In a standard 8-bit sRGB image, each stored red, green, and blue
channel code value ranges from 0 to 255, for 256 possible values per channel.

#figure(
  align(center)[
    #grid(
      columns: (1fr, 1fr),
      gutter: 0.36in,
      row-gutter: 0.72em,
      color-card(black, [RGB(0, 0, 0)]),
      color-card(white, [RGB(255, 255, 255)]),
      color-card(rgb("#f0b5c4"), [RGB(240, 181, 196)]),
      color-card(rgb("#abcff0"), [RGB(171, 207, 240)]),
    )
  ],
  caption: [Four 8-bit RGB colors, each shown as a 16:9 field.],
) <fig-rgb-color-samples>

Each of these three colors is considered a single channel for an image, and
while we have three channels from our three subpixel colors, there is generally
an additional fourth channel, commonly known as the alpha channel. In formats
like PNG, alpha stores opacity and controls how much of the color behind a
rendered pixel is allowed to influence or shine through the top color. This is
used very frequently in icons and logos, such as the example below.

Alpha does not mean there is a fourth subpixel. It is not a color or luminance
channel; it is a per-pixel opacity value used during compositing to calculate
the final displayed color.

#figure(
  grid(
    columns: (1fr, 1fr),
    gutter: 0.25in,
    image(
      "/assets/images/with alpha example.png",
      width: 100%,
      alt: "Yellow Stack S icon with a transparent background, composited over the manual's charcoal page.",
    ),
    image(
      "/assets/images/Without alpha example.png",
      width: 100%,
      alt: "Yellow Stack S icon with an opaque yellow background.",
    ),
    alpha-label([With alpha: transparent background]),
    alpha-label([Without alpha: opaque background]),
  ),
  caption: [Alpha changes opacity, not the RGB color channels themselves.],
) <fig-alpha-examples>
