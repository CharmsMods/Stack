#pragma once

#include "NotificationModel.h"
#include <memory>

namespace Stack::Notifications {

class NotificationStore;

// Copies retain the initiating owner, even when another project becomes active.
class Notifier {
public:
    Notifier() = default;
    bool Valid() const;
    explicit operator bool() const { return Valid(); }
    const Owner& GetOwner() const { return m_Owner; }
    OperationId NewOperation() const;
    bool IsOperationCurrent(OperationId operation) const;
    void InvalidateOperation(OperationId operation) const;
    EventId Post(NoticeSpec spec) const;
    EventId Info(std::string message, std::string title = {}, std::string details = {}) const;
    EventId Warning(std::string message, std::string title = {}, std::string details = {}) const;
    EventId Error(std::string message, std::string title = {}, std::string details = {}) const;
    EventId RequestDecision(NoticeSpec spec) const;
    ActivityHandle BeginActivity(NoticeSpec spec) const;
    ActivityHandle BeginActivity(std::string title, bool meaningful = true) const;
    void UpdateActivity(ActivityHandle activity, std::string stage,
        std::optional<Progress> progress = {}, std::string details = {}) const;
    void CompleteActivity(ActivityHandle activity, std::string message = {},
        bool preview = true) const;
    void FinishActivity(ActivityHandle activity, Outcome outcome, std::string message,
        std::string details = {}, bool preview = true) const;
    void FailActivity(ActivityHandle activity, std::string message,
        std::string details = {}) const;
    void CancelActivity(ActivityHandle activity, std::string message = {}) const;
    void ForgetActivity(ActivityHandle activity) const;
    void Resolve(EventId event) const;
    void Dismiss(EventId event) const;
    void FinishAction(EventId event, std::size_t action, ActionResult result) const;

private:
    friend class NotificationStore;
    Notifier(std::shared_ptr<NotificationStore> store, Owner owner)
        : m_Store(std::move(store)), m_Owner(std::move(owner)) {}
    std::weak_ptr<NotificationStore> m_Store;
    Owner m_Owner;
};

} // namespace Stack::Notifications
