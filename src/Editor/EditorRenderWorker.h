#pragma once

#include "ThirdParty/json.hpp"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/GLLoader.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;
class RenderPipeline;

class EditorRenderWorker {
public:
    struct SharedTextureResult {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        GLsync readyFence = nullptr;
    };

    struct CompositeOutputRequest {
        int outputNodeId = -1;
        int sourceNodeId = -1;
        std::vector<unsigned char> sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        std::uint64_t dirtyGeneration = 0;
        std::size_t chainFingerprint = 0;
    };

    struct CompositeOutputResult {
        int outputNodeId = -1;
        bool success = false;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::uint64_t dirtyGeneration = 0;
        std::size_t chainFingerprint = 0;
        std::string error;
    };

    struct PreviewRequest {
        int previewNodeId = -1;
        int sourceNodeId = -1;
        std::string sourceSocketId;
        bool maskInput = false;
        std::vector<unsigned char> sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        std::uint64_t dirtyGeneration = 0;
    };

    struct PreviewResult {
        int previewNodeId = -1;
        bool success = false;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::uint64_t dirtyGeneration = 0;
        std::string error;
    };

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
        std::vector<CompositeOutputRequest> compositeOutputs;
        std::vector<PreviewRequest> previews;
    };

    struct Result {
        std::uint64_t generation = 0;
        bool success = false;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        SharedTextureResult outputTexture;
        std::string error;
        std::vector<CompositeOutputResult> compositeOutputs;
        std::vector<PreviewResult> previews;
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
    std::unique_ptr<RenderPipeline> m_PersistentPipeline;
};
