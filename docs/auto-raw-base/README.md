# Stack Auto RAW Base Implementation Packet

This folder contains the implementation plan for turning the Deep Research report into Stack features. The original report is preserved as:

- `deep-research-report.md`
- `implementation-crosswalk.md`

The pass documents are written for direct implementation. They define file ownership, data structures, algorithms, UI behavior, validation steps, and acceptance criteria. Treat them as the working specification for Auto Base.

## Current Stack Baseline

As of this packet, Stack already has these relevant pieces:

- A persisted RAW development recipe in `src/Raw/RawDevelopmentRecipe.h` and `src/Raw/RawDevelopmentRecipe.cpp`.
- A RAW workspace UI in `src/Editor/Internal/EditorModuleRawWorkspace.cpp`.
- A render worker path in `src/Editor/EditorRenderWorker.cpp` and `src/Editor/EditorRenderWorker.h`.
- A render pipeline with readback/stat helpers in `src/Renderer/RenderPipeline.h` and `src/Renderer/Internal/RenderPipelineReadback.cpp`.
- A RAW development graph node in `src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp`.
- Local Range luminance targeting from image drag.
- Local Range color qualification using sampled scene-linear RGB, color width, feather, and neutral/chroma guard.
- Current RAW View Transform auto-fit stats sampled immediately before the View Transform.
- Behavior coverage in `tools/graph_behavior_tests.cpp`.

The implementation passes below assume that baseline.

## Non-Negotiable Auto Base Contract

This contract overrides any ambiguous detail in the pass documents.

1. Auto Base must make the RAW readable and technically sane; it must not create a hidden style/look engine.
2. Every value Auto Base changes must be a normal visible Stack control or recipe value.
3. Every Auto Base change must have a summary message and must be reversible in one action when applied from the RAW tab.
4. User edits win. Once the user edits a control, Auto Base must not keep refitting or rewriting that control for the same source unless the user explicitly runs Auto Base again.
5. Saved/existing RAW project recipes must not be silently overwritten on load. Auto Base may analyze them and offer `Apply Auto Base`, but default auto-application is only for new/default recipes.
6. View Transform Auto Fit is the primary automatic readability action.
7. Content-derived RAW Exposure changes are suggestions by default. Treat `autoApplyAllowed` as permission for future opt-in behavior, not as a command to silently change exposure in v1.
8. Camera/as-shot white balance is the default when available. Alternate white balance candidates are suggestions unless camera WB is absent or invalid.
9. Local Range edits are suggestions by default. They may only be created automatically if a future explicit user setting such as `Apply suggested local edits on load` exists.
10. Highlight reconstruction is a suggestion. Do not silently enable heavy reconstruction or inpainting.
11. Denoise/detail changes are suggestions by default unless minimal high-ISO chroma cleanup is visibly marked and supported by existing editable controls.
12. Strong sharpening, saturation, dehaze, skin edits, creative looks, and aggressive local dodge/burn must never be silently auto-applied.

If a future implementation cannot satisfy visibility, editability, reversibility, and user-ownership rules, it should stop at diagnostics/suggestions.

## Current Architecture Risk

A repo inspection before implementation showed several places where Auto Base could make already-large files harder to maintain if new work is simply appended:

| File | Current role | Risk for Auto Base |
|---|---|---|
| `src/Editor/Internal/EditorModuleRawWorkspace.cpp` | RAW workspace loading, browser/gallery UI, thumbnail texture uploads, app-state persistence, preview staging, controls panel, preview panel, Local Range overlay/target interaction | Already around 3800 lines and mixes UI, state transitions, GL texture handling, persistence, and recipe editing. Do not add Auto Base panels, suggestion UI, and analysis glue here without splitting. |
| `src/Editor/EditorModule.h` | Global editor declaration and state for many subsystems | RAW workspace fields already occupy a large contiguous state block. Auto Base state should be grouped in small structs, preferably declared in `EditorModuleTypes.h` or a RAW workspace UI state header. |
| `src/Editor/EditorRenderWorker.h` and `.cpp` | General render worker plus previews, composite outputs, develop candidate rendering, RAW workspace overlay/sample payloads | RAW workspace fields are currently appended directly to `Snapshot` and `Result`. Auto Base analysis/recommendations should be transported through grouped RAW workspace payload structs. |
| `src/Renderer/RenderPipeline.h` and `src/Renderer/Internal/RenderPipelineReadback.cpp` | General render pipeline plus RAW development overlay/sample/view-transform readback | Analysis readback should be grouped as RAW development readback state, not a growing list of unrelated public getters. |
| `src/Renderer/Internal/RenderPipelinePrograms.cpp` | Many shader program definitions, including RAW development Local Range and overlay shaders | Avoid adding analysis shaders here unless needed. Prefer CPU analysis from low-resolution readbacks first. If shader code grows, split RAW development shader/program setup into a dedicated renderer file. |

