# Pass 11 Validation Notes

Date: 2026-06-30

## Automated Evidence

The following checks were run after the Pass 10 density polish and before closing the first RAW tab UI overhaul implementation set:

- `.\tools\build_stack.ps1 -Root 'D:\Program Development\Stack' -BuildDir 'D:\Program Development\Stack\build'`
  - Passed. Produced `D:\Program Development\Stack\build\Stack.exe` and `D:\Program Development\Stack\build\StackGraphBehaviorTests.exe`.
- `.\build\StackGraphBehaviorTests.exe`
  - Passed with `Stack graph behavior tests passed.`
- `.\build\Stack.exe --validate-layer-registry`
  - Passed with `LayerRegistry validation passed.`
- `.\build\Stack.exe --validate-develop-node-smoke`
  - Passed with `Develop node smoke validation passed.`
- `.\build\Stack.exe --validate-raw-workspace-loading-smoke 'C:\Users\djhbi\Downloads\all in extract\all copied images' --expect-min-sources 1`
  - Passed with 178 sources, 1 group, 178 valid thumbnails, 0 queued thumbnails, 0 failed thumbnails, and 29 existing projects.
- `.\build\Stack.exe --validate-develop-real-raw-smoke 'C:\Users\djhbi\Downloads\all in extract\all copied images\DSC00122.ARW' 'C:\Users\djhbi\Downloads\all in extract\all copied images\DSC00123.ARW'`
  - Passed for both real RAW files with stable repeated solves.

## Manual Visual Validation Status

The native Dear ImGui RAW workspace does not currently have a repo-local screenshot or UI automation harness for the manual visual states required by `implementation-contract.md`. The app builds and the available command-line validation suite passes, but the following checks still need hands-on verification in the native app before the UI can be called fully visually validated:

- narrow, normal, and wide window views
- long RAW filenames and long workspace paths
- no selected RAW empty state
- new/default RAW with Auto Base
- existing saved recipe
- read-only/error/custom graph state
- right gallery expanded/collapsed
- bottom filmstrip mode
- active Local Range, Color Target, and Finish Tone
- suggestions available, no suggestions, and warnings/advisories
- Diagnostics drawer collapsed/opened/from warning link
- center view modes and disabled unavailable modes

Use the existing local RAW workspace from the automated smoke test when running the manual pass:

```text
C:\Users\djhbi\Downloads\all in extract\all copied images
```

## Post-Pass 11 Continuation Log

### July 2, 2026 - Pass 8 Display Fit Auto-Owned Adjustment State UI Added

```text
Scope: Make Base Light, Base Assist, and Diagnostics report auto-owned View Transform adjustments that are not full Display Fit applications without changing any recipe write path.
Files touched: EditorModuleRawWorkspace.cpp, EditorModuleRawWorkspaceAutoBase.cpp, EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Base Light, Base Assist, and native Diagnostics now label auto-owned View Transform adjustments without full Display Fit state as Auto-adjusted / Auto adjustment. Existing Highlight Protection recipe writes and ownership state are unchanged; no recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation/selection/scoring, warning generation, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new Auto-adjusted/Auto adjustment label paths plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Display Fit Auto-Owned Adjustment State UI Started

```text
Scope: Make Base Light, Base Assist, and Diagnostics report auto-owned View Transform adjustments that are not full Display Fit applications without changing any recipe write path.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Suggestion Undo Snapshot Coverage Added

```text
Scope: Ensure explicit RAW Exposure, White Balance, Highlight, and Local Range suggestion apply paths capture the existing automatic-action undo snapshot before they write visible recipe values.
Files touched: EditorModule.h, EditorModuleRawWorkspaceAutoBase.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: Undo coverage only. Explicit RAW Exposure, White Balance, Highlight, and Local Range suggestion apply paths now capture the selected RAW source's pre-action recipe into the existing automatic-action revert snapshot before writing their already-visible recipe values. Existing suggestion recipe writes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation/selection/scoring, warning generation, diagnostics content/serialization, validation readiness thresholds, report schema versions, and RAW-file-dependent tuning are unchanged.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new snapshot helper and calls in the individual suggestion apply paths plus existing explicit apply/recipe paths.
Guardrails: No constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Suggestion Undo Snapshot Coverage Started

```text
Scope: Ensure explicit RAW Exposure, White Balance, Highlight, and Local Range suggestion apply paths capture the existing automatic-action undo snapshot before they write visible recipe values.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes beyond existing suggestion writes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Diagnostics Undo Snapshot State UI Added

```text
Scope: Show whether the existing automatic-action undo snapshot is available for the selected RAW source in the native Diagnostics drawer.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. The native Diagnostics drawer now includes a read-only Undo Snapshot State block using existing undo snapshot/source identity fields. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new Undo Snapshot State helper/render lines plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Diagnostics Undo Snapshot State UI Started

```text
Scope: Show whether the existing automatic-action undo snapshot is available for the selected RAW source in the native Diagnostics drawer.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Diagnostics Applied-Suggestion State UI Added

```text
Scope: Show existing applied-suggestion label, owning control section, selected-source match, and analysis freshness in the native Diagnostics drawer.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. The native Diagnostics drawer now includes a read-only Applied Suggestion State block using existing applied-suggestion fields. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new Applied Suggestion State helper/render lines plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Diagnostics Applied-Suggestion State UI Started

```text
Scope: Show existing applied-suggestion label, owning control section, selected-source match, and analysis freshness in the native Diagnostics drawer.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Main Controls Diagnostics Handoff UI Added

```text
Scope: Add read-only Diagnostics drawer handoff buttons near Base Light and White Balance so users can jump from owning controls to existing RAW Exposure, Display Fit, WB, highlight, and Starting Point rationale.
Files touched: EditorModuleRawWorkspace.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Base Light and White Balance now include Diagnostics buttons that open the existing native Diagnostics drawer. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new Diagnostics button IDs and diagnostics-open requests plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Main Controls Diagnostics Handoff UI Started

```text
Scope: Add read-only Diagnostics drawer handoff buttons near Base Light and White Balance so users can jump from owning controls to existing RAW Exposure, Display Fit, WB, highlight, and Starting Point rationale.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Graph Controls Diagnostics Handoff UI Added

```text
Scope: Add read-only Diagnostics drawer handoff buttons near the Local Range and Finish Tone graph controls so users can jump from owning controls to existing Starting Point rationale.
Files touched: EditorModuleRawWorkspace.cpp, EditorModuleRawWorkspaceLocalRange.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Local Range and Finish Tone now include Diagnostics buttons that open the existing native Diagnostics drawer. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new Diagnostics button IDs and diagnostics-open requests plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics content/serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Graph Controls Diagnostics Handoff UI Started

```text
Scope: Add read-only Diagnostics drawer handoff buttons near the Local Range and Finish Tone graph controls so users can jump from owning controls to existing Starting Point rationale.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Finish Tone Applied-Marker UI Added

```text
Scope: Mirror the existing Mild Finish Tone applied state beside the owning Finish Tone controls using the existing non-mutating suggestion/applied marker path.
Files touched: EditorModuleRawWorkspace.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. The Finish Tone section now renders the existing applied marker for appliedSuggestionSection == "Finish Tone", and clicking the marker opens the existing suggestions popout. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found only the new Finish Tone marker compute/render lines plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Finish Tone Applied-Marker UI Started

```text
Scope: Mirror the existing Mild Finish Tone applied state beside the owning Finish Tone controls using the existing non-mutating suggestion/applied marker path.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Diagnostics Display Fit State UI Added

```text
Scope: Show the current Display Fit ownership and freshness state in the native Diagnostics drawer using existing owner/source/hash fields.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. The native Diagnostics drawer now includes a read-only Display Fit State block with source ownership and analysis freshness. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, serialized diagnostics, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new read-only Diagnostics Display Fit state helper/render wiring plus existing diagnostics read paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, serialized diagnostics, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Diagnostics Display Fit State UI Started

```text
Scope: Show the current Display Fit ownership and freshness state in the native Diagnostics drawer using existing owner/source/hash fields.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, serialized diagnostics, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Base Light Display Fit State UI Added

```text
Scope: Make the Base Light owning-control summary report Display Fit freshness and ownership using existing owner and analysis-hash fields.
Files touched: EditorModuleRawWorkspace.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. The Base Light summary now reports Preview pending, Ready, Auto-current, Needs Refit, Auto recorded, or Manual/locked for Display Fit instead of only Auto/Manual/Default. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new read-only Base Light Display Fit state helper/render wiring plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Base Light Display Fit State UI Started

```text
Scope: Make the Base Light owning-control summary report Display Fit freshness and ownership using existing owner and analysis-hash fields.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Base Assist Display Fit State UI Added

```text
Scope: Make Base Assist report whether Display Fit is preview-pending, ready, auto-current, auto-stale, auto-recorded, or manual/locked using existing owner and analysis-hash fields.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Base Assist now shows a read-only Display Fit state line near the readiness summary. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found the new read-only Display Fit state helper/render lines plus existing explicit apply/recipe paths.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Base Assist Display Fit State UI Started

```text
Scope: Make Base Assist report whether Display Fit is preview-pending, ready, auto-current, auto-stale, or manual/locked using existing owner and analysis-hash fields.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics serialization, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Source-Attribution Validation Coverage Added

```text
Scope: Add non-gating validation-summary coverage for the existing Source UI-readable diagnostics line.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: Validation/reporting only. Validation-set summary schema v32 and validation-summary report schema v33 now include non-gating counts and coverage for the Source UI line and source-scoping detail. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed with summary report schema v33, validation-set summary schema v32, Source coverage complete for the synthetic record, and mechanicalInputsComplete=false for the intentionally incomplete record; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found only validation/report coverage and existing recipe summary reads.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Source-Attribution Validation Coverage Started

```text
Scope: Add non-gating validation-summary coverage for the existing Source UI-readable diagnostics line.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, focused synthetic validation-summary smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Visible Action-Scope Validation Coverage Added

