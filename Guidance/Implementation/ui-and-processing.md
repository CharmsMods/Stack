# UI and processing

Read when changing controls, views, rendering, processing algorithms, shaders or their performance. Trace the existing request and result path before adding a second path for the same operation.

## Give each part the right decisions

| Responsibility | Appropriate work |
| --- | --- |
| UI and presentation | Layout, input interpretation, temporary interaction state, animation, and displaying progress or errors |
| Project action and coordination | Committing an edit, recording history, deciding what becomes stale, scheduling work and accepting a result for its owner |
| Processing engine | Computing from explicit recipes, sources and options, with defined results and failure information |
| Rendering infrastructure | GPU resources, execution and output publication under explicit ownership and context rules |

These are responsibilities, not a requirement for four new classes. Existing direct calls can satisfy them. UI drawing and GPU image processing both render something, but they have different dependencies.

A control can translate input into a project operation. Rules such as whether a bracket result is current or which stages need recalculation belong below its widget. Engine validation still applies when the request comes from a queue, another tool or a caller without an editor. The UI may add feedback suited to the interaction without becoming the only place that enforces correctness.

Keep hover, focus, drawer state and animation timing with their presentation owner. Keep authored parameters and processing truth with the project or computation owner. A temporary edit preview can differ from committed state, but its commit, cancellation and history behavior must be explicit. Drawing the same frame again should not itself commit the edit or restart expensive work.

Loading presentation acknowledges the request before foreground decoding begins, but must not delay applying ready data or keep an editor locked after its core state and first usable image are ready. Presentation transitions do not establish save, export or processing success. Retain useful accepted imagery while replacement work runs, and keep errors attached to the real operation.

Input descriptors and plotted histograms are derived UI data. Cache them within the document/node owner using the relevant graph, source, timeline and accepted-readback revisions plus display-domain settings. Uncommitted graph or timeline drafts require fresh descriptor evaluation; camera and floating-tool movement do not require rescanning unchanged image samples.

Batch color and curve evaluation prepares immutable coefficients once per input recipe. Preserve the scalar reference's interpolation, ordering and working-space rules when sharing that preparation with UI visualization. Color Warp's live cloud captures at most 4,096 RGB/UV samples and prepares vertices on ordinary project workers, with one job per node and two queued/running jobs across owners. The UI adopts completed vertices without waiting. Compatible parameter edits may retain the last completed cloud while the next job runs; source, selection-membership and interpretation changes must not relabel old vertices or selection highlighting as current. Worker captures own their inputs and must survive editor cancellation without accessing the editor or its GL context.

In the unified graph viewport, optional scope readbacks are independent auxiliary requests after the current image settles at display density or refinement is budget-deferred. Drain pending analysis before starting more idle native sections, while leaving visible image demand and active navigation ahead of it. Queue intent separately from an in-flight generation, and reject readbacks from an older document revision. Refreshing a scope or selecting a control whose selection does not change pixels must not advance the main-image revision or restart refinement. Preserve other tools' revision-keyed analysis packets when changing the active scope owner. Descriptor lookups borrow the immutable compiled graph instead of copying it once per visible tool.

Image cache identities include runtime policies that change pixels, not just authored parameters. Spatial Color Warp mask resolution, interactive fallback and required-spatial policy affect its output and downstream stages, but not preceding RAW, exposure or tone stages. Color Warp's ordered HSL selections, their pixel-affecting fields and the limit-to-selections mode also participate in that identity; display names do not. Cache-availability telemetry uses the same policy as execution. Input-dependent operations derive their raster identity from their actual input and viewport demand, not mutable dimensions left by an unrelated graph branch.

Graph target precision and memory estimates share `Renderer/GraphResourcePolicy.h` and use requested dependencies, including auxiliary consumers, rather than every node in the document. Retained float32 dependencies remain a precision floor. Cache-aware scheduling may stop dependencies at an owned, current cached result only when its identity can be established without executing those dependencies; extent-dependent legacy paths keep topological evaluation. Full document membership still governs resource lifetimes. GPU headroom follows the rendering context's adapter identity when available; ambiguous identities use the least measured working headroom, not the largest dedicated-memory device. A known device with a failed query must not borrow another adapter's allowance. Missing telemetry uses a bounded 512 MiB planning allowance; the graph worker limits optional caches to 64 MiB within its unused portion. These are conservative admission limits, not measured capacity. Real exhausted headroom remains zero. Measured free headroom already excludes resident presentation; only reclaimable owned caches may be credited back. Keep budget polling paced even at zero headroom.

Graph channel colors use `GraphOutputDescription` component origin and role. A modified channel keeps that identity while it still derives from one source component; independent spatial operands or different channels clear it. Uniform numeric coefficients do not introduce another image lineage. Neutral themes retain these semantic channel colors. Drawing reads the editor's revisioned output-description cache. Direct component sockets are authored `Node::channelOutputs` declarations managed by `NodeGraph/ChannelOutputs`; execution lowers them through the existing Split Channels operation and publishes the authored endpoint aliases. RAW technical-source declarations belong to `RawLayerStackState::sourceChannelOutputs`. Fold redundant splits only inside an explicit graph edit, preserving retained compound input contracts and the existing history/layout owners; do not scan for them during drawing.

