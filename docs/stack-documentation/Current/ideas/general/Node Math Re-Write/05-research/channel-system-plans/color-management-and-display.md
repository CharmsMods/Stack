# Color Management And Display — Research Plan

> **Research plan:** This file defines questions and deliverables; it does not
> contain completed findings. New evidence may propose a recorded design
> revision, but it does not silently change accepted direction.

- Started: 2026-07-17
- Refocused: 2026-07-20
- Authority: `../../02-channel-based-design/accepted-product-direction.md`
- Status: product boundary accepted; technical and workflow research required

## Fixed Product Boundary

Research must preserve explicit graph math, per-value color state, Unknown
sources, no mandatory hidden working space, visible source/output compounds,
and export pixels controlled by the authored graph. It may propose a
non-exporting monitor-profile presentation boundary only through an explicit
revision of NMR-105.

## Research Questions

- Explain primaries, white point, transfer, reference, ICC/cICP, sRGB,
  Display-P3, linear light, and tone mapping in plain language.
- Distinguish Image-value state from monitor/output-device state.
- Compare source, document/working, view, proof, monitor, and export boundaries
  in professional tools.
- Define known-sRGB and Unknown-source behavior.
- Identify which named edits require linear light or another declared domain.
- Separate tone/range mapping, gamut mapping, output encoding, and monitor
  presentation in the current View Transform replacement.
- Define a smallest professional SDR workflow that can later support HDR/PQ/HLG
  and wide gamut.

## Preferred Sources

Use ICC, IEC/ITU/SMPTE material where accessible, OpenColorIO documentation,
official professional-application documentation, and GPU API documentation for
implementation boundaries. Technical questions must rely on primary sources.

## Deliverables

- plain-language glossary;
- source/edit/view/output pipeline diagram;
- standard-photo and scene-linear template comparison;
- View Transform replacement contract;
- monitor-presentation authority recommendation; and
- numerical and visual test plan for the first SDR slice.
