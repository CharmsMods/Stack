# Feature Ledger v1

Status meanings follow `checkpoint-protocol.md`. `Accepted` means acceptable
for the named diagnostic or constraint role, not that a final threshold or
objective weight exists.

| ID | Status | Domain / units | Role and validation |
| --- | --- | --- | --- |
| `source_identity` | Accepted | Original bytes / SHA-256 | Tier 0 identity and corpus split. Byte-change and duplicate fixtures. |
| `decode_identity` | Accepted | Decode configuration / canonical fingerprint | Tier 0 cache/cancel key. Field-order and decode-change fixtures. |
| `recipe_identity` | Accepted | Visible recipe / canonical fingerprint | Tier 0 candidate isolation. Every visible field and graph point participates. |
| `raw_normalized_x` | Accepted for Phase 01 diagnostics | Linear raw / normalized sensor fraction | DNG mapping in `raw-math-v1`; black/white/spatial-pattern fixtures. |
| `raw_valid_fraction` | Accepted | Raw active area / fraction | Confidence and blocking. Active/masked/crop/orientation fixtures. |
| `per_plane_clip_fraction` | Accepted | Linear raw / fraction | Tier 1 raw evidence. Per-plane threshold perturbations. |
| `partial_all_clip_state` | Accepted with aligned-stage requirement | CFA-aware neighborhood / fraction and region | Tier 1. Mosaic-only counts cannot claim all-channel clipping. |
| `linear_response_pressure` | Accepted when tag valid | Linear raw / fraction above LRL and EV headroom | Tier 1 highlight color risk; missing-tag fallback is explicit. |
| `wb_scaled_headroom_ev` | Accepted | Linear raw / EV by plane | Positive RAW Exposure bound. WB/headroom monotonic fixtures. |
| `dng_noise_sigma` | Accepted when profile valid | Linear raw / normalized sigma | Phase 01 diagnostic and Phase 02 noise prior. Profile coefficient fixtures. |
| `lifted_noise_visibility` | Prototype-only | Raw prediction plus rendered display / sigma and visible contrast | Needs real high-ISO and proxy/full correlation; multiplication alone does not change raw SNR. |
| `scene_luma_ev` | Accepted for diagnostics | Demosaiced working-linear / EV relative to declared grey | Placement evidence, never one permanent target. Matrix/negative-value fixtures. |
| `robust_scene_percentiles` | Accepted for diagnostics | Working-linear / luma or EV | Record crop, weights, valid fraction, and uncertainty. High/low-key human study before targets. |
| `regional_visibility` | Prototype-only | Oriented working-linear / EV by mask | Needs backlit/subject labels and mask reliability evidence. |
| `bilateral_base_detail` | Prototype-only | Log scene luminance / multiscale | Compare support, halo, staircase, cost, and proxy agreement. |
| `guided_base_mask` | Prototype-only | Scene luminance with guide / multiscale | Compare guide leakage, low-variance behavior, luminance/contrast halo. |
| `local_laplacian_structure` | Prototype-only | Scene luminance pyramid / coefficient energy | Compare cost, noise response, parameter stability, and proxy agreement. |
| `edge_reversal` | Prototype-only; hard-constraint candidate | Log-luma edge profiles / normalized gradient energy | Synthetic monotonic/reversal fixtures plus labeled smooth boundaries. |
| `edge_overshoot_band` | Prototype-only | Log-luma profiles / normalized contrast-area | Scale/view sensitivity and complex-scene study required. |
| `edge_texture_ratio` | Prototype-only | Detail variance near/away from edge / ratio | Contrast-halo fixture and real texture boundary labels. |
| `graph_finite_serializable` | Accepted | Recipe / boolean plus reason | Tier 0 hard constraint. NaN/Inf/round-trip fixtures. |
| `finish_monotone_slope_curvature` | Accepted as structural measurements | Normalized graph / slopes and second differences | Limits remain fixture-derived; monotonicity and endpoint tests. |
| `local_graph_order_capacity` | Accepted | EV graph / counts, spacing, range | Tier 0 hard constraint; endpoints, duplicate x, 12-point capacity fixtures. |
| `wb_estimator_disagreement` | Accepted as uncertainty only | Linear color estimates / angular or log-chroma distance | Blocks/narrows WB; mixed-light and stylized scenes. |
| `neutral_error` | Prototype-only | Declared perceptual/linear space / distance | Needs trustworthy neutral labels; not a universal mood target. |
| `hue_shift` | Prototype-only | Declared perceptual space / degrees | Chroma reliability, saturated hues, skin and highlight studies. |
| `gamut_pressure` | Prototype-only | Pre-map display space / distance and fraction | Needs output-space declaration and human correlation. |
| `display_clip_fraction` | Accepted | Final display linear/encoded as declared / fraction | Tier 1/3 output safety; separate from raw clipping. |
| `display_rolloff_continuity` | Prototype-only | Display luminance / derivative or profile term | Shoulder/toe perturbations and highlight boundary labels. |
| `display_multiscale_contrast` | Prototype-only | Display model / transducer or normalized contrast | Absolute claim needs calibrated luminance/ambient; relative mode lower confidence. |
| `tmqi` | Rejected for production objective | HDR reference plus LDR / index | Offline advisory only; reference and naturalness assumptions conflict with no-unique-target rule. |
| `hdr_vdp` | Rejected for default production objective | Absolute display/reference / visibility or quality | Offline calibrated validation only. |
| `single_histogram_target` | Rejected | Any rendered histogram | Fails high-key, low-key, backlit, and intent diversity. |
| `iso_noise_penalty` | Rejected as sole noise model | Metadata ISO | Context only; not signal-dependent noise or display visibility. |
| `skin_memory_color` | Needs human-label study | Perceptual color / distance | Requires confident subject/skin labels and diverse review. |
| `scene_intent_classifier` | Needs human-label study | Image/metadata / probabilities | May alter soft ranges only; uncertainty cannot relax Tier 0/1. |
| `acceptable_control_range` | Needs human-label study | Visible control / interval | Preferred supervision form; reviewer consistency and session splits required. |
| `edit_distance` | Accepted as Tier 5 tie-breaker | Normalized visible parameter L1 plus changed count | Applied only after higher tiers; neutral/no-op fixtures. |

## Admission Rule

A prototype becomes accepted only after the Phase 02 record supplies version,
units, stage, color space, reference, mask/valid fraction, uncertainty,
controlled response, proxy/full sensitivity, real-image correlation, cost, and
minimum valid resolution. A feature accepted as uncertainty evidence does not
automatically become an objective term.
