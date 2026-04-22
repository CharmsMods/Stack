# Glass Sphere Isolation Test Follow-Up

## Summary

Your interpretation is correct.

The result

- `glass sphere only + no environment + no lights = black`
- `glass sphere only + point light + no environment + no other objects = still black`

is **expected in the current integrator**, and it is **mostly inconclusive** for judging whether the earlier inner-ring effect is physically correct or a bug.

This test does **not** kill the earlier “background compression / far-side exit interface” explanation. It mainly proves that your current renderer does not have an efficient path to show a dielectric sphere under only a point light when there is no environment, no emissive geometry, and no diffuse receiver.

## 1. Code paths inspected for this specific question

- [Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:15>)
  - miss handling
  - emissive-hit handling
  - glass bounce continuation
- [Stack/src/RenderTab/Shaders/PathTraceCommon.glsl](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:696>)
  - `SampleDirectLight(...)`
  - explicit early return for glass
  - point-light contribution path
- [Stack/src/RenderTab/Shaders/PathTraceCommon.glsl](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:274>)
  - environment contribution on miss
- [Stack/src/RenderTab/Shaders/PathTraceGenerate.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceGenerate.comp:37>)
  - primary ray state initialization
- [Stack/src/RenderTab/Shaders/PathTraceShadow.comp](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadow.comp:14>)
  - shadow-ray-only light visibility path

## 2. Why the point-light-only sphere is black in this renderer

### A. Misses only see the environment

When a ray misses scene geometry, the only radiance added is environment radiance:

- [PathTraceShadeSurface.comp:16-20](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:16>)

```glsl
if (!HitStateIsValid(hit)) {
    rayState.radiance += rayState.throughput * EvaluateEnvironmentSpectrum(...);
    rayState.direction.w = 0.0;
    ...
}
```

If the environment is disabled, a miss contributes zero:

- [PathTraceCommon.glsl:274-276](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:274>)

```glsl
if (uEnvironment.params.x < 0.5 || uEnvironment.params.y <= kEpsilon) {
    return vec4(0.0);
}
```

So in your isolation scene, any camera ray that goes through the glass sphere and then leaves the scene contributes **black**.

### B. Point lights are not emissive geometry

There is no code path where a point light is intersected as a visible scene object and directly contributes on hit.

The only direct light path for point/spot/area/directional lights is `SampleDirectLight(...)`, which samples a light and creates a shadow ray:

- [PathTraceCommon.glsl:696-785](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:696>)

That means a point light can contribute only through explicit direct-light sampling, not by being “seen” like emissive geometry.

### C. Glass explicitly skips direct-light sampling

This is the key line:

- [PathTraceCommon.glsl:711-713](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:711>)

```glsl
if (uLightCount <= 0 || IsGlass(material)) {
    return false;
}
```

So at a glass hit:

- the renderer does **not** sample the point light,
- does **not** create a shadow ray to the point light,
- and therefore does **not** connect camera -> glass -> point-light paths via NEE.

### D. With no environment and no emissive backdrop, the glass has nothing to show

The glass sphere becomes visible in this renderer mainly through:

1. refracted/reflected environment,
2. refracted/reflected emissive geometry,
3. or indirect visibility through a lit scene that eventually contributes radiance.

Your point-light-only isolation scene has:

- no environment,
- no emissive geometry,
- no diffuse receiver,
- and glass direct-light sampling is disabled.

So black is the expected result.

## 3. Is this test valid for judging whether the inner ring is physically real?

### Short answer

- **No, not by itself.**
- It is mostly **inconclusive** for the inner-ring question.

### Why

This test is removing exactly the kinds of signals that the current integrator is capable of showing through glass:

- background/environment seen through refraction,
- emissive surfaces seen through refraction,
- diffuse receivers that can reveal transmitted/reflected structure.

So the fact that the glass sphere disappears under a point-light-only setup does **not** mean the earlier inner feature was fake.

It mainly means:

- your current path tracer cannot efficiently render a dielectric sphere under only a point light in empty space,
- especially because specular transmission is not explicitly connected to point lights.

## 4. Does the black result weaken the earlier “background compression / far-side interface” theory?

### Answer

- **Not really.**

It does **not** materially weaken that theory, because the earlier theory was based on a scene where the glass sphere had something visible to refract:

- a bright ground plane,
- a sky/ground environment split,
- high-contrast background structure.

That is exactly the class of signal your current renderer *can* show through glass:

- camera ray
- enters sphere
- exits sphere
- then either hits background geometry or misses to environment
- and accumulates that radiance

That path is directly represented in your code.

By contrast, `point light only + empty scene` removes that signal and replaces it with a lighting configuration that your current sampling strategy does not connect well, or at all.

So the earlier explanation is still alive.

