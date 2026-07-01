# Human Workflow Notes

## Reading Intent

This is the canonical beginner-facing explanation for the RAW tab's four
foundational controls and their automatic helpers. Use this file when writing UI
labels, tooltips, status text, docs, or final answers to the user.

The implementation details live elsewhere. This note keeps the mental model
clear enough that a person can hold it in their head while editing.

## The Core Workflow

The practical editing loop is:

```text
Fit to see -> expose the scene -> fix regions -> shape contrast -> refit display
```

That sentence matters because the four controls do not all do the same kind of
work. Display Fit makes the current RAW readable on the screen. RAW Exposure
places the whole scene before later edits. Local Range fixes specific tone
zones or regions. Finish Tone shapes the global contrast relationship. Display
Fit can then be refreshed after the upstream edits have changed what it sees.

The UI can show Display Fit near the top because it helps the user see the file,
but the wording must make clear that it is a final display mapping, not a
captured-exposure edit.

## The Two Orders

There are two orders to keep separate.

The processing order is what the renderer does:

```text
RAW decode / WB / camera transform
-> RAW Exposure
-> Local Range
-> Finish Tone
-> Display Fit / View Transform
```

The teaching order is how a human should usually approach the controls:

```text
initial Display Fit
-> RAW Exposure
-> Local Range
-> Finish Tone
-> final Display Fit
```

The UI is allowed to teach in the second order as long as it never lies about
the first order.

## RAW Exposure

RAW Exposure is the global scene placement control. In STACK it writes
`preToneExposureEv`, and a +1 EV move doubles the scene-linear values before
Local Range, Finish Tone, and Display Fit receive the image.

Use RAW Exposure when the whole scene is placed too low or too high. Do not use
it to fix one dark subject if the sky or highlights are already near risk. That
is where Local Range or Display Fit should carry more of the result.

Beginner wording:

```text
RAW Exposure moves the whole scene before the rest of the RAW recipe.
```

## Local Range

Local Range is the local scene-EV correction graph. It should handle conflicts:
a face that needs lift while the sky should stay protected, shadows that need
opening without moving the whole file, or a bright region that needs restraint.

Automatic Local Range suggestions must become visible graph points, masks, or
ghost markers. If the user cannot see and edit the authored result in Local
Range, the automation is too hidden.

Beginner wording:

```text
Local Range changes selected tone zones or regions without moving the whole scene.
```

## Finish Tone

Finish Tone is the global tone relationship control. It shapes contrast and the
relationship between shadows, midtones, lights, and highlights after exposure
and local conflicts have been handled.

Finish Tone should not secretly rescue bad exposure placement, solve a local
subject problem, or behave like a second display transform. For automatic
starting points, Base mode should usually leave it neutral. Balanced mode can
apply a mild, visible tone graph only when the pre-display image is genuinely
flat.

Beginner wording:

```text
Finish Tone shapes the overall contrast after the scene and regions are placed.
```

## Display Fit / View Transform

Display Fit is the plain-language job. View Transform is the technical stage.
It maps the current scene-linear/tone-shaped image to the display. It owns
screen readability, display white and black bounds, shoulder, toe, and final
display contrast.

Display Fit is useful at the beginning as a viewing helper and at the end as a
final refit. It should not keep silently chasing every RAW Exposure, Local
Range, or Finish Tone drag, because that can make earlier controls feel like
they do nothing.

Beginner wording:

```text
Display Fit makes the current edit readable on the screen; it does not change the captured exposure.
```

## Automatic Actions

`Analyze` should gather evidence without changing the recipe.

`Fit Display` or `Refit Display` should update only the visible View Transform /
Display Fit controls.

`Build Starting Point` should choose a small set of visible manual values across
RAW Exposure, optional WB, optional Local Range, optional Finish Tone, and
Display Fit. It should feel like a balanced place to begin, not a finished edit.

`Undo` should restore the recipe snapshot from before the automatic action.

The most important user-facing promise is simple: after STACK applies an
automatic result, the user should be able to find the changed values in the same
manual sections they would have used by hand.

## Preferred Labels

| Concept | Preferred Label | Avoid |
| --- | --- | --- |
| View Transform action | `Fit Display` or `Refit Display` | Vague `Apply` |
| View Transform section | `Display Fit / View Transform` | `Auto Base` as the edited thing |
| Multi-control solver | `Build Starting Point` | `Auto Enhance` |
| Evidence panel | `Diagnostics` | `Image Analysis` as a main edit section |
| Image-derived WB | `Suggested WB` or `Neutral Estimate` | `Correct WB` |
| Local sampling tool | `Target` | Treating it as global auto |

## UI Explanation Rule

When explaining an automatic result, name the manual controls that changed:

```text
Starting point: RAW Exposure +0.35 EV, Local Range 2 points, mild Finish Tone,
Display Fit refreshed.
```

Do not say only:

```text
Auto applied.
```

That hides the ownership model the UI is trying to teach.

