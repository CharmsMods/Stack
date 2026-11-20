# RFC — Bracket Noise Guidance: palette review, manual chroma and luminance feedback

**Status:** DESIGN / discussion. The guided analyzer, overlay, picker, and solver are **not yet implemented**. This RFC incorporates the user's clarified vision as of November 18, 2026. Base: \`main\` 52631b910c9c76187aaf0e6b6d272f630206614f (after alignment PR #10).

## Product vision

Bracket should perform its best complete **alignment + HDR + temporal denoise + reconstruction** first, with no mandatory interaction. The user can then review a post-merge **Noise Guidance** tool as another handle on the *same* Bracket engine. This is not a Photoshop-style color replacement tool.

There are **three complementary ways to identify suspected noise**:

1. **Suggested color palette** — Stack analyzes the first completed merge (and source evidence) and proposes clusters of colors/speckles that **might** be residual chroma noise or false color. Users click through swatches; the rest of the image goes grayscale, and only the selected candidate's **original local colors** remain visible.
2. **Manually chosen color** — regardless of suggestions, users can sample a color from the image **or** choose any custom color (e.g. green, cyan, magenta, or any RGB/hue). Stack finds pixels/regions matching it within adjustable tolerance; the user can isolate the matches and **submit a judgment** about whether they are noise, already cleaned, or real color/texture. A manual swatch is a **search/selection clue**, not a replacement output color and not proof that all green/cyan/magenta pixels are bad.
3. **Manually chosen luminance anomalies** — users click/brush pixels or patches that look **too bright** or **too dark**, or review optional suggested local bright/dark outliers. These selections help Stack distinguish *luminance noise* from real highlights, shadows, fine surface texture, stars, edges, etc. This does not require choosing a hue.

Color and luminance selections can coexist in one project and may overlap spatially. The user supplies **evidence and feedback**; Stack revisits reliable capture measurements to improve the next result, without hallucinating a supposedly recorded sensor value.

## User-facing workflow

