# Node Math Rewrite Research Index

- Updated: 2026-07-16
- Purpose: route future agents to the smallest sufficient evidence set without
  turning the operation catalogs into mandatory cold-start reading

## Evidence Classes

| Class | Meaning |
| --- | --- |
| Ground truth | Evidence about the dated Stack working tree. Re-check relevant code before implementation. |
| Foundational research | Architecture or contract material needed across multiple phases. Still a proposal until accepted in the decision register. |
| Phase-specific research | Required only when its information scope or subsystem becomes active. |
| Operation reference | Formula/algorithm encyclopedia consulted for a selected definition, not a roadmap. |
| Historical source | User intent, earlier prompts, or superseded framing; not current-code authority. |

## Foundational Read Set

Read these for architecture or program-direction work:

1. `STACK_IMAGE_MATH_AND_PIPELINE_AUDIT as of 7-12-26.md`
   - Ground truth for the audited tree.
   - Highest-value sections: 1, 3–5, 8–15, 17, and 19–22.
   - Read the full audit before a broad architecture decision.
2. `Stack_Image_Math_Research_Package/00_README.md`
   - Package orientation, terms, scope codes, and research intent.
3. `Stack_Image_Math_Research_Package/16_Current_Stack_Implementation_Mapping.md`
   - Best bridge from the audit to the proposed transition boundaries.
4. `Stack_Image_Math_Research_Package/12_Proposed_Image_Buffer_Contract.md`
   - Central logical type/semantic descriptor/resource/view proposal.
   - Product defaults inside it remain open decisions.
5. `Stack_Image_Math_Research_Package/05_Alpha_Masks_and_Compositing.md`
   - Foundational despite living among operation references; separates blend,
     coverage, alpha association, and normative source-over.
6. `Stack_Image_Math_Research_Package/14_Composition_and_Validation_Rules.md`
   - Diagnostic levels, explicit-conversion policy, descriptor propagation,
     validation stages, and compatibility proposal.
7. `Stack_Image_Math_Research_Package/17_Composable_Node_Architecture_Research.md`
   - Authored graph, semantic IR, physical plan, fusion, compound model, and
     phase recommendation.
8. `Stack_Image_Math_Research_Package/18_Research_Sources.md`
   - Source ledger; consult the actual primary source when a decision depends
     on it.

## Phase-Specific Routes

| Phase or question | Required research | Use |
| --- | --- | --- |
| Phase 0 forward tests | Audit sections 7–8, 14–15, and 22; files 01, 14, 16, and 17 testing/fusion sections | Choose generated numeric cases, CPU/GPU reference strategy, order-sensitive checks, and instrumentation without preserving old output. |
| Phase 1 descriptor and definition schema | Files 12, 14, 16, 17, and 18; file 05 for alpha | Resolve minimal canonical contract and stable identity/version rules. |
| Phase 2 color/view/output | Files 03, 04, 10, 12, 14, 16, 18 | Implement user-confirmed non-converting source labels, direct graph-output viewport display with footer color state, explicit graph color transforms, export, and metadata policy. |
| Phase 2 alpha/compositing | Files 05, 12, 14, 16, 18 | Independent channels, explicit straight/premultiplied state, blend/composite separation, and generated reference cases. |
| Phase 3 value types | Files 01 and 02 purpose/contracts/recommendations; files 08–10 type recommendations; files 12, 14, 17 | Select the smallest useful value set and typed broadcasts. |
| Phase 3 unified browser/definitions | File 13 plus files 16 and 17 | Definition metadata, taxonomy, aliases, tags, parameters, and UI schema. |
| Phase 4 IR/fusion/resources | Files 01, 02, 14, 16, 17, 18 | Expression semantics, numerical rules, fusion barriers, cache/resources, and evidence. |
| Phase 5 compounds | Files 15, 16, 17, 18 | Canonical decompositions, definition/instance/forward-version behavior, and equivalence; RAW remains opaque by default. |
| Phase 6 neighborhood | File 06 plus files 14, 17, 18 | Halo, border, kernel, separability, ROI, precision, and tiling. |
| Phase 6 geometry | File 07 plus files 12, 14, 17, 18 | Coordinates, windows, extent, reconstruction, and sampling. |
| Phase 6 reductions/auto | File 08 plus files 14, 15, 17, 18 | Measurement→decision→application and typed reduction results. |
| Phase 6 multi-image/temporal | File 09 plus files 14, 17, 18 | Collections, alignment, frames, flow/depth/transform types, and dependencies. |
| Phase 6 RAW/calibration | File 10 plus audit RAW sections and files 12, 14, 16, 18 | Metadata lifetime, specialized boundaries, and resulting image semantics. |
| Model/external boundary | File 11 | Scope exclusion and strict model/runtime identity; not an initial implementation target. |
| Phase 7 operation selection | Selected rows in files 01–11 and decompositions in 15 | Exact definition and references for a product-selected node only. |

