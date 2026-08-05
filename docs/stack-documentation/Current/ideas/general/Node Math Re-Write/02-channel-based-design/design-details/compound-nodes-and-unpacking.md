# Compound Nodes And Honest Decomposition

- Started: 2026-07-17
- Consolidated: 2026-07-20
- Authority: `../accepted-product-direction.md`
- Verification: current Phase 5 compound contract and implementation evidence inspected on 2026-07-20

This file owns how nodes are combined, inspected, edited, and dissolved without
misrepresenting their real algorithms.

## User Actions

- **Inspect:** open the canonical internal structure read-only without changing
  the containing graph.
- **Edit / Make Unique:** create an editable local definition instead of
  silently modifying a shared pinned definition.
- **Dissolve / Unpack:** replace the instance with a fresh exact canonical graph
  and rewire stable public ports.

These actions remain distinct because temporary understanding, reusable
definition editing, and destructive graph expansion have different persistence
and clutter consequences.

## Honesty Classes

1. **Primitive:** one exact operation; nothing meaningful to dissolve.
2. **Transparent Compound:** the canonical graph is the implementation and may
   be inspected or dissolved exactly.
3. **Optimized Compound:** the canonical graph defines the result; a fused or
   specialized backend is accepted only through equivalence/tolerance tests.
4. **Structured Specialized Operation:** exposes truthful stages,
   documentation, diagnostics, and selected intermediates, but cannot claim a
   complete ordinary-node decomposition.

Automatic reverse engineering of arbitrary shaders into a clean canonical
graph is not a product promise.

## Canonical Graph Requirements

A dissolvable graph includes every material part of the promise:

- operation order, constants, and parameter mappings;
- channel extraction, relationships, and recombination;
- alpha association and component participation;
- color, transfer, reference, range, and invalid-value behavior;
- extent, coordinates, sampling, borders, and neighborhood support;
- mask formula position;
- reductions, feedback, iteration, randomness, state, and resources; and
- precision and equivalence tolerance for optimized implementations.

An educational approximation is not a valid Dissolve result.

## Channel-First Public Interfaces

Compounds expose only stable useful boundaries:

- Image or Channel input/output;
- expandable component or auxiliary Channel ports;
- Value and declared `Value/Channel` controls;
- Mask inputs with exact formula position;
- color/alpha/sampling policy where it changes the promise; and
- selected intermediate outputs.

Several required Channels may remain compact behind one Image port. The
definition still declares presence and registration requirements.

## Specialized Limits

Iterative, recursive, temporal, external-model, RAW, large FFT, denoise, and
alignment algorithms may have useful stages without a finite practical
primitive graph. Inspect should explain that boundary rather than presenting
“cannot dissolve” as a defect. Graph-explosion safeguards may recommend
Inspect instead of Dissolve for very large canonical graphs.

## Current Stack And Remaining Contract

Phase 5 already supplies executable exact compounds, embedded dependency
closure, stable public ports, Make Unique, Unpack, and unresolved-instance
behavior. It distinguishes transparent, optimized-equivalent, and opaque
specialized definitions.

The remaining work is the C8 interaction contract: exact Inspect UI, shared
definition editing, optional intermediates, parameter mappings, graph-explosion
policy, and representative case studies. R3 owns external comparison.

Recommended first case studies are one pointwise adjustment, Saturation,
Gaussian Blur, a masked adjustment, view/output finishing, and one operation
that must remain specialized.

## Related Authority

- `../../03-technical-contracts/compound-nodes/compound-node-contract-v1.md`
- `../../06-completed-work/phase-05-compound-nodes-and-connections/compound-nodes-completion-record-2026-07-16.md`
- `../../03-technical-contracts/program-goals-and-rules.md`
- `channels-values-images-and-output.md`
- `node-library-and-migration.md`
- `../../05-research/channel-system-plans/compound-node-behavior.md`
