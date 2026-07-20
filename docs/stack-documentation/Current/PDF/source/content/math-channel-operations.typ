#import "math-catalog.typ": catalog-column, catalog-entry

#heading(level: 2, numbering: none)[Channel operations] <math-channel-operations>

These operations normally work at one coordinate at a time, but treat the
pixel as an RGB or RGBA vector. Let $p = mat(r, g, b)^T$ and let $M$ be a 3 by
3 matrix where needed.

#v(0.45em)

#grid(
  columns: (1fr, 1fr),
  gutter: 0.28in,
  catalog-column(spacing: 4.4em,
    catalog-entry([Split channels], [$p -> (r, g, b)$], [Extracts red, green, and blue as separate outputs.]),
    catalog-entry([Merge channels], [$(r, g, b) -> p$], [Builds one RGB pixel from separate values.]),
    catalog-entry([Swizzle], [$p -> (b, g, r)^T$], [Reorders or repeats selected channels.]),
    catalog-entry([Copy channel], [$b = g$], [Replaces one channel with another.]),
    catalog-entry([Remove channel], [$r = 0$], [Zeros or neutralizes one channel.]),
    catalog-entry([Per-channel gain], [$p' = (g_r r, g_g g, g_b b)^T$], [Scales R, G, and B independently.]),
    catalog-entry([Per-channel offset], [$p' = p + o$], [Adds a different offset to each channel.]),
    catalog-entry([Channel mixer], [$p' = M p$], [Builds each output channel from every input channel.]),
    catalog-entry([Grayscale], [$y = w_r r + w_g g + w_b b$], [Reduces RGB to a luminance-like value.]),
    catalog-entry([False color], [$p' = L(y)$], [Maps a scalar through selected colors.]),
  ),
  catalog-column(spacing: 4.4em,
    catalog-entry([RGB matrix], [$p' = M p$], [Applies a general linear color transform.]),
    catalog-entry([Matrix plus offset], [$p' = M p + b$], [Combines a linear transform and translation.]),
    catalog-entry([Normalize vector], [$p' = p / sqrt(r^2 + g^2 + b^2)$], [Scales an RGB vector to unit length.]),
    catalog-entry([Dot product], [$y = a_r r + a_g g + a_b b$], [Reduces RGB to one value for luma or similarity.]),
    catalog-entry([Cross product], [$p' = a times p$], [Calculates a vector relation for rare procedural uses.]),
    catalog-entry([Channel difference], [$r - g$], [Extracts color-opponent information.]),
    catalog-entry([Channel ratio], [$r / g$], [Measures chromaticity or supports analysis.]),
    catalog-entry([Largest channel], [$max(r, g, b)$], [Finds HSV value or a masking signal.]),
    catalog-entry([Smallest channel], [$min(r, g, b)$], [Supports HSV and related calculations.]),
    catalog-entry([Channel ranking], [$r_(1) >= r_(2) >= r_(3)$], [Determines the dominant color channel.]),
  ),
)
