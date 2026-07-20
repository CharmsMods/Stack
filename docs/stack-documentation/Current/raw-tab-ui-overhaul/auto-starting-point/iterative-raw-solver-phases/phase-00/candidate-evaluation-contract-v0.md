# Candidate Evaluation Contract v0

## Invariants

1. Candidate generation and evaluation never mutate the current project
   recipe through Phase 05.
2. Every render consumes an immutable recipe value object.
3. Every result is keyed to exact source, decode, input recipe, candidate,
   renderer, proxy, and feature identities.
4. A source/recipe/decode change cancels pending work and makes completed stale
   results ineligible.
5. A failed render returns a failed evaluation, not partially initialized
   features or an applied recipe.
6. Candidate preview and applied output are distinct states.
7. Full-resolution verification precedes production apply.

## Canonical Identity

```text
source_id = SHA256(original source bytes)
decode_id = hash(canonical decode-affecting fields, decoder/library version)
input_recipe_id = hash(canonical complete visible recipe)
candidate_recipe_id = hash(canonical candidate complete visible recipe)
render_id = hash(renderer/shader/pipeline version, stage)
proxy_id = hash(width, height, sampling, demosaic, crop, orientation)
feature_id = hash(feature-set version and per-feature settings)
budget_id = hash(mode, evaluation/time limits, deterministic seed)

evaluation_id = hash(source_id, decode_id, input_recipe_id,
                     candidate_recipe_id, render_id, proxy_id,
                     feature_id, budget_id)
```

Canonical floats use one declared binary/decimal representation. Arrays retain
order. Graph endpoints, disabled controls, ownership, masks, color targets,
and view-transform fields participate even when a particular feature ignores
them. Path, pointer, modification time, and UI labels are not content identity.

## Cache Layers

| Cache | Key | May contain |
| --- | --- | --- |
| Raw technical | `source_id + decode_id + raw_feature_version` | Metadata crosswalk, normalized samples, black/white, clip/noise/headroom diagnostics |
| Neutral scene | raw key plus neutral recipe/render/proxy identity | Oriented scene-linear proxy and stable masks proposed by an accepted feature version |
| Candidate render | complete `evaluation_id` without feature id when render bytes are reusable | Isolated stage render and render status |
| Candidate features | complete `evaluation_id` | Per-term values, uncertainty, masks/valid fractions, timing |
| Full verification | candidate recipe plus full-resolution proxy/render/feature identity | Final constraint and agreement record |

Cache lookup verifies the full key after any compact hash lookup. Partial
feature results cannot masquerade as a complete record. Failures may be cached
for the same immutable identity only when their cause is deterministic; cancel
and out-of-memory states are not permanent evidence.

## Evaluation Record

```yaml
schema: stack.iterative-raw-solver.candidate-evaluation
version: 0
evaluation_id:
identities: {}
parameter_vector: []
complete_visible_recipe: {}
parent_candidate_id:
proposal_reason:
stage:
resolution:
status: pending|complete|rejected|failed|canceled|stale
constraints:
  tier0: []
  tier1: []
terms:
  tier2: []
  tier3: []
  tier4: []
tie_breakers: []
valid_pixel_fraction:
uncertainty: []
render_runtime_ms:
feature_runtime_ms:
cache_hits: []
rejection_reason:
```

The total/rank, when later defined, never replaces individual terms.

## Cancellation And Concurrency

Each solve owns a monotonically increasing generation token and immutable
start identities. Workers check cancellation before decode, before/after a
render, between feature groups, and before publishing. Publication uses:

```text
if generation != active_generation: stale
if current source/decode/recipe != start identities: stale
else publish result to solve-owned result store
```

Changing source, decode-affecting settings, crop/orientation policy, or current
recipe cancels. Budget expiry stops proposal generation, but already-running
work may only publish if still current. UI teardown/shutdown cancels without
blocking on an unbounded GPU wait.

## No-Mutation Test

For every evaluation and cancellation path:

```text
before = canonical current recipe bytes + graph ownership + undo depth
run candidate work
after  = same capture
assert before == after
assert no project dirty transition caused by candidate evaluation
```

Render targets, analysis caches, and solve diagnostics may change. Project
recipe, graph, undo history, and save state may not.

## Determinism

Candidate enumeration has a stable order. Parallel completion order cannot
break ties. Any stochastic benchmark declares seed, generator, version, and
replay record; production acceptance still uses deterministic tie breaking.

## Final Apply Boundary

This contract designs but does not authorize apply. In Phase 06 only, a
full-resolution-verified winning complete recipe may be written atomically
after rechecking start identities. One Undo snapshot restores the exact input
recipe. Any failed recheck retains the current recipe and reports stale or
canceled.
