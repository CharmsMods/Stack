#heading(level: 2, numbering: none)[From pixels to operations] <math-from-pixels>

The colors in an image are data before they are light on a display. For a
calculation, an 8-bit channel value is commonly normalized from the stored
range 0--255 to a number between 0 and 1. A pixel can then be written as a
small vector of channel values:

#align(center)[
  $ p(u, v) = mat(r, g, b)^T $
]

Here, $(u, v)$ locates the pixel in the image and $r$, $g$, and $b$ are its
red, green, and blue channel values. An image operation applies a mathematical
rule to those values and produces a new pixel at the same coordinate:

#align(center)[
  $ p'(u, v) = f(p(u, v)) $
]

For a simple per-pixel operation, $f$ can add, scale, clamp, or reshape every
channel value independently. This is how a change to brightness, exposure, or
contrast can begin as a small equation and become a visible change across an
entire image.

The pages that follow begin with these elementary transforms. They are not
complete image-editing recipes; they are the small, explicit operations from
which more involved color, tone, and compositing tools can be built.
