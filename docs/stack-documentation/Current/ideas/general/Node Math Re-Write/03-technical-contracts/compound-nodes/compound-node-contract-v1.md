# Phase 5 Executable Compound Contract v1

- Status: implemented and verified
- Accepted: 2026-07-16
- Decision: NMR-110
- Scope: Phase 5 only

## Product Result

A compound is an executable, versioned node definition with a deliberate
public interface around a canonical internal graph. It is not a visual group
and it is not a copied preset. Several instances can refer to the same exact
definition while retaining their own connections, parameter overrides, and
layout.

The canonical internal graph defines the order and meaning of the work. Stack
may flatten and fuse that graph for execution, but optimization does not change
the saved compound boundary or the authored operation order.

## Exact Identity And Interface

- Every compound definition is identified by definition ID, exact semantic
  version, and SHA-256 content identity.
- Every authored node instance has a canonical UUID. Copy/paste creates new
  instance UUIDs while preserving exact definition references.
- Compound input ports, output ports, and promoted parameters use stable IDs.
  Labels are presentation and may change without breaking connections.
- Internal definition nodes also have stable UUIDs. Interface bindings and
  promoted controls target those UUIDs plus stable socket/parameter IDs.
- An instance stores an interface snapshot. If its definition is missing, the
  unresolved node and its existing typed connections remain visible instead of
  being replaced or guessed.

## Storage And Portability — NMR-110

Stack uses exact, self-contained project embedding for Phase 5:

1. A saved project embeds the exact transitive compound-definition dependency
   closure used by its instances.
2. A copied selection or graph preset carries the same exact closure, so paste
   does not depend on a separate machine-wide asset library.
3. Shipped compound templates seed definitions into a project when added. Once
   used, the project carries the exact definition it authored against.
4. Version ranges and automatic “latest compatible” resolution are not used.
   An instance remains pinned to its saved ID/version/hash.
5. If an exact dependency is absent, hash-mismatched, invalid, or recursive,
   the instance is unresolved. Stack does not substitute another definition.
6. Direct and indirect recursive compound dependencies are rejected. Ordinary
   acyclic nesting is allowed.

This deliberately rejects a shared-library-only model because that would make
project results depend on external mutable state. A future library can provide
discovery and explicit update offers without changing this project contract.

## Definition Classes

- **Transparent graph:** the canonical graph is the implementation and can be
  inspected or unpacked.
- **Graph-defined, optimized-equivalent:** the canonical graph defines the
  result; an optimized implementation is allowed only within its recorded
  tolerance and evidence.
- **Opaque specialized:** the real algorithm boundary is documented but is not
  represented as an editable graph. RAW nodes are in this class by default and
  do not offer automatic Unpack.

## Authoring And Lifecycle

- Creating a compound from a selection captures the internal graph, its exact
  compound dependencies, boundary ports, and deliberately promoted controls.
- Editing definition content creates an explicitly versioned definition. It
  does not mutate instances pinned to an earlier exact version.
- Updating an instance is an explicit rebind to another embedded exact
  definition after its interface is checked. No project-load update occurs.
- **Make Unique** clones the exact definition under a new stable definition ID
  and resets that private definition to version `1.0.0`; the selected instance
  is rebound without changing its external port IDs.
- **Unpack** replaces one transparent or optimized-equivalent instance with a
  fresh copy of its canonical internal nodes, applies promoted overrides, and
  rewires external connections by stable port IDs. It is an authoring action,
  not the normal compiler flattening path.
- Graphical groups remain independent layout objects and never become an
  execution or version boundary.

Semantic-version behavior follows the Phase 1 contract: patch changes stay
inside the promised result/tolerance, minor changes are additive with
result-preserving defaults, and output/interface/ordering changes are major.
Pixel-changing “bug fixes” are therefore not silently published as patches.

## Execution And Equivalence

Resolved graph-defined compounds are flattened into a temporary execution copy
before the renderer snapshot is built. The expansion result records the outer
compound instance and ordered internal node identities for diagnostics and
tests; the renderer snapshot uses those ordered internal identities. The
authored graph and saved definition are not destructively expanded.

Pointwise internal chains may use the Phase 4 IR and fusion path. A compound
classified as optimized-equivalent must have generated canonical-versus-
optimized evidence at its declared tolerance. Unsupported, unresolved, opaque,
or invalid definitions do not lower through this path.

## Phase Boundary

This contract does not authorize ROI/halo planning, neighborhood infrastructure,
general reductions, specialized-stage rewrites, or broad public node-library
expansion. Those remain Phase 6 or Phase 7 work.
