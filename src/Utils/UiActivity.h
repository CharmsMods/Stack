#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include "Notifications/NotificationModel.h"

namespace Stack::UiActivity {

// Collected from operation owners each frame, including inactive workspaces.
// Ordering is priority ordering. Completion of one owner cannot clear another.
struct Entry {
    std::uint64_t ownerId = 0;
    std::string ownerLabel;
    std::string key;
    std::string label;
    std::vector<std::string> details;
    std::optional<Notifications::Progress> progress;
    std::uint64_t id = 0;
    std::uint64_t operationId = 0;
    bool maintenance = false;
};

struct Snapshot {
    struct SuppressedLabel { std::uint64_t ownerId; std::string label; };
    struct WaitingOwner { std::uint64_t ownerId; bool waiting; };
    std::string primaryLabel;
    std::uint64_t primaryOwnerId = 0;
    std::vector<std::string> labels;
    std::vector<std::string> details;
    std::vector<std::string> suppressedWorkerLabels;
    std::vector<Entry> entries;
    std::vector<SuppressedLabel> ownerSuppressedLabels;
    std::vector<WaitingOwner> waitingOwners;
    bool awaitingInteractionRelease = false;

    void SetOwner(std::uint64_t id, std::string label, bool maintenance = false) {
        currentOwnerId = id;
        currentOwnerLabel = std::move(label);
        currentMaintenance = maintenance;
    }
    void SetAwaitingInteractionRelease(bool waiting) {
        const auto existing = std::find_if(waitingOwners.begin(), waitingOwners.end(),
            [&](const WaitingOwner& owner) { return owner.ownerId == currentOwnerId; });
        if (existing == waitingOwners.end()) waitingOwners.push_back({currentOwnerId, waiting});
        else existing->waiting = waiting;
        awaitingInteractionRelease = std::any_of(waitingOwners.begin(), waitingOwners.end(),
            [](const WaitingOwner& owner) { return owner.waiting; });
    }
    void AddOperation(bool active, std::string key, std::string label,
        std::uint64_t id = 0, std::uint64_t operationId = 0,
        std::optional<Notifications::Progress> progress = {}) {
        if (!active || label.empty()) return;
        if (std::find(labels.begin(), labels.end(), label) == labels.end()) labels.push_back(label);
        const auto existing = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
            return entry.ownerId == currentOwnerId && entry.key == key && entry.operationId == operationId;
        });
        if (existing == entries.end()) {
            entries.push_back({currentOwnerId, currentOwnerLabel, std::move(key), std::move(label),
                {}, std::move(progress), id, operationId, currentMaintenance});
        } else {
            existing->label = std::move(label);
            existing->progress = std::move(progress);
            existing->id = id;
        }
    }
    void AddOperationDetail(const std::string& key, std::string detail) {
        if (detail.empty()) return;
        AddDetail(detail);
        const auto existing = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
            return entry.ownerId == currentOwnerId && entry.key == key;
        });
        if (existing != entries.end() && std::find(existing->details.begin(), existing->details.end(), detail) == existing->details.end())
            existing->details.push_back(std::move(detail));
    }
    void Add(bool active, const char* label) {
        if (label) AddOperation(active, label, label);
    }
    void Add(bool active, const std::string& label) {
        Add(active, label.c_str());
    }
    void AddMaintenance(bool active, const char* label) {
        Add(active,label);
        if (active && label)
            for (auto& entry : entries)
                if (entry.ownerId == currentOwnerId && entry.key == label && !entry.operationId) entry.maintenance = true;
    }
    void SetPrimary(bool active, std::string label) {
        if (active && primaryLabel.empty()) {
            primaryLabel = std::move(label);
            primaryOwnerId = currentOwnerId;
        }
    }
    void AddDetail(std::string detail) {
        if (!detail.empty() &&
            std::find(details.begin(), details.end(), detail) == details.end()) {
            details.push_back(std::move(detail));
        }
    }
    void SuppressWorkerLabel(const char* label) {
        if (!label) return;
        if (std::none_of(ownerSuppressedLabels.begin(), ownerSuppressedLabels.end(), [&](const SuppressedLabel& item) {
            return item.ownerId == currentOwnerId && item.label == label;
        })) ownerSuppressedLabels.push_back({currentOwnerId, label});
        if (currentOwnerId != 0) return;
        if (std::find(suppressedWorkerLabels.begin(),
                suppressedWorkerLabels.end(), label) ==
            suppressedWorkerLabels.end()) {
            suppressedWorkerLabels.emplace_back(label);
        }
    }
    bool IsWorkerLabelSuppressed(const std::string& label) const {
        return std::find(suppressedWorkerLabels.begin(),
                   suppressedWorkerLabels.end(), label) !=
            suppressedWorkerLabels.end();
    }
    bool IsWorkerLabelSuppressed(const std::string& label, std::uint64_t ownerId) const {
        return std::any_of(ownerSuppressedLabels.begin(), ownerSuppressedLabels.end(),
            [&](const SuppressedLabel& item) { return item.ownerId == ownerId && item.label == label; });
    }
    void Merge(const Snapshot& other) {
        if (primaryLabel.empty()) {
            primaryLabel = other.primaryLabel;
            primaryOwnerId = other.primaryOwnerId;
        }
        for (const auto& entry : other.entries) {
            if (std::none_of(entries.begin(), entries.end(), [&](const Entry& existing) {
                return existing.ownerId == entry.ownerId && existing.key == entry.key && existing.operationId == entry.operationId;
            })) entries.push_back(entry);
        }
        for (const auto& label : other.labels)
            if (std::find(labels.begin(), labels.end(), label) == labels.end()) labels.push_back(label);
        for (const auto& detail : other.details) AddDetail(detail);
        for (const auto& label : other.suppressedWorkerLabels)
            if (std::find(suppressedWorkerLabels.begin(), suppressedWorkerLabels.end(), label) == suppressedWorkerLabels.end())
                suppressedWorkerLabels.push_back(label);
        for (const auto& item : other.ownerSuppressedLabels) {
            if (std::none_of(ownerSuppressedLabels.begin(), ownerSuppressedLabels.end(), [&](const SuppressedLabel& existing) {
                return existing.ownerId == item.ownerId && existing.label == item.label;
            })) ownerSuppressedLabels.push_back(item);
        }
        for (const auto& owner : other.waitingOwners) {
            const auto existing = std::find_if(waitingOwners.begin(), waitingOwners.end(),
                [&](const WaitingOwner& item) { return item.ownerId == owner.ownerId; });
            if (existing == waitingOwners.end()) waitingOwners.push_back(owner);
            else existing->waiting = existing->waiting || owner.waiting;
        }
        awaitingInteractionRelease = awaitingInteractionRelease || other.awaitingInteractionRelease;
    }
    bool Busy() const { return !primaryLabel.empty() || !entries.empty() || !labels.empty(); }

