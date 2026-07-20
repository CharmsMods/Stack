# Signed Divide Correction

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: fix
- Topic: data-math-divide
- Verification: verified against current shader and current audit

## Defect

The current Data Math Divide shader uses:

```glsl
result = a / max(abs(b), vec4(0.00001));
```

This avoids a zero denominator but discards the sign of every negative
denominator. For example, `1 / -2` produces `+0.5` instead of `-0.5`.

The production location is
`src/Renderer/Internal/RenderPipelinePrograms.cpp` in the Data Math program.
The dated audit, research package, and `implementation-progress.md` already
record the problem; the latter currently lists Divide correction outside the
authorized Phase 6 scope.

## Intended Correction

Retain a documented epsilon guard while preserving denominator sign per
component:

```text
safeDenominator =
    denominator < 0
        ? -max(abs(denominator), epsilon)
        :  max(abs(denominator), epsilon)

result = numerator / safeDenominator
```

Exact zero uses positive epsilon unless a later numerical-policy decision
selects IEEE infinity/NaN or another named zero mode. This preserves the
existing guarded-zero intent while correcting negative finite values and
negative near-zero values.

## Required Implementation Slice

- Activate a small pixel-changing correction in `implementation-progress.md`.
- Update the canonical Divide formula/definition identity according to the
  forward versioning rules. A pixel-changing correction must not masquerade as
  the same exact promise.
- Correct the production shader.
- Add CPU reference cases and GPU/readback comparisons where the current test
  harness permits.
- Cover positive, negative, positive near-zero, negative near-zero, zero, and
  componentwise RGBA inputs.
- Cover a scalar-field path because scalar values are broadcast/read through
  the same Data Math implementation.
- Confirm that Average Images, which divides by a positive image count, remains
  unchanged.
- Run the focused reference/validation tests and the preferred Windows build.
- Record formula, epsilon, version, tolerance, and evidence in a completion
  note.

## Not Part Of This Fix

- General NaN/Inf scrubbing.
- Adding Divide to pointwise fusion.
- Choosing a project-wide numerical safety policy.
- Changing alpha participation defaults.
- Renaming Data Math nodes or value types.

## Status

Ready for explicit activation. No source code was changed during this intake.

## Related Docs

- `../../implementation-progress.md`
- `../../STACK_IMAGE_MATH_AND_PIPELINE_AUDIT as of 7-12-26.md`
- `../../Stack_Image_Math_Research_Package/14_Composition_and_Validation_Rules.md`
