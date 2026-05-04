#include "BundlerJobQueue.h"
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────

BundlerJobQueue::BundlerJobQueue() {}

BundlerJobQueue::~BundlerJobQueue() {
    Stop();
}

void BundlerJobQueue::Start() {
    if (m_WorkerThread.joinable()) return;
    m_Stopping.store(false);
    m_WorkerThread = std::thread(&BundlerJobQueue::WorkerLoop, this);
}

void BundlerJobQueue::Stop() {
    if (!m_WorkerThread.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(m_JobMutex);
        m_Stopping.store(true);
        m_JobCV.notify_all();
    }
    m_WorkerThread.join();
}

void BundlerJobQueue::PushJob(const Job& job) {
    std::lock_guard<std::mutex> lock(m_JobMutex);
    m_Jobs.push_back(job);
    m_JobCV.notify_one();
}

bool BundlerJobQueue::TryPopResult(JobResult& outResult) {
    std::lock_guard<std::mutex> lock(m_ResultMutex);
    if (m_Results.empty()) return false;
    outResult = std::move(m_Results.front());
    m_Results.pop_front();
    return true;
}

// ── Worker thread ────────────────────────────────────────────────────────────

void BundlerJobQueue::WorkerLoop() {
    while (!m_Stopping.load()) {
        Job currentJob;
        {
            std::unique_lock<std::mutex> lock(m_JobMutex);
            m_JobCV.wait(lock, [this] {
                return m_Stopping.load() || !m_Jobs.empty();
            });
            if (m_Stopping.load()) break;

            currentJob = std::move(m_Jobs.front());
            m_Jobs.pop_front();
        }

        // ── Scan job ────────────────────────────────────────────────────
        if (currentJob.kind == JobKind::Scan) {
            {
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                m_Results.push_back({currentJob.id, true,
                    "Scanning inputs...",
                    0.0f, false, nullptr, nullptr});
            }

            auto sr = std::make_shared<ScanResult>(ScanInputs(currentJob.inputPaths, currentJob.computedRoot));

            if (!sr->errorMessage.empty()) {
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                m_Results.push_back({currentJob.id, false,
                    "Scan failed: " + sr->errorMessage,
                    0.0f, true, nullptr, nullptr});
            } else {
                int total = static_cast<int>(sr->graph.GetNodes().size());
                {
                    std::lock_guard<std::mutex> rLock(m_ResultMutex);
                    m_Results.push_back({currentJob.id, true,
                        "Found " + std::to_string(total) + " files. Parsing references...",
                        0.5f, false, nullptr, nullptr});
                }

                ParseReferences(*sr);

                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                m_Results.push_back({currentJob.id, true,
                    "Scan + parse complete: " + std::to_string(total) + " files, "
                        + std::to_string(sr->refsFound) + " refs ("
                        + std::to_string(sr->refsResolved) + " resolved, "
                        + std::to_string(sr->refsUnresolved) + " unresolved).",
                    1.0f, true, sr, nullptr});
            }
        }
        // ── Minify / Build job ──────────────────────────────────────────
        else if (currentJob.kind == JobKind::Minify) {
            if (!currentJob.graphSnapshot || !currentJob.buildConfig || !currentJob.toolAdapter) {
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                m_Results.push_back({currentJob.id, false,
                    "Build job missing required data.",
                    0.0f, true, nullptr, nullptr});
                continue;
            }

            {
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                m_Results.push_back({currentJob.id, true,
                    "Starting per-file build...",
                    0.0f, false, nullptr, nullptr});
            }

            // Progress callback posts intermediate messages
            auto progressFn = [this, jobId = currentJob.id](int current, int total, const std::string& fileName) {
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                float prog = total > 0 ? static_cast<float>(current) / static_cast<float>(total) : 0.0f;
                m_Results.push_back({jobId, true,
                    "Processing (" + std::to_string(current) + "/" + std::to_string(total) + "): " + fileName,
                    prog, false, nullptr, nullptr});
            };

            std::shared_ptr<BuildResult> br;
            if (currentJob.buildConfig->mode == BuildMode::SingleHTML) {
                br = std::make_shared<BuildResult>(
                    RunSingleHTMLBuild(*currentJob.graphSnapshot,
                                       *currentJob.buildConfig,
                                       *currentJob.toolAdapter,
                                       progressFn));
            } else if (currentJob.buildConfig->mode == BuildMode::BundleByType) {
                br = std::make_shared<BuildResult>(
                    RunBundleByTypeBuild(*currentJob.graphSnapshot,
                                         *currentJob.buildConfig,
                                         *currentJob.toolAdapter,
                                         progressFn));
            } else {
                br = std::make_shared<BuildResult>(
                    RunPerFileBuild(*currentJob.graphSnapshot,
                                    *currentJob.buildConfig,
                                    *currentJob.toolAdapter,
                                    progressFn));
            }

            // Format size
            auto formatSize = [](uint64_t bytes) -> std::string {
                char buf[64];
                if (bytes < 1024)
                    snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
                else if (bytes < 1024 * 1024)
                    snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
                else
                    snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
                return buf;
            };

            std::string summary = "Build complete: "
                + std::to_string(br->filesProcessed) + " files ("
                + std::to_string(br->filesMinified) + " minified, "
                + std::to_string(br->filesCopied) + " copied";
            if (br->filesFailed > 0)
                summary += ", " + std::to_string(br->filesFailed) + " FAILED";
            summary += "). Size: " + formatSize(br->totalInputSize)
                     + " -> " + formatSize(br->totalOutputSize);

            if (br->totalInputSize > 0 && br->totalOutputSize < br->totalInputSize) {
                double savings = 100.0 * (1.0 - (double)br->totalOutputSize / (double)br->totalInputSize);
                char pctBuf[32];
                snprintf(pctBuf, sizeof(pctBuf), " (%.1f%% smaller)", savings);
                summary += pctBuf;
            }

            std::lock_guard<std::mutex> rLock(m_ResultMutex);
            m_Results.push_back({currentJob.id, br->success, summary,
                1.0f, true, nullptr, br});
        }
        // ── Simulated delay (Phase 1 legacy) ────────────────────────────
        else if (currentJob.kind == JobKind::SimulatedDelay) {
            {
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                m_Results.push_back({currentJob.id, true,
                    "Starting simulated job " + std::to_string(currentJob.id),
                    0.0f, false, nullptr, nullptr});
            }
            constexpr int steps = 10;
            for (int i = 1; i <= steps; ++i) {
                if (m_Stopping.load()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                float prog = static_cast<float>(i) / static_cast<float>(steps);
                m_Results.push_back({currentJob.id, true,
                    "Simulated step " + std::to_string(i) + "/" + std::to_string(steps),
                    prog, false, nullptr, nullptr});
            }
            if (!m_Stopping.load()) {
                std::lock_guard<std::mutex> rLock(m_ResultMutex);
                m_Results.push_back({currentJob.id, true,
                    "Simulated job complete.",
                    1.0f, true, nullptr, nullptr});
            }
        }
        else {
            std::lock_guard<std::mutex> rLock(m_ResultMutex);
            m_Results.push_back({currentJob.id, false,
                "Unknown job kind.", 0.0f, true, nullptr, nullptr});
        }
    }
}
