#include "Notifications/NotificationStore.h"
#include "Notifications/Notifier.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct CountCopies {
    int* copies;
    explicit CountCopies(int& count) : copies(&count) {}
    CountCopies(const CountCopies& other) : copies(other.copies) { ++*copies; }
    void operator()() const {}
};
void Check(const Stack::Notifications::NotificationStore& store, int& copies) {
    const auto records = store.Snapshot();
    const auto expected = static_cast<std::size_t>(std::count_if(records.begin(), records.end(),
        [](const auto& record) { return record.NeedsAttention(); }));
    copies = 0;
    for (int frame = 0; frame < 16; ++frame)
        Require(store.CountNeedsAttention() == expected, "Indicator attention semantics changed");
    Require(copies == 0, "Attention-only query copied feature callback payloads");
}

std::vector<Stack::Notifications::RunningActivityIdentity> CheckRunningActivities(
    const Stack::Notifications::NotificationStore& store, int& copies,
    const std::shared_ptr<int>& previewLifetime) {
    using namespace Stack::Notifications;
    const auto records = store.Snapshot();
    const auto previewOwners = previewLifetime.use_count();
    copies = 0;
    const auto activities = store.SnapshotOwnedRunningActivities();
    Require(copies == 0, "Identity query copied feature callback payloads");
    Require(previewLifetime.use_count() == previewOwners, "Identity query retained a preview lease");
    std::size_t index = 0;
    for (const auto& record : records) {
        if (record.state != RecordState::Running || record.content.dedupeKey == "collected-activity") continue;
        Require(index < activities.size(), "Identity query dropped a matching Running record");
        const auto& activity = activities[index++];
        Require(activity.ownerId == record.owner.id && activity.operationId == record.operationId &&
            activity.title == record.content.title, "Identity query changed collector matching fields");
    }
    Require(index == activities.size(), "Identity query included a nonmatching record");
    return activities;
}

void CheckActivityIdentityQueries() {
    using namespace Stack::Notifications;
    auto store = std::make_shared<NotificationStore>();
    auto first = store->ForOwner(1, "First");
    auto second = store->ForOwner(2, "Second");
    auto previewLifetime = std::make_shared<int>(42);
    int copies = 0;
    Require(CheckRunningActivities(*store, copies, previewLifetime).empty(), "Empty store has identities");

    NoticeSpec owned;
    owned.title = std::string(96, 'r');
    owned.dedupeKey = "owned-work";
    owned.details = std::string(4096, 'x');
    owned.customBody = CountCopies(copies);
    owned.imagePreview = ImagePreview{0, 0, 0, "preview", previewLifetime, {}};
    const auto activity = first.BeginActivity(std::move(owned));
    const auto otherActivity = second.BeginActivity("Other owner");
    NoticeSpec collector;
    collector.title = "Collected work";
    collector.dedupeKey = "collected-activity";
    collector.operationId = activity.operationId;
    const auto collectedActivity = first.BeginActivity(std::move(collector));
    const auto notice = first.Info("Not running");
    Require(activity && otherActivity && collectedActivity && notice, "Identity fixture creation failed");
    const auto retained = CheckRunningActivities(*store, copies, previewLifetime);
    Require(retained.size() == 2, "Owned Running activities must exclude collector and notice records");

    first.Dismiss(activity.eventId);
    Require(CheckRunningActivities(*store, copies, previewLifetime).size() == 2,
        "Dismissal must not remove Running activity identities");
    NoticeSpec replacement;
    replacement.title = "Retitled work";
    replacement.dedupeKey = "owned-work";
    replacement.operationId = activity.operationId;
    replacement.customBody = CountCopies(copies);
    replacement.imagePreview = ImagePreview{0, 0, 0, "preview", previewLifetime, {}};
    Require(first.BeginActivity(std::move(replacement)).eventId == activity.eventId,
        "Fixture must update the existing owned activity");
    Require(CheckRunningActivities(*store, copies, previewLifetime).front().title == "Retitled work",
        "Identity query missed an updated title");
    Require(retained.front().title == std::string(96, 'r'), "Identity snapshot borrowed mutable title storage");

    second.CompleteActivity(otherActivity);
    Require(CheckRunningActivities(*store, copies, previewLifetime).size() == 1,
        "Completed activities must not suppress collector entries");
    first.InvalidateOperation(activity.operationId);
    Require(CheckRunningActivities(*store, copies, previewLifetime).empty(),
        "Cancelled operation must not suppress collector entries");
    const auto invalidOwnerActivity = first.BeginActivity("Closing owner");
    Require(static_cast<bool>(invalidOwnerActivity), "Invalid owner fixture creation failed");
    store->InvalidateOwner(1);
    Require(CheckRunningActivities(*store, copies, previewLifetime).size() == 1,
        "Invalid owners' Running records must preserve existing collector matching");
    first = store->ForOwner(1, "Reopened");
    const auto reopened = first.BeginActivity("Closing owner");
    Require(static_cast<bool>(reopened), "Reopened owner fixture creation failed");
    Require(CheckRunningActivities(*store, copies, previewLifetime).size() == 2,
        "Owner generation changes must preserve existing owner-ID matching");
    first.FailActivity(reopened, "Failed");
    Require(CheckRunningActivities(*store, copies, previewLifetime).size() == 1,
        "Failed activities must not suppress collector entries");
    store->Resolve(invalidOwnerActivity.eventId);
    Require(CheckRunningActivities(*store, copies, previewLifetime).empty(),
        "Resolved activities must not suppress collector entries");
}
}

int main() try {
    using namespace Stack::Notifications;
    auto store = std::make_shared<NotificationStore>();
    auto first = store->ForOwner(1, "First");
    auto second = store->ForOwner(2, "Second");
    int copies = 0;
    Check(*store, copies);
    NoticeSpec spec;
    spec.title = "Review";
    spec.details = std::string(4096, 'x');
    spec.customBody = CountCopies(copies);
    spec.severity = Severity::Warning;
    auto lifetime = std::make_shared<int>(42);
    spec.imagePreview = ImagePreview{0, 0, 0, "preview", lifetime, {}};
    const auto warning = first.Post(std::move(spec));
    const auto info = first.Info("Information");
    const auto error = second.Error("Failure");
    auto activity = second.BeginActivity("Work", false);
    Require(warning && info && error && activity, "Fixture creation failed");
    const auto owners = lifetime.use_count();
    Check(*store, copies);
    Require(store->CountNeedsAttention() == 2, "Warnings/errors must retain attention");
    Require(lifetime.use_count() == owners, "Aggregate retained an extra preview lease");
    first.Dismiss(warning);
    Check(*store, copies); // Dismissal does not resolve an active issue.
    first.Resolve(warning);
    Check(*store, copies);
    second.FailActivity(activity, "Failed work");
    Check(*store, copies);
    store->InvalidateOwner(2);
    Check(*store, copies);
    Require(store->CountNeedsAttention() == 0, "Invalid owner retained attention");
    CheckActivityIdentityQueries();
    std::cout << "Notification attention and activity identities match record semantics with zero callback copies.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
