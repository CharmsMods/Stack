# Channel-Based Node Design

- Accepted direction: 2026-07-20
- Status: product design accepted; exact next contracts and implementation are inactive

This area explains how Stack should treat Values, Channels, Images, Data, and
Specialized work. It is the main product-design area for the next generation
of the node graph.

## Choose What You Want To Read

- [Accepted Product Direction](accepted-product-direction.md) is the concise
  current design that NMR-139 adopts.
- [Work Still To Define](work-still-to-define.md) contains the exact C1–C8
  contract queue and R1–R4 research queue.
- [Questions For You](../01-start-here/questions-for-you.md) translates that
  queue into ordinary-language choices with answer space.
- [Design Details](design-details/) contains one focused file per subject.
- [Channel-System Research Plans](../05-research/channel-system-plans/) contains
  the research that still needs sources and findings.
- [Channel-System Research Findings](../05-research/channel-system-findings/README.md)
  is the destination for completed source-backed reports.
- [Channel-System Readiness](../04-implementation/status/channel-system-readiness-2026-07-20.md)
  is a dated code/status audit, not permission to begin implementation.

You may open and work on any topic. The dependency order controls eventual
implementation, not the order of conversation, goal editing, documentation,
or research.

## Accepted Direction In Brief

- Teach a small public model: Value, Channel, Image, Data, and Specialized.
- Treat Mask and Alpha as Channel roles, not unrelated mathematical containers.
- Let Images contain only the components that are actually present.
- Keep a missing component different from a present zero-valued Channel.
- Let Output inspect a standalone Channel without silently creating RGB export data.
- Create opaque Alpha as a visible, editable Constant Channel when the accepted
  Image Combine behavior calls for it.
- Broadcast a Value only through ports that explicitly accept `Value/Channel`.
- Store component participation on applicable nodes instead of applying a
  hidden graph-wide reinterpretation.
- Keep compounds inspectable or unpackable only when their canonical structure
  is truthful; expose named stages for specialized work.
- Use one versioned node-definition model for primitives, compounds, migrated
  legacy tools, generators, and specialized stages.

## Design Details By Subject

| Subject | Technical details are in |
| --- | --- |
| Values, Channels, Images, partial Images, Output, and source dissolution | [Channels, Values, Images, And Output](design-details/channels-values-images-and-output.md) |
| Connections, failures, diagnostics, pins, and wires | [Connections, Errors, And Visual Feedback](design-details/connections-errors-and-visual-feedback.md) |
| Alpha, Masks, constant Alpha, and component participation | [Alpha, Masks, And Math](design-details/alpha-masks-and-math.md) |
| Operation scope, broadcast, fusion, materialization, and performance | [Execution And Performance](design-details/execution-and-performance.md) |
| Inspect, Make Unique, Unpack, and specialized honesty | [Compound Nodes And Unpacking](design-details/compound-nodes-and-unpacking.md) |
| Source color, editing, display, monitor presentation, and export | [Color Editing, Display, And Export](design-details/color-editing-display-and-export.md) |
| One node library, browser presentation, migration, and Phase 7 | [Node Library And Migration](design-details/node-library-and-migration.md) |

## Authority And Future Contracts

The [decision log](../01-start-here/decision-log.md), especially NMR-139,
adopts the accepted product direction. Topic files provide detail but do not
silently overrule it.

A future approved C1–C8 contract will govern exact implementation detail. If
evidence shows that the product experience itself should change, propose and
record that revision in the accepted direction and decision log rather than
letting code settle the conflict accidentally.

The recommended next implementation contract is the combined partial-Image,
Output/inspection, and visible constant-Alpha slice. That recommendation does
not make other topics off-limits.
