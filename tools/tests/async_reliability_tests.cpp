#include "Async/MainThreadRequestScope.h"
#include "Async/TaskSystem.h"
#include "Async/TaskGroup.h"
#include "App/UiFrameBudget.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <limits>
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

void TestMainThreadCompletionBudget() {
    auto& tasks = Async::TaskSystem::Get();
    tasks.Initialize();
    int completed = 0;
    std::vector<int> order;
    for (int expected = 1; expected <= 3; ++expected) {
        Require(tasks.PostToMain([&, expected] {
            ++completed;
            order.push_back(expected);
        }), "Budgeted completion should be accepted");
    }
    // A positive budget smaller than clock resolution still runs one callback.
    // This checks progress and deferred ownership without introducing a delay.
    tasks.PumpMainThreadTasks(4, std::numeric_limits<double>::min());
    Require(completed == 1, "A completion budget must make progress and leave the rest queued");
    tasks.PumpMainThreadTasks(1, 1.0);
    Require(completed == 2, "Task count must remain a limit when a time budget is supplied");
    tasks.PumpMainThreadTasks();
    Require(completed == 3, "Unbudgeted draining must keep its existing behavior");
    Require(order == std::vector<int>{1, 2, 3}, "Budgeted completions changed queue order");
    Require(Stack::App::MainThreadCompletionBudgetMs(400) == 0.25 &&
        Stack::App::MainThreadCompletionBudgetMs(144) <
            Stack::App::MainThreadCompletionBudgetMs(120) &&
        Stack::App::MainThreadCompletionBudgetMs(60) == 1.0 &&
        Stack::App::MainThreadCompletionBudgetMs(0) == 1.0,
        "Completion budgets must follow the current display without a 60 Hz cap");
    tasks.Shutdown();
}

void TestMainThreadTurnBoundary() {
    auto& tasks = Async::TaskSystem::Get();
    tasks.Initialize();
    int completed = 0;
    bool followupAccepted = false;
    const auto firstStage = [&] {
        ++completed;
        followupAccepted = tasks.PostToMain([&] { ++completed; });
    };

    Require(tasks.PostToMain(firstStage), "Staged completion should be accepted");
    tasks.PumpMainThreadTasks(0, 1000.0);
    Require(followupAccepted && completed == 1,
        "A budgeted UI turn must defer callbacks posted during that turn");
    tasks.PumpMainThreadTasks();
    Require(completed == 2, "Deferred followup must remain queued for the next turn");

    Require(tasks.PostToMain(firstStage), "Count-limited completion should be accepted");
    tasks.PumpMainThreadTasks(2);
    Require(followupAccepted && completed == 4,
        "Count-only pumping must retain its existing staged drain behavior");
    Require(tasks.PostToMain(firstStage), "Unbudgeted completion should be accepted");
    tasks.PumpMainThreadTasks();
    Require(followupAccepted && completed == 6,
        "Unlimited pumping must still drain callbacks posted during the same call");
    tasks.Shutdown();
}

void TestMainThreadCaptureTiming() {
    auto& tasks = Async::TaskSystem::Get();
    tasks.Initialize();
    static bool captureReleased = false;
    static bool observedAfterRelease = false;
    captureReleased = observedAfterRelease = false;
    bool cleanupAccepted = false;
    int cleanupCallbacks = 0;
    auto capture = std::shared_ptr<int>(new int(0), [&](int* value) {
        delete value;
        captureReleased = true;
        cleanupAccepted = tasks.PostToMain([&] { ++cleanupCallbacks; });
    });
    Require(tasks.PostToMain([capture = std::move(capture)] {}),
        "Callback capture should queue successfully");
    tasks.SetMainTaskTimingObserver([](double) { observedAfterRelease = captureReleased; });
    tasks.PumpMainThreadTasks(4, 1000.0);
    tasks.SetMainTaskTimingObserver(nullptr);
    Require(observedAfterRelease && cleanupAccepted && cleanupCallbacks == 0,
        "Callback timing must include capture release and safely defer its cleanup work");
    tasks.PumpMainThreadTasks();
    Require(cleanupCallbacks == 1, "Deferred capture cleanup must execute on the next turn");
    tasks.Shutdown();
}

