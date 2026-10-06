#pragma once

#include "TaskState.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <queue>
#include <thread>
#include <vector>

namespace Async {

// A ticket describes execution only. Its disappearance is never a successful
// result. Operations report their actual outcome through their owning feature.
struct ActivityMetadata {
    std::uint64_t id = 0;
    std::uint64_t ownerId = 0;
    std::uint64_t ownerGeneration = 0;
    std::uint64_t operationId = 0;
    std::string label;
    bool maintenance = false;
};

class TaskSystem {
public:
    using Task = std::function<void()>;

    static TaskSystem& Get();

    void Initialize();
    void Shutdown();
    void RequestStopDiscardQueued();
    bool IsDrainedForShutdown() const;

    bool HasPendingWork();

    bool Submit(Task task);
    bool Submit(std::string activityLabel, Task task);
    bool Submit(ActivityMetadata activity, Task task);
    std::vector<std::string> ActivityLabels();
    std::vector<ActivityMetadata> Activities();
    static ActivityMetadata CurrentActivity();
    // Use sparingly for work directly initiated by the user. Background
    // maintenance remains FIFO so it cannot delay an interactive import.
    bool SubmitHighPriority(Task task);
    bool SubmitHighPriority(std::string activityLabel, Task task);
    bool SubmitHighPriority(ActivityMetadata activity, Task task);
    bool PostToMain(Task task);
    bool PostToMain(ActivityMetadata activity, Task task);
    void PumpMainThreadTasks(std::size_t maxTasks = 0);

private:
    TaskSystem() = default;
    ~TaskSystem();

    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;

    void WorkerLoop(bool interactiveOnly = false);
    Task TrackActivity(ActivityMetadata activity, Task task);
    std::size_t ResolveWorkerCount() const;

    std::atomic<bool> m_Initialized = false;
    std::mutex m_LifecycleMutex;
    bool m_StopRequested = false;
    std::mutex m_WorkMutex;
    std::condition_variable m_WorkCv;
    std::queue<Task> m_HighPriorityWorkQueue;
    std::queue<Task> m_WorkQueue;
    std::vector<std::thread> m_Workers;
    std::thread m_InteractiveWorker;
    std::atomic<std::size_t> m_ActiveWorkers = 0;

    std::mutex m_MainMutex;
    std::queue<Task> m_MainQueue;
    bool m_AcceptMainTasks = true;
    std::mutex m_ActivityMutex;
    std::vector<std::weak_ptr<const ActivityMetadata>> m_Activities;
    std::atomic<std::uint64_t> m_NextActivityId{1};
};

} // namespace Async
