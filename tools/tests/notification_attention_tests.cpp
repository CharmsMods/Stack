#include "Notifications/NotificationStore.h"
#include "Notifications/Notifier.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

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

void CheckPresentationSelection(const Stack::Notifications::NotificationStore& store, int& copies) {
    using namespace Stack::Notifications;
    const auto records = store.Snapshot();
    std::vector<EventId> currentDialogs{0, std::numeric_limits<EventId>::max()};
    std::vector<std::uint64_t> seenRevisions{0, std::numeric_limits<std::uint64_t>::max()};
    std::unordered_set<std::uint64_t> previewRevisions;
    for (const auto& record : records) {
        currentDialogs.push_back(record.id);
        seenRevisions.push_back(record.previewRevision);
        Require(!record.previewRevision || previewRevisions.insert(record.previewRevision).second,
            "Nonzero preview revisions must be unique, independent of presentation sort order");
    }
    copies = 0;
    for (const auto current : currentDialogs) {
        EventId expected = 0;
        for (const auto& record : records) {
            if (!record.ownerValid || !record.centerRequested || record.Terminal()) continue;
            if (!expected) expected = record.id;
            if (record.id == current) { expected = current; break; }
        }
        Require(store.FindRequestedDialog(current) == expected, "Dialog selection changed record order or eligibility");
    }
    for (const auto seen : seenRevisions) {
        const Record* expected = nullptr;
        for (const auto& record : records) {
            if (record.previewRevision > seen && record.content.preview &&
                !record.dismissed && record.state != RecordState::Running &&
                (!expected || record.previewRevision > expected->previewRevision)) expected = &record;
        }
        const auto selected = store.FindLatestPreview(seen);
        Require(selected.has_value() == (expected != nullptr), "Preview selection changed eligibility");
        if (expected) Require(selected->eventId == expected->id && selected->revision == expected->previewRevision,
            "Preview selection changed newest-unseen ordering");
    }
    Require(copies == 0, "Presentation selection copied feature callback payloads");
}

void CheckPresentationQueries() {
    using namespace Stack::Notifications;
    auto store = std::make_shared<NotificationStore>();
    auto owner = store->ForOwner(1, "Owner");
    int copies = 0;
    CheckPresentationSelection(*store, copies);
    NoticeSpec decision;
    decision.title = "First decision";
    decision.foreground = true;
    decision.customBody = CountCopies(copies);
    const auto first = owner.RequestDecision(decision);
    decision.title = "Second decision";
    const auto second = owner.RequestDecision(decision);
    Require(first && second, "Presentation fixture creation failed");
    CheckPresentationSelection(*store, copies);
    Require(store->FindRequestedDialog() == first && store->FindRequestedDialog(second) == second,
        "A retained eligible dialog must take priority over an earlier queued record");
    store->CloseDialog(first);
    CheckPresentationSelection(*store, copies);
    store->RequestCenter(first);
    store->Resolve(first);
    CheckPresentationSelection(*store, copies);
    store->Dismiss(second);
    CheckPresentationSelection(*store, copies);
    Require(!store->FindRequestedDialog() && store->FindLatestPreview(0)->eventId == first,
        "Resolved records remain preview-eligible while dismissed records do not");

    NoticeSpec work;
    work.title = "Running work";
    work.customBody = CountCopies(copies);
    const auto activity = owner.BeginActivity(std::move(work));
    Require(static_cast<bool>(activity), "Presentation activity fixture failed");
    CheckPresentationSelection(*store, copies);
    owner.CompleteActivity(activity, "Finished");
    CheckPresentationSelection(*store, copies);
    Require(store->FindLatestPreview(0)->eventId == activity.eventId, "Completion must publish the newest preview");
    NoticeSpec silent;
    silent.title = "No preview";
    silent.preview = false;
    silent.customBody = CountCopies(copies);
    Require(owner.Post(std::move(silent)) != 0, "Silent fixture creation failed");
    CheckPresentationSelection(*store, copies);
    store->InvalidateOwner(1);
    CheckPresentationSelection(*store, copies);
    store->Show(activity.eventId);
    CheckPresentationSelection(*store, copies);
    Require(store->FindLatestPreview(0)->eventId == activity.eventId,
        "An explicitly shown invalid-owner record must retain existing preview eligibility");
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
    CheckPresentationQueries();
    std::cout << "Notification attention, activity identities and presentation selection match record semantics with zero callback copies.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
