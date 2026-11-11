# Node editing surfaces

Read when adding or changing the image editing interface for a graph node. Graph and RAW are two views of the same authored instance. Opening a node must not create another processing operation or a separate saved set of values.

## Register the presentation

`src/Editor/NodeEditors/NodeEditorRegistry` associates an existing processing definition ID with optional primary controls, Settings, reset, image overlays, preferred size and input analysis. Processing contracts, parameters and socket capabilities remain in the live node definition registry. Keep ImGui out of processing definitions.

A missing custom primary intentionally borrows the existing graph controls. Preserve this fallback as tools receive individual designs. An editor that only has image handles can disable its floating surface, avoiding an invisible window over the image. A connection-only node may have no editable values. Do not invent a custom interface merely to populate every registration.

Primary, Settings, reset and overlay callbacks can be supplied independently. Any custom editing callback receives its node draft. An overlay without a floating control must still observe release and cancellation frames through the host, including frames whose values did not change. Current callbacks retain an ImGui active item or the existing explicit shape interaction while dragging; a future custom gesture contract should replace additional tool-specific host checks.

Keep each substantial editor's geometry and interaction logic in its own module. The current registration callbacks adapt existing EditorModule methods; new standalone drawing functions should accept their parameter draft and local view state, as Calibration already does. Similar-looking controls only need shared code when they share behavior.

## Address an instance and commit its draft

The edit target is document ID, owning layer ID and node UUID. Canvas integer IDs and legacy settings indexes are temporary implementation details. Graph selection, double-click and Alt navigation use the same target. Alt replaces the floating editor with an ordered list of connected image-processing instances, omitting mask and parameter-only branches. Hidden captured pointer movement selects full-width rows; horizontal drift cannot leave the list. Release commits, Escape cancels, and capture restores the original pointer position. The list and floating tool crossfade over a finite 220 ms transition while the list surface morphs between the tool bounds and its compact extent. Reduced motion applies the endpoint immediately. There is no graph preview. Owner/revision changes, focus loss and resizing cancel the gesture. The floating host stops submitting controls once the list is fully visible; transitional controls cannot take input. Repeated names are numbered 2, 3 and onward. Resolve embedded parameters from the owning graph's exact definition through `ResolveNodeDefinition`; the builtin inventory alone is not the complete instance catalog.

Coverage behavior follows the live input port's mask contract, including named area and gradient inputs; coverage participation does not remove an editable instance from Alt. Accepted graph edits and history restoration reconcile a removed or newly ineligible RAW target to an available tool. Opening a layer cap preserves its remembered tool. Single-RAW source preparation and output presentation keep their separate selection. A Bracketing source instead opens its canonical managed source-node instance in the ordinary View canvas.

`RawWorkspaceEditContext` captures the target, revision, editability, authored input endpoints and graph-driven parameter IDs. Existing RAW designs use a recipe adapter. Other custom designs edit `nodeDraft` and, for a legacy effect, `nodeSettings`. The host initializes both from the addressed node for fallback controls as well. They return whether values changed. Do not write directly into the live project from drawing code.

Managed Bracketing edits use the source set's existing project actions, rather than a second writable recipe in the node payload. Its Source side panel and floating contribution tool address that same owner. Capture membership remains in the source set; the graph presents one image source. Processing keeps the controls editable and the previous accepted image visible with a compact progress bar. Edits cancel obsolete work and coalesce into one replacement after the gesture settles; result adoption still checks the owning project and recipe revision.

Each RAW operation's recipe adapter caches its decoded recipe in the per-instance editor-state store by document, layer, node UUID and authored graph revision. Each drawing context takes its own value copy. Moving the floating tool resolves pointer input before window layout and does not invalidate that recipe. Curve histogram reuse follows the accepted measurement publication and plotted domain, so new measurements still replace the displayed distribution.

The host prepares changes through `PrepareRawNodeEdit` and commits through `ApplyRawLayerStackEdit`, which also serves graph edits. A changed project, missing node or stale revision rejects a prepared edit. Preserve ownership, layout, connections and the legacy settings slot when changing parameters. Topology changes belong in explicit project commands.

