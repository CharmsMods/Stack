# Interaction Design Evidence

Status: focused evidence pass complete.

## Direct Manipulation

Ben Shneiderman's direct-manipulation model emphasizes:

- continuously visible objects of interest;
- physical actions or labeled controls instead of complex syntax; and
- rapid, incremental, reversible changes whose result is immediately visible.

For Stack, the graphs and preview are the objects of interest. A Local Exposure
point should be moved directly; a curve should be shaped directly; a view
anchor should ideally be adjusted on a mapping visualization. Sliders remain
useful where no honest direct representation exists, but should not become the
default merely because they are easy to implement.

This principle also depends on the preview-performance work. A graph that
updates late or unpredictably is not fully direct manipulation even if the
pointer moves a control point.

## Progressive Disclosure Without Accordions

Progressive disclosure means showing the primary path first and making expert
features clearly reachable at a secondary level. It does not require stacked
expand/collapse sections.

Stack can use:

- tool-family switching through a rail;
- a named active workspace;
- a small overflow action for low-frequency commands;
- a temporary advanced sheet or side drawer;
- contextual controls that appear when a graph point, target, or mask is
  selected; and
- a dedicated Setup/Inspect tool family for technical controls.

The critical requirement is information scent: users must be able to predict
where secondary controls live. One consistent secondary mechanism is better
than a mixture of disclosure triangles, hidden right-click menus, double-click
secrets, and unrelated dropdowns.

## Icon Navigation

Icon-only navigation is compact but ambiguous. Recognizing a drawing and
interpreting the action it represents are different problems. Professional
tools such as Resolve mitigate this by:

- keeping icon order stable;
- grouping related tools;
- showing a tooltip;
- naming the active palette in the content region; and
- using distinct selected state.

Recommended Stack policy:

1. Use icons for a small, stable set of tool families.
2. Show the active tool's plain-language name once at the top of its surface.
3. Supply immediate tooltips and accessible names.
4. Use a dot or tint to show changed state.
5. Provide a visible keyboard-focus treatment.
6. Consider an optional expanded rail with labels for learning and
   accessibility.
7. Do not encode numeric parameters or unfamiliar pipeline concepts as icons.

## Bare Visuals Versus Pointer Targets

Removing a visible button container does not require shrinking the interactive
target to the painted icon pixels. W3C's WCAG 2.2 guidance uses 24 by 24 CSS
pixels as the minimum pointer target unless spacing or another exception
applies. Material's touch guidance is larger.

For Stack's desktop interface:

- keep icon artwork visually bare;
- give it at least a 24-pixel invisible pointer box;
- space adjacent targets so invisible boxes do not overlap;
- reveal hover/active state through tint, a small underline/dot, or the active
  tool name rather than a filled rectangle; and
- preserve a visible keyboard-focus ring or underline even when normal borders
  are absent.

This is a usability boundary, not an argument for restoring visible button
containers.

## Grouping Without Boxes

Perceptual grouping can be achieved through proximity, alignment, similarity,
and consistent placement. Research on proximity and common-region grouping
also explains why boxes are powerful: enclosing controls strongly groups them.
The consequence is not “never use a boundary”; it is “do not spend the
strongest grouping cue everywhere.”

Stack should order grouping cues from quietest to strongest:

1. proximity and whitespace;
2. shared alignment and repeated dimensions;
3. text style or subtle tint;
4. background-value shift or gradient;
5. a line;
6. a full enclosing frame.

Most everyday RAW controls should stop at levels 1–3. A line or frame should be
reserved for real semantic or safety separation, not routine decoration.

## Sources

- [Ben Shneiderman publications and “Direct Manipulation: A Step Beyond Programming Languages”](https://www.cs.umd.edu/~ben/publications.html)
- [Progressive disclosure overview and Nielsen-derived guidelines](https://www.interaction-design.org/literature/book/the-glossary-of-human-computer-interaction/progressive-disclosure)
- [Apple Human Interface Guidelines: Toolbars](https://developer.apple.com/design/human-interface-guidelines/toolbars)
- [Material Design: Tooltips](https://m1.material.io/components/tooltips.html)
- [W3C: Understanding target size minimum](https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum)
- [Palmer: Common region as a perceptual-grouping principle](https://www.sciencedirect.com/science/article/pii/001002859290014S)
- [Review of Gestalt perceptual grouping and figure-ground organization](https://pmc.ncbi.nlm.nih.gov/articles/PMC3482144/)