1. Import bracket/burst, process normally, and accept a **first complete result**. Keep that result immutable for comparison and rollback.
2. In the first graph source node's **Bracket** tab, open **Noise Guidance**. The review subsection has \`Suggested\`, \`Pick color\`, and \`Pick brightness\` selectors.
3. **Suggested:** show a compact palette of tentative suspect chroma clusters, based on camera-space variance, disagreement, residual high-frequency chroma, clipping, motion and local spatial support. Rank by *heuristic suspicion*, never claim a validated "probability of noise".
4. **Pick color:** eyedropper from the completed image, **or a freely editable custom swatch / hue-color picker**. Adjustable tolerance and scope: \`Local area\` (safe default) or \`Find similar across image\` (explicit opt-in). Show the **exact proposed selected-pixel mask** before the user submits. Sampling should use robust local color, not an arbitrary isolated noisy pixel; user-entered color remains allowed.
5. **Pick brightness:** eyedropper/brush a spot, label it **Too bright** or **Too dark**, optionally choose \`Find similar outliers\` and select a spatial scope + sensitivity. This is *relative to appropriate surrounding structure and corroborating source data*, not a global threshold that brands all shadows as bad or all highlights as hot pixels.
6. For any selection, inspect a high-zoom crop and, if useful, source-frame evidence. Assign one of these decisions:
   - **Needs more cleanup** — selection contains residual color and/or brightness noise that should be reduced if evidence permits.
   - **Already cleaned** — user is inspecting an input-versus-final diagnostic and confirms Stack already removed it adequately; **do not** apply additional cleanup. This label is inappropriate for a residual-only candidate that still exists unless a before/after comparison confirms the claim.
   - **Real color / texture** — preserve the material's true chroma or luminance detail and avoid oversmoothing.
   - **Unsure / skip** — no instruction; retain automatic behavior.
   In brightness mode, retain the \`Too bright\` / \`Too dark\` direction with the decision. A label can target one connected component, a brush region, or an explicitly reviewed color-group selection.
7. Review all submitted hints in a small queue; edit strength/tolerance/scope, undo, delete, and preview their masks. Applying a label updates the UI **immediately** without rerunning expensive reconstruction per pointer movement.
8. Press **Refine** to ask the existing Bracket engine for **a second evidence-based pass** over affected tiles. Provide a before/after comparison and explicit messages when no valid source data supports a requested change. Only an accepted complete result replaces the published upstream Bracket output.
9. Save/reopen all accepted annotations, while preserving the original first merge, automatic behavior when unannotated, and downstream node graph edits.

### Proposed first-version panel hierarchy

\`\`\`text
Bracket (first graph node / source)
  Processing                    [existing]
  Captures                      [existing]
  Noise Guidance                [new; only after completed first merge]
    Mode: [Suggested] [Pick color] [Pick brightness]
    Candidate swatches / manually entered colors / light-dark marks
    Display: [Isolate selection] [All suspects] [Normal]
    Scope: [Local region] [Find similar in image]
    Tolerance / sensitivity / region size
    Selected pixels, source evidence, zoom
    Decision: [More cleanup] [Already cleaned] [Real texture] [Unsure]
    Saved decisions; Refine; Original merge / Refined
\`\`\`

**Do not** show a nonfunctional \`Noise Guidance\` tool for an unsupported output mode. Normal single-RAW projects retain the **Interpretation** tab.

## Isolation and display semantics

**Color mode**:

- The photo retains its base *displayed luminance/texture* everywhere.
- Outside the chosen suspect/manual candidate mask: neutral grayscale (no original hue).
- Inside the chosen mask: show the pixels' **real local RGB colors** as rendered in the current view. A palette swatch is a representative selector, **not** a flat colored paint overlay.
- For \`All suspects\`, show the union of enabled candidate masks. Outline or number connected regions; click one to override a group-level judgment. Repeated similar colors in different materials can be different regions.
- Preserve zoom/pan/viewport coordinates and edge alignment. At zoom, render native-resolution mask boundaries; Stack's existing **512-max-dimension \`SceneColorGuide\` cannot provide pixel-perfect dots alone**.
- Grayscale isolation is a **non-destructive diagnostic display** and **never** changes source pixels, graph result, exports, RAW evidence, or tone settings.

**Brightness mode**:

- Make luminance deviations visible on a neutral grayscale photo without replacing the pixel intensity with a false bright/dark value. Outline selected spots or use an optional overlay (e.g. warm outline for suspected bright spikes and cool outline for dark spikes) rather than pretending their brightness changed.
- An optional local residual/contrast magnification view may help see one-pixel hot/cold points; label it clearly as diagnostic rather than current output.
- A bright light, reflection, specular edge or dark painted detail is not automatically noise. The user marks a **suspicious local outlier** and then decides its classification.

**Already cleaned**:

- To claim a noisy dot was removed, users must be able to inspect **Before input / Removed by merge / Remaining in merge** evidence. The \`Removed\` visualization is a comparison diagnostic, not color overlays painted onto the accepted final result.
- If the original observation is unavailable, disable or mark this evidence mode as unavailable; never fabricate a "removed noise" map.

## Candidate representation

Keep **engine-suggested candidates** and **user-authored selections** separately identified, but make them share a common local mask and decision UI.

Suggested candidate metadata:
- Stable candidate analysis ID tied to **baseline merge identity** + detector algorithm revision.
- Representative swatch/hue family, per-region source-space chroma, uncertainty / reliability, area, bounding region, connected-component IDs, accepted motion/depth support, calibrated measurement disagreement, clipped/fallback status and category.
- Sparse per-tile native-resolution membership/provenance for selected regions, with memory-bounded cached masks. Candidate clusters can share a hue yet have different reliability or texture status.
- Rank/score as **heuristic suspicion**, *not* calibrated probability.

Manual color metadata:
- Stable annotation ID; source \`eyedropper\` or \`custom color\`; RGB/color-space identity as entered/displayed; canonical **scene/camera-space or a documented normalized selection representation**, not an implicit 8-bit display RGB baked into processing.
- Hue/chroma/lightness tolerance and an explicit scope: local brush/ROI (default) or image-wide **candidate matching** (opt-in, independently reviewable).
- Full-resolution selection mask/connected components determined using a known view-to-source transform. Changing WB/view appearance should either recompute presentation matching explicitly or preserve the authored scene-space anchor, never silently reinterpret the historical swatch.
- A green/cyan/magenta match must **not** become a global hue-exclusion filter. Identical hue from a real object is separately reviewable and has its own source support.

Manual brightness metadata:
- Stable annotation ID; direction \`too-bright\` or \`too-dark\`; selected point(s)/ROI; local-baseline scale (spot, texture, blotch); tolerance/sensitivity; source coordinate transform and relevant exposure/scene-linear unit identity.
- Use a **local luminance residual** measured against robust neighborhoods and source consistency; not "all pixels below X" or "above Y". Save the actual selected region so future reprocessing does not over-expand it on appearance changes.
- Luminance and chroma hints are independent **channels of guidance**. Opposing labels on overlapping regions must resolve explicitly: \`Real detail\` protection should block destructive smoothing until reviewed, and the UI should flag conflicting labels.

Shared decision data:
- \`needs_cleanup\`, \`already_cleaned\`, \`real_texture\`, \`unsure\`; emphasis on chroma, luminance, or both; local severity/strength; enabled flag; authoring order/history; optional frame/group reference as supporting evidence.
- Validate finite parameters, maximum mask/stroke sizes, source-set membership, image transform, stale baseline, legacy reader behavior, duplicate/overlapping identities, and output-mode support.
- No annotation changes **raw sample values**, motion validity, clipping flags, measured noise model, or evidence confidence as if it were a real observation.

## Detection approach (prototype, evidence-driven and testable)

### Suspected chroma noise / false color

1. Compare per-pixel or per-tile reconstructed chroma to an **edge-aware local reference** at appropriate scales; distinguish unexpected high-frequency color changes from actual luminance/colored surface structure.
2. Use retained **aligned multi-frame observations** with calibrated noise/uncertainty. Agreement between independent valid captures makes true material differences more plausible; large noise-normalized disagreement suggests uncertain chroma. Correlated frames or shared registration errors must not be counted as independent confirmations.
3. Use clipping/partial source validity, source-group contribution, motion rejection, occlusion/depth boundaries, interpolation/demosaic false color and shot/read-noise characteristics. An isolated saturated speck in a dark area is a *suggestion*, not an absolute fact.
4. Cluster suspect observations by **chroma similarity + spatial/evidence context**, then offer representative palette swatches. Avoid generating hundreds of swatches or grouping all green pixels together.
5. After user chooses a color, compute a **bounded** similarity mask, then intersect with the user scope and present connected-component review. It may include legitimate green textures; classification or protected region overrides handle those cases.

### Suspected luminance noise

1. Build local expected-luminance references at several spatial scales from scene-linear calibrated samples, with careful handling of HDR exposure levels, zero/negative linear values, and unavoidable tone/display transformation for selection.
2. Compare signed local deviations (bright vs dark spikes/blotches) to the **source noise model**, repeated capture support, and local gradients/edges. Avoid treating all shadows, stars, detailed rough surfaces, sensor clipping, or true specular highlights as denoise targets.
3. Optionally propose \`bright spots\`, \`dark spots\`, or \`blotches\`, but manual marking works independently even when automatic detection returns no suggestions.
4. A \`needs_cleanup\` luminance hint requests a bounded luminance-consistent reconstruction (or evidence-weighted temporal selection), not a blanket exposure reduction or global tone curve. \`real_texture\` protects supported luminance detail while refining chroma independently.

### Second-pass fusion/refinement

- The first completed merge stays authoritative until replacement; labels are *preferences* interpreted against valid observations, **not** new measurements or user-specified output RGB/Y.
- Prefer reweighing reliable, nonclipped, motion-consistent source groups and **selective frequency-aware chroma/luminance regularization** over generic blur. Separate chroma and luminance penalties; respect group-wise RGB coherence and source uncertainty.
- Edge-aware smoothing must not cross valid structural boundaries, depth/occlusion edges or moving subjects; a texture-preservation hint must protect local fine details. Both over-denoise and under-denoise are failure modes.
- Cap correction by measured uncertainty and support. Reject impossible recolor/brightness requests gracefully. If only one source is usable or all exposures lack information, report \`insufficient evidence\`.
- Work on **source-referred calibrated measurements** or the chosen reconstruction's internal data, not the final tone-mapped 8-bit viewport. Display-only grayscale is excluded from processing.
- Do not claim precise quantitative improvement until compared with controlled ground-truth captures. First test basic robust group reweighting against more complex spatial optimization.

## Output contracts and placement

\`\`\`text
Source captures
  -> alignment / exposure calibration / noise model / HDR & temporal fusion
  -> Standard Bayer / reconstructed RGB / adaptive output as today
  -> ACCEPTED IMMUTABLE FIRST MERGE
  -> optional suspect analysis + user manual selections and labels (review only)
  -> optional selective guided second pass using retained source evidence
  -> ACCEPTED BRACKET RESULT (same source node)
  -> downstream RAW development, demosaic where actually required, graph adjustments, display/export
\`\`\`

**Mode-specific guardrails**:

- **Standard (Bayer):** the image may still require actual downstream demosaic. Hide the single-RAW demosaic *control section* from the Bracket panel, **do not delete the required demosaicing**. Corrections must remain compatible with valid CFA observation reweighting; a post-RGB paint layer cannot be misrepresented as Bayer recovery.
- **Super-resolution / reconstructed-camera-RGB:** a targeted supported color/luminance refinement can occur in the reconstruction stage. Mark inferred chroma as inferred, not measured.
- **Adaptive:** preserve its selected output contract and branch on the actual mode; no hidden switch to RGB because a user picked a color.
- **Panorama:** opt out of unsupported v1 review/refinement until coordinate/overlap semantics have explicit tests.
- Remain correct for full HDR/signed values, clipping, coverage, provenance, and no-double-demosaic rules.

## Ownership, cache identity and persistence

Repository integration points confirmed:
- \`src/Raw/Bracketing/Processor.cpp\`, \`HdrColorFusion.cpp\`, \`Merge.cpp\` — prepared observations, color-aware exposure fusion, reblend and reconstruction.
- \`src/Raw/Bracketing/SceneColorEvidence.h\`, \`SceneColorContract.h\`, \`src/Raw/SceneColorGuide.h\` — **immutable**, compact, source-derived color reliability (maximum side 512), which can guide ranking but **is not** a native-resolution editable mask.
- \`src/Raw/Bracketing/Recipe.h\` — currently RecipeVersion 10. Propose a **separate versioned source-set review document**, not a new per-brush preparation dependency.
- \`src/Project/BracketingState.h\` — draft, completed result, revisioned async preview/adoption. Extend saved review state and independently cancelable candidate/refinement jobs.
- \`src/Editor/Bracketing/BracketingWorkspace.cpp\`, \`src/Editor/Internal/RawLab/EditorModuleRawSectionPanel.cpp\` — existing Bracket sidebar and proposed Noise Guidance subsection; panel currently dispatches to bracket controls instead of single-RAW Interpretation.
- \`src/Project/RawProjectGraph.cpp\`, \`src/Editor/Internal/RawLab/EditorModuleRawLayerGraph.cpp\`, \`EditorModuleRawProjectGraph.cpp\` — the first graph source already exists as a managed \`MultiFrameHdr\` node / protected projection. The same source owns the guidance and accepted result; no duplicate upstream node.
- \`src/BurstEngine/Client.cpp\`, \`Worker.cpp\`, result serialization — shared headless/desktop processing contract. Headless without labels must be identical to today.

Separate identities:
- **Prepared source identity**: capture hashes, calibration, origin, alignment, exposure groups, motion/depth; never recomputed because of a swatch click.
- **Baseline merge identity**: prepared source + reconstruction/output modes and authored merge settings.
- **Candidate analysis identity**: baseline + detector revision + scale/quality; candidates themselves should not mutate when a user changes a decision.
- **Review identity**: canonical manual swatch/brightness selections, labels, resolved connected-component mask IDs, and baseline transform. UI highlight/selection has no durable result-identity cost.
- **Refinement identity**: baseline + review identity + solver revision + output contract, with tile/halo-level cache invalidation when possible.

Keep masks/provenance memory bounded: save sparse strokes or compact region descriptors and re-derive selection masks deterministically; don't put massive full-resolution RGB/mask arrays into project JSON. Store known source-space representation, allow undo/redo, validate stale transforms, handle old projects with zero hints, and permit reopening without prepared input data (display saved output safely, but disable unsupported re-refinement).

When a candidate is marked \`already_cleaned\`, this is diagnostic review evidence and should have **no pixel effect**. \`real_texture\` should protect candidate pixels from refinements which would otherwise smooth them. Changes must be atomic, cancelable, project-revision checked, memory-budgeted and never block ImGui. A failed refinement keeps the last accepted result.

## Test plan

1. **No-label parity** — all existing bracket modes and sources are unchanged bit-for-bit or within the established numerical contract; no unexpected memory or resource changes.
2. **Candidate suggestion** — noisy magenta/green/cyan speckles against uniform wall; one true saturated LED / painted stripe of same hue; verify separate spatial candidate masks and "suspicion" not certainty.
3. **Manual color entry** — eyedropper and freely typed/picked arbitrary colors; tolerance changes; local vs explicit image-wide selection; grayscale outside selected mask; pixel-accurate selected dots at high zoom and stable performance.
4. **Manual luminance** — hot bright flecks and dark dead-pixel-like flecks, low-frequency blotches, shadows, reflections, stars, high-contrast edges, and patterned/rough real textures. Selected direction and reason survive save/reopen.
5. **Judgments** — \`more_cleanup\` only influences supported selected regions, \`already_cleaned\` no pixel effect, \`real_texture\` prevents over-smoothing, \`unsure\` no effect; conflicts and local overrides behave predictably.
6. **View separation** — grayscale isolation and bright/dark highlights only change screen diagnostics, never final export or node-graph image; compare views clearly mark which evidence stage they show.
7. **Hard source cases** — all exposures clipped, very low photon count, no valid alternative, motion/depth edges, different illumination, HDR overrange, Bayer vs reconstructed RGB, adaptive output, stale color space or coordinates.
8. **Lifecycle** — first-process completion, candidate analysis, input edits, brush edits, undo/redo, background cancellation, stale-result rejection, save/load, worker/headless replay, compare and final acceptance.
9. **Budget/performance** — bounded tile cache/halo, progressive high-res masks, no UI-thread GPU/IPC stalls, weak vs strong hardware, repeated swatch selection, rapid zoom/pan.

Related existing validation entry points: \`BracketingNodeSurfaceValidation\`, \`BracketingUiValidation\`, \`BracketingSceneColorValidation\`, \`InterpretationValidation\`, \`BracketingValidation\` and Burst-worker tests. **None of the new tests are implemented or run in this draft PR.**

## Implementation slices

**A — Review UX & source ownership.** Keep the source graph node. Label its bracket-only first tab **Bracket** (already changed in this draft PR), ensure selecting it activates that tab, build non-destructive color-isolation and bright/dark diagnostic overlay with **manual picker first** (does not depend on candidate-detector quality), source-space transforms, labeling and undo/redo.

**B — Suggested suspect analyzer.** Add bounded shared-engine chroma candidate analysis/ranking with explicit source/variance evidence and native-resolution masks. Optionally add luminance suspect suggestions after the manual pick path is trustworthy. Separate "removed in original merge" evidence from "still remaining" artifact detections.

**C — Versioned review persistence & worker transport.** Store swatches/brightness marks/masks and decisions per source-set with independent identities and source validity. Save/reopen migration; headless ignores absent labels.

**D — Guided second-pass solver.** Try reliable local source-group reweighting, chroma vs luminance controls and texture-preserve guardrails, then selective spatial regularization with bounded tile halos. Support RGB and CFA output contracts explicitly; surface unsupported operations.

**E — Acceptance, validation and productization.** Exact no-guidance parity, controlled ground truth, interactive performance, end-to-end project stability, UI screenshots, full HDR and high-resolution behavior.

## Open questions and proposed defaults

1. **Manual color matching scope:** local brush/region by default with explicit \`Find similar across image\` toggle; show exact mask before submitting.
2. **Input method:** offer automatic palette swatches, image eyedropper, and unrestricted custom color picker. All can be classified using the same decisions.
3. **Luminance controls:** \`Too bright\` and \`Too dark\` selections, optional surrounding pixel search and sensitivity. Avoid global darkness/brightness thresholds.
4. **Decisions:** \`Needs more cleanup\`, \`Already cleaned\`, \`Real color / texture\`, \`Unsure\`; no pixel change from \`Already cleaned\`, which requires comparison provenance.
5. **Insufficient source evidence:** preserve baseline and report why; never fabricate recovered sensor data. Optional explicitly labeled creative repair belongs to a distinct future tool.
6. **Reprocessing:** run once after initial merge, preview masks immediately, refine explicitly via background worker and selectively re-evaluate affected tiles. No per-mouse-move global merge.
7. **Bayer/RGB behavior:** preserve current output contract; implement mode-specific correction or present a clear unavailable state.

## Research starting points

- Granados et al., *Optimal HDR Reconstruction with Linear Digital Cameras* (2010): calibrated multi-exposure noise/weighting. https://vcai.mpi-inf.mpg.de/projects/opthdr/granados10_opthdr.pdf
- Wronski et al., *Handheld Multi-Frame Super-Resolution* (ACM TOG 2019): CFA-aware, noise-corrected aligned source evidence. https://research.google/pubs/handheld-multi-frame-super-resolution/
- Levin, Lischinski and Weiss, *Colorization using Optimization* (ACM TOG 2004): conceptual reference for edge-aware sparse-constraint propagation, **not** a license to invent camera measurements. https://doi.org/10.1145/1015706.1015780

Citations here are architecture background, not evidence that a particular future algorithm will work. Validate candidate detection, false positives and guided denoise against real and controlled reference data before claiming results.
