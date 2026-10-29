# Node editing surfaces

Read when adding or changing the image editing interface for a graph node. Graph and RAW are two views of the same authored instance. Opening a node must not create another processing operation or a separate saved set of values.

## Register the presentation

`src/Editor/NodeEditors/NodeEditorRegistry` associates an existing processing definition ID with optional primary controls, Settings, reset, image overlays, preferred size and input analysis. Processing contracts, parameters and socket capabilities remain in the live node definition registry. Keep ImGui out of processing definitions.

A missing custom primary intentionally borrows the existing graph controls. Preserve this fallback as tools receive individual designs. An editor that only has image handles can disable its floating surface, avoiding an invisible window over the image. A connection-only node may have no editable values. Do not invent a custom interface merely to populate every registration.

Primary, Settings, reset and overlay callbacks can be supplied independently. Any custom editing callback receives its node draft. An overlay without a floating control must still observe release and cancellation frames through the host, including frames whose values did not change. Current callbacks retain an ImGui active item or the existing explicit shape interaction while dragging; a future custom gesture contract should replace additional tool-specific host checks.

Keep each substantial editor's geometry and interaction logic in its own module. The current registration callbacks adapt existing EditorModule methods; new standalone drawing functions should accept their parameter draft and local view state, as Calibration already does. Similar-looking controls only need shared code when they share behavior.

## Address an instance and commit its draft

The edit target is document ID, owning layer ID and node UUID. Canvas integer IDs and legacy settings indexes are temporary implementation details. Graph selection, double-click and Alt navigation use the same target. Alt lists owned, resolved ordinary instances with editable parameters, including embedded compounds, mask textures and their editable coverage-processing branches. Repeated names are numbered 2, 3 and onward. Resolve embedded parameters from the owning graph's exact definition through `ResolveNodeDefinition`; the builtin inventory alone is not the complete instance catalog.

Coverage behavior follows the live input port's mask contract, including named area and gradient inputs; coverage participation does not remove an editable instance from Alt. Accepted graph edits and history restoration reconcile a removed or newly ineligible RAW target to an available tool. Opening a layer cap preserves its remembered tool. Single-RAW source preparation and output presentation keep their separate selection. A Bracketing source instead opens its canonical managed source-node instance in the ordinary View canvas.

`RawWorkspaceEditContext` captures the target, revision, editability, authored input endpoints and graph-driven parameter IDs. Existing RAW designs use a recipe adapter. Other custom designs edit `nodeDraft` and, for a legacy effect, `nodeSettings`. They return whether values changed. Do not write directly into the live project from drawing code.

Managed Bracketing edits use the source set's existing project actions, rather than a second writable recipe in the node payload. Its Source side panel and floating contribution tool address that same owner. Capture membership remains in the source set; the graph presents one image source. Processing locks that project's document behind its full processing surface while other project owners remain available.

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

Local EV's overall curve and Tone's component/direct-luminance curves offer optional tonal sliders through `RawLab/CurveTonalSliders`. Their points remain in the node's existing authored collection, with ownership and a retained insertion baseline in `Raw/Curves/TonalCurvePoints`. Owned anchors move only vertically; zero removes the anchor or restores an ordinary point it temporarily replaced. Preserve that metadata through conversion, amount references, save/history and graph gestures. Slider visibility is node-local presentation state. Curve geometry, normalization and histogram display preparation are cached by their actual inputs; showing the sliders does not request image analysis.

Tone's direct editor and Local EV's primary overall curve use `RawLab/CurveCanvas`. Older Local EV area/gradient surfaces use `DrawBezierCurveInteraction` in `RawLabCurveEditor`; Film Grain reuses that interaction through `DrawFramelessResponseBezier`. Grain's points live in its authored layer parameters, with a flat 1x multiplier preserving the existing three-zone response. Its point editor and reader opt into uncapped collections; existing RAW callers retain their limits. `BuildRawBezierSegments` prepares a collection's geometry once for drawing or processing. Local EV's normalization retains the total authored curve and derives a residual plus uniform offset through `Raw/Curves/LocalEvCurveNormalization`; processing combines them before the existing strength, protection and masks. Node-specific selection, gestures and the grain reference preview belong to `context.State<T>()`. Some older area/RGB curve and Color Warp interaction state still belongs to the RAW view structure. Selection clears incompatible gestures and analysis caches. Move that state into the node's own module when changing that design, rather than adding more shared tool-specific fields.

## View Controls panel

View's Controls tab is an additional host for the registered primary editor, not
another parameter model or a list of default tool enums. The catalog uses exact
resolved definition identities and stable document/layer/node targets across all
owned graphs. Repeated definitions share one section with numbered instances;
selection of an instance changes presentation, never the graph's membership.
Names are native activation buttons: clicking one selects that section's chosen
instance through the existing node selection path, including image handles and
Settings. Names are slightly dimmed at rest, white with a glyph halo on hover.
The numbered instances sit beside the name in a darkened pill; its selection
bead uses the shared glass selection motion. Native capture lets it follow a
drag within the pill; release selects the nearest instance and cancellation keeps
the previous target. Long lists wrap without changing instance order. Hover never changes the selected target.
The catalog changes on owner/structure revisions, independently of drawing.

