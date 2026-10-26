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
    std::cout << "Notification attention matches record semantics with zero callback copies.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
