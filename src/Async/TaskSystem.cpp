#include "TaskSystem.h"

#include <algorithm>
#include <exception>

namespace Async {

TaskSystem& TaskSystem::Get() {
    static TaskSystem instance;
    return instance;
}

TaskSystem::~TaskSystem() {
    Shutdown();
}

void TaskSystem::Initialize() {
    if (m_Initialized) {
        return;
    }

    m_StopRequested = false;
    const std::size_t workerCount = ResolveWorkerCount();
    m_Workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        m_Workers.emplace_back([this]() { WorkerLoop(); });
    }

    m_Initialized = true;
}

void TaskSystem::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_WorkMutex);
        m_StopRequested = true;
    }
    m_WorkCv.notify_all();

    for (auto& worker : m_Workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_Workers.clear();

    {
        std::lock_guard<std::mutex> workLock(m_WorkMutex);
        std::queue<Task> empty;
        m_WorkQueue.swap(empty);
    }

    {
        std::lock_guard<std::mutex> mainLock(m_MainMutex);
        std::queue<Task> empty;
        m_MainQueue.swap(empty);
    }

    m_StopRequested = false;
    m_Initialized = false;
}

void TaskSystem::Submit(Task task) {
    if (!task) {
        return;
    }

    if (!m_Initialized) {
        Initialize();
    }

    {
        std::lock_guard<std::mutex> lock(m_WorkMutex);
        m_WorkQueue.push(std::move(task));
    }
    m_WorkCv.notify_one();
}

void TaskSystem::PostToMain(Task task) {
    if (!task) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_MainMutex);
    m_MainQueue.push(std::move(task));
}

void TaskSystem::PumpMainThreadTasks(std::size_t maxTasks) {
    std::size_t processed = 0;

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
            task();
        }

        ++processed;
        if (maxTasks > 0 && processed >= maxTasks) {
            break;
        }
    }
}

void TaskSystem::WorkerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_WorkMutex);
            m_WorkCv.wait(lock, [this]() {
                return m_StopRequested || !m_WorkQueue.empty();
            });

            if (m_StopRequested && m_WorkQueue.empty()) {
                return;
            }

            task = std::move(m_WorkQueue.front());
            m_WorkQueue.pop();
        }

        if (!task) {
            continue;
        }

        try {
            task();
        } catch (const std::exception&) {
            // Keep the worker alive; failures are reported by callers.
        } catch (...) {
            // Keep the worker alive; failures are reported by callers.
        }
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