Gallery navigation presentation belongs to the shell's `GalleryNavigationTransition`, built on the shared scalar channels. Gallery owns card layout and Browse anchors. Editors own documents and accepted image readiness. On an accepted grid or filmstrip open, the clicked tile shows a loader immediately while Gallery stays interactive. The default destination is the editor. The optional sidebar opening preference carries the loader from the tile to the flight and then the project entry until the preview is ready. The navigation rail starts pinned and remembers its right-click pin state. Filmstrip scrolling sends the opening to its sidebar entry and supersedes automatic activation. Once a foreground opening has a current editor image, fade it over the source thumbnail, fade out the scene, expand the image, then reveal Controls. Reserve the final unobscured View body before the flight. Fitted images use that body to stay clear of Controls. Live image handles are borrowed only for the submitted frame. Keep destination layouts stable and suppress hit targets while geometry differs from native layout. Finish tracks exactly and honor reduced motion. Navigation must not invalidate processing or rebuild font sizes, catalogs or texture resources per animation frame. Retain temporary thumbnail leases through submitted drawing and retire them through the cache's owning GL deletion path. Count retained residency against the existing budgets.

## Connect new behavior to existing behavior

Retained decompiled containers own their `compoundMembers`; rectangle overlap alone is not ownership. The shared membership helper handles explicit admission/departure, and measured bounds include nested containers. Packing transfers all owned operations, including effect-layer nodes, and exposes crossing output dependencies as compound ports. The Inputs gateway is a presentation of real typed endpoints, not another processing operation. Its binding cache follows graph ownership and structure revisions; displayed wire geometry and picking use the same entry/exit anchors. The RAW canvas omits the redundant layer Start while `RawProjectGraph` preserves and reconstructs the internal `CurrentImage` boundary on commit.

For a new processing control, locate its parameter model, edit action, invalidation path, engine input and result publication. If the edit is meant to persist, include its saved representation and load path. If it supports undo or reset, check those operations too. Extend the existing semantic path instead of making preview, graph, queue and export interpret the same parameter differently.

View-specific presentation settings can stay local when they do not alter the saved result. Distinguish a cheaper preview from an authoritative output deliberately. Preserve color space, transfer function, orientation, alpha, dimensions and precision at processing boundaries. Name any intentional approximation.

RAW and Graph visible-area previews use [ViewportPreviewTypes](../../src/Renderer/ViewportPreviewTypes.h) to distinguish the logical output canvas, its complete sample grid, and the resident texture rectangle. The editor owns each view's demand, resolution feedback and result acceptance; [GraphViewportPlanning](../../src/Renderer/GraphViewportPlanning.cpp) resolves endpoint and input demands from an explicit graph. Complete dependencies remain complete when an operation needs global data, spatial neighborhoods or an unsupported coordinate mapping. Compatible consumers may crop those immutable outputs and process a smaller raster. A viewport crop must not change an authored image window, mask canvas or export request.

Camera movement changes view demand, not image content. After the image-edit quiet interval, complete visible screen-density pixels first. Once the camera is quiet, prepare bounded whole-image coverage for navigation, then continue native sections. The navigation raster is at most the current screen sampling edge, four visible sample rasters and eight Mi pixels; its normal complete-dependency admission still applies. Do not hold a completed visible result while evaluating optional coverage. During movement, reuse spatially complete pixels at up to 20% below display density only when the user's minimum detail allows it; require full display density again after settling. Apply the same rule to scheduling and delayed-result adoption. A new visible-screen request can interrupt background coverage/native work; let an existing screen request finish while input coalesces.

After 200 ms without an image adjustment, at least 80% visible area arms whole-image native completion. That intent survives camera motion until the content changes. Complete one bounded native section per dispatch, selecting missing visible detail before remaining whole-image work. Pause speculative native work during navigation; already stored visible sections may reload without graph execution. Finished sections belong to the project/view content revision and survive camera changes. Retain native float32 pixels and paired HDR output in the CPU section store with a shared 256 MiB retained RAM allowance and temporary delete-on-close disk spill. Perform readback, storage and reload on workers; retire the CPU store off the UI thread. Coverage queries read separately published immutable readiness and never acquire the payload/I/O lock. Publish the store and its presentation slots transactionally.

GPU section residency counts both native images and smaller display copies, including HDR pairs. Plan the admitted visible set before eviction or restoration; retaining every visible native section cannot override the byte allowance. Preserve baseline coverage and affordable existing detail first, then recently used offscreen copies. The per-view allowance is at most 1 GiB and one quarter of measured working headroom plus that view's owned section bytes, sampled together; unmeasured devices use 128 MiB. Restore only admitted missing display data, avoiding eviction/reload loops. CPU/disk completion continues independently, and stored native pixels survive GPU eviction. Never draw a coarser section copy over finer matching current presentation. Reloads need admission for both native and display allocations but must not execute the image graph again. Memory refusals preserve accepted imagery and defer at the budget polling cadence, separately from bounded execution-error retries; settled stored coverage can still allow scopes. Draw only intersecting sections, with no storage I/O or graph planning in drawing. Whole-window backdrop coverage includes photo pixels beneath panels and at every window edge; pane fitting/input bounds do not clip backdrop demand. Scissor each disjoint backdrop copy patch to its actual coverage, including explicit edge extension, and reuse the completed screen copy while content and placement are unchanged. Image edits invalidate the old section store; original RAW inputs remain read-only.

Section requests do not regenerate comparison imagery owned by the existing base presentation. Image Output aliases inherit their input raster identity so a valid downstream cache can stop earlier work through lowered layer boundaries; scalar visualizations retain their canvas dependency. Preserve reusable complete processing boundaries ahead of regional intermediates, within the available cache allowance after reserving active work. For verified single-pass Bayer preview paths, count sampled complete-color output separately from fixed native sensor/correction/variance residency. Global operations need complete input evidence but do not automatically promote sampled RAW input to native dimensions; multipass demosaics and unproven sources retain conservative estimates. Admit native section work on the context-owning worker using measured headroom plus its actual reclaimable cache bytes; free-memory checks on the UI must not reject subsequent sections because prior sections retained reusable dependencies. Retire obsolete output borrows before trimming, and let reuse preferences yield to a reduced budget while preserving active dependency protections. Retired owned graph outputs can enter the existing transient allocation pool only with matching dimensions/format and within its allowance. They remain unavailable during that execution; between-execution admission releases that protection even for a restore-only request. Budget eviction still frees memory rather than recycling it.

