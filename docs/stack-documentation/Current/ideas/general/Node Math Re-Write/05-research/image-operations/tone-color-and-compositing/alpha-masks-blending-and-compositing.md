# 05 — Alpha, Masks, Blend Modes, and Compositing

> **Reference status:** Formulas and sources are technical reference. Any Stack
> status in this file is a 2026-07-12 snapshot, not current state or a release
> backlog.

## The essential separation

A **blend function** decides what color two overlapping colors produce. A **compositing operator** decides how source and backdrop coverage/alpha combine. They are separate stages.

Let:

- $C_s,C_b$ be straight source/backdrop color.
- $\alpha_s,\alpha_b$ be source/backdrop alpha.
- $c_s=\alpha_sC_s$ and $c_b=\alpha_bC_b$ be premultiplied colors.
- $c_o$ be premultiplied output color.

The general Porter–Duff form is:

$$c_o=F_sc_s+F_bc_b,\qquad \alpha_o=F_s\alpha_s+F_b\alpha_b$$

For source-over:

$$c_o=c_s+c_b(1-\alpha_s),\qquad \alpha_o=\alpha_s+\alpha_b(1-\alpha_s)$$

If straight output is required, $C_o=c_o/\alpha_o$ when $\alpha_o>0$. When $\alpha_o=0$, Stack must use a declared policy; returning straight transparent black is the simplest deterministic choice, while preserving hidden straight RGB is a different explicit policy.

Stack’s current Mix “Alpha Over” omits the backdrop-alpha factor in its straight-looking RGB numerator. It is incorrect for both straight and premultiplied source-over. The future operator must use normative equations and be versioned separately from legacy behavior.

## Representation operations

| ID | Operation | Canonical formula/algorithm | Scope | Required state | What it does/common uses | Cautions | Stack status, 2026-07-12 |
|---|---|---|---|---|---|---|---|
| `COMP.ALPHA_EXTRACT` | Extract alpha | $m=\alpha$ | `P` | RGBA with declared alpha | Produces coverage/matte | Output is a mask/coverage field, not generic RGB | **Existing:** Channel Split can extract A, but output semantics are overloaded Mask |
| `COMP.SET_ALPHA` | Set/replace alpha | $\alpha'=m$; RGB policy declared | `P+M` | Color + mask | Assigns coverage | For premultiplied output, RGB must be adjusted consistently; replacing alpha alone can violate association | **Partial:** Channel Combine can construct alpha, no semantic validation |
| `COMP.PREMULT` | Premultiply | $c=\alpha C$ | `P` | Straight RGBA | Converts straight to associated representation | Preserve alpha; behavior of nonfinite/negative alpha declared | **Missing** explicit node |
| `COMP.UNPREMULT` | Unpremultiply | $C=c/\alpha$ for $\alpha>\epsilon$ | `P` | Premultiplied RGBA | Exposes independent RGB for color edits | Define $\alpha\le\epsilon$ policy; can amplify noise/garbage | **Missing** explicit node |
| `COMP.OPACITY` | Source/group opacity | Premultiplied form $c'=kc$, $\alpha'=k\alpha$ | `P` | Declared alpha association | Fades a source/group without changing its straight color | Multiplying alpha only is wrong for premultiplied data; opacity is not automatically a crossfade with arbitrary background | **Partial:** Mix factor and mask interpolation, but no system alpha contract |
| `COMP.MASK_MIX` | Mask interpolation | $O=(1-m)A+mB$ | `P+M` | Compatible values and scalar mask | Applies local adjustment/crossfade | Decide whether $m$ clamps; RGBA interpolation requires alpha/color policy | **Existing:** generic mask blend uses clamped red channel |

## Porter–Duff compositing operators

The table uses premultiplied inputs. These are coverage operators; they are not artistic blend modes.

