# Classical Pre-Demosaic Foundation

Last updated: July 24, 2026.

## Plain-Language Goal

Clean the camera's individual red, green, and blue sensor measurements before
Stack invents the missing colors through demosaicing.

This pass strengthens Stack's existing fast, non-AI mosaic filter. It does not
add a neural model, replace demosaicing, or enable denoising automatically.

## Implemented Contract

The authored setting now distinguishes:

- `LegacyFixedThreshold`: the historical same-CFA bilateral-like behavior;
- `DngNoiseProfileV1`: the same spatial filter, but its range weights are
  measured in expected sensor-noise standard deviations.

For the DNG-aware mode, Stack:

1. resolves the DNG `NoiseProfile` pairs according to `CFAPlaneColor`;
2. evaluates normalized variance as `S * signal + O`;
3. propagates OpcodeList2 gain-map amplification as `gain squared * variance`;
4. uploads the per-sensor-pixel variance as a separate `R32F` texture;
5. compares same-CFA neighbors relative to their combined expected noise;
6. uses Edge Protection to tighten support from approximately three expected
   standard deviations to one;
7. leaves the existing authored green-plane and red/blue-plane strength mix in
   control of the final correction; and
8. preserves the historical hot-pixel classifier independently from the new
   range weighting.

The DNG specification describes `NoiseProfile` as a normalized shot/read model
for unprocessed linear RAW measurements. It explicitly states that the
standard deviation is the square root of `S * signal + O`, with signal in
`[0,1]`. Stack stores and propagates the variance, then takes the square root
only when comparing two samples:

- [Adobe DNG Specification 1.7.1.0](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf)

The stage remains before demosaic, matching the published domain of
darktable's classical RAW denoise module:

- [darktable raw denoise](https://darktable-org.github.io/dtdocs/en/module-reference/processing-modules/raw-denoise/)

## Compatibility

- New settings default to `DngNoiseProfileV1`, but denoise itself remains off.
- A serialized graph without the new mode field loads as
  `LegacyFixedThreshold`.
- DNG-aware mode is used only for a Truthful V1 Bayer mosaic with a complete,
  valid DNG noise profile.
- Missing or invalid noise metadata produces the historical fixed-threshold
  result. Stack does not silently treat missing coefficients as zero noise.
- `BaselineNoise` is not substituted as camera-specific truth in this pass.
- The current serialized `lumaStrength` and `chromaStrength` field names remain
  for project compatibility. The UI now identifies their actual meaning as
  Green Plane Strength and Red / Blue Plane Strength.
- The managed RAW recipe remains valid when an old, disabled denoise block
  carries the legacy mode.

## Interactive Preview

Stack's CFA-aware proxy area-averages same-plane sensor samples. Independent
noise variance falls as samples are averaged, so the proxy scales both DNG
coefficients by its image-area ratio. This is a deterministic global estimate:
fractional edge footprints cannot be represented exactly by one DNG
coefficient pair.

The settled full-resolution render uses the original DNG coefficients.

## Checklist

### Completed

- [x] Keep the existing filter as an explicit non-AI classical baseline.
- [x] Add a versioned DNG noise-profile mode without changing old saved pixels.
- [x] Map one-pair and per-plane DNG profiles to actual CFA colors.
- [x] Evaluate shot/read variance in normalized pre-white-balance sensor space.
- [x] Propagate supported gain-map amplification into the variance map.
- [x] Make edge weighting adapt to expected per-pixel noise.
- [x] Preserve hot-pixel classification across legacy and DNG modes.
- [x] Correct misleading luminance/chroma labels in the technical controls.
- [x] Scale profile variance for the area-averaged interactive RAW proxy.
- [x] Add CPU tests for profile mapping, variance math, range weighting,
  gain-map scaling, and missing-profile fallback.
- [x] Add graph tests for mode round-trip, old-graph migration, and proxy
  coefficient scaling.
- [x] Add a live OpenGL test that executes the variance-texture shader path,
  verifies that it changes the noisy mosaic, and verifies that the hot-pixel
  mask is identical between modes.
- [x] Run the complete real-DNG Develop/RAW workspace smoke path with the new
  mode present, using `IMG_260608_204838.dng`.
- [x] Add an experimental RAW Lab CFA Denoise tool before Exposure, backed by a
  schema-9 recipe block and the same processing path.
- [x] Include the denoise mode in RAW placement cache identity so DNG/Fixed
  comparisons cannot reuse stale pixels.

### Still Required Before Calling `Fast` Validated

- [ ] Define a real noisy/clean or repeated-capture fixture set with at least
  two Bayer layouts and multiple ISO levels.
- [ ] Measure flat-field noise reduction and edge/detail retention at 100%.
- [ ] Run real-DNG A/B comparisons for no denoise, legacy, and DNG-aware modes.
- [ ] Tune the sigma-support mapping from measured results rather than one
  synthetic fixture.
- [ ] Decide whether two iterations are worth their detail loss and cost. The
  current shader re-evaluates the center while reading original neighbors; it
  is not a separately materialized second denoise pass.
- [ ] Add an explicit estimated/manual noise model for sources without a valid
  DNG `NoiseProfile`; do not use `BaselineNoise` without a documented
  conversion and tests.
- [ ] Tune and promote the experimental RAW Lab `Fast` interaction surface
  only after the processing gates above pass.
- [ ] Profile the variance-map CPU build/upload and cache behavior on
  full-resolution real images.

## Verification

Passed on July 24, 2026:

```text
StackRawEvidenceTests
StackGraphBehaviorTests
Stack.exe --validate-develop-node-smoke
Stack.exe --validate-develop-real-raw-smoke IMG_260608_204838.dng
```

`build.cmd` completed successfully. The real-DNG smoke is an end-to-end
pipeline sanity check; it does not replace the still-open real-image A/B and
measurement work above.

## Current Limit

This remains a small same-CFA bilateral-like filter. DNG metadata makes its
decisions better grounded, but does not turn it into a state-of-the-art
denoiser. The DNG model also assumes white, spatially independent shot/read
noise and does not describe every fixed-pattern, thermal, or defective-pixel
artifact.