Native-backed display coverage and a single native output suitable for export/readback are separate proofs. Display copies are derived from finished native pixels and may be smaller than the source when the screen is smaller. Preserve ordinary explicit-export validation. Base provisional sampling on visible pixels, retain full-image sample coordinates, and include guard bands and complete-input operations in memory admission. Image edits may use adaptive previews; navigation and floating-window movement must not postpone settling an actual image edit. Native texture dimensions alone do not prove settled processing quality when interactive kernels were used.

Keep auxiliary thumbnails, scopes and captures independent of the main image demand. Explicit exports are terminal operations: submit one captured render, preserve its completion through preview coalescing, and accept it by operation generation before viewport freshness checks. A later preview may wait for the active export readback; encoding and publication stay on their operation worker. Accepted image geometry does not establish a view's physical display demand: before a current image consumer reports coverage, use the provisional preview size rather than unmeasured placeholder dimensions. Published detail, matching overview and retained fade textures have separate leases and readiness fences. Preserve a bounded copy of a whole-view result when available; schedule missing whole-image coverage as a separate refinement request. Budget the copy that actually runs, not a removed second graph evaluation. A settled navigation result can improve the overview without replacing a finer current patch. Optional coverage failures or provisional denoise must not downgrade settled foreground pixels or mark them failed. Never substitute original/before-image pixels for current-content coverage. Presentation uses logical coordinates and sample-grid coverage; numerical pixel inspection requires resident native samples. A view change cancels an incompatible fade, and delayed results must still match project, output, view and required coverage. Interactive timing includes publication work; cached navigation/native refinement does not teach the editing cost curve.

HDR preview publication distinguishes an output that has no HDR rendition from a failed supported rendition. `Hdr/PhotoPreview` reports the latter to its caller. Main and tiled jobs withhold failed presentations; optional overview failures leave the accepted detail intact. Do not substitute an SDR rendition for a failed HDR render as resolution or visible coverage changes.

SDR output adjustments belong to the shared View Transform (`Hdr/SdrFinish.h`), after the common edited finish. Brightness, highlight separation and color affect SDR PNG/JPEG export, proof and the gain-map JPEG base; they do not alter the HDR master. SDR adaptation must balance brightness and color preservation; numerical RGB-ratio preservation alone does not establish a perceptual hue match. Validate saturated real-photo highlights and ordinary tones as well as CPU/GPU parity. The HDR-derived finish reserves range above ordinary white and has a continuous tail for imported highlights beyond the mastering peak. PQ sources without an authored SDR base use this same SDR finish; gain-map JPEGs retain their authored base. The SDR appearance table is baked from a pinned perceptual reference and embedded in the binaries; CPU and processing shaders use the same tetrahedral interpolation. The View layer retains its immutable GPU table until context-owned cleanup. Prepare trim coefficients on an image request, not per pixel or UI frame. Preview SDR is transient editor state that selects the accepted SDR texture, without rerendering or changing Windows HDR. On a display with less than mastering headroom, photo drawing reconstructs the available brightness from its SDR/HDR pair before UI composition (`Hdr/GainMapPreview.h`). `Hdr/PhotoPairs` contains only bounded, current-frame texture-pair metadata, not image ownership or another pixel cache. The final scRGB conversion scales to Windows white without applying a second highlight shoulder. Unpaired HDR decorations use a continuous white-anchored highlight shoulder; they are not an authored export proof.

The viewport FPS slider's colored track forecasts editing display sampling from the last compatible admitted graph plans and current timing evidence. It never triggers graph planning or rendering from UI drawing. Gray means evidence is unavailable, expired or incompatible with the current zoom/detail floor; colors are estimates, not perceptual-quality scores or guaranteed achieved FPS. The minimum-editing-detail control maps Auto to the existing TargetFps preference and percentages to PreserveDetail, keeping inactive saved percentages from silently becoming enforced floors. Native settled detail remains independent of that editing target.

Track refinement by its submitted render generation and carried screen/coverage/native intent. Completing or discarding a packet releases only that request's pending state, even when its camera or content is stale. Refused submission must restore any older request's ownership. Geometry-plan refusal must stop submission and back off independently of camera animation, rather than enqueueing a disconnected request or planning again every UI frame. A transient execution or synchronization failure receives bounded retries; memory/texture admission deferral remains a separate decision. Released or replaced controls must end transient drag state even if their widgets are no longer drawn, so hidden controls cannot hold the image at interactive quality.

Return useful progress, cancellation and failure information from the operation. Let the caller decide how to present it. Preserve a specific underlying error rather than replacing every failure with empty output or a generic UI message.

The shipping Bracketing computation runs in `StackBurst.exe`, built by
[`cmake/StackBurst.cmake`](../../cmake/StackBurst.cmake). Stack callers use
[`BurstEngine/Client.h`](../../src/BurstEngine/Client.h) on operation workers.
The client retains prepared sessions for processing, reblend and inspection;
project ownership and result acceptance stay in Stack. Do not link the private
Bracketing computation back into the shipping desktop or wait for IPC while
drawing. Shared recipe, identity, metadata and result serialization code does
not own processing. Internal algorithm diagnostics use the separate diagnostic
build described in [Building](../BUILDING.md).

Completed-result inspection must retain the saved pixels even when an engine
upgrade attaches newly prepared analysis. Native detail reads a bounded saved
result region; compatible live Standard edits use the separate proposed-detail
operation. The project owns this choice and rejects delayed detail when its
result, recipe, revisions or cancellation state changes. Missing saved diagnostic
planes are unavailable, not zero-valued evidence. Distinguish prepared-source
estimates from historical output diagnostics without rewriting the saved result.

