#import "math-catalog.typ": catalog-column, catalog-entry

#heading(level: 2, numbering: none)[Tone and brightness operations] <math-tone-and-brightness>

Most of these are pointwise transforms, but their visual meaning depends on
whether values are linear, sRGB-encoded, log-encoded, or perceptual. Here,
$x$ is an input value, $p$ is a chosen pivot, and $c$ is an adjustment.

#v(0.45em)

#grid(
  columns: (1fr, 1fr),
  gutter: 0.28in,
  catalog-column(spacing: 1.55em,
    catalog-entry([Exposure], [$p' = 2^e p$], [Scales linear RGB by exposure value.]),
    catalog-entry([Gain], [$y = c x$], [Scales values; terminology varies by tool.]),
    catalog-entry([Offset], [$y = x + c$], [Raises black and white levels together.]),
    catalog-entry([Lift], [$y = L_c(x)$], [Raises or lowers darker values preferentially.]),
    catalog-entry([Gamma], [$y = x^(1 / gamma)$], [Usually changes midtones most.]),
    catalog-entry([Lift / gamma / gain], [$y = G(Gamma(L(x)))$], [Combines offset, curve, and scale controls.]),
    catalog-entry([Printer lights], [$p' = P(p, l)$], [Adds RGB exposure-like increments in log-like space.]),
    catalog-entry([Brightness], [$y = B(x, c)$], [May be additive, multiplicative, curved, or perceptual.]),
    catalog-entry([Contrast], [$y = p + (x - p)c$], [Expands distance from a chosen pivot.]),
    catalog-entry([Contrast pivot], [$p = p_0$], [Selects the fixed center of contrast.]),
    catalog-entry([Levels], [$y = ((x - b) / (w - b))^(1 / gamma)$], [Remaps black, white, and midpoint.]),
    catalog-entry([Black point], [$y = max(x - b, 0)$], [Moves or clips the lowest values.]),
    catalog-entry([White point], [$y = min(x / w, 1)$], [Moves or clips the highest values.]),
    catalog-entry([Shadows], [$y = x + s(x)c$], [Weights an adjustment toward dark tones.]),
    catalog-entry([Midtones], [$y = x + m(x)c$], [Weights an adjustment around middle tones.]),
    catalog-entry([Highlights], [$y = x + h(x)c$], [Weights an adjustment toward bright tones.]),
    catalog-entry([Tone curve], [$y = C(x)$], [Maps input intensity through a curve or lookup.]),
  ),
  catalog-column(spacing: 2.2em,
    catalog-entry([RGB curve], [$p' = C(p)$], [Applies one curve to all RGB channels.]),
    catalog-entry([Per-channel curves], [$p' = (C_r(r), C_g(g), C_b(b))$], [Changes tone and color with separate curves.]),
    catalog-entry([S-curve], [$y = S(x)$], [Lowers shadows and raises highlights.]),
    catalog-entry([Filmic curve], [$y = F(x)$], [Uses a soft toe and shoulder to compress extremes.]),
    catalog-entry([Toe], [$y = T_s(x)$], [Creates a smooth shadow rolloff.]),
    catalog-entry([Shoulder], [$y = T_h(x)$], [Creates a smooth highlight rolloff.]),
    catalog-entry([Sigmoid contrast], [$y = sigma(a x + b)$], [Applies a smooth S-shaped contrast mapping.]),
    catalog-entry([Log encoding], [$y = log(x)$], [Compresses linear dynamic range for storage or grading.]),
    catalog-entry([Log decoding], [$y = exp(x)$], [Restores log values to a linear form.]),
    catalog-entry([Threshold], [$y = H(x - e)$], [Creates a binary mask or graphic effect.]),
    catalog-entry([Posterization], [$y = round((n - 1)x) / (n - 1)$], [Quantizes tones into a limited set of bands.]),
    catalog-entry([Solarization], [$y = S_c(x)$], [Inverts part of the tonal range with a curve.]),
    catalog-entry([Inversion], [$y = 1 - x$], [Inverts normalized values; encoding matters.]),
    catalog-entry([Highlight recovery], [$p' = R(p, k)$], [Estimates clipped detail; may need RAW or neighborhood data.]),
  ),
)