| ID | Operator | Factors $(F_s,F_b)$ | Result meaning | Common uses | Stack status |
|---|---|---|---|---|---|
| `COMP.PD_CLEAR` | Clear | $(0,0)$ | Transparent output | Erase/initialize | **Missing** |
| `COMP.PD_COPY` | Source / Copy | $(1,0)$ | Source replaces backdrop | Replace region | **Partial:** Normal Mix selects B by convention, but not a named coverage operator |
| `COMP.PD_DEST` | Destination | $(0,1)$ | Keep backdrop | Identity/bypass | **Missing** explicit |
| `COMP.PD_SRC_OVER` | Source over | $(1,1-\alpha_s)$ | Source in front of backdrop | Standard layer composite | **Needs fix:** current Alpha Over is defective |
| `COMP.PD_DST_OVER` | Destination over | $(1-\alpha_b,1)$ | Backdrop in front of source | Under composite | **Missing** |
| `COMP.PD_SRC_IN` | Source in destination | $(\alpha_b,0)$ | Source only where backdrop exists | Stencil/intersection | **Missing** |
| `COMP.PD_DST_IN` | Destination in source | $(0,\alpha_s)$ | Backdrop only where source exists | Matte backdrop | **Missing** |
| `COMP.PD_SRC_OUT` | Source out of destination | $(1-\alpha_b,0)$ | Source only outside backdrop | Knockout/holdout | **Missing** |
| `COMP.PD_DST_OUT` | Destination out of source | $(0,1-\alpha_s)$ | Backdrop only outside source | Holdout | **Missing** |
| `COMP.PD_SRC_ATOP` | Source atop destination | $(\alpha_b,1-\alpha_s)$ | Source over, confined to backdrop coverage | Texture inside existing matte | **Missing** |
| `COMP.PD_DST_ATOP` | Destination atop source | $(1-\alpha_b,\alpha_s)$ | Backdrop over, confined to source coverage | Reverse atop | **Missing** |
| `COMP.PD_XOR` | XOR | $(1-\alpha_b,1-\alpha_s)$ | Nonoverlapping parts only | Matte difference/graphic composite | **Missing** |
| `COMP.PD_PLUS` | Plus / lighter | $(1,1)$ | Add premultiplied contributions; may exceed one, so alpha/range clamping must be explicit | Additive light accumulation | **Partial:** Add blend performs componentwise RGBA add, not a fully declared operator |

## Blend-mode integration

For a blend function $B(C_b,C_s)$ followed by source-over, W3C’s premultiplied overlap model may be written:

$$c_o=(1-\alpha_s)c_b+(1-\alpha_b)c_s+\alpha_s\alpha_bB(C_b,C_s)$$

$$\alpha_o=\alpha_s+\alpha_b(1-\alpha_s)$$

This shows why applying a blend formula componentwise to RGBA is not sufficient.

| ID | Blend function $B(C_b,C_s)$ | Formula/definition | Typical use | Domain/cautions | Stack status |
|---|---|---|---|---|---|
| `BLEND.NORMAL` | Normal | $B=C_s$ | Ordinary source-over | Still requires compositing equation | **Partial:** Normal Mix crossfades values rather than a declared source-over layer operation |
| `BLEND.ADD` | Add | $B=C_b+C_s$ | Light, glows, accumulation | Preserve HDR; clamp is separate | **Existing** componentwise mode, alpha semantics wrong/undeclared |
| `BLEND.SUBTRACT` | Subtract | $B=C_b-C_s$ | Difference/effects | Signed output and operand order | **DataMath exists**, not Mix blend mode |
| `BLEND.MULTIPLY` | Multiply | $B=C_bC_s$ | Darkening, texture/color combination | Appearance differs in encoded versus linear RGB | **Existing** Mix mode; currently applies to RGBA too |
| `BLEND.SCREEN` | Screen | $B=1-(1-C_b)(1-C_s)$ | Brightening | Normalized formula; HDR extension policy required | **Existing** Mix mode; current RGBA componentwise treatment |
| `BLEND.OVERLAY` | Overlay | If $C_b\le.5$, $2C_bC_s$; else $1-2(1-C_b)(1-C_s)$ | Contrast with backdrop controlling branch | Backdrop branch distinguishes it from Hard Light | **Missing** |
| `BLEND.HARDLIGHT` | Hard light | Overlay with source/backdrop roles swapped | Source-controlled contrast | Source branch; normalized/HDR policy | **Missing** |
| `BLEND.SOFTLIGHT` | Soft light | Use the W3C/PDF piecewise function, including its low-backdrop cubic helper | Gentler contrast/lighting | Several incompatible historical formulas exist; name the W3C variant | **Missing** |
| `BLEND.DARKEN` | Darken | $\min(C_b,C_s)$ | Keep darker components | Per-component result may synthesize color | **DataMath Minimum exists**, not declared blend/composite |
| `BLEND.LIGHTEN` | Lighten | $\max(C_b,C_s)$ | Keep lighter components | Per-component result may synthesize color | **DataMath Maximum exists** |
| `BLEND.DIFFERENCE` | Difference | $\lvert C_b-C_s\rvert$ | Alignment and graphic inversion | Not a perceptual color difference | **Existing:** DataMath Difference |
| `BLEND.EXCLUSION` | Exclusion | $C_b+C_s-2C_bC_s$ | Softer difference | Normalized formula; HDR extension policy | **Missing** |
| `BLEND.DIVIDE` | Divide | Declared variant, commonly $C_b/\max(C_s,\epsilon)$ | Brightening/normalization effects | Zero, sign, HDR, clamp and operand convention required | **DataMath Divide exists with sign defect** |
| `BLEND.DODGE` | Color dodge | If $C_s\ge1$, 1; else $\min(1,C_b/(1-C_s))$ | Strong brightening | Denominator/extended-range behavior specified by W3C normalized form | **Missing** |
| `BLEND.BURN` | Color burn | If $C_s\le0$, 0; else $1-\min(1,(1-C_b)/C_s)$ | Strong darkening | Same normalized-domain issue | **Missing** |
| `BLEND.HUE` | Hue | W3C nonseparable `SetLum(SetSat(Cs,Sat(Cb)),Lum(Cb))` | Apply source hue | Uses W3C Lum/Sat/ClipColor helpers, not arbitrary HSV replacement | **Missing** |
| `BLEND.SATURATION` | Saturation | `SetLum(SetSat(Cb,Sat(Cs)),Lum(Cb))` | Apply source saturation | Named standardized helper algorithm | **Missing** |
| `BLEND.COLOR` | Color | `SetLum(Cs,Lum(Cb))` | Apply source hue/saturation while retaining backdrop luminosity | Standardized numeric RGB helper, domain-dependent appearance | **Missing** |
| `BLEND.LUMINOSITY` | Luminosity | `SetLum(Cb,Lum(Cs))` | Apply source luminosity | “Luminosity” here is W3C blend helper, not physical luminance | **Missing** |

