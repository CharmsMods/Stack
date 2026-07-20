# Progress Log

## 2026-07-02

- Created the documentation reorganization tracker workspace.
- Mapped `103` documentation files across `15` source groups.
- Defined review batches `B01` through `B11`.
- Added live ledgers for batch status, folder status, and future move history.
- No documentation content has been reviewed in detail yet.
- No documentation files have been moved or renamed yet.

## 2026-07-02 - B01

- Reviewed batch `B01` (`repo-root`).
- Classified `AGENTS.md`, `README.md`, `BUILDING.md`, and `THIRD_PARTY_NOTICES.md` as intentional keep-in-place root documents.
- Verified `BUILDING.md` against the current build scripts, version workflow, release output paths, and environment helper scripts.
- Verified `THIRD_PARTY_NOTICES.md` against the current CMake and packaging/release copy paths.
- Verified that `loading fixes findings.txt` is not referenced elsewhere, uses stale line references, and is superseded by newer RAW workspace handoff/current-state documentation.
- Marked `loading fixes findings.txt` as `Archived` and `ready-to-move`.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B02

- Reviewed batch `B02` (`AGENT RULES`).
- Used both content and file dates to evaluate whether this folder behaves like ordinary Stack documentation or like an active private instruction bundle.
- Classified the entire `AGENT RULES` folder as `keep-in-place`.
- Verified that `program-preset.txt` explicitly describes the bundle as a private operating specification, not public documentation, and not tied to a specific repository.
- Verified that `07-update-guide.txt` and `program-preset.txt` cross-reference the same companion bundle structure, which would be weakened by splitting the files into normal documentation categories.
- Verified that `08-debugging-watchlist.txt` contains still-relevant Stack-specific debugging history, but kept it with the preset bundle because the folder is designed to function as one active instruction set.
- Confirmed that `AGENTS.md` does not redirect into this folder and that the folder is best treated as a second keep-in-place exception rather than normal project docs.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B03

- Reviewed batch `B03` (`docs` top-level standalone docs).
- Used both content and file dates to separate active future plans from current technical references and superseded plans.
- Classified `docs/LOCAL_TONE_MAPPING_PLAN.md` as `Current` because it is still an open future implementation idea and no implementation or superseding doc references were found.
- Classified `docs/INSTALLER_UPDATER_RELEASES.md`, `docs/RAW_ARW_PIPELINE_PLAN.md`, `docs/RAW_DNG_SUPPORT_PLAN.md`, `docs/RAW_LIBRAW_PRODUCTION_AUDIT.md`, `docs/RAW_NODE_TECHNICAL_NOTES.md`, `docs/RESTORMER_EXPORT_WORKFLOW.md`, and `docs/RESTORMER_ONNX_INTEGRATION.md` as `Info`.
- Verified `docs/INSTALLER_UPDATER_RELEASES.md` against the current release scripts, updater code, installer marker, release output layout, and website download behavior.
- Verified the RAW/LibRaw docs against the current decoder, metadata, and GPU pipeline code, including linear DNG handling.
- Verified the Restormer docs against the current neural denoise tooling, ONNX backend, model manifest handling, and manual `Run Denoise` / `Refresh Denoise` workflow.
- Classified `docs/NEURAL_DENOISE_SYSTEM_PLAN.md` as `Archived` because it assumes a pre-inference state that current RGB neural-denoise code and later Restormer docs have already moved beyond.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B04