Do not overwrite a graph-driven value with the value shown in a manual control. Use the context's driven-parameter information and the node's existing resolution rules rather than querying the host's current selection again. Missing-mask behavior, source-preparation restrictions and connection validation remain project/processing behavior, not alternate rules in the editor.

## Keep presentation state separate

Graph node notes are authored `Node::notes`, separate from processing parameters. Holding F shares the connection-information reveal and replaces node interiors with notes while retaining native bounds and sockets. `NodeNotesView` owns the transient draft, logical text layout and scroll cache; it commits once through `EditorModule::SetGraphNodeNotes` using document, graph and node-instance identity. Clicking a note retains editing after F is released; Ctrl+Enter or clicking outside saves, and Escape cancels. Text keeps a fixed logical size, wraps inside the node and scrolls when necessary. Do not derive explanatory text or rebuild wrap geometry on every frame. Compound templates supply editable defaults at creation, and decompile/repack retains both outer and child annotations, including deliberately empty text.

Use `context.State<T>()` for per-instance hover, selection, view and interaction state while a registered editor is drawing. The store is owned by the editor host and keyed by the stable target. It does not hold authored values or undo history. State types can implement `EndInteraction()`; the host dispatches that lifecycle on selection/host changes without clearing cached presentation or authored values. Do not retain the drawing context or its borrowed pointers in asynchronous work.

Graph double-click opening uses `NodeOpenTransition`: capture the clicked node's geometry once, retaining small immutable copies of textured node details, morph into the live image, travel to the View camera, then reveal the surround and floating controls. Escape resolves the currently edited node by document/layer/node identity and captures its current geometry against the destination Graph canvas. Start from the current displayed photo even when View was entered directly from Gallery and no opening transition exists. Graph opens with its node library closed. Do not reuse the opening node's old screen rectangle as the return destination: tools, node layout and chrome may have changed. Retarget interrupted motion from its last displayed bounds and image/node/surround/control opacities, retaining the outgoing appearance until it fades. Follow chrome translation without recapturing geometry each frame. Fading controls and tool selection cannot change the editing target during motion; Escape remains available. Ineligible top chrome finishes its exit with input disabled rather than resetting its visible extent at the Graph handoff. The main photo remains borrowed from the current viewport owner. Node-detail snapshots are capped at 512 pixels per edge, shared within the captured appearance, and released when motion ends; do not copy or read back textures per animation frame. Display bounds must not replace the saved camera or drive per-frame processing demand. Update the transition once per frame before shell layout, suppress image gestures/probes during motion, and skip competing image-update fades. Reduced motion or an unavailable target uses an immediate return; document replacement and leaving View clear the retained state.

The shared floating host owns tool placement, the two-button move gesture and visibility. RAW's image canvas owns image navigation and reuses the same press arbitration for left-plus-right dragging outside tools. Active edits and captures keep pointer ownership. Graph's photo has a fixed full-viewport fit and borrows RAW's display textures and blur. `ImageViewTransition` animates photo geometry between views without changing RAW's camera or authored values. A definition supplies its preferred dimensions; measured content size is cached per instance. The host supplies an instance-specific ImGui ID scope. Preserve the minimal, unframed primary-surface design and avoid adding a scrollbar as a substitute for arranging the controls.

Legacy layer controls can opt into a wider expanded graph node through `NodeSurfaceSpec::inlineWidth`, bounded by `maxWidth`. Zero uses the shared width class, and collapsed nodes retain their compact size. Size and reflow the controls from the host's current item width and font scale; raw labeled ImGui inputs can overflow the graph's inherited text-wrap boundary. Prefer the shared node controls for numeric values and interaction capture.

Graph plot presentation lives in [GraphUI/GraphPlotStyle](../../src/Editor/GraphUI/GraphPlotStyle.h). Local EV, Tone and Film Grain use its theme colors, scaled dimensions and drawing primitives. Extend this presentation interface for visual rules shared by node editors; keep domain mapping, authored parameters, processing and gestures with their respective owners. The default remains frameless. A common appearance does not require unrelated nodes to share an editing algorithm.

