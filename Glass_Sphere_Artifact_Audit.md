# Glass Sphere Artifact Audit

## 1. Files/functions inspected

### Primary glass / transport path

- [Stack/src/RenderTab/Shaders/PathTraceCommon.glsl](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:327>)
  - `UpdateSphereHit(...)`
  - `FresnelDielectric(...)`
  - `RefractDirection(...)`
  - `RefractThroughThinSheet(...)`
  - `EvaluateAbsorption(...)`
  - `SampleDirectLight(...)`
  - `SampleSurfaceBounce(...)`
  - `OffsetRayOrigin(...)`
- [Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:30>)
  - hit unpacking
  - front-face usage
  - Beer-Lambert application
  - throughput update
  - bounce-ray spawn
- [Stack/src/RenderTab/Shaders/PathTraceIntersect.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceIntersect.comp:1>)
- [Stack/src/RenderTab/Shaders/PathTraceShadow.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadow.comp:14>)
- [Stack/src/RenderTab/Shaders/PathTraceGenerate.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceGenerate.comp:37>)
- [Stack/src/RenderTab/Shaders/PathTraceAccumulate.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceAccumulate.comp:12>)

### Scene/material setup

- [Stack/src/RenderTab/Foundation/RenderFoundationState.cpp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Foundation/RenderFoundationState.cpp:12>)
  - default glass material
  - default hero sphere scene
- [Stack/src/RenderTab/Runtime/Geometry/RenderSceneGeometry.cpp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Runtime/Geometry/RenderSceneGeometry.cpp:28>)
  - resolved sphere geometry
- [Stack/src/RenderTab/Runtime/Materials/RenderMaterial.cpp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Runtime/Materials/RenderMaterial.cpp:98>)
  - surface preset defaults / `thinWalled`
- [Stack/src/RenderTab/Contracts/SceneCompiler.cpp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Contracts/SceneCompiler.cpp:221>)
  - GPU material payload, `thinWalled`, `ior`, `transmissionRoughness`

## 2. Findings by subsystem

### A. Sphere hit / normal correctness

#### What the code does

- Sphere intersection uses the standard half-`b` quadratic form:
  - `oc = rayOrigin - center`
  - `b = dot(oc, rayDirection)`
  - `c = dot(oc, oc) - r^2`
  - `discriminant = b*b - c`
  - roots `-b +/- sqrt(discriminant)`
  - [PathTraceCommon.glsl:327-358](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:327>)
- The outward normal is computed as:
  - `normal = normalize(hitPosition - center)`
  - [PathTraceCommon.glsl:349-353](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:349>)
- `frontFace` is stored using the conventional test:
  - `dot(rayDirection, normal) < 0.0`
  - [PathTraceCommon.glsl:352](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:352>)
- For spheres, `hit1.xyz` and `hit2.xyz` both store that same geometric/outward normal:
  - [PathTraceCommon.glsl:352-353](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:352>)

#### Assessment

- The sphere intersection math itself is correct.
- The nearest-root selection is mostly correct.
- The outward normal is normalized correctly.
- `frontFace = dot(ray.direction, outward_normal) < 0` is correct and conventional.
- I do **not** see a sphere-specific normal flip bug.
- I do **not** see a later accidental re-flip for spheres.

#### Minor issue

- This condition is slightly sloppy:
  - `if (t <= kEpsilon && bestHit.hit0.x > 0.0) return;`
  - [PathTraceCommon.glsl:339-345](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:339>)
- If both roots are behind the origin and `bestHit` is still invalid, the function can still write a negative-`t` hit record.
- That does **not** become a valid shading hit because `HitStateIsValid(hit)` requires `hit.hit0.x > 0.0`:
  - [PathTraceCommon.glsl:323-324](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:323>)
- So this is real cleanup debt, but it is **not** the most likely cause of the inner-ring artifact.

### B. Dielectric entry/exit eta logic

#### What the code does

- In the glass path, the surface normal used for refraction is:
  - `surfaceNormal = frontFace ? geometricNormal : -geometricNormal`
  - [PathTraceCommon.glsl:808-810](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:808>)
- For solid glass:
  - entering uses `etaI = 1.0`, `etaT = eta`
  - exiting uses `etaI = eta`, `etaT = 1.0`
  - [PathTraceCommon.glsl:813-816](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:813>)
- `RefractDirection(...)` uses:
  - `cosThetaI = dot(-incident, normal)`
  - `sin2ThetaT = eta * eta * sin2ThetaI`
  - `refracted = normalize(eta * incident + (eta * cosThetaI - cosThetaT) * normal)`
  - [PathTraceCommon.glsl:558-567](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:558>)