```text
Scope: Add non-gating validation-summary coverage for the serialized Visible action scope diagnostics line.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: Validation/reporting only. Validation-set summary schema v31 and validation-summary report schema v32 now include non-gating counts and coverage for the Visible action scope UI line and guardrail detail. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed with summary report schema v32, validation-set summary schema v31, Visible action scope coverage complete for the synthetic record, and mechanicalInputsComplete=false for the intentionally incomplete record; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found only validation/report coverage, existing score reads, and existing report writes.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Visible Action-Scope Validation Coverage Started

```text
Scope: Add non-gating validation-summary coverage for the serialized Visible action scope diagnostics line.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, focused synthetic validation-summary smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Serialized Action-Scope Diagnostics Added

```text
Scope: Mirror the Base Assist visible-control action-scope promise into UI-readable/serialized Starting Point diagnostics.
Files touched: RawAutoStartPoint.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: Diagnostics/reporting only. Starting Point dry-run diagnostics now include a read-only "Visible action scope" line saying Build Base writes RAW Exposure and Display Fit, Add Local Range writes Local Range only, Add Mild Tone writes Finish Tone only, and White Balance remains unchanged by these actions. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics readiness thresholds, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused source check confirmed the line is added through existing uiView serialization and no schema version constant changed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found only the new diagnostics helper plus existing candidate scoring/selection fields and recipe-value reads.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics readiness thresholds, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Serialized Action-Scope Diagnostics Started

```text
Scope: Mirror the Base Assist visible-control action-scope promise into UI-readable/serialized Starting Point diagnostics.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, focused diagnostics/serialization smoke or source check, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, diagnostics readiness thresholds, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Base Assist Action-Scope UI Added

```text
Scope: Keep Base Assist visible-control action boundaries scannable in the RAW side panel without changing action behavior.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Base Assist now shows a read-only visible action-scope line: Build Base writes RAW Exposure and Display Fit; Add Local Range writes Local Range only; Add Mild Tone writes Finish Tone only; White Balance remains unchanged by these actions. No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply/action-boundary guardrail search found only the new read-only action-scope helper/render lines plus existing explicit apply paths.
Guardrails: No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Base Assist Action-Scope UI Started

```text
Scope: Keep Base Assist visible-control action boundaries scannable in the RAW side panel without changing action behavior.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/action-boundary guardrail search.
Guardrails: Do not change diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Validation Gate Advisory CLI Summary Added

```text
Scope: Show non-gating advisory check counts in the validation gate-status command-line summary.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: CLI/reporting only. The validation gate-status command output now prints nonGatingAdvisoryChecks and nonGatingAdvisoryIncomplete from the existing nonGatingAdvisories report object. No JSON schema, report artifact content, required gate readiness, validation threshold, diagnostics generation/serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --check-raw-starting-point-validation-gates CLI smoke passed with allRequiredGatesReady=true, readyGateCount=5, requiredGateCount=5, nonGatingAdvisoryChecks=2, and nonGatingAdvisoryIncomplete=2; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found only existing validation/reporting recipe/apply/write references and the new CLI summary tokens.
Guardrails: No JSON report schema versions, diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness thresholds, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Validation Gate Advisory CLI Summary Started

```text
Scope: Show non-gating advisory check counts in the validation gate-status command-line summary.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, focused synthetic validation-gate CLI advisory smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write guardrail search.
Guardrails: Do not change diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness thresholds, validation readiness thresholds, validation gates, report schema versions, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Validation Gate Advisory Coverage Added

```text
Scope: Surface non-gating advisory coverage from ready validation artifacts in the aggregate validation gate-status report.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: Validation/gate-status reporting only. Validation gate status reports are schema v3 and now include nonGatingAdvisories plus per-gate nonGatingAdvisoryChecks for stage-evidence and validation-summary candidate-stage coverage. Advisory checks can report incomplete candidate-stage coverage while allRequiredGatesReady remains true when required gates are otherwise ready. No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness thresholds, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --check-raw-starting-point-validation-gates smoke passed with schema v3, allRequiredGatesReady=true, readyGateCount=5/5, two incomplete non-gating candidate-stage advisory checks, and affectsRequiredReadiness=false; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no new recipe/apply/write paths in the new advisory reporting lines.
Guardrails: No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness thresholds, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Validation Gate Advisory Coverage Started

```text
Scope: Surface non-gating advisory coverage from ready validation artifacts in the aggregate validation gate-status report.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, focused synthetic validation-gate advisory smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write guardrail search.
Guardrails: Do not change diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness thresholds, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Stage-Evidence Preflight Candidate-Stage Coverage Added

```text
Scope: Add non-gating stage-evidence preflight coverage for whether each expected dry-run candidate carries complete required named stage diagnostics before records are merged.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: Validation/preflight reporting only. Stage-evidence check reports are schema v6 and now include per-source and aggregate candidate-stage diagnostic coverage for CurrentFit/Base/Balanced across Neutral Scene, Raw Placement, Local Candidate, Finish Tone Candidate, and Display Candidate. Missing per-candidate stage diagnostics are reported with source IDs while readyForRecordMerge and blockingReasons remain controlled only by the existing matched-source, candidate-diagnostics, selected-candidate, and required-stage checks. No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --check-raw-starting-point-stage-evidence complete-coverage smoke passed with schema v6, 15 candidate-stage coverage rows, and non-gating complete coverage; focused synthetic missing-stage smoke passed and reported balanced/display-candidate missing while readyForRecordMerge stayed true; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no new recipe/apply/write paths in the new candidate-stage coverage lines.
Guardrails: No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness thresholds, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Stage-Evidence Preflight Candidate-Stage Coverage Started

```text
Scope: Add non-gating stage-evidence preflight coverage for whether each expected dry-run candidate carries complete required named stage diagnostics before records are merged.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, focused synthetic stage-evidence check smokes, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write guardrail search.
Guardrails: Do not change diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, record merge readiness thresholds, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Candidate Stage Diagnostics Summary Coverage Added

```text
Scope: Add non-gating validation-summary coverage for whether each expected dry-run candidate carries complete required named stage diagnostics.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: Validation/reporting only. Validation summaries now report candidate-stage diagnostic coverage for each expected CurrentFit/Base/Balanced dry-run candidate across Neutral Scene, Raw Placement, Local Candidate, Finish Tone Candidate, and Display Candidate stages, with missing-record IDs and matching non-gating per-record completeness fields. No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records complete-coverage smoke passed with schema versions 30/31, 15 candidate-stage coverage rows, and non-gating complete coverage; focused synthetic missing-stage smoke passed and reported balanced/display-candidate missing for the synthetic record; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no new recipe/apply/write paths in the new candidate-stage coverage lines.
Guardrails: No diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Candidate Stage Diagnostics Summary Coverage Started

```text
Scope: Add non-gating validation-summary coverage for whether each expected dry-run candidate carries complete required named stage diagnostics.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write guardrail search.
Guardrails: Do not change diagnostics generation or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate generation/selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Diagnostics Candidate Score Breakdown Disclosure Added

```text
Scope: Make each candidate's existing score terms and penalties scannable by keeping total score/summary visible and rendering the detailed breakdown under a disclosure row.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Starting Point Diagnostics now keeps each candidate's total score and score summary visible, while grouping the existing score terms, weights, rationales, and penalties under a Score breakdown disclosure row. No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths or score assignment changes in the touched source file.
Guardrails: No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Diagnostics Candidate Score Breakdown Disclosure Started

```text
Scope: Make each candidate's existing score terms and penalties scannable by keeping total score/summary visible and rendering the detailed breakdown under a disclosure row.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, graph behavior tests, layer registry validation, develop-node smoke, git diff --check, touched-file whitespace sweep, and focused guardrail search.
Guardrails: Do not change diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Diagnostics Candidate Stage Evidence Disclosure Added

```text
Scope: Make each candidate's internal Starting Point stage evidence scannable by rendering existing stages under disclosure rows, with warning or non-complete stages opened by default.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Starting Point Diagnostics now groups each candidate's existing per-stage status, raw-safety summary, scene summary, display summary, status message, and warnings under stage disclosure rows. Warning or non-complete stages are default-open. No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths in the touched source file.
Guardrails: No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Diagnostics Candidate Stage Evidence Disclosure Started

```text
Scope: Make each candidate's internal Starting Point stage evidence scannable by rendering existing stages under disclosure rows, with warning or non-complete stages opened by default.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, graph behavior tests, layer registry validation, develop-node smoke, git diff --check, touched-file whitespace sweep, and focused guardrail search.
Guardrails: Do not change diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Base Assist Selected-Candidate Inline Summary Added

```text
Scope: Add a compact read-only Base Assist summary for the current selected dry-run Starting Point candidate and its visible manual controls.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Base Assist now shows a wrapped read-only "Selected dry-run candidate" summary built from existing dry-run Starting Point diagnostics, including the candidate label and ready visible manual values, followed by "Not applied." This is UI-only. No diagnostics serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused review confirmed the new slice only adds read-only summary helper/render code, while existing apply/write handlers in the same dirty file remain unchanged.
Follow-up: Native visual QA should confirm the inline selected-candidate summary wraps cleanly at narrow RAW side-panel widths and stays visually subordinate to the explicit action buttons.
```

### July 2, 2026 - Base Assist Selected-Candidate Inline Summary Started

