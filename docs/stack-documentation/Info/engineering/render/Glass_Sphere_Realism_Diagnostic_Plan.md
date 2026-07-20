# Glass Sphere Realism Diagnostic Plan

## 1. Best minimal diagnostic scene for this renderer

This scene needs to satisfy two constraints at the same time:

- it must be visible to the **current** integrator without relying on point-light-through-specular-caustic sampling,
- and it must make it obvious whether the “inner ring” is just refracted background structure or a fake concentric shell.

### Use this as the primary controlled scene

#### Objects

1. One **solid glass sphere**
   - center at world origin or wherever you want the camera to frame it directly
   - radius: keep it near your current default visual scale, roughly `0.75` world units
   - `thinWalled = false`
   - `Transmission Roughness = 0`
   - `IOR = 1.50` to `1.52`
   - `Absorption Distance = 2.0` or larger
   - `Aperture Radius = 0` for the camera during this test

2. One **large emissive white backdrop panel**
   - place it **behind** the sphere along the camera viewing direction
   - distance from sphere center: `5` to `8` sphere radii
   - size: at least `8` sphere diameters wide and `6` sphere diameters tall
   - this must fully cover the background behind the sphere in camera view

3. One **large black non-emissive adjacent panel**
   - same depth as the emissive panel
   - same height
   - directly touching the white panel so they form one clean **vertical seam**

#### Lighting / environment

- `Environment = off`
- `No point / spot / directional / area lights`
- The white backdrop should be **emissive**, not diffuse-lit

Reason:

- Your current integrator can see `camera -> sphere -> emissive backdrop` paths directly.
- It cannot efficiently show `camera -> sphere -> point light` paths because glass direct-light sampling is disabled in [PathTraceCommon.glsl:711](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:711>).

### Exact seam placement

Run two passes with the same setup.

#### Pass A: seam slightly off center

- Put the vertical seam about `0.15` to `0.25` sphere radii to the right of the sphere center in the **camera image**, not in world coordinates.
- This is the best first diagnostic because it avoids a perfectly symmetric image.

#### Pass B: seam through the sphere center

- Move the seam so it crosses the exact image center of the sphere.
- This is useful second, because a fake concentric artifact can hide inside symmetry.

### Camera placement guidance

- Frame the sphere so it occupies roughly `20%` to `35%` of the viewport height.
- Keep the camera at about `4` to `6` sphere radii away.
- Use `FOV = 35` to `50`.
- Keep the camera nearly head-on to the backdrop.
- Avoid depth of field for this test: `Aperture Radius = 0`.

Reason:

- You want the refracted mapping to be clean and easy to read.
- DOF and wide FOV both make this diagnosis noisier.

### Secondary scene variant

After the seam scene, run a second version with:

- the same sphere
- environment still off
- no lights
- a **checkerboard emissive backdrop**

Use a checkerboard with large squares first, not tiny ones.

Reason:

- The black/white seam is the strongest “is this ring tracking actual refracted background structure?” test.
- The checkerboard is the best second test for general lens distortion quality.

## 2. What result would support “physically normal” vs “artifact / bug”

### Result that supports “physically normal far-side refraction/background compression”

In the seam scene, a physically plausible solid glass sphere should mostly look like:

- a lens-distorted view of the white/black split,
- with a curved transition region inside the sphere,
- with stronger distortion near the edges,
- and with that transition curve **moving when the seam moves**.

The important test is this:

- if you move the backdrop seam left or right, the internal curved transition inside the sphere should move correspondingly.

That means the apparent “ring” is really tracking refracted background structure.

### Result that supports “fake inner-shell artifact / bug”

The result becomes suspicious if:

- a strong concentric inner ring stays centered even when the seam is moved,
- the sphere keeps looking like it contains a second smaller sphere regardless of backdrop pattern,
- the ring is much cleaner and more symmetric than the refracted seam itself,
- or the ring survives in places where the sphere should be showing mostly uniform white or uniform black from the backdrop.

That would indicate the feature is not being driven primarily by refracted scene content.

