# Compound Nodes And Honest Decomposition

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: idea
- Topic: compound-decomposition
- Verification: partially-verified against the implemented Phase 5 compound system

## User Direction

Stack needs a large, coherent system for both directions:

1. Combine several nodes into one reusable high-level node.
2. Dissolve an existing high-level node into smaller sub-nodes that expose the
   math or stages from which its behavior is genuinely built.

This must work with the channel mindset and with operations whose results
depend on relationships among channels, neighboring pixels, complete frames,
multiple images, or specialized data.

## What Current Stack Already Provides

Phase 5 implements executable, versioned compounds with stable public ports,
promoted parameters, exact embedded definitions, Make Unique, Unpack, and
temporary expansion before rendering. It distinguishes:

- transparent graph compounds;
- graph-defined semantics with an optimized equivalent; and
- opaque specialized operations.

That is a strong foundation for combining nodes. The larger unresolved problem
is deciding which existing high-level nodes have a truthful canonical graph and
what “dissolve” should promise.

## The Core Honesty Rule

A node may dissolve into sub-nodes only if the subgraph is a complete canonical
definition of its promised result, including relevant:

- operation order;
- constants and parameter mappings;
- channel extraction and recombination;
- alpha behavior;
- color/transfer/reference assumptions;
- range and invalid-value behavior;
- sampling coordinates, border modes, extents, and neighborhood support;
- masks and where they enter the computation;
- reductions, feedback, iteration, state, random seeds, or external resources;
  and
- precision and equivalence tolerance where an optimized backend is retained.

An attractive educational approximation is not a valid decomposition if the
real node does something materially different.

## Candidate Inspectability Classes

The existing three classes may be enough, but this discussion should consider a
four-level user explanation:

1. **Primitive:** one exact public operation; nothing meaningful to dissolve.
2. **Transparent compound:** the canonical graph is the implementation and can
   be opened or unpacked exactly.
3. **Optimized compound:** the visible canonical graph defines the result;
   Stack may run a fused or specialized equivalent backed by tolerance tests.
4. **Structured specialized operation:** exposes honest stages, evidence, and
   perhaps selected intermediate outputs, but cannot be reduced to ordinary
   editable primitives without omitting essential behavior.

The fourth class could remain an explanatory presentation of the current
`opaque specialized` boundary rather than a new executable compound class.

## Channel-First Compound Interfaces

A compound should not have to expose every internal channel wire permanently.
Its public interface can deliberately provide:

- a full Image bundle input/output;
- expandable R/G/B/A or named auxiliary Channel ports;
- one-value controls;
- per-pixel map/mask inputs;
- explicit color/alpha conversion ports or policies;
- advanced intermediate outputs; and
- stable promoted parameters.

Internally, an RGB-dependent node can split channels, calculate their
relationship, and recombine them while still presenting one friendly Image
input. A channel-independent compound can accept either a Channel or Image
bundle through a precisely defined overload/broadcast policy.

## Important Limitations

- Automatic reverse-engineering of arbitrary shader source into a clean node
  graph is not a reliable product contract.
- Iterative, recursive, feedback, temporal, external-model, and stateful
  algorithms may not have a finite acyclic primitive graph.
- RAW processing carries sensor metadata, calibration, and specialized storage
  that ordinary sub-nodes cannot currently represent honestly.
- FFT can be represented as a specialized transform node, but expanding it into
  thousands of butterfly operations would not be useful authoring.
- Denoisers and alignment algorithms may have meaningful stages without having
  a practical primitive decomposition.
- Dissolving a large node can create graph explosion. Open/inspect and Unpack
  may need separate commands.
- Optimized equivalents require reference tests and declared tolerances.
- Saved projects must remain pinned to exact compound definitions; a library
  update cannot silently change an existing result.

## Questions For The Next Conversation

- Should “Open,” “Inspect,” and “Unpack/Dissolve” be three different actions?
- Should users be able to edit a shared definition directly, or always Make
  Unique before changing internals?
- Can a high-level node expose optional intermediate outputs without forcing
  full decomposition?
- Should browser nodes primarily be primitives, compounds, or a curated mix?
- How are parameter widgets mapped to multiple internal values and equations?
- Can a compound accept either one Channel or an Image bundle without hiding
  broadcast or per-channel behavior?
- How does a compound declare that several channels must remain spatially
  registered?
- When is a fused shader merely an implementation of a compound, and when is
  it a different node definition?
- Which existing legacy nodes should be the first decomposition case studies?
- What should Stack display when a node is opaque: stages, documentation,
  diagnostics, intermediate previews, or only its public contract?

## Recommended First Case Studies

These are discussion candidates, not implementation authorization:

- Brightness or Contrast as a small pointwise primitive/compound boundary.
- Saturation as an RGB-relationship example.
- Gaussian Blur as a neighborhood/halo example.
- A masked image adjustment showing image, channel, one-value, and mask inputs.
- View/output finishing as a multi-stage color example.
- One node that must remain specialized, to prove the system can say “cannot
  truthfully dissolve” without seeming broken.

## Research Needed

See `../research-backlog/2026-07-17-compound-and-decomposition-research.md`.

## Related Docs

- `../../phase-5-compound-contract-v1.md`
- `../../2026-07-16-phase-5-completion-evidence.md`
- `../../program-contract.md`
- `2026-07-17-channel-first-data-model-and-language.md`
- `2026-07-17-unified-node-library-and-future-growth.md`
