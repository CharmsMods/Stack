#include "NotificationStore.h"
#include "Notifier.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace Stack::Notifications {
namespace {

double Now() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

bool SameOwner(const Owner& a, const Owner& b) {
    return a.id == b.id && a.generation == b.generation;
}

auto FindRecord(std::vector<Record>& records, EventId id) {
    return std::find_if(records.begin(), records.end(),
        [id](const Record& record) { return record.id == id; });
}

void SanitizeActions(NoticeSpec& spec) {
    for (auto& action : spec.actions) {
        if (action.destructive) {
            action.defaultAction = false;
            action.safeCancel = false;
        }
    }
}

Outcome ActualOutcome(const NoticeSpec& spec) {
    if (spec.outcome != Outcome::None) return spec.outcome;
    if (spec.severity == Severity::Success) return Outcome::Success;
    if (spec.severity == Severity::Error) return Outcome::Failure;
    return Outcome::None;
}

void AddAcknowledgement(NoticeSpec& spec, std::weak_ptr<NotificationStore> store, EventId id) {
    if (spec.route != Route::Center || !spec.actions.empty()) return;
    const bool issue = spec.severity == Severity::Warning || spec.severity == Severity::Error;
    ActionSpec acknowledge;
    acknowledge.label = "Acknowledge";
    acknowledge.resolveOnSuccess = false;
    acknowledge.invoke = [store = std::move(store), id, issue] {
        if (const auto retained = store.lock()) {
            retained->CloseDialog(id);
            if (issue) retained->Dismiss(id);
            else retained->Resolve(id);
        }
        return ActionResult::Success();
    };
    spec.actions.push_back(std::move(acknowledge));
}

} // namespace

NotificationStore::NotificationStore() : m_MainThread(std::this_thread::get_id()) {}

Notifier NotificationStore::ForOwner(OwnerId id, std::string label, OwnerKind kind) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    auto found = m_Owners.find(id);
    if (found == m_Owners.end()) {
        found = m_Owners.emplace(id, OwnerState{Owner{id, 1, std::move(label), kind}, true}).first;
    } else if (!found->second.valid) {
        found->second.value.generation++;
        found->second.value.label = std::move(label);
        found->second.value.kind = kind;
        found->second.valid = true;
    } else {
        found->second.value.label = std::move(label);
        found->second.value.kind = kind;
    }
    return Notifier(shared_from_this(), found->second.value);
}

bool NotificationStore::IsOwnerValidLocked(const Owner& owner) const {
    const auto found = m_Owners.find(owner.id);
    return found != m_Owners.end() && found->second.valid &&
        found->second.value.generation == owner.generation;
}

bool NotificationStore::IsOwnerValid(const Owner& owner) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return IsOwnerValidLocked(owner);
}

void NotificationStore::SetOwnerLabel(OwnerId id, std::string label) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = m_Owners.find(id);
    if (found == m_Owners.end() || !found->second.valid) return;
    found->second.value.label = std::move(label);
    for (auto& record : m_Records) {
        if (SameOwner(record.owner, found->second.value)) {
            record.owner.label = found->second.value.label;
        }
    }
}

void NotificationStore::InvalidateOwner(OwnerId id) {
    CaptureRelease release{*this};
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_Owners.find(id);
        if (found == m_Owners.end()) return;
        found->second.valid = false;
        for (auto it = m_Operations.begin(); it != m_Operations.end();) {
            if (it->second.id == id) it = m_Operations.erase(it);
            else ++it;
        }
        for (auto& record : m_Records) {
            if (!SameOwner(record.owner, found->second.value)) continue;
            record.ownerValid = false;
            record.centerRequested = false;
            record.actionPending = false;
            record.dismissed = true;
            // Do not leave feature callbacks referring to the closing owner.
            RetireCallbacksLocked(record.content);
            record.updatedAt = Now();
            record.revision = ++m_Revision;
        }
        TrimLocked();
    }
    // Feature captures can release task leases. Destroy them outside the lock.
}

std::vector<Record> NotificationStore::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Records;
}

std::optional<Record> NotificationStore::Find(EventId id) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = std::find_if(m_Records.begin(), m_Records.end(),
        [id](const Record& record) { return record.id == id; });
    return found == m_Records.end() ? std::optional<Record>{} : *found;
}

EventId NotificationStore::Post(const Owner& owner, NoticeSpec spec, RecordKind kind) {
    if (spec.title.empty() && spec.message.empty() && !spec.customBody && !spec.imagePreview) return 0;
    SanitizeActions(spec);
    CaptureRelease release{*this};
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!IsOwnerValidLocked(owner)) return 0;
    if (spec.operationId != 0) {
        auto operation = m_Operations.find(spec.operationId);
        if (operation == m_Operations.end()) {
            // Async execution IDs use a separate range. Adopt their owner on
            // first actual result, without interpreting ticket expiry.
            if ((spec.operationId & (OperationId{1} << 63)) == 0 ||
                m_InvalidOperations.count(spec.operationId)) return 0;
            operation = m_Operations.emplace(spec.operationId, owner).first;
        }
        if (!SameOwner(operation->second, owner)) return 0;
    }
    if (!spec.dedupeKey.empty()) {
        const auto existing = std::find_if(m_Records.begin(), m_Records.end(),
            [&](const Record& record) {
                return SameOwner(record.owner, owner) && record.kind == kind &&
                    record.operationId == spec.operationId &&
                    record.content.dedupeKey == spec.dedupeKey &&
                    record.state != RecordState::Resolved && record.state != RecordState::Cancelled;
            });
        if (existing != m_Records.end()) {
            // Updates to an existing issue never reopen a dismissed preview.
            existing->occurrences++;
            const auto previousOutcome = existing->outcome;
            if (!existing->actionPending) {
                AddAcknowledgement(spec, weak_from_this(), existing->id);
                RetireCallbacksLocked(existing->content);
                existing->content = std::move(spec);
                existing->outcome = ActualOutcome(existing->content);
                if (kind == RecordKind::Notice)
                    existing->state = existing->outcome == Outcome::Success && existing->content.route != Route::Center
                        ? RecordState::Succeeded : RecordState::Active;
            }
            existing->updatedAt = Now();
            existing->revision = ++m_Revision;
            if (existing->content.preview && existing->outcome != previousOutcome &&
                existing->outcome != Outcome::None) {
                existing->dismissed = false;
                existing->previewRevision = existing->revision;
            }
            return existing->id;
        }
    }
    Record record;
    record.id = m_NextEvent++;
    record.owner = m_Owners.at(owner.id).value;
    record.operationId = spec.operationId;
    record.kind = kind;
    record.content = std::move(spec);
    record.outcome = ActualOutcome(record.content);
    if (kind == RecordKind::Notice && record.outcome == Outcome::Success && record.content.route != Route::Center)
        record.state = RecordState::Succeeded;
    record.centerRequested = record.content.route == Route::Center && record.content.foreground;
    record.createdAt = record.updatedAt = Now();
    record.revision = ++m_Revision;
    record.previewRevision = record.content.preview ? record.revision : 0;
    const auto id = record.id;
    AddAcknowledgement(record.content, weak_from_this(), id);
    m_Records.push_back(std::move(record));
    TrimLocked();
    return id;
}

OperationId NotificationStore::NewOperation(const Owner& owner) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!IsOwnerValidLocked(owner)) return 0;
    const OperationId id = m_NextOperation++;
    m_Operations.emplace(id, owner);
    return id;
}

bool NotificationStore::IsOperationCurrent(const Owner& owner, OperationId operation) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!IsOwnerValidLocked(owner)) return false;
    if (operation == 0) return true;
    const auto found = m_Operations.find(operation);
    return found != m_Operations.end() && SameOwner(found->second, owner);
}

void NotificationStore::InvalidateOperation(const Owner& owner, OperationId operation) {
    CaptureRelease release{*this};
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_Operations.find(operation);
        if (found == m_Operations.end() || !SameOwner(found->second, owner)) return;
        m_Operations.erase(found);
        m_InvalidOperations.insert(operation);
        for (auto& record : m_Records) {
            if (!SameOwner(record.owner, owner) || record.operationId != operation) continue;
            RetireCallbacksLocked(record.content);
            record.centerRequested = false;
            record.actionPending = false;
            if (!record.Terminal()) {
                record.state = RecordState::Cancelled;
                if (record.outcome == Outcome::None) record.outcome = Outcome::Cancelled;
            }
            record.revision = ++m_Revision;
            record.updatedAt = Now();
        }
        TrimLocked();
    }
}

ActivityHandle NotificationStore::BeginActivity(const Owner& owner, NoticeSpec spec) {
    if (spec.operationId == 0) spec.operationId = NewOperation(owner);
    if (spec.operationId == 0) return {};
    const auto operation = spec.operationId;
    // Running work stays in the panel and header. Only its result may preview.
    const bool resultPreview = spec.preview;
    spec.preview = false;
    const EventId id = Post(owner, std::move(spec), RecordKind::Activity);
    if (id == 0) return {};
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, id);
    if (found == m_Records.end() || !SameOwner(found->owner, owner)) return {};
    found->state = RecordState::Running;
    found->outcome = Outcome::None;
    found->content.preview = resultPreview;
    found->centerRequested = false;
    return {id, operation};
}

void NotificationStore::UpdateActivity(const Owner& owner, ActivityHandle activity,
    std::string stage, std::optional<Progress> progress, std::string details) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!IsOwnerValidLocked(owner)) return;
    const auto found = FindRecord(m_Records, activity.eventId);
    if (found == m_Records.end() || !SameOwner(found->owner, owner) ||
        found->operationId != activity.operationId || found->state != RecordState::Running) return;
    if (found->content.message == stage && found->content.details == details && found->content.progress.has_value() == progress.has_value() &&
        (!progress || (found->content.progress->completed == progress->completed &&
         found->content.progress->total == progress->total && found->content.progress->label == progress->label))) return;
    found->content.message = std::move(stage);
    found->content.details = std::move(details);
    found->content.progress = std::move(progress);
    found->updatedAt = Now();
    found->revision = ++m_Revision;
}

void NotificationStore::EndActivity(const Owner& owner, ActivityHandle activity,
    Outcome outcome, std::string message, std::string details, bool preview) {
    CaptureRelease release{*this};
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, activity.eventId);
    if (found == m_Records.end() || !SameOwner(found->owner, owner) ||
        found->operationId != activity.operationId || found->state != RecordState::Running) return;
    found->outcome = outcome;
    found->state = outcome == Outcome::Success ? RecordState::Succeeded :
        outcome == Outcome::Partial ? RecordState::Active :
        outcome == Outcome::Failure ? RecordState::Failed : RecordState::Cancelled;
    found->content.severity = outcome == Outcome::Success ? Severity::Success :
        outcome == Outcome::Partial ? Severity::Warning :
        outcome == Outcome::Failure ? Severity::Error : Severity::Info;
    found->content.outcome = outcome;
    if (!message.empty()) found->content.message = std::move(message);
    found->content.details = std::move(details);
    if (found->ownerValid) found->dismissed = false;
    found->updatedAt = Now();
    found->revision = ++m_Revision;
    if (preview && found->content.preview && found->ownerValid) found->previewRevision = found->revision;
    TrimLocked();
}

void NotificationStore::ForgetActivity(const Owner& owner, ActivityHandle activity) {
    CaptureRelease release{*this};
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, activity.eventId);
    if (found == m_Records.end() || !SameOwner(found->owner, owner) ||
        found->operationId != activity.operationId || found->state != RecordState::Running) return;
    RetireCallbacksLocked(found->content);
    m_Records.erase(found);
    if (std::none_of(m_Records.begin(), m_Records.end(), [&](const Record& record) {
        return record.operationId == activity.operationId;
    })) m_Operations.erase(activity.operationId);
}

bool NotificationStore::CanInvokeAction(EventId event, std::size_t action) const {
    if (std::this_thread::get_id() != m_MainThread) return false;
    ActionSpec spec;
    Owner owner;
    OperationId operation = 0;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = std::find_if(m_Records.begin(), m_Records.end(),
            [event](const Record& record) { return record.id == event; });
        if (found == m_Records.end() || !IsOwnerValidLocked(found->owner) ||
            found->actionPending || found->state == RecordState::Resolved ||
            found->state == RecordState::Cancelled || action >= found->content.actions.size()) return false;
        owner = found->owner;
        operation = found->operationId;
        spec = found->content.actions[action];
    }
    if (!spec.invoke || !IsOperationCurrent(owner, operation)) return false;
    try { return !spec.canInvoke || spec.canInvoke(); }
    catch (...) { return false; }
}

ActionResult NotificationStore::InvokeAction(EventId event, std::size_t action) {
    if (std::this_thread::get_id() != m_MainThread) {
        return ActionResult::Failure("This action must run on the main thread.");
    }
    ActionSpec spec;
    Owner owner;
    OperationId operation = 0;
    std::uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = FindRecord(m_Records, event);
        if (found == m_Records.end() || !IsOwnerValidLocked(found->owner) ||
            found->actionPending || found->state == RecordState::Resolved ||
            found->state == RecordState::Cancelled || action >= found->content.actions.size()) {
            return ActionResult::Failure("This action is no longer available.");
        }
        owner = found->owner;
        operation = found->operationId;
        revision = found->revision;
        spec = found->content.actions[action];
    }
    bool allowed = spec.invoke && IsOperationCurrent(owner, operation);
    try { allowed = allowed && (!spec.canInvoke || spec.canInvoke()); }
    catch (...) { allowed = false; }
    if (!allowed) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = FindRecord(m_Records, event);
        if (found != m_Records.end() && IsOwnerValidLocked(owner)) {
            found->actionError = "This action is no longer available.";
        }
        return ActionResult::Failure("This action is no longer available.");
    }
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = FindRecord(m_Records, event);
        if (found == m_Records.end() || found->revision != revision || found->actionPending ||
            !IsOwnerValidLocked(owner)) return ActionResult::Failure("This action has changed. Review it again.");
        if (operation != 0) {
            const auto liveOperation = m_Operations.find(operation);
            if (liveOperation == m_Operations.end() || !SameOwner(liveOperation->second, owner))
                return ActionResult::Failure("This action is no longer available.");
        }
        found->actionPending = true;
        found->pendingAction = action;
        found->actionError.clear();
        found->revision = ++m_Revision;
    }
    ActionResult result;
    try { result = spec.invoke(); }
    catch (const std::exception& exception) { result = ActionResult::Failure(exception.what()); }
    catch (...) { result = ActionResult::Failure("The action failed unexpectedly. Try again."); }
    FinishAction(event, action, result);
    return result;
}

void NotificationStore::ApplyActionResultLocked(Record& record, std::size_t action, ActionResult result) {
    if (result.state == ActionState::Pending) return;
    record.actionPending = false;
    record.updatedAt = Now();
    record.revision = ++m_Revision;
    if (result.state == ActionState::Failure) {
        record.actionError = result.message.empty() ? "The action failed. Try again." : std::move(result.message);
        if (record.outcome == Outcome::None) record.outcome = Outcome::Failure;
        return;
    }
    record.actionError.clear();
    if (action < record.content.actions.size() && record.content.actions[action].resolveOnSuccess) {
        const bool cancelled = record.content.actions[action].safeCancel;
        record.state = cancelled ? RecordState::Cancelled : RecordState::Resolved;
        if (record.outcome == Outcome::None)
            record.outcome = cancelled ? Outcome::Cancelled : Outcome::Success;
        record.centerRequested = false;
    }
}