void TestAdaptiveUiFrameBudget() {
    Stack::App::UiFrameBudget policy;
    const auto normal = policy.Limits(60, false);
    const auto interactive = policy.Limits(60, true);
    Require(interactive.totalMs < normal.totalMs && interactive.completionCount > 0 &&
        interactive.backgroundProjects > 0,
        "Interaction must reserve UI capacity without stopping completion or hidden-owner progress");

    policy.ObserveFrame(180.0, 14.0, 60);
    const auto afterHitch = policy.Limits(60, false);
    Require(afterHitch.totalMs < interactive.totalMs &&
        afterHitch.completionCount < normal.completionCount,
        "One severe hitch must constrain optional work on the next frame");
    for (int frame = 0; frame < 30; ++frame) {
        policy.ObserveFrame(22.0, 1.0, 60);
        Require(policy.Limits(60, false).totalMs <= afterHitch.totalMs,
            "Sustained overload must not age out the recovery allowance");
    }
    for (int frame = 0; frame < 10; ++frame) policy.ObserveFrame(16.0, 14.0, 60);
    Require(policy.Limits(60, false).totalMs < normal.totalMs,
        "A short healthy run must not immediately undo hitch protection");
    for (int frame = 0; frame < 20; ++frame) policy.ObserveFrame(16.0, 14.0, 60);
    Require(policy.Limits(60, false).totalMs == normal.totalMs,
        "Healthy frames must eventually restore the ordinary allowance");

    double previousAllowance = std::numeric_limits<double>::infinity();
    for (int refreshRate : {60, 144, 240, 400}) {
        Stack::App::UiFrameBudget vsyncPolicy;
        const double interval = 1000.0 / refreshRate;
        const auto initial = vsyncPolicy.Limits(refreshRate, false);
        for (int frame = 0; frame < 30; ++frame)
            vsyncPolicy.ObserveFrame(interval, interval - 0.5, refreshRate);
        const auto settled = vsyncPolicy.Limits(refreshRate, false);
        Require(settled.totalMs == initial.totalMs,
            "Ordinary display synchronization must not be mistaken for overload");
        Require(settled.totalMs < previousAllowance &&
            settled.totalMs <= interval * 0.25 && settled.completionMs > 0.0 &&
            settled.projectApplyMs > 0.0 && settled.libraryUploadMs > 0.0,
            "Higher-refresh displays must receive smaller, positive work allowances");
        previousAllowance = settled.totalMs;
        vsyncPolicy.ObserveFrame(100.0, 95.0, refreshRate);
        Require(vsyncPolicy.Limits(refreshRate, false).totalMs < initial.totalMs,
            "A stalled presentation must still trigger pressure protection");
    }

    Stack::App::UiFrameWorkBudget allowance(normal);
    allowance.Charge(normal.totalMs * 0.75);
    Require(allowance.RemainingMs() < normal.totalMs &&
        allowance.ProgressSliceMs(normal.projectApplyMs) <= allowance.RemainingMs(),
        "Later housekeeping must share the allowance already spent by earlier work");
    allowance.Charge(normal.totalMs);
    const double progressSlice = allowance.ProgressSliceMs(normal.completionMs);
    Require(allowance.RemainingMs() == 0.0 && std::isfinite(progressSlice) &&
        progressSlice > 0.0 && progressSlice < normal.completionMs,
        "An exhausted allowance must retain a small positive slice, never unlimited draining");
}

} // namespace

int main() {
    try {
        TestConcurrentFirstSubmission();
        TestDiscardedCapturesCanReenterQueues();
        TestShutdownWaitsForCaptureDestruction();
        TestStaleMainThreadCompletions();
        TestProjectTaskGroups();
        TestMainThreadCompletionBudget();
        TestMainThreadTurnBoundary();
        TestMainThreadCaptureTiming();
        TestAdaptiveUiFrameBudget();
        std::cout << "Async initialization, shutdown, and stale completion tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        Async::TaskSystem::Get().Shutdown();
        return 1;
    }
}
