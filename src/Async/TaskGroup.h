#pragma once

#include "Async/TaskSystem.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace Async {

// Counts one owner's queued work, running work, and main-thread completions.
// The owner must remain alive until IsIdle() after Stop(). The pool is shared;
// this object owns no threads and never waits for unrelated projects.
class TaskGroup {
public:
    using Task = TaskSystem::Task;

    TaskGroup() : m_State(std::make_shared<State>()) {}
    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;

    bool Submit(Task task) { return Submit(std::string{}, std::move(task)); }
    bool Submit(std::string label, Task task) {
        auto tracked = Track(std::move(task), false);
        return tracked && TaskSystem::Get().Submit(Metadata(std::move(label)), std::move(tracked));
    }
    bool Submit(ActivityMetadata activity, Task task) {
        auto tracked = Track(std::move(task), false);
        return tracked && TaskSystem::Get().Submit(std::move(activity), std::move(tracked));
    }
    bool SubmitHighPriority(Task task) { return SubmitHighPriority(std::string{}, std::move(task)); }
    bool SubmitHighPriority(std::string label, Task task) {
        auto tracked = Track(std::move(task), false);
        return tracked && TaskSystem::Get().SubmitHighPriority(Metadata(std::move(label)), std::move(tracked));
    }
    bool SubmitHighPriority(ActivityMetadata activity, Task task) {
        auto tracked = Track(std::move(task), false);
        return tracked && TaskSystem::Get().SubmitHighPriority(std::move(activity), std::move(tracked));
    }
    bool PostToMain(Task task) {
        auto tracked = Track(std::move(task), true);
        return tracked && TaskSystem::Get().PostToMain(std::move(tracked));
    }
    bool PostToMain(ActivityMetadata activity, Task task) {
        auto tracked = Track(std::move(task), true);
        return tracked && TaskSystem::Get().PostToMain(std::move(activity), std::move(tracked));
    }
    void SetActivityOwner(std::uint64_t id, std::uint64_t generation, bool maintenance = false) {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        m_State->activity.ownerId = id;
        m_State->activity.ownerGeneration = generation;
        m_State->activity.maintenance = maintenance;
    }
    void SetActivityOperation(std::uint64_t operationId) {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        m_State->activity.operationId = operationId;
    }

    // For callbacks owned by another service, such as the Queue inspection.
    // Capture the returned lease until that service destroys the callback.
    std::shared_ptr<void> Retain() { return std::make_shared<Work>(m_State, Task{}); }

    void Stop() {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        m_State->accepting = false;
    }
    bool IsIdle() const { return m_State->outstanding.load(std::memory_order_acquire) == 0; }

private:
    struct State {
        std::mutex mutex;
        bool accepting = true;
        std::atomic<std::size_t> outstanding{0};
        ActivityMetadata activity;
    };
    struct Work {
        std::shared_ptr<State> state;
        Task task;
        Work(std::shared_ptr<State> owner, Task work)
            : state(std::move(owner)), task(std::move(work)) {
            state->outstanding.fetch_add(1, std::memory_order_relaxed);
        }
        ~Work() {
            // Captures can themselves own resources or enqueue cleanup. They
            // must be destroyed before the owner can observe an idle group.
            task = {};
            state->outstanding.fetch_sub(1, std::memory_order_release);
        }
    };

    Task Track(Task task, bool completion) {
        if (!task) return {};
        std::lock_guard<std::mutex> lock(m_State->mutex);
        if (!completion && !m_State->accepting) return {};
        auto work = std::make_shared<Work>(m_State, std::move(task));
        return [work = std::move(work)] { work->task(); };
    }

    ActivityMetadata Metadata(std::string label) const {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        ActivityMetadata metadata = m_State->activity;
        const auto current = TaskSystem::CurrentActivity();
        if (current.ownerId == metadata.ownerId &&
            current.ownerGeneration == metadata.ownerGeneration && current.operationId != 0) {
            metadata.operationId = current.operationId;
        }
        metadata.label = std::move(label);
        return metadata;
    }

    std::shared_ptr<State> m_State;
};

} // namespace Async