The former animated processing scene is archived under `ARCHIVE/burst-processing-visualization`, outside the build. Gallery's Bracket command creates a project in the background, flies its retained thumbnail into the navigation sidebar, and shows a spinner there while metadata/import/processing is pending. It does not activate the receiving project or bind the Bracketing controls. Opening the result from Gallery or its sidebar entry starts in View; explicit Bracketing controls remain available. Other desktop bracket processing presents a progress bar without capturing editor input. Creating a capture set starts the default processing after asynchronous metadata inspection and any required orientation review. Memory-deferred manual brackets retain their project-owned pending request without a worker slot and sample headroom at most once per second while waiting. Automatic workers yield to foreground render/gesture demand and low memory headroom, cancel their private worker process, and retry pending work after a quiet interval with memory hysteresis. Automatic system headroom sampling is bounded to twice per second; drawing does not query hardware.

Burst hosts share the same reconstruction quality plan. Limit background concurrency
for responsiveness; do not omit alignment/refinement because a different host supplies
a smaller budget. Resource or execution failures must remain errors, distinct from
evidence-based rejection of an unreliable match. Standalone finished outputs use the
shared default [View Transform](../../src/Raw/ViewTransform.h): zero-skew Sigmoid
with no automatic exposure offset, followed by the HDR-derived SDR highlight finish.
Its CPU output and GPU layer share prepared curve coefficients and output-finishing
math; color mapping precedes output-primary conversion and encoding.
The View graph caches samples by curve values, independently of its draw geometry.
Sigmoid is the sole supported display mapper for singles and burst results.
Fresh single-RAW opens can seed editable Exposure in the default Background Local EV.
The opening render worker measures at most one 128-pixel scene-linear input probe,
with a 20 ms readback allowance. A matching positive estimate seeds the pristine
graph before the first preview is published. Unavailable evidence uses 0 EV.
Fresh HDR project exposure and highlight fitting run once at first merge acceptance, using
the bounded result overview and unclipped capture evidence. It writes ordinary
editable scene-luminance points to the background Tone Curve before Sigmoid.
Fresh HDR fitting also enables that curve's optional local-contrast preservation:
a bounded log-luminance guide selects regional exposure changes while the original
pixels retain fine detail. New merges additionally retain a compact, source-neutral
camera-color guide with uncertainty and source support. The renderer converts it
with the actual white balance, color transform and orientation. Color protection
uses this evidence for chroma boundaries while current input luminance remains the
tone target; unsupported color falls back to luminance. Local EV area guidance keeps
native color edges and uses the compact evidence to qualify their reliability.
Neither confidence nor source-support fraction is geometric alpha. Keep guidance
owned by its result and renderer, and stop propagation at unknown geometry or
unrelated-source blends. Saved masters without evidence remain usable; rebuilding
the bracket creates the new evidence. Keep guides in the actual working space, normalize
valid coverage, and apply one common RGB gain before the display transform. Its
radius uses the complete image's longer side; viewport crops must consume the
complete local result. Include its bounded temporary fields in memory admission
and local-filter cost in timing identities. Missing local settings mean pointwise
tone processing, and missing color protection leaves the luminance-only local
filter unchanged, so previously saved tone edits retain their behavior. These authored
project adjustments do not bake a look into the linear burst master or imply that
standalone default finishes and diagnostic previews use the project graph.
The retired Standard/Photographic HDR algorithms, their controls and the
Standard-only automatic starting-look planner are removed. Authored Stack edits
can intentionally change the developed appearance. Color-domain contracts belong
inside processing; they do not require persistent graph labels or user-authored
conversion nodes.

HDR admission checks decoded CFA, active-image size and resolved orientation, not
camera/lens labels or exposure tags. The alignment initializer uses bounded image
ranks only; it is not a radiometric calibration. Registered, noise-aware image
patches and explicit user EV anchors establish relative exposure. Captures without
observable overlap get a retry after Automatic local alignment, then are excluded
with diagnostics rather than assigned an EXIF gain or causing usable captures to fail. Missing
information is not recovered HDR. Decoder black/white levels and CFA calibration
remain necessary to interpret sensor samples correctly. Temporal noise fitting
requires calibrated image gain, accepted geometry and compatible measured noise
profiles; ISO equality alone is not a sensor-noise model. The final generic prior
remains uncalibrated and may use a finite ISO hint when image noise fitting fails;
its precision must not be represented as measured noise confidence. Multi-capture GPU
combining first estimates a common scene signal from admitted observations, using
observed headroom and without detail or authored group preferences. The final pass
predicts each capture's noise and headroom from that signal at its calibrated gain;
hard clipping, defects and correspondence rejection remain observation-specific.
The extra sampling pass reuses the existing tile buffers. Single-capture combining
and scalar guide consumers retain observed headroom because they have no refined
common signal. Detail bands use the final pass's same capture headroom.
Alignment proxies count and normalize valid samples independently per CFA color,
require at least 99% channel coverage, and retain clipped-channel exclusion for
exposure fitting. Their resolution budget counts prepared captures, including a
disabled coordinate origin, rather than every entry in the saved source list.
Alignment-only exposure cohorts and local-refit decisions use calibrated image
gains, not saved UI groups. The 0.15 EV full-span convention selects work/anchors,
not valid evidence; uncertain sources align directly to the origin. This never
changes saved groups or enables excluded pixels. A disabled origin remains only
the coordinate/radiometric gauge. Eligible brighter, non-origin cohorts can choose
an anchor from common native image patches using noise-corrected directional
structure. This ranks existing members only; it neither changes output framing
nor removes weak captures. Missing comparable noise/geometry, insufficient common
spatial coverage, tied scores and reused motion retain the median anchor. The
ranking is a heuristic with added prepared-tile reads, measured separately as
`alignment_anchor_ranking`; it is not a new confidence or admission test. Native
sampling phase, noise-model error and dominant moving foreground remain limits.
Optional learned/external motion proposals preserve these native cohorts and
measured anchor choices. Rebase a proposal through the accepted origin-to-anchor
map for the actual pair; a bounded inverse failure omits that hint only.

