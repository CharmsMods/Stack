# 07 — Geometry, Coordinates, Warping, and Resampling

> **Reference status:** Formulas and sources are technical reference. Any Stack
> status in this file is a 2026-07-12 snapshot, not current state or a release
> backlog.

## Scope

Geometry nodes change where an output pixel samples the source. The robust implementation model is inverse mapping:

$$I_{out}(p)=\operatorname{sample}(I_{in},T^{-1}(p))$$

Forward mapping tends to create holes unless it uses a deliberate splatting/rasterization algorithm. The transform matrix is only one part of the node. Pixel-center convention, output extent, reconstruction filter, antialiasing, border behavior, alpha association, and resolution metadata are equally important.

Stack currently performs most geometry inside a fixed global canvas with linearly filtered normalized coordinates. Crop, Rotate, and Expand Canvas do not actually change downstream raster extent. This is an architectural limitation, not a mathematical requirement.

## Coordinate contract

A future coordinate-aware edge or image descriptor should state:

- Integer pixel-center convention and normalized-coordinate mapping.
- Origin and axis directions.
- Width, height, data window, display/canvas window, and pixel aspect.
- Transform direction: source→destination or destination→source.
- Reconstruction/minification filter and prefilter.
- Border mode and constant border value.
- Whether RGB is straight or premultiplied during resampling.
- Whether the transform changes extent or only samples inside the old extent.

## Canonical operation table

