#include "Editor/EditorModule.h"

#include "App/settings/AppearanceTheme.h"
#include "Editor/Layers/ToneLayers.h"
#include "Raw/RawAutoBase.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr int kRawWorkspaceFastPreviewMaxDimension = 2048;
constexpr std::size_t kViewportOutputTileTextureDeletesPerFrame = 12;

double MillisecondsBetween(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::size_t HashJsonValue(const nlohmann::json& value) {
    return std::hash<std::string>{}(value.dump());
}

Stack::RawAnalysis::CurrentFrameInputStats ToRawCurrentFrameInputStats(const RenderTextureStats& stats) {
    Stack::RawAnalysis::CurrentFrameInputStats rawStats;
    rawStats.valid = stats.valid;
    rawStats.p001Luma = stats.p001Luma;
    rawStats.p01Luma = stats.p01Luma;
    rawStats.p05Luma = stats.p05Luma;
    rawStats.p50Luma = stats.p50Luma;
    rawStats.p95Luma = stats.p95Luma;
    rawStats.p99Luma = stats.p99Luma;
    rawStats.p999Luma = stats.p999Luma;
    rawStats.logAverageLuma = stats.logAverageLuma;
    rawStats.dynamicRangeEv = stats.dynamicRangeEv;
    rawStats.validPixelPercent = stats.validPixelPercent;
    rawStats.hdrPixelPercent = stats.hdrPixelPercent;
    rawStats.displayClipPercent = stats.displayClipPercent;
    return rawStats;
}

struct CroppedRgbaImage {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

bool PollSharedTextureFence(GLsync& fence, bool& failed) {
    failed = false;
    if (!fence) {
        return true;
    }
    const GLenum waitResult = glClientWaitSync(fence, 0, 0);
    if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED) {
        glDeleteSync(fence);
        fence = nullptr;
        return true;
    }
    if (waitResult == GL_TIMEOUT_EXPIRED) {
        return false;
    }
    glDeleteSync(fence);
    fence = nullptr;
    failed = true;
    return false;
}

bool IsMaskOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::MaskGenerator ||
           kind == EditorNodeGraph::NodeKind::MaskCombine ||
           kind == EditorNodeGraph::NodeKind::MaskUtility ||
           kind == EditorNodeGraph::NodeKind::CustomMask ||
           kind == EditorNodeGraph::NodeKind::ImageToMask;
}

bool IsImageOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::Image ||
           kind == EditorNodeGraph::NodeKind::RawDevelop ||
           kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
           kind == EditorNodeGraph::NodeKind::HdrMerge ||
           kind == EditorNodeGraph::NodeKind::Mfsr ||
           kind == EditorNodeGraph::NodeKind::Layer ||
           kind == EditorNodeGraph::NodeKind::ImageGenerator ||
           kind == EditorNodeGraph::NodeKind::Mix ||
           kind == EditorNodeGraph::NodeKind::DataMath ||
           kind == EditorNodeGraph::NodeKind::ChannelCombine ||
           kind == EditorNodeGraph::NodeKind::Output;
}

void ResolveRawDisplayDimensions(const Raw::RawMetadata& metadata, int& width, int& height) {
    width = Raw::DisplayWidth(metadata);
    height = Raw::DisplayHeight(metadata);
}

CroppedRgbaImage CropToAlphaBounds(const std::vector<unsigned char>& rgbaPixels, int width, int height, int padding = 0) {
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

    minX = std::max(0, minX - std::max(0, padding));
    minY = std::max(0, minY - std::max(0, padding));
    maxX = std::min(width - 1, maxX + std::max(0, padding));
    maxY = std::min(height - 1, maxY + std::max(0, padding));

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
    const std::string sourceSocketId = IsMaskOutputNode(node->kind)
        ? EditorNodeGraph::kMaskOutputSocketId
        : EditorNodeGraph::kImageOutputSocketId;
    if ((node->kind == EditorNodeGraph::NodeKind::Output &&
         TryResolveReferenceSourcePixelsForOutput(nodeId, sourcePixels, sourceW, sourceH, sourceCh)) ||
        (node->kind != EditorNodeGraph::NodeKind::Output &&
         TryResolveReferenceSourcePixels(nodeId, sourceSocketId, sourcePixels, sourceW, sourceH, sourceCh))) {
        // Scope renders use the same reference canvas as the graph path.
    } else if (node->kind == EditorNodeGraph::NodeKind::Image && !node->image.pixels.empty() &&
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
    if (TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
        // Preview renders use the same reference canvas as the inspected stream.
    } else if (sourceNode->kind == EditorNodeGraph::NodeKind::Image && !sourceNode->image.pixels.empty() &&
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
    snapshot.outputNodeId = input->fromNodeId;
    snapshot.outputSocketId = input->fromSocketId;

    RenderPipeline previewPipeline;
    previewPipeline.Initialize();
    previewPipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    previewPipeline.ExecuteGraph(snapshot);
    return previewPipeline.GetPreviewPixels(outW, outH, 512);
}

bool EditorModule::BuildSingleOutputExportRaster(std::vector<unsigned char>& outPixels, int& outW, int& outH) const {
    outW = 0;
    outH = 0;
    outPixels.clear();

    if (!m_NodeGraph.IsOutputConnected()) {
        return false;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;

    if (TryResolveReferenceSourcePixelsForOutput(snapshot.outputNodeId, sourcePixels, sourceW, sourceH, sourceCh)) {
        // Use reference canvas.
    } else if (const EditorNodeGraph::Node* activeImage = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId())) {
        if (activeImage->kind == EditorNodeGraph::NodeKind::Image && !activeImage->image.pixels.empty() && activeImage->image.width > 0 && activeImage->image.height > 0) {
            sourcePixels = activeImage->image.pixels;
            sourceW = activeImage->image.width;
            sourceH = activeImage->image.height;
            sourceCh = std::max(1, activeImage->image.channels);
        }
    }

    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Image && !node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                sourcePixels = node.image.pixels;
                sourceW = node.image.width;
                sourceH = node.image.height;
                sourceCh = std::max(1, node.image.channels);
                break;
            }
        }
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
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

    RenderPipeline exportPipeline;
    exportPipeline.Initialize();
    exportPipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    exportPipeline.ExecuteGraph(snapshot);
    outPixels = exportPipeline.GetOutputPixels(outW, outH);
    return !outPixels.empty() && outW > 0 && outH > 0;
}


void EditorModule::RenderGraphScopeNode(EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId) {
    m_Scopes.RenderScopeNode(this, scopeKind, sourceNodeId);
}

