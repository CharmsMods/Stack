# Automatic UI Archive Inventory

Last checked: July 23, 2026.

## Product Surfaces To Retire

### Develop Node

- Auto/Manual mode selector
- Auto as the default for a new Develop node
- automatic solve when a Develop node is created from a selected RAW Source
- Auto Guidance controls and automatic status readouts
- Return to Auto and Recalibrate Auto
- Auto WB as an active manual-control choice

### RAW Workspace

- suggestion count button, suggestion expander, and Apply actions
- Build Starting Point
- Precise/Fast selector and precise-solve cancellation UI
- Analyze-only, Fit Display, Refit Display, Add Local Range, Add Mild Tone, and
  automatic-action Undo
- Starting Point result/status blocks
- automatic ownership labels, suggestion markers, and per-control readouts
- Starting Point stage readbacks, candidate reports, rationales, applied
  suggestion state, and automatic undo state in Diagnostics
- Auto White Balance as an active manual-control choice
- automatic noise/detail recommendation rows

## Preserve During The UI Archive

- backend types, solver code, and validation scenarios
- serialized legacy fields
- previously authored visible recipe values
- camera/as-shot white balance
- manual custom and gray-point white balance controls
- Technical RAW, Highlight Signals, and Current Frame Stats diagnostics
- Local Range and Finish Tone graph editing
- explicit View Transform controls

## Later Audit

The following behaviors use automatic or adaptive algorithms but are not part
of the retired automatic-editor UI pass. The neutral-baseline audit must decide
whether each is mandatory processing, a safe metadata default, or a creative
choice that should be manual:

- baseline exposure metadata
- camera transform source selection
- highlight reconstruction defaults
- raw detail fusion / scene-prep adaptive behavior
- denoise strength derived from metadata
- display transform defaults
- orientation and lens/profile corrections
