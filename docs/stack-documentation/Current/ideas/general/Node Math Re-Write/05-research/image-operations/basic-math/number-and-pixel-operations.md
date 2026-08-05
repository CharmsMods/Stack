# 01 — Fundamental Per-Pixel Math

> **Reference status:** Formulas and sources are technical reference. Any Stack
> status in this file is a 2026-07-12 snapshot, not current state or a release
> backlog.

## Purpose

These are the smallest transparent operations from which many larger image nodes can be built. “Per-pixel” means that an output sample at coordinate **p** depends only on values and parameters available at that same coordinate. It may still consume several images, channels, scalars, or a mask; it does not inspect neighboring coordinates or whole-image statistics.

For Stack, these operations should normally be **primitive nodes** or compiler-level expressions. User-facing nodes such as Brightness, Levels, Saturation, and blend modes should be documented as convenience compositions over these primitives. This distinction matters because Stack currently evaluates almost every visible node as a separate full-canvas RGBA16F pass: exposing every arithmetic step literally would be flexible but expensive and visually noisy until expression fusion or executable compound nodes exist.

## Non-negotiable numeric contract

Every primitive needs an explicit policy for input type, broadcasting, range, division by zero, negative bases, NaN/Inf, clipping, and alpha. No primitive should silently imply sRGB, scene-linear light, normalized `[0,1]`, or “color” merely because its input happens to be an image. Stack’s current generic `Image` wire records none of those meanings.

Status terms below come from the 2026-07-12 Stack audit. **Implemented** means a discoverable equivalent exists; **node-specific** means the math exists only inside a specialized node; **composable** means the result can be assembled today but has no direct primitive; **absent** means no reusable graph operation was found.

## Arithmetic and range primitives

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `PIX.ADD` | Add | `y = a + b` | Pointwise; unary offset or two inputs | Scalar/vector/image; compatible units and channel shapes | Offset, accumulation, brightness-like adjustment | Addition is physically meaningful for light only in an additive linear-light representation; decide alpha/broadcast behavior | **Implemented** in DataMath; Brightness is a clamped RGB-only shorthand |
| `PIX.SUB` | Subtract | `y = a - b` | Pointwise; two inputs | Scalar/vector/image; compatible units | Black subtraction, residuals, differences with sign | Preserve negatives unless the calling operation explicitly clips; operand order must be visible | **Implemented** in DataMath |
| `PIX.MUL` | Multiply | `y = a · b` | Pointwise; scalar or componentwise vector product | Numeric; compatible shapes | Gain, mask modulation, linear-light exposure factor | Componentwise RGB multiplication is not a matrix transform; premultiplied alpha needs deliberate handling | **Implemented** in DataMath |
| `PIX.DIV` | Divide | `y = a / b` | Pointwise; scalar or componentwise | Numeric plus declared zero policy | Normalization, ratios, inverse gain | Must define `b=0`, signed zero, near-zero epsilon, Inf/NaN, and whether output clips | **Implemented with a verified sign defect** in DataMath; unsafe as the canonical contract |
| `PIX.SAFE_DIV` | Safe divide | `y = select(abs(b)>ε, a/b, fallback)` | Pointwise; two inputs | Numeric; user or type-specific `ε` and fallback | Stable ratios and normalization | Epsilon has units and should scale with precision/domain; fallback must not be hidden | **Absent**; recommended core primitive |
| `PIX.FMA` | Fused multiply-add | `y = fma(a,b,c) ≈ a·b+c` with one final rounding when supported | Pointwise; three inputs | Floating point | Matrices, affine transforms, polynomials, efficient shader math | Fused and unfused results can differ slightly; reproducibility mode should state which is promised | **Absent** as a graph primitive; shaders naturally use multiply/add expressions |
| `PIX.RECIP` | Reciprocal | `y = 1/x` | Pointwise unary | Numeric; nonzero or explicit singularity policy | Inverse gain and specialized transforms | Same zero/epsilon issues as divide; rarely deserves a user-facing node | **Composable only** with Divide; inherits its defect |
| `PIX.NEGATE` | Negate | `y = -x` | Pointwise unary | Signed numeric representation | Signed-data manipulation; reverse a residual | On unsigned/clamped color it is usually meaningless until an offset is added | **Composable only** as `0-x` |
| `PIX.ABS` | Absolute value | `y = abs(x)` | Pointwise unary | Signed numeric | Residual magnitude, masks, procedural effects | Removes sign and is non-invertible; vector `abs` is componentwise, not vector length | **Node-specific / partial**: Difference provides `abs(a-b)`; no unary primitive |
| `PIX.MIN` | Minimum | `y = min(a,b)` | Pointwise; componentwise for vectors | Comparable numeric inputs | Darken, lower envelope, clamping composition | Componentwise RGB minimum is not a perceptual minimum | **Implemented** in DataMath |
| `PIX.MAX` | Maximum | `y = max(a,b)` | Pointwise; componentwise for vectors | Comparable numeric inputs | Lighten, upper envelope, clamping composition | Componentwise RGB maximum can synthesize colors absent from either input | **Implemented** in DataMath |
| `PIX.MEAN` | Arithmetic mean | `y = (1/N) Σᵢxᵢ` | Pointwise over N connected inputs | Numeric; `N>0` | Average channels, images, parameters | This is not a spatial mean/reduction; accumulation precision and alpha policy matter | **Implemented** as DataMath Average; image averaging is multipass |
| `PIX.CLAMP` | Clamp | `y = min(max(x,lo),hi)` | Pointwise | Ordered numeric bounds; define `lo>hi` behavior | Range limiting, legal code values | Destructive and non-invertible; do not make it an invisible default for HDR or negative scene data | **Implemented** in DataMath |
| `PIX.SATURATE` | Saturate | `y = clamp(x,0,1)` | Pointwise | Normalized convention `[0,1]` | Convenient display/mask limiting | This is a specialized clamp, not color saturation; name collision must be avoided in UI | **Composable** with Clamp; many shaders hard-clamp locally |
| `PIX.REMAP` | Linear remap | `y = c + (x-a)(d-c)/(b-a)` | Pointwise | Source range `[a,b]`, destination `[c,d]`; `a≠b` or declared fallback | Levels, normalization, unit conversion | Clamping is a separate option; reversed ranges are valid; define degenerate source span | **Implemented** in DataMath and Mask Remap; verify its range/zero-span behavior before treating as normative |
| `PIX.LERP` | Linear interpolation / mix | `y = a(1-t)+bt` | Pointwise; three inputs | Compatible `a,b`; scalar or componentwise `t` | Blend any two values, masks, crossfades | Decide whether `t` is clamped; interpolate straight-alpha colors only with an explicit alpha/color-space policy | **Implemented node-specifically** in Mix and mask application; no general typed-value primitive |