void EditorModule::MarkRenderDirty(int touchedNodeId) {
    const bool wasAlreadyDirty = m_RenderDirty;
    m_RenderDirty = true;
    m_Dirty = true;
    ++m_RenderRevision;
    if (!wasAlreadyDirty) {
        m_LastRenderDirtyTime = ImGui::GetTime();
    }
    if (touchedNodeId > 0) {
        const std::vector<int> downstreamNodeIds = m_NodeGraph.GetDownstreamRenderNodeIds(touchedNodeId);
        const std::vector<int> downstreamOutputNodeIds = m_NodeGraph.GetDownstreamOutputNodeIds(touchedNodeId);
        m_GraphPerformanceStats.lastInvalidationWasFull = false;
        m_GraphPerformanceStats.lastTouchedNodeId = touchedNodeId;
        m_GraphPerformanceStats.lastDirtyNodeCount = static_cast<int>(downstreamNodeIds.size());
        m_GraphPerformanceStats.lastDirtyOutputCount = static_cast<int>(downstreamOutputNodeIds.size());
        MarkDownstreamNodesDirty(touchedNodeId);
        MarkCompositeOutputsDirty(downstreamOutputNodeIds);
    } else {
        m_GraphPerformanceStats.lastInvalidationWasFull = true;
        m_GraphPerformanceStats.lastTouchedNodeId = -1;
        m_GraphPerformanceStats.lastDirtyNodeCount = 0;
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (m_NodeGraph.IsRenderChainNode(node)) {
                ++m_GraphPerformanceStats.lastDirtyNodeCount;
            }
        }
        m_GraphPerformanceStats.lastDirtyOutputCount =
            static_cast<int>(m_NodeGraph.GetConnectedOutputNodeIds().size());
        MarkAllRenderNodesDirty();
        MarkCompositeOutputsDirty(m_NodeGraph.GetConnectedOutputNodeIds());
        m_PreviewDisplayedRevisions.clear();
        m_PreviewRequestedGenerations.clear();
        m_PreviewCompletedGenerations.clear();
        m_ScopeDisplayedRevisions.clear();
    }
}

void EditorModule::MarkRenderRefreshDirty() {
    const bool wasAlreadyDirty = m_RenderDirty;
    m_RenderDirty = true;
    ++m_RenderRevision;
    if (!wasAlreadyDirty) {
        m_LastRenderDirtyTime = ImGui::GetTime();
    }
}

void EditorModule::ClearViewportOutputTiles() {
    QueueViewportOutputTileSetRelease(m_ViewportOutputTiles);
    ClearRawWorkspaceLocalRangeOverlayState();
    m_RawWorkspacePreviewOutputKind = RawWorkspacePreviewOutputKind::None;
    m_ViewportOutputRawWorkspaceSourceKey.clear();
    m_ViewportOutputPreviewMaxDimension = 0;
    m_ViewportOutputRenderGeneration = 0;
}

void EditorModule::QueueViewportOutputTextureRelease(EditorRenderWorker::SharedTextureResult& texture) {
    if (!texture.readyFence && texture.texture == 0) {
        texture = {};
        return;
    }

    m_DeferredViewportOutputTextureReleases.push_back(std::move(texture));
    texture = {};
}

void EditorModule::ClearRawWorkspaceLocalRangeOverlayState() {
    if (m_RawWorkspaceLocalRangeOverlayTexture != 0) {
        EditorRenderWorker::SharedTextureResult overlayTexture;
        overlayTexture.texture = m_RawWorkspaceLocalRangeOverlayTexture;
        overlayTexture.width = m_RawWorkspaceLocalRangeOverlayWidth;
        overlayTexture.height = m_RawWorkspaceLocalRangeOverlayHeight;
        QueueViewportOutputTextureRelease(overlayTexture);
    }
    m_RawWorkspaceLocalRangeOverlayTexture = 0;
    m_RawWorkspaceLocalRangeOverlayWidth = 0;
    m_RawWorkspaceLocalRangeOverlayHeight = 0;
    m_RawWorkspaceLocalRangeOverlaySourceKey.clear();
    m_RawWorkspaceLocalRangeOverlayAcceptedMode.clear();
    m_RawWorkspaceLocalRangeOverlayGeneration = 0;
}

void EditorModule::AdoptRawWorkspaceLocalRangeOverlayFromResult(const EditorRenderWorker::Result& result) {
    if (result.rawWorkspace.sourceKey.empty() ||
        result.rawWorkspace.sourceKey != m_ActiveRawWorkspaceSourceKey ||
        result.rawWorkspace.localRangeOverlayMode.empty() ||
        result.rawWorkspace.localRangeOverlayMode == "none" ||
        result.rawWorkspace.localRangeOverlayMode != m_RawWorkspaceLocalRangeOverlayMode ||
        result.rawWorkspace.localRangeOverlayPixels.empty() ||
        result.rawWorkspace.localRangeOverlayWidth <= 0 ||
        result.rawWorkspace.localRangeOverlayHeight <= 0) {
        ClearRawWorkspaceLocalRangeOverlayState();
        return;
    }

    const unsigned int overlayTexture = GLHelpers::CreateTextureFromPixels(
        result.rawWorkspace.localRangeOverlayPixels.data(),
        result.rawWorkspace.localRangeOverlayWidth,
        result.rawWorkspace.localRangeOverlayHeight,
        4);
    if (overlayTexture == 0) {
        ClearRawWorkspaceLocalRangeOverlayState();
        return;
    }

    ClearRawWorkspaceLocalRangeOverlayState();
    m_RawWorkspaceLocalRangeOverlayTexture = overlayTexture;
    m_RawWorkspaceLocalRangeOverlayWidth = result.rawWorkspace.localRangeOverlayWidth;
    m_RawWorkspaceLocalRangeOverlayHeight = result.rawWorkspace.localRangeOverlayHeight;
    m_RawWorkspaceLocalRangeOverlaySourceKey = result.rawWorkspace.sourceKey;
    m_RawWorkspaceLocalRangeOverlayAcceptedMode = result.rawWorkspace.localRangeOverlayMode;
    m_RawWorkspaceLocalRangeOverlayGeneration = result.generation;
}

bool EditorModule::HasRawWorkspaceLocalRangeOverlayForSource(const std::string& sourceKey) const {
    return !sourceKey.empty() &&
        m_RawWorkspaceLocalRangeOverlayTexture != 0 &&
        m_RawWorkspaceLocalRangeOverlayWidth > 0 &&
        m_RawWorkspaceLocalRangeOverlayHeight > 0 &&
        m_RawWorkspaceLocalRangeOverlaySourceKey == sourceKey &&
        m_RawWorkspaceLocalRangeOverlayAcceptedMode == m_RawWorkspaceLocalRangeOverlayMode &&
        m_RawWorkspaceLocalRangeOverlayGeneration == m_ViewportOutputRenderGeneration;
}

