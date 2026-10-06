#include "Editor/EditorModule.h"
#include "Editor/AutoBracket/AutoBracketCoordinator.h"

Stack::Notifications::EventId EditorModule::RequestNotificationDecision(Stack::Notifications::NoticeSpec notice) {
    const auto notifier = GetNotifier();
    if (!notifier.Valid()) return 0;
    if (!notice.operationId) notice.operationId = notifier.NewOperation();
    const auto operation = notice.operationId;
    const auto event = notifier.RequestDecision(std::move(notice));
    if (event) {
        m_NotificationDecisionOwners.push_back({operation, GetProjectDocumentId(),
            m_Project->files, m_Project->files->load.generation});
        m_NotificationDecisionFiles = m_Project->files;
        m_NotificationDecisionDocument = GetProjectDocumentId();
        m_NotificationDecisionLoadGeneration = m_Project->files->load.generation;
    }
    return event;
}

void EditorModule::UpdateNotificationDecisions(bool foreground) {
    m_NotificationForeground = foreground;
    const auto notifier = GetNotifier();
    if (m_ProjectConflictReloadPending) {
        const auto pending = std::find_if(m_NotificationDecisionOwners.begin(), m_NotificationDecisionOwners.end(),
            [this](const auto& owner) { return owner.operation == m_ProjectConflictReloadOperation; });
        if (pending == m_NotificationDecisionOwners.end() || pending->files.lock() != m_Project->files ||
            m_Project->files->load.generation != m_ProjectConflictReloadGeneration) m_ProjectConflictReloadPending = false;
    }
    m_NotificationDecisionOwners.erase(std::remove_if(m_NotificationDecisionOwners.begin(),
        m_NotificationDecisionOwners.end(), [this, &notifier](const auto& owner) {
            if (!notifier.IsOperationCurrent(owner.operation)) return true;
            if (m_ProjectConflictReloadPending && owner.operation == m_ProjectConflictReloadOperation &&
                owner.files.lock() == m_Project->files &&
                m_Project->files->load.generation == m_ProjectConflictReloadGeneration) return false;
            if (owner.document == GetProjectDocumentId() && owner.files.lock() == m_Project->files &&
                owner.loadGeneration == m_Project->files->load.generation) return false;
            notifier.InvalidateOperation(owner.operation);
            return true;
        }), m_NotificationDecisionOwners.end());
    if (m_NotificationDecisionFiles.lock() != m_Project->files ||
        m_NotificationDecisionDocument != GetProjectDocumentId() ||
        m_NotificationDecisionLoadGeneration != m_Project->files->load.generation) {
        m_NotificationDecisionFiles = m_Project->files;
        m_NotificationDecisionDocument = GetProjectDocumentId();
        m_NotificationDecisionLoadGeneration = m_Project->files->load.generation;
        m_OpenMultiFrameDeletePopup = m_OpenMultiFrameFrameDeletePopup = false;
        m_PendingDeleteMultiFrameSourceSetId.clear();
        m_PendingDeleteMultiFrameFrameSetId.clear();
        m_PendingDeleteMultiFrameFrameId.clear();
        if (!m_ProjectConflictReloadPending) {
            m_ProjectConflictNotice = 0;
            m_ProjectConflictDocument.clear();
            m_ProjectInteractionUi.suppressConflictPrompt = false;
        }
        if (m_AutoBracket && m_AutoBracket->Yielding()) m_AutoBracket->DismissForeground();
        m_AutoBracketDecisionOwner = nullptr;
        m_AutoBracketDecisionGeneration = 0;
        if (m_GraphCaptureSavePending) {
            m_GraphCaptureSavePending = false;
            ++m_GraphCaptureSaveGeneration;
            m_GraphCaptureStatusText = "The graph save request is no longer current.";
        }
    }
    RenderMultiFrameSourceSetDeletePopup();
    RenderMultiFrameFrameDeletePopup();
    RenderRawWorkspaceGalleryRevertPopup();
    RenderBracketingOrientationReview();
    UpdateRawWorkspaceNotificationDecisions();

    if (!m_AutoBracket || !m_AutoBracket->WaitingForChoice()) return;
    auto* const coordinator = m_AutoBracket.get();
    const auto generation = coordinator->ForegroundRequestId();
    if (m_AutoBracketDecisionOwner == coordinator && m_AutoBracketDecisionGeneration == generation) return;
    m_AutoBracketDecisionOwner = coordinator;
    m_AutoBracketDecisionGeneration = generation;
    namespace N = Stack::Notifications;
    const auto valid = [this, coordinator, generation] {
        return m_AutoBracket.get() == coordinator && !m_AutoBracketWorkspaceClosing &&
            coordinator->WaitingForChoice() && coordinator->ForegroundRequestId() == generation;
    };
    N::NoticeSpec notice;
    notice.title = "Bracket processing in progress";
    notice.message = "Finish this bracket before you " + coordinator->ActionLabel() + ", or cancel it and continue.";
    notice.details = "Completed brackets are already saved.";
    notice.route = N::Route::Center;
    notice.foreground = foreground;
    notice.operationId = GetNotifier().NewOperation();
    N::ActionSpec finish;
    finish.label = "Finish and continue";
    finish.canInvoke = valid;
    finish.invoke = [coordinator] { coordinator->ResolveForeground(false); return N::ActionResult::Success(); };
    N::ActionSpec cancel;
    cancel.label = "Cancel bracket and continue";
    cancel.canInvoke = valid;
    cancel.destructive = true;
    cancel.invoke = [coordinator] { coordinator->ResolveForeground(true); return N::ActionResult::Success(); };
    N::ActionSpec keep;
    keep.label = "Keep processing";
    keep.safeCancel = true;
    keep.canInvoke = valid;
    keep.invoke = [coordinator] { coordinator->DismissForeground(); return N::ActionResult::Success(); };
    notice.actions = {std::move(finish), std::move(cancel), std::move(keep)};
    RequestNotificationDecision(std::move(notice));
}