| ID | Operation | Canonical formula or algorithm | Scope | Required state | What it does/common uses | Cautions | Stack status, 2026-07-12 |
|---|---|---|---|---|---|---|---|
| `GEO.TRANSLATE` | Translate | $p_s=p_d-t$ | `C` | Coordinate convention, extent, border | Moves content | The inverse sign is easy to get wrong; decide whether canvas moves or content moves | **Partial:** expressible inside existing fixed-canvas transform/effect code; no general coordinate value |
| `GEO.SCALE` | Scale | $p_s=c+(p_d-c)/s$ | `C+N` | Center, output extent, reconstruction filter | Resize or zoom | Downscaling requires a low-pass/prefilter; magnification factor uses inverse source mapping | **Partial:** no general extent-changing scale node |
| `GEO.ROTATE` | Rotate | $p_s=c+R(-\theta)(p_d-c)$ | `C+N` | Center, extent, filter, border | Rotates image | Output extent may crop corners or expand; interpolation changes result | **Existing:** fixed-canvas, linear/clamp sampling |
| `GEO.SHEAR` | Shear | $[x_s,y_s]^T=S^{-1}[x_d,y_d]^T$ | `C+N` | Origin/center and extent | Skew and affine construction | Needs inverse matrix and resampling | **Missing** as public node |
| `GEO.FLIP` | Flip/reflect | $x_s=W-1-x_d$ and/or $y_s=H-1-y_d$ | `C` | Pixel-center convention | Horizontal/vertical mirror | Exact integer mapping should avoid half-pixel shifts | **Existing:** primitive-like Flip |
| `GEO.AFFINE` | Affine transform | $\tilde p_s=A^{-1}\tilde p_d$ for 2×3 homogeneous affine $A$ | `C+N` | Matrix direction, extent, filter | Unified translate/rotate/scale/shear | Convenience umbrella over primitives; singular matrices must error | **Missing** general matrix-driven node |
| `GEO.HOMOGRAPHY` | Projective transform / homography | $\tilde p_s\sim H^{-1}\tilde p_d$, divide by homogeneous $w$ | `C+N` | 3×3 matrix, $w$ policy, extent | Perspective correction and planar mapping | Homography and “perspective transform” are the same family; handle $w\approx0$ | **Missing** |
| `GEO.CROP` | Crop | Change data/canvas window or sample only a rectangle | `C` | Data window and canvas semantics | Trim image, isolate region | A true crop changes extent; masking outside the rectangle is a different operation | **Needs fix:** current Crop masks in a fixed raster |
| `GEO.CANVAS` | Canvas resize/reformat | Allocate a new extent and place source via an affine mapping | `C+N` | New extent, anchor, fill, filter | Add borders, reframe, standardize output size | Distinguish canvas-only change from image resampling | **Missing:** Expand Canvas does not change extent |
| `GEO.REMAP` | General coordinate remap | $I_o(p)=\operatorname{sample}(I_i,M(p))$ | `C+M+N` | Coordinate field type and units | Common foundation for warps | Map may be absolute coordinates or displacement; declare validity/border | **Missing** first-class coordinate-field type |
| `GEO.DISPLACE` | Displacement map | $M(p)=p+sD(p)$ | `C+M+N` | Vector field, scale, units | Texture warps, lens/heat effects | A scalar mask does not define 2D displacement without a direction rule | **Partial:** effects embed private procedural displacement |
| `GEO.MESH` | Mesh warp | Interpolate source/destination control meshes and resample | `C+M+N` | Mesh topology, interpolation | Perspective/shape correction, puppet warps | Foldovers and out-of-mesh regions need policy | **Missing** |
| `GEO.LIQUIFY` | Liquify | Integrate/edit a displacement field from brush strokes | `C+M+I` | Editable vector field/history | Interactive push, twirl, bloat | UI interaction creates data; runtime application is still remap | **Missing** |
| `GEO.LENS_PARAM` | Parametric lens distortion | Common radial form $p'=p(1+k_1r^2+k_2r^4+k_3r^6)$ plus tangential terms | `C+N` | Optical center, coefficients, direction | Creative or calibrated distortion correction | Correction requires inverse mapping; creative “amount” and calibrated coefficients are different nodes | **Existing:** simple radial Lens Distortion, not profile-calibrated |
| `GEO.FISHEYE` | Fisheye projection | Map radius to angle with a named model: equidistant, equisolid, stereographic, etc. | `C+N` | Projection model, focal scale | Lens simulation/correction | “Fisheye” is not one equation | **Missing** as declared model |
| `GEO.PANORAMA` | Panorama projection | Convert rays among planar/cylindrical/spherical/equirectangular coordinates | `C+N` | Camera/projection parameters | Stitching and 360° conversion | Projection mapping is distinct from estimating camera alignment | **Missing** |
| `GEO.POLAR` | Cartesian ↔ polar | $r=\sqrt{x^2+y^2}$, $\theta=\operatorname{atan2}(y,x)$; inverse $x=r\cos\theta,y=r\sin\theta$ | `C+N` | Center, angle wrapping, extent | Radial effects and analysis | The output grid/domain must be defined | **Missing** |
| `GEO.TWIRL` | Twirl | $\theta_s=\theta_d-f(r)$ | `C+N` | Center, radius, falloff | Spiral distortion | Use smooth falloff and declare border | **Missing** named node; related private effects |
| `GEO.PINCH` | Pinch/bulge | $r_s=g(r_d)$ with monotone radial mapping | `C+N` | Center, radius, curve | Local radial deformation | Avoid singular/non-monotone mapping unless foldover is intended | **Missing** |
| `GEO.RIPPLE` | Ripple | $p_s=p_d+a\sin(kr+\phi)\hat r$ or directional variant | `C+N` | Center/direction, phase, units | Water/heat stylization | Many valid formulas; node must name its form | **Existing:** radial Ripple, private formula |
| `GEO.HEATWAVE` | Procedural heat distortion | $p_s=p_d+D(p,t)$ where $D$ is time-varying noise/sine field | `C+N` | Time, scale, seed/direction | Mirage effect | Procedural convenience node, not one universal primitive | **Existing:** Heatwave Distortion |
| `GEO.KALEIDO` | Kaleidoscope | Fold polar angle into repeated mirrored sectors | `C+N` | Center, sector count, phase | Symmetric pattern generation | Seam/border sampling and antialiasing required | **Missing** |
| `GEO.TILE` | Tile/repeat | $p_s=\operatorname{fract}(p_d/s)$ | `C+N` | Period, phase, seam policy | Repeat textures | Fractional wrap can create derivative/antialias discontinuities | **Partial:** Block Shift wraps privately |
| `GEO.MIRROR_TILE` | Mirror repeat | Use triangular-wave coordinates, e.g. $u=1-\lvert2\operatorname{fract}(x/2)-1\rvert$ | `C+N` | Period/phase | Seam-reduced repeating patterns | Pixel-center and derivative continuity matter | **Missing** |
| `GEO.OFFSET_WRAP` | Seamless offset | $p_s=(p_d+t)\bmod(W,H)$ | `C+N` | Integer/continuous wrap convention | Move texture seams to center | Modulo behavior for negative coordinates must be fixed | **Partial:** private wrapped effects |
| `GEO.PIXELATE` | Pixelation | Quantize coordinates to a cell representative and sample/average | `C+N` | Cell grid and representative filter | Mosaic/block effect | Center-sample and cell-average produce different results | **Existing:** center-sample Pixelation |
| `RESAMPLE.NEAREST` | Nearest neighbor | Choose sample with nearest pixel center | `N+C` | Pixel-center convention | Masks, pixel art, labels | No smoothing; exact tie rule matters | **Missing** selectable public sampler |
| `RESAMPLE.BILINEAR` | Bilinear | Weighted blend of four nearest samples | `N+C` | Sample grid/border | Fast magnification and mild transforms | Not an adequate antialiasing downfilter by itself | **Implicit existing default** in many Stack textures |
| `RESAMPLE.BICUBIC` | Bicubic family | Separable cubic reconstruction, commonly Mitchell–Netravali with $(B,C)$ | `N+C` | Kernel parameters and support | Higher-quality scaling/rotation | “Bicubic” is a family; can overshoot and ring | **Missing** |
| `RESAMPLE.LANCZOS` | Lanczos | $L_a(x)=\operatorname{sinc}(x)\operatorname{sinc}(x/a)$ for $\lvert x\rvert<a$ | `N+C` | Lobe count $a$, normalization | Sharp resampling and downscaling | Rings/overshoots; needs prefilter/scale-aware support | **Missing** |
| `RESAMPLE.AREA` | Area/box downsampling | Integrate source coverage over each destination pixel footprint | `N+C` | Source/destination footprints | Robust image reduction | Efficient approximations differ; alpha should be premultiplied | **Missing** |
| `RESAMPLE.MIP` | Mipmap/trilinear | Prefilter a pyramid and interpolate between levels | `N+C+I` | Derivatives/scale, pyramid filter | Minification and repeated texture sampling | Mipmap construction and color/alpha domain matter | **Missing** generic image-pyramid sampling |
| `RESAMPLE.EWA` | Elliptical weighted average | Integrate samples with elliptical footprint derived from transform Jacobian | `N+C` | Coordinate derivatives and reconstruction kernel | High-quality anisotropic warps | Complex but valuable for severe perspective/minification | **Missing** |
| `GEO.CONTENT_SCALE` | Seam carving/content-aware scale | Compute energy → dynamic-program minimum seam → remove/insert → repeat | `C+G+I` | Importance/energy map, protect/remove masks | Content-aware retargeting | Global iterative algorithm; not ordinary resampling and may distort structures | **Missing** |
| `GEO.OPTICAL_WARP` | Optical-flow warp | $I_t(p)=\operatorname{sample}(I_0,p+u(p,t))$ | `C+M+N` | Flow field + time fraction | Motion compensation/morphing | Applying flow is geometry; estimating flow is a separate multi-frame analysis | **Missing** coordinate-field type |

