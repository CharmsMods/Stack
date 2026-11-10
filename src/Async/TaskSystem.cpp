#include "TaskSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>

namespace Async {

namespace {
thread_local std::shared_ptr<const ActivityMetadata> currentActivity;

struct ActivityScope {
    std::shared_ptr<const ActivityMetadata> previous;
    explicit ActivityScope(std::shared_ptr<const ActivityMetadata> activity)
        : previous(std::move(currentActivity)) { currentActivity = std::move(activity); }
    ~ActivityScope() { currentActivity = std::move(previous); }
};
}

TaskSystem& TaskSystem::Get() {
    static TaskSystem instance;
    return instance;
}

TaskSystem::~TaskSystem() {
    Shutdown();
}

void TaskSystem::Initialize() {
    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    if (m_Initialized) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_WorkMutex);
        m_StopRequested = false;
    }
    const std::size_t workerCount = ResolveWorkerCount();
    try {
        m_Workers.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            m_Workers.emplace_back([this]() { WorkerLoop(false); });
        }
        // Keep one worker exclusively available for foreground actions such
        // as Add Slice. The normal pool is often busy with library scans,
        // thumbnails, or saves, and priority alone cannot pre-empt
        // already-running work.
        m_ThumbnailWorker = std::thread([this]() { WorkerLoop(false, true); });
        m_InteractiveWorker =
            std::thread([this]() { WorkerLoop(true); });
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_WorkMutex);
            m_StopRequested = true;
        }
        m_WorkCv.notify_all();
        for (std::thread& worker : m_Workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_Workers.clear();
        if (m_ThumbnailWorker.joinable()) m_ThumbnailWorker.join();
        if (m_InteractiveWorker.joinable()) {
            m_InteractiveWorker.join();
        }
        {
            std::lock_guard<std::mutex> lock(m_WorkMutex);
            m_StopRequested = false;
        }
        throw;
    }

    {
        std::lock_guard<std::mutex> lock(m_MainMutex);
        m_AcceptMainTasks = true;
    }
    m_Initialized = true;
}

