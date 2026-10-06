# Notifications and activity

Features supply content and guarded actions. This subsystem owns the session records, default layout, routing, dismissal and presentation. Ordinary notices need no drawing code or entry in a message catalogue.

Use a short title and one useful sentence. Put paths, diagnostic text and longer explanations in `details`, which starts collapsed. Add `context`, `items`, measured `Progress`, or a preview only when they help the user decide or understand the result. The shared presenters follow Stack's theme and text scale.

## Bind an owner

The application owns one `std::shared_ptr<NotificationStore>`. It creates scopes with stable workspace or service IDs and passes them through `SetNotificationScope`. Features expose their stored scope through `GetNotifier()`.

```cpp
namespace N = Stack::Notifications;
auto scope = store->ForOwner(workspaceId, projectName, N::OwnerKind::Project);
editor.SetNotificationScope(scope);
```

Capture a copy of the initiating scope when work starts. It retains owner identity and generation through tab changes, and holds the store weakly. `SetOwnerLabel` updates the displayed label. `InvalidateOwner` disables callbacks when a workspace or service closes; a later scope for that ID gets a new generation. Do not resolve a worker's recipient by looking up the active editor.

Processing state, cancellation and result acceptance stay with the existing operation owner. A notification record is feedback about that work. Its absence or removal never means the work succeeded.

## Post notices and real outcomes

```cpp
auto scope = GetNotifier();
scope.Info("The folder has no matching images.");
scope.Warning("Some images could not be imported.", "Import", failureDetails);
scope.Error("Could not open the project.", "Open project", errorDetails);

N::NoticeSpec notice;
notice.title = "Export";
notice.message = "Image saved.";
notice.context = outputName;
notice.severity = N::Severity::Success;
notice.outcome = N::Outcome::Success;
scope.Post(std::move(notice));
```

`NoticeSpec` also accepts actions, `operationId`, `dedupeKey`, `imagePreview` and specialized `customBody` content. Reuse one operation ID for stages and outcomes of the same operation. Independent operations get different IDs even when their labels match. A dedupe key groups repeated events within that owner, operation and record kind; it is a local feature key, not a central message catalogue.

Post success after the operation's accepted result or completed write. Use `Outcome::Partial` when only part succeeded and preserve the failed items in details. Routine recomputes and autosaves normally use quiet results with `preview = false`. The default preview lasts four seconds, pauses for hover or keyboard focus, and waits behind a center dialog.

## Track asynchronous work

```cpp
auto scope = GetNotifier();
auto activity = scope.BeginActivity("Import");
scope.UpdateActivity(activity, "Reading images", N::Progress{3, 10, "images"});
scope.CompleteActivity(activity, "10 images imported.");
// Other terminal paths:
// scope.FailActivity(activity, "Import failed.", errorDetails);
// scope.FinishActivity(activity, N::Outcome::Partial, "8 of 10 images imported.", failureDetails);
// scope.CancelActivity(activity, "Import cancelled.");
```

Use `BeginActivity(title, false)` for routine work. `UpdateActivity` changes stage, progress and details without another preview. `ForgetActivity` removes an execution-only collector record without inventing an outcome; features should normally report an actual terminal result instead.

For workers, include `Notifications/AsyncActivity.h` and pass explicit metadata through the owner's existing `Async::TaskGroup` or shared task system:

```cpp
auto metadata = N::ForAsyncActivity(scope, activity, "Reading images");
auto completion = N::RetainAsyncActivity(scope, activity);
```

Capture `scope`, `activity` and `completion` in the submitted worker and its main-thread completion. `ForAsyncActivity` preserves owner, generation and operation identity. Set `metadata.maintenance` for routine work. `TaskGroup::PostToMain` inherits the worker ticket, including during the handoff. Check submission and main-thread publication rejection, and catch feature worker failures. Report the result when the owner accepts it. Keep the retained completion through that delivery; discarding it cancels an unfinished activity rather than leaving a permanent running record. This helper supplements the feature's existing lifetime lease and generation checks.

## Request a decision

Use `RequestDecision` for a required choice. Set `foreground` only when this choice blocks the user's current action. Background decisions remain in activity until the user reviews them. Opening a background decision names its owner and keeps the current project visible; switching projects requires the explicit View project action.

In this example, `current` and `replace` are feature callbacks. They capture the original owner, stable target ID and applicable generation, with a safe lifetime.

```cpp
N::NoticeSpec decision;
decision.title = "Replace imported project?";
decision.message = "A project with this name already exists.";
decision.context = projectName;
decision.operationId = scope.NewOperation();
decision.foreground = blocksCurrentAction;

N::ActionSpec cancel;
cancel.label = "Cancel";
cancel.safeCancel = true;
cancel.invoke = [] { return N::ActionResult::Success(); };

N::ActionSpec accept;
accept.label = "Replace";
accept.destructive = true;
accept.canInvoke = current;
accept.invoke = replace;
decision.actions = {std::move(cancel), std::move(accept)};
const auto event = scope.RequestDecision(std::move(decision));
```

`canInvoke` and `invoke` run on the main thread. Return `Success` only when the action succeeded. Return `Failure(message)` to explain the failure in the same dialog and keep it available for retry. Return `Pending` for asynchronous actions, then call `scope.FinishAction(event, actionIndex, result)` on delivery. The store blocks repeat activation while pending. Use `resolveOnSuccess = false` when the action starts recovery and the issue should stay until the real outcome arrives.

Only a named `safeCancel` action handles Escape. Destructive actions cannot be the default keyboard action. A decision without supplied actions receives an Acknowledge action. Outside clicks do not dismiss center dialogs. The shared frame is fixed, centered and blocks underlying Stack input. Retained naming and editing forms finish before a notification dialog opens.

When the target is replaced or cancelled while its workspace stays open, call `InvalidateOperation` and keep feature generation checks. Editor decisions can use `EditorModule::RequestNotificationDecision`, which also tracks document and load identity. Application owner invalidation handles closed workspaces, but cannot prove that a surviving workspace still contains the original target.

## Previews and specialized content

`NoticeSpec::imagePreview` accepts `ImagePreview {textureId, width, height, caption, lifetime, canShow}`. Supply an existing renderer texture and a feature-owned `shared_ptr<void>` lifetime. `canShow` validates that its contents still match the original operation. The presenter sizes the image; it does not create a render, upload pixels, or own GPU allocation rules. Use a resource owner with the correct renderer/context cleanup behavior when the record releases its capture.

Orientation and import comparisons use `customBody` inside the same dialog frame. Keep those callbacks limited to feature content and guard their targets. Set `DialogSize::Large` for comparisons that need more space; ordinary dialogs retain the compact default. The feature still supplies the actions through `ActionSpec`.

`Dismiss(event)` hides the preview or dialog without fixing its cause. `Resolve(event)` marks the issue handled after a verified recovery or explicit acknowledgement. Keep active issues and unanswered decisions until then. History is session-only and retains the latest 200 completed or resolved records.
