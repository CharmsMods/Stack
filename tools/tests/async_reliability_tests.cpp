#include "Async/MainThreadRequestScope.h"
#include "Async/TaskSystem.h"
#include "Async/TaskGroup.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void TestConcurrentFirstSubmission() {
    auto& tasks = Async::TaskSystem::Get();
    tasks.Shutdown();
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::atomic<int> accepted = 0;
    std::atomic<int> completed = 0;
    std::vector<std::thread> submitters;
    for (int index = 0; index < 16; ++index) {
        submitters.emplace_back([&, index] {
            ready.wait();
            const auto work = [&] { ++completed; };
            if (index % 2 == 0 ? tasks.Submit(work) : tasks.SubmitHighPriority(work)) {
                ++accepted;
            }
        });
    }
    start.set_value();
    for (auto& submitter : submitters) submitter.join();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (completed != 16 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    tasks.Shutdown();
    Require(accepted == 16 && completed == 16,
        "Concurrent first submissions must share one initialized worker pool");
}

void TestDiscardedCapturesCanReenterQueues() {
    auto& tasks = Async::TaskSystem::Get();
    tasks.Initialize();
    std::promise<void> release;
    const auto released = release.get_future().share();
    std::mutex mutex;
    std::condition_variable started;
    int active = 0;
    const unsigned int hardware = std::thread::hardware_concurrency();
    const unsigned int workerCount = hardware <= 1 ? 2 : std::clamp(hardware - 1, 2u, 4u);
    for (unsigned int index = 0; index < workerCount; ++index) {
        Require(tasks.Submit([&] {
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++active;
            }
            started.notify_all();
            released.wait();
        }), "Blocking background work should be accepted");
    }
    bool allStarted;
    {
        std::unique_lock<std::mutex> lock(mutex);
        allStarted = started.wait_for(lock, 5s, [&] {
            return active == static_cast<int>(workerCount);
        });
    }
    if (!allStarted) {
        release.set_value();
        tasks.Shutdown();
        Require(false, "Background workers did not start");
    }

    bool workCaptureDestroyed = false;
    bool stoppedSubmissionRejected = false;
    auto workCapture = std::shared_ptr<int>(new int(0), [&](int* value) {
        delete value;
        stoppedSubmissionRejected = !tasks.Submit([] {});
        workCaptureDestroyed = true;
    });
    Require(tasks.Submit([capture = std::move(workCapture)] {}),
        "Capture cleanup test must remain queued behind the blocked workers");

    bool mainCaptureDestroyed = false;
    bool cleanupCompletionAccepted = true;
    auto mainCapture = std::shared_ptr<int>(new int(0), [&](int* value) {
        delete value;
        cleanupCompletionAccepted = tasks.PostToMain([] {});
        mainCaptureDestroyed = true;
    });
    Require(tasks.PostToMain([capture = std::move(mainCapture)] {}),
        "Main-thread capture cleanup test should queue successfully");

    tasks.RequestStopDiscardQueued();
    release.set_value();
    tasks.Shutdown();
    Require(workCaptureDestroyed && stoppedSubmissionRejected,
        "Discarded worker captures must be destroyed outside the work queue lock");
    Require(mainCaptureDestroyed && !cleanupCompletionAccepted && !tasks.HasPendingWork(),
        "Main cleanup must not deadlock or retain new callbacks after shutdown");
}

void TestShutdownWaitsForCaptureDestruction() {
    auto& tasks = Async::TaskSystem::Get();
    std::promise<void> destructorStarted;
    auto entered = destructorStarted.get_future();
    std::promise<void> release;
    const auto released = release.get_future().share();
    auto capture = std::shared_ptr<int>(new int(0), [&](int* value) {
        delete value;
        destructorStarted.set_value();
        released.wait();
    });
    Require(tasks.Submit([capture = std::move(capture)] {}),
        "Worker capture lifetime test should be accepted");
    const bool destructorEntered = entered.wait_for(5s) == std::future_status::ready;
    tasks.RequestStopDiscardQueued();
    const bool drainedBeforeRelease = tasks.IsDrainedForShutdown();
    const bool pendingBeforeRelease = tasks.HasPendingWork();
    release.set_value();
    tasks.Shutdown();
    Require(destructorEntered && !drainedBeforeRelease && pendingBeforeRelease,
        "Shutdown must count capture destruction as active work");
}

void TestStaleMainThreadCompletions() {
    auto& tasks = Async::TaskSystem::Get();
    tasks.Initialize();
    struct Receiver {
        Async::MainThreadRequestScope request;
        int appliedValue = 0;
    };
    auto receiver = std::make_unique<Receiver>();
    int completionCount = 0;
    const auto queueResult = [&](int value) {
        const auto ticket = receiver->request.Begin();
        Receiver* target = receiver.get();
        Require(tasks.PostToMain([ticket, target, value, &completionCount] {
            if (ticket.expired()) return;
            target->appliedValue = value;
            ++completionCount;
        }), "Request completion should be accepted");
    };

    queueResult(1);
    receiver->request.Cancel(); // The first load timed out.
    queueResult(2); // The next item is now in the same loading phase.
    tasks.PumpMainThreadTasks(1);
    Require(receiver->appliedValue == 0 && completionCount == 0,
        "A late result from a timed-out load must not apply to the next item");
    tasks.PumpMainThreadTasks();
    Require(receiver->appliedValue == 2 && completionCount == 1,
        "The current request should still complete");

    queueResult(3);
    queueResult(4);
    tasks.PumpMainThreadTasks();
    Require(receiver->appliedValue == 4 && completionCount == 2,
        "Replacing a request must invalidate an already-queued completion");

    queueResult(5);
    receiver.reset();
    tasks.PumpMainThreadTasks();
    tasks.Shutdown();
    Require(completionCount == 2,
        "Queued completions must not access a destroyed receiver");
}

void TestProjectTaskGroups() {
    auto& pool = Async::TaskSystem::Get();
    pool.Initialize();
    Async::TaskGroup a, b;
    int applied = 0;
    auto bLease = b.Retain();
    Require(a.Submit([&] {
        Require(a.PostToMain([&] { ++applied; }), "Project completion was rejected");
    }), "Project work was rejected");
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (applied == 0 && std::chrono::steady_clock::now() < deadline) {
        pool.PumpMainThreadTasks();
        std::this_thread::yield();
    }
    a.Stop();
    while (!a.IsIdle() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    Require(applied == 1 && a.IsIdle() && !b.IsIdle(),
        "A project's retirement must not wait for another project's callback");
    Require(!a.Submit([] {}), "Retired projects must reject new worker jobs");
    Require(a.PostToMain([&] { ++applied; }) && !a.IsIdle(),
        "Retired projects must retain cleanup completions until drained");
    pool.PumpMainThreadTasks();
    Require(a.IsIdle() && applied == 2, "Completion captures must drain with their owner");
    bLease.reset();
    Require(b.IsIdle(), "External completion leases must release their owner");
    Require(b.PostToMain([] {}) && !b.IsIdle(), "Queued callback must count as owner work");
    pool.Shutdown();
    Require(b.IsIdle(), "Discarded callbacks must not retain a retired project");
}

} // namespace

int main() {
    try {
        TestConcurrentFirstSubmission();
        TestDiscardedCapturesCanReenterQueues();
        TestShutdownWaitsForCaptureDestruction();
        TestStaleMainThreadCompletions();
        TestProjectTaskGroups();
        std::cout << "Async initialization, shutdown, and stale completion tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        Async::TaskSystem::Get().Shutdown();
        return 1;
    }
}
