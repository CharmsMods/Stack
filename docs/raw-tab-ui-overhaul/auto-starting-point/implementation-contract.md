# RAW Starting Point Implementation Contract

## Reading Intent

This is the compact implementation contract for the RAW tab automatic starting
point work. Read it before changing code that touches Auto Base, Display Fit,
RAW Exposure suggestions, Local Range suggestions, Finish Tone authoring, RAW
analysis, or render-stage readbacks.

The longer research files explain why these rules exist. This file says what a
future implementation must preserve.

For normalized score formulas, validation logging, and the recommended staged
implementation sequence, read `implementation-pass-readiness.md` after this
file.

For active pass state, read `implementation-progress.md` before changing code.
That file is the compact handoff ledger for what has been completed, what is
allowed next, and what must not be skipped.

This contract applies across the whole multi-update implementation, not only to
the first code pass. Conservative early passes are allowed to implement a subset
of the system, but they must leave the architecture pointed toward staged
candidate solving, visible manual writes, and validation-driven expansion.

## Non-Negotiable Rules

Automatic processing must write visible manual controls. It may compute private
evidence, but it may not create a hidden output pass that changes the preview
without editable recipe values.

User ownership wins. Once the user edits a value directly, automatic systems
must stop rewriting that value unless the user explicitly asks for a new solve
or refit.

Final display evidence is not proof that upstream controls are correct. View
Transform can make a poor upstream placement look readable, so candidate
scoring must include pre-display or staged metrics.

Display Fit / View Transform should not continuously compensate while the user
drags RAW Exposure, Local Range, or Finish Tone. Prefer stale/refit states over
silent live refitting.

All automatic actions need an undo or revert snapshot.

Every implementation pass must update `implementation-progress.md` before work
starts and after work finishes or blocks. The update should be minimal: active
pass, files touched, behavior changed, verification, docs updated, next allowed
work, and what not to do next.

## Control Ownership

| Control | Recipe Field | Owns | Automation Policy |
| --- | --- | --- | --- |
| RAW Exposure | `preToneExposureEv` | Global scene-linear placement | Suggested or Starting Point authored; limited by raw headroom. |
| White Balance | WB recipe mode/multipliers | Capture illuminant / neutral baseline | Prefer camera/as-shot; image-derived WB is usually a suggestion. |
| Local Range | `localRange` graph and masks | Regional or tone-zone conflicts | Suggestions must become visible graph points, masks, or ghost markers. |
| Finish Tone | `finishTone.layerJson` | Global tonal relationship | Base neutral; Balanced may author mild visible points. |
| Display Fit / View Transform | `viewTransform.layerJson` | Scene-to-display mapping | Safe initial fit/refit; mark stale after upstream edits. |

If a correction does not clearly belong to one of these controls, it should
remain diagnostic until the UI exposes an editable control for it.

## Evidence Ownership

The solver should keep three evidence layers mentally separate.

| Evidence Layer | Use For | Do Not Use For |
| --- | --- | --- |
| Raw safety ledger | Black/white levels, clipping, headroom, hot/dead pixels, noise risk, as-shot metadata. | Tone curve shape, sky masks, subject brightness, display readability. |
| Scene appearance ledger | Luma percentiles, log-average key, subject/center brightness, image-derived WB, Local Range targets, Finish Tone strength, View Transform input fit. | Sensor clipping truth after tone/display have changed values. |
| Final display evidence | Display clipping, screen readability, preview brightness, final sanity check. | Proving RAW Exposure, Local Range, or Finish Tone were well placed. |

The most important implementation mistake to avoid is sampling only the final
displayed image and then deciding every upstream value from that sample.

## Starting Point Candidate Flow

A bounded one-click solve should follow this shape:

```text
1. Build raw safety ledger from raw buffer and metadata.
2. Render neutral scene-linear baseline.
3. Choose WB policy and RAW Exposure candidate.
4. Render raw-placement candidate.
5. Add Local Range only for unresolved regional conflicts.
6. Add mild Finish Tone only if pre-display tone is flat.
7. Fit View Transform from the selected upstream candidate.
8. Score staged metrics and final display.
9. Apply selected visible recipe values.
```

This may be implemented with a small candidate set instead of a single straight
line. The candidate set should stay bounded: current fit, Base, Balanced, and
possibly Farther. It should not become an open-ended loop that keeps changing
controls until the final histogram looks good.

## Stage Readbacks Needed

The current RAW workspace analysis is mainly pre-View-Transform, which is good
for Fit Display but too narrow for full Build Starting Point behavior. Future
implementation should add named readbacks or equivalent candidate render
outputs for:

| Stage | Purpose |
| --- | --- |
| Raw technical | Sensor facts, clip/headroom, metadata safety. |
| Neutral scene | Initial scene key and color after minimal conversion. |
| Raw placement | Check RAW Exposure/WB before local/tone/display. |
| Local candidate | Check regional corrections before global tone/display. |
| Pre-display tone | Check Finish Tone before View Transform can hide issues. |
| Final display | Check screen readability and display clipping. |

Debug views are useful for inspection but should not be treated as the stable
API for solver evidence unless they are promoted into named analysis stages.

## Recompute And Ownership Timing

Live while dragging:

```text
preview render
visible values
graph redraw
target drag feedback
simple state badges
```

After settle:

```text
full preview
current-frame stats
recommendations
highlight/noise diagnostics
stale Display Fit state
```

On explicit request:

```text
Analyze
Fit Display / Refit Display
Build Starting Point
Apply individual suggestion
Undo automatic action
```

Expensive recommendation building and multi-control solves should run after a
settled render or explicit action, not on every slider tick.

## Staged Implementation Bias

The safest early implementation sequence is conservative:

1. Keep current Fit Display behavior as View Transform-only.
2. Add the Starting Point data model separately from Auto Base.
3. Add two stage metrics first: pre-View-Transform and final display.
4. Add neutral/raw-placement stage capture before applying multi-control solves.
5. Ship Base mode before making Balanced the default.

This keeps the feature understandable while the stage evidence and constants
are being validated against real RAW images. It is a sequencing rule, not the
scope limit of the feature. Later passes should continue into dry-run
candidates, visible Base application, conservative Balanced Local Range,
Finish Tone authoring where editable, and validation tuning.

## Forbidden Shortcuts

Do not write an invisible auto result texture.

Do not apply Local Range or Finish Tone changes that the compact RAW tab cannot
show or bridge to full controls.

Do not let final display success erase raw clipping, highlight headroom, or
pre-display tone problems.

Do not present image-derived WB as truth. It is a neutral estimate unless the
evidence is unusually strong and metadata is missing.

Do not label a View Transform-only action as if it performed full automatic RAW
development.