The implementation passes therefore include organization work, not just feature work.

## Organization Targets

Use these boundaries while implementing Auto Base:

| Area | Preferred location | Notes |
|---|---|---|
| Pure image statistics, masks, clipping, percentiles | `src/Raw/RawImageAnalysis.h/.cpp` | No ImGui, no editor state, no GL ownership. Functions should be unit-testable. |
| Auto Base decisions and recommendation math | `src/Raw/RawAutoBase.h/.cpp` | No ImGui. Takes `RawImageAnalysis` plus recipe/current settings and returns decisions/suggestions. |
| RAW workspace Auto Base UI glue | `src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp` | Summary banner, apply/revert buttons, suggestion chip rendering, adoption of worker results. |
| RAW workspace controls panel split | `src/Editor/Internal/EditorModuleRawWorkspaceControls.cpp` | Move `RenderRawWorkspaceControlsPanel` and its large control-section helpers out of `EditorModuleRawWorkspace.cpp`. |
| Local Range UI and target interaction | `src/Editor/Internal/EditorModuleRawWorkspaceLocalRange.cpp` | Local Range controls, overlay mode UI, target sample adoption, drag interaction, suggestion application. |
| RAW workspace preview panel | `src/Editor/Internal/EditorModuleRawWorkspacePreview.cpp` | Preview drawing, overlay drawing, image-fit helpers, local target interaction calls. |
| RAW workspace browser/gallery | `src/Editor/Internal/EditorModuleRawWorkspaceBrowser.cpp` | Empty state, folder/source browser, grid/list/filmstrip layout. |
| RAW workspace lifecycle/persistence | Existing `EditorModuleProjectLifecycle.cpp` plus optional `EditorModuleRawWorkspaceLifecycle.cpp` | Source selection, staging, project state, app-state persistence, catalog persistence. |
| Render-worker RAW workspace payloads | `EditorRenderWorker::RawWorkspaceSnapshot` and `EditorRenderWorker::RawWorkspaceResult` | Group existing overlay/sample/stats fields before adding analysis/recommendations. |
| Renderer RAW readback state | `RenderPipelineRawDevelopmentReadback.cpp` or grouped structs in `RenderPipeline.h` | Keep generic output readback separate from RAW development analysis/sample/overlay readback. |

Do not block the feature on a perfect full refactor. Use a strangler pattern:

1. Create the new module/file.
2. Move one coherent responsibility at a time.
3. Keep public `EditorModule` method names stable if that avoids broad churn.
4. Add Auto Base code only to the new module after its boundary exists.
5. Keep each new file below roughly 800-1000 lines unless there is a strong reason.

## Product Rule

Auto Base is not an auto-enhance filter.

Every applied value must be visible, editable, reversible, and explainable. Strong interpretive changes should appear as suggestions, not hidden edits.

Use this policy everywhere:

| Action type | Default behavior |
|---|---|
| Metadata-backed technical normalization | Apply automatically |
| View Transform fit for readability | Apply automatically, visibly |
| Content-derived RAW exposure | Suggest unless confidence is very high and headroom is safe |
| Alternate white balance | Suggest unless camera WB is absent or invalid |
| Highlight reconstruction | Suggest |
| Local Range edits | Suggest |
| Strong denoise, sharpening, saturation, dehaze, style looks | Never silently auto-apply |

## Pass Order

1. `pass-01-analysis-foundation.md`
   - Build the shared analysis model, valid-pixel masking, percentiles, clipping stats, grouped RAW worker payloads, and the first RAW workspace file split.

2. `pass-02-auto-base-view-transform.md`
   - Apply Auto Base view fitting on RAW load, mark controls as auto-set, add summary/revert behavior, make current-frame fit stable, and keep Auto Base UI glue out of the main RAW workspace file.

3. `pass-03-exposure-wb-highlight-recommendations.md`
   - Add conservative RAW exposure suggestions, white-balance candidate suggestions, and sensor/display highlight risk reporting.

4. `pass-04-local-range-suggestions-and-color.md`
   - Add suggestion actions for shadow lift, sky protection, backlit subject opening, highlight recovery, and foliage brightening using editable Local Range recipes.
   - In the newer RAW tab UI direction, these actions are reviewed in the suggestions popout/expander, while affected controls show compact local markers or ghost graph points.

