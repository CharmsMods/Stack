#include "EditorRenderWorker.h"

#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
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
    m_PersistentPipeline = std::make_unique<RenderPipeline>();
    m_PersistentPipeline->Initialize();

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

    m_PersistentPipeline.reset();
    glfwMakeContextCurrent(nullptr);
}

EditorRenderWorker::Result EditorRenderWorker::RenderSnapshot(const Snapshot& snapshot) {
    Result result;
    result.generation = snapshot.generation;

    try {
        if (!m_PersistentPipeline) {
            m_PersistentPipeline = std::make_unique<RenderPipeline>();
            m_PersistentPipeline->Initialize();
        }
        RenderPipeline& pipeline = *m_PersistentPipeline;

        auto renderPreviewRequests = [&]() {
            if (snapshot.previews.empty()) {
                return;
            }

            result.previews.reserve(snapshot.previews.size());
            for (const PreviewRequest& request : snapshot.previews) {
                PreviewResult previewResult;
                previewResult.previewNodeId = request.previewNodeId;
                previewResult.dirtyGeneration = request.dirtyGeneration;

                const unsigned char* sourceData = request.sourcePixels.empty() ? nullptr : request.sourcePixels.data();
                int sourceWidth = request.width;
                int sourceHeight = request.height;
                int sourceChannels = request.channels;
                if ((!sourceData || sourceWidth <= 0 || sourceHeight <= 0) && request.sourceNodeId > 0) {
                    const auto sourceIt = std::find_if(
                        snapshot.graph.nodes.begin(),
                        snapshot.graph.nodes.end(),
                        [&request](const RenderGraphNode& node) { return node.nodeId == request.sourceNodeId; });
                    if (sourceIt != snapshot.graph.nodes.end() &&
                        sourceIt->kind == RenderGraphNodeKind::Image &&
                        !sourceIt->image.pixels.empty() &&
                        sourceIt->image.width > 0 &&
                        sourceIt->image.height > 0) {
                        sourceData = sourceIt->image.pixels.data();
                        sourceWidth = sourceIt->image.width;
                        sourceHeight = sourceIt->image.height;
                        sourceChannels = std::max(1, sourceIt->image.channels);
                    }
                }
                if (!sourceData || sourceWidth <= 0 || sourceHeight <= 0) {
                    previewResult.error = "No source image.";
                    result.previews.push_back(std::move(previewResult));
                    continue;
                }

                pipeline.LoadSourceFromPixels(sourceData, sourceWidth, sourceHeight, sourceChannels);
                RenderGraphSnapshot graph = snapshot.graph;
                if (request.maskInput) {
                    graph.outputNodeId = request.sourceNodeId;
                    graph.outputSocketId = request.sourceSocketId;
                } else {
                    const int syntheticOutputId = -100000 - request.previewNodeId;
                    RenderGraphNode outputNode;
                    outputNode.nodeId = syntheticOutputId;
                    outputNode.kind = RenderGraphNodeKind::Output;
                    graph.nodes.push_back(std::move(outputNode));
                    graph.links.push_back(RenderGraphLink{
                        request.sourceNodeId,
                        request.sourceSocketId,
                        syntheticOutputId,
                        EditorNodeGraph::kImageInputSocketId
                    });
                    graph.outputNodeId = syntheticOutputId;
                }
                pipeline.ExecuteGraph(graph);
                previewResult.pixels = pipeline.GetScopesPixels(previewResult.width, previewResult.height);
                previewResult.success = !previewResult.pixels.empty();
                if (!previewResult.success) {
                    previewResult.error = "Preview produced no pixels.";
                }
                result.previews.push_back(std::move(previewResult));
            }
        };

        if (!snapshot.compositeOutputs.empty()) {
            result.success = true;
            result.compositeOutputs.reserve(snapshot.compositeOutputs.size());
            for (const CompositeOutputRequest& request : snapshot.compositeOutputs) {
                CompositeOutputResult outputResult;
                outputResult.outputNodeId = request.outputNodeId;
                outputResult.dirtyGeneration = request.dirtyGeneration;
                outputResult.chainFingerprint = request.chainFingerprint;
                const unsigned char* sourceData = request.sourcePixels.empty() ? nullptr : request.sourcePixels.data();
                int sourceWidth = request.width;
                int sourceHeight = request.height;
                int sourceChannels = request.channels;
                if ((!sourceData || sourceWidth <= 0 || sourceHeight <= 0) && request.sourceNodeId > 0) {
                    const auto sourceIt = std::find_if(
                        snapshot.graph.nodes.begin(),
                        snapshot.graph.nodes.end(),
                        [&request](const RenderGraphNode& node) { return node.nodeId == request.sourceNodeId; });
                    if (sourceIt != snapshot.graph.nodes.end() &&
                        sourceIt->kind == RenderGraphNodeKind::Image &&
                        !sourceIt->image.pixels.empty() &&
                        sourceIt->image.width > 0 &&
                        sourceIt->image.height > 0) {
                        sourceData = sourceIt->image.pixels.data();
                        sourceWidth = sourceIt->image.width;
                        sourceHeight = sourceIt->image.height;
                        sourceChannels = std::max(1, sourceIt->image.channels);
                    }
                }
                if (!sourceData || sourceWidth <= 0 || sourceHeight <= 0) {
                    outputResult.error = "No source image.";
                    result.success = false;
                    result.compositeOutputs.push_back(std::move(outputResult));
                    continue;
                }

                pipeline.LoadSourceFromPixels(
                    sourceData,
                    sourceWidth,
                    sourceHeight,
                    sourceChannels);
                RenderGraphSnapshot graph = snapshot.graph;
                graph.outputNodeId = request.outputNodeId;
                pipeline.ExecuteGraph(graph);
                outputResult.pixels = pipeline.GetOutputPixels(outputResult.width, outputResult.height);
                outputResult.success = !outputResult.pixels.empty();
                if (!outputResult.success) {
                    outputResult.error = "Render produced no pixels.";
                    result.success = false;
                }
                result.compositeOutputs.push_back(std::move(outputResult));
            }
            renderPreviewRequests();
            return result;
        }

        if (!snapshot.outputConnected) {
            result.success = true;
            renderPreviewRequests();
            return result;
        }
        if (snapshot.sourcePixels.empty() || snapshot.width <= 0 || snapshot.height <= 0) {
            result.error = "No source image.";
            renderPreviewRequests();
            return result;
        }
        pipeline.LoadSourceFromPixels(snapshot.sourcePixels.data(), snapshot.width, snapshot.height, snapshot.channels);

        if (!snapshot.graph.nodes.empty()) {
            pipeline.ExecuteGraph(snapshot.graph);
            result.outputTexture.texture = pipeline.PublishSharedOutputTexture(result.outputTexture.width, result.outputTexture.height);
            if (result.outputTexture.texture != 0) {
                result.outputTexture.readyFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                glFlush();
            }
            result.success = result.outputTexture.texture != 0;
            if (!result.success) {
                result.error = "Render produced no pixels.";
            }
            renderPreviewRequests();
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
        result.outputTexture.texture = pipeline.PublishSharedOutputTexture(result.outputTexture.width, result.outputTexture.height);
        if (result.outputTexture.texture != 0) {
            result.outputTexture.readyFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
        }
        result.success = result.outputTexture.texture != 0;
        if (!result.success) {
            result.error = "Render produced no pixels.";
        }
        renderPreviewRequests();
    } catch (const std::exception& e) {
        result.error = e.what();
    } catch (...) {
        result.error = "Unknown render worker failure.";
    }

    return result;
}