```text
Scope: Add a compact read-only Base Assist summary for the current selected dry-run Starting Point candidate and its visible manual controls.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write guardrail search.
Guardrails: Do not change diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Action-Readiness Detail Summary Coverage Added

```text
Scope: Add non-gating validation-summary coverage for the explicit-action guardrail details in the existing Build Base, Add Local Range, and Add Mild Tone action-readiness UI rows.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summaries now report action-readiness guardrail detail coverage, missing record IDs, accepted detail fragments, and matching non-gating per-record completeness fields. This is validation/reporting only. No diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed and reported actionDetails=1/1, coverage=3, uiComplete=True, and schema versions 29/30; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no new recipe/apply/write paths in the touched validation code.
Follow-up: Later real-RAW validation summaries should use actionReadinessGuardrailDetailCoverage.missingDetailRecordIds to find stale records whose action-readiness rows no longer explain explicit visible-control writes or unavailable-candidate guardrails.
```

### July 2, 2026 - Action-Readiness Detail Summary Coverage Started

```text
Scope: Add non-gating validation-summary coverage for the explicit-action guardrail details in the existing Build Base, Add Local Range, and Add Mild Tone action-readiness UI rows.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write guardrail search.
Guardrails: Do not change diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Candidate Control-Value Summary Coverage Added

```text
Scope: Add non-gating validation-summary coverage for the existing candidate RAW Exposure, Display Fit, Local Range, and Finish Tone value UI rows so validation summaries can confirm candidate-visible values remain serialized for review.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summaries now report candidate control-value line/detail coverage, missing record IDs, value counts, required detail fragments, and matching non-gating per-record completeness fields. This is validation/reporting only. No diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed and reported candidateControlValueLines=1/1, details=1/1, coverage=11, uiComplete=True, and schema versions 28/29; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths in the touched validation code.
Follow-up: Later real-RAW validation summaries should use candidateControlValueLineCoverage.missingLineRecordIds and missingDetailRecordIds to find stale records whose candidate value rows or not-applied detail text are absent.
```

### July 2, 2026 - Candidate Control-Value Summary Coverage Started

```text
Scope: Add non-gating validation-summary coverage for the existing candidate RAW Exposure, Display Fit, Local Range, and Finish Tone value UI rows so validation summaries can confirm candidate-visible values remain serialized for review.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Partial-Evidence Summary Coverage Added

```text
Scope: Add non-gating validation-summary coverage for the existing Partial evidence UI warning rows so validation summaries can confirm fallback stage-evidence warnings remain visible.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summaries now report partial-evidence warning counts, visible Partial evidence UI-row counts, missing/mismatched record IDs, line value counts, and matching non-gating per-record completeness fields. This is validation/reporting only. No diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed and reported partialEvidence=1/1, totals=2/2, uiComplete=True, and schema versions 27/28; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths in the touched validation code.
Follow-up: Later real-RAW validation summaries should use partialEvidenceUiCoverage.missingUiLineRecordIds and lineCountMismatchRecordIds to find stale records whose fallback warnings are not visible in the diagnostics UI rows.
```

### July 2, 2026 - Partial-Evidence Summary Coverage Started

```text
Scope: Add non-gating validation-summary coverage for the existing Partial evidence UI warning rows so validation summaries can confirm fallback stage-evidence warnings remain visible.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Dry-Run Guardrail Summary Coverage Added

```text
Scope: Add non-gating validation-summary coverage for the existing Dry run, Recipe writes, and Stage evidence UI rows so validation summaries can confirm the dry-run/read-only guardrails are visible.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summaries now report Dry run, Recipe writes, and Stage evidence UI-row coverage plus matching non-gating per-record completeness fields. This is validation/reporting only. No diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed and reported dryRun=1/1, recipeWrites=1/1, stageEvidence=1/1, uiComplete=True, and schema versions 26/27; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths in the touched validation code.
Follow-up: Later real-RAW validation summaries should use dryRunGuardrailCoverage, recipeWritesGuardrailCoverage, and stageEvidenceUiLineCoverage to find stale records that are missing the visible dry-run/read-only rows.
```

### July 2, 2026 - Dry-Run Guardrail Summary Coverage Started

```text
Scope: Add non-gating validation-summary coverage for the existing Dry run, Recipe writes, and Stage evidence UI rows so validation summaries can confirm the dry-run/read-only guardrails are visible.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Selected Candidate Detail Summary Coverage Added

```text
Scope: Add validation-summary coverage for whether each record's Diagnostic selection UI line carries the selected-candidate visible-control handoff detail.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summaries now report Diagnostic selection detail coverage for the required "Visible controls:" fragment. This is validation/reporting only and non-gating. No diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed and reported recordsWithSelectedCandidateVisibleControlDetail=1 plus selectedCandidateDetailCoverageIsGating=false; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused validation summary search confirmed schema version bumps and selectedCandidateDetailCoverage fields; focused recipe/apply/write search found no new recipe-write paths.
Follow-up: Later real-RAW validation summaries should use selectedCandidateDetailCoverage.missingVisibleControlDetailRecordIds to find older records that need regeneration or merged diagnostics.
```

### July 2, 2026 - Selected Candidate Detail Summary Coverage Started

```text
Scope: Add validation-summary coverage for whether each record's Diagnostic selection UI line carries the selected-candidate visible-control handoff detail.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change diagnostics generation, recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Selected Candidate Visible Controls Diagnostics Added

```text
Scope: Make the selected-candidate diagnostics handoff name the visible controls that the selected dry-run candidate would touch, so the UI-readable/serialized report connects selection back to editable control ownership.
Files touched: RawAutoStartPoint.cpp, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point dry-run Diagnostic selection detail now appends the selected candidate's existing touched-control list. This is diagnostics presentation only. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate generation, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused source/test/docs search confirmed FormatTouchedControls and the Diagnostic selection visible-controls assertion; focused RawAutoStartPoint.cpp recipe/apply/write review found no new recipe writes and only existing dry-run recipe serialization/proposal code.
Follow-up: Later native Diagnostics drawer QA should confirm the selected candidate line remains readable when the selected controls list is long.
```

### July 2, 2026 - Selected Candidate Visible Controls Diagnostics Started

```text
Scope: Make the selected-candidate diagnostics handoff name the visible controls that the selected dry-run candidate would touch, so the UI-readable/serialized report connects selection back to editable control ownership.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics readiness thresholds, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Base Assist Diagnostics Access Added

```text
Scope: Add a direct Diagnostics command inside Base Assist so users can open the existing Diagnostics drawer for Starting Point candidate scores, visible controls, action readiness, and warnings without hunting elsewhere.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Base Assist now has a small Diagnostics button that sets the existing diagnosticsOpenRequested flag. This is UI access only. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payloads, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused source search confirmed the new RawWorkspaceBaseAssistDiagnostics button only sets diagnosticsOpenRequested and adds the Starting Point Diagnostics tooltip.
Follow-up: Later native visual QA should confirm the Base Assist Diagnostics button is easy to hit and opens the drawer at narrow and normal RAW side-panel widths.
```

### July 2, 2026 - Base Assist Diagnostics Access Started

```text
Scope: Add a direct Diagnostics command inside Base Assist so users can open the existing Diagnostics drawer for Starting Point candidate scores, visible controls, action readiness, and warnings without hunting elsewhere.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payloads, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Optional Action Responsive Layout Added

```text
Scope: Make the Base Assist optional Add Local Range and Add Mild Tone action buttons use a responsive row/stack layout so narrow RAW side-panel widths do not crowd the labels.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Base Assist now sizes the optional Add Local Range and Add Mild Tone buttons with explicit width-aware ImGui buttons and stacks them when two side-by-side buttons would be too narrow. This is layout-only. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payloads, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused layout search confirmed optionalAction sizing/stacking call sites.
Follow-up: Later native visual QA should confirm the optional action row stacks cleanly at narrow RAW side-panel widths and remains side-by-side at comfortable widths.
```

### July 2, 2026 - Optional Action Responsive Layout Started

```text
Scope: Make the Base Assist optional Add Local Range and Add Mild Tone action buttons use a responsive row/stack layout so narrow RAW side-panel widths do not crowd the labels.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payloads, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Candidate Score-Order Diagnostics Added

```text
Scope: Add a compact UI-readable Candidate score order line to existing Starting Point dry-run diagnostics so the Diagnostics drawer and serialized uiView expose CurrentFit/Base/Balanced ranking at a glance.
Files touched: RawAutoStartPoint.cpp, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point dry-run diagnostics now include a Candidate score order uiView line sorted from existing candidate total scores. This is diagnostics-only and explicitly non-mutating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; graph behavior assertions confirmed the score-order line contains the selected candidate label, compares scored candidates at a glance, and remains diagnostic-only; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused RawAutoStartPoint.cpp diff recipe/apply/write search found no matches.
Follow-up: Later native visual QA should confirm the new score-order row wraps cleanly in the Diagnostics drawer at narrow RAW side-panel widths.
```

### July 2, 2026 - Candidate Score-Order Diagnostics Started

```text
Scope: Add a compact UI-readable Candidate score order line to existing Starting Point dry-run diagnostics so the Diagnostics drawer and serialized uiView expose CurrentFit/Base/Balanced ranking at a glance.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, touched-file trailing-whitespace sweep, and focused recipe/apply/write diff search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Per-Record Diagnostic Completeness Summary Added

```text
Scope: Add a compact, non-gating per-record validation summary of existing Starting Point diagnostic completeness so later RAW review can see which records have the expected candidate diagnostics, selected candidate, candidate scores, visible-control rows, score-component rows, warning rows, and action-readiness rows.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include recordsWithCompleteUiDiagnosticSet, recordDiagnosticCompletenessComplete, recordDiagnosticCompletenessIsGating=false, and recordDiagnosticCompleteness rows for existing Starting Point diagnostics. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload generation, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed schema versions 22/23, recordsWithCompleteUiDiagnosticSet=1, recordDiagnosticCompletenessComplete=false, recordDiagnosticCompletenessIsGating=false, two per-record rows, a complete row for diagnostic-completeness-record-a, and missing Balanced Local/Tone warnings plus Add Mild Tone action labels for diagnostic-completeness-record-b; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused diff recipe/apply/write search found no new recipe, apply, or write call sites.
Follow-up: Later real RAW validation summaries can use recordDiagnosticCompleteness to triage stale or partial Starting Point diagnostics sidecars before changing readiness gates or tuning constants.
```

### July 2, 2026 - Per-Record Diagnostic Completeness Summary Started

```text
Scope: Add a compact, non-gating per-record validation summary of existing Starting Point diagnostic completeness so later RAW review can see which records have the expected candidate diagnostics, selected candidate, candidate scores, visible-control rows, score-component rows, warning rows, and action-readiness rows.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Candidate-Warning Summary Coverage Added

```text
Scope: Add non-gating validation summary coverage for the existing UI-readable candidate warning diagnostic lines so later RAW review can see whether CurrentFit, Base, and Balanced Local/Tone warning rows are present in validation records and what warning-count values they show.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include recordsWithExpectedWarningLines, candidateWarningLineCoverage, candidateWarningLineCoverageComplete, and candidateWarningLineCoverageIsGating=false for existing candidate warning UI rows. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload generation, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed schema versions 21/22, recordsWithExpectedWarningLines=2, candidateWarningLineCoverageComplete=true, candidateWarningLineCoverageIsGating=false, complete coverage rows for CurrentFit/Base/Balanced Local/Tone, and no regression in visible-control or score-component coverage; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply search found no validation-file apply path.
Follow-up: Later real RAW validation summaries can use these non-gating rows to spot stale sidecars where warning diagnostics are missing or to review warning-count distribution across candidate kinds.
```

### July 2, 2026 - Candidate-Warning Summary Coverage Started

```text
Scope: Add non-gating validation summary coverage for the existing UI-readable candidate warning diagnostic lines so later RAW review can see whether CurrentFit, Base, and Balanced Local/Tone warning rows are present in validation records and what warning-count values they show.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Visible-Control Summary Coverage Added

```text
Scope: Add non-gating validation summary coverage for the existing UI-readable candidate visible-control diagnostic lines so later RAW review can see whether CurrentFit, Base, and Balanced Local/Tone visible-control rows are present in validation records.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include recordsWithExpectedVisibleControlLines, candidateVisibleControlLineCoverage, candidateVisibleControlLineCoverageComplete, and candidateVisibleControlLineCoverageIsGating=false for existing candidate visible-control UI rows. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload generation, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed schema versions 20/21, recordsWithExpectedVisibleControlLines=2, candidateVisibleControlLineCoverageComplete=true, candidateVisibleControlLineCoverageIsGating=false, and complete coverage rows for CurrentFit/Base/Balanced Local/Tone; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply search found no validation-file apply path.
Follow-up: Later real RAW validation summaries can use these non-gating rows to spot stale sidecars where candidate diagnostics exist but the UI-readable visible-control ownership rows are missing.
```

### July 2, 2026 - Visible-Control Summary Coverage Started

```text
Scope: Add non-gating validation summary coverage for the existing UI-readable candidate visible-control diagnostic lines so later RAW review can see whether CurrentFit, Base, and Balanced Local/Tone visible-control rows are present in validation records.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Score-Component Summary Coverage Added

```text
Scope: Add non-gating validation summary coverage for the existing UI-readable score-component diagnostic lines so later RAW review can see whether CurrentFit, Base, and Balanced Local/Tone score-component rows are present in validation records.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include recordsWithExpectedScoreComponentLines, candidateScoreComponentLineCoverage, candidateScoreComponentLineCoverageComplete, and candidateScoreComponentLineCoverageIsGating=false for existing score-component UI rows. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload generation, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed schema versions 19/20, recordsWithExpectedScoreComponentLines=2, candidateScoreComponentLineCoverageComplete=true, candidateScoreComponentLineCoverageIsGating=false, and complete coverage rows for CurrentFit/Base/Balanced Local/Tone; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused recipe/apply search found no validation-file apply path.
Follow-up: Later real RAW validation summaries can use these non-gating rows to spot stale sidecars where score totals exist but the UI-readable score-component rows are missing.
```

### July 2, 2026 - Score-Component Summary Coverage Started

```text
Scope: Add non-gating validation summary coverage for the existing UI-readable score-component diagnostic lines so later RAW review can see whether CurrentFit, Base, and Balanced Local/Tone score-component rows are present in validation records.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Score-Component Diagnostics Added

```text
Scope: Add UI-readable score-component diagnostics for existing Starting Point dry-run candidates so Diagnostics can explain existing subscore and penalty values without changing candidate scoring or applying recipe values.
Files touched: RawAutoStartPoint.cpp, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point dry-run diagnostics now include per-candidate score-component lines summarizing existing Raw Safety, Scene Placement, Display Readability, and detailed subscore/penalty values. This is diagnostics-only and still reports no recipe writes. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused graph behavior assertions confirmed Base score-component value/detail visibility and diagnostic-only wording; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; recipe-write search found only existing candidate visible-edit/test setup assignments and no apply path.
Follow-up: Later native visual QA should confirm the score-component detail wraps cleanly in the Diagnostics drawer at narrow side-panel widths.
```

### July 2, 2026 - Score-Component Diagnostics Started

```text
Scope: Add UI-readable score-component diagnostics for existing Starting Point dry-run candidates so Diagnostics can explain existing subscore and penalty values without changing candidate scoring or applying recipe values.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Candidate-Score Summary Added

```text
Scope: Add non-gating validation summary coverage for existing Starting Point dry-run candidate scores so later RAW review can see score availability and score ranges for CurrentFit, Base, and Balanced candidates.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include recordsWithExpectedCandidateScores, candidateScoreCoverage, candidateScoreCoverageComplete, and candidateScoreCoverageIsGating=false for existing dry-run candidate scores. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed schema versions 18/19, recordsWithExpectedCandidateScores=2, candidateScoreCoverageComplete=true, candidateScoreCoverageIsGating=false, and CurrentFit/Base/Balanced score counts plus min/max/average values; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Later real RAW validation summaries can use these non-gating candidate-score coverage rows to review whether score evidence is present and how candidate total scores are distributed across the validation set.
```

### July 2, 2026 - Candidate-Score Summary Started

```text
Scope: Add non-gating validation summary coverage for existing Starting Point dry-run candidate scores so later RAW review can see score availability and score ranges for CurrentFit, Base, and Balanced candidates.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Candidate-Kind Summary Added

```text
Scope: Add non-gating validation summary coverage for existing Starting Point dry-run candidate kinds so later RAW review can see which records include CurrentFit, Base, and Balanced candidates and which candidate kind was selected.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include candidateKindCoverage, selectedCandidateKindCounts, and candidateKindCoverageComplete for existing dry-run candidates. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed CurrentFit/Base/Balanced coverage, Base/Balanced selected-kind counts, and candidateKindCoverageIsGating=false; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Later real RAW validation summaries can use these non-gating candidate-kind counts to review whether expected dry-run candidate families are present and which family is being selected across the validation set.
```

### July 2, 2026 - Candidate-Kind Summary Started

```text
Scope: Add non-gating validation summary coverage for existing Starting Point dry-run candidate kinds so later RAW review can see which records include CurrentFit, Base, and Balanced candidates and which candidate kind was selected.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Action-Readiness Value Summary Added

```text
Scope: Add non-gating validation summary value counts for the existing UI-readable Build Base, Add Local Range, and Add Mild Tone action-readiness diagnostic lines.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include valueCounts for each action-readiness line coverage row. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed valueCounts for Build Base Ready/Pending, Add Local Range No candidate/2 point(s), and Add Mild Tone No candidate/Ready; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Later real RAW validation summaries can use these non-gating value counts to review how often each Starting Point action is ready, pending, or unavailable across the validation set.
```

### July 2, 2026 - Action-Readiness Value Summary Started

```text
Scope: Add non-gating validation summary value counts for the existing UI-readable Build Base, Add Local Range, and Add Mild Tone action-readiness diagnostic lines.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Action-Readiness Summary Coverage Added

```text
Scope: Add non-gating validation summary coverage for UI-readable action-readiness diagnostics so record summaries show whether Build Base, Add Local Range, and Add Mild Tone readiness lines are present.
Files touched: RawStartingPointValidation.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Validation summary reports now include action-readiness diagnostic coverage counts and missing-record IDs for Build Base, Add Local Range, and Add Mild Tone action lines. This is report-only and explicitly non-gating. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic no-RAW --summarize-raw-starting-point-records smoke passed and confirmed actionReadinessCoverageIsGating=false with all three action lines counted; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Later real RAW validation summaries can use the new non-gating action-readiness coverage block to spot stale or incomplete Starting Point diagnostics sidecars before review.
```

### July 2, 2026 - Action-Readiness Summary Coverage Started

```text
Scope: Add non-gating validation summary coverage for UI-readable action-readiness diagnostics so record summaries show whether Build Base, Add Local Range, and Add Mild Tone readiness lines are present.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, a focused no-RAW summary-command smoke, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, validation readiness thresholds, or RAW-file-dependent tuning.
```

### July 2, 2026 - Build Base Readiness Item Added

```text
Scope: Add a UI-only Build Base readiness item to the Base Assist summary so the visible action summary matches the explicit Build Base action and Starting Point diagnostics readiness language.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Base Assist readiness text now includes Build Base ready/pending derived from the existing current-preview-analysis gate. No recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the longer Base Assist readiness summary wraps cleanly at narrow RAW side-panel widths.
```

### July 2, 2026 - Build Base Readiness Item Started

