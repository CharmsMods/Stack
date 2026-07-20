# Color Management Workflow Research

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: research
- Topic: color-management
- Verification: unverified; research not yet performed

## Research Goal

Design a professional but understandable source-to-edit-to-view-to-export
workflow that preserves Stack's flexibility and makes quality-oriented defaults
easy. Determine which operations belong in the authored graph, which may be
visible source/output compounds, and whether monitor presentation is a separate
non-destructive application boundary.

## Questions

- What do primaries, white point, transfer function, scene/display reference,
  ICC, cICP, sRGB, Display-P3, linear light, and tone mapping each mean in plain
  language?
- Which facts describe the numeric image value, and which describe the monitor
  or output device?
- How do established image editors distinguish document/working space, source
  profile, view transform, proof transform, monitor profile, and export
  encoding?
- Can Stack retain “no hidden repair” while offering visible automatic source
  and output compounds?
- What is the best default for known sRGB photographs? What is the best default
  for untagged sources?
- Which operations have a meaningful linear-light contract, and which are
  commonly performed in nonlinear or perceptual domains?
- Should the current View Transform be split into tone mapping, gamut mapping,
  transfer encoding, and monitor presentation?
- How can an SDR-first design remain compatible with future HDR/PQ/HLG and
  wide-gamut workflows?

## Candidate Sources For A Later Research Pass

- ICC specifications and official ICC educational material.
- IEC/ITU/SMPTE publications for sRGB, SDR/HDR transfers, and colorimetry where
  accessible.
- OpenColorIO documentation and architecture.
- Official documentation for established professional image, compositing, and
  grading applications.
- GPU API documentation for sRGB texture/framebuffer conversion, used only to
  understand implementation boundaries rather than define the product.

## Deliverables

- Plain-language glossary.
- Source/edit/view/output pipeline diagrams.
- Comparison of explicit manual, visible auto-authored, template, and implicit
  workflows.
- A proposed replacement contract for View Transform.
- A smallest professional SDR vertical slice with testable numerical and visual
  behavior.