Native subpixel fitting uses a common measured support across its bounded seed
neighborhood and fixed seed noise denominators, with residual-dependent Huber
weights and bounded sufficient-decrease backtracking. Cap ambiguity evidence
against every eligible separated hypothesis in that node's finest search domain,
including GPU candidates outside the numerical shortlist. Nonnegative partial
costs may exclude a rival only when they prove it cannot reduce that cap; missing
competitive evidence cannot establish uniqueness. This finite search does not
certify continuous or global uniqueness. Re-evaluate physical noise, observability
and covariance at the accepted coordinate; the fitting objective is not a
calibrated measurement likelihood. Point corrections also need the native
positional-precision confidence factor before publication. Keep coverage and
rejection requirements, and distinguish cancellation/resource failure from weak
evidence.

Exposure-cohort composition transports uncertainty through the field's analytic
one-sided derivatives. At a knot, use a PSD upper envelope over supported local
models; averaging opposing slopes can conceal uncertainty. Reject emitted nodes,
including padded boundary nodes, whose covariance would exceed the existing
sampling ceiling. An all-rejected field still has a valid descriptor for reliability
evaluation; rejected evidence must not become a malformed-request error. This
local, first-order envelope does not calibrate a posterior, model correlations
between fitted links, or change the generic interpolation/scatter ceiling. Keep
closure's per-side checks separate; larger covariance does not make every
variance-normalized acceptance test stricter.

Photometrically compatible flat regions are not independent registration evidence.
Exposure fitting tracks the provenance of the current motion field before synthetic
fallback nodes, boundary extension or cohort composition. A direct field needs
accepted global registration or genuine structured local-search support; a composed
field needs that evidence for both the anchor and relative link. Refresh preserves
this provenance. Flat pixels remain useful with independently established geometry;
explicit fixed coordinates and manual EV anchors retain their separate meanings.
Exposure-refresh comparisons use relative gain variance, matching the fusion
domain rather than an absolute gain-dependent tolerance. Current uncertainty
propagation retains marginal variances. Pyramid marginals compose the complete
filter on native samples before squaring coefficients, including reflected
duplicate taps. Correlations between neighboring coarse samples, later
interpolation footprints and shared exposure-graph paths are not fully represented.
Do not present these estimates as a complete covariance-calibrated confidence.