## Border modes as explicit data

| Mode | Definition | Typical use |
|---|---|---|
| Clamp | Use nearest edge sample | General photo filtering; may smear borders |
| Mirror | Reflect coordinates at edge | Blur/resampling with reduced edge discontinuity |
| Wrap | Periodic coordinates | Textures and seamless patterns |
| Constant | Use declared RGBA/scalar value | Canvas extension and scientific processing |
| Transparent | Constant transparent black in the declared alpha convention | Compositing and rotations |
| Valid only | Produce only where the full footprint is available | Analysis/scientific pipelines |

## Recommended primitive/convenience split

Technical primitives:

- Affine/Projective Coordinate Transform
- Coordinate Field / Displacement
- Reformat/Canvas
- Sample Image
- Border Policy
- Reconstruction Filter
- Polar Conversion

Convenience nodes:

- Crop, Rotate, Scale, Perspective, Lens, Liquify, Ripple, Kaleidoscope, Pixelate, Panorama, Seam Carve.

The convenience node may lower to primitives, but its interface should bundle the parameters users expect.

## Stack-specific consequence

Stack needs an extent-aware image descriptor before true Crop, Canvas Resize, or Scale can exist. It also needs a first-class coordinate/vector field if user-built warps are a goal. Until then, geometry nodes remain special image shaders that cannot communicate changed resolution or coordinate meaning downstream.

## Primary sources

- [OpenCV geometric image transformations](https://docs.opencv.org/4.x/da/d54/group__imgproc__transform.html)
- [Mitchell and Netravali, Reconstruction Filters in Computer Graphics](https://doi.org/10.1145/378456.378514)
- [Duchon, Lanczos Filtering in One and Two Dimensions](https://doi.org/10.1175/1520-0450(1979)018%3C1016:LFIOAT%3E2.0.CO;2)
- [OpenImageIO ImageBuf and regions/windows](https://openimageio.readthedocs.io/en/latest/imagebuf.html)
- [OpenFX image-effect actions and regions of interest](https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectActions.html)
- [Avidan and Shamir, Seam Carving for Content-Aware Image Resizing](https://doi.org/10.1145/1275808.1276390)