## Nonlinear scalar functions

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `PIX.POW` | Power | `y = x^p` | Pointwise; componentwise for vectors | For arbitrary real `p`, normally `x≥0`; otherwise define signed/integer-power behavior | Gamma-style shaping, polynomials, nonlinear contrast | `pow(negative,fractional)` is undefined/NaN; `0^0` policy; exponent convention must be named | **Absent** generically; node-specific in View Transform, LUT gamma, Tone, and Dither; Dither can generate NaN on negative input |
| `PIX.SQRT` | Square root | `y = √x` | Pointwise unary | `x≥0`, or explicit complex/signed extension | Encoding curves, distance/magnitude components | Negative policy required; `sqrt` is `pow(x,0.5)` but merits a direct efficient primitive | **Absent** generically |
| `PIX.LOG` | Logarithm | `y = log_b(x) = ln(x)/ln(b)` | Pointwise unary | `x>0`, `b>0`, `b≠1`; or explicit floor/negative extension | Diagnostic log, dynamic-range compression building block | A raw logarithm is not a named camera/log encoding; expose base and floor and preserve inverse parameters | **Absent** generically; local shaders use inconsistent epsilon guards |
| `PIX.LOG2` | Base-2 logarithm | `y = log₂(x)` | Pointwise unary | `x>0` or declared floor | Stops/EV math, powers-of-two analysis | Near-zero values become very negative; scene-linear negatives need a policy | **Absent** generically; embedded in scene/tone code |
| `PIX.EXP` | Exponential | `y = e^x` | Pointwise unary | Floating point with overflow policy | Inverse natural-log transform, procedural curves | Not a production “inverse log encoding” without matched scaling/offset parameters | **Absent** generically |
| `PIX.EXP2` | Base-2 exponential | `y = 2^x` | Pointwise unary | Floating point with overflow policy | EV-to-gain conversion | Often paired with `log2`; large values overflow quickly | **Absent** generically; embedded in RAW exposure |
| `PIX.FRACT` | Fractional part | `y = x-floor(x)` | Pointwise unary | Numeric; GLSL convention gives `[0,1)` even for negative inputs via `floor` | Repeating ramps, tiling, procedural patterns | Other languages define fractional/remainder behavior differently for negatives | **Node-specific** in wrapping/effect shaders; no graph primitive |
| `PIX.MOD` | Modulo / floor remainder | `y = x - m·floor(x/m)` | Pointwise; two inputs | `m≠0`; declared negative convention | Periodic patterns, wrapping | Distinguish floor-mod from C/GLSL `mod`/remainder conventions; define zero divisor | **Absent** generically |
| `PIX.FLOOR` | Floor | `y = ⌊x⌋` | Pointwise unary | Numeric | Indexing, quantization, procedural grids | Negative inputs round toward `-∞`, not toward zero | **Absent** generically; embedded in shaders |
| `PIX.CEIL` | Ceiling | `y = ⌈x⌉` | Pointwise unary | Numeric | Upper quantization and indexing | Negative behavior differs from truncation | **Absent** generically |
| `PIX.ROUND` | Round to nearest | `y = round(x)` with explicitly selected tie rule | Pointwise unary | Numeric | Quantization, banding, integer-like effects | Tie-to-even vs away-from-zero differs across APIs; declare deterministic rule | **Node-specific** in dithers/quantizers; no graph primitive |
| `PIX.QUANTIZE` | Uniform quantize | Endpoint-inclusive normalized form: `y=round(x·(N-1))/(N-1)`, `N≥2` | Pointwise | Declared interval, level count, rounding, clamp/dither policy | Posterization, bit-depth simulation | `2^bits` bins vs `2^bits-1` denominator is a common off-by-one source; Stack’s current dither uses `round(color·2^bits)/2^bits` | **Node-specific and semantically questionable** in Dither/Cell Shading; recommended explicit primitive |
| `PIX.SIGN` | Sign | `y=-1 if x<0; 0 if x=0; +1 if x>0` | Pointwise unary | Ordered numeric; NaN policy | Signed masks, direction, signed-power construction | Floating-point `-0` and NaN behavior should be stated | **Absent** generically |

