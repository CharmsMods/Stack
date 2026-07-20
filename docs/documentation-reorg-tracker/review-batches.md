# Review Batches

These batches are meant to keep the documentation reorganization safe and
incremental.

`review-batches.md` defines the batches. `batch-status.tsv` is the live status
ledger that should be updated during real work.

## Status Key

- `pending`: not reviewed yet
- `in-progress`: currently being checked
- `reviewed`: read and classified
- `verified`: checked against code where needed
- `moved`: placed into the final destination structure

| Batch | Files | Scope | Status | Notes |
| --- | ---: | --- | --- | --- |
| `B01` | 5 | `repo-root` | `completed` | Root convention/legal docs were kept in place and mirrored into `Info/repo-root/`. `loading fixes findings.txt` was classified as an archive candidate and moved. |
| `B02` | 9 | `AGENT RULES` | `completed` | Classified the whole folder as an active keep-in-place instruction bundle rather than normal repo documentation. |
| `B03` | 9 | `docs` | `completed` | Classified one active future-plan doc, seven current reference docs, and one superseded neural-denoise plan. |
| `B04` | 4 | `docs/engineering` and `docs/engineering/architecture` | `completed` | Classified three active current engineering/architecture guidance docs and one stable reference postmortem. |
| `B05` | 8 | `docs/engineering/mfsr` | `completed` | Classified the whole MFSR packet as `Current`; the docs still match the code state with Phase 2 node shell complete and Phase 3 tab-shell work still next. |
| `B06` | 5 | `docs/engineering/develop` | `completed` | Classified the whole develop protocol/tracker bundle as `Current`; it remains the live workflow source of truth and is referenced directly by `AGENTS.md`. |
| `B07` | 17 | `docs/engineering/develop/raw_manual_workspace` and `implementation_phases` | `completed` | Classified the top-level workspace packet plus `00_*`, `07`, `08`, and `08A` as `Current`, and the completed Phase 1 through Phase 6 milestone docs as `Archived`. |
| `B08` | 13 | `docs/engineering/develop/spec_sources` and `Stack_Develop_Node_Detailed_Guides` | `completed` | Classified `DEVELOP_NODE_CONTEXT.txt` and the active detailed guide set as `Current`, and the older monolithic `Develop node first think through.txt` file as `Archived`. |
| `B09` | 8 | `docs/auto-raw-base` | `completed` | Classified the research report and crosswalk as `Info`, and the older Auto Base packet README/pass docs as `Archived` completed or superseded implementation guidance. |
| `B10` | 11 | `docs/raw-tab-ui-overhaul` | `completed` | Classified the active UI contract/checklist/validation log as `Current`, the broader redesign background docs as `Info`, and the outdated pre-overhaul map plus resolved pass-plan docs as `Archived`. |
| `B11` | 14 | `docs/raw-tab-ui-overhaul/auto-starting-point` and its `archive` subfolder | `completed` | Classified the main auto-starting-point packet as a current keep-together protocol/research bundle, and placed the preserved Pass 0-8 ledger under `Archived`. |
| `B12` | 3 | `website` | `completed` | Reviewed the website HTML files and excluded them from the documentation workflow because they are product site assets, not repository docs. |
| `B13` | 7 | `_local_archive` root docs and `_local_archive/Documentation` | `completed` | Classified two active unification/compositor direction docs as `Current`, three architecture/research references as `Info`, and two older composite-integration notes as `Archived`. |
| `B14` | 8 | `_local_archive/Current Plan Files and Documentation In Use` | `completed` | Classified the render-foundation progress tracker as `Current`, the Composite/render/bundler/glass-sphere docs as `Info`, and the exported prompt/chat artifacts as `Archived`. |
| `B15` | 6 | `_local_archive/UI research` | `completed` | Classified the six UI structure, organization, naming, portability, UX, and architecture audits as `Info` reference material. |
| `B16` | 5 | `_local_archive/Redefining Rendering` | `completed` | Classified the full rendering redesign specification packet as `Current` because the active render-foundation tracker still treats it as source-of-truth input. |
| `B17` | 6 | `_local_archive/docs/old_docs` | `completed` | Classified the graph-interaction guide and cleanup roadmap as `Current`, the node-library audits as `Info`, and the demosaicing Q&A transcript as `Archived`. |
| `B18` | 1 | `_local_archive/notes` | `completed` | Classified the loose future-note capture as `Archived` scratch/source material. |

## Suggested Order

1. `B01`
2. `B03`
3. `B04`
4. `B02`
5. `B09`
6. `B05`
7. `B06`
8. `B08`
9. `B07`
10. `B10`
11. `B11`
12. `B12`
13. `B13`
14. `B14`
15. `B15`
16. `B16`
17. `B17`
18. `B18`
