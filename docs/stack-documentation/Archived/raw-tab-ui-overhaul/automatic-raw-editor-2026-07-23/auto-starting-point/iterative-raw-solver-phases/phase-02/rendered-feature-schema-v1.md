# Rendered Feature Schema v1

## Public API

Phase 02 freezes `Stack::RenderedFeatures` in
`src/Raw/RenderedFeatureEvidence.h/.cpp` as `rendered-features-v1`.

The API accepts caller-declared pixels and context. It does not load a project,
render a candidate, score an image, choose a control, or mutate a recipe.

```text
AnalyzeSceneLinear       working-linear scene measurements
AnalyzeRegionMask        continuous mask/region reliability measurements
AnalyzeDisplayMapped     declared linear-display measurements
CompareRenderedImages    before/after structure, halo, and color measurements
CompareFeatureRecords    proxy/reference disagreement and stale-record rejection
EvaluateEdgeProfile      exact signed one-dimensional edge diagnostics
```

The bilateral, guided, and local-Laplacian-inspired functions in the same API
are research prototypes. They are not production filters and are not consumed
by the solver.

## Identity And Rejection

A `FeatureContext` declares:

```text
source identity
recipe identity
stage
color-space name
color-transform identity and working-to-XYZ matrix
transfer function
grey reference
exact Phase 01 evidence identity when raw priors are supplied
crop identity
orientation-normalized state
source and measurement resolution
declared global and maximum local lift
```

Every feature value repeats the feature version, stage, space, transform,
transfer function, grey/raw reference, units, resolution, crop, valid fraction,
uncertainty, measured runtime share, minimum resolution, disposition, and
reason. The record repeats source/recipe/upstream identities and dimensions.

Phase 01 evidence is accepted only when all of these match:

```text
raw record valid
raw feature version == raw-technical-evidence-v1
raw source SHA-256 == rendered source identity
expected evidence identity is present
raw evidence identity == expected evidence identity
```

A proxy comparison rejects a source, recipe, stage, space, transform, transfer,
grey, raw-evidence, or crop mismatch. It also omits features below their
declared minimum resolution. Missing or invalid evidence remains unavailable;
it is never converted to zero.

## Stage And Luminance Rules

Scene analysis requires linear input. Luminance is computed only from the
declared working-to-XYZ Y row:

```text
Y = M_YR*R + M_YG*G + M_YB*B
EV = log2(Y / declared_grey)
```

There is no silent Rec.709 substitution. Non-positive luminance is excluded
from log statistics while negative-channel fraction is retained. Display
analysis requires declared linear display RGB and emits linear and encoded
measurements as separate feature IDs. Absolute display dynamic range is
unavailable unless peak, black, and ambient luminance are supplied.

## Accepted Technical Families

Accepted means reliable for its named measurement/uncertainty role. It does
not mean an objective weight, aesthetic target, or numeric acceptability limit
has been selected.

| Family | Accepted measurements | Minimum resolution |
| --- | --- | ---: |
| Scene placement | EV p0.1/p1/p5/p10/p25/p50/p75/p95/p99/p99.9, log-average EV, occupied range, shadow/highlight mass | 16-32 px |
| Regional conflict | Oriented center/border/top/bottom EV and signed conflict | 64 px |
| Mask reliability | Strength, continuous ambiguity, foreground/background EV conflict, boundary support, unsupported-boundary leakage uncertainty | 64-128 px |
| Multiscale structure | Log-luminance detail and base-gradient RMS at radii 1/2/4/8; before/after detail-energy ratios | 32-64 px |
| Halo/artifact | Gradient reversal, overshoot, adjacent-band energy, and edge shift against a reference | 128 px |
| Rendered noise | Smooth-shadow log-luma residual, opponent-chroma residual, and declared-lift amplification | 128 px for residuals |
| Raw noise/highlight priors | Phase 01 minimum predicted SNR and partial/multi/all-plane clip fractions | 1 px rendered record; upstream record controls validity |
| WB uncertainty | Gray World/Shades-of-Gray/Gray Edge disagreement and quadrant disagreement | 128 px |
| Color | Working-space out-of-range pressure and chroma-weighted before/after hue shift | 1 px / 64 px |
| Display | Linear clip fractions and p05/p50/p95, encoded p05/p50/p95, calibrated absolute dynamic range | 16 px / declared display model |
| Resolution uncertainty | Per-feature proxy/reference relative delta, missing list, mean and maximum disagreement | Feature-specific |

## Explicit Non-Admissions

These remain visible but cannot enter a Phase 03 objective as accepted terms:

| Feature or approach | Disposition | Reason |
| --- | --- | --- |
| High-key/low-key ambiguity | Needs human study | Histogram evidence cannot establish intent. |
| Display shoulder/toe tail ratios | Prototype-only | Distribution continuity is not a spatial/perceptual rolloff judgment. |
| Edge texture-energy ratio | Prototype-only | Texture suppression and contrast-halo separation need labeled natural boundaries. |
| Mean RGB chroma change | Prototype-only | Not a perceptual saturation metric. |
| Bilateral base/detail | Prototype-only | Research mask/base constructor only; cost and parameter behavior are not production policy. |
| Guided base/mask | Prototype-only | Research mask/base constructor only; guide leakage remains a study item. |
| Local-Laplacian-inspired structure | Prototype-only | The implementation is an experiment, not the cited paper's production algorithm. |
| Skin/memory color and semantic intent | Needs human-label study | No labels or consented preference model exist. |
| TMQI/HDR-VDP as a default objective | Rejected | Reference/display assumptions conflict with the no-unique-target rule. |
| One histogram target or ISO-only noise cost | Rejected | Known high/low-key and signal-dependent-noise failures. |

No halo, hue, WB, display, or noise acceptability threshold is frozen in this
phase. Phase 03 may consume accepted measurements individually and must retain
their uncertainties; it may not silently promote the non-admitted items.

## Cost And Resolution

Each record measures wall-clock runtime and assigns a runtime share to every
emitted feature. The 16-source audit used 1024-pixel-long-side diagnostic bases
and recorded individual baseline record runtimes. Across the 14 non-full-
reference records, mean scene-record time was `714.8 ms` (`638.3-796.3 ms`) on
the validation machine. The whole controlled audit, including two true full-
resolution decodes/references and all perturbations, took `289670.3 ms`.

These values describe the current CPU diagnostic implementation. They are not
a future interactive budget. Phase 03 must measure actual candidate-render and
feature cost before choosing an evaluation budget.

## Consumer Rule

Phase 03 may consume only accepted `rendered-features-v1` values whose identity,
stage, space, reference, resolution, validity, and upstream evidence match the
candidate. It must preserve individual terms and reasons. It may not turn this
record into a single score without the separate Phase 03 constraint/objective
contract and surface experiments.