#### Assessment

- For a **solid sphere in air**, the code is **not** obviously using the wrong entry/exit ratio.
- Entry is treated as air -> glass.
- Exit is treated as glass -> air.
- The normal orientation used for refraction is consistent with that convention.
- I do **not** see evidence that the solid sphere is being treated like a hollow shell **unless** `thinWalled` is explicitly enabled.

#### Thin-wall behavior

- `thinWalled` is a distinct code path, not the default:
  - GPU payload sets it explicitly from material state: [SceneCompiler.cpp:231-234](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Contracts/SceneCompiler.cpp:231>)
  - the default glass preset does **not** set `thinWalled = true`: [RenderMaterial.cpp:118-126](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Runtime/Materials/RenderMaterial.cpp:118>)
- The default hero sphere is just one sphere primitive:
  - [RenderFoundationState.cpp:351-354](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Foundation/RenderFoundationState.cpp:351>)
- The resolved runtime geometry is also a single sphere:
  - [RenderSceneGeometry.cpp:28-34](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Runtime/Geometry/RenderSceneGeometry.cpp:28>)

### C. Fresnel / reflection / transmission split

#### What the code does

- Full dielectric Fresnel is used, not Schlick:
  - [PathTraceCommon.glsl:530-547](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:530>)
- Smooth glass branches probabilistically by Fresnel:
  - compute `fresnel`
  - reflect if `randomFloat(rngState) < fresnel`
  - otherwise refract
  - [PathTraceCommon.glsl:819-845](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:819>)
- Rough glass does the same with a GGX-like branch:
  - [PathTraceCommon.glsl:848-895](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:848>)

#### Assessment

- Fresnel itself is implemented correctly enough for this audit.
- The **suspicious part is not the Fresnel calculation**.
- The suspicious part is the **throughput weighting after the branch**.

#### Major suspicious logic

- Smooth glass reflection returns:
  - `return max(baseSpectrum, vec4(0.25));`
  - [PathTraceCommon.glsl:822-825](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:822>)
- Smooth glass transmission also returns:
  - `return max(baseSpectrum, vec4(0.25));`
  - [PathTraceCommon.glsl:841-845](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:841>)
- There is no explicit Fresnel factor in the returned throughput.
- There is no explicit transmission Jacobian / eta transport factor in the returned throughput.
- The rough glass path is also heavily heuristic:
  - reflection/transmission weights are clamped ad hoc
  - [PathTraceCommon.glsl:865-895](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:865>)

#### Why this matters

- A probabilistic reflect/refract branch can still be unbiased **if** the returned weight matches the sampled event correctly.
- This implementation is not clearly doing that.
- In particular, the smooth transmission path is effectively saying: “if refraction is chosen, carry almost full base throughput”.
- That can make the far-side exit surface / refracted background look too clean and too strong.
- This is one of the strongest code-level reasons the inner ring may look more pronounced than expected.

### D. Self-intersection / epsilon

#### What the code does

- Ray offset uses:
  - `position + geometricNormal * (0.0015 * sign(dot(direction, geometricNormal)))`
  - [PathTraceCommon.glsl:288-290](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:288>)
- New bounce rays are spawned with that function here:
  - [PathTraceShadeSurface.comp:104-105](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:104>)
- Shadow rays also use the same offset:
  - [PathTraceCommon.glsl:782-783](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:782>)

#### Assessment

- The **sign** of the offset is reasonable.
  - reflection outside the sphere offsets outward
  - transmission into the sphere offsets inward
  - exit transmission offsets outward
- I do **not** see evidence that refracted rays are systematically being offset the wrong way.
- I do **not** think the sphere is re-hitting the same interface immediately due to an obvious sign error.

#### Secondary concern

- `0.0015` is a fixed world-space epsilon.
- That is not robust across scene scale.
- It could contribute to subtle thickness distortion or edge bias.
- It is **not** my top explanation for the clean inner-ring artifact in this specific scene.

### E. Geometry / thin-wall / hollow behavior

#### What the code does

- The hero sphere is one primitive:
  - [RenderFoundationState.cpp:351-354](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Foundation/RenderFoundationState.cpp:351>)
- Default glass material:
  - transmission `= 1.0`
  - `ior = 1.52`
  - `absorptionDistance = 2.0`
  - no default `thinWalled = true`
  - [RenderFoundationState.cpp:27-32](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Foundation/RenderFoundationState.cpp:27>)
