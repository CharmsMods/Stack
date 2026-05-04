#include "EditorRenderWorker.h"

#include "Editor/LayerRegistry.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <exception>
#include <memory>

EditorRenderWorker::EditorRenderWorker() = default;

EditorRenderWorker::~EditorRenderWorker() {
    Shutdown();
}

bool EditorRenderWorker::Initialize(GLFWwindow* sharedWindow) {
    if (m_Thread.joinable()) {
        return true;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    m_WorkerWindow = glfwCreateWindow(16, 16, "Stack Editor Render Worker", nullptr, sharedWindow);
    if (!m_WorkerWindow) {
        return false;
    }

    m_StopRequested = false;
    m_Thread = std::thread([this]() { ThreadMain(); });
    return true;
}

void EditorRenderWorker::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_StopRequested = true;
    }
    m_Cv.notify_all();
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
    if (m_WorkerWindow) {
        glfwDestroyWindow(m_WorkerWindow);
        m_WorkerWindow = nullptr;
    }
}

void EditorRenderWorker::Submit(Snapshot snapshot) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pending = std::move(snapshot);
        m_HasPending = true;
    }
    m_Cv.notify_one();
}

bool EditorRenderWorker::TryConsumeCompleted(Result& result) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Completed.empty()) {
        return false;
    }
    result = std::move(m_Completed.back());
    std::queue<Result> empty;
    m_Completed.swap(empty);
    return true;
}

void EditorRenderWorker::ThreadMain() {
    glfwMakeContextCurrent(m_WorkerWindow);
    LoadGLFunctions();

    while (true) {
        Snapshot snapshot;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Cv.wait(lock, [this]() {
                return m_StopRequested || m_HasPending;
            });
            if (m_StopRequested) {
                break;
            }
            snapshot = std::move(m_Pending);
            m_HasPending = false;
            m_Busy = true;
        }

        Result result = RenderSnapshot(snapshot);
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Completed.push(std::move(result));
            m_Busy = m_HasPending;
        }
    }

    glfwMakeContextCurrent(nullptr);
}

EditorRenderWorker::Result EditorRenderWorker::RenderSnapshot(const Snapshot& snapshot) {
    Result result;
    result.generation = snapshot.generation;

    if (!snapshot.outputConnected) {
        result.success = true;
        return result;
    }
    if (snapshot.sourcePixels.empty() || snapshot.width <= 0 || snapshot.height <= 0) {
        result.error = "No source image.";
        return result;
    }

    try {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(snapshot.sourcePixels.data(), snapshot.width, snapshot.height, snapshot.channels);

        if (!snapshot.graph.nodes.empty()) {
            pipeline.ExecuteGraph(snapshot.graph);
            result.pixels = pipeline.GetOutputPixels(result.width, result.height);
            result.success = !result.pixels.empty();
            if (!result.success) {
                result.error = "Render produced no pixels.";
            }
            return result;
        }

        std::vector<RenderLayerStep> steps;
        steps.reserve(snapshot.layerSteps.empty() ? snapshot.layers.size() : snapshot.layerSteps.size());

        const auto addLayerStep = [&](const nlohmann::json& layerJson, int maskNodeId) {
            const std::string type = layerJson.value("type", std::string());
            std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
            if (!layer) {
                return;
            }
            layer->InitializeGL();
            layer->Deserialize(layerJson);
            RenderLayerStep step;
            step.layer = std::move(layer);
            step.maskNodeId = maskNodeId;
            steps.push_back(std::move(step));
        };

        if (!snapshot.layerSteps.empty()) {
            for (const nlohmann::json& stepJson : snapshot.layerSteps) {
                if (!stepJson.is_object()) {
                    continue;
                }
                addLayerStep(stepJson.value("layer", nlohmann::json::object()), stepJson.value("maskNodeId", -1));
            }
        } else {
            for (const nlohmann::json& layerJson : snapshot.layers) {
                addLayerStep(layerJson, -1);
            }
        }

        pipeline.ExecuteMasked(steps, snapshot.masks);
        result.pixels = pipeline.GetOutputPixels(result.width, result.height);
        result.success = !result.pixels.empty();
        if (!result.success) {
            result.error = "Render produced no pixels.";
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    } catch (...) {
        result.error = "Unknown render worker failure.";
    }

    return result;
}