void EditorModule::QueueViewportOutputTileSetRelease(EditorRenderWorker::SharedTextureTileSet& tileSet) {
    if (!tileSet.readyFence && tileSet.tiles.empty()) {
        tileSet = {};
        return;
    }

    m_DeferredViewportOutputTileReleases.push_back(std::move(tileSet));
    tileSet = {};
}

void EditorModule::PumpViewportOutputTextureDeletes(bool drainAll) {
    while (!m_DeferredViewportOutputTextureReleases.empty()) {
        EditorRenderWorker::SharedTextureResult& texture =
            m_DeferredViewportOutputTextureReleases.front();
        if (texture.readyFence) {
            if (drainAll) {
                glClientWaitSync(texture.readyFence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000);
                glDeleteSync(texture.readyFence);
                texture.readyFence = nullptr;
            } else {
                bool fenceFailed = false;
                if (!PollSharedTextureFence(texture.readyFence, fenceFailed) && !fenceFailed) {
                    break;
                }
            }
        }

        if (texture.texture != 0) {
            glDeleteTextures(1, &texture.texture);
            texture.texture = 0;
        }
        m_DeferredViewportOutputTextureReleases.pop_front();
    }
}

void EditorModule::PumpViewportOutputTileTextureDeletes(bool drainAll) {
    std::size_t deletedThisFrame = 0;

    while (!m_DeferredViewportOutputTileReleases.empty()) {
        EditorRenderWorker::SharedTextureTileSet& tileSet = m_DeferredViewportOutputTileReleases.front();
        if (tileSet.readyFence) {
            if (drainAll) {
                glClientWaitSync(tileSet.readyFence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000);
                glDeleteSync(tileSet.readyFence);
                tileSet.readyFence = nullptr;
            } else {
                bool fenceFailed = false;
                if (!PollSharedTextureFence(tileSet.readyFence, fenceFailed) && !fenceFailed) {
                    break;
                }
            }
        }

        while (!tileSet.tiles.empty() &&
            (drainAll || deletedThisFrame < kViewportOutputTileTextureDeletesPerFrame)) {
            EditorRenderWorker::SharedTextureTile tile = tileSet.tiles.back();
            tileSet.tiles.pop_back();
            if (tile.texture != 0) {
                glDeleteTextures(1, &tile.texture);
            }
            ++deletedThisFrame;
        }

        if (!tileSet.tiles.empty()) {
            break;
        }

        m_DeferredViewportOutputTileReleases.pop_front();
    }
}

