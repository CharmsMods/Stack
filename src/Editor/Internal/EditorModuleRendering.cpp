#include "Editor/EditorModule.h"

#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <imgui.h>

namespace {

struct CroppedRgbaImage {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

bool WaitForSharedTextureFence(GLsync& fence) {
    if (!fence) {
        return true;
    }
    const GLenum waitResult = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ULL);
    glDeleteSync(fence);
    fence = nullptr;
    return waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED;
}

bool IsMaskOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::MaskGenerator ||
           kind == EditorNodeGraph::NodeKind::MaskUtility ||
           kind == EditorNodeGraph::NodeKind::ImageToMask;
}

bool IsImageOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::Image ||
           kind == EditorNodeGraph::NodeKind::Layer ||
           kind == EditorNodeGraph::NodeKind::ImageGenerator ||
           kind == EditorNodeGraph::NodeKind::Mix;
}

CroppedRgbaImage CropToAlphaBounds(const std::vector<unsigned char>& rgbaPixels, int width, int height) {
    CroppedRgbaImage result;
    if (rgbaPixels.empty() || width <= 0 || height <= 0) {
        return result;
    }

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t alphaIndex = static_cast<size_t>((y * width + x) * 4 + 3);
            if (alphaIndex >= rgbaPixels.size() || rgbaPixels[alphaIndex] == 0) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        result.pixels = rgbaPixels;
        result.width = width;
        result.height = height;
        return result;
    }

    result.width = std::max(1, maxX - minX + 1);
    result.height = std::max(1, maxY - minY + 1);
    result.pixels.assign(static_cast<size_t>(result.width * result.height * 4), 0);
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const size_t srcIndex = static_cast<size_t>(((minY + y) * width + (minX + x)) * 4);
            const size_t dstIndex = static_cast<size_t>((y * result.width + x) * 4);
            if (srcIndex + 3 >= rgbaPixels.size() || dstIndex + 3 >= result.pixels.size()) {
                continue;
            }
            result.pixels[dstIndex + 0] = rgbaPixels[srcIndex + 0];
            result.pixels[dstIndex + 1] = rgbaPixels[srcIndex + 1];
            result.pixels[dstIndex + 2] = rgbaPixels[srcIndex + 2];
            result.pixels[dstIndex + 3] = rgbaPixels[srcIndex + 3];
        }
    }
    return result;
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width * height * 4), 0);
}

} // namespace

