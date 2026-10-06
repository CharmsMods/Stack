#pragma once

#include "Notifications/Notifier.h"
#include "Async/TaskSystem.h"
#include <string>

using UiNotificationSeverity = Stack::Notifications::Severity;

// Compatibility for existing producers. Presentation and records are owned by
// the common system; new features can call their scoped Notifier directly.
inline Stack::Notifications::EventId PostUiNotification(
    const Stack::Notifications::Notifier& notifier, UiNotificationSeverity severity,
    std::string message, std::string key = {}, bool preview = true) {
    if (message.empty()) return 0;
    Stack::Notifications::NoticeSpec notice;
    notice.severity = severity;
    notice.message = std::move(message);
    notice.dedupeKey = std::move(key);
    notice.preview = preview;
    const auto current = Async::TaskSystem::CurrentActivity();
    if (current.ownerId == notifier.GetOwner().id &&
        current.ownerGeneration == notifier.GetOwner().generation)
        notice.operationId = current.operationId;
    return notifier.Post(std::move(notice));
}
