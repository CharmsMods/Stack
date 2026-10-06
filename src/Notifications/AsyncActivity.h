#pragma once

#include "Async/TaskSystem.h"
#include "Notifications/Notifier.h"

namespace Stack::Notifications {

inline Async::ActivityMetadata ForAsyncActivity(const Notifier& notifier,
    ActivityHandle activity, std::string label) {
    Async::ActivityMetadata metadata;
    metadata.ownerId = notifier.GetOwner().id;
    metadata.ownerGeneration = notifier.GetOwner().generation;
    metadata.operationId = activity.operationId;
    metadata.label = std::move(label);
    return metadata;
}

// Keep this lease through worker execution and result delivery. A discarded
// callback cancels the record. A completed record ignores the final release.
class AsyncActivityCompletion {
public:
    AsyncActivityCompletion(Notifier notifier, ActivityHandle activity)
        : m_Notifier(std::move(notifier)), m_Activity(activity) {}
    ~AsyncActivityCompletion() {
        m_Notifier.CancelActivity(m_Activity, "Work ended before its result could be delivered.");
    }
private:
    Notifier m_Notifier;
    ActivityHandle m_Activity;
};

inline std::shared_ptr<AsyncActivityCompletion> RetainAsyncActivity(
    const Notifier& notifier, ActivityHandle activity) {
    return std::make_shared<AsyncActivityCompletion>(notifier, activity);
}

} // namespace Stack::Notifications
