# Phase 04 Optimizer Source Extraction

- Extraction version: `optimizer-source-extraction-v1`
- Stack decision scope: optimizer mechanics and benchmark design only
- Production recipe integration: prohibited
- Reviewed: 2026-07-10

## Stack Problem Being Solved

Stack's candidate evaluator is a bounded, derivative-free, expensive black
box. Its output is not one smooth scalar. It is an ordered record containing
identity/state constraints, RAW-artifact constraints, technical and display
goals, uncertainty, failures, and tie-breakers. Candidate values include
continuous controls, discrete sampled controls, ordered graph ordinates, and
declared cross-stage interactions.

The optimizer therefore has to preserve lexicographic feasibility, use exact
candidate caching, survive rejected/missing/failed evaluations, retain the
Pass 94 warm candidate, and stop under both time and evaluation budgets. An
algorithm paper's scalar smooth-objective convergence theorem does not by
itself establish those Stack-specific properties.

## Primary Sources And Extraction

### Pattern search

Virginia Torczon's [On the Convergence of Pattern Search
Algorithms](https://epubs.siam.org/doi/abs/10.1137/S1052623493250780)
defines a broad derivative-free pattern-search family. Its scaled, translated
integer-lattice structure supports deterministic poll steps and convergence
analysis without explicit derivatives or an explicit gradient approximation.

Stack extraction:

- Use a deterministic ordered poll, not a random direction set.
- Normalize control spans so a radius has comparable meaning across units.
- Treat the visible control ownership order as part of the poll definition.
- Preserve exact accepted candidate records and shrink after no meaningful
  accepted step.
- For explicitly discrete dimensions, poll the adjacent discrete value rather
  than letting a continuous radius snap repeatedly to the incumbent.

Scope difference: Stack uses bounded mixed variables and a lexicographic
record, so Phase 04 claims deterministic bounded-search behavior and measured
benchmark success, not Torczon's unconstrained asymptotic theorem.

Kolda, Lewis, and Torczon's [Stationarity Results for Generating Set Search for
Linearly Constrained
Optimization](https://epubs.siam.org/doi/10.1137/S1052623403433638)
connects feasible generating directions, constrained geometry, step length,
and stationarity measures.

Stack extraction:

- Poll directions must conform to bounds and graph invariants.
- Radius/step size is an explicit convergence signal, but a small step alone
  does not prove photographic optimality.
- Constraint-boundary behavior needs its own recorded stop/shrink rule.
- A stopped search reports its runtime state; budget exhaustion is not renamed
  convergence.

### Model-based derivative-free trust regions

Powell's [On the convergence of trust region algorithms for unconstrained
minimization without
derivatives](https://optimization-online.org/2011/01/2882/) analyzes linear or
quadratic interpolation models, trust-region steps, and interpolation-set
geometry. Quadratic models can converge faster, but require maintained model
information and assumptions that do not directly cover rejected or
lexicographic Stack evaluations.

Ragonneau and Zhang's [PDFO: A Cross-Platform Package for Powell's
Derivative-Free Optimization Solvers](https://arxiv.org/abs/2302.13246)
summarizes Powell's COBYLA, UOBYQA, NEWUOA, BOBYQA, and LINCOA family and
reports robustness work for noise, ill-conditioning, and failed function
evaluations. BOBYQA is the relevant bound-constrained reference family.

Stack extraction:

- Include a bounded model-based comparator because smooth low-dimensional
  surfaces may benefit from interpolation.
- Build a deliberately small diagonal quadratic model around one active merit
  component; never fit a fabricated combined photo score.
- Keep trust-ratio, predicted reduction, interpolation sites, and radius
  changes in the trace.
- Do not call the comparator BOBYQA or PDFO: Phase 04 does not reproduce their
  interpolation geometry, update rules, or convergence guarantees.
- Defer a production model-based dependency until measured Stack surfaces show
  enough benefit to justify geometry management and failure recovery.

## Licensing And Dependency Decision

The selected implementation is original Stack C++ mechanics informed by the
algorithm literature. No paper code, PDFO package code, Fortran solver, or new
third-party optimizer library is copied or linked. Consequently Phase 04 adds
no optimizer runtime dependency and no new third-party license obligation.

## Benchmark Consequence

The required comparison is:

1. Pass 94 warm start with no search.
2. Deterministic stage-ordered coordinate/pattern search with declared pair
   interactions.
3. A bounded diagonal-quadratic trust-region comparator.

Bayesian optimization and CMA-ES remain deferred because the frozen Phase 03
cost, dimensional ownership, exact repeatability, and two-source engineering
surface set do not justify stochastic or population-search complexity.

## Decision

Select `stage-ordered-pattern-search-v1` for the first precise solver dry run.
It passed all controlled failure/state cases, restored the frozen ARW's hard
RAW feasibility in two unique evaluations, retained the already-safe DNG warm
start, and remained exactly deterministic. The model-based comparator also
passed those two frozen cases, but delivered no measured real-surface advantage
that justified its additional model assumptions.

This is a local bounded-search engineering decision. It is not a claim that
pattern search is globally superior, and it does not authorize Phase 05.