void TaskSystem::Shutdown() {
    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    if (!m_Initialized) {
        return;
    }

    RequestStopDiscardQueued();

    for (auto& worker : m_Workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_Workers.clear();
    if (m_ThumbnailWorker.joinable()) m_ThumbnailWorker.join();
    if (m_InteractiveWorker.joinable()) {
        m_InteractiveWorker.join();
    }

    std::deque<ThumbnailTask> discardedThumbnails;
    std::queue<Task> discardedHighPriority;
    std::queue<Task> discardedWork;
    std::queue<Task> discardedMain;
    {
        std::lock_guard<std::mutex> workLock(m_WorkMutex);
        m_ThumbnailQueue.swap(discardedThumbnails);
        m_HighPriorityWorkQueue.swap(discardedHighPriority);
        m_WorkQueue.swap(discardedWork);
    }

    {
        std::lock_guard<std::mutex> mainLock(m_MainMutex);
        m_AcceptMainTasks = false;
        m_MainQueue.swap(discardedMain);
    }

    // Captured objects may post cleanup while being destroyed. Destroy them
    // without either queue lock, while submissions still see a stopped pool.
    while (!discardedHighPriority.empty()) discardedHighPriority.pop();
    while (!discardedWork.empty()) discardedWork.pop();
    while (!discardedMain.empty()) discardedMain.pop();
    m_Initialized = false;
}

void TaskSystem::RequestStopDiscardQueued() {
    if (!m_Initialized) {
        return;
    }

    std::deque<ThumbnailTask> discardedThumbnails;
    std::queue<Task> discardedHighPriority;
    std::queue<Task> discardedWork;
    {
        std::lock_guard<std::mutex> lock(m_WorkMutex);
        m_StopRequested = true;
        m_ThumbnailQueue.swap(discardedThumbnails);
        m_HighPriorityWorkQueue.swap(discardedHighPriority);
        m_WorkQueue.swap(discardedWork);
    }
    m_WorkCv.notify_all();
}

bool TaskSystem::IsDrainedForShutdown() const {
    if (!m_Initialized) {
        return true;
    }
    return m_ActiveWorkers.load() == 0;
}

bool TaskSystem::HasPendingWork() {
    std::scoped_lock lock(m_WorkMutex, m_MainMutex);
    return m_ActiveWorkers.load(std::memory_order_acquire) != 0 ||
        !m_WorkQueue.empty() || !m_HighPriorityWorkQueue.empty() || !m_ThumbnailQueue.empty() || !m_MainQueue.empty();
}

bool TaskSystem::Submit(Task task) {
    if (!task) {
        return false;
    }

    if (!m_Initialized) {
        try {
            Initialize();
        } catch (...) {
            return false;
        }
    }

    try {
        std::lock_guard<std::mutex> lock(m_WorkMutex);
        if (m_StopRequested) {
            return false;
        }
        m_WorkQueue.push(std::move(task));
    } catch (...) {
        return false;
    }
    // The dedicated interactive worker shares this condition variable but
    // cannot take ordinary work. notify_one can wake only that worker and
    // strand this queue until another submission. Wake eligible workers too.
    m_WorkCv.notify_all();
    return true;
}

bool TaskSystem::Submit(std::string activityLabel, Task task) {
    try {
        ActivityMetadata activity = CurrentActivity();
        activity.id = 0;
        activity.label = std::move(activityLabel);
        return Submit(std::move(activity), std::move(task));
    } catch (...) {
        return false;
    }
}

bool TaskSystem::SubmitHighPriority(std::string activityLabel, Task task) {
    try {
        ActivityMetadata activity = CurrentActivity();
        activity.id = 0;
        activity.label = std::move(activityLabel);
        return SubmitHighPriority(std::move(activity), std::move(task));
    } catch (...) {
        return false;
    }
}

bool TaskSystem::Submit(ActivityMetadata activity, Task task) {
    // Activity allocation is part of submission, so it must use the same
    // rejection path as queue allocation. Callers then release busy state.
    try {
        return Submit(TrackActivity(std::move(activity), std::move(task)));
    } catch (...) {
        return false;
    }
}

bool TaskSystem::SubmitHighPriority(ActivityMetadata activity, Task task) {
    try {
        return SubmitHighPriority(TrackActivity(std::move(activity), std::move(task)));
    } catch (...) {
        return false;
    }
}

ActivityMetadata TaskSystem::CurrentActivity() {
    return currentActivity ? *currentActivity : ActivityMetadata{};
}

TaskSystem::Task TaskSystem::TrackActivity(ActivityMetadata metadata, Task task) {
    if (!task) return {};
    if (metadata.id == 0) metadata.id = m_NextActivityId.fetch_add(1, std::memory_order_relaxed);
    if (metadata.operationId == 0) metadata.operationId = metadata.id | (std::uint64_t{1} << 63);
    const auto activity = std::make_shared<const ActivityMetadata>(std::move(metadata));
    {
        std::lock_guard<std::mutex> lock(m_ActivityMutex);
        m_Activities.erase(std::remove_if(m_Activities.begin(), m_Activities.end(),
            [](const auto& entry) { return entry.expired(); }), m_Activities.end());
        m_Activities.emplace_back(activity);
    }
    // The ticket lives through queueing and execution, including exceptions.
    // Discarding a queued task also removes its activity automatically.
    return [activity, task = std::move(task)] {
        ActivityScope scope(activity);
        task();
    };
}

std::vector<std::string> TaskSystem::ActivityLabels() {
    std::vector<std::string> labels;
    for (const auto& activity : Activities()) {
        if (!activity.label.empty() &&
            std::find(labels.begin(), labels.end(), activity.label) == labels.end()) {
            labels.push_back(activity.label);
        }
    }
    return labels;
}

std::vector<ActivityMetadata> TaskSystem::Activities() {
    std::lock_guard<std::mutex> lock(m_ActivityMutex);
    std::vector<ActivityMetadata> activities;
    for (auto it = m_Activities.begin(); it != m_Activities.end();) {
        if (const auto activity = it->lock()) {
            activities.push_back(*activity);
            ++it;
        } else {
            it = m_Activities.erase(it);
        }
    }
    return activities;
}

bool TaskSystem::SubmitHighPriority(Task task) {
    if (!task) {
        return false;
    }

    if (!m_Initialized) {
        try {
            Initialize();
        } catch (...) {
            return false;
        }
    }

    try {
        std::lock_guard<std::mutex> lock(m_WorkMutex);
        if (m_StopRequested) {
            return false;
        }
        m_HighPriorityWorkQueue.push(std::move(task));
    } catch (...) {
        return false;
    }
    m_WorkCv.notify_all();
    return true;
}

bool TaskSystem::SubmitThumbnail(ActivityMetadata activity, std::shared_ptr<std::atomic<bool>> canceled, Task task) {
    if (!task || !canceled) return false;
    // Destroy retired owner leases outside the scheduler lock.
    std::deque<ThumbnailTask> retired;
    try {
        if (!m_Initialized) Initialize();
        auto tracked = TrackActivity(std::move(activity), std::move(task));
        {
            std::lock_guard<std::mutex> lock(m_WorkMutex);
            if (m_StopRequested) return false;
            for (auto it=m_ThumbnailQueue.begin(); it!=m_ThumbnailQueue.end();) {
                if (it->canceled->load(std::memory_order_relaxed)) {
                    retired.push_back(std::move(*it));
                    it=m_ThumbnailQueue.erase(it);
                } else ++it;
            }
            if (m_ThumbnailQueue.size()>=24) return false;
            m_ThumbnailQueue.push_back({std::move(canceled),std::move(tracked)});
        }
        m_WorkCv.notify_all();
        return true;
    } catch (...) { return false; }
}

bool TaskSystem::PostToMain(Task task) {
    if (!task) {
        return false;
    }

    try {
        // Keep the worker's label through its UI completion, including any
        // further main-thread stages. A handoff is still the same operation.
        if (currentActivity) {
            task = [activity = currentActivity, task = std::move(task)] {
                ActivityScope scope(activity);
                task();
            };
        }
        std::lock_guard<std::mutex> lock(m_MainMutex);
        if (!m_AcceptMainTasks) {
            return false;
        }
        m_MainQueue.push(std::move(task));
    } catch (...) {
        return false;
    }
    return true;
}

bool TaskSystem::PostToMain(ActivityMetadata activity, Task task) {
    try {
        return PostToMain(TrackActivity(std::move(activity), std::move(task)));
    } catch (...) {
        return false;
    }
}

void TaskSystem::PumpMainThreadTasks(std::size_t maxTasks, double maxMilliseconds) {
    std::size_t processed = 0;
    const bool timeLimited = std::isfinite(maxMilliseconds) && maxMilliseconds > 0.0;
    const auto started = timeLimited ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
    if (timeLimited) {
        // A UI turn only adopts completions that were already queued. Work
        // posted by those callbacks (or their cleanup) waits for the next
        // turn instead of extending a chain while input is waiting. Keep
        // count-only and unlimited draining unchanged for other callers.
        std::lock_guard<std::mutex> lock(m_MainMutex);
        const auto queued = m_MainQueue.size();
        if (queued == 0) return;
        maxTasks = maxTasks > 0 ? std::min(maxTasks, queued) : queued;
    }

    while (true) {
        Task task;
        {
            std::lock_guard<std::mutex> lock(m_MainMutex);
            if (m_MainQueue.empty()) {
                break;
            }

            task = std::move(m_MainQueue.front());
            m_MainQueue.pop();
        }

        if (task) {
            const auto callbackStarted=m_MainTaskTimingObserver ? std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[TaskSystem] Main-thread task failed: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[TaskSystem] Main-thread task failed: unknown exception\n";
            }
            // Releasing image payloads and owner leases is part of adopting
            // a completion. Include it in both timing and the frame budget,
            // outside the queue lock so cleanup can safely post more work.
            task = {};
            if(m_MainTaskTimingObserver)m_MainTaskTimingObserver(
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-callbackStarted).count());
        }

        ++processed;
        if (maxTasks > 0 && processed >= maxTasks) {
            break;
        }
        if (timeLimited && std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count() >= maxMilliseconds) {
            break;
        }
    }
}

