# Color Management And Display Pipeline

- Started: 2026-07-17
- Consolidated: 2026-07-20
- Authority: `../accepted-product-direction.md`
- Verification: current Technical Image, source metadata, View Transform, viewport, and PNG contracts inspected on 2026-07-20

This file owns the source/edit/output/display boundaries. It does not define
the detailed color science; that remains R1 research.

## Why Metadata Matters

A shader can calculate `a + b` without knowing a color space. Metadata matters
when a node promises physical, perceptual, or output meaning:

- generic Multiply is numeric;
- Exposure EV conventionally doubles linear-light RGB per stop;
- saturation and luminance require a declared channel relationship and method;
- a color conversion needs source and destination identities; and
- display/export need declared encoding and interpretation.

The math is not morally right or wrong. A diagnostic reports when the promise
of a named operation does not match its input state. Stack still permits
deliberate generic experimentation where execution is structurally valid.

## Accepted Source And Edit Boundary

- Color identity, transfer, reference, and alpha state travel with Image values
  or remain Unknown.
- Known embedded metadata is retained without changing pixels.
- Untagged input remains Unknown; Stack does not guess sRGB.
- **Assign** changes descriptor meaning only.
- **Decode**, **Convert**, **Encode**, tone mapping, and gamut mapping are
  explicit pixel operations.
- There is no mandatory hidden working color space.
- Named operations declare whether color state is agnostic, informative, or
  structurally required.

Templates may author visible, compact source-preparation and output-finishing
compounds. Diagnostics may offer one-click insertion of explicit nodes. Neither
path silently mutates an existing graph.

## Output Stages

The current View Transform performs tone/range and gamut-oriented work but does
not complete a general output-encoding or monitor-profile pipeline. Its name is
therefore broader than its actual behavior.

The accepted direction is to separate:

1. scene/display range or tone mapping;
2. gamut mapping when declared;
3. output transfer encoding; and
4. monitor-profile presentation.

A friendly compound may expose the first three as one inspectable output
workflow while preserving exact internal stages.

## Monitor Presentation And NMR-105

The authored graph owns image pixels and export encoding. Accurate monitor
presentation may require a non-exporting conversion from the declared graph
output to the actual display profile. That is not a creative look or hidden
graph repair, but it would refine the current direct-viewport rule.

No monitor conversion is adopted as an implementation contract yet. R1 must
define the boundary, profiles, failure behavior, bypass/inspection behavior,
and tests. Any change to NMR-105 must be explicit in the parent decision
register before code changes.

## Smallest Professional Workflow To Research

R1 should deliver:

- a plain-language glossary for primaries, white point, transfer, reference,
  ICC/cICP, sRGB, Display-P3, linear light, and tone mapping;
- known-sRGB and Unknown source workflows;
- a standard-photo template and a scene-linear template;
- exact domain promises for Exposure, Brightness, Contrast, and Saturation;
- a replacement contract for View Transform;
- a color-managed SDR display/export vertical slice; and
- a path that does not preclude HDR/PQ/HLG and wide gamut.

## Current Code Boundary

Stack currently preserves selected PNG/JPEG metadata, supports explicit sRGB
and Display-P3 assignment/conversion nodes, directly displays the connected
graph output, and exports PNG without hidden conversion. General ICC transforms,
complete output encoding after View Transform, and monitor-profile presentation
are not implemented as one professional pipeline.

## Related Authority

- `../../07-history/decisions/confirmed-rewrite-direction-2026-07-15.md`
- `../../01-start-here/decision-log.md` — NMR-018, NMR-102, NMR-105, NMR-128, NMR-129
- `../../03-technical-contracts/program-goals-and-rules.md`
- `../../05-research/channel-system-plans/color-management-and-display.md`
