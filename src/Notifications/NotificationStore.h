#pragma once

#include "NotificationModel.h"

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Stack::Notifications {

class Notifier;

// Application-owned, session-only records. Posting and activity updates are
// thread safe. Action handlers and their feature guards run on the main thread.
class NotificationStore : public std::enable_shared_from_this<NotificationStore> {
public:
    NotificationStore();

    Notifier ForOwner(OwnerId id, std::string label,
        OwnerKind kind = OwnerKind::Project);
    void InvalidateOwner(OwnerId id);
    void SetOwnerLabel(OwnerId id, std::string label);
    bool IsOwnerValid(const Owner& owner) const;
    bool IsOperationCurrent(const Owner& owner, OperationId operation) const;
    std::vector<Record> Snapshot() const;
    std::optional<Record> Find(EventId id) const;
    bool CanInvokeAction(EventId event, std::size_t action) const;
    ActionResult InvokeAction(EventId event, std::size_t action);
    void FinishAction(EventId event, std::size_t action, ActionResult result);
    void Resolve(EventId event);
    void Dismiss(EventId event);
    void Show(EventId event);
    void RequestCenter(EventId event);
    void CloseDialog(EventId event);
    void ClearCompleted();

private:
    friend class Notifier;
    EventId Post(const Owner& owner, NoticeSpec spec, RecordKind kind);
    ActivityHandle BeginActivity(const Owner& owner, NoticeSpec spec);
    OperationId NewOperation(const Owner& owner);
    void InvalidateOperation(const Owner& owner, OperationId operation);
    void UpdateActivity(const Owner& owner, ActivityHandle activity,
        std::string stage, std::optional<Progress> progress, std::string details);
    void EndActivity(const Owner& owner, ActivityHandle activity,
        Outcome outcome, std::string message, std::string details, bool preview);
    void ForgetActivity(const Owner& owner, ActivityHandle activity);
    bool IsOwnerValidLocked(const Owner& owner) const;
    void TrimLocked();
    void ReleaseCaptures();
    void RetireCallbacksLocked(NoticeSpec& content);
    void ApplyActionResultLocked(Record& record, std::size_t action, ActionResult result);

    struct CaptureRelease {
        NotificationStore& store;
        ~CaptureRelease() { store.ReleaseCaptures(); }
    };

    struct OwnerState { Owner value; bool valid = true; };
    mutable std::mutex m_Mutex;
    std::unordered_map<OwnerId, OwnerState> m_Owners;
    std::unordered_map<OperationId, Owner> m_Operations;
    std::unordered_set<OperationId> m_InvalidOperations;
    std::vector<Record> m_Records;
    std::vector<NoticeSpec> m_ReleasedContent;
    std::thread::id m_MainThread;
    EventId m_NextEvent = 1;
    OperationId m_NextOperation = 1;
    std::uint64_t m_Revision = 0;
};

} // namespace Stack::Notifications