std::vector<unsigned char> EditorModule::GetScopePixelsForNode(int nodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return {};
    }

    if (!IsImageOutputNode(node->kind) && !IsMaskOutputNode(node->kind)) {
        return {};
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (node->kind == EditorNodeGraph::NodeKind::Image && !node->image.pixels.empty() &&
        node->image.width > 0 && node->image.height > 0) {
        sourcePixels = node->image.pixels;
        sourceW = node->image.width;
        sourceH = node->image.height;
        sourceCh = std::max(1, node->image.channels);
    } else {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& graphNode : m_NodeGraph.GetNodes()) {
            if (graphNode.kind == EditorNodeGraph::NodeKind::Image && !graphNode.image.pixels.empty() &&
                graphNode.image.width > 0 && graphNode.image.height > 0) {
                sourcePixels = graphNode.image.pixels;
                sourceW = graphNode.image.width;
                sourceH = graphNode.image.height;
                sourceCh = std::max(1, graphNode.image.channels);
                break;
            }
        }
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    if (IsMaskOutputNode(node->kind) || node->kind == EditorNodeGraph::NodeKind::Output) {
        snapshot.outputNodeId = nodeId;
    } else {
        const int syntheticOutputId = -200000 - nodeId;
        RenderGraphNode outputNode;
        outputNode.nodeId = syntheticOutputId;
        outputNode.kind = RenderGraphNodeKind::Output;
        snapshot.nodes.push_back(std::move(outputNode));
        snapshot.links.push_back(RenderGraphLink{
            nodeId,
            EditorNodeGraph::kImageOutputSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
        snapshot.outputNodeId = syntheticOutputId;
    }

    RenderPipeline scopePipeline;
    scopePipeline.Initialize();
    scopePipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, sourceCh);
    scopePipeline.ExecuteGraph(snapshot);
    return scopePipeline.GetScopesPixels(outW, outH);
}

std::vector<unsigned char> EditorModule::GetPreviewPixelsForNode(int nodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;

    const EditorNodeGraph::Node* previewNode = m_NodeGraph.FindNode(nodeId);
    if (!previewNode || previewNode->kind != EditorNodeGraph::NodeKind::Preview) {
        return {};
    }

    const EditorNodeGraph::Link* input = m_NodeGraph.FindAnyInputLink(nodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return {};
    }

    EditorNodeGraph::SocketDefinition sourceSocket;
    if (!m_NodeGraph.FindSocket(input->fromNodeId, input->fromSocketId, &sourceSocket)) {
        return {};
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(input->fromNodeId);
    if (!sourceNode) {
        return {};
    }

    if (sourceSocket.type != EditorNodeGraph::SocketType::Image &&
        sourceSocket.type != EditorNodeGraph::SocketType::Mask) {
        return {};
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (sourceNode->kind == EditorNodeGraph::NodeKind::Image && !sourceNode->image.pixels.empty() &&
        sourceNode->image.width > 0 && sourceNode->image.height > 0) {
        sourcePixels = sourceNode->image.pixels;
        sourceW = sourceNode->image.width;
        sourceH = sourceNode->image.height;
        sourceCh = std::max(1, sourceNode->image.channels);
    } else {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Image && !node.image.pixels.empty() &&
                node.image.width > 0 && node.image.height > 0) {
                sourcePixels = node.image.pixels;
                sourceW = node.image.width;
                sourceH = node.image.height;
                sourceCh = std::max(1, node.image.channels);
                break;
            }
        }
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
        for (size_t i = 3; i < sourcePixels.size(); i += 4) {
            sourcePixels[i] = 255;
        }
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int syntheticOutputId = -100000 - nodeId;
    if (sourceSocket.type == EditorNodeGraph::SocketType::Mask) {
        snapshot.outputNodeId = input->fromNodeId;
    } else {
        RenderGraphNode outputNode;
        outputNode.nodeId = syntheticOutputId;
        outputNode.kind = RenderGraphNodeKind::Output;
        snapshot.nodes.push_back(std::move(outputNode));
        snapshot.links.push_back(RenderGraphLink{
            input->fromNodeId,
            input->fromSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
        snapshot.outputNodeId = syntheticOutputId;
    }

    RenderPipeline previewPipeline;
    previewPipeline.Initialize();
    previewPipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    previewPipeline.ExecuteGraph(snapshot);
    return previewPipeline.GetScopesPixels(outW, outH);
}

void EditorModule::RenderGraphScopeNode(EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId) {
    m_Scopes.RenderScopeNode(this, scopeKind, sourceNodeId);
}

void EditorModule::MarkRenderDirty(int touchedNodeId) {
    const bool wasAlreadyDirty = m_RenderDirty;
    m_RenderDirty = true;
    ++m_RenderRevision;
    if (!wasAlreadyDirty) {
        m_LastRenderDirtyTime = ImGui::GetTime();
    }
    if (touchedNodeId > 0) {
        MarkDownstreamNodesDirty(touchedNodeId);
        MarkCompositeOutputsDirty(m_NodeGraph.GetDownstreamOutputNodeIds(touchedNodeId));
    } else {
        MarkAllRenderNodesDirty();
        MarkCompositeOutputsDirty(m_NodeGraph.GetConnectedOutputNodeIds());
        m_PreviewDisplayedRevisions.clear();
        m_PreviewRequestedGenerations.clear();
        m_PreviewCompletedGenerations.clear();
        m_ScopeDisplayedRevisions.clear();
    }
}

EditorRenderWorker::Snapshot EditorModule::BuildRenderSnapshot(std::uint64_t generation) const {
    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.outputConnected = GetViewportMode() == ViewportMode::SingleOutputPreview && m_NodeGraph.IsOutputConnected();
    if (snapshot.outputConnected) {
        snapshot.sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        snapshot.width = m_Pipeline.GetCanvasWidth();
        snapshot.height = m_Pipeline.GetCanvasHeight();
        snapshot.channels = m_Pipeline.GetSourceChannels();
    }
    snapshot.graph = BuildGraphSnapshot();
    snapshot.masks = BuildGraphRenderMasks();
    for (const RenderLayerStep& step : BuildGraphRenderSteps()) {
        if (step.layer) {
            nlohmann::json item = nlohmann::json::object();
            item["layer"] = step.layer->Serialize();
            item["maskNodeId"] = step.maskNodeId;
            snapshot.layerSteps.push_back(std::move(item));
            snapshot.layers.push_back(step.layer->Serialize());
        }
    }
    return snapshot;
}

void EditorModule::RefreshCompletedChainCacheIfNeeded() const {
    const std::uint64_t structureRevision = m_NodeGraph.GetStructureRevision();
    if (m_CachedCompletedChainsStructureRevision == structureRevision) {
        return;
    }

    m_CachedCompletedChains.clear();
    for (const EditorNodeGraph::CompletedChainInfo& chain : m_NodeGraph.GetCompletedChains()) {
        CachedCompositeChainState state;
        state.info = chain;
        m_CachedCompletedChains.push_back(std::move(state));
    }
    m_CachedConnectedOutputCount = static_cast<int>(m_CachedCompletedChains.size());
    m_CachedCompletedChainsStructureRevision = structureRevision;
}

void EditorModule::RefreshCompositeMetadataCacheIfNeeded() {
    RefreshCompletedChainCacheIfNeeded();
    const std::uint64_t structureRevision = m_NodeGraph.GetStructureRevision();
    if (m_CachedCompositeMetadataStructureRevision == structureRevision &&
        m_CachedCompositeMetadataRenderRevision == m_RenderRevision &&
        m_CachedCompositeFingerprints.size() == m_CachedCompletedChains.size() &&
        m_CachedCompositeLabels.size() == m_CachedCompletedChains.size()) {
        return;
    }

    std::unordered_map<int, std::size_t> previousFingerprints = m_CachedCompositeFingerprints;
    m_CachedCompositeFingerprints.clear();
    m_CachedCompositeLabels.clear();
    for (CachedCompositeChainState& chain : m_CachedCompletedChains) {
        chain.fingerprint = BuildCompositeChainFingerprint(chain.info);
        chain.label = BuildCompositeChainLabel(chain.info);
        m_CachedCompositeFingerprints[chain.info.outputNodeId] = chain.fingerprint;
        m_CachedCompositeLabels[chain.info.outputNodeId] = chain.label;
        const auto previousIt = previousFingerprints.find(chain.info.outputNodeId);
        if (previousIt == previousFingerprints.end() || previousIt->second != chain.fingerprint) {
            MarkCompositeOutputsDirty(std::vector<int>{ chain.info.outputNodeId });
        }
    }

    PruneCompositeDirtyState();
    m_CachedCompositeMetadataStructureRevision = structureRevision;
    m_CachedCompositeMetadataRenderRevision = m_RenderRevision;
}

void EditorModule::MarkDownstreamNodesDirty(int touchedNodeId) {
    for (int nodeId : m_NodeGraph.GetDownstreamRenderNodeIds(touchedNodeId)) {
        m_NodeDirtyGenerations[nodeId] = ++m_NodeDirtyGenerationCounter;
    }
}

void EditorModule::MarkAllRenderNodesDirty() {
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (m_NodeGraph.IsRenderChainNode(node)) {
            m_NodeDirtyGenerations[node.id] = ++m_NodeDirtyGenerationCounter;
        }
    }
}

void EditorModule::MarkCompositeOutputsDirty(const std::vector<int>& outputNodeIds) {
    for (const int outputNodeId : outputNodeIds) {
        if (outputNodeId > 0) {
            m_CompositeOutputDirtyGenerations[outputNodeId] = ++m_CompositeDirtyGenerationCounter;
        }
    }
}

void EditorModule::PruneCompositeDirtyState() {
    std::unordered_set<int> activeOutputIds;
    activeOutputIds.reserve(m_CachedCompletedChains.size());
    for (const CachedCompositeChainState& chain : m_CachedCompletedChains) {
        activeOutputIds.insert(chain.info.outputNodeId);
    }

    auto pruneMap = [&activeOutputIds](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (!activeOutputIds.count(it->first)) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };
    pruneMap(m_CompositeOutputDirtyGenerations);
    pruneMap(m_CompositeOutputRequestedGenerations);
    pruneMap(m_CompositeOutputCompletedGenerations);
}

std::uint64_t EditorModule::GetNodeDirtyGeneration(int nodeId) const {
    const auto it = m_NodeDirtyGenerations.find(nodeId);
    return it != m_NodeDirtyGenerations.end() ? it->second : 0;
}

bool EditorModule::CanRefreshPreviewLikeNodes() const {
    return !m_RenderDirty && !m_RenderPending && !m_RenderWorker.IsBusy();
}

bool EditorModule::HasPendingPreviewRefreshes() const {
    if (!CanRefreshPreviewLikeNodes()) {
        return true;
    }

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Preview) {
            continue;
        }
        const EditorNodeGraph::Link* input =
            m_NodeGraph.FindAnyInputLink(node.id, EditorNodeGraph::kPreviewInputSocketId);
        if (!input) {
            continue;
        }
        const std::uint64_t desiredRevision = GetNodeDirtyGeneration(input->fromNodeId);
        const auto displayedIt = m_PreviewDisplayedRevisions.find(node.id);
        const std::uint64_t displayedRevision = displayedIt != m_PreviewDisplayedRevisions.end() ? displayedIt->second : 0;
        if (desiredRevision != displayedRevision) {
            return true;
        }
    }
    return false;
}

std::vector<EditorRenderWorker::CompositeOutputRequest> EditorModule::BuildCompositeOutputRequests() {
    RefreshCompositeMetadataCacheIfNeeded();

    std::vector<EditorRenderWorker::CompositeOutputRequest> requests;
    requests.reserve(m_CachedCompletedChains.size());
    for (const CachedCompositeChainState& chainState : m_CachedCompletedChains) {
        CompositeSceneItem* item = FindCompositeSceneItem(chainState.info.outputNodeId);
        if (!item) {
            continue;
        }

        const std::uint64_t dirtyGeneration =
            m_CompositeOutputDirtyGenerations.count(chainState.info.outputNodeId)
                ? m_CompositeOutputDirtyGenerations[chainState.info.outputNodeId]
                : 0;
        const std::uint64_t completedGeneration =
            m_CompositeOutputCompletedGenerations.count(chainState.info.outputNodeId)
                ? m_CompositeOutputCompletedGenerations[chainState.info.outputNodeId]
                : 0;
        const std::uint64_t requestedGeneration =
            m_CompositeOutputRequestedGenerations.count(chainState.info.outputNodeId)
                ? m_CompositeOutputRequestedGenerations[chainState.info.outputNodeId]
                : 0;
        const bool needsTextureRefresh =
            item->texture == 0 ||
            completedGeneration < dirtyGeneration ||
            item->cachedChainFingerprint != chainState.fingerprint;
        const bool requestAlreadyPending =
            requestedGeneration >= dirtyGeneration &&
            item->requestedChainFingerprint == chainState.fingerprint;
        if (!needsTextureRefresh || requestAlreadyPending) {
            continue;
        }

        EditorRenderWorker::CompositeOutputRequest request;
        request.outputNodeId = chainState.info.outputNodeId;
        request.sourceNodeId = chainState.info.sourceNodeId;

        const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chainState.info.sourceNodeId);
        if (sourceNode &&
            sourceNode->kind == EditorNodeGraph::NodeKind::Image &&
            !sourceNode->image.pixels.empty() &&
            sourceNode->image.width > 0 &&
            sourceNode->image.height > 0) {
            request.width = sourceNode->image.width;
            request.height = sourceNode->image.height;
            request.channels = std::max(1, sourceNode->image.channels);
        } else {
            request.width = 256;
            request.height = 256;
            request.channels = 4;
            request.sourcePixels = BuildTransparentPixels(request.width, request.height);
        }

        request.dirtyGeneration = dirtyGeneration;
        request.chainFingerprint = chainState.fingerprint;
        item->requestedRenderRevision = dirtyGeneration;
        item->requestedChainFingerprint = chainState.fingerprint;
        m_CompositeOutputRequestedGenerations[chainState.info.outputNodeId] = dirtyGeneration;
        requests.push_back(std::move(request));
    }

    return requests;
}