## Thresholds, comparisons, and selection

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `PIX.STEP` | Step / hard threshold | `y = 0 if x<edge, else 1` | Pointwise | Ordered scalar/channel; threshold in same units | Binary masks and hard decisions | Equality convention matters; antialiasing/softness requires a different operation | **Node-specific** in Mask Threshold; no generic numeric primitive |
| `PIX.SMOOTHSTEP` | Cubic smooth threshold | `t=clamp((x-e0)/(e1-e0),0,1)`; `y=t²(3-2t)` | Pointwise | `e0≠e1` or declared degenerate behavior | Soft masks and C¹ transitions | Reversed/equal edges and working units must be defined; this is not a Gaussian feather | **Node-specific** in mask/effect shaders; no generic primitive |
| `PIX.COMPARE` | Numeric comparisons | `x<y`, `≤`, `>`, `≥`, `==`, `!=` → Boolean/mask | Pointwise; two inputs | Comparable types; tolerance policy for float equality | Conditions, keys, range tests | Exact float equality is brittle; vector comparison needs per-component versus all/any choice | **Absent**; recommended core primitive family |
| `PIX.IS_FINITE` | Finite / NaN / Inf tests | `isfinite(x)`, `isnan(x)`, `isinf(x)` | Pointwise unary | Floating point | Debugging, sanitizing imported/model data | Do not silently sanitize without exposing replacement policy | **Absent**; recommended reliability primitive |
| `PIX.BOOL` | Boolean logic | `AND`, `OR`, `XOR`, `NOT` on Boolean/mask values | Pointwise | Boolean, or explicitly binarized mask | Combine conditions and hard masks | Soft-mask arithmetic (`min/max/product`) is not Boolean logic; keep them separate | **Absent** as typed logic; mask-combine nodes use continuous formulas |
| `PIX.SELECT` | Conditional select | `y = condition ? a : b` | Pointwise; three inputs | Boolean condition; compatible branches | Branching math, range guards, mask-driven processing | A soft blend is `lerp`, not Boolean select; GPU branchless implementation is allowed if results match | **Node-specific / composable** through threshold + Mix; no generic typed primitive |

## Procedural shaping functions

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `PIX.BIAS_CURVE` | Procedural bias | One common definition: `bias(x,b)=x^(log(b)/log(0.5))`, with `x,b∈[0,1]` | Pointwise | Normalized scalar and a chosen documented definition | Push normalized values toward 0 or 1 | “Bias” has multiple incompatible formulas; do not ship the name without the exact equation, identity (`b=.5`), and endpoint policy | **Absent** |
| `PIX.GAIN_CURVE` | Procedural gain curve | Symmetric construction from a chosen bias function around `x=.5` | Pointwise | Normalized scalar and chosen bias definition | S-shaped contrast around the midpoint | Do not confuse with multiplicative image gain; several published formulas differ | **Absent**; reserve a distinct name |

## Recommended Stack treatment

- Make arithmetic, nonlinear functions, comparisons, select, and vector-safe broadcasting the **primitive layer**.
- Keep Brightness, Exposure, Contrast, Levels, Posterize, and similar familiar controls as **convenience nodes** with a visible “method/domain” contract and an inspectable composition.
- Add a graph-wide numeric policy for division by zero, non-finite values, negative power/log inputs, clipping, and alpha. Per-node accidents are already present in Divide and Dither.
- Do not let a scalar output masquerade as a generic `Image`. A future value system should distinguish scalar, Boolean, vector, image, and mask while still permitting explicit broadcasting.
- Fuse adjacent pointwise primitives into one shader/pass before encouraging very granular graphs.

## Authoritative references

- [OpenGL Shading Language 4.60 specification — built-in numeric functions and edge cases](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.pdf)
- [W3C CSS Color 4 — linear-light versus encoded RGB and color math](https://www.w3.org/TR/css-color-4/)
- [IEEE 754-2019 overview and standard record — floating-point arithmetic](https://standards.ieee.org/standard/754-2019.html)
- [Khronos OpenGL `glReadPixels` reference — conversion/clamping at Stack’s current export readback](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glReadPixels.xhtml)
