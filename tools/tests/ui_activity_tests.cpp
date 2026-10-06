#include "Utils/UiActivity.h"
#include "Utils/UiBusyState.h"
#include "Utils/SavePathConfirmation.h"
#include "Notifications/Notifier.h"
#include "Notifications/NotificationPresenter.h"
#include "Async/TaskGroup.h"
#include "App/WorkspaceInputScope.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <future>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

namespace {
using namespace Stack::Notifications;

void CheckActivity() {
    using namespace Stack::UiActivity;
    Presentation view;
    Snapshot idle, overlap;
    view.Update(idle, 0, .016f);
    Require(view.busyBlend == 0, "Idle must not claim an outcome");
    overlap.SetOwner(11, "Coast");
    overlap.Add(true, "Exporting");
    overlap.Add(true, "Exporting");
    overlap.SuppressWorkerLabel("Scanning folder");
    overlap.SetAwaitingInteractionRelease(true);
    overlap.SetOwner(22, "Forest");
    overlap.Add(true, "Exporting");
    overlap.SetAwaitingInteractionRelease(false);
    Require(overlap.entries.size() == 2, "Same-label work must retain both project owners");
    Require(overlap.awaitingInteractionRelease, "An inactive owner must not clear another owner's held adjustment");
    Require(overlap.IsWorkerLabelSuppressed("Scanning folder", 11) &&
        !overlap.IsWorkerLabelSuppressed("Scanning folder", 22), "Worker suppression must stay with its owner");
    overlap.SetPrimary(true, "Hash 12.4 GB");
    view.Update(overlap, 1, .1f);
    Require(view.label == "Hash 12.4 GB", "The measured stage label must take priority");
    Snapshot render;
    render.AddOperation(true, "render", "Rendering proxy");
    render.AddOperation(true, "render", "Full resolution");
    Require(render.entries.size() == 1, "A changing stage must remain one operation");
    view.Update(render, 1.1, .1f);
    view.Update(idle, 1.2, .1f);
    Require(view.busyBlend == 1, "A short stage handoff must retain the spinner");
    view.Update(idle, 1.4, .1f);
    view.Update(idle, 1.5, .1f);
    Require(view.busyBlend == 0 && view.label.empty(), "Stopped execution must return to idle");
    Snapshot held;
    held.SetAwaitingInteractionRelease(true);
    Presentation gesture;
    gesture.Update(render, 10, .1f);
    gesture.Update(held, 10.01, .1f);
    gesture.Update(held, 10.19, .1f);
    Require(!gesture.waiting && gesture.busyBlend == 1, "A pause under 200ms must retain the spinner");
    gesture.Update(held, 10.22, .1f);
    gesture.Update(held, 10.32, .1f);
    Require(gesture.waiting && gesture.busyBlend == 0, "A held adjustment must settle to waiting");
    Snapshot moving = render;
    moving.SetAwaitingInteractionRelease(true);
    gesture.Update(moving, 10.4, .1f);
    Require(!gesture.waiting && gesture.busyBlend > 0, "New proxy work must immediately replace waiting");
    gesture.Update(render, 10.6, .1f);
    gesture.Update(idle, 11, .1f);
    gesture.Update(idle, 11.1, .1f);
    Require(!gesture.waiting && gesture.busyBlend == 0, "Released refinement must return to idle");
}

void CheckRecords() {
    auto store = std::make_shared<NotificationStore>();
    auto coast = store->ForOwner(11, "Coast");
    auto forest = store->ForOwner(22, "Forest");
    const auto a = coast.BeginActivity("Exporting");
    const auto b = forest.BeginActivity("Exporting");
    Require(a && b && a.operationId != b.operationId, "Separate jobs require separate operation identities");
    coast.UpdateActivity(a, "Writing", Progress{2,4,"2 of 4 images"}, "Output folder");
    coast.CompleteActivity(a, "Exported 4 images");
    Require(store->Find(a.eventId)->outcome == Outcome::Success && store->Find(b.eventId)->state == RecordState::Running,
        "One completed job must leave another owner running");
    forest.FailActivity(b, "Could not write the image", "Disk full");
    store->Dismiss(b.eventId);
    Require(store->Find(b.eventId)->NeedsAttention() && store->Find(b.eventId)->outcome == Outcome::Failure,
        "Dismissal must not resolve the failed operation");
    forest.Resolve(b.eventId);
    Require(!store->Find(b.eventId)->NeedsAttention() && store->Find(b.eventId)->outcome == Outcome::Failure,
        "Resolution must preserve the actual outcome");
    const auto observed = coast.BeginActivity("Rendering", false);
    coast.ForgetActivity(observed);
    Require(!store->Find(observed.eventId), "An ended execution predicate must not invent a result");

    NoticeSpec notice;
    notice.severity = Severity::Error;
    notice.message = "Disk full";
    notice.dedupeKey = "save";
    const auto issueA = coast.Post(notice);
    const auto issueB = forest.Post(notice);
    coast.Post(notice);
    Require(issueA != issueB && store->Find(issueA)->occurrences == 2,
        "Repeats group within their owner and leave another owner's issue separate");
    notice.severity = Severity::Success;
    notice.outcome = Outcome::Success;
    notice.message = "Saved";
    coast.Post(notice);
    Require(store->Find(issueA)->outcome == Outcome::Success && !store->Find(issueA)->NeedsAttention(),
        "An actual keyed success must replace the previous failed outcome");
    notice.dedupeKey.clear();
    notice.message = "Imported some images";
    notice.severity = Severity::Warning;
    notice.outcome = Outcome::Partial;
    const auto partial = coast.Post(notice);
    Require(store->Find(partial)->outcome == Outcome::Partial && store->Find(partial)->NeedsAttention(),
        "Partial results must remain distinct from complete success and failure");

    NoticeSpec reviewedIssue;
    reviewedIssue.title = "Partial result";
    reviewedIssue.severity = Severity::Warning;
    reviewedIssue.outcome = Outcome::Partial;
    ActionSpec keepResult;
    keepResult.label = "Keep result";
    keepResult.safeCancel = true;
    keepResult.invoke = [] { return ActionResult::Success(); };
    reviewedIssue.actions.push_back(keepResult);
    const auto reviewed = coast.RequestDecision(reviewedIssue);
    store->InvokeAction(reviewed, 0);
    Require(store->Find(reviewed)->state == RecordState::Cancelled && store->Find(reviewed)->outcome == Outcome::Partial,
        "A safe display action must preserve an original partial processing outcome");
    NoticeSpec critical;
    critical.title = "Critical error";
    critical.severity = Severity::Error;
    critical.route = Route::Center;
    critical.foreground = true;
    const auto criticalId = coast.Post(critical);
    Require(store->Find(criticalId)->content.actions.size() == 1 &&
        store->Find(criticalId)->content.actions[0].label == "Acknowledge",
        "A critical notice without custom actions needs a shared visible acknowledgement");
    store->InvokeAction(criticalId, 0);
    Require(!store->Find(criticalId)->centerRequested && store->Find(criticalId)->NeedsAttention() &&
        store->Find(criticalId)->outcome == Outcome::Failure,
        "Acknowledging a critical display must preserve the unresolved cause and its failed outcome");

    const auto retiredOperation = coast.NewOperation();
    int retiredInvocations = 0;
    NoticeSpec terminalAction;
    terminalAction.message = "Saved result";
    terminalAction.severity = Severity::Success;
    terminalAction.operationId = retiredOperation;
    ActionSpec retiredAction;
    retiredAction.label = "View result";
    retiredAction.canInvoke = [coast, retiredOperation] {
        coast.InvalidateOperation(retiredOperation);
        return true;
    };
    retiredAction.invoke = [&] { ++retiredInvocations; return ActionResult::Success(); };
    terminalAction.actions.push_back(std::move(retiredAction));
    const auto terminalEvent = coast.Post(std::move(terminalAction));
    const auto terminalRevision = store->Find(terminalEvent)->revision;
    Require(store->InvokeAction(terminalEvent,0).state == ActionState::Failure && retiredInvocations == 0 &&
        store->Find(terminalEvent)->revision > terminalRevision && store->Find(terminalEvent)->outcome == Outcome::Success,
        "Retiring an operation during a guard must prevent the captured handler, including on terminal results");

    int attempts = 0;
    bool current = true;
    OwnerId invokedOwner = 0;
    NoticeSpec decision;
    decision.title = "Import conflict";
    decision.dedupeKey = "import-decision";
    ActionSpec replace;
    replace.label = "Replace";
    replace.destructive = true;
    replace.defaultAction = true;
    replace.canInvoke = [&] { return current && coast.Valid(); };
    replace.invoke = [&] { ++attempts; invokedOwner = coast.GetOwner().id; return ActionResult::Pending(); };
    decision.actions.push_back(replace);
    const auto request = coast.RequestDecision(decision);
    Require(!store->Find(request)->centerRequested && !store->Find(request)->content.actions[0].defaultAction,
        "Background decisions must wait for review and destructive actions must not be the keyboard default");
    store->RequestCenter(request);
    Require(store->InvokeAction(request, 0).state == ActionState::Pending && invokedOwner == 11,
        "An action must target its original owner");
    store->InvokeAction(request, 0);
    Require(attempts == 1, "A pending action cannot run twice");
    coast.FinishAction(request, 0, ActionResult::Failure("Could not replace the project"));
    Require(store->Find(request)->centerRequested && !store->Find(request)->actionPending &&
        !store->Find(request)->actionError.empty() && store->CanInvokeAction(request, 0),
        "A failed resolver must explain its failure in the same dialog and remain available");
    current = false;
    store->InvokeAction(request, 0);
    Require(attempts == 1, "Stale operation guards must prevent activation");
    current = true;
    store->InvalidateOwner(11);
    Require(!coast.Valid() && !store->CanInvokeAction(request, 0) && store->Find(request)->content.actions.empty(),
        "Owner retirement must disable and release unsafe callbacks");
    Require(store->Find(partial)->outcome == Outcome::Partial, "Closure must preserve a known partial outcome");
    Require(coast.Info("Stale callback") == 0, "Stale producers must not resurrect an owner");
    const auto reopened = store->ForOwner(11, "New Coast");
    Require(reopened.GetOwner().generation != coast.GetOwner().generation && !coast.Valid(),
        "Reusing an owner ID must not validate old scopes");
    const auto draining = forest.BeginActivity("Saving");
    store->InvalidateOwner(22);
    forest.FailActivity(draining, "Save failed after retirement");
    Require(store->Find(draining.eventId)->outcome == Outcome::Failure && !store->Find(draining.eventId)->ownerValid &&
        !store->Find(draining.eventId)->centerRequested, "Draining work must report its actual result without reviving presentation");

    auto history = std::make_shared<NotificationStore>();
    auto scope = history->ForOwner(1, "Stack", OwnerKind::Application);
    const auto retained = scope.Error("Unresolved failure");
    for (int i=0; i<220; ++i) scope.Info("Result " + std::to_string(i));
    Require(history->Snapshot().size() == 201 && history->Find(retained), "Keep 200 results plus unresolved issues");
    scope.Resolve(retained);
    Require(history->Snapshot().size() == 200 && history->Find(retained), "Newly resolved issues belong in the latest results");
    std::weak_ptr<NotificationStore> lifetime = history;
    ActionSpec captureScope;
    captureScope.label = "Dismiss";
    captureScope.invoke = [scope] { return ActionResult::Success(); };
    NoticeSpec capture;
    capture.title = "Lifetime";
    capture.actions.push_back(captureScope);
    scope.RequestDecision(capture);
    history.reset();
    Require(lifetime.expired() && !scope.Valid(), "A handler's captured notifier must not keep the store alive in a cycle");

    auto previews = std::make_shared<NotificationStore>();
    auto previewScope = previews->ForOwner(33, "Preview owner");
    std::weak_ptr<NotificationStore> previewStore = previews;
    int releasedPreviews = 0;
    const auto makePreview = [&] {
        ImagePreview preview;
        preview.textureId = 1;
        preview.width = 640;
        preview.height = 360;
        preview.lifetime = std::shared_ptr<void>(new int(1), [&, previewStore](void* value) {
            delete static_cast<int*>(value);
            ++releasedPreviews;
            if (const auto store = previewStore.lock()) store->Snapshot();
        });
        preview.canShow = [previewScope] { return previewScope.Valid(); };
        return preview;
    };
    NoticeSpec previewNotice;
    previewNotice.message = "Image ready";
    previewNotice.dedupeKey = "image-result";
    previewNotice.imagePreview = makePreview();
    const auto previewEvent = previewScope.Post(std::move(previewNotice));
    Require(releasedPreviews == 0 && previews->Find(previewEvent)->content.imagePreview,
        "Declarative previews retain their feature resource while the record exists");
    NoticeSpec replacement;
    replacement.message = "New image ready";
    replacement.dedupeKey = "image-result";
    replacement.imagePreview = makePreview();
    previewScope.Post(std::move(replacement));
    Require(releasedPreviews == 1, "Replacing a preview releases its lease outside the store lock");
    previews->InvalidateOwner(33);
    Require(releasedPreviews == 2 && !previews->Find(previewEvent)->content.imagePreview,
        "Owner retirement removes preview guards and renderer leases outside the store lock");
}

void CheckNativePresenters() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(800,600);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    io.ConfigInputTrickleEventQueue = false;
    io.AddKeyEvent(ImGuiKey_A, true);
    io.AddMousePosEvent(30,30);
    io.AddMouseButtonEvent(0, true);
    io.AddInputCharacter('x');
    io.DeltaTime = 1.f/60.f;
    ImGui::NewFrame();
    ImGui::Begin("Input scope guard");
    Require(ImGui::IsKeyPressed(ImGuiKey_A) && ImGui::IsMouseClicked(0) && io.InputQueueCharacters.Size > 0,
        "The native input scope check needs current keyboard, pointer and text events");
    const auto characters = io.InputQueueCharacters.Size;
    {
        Stack::Workspace::InputScope blocked(true,false);
        ImGui::BeginDisabled();
        Require(!ImGui::IsKeyPressed(ImGuiKey_A) && !ImGui::IsMouseClicked(0) && io.InputQueueCharacters.empty(),
            "A notification dialog must block underlying direct input polling and text entry");
        Require(!ImGui::Button("Underlying action"), "Blocked widgets must not activate");
        ImGui::EndDisabled();
    }
    Require(ImGui::IsKeyPressed(ImGuiKey_A) && ImGui::IsMouseClicked(0) && io.InputQueueCharacters.Size == characters,
        "Leaving the underlying scope must restore the same input for specialized dialog content");
    ImGui::End();
    ImGui::Render();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddMouseButtonEvent(0, false);
    auto store = std::make_shared<NotificationStore>();
    auto coast = store->ForOwner(11,"Coast");
    auto forest = store->ForOwner(22,"Forest");
    Presenter presenter;
    Stack::UiActivity::Presentation activity;
    Stack::UiActivity::Snapshot idle, running;
    running.SetOwner(11,"Coast");
    running.Add(true,"Exporting");
    PresentationContext context;
    context.currentOwner = 22;
    context.workspacePosition = ImVec2(0,50);
    context.workspaceSize = ImVec2(800,550);
    context.reducedMotion = true;
    bool showForm = false;
    bool closeForm = false;
    const auto frame = [&](const Stack::UiActivity::Snapshot& snapshot) {
        presenter.BeginFrame(*store);
        io.DeltaTime = 1.f/60.f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(520,0));
        ImGui::SetNextWindowSize(ImVec2(260,48));
        ImGui::Begin("Activity test",nullptr,ImGuiWindowFlags_NoDecoration);
        const float alpha = ImGui::GetStyle().Alpha;
        Stack::UiActivity::BeginDisabledForWork(true);
        Require(ImGui::GetStyle().Alpha == alpha,"Work guards must preserve UI appearance");
        ImGui::EndDisabled();
        presenter.RenderIndicator(*store,snapshot,activity,220,28,false,context);
        ImGui::End();
        if (showForm && ImGui::BeginPopupModal("Retained form", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted("Configuration");
            if (closeForm) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        presenter.RenderActivity(*store,context);
        presenter.RenderDialog(*store,context);
        ImGui::Render();
        Require(ImGui::GetDrawData()->TotalVtxCount > 0,"Native presenters must produce geometry");
    };
    const auto failure = forest.Error("An export could not be written");
    frame(idle);
    Require(!presenter.BlocksInput() && !store->Find(failure)->centerRequested,
        "Background failures must not open a center dialog");
    NoticeSpec decision;
    decision.title = "Replace this project?";
    decision.message = "The target already contains a project.";
    decision.foreground = true;
    ActionSpec cancel;
    cancel.label = "Keep both";
    cancel.safeCancel = true;
    cancel.invoke = [] { return ActionResult::Success(); };
    decision.actions.push_back(cancel);
    ActionSpec replace;
    replace.label = "Replace";
    replace.destructive = true;
    replace.invoke = [] { return ActionResult::Failure("Could not replace the target"); };
    decision.actions.push_back(replace);
    const auto first = coast.RequestDecision(decision);
    decision.dedupeKey = "second";
    const auto second = forest.RequestDecision(decision);
    ImGui::NewFrame();
    ImGui::OpenPopup("Retained form");
    if (ImGui::BeginPopupModal("Retained form", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("Configuration");
        ImGui::EndPopup();
    }
    ImGui::Render();
    showForm = true;
    frame(idle);
    Require(!presenter.BlocksInput() && store->Find(first)->centerRequested,
        "Notification decisions must wait while a retained configuration form is open");
    closeForm = true;
    frame(idle);
    showForm = false;
    for (int theme=0; theme<2; ++theme) {
        if (theme == 0) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();
        io.FontGlobalScale = theme == 0 ? 1.f : 1.4f;
        for (int i=0;i<3;++i) frame(running);
        Require(presenter.BlocksInput(),"A center decision must block application input");
        const auto* dialog = ImGui::FindWindowByName("##StackNotificationDialog");
        Require(dialog && dialog->Active && (dialog->Flags & ImGuiWindowFlags_NoMove) &&
            (dialog->Flags & ImGuiWindowFlags_NoResize) && (dialog->Flags & ImGuiWindowFlags_NoTitleBar),
            "The single shared dialog must have no movement, sizing or titlebar controls");
        Require(std::abs(dialog->Pos.x+dialog->Size.x*.5f-400.f)<2.f &&
            std::abs(dialog->Pos.y+dialog->Size.y*.5f-325.f)<2.f,"Dialogs must remain centered at either text scale");
        Require(dialog->Size.x<=context.workspaceSize.x && dialog->Size.y<=context.workspaceSize.y,
            "Scaled dialogs must remain bounded by the workspace");
        io.AddMousePosEvent(5,580);
        io.AddMouseButtonEvent(0,true);
        frame(idle);
        io.AddMouseButtonEvent(0,false);
        frame(idle);
        Require(store->Find(first)->centerRequested && store->Find(second)->centerRequested,
            "Outside clicks must not dismiss queued decisions");
    }
    io.AddKeyEvent(ImGuiKey_Escape,true);
    frame(idle);
    io.AddKeyEvent(ImGuiKey_Escape,false);
    frame(idle);
    Require(store->Find(first)->state==RecordState::Cancelled && store->Find(second)->centerRequested,
        "Escape must invoke the named safe action on only the displayed decision");
    store->Resolve(second);
    frame(idle);
    Require(!presenter.BlocksInput(),"Resolved decisions must release application input");
    decision.dedupeKey = "external-resolution-first";
    const auto externallyResolved = coast.RequestDecision(decision);
    decision.dedupeKey = "external-resolution-next";
    const auto queuedAfterResolution = forest.RequestDecision(decision);
    frame(idle);
    frame(idle);
    store->Resolve(externallyResolved);
    frame(idle);
    const auto* nextDialog = ImGui::FindWindowByName("##StackNotificationDialog");
    Require(nextDialog && nextDialog->Active && presenter.BlocksInput() &&
        store->Find(queuedAfterResolution)->centerRequested,
        "An externally resolved dialog must release its popup and show the next queued decision");
    store->Resolve(queuedAfterResolution);
    frame(idle);
    int destructiveInvocations = 0;
    NoticeSpec destructiveDecision;
    destructiveDecision.title = "Remove this result?";
    destructiveDecision.message = "Removing the result requires choosing the action.";
    destructiveDecision.foreground = true;
    ActionSpec destructiveOnly;
    destructiveOnly.label = "Remove result";
    destructiveOnly.destructive = true;
    destructiveOnly.defaultAction = true;
    destructiveOnly.invoke = [&] { ++destructiveInvocations; return ActionResult::Success(); };
    destructiveDecision.actions.push_back(std::move(destructiveOnly));
    const auto destructiveEvent = coast.RequestDecision(std::move(destructiveDecision));
    io.AddKeyEvent(ImGuiKey_Enter,true);
    frame(idle);
    io.AddKeyEvent(ImGuiKey_Enter,false);
    for (int i=0; i<3; ++i) frame(idle);
    io.AddKeyEvent(ImGuiKey_Enter,true);
    frame(idle);
    io.AddKeyEvent(ImGuiKey_Enter,false);
    frame(idle);
    Require(destructiveInvocations == 0 && store->Find(destructiveEvent)->centerRequested && presenter.BlocksInput(),
        "A destructive-only dialog must not assign initial keyboard focus or activate from unchosen Enter input");
    store->Resolve(destructiveEvent);
    frame(idle);
    presenter.TogglePanel();
    frame(idle);
    Require(presenter.PanelOpen(),"The spinner must open the persistent activity panel");
    frame(running);
    Require(presenter.PanelOpen(),"Background updates must not close the persistent panel");
    ImGui::DestroyContext();
}

void CheckWorkerIdentity() {
    auto& tasks = Async::TaskSystem::Get();
    Async::TaskGroup coast, forest;
    coast.SetActivityOwner(11,3);
    forest.SetActivityOwner(22,5);
    std::promise<void> startedA, startedB, releaseA, releaseB, finishedA;
    auto gateA = releaseA.get_future().share();
    auto gateB = releaseB.get_future().share();
    Async::ActivityMetadata workerA, mainA;
    Require(coast.Submit("Preparing image",[&] {
        workerA = Async::TaskSystem::CurrentActivity();
        startedA.set_value();
        gateA.wait_for(std::chrono::seconds(5));
        coast.PostToMain([&] { mainA=Async::TaskSystem::CurrentActivity(); finishedA.set_value(); });
    }),"Queue Coast work");
    Require(forest.Submit("Preparing image",[&] {
        startedB.set_value();
        gateB.wait_for(std::chrono::seconds(5));
    }),"Queue Forest work");
    Require(startedA.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready &&
        startedB.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready,"Both workers must start");
    const auto jobs = tasks.Activities();
    Require(jobs.size()==2 && jobs[0].id!=jobs[1].id && jobs[0].operationId!=jobs[1].operationId,
        "Same-label workers must retain distinct runtime and operation identity");
    releaseA.set_value();
    auto done = finishedA.get_future();
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (done.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready && std::chrono::steady_clock::now()<deadline) {
        Require(tasks.HasPendingWork(),"Worker-to-main handoff must stay busy");
        tasks.PumpMainThreadTasks();
        std::this_thread::yield();
    }
    Require(done.wait_for(std::chrono::milliseconds(0))==std::future_status::ready && workerA.id==mainA.id &&
        workerA.operationId==mainA.operationId && mainA.ownerId==11 && mainA.ownerGeneration==3,
        "Main completion must retain its original owner generation and operation identity");
    const auto remaining = tasks.Activities();
    Require(std::any_of(remaining.begin(),remaining.end(),[](const auto& job) { return job.ownerId==22; }),
        "Completing Coast must leave Forest's identically named job visible");
    releaseB.set_value();
    tasks.Shutdown();
    Require(!tasks.HasPendingWork() && tasks.Activities().empty(),"Drained work must release its execution tickets");
}

void CheckSaveTarget() {
    const auto path = std::filesystem::temp_directory_path() /
        ("stack-notification-target-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tmp");
    std::error_code error;
    const auto absent = Stack::FileSave::CaptureTargetApproval(path,error);
    Require(absent && !absent->existed && Stack::FileSave::TargetApprovalStillMatches(*absent,error),
        "An absent target must remain approved while absent");
    { std::ofstream file(path); file<<"saved"; }
    Require(!Stack::FileSave::TargetApprovalStillMatches(*absent,error),"A target appearing after selection needs new approval");
    const auto existing = Stack::FileSave::CaptureTargetApproval(path,error);
    Require(existing && existing->existed && Stack::FileSave::TargetApprovalStillMatches(*existing,error),
        "An unchanged existing target must preserve replacement approval");
    { std::ofstream file(path); file<<"changed externally"; }
    Require(!Stack::FileSave::TargetApprovalStillMatches(*existing,error),"A changed target must invalidate replacement approval");
    Require(!Stack::FileSave::CaptureTargetApproval({},error) && error,
        "An empty destination must never receive approval");
    Require(!Stack::FileSave::CaptureTargetApproval(std::filesystem::temp_directory_path(),error) && error,
        "A directory must never receive file replacement approval");
    std::filesystem::remove(path);
}
} // namespace

int main() {
    try {
        CheckActivity();
        CheckRecords();
        CheckNativePresenters();
        CheckWorkerIdentity();
        CheckSaveTarget();
        std::cout<<"Notification ownership, actions, retention, native presentation and worker handoff passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
