# One-Click Starting Point Alignment

Date: July 8, 2026.

## Purpose

This note records the product correction from native RAW tab review: the current
code has important Starting Point plumbing, but the visible workflow is not yet
the desired single-click base experience.

Use this file before changing the RAW Starting Point UI, planner breadth,
button layout, or diagnostics wording.

## Current Discrepancy

The docs previously used "one-click" too loosely. They meant that a
`Build Starting Point` action exists and can apply visible recipe values from
one button. In the running app before the first Pass 10 UI correction, however,
the Starting Point panel exposed multiple automatic actions as peer workflow
buttons:

```text
Analyze
Fit / Refit Display
Undo
Build Starting Point
Add Local Range
Add Mild Tone
Diagnostics
```

That meant the user experience was a multi-action assist panel. It was not the
target beginner-facing behavior described by the user: one click, processing,
then a base recipe applied across the visible RAW controls when safe.

The first Pass 10 UI correction changed presentation, not solver behavior:
`Build Starting Point` is now the primary full-width action; Analyze, Fit/Refit
Display, Undo, and Diagnostics remain secondary; and Add Local Range /
Add Mild Tone live behind an Advanced disclosure. The remaining implementation
should still be treated as scaffolding where it has not been validated against
real RAW images:

- useful planner, diagnostics, undo, and queueing infrastructure;
- not an accepted final interaction model;
- not enough evidence for constant tuning or validation-readiness claims.

## Current Code Truth

`src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp` renders the current
Starting Point controls. `Build Starting Point` is the primary action, calls
the conservative planner, and can queue initial analysis or a post-edit Display
Fit. The same panel still keeps `Add Local Range` and `Add Mild Tone` as
separate optional automatic actions, but they are now advanced scaffolding
rather than peer base-workflow buttons.

`src/Raw/RawAutoStartPoint.cpp` contains the conservative plan builder. It can
apply:

- RAW Exposure when the recommendation is safe and auto-applyable;
- at most one strict Local Range point when confidence is at least 0.85 and the
  EV delta is at most 0.60;
- mild Finish Tone when the proposal is visible, low strength, and highlight
  safe;
- Display Fit after upstream edits settle.

That is close to the desired engine shape. The first UI correction now points
the panel at the desired one-click path, but the implementation still needs
editor-state tests, richer staged evidence, real-image validation, and a more
precise interaction/solver spec before it should be treated as finished.

## Target Product Shape

The target first-run RAW base flow should be:

```text
User clicks Build Starting Point
-> Stack analyzes or reuses current evidence
-> Stack renders bounded candidate stages
-> Stack applies safe visible recipe values
-> Stack summarizes applied and withheld controls
-> User edits the normal visible controls from there
```

The primary visible action should be `Build Starting Point`. `Analyze` and
`Diagnostics` may remain available for inspection. `Undo` should appear when an
automatic snapshot exists. `Fit / Refit Display` can remain a manual display
tool, but it should not feel required before or after the base solve. Separate
`Add Local Range` and `Add Mild Tone` buttons should not be the main path to a
normal base result; they should either be folded into `Build Starting Point`,
moved behind diagnostics/advanced affordances, or removed after the primary
solve is reliable.

One click does not mean a hidden output pass. The result must still write to
visible controls:

```text
RAW Exposure -> preToneExposureEv
Local Range  -> localRange graph/mask fields
Finish Tone  -> finishTone.layerJson visible graph points
Display Fit  -> viewTransform.layerJson
WB           -> visible WB fields only when policy allows
```

## Solver Direction

The one-click solve should keep the existing control ownership model:

```text
raw safety evidence constrains exposure
neutral/raw-placement evidence chooses global scene placement
local-candidate evidence decides whether a graph point is warranted
pre-display tone evidence decides whether mild Finish Tone is warranted
final display evidence checks readability after upstream choices
```

If evidence is missing, the button should still behave as one user action. It
can apply only the safe subset, queue the missing analysis, or withhold a
control, but the status text must say what happened. It should not ask the user
to manually discover that separate Local Range or Mild Tone buttons are needed
to complete the base.

## Research Anchors

Adobe Camera Raw's `Auto` is a single action that analyzes the image and writes
editable tone controls, while also allowing separate per-slider auto actions:

```text
https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html
```

darktable's exposure module supports the RAW Exposure policy: automatic exposure
uses histogram analysis to shift a selected percentile toward a target EV
relative to camera white:

```text
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/exposure/
```

darktable's tone equalizer supports the Local Range policy: EV-zone local
adjustments need mask quality and edge preservation, not a blind global
histogram correction:

```text
https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/
```

The MIT-Adobe FiveK paper is the caution against simple one-size-fits-all
histogram rules. It shows that photographic adjustment depends on scene content,
spatial tone distribution, clipping, and human preference:

```text
https://people.csail.mit.edu/vladb/photoadjust/db_imageadjust.pdf
```

## Next Documentation Pass

After the first UI hierarchy correction, write or extend the solver spec with:

- the exact primary button states: idle, analyzing, rendering candidates,
  applying, applied, partially applied, unavailable, canceled;
- which existing secondary buttons remain visible, where they live, and why;
- the candidate set for Base and Balanced;
- the conditions under which Local Range and Finish Tone are applied by the
  primary click;
- the real RAW validation categories and review labels;
- the editor-state tests for queued analysis, queued post-fit, source change,
  recipe change, cancellation, and undo availability.

The next implementation should then deepen the actual one-click solve instead
of tuning constants or broadening behavior before staged evidence and tests are
ready.
