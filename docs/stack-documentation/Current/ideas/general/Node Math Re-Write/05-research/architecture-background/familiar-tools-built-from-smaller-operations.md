# 15 — High-Level Tools Decomposed into Lower-Level Operations

> **Research status:** Architecture rationale and proposal, not an accepted
> implementation contract. Current code/tests, accepted product direction, and
> exact technical contracts take precedence.

## Purpose

This document tests the “math Legos” idea against real editing tools. It does not claim that every high-level node should literally execute as dozens of ordinary graph passes. It defines each tool as an inspectable semantic graph. The compiler may fuse pointwise stages or replace a proven-equivalent subgraph with an optimized specialized implementation.

Three levels of truth are used:

1. **Exact decomposition:** the high-level tool is fully defined by the listed stages.
2. **Parameterized family:** several legitimate formulas exist; the node must name the selected variant.
3. **Conceptual decomposition:** the stages are understandable, but the operation requires specialized analysis, iteration, or reconstruction that should remain a dedicated execution unit.

## 1. Exposure

**Status:** exact pointwise decomposition.

```text
Color Image
→ require scene-linear color
→ RGB × 2^EV
→ preserve alpha and extended range
```

Formula:

$$RGB_{out}=RGB_{in}2^{EV}$$

Primitives: `Exp2`, `Multiply`, scalar broadcast.  
Warnings: encoded input, display-referred input, unexpected clamp downstream.  
Stack today: RAW exposure follows this model; ordinary image nodes have no general Exposure node and no semantic guarantee.

## 2. Brightness as explicit variants

**Status:** parameterized family.

Do not create one primitive named Brightness. Create a friendly node with a visible **Method**:

| Method | Definition | Typical appearance |
|---|---|---|
| Offset | $RGB'=RGB+b$ | Moves black and white together; clips quickly in bounded data |
| Gain | $RGB'=RGBg$ | Scales from zero; exposure-like only in linear light |
| Perceptual lightness | Convert to Oklab/Lab, change $L$, convert back | More even apparent brightness; may gamut-map |
| Curve | $RGB'=f(RGB)$ or luminance-scale reconstruction | Custom tone shaping |

Stack today: Brightness is offset plus final clamp. Its high-level label should expose that fact.

## 3. Contrast around a pivot

**Status:** exact once domain and pivot are selected.

```text
Input → subtract pivot → multiply contrast → add pivot → optional clamp
```

$$y=p+c(x-p)$$

Expose pivot, contrast factor, processing basis (per-channel or luma), and clamp. Stack’s current Contrast is this formula with $p=0.5$, $c=1+amount$, per-channel encoded/unknown RGB, and final `[0,1]` clamp.

## 4. S-curve contrast

**Status:** exact curve compound.

```text
Input measure x
→ 1D monotone curve f(x)
→ either apply per channel or scale RGB by f(Y)/Y
```

Possible implementations include editable splines, a logistic/sigmoid normalized to endpoints, or two smooth polynomial halves. “S-curve” describes shape, not coefficients. Required primitives: `Curve1D`, luminance/chroma-preserving reconstruction, safe divide. Stack already evaluates a private 256-sample Tone Curve LUT; it needs a reusable Curve value and declared domain.

## 5. Levels

**Status:** exact once order/convention is chosen.

One conventional endpoint-inclusive form:

$$u=\operatorname{clamp}\left(\frac{x-b}{w-b},0,1\right),\qquad y=o_b+(o_w-o_b)u^{1/\gamma}$$

```text
Subtract input black
→ safe divide by input span
→ optional clamp
→ midpoint power
→ output-range scale and offset
```

Expose input black/white, midpoint convention, output black/white, linked RGB versus per-channel, and clamp. Do not label the midpoint exponent merely “gamma encoding.”

## 6. Lift / Gamma / Gain

**Status:** parameterized grading family.

A declared implementation might be:

```text
Input → lift/offset shaping → guarded power → gain → optional range policy
```

But grading applications use different lift formulas and ordering. If Stack adopts a formula, give it a stable technical name/version. If the intended model is ASC CDL, expose it separately as Slope/Offset/Power plus Saturation rather than calling it universal LGG.

## 7. Shadows, midtones, and highlights adjustments

**Status:** exact once masks and adjustment operators are defined.

```text
Luma/tonal measure Y
→ three overlapping weight curves Ws, Wm, Wh
→ apply selected shadow/mid/high operator
→ weighted combine
```

Required primitives: `Luma`, `Curve1D` or `Smoothstep`, `Multiply`, `Add`, `Mix`, safe normalization. A strong design lets the high-level node show its three weight curves and lets users open the compound.

Stack today uses nonoverlapping Rec.709-based weights, additive chroma offsets for shadows/midtones, multiplicative highlight chroma, and final clamp. That is a legitimate specific effect, but the asymmetry should not silently define all future three-way grading.

## 8. Three-way color wheels

**Status:** parameterized family.

```text
Input RGB
→ derive tonal measure
→ derive shadow/mid/high weights
→ convert each wheel position to a declared color adjustment
→ apply adjustment in declared color model
→ weighted merge
```

Possible wheel operators include RGB offset, printer-light-like log exposure, chroma-axis movement in a perceptual space, or balance around an achromatic axis. The wheel UI is an interaction device, not the algorithm.

Recommended inspectable interface:

- Tonal masks shown as editable mini-curves.
- Wheel method and processing space in Technical Details.
- Luma-preservation toggle with named formula.
- Optimized one-shader execution when all stages are pointwise.

## 9. Split toning

**Status:** exact pointwise compound.

```text
Measure Y
→ shadow weight Ws(Y)
→ highlight weight Wh(Y)
→ apply two tint operators
→ balance/crossover control shifts weights
→ mix by amount
```

This is a smaller two-zone form of three-way grading. The tint operator and working space must be explicit.

## 10. Hue-versus-saturation

**Status:** exact pointwise compound.

```text
Convert working RGB → chosen hue/chroma representation
→ evaluate periodic curve k = Curve(hue)
→ C' = C × k
→ convert back → optional gamut mapping
```

Required primitives: explicit color conversion, periodic `Curve1D`, chroma scale, gamut policy. Near the neutral axis hue is unstable/undefined; gate the adjustment by chroma. “HSV saturation” and “OKLCh chroma” produce different tools.

## 11. Saturation

**Status:** exact once achromatic axis and space are named.

Simple RGB-axis form:

$$g=w_RR+w_GG+w_BB,\qquad RGB'=g+s(RGB-g)$$

Primitives: dot product, scalar broadcast, subtract, multiply, add. This can fuse into a single matrix-like expression. Stack currently uses Rec.709 coefficients with factor `1+amount`, regardless of whether input is encoded or scene-linear.

## 12. Vibrance

**Status:** heuristic family, still pointwise in many implementations.

One transparent design:

```text
Convert to hue/chroma/lightness representation
→ measure normalized chroma Cn
→ muted-color weight w = (1-Cn)^p
→ optional skin/hue protection mask k(h)
→ C' = C × [1 + amount × w × (1-k)]
→ gamut compress → convert back
```

This makes the heuristic visible. Skin protection is optional and culturally/illumination sensitive; it should not be hidden as universal truth. A simpler no-skin variant is more “rudely factual.” Stack has no current Vibrance node.

## 13. Temperature and tint

**Status:** several different families.

Possible decompositions:

1. **RAW white-balance gains:** camera-plane/channel gains from metadata or a neutral sample.
2. **Chromatic adaptation:** RGB→XYZ, adapt source white to destination white with a named transform, XYZ→RGB.
3. **Creative warmth:** apply an explicit RGB/opponent-axis offset.

