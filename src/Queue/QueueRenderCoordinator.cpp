#include "Queue/QueueRenderCoordinator.h"

#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Raw/RawLoader.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <system_error>
#include <utility>

namespace Stack::Queue {
namespace {

Async::ActivityMetadata ExecutionActivity(const Notifications::Notifier& notifier,
    Notifications::ActivityHandle activity, std::string label) {
    Async::ActivityMetadata metadata;
    metadata.ownerId = notifier.GetOwner().id;
    metadata.ownerGeneration = notifier.GetOwner().generation;
    metadata.operationId = activity.operationId;
    metadata.label = std::move(label);
    return metadata;
}

std::string SourceLoadPath(const std::filesystem::path& path) {
    const std::string utf8 = path.u8string();
    // RAW decoding accepts UTF-8. The current raster loader uses narrow ANSI
    // filenames, so preserve its existing path conversion until it is updated.
    return Raw::RawLoader::IsRawPath(utf8) ? utf8 : path.string();
}

std::string SanitizeStem(std::string value) {
    for (char& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 32u || character == '<' || character == '>' ||
            character == ':' || character == '"' || character == '/' ||
            character == '\\' || character == '|' || character == '?' ||
            character == '*') {
            character = '_';
        }
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '.')) {
        value.pop_back();
    }
    if (value.empty()) return "Stack Export";
    std::string base = value.substr(0, value.find('.'));
    std::transform(base.begin(), base.end(), base.begin(),
        [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    const bool devicePrefix = base.compare(0, 3, "COM") == 0 ||
        base.compare(0, 3, "LPT") == 0;
    const bool deviceNumber = (base.size() == 4u && base[3] >= '1' && base[3] <= '9') ||
        (base.size() == 5u && static_cast<unsigned char>(base[3]) == 0xC2u &&
            (static_cast<unsigned char>(base[4]) == 0xB9u ||
             static_cast<unsigned char>(base[4]) == 0xB2u ||
             static_cast<unsigned char>(base[4]) == 0xB3u));
    const bool reserved = base == "CON" || base == "PRN" || base == "AUX" ||
        base == "NUL" || (devicePrefix && deviceNumber);
    if (reserved) value.insert(value.begin(), '_');
    return value;
}

} // namespace

QueueRenderCoordinator::QueueRenderCoordinator() = default;

QueueRenderCoordinator::~QueueRenderCoordinator() {
    Shutdown();
}

void QueueRenderCoordinator::SetNotificationScope(Notifications::Notifier notifier) {
    m_Notifier = std::move(notifier);
    if (m_RenderEditor) m_RenderEditor->SetNotificationScope(m_Notifier);
}

void QueueRenderCoordinator::Configure(
    GLFWwindow* sharedWindow,
    StackAppearance::AppearanceManager* appearance,
    RenderQueueModel* model) {
    m_SharedWindow = sharedWindow;
    m_Appearance = appearance;
    m_Model = model;
}

bool QueueRenderCoordinator::EnsureRenderEditor(std::string* errorMessage) {
    if (m_Initialized && m_RenderEditor) return true;
    if (!m_SharedWindow || !m_Model) {
        if (errorMessage) *errorMessage =
            "The Queue renderer has not been attached to the application.";
        return false;
    }
    try {
        m_RenderEditor = std::make_unique<EditorModule>();
        m_RenderEditor->SetNotificationScope(m_Notifier);
        m_RenderEditor->Initialize(m_SharedWindow, m_Appearance, false);
        m_RenderEditor->SetDocumentPersistenceEnabled(false);
        m_Initialized = true;
        return true;
    } catch (...) {
        m_RenderEditor.reset();
        if (errorMessage) *errorMessage =
            "The isolated Queue render session could not be initialized.";
        return false;
    }
}

bool QueueRenderCoordinator::Start(
    const std::filesystem::path& destination,
    const std::vector<Item>& items,
    std::string* errorMessage) {
    if (m_WorkerShutdownRequested) {
        if (errorMessage) *errorMessage = "The Queue is shutting down.";
        return false;
    }
    if (destination.empty() || items.empty()) {
        if (errorMessage) *errorMessage =
            "Choose an export folder and at least one queued item.";
        return false;
    }
    std::string initializeError;
    if (!EnsureRenderEditor(&initializeError)) {
        if (errorMessage) *errorMessage = initializeError;
        return false;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(destination, directoryError);
    if (directoryError || !std::filesystem::is_directory(destination, directoryError)) {
        if (errorMessage) *errorMessage =
            "The Queue export folder could not be created or opened.";
        return false;
    }

    const auto resolvedDestination = std::filesystem::absolute(destination, directoryError);
    if (directoryError) {
        if (errorMessage) *errorMessage =
            "The Queue export folder could not be resolved.";
        return false;
    }
    const auto normalizedDestination = resolvedDestination.lexically_normal();
    if (IsBusy()) {
        if (m_StopAfterCurrent) {
            if (errorMessage) *errorMessage =
                "The Queue is stopping after the current image. Wait for it to finish before restarting.";
            return false;
        }
        const bool sameDestination = std::filesystem::equivalent(
            normalizedDestination, m_Destination, directoryError);
        if (directoryError || !sameDestination) {
            if (errorMessage) *errorMessage =
                "The active Queue export uses another folder. Wait for it to finish before choosing a different export folder.";
            return false;
        }
        auto claimed = m_Model->ClaimForExport(items);
        const auto added = claimed.size();
        if (added == 0) {
            if (errorMessage) *errorMessage =
                "The selected items are already scheduled or are no longer waiting for export.";
            return false;
        }
        for (Item& item : claimed) m_Pending.push_back(std::move(item));
        m_StatusText = "Added work to the active Queue export.";
        m_ExportTotal += added;
        m_Notifier.UpdateActivity(m_ExportActivity, "Exporting",
            Notifications::Progress{static_cast<double>(m_ExportSucceeded + m_ExportFailed), static_cast<double>(m_ExportTotal), "images"});
        return true;
    }
    auto claimed = m_Model->ClaimForExport(items);
    if (claimed.empty()) {
        if (errorMessage) *errorMessage =
            "The selected items are no longer waiting for export.";
        return false;
    }
    CancelActiveInspection(true);
    m_Destination = normalizedDestination;
    m_Pending.assign(std::make_move_iterator(claimed.begin()), std::make_move_iterator(claimed.end()));
    m_Current = {};
    m_CurrentOutputPath.clear();
    m_StopAfterCurrent = false;
    m_ExportTotal = m_Pending.size();
    m_ExportSucceeded = 0;
    m_ExportFailed = 0;
    Notifications::NoticeSpec activity;
    activity.title = "Exporting Queue";
    activity.context = m_Destination.filename().u8string();
    activity.details = m_Destination.u8string();
    activity.progress = Notifications::Progress{0.0, static_cast<double>(m_ExportTotal), "images"};
    m_ExportActivity = m_Notifier.BeginActivity(std::move(activity));
    SetPhase(Phase::PrepareItem, "Preparing Queue export...");
    return true;
}

void QueueRenderCoordinator::StopAfterCurrent() {
    if (!IsBusy() || m_StopAfterCurrent) return;
    std::vector<std::uint64_t> ids;
    ids.reserve(m_Pending.size());
    for (const Item& item : m_Pending) ids.push_back(item.id);
    if (m_Model) m_Model->ReleaseQueued(ids);
    m_Pending.clear();
    m_StopAfterCurrent = true;
    m_StatusText = "Stopping after the current image. Remaining jobs are waiting.";
    m_Notifier.UpdateActivity(m_ExportActivity, "Stopping after current image");
}

bool QueueRenderCoordinator::RequestInspection(
    Stack::RawGalleryInspection::Request request,
    InspectionCompletion completion,
    std::string* errorMessage,
    Notifications::Notifier notifier) {
    const bool applicationCancel = request.cancel && notifier.GetOwner().id == 0;
    if (notifier.GetOwner().id == 0) notifier = m_Notifier;
    if (request.cancel) {
        const auto& target = notifier.GetOwner();
        const auto& current = m_InspectionNotifier.GetOwner();
        if (applicationCancel || (target.id == current.id && target.generation == current.generation)) CancelActiveInspection(false);
        return true;
    }
    if (m_WorkerShutdownRequested) {
        if (errorMessage) *errorMessage = "The Queue is shutting down.";
        return false;
    }
    if (request.requestId == 0 ||
        (request.projectPath.empty() && request.sourcePath.empty())) {
        if (errorMessage) *errorMessage =
            "The Gallery inspection request has no image or project.";
        return false;
    }
    std::string initializeError;
    if (!EnsureRenderEditor(&initializeError)) {
        if (errorMessage) *errorMessage = initializeError;
        return false;
    }

    CancelActiveInspection(false);
    m_InspectionNotifier = std::move(notifier);
    Notifications::NoticeSpec activity;
    activity.title = "Preparing image preview";
    activity.context = request.displayName;
    activity.preview = false;
    m_InspectionActivity = m_InspectionNotifier.BeginActivity(std::move(activity));
    m_PendingInspection = std::move(request);
    m_InspectionCompletion = std::move(completion);
    m_CurrentInspection = {};
    m_InspectionMultiFrameProcessingStarted = false;
    m_InspectionPhase = InspectionPhase::Prepare;
    m_InspectionPhaseStartedAt = std::chrono::steady_clock::now();
    return true;
}

bool QueueRenderCoordinator::IsBusy() const {
    return m_Phase != Phase::Idle;
}

void QueueRenderCoordinator::SetPhase(Phase phase, std::string status) {
    if (phase != Phase::LoadProject) m_ProjectLoadRequest.Cancel();
    m_Phase = phase;
    m_PhaseStartedAt = std::chrono::steady_clock::now();
    m_StatusText = std::move(status);
    if (phase != Phase::Idle && phase != Phase::PrepareItem) {
        m_Notifier.UpdateActivity(m_ItemActivity, m_StatusText);
    }
}

bool QueueRenderCoordinator::PhaseTimedOut(
    std::chrono::minutes timeout) const {
    return m_PhaseStartedAt.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - m_PhaseStartedAt > timeout;
}

void QueueRenderCoordinator::CancelActiveInspection(
    bool preserveLatestRequest) {
    if (preserveLatestRequest && !m_PendingInspection &&
        m_CurrentInspection.requestId != 0) {
        m_PendingInspection = m_CurrentInspection;
    }
    m_InspectionLoadRequest.Cancel();
    if (!preserveLatestRequest) {
        m_InspectionNotifier.CancelActivity(m_InspectionActivity, "Image preview cancelled.");
        m_InspectionNotifier.InvalidateOperation(m_InspectionActivity.operationId);
        m_InspectionActivity = {};
        m_PendingInspection.reset();
        m_InspectionCompletion = {};
    } else if (m_PendingInspection) {
        m_InspectionNotifier.UpdateActivity(m_InspectionActivity, "Waiting for Queue export");
    }
    m_CurrentInspection = {};
    m_InspectionPhase = InspectionPhase::Idle;
    m_InspectionMultiFrameProcessingStarted = false;
}

void QueueRenderCoordinator::BeginInspection() {
    if (!m_PendingInspection) {
        m_InspectionPhase = InspectionPhase::Idle;
        return;
    }
    // Keep the old scope until its worker and main-thread tickets drain.
    // Rebinding this shared editor earlier could redirect an old completion.
    if (m_RenderEditor && !m_RenderEditor->ProjectTasks().IsIdle()) return;
    m_CurrentInspection = std::move(*m_PendingInspection);
    m_PendingInspection.reset();
    m_InspectionMultiFrameProcessingStarted = false;
    m_InspectionNotifier.UpdateActivity(m_InspectionActivity, "Preparing image preview");
    if (m_RenderEditor) {
        m_RenderEditor->CloseCurrentProject(true);
        m_RenderEditor->SetNotificationScope(m_InspectionNotifier);
        m_RenderEditor->ProjectTasks().SetActivityOperation(m_InspectionActivity.operationId);
    }

    const bool renderProject =
        m_CurrentInspection.version ==
            Stack::RawGalleryInspection::Version::After &&
        !m_CurrentInspection.projectPath.empty();
    if (renderProject) {
        BeginInspectionProjectLoad();
        return;
    }
    if (m_CurrentInspection.sourcePath.empty()) {
        FailInspection(
            "The original image is unavailable for Before inspection.");
        return;
    }
    m_InspectionPhase = InspectionPhase::LoadSource;
    m_InspectionPhaseStartedAt = std::chrono::steady_clock::now();
    m_InspectionNotifier.UpdateActivity(m_InspectionActivity, "Loading original image");
    m_RenderEditor->RequestLoadSourceImage(
        SourceLoadPath(m_CurrentInspection.sourcePath));
}

void QueueRenderCoordinator::BeginInspectionProjectLoad() {
    m_InspectionPhase = InspectionPhase::LoadProject;
    m_InspectionPhaseStartedAt = std::chrono::steady_clock::now();
    m_InspectionNotifier.UpdateActivity(m_InspectionActivity, "Opening saved edit");
    const auto ticket = m_InspectionLoadRequest.Begin();
    const std::filesystem::path path = m_CurrentInspection.projectPath;
    const bool submitted = Async::TaskSystem::Get().SubmitHighPriority(
        ExecutionActivity(m_InspectionNotifier, m_InspectionActivity, "Loading inspection"),
        [this, ticket, path]() mutable {
            if (ticket.expired()) return;
            Stack::Project::ProjectOpenResult opened =
                Stack::Project::ProjectOpenCoordinator::Load(path);
            Async::TaskSystem::Get().PostToMain(
                [this, ticket, opened = std::move(opened)]() mutable {
                    if (ticket.expired() ||
                        m_Phase != Phase::Idle ||
                        m_InspectionPhase != InspectionPhase::LoadProject) {
                        return;
                    }
                    if (!opened || !opened.candidate) {
                        FailInspection(opened.error.empty()
                            ? "The saved edit could not be opened for inspection."
                            : opened.error);
                        return;
                    }
                    if (!m_RenderEditor->BeginDeferredLoadedProjectApply(
                            std::move(opened.candidate))) {
                        FailInspection(
                            "The saved edit could not be applied to the inspection session.");
                        return;
                    }
                    m_InspectionLoadRequest.Cancel();
                    m_InspectionPhase = InspectionPhase::ApplyProject;
                    m_InspectionNotifier.UpdateActivity(m_InspectionActivity, "Applying saved edit");
                    m_InspectionPhaseStartedAt =
                        std::chrono::steady_clock::now();
                });
        });
    if (!submitted) {
        FailInspection(
            "The Gallery inspection load task could not be started.");
    }
}

void QueueRenderCoordinator::CompleteInspection() {
    m_InspectionLoadRequest.Cancel();
    Stack::RawGalleryInspection::Result result;
    result.requestId = m_CurrentInspection.requestId;
    if (!m_RenderEditor->CaptureSettledFullQualityPreviewRaster(
            result.pixels, result.width, result.height, 2048)) {
        FailInspection(
            "The full-quality inspection image could not be captured.");
        return;
    }
    result.status =
        m_CurrentInspection.version ==
            Stack::RawGalleryInspection::Version::Before
        ? "Original"
        : (m_CurrentInspection.projectPath.empty()
            ? "Original"
            : "Latest saved edit");
    InspectionCompletion completion = std::exchange(m_InspectionCompletion, {});
    m_CurrentInspection = {};
    m_InspectionPhase = InspectionPhase::Idle;
    m_InspectionNotifier.CompleteActivity(m_InspectionActivity, "Image preview ready.", false);
    m_InspectionActivity = {};
    if (completion) completion(std::move(result));
}

void QueueRenderCoordinator::FailInspection(std::string message) {
    m_InspectionLoadRequest.Cancel();
    if (message.empty()) message = "The image preview failed.";
    m_InspectionNotifier.FailActivity(m_InspectionActivity, "Could not prepare the image preview.", message);
    m_InspectionActivity = {};
    Stack::RawGalleryInspection::Result result;
    result.requestId = m_CurrentInspection.requestId;
    result.error = message.empty()
        ? "Gallery inspection failed."
        : std::move(message);
    InspectionCompletion completion = std::exchange(m_InspectionCompletion, {});
    m_CurrentInspection = {};
    m_InspectionPhase = InspectionPhase::Idle;
    if (completion) completion(std::move(result));
}

void QueueRenderCoordinator::TickInspection() {
    if (m_Phase != Phase::Idle || !m_RenderEditor) return;
    const auto timedOut = [&](std::chrono::minutes timeout) {
        return m_InspectionPhaseStartedAt.time_since_epoch().count() != 0 &&
            std::chrono::steady_clock::now() -
                m_InspectionPhaseStartedAt > timeout;
    };

    switch (m_InspectionPhase) {
    case InspectionPhase::Prepare:
        BeginInspection();
        return;
    case InspectionPhase::LoadProject:
        if (timedOut(std::chrono::minutes(5))) {
            FailInspection("Project inspection loading timed out.");
        }
        return;
    case InspectionPhase::LoadSource:
        if (m_RenderEditor->GetSourceLoadTaskState() ==
            Async::TaskState::Failed) {
            FailInspection(m_RenderEditor->GetSourceLoadStatusText());
        } else if (!m_RenderEditor->IsSourceLoadBusy()) {
            BeginInspectionFullQualityRender();
        } else if (timedOut(std::chrono::minutes(5))) {
            FailInspection("Original image inspection loading timed out.");
        }
        return;
    case InspectionPhase::ApplyProject:
        if (m_RenderEditor->HasDeferredLoadedProjectApplyFailed()) {
            FailInspection(
                m_RenderEditor->GetDeferredLoadedProjectStatusText());
        } else if (m_RenderEditor->IsDeferredLoadedProjectReadyForReveal()) {
            if (m_RenderEditor->IsMultiFrameRawProjectActive()) {
                std::string error;
                if (!m_RenderEditor->StartActiveMultiFrameProcessingForQueue(
                        &error)) {
                    FailInspection(std::move(error));
                    return;
                }
                m_InspectionMultiFrameProcessingStarted = true;
                m_InspectionPhase = InspectionPhase::ProcessMultiFrame;
                m_InspectionNotifier.UpdateActivity(m_InspectionActivity, "Processing frames");
            } else {
                if (!BeginInspectionFullQualityRender()) return;
            }
            if (m_InspectionPhase == InspectionPhase::ProcessMultiFrame) {
                m_InspectionPhaseStartedAt = std::chrono::steady_clock::now();
            }
        } else if (timedOut(std::chrono::minutes(5))) {
            FailInspection("Applying the saved edit for inspection timed out.");
        }
        return;
    case InspectionPhase::ProcessMultiFrame: {
        std::string error;
        if (m_RenderEditor->DidActiveMultiFrameProcessingForQueueFail(&error)) {
            FailInspection(std::move(error));
        } else if (m_InspectionMultiFrameProcessingStarted &&
            !m_RenderEditor->IsActiveMultiFrameProcessingForQueueBusy()) {
            BeginInspectionFullQualityRender();
        } else if (timedOut(std::chrono::minutes(30))) {
            FailInspection("Bracket inspection processing timed out.");
        }
        return;
    }
    case InspectionPhase::WaitForRender:
        if (m_RenderEditor->IsRenderSettledForFullQualityExport()) {
            CompleteInspection();
        } else if (timedOut(std::chrono::minutes(15))) {
            FailInspection(
                "Full-quality Gallery inspection timed out: " +
                m_RenderEditor->GetFullQualityRenderDiagnostic());
        }
        return;
    case InspectionPhase::Idle:
    default:
        if (m_PendingInspection) {
            m_InspectionPhase = InspectionPhase::Prepare;
            m_InspectionPhaseStartedAt = std::chrono::steady_clock::now();
        }
        return;
    }
}

void QueueRenderCoordinator::BeginNextItem() {
    if (m_Pending.empty()) {
        m_Current = {};
        m_CurrentOutputPath.clear();
        if (m_StopAfterCurrent) {
            SetPhase(Phase::Idle, "Queue stopped. Remaining jobs are waiting.");
            m_Notifier.CancelActivity(m_ExportActivity,
                "Queue stopped after the current image. Remaining jobs are waiting.");
            m_StopAfterCurrent = false;
        } else if (m_ExportFailed == 0) {
            SetPhase(Phase::Idle, "Queue export complete.");
            m_Notifier.CompleteActivity(m_ExportActivity,
                "Exported " + std::to_string(m_ExportSucceeded) + (m_ExportSucceeded == 1 ? " image." : " images."));
        } else {
            const std::string result = "Exported " + std::to_string(m_ExportSucceeded) + " of " + std::to_string(m_ExportTotal) + " images.";
            SetPhase(Phase::Idle, result);
            m_Notifier.FinishActivity(m_ExportActivity, m_ExportSucceeded ? Notifications::Outcome::Partial : Notifications::Outcome::Failure,
                result, std::to_string(m_ExportFailed) + " failed. See the Queue rows for details.");
        }
        m_ExportActivity = {};
        return;
    }
    if (m_RenderEditor && !m_RenderEditor->ProjectTasks().IsIdle()) return;
    if (m_RenderEditor) {
        m_RenderEditor->CloseCurrentProject(true);
        m_RenderEditor->SetNotificationScope(m_Notifier);
    }
    m_Current = std::move(m_Pending.front());
    m_Pending.pop_front();
    m_CurrentOutputPath.clear();
    m_MultiFrameProcessingStarted = false;
    Notifications::NoticeSpec itemActivity;
    itemActivity.title = "Exporting image";
    itemActivity.context = m_Current.displayName;
    itemActivity.operationId = m_ExportActivity.operationId;
    itemActivity.dedupeKey = "queue-item-" + std::to_string(m_Current.id);
    itemActivity.preview = false;
    m_ItemActivity = m_Notifier.BeginActivity(std::move(itemActivity));
    if (m_RenderEditor) m_RenderEditor->ProjectTasks().SetActivityOperation(m_ItemActivity.operationId);
    if (m_Model) {
        m_Model->UpdateState(
            m_Current.id, ItemState::Preparing, 0.05f, "Preparing");
    }
    if (m_Current.kind == ItemKind::ProjectSnapshot) BeginProjectSnapshot();
    else if (m_Current.kind == ItemKind::Project) BeginProjectLoad();
    else BeginSourceLoad();
}

void QueueRenderCoordinator::BeginProjectSnapshot() {
    if (!m_Current.projectSnapshot) {
        FailCurrent("The queued project capture is unavailable.");
        return;
    }
    // Applying/processing owns mutable document state. The queued revision is
    // immutable and remains reusable after failure without sharing that state.
    auto candidate = std::make_shared<Stack::Project::LoadedProjectData>(
        *m_Current.projectSnapshot);
    if (candidate->rawProjectSnapshot)
        candidate->rawProjectSnapshot = std::make_shared<Stack::Project::RawProjectSnapshot>(
            *candidate->rawProjectSnapshot);
    if (!m_RenderEditor->BeginDeferredLoadedProjectApply(std::move(candidate))) {
        FailCurrent("The queued project capture could not be applied to the isolated render session.");
        return;
    }
    SetPhase(Phase::ApplyProject, "Applying captured " + m_Current.displayName + "...");
    if (m_Model)
        m_Model->UpdateState(m_Current.id, ItemState::Preparing, 0.22f,
            "Applying captured project");
}

void QueueRenderCoordinator::BeginProjectLoad() {
    SetPhase(Phase::LoadProject,
        "Opening " + m_Current.displayName + " in the Queue session...");
    if (m_Model) {
        m_Model->UpdateState(
            m_Current.id, ItemState::Preparing, 0.12f, "Opening project");
    }
    const auto ticket = m_ProjectLoadRequest.Begin();
    const std::filesystem::path path = m_Current.path;
    const bool submitted = Async::TaskSystem::Get().SubmitHighPriority(
        ExecutionActivity(m_Notifier, m_ItemActivity, "Loading queued project"),
        [this, ticket, path]() mutable {
            if (ticket.expired()) return;
            Stack::Project::ProjectOpenResult opened =
                Stack::Project::ProjectOpenCoordinator::Load(path);
            Async::TaskSystem::Get().PostToMain(
                [this, ticket, opened = std::move(opened)]() mutable {
                    if (ticket.expired() ||
                        m_Phase != Phase::LoadProject) {
                        return;
                    }
                    if (!opened || !opened.candidate) {
                        FailCurrent(opened.error.empty()
                            ? "The queued project could not be opened."
                            : opened.error);
                        return;
                    }
                    if (!m_RenderEditor->BeginDeferredLoadedProjectApply(
                            std::move(opened.candidate))) {
                        FailCurrent(
                            "The queued project could not be applied to the isolated render session.");
                        return;
                    }
                    SetPhase(
                        Phase::ApplyProject,
                        "Applying " + m_Current.displayName + "...");
                    if (m_Model) {
                        m_Model->UpdateState(
                            m_Current.id, ItemState::Preparing, 0.22f,
                            "Applying project");
                    }
                });
        });
    if (!submitted) {
        FailCurrent("The queued project open task could not be started.");
    }
}

void QueueRenderCoordinator::BeginSourceLoad() {
    SetPhase(Phase::LoadSource,
        "Loading " + m_Current.displayName + "...");
    if (m_Model) {
        m_Model->UpdateState(
            m_Current.id, ItemState::Preparing, 0.12f, "Loading image");
    }
    m_RenderEditor->RequestLoadSourceImage(SourceLoadPath(m_Current.path));
}

bool QueueRenderCoordinator::BeginFullQualityRender(
    std::string status,
    float progress) {
    std::string error;
    if (!m_RenderEditor->RequestFullQualityRender(&error)) {
        FailCurrent(error.empty()
            ? "The full-quality render could not be requested."
            : std::move(error));
        return false;
    }
    m_FullQualityProgressBase = std::clamp(progress, 0.0f, 0.8f);
    SetPhase(Phase::WaitForRender, std::move(status));
    if (m_Model) {
        m_Model->UpdateState(
            m_Current.id,
            ItemState::Rendering,
            m_FullQualityProgressBase,
            "Preparing full-quality render");
    }
    return true;
}

bool QueueRenderCoordinator::BeginInspectionFullQualityRender() {
    std::string error;
    if (!m_RenderEditor->RequestFullQualityRender(&error)) {
        FailInspection(error.empty()
            ? "The full-quality inspection render could not be requested."
            : std::move(error));
        return false;
    }
    m_InspectionPhase = InspectionPhase::WaitForRender;
    m_InspectionPhaseStartedAt = std::chrono::steady_clock::now();
    m_InspectionNotifier.UpdateActivity(m_InspectionActivity, "Rendering full quality");
    return true;
}

std::filesystem::path QueueRenderCoordinator::BuildOutputPath(
    const Item& item) const {
    const std::string stem = SanitizeStem(item.displayName);
    std::filesystem::path candidate = m_Destination / std::filesystem::u8path(stem + ".png");
    std::error_code error;
    bool exists = std::filesystem::exists(candidate, error);
    if (error) return {};
    if (!exists) return candidate;
    for (int version = 2; version < 10000; ++version) {
        candidate = m_Destination /
            std::filesystem::u8path(stem + " " + std::to_string(version) + ".png");
        error.clear();
        exists = std::filesystem::exists(candidate, error);
        if (error) return {};
        if (!exists) return candidate;
    }
    return {};
}

void QueueRenderCoordinator::BeginOutputWrite() {
    const std::filesystem::path output = BuildOutputPath(m_Current);
    if (output.empty()) {
        FailCurrent("A new output filename could not be chosen in the export folder. Existing files were preserved.");
        return;
    }
    if (!m_RenderEditor->RequestQueueExportImage(output.u8string())) {
        FailCurrent(m_RenderEditor->GetExportStatusText().empty()
            ? "The full-quality Queue export could not be captured."
            : m_RenderEditor->GetExportStatusText());
        return;
    }
    m_CurrentOutputPath = output;
    SetPhase(Phase::WriteOutput,
        "Writing " + output.filename().u8string() + "...");
    if (m_Model) {
        m_Model->UpdateState(
            m_Current.id, ItemState::Writing, 0.82f, "Writing PNG");
    }
}

void QueueRenderCoordinator::CompleteCurrent() {
    if (m_Model) {
        m_Model->SetOutputPath(m_Current.id, m_CurrentOutputPath);
        m_Model->UpdateState(
            m_Current.id, ItemState::Complete, 1.0f, "Complete");
    }
    ++m_ExportSucceeded;
    m_Notifier.CompleteActivity(m_ItemActivity, "Image exported.", false);
    m_ItemActivity = {};
    m_Notifier.UpdateActivity(m_ExportActivity, "Exporting",
        Notifications::Progress{static_cast<double>(m_ExportSucceeded + m_ExportFailed), static_cast<double>(m_ExportTotal), "images"});
    SetPhase(Phase::PrepareItem,
        "Finished " + m_Current.displayName + ".");
}

void QueueRenderCoordinator::FailCurrent(std::string message) {
    if (message.empty()) message = "The queued render failed.";
    if (m_Model) {
        m_Model->UpdateState(
            m_Current.id, ItemState::Failed, 0.0f, message);
    }
    ++m_ExportFailed;
    m_Notifier.FinishActivity(m_ItemActivity, Notifications::Outcome::Failure,
        "Could not export this image.", message, false);
    m_ItemActivity = {};
    m_Notifier.UpdateActivity(m_ExportActivity, "Exporting",
        Notifications::Progress{static_cast<double>(m_ExportSucceeded + m_ExportFailed), static_cast<double>(m_ExportTotal), "images"});
    SetPhase(Phase::PrepareItem,
        "Could not export " + m_Current.displayName + ": " + message);
}

void QueueRenderCoordinator::Tick() {
    if (!m_RenderEditor || m_WorkerShutdownRequested) return;
    if (m_Phase == Phase::Idle && !IsInspectionBusy() &&
        m_RenderEditor->ProjectTasks().IsIdle() && !m_RenderEditor->IsEditorRenderBusy()) return;
    m_RenderEditor->PumpNonRenderingWork(2.0, false);
    if (m_Phase == Phase::Idle) {
        TickInspection();
        return;
    }

    switch (m_Phase) {
    case Phase::PrepareItem:
        BeginNextItem();
        return;
    case Phase::LoadProject:
        if (PhaseTimedOut(std::chrono::minutes(5))) {
            FailCurrent("Project loading timed out.");
        }
        return;
    case Phase::LoadSource:
        if (m_RenderEditor->GetSourceLoadTaskState() ==
            Async::TaskState::Failed) {
            FailCurrent(m_RenderEditor->GetSourceLoadStatusText());
            return;
        }
        if (!m_RenderEditor->IsSourceLoadBusy()) {
            BeginFullQualityRender(
                "Rendering " + m_Current.displayName + "...",
                0.42f);
        } else if (PhaseTimedOut(std::chrono::minutes(5))) {
            FailCurrent("Image loading timed out.");
        }
        return;
    case Phase::ApplyProject:
        if (m_RenderEditor->HasDeferredLoadedProjectApplyFailed()) {
            FailCurrent(m_RenderEditor->GetDeferredLoadedProjectStatusText());
            return;
        }
        if (m_RenderEditor->IsDeferredLoadedProjectReadyForReveal()) {
            if (m_RenderEditor->IsMultiFrameRawProjectActive()) {
                std::string processingError;
                if (!m_RenderEditor->StartActiveMultiFrameProcessingForQueue(
                        &processingError)) {
                    FailCurrent(processingError);
                    return;
                }
                m_MultiFrameProcessingStarted = true;
                SetPhase(Phase::ProcessMultiFrame,
                    "Processing Bracket pipeline for " +
                        m_Current.displayName + "...");
                if (m_Model) {
                    m_Model->UpdateState(
                        m_Current.id, ItemState::Rendering, 0.35f,
                        "Processing Bracket pipeline");
                }
            } else {
                BeginFullQualityRender(
                    "Rendering " + m_Current.displayName + "...",
                    0.48f);
            }
        } else if (PhaseTimedOut(std::chrono::minutes(5))) {
            FailCurrent("Applying the project timed out.");
        }
        return;
    case Phase::ProcessMultiFrame: {
        std::string processingError;
        if (m_RenderEditor->DidActiveMultiFrameProcessingForQueueFail(
                &processingError)) {
            FailCurrent(processingError);
            return;
        }
        if (m_MultiFrameProcessingStarted &&
            !m_RenderEditor->IsActiveMultiFrameProcessingForQueueBusy()) {
            BeginFullQualityRender(
                "Rendering developed Bracket result...",
                0.62f);
        } else if (PhaseTimedOut(std::chrono::minutes(30))) {
            FailCurrent("Bracket processing timed out.");
        }
        return;
    }
    case Phase::WaitForRender:
        if (m_RenderEditor->IsRenderSettledForFullQualityExport()) {
            BeginOutputWrite();
        } else if (PhaseTimedOut(std::chrono::minutes(15))) {
            FailCurrent(
                "Full-quality rendering timed out: " +
                m_RenderEditor->GetFullQualityRenderDiagnostic());
        } else if (m_Model) {
            std::string renderStatus;
            const float renderProgress =
                m_RenderEditor->GetFullQualityRenderProgress(&renderStatus);
            const float itemProgress = m_FullQualityProgressBase +
                (0.80f - m_FullQualityProgressBase) *
                    std::clamp(renderProgress, 0.0f, 1.0f);
            m_Model->UpdateState(
                m_Current.id,
                ItemState::Rendering,
                itemProgress,
                std::move(renderStatus));
        }
        return;
    case Phase::WriteOutput:
        if (m_RenderEditor->GetExportTaskState() ==
            Async::TaskState::Failed) {
            FailCurrent(m_RenderEditor->GetExportStatusText());
        } else if (!m_RenderEditor->IsExportBusy()) {
            CompleteCurrent();
        } else if (PhaseTimedOut(std::chrono::minutes(10))) {
            FailCurrent("Writing the PNG timed out.");
        }
        return;
    case Phase::Idle:
    default:
        return;
    }
}

void QueueRenderCoordinator::RequestWorkerShutdownForAppClose() {
    if (m_WorkerShutdownRequested) return;
    m_WorkerShutdownRequested = true;
    m_ProjectLoadRequest.Cancel();
    CancelActiveInspection(false);
    if (m_RenderEditor) m_RenderEditor->RequestWorkerShutdownForAppClose();
}

void QueueRenderCoordinator::Shutdown() {
    RequestWorkerShutdownForAppClose();
    m_Notifier.CancelActivity(m_ItemActivity, "Queue export stopped.");
    m_Notifier.CancelActivity(m_ExportActivity, "Queue export stopped.");
    m_InspectionNotifier.CancelActivity(m_InspectionActivity, "Image preview stopped.");
    m_ItemActivity = {};
    m_ExportActivity = {};
    m_InspectionActivity = {};
    m_ProjectLoadRequest.Cancel();
    m_InspectionLoadRequest.Cancel();
    std::vector<std::uint64_t> queued;
    queued.reserve(m_Pending.size());
    for (const Item& item : m_Pending) queued.push_back(item.id);
    if (m_Model) {
        m_Model->ReleaseQueued(queued);
        if (m_Current.id && m_Phase != Phase::Idle && m_Phase != Phase::PrepareItem)
            m_Model->UpdateState(m_Current.id, ItemState::Failed, 0.0f,
                "Queue export was interrupted.");
    }
    m_Pending.clear();
    m_Current = {};
    m_CurrentOutputPath.clear();
    m_StopAfterCurrent = false;
    m_PendingInspection.reset();
    m_InspectionCompletion = {};
    m_InspectionPhase = InspectionPhase::Idle;
    m_Phase = Phase::Idle;
    if (m_RenderEditor) {
        m_RenderEditor->Shutdown();
        m_RenderEditor.reset();
    }
    m_Initialized = false;
}

} // namespace Stack::Queue