std::vector<EditorRenderWorker::PreviewRequest> EditorModule::BuildPreviewRequests() {
    std::vector<EditorRenderWorker::PreviewRequest> requests;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Preview) {
            continue;
        }

        const EditorNodeGraph::Link* input =
            m_NodeGraph.FindAnyInputLink(node.id, EditorNodeGraph::kPreviewInputSocketId);
        if (!input) {
            m_PreviewRequestedGenerations.erase(node.id);
            m_PreviewCompletedGenerations.erase(node.id);
            m_PreviewPixelCache.erase(node.id);
            continue;
        }

        EditorNodeGraph::SocketDefinition sourceSocket;
        if (!m_NodeGraph.FindSocket(input->fromNodeId, input->fromSocketId, &sourceSocket)) {
            continue;
        }
        if (sourceSocket.type != EditorNodeGraph::SocketType::Image &&
            sourceSocket.type != EditorNodeGraph::SocketType::Mask) {
            continue;
        }

        const std::uint64_t dirtyGeneration = std::max<std::uint64_t>(1, GetNodeDirtyGeneration(input->fromNodeId));
        const std::uint64_t requestedGeneration =
            m_PreviewRequestedGenerations.count(node.id) ? m_PreviewRequestedGenerations[node.id] : 0;
        const std::uint64_t completedGeneration =
            m_PreviewCompletedGenerations.count(node.id) ? m_PreviewCompletedGenerations[node.id] : 0;
        if (requestedGeneration >= dirtyGeneration || completedGeneration >= dirtyGeneration) {
            continue;
        }

        const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(input->fromNodeId);
        if (!sourceNode) {
            continue;
        }

        EditorRenderWorker::PreviewRequest request;
        request.previewNodeId = node.id;
        request.sourceNodeId = input->fromNodeId;
        request.sourceSocketId = input->fromSocketId;
        request.maskInput = sourceSocket.type == EditorNodeGraph::SocketType::Mask;
        request.dirtyGeneration = dirtyGeneration;

        if (sourceNode->kind == EditorNodeGraph::NodeKind::Image &&
            !sourceNode->image.pixels.empty() &&
            sourceNode->image.width > 0 &&
            sourceNode->image.height > 0) {
            request.sourcePixels = sourceNode->image.pixels;
            request.width = sourceNode->image.width;
            request.height = sourceNode->image.height;
            request.channels = std::max(1, sourceNode->image.channels);
        } else {
            request.sourcePixels = m_Pipeline.GetSourcePixelsRaw();
            request.width = m_Pipeline.GetCanvasWidth();
            request.height = m_Pipeline.GetCanvasHeight();
            request.channels = std::max(1, m_Pipeline.GetSourceChannels());
        }
        if (request.sourcePixels.empty()) {
            for (const EditorNodeGraph::Node& graphNode : m_NodeGraph.GetNodes()) {
                if (graphNode.kind == EditorNodeGraph::NodeKind::Image &&
                    !graphNode.image.pixels.empty() &&
                    graphNode.image.width > 0 &&
                    graphNode.image.height > 0) {
                    request.sourcePixels = graphNode.image.pixels;
                    request.width = graphNode.image.width;
                    request.height = graphNode.image.height;
                    request.channels = std::max(1, graphNode.image.channels);
                    break;
                }
            }
        }
        if (request.sourcePixels.empty() || request.width <= 0 || request.height <= 0) {
            request.width = 256;
            request.height = 256;
            request.channels = 4;
            request.sourcePixels.assign(static_cast<size_t>(request.width * request.height * request.channels), 0);
            for (size_t i = 3; i < request.sourcePixels.size(); i += 4) {
                request.sourcePixels[i] = 255;
            }
        }

        m_PreviewRequestedGenerations[node.id] = dirtyGeneration;
        requests.push_back(std::move(request));
    }
    return requests;
}

