#pragma once

#include "TaskState.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Async {

class TaskSystem {
public:
    using Task = std::function<void()>;

    static TaskSystem& Get();

    void Initialize();
    void Shutdown();
    void RequestStopDiscardQueued();
    bool IsDrainedForShutdown() const;

    bool Submit(Task task);
    // Use sparingly for work directly initiated by the user. Background
    // maintenance remains FIFO so it cannot delay an interactive import.
    bool SubmitHighPriority(Task task);
    bool PostToMain(Task task);
    void PumpMainThreadTasks(std::size_t maxTasks = 0);

private:
    TaskSystem() = default;
    ~TaskSystem();

    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;

    void WorkerLoop(bool interactiveOnly = false);
    std::size_t ResolveWorkerCount() const;

    bool m_Initialized = false;
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
};

} // namespace Async