`RawWorkspaceEditContext::presentation` distinguishes Panel from Floating.
Target-specific context preparation and `DrawNodeEditorPrimary` reuse the same
registry callbacks and commit path. Drawing an unselected section must not change
project selection. The host scopes legacy primary interaction state per instance;
GPU resources and asynchronous services retain their existing owners. Generic
graph controls draw a private node draft and report changes to that draft rather
than marking the live graph edited from the drawing callback.

Controls uses a thin native scrollbar over the panel seam while scrolling; it
fades out when idle and never reserves a layout gutter. Wheel requests use the
shared finite easing before child layout, preserving native wheel ownership and
matching control hit geometry. Scrollbar dragging and keyboard navigation remain
direct; hiding the body ends its wheel tail. Its primary surfaces retain
their designs at the available width; only the outer body scrolls. Hidden measured
sections reserve their cached height without submitting controls or analysis.
The panel header exposes Reset through its three-dot menu. The definition groups
supply its labels; duplicate instances offer Reset all and individual layer/node
entries. Reset invokes the registered callback and existing draft/history path.
Serialized payload and inspection-only metadata do not create control sections.
Calibration stays fitted at panel width and lets wheel input scroll the panel.
The panel host suppresses hover tooltip windows, including help emitted by shared
primary designs; floating help and click-opened menus retain their own behavior.
Primary drawing uses a stable content budget independent of its measured outer
slot. Preserve section measurements through catalog refreshes; image revisions
must not feed a height feedback loop. Only the outer body scrolls, and captured
controls remain submitted through release outside its clipping bounds. Hold the
last accepted input-analysis display mapping during the active gesture, without
accepting stale asynchronous results.
Floating-only glass and movement stay with the floating host. While Controls is
visible, that host releases pointer ownership and submits no floating window.
Leaving Controls restores the last-used instance with a finite 180 ms fade
(immediate for reduced motion). Panel/tab preferences belong to app state;
numbered-instance selections belong to their document's transient editor state.

Visible editors request their node's actual incoming analysis through the existing
independent analysis lane. Requests are serial, identity/revision checked and
latched on unchanged failures; they do not dirty the main image. Accepted packets
are immutable per-instance presentation data. Hidden sections do not queue new
requests, and removed catalog instances release retained analysis packets.

## Reuse analysis and rendering services

An editor can request an existing input-analysis format through its registration. The renderer samples the selected node's actual incoming image connection and keys results by instance and processing revision. The legacy names of these formats do not force sampling at an old recipe-stage position. Specialized Local EV area statistics remain part of its existing analysis adapter.

The existing graph snapshot pass also publishes each RAW operation's input color interpretation. Drawing reads the per-editor cache by stable node target and incoming endpoint; it does not rebuild graph snapshots. The renderer and UI share the supported working-space mapping. A connected input with unsupported or undeclared color metadata requires an explicit conversion. Local EV's curve guide includes its own exposure offset, while its area-reference statistics retain the original input used by the area calculation. Older area history is scoped to the exact edited node as well as the source photograph.

The context exposes authored input endpoints for additional services. A new asynchronous request must capture its project, target, revision and required source/representation information; completion must validate them before publication. Extend existing graph preview, spectrum and readback services for the required data type. Numerical masks and spectra must not acquire a display transform intended for a color photo.

Freehand capture uses the selected operation's incoming image. The host owns the gesture and coordinate mapping; `RawAssetCapture` turns immutable source pixels and path coordinates into typed samples, value textures or coverage. Saved results retain their source metadata. Saving a capture alone does not modify the graph. Create and apply mask begins one capture action: Background immediately inserts and selects a fresh default instance after the open operation; other layers attach coverage directly to that operation. An unfinished capture has valid zero coverage, distinct from a disconnected mask. The project completes the same mask after validating its source and target, preserves intervening adjustment edits, and fills matching history states so undo does not restore an unfinished placeholder after success. Cancellation or a changed input rejects late results. An interrupted capture remains visibly unfinished and contributes zero coverage. Saving a capture alone never applies it. RAW creation is reached through C-drag or the top-bar mask menu. Graph and RAW host the same project Assets panel. Newly applied captured masks insert a normal Gaussian Blur node at amount 100 between the texture and the receiving operation. The texture remains the asynchronous completion target; existing mask chains are preserved. Finished asset pixels are shared immutably with graph coverage, with their fingerprint prepared on the worker before UI adoption. Captured masks are texture sources with previews and graph math; the retired CustomMask painting editor is not a second edit surface. Its payload remains readable for saved-project compatibility. Keep sample strips, unrestricted numerical values and bounded coverage distinct when adding consumers. Stroke matching indexes exact sampled values and uses a continuous distance falloff; the index must not quantize coverage. Captured rasters retain native dimensions independently of the retired painting-canvas limit. Mask render extents are explicit for each preview request, never inherited from whichever branch ran earlier. Downsampling preserves pixel-area coverage; native rendering retains the captured samples. Existing saved masks remain baked data and are not silently regenerated by a new matching algorithm.

This framework does not flatten the saved layer graphs or separate every legacy LayerBase effect into new settings/UI/GPU classes. Those are distinct migrations. Keep adapters explicit while using the shared editing path, and follow the repository's bounded validation policy when replacing one.