void EditorModule::ConsumeRenderWorkerResults() {
    EditorRenderWorker::Result result;
    while (m_RenderWorker.TryConsumeCompleted(result)) {
        if (result.outputTexture.texture != 0 && !WaitForSharedTextureFence(result.outputTexture.readyFence)) {
            glDeleteTextures(1, &result.outputTexture.texture);
            result.outputTexture.texture = 0;
        }
        if (result.generation < m_RenderGeneration) {
            if (result.outputTexture.texture != 0) {
                glDeleteTextures(1, &result.outputTexture.texture);
                result.outputTexture.texture = 0;
            }
            continue;
        }
        m_RenderPending = m_RenderWorkerAvailable && m_RenderWorker.IsBusy();
        if (result.success && result.outputTexture.texture != 0) {
            m_Pipeline.AdoptExternalOutputTexture(
                result.outputTexture.texture,
                result.outputTexture.width,
                result.outputTexture.height);
            result.outputTexture.texture = 0;
            m_LastCompletedRenderGeneration = result.generation;
        } else if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }

        for (const EditorRenderWorker::CompositeOutputResult& compositeResult : result.compositeOutputs) {
            CompositeSceneItem* item = FindCompositeSceneItem(compositeResult.outputNodeId);
            if (!item) {
                continue;
            }
            if (!compositeResult.success || compositeResult.pixels.empty() || compositeResult.width <= 0 || compositeResult.height <= 0) {
                continue;
            }

            const CroppedRgbaImage cropped = CropToAlphaBounds(
                compositeResult.pixels,
                compositeResult.width,
                compositeResult.height);
            const std::vector<unsigned char>& uploadPixels =
                !cropped.pixels.empty() ? cropped.pixels : compositeResult.pixels;
            const int uploadW = cropped.width > 0 ? cropped.width : compositeResult.width;
            const int uploadH = cropped.height > 0 ? cropped.height : compositeResult.height;
            if (item->texture != 0) {
                glDeleteTextures(1, &item->texture);
                item->texture = 0;
            }
            item->texture = GLHelpers::CreateTextureFromPixels(uploadPixels.data(), uploadW, uploadH, 4);
            item->textureWidth = uploadW;
            item->textureHeight = uploadH;
            item->rgbaPixels = uploadPixels;
            item->cachedRenderRevision = compositeResult.dirtyGeneration;
            item->cachedChainFingerprint = compositeResult.chainFingerprint;
            m_CompositeOutputCompletedGenerations[compositeResult.outputNodeId] = compositeResult.dirtyGeneration;
        }

        for (const EditorRenderWorker::PreviewResult& previewResult : result.previews) {
            if (!previewResult.success ||
                previewResult.pixels.empty() ||
                previewResult.width <= 0 ||
                previewResult.height <= 0) {
                continue;
            }
            const std::uint64_t desiredRevision = GetPreviewNodeRevision(previewResult.previewNodeId);
            if (previewResult.dirtyGeneration < desiredRevision) {
                continue;
            }

            GraphPreviewPixels cached;
            cached.pixels = previewResult.pixels;
            cached.width = previewResult.width;
            cached.height = previewResult.height;
            cached.revision = previewResult.dirtyGeneration;
            m_PreviewPixelCache[previewResult.previewNodeId] = std::move(cached);
            m_PreviewCompletedGenerations[previewResult.previewNodeId] = previewResult.dirtyGeneration;
            m_PreviewDisplayedRevisions[previewResult.previewNodeId] = previewResult.dirtyGeneration;
        }
    }
}