void TaskSystem::WorkerLoop(bool interactiveOnly, bool thumbnailOnly) {
    while (true) {
        Task task;
        bool thumbnail = false;
        {
            std::unique_lock<std::mutex> lock(m_WorkMutex);
            m_WorkCv.wait(lock, [this, interactiveOnly, thumbnailOnly]() {
                return m_StopRequested ||
                    (!thumbnailOnly && !m_HighPriorityWorkQueue.empty()) ||
                    (thumbnailOnly && !m_ThumbnailActive && !m_ThumbnailQueue.empty()) ||
                    (!interactiveOnly && !thumbnailOnly && !m_WorkQueue.empty());
            });

            if (m_StopRequested) {
                return;
            }

            if (!thumbnailOnly && !m_HighPriorityWorkQueue.empty()) {
                task = std::move(m_HighPriorityWorkQueue.front());
                m_HighPriorityWorkQueue.pop();
            } else if (thumbnailOnly && !m_ThumbnailActive && !m_ThumbnailQueue.empty()) {
                auto next=std::move(m_ThumbnailQueue.front());
                m_ThumbnailQueue.pop_front();
                task=[next=std::move(next)] {
                    if(!next.canceled->load(std::memory_order_relaxed))next.task();
                };
                thumbnail = m_ThumbnailActive = true;
            } else if (!interactiveOnly && !m_WorkQueue.empty()) {
                task = std::move(m_WorkQueue.front());
                m_WorkQueue.pop();
            }
            if (task) {
                // Publish the task as active before releasing the queue lock.
                // Otherwise shutdown can observe an empty queue and zero
                // active workers during the small pop-to-increment window.
                m_ActiveWorkers.fetch_add(1, std::memory_order_release);
            }
        }

        if (!task) {
            continue;
        }

        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[TaskSystem] Worker task failed: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[TaskSystem] Worker task failed: unknown exception\n";
        }
        // A task's captured objects can own cleanup work too. Do not report
        // the pool drained while those destructors still use application data.
        task = {};
        if (thumbnail) {
            std::lock_guard<std::mutex> lock(m_WorkMutex);
            m_ThumbnailActive = false;
            m_WorkCv.notify_all();
        }
        m_ActiveWorkers.fetch_sub(1, std::memory_order_release);
    }
}

std::size_t TaskSystem::ResolveWorkerCount() const {
    const unsigned int hardware = std::thread::hardware_concurrency();
    if (hardware <= 1) {
        return 2;
    }

    return static_cast<std::size_t>(std::max(2u, std::min(4u, hardware - 1)));
}

} // namespace Async
