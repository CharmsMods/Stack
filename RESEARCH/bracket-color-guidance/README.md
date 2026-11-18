# RFC: Bracket guided color evidence (post-first-merge)

Status: DESIGN PROPOSAL — not an implemented color solver. Based on Stack main at 52631b910c9c76187aaf0e6b6d272f630206614f (after alignment PR #10 merged on 2026-11-18).

## Intent

A bracket project already attempts the best possible joint alignment, temporal noise reduction, HDR exposure recovery, and (where justified) native/Bayer or full-RGB super-resolution. Users should be able to inspect the first **completed** result and mark local color evidence they believe is **correct/trustworthy** or **incorrect/suspect**. The same Bracket Engine then uses those hints to selectively improve its reconstruction, rather than handing the problem to an unrelated post-export paint or denoise tool.

The unannotated automatic result must remain the baseline and the default. There is no manual step before initial processing, no new stand-alone "denoise-only" mode, and no generative AI image reconstruction.

### Non-negotiable contracts

- Never rewrite sensor observations, calibrated sample values, clipping/support flags, per-capture noise estimates, or recorded provenance to agree with a hint.
- Distinguish measured colors, measurement-supported reconstruction, inferred/regularized colors, and a display-only fallback. A user correction is a preference or constraint, **not** newly observed ground truth.
- A wrong-color mark must not globally ban a hue: a purple artifact in one shadow does not mean purple objects elsewhere are wrong.
- Never increase confidence just because the user clicked an isolated, extremely noisy pixel; sample a neighborhood and retain measurement uncertainty.
- Preserve geometric coverage, alpha, HDR range, negative/overrange linear values, motion/depth boundaries, and the intended RAW/scene-linear output contract.
- Do not require a model download, neural inference, or CPU substitute for Stack's currently GPU-required Bracket pipeline.
- A project with zero hints must reproduce the existing result without altering colors, identities, or performance in the normal path.

## Existing code that matters

1. "src/Raw/Bracketing/Processor.cpp" produces BracketingResult plus retained BracketingAnalysis and prepared captures; matching analysis identity lets the engine reuse preparation on later passes. It already offers ReblendPreview for changes to contribution weights.
2. "src/Raw/Bracketing/HdrColorFusion.cpp" and "Merge.cpp" evaluate aligned exposure-group observations, shared color-cell selection, uncertainty, clipping, headroom, and explicitly categorized fallbacks.
3. "src/Raw/Bracketing/SceneColorEvidence.h", "SceneColorContract.h", and "src/Raw/SceneColorGuide.h" create a compact **immutable source-derived** camera-color reliability/variance guide. Its maximum dimension is 512; it exists to guide downstream processing and is not a native-resolution user edit mask or reconstructed pixels. Preserve this contract; create separately owned user-hint and refinement data.
4. "src/Raw/Bracketing/Recipe.h" is currently RecipeVersion 10, with automatic modes, alignment, reconstruction, smoothing, groups, and contribution curves. Do not insert hint arrays into AnalysisIdentity's preparation-defining portion by accident: that would throw away cached alignment for a brush stroke.
5. "src/Project/BracketingState.h" separates authored recipe/draft, completed result, interactive preview, revisioned jobs, and adoption. "src/BurstEngine/Worker.cpp", "Client.cpp", and result serialization carry the same shared engine to headless and editor use.
6. "src/Editor/Internal/RawLab/EditorModuleRawSectionPanel.cpp" currently labels the first tab "Source" in bracket mode and routes it to "RenderBracketingSidePanelUI", rather than the single-RAW Interpretation panel. "src/Editor/Bracketing/BracketingWorkspace.cpp" already provides Processing, Captures, and other bracket controls.
7. The graph already has a canonical source: a managed "MultiFrameHdr" node. "src/Project/RawProjectGraph.cpp" projects one immutable prepared-source node at the start of the layer graph; "src/Editor/Internal/RawLab/EditorModuleRawLayerGraph.cpp" presents the bracket source there, and "EditorModuleRawProjectGraph.cpp" routes its opening into Bracketing. Extend this owner instead of introducing a second upstream source node.

## Intended user sequence

1. Import captures and press Process (or use the existing automatic workflow). Stack completes its initial merge without requesting labels.
2. Inspect the result at a sufficiently high resolution. In the Bracket panel, show **Color guidance** only after a complete, publishable first result.
3. Choose **Trust color** or **Question color**, then click or paint a **local** region on the completed image. Use a robust neighborhood estimate instead of a single pixel. Show the sampled color, range/coverage, and available supporting frame evidence.
4. Optional: choose a source capture/exposure group as the preferred *reference* for the region. This is more informative than claiming a noisy output pixel is true. Selecting a source does not override validity checks.
5. Review each hint, adjust radius/strength, undo/delete it, and press **Refine color** (a later phase may enable debounced preview). Preview only affected tiles first, then publish an accepted refined result.
6. Compare **Original merge / Refined** and inspect a **support/confidence** overlay. If no usable alternative evidence exists, say so; keep the baseline or explicitly label an optional inferred-chroma route. Preserve the old completed result while an update runs or fails.
7. The same Bracket node outputs the accepted result to the normal layer/node graph. Subsequent color grading, exposure, local masks, tone rendering, and export operate on that upstream result.

Suggested first-version UI inside Bracket: Processing; Captures; Color guidance (empty state until result); optional diagnostics. Two obvious tools, "Trust color" and "Question color"; one brush/patch radius; per-hint strength; clear/undo; Refine; Before/After; reliability overlay. No separate global color-palette editor or global hue exclusion. Use labels/patterns as well as colored overlays.

## Proposed pipeline boundary

Capture preparation / alignment / calibration / group observations
  -> first neutral/automatic HDR + denoise + native or RGB reconstruction
  -> completed baseline and retained evidence
  -> optional authored color hints
  -> **selective guided re-fusion / reconstruction**, while retaining baseline luminance and evidence contracts
  -> accepted Bracket output
  -> downstream RAW development and authored graph operations / display transform

"After the first merge" describes **when the user can author guidance**, not permission to colorize the already tone-mapped display framebuffer. The final computation should consult calibrated scene-referred source data, not bake in current tone curve, white balance appearance, view transform, or exposure sliders. The viewport is a selection surface; its coordinates and picked RGB require a known inverse/source mapping.

Implementation must resolve a key difference:

- **Standard (Bayer)** can output a native normalized mosaic. There is still a legitimate downstream demosaic operation. For this mode, guided changes must remain valid as re-selection/weighting of real CFA observations. Do **not** silently convert the output to invented full RGB or remove required internal demosaicing. If a requested correction cannot be represented as a valid Bayer reblend, report "insufficient color evidence" for v1.
- **Super-resolution / reconstructed camera RGB** can incorporate color guidance into the RGB reconstruction stage, with chroma regularization clearly marked as inferred where measurements do not determine it. No synthetic detail may be passed off as captured RAW.
- **Adaptive** may decide between these outputs. Keep one authored hint format in native reference coordinates, and dispatch through an output-contract-aware backend. Ensure preview and final render use the same decisions.
- **Panorama** is deliberately out of v1 until its geometry, non-CFA raster result, and overlap color ownership are explicitly supported.

## Hint representation and persistence (proposed, not finalized)

Define a versioned per-Bracket-source-set ColorGuidance document, or an explicitly versioned BracketingRecipe adjunct, with:

- Stable hint UUID, polarity (Trust / Question), strength, enabled flag.
- Reference-domain coordinates and region (center/path/radius/softness); optional saved mask asset for complex strokes later. No screen coordinates or display-scale dependence.
- Sample descriptor: robust neighborhood chromaticity, noise/covariance estimate, support category, reference exposure/group/frame if selected, and the **color-space and calibration identity** in which it was measured. The clicked preview's post-view RGB is not itself a camera-space target.
- Source-set identity, baseline merge identity, orientation/crop mapping identity, and algorithm version. Reprojection on source changes must be verified rather than guessed.
- A separate diagnostics/provenance object for engine acceptance/rejection, readable in UI but **not** hand-editable raw-measurement status.

Prefer small persisted hint records and recomputed tile evidence over serializing full-resolution masks or gigabytes of per-frame measurements in the project file. Normalize and bounds-check geometry; reject NaN/Inf, impossible frame IDs, stale anchor transforms, unsupported versions, and unreasonable allocations. Disabled or stale hints must not be applied. Saving/reopening must be deterministic and undoable. The shared worker must accept the same document without linking ImGui.

Identity/caching split:
- **Preparation identity:** input hashes, calibration, exposure grouping, alignment, motion/depth, algorithm revisions. Unaffected by brush edits.
- **Baseline merge identity:** preparation plus reconstruction/curve choices.
- **Guidance identity:** baseline + canonical sorted hints + guidance algorithm revision + output contract. Changing one hint invalidates only impacted tile work when geometry and prepared sources remain identical.
- A change in alignment, origin, orientation or source set may invalidate/reproject hints explicitly. A change to downstream tone/white balance must not silently shift a hint's recorded scene color.

## Candidate solver (v1)

Use evidence-based, bounded correction — not user-stroke color replacement.

1. From prepared, aligned groups for each touched tile, recover candidate calibrated camera-color observations and their measurement/model variances, support, clipping, fallback state, motion rejection and exposure-group provenance. A low-res SceneColorGuide may prioritize problem areas but is not sufficient to perform full-res reconstruction.
2. Establish the **baseline** joint color decision and unbiased luminance/detail estimate. Preserve current reliable weights for clearly supported regions. Detect genuinely ambiguous local chroma using noise-corrected differences, cross-exposure disagreement, spatial support and local motion/depth risk.
3. **Trust** biases a measured, supported candidate or neighborhood color family *locally* where it is plausible. **Question** reduces the preference for that local color hypothesis and looks for an independently supported alternative. It never marks an unmeasured color as clipped, and never makes an invalid source usable.
4. Use robust chromaticity / opponent-color differences with uncertainty-adaptive tolerances; in near-zero light, color ratios are unstable, so cap influence and use a wider neighborhood plus absolute signal checks. Prefer joint RGB/exposure-group reweighting to independent per-channel cherry-picking.
5. Optional edge-aware spatial propagation may regularize **chroma**, but should stop at luminance edges, depth/occlusion masks, accepted motion boundaries, and regions with conflicting reliable evidence. Do not blur high-frequency luminance/detail or spread a swatch across the entire frame.
6. Bound deviation from the baseline by measured variance and total support; cap guided-weight multipliers. If there is no valid alternative (including fully clipped, black/no-signal, or moving subjects without correspondence), do not force a "correct" color. Surface the reason.
7. Re-run a bounded second pass on affected tiles through shared Bracket processing. GPU memory/headroom and worker admission must be accounted for, cancellation responsive, accepted result atomically adopted, and stale preview results rejected.

A plausible future objective is a robust data term over actual exposure samples + spatially edge-aware chroma regularity + locally gated user preferences. It is **not** an unconstrained global colorization solve. Measure before committing to a specific optimizer, threshold, chromaticity space, or number of iterations; compare against simple noise-aware group reweighting first.

### Important conceptual distinction

Marking a color as **Question** is evidence about the *output appearance*, not about which source is at fault. First identify whether the color came from weak measurements, mismatched exposures, clipping fallback, demosaic/aliasing, or motion mismatch; only then choose the correction mechanism. Questioning purple shadows should not suppress authentic purple lights or paint gray over everything. Likewise, marking a noisy red pixel "Trust" must not promote it above reliable green/blue evidence just because it was clicked.

## Graph and panel plan

- Keep the canonical managed "MultiFrameHdr" source/project-graph first node; display its photographic identity as **Bracket**, rather than introducing editable upstream graph nodes or moving bracket reconstruction after adjustment nodes.
- For bracket project/context, rename first side-panel tab **Source -> Bracket**. For ordinary RAW projects retain **Interpretation**.
- When opening/selecting the first bracket source node, ensure the Bracket tab is visibly selected and its controls reflect the active source set. Do not simply rely on an incidental selected adjustment node.
- In Bracket mode, do not render the single-RAW demosaic *control section*. Today the panel already dispatches to separate Bracket controls, so this is an invariant to test, not a request to delete working downstream Bayer demosaicing.
- Bracket-specific colors/brushes attach to the Bracket source-set owner and are evaluated **before** later graph nodes. Editing later nodes must not rewrite hints, change the first source's ownership, or duplicate the Bracket source in the projection.
- Single-RAW Interpretation, white balance and its existing demosaic controls remain unchanged. If a Bracket output is reconstructed RGB, preserve the no-double-demosaic behavior; if native Bayer, ensure it is developed once.

## Performance / failure / lifecycle

- Keep original accepted baseline visible during guidance editing and a second-pass worker; do not stall ImGui or start a full reprojection on each mouse move.
- Interacting with the hint brush should update lightweight overlay state immediately. Debounce processing and cancel superseded previews. Never run disk/IPC/GL-wait work on the UI thread.
- Cache tile results by source identity, hint subset and output mode, with suitable halo width for edge-aware propagation. Bounded caches obey the same weak-hardware GPU/host-memory policies as the rest of Bracket.
- Stage outcomes: first merge complete; guidance pending; refining; refined accepted; guidance failed/stale. On failure keep the previous accepted image rather than exporting a partial/inconsistent result.
- On reopening a project whose prepared inputs are gone, display stored final output and hint metadata safely; recompute only when sources are available. Do not mislabel old guides as valid for new sources.
- Both the desktop Stack editor and StackBurst/headless path must consume the same versioned recipe/data contract; unattended processing simply has no hints by default.

## Test and measurement plan

1. **Zero-hint parity:** exact baseline parity for Standard Bayer, Adaptive, SR 1x/2x, ordinary HDR, and mixed exposure stacks. No surprise identity or GPU allocation changes in the no-hint path.
2. **Supported correction:** noisy shadow with multiple exposure sources where one has better valid color; trust/question hints improve chroma versus a known controlled reference, without altering measured luminance or crossing edges.
3. **Impossible correction:** all colors clipped, black/no-signal, no compatible source, synthetic magenta fallback, or only one valid sample. No invented certainty, bad sample activation or non-finite values; explicit warning.
4. **Mixed evidence:** real saturated colored lights, red/blue sensor saturation asymmetry, mixed illumination/white balance, motion/occlusion, depth/parallax, demosaic artifacts; no color wash, channel seams or color bleeding.
5. **UX:** preview -> sample -> brush -> undo/redo -> refine -> save/reopen -> source-set switch -> graph-first-node selection; verify no demosaic selector in Bracket and standard RAW Interpretation still works.
6. **Persistence/transport:** old recipes load as zero-hint; same hints round-trip; deterministic hashes; no stale guidance accepted; worker wire/result artifacts tolerate missing optional guidance; no unsupported schema read.
7. **Quality/perf:** reference screenshots and per-region quantitative color error plus false-change area, retained luminance texture, fallback count, source provenance, tile latency, max resident CPU/GPU budget, cancellation, repeated edits and stress tests. Test with Stack's documented Windows/GL workflows, not only synthetic CPU helpers.

Do not claim this plan's acceptance tests are passing until code for the guided pass is written and validated.

## Incremental implementation proposals

- **A — UI and contract:** rename first tab; confirm first-node routing; implement hint types, source-set persistence and migrations; add UI editing/picking overlays without affecting the output. Establish reliable coordinate transforms and history.
- **B — Evidence/prototype:** instrument per-tile candidate chroma/support and preview diagnostics from shared Bracket engine; test a simple local robust reweighting against noisy shadow fixtures before expanding spatial propagation.
- **C — Guided reconstruction:** implement selective processing and cache identities for RGB output, plus a strictly measurement-supported Standard Bayer path (or postpone unsupported native cases with clear status); preserve no-hint parity.
- **D — Productization:** native-resolution acceptance/publish, headless parity, stress tests, robustness in saving/reopening, visual QA, UI affordances and Windows GPU performance.

## Decisions for discussion (recommended defaults)

1. **What is "correct" sampling?** Default: mark a region from the merged image; optionally pin a trustworthy source exposure when it exists. A chosen source is still tested for clipping/motion/noise. Do not request numerical RGB entry for v1.
2. **How far may a hint reach?** Default: local brush region with modest edge-aware propagation. No global "all pixels of this hue" option in v1.
3. **What if no physical source supports the intended hue?** Default: preserve measured baseline and warn. Keep speculative colorization or author-painted chroma as a distinct future, explicitly labeled mode rather than misrepresenting it as recovered RAW.
4. **When to run?** Default: initial Process remains automatic; after hints, explicit Refine with optional bounded region preview. Defer live full-resolution reruns.
5. **Output modes?** Default: keep current native Bayer and RGB contracts. Scope the first actual solve to RGB if native CFA-guided weighting proves too constrained; still preserve authored hints and report unsupported cases.
6. **How should bracket source appear?** Default: one first **Bracket** node opening the Bracket side tab; ordinary Interpretation is untouched.

## Literature and adjacent techniques

- Wronski et al., *Handheld Multi-Frame Super-Resolution* (ACM TOG 2019): noise-corrected local color differences, CFA-aware reconstruction, and motion/rejection. https://research.google/pubs/handheld-multi-frame-super-resolution/
- Granados et al., *Optimal HDR Reconstruction with Linear Digital Cameras* (2010): noise-calibrated multi-exposure weighting and uncertainty. https://vcai.mpi-inf.mpg.de/projects/opthdr/granados10_opthdr.pdf
- Levin, Lischinski and Weiss, *Colorization using Optimization* (ACM TOG 2004): a reference for sparse local hints and edge-aware propagation, **not** a license to hallucinate camera measurements. https://doi.org/10.1145/1015706.1015780

These works motivate concepts, not a copied implementation. New tests and source verification must determine whether a given method is appropriate for Stack's exact CFA, radiance, HDR and resource contracts.