These should not share one ambiguous primitive. Stack’s Warmth is the third type: add `0.1×amount` to red and subtract it from blue.

## 14. Vignette

**Status:** exact coordinate/pointwise compound.

```text
Normalized coordinates
→ distance from center with aspect/shape transform
→ radial falloff curve m(r)
→ color or exposure adjustment weighted by m
```

For exposure-style vignette in linear light:

$$RGB'=RGB\,2^{EV\,m(r)}$$

For a color mix:

$$RGB'=\operatorname{mix}(RGB,C,m(r))$$

Stack currently implements a radial weight and color mix/multiply-like effect. A calibrated lens-vignetting correction is a different metadata-dependent operation.

## 15. Unsharp mask / sharpen

**Status:** exact neighborhood compound.

```text
Input → low-pass blur L
Input - L → signed detail H
threshold/halo gate H
Input + amount×H
```

Required infrastructure: neighborhood filter, signed intermediate, threshold curve, alpha-safe filtering. Stack’s Sharpen uses a fixed four-neighbor average and smooth detail gate; a user-facing general Unsharp Mask should expose radius/blur definition separately.

## 16. Clarity

**Status:** conceptual/parameterized neighborhood compound.

```text
Luminance or log-luminance
→ edge-aware or multiscale base/detail decomposition
→ select mid-frequency detail
→ tonal gate, commonly suppress extreme shadows/highlights
→ scale detail with halo limiting
→ reconstruct color
```

Clarity is not merely a contrast slider. A faithful version needs neighborhood/pyramid infrastructure. It can be inspectable as stages but may execute as specialized fused/multipass code.

## 17. Texture

**Status:** parameterized frequency-band compound.

```text
Create fine-scale detail band Bsmall - Bmedium
→ protect noise/flat regions if desired
→ scale band
→ reconstruct
```

The difference from Clarity is mostly the selected spatial scale and gating, not a universal industry formula. Expose scale in pixel units or a resolution-normalized policy.

## 18. Frequency separation

**Status:** exact once method is selected.

Additive form:

$$Low=L(I),\qquad High=I-Low,\qquad I=Low+High$$

Divisive/log forms are different and may better preserve ratios. The compound should have two outputs and a reconstruction invariant test. Stack’s current FFT path converts to grayscale and has magnitude/recombine defects, so it should not be used as the semantic base for spatial RGB frequency separation.

## 19. Dehaze by dark-channel prior

**Status:** conceptual/staged global + neighborhood algorithm.

```text
Estimate atmospheric light A globally
→ compute local dark channel
→ estimate transmission t(x)
→ refine t with guided/edge-aware filtering
→ recover J = (I-A)/max(t,t0) + A
→ color/range cleanup
```

The method rests on a natural-image prior and can fail on sky/bright objects. This is a convenience algorithm, not a universal “dehaze formula.” It needs reduction, min filter, guided filter, safe divide, and declared input domain.

## 20. Global photographic tone mapping

**Status:** named algorithm family.

A Reinhard-style global decomposition:

```text
Scene-linear luminance
→ log-average luminance
→ key/exposure normalization
→ compress luminance with named curve
→ scale RGB by mapped/original luminance
→ gamut/display encoding later
```

The node should not pretend the curve alone completes the output transform. Scene-to-display rendering also needs color appearance/gamut handling and target display encoding.

## 21. Filmic/view transform

**Status:** compound rendering family, not one curve.

```text
Scene working RGB
→ exposure/reference scaling
→ tone scale with toe/shoulder
→ chroma and hue management
→ gamut compression/mapping
→ display-linear target primaries
→ target display encoding/EOTF inverse as appropriate
```

ACES 2 documents a similar separation between rendering and display encoding. Stack’s current View Transform performs part of the range/tone/gamut work but omits the final display OETF and is followed by an unmanaged viewport.

## 22. Highlight compression versus highlight recovery

These are not synonyms.