- Glass preset in runtime material code does not force thin-walled mode:
  - [RenderMaterial.cpp:118-126](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Runtime/Materials/RenderMaterial.cpp:118>)

#### Assessment

- I do **not** find evidence that the solid sphere is accidentally implemented as:
  - two concentric spheres
  - a shell
  - a duplicated instance
  - a default thin-wall object
- On current code, your solid sphere is being treated as a **single solid sphere**, not a hollow shell, unless the material checkbox is explicitly changed.

### F. Throughput / BSDF / integrator correctness

#### What the code does

- Beer-Lambert absorption is applied only while `insideMedium > 0`:
  - [PathTraceShadeSurface.comp:37-42](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:37>)
- `insideMedium` is toggled by the glass bounce code:
  - entry sets `insideMedium = 1`
  - exit sets `insideMedium = 0`
  - [PathTraceCommon.glsl:841-844](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:841>)
  - [PathTraceCommon.glsl:878-882](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:878>)
- Glass skips direct-light sampling entirely:
  - `if (uLightCount <= 0 || IsGlass(material)) return false;`
  - [PathTraceCommon.glsl:711-713](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:711>)
- Display uses ACES tonemapping after averaging:
  - [PathTraceAccumulate.comp:24-26](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceAccumulate.comp:24>)

#### Assessment

- Beer absorption handling is simple but directionally consistent.
- I do **not** see evidence of double-accumulating both reflection and refraction on the same event.
- I **do** see biased or at least physically under-specified throughput on glass bounces.
- I **do** see the integrator making the far-side exit surface more visually prominent than a real photo can, due to a combination of:
  - perfectly smooth glass
  - very simple bright plane / sky-ground split
  - ACES tonemapping
  - glass throughput that is not physically rigorous

### G. Environment / ground-plane contribution

#### What the code does

- The environment is not a neutral black background.
- It has a split sky / horizon / ground model:
  - [PathTraceCommon.glsl:274-285](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:274>)
- The default scene also places the sphere over a bright ground plane:
  - [RenderFoundationState.cpp:346-354](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Foundation/RenderFoundationState.cpp:346>)

#### Assessment

- Yes, the environment/ground setup **absolutely can** produce a real, sharp-looking internal boundary in a solid glass sphere.
- A glass ball over a bright plane with a horizon transition often shows:
  - a compressed/inverted background patch
  - a strong far-side refracted interface
  - strong edge distortion and TIR regions
- So part of what you are seeing is physically normal.

## 3. Likely artifact explanation

This artifact is **not best explained by the sphere being hollow**. The code path says it is a solid sphere.

The strongest interpretation from the code is:

1. There is a **physically normal far-side refracted interface/background compression effect** because the scene is exactly the kind of setup that makes it visible:
   - smooth glass sphere
   - bright ground plane
   - sky/ground environment split

2. That normal effect is then likely being **over-emphasized by the current glass transport implementation**, especially:
   - smooth-glass throughput returning nearly full base throughput for both reflection and transmission
   - lack of a more rigorous dielectric BTDF weighting / eta transport factor
   - heuristic rough-glass weighting for the rough path
   - ACES display tonemapping increasing contrast

So the answer is **combination**, ranked like this:

1. **Physically plausible effect exaggerated by scene/background and current transport weighting**
2. **Glass BSDF / throughput implementation is suspicious and likely amplifies the artifact**
3. **Not a hollow-shell geometry issue**
4. **Not primarily a front-face / normal flip issue**
5. **Not primarily a ray-offset issue**

## 4. What is physically expected in a real solid glass sphere

These are normal and should not be treated as bugs:

- Seeing an apparent “inner” boundary that is really the refracted image of the far-side exit surface.
- Seeing strong distortion and compression near the edges.
- Seeing the background flip or invert depending on camera/background setup.
- Seeing the effect get weaker as `IOR` approaches `1.0`.

These are **not automatically suspicious**:

- A bright circular region inside the sphere when the ground plane is bright.
- A sharp boundary tied to the horizon or ground/sky split.
- Stronger edge distortion than center distortion.

What would be suspicious is:

- the effect staying unnaturally clean under very neutral backgrounds
- the effect looking like a perfect nested sphere regardless of background
- dramatic brightness/contrast that does not track Fresnel or scene lighting plausibly

## 5. Bugs or suspicious logic found

Ranked most likely to least likely.