## 3. Is some internal curved boundary still expected in the new controlled backdrop test?

### Explicit answer

- **Yes.**
- Even with a physically correct implementation, I would still expect **some internal curved boundary / compressed region** in that controlled backdrop scene.

### But this is the important realism distinction

If the implementation is physically correct, the sphere should read mainly as:

- a distorted refractive lensing of the backdrop,
- not a clean concentric second sphere.

So:

- **some curved internal structure is expected**
- **a strong, nearly concentric, self-contained inner shell is not the realism target**

The new controlled backdrop test is valuable because it makes that distinction much easier to see.

### Practical expectation

In the seam scene, the most believable result is:

- the seam appears warped into a curved band,
- that band may resemble an internal arc,
- but it should feel like “background seen through a sphere,” not “another glass sphere nested inside.”

If the image still reads as a stable inner sphere rather than a refracted seam, the implementation remains suspicious.

## 4. Strongest code-side suspects still remaining

Ranked from most likely to least likely based on the code already inspected.

### 1. Suspicious dielectric throughput / event weighting

Most likely.

Relevant block:

- [PathTraceCommon.glsl:819-845](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:819>)
- [PathTraceCommon.glsl:848-895](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:848>)

Why it is still the top suspect:

- smooth reflection and smooth transmission both return heuristic weights like `max(baseSpectrum, vec4(0.25))`
- the transmission branch does not read like a proper dielectric delta BTDF weight
- there is no clear physically rigorous eta transport factor in the smooth path
- this can easily make the far-side exit interface look too clean or too strong

### 2. Display contrast exaggeration from the output path

Second most likely.

Relevant block:

- [PathTraceAccumulate.comp:24-26](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceAccumulate.comp:24>)

Why it matters:

- ACES tonemapping can make a physically normal transition look more graphic and higher-contrast than the underlying linear result
- it does not create the feature by itself, but it can make it look more like a fake shell

### 3. Fixed ray epsilon / ray origin offset

Third most likely.

Relevant block:

- [PathTraceCommon.glsl:288-290](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:288>)

Why it stays on the list:

- `0.0015` is a fixed world-space offset
- it is not scale-aware
- it can distort very close entry/exit behavior or bias edge transitions
- still less likely than the throughput weighting, but worth checking

### 4. Hidden geometry duplication / scene compiler / default scene issue

Low likelihood.

Relevant pieces already checked:

- one hero sphere in [RenderFoundationState.cpp:351-354](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Foundation/RenderFoundationState.cpp:351>)
- one resolved runtime sphere in [RenderSceneGeometry.cpp:28-34](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Runtime/Geometry/RenderSceneGeometry.cpp:28>)

Why it is low:

- I did not find evidence of a shell, duplicate sphere, or accidental concentric geometry path

### 5. Scene compiler / material preset issue around `thinWalled`

Very low likelihood.

Relevant blocks:

- [RenderMaterial.cpp:118-126](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Runtime/Materials/RenderMaterial.cpp:118>)
- [SceneCompiler.cpp:221-239](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Contracts/SceneCompiler.cpp:221>)

Why it is low:

- the default glass sphere is not being forced into `thinWalled` mode
- the payload carries `thinWalled` explicitly and defaults to false

### 6. Front-face / normal / eta entry-exit bug

Lowest of the major suspects.

Relevant blocks:

- [PathTraceCommon.glsl:327-358](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:327>)
- [PathTraceCommon.glsl:808-816](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:808>)

Why it is low:

- sphere `frontFace` computation is conventional
- outward normal is correct
- entry/exit eta choice for a solid sphere in air reads consistently

## 5. Top 3 debug overlays / tools to add first

Only the three most useful ones.

### Debug tool 1: one selected camera-ray bounce log

#### What to record

For one chosen viewport pixel:

- hit number
- `t`
- object/material ID
- `frontFace`
- `insideMedium`
- outward/geometric normal
- shading normal
- `etaI`
- `etaT`
- chosen eta ratio
- Fresnel
- reflect vs refract decision
- spawned ray origin
- spawned ray direction