```text
Scope: Add a UI-only Build Base readiness item to the Base Assist summary so the visible action summary matches the explicit Build Base action and Starting Point diagnostics readiness language.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement gates, candidate selection/scoring, warning generation, recipe writes, automatic action boundaries, diagnostics payload structure, or RAW-file-dependent tuning.
```

### July 1, 2026 - Action-Readiness Diagnostics Added

```text
Scope: Add UI-readable Starting Point dry-run diagnostics for explicit Build Base, Add Local Range, and Add Mild Tone action readiness without changing action gates or applying recipe values.
Files touched: RawAutoStartPoint.cpp, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point dry-run diagnostics now include Build Base, Add Local Range, and Add Mild Tone action-readiness lines derived from existing candidates. This is diagnostics-only and still reports no recipe writes. No recipes, constants, action enablement gates, candidate selection, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, warning generation, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the action-readiness diagnostics line wrapping matches the Base Assist button readiness states.
```

### July 1, 2026 - Action-Readiness Diagnostics Started

```text
Scope: Add UI-readable Starting Point dry-run diagnostics for explicit Build Base, Add Local Range, and Add Mild Tone action readiness without changing action gates or applying recipe values.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source/test edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, action enablement gates, candidate selection/scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, warning generation, or RAW-file-dependent tuning.
```

### July 1, 2026 - Selected-Candidate Diagnostics Added

```text
Scope: Add selected-candidate score/reason detail to UI-readable Starting Point dry-run diagnostics without changing selection, scoring, or recipe application.
Files touched: RawAutoStartPoint.cpp, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point dry-run diagnostics now include the selected candidate's existing dry-run score and score summary in the Diagnostic selection detail. This is diagnostics-only and still reports no recipe writes. No recipes, constants, candidate selection, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, warning generation, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the selected-candidate score/reason detail wraps cleanly and helps review why Diagnostics preferred the dry-run candidate.
```

### July 1, 2026 - Selected-Candidate Diagnostics Started

```text
Scope: Add selected-candidate score/reason detail to UI-readable Starting Point dry-run diagnostics without changing selection, scoring, or recipe application.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source/test edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, candidate selection, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, warning generation, or RAW-file-dependent tuning.
```

### July 1, 2026 - Candidate Warning Diagnostics Added

```text
Scope: Add candidate-level warning count/detail lines to UI-readable Starting Point dry-run diagnostics without changing warning generation or applying recipe values.
Files touched: RawAutoStartPoint.cpp, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point dry-run diagnostics now include a per-candidate warnings line with the existing warning count and joined warning detail, or a zero-warning explanation. This is diagnostics-only and still reports no recipe writes. No recipes, constants, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, warning generation, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the new candidate warning lines wrap cleanly for longer fallback/readback warning text and make diagnostics easier to scan.
```

### July 1, 2026 - Candidate Warning Diagnostics Started

```text
Scope: Add candidate-level warning count/detail lines to UI-readable Starting Point dry-run diagnostics without changing warning generation or applying recipe values.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source/test edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, warning generation, or RAW-file-dependent tuning.
```

### July 1, 2026 - Candidate Visible-Control Diagnostics Added

```text
Scope: Add candidate-level visible-control ownership lines to UI-readable Starting Point dry-run diagnostics without applying recipe values.
Files touched: RawAutoStartPoint.cpp, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point dry-run diagnostics now include a per-candidate visible controls line naming the manual controls the candidate would touch, or None. This is diagnostics-only and still reports no recipe writes. No recipes, constants, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the new candidate visible-control lines wrap cleanly for long Display Fit / View Transform labels and make candidate ownership easier to scan in Diagnostics.
```

### July 1, 2026 - Candidate Visible-Control Diagnostics Started

```text
Scope: Add candidate-level visible-control ownership lines to UI-readable Starting Point dry-run diagnostics without applying recipe values.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source/test edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, or RAW-file-dependent tuning.
```

### July 1, 2026 - Starting Point Diagnostics View Regression Tests Added

```text
Scope: Add no-RAW regression coverage for UI-readable Starting Point diagnostics view source attribution, dry-run/no-recipe-write reporting, and fallback view behavior.
Files touched: StackSources.cmake, graph_behavior_tests.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Production behavior unchanged. StackGraphBehaviorTests now links RawAutoStartPoint.cpp and verifies unavailable, synthesized fallback, dry-run, serialized, and non-mutating Starting Point diagnostics-view behavior without RAW files or renderer readbacks.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Continue Pass 8 no-RAW-safe diagnostics/UI/readiness work; native visual QA should still confirm long Source lines and dry-run diagnostics wrap cleanly in the Diagnostics drawer.
```

### July 1, 2026 - Starting Point Diagnostics View Regression Tests Started

```text
Scope: Add no-RAW regression coverage for UI-readable Starting Point diagnostics view source attribution, dry-run/no-recipe-write reporting, and fallback view behavior.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source/test edit pending.
Validation planned: .\build.cmd, StackGraphBehaviorTests, layer registry validation, develop-node smoke validation, git diff whitespace checks, and touched-file trailing-whitespace sweep.
Guardrails: Do not change recipes, constants, candidate scoring, stage stats values, render output, default automatic behavior, hidden processing, automatic action boundaries, or RAW-file-dependent tuning.
```

### July 1, 2026 - Stage Readback Empty-State Wording Added

```text
Scope: Clarify the empty Starting Point stage-readback state and surface existing status text when no current readbacks are present.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: The Starting Point Stage Readbacks empty state now says no readbacks are present for the current RAW preview and shows the existing Starting Point diagnostics status text when available. No readback capture, stats domains, Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the empty stage-readback state reads correctly before a RAW preview has populated stage readbacks.
```

### July 1, 2026 - Stage Readback Empty-State Wording Started

```text
Scope: Clarify the empty Starting Point stage-readback state and surface existing status text when no current readbacks are present.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change readback capture, stats domains, Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Starting Point Diagnostics Title Rendering Added

```text
Scope: Render the existing UI-readable Build Starting Point diagnostics view title instead of a hard-coded diagnostics section title.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: The Build Starting Point Diagnostics report header now uses the existing diagnostics view title with a fallback to Build Starting Point Diagnostics. No Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm unavailable and dry-run Starting Point diagnostics use the expected header text and wrap cleanly in the Diagnostics drawer.
```

### July 1, 2026 - Starting Point Diagnostics Title Rendering Started

```text
Scope: Render the existing UI-readable Build Starting Point diagnostics view title instead of a hard-coded diagnostics section title.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Starting Point Source Attribution Added

```text
Scope: Add source-key attribution to the UI-readable Build Starting Point diagnostics view so dry-run evidence is self-identifying.
Files touched: RawAutoStartPoint.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Build Starting Point diagnostics view lines now include the existing diagnostics sourceKey when available for unavailable, dry-run, and synthesized fallback views. No Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the Source line wraps acceptably for long RAW relative paths in the Build Starting Point diagnostics report.
```

### July 1, 2026 - Starting Point Source Attribution Started

```text
Scope: Add source-key attribution to the UI-readable Build Starting Point diagnostics view so dry-run evidence is self-identifying.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Starting Point Fallback Wording Added

```text
Scope: Refresh stale fallback Build Starting Point diagnostics wording now that explicit visible-control actions exist.
Files touched: RawAutoStartPoint.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Fallback diagnostics-view text no longer says candidate reports are inert until a later solver pass or that visible recipe writes are future-only. It now says explicit starting-point actions write visible recipe controls and report rendering itself does not write recipe values. No Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm fallback Build Starting Point diagnostic wording reads as current explicit-action guidance when a diagnostics view has to be synthesized.
```

### July 1, 2026 - Starting Point Fallback Wording Started

```text
Scope: Refresh stale fallback Build Starting Point diagnostics wording now that explicit visible-control actions exist.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload structure, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Diagnostics Last Action Source Guard Added

```text
Scope: Guard the Diagnostics last visible automatic-action summary with the same selected-source and source-hash ownership checks used by Base Assist.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Diagnostics now renders the stored last visible automatic-action summary only when it belongs to the selected RAW source and current source hash. No Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload generation, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the Diagnostics drawer shows the last-action summary for the active RAW source and does not show it after switching to a different or changed source.
```

### July 1, 2026 - Diagnostics Last Action Source Guard Started

```text
Scope: Guard the Diagnostics last visible automatic-action summary with the same selected-source and source-hash ownership checks used by Base Assist.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload generation, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Base Assist Last Action Summary Added

```text
Scope: Render the existing last visible automatic-action summary inside the Base Assist panel for the selected RAW source.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Base Assist now shows the existing selected-source last visible automatic-action summary as passive disabled-style text after the Base Assist controls. No Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload generation, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the last-action summary wraps cleanly at narrow panel widths and reads as passive status, not an extra action.
```

### July 1, 2026 - Base Assist Last Action Summary Started

```text
Scope: Render the existing last visible automatic-action summary inside the Base Assist panel for the selected RAW source.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, diagnostics payload generation, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Starting Point Stage Evidence Summaries Added

```text
Scope: Render compact per-stage raw-safety, scene, and display evidence summaries from existing Starting Point candidate diagnostics.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Starting Point candidate stage evidence now shows compact existing raw-safety, scene, display, neutral-sample, status, and warning summaries when the corresponding diagnostics payload fields are already valid. No stage capture/readbacks, stats domains, stats values, candidate scoring, recommendation building, Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipes, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm candidate stage evidence remains readable in Diagnostics and that the compact summaries help review the later real-RAW validation runs.
```

### July 1, 2026 - Starting Point Stage Evidence Summaries Started

