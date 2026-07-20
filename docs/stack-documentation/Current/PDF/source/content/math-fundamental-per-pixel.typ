#import "math-catalog.typ": catalog-column, catalog-entry

#heading(level: 2, numbering: none)[Fundamental per-pixel math] <math-fundamental-per-pixel>

Each output pixel can be calculated independently from the corresponding input
pixel. In these compact formulas, $x$ is an input value, $y$ is the output,
and $c$ is a supplied scalar unless another symbol is named.

#v(0.45em)

#grid(
  columns: (1fr, 1fr),
  gutter: 0.28in,
  catalog-column(spacing: 2em,
    catalog-entry([Add], [$y = x + c$], [Offsets values; a brightness-like lift.]),
    catalog-entry([Subtract], [$y = x - c$], [Removes a black level or channel bias.]),
    catalog-entry([Multiply], [$y = x c$], [Scales values for gain or exposure-like change.]),
    catalog-entry([Divide], [$y = x / c$], [Normalizes values or applies inverse gain.]),
    catalog-entry([Power], [$y = x^c$], [Creates gamma and nonlinear contrast curves.]),
    catalog-entry([Square root], [$y = sqrt(x)$], [Useful for encoding curves and effects.]),
    catalog-entry([Logarithm], [$y = log(x)$], [Compresses dynamic range into a log form.]),
    catalog-entry([Exponential], [$y = exp(x)$], [Performs an inverse-log-style expansion.]),
    catalog-entry([Absolute value], [$y = abs(x)$], [Shows magnitude in difference and procedural work.]),
    catalog-entry([Negate], [$y = -x$], [Manipulates signed data.]),
    catalog-entry([Reciprocal], [$y = 1 / x$], [Supports specialized inverse transforms.]),
    catalog-entry([Minimum], [$y = min(a, b)$], [Chooses the darker value or clamps a limit.]),
    catalog-entry([Maximum], [$y = max(a, b)$], [Chooses the lighter value or clamps a limit.]),
    catalog-entry([Clamp], [$y = min(max(x, l), h)$], [Keeps values within lower and upper bounds.]),
    catalog-entry([Saturate], [$y = min(max(x, 0), 1)$], [Clips normalized values to the standard range.]),
  ),
  catalog-column(spacing: 2.6em,
    catalog-entry([Modulo], [$y = mod(x, c)$], [Repeats a value for procedural patterns.]),
    catalog-entry([Fractional part], [$y = x - floor(x)$], [Builds repeating gradients and patterns.]),
    catalog-entry([Floor], [$y = floor(x)$], [Rounds downward for indexing or posterization.]),
    catalog-entry([Ceiling], [$y = ceil(x)$], [Rounds upward for quantization effects.]),
    catalog-entry([Round], [$y = floor(x + 1 / 2)$], [Chooses the nearest discrete value.]),
    catalog-entry([Sign], [$y = x / abs(x)$], [Returns plus or minus one for nonzero input; zero needs a special case.]),
    catalog-entry([Step], [$y = H(x - e)$], [Creates a hard threshold or mask.]),
    catalog-entry([Smoothstep], [$y = 3t^2 - 2t^3$], [Creates a softened threshold or transition.]),
    catalog-entry([Linear interpolation], [$y = a (1 - t) + b t$], [Blends between two values.]),
    catalog-entry([Remap], [$y = l + (h-l)(x-a) / (b-a)$], [Converts one value range to another.]),
    catalog-entry([Bias], [$y = B_b(x)$], [Weights values nonlinearly toward 0 or 1.]),
    catalog-entry([Gain], [$y = G_g(x)$], [Shapes contrast around a midpoint.]),
    catalog-entry([Conditional select], [$y = q A + (1-q) B$], [Chooses A or B from a condition or mask.]),
  ),
)
