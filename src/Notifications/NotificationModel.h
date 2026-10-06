#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

namespace Stack::Notifications {

using OwnerId = std::uint64_t;
using OperationId = std::uint64_t;
using EventId = std::uint64_t;

enum class OwnerKind { Application, Project, Library, Queue };
enum class Severity { Info, Success, Warning, Error };
enum class RecordKind { Notice, Activity, Decision };
enum class RecordState { Active, Running, Succeeded, Failed, Cancelled, Resolved };
enum class Outcome { None, Success, Partial, Failure, Cancelled };
enum class Route { Activity, Center };
enum class DialogSize { Compact, Large };
enum class ActionState { Success, Pending, Failure };

struct Owner {
    OwnerId id = 0;
    std::uint64_t generation = 0;
    std::string label;
    OwnerKind kind = OwnerKind::Application;
};

struct ActionResult {
    ActionState state = ActionState::Success;
    std::string message;
    static ActionResult Success() { return {}; }
    static ActionResult Pending() { return {ActionState::Pending, {}}; }
    static ActionResult Failure(std::string message) {
        return {ActionState::Failure, std::move(message)};
    }
};

struct ActionSpec {
    std::string label;
    std::function<ActionResult()> invoke;
    // Lifetime and operation guards run on the main thread before invoke.
    std::function<bool()> canInvoke;
    bool safeCancel = false;
    bool destructive = false;
    bool defaultAction = false;
    bool resolveOnSuccess = true;
};

struct Item {
    std::string label;
    std::string value;
};

struct Progress {
    double completed = 0.0;
    double total = 0.0;
    std::string label;
    bool Measured() const { return total > 0.0; }
};

struct ImagePreview {
    std::uintptr_t textureId = 0;
    int width = 0;
    int height = 0;
    std::string caption;
    // The feature retains its renderer resource and validates its contents.
    std::shared_ptr<void> lifetime;
    std::function<bool()> canShow;
};

struct NoticeSpec {
    Severity severity = Severity::Info;
    Outcome outcome = Outcome::None;
    std::string title;
    std::string message;
    std::string details;
    std::string context;
    std::string dedupeKey;
    OperationId operationId = 0;
    std::vector<Item> items;
    std::optional<Progress> progress;
    std::optional<ImagePreview> imagePreview;
    std::vector<ActionSpec> actions;
    // Specialized feature content is called only by a main-thread presenter.
    std::function<void()> customBody;
    Route route = Route::Activity;
    DialogSize dialogSize = DialogSize::Compact;
    bool preview = true;
    bool maintenance = false;
    bool foreground = false;
};

struct ActivityHandle {
    EventId eventId = 0;
    OperationId operationId = 0;
    explicit operator bool() const { return eventId != 0; }
};

struct Record {
    EventId id = 0;
    Owner owner;
    OperationId operationId = 0;
    RecordKind kind = RecordKind::Notice;
    RecordState state = RecordState::Active;
    Outcome outcome = Outcome::None;
    NoticeSpec content;
    double createdAt = 0.0;
    double updatedAt = 0.0;
    std::uint64_t revision = 0;
    std::uint64_t previewRevision = 0;
    std::uint32_t occurrences = 1;
    bool dismissed = false;
    bool ownerValid = true;
    bool centerRequested = false;
    bool actionPending = false;
    std::size_t pendingAction = 0;
    std::string actionError;

    bool NeedsAttention() const {
        return ownerValid && (state == RecordState::Active || state == RecordState::Failed) &&
            (kind == RecordKind::Decision || content.severity == Severity::Warning ||
             content.severity == Severity::Error || content.route == Route::Center);
    }
    bool Terminal() const {
        if (!ownerValid && state != RecordState::Running) return true;
        return state == RecordState::Succeeded || state == RecordState::Cancelled ||
            state == RecordState::Resolved || (state == RecordState::Active &&
            kind == RecordKind::Notice && content.severity != Severity::Warning &&
            content.severity != Severity::Error && content.route != Route::Center);
    }
};

} // namespace Stack::Notifications