### 1. Glass throughput / BSDF weighting is under-specified and likely biased

Most suspicious block:

- [PathTraceCommon.glsl:819-845](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:819>)
- [PathTraceCommon.glsl:848-895](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:848>)

Why it is suspicious:

- Smooth reflection and smooth transmission both return `max(baseSpectrum, vec4(0.25))`.
- That is not a rigorous dielectric delta BSDF/BTDF weight.
- The rough branch uses heuristic clamps instead of a proper microfacet transmission model with the correct Jacobian and eta terms.
- This is the most likely place where a physically normal exit-surface image is getting over-strengthened.

### 2. The scene/background strongly favors a visible far-side exit image

Relevant code:

- [PathTraceCommon.glsl:274-285](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:274>)
- [RenderFoundationState.cpp:346-354](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Foundation/RenderFoundationState.cpp:346>)

Why it matters:

- You are not testing against a neutral studio void.
- You have a bright plane plus sky-ground split, which is one of the easiest ways to make the exit interface look like a bright inner sphere.

### 3. ACES tonemapping can increase the perceived contrast of the inner ring

Relevant code:

- [PathTraceAccumulate.comp:24-26](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceAccumulate.comp:24>)

Why it matters:

- This is not the root cause, but it can make the internal boundary look cleaner and more “graphic” than the underlying linear HDR image.

### 4. Fixed epsilon is not ideal, but not my primary suspect here

Relevant code:

- [PathTraceCommon.glsl:288-290](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:288>)

Why it matters:

- It is a real robustness issue.
- It can bias very thin geometry or extreme closeups.
- It does not read like the main explanation for this artifact.

### 5. Minor sphere-hit cleanup issue

Relevant code:

- [PathTraceCommon.glsl:339-345](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:339>)

Why it matters:

- The code should reject `t <= kEpsilon` cleanly even when `bestHit` is invalid.
- It is not the likely reason for the clean inner refracted ring.

## 6. Exact fixes to try first

### First fix: replace the smooth-glass throughput with a proper dielectric delta weight

Target:

- [PathTraceCommon.glsl:819-845](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:819>)

What to change first:

- Keep the Fresnel-based branch decision.
- Replace the returned `max(baseSpectrum, vec4(0.25))` with physically derived event weights.
- For the transmission branch, include the correct refractive transport factor for your transport convention instead of a constant base color carry-through.
- Remove the hard floor from the smooth-glass return path.

Reason:

- This is the most likely code-level exaggeration of the artifact.

### Second fix: add a controlled debug mode for a single camera ray

Instrument:

- ray generation in [PathTraceGenerate.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceGenerate.comp:37>)
- bounce handling in [PathTraceShadeSurface.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:30>)

Capture per hit:

- hit number
- `t`
- `front_face`
- outward normal
- shading normal
- `eta_i`
- `eta_t`
- chosen eta ratio
- Fresnel
- reflect vs refract choice
- spawned origin
- spawned direction

Reason:

- This will let you confirm the sphere is entering once and exiting once, with the expected eta swap.

### Third fix: test the artifact against a black or neutral environment

No code change needed first.

Reason:

- This cleanly separates “real far-side refraction” from “transport bug exaggeration”.

### Fourth fix: make the ray epsilon scale-aware

Target:

- [PathTraceCommon.glsl:288-290](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:288>)

What to change:

- Replace the hardcoded `0.0015` with a scale-aware offset based on hit distance and/or object size.

Reason:

- Not the first fix, but worth doing for robustness.

### Fifth fix: clean up the smooth-hit root rejection

Target:

- [PathTraceCommon.glsl:339-345](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:339>)

What to change:

- Reject `t <= kEpsilon` unconditionally after trying both roots.

Reason:

- Correctness cleanup.

## 7. Short diagnostic experiments

### High-value scene tests

1. Solid glass sphere with **black background**, no ground plane.
   - If the “inner sphere” mostly disappears, the current strong effect is heavily scene/background-driven.

2. Solid glass sphere with a **checkerboard environment** and no ground plane.
   - This helps distinguish normal far-side refraction from a fake shell look.

3. IOR sweep from `1.0` to `1.7`.
   - Expected: refraction and internal-looking boundary strengthen smoothly with IOR.
   - Suspicious: abrupt or qualitatively wrong transitions.

4. Compare **thin-walled sphere** vs **solid sphere**.
   - They should look clearly different.
   - If they look almost identical, your interior/exterior logic is suspect.

5. Increase/decrease epsilon by 10x.
   - If the ring shape changes strongly, your origin offset is contributing more than expected.

