# Research Library

This is the deep technical reference for image math, color, compositing,
spatial processing, graph architecture, and external standards. You do not
need to read it all before discussing a product idea.

## Authority And Date Warning

The formulas, standards, and primary-source explanations remain useful. Any
column or sentence describing what Stack currently has, lacks, or should do
was generally based on the 2026-07-12 audit unless a later date is named.
Phases 0–6 and the accepted channel-based direction supersede many of those
old implementation-status claims.

Use this library as technical reference and architecture rationale. Current
code/tests, the [accepted product direction](../02-channel-based-design/accepted-product-direction.md),
the [decision log](../01-start-here/decision-log.md), and implemented
[technical contracts](../03-technical-contracts/README.md) take precedence.
The operation lists are an encyclopedia, not a node-library backlog.

## Choose A Research Area

### Image Operations

| Area | Files |
| --- | --- |
| Basic number, pixel, channel, and vector math | [Number And Pixel Operations](image-operations/basic-math/number-and-pixel-operations.md), [Channel And Vector Operations](image-operations/basic-math/channel-and-vector-operations.md) |
| Tone, color, alpha, masks, blending, and compositing | [Tone, Exposure, And Curves](image-operations/tone-color-and-compositing/tone-exposure-and-curves.md), [Color Grading](image-operations/tone-color-and-compositing/color-grading-operations.md), [Alpha, Masks, Blending, And Compositing](image-operations/tone-color-and-compositing/alpha-masks-blending-and-compositing.md) |
| Filtering, detail, morphology, geometry, and resampling | [Filters, Detail, And Morphology](image-operations/spatial-processing/filters-detail-and-morphology.md), [Geometry, Warping, And Resampling](image-operations/spatial-processing/geometry-warping-and-resampling.md) |
| Statistics, analysis, references, multiple images, and time | [Image Analysis And Statistics](image-operations/analysis-and-multiple-inputs/image-analysis-and-statistics.md), [Multiple Images, References, And Time](image-operations/analysis-and-multiple-inputs/multiple-images-references-and-time.md) |
| Camera metadata, calibration, scene data, and model boundaries | [Camera, Metadata, Calibration, And Scene](image-operations/specialized-inputs/camera-metadata-calibration-and-scene.md), [Model-Based Tools Boundary](image-operations/specialized-inputs/model-based-tools-boundary.md) |

Read [How To Read The Operation Reference](operation-reference-guide.md) for
scope codes, dated Stack-status labels, and the technical glossary.

### Architecture Background

| Question | File | How to use it now |
| --- | --- | --- |
| How might values, descriptors, wires, resources, and viewing be separated? | [Image Values And Wires Research](architecture-background/image-values-and-wires-research.md) | Rationale only; implemented contracts and NMR-139 supersede policy choices |
| How might the node library be named and organized? | [Node Library Organization Research](architecture-background/node-library-organization-research.md) | Proposal and vocabulary reference |
| How might validation, propagation, and diagnostics work? | [Graph Validation And Composition Research](architecture-background/graph-validation-and-composition-research.md) | Rationale only; current permissive rules and implemented contracts take precedence |
| How can familiar tools be built from smaller operations? | [Familiar Tools From Smaller Operations](architecture-background/familiar-tools-built-from-smaller-operations.md) | Useful design examples, not exact production definitions |
| Why separate authored graph, semantic meaning, and execution plan? | [Composable Node Graph Research](architecture-background/composable-node-graph-research.md) | Deep architecture rationale; its old phase plan is historical |

The old 2026-07-12 “current Stack mapping” now lives beside the dated audit in
[History](../07-history/2026-07-12-code-audit/stack-to-research-model-map.md).

### Channel-System Research Plans

These are plans for work that still needs sources and findings; they are not
completed research reports:

- [Naming And Industry Terms](channel-system-plans/naming-and-industry-terms.md)
- [Color Management And Display](channel-system-plans/color-management-and-display.md)
- [Compound Node Behavior](channel-system-plans/compound-node-behavior.md)

Completed source-backed reports belong in
[Channel-System Research Findings](channel-system-findings/README.md), separate
from the plans.

Current completed finding:

- [Naming And Industry Terms — 2026-07-21](channel-system-findings/2026-07-21-naming-and-industry-terms.md)

### Sources

[Standards And Research Source Ledger](sources/standards-and-research-source-ledger.md)
collects the primary standards, official documentation, and papers used by
the technical reference. When a new implementation decision depends on a
claim, inspect the current primary source and record an access date or pinned
version where possible.

## Smallest Useful Reading Routes

- For a specific node formula, open only its operation category and the source
  ledger entries it cites.
- For channel-product work, begin with the
  [accepted design](../02-channel-based-design/accepted-product-direction.md),
  then use the relevant research plan or operation reference.
- For compiler/fusion work, use the pointwise execution contract first and the
  composable-graph research only for rationale.
- For compounds, use the implemented compound contract first, then the
  familiar-tool examples and compound research plan.
- For current-code claims, inspect current code and tests. The dated audit is a
  baseline, not present-day ground truth.
