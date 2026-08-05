# Failure Taxonomy And Review v1

## Severity

| Severity | Meaning |
| --- | --- |
| Critical | Unsafe state, hidden output, lost user work, invalid graph, stale apply, severe clipping/halo/color failure, or crash. Candidate must be rejected. |
| Major | Technically unacceptable starting point requiring substantial repair. |
| Minor | Usable start with an obvious but easy manual correction. |
| Preference | Both results are technically acceptable; reviewer prefers one. |

## Required Families

| Family | Required examples or fixture |
| --- | --- |
| Identity/state | source switch, decode change, recipe change, cancel, failed render, stale cache, one Undo, save/load |
| Raw darkness | severe recoverable underexposure, ordinary dark scene, intentional low key, noise-dominated shadow |
| Dynamic range | wide range without clipping, limiting-plane headroom, partial-plane clipping, all-plane clipping, non-linear response |
| Regions | backlit subject/object, bright window, sky/foreground conflict, high-key scene, ordinary well-exposed no-op |
| Highlight appearance | neon, fire, sunset, stage light, specular reflection, smooth bright boundary |
| Noise/detail | high ISO, flat dark region, fine foliage/fabric, demosaic-sensitive edge |
| Color/WB | as-shot valid, neutral evidence, mixed light, intentional warm/cool, saturated hue, skin-tone review |
| Geometry | orientation 1/6/8, active area, default crop, user crop, bright border in every side |
| Graphs | neutral graph, user-owned non-neutral graph, capacity limit, duplicate x, non-monotone proposal, extreme slope/curvature |
| Display | clipped output, crushed black, weak mid-grey readability, highlight shoulder, shadow toe, calibrated/unknown display |
| Runtime | converged, budget exhausted, warm start retained, blocked evidence, canceled, failed render, proxy/full mismatch |

## Reason Codes

```text
critical.hidden_processing
critical.recipe_mutated_before_apply
critical.user_graph_overwritten
critical.stale_candidate_applied
critical.invalid_graph
critical.raw_safety_violation
critical.all_channel_clip_increased
critical.partial_channel_color_failure
critical.halo_or_gradient_reversal
critical.nan_inf_or_crash

major.too_dark_recoverable
major.too_bright
major.highlight_detail_lost
major.shadow_noise_revealed
major.subject_still_hidden
major.local_mask_leak
major.flat_or_muddy
major.color_cast
major.hue_or_gamut_damage
major.overprocessed
major.proxy_full_disagreement

minor.exposure
minor.local_range
minor.finish_tone
minor.display_fit
minor.white_balance
minor.no_unnecessary_change

preference.brighter
preference.darker
preference.warmer
preference.cooler
preference.more_contrast
preference.less_contrast
```

## Human Review Record

```yaml
schema: stack.iterative-raw-solver.human-review
version: 1
source:
  record_id:
  sha256:
  session_group:
  partition:
  camera:
  iso:
scene:
  observable_tags: []
  declared_intent: unknown
  intent_confidence: 0.0
candidate:
  solver_version:
  feature_version:
  parameter_space_version:
  mode:
  evaluation_budget:
  stop_status:
  changed_visible_controls: []
technical:
  acceptable: null
  severity: null
  raw_safety: unknown
  noise: unknown
  highlight: unknown
  halo_edge: unknown
  color: unknown
  display: unknown
  proxy_full_agreement: unknown
  graph_and_ownership: unknown
comparison:
  baseline_acceptable: null
  candidate_acceptable: null
  preferred: tie
  confidence: 0.0
  acceptable_control_ranges: {}
failure_codes: []
next_manual_control:
notes:
reviewer:
  id:
  role:
  display_condition: unknown
  timestamp:
```

## Review Protocol

- Review baseline and candidate blind when practical.
- Decide technical acceptability before preference.
- A tie is valid; an ordinary image may be best unchanged.
- Record acceptable ranges when moving one visible control, not one alleged
  ground-truth vector.
- Do not infer intent from darkness alone. Mark it unknown when unsupported.
- Inspect high-contrast edges at native scale and a declared viewing scale.
- Report results by family and session, not only one overall percentage.
- Any critical failure is listed individually and cannot be averaged away.