void EditorModule::SubmitRenderIfReady() {
    const bool compositeMode = GetViewportMode() == ViewportMode::CompositeCanvas;
    if (!m_RenderWorkerAvailable && !m_RenderDirty) {
        return;
    }
    if (m_RenderDirty && ImGui::GetTime() - m_LastRenderDirtyTime < 0.02) {
        return;
    }
    const bool workerBusy = m_RenderWorkerAvailable && (m_RenderPending || m_RenderWorker.IsBusy());
    std::vector<EditorRenderWorker::PreviewRequest> previewRequests;
    if (m_RenderWorkerAvailable && !workerBusy) {
        previewRequests = BuildPreviewRequests();
    }
    if (!m_RenderDirty && previewRequests.empty()) {
        return;
    }
    if (!compositeMode && m_RenderPending) {
        return;
    }

    if (compositeMode) {
        std::vector<EditorRenderWorker::CompositeOutputRequest> requests = BuildCompositeOutputRequests();
        if (requests.empty() && previewRequests.empty()) {
            m_RenderDirty = false;
            if (!m_RenderWorkerAvailable || !m_RenderWorker.IsBusy()) {
                m_RenderPending = false;
            }
            return;
        }

        ++m_RenderGeneration;
        m_LastSubmittedRenderRevision = m_RenderRevision;
        m_RenderDirty = false;

        if (m_RenderWorkerAvailable) {
            EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
            snapshot.outputConnected = false;
            snapshot.compositeOutputs = std::move(requests);
            snapshot.previews = std::move(previewRequests);
            m_RenderPending = true;
            m_RenderWorker.Submit(std::move(snapshot));
        } else {
            m_RenderPending = false;
            for (const EditorRenderWorker::CompositeOutputRequest& request : requests) {
                int texW = 0;
                int texH = 0;
                std::vector<unsigned char> pixels = GetCompositePixelsForOutputNode(request.outputNodeId, texW, texH);
                CompositeSceneItem* item = FindCompositeSceneItem(request.outputNodeId);
                if (!item || pixels.empty() || texW <= 0 || texH <= 0) {
                    continue;
                }
                const CroppedRgbaImage cropped = CropToAlphaBounds(pixels, texW, texH);
                const std::vector<unsigned char>& uploadPixels = !cropped.pixels.empty() ? cropped.pixels : pixels;
                const int uploadW = cropped.width > 0 ? cropped.width : texW;
                const int uploadH = cropped.height > 0 ? cropped.height : texH;
                if (item->texture != 0) {
                    glDeleteTextures(1, &item->texture);
                    item->texture = 0;
                }
                item->texture = GLHelpers::CreateTextureFromPixels(uploadPixels.data(), uploadW, uploadH, 4);
                item->textureWidth = uploadW;
                item->textureHeight = uploadH;
                item->rgbaPixels = uploadPixels;
                item->cachedRenderRevision = request.dirtyGeneration;
                item->cachedChainFingerprint = request.chainFingerprint;
                m_CompositeOutputCompletedGenerations[request.outputNodeId] = request.dirtyGeneration;
            }
        }
        return;
    }
    if (!m_RenderDirty && !previewRequests.empty()) {
        ++m_RenderGeneration;
        EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
        snapshot.outputConnected = false;
        snapshot.sourcePixels.clear();
        snapshot.previews = std::move(previewRequests);
        m_RenderPending = true;
        m_RenderWorker.Submit(std::move(snapshot));
        return;
    }
    if (!m_NodeGraph.IsOutputConnected()) {
        m_RenderDirty = false;
        m_Pipeline.ClearOutput();
        if (!previewRequests.empty() && m_RenderWorkerAvailable) {
            ++m_RenderGeneration;
            EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
            snapshot.outputConnected = false;
            snapshot.sourcePixels.clear();
            snapshot.previews = std::move(previewRequests);
            m_RenderPending = true;
            m_RenderWorker.Submit(std::move(snapshot));
        } else {
            m_RenderPending = false;
        }
        return;
    }
    if (m_RenderRevision <= m_LastSubmittedRenderRevision) {
        return;
    }

    ++m_RenderGeneration;
    m_LastSubmittedRenderRevision = m_RenderRevision;
    m_RenderDirty = false;

    if (m_RenderWorkerAvailable) {
        m_RenderPending = true;
        EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
        snapshot.previews = std::move(previewRequests);
        m_RenderWorker.Submit(std::move(snapshot));
    } else {
        m_Pipeline.ExecuteGraph(BuildGraphSnapshot());
        m_LastCompletedRenderGeneration = m_RenderGeneration;
    }
}
