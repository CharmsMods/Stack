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

Input descriptors and plotted histograms are derived UI data. Cache them within the document/node owner using the relevant graph, source, timeline and accepted-readback revisions plus display-domain settings. Uncommitted graph or timeline drafts require fresh descriptor evaluation; camera and floating-tool movement do not require rescanning unchanged image samples.

In the unified graph viewport, optional scope readbacks are independent auxiliary requests after the current image settles. Queue intent separately from an in-flight generation, and reject readbacks from an older document revision. Refreshing a scope must not advance the main-image revision or restart its native refinement. A memory-deferred native image may still permit settled analysis.

Image cache identities include runtime policies that change pixels, not just authored parameters. Spatial Color Warp mask resolution, interactive fallback and required-spatial policy affect its output and downstream stages, but not preceding RAW, exposure or tone stages. Cache-availability telemetry uses the same policy as execution. Input-dependent operations derive their raster identity from their actual input and viewport demand, not mutable dimensions left by an unrelated graph branch.

Graph channel colors use `GraphOutputDescription` component origin and role. A modified channel keeps that identity while it still derives from one source component; independent spatial operands or different channels clear it. Uniform numeric coefficients do not introduce another image lineage. Neutral themes retain these semantic channel colors. Drawing reads the editor's revisioned output-description cache. Direct component sockets are authored `Node::channelOutputs` declarations managed by `NodeGraph/ChannelOutputs`; execution lowers them through the existing Channel Split operation and publishes the authored endpoint aliases. RAW technical-source declarations belong to `RawLayerStackState::sourceChannelOutputs`. Fold redundant splits only inside an explicit graph edit, preserving retained compound input contracts and the existing history/layout owners; do not scan for them during drawing.

## Connect new behavior to existing behavior

Retained decompiled containers own their `compoundMembers`; rectangle overlap alone is not ownership. The shared membership helper handles explicit admission/departure, and measured bounds include nested containers. Packing transfers all owned operations, including effect-layer nodes, and exposes crossing output dependencies as compound ports. The Inputs gateway is a presentation of real typed endpoints, not another processing operation. Its binding cache follows graph ownership and structure revisions; displayed wire geometry and picking use the same entry/exit anchors. The RAW canvas omits the redundant layer Start while `RawProjectGraph` preserves and reconstructs the internal `CurrentImage` boundary on commit.

For a new processing control, locate its parameter model, edit action, invalidation path, engine input and result publication. If the edit is meant to persist, include its saved representation and load path. If it supports undo or reset, check those operations too. Extend the existing semantic path instead of making preview, graph, queue and export interpret the same parameter differently.

View-specific presentation settings can stay local when they do not alter the saved result. Distinguish a cheaper preview from an authoritative output deliberately. Preserve color space, transfer function, orientation, alpha, dimensions and precision at processing boundaries. Name any intentional approximation.

RAW and Graph visible-area previews use [ViewportPreviewTypes](../../src/Renderer/ViewportPreviewTypes.h) to distinguish the logical output canvas, its complete sample grid, and the resident texture rectangle. The editor owns each view's demand, resolution feedback and result acceptance; [GraphViewportPlanning](../../src/Renderer/GraphViewportPlanning.cpp) resolves endpoint and input demands from an explicit graph. Complete dependencies remain complete when an operation needs global data, spatial neighborhoods or an unsupported coordinate mapping. Compatible consumers may crop those immutable outputs and process a smaller raster. A viewport crop must not change an authored image window, mask canvas or export request.

Camera movement changes view demand, not image content. Reuse sufficient resident coverage without repeatedly invalidating the graph. Prefer retaining a complete settled native output when the memory and texture-size budgets admit it, so panning across that image requests no processing. When only regional detail fits, keep its matching overview visible during navigation and schedule missing coverage after the gesture ends. Base provisional sampling on visible pixels, retain full-image sample coordinates, and budget any guard band as real execution work. Image edits may use adaptive previews; navigation and floating-window movement must not postpone settling an actual image edit. Native texture dimensions alone do not prove settled processing quality when interactive kernels were used.

Keep auxiliary thumbnails, scopes and captures independent of the main image demand. Accepted image geometry does not establish a view's physical display demand: before a current image consumer reports coverage, use the provisional preview size rather than unmeasured placeholder dimensions. Published detail, matching overview and retained fade textures have separate leases and readiness fences. Presentation uses logical coordinates and sample-grid coverage; numerical pixel inspection requires resident native samples. A view change cancels an incompatible fade, and a delayed result must still match its project, output, view and required coverage before adoption. Interactive timing includes the requested overview and publication work; cached native refinement does not teach the editing cost curve.