## Matte and mask operations

| ID | Operation | Canonical formula/algorithm | Scope | Meaning/use | Cautions | Stack status |
|---|---|---|---|---|---|---|
| `MASK.INTERSECT` | Intersection | Soft coverage $m=ab$ | `P+M` | Keep common coverage | Product is probabilistic/coverage-like; Boolean AND requires binarization | **Existing:** Intersect uses multiply |
| `MASK.UNION_COVER` | Coverage union | $m=a+b-ab$ | `P+M` | Union of independent/overlapping coverage | Different from max; coverage semantics required | **Missing** |
| `MASK.UNION_MAX` | Maximum union | $m=\max(a,b)$ | `P+M` | Keep stronger selection | Useful fuzzy-set choice, not Porter–Duff union | **Existing:** Add Mask uses max |
| `MASK.SUBTRACT` | Subtraction/holdout | $m=a(1-b)$ | `P+M` | Remove B coverage from A | Different from `max(a-b,0)` | **Existing:** Subtract Mask uses this form |
| `MASK.DIFFERENCE` | Symmetric difference-like soft mask | Named choice: $\lvert a-b\rvert$ or coverage XOR $a(1-b)+b(1-a)$ | `P+M` | Nonoverlap/difference | These formulas differ for soft values | **Existing:** Difference uses absolute difference |
| `MASK.INVERT` | Invert | $m'=1-m$ | `P` | Reverse selection | Assumes normalized mask; extended values need policy | **Existing** |
| `MASK.OPACITY` | Mask gain | $m'=km$ | `P` | Scale selection strength | Clamp is separate; $k>1$ may be useful | **Partial:** generator/compound opacity paths |
| `MASK.THRESHOLD` | Hard/soft threshold | Step or smoothstep around threshold | `P` | Convert soft probability/coverage to selection | Softness formula and equality rule | **Existing** |
| `MASK.REMAP` | Mask levels/curve | Declared remap $m'=f(m)$ | `P` | Tighten, soften, reshape matte | Gamma/curve and clamp policies | **Existing** linear remap; no first-class curve input |
| `MASK.FEATHER` | Feather | Blur/distance-based falloff of boundary | `N/I` | Soft edge | Gaussian blur and geometric distance feather differ | **Partial:** Custom Mask private blur/feather; no generic morphology/distance system |
| `MASK.HOLDOUT` | Holdout/stencil | Apply mask through an explicit Porter–Duff in/out operator | `P+M` | Cut one element from another | Should not be a vague checkbox; show operator and alpha association | **Missing** general composite |

## Recommended Stack contract

1. Every color image declares `Opaque/NoAlpha`, `Straight`, or `Premultiplied`.
2. Use premultiplied linear-light data as the preferred compositing/filtering representation, while allowing explicit alternatives.
3. Provide `Premultiply` and guarded `Unpremultiply` nodes.
4. Define artistic blend function separately from coverage operator.
5. Make blend space selectable/declared; traditional encoded-RGB appearance and linear-light behavior are both valid when explicit.
6. Do not process alpha with RGB formulas merely because storage is RGBA.
7. Masks are typed scalar fields. Coverage, opacity, alpha, selection, luma, and arbitrary data are related but not identical semantic roles.
8. Correct Alpha Over under a new version and preserve legacy projects through an explicit legacy operator/migration.
9. Test every operator with opaque, translucent, zero-alpha/nonzero-RGB, HDR, negative, and mismatched-association vectors.

## Primary sources

- [W3C Compositing and Blending Level 1](https://www.w3.org/TR/compositing-1/)
- [Porter and Duff, Compositing Digital Images](https://doi.org/10.1145/800031.808606)
- [W3C PNG alpha representation](https://www.w3.org/TR/png-3/#6AlphaRepresentation)
- [OpenEXR premultiplied versus unpremultiplied channels](https://openexr.com/en/latest/TechnicalIntroduction.html#premultiplied-vs-un-premultiplied-color-channels)
- [OpenImageIO ImageBufAlgo `over` and image arithmetic](https://openimageio.readthedocs.io/en/latest/imagebufalgo.html)
