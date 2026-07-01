# RAW Tab UI Validation Checklist

## Purpose

Use this checklist after each implementation pass that changes RAW tab layout or interaction. It is focused on UI behavior and clarity, not the Auto Base algorithm math.

## Build And Basic Safety

- App builds.
- Existing RAW workspace opens.
- No crash when no RAW workspace is open.
- No crash when switching selected RAW sources.
- Existing project recipes still load.
- New/default RAW recipes still load.
- Read-only/error states still render.

## Layout Widths

Check these viewport conditions:

- narrow app window
- normal desktop window
- wide desktop window
- right gallery expanded
- right gallery collapsed
- bottom filmstrip mode

Confirm:

- controls panel width is usable
- preview remains visible and useful
- graphs do not collapse below useful height
- gallery collapses or yields space before the editor becomes unusable
- popouts do not open off-screen
- no text overlaps buttons or graph surfaces

## Compact RAW Top Area

Check:

- long RAW filename
- long workspace path
- new project
- existing project
- embedded/read-only state if available
- saving/loading state if available
- no suggestions
- multiple suggestions
- warning/advisory state

Confirm:

- filename is ellipsized cleanly
- badges fit without wrapping badly
- Save is visible only when useful
- More menu contains structural project actions
- critical errors remain visible

## Auto Base / Readiness

Check:

- pending analysis
- Auto Base view fit applied
- existing recipe where Auto Base should not auto-apply
- user-edited View Transform
- unavailable/fallback analysis

Confirm:

- RAW Exposure unchanged is visible when relevant
- Auto Base does not imply it applied Local Range/WB/denoise when it did not
- Apply/Revert/Analyze remain reachable
- long rationale has moved to details/Diagnostics

## Base Light

Check:

- RAW Exposure drag
- Refit Display
- View Transform manual edit
- Display Fit auto-owned state
- Display Fit manual/locked state
- Needs Refit state after settled analysis if implemented

Confirm:

- Display Fit does not continuously refit during RAW Exposure drag
- user can understand RAW Exposure versus Display Fit
- advanced View Transform controls remain reachable

## Local Range

Check:

- Local Range off/default
- Local Range enabled
- targeting started/stopped
- target sample adopted
- color target off
- color target on
- region mask active
- legacy local exposure recipe if available

Confirm:

- graph remains dominant
- target row shows EV/delta/color when relevant
- swatch is visible without noisy RGB text
- numeric scene-linear values are in tooltip/details
- overlay/view labels are `Affected`, `Delta`, and `Mask`

## Finish Tone

Check:

- graph default
- channel/mode controls
- log domain controls
- reset action
- advanced drawer

Confirm:

- graph is not hidden behind advanced controls
- reset is not a visually dominant full-width primary action
- section is visually distinct from Display Fit

## Suggestions Popout

Check:

- no suggestions
- one suggestion
- multiple suggestions
- suggestion with warning/caution
- Local Range suggestion
- WB suggestion
- RAW Exposure suggestion
- suggestion that cannot be applied because controls/data are unavailable

Confirm:

- popout opens from the RAW top area suggestion badge
- closing popout does not mutate recipe values
- click-to-apply creates visible editable recipe changes
- owning controls show compact markers/ghost/applied state where supported
- full rationale is not duplicated throughout the panel
- suggestion count includes applyable suggestions, not withheld diagnostics

## Diagnostics

Check:

- drawer collapsed by default
- drawer opened manually
- drawer opened from warning/details link if implemented
- technical stats
- current-frame stats
- highlight risk
- missing metadata
- fallback analysis
- withheld recommendation

Confirm:

- technical detail is preserved
- diagnostics do not dominate normal editing
- unavailable states are honest and not shown as broken disabled actions

## Center Workspace

Check available modes:

- Final
- Compare
- Affected
- Delta
- Mask
- Highlight Risk

Confirm:

- active mode is visible
- unavailable modes are hidden or disabled with tooltip
- changing view mode does not mutate recipe values
- Local Range overlay state and center view mode are not contradictory
- mask/highlight colors have enough legend or labels to be understood

## Icon-First Controls

Check:

- every icon button has tooltip
- active state is visible beyond color alone
- disabled state has a reason where needed
- destructive/structural actions include text or confirmation
- keyboard/Esc behavior remains predictable for popouts and targeting

## Regression Notes

Record any known limitation introduced by a pass:

```text
Pass:
Limitation:
Why accepted:
Follow-up:
```