## Reference-Only Material

The canonical operation tables in files 01–11 and most examples in file 15
are valuable references but low-priority for cold-start reading. Use them when a
specific operation is selected. They are not an implied backlog.

For files 01–04 and 06–10, the Purpose, contract/ambiguity notes, recommended
treatment, and Stack-specific consequence sections are more foundational than
the repeated operation rows.

## Historical Root Files

| File | Classification | Use |
| --- | --- | --- |
| `Conversation with ChatGPT context.txt` | Historical source; strong user-intent handoff | Product philosophy, terminology, permissive graph, wire/view labels, and original open questions. |
| `Long prompt, beginning the process of research.txt` | Historical source | Original math-Legos intent, balance between transparency and UX, taxonomy request, and conventional/non-AI boundary. |
| `Codex_Stack_Architecture_and_Image_Pipeline_Audit_Prompt.txt` | Historical audit instructions | Explains the audit's scope and evidence standard; the returned audit supersedes it for findings. |
| `Full Operation Tables.txt` | Early repetitive operation reference | Use only to trace earlier terminology; files 01–11 are the stronger organized references. |

Do not delete or silently rewrite these source files. If they are later moved to
an archive folder, preserve paths through a documented move and update all
references together.

## Research Proposals That Are Not Yet Decisions

Future work must not assume these are already accepted:

- any mandatory graph-wide working color space or normalized color state; user
  direction explicitly rejects that restriction;
- any automatic source-profile conversion or guessed untagged-image identity;
  embedded profiles are descriptive and untagged sources remain `Unknown`;
- any separate viewport preview transform; the viewport directly displays the
  connected graph output and reports its current color state;
- ICC, OCIO, or a hybrid color engine;
- premultiplied linear light as Stack's universal internal association; user
  direction requires both alpha states and independent channels;
- the maximal value-type and descriptor lists in files 12, 14, and 17;
- semantic mismatch as a hard connection error; user direction establishes
  permissive informational diagnostics except for structural impossibility;
- pre-rewrite project migration or historical-output preservation; user
  direction explicitly places those outside this program;
- UUID representation, semantic-version policy details, dependency embedding,
  or library update rules;
- taxonomy family names, colors, and public browser layout; and
- any proposed operation stable ID or ambiguous friendly-node formula.

These are routed through `decision-register.md`.

## Update Ownership

| Change | Update here |
| --- | --- |
| Current code behavior changes materially | Create a new dated audit or audit delta; then update file 16's mapping. Do not rewrite the old dated audit. |
| A product choice closes | `decision-register.md`, plus the canonical contract it creates. |
| A phase starts or finishes | `implementation-progress.md`. |
| Phase dependency/order changes | `phase-roadmap.md`. |
| A research file becomes foundational or stale | `research-index.md`. |
| A node is implemented/corrected | Its selected operation reference status, exact formula, forward definition version, test ID, implementation mapping, and user-review status. |
| A code owner moves | `code-source-map.md` after code inspection. |

Avoid copying accepted schemas into several research documents. When Phase 1
creates canonical contracts, those shorter accepted documents become authority;
the research package remains the rationale and reference library.

Phase 1 accepted `phase-1-contract-v1.md` as that authority on 2026-07-16.
Phase 3 accepted `src/NodeMath/FirstClassValue.*` and
`src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.*` as the compiled value
and live-definition authorities on 2026-07-16; the broader value lists in the
research package remain reference material rather than an automatic public
node backlog.

Phase 4 accepted NMR-109 and `phase-4-ir-contract-v1.md` as the authority for
the first bounded pointwise IR, ordering, fusion/materialization barriers,
precision, program limits/cache, and graph-resource policy on 2026-07-16.
`src/NodeMath/PointwiseIR.*` is the compiled contract; later IR expansion still
requires a separately selected definition and evidence rather than inheriting
the full research operation tables.