```text
Scope: Render compact per-stage raw-safety, scene, and display evidence summaries from existing Starting Point candidate diagnostics.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change stage capture/readbacks, stats domains, candidate scoring, stage stats values, recommendation building, Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipes, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Starting Point Diagnostics View Lines Added

```text
Scope: Render the existing UI-readable Starting Point diagnostics view lines in the Diagnostics drawer before the candidate details.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: The Build Starting Point Dry Run diagnostics now display the existing diagnostics-view lines such as state, mode, recipe-write status, candidate count, and diagnostic selection before candidate details. No candidate scoring, stage stats, recommendation building, Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipes, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the diagnostics-view lines remain readable in the expanded Diagnostics drawer and do not make candidate details feel duplicated.
```

### July 1, 2026 - Starting Point Diagnostics View Lines Started

```text
Scope: Render the existing UI-readable Starting Point diagnostics view lines in the Diagnostics drawer before the candidate details.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change candidate scoring, stage stats, recommendation building, Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipes, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Base Assist Readiness Summary Added

```text
Scope: Add a compact display-only readiness summary at the top of Base Assist using existing preview-analysis and candidate gate state.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Base Assist now shows a wrapped passive readiness summary for preview analysis, Display Fit, optional Local Range, optional Mild Tone, and Undo availability. No Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipes, candidate scoring, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the readiness summary wraps cleanly at narrow panel widths and reads as passive status rather than an extra action.
```

### July 1, 2026 - Base Assist Readiness Summary Started

```text
Scope: Add a compact display-only readiness summary at the top of Base Assist using existing preview-analysis and candidate gate state.
Files touched: pass-11-validation-notes.md, implementation-progress.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, suggestion application, undo/revert snapshots, recipe writes, candidate scoring, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Suggestion Applied Status Density Added

```text
Scope: Render already-applied suggestion popout entries as passive Applied status text instead of disabled Applied buttons, while preserving normal Apply behavior for actionable suggestions.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: Already-applied and applied-only suggestion popout rows now show passive Applied text with an applied-state tooltip instead of a disabled Applied button. Actionable suggestions still use the same Apply button and applySuggestion path, and disabled Apply for non-editable projects still uses the same disabled behavior. No suggestion discovery, suggestion application, applied-suggestion tracking, recipes, undo/revert snapshots, candidate scoring, constants, default automatic behavior, render output, hidden processing, hover preview, pinned preview, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm applied suggestion rows read as status, not unavailable actions, in the suggestions expander.
```

### July 1, 2026 - Suggestion Applied Status Density Started

```text
Scope: Render already-applied suggestion popout entries as passive Applied status text instead of disabled Applied buttons, while preserving normal Apply behavior for actionable suggestions.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change suggestion discovery, suggestion application, applied-suggestion tracking, recipes, undo/revert snapshots, candidate scoring, constants, default automatic behavior, render output, hidden processing, hover preview, pinned preview, or RAW-file-dependent tuning.
```

### July 1, 2026 - Base Assist Strip Label Added

```text
Scope: Rename the readiness strip heading from Display Fit / View Transform to Base Assist so the strip honestly covers Analyze/Fit Display, Build Base, and optional starting-point add-ons.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: The readiness strip now uses the broader Base Assist label, and the no-RAW-selected status matches it. Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, undo/revert snapshots, recipe writes, candidate scoring, constants, default automatic behavior, render output, hidden processing, and RAW-file-dependent tuning are unchanged.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the Base Assist heading reads clearly as the assistant/readiness strip and does not obscure that Display Fit remains an editable View Transform control in Base Light.
```

### July 1, 2026 - Base Assist Strip Label Started

```text
Scope: Rename the readiness strip heading from Display Fit / View Transform to Base Assist so the strip honestly covers Analyze/Fit Display, Build Base, and optional starting-point add-ons.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Analyze, Fit Display, Build Base, Balanced Local, Mild Tone, undo/revert snapshots, recipe writes, candidate scoring, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Starting Point Optional Action Density Added

```text
Scope: Keep Build Base as the primary starting-point action while reducing Add Balanced Local and Add Mild Tone from full-width peer actions to smaller secondary optional-step affordances.
Files touched: EditorModuleRawWorkspaceAutoBase.cpp, pass-11-validation-notes.md, implementation-progress.md
Behavior changed: The readiness strip still uses the same Build Base, Balanced Local, and Mild Tone apply paths and the same enable/disable gates, but optional Local Range and Mild Tone add-ons now render as compact secondary buttons beside each other instead of full-width peers to Build Base. No Build Base, Balanced Local, Mild Tone, undo/revert snapshots, recipe writes, candidate scoring, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the compact optional actions remain discoverable and fit cleanly at narrow panel widths.
```

### July 1, 2026 - Starting Point Optional Action Density Started

```text
Scope: Keep Build Base as the primary starting-point action while reducing Add Balanced Local and Add Mild Tone from full-width peer actions to smaller secondary optional-step affordances.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Build Base, Balanced Local, Mild Tone, undo/revert snapshots, recipe writes, candidate scoring, constants, default automatic behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Local Range Mask View Action Density Added

```text
Scope: Reduce the Color Target and Region Mask Show Mask controls from full-width actions to smaller secondary View Mask affordances while preserving existing overlay behavior.
Files touched: EditorModuleRawWorkspaceLocalRange.cpp, pass-11-validation-notes.md
Behavior changed: Color Target and Region Mask now use compact View Mask buttons that still switch the Local Range overlay mode to region-mask, clear overlay state, and request render refresh through the existing path. No Local Range recipe values, color target behavior, region mask behavior, overlay mode semantics, mask generation, graph math, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the compact View Mask buttons remain discoverable and fit at narrow panel widths.
```

### July 1, 2026 - Local Range Mask View Action Density Started

```text
Scope: Reduce the Color Target and Region Mask Show Mask controls from full-width actions to smaller secondary View Mask affordances while preserving existing overlay behavior.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Local Range recipe values, color target behavior, region mask behavior, overlay mode semantics, mask generation, graph math, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Local Range Use Sample Guidance Added

```text
Scope: Replace the disabled full-width Use Sample action in Local Range Color Target with passive guidance when no target sample exists, while preserving the enabled Use Sample behavior.
Files touched: EditorModuleRawWorkspaceLocalRange.cpp, pass-11-validation-notes.md
Behavior changed: Local Range Color Target now shows passive guidance to use Target first when no sampled color is available. When a target sample exists, Use Sample still copies the target color into Color Target, enables the color mask, switches the overlay to the mask, clears overlay state, and marks render refresh dirty through the existing path. No Local Range target sampling, color target values, Use Sample behavior with a sample, overlay behavior, graph math, recipes, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the passive guidance reads cleanly in the Color Target drawer and the enabled Use Sample action remains discoverable after targeting.
```

### July 1, 2026 - Local Range Use Sample Guidance Started

```text
Scope: Replace the disabled full-width Use Sample action in Local Range Color Target with passive guidance when no target sample exists, while preserving the enabled Use Sample behavior.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Local Range target sampling, color target values, Use Sample behavior when a sample exists, overlay behavior, graph math, recipes, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Compare Placeholder Status Added

```text
Scope: Replace the disabled Compare toolbar button with a compact passive deferred-status label while preserving the honest deferred Compare state.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: The center view toolbar now shows Compare as passive disabled text with the same deferred-state tooltip instead of a disabled button. No Compare preview state, active view-mode behavior, recipes, Local Range overlay behavior, Risk diagnostics routing, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the passive Compare label reads as unavailable without increasing toolbar clutter at narrow preview widths.
```

### July 1, 2026 - Compare Placeholder Status Started

```text
Scope: Replace the disabled Compare toolbar button with a compact passive deferred-status label while preserving the honest deferred Compare state.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not implement Compare preview state, change active view-mode behavior, mutate recipes, change Local Range overlay behavior, alter Risk diagnostics routing, change render output, add hidden processing, or perform RAW-file-dependent tuning.
```

### July 1, 2026 - White Balance Gray Point Placeholder Added

```text
Scope: Replace the disabled full-width Gray Point picker placeholder with a compact passive status label while preserving manual sample coordinate editing.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: White Balance Gray Point mode now shows a passive "Gray-point picker pending" status instead of a disabled full-width Pick Gray Point button. Manual Gray Point X/Y sliders remain unchanged. No White Balance mode, sample coordinates, temperature/tint, RGB multipliers, picker backend behavior, recipes, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the passive status label reads clearly at narrow panel widths and does not look like a broken unavailable command.
```

### July 1, 2026 - White Balance Gray Point Placeholder Started

```text
Scope: Replace the disabled full-width Gray Point picker placeholder with a compact passive status label while preserving manual sample coordinate editing.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change White Balance mode, sample coordinates, temperature/tint, RGB multipliers, picker backend behavior, recipes, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Base Light Reset Density Added

```text
Scope: Reduce the Base Light Advanced reset action from a full-width dominant button to a smaller secondary affordance while preserving the existing View Transform reset behavior.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: Base Light Advanced now renders Reset View Transform as a small secondary action. The explicit reset still assigns DefaultViewTransformJson and marks View Transform as user-edited through the existing path. No RAW Exposure, Display Fit/View Transform values outside the explicit reset click, refit behavior, ownership marking, recipes, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the smaller reset remains discoverable in Base Light Advanced without competing with Refit Display.
```

### July 1, 2026 - Base Light Reset Density Started

```text
Scope: Reduce the Base Light Advanced reset action from a full-width dominant button to a smaller secondary affordance while preserving the existing View Transform reset behavior.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change RAW Exposure, Display Fit/View Transform values except through the existing explicit reset click, refit behavior, ownership marking, recipes, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Local Range Reset Density Added

```text
Scope: Reduce Local Range Reset from an equal-weight action beside Target to a smaller secondary affordance while preserving the existing reset behavior.
Files touched: EditorModuleRawWorkspaceLocalRange.cpp, pass-11-validation-notes.md
Behavior changed: Local Range now gives Target the primary row width and renders Reset as a small secondary action. Reset still uses the existing RawLocalRangePreset::Reset path. No Local Range target mode, reset preset semantics, graph math, recipe defaults, overlay behavior, color target behavior, region mask behavior, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the Target/Reset row fits cleanly at narrow panel widths and that Reset still feels discoverable without dominating the graph controls.
```

### July 1, 2026 - Local Range Reset Density Started

```text
Scope: Reduce Local Range Reset from an equal-weight action beside Target to a smaller secondary affordance while preserving the existing reset behavior.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Local Range target mode, reset preset semantics, graph math, recipe defaults, overlay behavior, color target behavior, region mask behavior, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Main And Graph Group Labels Added