Light's five independent amounts live in `RawLocalRangeRecipe::light`, with automatic/manual tonal range settings. `Raw/Curves/LightBands` defines nonnegative raised-cosine weights and the common 1%/99% log-luminance fit with a four-stop minimum span. The floating band graph and panel author the same values; Advanced curve edits the existing additive freehand correction. No curve-point ownership conversion is performed. The renderer measures every valid incoming pixel into a bounded histogram, caches scalar fits by incoming content/geometry/working space, and retains at most one full-resolution brightness field. Adjustment-only edits reuse analysis. Scope publication carries the actual fitted range; drawing never measures image pixels. Hidden controls request no presentation analysis.

Tone's component/direct-luminance curves offer optional tonal sliders through `RawLab/CurveTonalSliders`. Their points remain in the node's existing authored collection, with ownership and a retained insertion baseline in `Raw/Curves/TonalCurvePoints`. Owned anchors move only vertically; zero removes the anchor or restores an ordinary point it temporarily replaced. Preserve that metadata through conversion, amount references, save/history and graph gestures. Slider visibility is node-local presentation state. Curve geometry, normalization and histogram display preparation are cached by their actual inputs; showing the sliders does not request image analysis.

Tone's direct editor and Local EV's advanced correction curve use `RawLab/CurveCanvas`. Older Local EV area/gradient surfaces use `DrawBezierCurveInteraction` in `RawLabCurveEditor`; Film Grain reuses that interaction through `DrawFramelessResponseBezier`. Grain's points live in its authored layer parameters, with a flat 1x multiplier preserving the existing three-zone response. Its point editor and reader opt into uncapped collections; existing RAW callers retain their limits. `BuildRawBezierSegments` prepares a collection's geometry once for drawing or processing. Local EV always applies full normalization, retaining the total authored curve and deriving a residual plus uniform offset through `Raw/Curves/LocalEvCurveNormalization`; processing combines them before the existing strength, protection and masks. Normalization is automatic and has no graph parameter socket. Double-click insertion into editable point graphs places the new anchor on the existing line at the pointer horizontal coordinate, preserving the cubic shape. Moving while the second press is held drags the new point to the pointer. Node-specific selection, gestures and the grain reference preview belong to `context.State<T>()`. Color Warp keeps wheel gestures and retained GPU/cloud resources in its per-node ColorWarpEditorState, and selection analysis in ColorWarpFeedback. Photo sampling and selection bridge through the host-swapped per-node primary UI state. Some older area/RGB curve interaction state still belongs to the RAW view structure. Selection clears incompatible gestures and analysis caches. Move that state into the node's own module when changing that design, rather than adding more shared tool-specific fields.

`RawLab/CurveCanvasMapping` owns continuous presentation scales for Tone's direct editors and Local EV's primary curve. Each node/channel initially fits the first valid input histogram with padding, then retains that view through edits and later histogram publications. Wheel zoom crops the scale without compressing its edges; Bend mode reserves the plain wheel for band width and uses Ctrl+wheel for zoom. The graph-view menu explicitly refits or restores the full range. Domain changes start a fresh fit. Linear-value channel curves use logarithmic brightness spacing; exposure curves already use stops. Tone fits its output view to include the existing curve, while Local EV retains its adjustment scale. Histogram bins, evaluated geometry, point picking, probes and inverse pointer coordinates share the mapping; offscreen anchors remain authored but are not hit targets. View navigation never authors an edit or requests image analysis. Cache labels, histogram rebins and transformed/screen geometry by their actual dependencies.

## View Controls panel

Controls is the default photographic editor when opening an image. Its fixed
registration order is Light, Tone, HSL, and Calibration, independent of
rendering order. Light edits Exposure and five tonal anchors in one Local EV
node; Tone retains its luminance/RGB curve; Calibration exposes the three primary
Hue/Saturation pairs; HSL exposes eight fixed families with Hue, Saturation, or
Brightness (the existing per-pin exposure) selected above them. These are views
of existing authored parameters, not a second recipe or an automatic graph repair.

The catalog uses exact resolved definition identities and stable document/layer/node
targets across owned graphs. Include RAW-layer graphs only when the renderer lowers them for the current document; ordinary documents may retain unused RAW defaults. Connected image ancestry determines available
instances, excluding mask and parameter-only branches. Missing or disconnected
nodes leave disabled sections in place. Repeated definitions share a numbered
instance selector; one instance needs no numbered pill. Selection is retained
through graph edits and focus transitions. Graph tools outside the fixed inventory
keep their existing editing route. Rebuild catalogs only on owner/structure changes.