- Reviewed batch `B04` (`docs/engineering` and `docs/engineering/architecture`).
- Used both content and late-June 2026 file dates to distinguish active standing guidance from background reference material.
- Classified `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, and `docs/engineering/NODE_LIBRARY_MARKER_AUDIT.md` as `Current`.
- Classified `docs/engineering/WINDOW_CHROME_POSTMORTEM.md` as `Info`.
- Verified the window chrome postmortem against the current AppWindow titlebar bridge, build flags, trace flags, and the existing link from `BUILDING.md`.
- Verified the node library marker audit against the current layer registry, MFSR placeholder contract, RAW/CFA neural denoise pass-through state, and the visible lifecycle-marker code.
- Verified the architecture docs against the current file layout and implementation tracker references; the hotspot and guide docs are still being used as live organization guidance.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B09

- Reviewed batch `B09` (`docs/auto-raw-base`).
- Used file dates, current code, and the newer `docs/raw-tab-ui-overhaul` and `docs/raw-tab-ui-overhaul/auto-starting-point` docs to separate still-useful reference material from older completed implementation-phase guidance.
- Classified `docs/auto-raw-base/deep-research-report.md` and `docs/auto-raw-base/implementation-crosswalk.md` as `Info`.
- Classified `docs/auto-raw-base/README.md` and `pass-01` through `pass-05` as `Archived`.
- Verified that current code already contains the main `RawImageAnalysis`, `RawAutoBase`, `RawAutoStartPoint`, and RAW workspace Auto Base/Starting Point modules, so this folder is no longer the active implementation packet.
- Verified that later RAW overhaul docs still cite parts of `docs/auto-raw-base` as technical background, which supports keeping the research/crosswalk material but archiving the completed/superseded pass docs.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B05

- Reviewed batch `B05` (`docs/engineering/mfsr`).
- Used both the MFSR packet docs and the current repo state to determine whether this folder is still an active implementation bundle or an older completed plan set.
- Classified the entire `docs/engineering/mfsr` packet as `Current`.
- Verified that `src/MFSR/MFSRTypes.h/.cpp`, MFSR graph-node wiring, serializer support, renderer placeholder pass-through behavior, and focused graph behavior tests all exist and match the packet's claimed Phase 1/2 state.
- Verified that no dedicated MFSR tab shell implementation was found beyond placeholder node/sidebar status UI, so the docs' `Phase 2 complete / ready for Phase 3` position still matches the codebase.
- Noted that `MFSR_STATUS.md` references a local `build_codex_verify` directory in its last verified command, which should be refreshed when MFSR work resumes, but this does not change the packet's overall current status.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B06

- Reviewed batch `B06` (`docs/engineering/develop`).
- Followed the repo instruction to start with `DEVELOP_PASS_PROTOCOL.md` and used it to confirm that this bundle still defines the live continuation rules for Develop work.
- Classified `DEVELOP_PASS_PROTOCOL.md`, `DEVELOP_IMPLEMENTATION_TRACKER.md`, `DEVELOP_SOURCE_MAP.md`, `DEVELOP_DECISIONS.md`, and `DEVELOP_DEFERRED_SCOPE.md` as `Current`.
- Verified that `AGENTS.md` still points directly to `DEVELOP_PASS_PROTOCOL.md`, that the protocol still names the other four files as active sources of truth, and that the referenced companion spec files exist.
- Verified that the implementation tracker's latest handoff entries and the source map's recent organization notes match real current source files such as `RenderPipelineGraphRawStages.cpp`, `RenderPipelineGraphRawDevelopNode.cpp`, `RenderPipelineGraphLayerNode.cpp`, `RenderPipelineGraphDataMathNode.cpp`, `RenderPipelineGraphRawDetailNode.cpp`, and `RenderPipelineGraphLutNode.cpp`.
- Noted that this bundle is path-sensitive because `AGENTS.md` and the protocol hardcode its current location, so any future move should update those references together rather than treating the files as free-floating docs.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B08

- Reviewed batch `B08` (`docs/engineering/develop/spec_sources` and `Stack_Develop_Node_Detailed_Guides`).
- Used `DEVELOP_PASS_PROTOCOL.md` and the companion develop bundle to separate current-behavior truth from future-direction guide material and from the older pre-split planning document.
- Classified `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt` as `Current`.
- Classified `docs/engineering/develop/spec_sources/Stack_Develop_Node_Detailed_Guides/00_INDEX_README.txt` and guides `01` through `10` as `Current`.
- Classified `docs/engineering/develop/spec_sources/Develop node first think through.txt` as `Archived`.
- Verified that the protocol, implementation tracker, decisions log, and source map still point directly to `DEVELOP_NODE_CONTEXT.txt` and the split guide set, while the older `Develop node first think through.txt` file is not part of the active continuation path and appears superseded by the split guide set.
- Noted that these files are path-coupled to the active develop protocol bundle, so any later move should update the `AGENTS.md` / `DEVELOP_PASS_PROTOCOL.md` references together.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B07

- Reviewed batch `B07` (`docs/engineering/develop/raw_manual_workspace` and `implementation_phases`).
- Followed the repo instruction to start from `docs/engineering/develop/DEVELOP_PASS_PROTOCOL.md`, then used the RAW workspace packet read-order docs to evaluate the current handoff, plan, contracts, and phase history against the current codebase.
- Classified `docs/engineering/develop/raw_manual_workspace/README.md`, `CURRENT_HANDOFF.md`, `RAW_MANUAL_WORKSPACE_PLAN.md`, and `NATIVE_RAW_UI_SMOKE_RESULTS.md` as `Current`.
- Classified `implementation_phases/00_IMPLEMENTATION_GUIDE.md`, `00_MODE_CONTRACT.md`, `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md`, `00_STORAGE_LAYOUT.md`, `07_managed_decomposition.md`, `08_later_workflow_upgrades.md`, and `08A_local_range_tone_equalizer.md` as `Current`.
- Classified completed milestone phases `01_folder_catalog_foundation.md` through `06_raw_workspace_panels.md` as `Archived`.
- Verified against current code that the folder-backed RAW workspace, per-image project lifecycle, dedicated RAW panels, managed decomposition hooks, and Local Range branch all exist in the repo, while the remaining open work is the documented native/manual smoke evidence recorded by the current handoff and checklist/results files.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B10

- Reviewed batch `B10` (`docs/raw-tab-ui-overhaul`).
- Compared the older overhaul packet against both the current RAW workspace source layout and the newer `docs/raw-tab-ui-overhaul/auto-starting-point` packet to separate live validation/contract docs from older planning material.
- Classified `docs/raw-tab-ui-overhaul/implementation-contract.md`, `ui-validation-checklist.md`, and `pass-11-validation-notes.md` as `Current`.
- Classified `docs/raw-tab-ui-overhaul/README.md`, `human-workflow-notes.md`, `interaction-ideas.md`, `missed-angles-audit.md`, and `side-panel-redesign-spec.md` as `Info`.
- Classified `docs/raw-tab-ui-overhaul/current-ui-map.md`, `implementation-passes.md`, and `open-questions.md` as `Archived`.
- Verified against current code that the overhaul's key layout and labeling changes are shipped: the controls panel defaults to `420/340/520`, the RAW panel uses `Main Controls` and `Graph Controls`, suggestions open from a compact top-area expander, Local Range overlay labels are `Affected`, `Delta`, and `Mask`, and project actions now use compact menu wording such as `Convert to Nodes` and `Use Graph as Recipe`.
- Verified that `pass-11-validation-notes.md` is still an active companion log because it was updated on 2026-07-02 with current UI and Starting Point continuation entries, while the newer `auto-starting-point` folder now owns the active RAW resume entrypoint.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - B11

- Reviewed batch `B11` (`docs/raw-tab-ui-overhaul/auto-starting-point` and its `archive` subfolder).
- Followed the repo's required read order for this workstream and treated the folder as an active, path-coupled protocol bundle instead of as loose standalone docs.
- Classified all 13 files in `docs/raw-tab-ui-overhaul/auto-starting-point` as `Current`.
- Marked `docs/raw-tab-ui-overhaul/auto-starting-point/archive/implementation-progress-pass8-ledger-2026-07-02.md` as `archived-in-place`.
- Verified that `AGENTS.md` routes directly into this folder's `implementation-progress.md`, `README.md`, `agent-reread-guide.md`, `implementation-contract.md`, and `implementation-pass-readiness.md`, and that the companion docs explicitly route future work into the task-specific research files rather than treating them as stale background.
- Verified against current code and validation/test entrypoints that the documented feature area exists and matches the bundle: `RawAutoStartPoint` types and planner/diagnostics code are present, the RAW workspace exposes `Build Starting Point`, `Refit Display`, `Add Local Range`, and `Add Mild Tone`, the Diagnostics drawer renders selected-candidate and candidate-evidence sections, the renderer publishes named stage diagnostics, the validation command runner exposes `--validate-raw-starting-point-records` and `--summarize-raw-starting-point-records`, and graph behavior tests cover Starting Point diagnostics and conservative planner behavior.
- Preserved the current dirty worktree state while classifying the docs; the folder currently has in-flight documentation edits and the archive subfolder is still untracked history, so no moves or content edits were made inside the Starting Point packet during this pass.
- No documentation files were moved or renamed in this pass.

## 2026-07-02 - Move Phase 1

- Created `docs/stack-documentation/` as the consolidated documentation home with `Current`, `Info`, and `Archived` as the intended category model.
- Moved `loading fixes findings.txt` into `docs/stack-documentation/Archived/repo-root/`.
- Moved the reviewed top-level `Info` docs from `docs/` into `docs/stack-documentation/Info/top-level/`.
- Moved `docs/NEURAL_DENOISE_SYSTEM_PLAN.md` into `docs/stack-documentation/Archived/top-level/`.
- Updated `BUILDING.md` so its release-doc link points to the moved `INSTALLER_UPDATER_RELEASES.md` file.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, and `source-map.md` to reflect the new locations.
- Left `docs/LOCAL_TONE_MAPPING_PLAN.md` and the various path-coupled current bundles in place for a later coordinated current-doc move pass.

## 2026-07-02 - Move Phase 2

- Moved the reviewed Auto Raw Base research pair into `docs/stack-documentation/Info/auto-raw-base/`.
- Moved the older Auto Raw Base implementation packet into `docs/stack-documentation/Archived/auto-raw-base/`.
- Removed the now-empty `docs/auto-raw-base/` source folder after the move.
- Updated `docs/raw-tab-ui-overhaul/README.md` and `implementation-contract.md` so their background references point at the moved Auto Raw Base packet.
- Updated the moved Auto Raw Base packet notes so the archived README and research crosswalk still describe the new split between `Info` and `Archived`.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 3

- Moved the reviewed `Info` subset of the older RAW overhaul packet into `docs/stack-documentation/Info/raw-tab-ui-overhaul/`.
- Moved the reviewed `Archived` subset of the older RAW overhaul packet into `docs/stack-documentation/Archived/raw-tab-ui-overhaul/`.
- Left `docs/raw-tab-ui-overhaul/implementation-contract.md`, `ui-validation-checklist.md`, and the actively updated `pass-11-validation-notes.md` in place as the current subset for a later coordinated move.
- Updated `docs/raw-tab-ui-overhaul/implementation-contract.md` so its design-background references point to the rehomed `Info` docs.
- Updated the moved RAW overhaul `README.md`, `implementation-passes.md`, and `side-panel-redesign-spec.md` so their cross-references still point to the correct current, info, and archived locations after the split move.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 4

- Moved `docs/LOCAL_TONE_MAPPING_PLAN.md` into `docs/stack-documentation/Current/top-level/`.
- Moved `docs/engineering/WINDOW_CHROME_POSTMORTEM.md` into `docs/stack-documentation/Info/engineering/`.
- Updated `BUILDING.md` so its window-chrome postmortem link points at the rehomed info doc.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 5

- Moved the active MFSR packet from `docs/engineering/mfsr/` into `docs/stack-documentation/Current/engineering/mfsr/` as a keep-together current bundle.
- Updated `docs/engineering/NODE_LIBRARY_MARKER_AUDIT.md` so its MFSR citations point at the rehomed packet.
- Updated `04_CODEX_GOAL_RULES.md` and `03_MFSR_IMPLEMENTATION_PLAN.md` inside the moved packet so their explicit folder-path references point at the new current-doc location.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 6

- Moved `docs/engineering/NODE_LIBRARY_MARKER_AUDIT.md` into `docs/stack-documentation/Current/engineering/`.
- Left the `docs/engineering/architecture/` pair in place because those current docs are still woven through the active develop tracker bundle and need a more coordinated reference-update pass.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 7

- Moved `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md` and `ARCHITECTURE_ORGANIZATION_GUIDE.md` into `docs/stack-documentation/Current/engineering/architecture/`.
- Updated `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md` and `DEVELOP_DECISIONS.md` so their architecture-doc references point at the new current-doc paths.
- Updated the moved `ARCHITECTURE_ORGANIZATION_GUIDE.md` so its hotspot-map cross-reference points at the rehomed architecture location.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 8

- Moved the active Develop protocol bundle from `docs/engineering/develop/` into `docs/stack-documentation/Current/engineering/develop/`.
- Moved `DEVELOP_NODE_CONTEXT.txt` plus the split numbered detailed-guide set into `docs/stack-documentation/Current/engineering/develop/spec_sources/`.
- Moved `Develop node first think through.txt` into `docs/stack-documentation/Archived/engineering/develop/spec_sources/`.
- Updated `AGENTS.md` and the moved Develop bundle's internal path references so the repo entrypoint and protocol/tracker/source-map cross-links now point at the rehomed current-doc paths.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 9

- Moved the active RAW manual workspace packet from `docs/engineering/develop/raw_manual_workspace/` into `docs/stack-documentation/Current/engineering/develop/raw_manual_workspace/`.
- Moved completed milestone phases `01` through `06` into `docs/stack-documentation/Archived/engineering/develop/raw_manual_workspace/implementation_phases/`.
- Updated the moved current packet so its phase-order and folder-map references now point at the archived milestone docs, and updated the archived milestone docs that still cite the live contracts or smoke ledger so those links continue to resolve.
- Removed the now-empty original `docs/engineering/develop/` source branch after the B06, B07, and B08 moves were fully rehomed.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 10

- Moved `docs/raw-tab-ui-overhaul/implementation-contract.md`, `ui-validation-checklist.md`, and `pass-11-validation-notes.md` into `docs/stack-documentation/Current/raw-tab-ui-overhaul/`.
- Updated the already-moved overhaul background and archived-history docs so their references to the current contract/checklist/validation files now point at the new `docs/stack-documentation/Current/raw-tab-ui-overhaul/` paths.
- Left `docs/raw-tab-ui-overhaul/auto-starting-point/` and its archived ledger in place because that packet is still path-coupled through `AGENTS.md` and its own internal entry-doc routing.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 11

- Moved the main Starting Point packet from `docs/raw-tab-ui-overhaul/auto-starting-point/` into `docs/stack-documentation/Current/raw-tab-ui-overhaul/auto-starting-point/`.
- Updated `AGENTS.md`, the moved `agent-reread-guide.md` command examples, and the already-moved overhaul background notes so live path references now point at the new Starting Point current-doc location.
- Intentionally left `docs/raw-tab-ui-overhaul/auto-starting-point/archive/implementation-progress-pass8-ledger-2026-07-02.md` archived-in-place at its original path, and kept the moved `implementation-progress.md` ledger pointing there explicitly.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 12

- Moved `docs/raw-tab-ui-overhaul/auto-starting-point/archive/implementation-progress-pass8-ledger-2026-07-02.md` into `docs/stack-documentation/Archived/raw-tab-ui-overhaul/auto-starting-point/`.
- Updated the active Starting Point `implementation-progress.md` ledger so its preserved Pass 0-8 history path now points at the new archive location.
- Removed the now-empty original `docs/raw-tab-ui-overhaul/` source branch after the current, info, and archived overhaul docs were fully rehomed.
- Updated `move-log.tsv`, `file-inventory.tsv`, batch/folder moved counts, `resume-protocol.md`, and `source-map.md` to reflect the new locations.

## 2026-07-02 - Move Phase 13

- Mirrored `README.md`, `BUILDING.md`, and `THIRD_PARTY_NOTICES.md` into `docs/stack-documentation/Info/repo-root/`.
- Kept the repo-root originals in place so GitHub landing-page behavior, build entrypoints, and release/packaging conventions remain intact.
- Updated `file-inventory.tsv`, `move-log.tsv`, `resume-protocol.md`, `source-map.md`, and `docs/stack-documentation/README.md` to record the mirrored root-doc copies explicitly.

## 2026-07-02 - Audit Pass 14

- Reviewed `website/index.html`, `website/beta main version/index2.html`, and `website/other versions/index3.html`.
- Classified them as product website assets rather than repository documentation and left them in place.
- Added `non-documentation-candidates.tsv` and updated the batch/folder trackers so future documentation passes do not re-audit those files.

## 2026-07-03 - Intake Rule Update

- Updated `docs/stack-documentation/IDEA_INTAKE_PROTOCOL.md` so routed note intake now prefers the real documentation tree first and only uses `Current/ideas`, `Current/updates`, `Current/fixes`, `Info/research`, and `Info/questions` as fallback destinations.
- Updated `docs/stack-documentation/README.md` to match the new routing rule.

## 2026-07-03 - _local_archive Pass 16 / B13

- Mapped `33` `_local_archive` documentation candidates into new review batches `B13` through `B18`.
- Reviewed `B13`, covering the `_local_archive` root doc set plus `_local_archive/Documentation/`.
- Classified `UNIFIED_WORKSPACE_ARCHITECTURE.md` and `ADVANCED_NODE_GRAPH_COMPOSITOR_GUIDE.md` as `Current`.
- Classified `Dear ImGui Desktop UI Reference.md`, `Documentation/SystemOverview.md`, and `Documentation/CompositeAnimationSpecs.md` as `Info`.
- Classified `composite tab integration into editor.txt` and `composite tab integration into editor progress.txt` as `Archived`.
- Updated `batch-status.tsv`, `folder-status.tsv`, `file-inventory.tsv`, `review-batches.md`, `resume-protocol.md`, and `source-map.md` to record the `_local_archive` extension batches and the completed `B13` review.

## 2026-07-03 - _local_archive Pass 17 / B14

- Reviewed `B14`, covering `_local_archive/Current Plan Files and Documentation In Use/`.
- Classified `Render_Tab_Foundation_Progress.md` as `Current`.
- Classified `Composite_Web_Parity_Implementation_Guide.md`, the three glass-sphere render-diagnostic notes, and `Truth Source for a C++ Desktop Bundler and Minifier.md` as `Info`.
- Classified `CODEX_PROMPT_TYPED_SOCKETS_FIRST_PASS.md` and `cursor_composite_tab_implementation_gui.md` as `Archived`.
- Updated `batch-status.tsv`, `folder-status.tsv`, `file-inventory.tsv`, `review-batches.md`, and `resume-protocol.md` to record the completed `B14` review.

## 2026-07-03 - _local_archive Pass 18 / B15-B16

- Reviewed `B15`, covering `_local_archive/UI research/`.
- Classified all six UI research files as `Info` because they are architecture, UX, naming, portability, and code-organization audits rather than active instruction files.
- Reviewed `B16`, covering `_local_archive/Redefining Rendering/`.
- Classified all five rendering redesign specification files as `Current` because `Render_Tab_Foundation_Progress.md` still treats the packet as the live source-of-truth for future render slices.
- Updated `batch-status.tsv`, `folder-status.tsv`, `file-inventory.tsv`, `review-batches.md`, `README.md`, and `resume-protocol.md` to record the completed `B15` and `B16` reviews.

## 2026-07-03 - _local_archive Pass 19 / B17-B18

- Reviewed `B17`, covering `_local_archive/docs/old_docs/`.
- Classified `NODE_GRAPH_INTERACTION_GUIDE.md` and `NODE_LIBRARY_CLEANUP_ROADMAP.txt` as `Current`.
- Classified `NODE_LIBRARY_ADVANCED_UI_CANDIDATES.txt`, `NODE_LIBRARY_AUDIT.txt`, and `NODE_LIBRARY_CHANNEL_COMPATIBILITY.txt` as `Info`.
- Classified `DEMOSAICING GAIN RESEARCH ANSWERS.txt` and `_local_archive/notes/very future stuff.txt` as `Archived`.
- Updated `batch-status.tsv`, `folder-status.tsv`, `file-inventory.tsv`, `review-batches.md`, `README.md`, and `resume-protocol.md` to record the completed `B17` and `B18` reviews.

## 2026-07-03 - _local_archive Move Pass 20 / Move Phase 14

- Rehomed all `33` reviewed `_local_archive` documentation files into `docs/stack-documentation/`.
- Grouped current render/editor architecture packets under `docs/stack-documentation/Current/engineering/`.
- Grouped retained research, audits, and parity/reference notes under `docs/stack-documentation/Info/engineering/`.
- Grouped historical prompt/chat/scratch material under `docs/stack-documentation/Archived/engineering/` and `docs/stack-documentation/Archived/top-level/`.
- Updated `Render_Tab_Foundation_Progress.md` and `NODE_LIBRARY_CLEANUP_ROADMAP.txt` so their source-of-truth references point at the moved packet locations.
- Updated `move-log.tsv`, `file-inventory.tsv`, `source-map.md`, `README.md`, and `resume-protocol.md` to record the executed `_local_archive` move set.