Optional background: [Ward's exposure-resistant registration](https://www.anyhere.com/gward/papers/jgtpap2.pdf)
informs the separation of search evidence from radiometric samples; [Hanji and
Mantiuk's image-based exposure estimation](https://arxiv.org/abs/2308.02968) informs
noise-aware overlap fitting. Neither substitutes for validation on real captures.

The shared `ProcessingRequest::alignmentProposals` contract accepts optional,
content-addressed forward/reverse motion fields as experimental search seeds.
Both hosts use the same RAW refinement, verification and confidence decisions;
proposal values never supply output pixels or override rejection. Source hashes,
model/preprocessing identity, geometry and flow hashes participate in cache identity.
Normal requests omit these assets. The saved recipe's `aiAlignment` switch
defaults to false in both hosts. When enabled, the shared engine builds bounded
guides from prepared RAW captures and invokes the optional pinned SEA-RAFT
package before native registration. Inference ends before native search/merge;
the model supplies motion proposals only. The worker owns cancellation and the
child lifetime. Model/runtime failures are explicit errors, not silent changes
to the selected recipe. The package is optional and separate from the normal
application payload; see [Building](../BUILDING.md). Never run package discovery
or inference from frame drawing. Changing the switch uses normal recipe history,
persistence and analysis invalidation.

Capture combining uses the prepared-session [TemporalGpu](../../src/Raw/Bracketing/TemporalGpu.h)
owner for double-precision sampling, uncertainty propagation and ordered temporal/detail
fusion. CPU correspondence and local-detail evidence remain authoritative. Tile buffers
are bounded and reused across captures. The joined group-processing stage may retain
one idle exact-layout workspace for reuse between tiles; its final operation scope
drains that storage, so retained prepared results do not retain bulk GPU buffers.
Leased, idle and retiring bytes remain budgeted until confirmed cleanup through
the context executor; uncertain cleanup stops further admission. In particular,
unavailable compute, allocation, verification or execution failures stop the job
with diagnostics, never a production CPU substitute. CPU orchestration, decoding
and scalar verification remain explicit algorithm responsibilities. Keep
measurement selection for occlusion/deghosting separate from backend failure.
Report actual GPU tile counts rather than inferring success from context availability.

Local-alignment host-memory admission covers a reference/alternate pair even
when the fixed reference is disabled for fusion. Estimate retained payloads plus
the largest simultaneous pyramid, search or reliability workspace, with checked
arithmetic and the owner-defined reliability cell layout. An insufficient budget
is a resource error, not an unreliable match or permission to reduce alignment
quality. Host admission and GPU allocation checks are distinct; neither estimate
alone establishes combined resident memory on every device.

Consume completed group tiles for neutral-guide statistics before releasing them,
using the same canonical float representation as the group cache. Single-input
uncached measurements retain their GPU double representation. Guide and histogram
writes belong to disjoint tiles, with reduction in stable tile order. Keep Groups
and Guide presentation ordered after worker joins without rereading cache payloads
only to display an already computed guide. Timing must distinguish combined group/
guide work from final guide presentation and reduction.

Motion covariance describes source-pixel displacement. Shared origin-guide gradients
use origin-pixel coordinates, so their registration risk uses covariance transported
through the inverse actual-map Jacobian, including local/composed/refined motion.
Keep source covariance for native interpolation. A singular or discontinuous
derivative disables only guide substitution, not source support or GPU execution.
The CPU scalar consumer and GPU Mapping layout must retain the same convention;
update memory accounting and measurement cache identity with layout/semantic changes.

Native bracket sampling distinguishes measured radiance from the gradient used
to estimate registration risk. A clipped outer stencil sample may request a
bounded local-plane reconstruction, but every positive central-bilinear sample
must remain measured. Preserve the original value when clipping affects only
its gradient. Check conditioning, residuals, variance amplification and clipped
lower bounds before accepting partial support; defects and missing resources
retain their separate failure behavior. Reconstruction risk remains separate
from sensor variance and survives shared-guide gradient substitution. Keep the
strict shared MFD sampler contract unchanged for consumers that require it.

Forward/reverse motion closure is measured in reference coordinates. Transport
forward displacement covariance through the actual reverse-map Jacobian before
combining it with reverse covariance. At a nonunique local-grid knot, require
every supported one-sided linearization to pass the closure gate; keep the native
source covariance unchanged for sampling. Missing derivative evidence leaves
closure unverified without changing the public motion-sample contract. This is
first-order marginal propagation, not a model of correlated forward/reverse fits.

Photometric reliability uses fixed Bayer-cell blocks in reference coordinates,
with a potentially shorter final block. Preserve the reference extent and stride
with the in-process store and use its raw-coordinate lookup; proportional scaling
by the number of stored cells shifts rejection boundaries. Missing geometry or an
unpaired trailing Bayer strip cannot authorize a match.
Carry that same stride into alignment inspection and its wire representation;
the overlay and cursor probe must select the cell actually used by processing.
Legacy inspection without stride remains readable, but its exact grid is
unavailable rather than inferred by proportional scaling.

In Automatic local, a verified patch supports its own footprint. Between
patches, use surrounding verified centers. When the global warp cannot
describe the scene across the frame, require local radiance agreement before
using it in an ambiguous region. Compare exposure-matched, filtered CFA
measurements against noise measured from the original captures.
For native Bayer fusion, uncertain local geometry may use an independently accepted
global transform after a separate, noise-aware four-site neighborhood check.
Use the tested transform for sampling and propagate its positional and model
uncertainty. Respect conflicting motion, depth, and clipping evidence. This
worker-only permission does not promote the local motion field for depth or SR.

Super-resolution keeps categorical photometric rejection separate from its
coarse geometric mapping. A cropped, authoritative reference-grid veto is sampled
by cell before scalar or GPU accumulation; interpolating confidence at coarse
corners must not bridge an interior hard rejection. Passing this veto does not
promote correspondence or replace the existing corner checks. Required raster
shape/resource errors fail the operation. When depth support exists, its owner
also evaluates a conditional veto at exact output points using the established
double-precision lookup; GPU consumers index that tile-bound mask rather than
recomputing rational depth-cell boundaries. The no-depth and fixed-reference
paths retain their existing behavior. Coarse motion/risk interpolation remains;
these categorical vetoes are not dense geometric validation or a new depth model.

SR may retain broad, low-frequency evidence when clipping removes distant
samples but supported local color remains. Fade that contribution through the
highlight shoulder and coordinate its acceptance across colors. Combine its
effective coefficients with the ordinary broad estimate before propagating
variance, narrow/broad covariance and per-capture support; two estimates of
the same capture are not independent evidence. Partial reconstruction does not
claim additional narrow-kernel sampling phases or override geometry vetoes.

Aligned non-panorama brackets estimate parallax on the processing worker.
The resulting near-to-far bands and supported pieces are optional graph mask
sources. Their coverage is registered to the sharp result and has no vote in
capture fusion. Mask nodes resolve immutable result identities during graph
lowering. Visible nodes show their grayscale coverage even when disconnected
and unselected. The preview worker serves cached thumbnails on demand. A
missing piece is an unavailable source. Users wire a mask to Gaussian Blur or
another operation in Graph. A failure in optional depth extraction or storage must preserve a valid merged image and report why the masks are unavailable.

New noise and HDR brackets publish native Bayer group measurements without
directional smoothing. Super-resolution retains its directional guide, broad
estimate and native fallback. Auto evaluates the same resolution evidence as
explicitly enabling smoothing and uses unsmoothed groups and HDR weights when
it selects native Bayer. The saved smoothing policy also applies to inspection.
The optional all-results setting uses four same-CFA neighbour directions and
a local range gate before HDR weighting. These measurements run on the worker.

In native HDR and noise bursts, retain accepted CFA measurements when another channel in the
cell has clipped. Use the fixed capture's saturation as a lower bound at that
location, including when the reference exposure group is itself a noise burst.
For an unsupported channel the worker can estimate display color from the
nearby unclipped highlight boundary or a compatible shorter exposure. Missing
channels that need a donor share one coherent donor color; per-channel medians
rank candidate donors rather than constructing a color from different captures.
Tied donor choices are stable under group reordering, including their clipping
masks. The estimate carries zero measurement support. When every reference channel is lost, first use a mapped shorter exposure if its exposure-normalized values satisfy all four clipping bounds. Prefer the capture with the most surviving sites and retain clipping flags on its lost channels. This fallback retains zero measurement support. If that evidence is unavailable, use the
dimmest as-shot neutral consistent with the clipping bounds if camera white
balance is available. The brightness stays marked unrecoverable. Use the same observations for the
native preview, inspection and master.

If only some reference channels are lost and neither boundary color nor a
compatible donor is available, complete only those missing channels using the
as-shot neutral and the fixed capture's surviving values and saturation bounds.
Keep accepted channels unchanged. This unresolved-color completion remains
clipped with zero measurement support; it is not recovered chromaticity. A
bounded boundary search must not leave capped channels next to brighter measured
colors merely because a clipped region extends beyond its search radius. Fade
boundary-color confidence toward unresolved completion at the search limit and
at the surviving-channel compatibility limit, rather than switching colors there.

Auto resolution tests distributed textured regions using green RAW samples at
their measured positions. Compare a multi-capture native estimate with a
single training capture and verify higher bands against independently withheld
captures. Select native Bayer, native RGB or a supported scale up to 4x per
dimension. Apply HDR exposure decisions locally at the selected size and
persist the actual scale and evaluated contribution maps for inspection. Old
recipes keep their authored reconstruction and smoothing settings.

For integer camera RAWs with padded codes, express the bracket clipping guard
in occupied code steps. Verify the least-significant spacing across the
decoded buffer before widening the guard. Keep the camera white calibration
independent of the brightest pixel in a particular exposure.

Resolved noise profiles already include quantization in their offset variance.
Do not add that component again in reconstruction, alignment or presentation;
unresolved inputs must honor their explicit inclusion flag. Defective and
decoder-repaired payloads cannot supply a temporal noise pilot when neighboring
pilot evidence is unavailable. Clipped but otherwise usable samples retain their
separate lower-bound behavior.
Temporal noise fitting averages signed black-subtracted samples before applying
the block signal's nonnegative bound. Rectifying each noisy sample first biases
dark-patch signal estimates; this is distinct from clamping a physical shot-noise
pilot during fusion. Temporal refitting still requires compatible eligible priors
and adequate scene evidence, rather than inventing a model for generic dark input.

Finite metadata does not guarantee finite GPU arithmetic. Validate temporal
readback before publishing any pending groups, preserving valid unsupported and
clipped sentinels. The group-cache owner separately validates finite float
representability before narrowing or writing; do not clamp overflow into an
apparently successful result. GPU failure preserves prior groups; failed
canonicalization preserves its inputs, and invalid writes preserve existing valid
cache files.

Use the [shared notifications subsystem](../../src/Notifications/README.md) for program-wide notices, activity and substantive decisions. Capture the initiating owner's notifier through asynchronous work and retain the feature's lifetime and generation checks. Report outcomes at accepted state transitions, rather than repeated drawing code. Keep messages short, put longer explanations in details, and leave settings forms and useful diagnostics beside their controls. Dismissing feedback does not resolve its cause, and notification history does not determine processing busy state.

## Account for the cost of the boundary

Trace large buffers and GPU resources through the changed calls. Know whether each is borrowed, shared, moved, copied or recreated. Prefer immutable sharing where lifetime permits; shared ownership alone does not make mutable pixels safe.

Watch for new full-frame copies, readbacks, uploads, conversions, repeated graph construction and duplicate caches. A cleaner API should not silently perform these on every frame or tab switch. Keep cache identity and invalidation tied to the inputs that affect output. Mutable progress and cancellation still belong to the requesting project.

The shell's UI work allowance follows display refresh and recent frame pressure, with a short recovery interval after a hitch. Its shared interaction lease covers mouse motion, active input and presentation animation, including the short settling interval. Count callback capture cleanup as work. Service the active project first, rotate bounded hidden-project turns, and advance their load transitions within that same turn. Optional work must not acquire a fresh allowance through a second call in the same frame. Keep minimum progress for completions, hidden owners and thumbnail uploads: these are cooperative limits between indivisible operations, not a promise that a single callback or driver call cannot stall. Deactivating a workspace cancels obsolete thumbnail demand and releases eligible presentation borrows without waiting for its next background turn.

Render adoption and texture retirement retain per-editor, per-frame count and time limits. Pause new adoption while the editor's retirement backlog is high, after allowing ready cleanup to advance. Leave unread results with their existing coalescing owner, service image and terminal results before optional timing evidence, and preserve export/cancellation acknowledgements. Scheduling metadata must not require copying an entire image/graph snapshot under a shared lock; rejected submissions still retain the full request needed to report the correct failure.

Pace main-image snapshot preparation to the viewport FPS target and coalesce edits while useful image work is running. At an active 100% minimum editing detail, use the display refresh ceiling, disable the FPS target control, and preserve its saved value for lower floors. The percentage describes physical display sampling at the current zoom, not native whole-image completion or kernel quality. Settings forecasts use prepared candidate plans and recent editing measurements; changing the detail floor recalculates the forecast without rendering or replanning. Mark estimates provisional when they include unmeasured sizes. Distinguish queued/rendering snapshots from parked external-model continuations so a slow model does not prevent ordinary edits.

Count actual GPU submissions separately from cheap CPU cancellation probes. Batch by a display-refresh-derived submission interval and count limit: at most two submissions during interaction, eight while idle, including native refinement. Keep at most two bounded batches in flight; fence/flush the new batch, then poll or wait only for the older one instead of emptying the queue at every checkpoint. Keep the final worker completion/publication fence. Only the context-owning worker may wait; the UI never waits on that fence. Fullscreen and RAW fragment passes retain the full sample grid while scissoring at most 128 Ki pixels during interaction and 512 Ki while idle. Reconsider the interaction tile height at each completed raster row so input can tighten an in-flight idle pass. Forward existing demosaic dispatch boundaries to the same queue rather than adding nested waits. Explicit exports cooperate while UI interaction is active without becoming disposable previews. Cancellation discards partial results but cannot remove commands already submitted. Synchronization failures retain the worker-only completion fallback. These limits bound submitted work, not execution time: driver compilation, allocation and indivisible commands can exceed the interval. Each refinement plans only its requested candidate rather than rebuilding the editing ladder.

Keep CPU dispatch cost, GPU service cost, queue delay and completed presentation cadence distinct, including when choosing a fade duration. The shared viewport learner uses the directly measured worker interval through GPU completion: taking the larger of CPU submission and GPU duration can miss the GPU tail after preparation. Exclude cached navigation and native refinement from editing samples. Successful warm native refinements may record settled costs under their separate execution identity. Expire individual timing points, and retire incompatible size history after a sustained cost change. One small measured raster cannot establish equal cost for larger unmeasured rasters. Graph viewport timing identities exclude camera position and coverage size; retain source, graph topology, changed operation, processing kernels and auxiliary-work compatibility. Fit fixed, planned regional-pixel and complete-input-pixel costs when independent evidence supports them. Use conservative growth estimates for missing feature combinations, expire stale evidence, and never infer new global-dependency behavior from regional-only samples. Fit on completed timing arrival and forecast from prepared candidates; no fitting or graph planning in image drawing. Select full screen detail immediately when its measured/predicted cost fits the target, while respecting memory admission.

Do not remove necessary synchronization or correctness checks for an assumed speedup. Explain a material memory or latency tradeoff, and choose a focused measurement if evidence is needed. Use the repository's bounded testing policy rather than automatically adding a benchmark suite.

For accepted display textures, use the live resource owner and completed readiness state to establish draw validity. Repeating driver existence queries during drawing can block behind unrelated worker activity. Validate incoming or unowned resources at their boundary; a remembered numeric texture name alone is not a lifetime guarantee.

View's image-surround blur starts disabled and toggles with an unmodified F press through the editor command-key guard. Its transient presentation choice survives Fit, image changes and control contact; none of those operations may change it. Graph's F note reveal keeps its existing input scope. Navigation may multiply the blur's reveal opacity, but cannot override a disabled surround. Toggling does not dirty the document or invalidate processing.

The shell compositor owns its presentation pass and receives its framebuffer destination explicitly; its backdrop receives the intermediate destination from the compositor. These passes establish their documented final state instead of querying and restoring arbitrary caller state every frame. Nested processing passes with unknown callers still need their existing state guards. The final-window compositor uses the owned-state ImGui entry point generated from the pinned backend draw loop. It assumes the application context's lower-left clip origin, establishes the next pass's state explicitly, and avoids per-pass driver state readbacks. Arbitrary/nested ImGui callers keep the state-preserving entry point; texture updates, callbacks, HDR pairing and VAO cleanup remain common. Large tightly packed float texture uploads are transferred in bounded row batches after allocation, with identical precision and unpack-state restoration; integer and packed formats retain their original path. The backdrop's copy/presentation shaders share explicit OpenGL 4.3 uniform locations with their draw code, avoiding repeated program-name queries. Resolution comparisons reuse operation capabilities only within one immutable graph snapshot.

The Windows HDR compatible transfer keeps one PBO in flight and polls its fence with zero timeout. During UI interaction, comparison and pixel readback share that fence so a changed frame does not require a second readback polling turn. Unchanged frames still skip the full CPU copy and D3D upload. Idle presentation compares first to avoid unnecessary full-window readback bandwidth. Preserve this latency/bandwidth distinction and the nonblocking presentation contract when changing transfer scheduling. Iconification/zero drawable extent suspends presentation; it must not destroy accepted HDR resources or change processing identity. Preserve pending transfers and display notifications, then apply genuine resize, device or SDR/HDR changes when the drawable returns. Ignore iconified viewport coverage updates. When all windows are suspended, pace the existing completion pumps with an event-woken wait; a visible detached window retains ordinary frame pacing.

Gallery thumbnail decoding uses a dedicated, bounded TaskSystem lane: one active decode and at most 24 queued requests across owners. Obsolete requests carry cancellation tokens, and each editor tracks only its bounded outstanding demand for frame-time retirement. Keep explicit image opens off this lane. Background source verification and camera-JPEG grouping yield to current thumbnail demand and active image reads; they must not occupy the thumbnail or foreground workers while waiting. Permanent Gallery does not run the catalog-wide neutral RAW-thumbnail generation pass. Texture upload byte/count/time budgets and project-scoped completion checks still apply.

Original-image browsing retains the camera JPEG representation when a larger embedded preview is unavailable; resolution demand must not silently switch to developed RAW. Filmstrip slots do not change size when decoded thumbnails arrive. Wheel input eases toward bounded travel without a direct tick jump or accumulated inertia, and finishes within 265 ms of the final input.

New integer RAW developments enable Color Reconstruction at full strength with a 0.98 sensor threshold. Reconstruction uses a continuous sensor-domain clipping confidence, bounded calibrated CFA color donors, and a working-space neutral fallback. Near clipping, fade contaminated demosaic detail toward non-ringing bilinear support; healthy pixels retain the selected demosaicer. Keep white balance consistent when fitting color and define neutral after camera conversion. Off retains the fully clipped sensor-white completion only. Floating/HDR mosaics carry no single-frame clipping mask: their overrange values are valid signal. HDR donor confidence blends whole color estimates into the prepared fallback without changing accepted measurements or giving estimates measurement support. This work runs on image invalidation or fusion, never unchanged UI frames. Neutral completion does not recover lost detail. Saved graph settings and current-format recipes missing the additive reconstruction fields retain Off / 0.5 / 0.98; new defaults must not alter them on load.

The `--diagnose-workspace-switcher` diagnostic explicitly drives the legacy switcher animation; it does not validate current project/window-switching appearance. Use the current editor/navigation paths when checking that UI.