- **Compression:** map valid high scene values into a smaller output range with a shoulder/curve. Pointwise or global tone mapping.
- **Recovery:** infer a clipped channel from unclipped RAW channels/neighbors/priors. Metadata/neighborhood-dependent and cannot restore detail if all channels are clipped.

A high-level Develop node may contain both, but the internal stages and failure limits should remain distinct.

## 23. Local tone mapping

**Status:** conceptual multiscale compound.

```text
Scene luminance
→ edge-aware base/detail or pyramid
→ compress base/global range
→ preserve or reshape detail bands
→ reconstruct luminance
→ restore color/chroma
→ output/view transform
```

It requires neighborhood or multiscale passes and careful halo/temporal behavior. It should remain a specialized execution class even if the internal stages are visible.

## 24. Auto exposure

**Status:** inspectable analysis compound.

```text
Image → luminance
→ metering mask/ROI
→ percentile or log-average reduction
→ solve EV against target gray/peak policy
→ Exposure node
```

This is a strong example of separating measurement from application. Users can edit the metering rule without changing Exposure math. Stack’s RAW auto routines do this privately; a general system needs scalar/reduction outputs.

## 25. Automatic white balance

**Status:** method-specific analysis compound.

Gray-world example:

```text
Scene RGB → masked channel means
→ solve relative channel gains
→ White Balance Gains
```

Other estimators may use white-patch, gamut, metadata, or learned models. The compound name must show the selected estimator, and the correction stage should remain an explicit gains/adaptation node.

## 26. Chroma key / background removal

**Status:** compound mask + composite tool.

```text
Convert to keying space
→ color-distance matte
→ threshold/softness
→ morphology/edge refinement
→ despill color operation
→ produce straight/premultiplied foreground
```

Stack’s Background Remover has a color-distance matte but several advanced paths are incomplete. The mask and despill should be separately inspectable outputs/stages.

## 27. LUT application with explicit domain

**Status:** exact color-transform compound.

```text
Declared input color state
→ optional shaper/transfer
→ domain map
→ 1D/3D LUT interpolation
→ optional output transform
→ declared output color state
```

`.cube` does not reliably declare primaries, white point, scene/display state, or intended role. Stack should store user-assigned input/output descriptors and warn when they are unknown. CLF is a stronger format for ordered self-contained technical transforms.

## Choosing graph versus specialized execution

| Operation | Authored representation | Recommended execution |
|---|---|---|
| Exposure, offset, contrast, saturation, vignette | Primitive graph | Fuse into pointwise shader |
| Levels, S-curve, hue-vs-sat | Primitive graph with curve/color conversion | Fuse pointwise; bind curve/LUT resource |
| Three-way grade, split tone, vibrance | Primitive graph or canonical compound | Fuse pointwise when possible |
| Unsharp, clarity, dehaze | Inspectable staged compound | Specialized sampled/multipass plan |
| Auto exposure/WB | Analysis compound | Reduction pass + scalar application |
| Tone/view transform | Versioned technical compound | Optimized color processor with visible stages |
| RAW highlight recovery, demosaic | Conceptual decomposition + dedicated node | Specialized metadata-aware kernels |

The visible definition is the source of meaning. The physical plan is the source of performance. Equivalence tests connect them.

## Sources

- [W3C CSS Color 4](https://www.w3.org/TR/css-color-4/)
- [ASC Color Decision List](https://theasc.com/asc/asc-cdl)
- [ACES Output Transforms](https://docs.acescentral.com/system-components/output-transforms/)
- [OpenColorIO concepts/configuration](https://opencolorio.readthedocs.io/en/latest/guides/authoring/authoring.html)
- [He, Sun, and Tang, Dark Channel Prior Dehazing](https://doi.org/10.1109/TPAMI.2010.168)
- [Reinhard et al., Photographic Tone Reproduction](https://doi.org/10.1145/566570.566575)
- [Common LUT Format specification](https://docs.acescentral.com/clf/specification/)