#### What correct behavior looks like

For a center ray through the sphere:

- first sphere hit: `frontFace = true`, `insideMedium` changes `0 -> 1`
- second sphere hit: `frontFace = false`, `insideMedium` changes `1 -> 0`
- `etaI/etaT` swap correctly between entry and exit
- no repeated tiny-`t` self-hit loop on the same interface

#### What incorrect behavior looks like

- repeated hits at tiny `t` on the same sphere interface
- `frontFace` never flipping on exit
- `insideMedium` staying wrong
- `etaI/etaT` not swapping
- exit ray origin staying on the wrong side of the surface

### Debug tool 2: refracted-source classification overlay

#### What to draw

For pixels whose first hit is the glass sphere, color them by what the path sees **after leaving the sphere**:

- white panel hit = blue
- black panel hit = red
- miss / nothing = black
- reflection back to visible emissive source = yellow if you want an extra class

#### What correct behavior looks like

- the visible internal curved transition inside the sphere should line up with the classification boundary between “white panel seen” and “black panel seen”
- when you move the seam, that boundary should move

#### What incorrect behavior looks like

- a strong concentric inner ring appears even where the classification says the path is seeing a mostly uniform source
- the ring stays centered while the source-class boundary moves

This is the best direct separator between “real refracted backdrop structure” and “fake inner shell.”

### Debug tool 3: tiny-`t` self-hit / repeated-object heatmap

#### What to record or draw

Track:

- whether a bounce hit the same object again with `t < threshold`
- count consecutive hits on the same object
- display a heatmap over the image or a per-pixel counter

Use a threshold like `t < 0.005` or a similar scene-scale-relative value for the first pass.

#### What correct behavior looks like

- center transmission rays enter once, travel through the sphere interior, then exit with a reasonable non-tiny distance
- the heatmap should be mostly quiet except near genuinely grazing cases

#### What incorrect behavior looks like

- broad regions of the sphere show immediate near-zero-distance re-hits
- the inner ring aligns with repeated tiny-`t` events

That would point strongly at the ray offset / epsilon path.

## 6. Best exact next diagnostic scene to build

This is the one I would use first.

### Primary seam test

- Environment: off
- Lights: none
- Sphere:
  - solid glass
  - `Transmission Roughness = 0`
  - `IOR = 1.50` to `1.52`
  - `thinWalled = false`
- Background:
  - left panel = emissive white
  - right panel = diffuse black or non-emissive black
  - both large and coplanar
  - seam vertical
- Camera:
  - aperture off
  - medium focal length
  - sphere centered
- Run:
  - seam offset slightly right of center
  - then seam through center

### Secondary checker test

Same setup, but replace the seam panels with:

- one large emissive checkerboard panel

Use large squares first.

## 7. Will the ring survive in the controlled backdrop test?

### Explicit realism answer

- **Some internal curved structure should survive.**
- **A strong clean concentric “second sphere” should not be the dominant look.**

More explicitly:

- If the implementation is physically correct, the sphere should primarily look like a refractive lens warping the seam or checkerboard.
- You may still see a curved internal band or compressed region because that is how a solid sphere images the far side.
- But that band should be clearly explainable by the backdrop pattern.

If instead you still get:

- a centered, stable, almost self-contained inner sphere,
- with weak dependence on seam position or backdrop pattern,

then the result is still pointing to implementation error or exaggeration, not just normal optics.

## 8. Final implementation priority list

### First thing to test in-scene

Build the **black environment + solid glass sphere + emissive white/black seam backdrop** scene and move the seam between:

- slightly off-center
- dead center

### First thing to instrument in code

Add the **refracted-source classification overlay** first.

That is the fastest way to tell whether the visible inner feature is actually tracking refracted scene content.

### First thing to change in code if the test still looks wrong

Replace the current smooth-glass bounce weighting in [PathTraceCommon.glsl:819-845](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:819>) with a physically grounded dielectric delta event weight instead of the current heuristic `max(baseSpectrum, vec4(0.25))` path.
