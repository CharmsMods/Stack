#pragma once

#include "ThirdParty/json.hpp"
#include "Renderer/MaskRenderTypes.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;

class EditorRenderWorker {
public:
    struct Snapshot {
        std::uint64_t generation = 0;
        bool outputConnected = false;
        std::vector<unsigned char> sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        std::vector<nlohmann::json> layers;
        std::vector<nlohmann::json> layerSteps;
        std::vector<RenderMaskSource> masks;
        RenderGraphSnapshot graph;
    };

    struct Result {
        std::uint64_t generation = 0;
        bool success = false;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::string error;
    };

    EditorRenderWorker();
    ~EditorRenderWorker();

    bool Initialize(GLFWwindow* sharedWindow);
    void Shutdown();
    void Submit(Snapshot snapshot);
    bool TryConsumeCompleted(Result& result);
    bool IsBusy() const { return m_Busy.load(); }

private:
    void ThreadMain();
    Result RenderSnapshot(const Snapshot& snapshot);

    GLFWwindow* m_WorkerWindow = nullptr;
    std::thread m_Thread;
    mutable std::mutex m_Mutex;
    std::condition_variable m_Cv;
    bool m_StopRequested = false;
    bool m_HasPending = false;
    Snapshot m_Pending;
    std::queue<Result> m_Completed;
    std::atomic<bool> m_Busy = false;
};