```text
Scope: Add explicit Main Controls and Graph Controls group labels around the existing Base Light/White Balance/Detail and Local Range/Finish Tone sections.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: The RAW side panel now labels the workflow groups before the existing Main Controls stack and the graph-backed Local Range/Finish Tone stack. This is display-only; existing section order, default-open behavior, recipes, suggestion application, graph editing, render output, hidden processing, and RAW-file-dependent tuning are unchanged.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the new group labels improve scanability without adding too much vertical weight at narrow panel widths.
```

### July 1, 2026 - Main And Graph Group Labels Started

```text
Scope: Add explicit Main Controls and Graph Controls group labels around the existing Base Light/White Balance/Detail and Local Range/Finish Tone sections.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not reorder controls, change default-open behavior, mutate recipes, change suggestion application, alter graph editing, change render output, add hidden processing, or perform RAW-file-dependent tuning.
```

### July 1, 2026 - Finish Tone Summary And Reset Density Added

```text
Scope: Add a compact, UI-only Finish Tone summary row using the existing mode/domain/point data, and reduce Reset Curve from a visually dominant full-width action to a secondary advanced action.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: Finish Tone now shows a compact summary such as Mode: RGB, Domain: Log Scene, and point count before the editable mode/domain controls. Reset Curve remains in Advanced with the same reset semantics, but now renders as a small secondary action instead of a full-width dominant button. No Finish Tone points, mode, domain, graph editing behavior, reset semantics, recipe defaults, render output, hidden processing, or RAW-file-dependent tuning changed automatically.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the summary and small reset action fit cleanly at narrow panel widths and that Reset Curve still preserves the active mode/domain after use.
```

### July 1, 2026 - Finish Tone Summary And Reset Density Started

```text
Scope: Add a compact, UI-only Finish Tone summary row using the existing mode/domain/point data, and reduce Reset Curve from a visually dominant full-width action to a secondary advanced action.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Finish Tone points, mode, domain, graph editing behavior, reset semantics, recipe defaults, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - White Balance Summary Row Added

```text
Scope: Add a compact, UI-only White Balance summary row using existing WB recipe mode and stored manual/sample values.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: White Balance now shows a compact WB summary before the editable mode control, including As Shot, Auto, Custom temperature/tint or RGB multipliers, and Gray Point sample coordinates when present. This is display-only. No White Balance mode, multipliers, temperature, tint, sample point behavior, recipe values, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm WB summaries wrap cleanly at narrow panel widths and match the visible controls after mode changes.
```

### July 1, 2026 - White Balance Summary Row Started

```text
Scope: Add a compact, UI-only White Balance summary row using existing WB recipe mode and stored manual/sample values.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change White Balance mode, multipliers, temperature, tint, sample point behavior, recipe values, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Base Light Summary Row Added

```text
Scope: Add a compact, UI-only Base Light summary row showing RAW Exposure and Display Fit state from existing recipe and ownership data.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: Base Light now shows a compact summary such as RAW Exposure +0.00 EV and Display Fit: Auto/Manual/Default before the editable controls. This is display-only and uses existing ownership state; Display Fit staleness thresholds remain deferred. No RAW Exposure values, View Transform values, refits, recipe mutation, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the summary wraps cleanly at narrow panel widths and that Auto/Manual/Default labels match expected ownership states.
```

### July 1, 2026 - Base Light Summary Row Started

```text
Scope: Add a compact, UI-only Base Light summary row showing RAW Exposure and Display Fit state from existing recipe and ownership data.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not add Display Fit staleness thresholds, change RAW Exposure, change View Transform values, apply refits, mutate recipes, change render output, add hidden processing, or perform RAW-file-dependent tuning.
```

### July 1, 2026 - Local Range Conditional Default Open Added

```text
Scope: Make the Local Range section default-open only when Local Range is active or suggested, matching the default-open rules while preserving user-toggled ImGui state.
Files touched: EditorModule.h, EditorModuleRawWorkspace.cpp, EditorModuleRawWorkspaceLocalRange.cpp, pass-11-validation-notes.md
Behavior changed: Local Range now receives a default-open hint from the controls panel and defaults open when Local Range is active, legacy Local Exposure is active, or a current Local Range suggestion marker exists. Otherwise it is compact by default. This only changes initial side-panel header state. No Local Range recipe values, graph math, target sampling, overlay rendering, suggestion application, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm default RAWs show Local Range compact, while active Local Range and Local Range suggestions default it open on first display.
```

### July 1, 2026 - Local Range Conditional Default Open Started

```text
Scope: Make the Local Range section default-open only when Local Range is active or suggested, matching the default-open rules while preserving user-toggled ImGui state.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Local Range recipe values, graph math, target sampling, overlay rendering, suggestion application, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Compact RAW Top Identity Badges Added

```text
Scope: Render the selected RAW filename and compact project/mode/status badges in the RAW controls top area using existing panel state.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: The RAW controls top area now shows the selected RAW filename on a clipped single line with full path tooltip, plus compact badges for project status, recipe/graph mode, and transient loading/saving/unsaved/read-only state. Existing Save, More, and Suggestions actions remain below the identity row. No project creation, loading, saving, graph conversion, recipe values, render output, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm long filenames, long workspace paths, warning badges, and narrow panel wrapping remain readable.
```

### July 1, 2026 - Compact RAW Top Identity Badges Started

```text
Scope: Render the selected RAW filename and compact project/mode/status badges in the RAW controls top area using existing panel state.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change project creation, loading, saving, graph conversion, recipe values, render output, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Local Range Overlay Segmented Control Added

```text
Scope: Replace the Local Range overlay combo with a compact segmented view-mode row, matching the Local Range overlay/view-mode control guidance.
Files touched: EditorModuleRawWorkspaceLocalRange.cpp, pass-11-validation-notes.md
Behavior changed: Local Range overlay selection now uses stable Off, Affected, Delta, and Mask buttons with per-mode tooltips and a visible selected state. The same overlay mode strings, overlay refresh path, and preview-only behavior are preserved. No Local Range recipe values, overlay rendering semantics, mask generation, target sampling, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the segmented row fits at narrow panel widths and that selecting each mode still drives the preview overlay expected by the center toolbar.
```

### July 1, 2026 - Local Range Overlay Segmented Control Started

```text
Scope: Replace the Local Range overlay combo with a compact segmented view-mode row, matching the Local Range overlay/view-mode control guidance.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change Local Range recipe values, overlay rendering semantics, mask generation, target sampling, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Crop / Output Summary Rows Added

```text
Scope: Add compact, UI-only summary rows for collapsed Crop & Rotate and Preview & Output sections, matching the Geometry summary guidance and keeping output settings out of the primary edit flow.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: Crop & Rotate now shows a compact summary such as Crop off or active crop percentage plus rotation, and Preview & Output now shows preview intent plus output color space below its header. These rows are display-only. No crop/output recipe values, render output, saving, preview intent, output color space, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm summary wrapping at narrow panel widths and that expanded controls remain unchanged for crop, rotation, preview intent, and output color space.
```

### July 1, 2026 - Crop / Output Summary Rows Started

```text
Scope: Add compact, UI-only summary rows for collapsed Crop & Rotate and Preview & Output sections, matching the Geometry summary guidance and keeping output settings out of the primary edit flow.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change crop/output recipe values, render output, saving, preview intent, output color space, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Crop / Output Default Collapse Added

```text
Scope: Make Crop & Rotate and Preview & Output default-collapsed unless their recipe values are active or non-default, matching the contract default-open rules.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: Crop & Rotate now defaults open only when crop/rotation values are active; Preview & Output now defaults open only when preview intent or output color space are non-default. This only changes initial side-panel header state. No crop/output recipe values, render output, saving, preview intent, output color space, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm default recipes show both sections collapsed, while active crop/rotation and non-default output settings default them open on first display.
```

### July 1, 2026 - Crop / Output Default Collapse Started

