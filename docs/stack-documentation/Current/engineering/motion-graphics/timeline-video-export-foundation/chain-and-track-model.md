# Chain And Track Model

This document captures the working language for connected chains and timeline
rows. It now includes the first user-confirmed row ownership decisions, while
still leaving code-specific details for the research pass.

## User Definition

A connected chain starts at an input, passes through any connected nodes in
between, and reaches an output. That full input-to-output path is one chain.

The user specifically called out these cases:

- multiple independent chains
- one chain that reaches two separate outputs at the end
- one chain that splits somewhere in the middle and reaches multiple outputs

Those split-output cases may need to appear as separate timeline objects.

## Working Terms

`Source input`
: A graph node or import point that begins a chain. Code research must identify
which current node types can be treated as source inputs.

`Terminal output`
: A graph output, canvas object output, export output, preview output, or other
node that marks the visible end of a chain. Code research must identify the
actual output concepts in the current graph.

`Connected chain`
: The traversed graph region from source input to terminal output.

`Branch`
: A subpath created when a chain fans out to more than one downstream result.

`Timeline row`
: A horizontal row in the timeline anchored internally to a final output node.
The row represents the output object chain that feeds that final output.

`Chain name`
: A user-visible name for a connected chain or chain-output track. Naming is
needed before users can select chains in a picker or understand timeline rows.

## Confirmed Row Model

Timeline rows should be created automatically from graph structure and anchored
to final output nodes.

- Make timeline row identity terminal-output based.
- Treat each terminal output as a trackable chain result.
- Let rows share upstream nodes when one input chain splits to multiple outputs.
- Give each row a stable generated name and allow user renaming.
- Store keyframed targets by stable IDs, not by display names.

When a shared upstream node is animated, every downstream output that depends on
that node should reflect the animated value. Keyframes belong to the actual node
parameter, not to a row-local copy.

This model is compatible with split-output graphs because two outputs can
produce two timeline rows even when they share upstream nodes.

## Future Row Hierarchy

The terminal-output row anchor remains the top-level chain/section identity,
but the end-goal keyframe editor needs deeper hierarchy than one flat row per
output.

Future visible structure should move toward:

- chain section: one completed output chain, anchored by final output node ID
- node row: one graph node participating in that chain
- property/value row: one animatable value exposed by that node

Keyframes for different graph nodes should not be placed on the same final
horizontal editing row. Chain rows can show summaries or inherited/shared
activity, but actual editable keyframes should live on node/property-specific
rows as the timeline matures.

Open research:

- how to handle shared upstream nodes that appear in multiple output chains
- whether a shared node row is duplicated under each affected chain, displayed
  once in a shared/source section, or summarized in multiple places
- how row expansion/collapse state should be stored
- whether property rows are generated only for animated values or for all
  animatable values on an expanded node

## Naming Questions

Open questions for user and code research:

- Should chain names appear directly on graph outputs, in the timeline, or both?
- Should Stack auto-name rows from the terminal output node, source input node,
  connected file name, or canvas object name?
- When one chain splits into two outputs, should the shared part have one name
  with output suffixes, or should each branch be fully named on its own?
- If a user renames a chain row, does that rename a graph node, a canvas object,
  a timeline-only object, or a separate chain metadata record?
- What happens to the chain name when links change and the old chain no longer
  exists?

## Identity And Persistence Risks

The future implementation must not address animated values by fragile labels or
node positions. It needs stable identity for:

- graph document
- chain or terminal output
- node
- parameter
- keyframe track
- frame number

Code research must identify which IDs already exist and which ones are stable
across save/load.

## Edge Cases To Inspect

- A single input fans out to two outputs.
- A chain splits in the middle, then only one branch reaches an output.
- Two branches merge again before the output.
- A node belongs to two visible timeline rows because it is shared upstream.
- A shared upstream node has keyframes that affect both visible output rows.
- A user deletes a node that has animated parameters.
- A user deletes or reconnects an output that owns a timeline row.
- A user duplicates a chain or copies/pastes nodes with keyframes.
- A graph has no valid output.
- A graph has disconnected islands.
- A project loads older documents without any timeline metadata.

## Needed Code Findings

Update this section after code inspection:

- graph node ID type: runtime graph nodes use integer node IDs exposed through
  `EditorNodeGraph::Node::id`.
- graph link ID type: timeline row discovery does not require a durable link ID
  yet; links are traversed through `EditorNodeGraph::Graph` helpers.
- output node concepts: the current timeline row anchor is
  `EditorNodeGraph::NodeKind::Output`, represented in completed-chain records
  by `CompletedChainInfo::outputNodeId`.
- source/input node concepts:
- existing node display-name behavior: row labels currently reuse
  `BuildCompositeChainLabel(...)`; future user-renamable chain labels still
  need a persistence design.
- existing node rename behavior:
- graph traversal helpers: `EditorNodeGraph::Graph::GetCompletedChains()` is
  the source of truth for automatic timeline rows.
- serialization paths: Pass 6 stores timeline project data under
  `editorTimeline` in the pipeline payload; row discovery itself remains
  derived from completed graph chains on load.
- copy/paste behavior:
- validation hooks:
