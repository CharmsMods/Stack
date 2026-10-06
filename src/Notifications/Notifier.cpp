#include "Notifier.h"
#include "NotificationStore.h"

namespace Stack::Notifications {

bool Notifier::Valid() const { const auto store = m_Store.lock(); return store && store->IsOwnerValid(m_Owner); }

OperationId Notifier::NewOperation() const {
    const auto store = m_Store.lock();
    return store ? store->NewOperation(m_Owner) : 0;
}

bool Notifier::IsOperationCurrent(OperationId operation) const {
    const auto store = m_Store.lock();
    return store && store->IsOperationCurrent(m_Owner, operation);
}

void Notifier::InvalidateOperation(OperationId operation) const {
    if (const auto store = m_Store.lock()) store->InvalidateOperation(m_Owner, operation);
}

EventId Notifier::Post(NoticeSpec spec) const {
    const auto store = m_Store.lock();
    return store ? store->Post(m_Owner, std::move(spec), RecordKind::Notice) : 0;
}

EventId Notifier::Info(std::string message, std::string title, std::string details) const {
    NoticeSpec spec;
    spec.message = std::move(message);
    spec.title = std::move(title);
    spec.details = std::move(details);
    return Post(std::move(spec));
}

EventId Notifier::Warning(std::string message, std::string title, std::string details) const {
    NoticeSpec spec;
    spec.severity = Severity::Warning;
    spec.message = std::move(message);
    spec.title = std::move(title);
    spec.details = std::move(details);
    return Post(std::move(spec));
}

EventId Notifier::Error(std::string message, std::string title, std::string details) const {
    NoticeSpec spec;
    spec.severity = Severity::Error;
    spec.message = std::move(message);
    spec.title = std::move(title);
    spec.details = std::move(details);
    return Post(std::move(spec));
}

EventId Notifier::RequestDecision(NoticeSpec spec) const {
    // Decisions need a persistent response, independently of processing busy.
    spec.route = Route::Center;
    const auto store = m_Store.lock();
    return store ? store->Post(m_Owner, std::move(spec), RecordKind::Decision) : 0;
}

ActivityHandle Notifier::BeginActivity(NoticeSpec spec) const {
    const auto store = m_Store.lock();
    return store ? store->BeginActivity(m_Owner, std::move(spec)) : ActivityHandle{};
}

ActivityHandle Notifier::BeginActivity(std::string title, bool meaningful) const {
    NoticeSpec spec;
    spec.title = std::move(title);
    spec.preview = meaningful;
    return BeginActivity(std::move(spec));
}

void Notifier::UpdateActivity(ActivityHandle activity, std::string stage,
    std::optional<Progress> progress, std::string details) const {
    if (const auto store = m_Store.lock()) store->UpdateActivity(m_Owner, activity, std::move(stage), std::move(progress), std::move(details));
}

void Notifier::CompleteActivity(ActivityHandle activity, std::string message, bool preview) const {
    if (const auto store = m_Store.lock()) store->EndActivity(m_Owner, activity, Outcome::Success, std::move(message), {}, preview);
}

void Notifier::FinishActivity(ActivityHandle activity, Outcome outcome, std::string message,
    std::string details, bool preview) const {
    if (const auto store = m_Store.lock()) store->EndActivity(m_Owner, activity,
        outcome, std::move(message), std::move(details), preview);
}

void Notifier::FailActivity(ActivityHandle activity, std::string message, std::string details) const {
    if (const auto store = m_Store.lock()) store->EndActivity(m_Owner, activity, Outcome::Failure,
        std::move(message), std::move(details), true);
}

void Notifier::CancelActivity(ActivityHandle activity, std::string message) const {
    if (const auto store = m_Store.lock()) store->EndActivity(m_Owner, activity, Outcome::Cancelled, std::move(message), {}, false);
}

void Notifier::ForgetActivity(ActivityHandle activity) const {
    if (const auto store = m_Store.lock()) store->ForgetActivity(m_Owner, activity);
}

void Notifier::Resolve(EventId event) const {
    const auto store = m_Store.lock();
    if (!store) return;
    const auto record = store->Find(event);
    if (record && record->owner.id == m_Owner.id && record->owner.generation == m_Owner.generation)
        store->Resolve(event);
}

void Notifier::Dismiss(EventId event) const {
    const auto store = m_Store.lock();
    if (!store) return;
    const auto record = store->Find(event);
    if (record && record->owner.id == m_Owner.id && record->owner.generation == m_Owner.generation)
        store->Dismiss(event);
}

void Notifier::FinishAction(EventId event, std::size_t action, ActionResult result) const {
    const auto store = m_Store.lock();
    if (!store) return;
    const auto record = store->Find(event);
    if (record && record->owner.id == m_Owner.id && record->owner.generation == m_Owner.generation)
        store->FinishAction(event, action, std::move(result));
}

} // namespace Stack::Notifications
