# Naming And Industry Terms — Research Plan

> **Research plan:** This file defines questions and deliverables; it does not
> contain completed findings. New evidence may propose a recorded design
> revision, but it does not silently change accepted direction.

- Started: 2026-07-17
- Refocused: 2026-07-20
- Researched: 2026-07-21
- Adopted: 2026-07-22
- Authority: `../../02-channel-based-design/accepted-product-direction.md`
- Status: complete; R2 closed
- Findings: `../channel-system-findings/2026-07-21-naming-and-industry-terms.md`

## Decided Language

Normal UI teaches **Value**, **Channel**, **Image**, **Data**, and
**Specialized**. Mask and Alpha are Channel roles. `Scalar`, `uniform`, and
`field` remain internal or advanced terms. Research should focus on the
remaining detailed language; materially contrary evidence may propose an
explicit recorded revision rather than silently changing these choices.

## Research Scope Addressed

The findings report addresses the wording that affects contracts or detailed
UI:

- whether **Channel Set** is the clearest advanced name for an arbitrary
  non-display bundle;
- how professional tools distinguish component origin, semantic purpose,
  units/range, and preview interpretation;
- compact text for partial Images and missing components;
- accessible pin/wire wording for `Value/Channel`, Mask, and role assignment;
- terminology for a reusable spatial Constant Channel; and
- whether Vector and Coordinate subtype labels need domain-specific aliases.

## Sources

Use official documentation and specifications from compositing, grading,
material/shader, computer-vision, MaterialX, and OpenUSD ecosystems. Research
should compare meaning, not copy one application's vocabulary wholesale.

## Deliverable

A short table mapping technical term, Stack's friendly label, exact meaning,
ambiguity risk, and example pin/wire text. Any proposed wording change returns
to `../../02-channel-based-design/accepted-product-direction.md` as an explicit decision.

## Result

Keep Value, Channel, Image, Data, Specialized, Channel Set, Constant Channel,
Component, Channel Role, View As, Vector, and purpose-specific Coordinate
aliases. Use `Image · R, B`, `Present channels: R, B`, and `Missing G` for
partial-Image presentation.

User-facing pins and accessible text say `Value or Channel`; technical
shorthand may remain `Value/Channel` where compact notation is useful.
