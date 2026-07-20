# Resume Protocol

Use this file as the operating procedure for every future documentation reorg
pass.

## Purpose

The goal is to let a later agent resume safely without:

- re-reading a file that is already fully classified
- re-verifying a file that has already been checked against code
- re-moving a file that was already moved or renamed
- losing track of what was completed in a prior pass

## Source Of Truth

Update these files in this order:

1. `batch-status.tsv`
2. `folder-status.tsv`
3. `file-inventory.tsv`
4. `move-log.tsv` when a move happens
5. `progress-log.md`

## Start Checklist

Before reading or moving any documentation:

1. Read `README.md`, this file, and `review-batches.md`.
2. Read `batch-status.tsv` and pick exactly one batch unless the task is
   explicitly about reconciliation only.
3. Read the matching rows in `folder-status.tsv`.
4. Read the matching rows in `file-inventory.tsv`.
5. If the batch is already `completed`, do not re-run it unless new files were
   added or a correction is needed.
6. If any file in the batch is already `moved`, check `move-log.tsv` before
   touching it again.

## Batch Rules

- Work one batch at a time.
- Set the chosen batch to `in-progress` before the first file review.
- Keep the batch `in-progress` until every file in that batch is either:
  `reviewed`, `verified`, `ready-to-move`, `moved`, `keep-in-place`, or
  `archived-in-place`.
- Set the batch to `completed` only when all file rows in that batch have a
  non-pending decision and the counts in `folder-status.tsv` agree.
- If work must stop mid-pass, leave accurate partial counts and set
  `next_action` in `batch-status.tsv`.

## Folder Rules

- A folder group row represents one mapped documentation source location.
- Mark a folder group `in-progress` when any file inside it is being reviewed.
- Mark it `completed` only when all files in that folder group have been
  classified and its counts match `file-inventory.tsv`.
- Do not delete a folder group row after moves. It remains the historical
  source location record.

## File Rules

Each file row must end in one of these statuses:

- `pending`: untouched
- `in-review`: currently being read or classified
- `reviewed`: read and classified, but code verification not needed or not done
- `verified`: checked against current code
- `ready-to-move`: destination decided and safe to move in a later pass
- `moved`: moved or renamed, with the result recorded in `move-log.tsv`
- `keep-in-place`: intentionally kept where it is
- `archived-in-place`: intentionally left in an existing archive location
- `blocked`: cannot be decided yet

Use `verify_against_code` like this:

- `yes`: verification is required
- `no`: verification is not needed
- `done`: verification happened
- `unknown`: not decided yet

Use `proposed_destination` like this:

- `Current`
- `Info`
- `Archived`
- `keep-in-place`
- `undecided`

Use the `notes` column in a short structured style:

`batch=B03; reviewed_on=2026-07-02; reason=active implementation plan`

Add `final_path=...` to `notes` after the final destination is known.

## Move Rules

- Never move a documentation file without updating its row in
  `file-inventory.tsv` first.
- Immediately append the move to `move-log.tsv`.
- If the move changes a folder from non-empty to empty, note that in
  `progress-log.md`, but do not remove the historical folder row from
  `folder-status.tsv`.
- If a move is partial, keep the batch `in-progress`.

## Progress Log Rules

Append one dated entry per work pass that states:

- what batch was touched
- what changed
- what remains
- whether any moves happened

## Current Baseline

As of `2026-07-02`, the tracker is at this state:

- all mapped documentation files were inventoried
- review batches were defined
- `B01` (`repo-root`) was reviewed and classified
- `B02` (`AGENT RULES`) was reviewed and classified
- `B03` (`docs`) was reviewed and classified
- `B04` (`docs/engineering` and `docs/engineering/architecture`) was reviewed and classified
- `B05` (`docs/engineering/mfsr`) was reviewed and classified
- `B06` (`docs/engineering/develop`) was reviewed, classified, and moved
- `B07` (`docs/engineering/develop/raw_manual_workspace` and `implementation_phases`) was reviewed, classified, and moved
- `B08` (`docs/engineering/develop/spec_sources` and `Stack_Develop_Node_Detailed_Guides`) was reviewed, classified, and moved
- `B09` (`docs/auto-raw-base`) was reviewed, classified, and moved
- `B10` (`docs/raw-tab-ui-overhaul`) was reviewed, classified, and moved
- `B11` (`docs/raw-tab-ui-overhaul/auto-starting-point` and `archive`) was reviewed and classified; the main packet was moved in that pass and the preserved Pass 0-8 ledger was rehomed into `Archived` in the following move pass
- `B12` (`website`) was reviewed and excluded as non-documentation product-site assets
- `B13` (`_local_archive` root docs and `_local_archive/Documentation`) was reviewed and classified
- `B14` (`_local_archive/Current Plan Files and Documentation In Use`) was reviewed and classified
- `B15` (`_local_archive/UI research`) was reviewed and classified
- `B16` (`_local_archive/Redefining Rendering`) was reviewed and classified
- `B17` (`_local_archive/docs/old_docs`) was reviewed and classified
- `B18` (`_local_archive/notes`) was reviewed and classified
- `AGENTS.md` is a keep-in-place repo instruction file. `README.md`, `BUILDING.md`, and `THIRD_PARTY_NOTICES.md` remain keep-in-place repo-root operational entrypoints and are also mirrored under `docs/stack-documentation/Info/repo-root/` so the documentation home contains the full non-instruction doc set
- the `AGENT RULES` folder is a second keep-in-place exception because it is an active private instruction bundle and not normal repository documentation
- the `website/` HTML files recorded in `non-documentation-candidates.tsv` are product assets, not repository documentation, and should stay out of the documentation move workflow unless the scope expands to website content
- the note-intake protocol now prefers placing routed ideas into the real documentation tree first, while `Current/ideas`, `Current/updates`, `Current/fixes`, `Info/research`, and `Info/questions` remain fallback destinations when no clear workstream home exists yet
- `_local_archive` is a mixed-content archive. Continue only with the mapped documentation candidate batches unless the scope expands to build artifacts, code snapshots, or landing-page assets
- `loading fixes findings.txt` is a verified archive candidate
- `docs/stack-documentation/Current/top-level/LOCAL_TONE_MAPPING_PLAN.md` is a current future-plan candidate with no implementation yet
- `docs/NEURAL_DENOISE_SYSTEM_PLAN.md` is a verified archive candidate because later code and docs supersede its pre-inference assumptions
- `docs/stack-documentation/Info/engineering/WINDOW_CHROME_POSTMORTEM.md` remains a current reference because the build docs still point to it and its guidance matches the current titlebar bridge/fallback code
- `docs/stack-documentation/Current/engineering/NODE_LIBRARY_MARKER_AUDIT.md`, `docs/stack-documentation/Current/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, and `docs/stack-documentation/Current/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md` remain current engineering guidance
- the `docs/stack-documentation/Current/engineering/mfsr` packet remains active current guidance because the repo still only implements MFSR Phase 1/2 contracts and the inert graph node shell, with the Phase 3 tab shell still pending
- the `docs/stack-documentation/Current/engineering/develop` bundle remains active current guidance because `AGENTS.md` and `DEVELOP_PASS_PROTOCOL.md` still treat it as the live implementation source of truth for Develop work, and the referenced tracker/source-map/decision files match current code organization
- within `docs/stack-documentation/Current/engineering/develop/raw_manual_workspace`, the top-level README/handoff/plan/smoke files plus `00_*`, Phase 7, Phase 8, and Phase 8A remain current because they still control the next RAW workspace validation and continuation steps, while completed milestone phases 1 through 6 now live under `docs/stack-documentation/Archived/engineering/develop/raw_manual_workspace/implementation_phases/`
- within `docs/stack-documentation/Current/engineering/develop/spec_sources`, `DEVELOP_NODE_CONTEXT.txt` remains the current-behavior truth and the split numbered guide set remains active future-direction guidance referenced by the protocol/tracker bundle, while `docs/stack-documentation/Archived/engineering/develop/spec_sources/Develop node first think through.txt` is the older monolithic predecessor to that guide set
- `docs/stack-documentation/Info/auto-raw-base/deep-research-report.md` and `docs/stack-documentation/Info/auto-raw-base/implementation-crosswalk.md` remain useful reference material, while the older Auto Raw Base packet now lives under `docs/stack-documentation/Archived/auto-raw-base/` because later raw-tab-ui-overhaul and starting-point docs superseded it as the active implementation set
- within `docs/stack-documentation/Current/raw-tab-ui-overhaul`, `implementation-contract.md`, `ui-validation-checklist.md`, and `pass-11-validation-notes.md` remain current because the shipped UI still follows that contract and the first-overhaul hands-on visual validation/continuation log is still open
- the `docs/stack-documentation/Current/raw-tab-ui-overhaul/auto-starting-point` folder is a current keep-together bundle because `AGENTS.md` hardcodes its entry docs, `implementation-progress.md` is the active state tracker, and the reread guide explicitly routes future work into the companion contract, readiness, workflow, and task-specific research files; the preserved Pass 0-8 ledger now lives under `docs/stack-documentation/Archived/raw-tab-ui-overhaul/auto-starting-point/`
- the first executed move set rehomed `loading fixes findings.txt` plus the reviewed `Info`/`Archived` top-level `docs` files into `docs/stack-documentation/`, and `BUILDING.md` was updated to the new release-doc path
- the second executed move set rehomed the reviewed Auto Raw Base research files into `docs/stack-documentation/Info/auto-raw-base/`, moved the older Auto Raw Base packet into `docs/stack-documentation/Archived/auto-raw-base/`, and updated `docs/raw-tab-ui-overhaul/README.md` plus `implementation-contract.md` to the new paths
- the third executed move set rehomed the reviewed `Info` and `Archived` subset of the older RAW overhaul packet into `docs/stack-documentation/Info/raw-tab-ui-overhaul/` and `docs/stack-documentation/Archived/raw-tab-ui-overhaul/`, while leaving `implementation-contract.md`, `ui-validation-checklist.md`, and the actively updated `pass-11-validation-notes.md` in place as the still-live current subset
- the fourth executed move set rehomed `docs/LOCAL_TONE_MAPPING_PLAN.md` into `docs/stack-documentation/Current/top-level/`, moved `docs/engineering/WINDOW_CHROME_POSTMORTEM.md` into `docs/stack-documentation/Info/engineering/`, and updated `BUILDING.md` to the new postmortem path
- the fifth executed move set rehomed the active MFSR packet into `docs/stack-documentation/Current/engineering/mfsr/` and updated the node-audit and MFSR goal/plan references to the new path
- the sixth executed move set rehomed `docs/engineering/NODE_LIBRARY_MARKER_AUDIT.md` into `docs/stack-documentation/Current/engineering/`, leaving only the architecture pair in the original engineering branch
- the seventh executed move set rehomed the architecture guide pair into `docs/stack-documentation/Current/engineering/architecture/` and updated the active Develop tracker bundle to point at the new architecture-doc paths
- the eighth executed move set rehomed the active Develop protocol bundle into `docs/stack-documentation/Current/engineering/develop/`, moved the current Develop spec-source bundle into `docs/stack-documentation/Current/engineering/develop/spec_sources/`, moved the older monolithic Develop planning note into `docs/stack-documentation/Archived/engineering/develop/spec_sources/`, and updated `AGENTS.md` plus the moved bundle's internal path references together
- the ninth executed move set rehomed the active RAW manual workspace packet into `docs/stack-documentation/Current/engineering/develop/raw_manual_workspace/`, moved completed milestone phases `01` through `06` into `docs/stack-documentation/Archived/engineering/develop/raw_manual_workspace/implementation_phases/`, updated the current packet to point at those archived milestone docs, and removed the now-empty original `docs/engineering/develop/` source branch
- the tenth executed move set rehomed the active RAW tab UI contract/checklist/validation trio into `docs/stack-documentation/Current/raw-tab-ui-overhaul/` and updated the already-moved overhaul background/history docs so their current-doc references now point at the new `Current` location
- the eleventh executed move set rehomed the main Starting Point packet into `docs/stack-documentation/Current/raw-tab-ui-overhaul/auto-starting-point/` and updated `AGENTS.md` plus the live packet's explicit path references
- the twelfth executed move set rehomed the preserved Starting Point Pass 0-8 ledger into `docs/stack-documentation/Archived/raw-tab-ui-overhaul/auto-starting-point/`, updated the active `implementation-progress.md` ledger to point at the new archive path, and removed the now-empty original `docs/raw-tab-ui-overhaul/` source branch
- the thirteenth executed mirror set copied `README.md`, `BUILDING.md`, and `THIRD_PARTY_NOTICES.md` into `docs/stack-documentation/Info/repo-root/` so the consolidated documentation home contains the full non-instruction doc set while the repo-root operational entrypoints remain intact
- the fourteenth tracker pass reviewed the `website/` HTML files and recorded them in `non-documentation-candidates.tsv` as excluded product-site assets so future documentation passes do not re-audit them
- the fifteenth tracker pass updated the idea-intake routing rule so routed notes prefer the real documentation tree first and only use the type-first intake buckets as fallback destinations
- the sixteenth tracker pass mapped `_local_archive` into batches `B13` through `B18`, reviewed the root plus `Documentation` pair as `B13`, and left the remaining `_local_archive` packets pending for later incremental review
- the seventeenth tracker pass reviewed `_local_archive/Current Plan Files and Documentation In Use` as `B14`, classifying its render-foundation progress tracker as `Current`, most of the packet as `Info`, and the exported prompt/chat artifacts as `Archived`
- the eighteenth tracker pass reviewed `_local_archive/UI research` as `B15`, classifying the full six-file packet as `Info`, and reviewed `_local_archive/Redefining Rendering` as `B16`, classifying the entire five-file redesign spec packet as `Current` because the active render-foundation tracker still references it as source-of-truth
- the nineteenth tracker pass reviewed `_local_archive/docs/old_docs` as `B17`, keeping the graph-interaction guide and cleanup roadmap as `Current`, the node-library audit packet as `Info`, and the demosaicing Q&A transcript as `Archived`; it also reviewed `_local_archive/notes/very future stuff.txt` as `B18` and classified it as archival scratch material
- the fourteenth executed move set rehomed the reviewed `_local_archive` documentation into `docs/stack-documentation/`, grouping current render/editor packets under `Current/engineering/`, research and audits under `Info/engineering/`, and historical prompt/scratch material under `Archived/engineering/` and `Archived/top-level/`