private:
    std::uint64_t currentOwnerId = 0;
    std::string currentOwnerLabel;
    bool currentMaintenance = false;
};

struct Presentation {
    float busyBlend = 0.0f;
    double lastBusyTime = -1.0;
    double waitingSince = -1.0;
    bool waiting = false;
    std::string label;

    void Update(const Snapshot& snapshot, double now, float deltaTime) {
        if (snapshot.Busy()) {
            lastBusyTime = now;
            label = !snapshot.primaryLabel.empty()
                ? snapshot.primaryLabel
                : !snapshot.labels.empty() ? snapshot.labels.front() : snapshot.entries.front().label;
        }
        if (snapshot.awaitingInteractionRelease && !snapshot.Busy()) {
            if (waitingSince < 0.0) waitingSince = now;
        } else {
            waitingSince = -1.0;
        }
        waiting = waitingSince >= 0.0 && now - waitingSince >= 0.2;
        // Keep the spinner through brief pauses in a held adjustment. Never
        // claim completion while refinement is waiting for the release.
        const bool holding = snapshot.awaitingInteractionRelease && !waiting;
        // Bridge short gaps between decode, upload and render stages.
        const bool busy = snapshot.Busy() ||
            holding || (lastBusyTime >= 0.0 && now - lastBusyTime < 0.18);
        const float step = std::clamp(deltaTime, 0.0f, 0.1f) / 0.12f;
        busyBlend = busy ? std::min(1.0f, busyBlend + step)
                         : std::max(0.0f, busyBlend - step);
        if (busyBlend == 0.0f) label.clear();
    }
};

} // namespace Stack::UiActivity