void NotificationStore::FinishAction(EventId event, std::size_t action, ActionResult result) {
    CaptureRelease release{*this};
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, event);
    if (found == m_Records.end() || !IsOwnerValidLocked(found->owner) ||
        !found->actionPending || found->pendingAction != action) return;
    ApplyActionResultLocked(*found, action, std::move(result));
    TrimLocked();
}

void NotificationStore::Resolve(EventId event) {
    CaptureRelease release{*this};
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, event);
    if (found == m_Records.end()) return;
    found->state = RecordState::Resolved;
    found->centerRequested = false;
    found->actionPending = false;
    found->updatedAt = Now();
    found->revision = ++m_Revision;
    TrimLocked();
}

void NotificationStore::Dismiss(EventId event) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, event);
    if (found == m_Records.end()) return;
    found->dismissed = true;
    found->centerRequested = false;
}

void NotificationStore::Show(EventId event) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, event);
    if (found != m_Records.end()) found->dismissed = false;
}

void NotificationStore::RequestCenter(EventId event) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, event);
    if (found == m_Records.end() || !IsOwnerValidLocked(found->owner) ||
        found->state == RecordState::Resolved || found->state == RecordState::Cancelled) return;
    found->centerRequested = true;
    found->dismissed = false;
}

void NotificationStore::CloseDialog(EventId event) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = FindRecord(m_Records, event);
    if (found != m_Records.end()) found->centerRequested = false;
}

void NotificationStore::ClearCompleted() {
    CaptureRelease release{*this};
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::unordered_set<OperationId> removedOperations;
    for (const auto& record : m_Records)
        if (record.Terminal() && record.operationId != 0) removedOperations.insert(record.operationId);
    for (auto& record : m_Records)
        if (record.Terminal()) RetireCallbacksLocked(record.content);
    m_Records.erase(std::remove_if(m_Records.begin(), m_Records.end(),
        [](const Record& record) { return record.Terminal(); }), m_Records.end());
    for (const auto operation : removedOperations) {
        if (std::none_of(m_Records.begin(), m_Records.end(), [&](const Record& record) {
            return record.operationId == operation;
        })) m_Operations.erase(operation);
    }
}

void NotificationStore::TrimLocked() {
    constexpr std::size_t retainedResults = 200;
    std::size_t completed = 0;
    for (const auto& record : m_Records) completed += record.Terminal() ? 1 : 0;
    while (completed > retainedResults) {
        const auto oldest = std::min_element(m_Records.begin(), m_Records.end(),
            [](const Record& a, const Record& b) {
                if (a.Terminal() != b.Terminal()) return a.Terminal();
                return a.updatedAt < b.updatedAt;
            });
        if (oldest == m_Records.end() || !oldest->Terminal()) break;
        const auto operation = oldest->operationId;
        RetireCallbacksLocked(oldest->content);
        m_Records.erase(oldest);
        if (operation != 0 && std::none_of(m_Records.begin(), m_Records.end(), [&](const Record& record) {
            return record.operationId == operation;
        })) m_Operations.erase(operation);
        --completed;
    }
}

void NotificationStore::RetireCallbacksLocked(NoticeSpec& content) {
    if (content.actions.empty() && !content.customBody && !content.imagePreview) return;
    NoticeSpec released;
    released.actions = std::move(content.actions);
    released.customBody = std::move(content.customBody);
    released.imagePreview = std::move(content.imagePreview);
    content.imagePreview.reset();
    m_ReleasedContent.push_back(std::move(released));
}

void NotificationStore::ReleaseCaptures() {
    std::vector<NoticeSpec> released;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        released.swap(m_ReleasedContent);
    }
}

} // namespace Stack::Notifications