```text
Scope: Make Crop & Rotate and Preview & Output default-collapsed unless their recipe values are active or non-default, matching the contract default-open rules.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not change crop/output recipe values, render output, saving, preview intent, output color space, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Owning Control Suggestion Markers Added

```text
Scope: Add compact, non-mutating suggestion/applied markers near the owning Base Light, White Balance, and Local Range controls while keeping full rationale in the suggestions expander and Diagnostics.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: Base Light/RAW Exposure, Display Fit, White Balance, and Local Range now show compact Suggested/Applied markers when current suggestion items target those sections; each marker has a View button that only opens the existing suggestions expander. No recipes, render output, hover preview, graph ghost rendering, pinned preview, batch apply, hidden processing, or RAW analysis algorithms changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm marker wrapping at narrow panel widths and that marker View buttons simply open the suggestions expander.
```

### July 1, 2026 - Owning Control Suggestion Markers Started

```text
Scope: Add compact, non-mutating suggestion/applied markers near the owning Base Light, White Balance, and Local Range controls while keeping full rationale in the suggestions expander and Diagnostics.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not add hover preview, graph ghost rendering, pinned preview, batch apply, recipe mutation from markers, render mutation, hidden processing, or new RAW analysis algorithms.
```

### July 1, 2026 - Center View Deferred Mode Affordances Added

```text
Scope: Add honest center-view affordances for deferred Compare and Highlight Risk modes without implementing fake preview layers.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: The center toolbar now shows a disabled Compare slot with a tooltip explaining that stable before/after RAW preview state is deferred, and the Risk button opens Diagnostics where Highlight Risk currently lives. No recipes, render output, preview-state compare, highlight-risk overlay rendering, hover preview, pinned preview, hidden processing, or RAW analysis algorithms changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should confirm the toolbar still fits at narrow preview widths and that Risk opens Diagnostics in the app.
```

### July 1, 2026 - Center View Deferred Mode Affordances Started

```text
Scope: Add honest center-view affordances for deferred Compare and Highlight Risk modes without implementing fake preview layers.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not add preview-state compare, highlight-risk overlay rendering, recipe mutation, render mutation, hover preview, pinned preview, hidden processing, or RAW-file-dependent tuning.
```

### July 1, 2026 - Suggestion / Warning Badge Entry Added

```text
Scope: Make the compact RAW top suggestion entry point show applyable suggestion and warning/advisory counts, and route warning-only clicks to Diagnostics.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: The RAW top suggestion button now labels known states as No suggestions, N suggestions, N warnings, or N suggestions + M warnings. Warning-only states open Diagnostics instead of an empty suggestion expander. No recipes, render output, hover preview, pinned preview, batch apply, hidden processing, or RAW analysis algorithms changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA should check long combined labels at narrow panel widths and confirm warning-only clicks open Diagnostics in the app.
```

### July 1, 2026 - Suggestion / Warning Badge Entry Started

```text
Scope: Make the compact RAW top suggestion entry point show applyable suggestion and warning/advisory counts, and route warning-only clicks to Diagnostics.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and existing no-RAW smoke validations if the build passes.
Guardrails: Do not add hover preview, pinned preview, batch apply, recipe mutation, render mutation, hidden processing, or new RAW analysis algorithms.
```

### July 1, 2026 - Detail / Noise Advisory Row Added

```text
Scope: Surface the already-computed Detail / Noise recommendation as a compact advisory row in Main Controls when relevant.
Files touched: EditorModuleRawWorkspace.cpp, pass-11-validation-notes.md
Behavior changed: Relevant noise/detail recommendations now appear between White Balance and Graph Controls as an advisory-only row with a Diagnostics jump. The suggestion warning count includes this advisory. No recipes, render output, RAW analysis algorithms, denoise/detail controls, hidden processing, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed.
Follow-up: Native visual QA still needs a relevant high-ISO/shadow-lift source to confirm the row placement and wrapping; new editable Detail/Noise controls remain deferred.
```

### July 1, 2026 - Detail / Noise Advisory Row Started

```text
Scope: Surface the already-computed Detail / Noise recommendation as a compact advisory row in Main Controls when relevant.
Files touched: pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, focused diff review, and UI checklist review for advisory-only behavior.
Guardrails: Do not add hidden denoise/detail processing, disabled denoise buttons, recipe mutation, new RAW analysis algorithms, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Selected Candidate Completeness Row Coverage Started

```text
Scope: Add existing selected-candidate Diagnostic selection line/detail coverage checks to each per-record diagnostic completeness row as non-gating validation-summary fields.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, graph behavior tests, layer registry validation, develop-node smoke, focused synthetic raw-starting-point summary smoke, git diff --check, touched-file whitespace sweep, and focused guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Selected Candidate Completeness Row Coverage Added

```text
Scope: Add existing selected-candidate Diagnostic selection line/detail coverage checks to each per-record diagnostic completeness row as non-gating validation-summary fields.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None; validation/reporting only. recordDiagnosticCompleteness rows now include hasSelectedCandidateDetailLine, hasSelectedCandidateVisibleControlDetail, selectedCandidateDetailCoverageIsGating=false, missingSelectedCandidateDetailLine, and missingSelectedCandidateVisibleControlDetail.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed and reported rowHasDetailLine=True, rowHasVisibleControlDetail=True, rowCoverageIsGating=False, uiDiagnosticSetComplete=True, validationSetSummaryVersion=24, and summaryReportVersion=25; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no apply/recipe write paths in the touched validation code.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Candidate Score Order Summary Coverage Started

```text
Scope: Add non-gating validation-summary coverage for the existing Candidate score order UI line and its scan-only guardrail detail.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, graph behavior tests, layer registry validation, develop-node smoke, focused synthetic raw-starting-point summary smoke, git diff --check, touched-file whitespace sweep, and focused guardrail search.
Guardrails: Do not change recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Candidate Score Order Summary Coverage Added

```text
Scope: Add non-gating validation-summary coverage for the existing Candidate score order UI line and its scan-only guardrail detail.
Files touched: RawStartingPointValidation.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None; validation/reporting only. Validation summaries now report recordsWithCandidateScoreOrderLine, recordsWithCandidateScoreOrderGuardrailDetail, candidateScoreOrderCoverageComplete, candidateScoreOrderCoverageIsGating=false, candidateScoreOrderCoverage details, and matching non-gating per-record diagnostic completeness row fields.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; focused synthetic --summarize-raw-starting-point-records smoke passed and reported rowHasScoreOrderLine=True, rowHasScoreOrderGuardrail=True, rowCoverageIsGating=False, summaryCoverageIsGating=False, summaryCoverageComplete=True, uiDiagnosticSetComplete=True, recordsWithScoreOrderLine=1, recordsWithScoreOrderGuardrail=1, validationSetSummaryVersion=25, and summaryReportVersion=26; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no apply/recipe write paths in the touched validation code.
Guardrails: No recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Starting Point Diagnostics Header Wrapping Added

```text
Scope: Make existing Starting Point diagnostic line headers wrap in the Diagnostics drawer so long severity/label/value rows remain readable at narrow panel widths.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Existing Starting Point diagnostics line headers now render as wrapped disabled text instead of a single clipped disabled text row. The header text is built from the same severity, label, and value fields and details remain unchanged. No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths in the touched source file.
Guardrails: No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Diagnostics Selected-Candidate Top Summary Started

```text
Scope: Surface the selected dry-run Starting Point candidate near the top of the Diagnostics report with its visible-control handoff before the long per-candidate evidence list.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, graph behavior tests, layer registry validation, develop-node smoke, git diff --check, touched-file whitespace sweep, and focused guardrail search.
Guardrails: Do not change diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Diagnostics Selected-Candidate Top Summary Added

```text
Scope: Surface the selected dry-run Starting Point candidate near the top of the Diagnostics report with its visible-control handoff before the long per-candidate evidence list.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. The Starting Point Diagnostics report now shows a read-only Selected Candidate block near the top, including the existing selected candidate label, selected score when available, candidate summary, visible-control handoff, and dry-run/applied recipe state. The existing per-candidate evidence list and bottom Diagnostic selection line remain unchanged. No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths in the touched source file.
Guardrails: No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Diagnostics Candidate Evidence Disclosure Started

```text
Scope: Make the long per-candidate Starting Point evidence list scannable by rendering each candidate under a disclosure row, with the selected candidate opened by default.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, graph behavior tests, layer registry validation, develop-node smoke, git diff --check, touched-file whitespace sweep, and focused guardrail search.
Guardrails: Do not change diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

### July 2, 2026 - Pass 8 Diagnostics Candidate Evidence Disclosure Added

```text
Scope: Make the long per-candidate Starting Point evidence list scannable by rendering each candidate under a disclosure row, with the selected candidate opened by default.
Files touched: EditorModuleRawWorkspaceAnalysis.cpp, implementation-progress.md, pass-11-validation-notes.md
Behavior changed: UI-only. Starting Point Diagnostics now groups each candidate's existing summary, visible-control handoff, score, warnings, and stage evidence under a Candidate Evidence disclosure row. The selected candidate is marked and default-open. No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
Validation: .\build.cmd passed; .\build\StackGraphBehaviorTests.exe passed; .\build\Stack.exe --validate-layer-registry passed; .\build\Stack.exe --validate-develop-node-smoke passed; git diff --check passed with CRLF warnings only; touched-file trailing-whitespace sweep passed; focused guardrail search found no recipe/apply/write paths in the touched source file.
Guardrails: No diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning changed.
```

### July 2, 2026 - Pass 8 Starting Point Diagnostics Header Wrapping Started

```text
Scope: Make existing Starting Point diagnostic line headers wrap in the Diagnostics drawer so long severity/label/value rows remain readable at narrow panel widths.
Files touched: implementation-progress.md, pass-11-validation-notes.md
Behavior changed: None yet; source edit pending.
Validation planned: .\build.cmd, graph behavior tests, layer registry validation, develop-node smoke, git diff --check, touched-file whitespace sweep, and focused guardrail search.
Guardrails: Do not change diagnostics content or serialization, recipes, constants, render output, default automatic behavior, hidden processing, action enablement, candidate selection/scoring, warning generation, recipe writes, validation readiness thresholds, validation gates, or RAW-file-dependent tuning.
```

## Known Deferred Behavior

Passes 00 through 10 kept behavior changes scoped to the first layout overhaul. These items remain intentionally deferred:

- Center `Compare` mode is not implemented yet. The toolbar exposes `Final`, `Affected`, `Delta`, and `Mask`; unavailable risk overlay remains disabled with a tooltip.
- Highlight Risk remains a Diagnostics item until there is a viewport overlay backend for it.
- Suggestions support click-to-apply from the RAW top expander, but hover preview, pinned compare, and batch suggestion review remain deferred.
- Pass 10 used compact text/ellipsis affordances rather than adding a new icon font or command icon asset system. Structural, destructive, and ambiguous project actions remain textual.

These limitations are accepted for the first UI structure pass because adding preview-state compare, highlight-risk overlays, hover preview tokens, batch review, or a new shared icon system would expand the scope beyond the layout and clarity goals of this implementation set.
