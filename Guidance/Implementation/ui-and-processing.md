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

## Connect new behavior to existing behavior

For a new processing control, locate its parameter model, edit action, invalidation path, engine input and result publication. If the edit is meant to persist, include its saved representation and load path. If it supports undo or reset, check those operations too. Extend the existing semantic path instead of making preview, graph, queue and export interpret the same parameter differently.

View-specific presentation settings can stay local when they do not alter the saved result. Distinguish a cheaper preview from an authoritative output deliberately. Preserve color space, transfer function, orientation, alpha, dimensions and precision at processing boundaries. Name any intentional approximation.

Return useful progress, cancellation and failure information from the operation. Let the caller decide how to present it. Preserve a specific underlying error rather than replacing every failure with empty output or a generic UI message.

Use the [shared notifications subsystem](../../src/Notifications/README.md) for program-wide notices, activity and substantive decisions. Capture the initiating owner's notifier through asynchronous work and retain the feature's lifetime and generation checks. Report outcomes at accepted state transitions, rather than repeated drawing code. Keep messages short, put longer explanations in details, and leave settings forms and useful diagnostics beside their controls. Dismissing feedback does not resolve its cause, and notification history does not determine processing busy state.

## Account for the cost of the boundary

Trace large buffers and GPU resources through the changed calls. Know whether each is borrowed, shared, moved, copied or recreated. Prefer immutable sharing where lifetime permits; shared ownership alone does not make mutable pixels safe.

Watch for new full-frame copies, readbacks, uploads, conversions, repeated graph construction and duplicate caches. A cleaner API should not silently perform these on every frame or tab switch. Keep cache identity and invalidation tied to the inputs that affect output. Mutable progress and cancellation still belong to the requesting project.

Do not remove necessary synchronization or correctness checks for an assumed speedup. Explain a material memory or latency tradeoff, and choose a focused measurement if evidence is needed. Use the repository's bounded testing policy rather than automatically adding a benchmark suite.