Track native refinement by its submitted render generation. Completing or discarding a packet releases only that request's pending state, even when its camera or content is stale. A transient execution or synchronization failure receives bounded retries; memory/texture admission deferral remains a separate decision. Released or replaced controls must end transient drag state even if their widgets are no longer drawn, so hidden controls cannot hold the image at interactive quality.

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

Burst hosts share the same reconstruction quality plan. Limit background concurrency
for responsiveness; do not omit alignment/refinement because a different host supplies
a smaller budget. Resource or execution failures must remain errors, distinct from
evidence-based rejection of an unreliable match. Standalone finished outputs use the
shared default [View Transform](../../src/Raw/ViewTransform.h); authored Stack edits
can intentionally change the developed appearance. Newly imported Bracketing drafts
can initialize the untouched default Local EV and Tone Curve nodes once, using
accepted scene-linear analysis and the ordinary RAW-layer history transaction.
This starting look changes neither reconstructed RAW nor standalone file output.
It is session-owned, rejects changed document/source/node revisions or custom
presentation, and does not rearm on undo, saved-project reopen or reprocessing.

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
its precision must not be represented as measured noise confidence. Capture headroom weights
apply before group averaging with matching CPU/GPU and detail-band arithmetic.
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

Photometrically compatible flat regions are not independent registration evidence.
Exposure fitting tracks the provenance of the current motion field before synthetic
fallback nodes, boundary extension or cohort composition. A direct field needs
accepted global registration or genuine structured local-search support; a composed
field needs that evidence for both the anchor and relative link. Refresh preserves
this provenance. Flat pixels remain useful with independently established geometry;
explicit fixed coordinates and manual EV anchors retain their separate meanings.
Exposure-refresh comparisons use relative gain variance, matching the fusion
domain rather than an absolute gain-dependent tolerance. Current uncertainty
propagation retains marginal variances; correlations from shared exposure-graph
paths and repeated overlapping pyramid filtering are not fully represented.
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

Super-resolution keeps categorical photometric rejection separate from its
coarse geometric mapping. A cropped, authoritative reference-grid veto is sampled
by cell before scalar or GPU accumulation; interpolating confidence at coarse
corners must not bridge an interior hard rejection. Passing this veto does not
promote correspondence or replace the existing corner checks. Required raster
shape/resource errors fail the operation. Coarse motion/risk interpolation and
depth-mask sampling remain separate limitations, not dense per-pixel validation.

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

Pace main-image snapshot preparation to the viewport FPS target and coalesce edits while useful image work is already running. Distinguish queued/rendering snapshots from parked external-model continuations so a slow model does not prevent ordinary edits. The render worker waits for its submitted GPU batch before publishing completion and dispatching another render; the UI never waits on that fence. Keep CPU dispatch cost, GPU service cost, queue delay and completed presentation cadence distinct, including when choosing a fade duration. The shared viewport learner uses the directly measured worker interval through GPU completion: taking the larger of CPU submission and GPU duration can miss the GPU tail after preparation. Exclude cached navigation and native refinement from editing samples, expire individual timing points, and retire incompatible size history after a sustained cost change. One small measured raster cannot establish equal cost for larger unmeasured rasters.

Do not remove necessary synchronization or correctness checks for an assumed speedup. Explain a material memory or latency tradeoff, and choose a focused measurement if evidence is needed. Use the repository's bounded testing policy rather than automatically adding a benchmark suite.

For accepted display textures, use the live resource owner and completed readiness state to establish draw validity. Repeating driver existence queries during drawing can block behind unrelated worker activity. Validate incoming or unowned resources at their boundary; a remembered numeric texture name alone is not a lifetime guarantee.

The shell compositor owns its presentation pass and receives its framebuffer destination explicitly; its backdrop receives the intermediate destination from the compositor. These passes establish their documented final state instead of querying and restoring arbitrary caller state every frame. Nested processing passes with unknown callers still need their existing state guards. Unbind completed compositor shaders before invoking ImGui so the backend does not need to validate and restore those shared programs. The backdrop's copy/presentation shaders share explicit OpenGL 4.3 uniform locations with their draw code, avoiding repeated program-name queries. Resolution comparisons reuse operation capabilities only within one immutable graph snapshot.