### Debug visualizations

6. Front-face visualization.
   - Color entry hits green, exit hits red.
   - A center ray through the sphere should usually show one entry then one exit.

7. Normal visualization.
   - Show geometric normal in RGB.
   - Confirm it points outward everywhere on the sphere.

8. Single-ray trace through sphere center.
   - Expect:
     - hit 1: `front_face = true`, `eta_i = 1`, `eta_t = ior`
     - hit 2: `front_face = false`, `eta_i = ior`, `eta_t = 1`

9. Single-ray trace near the sphere edge.
   - This is where Fresnel/TIR behavior will reveal eta/normal mistakes immediately.

## Requested direct answers

### Whether the solid glass sphere is being treated like a hollow shell by mistake

- **No, not from the code I inspected.**
- The default scene is one sphere, and default glass is not thin-walled.

### Whether you are using the wrong eta ratio on entry or exit

- **Not obviously.**
- Entry/exit eta selection for the smooth solid-glass path looks consistent with air -> glass and glass -> air.

### Whether normals are flipped incorrectly anywhere

- **For spheres, I do not see an obvious flip bug.**
- Sphere normals are outward, normalized, and `frontFace` is computed conventionally.

### Whether `front_face` is computed correctly and used consistently

- **Yes, for the sphere path it looks correct and consistently used.**

### Whether refracted rays may be immediately re-hitting the same surface because of bad origin offsets

- **Not obviously.**
- The offset sign looks correct.
- The fixed epsilon is still a robustness weakness, but not my main suspect here.

### Whether transmission/reflection is being double-counted or weighted incorrectly

- **I do not see both branches being accumulated simultaneously.**
- **I do see suspicious weighting of the chosen branch**, especially for glass throughput.

### Whether the integrator is making the far-side exit surface unnaturally visible

- **Yes, plausibly.**
- The scene setup already makes the effect visible, and the current glass weighting likely amplifies it.

### Whether environment/ground-plane setup alone could cause this

- **Yes, absolutely.**
- In this specific scene, it is very plausible that a large part of the visible internal boundary is real far-side refraction/background compression.
- But I do **not** think that is the whole story; the glass transport implementation looks too heuristic to trust fully.

## Conclusion

### Most likely root cause

- **Combination of a physically normal far-side refracted interface effect plus a glass BSDF/integrator implementation that likely exaggerates it.**

### Secondary possible causes

- ACES tonemapping increasing perceived contrast.
- Fixed ray epsilon causing subtle boundary bias.
- Minor sphere-hit cleanup issue, though low probability as the visible cause.

### Things that are physically normal and should NOT be treated as bugs

- Seeing an internal-looking far-side exit boundary in a solid glass sphere.
- Strong background compression / inversion.
- Stronger distortion near edges.
- The effect getting weaker as IOR approaches `1.0`.

### Exact code changes I should make first

1. Replace the smooth-glass bounce weight in [PathTraceCommon.glsl:819-845](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:819>) with a proper dielectric delta event weight instead of `max(baseSpectrum, vec4(0.25))`.
2. Add a one-ray debug trace path in [PathTraceGenerate.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceGenerate.comp:37>) and [PathTraceShadeSurface.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:30>) to log entry/exit eta and normals.
3. Re-test the same sphere with a black environment and no ground plane before touching geometry logic.
4. Make `OffsetRayOrigin(...)` scale-aware in [PathTraceCommon.glsl:288-290](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:288>).
5. Clean up the `t <= kEpsilon` rejection path in [PathTraceCommon.glsl:339-345](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:339>).

## Suggested temporary debug mode

Add a very small debug mode with these pieces:

- A `uDebugPixel` uniform in [PathTraceGenerate.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceGenerate.comp:3>) and [PathTraceShadeSurface.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:3>).
- A tiny SSBO ring buffer with one struct per bounce:
  - `hitIndex`
  - `t`
  - `frontFace`
  - `geometricNormal`
  - `shadingNormal`
  - `etaI`
  - `etaT`
  - `etaRatio`
  - `fresnel`
  - `didReflect`
  - `spawnedOrigin`
  - `spawnedDirection`
- Only write the buffer when `rayIndex == debugPixelIndex`.
- Read the SSBO back on CPU after the frame and show it in a small ImGui window in the Render tab.

That debug path is the fastest way to answer, with certainty, whether the ray is entering and exiting the sphere correctly or whether the transport math is exaggerating a physically normal far-side image.