`RawWorkspaceEditContext::presentation` and optional registry `panelPrimary` /
`panelSettings` callbacks select presentation while retaining the shared draft,
commit, history, and per-node state. Each section has Settings collapsed by default.
The panel reset menu operates on the fixed sections and their chosen instances.
Drawing an unselected section must not change project selection. Scope legacy
primary interaction state per instance; GPU resources keep their existing owners.

`PanelRoller` is a short stationary ribbed handle with a separate numeric readout.
Relative horizontal drag changes the draft, Shift reduces sensitivity, clicking
the readout opens exact entry, double-click resets, and Escape cancels the gesture.
Keep existing value limits and one undo transaction per gesture. Ribs move only
during active editing; idle frames do not animate. Disabled placeholders require
no authored model or retained gesture state. Calibration pairs columns only when
the available font-scaled width fits; narrow panels stack them.

Middle-clicking a control enters image-only focus with that node's existing
floating primary editor. Middle-clicking the floating editor again returns;
Escape returns after widgets/popups have had their cancellation opportunity.
The shell hides navigation, toolbars, panels, and their toggles without changing
saved presentation preferences or the image camera. Focus owns one tool: Alt
switching and duplicate selectors are suppressed until return. Leaving View,
switching documents, or losing the target ends focus. Curve middle-button panning
is removed; wheel zoom and explicit Fit/Full view actions remain. Named fixed-panel
tools no longer appear floating merely because the panel was closed.

Only the outer panel body scrolls. Its thin seam scrollbar has no layout gutter;
wheel easing is finite and hiding the body ends its tail. Hidden measured sections
reserve cached height without submitting controls. Keep primary drawing budgets
independent of measured outer extent, and preserve measurements through catalog
refreshes. Captured controls remain submitted until release beyond clipping bounds.
Panel hover tooltips are suppressed; click-opened settings and exact-entry popups
remain available. Panel/tab preferences belong to app state; instance selection,
settings disclosure, channels, and focus are transient presentation state.

Analysis uses the existing serial, identity/revision-checked lane. Tone's visible
curve can request incoming analysis; Light rollers, Calibration rollers, and HSL
rows need none. Expanded settings request analysis only when their existing
editor requires it. Hidden remembered tools do not refresh scopes. Hold the last
accepted display mapping through a gesture, reject stale asynchronous results,
and release retained analysis when a catalog instance disappears. No new idle
image scans, timers, or processing path are introduced by these presentations.

## Reuse analysis and rendering services

Tone Curve and Local EV fit the first valid input histogram, then retain that view; Fit and Full are explicit view actions. Point drags use equal 0.15x screen travel on both axes, including newly inserted points.

The photographic Tone Curve starts on white (luminance). The white, red, green and blue dots select editors for one node. Selection belongs to the node's presentation state and never edits the recipe. The executor applies the luminance gain first, then the three individual channel curves, and preserves alpha. Settings retain Linear values (the default) and Exposure stops. Numeric channels, parametric contrast and advanced tone modes are no longer exposed or accepted by the Tone Curve parameter validator. Input histograms describe the incoming wire and use the selected editor's coordinate scale.

An editor can request an existing input-analysis format through its registration. The renderer samples the selected node's actual incoming image connection and keys results by instance and processing revision. The legacy names of these formats do not force sampling at an old recipe-stage position. Specialized Local EV area statistics remain part of its existing analysis adapter.

The existing graph snapshot pass also publishes each RAW operation's input color interpretation. Drawing reads the per-editor cache by stable node target and incoming endpoint; it does not rebuild graph snapshots. The renderer and UI share the supported working-space mapping. A connected input with unsupported or undeclared color metadata requires an explicit conversion. Local EV's curve guide includes its own exposure offset, while its area-reference statistics retain the original input used by the area calculation. Older area history is scoped to the exact edited node as well as the source photograph.

The context exposes authored input endpoints for additional services. A new asynchronous request must capture its project, target, revision and required source/representation information; completion must validate them before publication. Extend existing graph preview, spectrum and readback services for the required data type. Numerical masks and spectra must not acquire a display transform intended for a color photo.