5. `pass-05-noise-detail-validation.md`
   - Add noise/detail defaults, define the validation corpus, and lock in regression testing for Auto Base behavior.

## Shared Vocabulary

Use these names consistently in code and UI:

- `Auto Base`: the safe baseline applied or offered at RAW load.
- `Analyze Image`: compute analysis without necessarily applying anything.
- `Apply Auto Base`: apply the current safe Auto Base decision.
- `Suggestion`: a visible optional action the user can apply.
- `Auto-set`: a visible control value Stack set automatically.
- `Technical analysis`: analysis from a metadata-normalized scene-linear image.
- `Current-frame analysis`: analysis from the scene-linear image immediately before View Transform.
- `Highlight risk`: RAW clipping, near sensor saturation, or display clipping risk.

## UI Reconciliation Note

Some pass documents use the older phrase `suggestion chip`. Treat that as a visible suggestion action, not a requirement to render full suggestion rows inline. The current RAW tab UI overhaul direction is:

- suggestions are reviewed in a dedicated popout/expander
- the RAW top area shows the suggestion count and entry point
- affected controls show compact local markers, ghost graph points, or applied state
- long rationale belongs in the popout/expander or Diagnostics, not duplicated beside every control

## Shared Non-Goals

Do not implement these in the first Auto Base rollout:

- Hidden local edits.
- Neural subject detection.
- Learned white balance.
- Heavy highlight inpainting.
- Automatic creative looks.
- Automatic skin edits.
- Automatic aggressive sharpening.
- Fully replacing the current View Transform with ACES, AgX, or another external system.

Those can be revisited after the foundational passes ship and are validated.

## User-Facing Workflow Target

On RAW load, the intended first experience is:

1. The image becomes readable quickly.
2. Stack shows a compact summary:
   - `Auto Base applied: View fit, Camera WB, Technical baseline. 3 suggestions available.`
3. Auto-set controls are marked.
4. Suggestions appear as chips or rows:
   - `Open shadows`
   - `Protect sky`
   - `Suggested WB`
   - `Recover highlights`
   - `Brighten foliage`
5. Applying a suggestion creates normal editable recipe values.
6. Manual edits freeze or clear relevant auto ownership so Stack does not fight the user.

## Engineering Invariants

These invariants apply to all passes:

- Do not change user edits behind their back after they manually adjust a control.
- Do not add more broad responsibilities to `EditorModuleRawWorkspace.cpp`; split or create a focused helper before adding a new Auto Base surface.
- Do not append more top-level RAW workspace fields directly to `EditorRenderWorker::Snapshot` or `EditorRenderWorker::Result`; use grouped RAW workspace payload structs.
- Do not add analysis/recommendation math to ImGui files.
- Do not recompute suggestions from display-referred output.
- Do not use display HSV/HSL for scene masks.
- Do not use image max/min as Auto Base anchors unless no robust stats are available.
- Use percentiles and valid-pixel masks for all image statistics.
- Separate RAW sensor clipping from display clipping.
- Keep recipe serialization backward-compatible.
- Add tests when recipe shape, hashing, equality, or recommendation logic changes.
- Keep `autoApplyAllowed` and `recommended` separate in code. A recommendation may be mathematically valid but still require explicit user acceptance.
- Any fallback because a perfect analysis stage is unavailable must be visible in code comments and diagnostics. Do not pretend an approximation is the researched ideal.
- Do not double-apply metadata baseline exposure or black/white normalization if the existing RAW pipeline already applies it.

## Implementation Stop Conditions

Stop and leave the feature as analysis-only or suggestions-only if any of these are true:

- The implementation cannot tell whether a recipe is new/default versus an existing saved user edit.
- Highlight clipping metrics are unavailable but a positive RAW exposure change would be applied.
- Camera/as-shot WB is unavailable and AWB confidence is low.
- The exact analysis stage is unavailable and the fallback would use display-referred pixels for scene decisions.
- A proposed local suggestion cannot be represented as visible editable Local Range recipe values.
- A proposed denoise/detail default cannot be represented by visible editable controls.

## First Implementation Milestone

The smallest useful Auto Base release is:

- Shared `RawImageAnalysis` and `AutoBaseRecommendation` model.
- Technical/current-frame percentile stats.
- Valid-pixel mask and border exclusion for stats.
- Highlight risk report.
- Auto View Transform on RAW load.
- Auto Base summary with one-click revert.
- RAW exposure suggestion chip.
- `StackGraphBehaviorTests` coverage for new recipe/recommendation behavior.

That release will already address the main user problem: dark RAW files with bright skies becoming hard to judge until View Transform is fitted.