EditorRenderWorker::Snapshot EditorModule::BuildRenderSnapshot(std::uint64_t generation) {
    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.graph = BuildGraphSnapshot();
    if (IsRawWorkspaceProjectActive()) {
        snapshot.rawWorkspace.sourceKey = m_ActiveRawWorkspaceSourceKey;
        snapshot.rawWorkspace.hasRecipe = !m_ActiveRawWorkspaceSourceKey.empty();
        snapshot.rawWorkspace.recipe = m_ActiveRawWorkspaceRecipe;
        if (!m_RawWorkspaceLocalRangeOverlayMode.empty() &&
            m_RawWorkspaceLocalRangeOverlayMode != "none") {
            snapshot.rawWorkspace.localRangeOverlayMode = m_RawWorkspaceLocalRangeOverlayMode;
            snapshot.graph.rawWorkspaceLocalRangeOverlayMode = m_RawWorkspaceLocalRangeOverlayMode;
        }
        if (m_RawWorkspaceLocalRangeTargetSamplePending &&
            m_RawWorkspaceLocalRangeTargetSourceKey == m_ActiveRawWorkspaceSourceKey) {
            snapshot.rawWorkspace.localRangeTargetSampleRequested = true;
            snapshot.rawWorkspace.localRangeTargetSampleU =
                std::clamp(m_RawWorkspaceLocalRangeTargetU, 0.0f, 1.0f);
            snapshot.rawWorkspace.localRangeTargetSampleV =
                std::clamp(m_RawWorkspaceLocalRangeTargetV, 0.0f, 1.0f);
            snapshot.graph.rawWorkspaceLocalRangeTargetSampleRequested = true;
            snapshot.graph.rawWorkspaceLocalRangeTargetSampleU =
                snapshot.rawWorkspace.localRangeTargetSampleU;
            snapshot.graph.rawWorkspaceLocalRangeTargetSampleV =
                snapshot.rawWorkspace.localRangeTargetSampleV;
        }
    }
    if (m_Appearance) {
        snapshot.viewportTiling = m_Appearance->GetViewportTilingSettings();
    }
    snapshot.outputConnected = GetViewportMode() == ViewportMode::SingleOutputPreview && m_NodeGraph.IsOutputConnected();
    const EditorNodeGraph::Node* activeComplexNode =
        m_ActiveComplexNodeId > 0 ? m_NodeGraph.FindNode(m_ActiveComplexNodeId) : nullptr;
    const bool rawDevelopComplexPreview =
        snapshot.outputConnected &&
        m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
        activeComplexNode &&
        activeComplexNode->kind == EditorNodeGraph::NodeKind::RawDevelop;
    const bool rawWorkspaceFastPreview = IsRawWorkspaceFastPreviewRenderActive(ImGui::GetTime());
    snapshot.previewMaxDimension =
        (rawDevelopComplexPreview || rawWorkspaceFastPreview) ? kRawWorkspaceFastPreviewMaxDimension : 0;
    const auto clampPreviewDimensions = [&](int& width, int& height) {
        if (snapshot.previewMaxDimension <= 0 || width <= 0 || height <= 0) {
            return;
        }
        const int longestSide = std::max(width, height);
        if (longestSide <= snapshot.previewMaxDimension) {
            return;
        }
        width = std::max(1, static_cast<int>(
            (static_cast<long long>(width) * snapshot.previewMaxDimension + longestSide / 2) / longestSide));
        height = std::max(1, static_cast<int>(
            (static_cast<long long>(height) * snapshot.previewMaxDimension + longestSide / 2) / longestSide));
    };
    const bool autoGainMaskPreviewActive =
        snapshot.outputConnected &&
        m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
        m_ActiveComplexNodeId == m_AutoGainMaskPreviewNodeId &&
        m_AutoGainMaskPreviewNodeId > 0 &&
        m_NodeGraph.FindNode(m_AutoGainMaskPreviewNodeId) &&
        m_NodeGraph.FindNode(m_AutoGainMaskPreviewNodeId)->kind == EditorNodeGraph::NodeKind::RawDetailFusion;
    if (autoGainMaskPreviewActive) {
        snapshot.graph.outputNodeId = m_AutoGainMaskPreviewNodeId;
        snapshot.graph.outputSocketId = EditorNodeGraph::kMaskOutputSocketId;
        snapshot.graph.autoGainMaskPreview = true;
    }
    if (snapshot.outputConnected) {
        if (autoGainMaskPreviewActive &&
            TryResolveReferenceSourceBuffer(
                m_AutoGainMaskPreviewNodeId,
                EditorNodeGraph::kMaskOutputSocketId,
                snapshot.sourcePixels,
                snapshot.width,
                snapshot.height,
                snapshot.channels)) {
            // Use the inspected Pre-Local Exposure node's reference canvas.
        } else if (!autoGainMaskPreviewActive &&
            TryResolveReferenceSourceBufferForOutput(
                snapshot.graph.outputNodeId,
                snapshot.sourcePixels,
                snapshot.width,
                snapshot.height,
                snapshot.channels)) {
            // Use the output's reference canvas for multi-source channel recombination.
        } else if (const EditorNodeGraph::Node* activeImage = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId())) {
            if (activeImage->kind == EditorNodeGraph::NodeKind::Image &&
                !activeImage->image.pixels.empty() &&
                activeImage->image.width > 0 &&
                activeImage->image.height > 0) {
                snapshot.sourcePixels = EnsureSharedImagePixels(activeImage->image);
                snapshot.width = activeImage->image.width;
                snapshot.height = activeImage->image.height;
                snapshot.channels = std::max(1, activeImage->image.channels);
            } else if (activeImage->kind == EditorNodeGraph::NodeKind::RawSource) {
                ResolveRawDisplayDimensions(activeImage->rawSource.metadata, snapshot.width, snapshot.height);
                snapshot.channels = 4;
                clampPreviewDimensions(snapshot.width, snapshot.height);
            }
        }
        if (snapshot.sourcePixels.empty() && (snapshot.width <= 0 || snapshot.height <= 0)) {
            snapshot.sourcePixels = MakeSharedSourcePixelBufferCopy(m_Pipeline.GetSourcePixelsRaw());
            snapshot.width = m_Pipeline.GetCanvasWidth();
            snapshot.height = m_Pipeline.GetCanvasHeight();
            snapshot.channels = m_Pipeline.GetSourceChannels();
        }
        if (snapshot.sourcePixels.empty() && (snapshot.width <= 0 || snapshot.height <= 0)) {
            for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
                if (node.kind == EditorNodeGraph::NodeKind::Image &&
                    !node.image.pixels.empty() &&
                    node.image.width > 0 &&
                    node.image.height > 0) {
                    snapshot.sourcePixels = EnsureSharedImagePixels(node.image);
                    snapshot.width = node.image.width;
                    snapshot.height = node.image.height;
                    snapshot.channels = std::max(1, node.image.channels);
                    break;
                } else if (node.kind == EditorNodeGraph::NodeKind::RawSource &&
                    node.rawSource.metadata.visibleWidth > 0 &&
                    node.rawSource.metadata.visibleHeight > 0) {
                    ResolveRawDisplayDimensions(node.rawSource.metadata, snapshot.width, snapshot.height);
                    snapshot.channels = 4;
                    clampPreviewDimensions(snapshot.width, snapshot.height);
                    break;
                }
            }
        }
        if (snapshot.width <= 0 || snapshot.height <= 0) {
            snapshot.width = 256;
            snapshot.height = 256;
            snapshot.channels = 4;
            snapshot.sourcePixels = MakeSharedPixelBufferOwned(BuildTransparentPixels(snapshot.width, snapshot.height));
        }
    }
    const bool requiresLegacyLayerStack = snapshot.graph.nodes.empty();
    if (requiresLegacyLayerStack) {
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
    }
    if (snapshot.outputConnected) {
        snapshot.developCandidateRenders =
            BuildDevelopCandidateRenderRequests(snapshot.graph, snapshot.width, snapshot.height);
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


void EditorModule::ApplyToneCurveAutoRewriteFeedback(const std::vector<ToneCurveAutoRewriteFeedback>& feedbacks) {
    if (feedbacks.empty()) {
        return;
    }

    std::unordered_map<int, ToneCurveAutoRewriteFeedback> latestByNode;
    latestByNode.reserve(feedbacks.size());
    for (const ToneCurveAutoRewriteFeedback& feedback : feedbacks) {
        if (!feedback.valid || feedback.nodeId <= 0 || feedback.requestRevision == 0) {
            continue;
        }
        latestByNode[feedback.nodeId] = feedback;
    }
    if (latestByNode.empty()) {
        return;
    }

    std::unordered_map<int, std::uint64_t> currentRevisions;
    currentRevisions.reserve(latestByNode.size());
    for (const auto& entry : latestByNode) {
        currentRevisions[entry.first] = GetNodeDirtyGeneration(entry.first);
    }

    bool persistedStateChanged = false;
    for (const auto& entry : latestByNode) {
        const ToneCurveAutoRewriteFeedback& feedback = entry.second;
        const auto currentRevisionIt = currentRevisions.find(feedback.nodeId);
        if (currentRevisionIt == currentRevisions.end() || currentRevisionIt->second != feedback.requestRevision) {
            continue;
        }

        EditorNodeGraph::Node* node = m_NodeGraph.FindNode(feedback.nodeId);
        if (!node) {
            continue;
        }

        if (node->kind == EditorNodeGraph::NodeKind::Layer) {
            if (node->layerIndex < 0 ||
                node->layerIndex >= static_cast<int>(m_Layers.size()) ||
                !m_Layers[node->layerIndex]) {
                continue;
            }

            ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[node->layerIndex].get());
            if (!toneCurve) {
                continue;
            }

            const std::size_t currentLayerHash = HashJsonValue(toneCurve->Serialize());
            toneCurve->ApplyAutoRewriteFeedback(feedback);
            if (currentLayerHash != feedback.authoredStateHash) {
                persistedStateChanged = true;
            }
        } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop && node->rawDevelop.integratedToneEnabled) {
            ToneCurveLayer integratedTone;
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
            const std::size_t currentLayerHash = HashJsonValue(integratedTone.Serialize());
            integratedTone.ApplyAutoRewriteFeedback(feedback);
            node->rawDevelop.integratedToneLayerJson = integratedTone.Serialize();
            if (currentLayerHash != feedback.authoredStateHash) {
                persistedStateChanged = true;
            }
        }
    }

    if (persistedStateChanged) {
        m_Dirty = true;
    }
}