Freehand capture uses the selected operation's incoming image. The host owns the gesture and coordinate mapping; `RawAssetCapture` turns immutable source pixels and path coordinates into typed samples, value textures or coverage. Saved results retain their source metadata. Saving a capture alone does not modify the graph. Create and apply mask begins one capture action: Background immediately inserts and selects a fresh default instance after the open operation; other layers attach coverage directly to that operation. An unfinished capture has valid zero coverage, distinct from a disconnected mask. The project completes the same mask after validating its source and target, preserves intervening adjustment edits, and fills matching history states so undo does not restore an unfinished placeholder after success. Cancellation or a changed input rejects late results. An interrupted capture remains visibly unfinished and contributes zero coverage. Saving a capture alone never applies it. RAW creation is reached through C-drag or the top-bar mask menu. Graph and RAW host the same project Assets panel. Newly applied captured masks insert a normal Gaussian Blur node at amount 100 between the texture and the receiving operation. The texture remains the asynchronous completion target; existing mask chains are preserved. Finished asset pixels are shared immutably with graph coverage, with their fingerprint prepared on the worker before UI adoption. Captured masks are texture sources with previews and graph math; the retired CustomMask painting editor is not a second edit surface. RAW layer documents own the captured mask payload and restore it for RAW tools. Keep sample strips, unrestricted numerical values and bounded coverage distinct when adding consumers. Stroke matching indexes exact sampled values and uses a continuous distance falloff; the index must not quantize coverage. Captured rasters retain native dimensions independently of the retired painting-canvas limit. Mask render extents are explicit for each preview request, never inherited from whichever branch ran earlier. Downsampling preserves pixel-area coverage; native rendering retains the captured samples. Existing saved masks remain baked data and are not silently regenerated by a new matching algorithm.

This framework does not flatten the saved layer graphs or separate every legacy LayerBase effect into new settings/UI/GPU classes. Those are distinct migrations. Keep adapters explicit while using the shared editing path, and follow the repository's bounded validation policy when replacing one.


Color Calibration's infinite saturation dial edits the existing three primary saturation values together. Their logarithmic radius controls receive the same offset, so radii scale by a common factor around D65 while hue and relative distances remain intact. The dial has no separate processing parameter or saved multiplier. A graph-driven primary disables the shared gesture. Existing primary serialization, undo and CPU/GPU processing remain authoritative. Cache transform diagnostics by the recipe and working space, rather than rebuilding the matrix during every draw.

Color Warp's version-4 recipe owns eight permanent hue families, up to eight accepted custom selections, perceptual brightness, neutral tint, and Move/Turn adjustments. Custom selections retain up to six joint normalized-OKLab/scene-EV population centers with editable brightness softness; these are a bounded approximation, not independent color and exposure marginals. CPU, shader, masks, and selection feedback use the same membership definition on immutable input colors. Brightness applies a bounded lightness-derived gain with highlight rolloff; defaults suppress unreliable neutral hues and very dark signals. The existing bounded guide smooths influence, not image RGB. HDR values and alpha are retained.

Sampling presents suggested swatches for individual acceptance. New selections start with zero adjustment and Everywhere scope; This region applies the retained spatial restriction. Resample explicitly replaces the sampled profile while retaining adjustments and the spatial restriction. Moving a region does not resample colors. Pending suggestions are rejected after input, node, or source identity changes. Accepted profiles serialize with the node and therefore follow ordinary document history. Version 4 intentionally rejects earlier Color Warp formats; no migration or compatibility renderer is provided.

Move translates normalized chroma from source to destination; Turn rotates hue and scales saturation. Neutral tint is a translation in either mode. Influence previews include strength, protection and overlap; an unadjusted selected pin still previews potential influence. Unchanged selections never dilute active edits. Membership and spatial fields are cached by input and selection definition; brightness, tint, destination, strength and protection edits reuse them. Profile analysis stays on the existing sampling worker; drawing introduces no image scans. The compact panel uses thin tracks with hollow handles; custom swatches and selected adjustments appear directly beneath the default HSL rows. The floating wheel separates vertical brightness from hue/chroma and shows quiet surrounding anchors and the retained brightness profile. Source/result swatches use the shared CPU transform on a representative source color, cached by node/local revision, selected pin and working space. They illustrate color transformation; spatial influence is inspected with the affected-area preview.