## 5. Should a glass sphere with only a point light and no other objects be visible at all in this renderer?

### Direct answer

- **No, not in any reliable or expected way.**

With current code, I would expect it to be effectively black.

### Why, very specifically

1. The sphere is not emissive.
2. A point light is not intersectable geometry here.
3. On a glass hit, `SampleDirectLight(...)` returns false immediately because `IsGlass(material)` is true:
   - [PathTraceCommon.glsl:711-713](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:711>)
4. If the camera path exits the sphere and misses everything else, it only sees the environment:
   - [PathTraceShadeSurface.comp:16-20](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceShadeSurface.comp:16>)
5. With environment off, that miss contributes zero:
   - [PathTraceCommon.glsl:274-276](</E:/WEBSITE/CharmsLink/CharmsLink/Charms Web Tools/Image Tools/Noise Studio/Modular Studio V2/Stack/src/RenderTab/Shaders/PathTraceCommon.glsl:274>)

So yes: the black result is a normal consequence of the current integrator.

## 6. What this means for the earlier audit

The earlier audit conclusion still stands:

- the sphere is not obviously being treated as a hollow shell,
- the entry/exit eta logic is not obviously backwards,
- the visible inner feature in the original lit scene can still be explained as a real far-side refracted interface/background compression effect,
- but your glass throughput / event weighting is still suspicious and may be exaggerating that effect.

This new isolation test does **not** refute that.

## 7. Best next diagnostic scene

You need a scene that:

1. your renderer can actually sample well,
2. does not depend on point-light-through-specular-caustic connections,
3. gives the sphere a controlled high-contrast background to refract.

### Best next scene

Use this exact minimal scene:

- `Environment`: disabled
- `Lights`: none
- `Objects`:
  - one **solid glass sphere**
  - one **large bright emissive backdrop panel** behind the sphere
  - one **large black non-emissive panel** immediately adjacent to it, forming a sharp vertical seam

### Why this is the best next test

It makes the sphere visible via **direct camera -> glass -> backdrop** paths, which your renderer *can* sample.

It avoids the point-light/caustic problem entirely.

It gives you a controlled discontinuity behind the sphere:

- if the internal-looking feature is mostly real far-side refraction/background compression, it should clearly track the backdrop seam and warp it,
- if there is a fake concentric inner-shell artifact, you may still see a strong concentric boundary that does not correspond cleanly to the seam geometry.

### Practical version with your current primitive set

Since the tab already supports planes/cubes/material presets, the easiest version is:

- one sphere in front,
- two large background cubes or planes behind it,
  - left object: emissive white
  - right object: diffuse black

Keep the camera centered on the sphere so the seam runs through the refracted image.

## 8. Exact answers to your direct questions

### 1. Should I expect to see a glass sphere with only a point light and no other objects in this renderer?

- **No.**
- In current code, I would expect it to be effectively black.

### 2. Does this black result weaken the “background/environment exaggeration” theory, or not really?

- **Not really.**
- The black result is explained by sampling limitations, not by disproving the earlier background-refraction explanation.

### 3. What exact minimal test scene should I build next?

Build this:

- glass sphere
- black environment
- no point light
- no other lights
- one white emissive backdrop panel behind the sphere
- one black diffuse panel adjacent to it behind the sphere

This is the most controlled next scene that your current renderer can actually show.

### 4. What exact debug overlays/logging should I add next?

Add these next, in this order:

1. **One selected-pixel ray log**
   - hit count
   - material ID / type
   - `frontFace`
   - `insideMedium`
   - `etaI`
   - `etaT`
   - chosen eta ratio
   - Fresnel
   - reflect vs refract decision
   - spawned origin
   - spawned direction

2. **Radiance source classification overlay**
   - color a pixel by where its final contribution came from:
     - blue = environment miss
     - orange = emissive surface hit
     - green = direct-light sample
     - red = no contribution / black

3. **Glass-direct-light-skip counter**
   - count how many times `SampleDirectLight(...)` early-returned because `IsGlass(material)` was true
   - this will directly verify why `sphere + point light only` is black

4. **frontFace / insideMedium overlay**
   - visualize entry hits vs exit hits
   - confirm center rays through the sphere are doing one entry / one exit in the expected order

## Conclusion

### Most important conclusion

The `sphere + point light only = black` result is **expected in your current renderer** and is **not a strong test** of whether the inner ring is physically real.

### What it proves

It proves your current integrator does not efficiently connect dielectric specular transmission to point lights in empty space.

### What it does not prove

It does **not** prove the earlier inner-ring effect was fake.
It does **not** prove the glass sphere is fully correct.
It does **not** kill the earlier “far-side exit surface + background compression” explanation.

### Best next move

Use a **sphere + emissive high-contrast backdrop** scene next, not a point-light-only empty-space test.