void EditorModule::ConsumeRenderWorkerResults() {
    PumpViewportOutputTextureDeletes();
    PumpViewportOutputTileTextureDeletes();

    auto releaseDeferredResultResources = [this](EditorRenderWorker::Result& result) {
        QueueViewportOutputTextureRelease(result.outputTexture);
        QueueViewportOutputTileSetRelease(result.outputTiles);
    };
    auto deferResultUntilReady = [&](EditorRenderWorker::Result&& result) {
        for (EditorRenderWorker::Result& pending : m_DeferredRenderResults) {
            releaseDeferredResultResources(pending);
        }
        m_DeferredRenderResults.clear();
        m_DeferredRenderResults.push_back(std::move(result));
    };

    std::deque<EditorRenderWorker::Result> resultsToProcess;
    while (!m_DeferredRenderResults.empty()) {
        resultsToProcess.push_back(std::move(m_DeferredRenderResults.front()));
        m_DeferredRenderResults.pop_front();
    }

    EditorRenderWorker::Result result;
    while (m_RenderWorker.TryConsumeCompleted(result)) {
        resultsToProcess.push_back(std::move(result));
    }

    while (!resultsToProcess.empty()) {
        EditorRenderWorker::Result result = std::move(resultsToProcess.front());
        resultsToProcess.pop_front();

        if (result.outputTexture.texture != 0) {
            bool fenceFailed = false;
            if (!PollSharedTextureFence(result.outputTexture.readyFence, fenceFailed)) {
                if (fenceFailed) {
                    QueueViewportOutputTextureRelease(result.outputTexture);
                } else {
                    deferResultUntilReady(std::move(result));
                    continue;
                }
            }
        }
        if (!result.outputTiles.tiles.empty()) {
            bool fenceFailed = false;
            if (!PollSharedTextureFence(result.outputTiles.readyFence, fenceFailed)) {
                if (fenceFailed) {
                    QueueViewportOutputTileSetRelease(result.outputTiles);
                } else {
                    deferResultUntilReady(std::move(result));
                    continue;
                }
            }
        }
        if (result.generation < m_RenderGeneration) {
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            continue;
        }
        const auto submittedIt = m_HdrMergeSubmittedNodesByGeneration.find(result.generation);
        const std::vector<int> activeHdrMergeNodeIds =
            submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()
                ? submittedIt->second
                : std::vector<int>{};
        m_RenderPending = m_RenderWorkerAvailable && m_RenderWorker.IsBusy();
        m_HdrMergeRenderingNodeIds.clear();
        m_GraphPerformanceStats.lastMainRenderMs = result.mainRenderMs;
        m_GraphPerformanceStats.lastPreviewRenderMs = result.previewRenderMs;
        m_GraphPerformanceStats.lastCompositeRenderMs = result.compositeRenderMs;
        m_GraphPerformanceStats.lastRenderedPreviewCount = result.renderedPreviewCount;
        m_GraphPerformanceStats.lastRenderedCompositeCount = result.renderedCompositeCount;
        m_GraphPerformanceStats.lastMainOutputTiled =
            result.outputTiles.tiled && result.outputTiles.complete && !result.outputTiles.tiles.empty();
        m_GraphPerformanceStats.lastMainOutputTileCount =
            m_GraphPerformanceStats.lastMainOutputTiled
                ? static_cast<int>(result.outputTiles.tiles.size())
                : 0;
        m_GraphPerformanceStats.lastMainGraphStats = result.mainGraphStats;
        auto markMainOutputAccepted = [&](RawWorkspacePreviewOutputKind outputKind) {
            m_RawWorkspacePreviewOutputKind = result.rawWorkspace.sourceKey.empty()
                ? RawWorkspacePreviewOutputKind::None
                : outputKind;
            m_ViewportOutputRawWorkspaceSourceKey = result.rawWorkspace.sourceKey;
            m_ViewportOutputPreviewMaxDimension = result.previewMaxDimension;
            m_ViewportOutputRenderGeneration = result.generation;
            if (!result.rawWorkspace.sourceKey.empty()) {
                m_RawWorkspaceViewTransformInputStats = result.rawWorkspace.viewTransformInputStats;
                m_RawWorkspaceAnalysis = result.rawWorkspace.analysis;
                if (result.rawWorkspace.recommendations.localReport.valid ||
                    !result.rawWorkspace.recommendations.localSuggestionRationale.empty() ||
                    !result.rawWorkspace.recommendations.localAdjustments.empty()) {
                    m_RawWorkspaceAutoBaseUi.recommendations = result.rawWorkspace.recommendations;
                }
                AdoptRawWorkspaceLocalRangeTargetSampleFromResult(result);
                AdoptRawWorkspaceLocalRangeOverlayFromResult(result);
                TryApplyRawWorkspaceAutoBaseOnAnalysis();
            } else {
                m_RawWorkspaceViewTransformInputStats = {};
                m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
                ClearRawWorkspaceLocalRangeTargetState(true);
                ClearRawWorkspaceLocalRangeOverlayState();
            }
            if (!result.rawWorkspace.sourceKey.empty() &&
                result.previewMaxDimension == 0 &&
                !m_RenderDirty) {
                m_RawWorkspaceFullResolutionPreviewPending = false;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFastPreviewUntilTime = -1.0;
            }
            m_LastCompletedRenderGeneration = result.generation;
            for (int nodeId : activeHdrMergeNodeIds) {
                m_HdrMergeCompletedGenerations[nodeId] = std::max(
                    m_HdrMergeCompletedGenerations[nodeId],
                    m_HdrMergeRequestedGenerations.count(nodeId) ? m_HdrMergeRequestedGenerations[nodeId] : GetNodeDirtyGeneration(nodeId));
                m_HdrMergeFailureMessages.erase(nodeId);
            }
        };
        const bool rawWorkspaceSourceMismatch =
            !result.rawWorkspace.sourceKey.empty() &&
            result.rawWorkspace.sourceKey != m_ActiveRawWorkspaceSourceKey;
        if (rawWorkspaceSourceMismatch) {
            if (m_ViewportOutputRawWorkspaceSourceKey == result.rawWorkspace.sourceKey) {
                ClearViewportOutputTiles();
                m_Pipeline.ClearOutput();
                m_RawWorkspaceViewTransformInputStats = {};
                m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
            }
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
        } else if (result.success &&
            !result.rawWorkspace.sourceKey.empty() &&
            !result.pixels.empty() &&
            result.width > 0 &&
            result.height > 0) {
            ClearViewportOutputTiles();
            int previousOutputWidth = 0;
            int previousOutputHeight = 0;
            EditorRenderWorker::SharedTextureResult previousOutputTexture;
            previousOutputTexture.texture =
                m_Pipeline.TakeExternalOutputTexture(previousOutputWidth, previousOutputHeight);
            previousOutputTexture.width = previousOutputWidth;
            previousOutputTexture.height = previousOutputHeight;
            QueueViewportOutputTextureRelease(previousOutputTexture);
            m_Pipeline.UploadOutputFromPixels(result.pixels.data(), result.width, result.height, 4);
            if (m_Pipeline.GetOutputTexture() != 0) {
                markMainOutputAccepted(RawWorkspacePreviewOutputKind::SingleTexture);
            } else {
                result.error = "Render produced no viewport texture.";
            }
        } else if (result.success && result.outputTexture.texture != 0) {
            ClearViewportOutputTiles();
            m_Pipeline.AdoptExternalOutputTexture(
                result.outputTexture.texture,
                result.outputTexture.width,
                result.outputTexture.height);
            result.outputTexture.texture = 0;
            markMainOutputAccepted(RawWorkspacePreviewOutputKind::SingleTexture);
        } else if (result.success && result.outputTiles.complete && !result.outputTiles.tiles.empty()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
            m_ViewportOutputTiles = std::move(result.outputTiles);
            result.outputTiles = {};
            markMainOutputAccepted(RawWorkspacePreviewOutputKind::Tiled);
        } else if (!m_NodeGraph.IsOutputConnected()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        } else if (!activeHdrMergeNodeIds.empty()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
            const std::string baseMessage = result.error.empty() ? "Render failed" : result.error;
            for (int nodeId : activeHdrMergeNodeIds) {
                const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
                const std::string nodeName = (node && !node->title.empty())
                    ? node->title
                    : std::string("HDR Merge");
                m_HdrMergeFailureMessages[nodeId] = baseMessage;
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    nodeName + ": " + baseMessage,
                    "hdr-merge-render-failed-" + std::to_string(nodeId));
            }
        }
        ApplyToneCurveAutoRewriteFeedback(result.toneCurveAutoRewrites);
        ApplyDevelopCandidateRenderFeedback(result.developCandidateRenders);
        if (submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()) {
            m_HdrMergeSubmittedNodesByGeneration.erase(submittedIt);
        }
        for (auto it = m_HdrMergeSubmittedNodesByGeneration.begin(); it != m_HdrMergeSubmittedNodesByGeneration.end();) {
            if (it->first < result.generation) {
                it = m_HdrMergeSubmittedNodesByGeneration.erase(it);
            } else {
                ++it;
            }
        }

        for (const EditorRenderWorker::CompositeOutputResult& compositeResult : result.compositeOutputs) {
            CompositeSceneItem* item = FindCompositeSceneItem(compositeResult.outputNodeId);
            if (!item) {
                continue;
            }
            if (!compositeResult.success || compositeResult.pixels.empty() || compositeResult.width <= 0 || compositeResult.height <= 0) {
                continue;
            }

            const bool scalableGenerator = CompletedChainSourceUsesScalableGenerator(compositeResult.outputNodeId);
            const bool keepFullRasterFrame = CompletedChainSourceKeepsFullRasterFrame(compositeResult.outputNodeId);
            const bool hadRasterBeforeUpload = item->textureWidth > 0 && item->textureHeight > 0;
            const float previousDrawWidth = static_cast<float>(item->textureWidth) * std::max(0.01f, std::abs(item->scale.x));
            const float previousDrawHeight = static_cast<float>(item->textureHeight) * std::max(0.01f, std::abs(item->scale.y));
            const CroppedRgbaImage cropped = (scalableGenerator && keepFullRasterFrame)
                ? CroppedRgbaImage{}
                : CropToAlphaBounds(
                    compositeResult.pixels,
                    compositeResult.width,
                    compositeResult.height,
                    scalableGenerator ? 2 : 0);
            const std::vector<unsigned char>& uploadPixels = (scalableGenerator && keepFullRasterFrame)
                ? compositeResult.pixels
                : (!cropped.pixels.empty() ? cropped.pixels : compositeResult.pixels);
            const int uploadW = (scalableGenerator && keepFullRasterFrame)
                ? compositeResult.width
                : (cropped.width > 0 ? cropped.width : compositeResult.width);
            const int uploadH = (scalableGenerator && keepFullRasterFrame)
                ? compositeResult.height
                : (cropped.height > 0 ? cropped.height : compositeResult.height);
            if (item->texture != 0) {
                glDeleteTextures(1, &item->texture);
                item->texture = 0;
            }
            item->texture = GLHelpers::CreateTextureFromPixels(uploadPixels.data(), uploadW, uploadH, 4);
            item->textureWidth = uploadW;
            item->textureHeight = uploadH;
            item->keepFullRasterFrame = keepFullRasterFrame;
            item->rgbaPixels = uploadPixels;
            item->cachedRenderRevision = compositeResult.dirtyGeneration;
            item->cachedChainFingerprint = compositeResult.chainFingerprint;
            item->requestedRasterWidth = uploadW;
            item->requestedRasterHeight = uploadH;
            m_CompositeOutputCompletedGenerations[compositeResult.outputNodeId] = compositeResult.dirtyGeneration;
        }

        for (const EditorRenderWorker::PreviewResult& previewResult : result.previews) {
            if (!previewResult.success ||
                previewResult.pixels.empty() ||
                previewResult.width <= 0 ||
                previewResult.height <= 0) {
                if (!previewResult.error.empty()) {
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        previewResult.error,
                        "editor-preview-failed-" + std::to_string(previewResult.previewNodeId));
                }
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
    const bool rawWorkspaceActive = IsRawWorkspaceProjectActive();
    const bool allowBackgroundRenderWorker = m_RenderWorkerAvailable && !rawWorkspaceActive;
    const double now = ImGui::GetTime();
    if (m_ActiveSubWindow == EditorSubWindow::NodeGraph &&
        m_Sidebar.GetNodeGraphUI().IsGraphMiddlePanActive()) {
        return;
    }
    UpdateRawWorkspaceSettledPreviewRender(now);
    if (!allowBackgroundRenderWorker && !m_RenderDirty) {
        return;
    }
    RefreshDeferredDevelopCandidateFeedbackIfReady(now);
    const bool recentRawDevelopInteraction = IsRecentRawDevelopInteraction(now);
    const bool localGraphEdit =
        !m_GraphPerformanceStats.lastInvalidationWasFull &&
        m_GraphPerformanceStats.lastTouchedNodeId > 0;
    const double renderSubmitDelay = (recentRawDevelopInteraction || localGraphEdit) ? 0.006 : 0.02;
    if (m_RenderDirty && now - m_LastRenderDirtyTime < renderSubmitDelay) {
        return;
    }
    const bool workerBusy = allowBackgroundRenderWorker && (m_RenderPending || m_RenderWorker.IsBusy());
    m_GraphPerformanceStats.lastPreviewRequestBuildMs = 0.0f;
    m_GraphPerformanceStats.lastCompositeRequestBuildMs = 0.0f;
    std::vector<EditorRenderWorker::PreviewRequest> previewRequests;
    if (allowBackgroundRenderWorker && !workerBusy && !ShouldDeferPreviewLikeWork(now)) {
        const auto previewBuildBegin = std::chrono::steady_clock::now();
        previewRequests = BuildPreviewRequests();
        m_GraphPerformanceStats.lastPreviewRequestBuildMs =
            MillisecondsBetween(previewBuildBegin, std::chrono::steady_clock::now());
    }
    if (!m_RenderDirty && previewRequests.empty()) {
        return;
    }
    // A newer single-output edit should be allowed to replace stale in-flight
    // background Develop feedback instead of waiting for every old probe to
    // drain. The worker checks the pending generation at safe GL boundaries.
    if (!compositeMode && m_RenderPending && !m_RenderDirty) {
        return;
    }

    const int activeOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
    const std::vector<int> activeHdrMergeNodeIds =
        (!compositeMode && activeOutputNodeId > 0) ? CollectHdrMergeNodesForOutput(activeOutputNodeId) : std::vector<int>{};
    const auto submitPreviewOnlyRequests = [&](std::vector<EditorRenderWorker::PreviewRequest>& requests) {
        if (requests.empty() || !allowBackgroundRenderWorker) {
            m_RenderPending = false;
            return;
        }
        ++m_RenderGeneration;
        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        snapshot.outputConnected = false;
        snapshot.sourcePixels = {};
        snapshot.developCandidateRenders.clear();
        snapshot.previews = std::move(requests);
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
        m_RenderPending = true;
        m_RenderWorker.Submit(std::move(snapshot));
    };
    const auto recordHdrMergeSubmission = [&]() {
        m_HdrMergeRenderingNodeIds.clear();
        for (int nodeId : activeHdrMergeNodeIds) {
            m_HdrMergeRequestedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeFailureMessages.erase(nodeId);
            m_HdrMergeRenderingNodeIds.insert(nodeId);
        }
        m_HdrMergeSubmittedNodesByGeneration[m_RenderGeneration] = activeHdrMergeNodeIds;
    };
    const auto blockInvalidHdrMergeOutput = [&]() -> bool {
        if (activeHdrMergeNodeIds.empty()) {
            return false;
        }

        bool blocked = false;
        for (int nodeId : activeHdrMergeNodeIds) {
            const HdrMergeNodeStatus status = GetHdrMergeNodeStatus(nodeId);
            if (status.state != HdrMergeRenderState::BlockedMissingInput &&
                status.state != HdrMergeRenderState::IncompatibleInput) {
                continue;
            }
            const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
            const std::string nodeName = (node && !node->title.empty())
                ? node->title
                : std::string("HDR Merge");
            QueueUiNotification(
                UiNotificationSeverity::Error,
                nodeName + ": " + status.message,
                "hdr-merge-invalid-" + std::to_string(nodeId));
            blocked = true;
        }
        if (!blocked) {
            return false;
        }

        m_RenderDirty = false;
        m_RenderPending = false;
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
        m_HdrMergeRenderingNodeIds.clear();
        submitPreviewOnlyRequests(previewRequests);
        return true;
    };

    if (compositeMode) {
        const auto compositeBuildBegin = std::chrono::steady_clock::now();
        std::vector<EditorRenderWorker::CompositeOutputRequest> requests = BuildCompositeOutputRequests();
        m_GraphPerformanceStats.lastCompositeRequestBuildMs =
            MillisecondsBetween(compositeBuildBegin, std::chrono::steady_clock::now());
        if (requests.empty() && previewRequests.empty()) {
            m_RenderDirty = false;
            if (!allowBackgroundRenderWorker || !m_RenderWorker.IsBusy()) {
                m_RenderPending = false;
            }
            return;
        }

        ++m_RenderGeneration;
        m_LastSubmittedRenderRevision = m_RenderRevision;
        m_RenderDirty = false;
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(previewRequests.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = static_cast<int>(requests.size());
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;

        if (allowBackgroundRenderWorker) {
            const auto snapshotBuildBegin = std::chrono::steady_clock::now();
            EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
            m_GraphPerformanceStats.lastSnapshotBuildMs =
                MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
            snapshot.outputConnected = false;
            snapshot.compositeOutputs = std::move(requests);
            snapshot.previews = std::move(previewRequests);
            m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
            m_GraphPerformanceStats.lastSubmittedCompositeCount = static_cast<int>(snapshot.compositeOutputs.size());
            m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
            m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
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
                const bool scalableGenerator = CompletedChainSourceUsesScalableGenerator(request.outputNodeId);
                const bool keepFullRasterFrame = CompletedChainSourceKeepsFullRasterFrame(request.outputNodeId);
                const bool hadRasterBeforeUpload = item->textureWidth > 0 && item->textureHeight > 0;
                const float previousDrawWidth = static_cast<float>(item->textureWidth) * std::max(0.01f, std::abs(item->scale.x));
                const float previousDrawHeight = static_cast<float>(item->textureHeight) * std::max(0.01f, std::abs(item->scale.y));
                const CroppedRgbaImage cropped =
                    (scalableGenerator && keepFullRasterFrame) ? CroppedRgbaImage{} : CropToAlphaBounds(pixels, texW, texH, scalableGenerator ? 2 : 0);
                const std::vector<unsigned char>& uploadPixels =
                    (scalableGenerator && keepFullRasterFrame) ? pixels : (!cropped.pixels.empty() ? cropped.pixels : pixels);
                const int uploadW =
                    (scalableGenerator && keepFullRasterFrame) ? texW : (cropped.width > 0 ? cropped.width : texW);
                const int uploadH =
                    (scalableGenerator && keepFullRasterFrame) ? texH : (cropped.height > 0 ? cropped.height : texH);
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
                item->requestedRasterWidth = uploadW;
                item->requestedRasterHeight = uploadH;
                m_CompositeOutputCompletedGenerations[request.outputNodeId] = request.dirtyGeneration;
            }
        }
        return;
    }
    if (!m_RenderDirty && !previewRequests.empty()) {
        submitPreviewOnlyRequests(previewRequests);
        return;
    }
    if (!m_NodeGraph.IsOutputConnected()) {
        m_RenderDirty = false;
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
        submitPreviewOnlyRequests(previewRequests);
        m_HdrMergeRenderingNodeIds.clear();
        return;
    }
    if (m_RenderRevision <= m_LastSubmittedRenderRevision) {
        return;
    }
    if (blockInvalidHdrMergeOutput()) {
        return;
    }

    ++m_RenderGeneration;
    m_LastSubmittedRenderRevision = m_RenderRevision;
    m_RenderDirty = false;
    m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(previewRequests.size());
    m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
    m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = true;
    m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;

    if (allowBackgroundRenderWorker) {
        recordHdrMergeSubmission();
        m_RenderPending = true;
        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        snapshot.previews = std::move(previewRequests);
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = true;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
        m_RenderWorker.Submit(std::move(snapshot));
    } else {
        if (rawWorkspaceActive &&
            ((m_RenderWorkerAvailable && m_RenderWorker.IsBusy()) ||
             (m_NodeBrowserRenderWorkerAvailable && m_NodeBrowserRenderWorker.IsBusy()))) {
            m_RenderWorker.InvalidateSnapshotsBefore(m_RenderGeneration);
            m_NodeBrowserRenderWorker.InvalidateSnapshotsBefore(m_RenderGeneration);
            m_RenderPending = true;
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision = m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            return;
        }

        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        m_Pipeline.SetPreviewMaxDimension(snapshot.previewMaxDimension);
        const auto mainRenderBegin = std::chrono::steady_clock::now();
        ClearViewportOutputTiles();
        m_Pipeline.ExecuteGraph(snapshot.graph);
        if (rawWorkspaceActive) {
            m_RawWorkspaceViewTransformInputStats = m_Pipeline.GetRawDevelopmentViewTransformInputStats();
            m_RawWorkspaceAnalysis =
                Stack::RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
                    ToRawCurrentFrameInputStats(m_RawWorkspaceViewTransformInputStats),
                    m_ActiveRawWorkspaceSourceKey);
            const Stack::RawAutoBase::LocalSuggestionAnalysisImage& localImage =
                m_Pipeline.GetRawDevelopmentLocalSuggestionImage();
            m_RawWorkspaceAutoBaseUi.recommendations =
                Stack::RawAutoBase::BuildAutoBaseRecommendations(
                    m_RawWorkspaceAnalysis,
                    m_ActiveRawWorkspaceRecipe,
                    nullptr,
                    &localImage);
            TryApplyRawWorkspaceAutoBaseOnAnalysis();
        } else {
            m_RawWorkspaceViewTransformInputStats = {};
            m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
        }
        if (rawWorkspaceActive && snapshot.rawWorkspace.localRangeTargetSampleRequested) {
            EditorRenderWorker::Result targetSampleResult;
            targetSampleResult.generation = m_RenderGeneration;
            targetSampleResult.previewMaxDimension = snapshot.previewMaxDimension;
            targetSampleResult.rawWorkspace.sourceKey = m_ActiveRawWorkspaceSourceKey;
            targetSampleResult.rawWorkspace.localRangeTargetSample.u =
                snapshot.rawWorkspace.localRangeTargetSampleU;
            targetSampleResult.rawWorkspace.localRangeTargetSample.v =
                snapshot.rawWorkspace.localRangeTargetSampleV;
            float sceneEv = 0.0f;
            float sceneLuma = 0.0f;
            float sampleU = 0.0f;
            float sampleV = 0.0f;
            std::array<float, 3> sceneRgb = { 0.0f, 0.0f, 0.0f };
            if (m_Pipeline.GetRawDevelopmentLocalRangeTargetSample(
                    sceneEv,
                    sceneLuma,
                    sampleU,
                    sampleV,
                    &sceneRgb)) {
                targetSampleResult.rawWorkspace.localRangeTargetSample.valid = true;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneEv = sceneEv;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneLuma = sceneLuma;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneR = sceneRgb[0];
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneG = sceneRgb[1];
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneB = sceneRgb[2];
                targetSampleResult.rawWorkspace.localRangeTargetSample.u = sampleU;
                targetSampleResult.rawWorkspace.localRangeTargetSample.v = sampleV;
            }
            AdoptRawWorkspaceLocalRangeTargetSampleFromResult(targetSampleResult);
        }
        m_GraphPerformanceStats.lastMainRenderMs =
            MillisecondsBetween(mainRenderBegin, std::chrono::steady_clock::now());
        m_GraphPerformanceStats.lastMainGraphStats = m_Pipeline.GetLastGraphExecutionStats();
        m_GraphPerformanceStats.lastPreviewRenderMs = 0.0f;
        m_GraphPerformanceStats.lastCompositeRenderMs = 0.0f;
        m_GraphPerformanceStats.lastRenderedPreviewCount = 0;
        m_GraphPerformanceStats.lastRenderedCompositeCount = 0;
        m_GraphPerformanceStats.lastMainOutputTiled = false;
        m_GraphPerformanceStats.lastMainOutputTileCount = 0;
        m_RenderPending = false;
        if (rawWorkspaceActive) {
            m_RawWorkspacePreviewOutputKind = m_Pipeline.GetOutputTexture() != 0
                ? RawWorkspacePreviewOutputKind::SingleTexture
                : RawWorkspacePreviewOutputKind::None;
            m_ViewportOutputRawWorkspaceSourceKey =
                m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::SingleTexture
                    ? m_ActiveRawWorkspaceSourceKey
                    : std::string();
            m_ViewportOutputPreviewMaxDimension = snapshot.previewMaxDimension;
            m_ViewportOutputRenderGeneration = m_RenderGeneration;
            if (snapshot.previewMaxDimension == 0 && !m_RenderDirty) {
                m_RawWorkspaceFullResolutionPreviewPending = false;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFastPreviewUntilTime = -1.0;
            }
        }
        ApplyToneCurveAutoRewriteFeedback(m_Pipeline.GetToneCurveAutoRewriteFeedback());
        for (int nodeId : activeHdrMergeNodeIds) {
            m_HdrMergeRequestedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeCompletedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeFailureMessages.erase(nodeId);
        }
        m_HdrMergeRenderingNodeIds.clear();
        m_LastCompletedRenderGeneration = m_RenderGeneration;
    }
}
