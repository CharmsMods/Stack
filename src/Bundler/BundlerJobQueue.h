#pragma once
#include <string>
#include <cstdint>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include "BundlerScanner.h"
#include "BundlerBuild.h"
#include "BundlerToolAdapter.h"

// ── Job types ────────────────────────────────────────────────────────────────

enum class JobKind {
    Scan,
    Minify,
    SimulatedDelay
};

struct Job {
    uint32_t    id = 0;
    JobKind     kind = JobKind::SimulatedDelay;
    
    // For Scan jobs
    std::vector<std::string> inputPaths;
    std::string computedRoot;

    // For Minify jobs — the module passes these in
    std::shared_ptr<BundlerGraph>       graphSnapshot;
    std::shared_ptr<BuildConfig>        buildConfig;
    std::shared_ptr<BundlerToolAdapter> toolAdapter;
};

struct JobResult {
    uint32_t    jobId = 0;
    bool        success = true;
    std::string message;
    float       progress = 0.0f;
    bool        isComplete = false;

    /// Non-null when a Scan job completes successfully.
    std::shared_ptr<ScanResult> scanResult;

    /// Non-null when a Minify job completes.
    std::shared_ptr<BuildResult> buildResult;
};

// ── Thread-safe job queue ────────────────────────────────────────────────────

class BundlerJobQueue {
public:
    BundlerJobQueue();
    ~BundlerJobQueue();

    void PushJob(const Job& job);
    bool TryPopResult(JobResult& outResult);
    void Start();
    void Stop();

private:
    void WorkerLoop();

    std::mutex              m_JobMutex;
    std::condition_variable m_JobCV;
    std::deque<Job>         m_Jobs;

    std::mutex              m_ResultMutex;
    std::deque<JobResult>   m_Results;

    std::thread             m_WorkerThread;
    std::atomic<bool>       m_Stopping{false};
};
