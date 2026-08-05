# Channel-First Frequency Contract v1

- Decision: NMR-141
- Status: implemented Phase 7A contract
- Activated: 2026-07-26
- Completed: 2026-07-26

## Boundary

This contract owns the minimum public Channel distinction required for
frequency work and the complete first channel-first frequency family. It does
not implement partial Images, general Value/Channel broadcast, source
dissolution, or unrelated Phase 7 nodes.

## Exact Public Values

- `Channel` is one real value per pixel with a preserved semantic role.
- `ComplexSpectrum` is an RG32F complex grid plus source/padding metadata.
- `FrequencyResponse` is a resolution-independent real response in `[0,1]`.
- `SpectrumMagnitude` is raw nonnegative amplitude.
- `SpectrumPhase` is raw radians in `[-pi, pi]`.
- Spectrum, response, magnitude, and phase are Specialized values, not Images,
  Channels, or Masks.

Connections require exact logical/socket types. Channel Split and Channel
Combine use Channel sockets. No Image-to-Channel, Image-to-Spectrum,
Mask-to-Response, Spectrum-to-Image, or implicit luminance conversion exists.

## Definitions

1. Frequency Filter: required Channel, optional Response, optional exposed
   Strength Value; emits Filtered Channel.
2. Frequency Response: emits Response; owns All Pass, Low Pass, High Pass,
   Band Pass, Band Stop, and symmetric multi-notch rejection.
3. Fourier Transform: Channel to Spectrum.
4. Inverse Fourier Transform: Spectrum to Channel.
5. Spectrum View: Spectrum to display Image.
6. Apply Frequency Response: Spectrum and Response to Spectrum, with optional
   Strength Value.
7. Combine Spectra: compatible Spectrum A/B to Spectrum; Add or Subtract.
8. Separate Spectrum: Spectrum to Magnitude and Phase.
9. Recombine Spectrum: compatible Magnitude and Phase to Spectrum.
10. Spectrum Analyzer: Spectrum to radial-power Data, band-power Value,
    peak-frequency Value, and peak-direction Value.

Legacy Frequency Mask, Spectrum Math, and modal Magnitude/Phase definitions are
not visible and are never silently resolved as these definitions.

## Math And Metadata

- Forward storage is canonical unshifted RG32F, unscaled.
- Dimensions pad symmetrically to the next power of two. The descriptor stores
  source/padded extents and padding origin.
- Edge policy is Mirror by default; Wrap and Zero Pad are explicit.
- Inverse applies `1/(paddedWidth*paddedHeight)`, requires a real-compatible
  Hermitian spectrum, crops by stored padding origin, and preserves Channel
  role. It never clamps.
- Low-pass prototypes use a half-response cutoff. Smooth uses a raised-cosine
  transition, Gaussian uses `exp(-ln(2)*(r/c)^2)`, Butterworth uses
  `1/(1+(r/c)^(2n))`, and Hard is a step. High pass is the complement; band
  pass is high-at-low times low-at-high; band stop is its complement.
- Each notch is evaluated at both `+frequency/direction` and its conjugate
  partner. Multiple notch responses multiply.
- Apply uses `coefficient * mix(1,response,clamp(strength,0,1))`.
- Separate/Recombine never applies log, gamma, exposure, palette, or DC shift.
- Spectrum View owns all display mapping and centering.
- Analyzer power is `real^2+imaginary^2`; DC is excluded. Radial Power has 256
  bins; Band Power is selected non-DC power divided by total non-DC power;
  Peak Frequency and Direction report the strongest selected conjugate pair.

## UI, Persistence, And Failure

Frequency Filter and Frequency Response expose an exact numeric editor plus a
centered preview with radial/notch handles. Preview work is bounded to 512
pixels and never changes graph results. Definition parameters declared
graph-input-capable may be explicitly exposed as persistent Value sockets; a
connection overrides the stored fallback.

All definitions, parameter ports, notch IDs, settings, and specialized
descriptors are versioned and fingerprinted. Incompatible extent, role,
normalization, component lineage, missing input, or non-Hermitian inverse
requests fail at the owning node with a stable repair message.

## Acceptance

CPU reference cases, graph connection/persistence tests, GPU forward/inverse
and response/component tests, analyzer cases, preview/handle and undo tests,
registry validation, and the preferred Windows `build.cmd` must pass. Human
native visual review remains recorded separately when it cannot be automated.
