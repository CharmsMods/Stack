#pragma once

#include "Raw/Bracketing/Processor.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string_view>

namespace Stack::AutoBracket {

// Only executing workers register. A foreground task must never wait for a
// queued automatic task that needs the foreground task's own pool slot.
class RunningAutomaticBurst {
public:
    explicit RunningAutomaticBurst(std::atomic<bool>& cancel) {
        std::lock_guard<std::mutex> lock(mutex_);
        id_ = ++generation_;
        workers_.emplace(id_, &cancel);
    }
    RunningAutomaticBurst(const RunningAutomaticBurst&) = delete;
    ~RunningAutomaticBurst() {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.erase(id_);
        drained_.notify_all();
    }

    static bool YieldForMemory(const std::function<bool()>& canceled,
                               const std::function<void()>& waiting) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (workers_.empty() || canceled()) return false;
        const auto last = workers_.rbegin()->first;
        for (const auto& worker : workers_) worker.second->store(true);
        lock.unlock();
        waiting();
        lock.lock();
        while (!workers_.empty() && workers_.begin()->first <= last) {
            if (canceled()) return false;
            drained_.wait_for(lock, std::chrono::milliseconds(50));
        }
        return !canceled();
    }
private:
    unsigned long long id_;
    inline static unsigned long long generation_ = 0;
    inline static std::mutex mutex_;
    inline static std::condition_variable drained_;
    inline static std::map<unsigned long long, std::atomic<bool>*> workers_;
};

// The engine and worker transport currently return diagnostic text. Match
// allocation/admission failures specifically, excluding invalid plans and
// ambiguous optional-runtime errors that merely mention memory.
inline bool IsBurstMemoryFailure(std::string_view message) {
    for (const auto reason : {"memory budget", "Less than 512 MiB is safely available",
            "exceed the processing memory allowance", "exceed the bracketing proxy budget",
            "workspace exceeds the processing budget", "needs at least 2 GiB of processing memory",
            " more memory; ",
            "could not be allocated", "bad allocation", "bad_alloc", "out of memory",
            "Out of memory", "Insufficient memory", "insufficient memory", "not enough memory", "Not enough memory", "GL_OUT_OF_MEMORY"}) {
        if (message.find(reason) != std::string_view::npos &&
            message.find("memory budget must be") == std::string_view::npos) return true;
    }
    return false;
}

template<class Attempt>
Raw::Bracketing::BracketingResult RunForegroundBurst(Attempt attempt, const std::function<bool()>& canceled,
    const std::function<void(const char*)>& report) {
    auto result = attempt();
    if (result.status == Raw::Bracketing::BracketingResult::Status::Failed &&
        IsBurstMemoryFailure(result.message) && RunningAutomaticBurst::YieldForMemory(canceled, [&] {
            report("Stopping automatic bracketing to free memory...");
        })) {
        result = {};
        report("Retrying this bracket...");
        if (!canceled()) return attempt();
    }
    return result;
}
}
