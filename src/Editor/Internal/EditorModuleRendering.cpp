#include "Editor/EditorModule.h"

#include "App/settings/AppearanceTheme.h"
#include "Editor/Layers/ToneLayers.h"
#include "Raw/RawAutoBase.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/ScopedGLObjects.h"
#include "Utils/PixelBufferUtils.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr int kRawDevelopComplexPreviewMaxDimension = 2048;
constexpr int kRawWorkspaceGraphScopeMaxDimension = 192;
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
           kind == EditorNodeGraph::NodeKind::ImageToMask ||
           kind == EditorNodeGraph::NodeKind::FrequencyMask ||
           kind == EditorNodeGraph::NodeKind::MagnitudePhase;
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
           kind == EditorNodeGraph::NodeKind::FrequencyFft ||
           kind == EditorNodeGraph::NodeKind::FrequencyIfft ||
           kind == EditorNodeGraph::NodeKind::SpectrumView ||
           kind == EditorNodeGraph::NodeKind::SpectrumMath ||
           kind == EditorNodeGraph::NodeKind::MagnitudePhase ||
           kind == EditorNodeGraph::NodeKind::ChannelCombine ||
           kind == EditorNodeGraph::NodeKind::Output;
}

void ResolveRawDisplayDimensions(const Raw::RawMetadata& metadata, int& width, int& height) {
    width = Raw::DisplayWidth(metadata);
    height = Raw::DisplayHeight(metadata);
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    return Stack::PixelBuffer::BuildTransparentRgbaPixels(width, height);
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
    if (sourceW <= 0 || sourceH <= 0) {
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
    if (sourceW <= 0 || sourceH <= 0) {
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
    scopePipeline.LoadSourceFromPixels(
        sourcePixels.empty() ? nullptr : sourcePixels.data(),
        sourceW,
        sourceH,
        sourceCh);
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
    if (sourceW <= 0 || sourceH <= 0) {
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
    if (sourceW <= 0 || sourceH <= 0) {
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
    previewPipeline.LoadSourceFromPixels(
        sourcePixels.empty() ? nullptr : sourcePixels.data(),
        sourceW,
        sourceH,
        std::max(1, sourceCh));
    previewPipeline.ExecuteGraph(snapshot);
    return previewPipeline.GetPreviewPixels(outW, outH, 512);
}

bool EditorModule::BuildSingleOutputExportRaster(std::vector<unsigned char>& outPixels, int& outW, int& outH) {
    return BuildSingleOutputTimelineFrameRaster(m_TimelineUi.currentFrame, outPixels, outW, outH);
}

bool EditorModule::BuildSingleOutputTimelineFrameRaster(
    int timelineFrame,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH) {
    outW = 0;
    outH = 0;
    outPixels.clear();

    if (!m_NodeGraph.IsOutputConnected()) {
        return false;
    }

    // Normal export follows a settled Editor render. Reuse that exact full
    // result instead of synchronously rebuilding a RAW graph and allocating a
    // second full-resolution pipeline while the UI is blocked.
    if (timelineFrame == m_TimelineUi.currentFrame &&
        !m_RenderDirty &&
        !m_RenderPending &&
        (!IsRawWorkspaceProjectActive() ||
         m_ViewportOutputPreviewMaxDimension == 0)) {
        outPixels = m_Pipeline.GetOutputPixels(outW, outH);
        if (Stack::PixelBuffer::HasCompletePixelBuffer(
                outPixels.size(), outW, outH, 4)) {
            return true;
        }
        outPixels.clear();
        outW = 0;
        outH = 0;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshotForTimelineFrame(timelineFrame);

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;

    if (TryResolveReferenceSourcePixelsForOutput(snapshot.outputNodeId, sourcePixels, sourceW, sourceH, sourceCh)) {
        // Use reference canvas.
    } else if (const EditorNodeGraph::Node* activeImage = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId())) {
        if (activeImage->kind == EditorNodeGraph::NodeKind::Image &&
            Stack::PixelBuffer::HasCompletePixelBuffer(
                activeImage->image.pixels.size(),
                activeImage->image.width,
                activeImage->image.height,
                activeImage->image.channels)) {
            sourcePixels = activeImage->image.pixels;
            sourceW = activeImage->image.width;
            sourceH = activeImage->image.height;
            sourceCh = std::max(1, activeImage->image.channels);
        }
    }

    if (sourceW <= 0 || sourceH <= 0) {
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Image &&
                Stack::PixelBuffer::HasCompletePixelBuffer(
                    node.image.pixels.size(),
                    node.image.width,
                    node.image.height,
                    node.image.channels)) {
                sourcePixels = node.image.pixels;
                sourceW = node.image.width;
                sourceH = node.image.height;
                sourceCh = std::max(1, node.image.channels);
                break;
            }
        }
    }

    if (sourceW <= 0 || sourceH <= 0) {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }

    if (sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
        for (size_t i = 3; i < sourcePixels.size(); i += 4) {
            sourcePixels[i] = 255;
        }
    }

    if (!sourcePixels.empty() &&
        !Stack::PixelBuffer::HasCompletePixelBuffer(
            sourcePixels.size(), sourceW, sourceH, sourceCh)) {
        std::cerr
            << "[EditorModule] Ignoring incomplete export source pixels: "
            << sourcePixels.size() << " bytes for "
            << sourceW << "x" << sourceH << "x" << sourceCh << ".\n";
        sourcePixels.clear();
    }

    RenderPipeline exportPipeline;
    exportPipeline.Initialize();
    exportPipeline.SetRawDevelopmentAnalysisEnabled(false);
    exportPipeline.LoadSourceFromPixels(
        sourcePixels.empty() ? nullptr : sourcePixels.data(),
        sourceW,
        sourceH,
        std::max(1, sourceCh));
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

void EditorModule::CacheRawWorkspaceGraphScopeReadback(
    const std::string& sourceKey,
    std::size_t inputFingerprint,
    const RawDevelopmentGraphScopeReadback& readback) {
    if (sourceKey.empty() || inputFingerprint == 0 || !readback.valid) {
        return;
    }

    RawWorkspaceGraphScopeCacheEntry* cache = nullptr;
    switch (readback.stage) {
        case RawDevelopmentGraphScopeStage::LocalRangeInput:
            cache = &m_RawWorkspaceLocalRangeInputGraphScopeCache;
            break;
        case RawDevelopmentGraphScopeStage::FinishToneInput:
            cache = &m_RawWorkspaceFinishToneInputGraphScopeCache;
            break;
        case RawDevelopmentGraphScopeStage::None:
        default:
            return;
    }

    cache->sourceKey = sourceKey;
    cache->inputFingerprint = inputFingerprint;
    cache->readback = readback;
}

bool EditorModule::RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool() {
    using Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint;
    using Stack::Renderer::RawDevelopmentCache::Stage;

    const RawWorkspaceGraphScopeCacheEntry* cache = nullptr;
    RawDevelopmentGraphScopeStage expectedReadbackStage =
        RawDevelopmentGraphScopeStage::None;
    Stage fingerprintStage = Stage::PostLocalExposure;
    switch (m_RawWorkspaceLabUi.activeTool) {
        case RawLabTool::Zones:
            cache = &m_RawWorkspaceLocalRangeInputGraphScopeCache;
            expectedReadbackStage =
                RawDevelopmentGraphScopeStage::LocalRangeInput;
            fingerprintStage = Stage::PostLocalExposure;
            break;
        case RawLabTool::Tone:
            cache = &m_RawWorkspaceFinishToneInputGraphScopeCache;
            expectedReadbackStage =
                RawDevelopmentGraphScopeStage::FinishToneInput;
            fingerprintStage = Stage::PostLocalRange;
            break;
        default:
            m_RawWorkspaceGraphScopeReadback = {};
            return false;
    }

    if (!IsRawWorkspaceProjectActive() ||
        m_ViewportOutputRawWorkspaceSourceKey !=
            m_ActiveRawWorkspaceSourceKey) {
        m_RawWorkspaceGraphScopeReadback = {};
        return false;
    }

    const std::size_t expectedFingerprint = BuildStageFingerprint(
        m_ActiveRawWorkspaceRecipe,
        m_ViewportOutputPreviewMaxDimension,
        fingerprintStage);
    if (cache->sourceKey != m_ActiveRawWorkspaceSourceKey ||
        cache->inputFingerprint != expectedFingerprint ||
        !cache->readback.valid ||
        cache->readback.stage != expectedReadbackStage) {
        m_RawWorkspaceGraphScopeReadback = {};
        return false;
    }

    m_RawWorkspaceGraphScopeReadback = cache->readback;
    return true;
}

void EditorModule::ClearRawWorkspaceGraphScopeReadbackCaches() {
    m_RawWorkspaceLocalRangeInputGraphScopeCache = {};
    m_RawWorkspaceFinishToneInputGraphScopeCache = {};
}

void EditorModule::ClearViewportOutputTiles() {
    QueueViewportOutputTileSetRelease(m_ViewportOutputTiles);
    ClearRawWorkspacePresentationTexture();
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

    // A viewport texture must never alias ImGui's font atlas. If corrupted or
    // stale state contains the atlas ID, dropping that state is safe; queueing
    // it for deletion would destroy the application's live font texture.
    if (texture.texture != 0 &&
        ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().Fonts != nullptr &&
        (ImTextureID)(intptr_t)texture.texture ==
            ImGui::GetIO().Fonts->TexRef.GetTexID()) {
        texture = {};
        return;
    }

    try {
        m_DeferredViewportOutputTextureReleases.push_back(
            std::move(texture));
    } catch (const std::bad_alloc&) {
        if (texture.readyFence) {
            glDeleteSync(texture.readyFence);
        }
        if (texture.texture != 0) {
            glDeleteTextures(1, &texture.texture);
        }
    } catch (const std::length_error&) {
        if (texture.readyFence) {
            glDeleteSync(texture.readyFence);
        }
        if (texture.texture != 0) {
            glDeleteTextures(1, &texture.texture);
        }
    }
    texture = {};
}

bool EditorModule::IsViewportTextureSafeForDrawing(unsigned int texture) const {
    if (texture == 0 || glIsTexture(texture) != GL_TRUE) {
        return false;
    }
    return ImGui::GetCurrentContext() == nullptr ||
        ImGui::GetIO().Fonts == nullptr ||
        (ImTextureID)(intptr_t)texture !=
            ImGui::GetIO().Fonts->TexRef.GetTexID();
}

void EditorModule::ClearRawWorkspacePresentationTexture() {
    QueueViewportOutputTextureRelease(m_RawWorkspacePresentationTexture);
}

bool EditorModule::AdoptRawWorkspacePresentationTexture(
    EditorRenderWorker::SharedTextureResult& texture) {
    if (!IsViewportTextureSafeForDrawing(texture.texture) ||
        texture.width <= 0 ||
        texture.height <= 0) {
        QueueViewportOutputTextureRelease(texture);
        return false;
    }

    QueueViewportOutputTextureRelease(m_RawWorkspacePresentationTexture);
    m_RawWorkspacePresentationTexture = std::move(texture);
    texture = {};
    return true;
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
    m_RawWorkspaceLocalRangeOverlayTargetPreviewGeneration = 0;
}

void EditorModule::AdoptRawWorkspaceLocalRangeOverlayFromResult(
    EditorRenderWorker::Result& result) noexcept {
    EditorRenderWorker::SharedTextureResult& overlayTexture =
        result.rawWorkspace.localRangeOverlayTexture;
    const bool targetOutlineResult =
        result.rawWorkspace.localRangeOverlayMode == "target-outline";
    const bool staleTargetOutline =
        targetOutlineResult &&
        result.rawWorkspace.localRangeTargetPreviewGeneration !=
            m_RawWorkspaceLocalRangeTargetPreview.generation;
    if (result.rawWorkspace.sourceKey.empty() ||
        result.rawWorkspace.sourceKey != m_ActiveRawWorkspaceSourceKey ||
        result.rawWorkspace.localRangeOverlayMode.empty() ||
        result.rawWorkspace.localRangeOverlayMode == "none" ||
        result.rawWorkspace.localRangeOverlayMode != m_RawWorkspaceLocalRangeOverlayMode ||
        staleTargetOutline ||
        overlayTexture.texture == 0 ||
        result.rawWorkspace.localRangeOverlayWidth <= 0 ||
        result.rawWorkspace.localRangeOverlayHeight <= 0) {
        QueueViewportOutputTextureRelease(overlayTexture);
        if (!targetOutlineResult) {
            ClearRawWorkspaceLocalRangeOverlayState();
        }
        return;
    }

    std::string acceptedSourceKey;
    std::string acceptedMode;
    try {
        acceptedSourceKey = result.rawWorkspace.sourceKey;
        acceptedMode = result.rawWorkspace.localRangeOverlayMode;
    } catch (...) {
        QueueViewportOutputTextureRelease(overlayTexture);
        return;
    }

    EditorRenderWorker::SharedTextureResult acceptedTexture =
        std::move(overlayTexture);
    overlayTexture = {};
    ClearRawWorkspaceLocalRangeOverlayState();
    m_RawWorkspaceLocalRangeOverlayTexture = acceptedTexture.texture;
    m_RawWorkspaceLocalRangeOverlayWidth = result.rawWorkspace.localRangeOverlayWidth;
    m_RawWorkspaceLocalRangeOverlayHeight = result.rawWorkspace.localRangeOverlayHeight;
    m_RawWorkspaceLocalRangeOverlaySourceKey = std::move(acceptedSourceKey);
    m_RawWorkspaceLocalRangeOverlayAcceptedMode = std::move(acceptedMode);
    m_RawWorkspaceLocalRangeOverlayGeneration = result.generation;
    m_RawWorkspaceLocalRangeOverlayTargetPreviewGeneration =
        result.rawWorkspace.localRangeTargetPreviewGeneration;
    if (result.rawWorkspace.localRangeOverlayMode == "target-outline") {
        m_RawWorkspaceLocalRangeTargetPreviewRefined =
            result.rawWorkspace.localRangeTargetPreviewRefined;
        m_RawWorkspaceLocalRangeTargetPreviewRefinementPending =
            result.rawWorkspace.localRangeTargetPreviewRefinementPending;
    }
    acceptedTexture.texture = 0;
    acceptedTexture.readyFence = nullptr;
}

bool EditorModule::HasRawWorkspaceLocalRangeOverlayForSource(const std::string& sourceKey) const {
    return !sourceKey.empty() &&
        IsViewportTextureSafeForDrawing(m_RawWorkspaceLocalRangeOverlayTexture) &&
        m_RawWorkspaceLocalRangeOverlayWidth > 0 &&
        m_RawWorkspaceLocalRangeOverlayHeight > 0 &&
        m_RawWorkspaceLocalRangeOverlaySourceKey == sourceKey &&
        m_RawWorkspaceLocalRangeOverlayAcceptedMode == m_RawWorkspaceLocalRangeOverlayMode &&
        Stack::RawLocalRangeTargetInteraction::OverlayMatchesPresentation(
            m_RawWorkspaceLocalRangeOverlayAcceptedMode == "target-outline",
            m_RawWorkspaceLocalRangeOverlayGeneration,
            m_ViewportOutputRenderGeneration,
            m_RawWorkspaceLocalRangeOverlayTargetPreviewGeneration,
            m_RawWorkspaceLocalRangeTargetPreview.generation);
}

void EditorModule::QueueViewportOutputTileSetRelease(EditorRenderWorker::SharedTextureTileSet& tileSet) {
    if (!tileSet.readyFence && tileSet.tiles.empty()) {
        tileSet = {};
        return;
    }

    try {
        m_DeferredViewportOutputTileReleases.push_back(
            std::move(tileSet));
    } catch (const std::bad_alloc&) {
        if (tileSet.readyFence) {
            glDeleteSync(tileSet.readyFence);
        }
        for (const EditorRenderWorker::SharedTextureTile& tile :
             tileSet.tiles) {
            if (tile.texture != 0) {
                glDeleteTextures(1, &tile.texture);
            }
        }
    } catch (const std::length_error&) {
        if (tileSet.readyFence) {
            glDeleteSync(tileSet.readyFence);
        }
        for (const EditorRenderWorker::SharedTextureTile& tile :
             tileSet.tiles) {
            if (tile.texture != 0) {
                glDeleteTextures(1, &tile.texture);
            }
        }
    }
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

        const bool aliasesFontAtlas =
            texture.texture != 0 &&
            ImGui::GetCurrentContext() != nullptr &&
            ImGui::GetIO().Fonts != nullptr &&
            (ImTextureID)(intptr_t)texture.texture ==
                ImGui::GetIO().Fonts->TexRef.GetTexID();
        if (texture.texture != 0 && !aliasesFontAtlas) {
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
        const Stack::RawWorkspace::SourceRecord* activeRawSource =
            FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
        snapshot.rawWorkspace.sourceHash =
            activeRawSource == nullptr
                ? 0
                : BuildRawWorkspaceAutoBaseSourceHash(*activeRawSource);
        snapshot.rawWorkspace.hasRecipe = !m_ActiveRawWorkspaceSourceKey.empty();
        snapshot.rawWorkspace.recipe = m_ActiveRawWorkspaceRecipe;
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones) {
            snapshot.rawWorkspace.graphScopeStage =
                RawDevelopmentGraphScopeStage::LocalRangeInput;
        } else if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Tone) {
            snapshot.rawWorkspace.graphScopeStage =
                RawDevelopmentGraphScopeStage::FinishToneInput;
        }
        Stack::PreciseIntegration::IntegrationState& preciseState =
            m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
        if (m_RenderWorkerAvailable && preciseState.active &&
            preciseState.mode == Stack::PreciseIntegration::ProductMode::Precise &&
            Stack::PreciseIntegration::IsRunning(preciseState.state) &&
            preciseState.identity.sourceKey == m_ActiveRawWorkspaceSourceKey &&
            preciseState.identity.sourceHash == snapshot.rawWorkspace.sourceHash) {
            Stack::PreciseIntegration::NativeSolveRequest preciseRequest;
            preciseRequest.identity = preciseState.identity;
            preciseRequest.identity.generation = generation;
            preciseRequest.inputRecipe = preciseState.originalRecipe;
            snapshot.rawWorkspace.preciseSolveRequest = std::move(preciseRequest);
            Stack::PreciseIntegration::MarkRunning(preciseState, generation);
        }
        Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
            m_RawWorkspaceStartPointCandidateRenderQueue,
            m_ActiveRawWorkspaceSourceKey,
            snapshot.rawWorkspace.sourceHash);
        if (Stack::EditorModuleTypes::RawStartingPointCandidateRenderQueueMatchesSource(
                m_RawWorkspaceStartPointCandidateRenderQueue,
                m_ActiveRawWorkspaceSourceKey,
                snapshot.rawWorkspace.sourceHash)) {
            snapshot.rawWorkspace.startPointCandidateRenderRequests =
                m_RawWorkspaceStartPointCandidateRenderQueue.requests;
        }
        if (!m_RawWorkspaceLocalRangeOverlayMode.empty() &&
            m_RawWorkspaceLocalRangeOverlayMode != "none") {
            snapshot.rawWorkspace.localRangeOverlayMode = m_RawWorkspaceLocalRangeOverlayMode;
            snapshot.graph.rawWorkspaceLocalRangeOverlayMode = m_RawWorkspaceLocalRangeOverlayMode;
        }
        if (m_RawWorkspaceLocalRangeTargetMode) {
            snapshot.rawWorkspace.localRangeTargetPreview =
                m_RawWorkspaceLocalRangeTargetPreview;
            snapshot.graph.rawWorkspaceLocalRangeTargetPreview =
                m_RawWorkspaceLocalRangeTargetPreview;
        }
        if (m_RawWorkspaceLocalRangeTargetSamplePending &&
            m_RawWorkspaceLocalRangeTargetSourceKey == m_ActiveRawWorkspaceSourceKey) {
            snapshot.rawWorkspace.localRangeTargetSampleRequested = true;
            snapshot.rawWorkspace.localRangeTargetHoverSample =
                m_RawWorkspaceLocalRangeTargetHoverSample;
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
    const bool rawWorkspaceTargetHover =
        snapshot.rawWorkspace.localRangeTargetSampleRequested &&
        snapshot.rawWorkspace.localRangeTargetHoverSample;
    const bool rawWorkspaceTargetAuxiliary =
        Stack::RawLocalRangeTargetInteraction::ShouldPreserveBasePresentation(
            rawWorkspaceTargetHover,
            snapshot.rawWorkspace.localRangeOverlayMode == "target-outline",
            snapshot.rawWorkspace.localRangeTargetPreview.enabled,
            snapshot.rawWorkspace.localRangeTargetPreview.interactionEditing);
    snapshot.rawWorkspace.analysisRequested =
        !rawWorkspaceFastPreview && !rawWorkspaceTargetAuxiliary;
    snapshot.previewMaxDimension = rawDevelopComplexPreview
        ? kRawDevelopComplexPreviewMaxDimension
        : ((rawWorkspaceFastPreview || rawWorkspaceTargetAuxiliary)
            ? m_RawWorkspaceInteractivePreviewMaxDimension
            : 0);
    if (snapshot.rawWorkspace.hasRecipe) {
        using Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint;
        using Stack::Renderer::RawDevelopmentCache::Stage;
        switch (snapshot.rawWorkspace.graphScopeStage) {
            case RawDevelopmentGraphScopeStage::LocalRangeInput:
                snapshot.rawWorkspace.graphScopeInputFingerprint =
                    BuildStageFingerprint(
                        snapshot.rawWorkspace.recipe,
                        snapshot.previewMaxDimension,
                        Stage::PostLocalExposure);
                break;
            case RawDevelopmentGraphScopeStage::FinishToneInput:
                snapshot.rawWorkspace.graphScopeInputFingerprint =
                    BuildStageFingerprint(
                        snapshot.rawWorkspace.recipe,
                        snapshot.previewMaxDimension,
                        Stage::PostLocalRange);
                break;
            case RawDevelopmentGraphScopeStage::None:
            default:
                break;
        }
    }
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

bool EditorModule::RefreshCompletedChainCacheIfNeeded() const noexcept {
    const std::uint64_t structureRevision = m_NodeGraph.GetStructureRevision();
    if (m_CachedCompletedChainsStructureRevision == structureRevision) {
        return true;
    }

    try {
        const std::vector<EditorNodeGraph::CompletedChainInfo>& completedChains =
            m_NodeGraph.GetCompletedChains();
        std::vector<CachedCompositeChainState> refreshedChains;
        refreshedChains.reserve(completedChains.size());
        for (const EditorNodeGraph::CompletedChainInfo& chain : completedChains) {
            CachedCompositeChainState state;
            state.info = chain;
            refreshedChains.push_back(std::move(state));
        }

        m_CachedCompletedChains.swap(refreshedChains);
        m_CachedConnectedOutputCount = static_cast<int>(m_CachedCompletedChains.size());
        m_CachedCompletedChainsStructureRevision = structureRevision;
        return true;
    } catch (...) {
        return false;
    }
}

bool EditorModule::RefreshCompositeMetadataCacheIfNeeded() noexcept {
    if (!RefreshCompletedChainCacheIfNeeded()) {
        return false;
    }
    const std::uint64_t structureRevision = m_NodeGraph.GetStructureRevision();
    if (m_CachedCompositeMetadataStructureRevision == structureRevision &&
        m_CachedCompositeMetadataRenderRevision == m_RenderRevision &&
        m_CachedCompositeFingerprints.size() == m_CachedCompletedChains.size() &&
        m_CachedCompositeLabels.size() == m_CachedCompletedChains.size()) {
        return true;
    }

    try {
        std::vector<CachedCompositeChainState> refreshedChains = m_CachedCompletedChains;
        std::unordered_map<int, std::size_t> refreshedFingerprints;
        std::unordered_map<int, std::string> refreshedLabels;
        std::vector<int> dirtyOutputIds;
        refreshedFingerprints.reserve(refreshedChains.size());
        refreshedLabels.reserve(refreshedChains.size());
        dirtyOutputIds.reserve(refreshedChains.size());

        for (CachedCompositeChainState& chain : refreshedChains) {
            chain.fingerprint = BuildCompositeChainFingerprint(chain.info);
            chain.label = BuildCompositeChainLabel(chain.info);
            refreshedFingerprints.emplace(chain.info.outputNodeId, chain.fingerprint);
            refreshedLabels.emplace(chain.info.outputNodeId, chain.label);
            const auto previousIt = m_CachedCompositeFingerprints.find(chain.info.outputNodeId);
            if (previousIt == m_CachedCompositeFingerprints.end() ||
                previousIt->second != chain.fingerprint) {
                dirtyOutputIds.push_back(chain.info.outputNodeId);
            }
        }

        MarkCompositeOutputsDirty(dirtyOutputIds);
        m_CachedCompletedChains.swap(refreshedChains);
        m_CachedCompositeFingerprints.swap(refreshedFingerprints);
        m_CachedCompositeLabels.swap(refreshedLabels);
        PruneCompositeDirtyState();
        m_CachedCompositeMetadataStructureRevision = structureRevision;
        m_CachedCompositeMetadataRenderRevision = m_RenderRevision;
        return true;
    } catch (...) {
        return false;
    }
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
    auto pruneMap = [this](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (m_CachedCompositeFingerprints.find(it->first) ==
                m_CachedCompositeFingerprints.end()) {
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


void EditorModule::ApplyToneCurveAutoRewriteFeedback(
    const std::vector<ToneCurveAutoRewriteFeedback>& feedbacks) noexcept {
    if (feedbacks.empty()) {
        return;
    }

    bool persistedStateChanged = false;
    for (std::size_t feedbackIndex = feedbacks.size(); feedbackIndex-- > 0;) {
        const ToneCurveAutoRewriteFeedback& feedback = feedbacks[feedbackIndex];
        if (!feedback.valid || feedback.nodeId <= 0 || feedback.requestRevision == 0) {
            continue;
        }

        bool supersededInBatch = false;
        for (std::size_t newerIndex = feedbackIndex + 1;
             newerIndex < feedbacks.size();
             ++newerIndex) {
            const ToneCurveAutoRewriteFeedback& newer = feedbacks[newerIndex];
            if (newer.valid &&
                newer.nodeId == feedback.nodeId &&
                newer.requestRevision != 0) {
                supersededInBatch = true;
                break;
            }
        }
        if (supersededInBatch ||
            GetNodeDirtyGeneration(feedback.nodeId) != feedback.requestRevision) {
            continue;
        }

        EditorNodeGraph::Node* node = m_NodeGraph.FindNode(feedback.nodeId);
        if (!node) {
            continue;
        }

        try {
            if (node->kind == EditorNodeGraph::NodeKind::Layer) {
                if (node->layerIndex < 0 ||
                    node->layerIndex >= static_cast<int>(m_Layers.size()) ||
                    !m_Layers[node->layerIndex]) {
                    continue;
                }

                ToneCurveLayer* toneCurve =
                    dynamic_cast<ToneCurveLayer*>(m_Layers[node->layerIndex].get());
                if (!toneCurve) {
                    continue;
                }

                const nlohmann::json previousLayerJson = toneCurve->Serialize();
                const std::size_t currentLayerHash =
                    HashJsonValue(previousLayerJson);
                try {
                    toneCurve->ApplyAutoRewriteFeedback(feedback);
                } catch (...) {
                    try {
                        toneCurve->Deserialize(previousLayerJson);
                    } catch (...) {
                    }
                    continue;
                }
                persistedStateChanged |=
                    currentLayerHash != feedback.authoredStateHash;
            } else if (
                node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
                node->rawDevelop.integratedToneEnabled) {
                ToneCurveLayer integratedTone;
                integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
                const std::size_t currentLayerHash =
                    HashJsonValue(integratedTone.Serialize());
                integratedTone.ApplyAutoRewriteFeedback(feedback);
                nlohmann::json refreshedLayerJson = integratedTone.Serialize();
                node->rawDevelop.integratedToneLayerJson =
                    std::move(refreshedLayerJson);
                persistedStateChanged |=
                    currentLayerHash != feedback.authoredStateHash;
            }
        } catch (...) {
            continue;
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
        QueueViewportOutputTextureRelease(
            result.rawWorkspace.localRangeOverlayTexture);
    };
    auto deferResultUntilReady = [&](EditorRenderWorker::Result&& result) {
        for (EditorRenderWorker::Result& pending : m_DeferredRenderResults) {
            releaseDeferredResultResources(pending);
        }
        m_DeferredRenderResults.clear();
        try {
            m_DeferredRenderResults.push_back(std::move(result));
        } catch (const std::bad_alloc&) {
            releaseDeferredResultResources(result);
        } catch (const std::length_error&) {
            releaseDeferredResultResources(result);
        }
    };

    std::deque<EditorRenderWorker::Result> resultsToProcess;
    while (!m_DeferredRenderResults.empty()) {
        try {
            resultsToProcess.push_back(
                std::move(m_DeferredRenderResults.front()));
        } catch (const std::bad_alloc&) {
            releaseDeferredResultResources(
                m_DeferredRenderResults.front());
        } catch (const std::length_error&) {
            releaseDeferredResultResources(
                m_DeferredRenderResults.front());
        }
        m_DeferredRenderResults.pop_front();
    }

    EditorRenderWorker::Result result;
    while (m_RenderWorker.TryConsumeCompleted(result)) {
        try {
            resultsToProcess.push_back(std::move(result));
        } catch (const std::bad_alloc&) {
            releaseDeferredResultResources(result);
        } catch (const std::length_error&) {
            releaseDeferredResultResources(result);
        }
    }

    while (!resultsToProcess.empty()) {
        EditorRenderWorker::Result result = std::move(resultsToProcess.front());
        resultsToProcess.pop_front();

        try {
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
        if (result.rawWorkspace.localRangeOverlayTexture.texture != 0) {
            bool fenceFailed = false;
            if (!PollSharedTextureFence(
                    result.rawWorkspace.localRangeOverlayTexture.readyFence,
                    fenceFailed)) {
                if (fenceFailed) {
                    QueueViewportOutputTextureRelease(
                        result.rawWorkspace.localRangeOverlayTexture);
                } else {
                    deferResultUntilReady(std::move(result));
                    continue;
                }
            }
        }
        if (result.generation < m_RenderGeneration) {
            if (result.rawWorkspace.preciseSolveResult.has_value() &&
                result.rawWorkspace.preciseSolveResult->canceled) {
                // A cancel request intentionally advances the live render
                // generation. The isolated worker's image result is stale,
                // but its cancellation acknowledgment is still required to
                // queue the first normal RAW preview after that worker stops.
                HandleRawWorkspacePreciseSolveResult(
                    *result.rawWorkspace.preciseSolveResult);
            }
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            QueueViewportOutputTextureRelease(
                result.rawWorkspace.localRangeOverlayTexture);
            if (!m_RenderWorkerAvailable || !m_RenderWorker.IsBusy()) {
                ResetIncompleteCompositeOutputRequestsForRetry();
                ResetIncompletePreviewRequestsForRetry();
            }
            continue;
        }
        const auto submittedIt = m_HdrMergeSubmittedNodesByGeneration.find(result.generation);
        const std::vector<int> activeHdrMergeNodeIds =
            submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()
                ? submittedIt->second
                : std::vector<int>{};
        m_RenderPending = m_RenderWorkerAvailable && m_RenderWorker.IsBusy();
        m_HdrMergeRenderingNodeIds.clear();
        if (result.rawWorkspace.preciseSolveResult.has_value()) {
            // A precise solve returns only an isolated verified-recipe record.
            // Never let that worker result replace or clear the live viewport,
            // analysis, readbacks, or diagnostics; the accepted recipe queues
            // its own ordinary render after the one atomic main-thread apply.
            HandleRawWorkspacePreciseSolveResult(
                *result.rawWorkspace.preciseSolveResult);
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            QueueViewportOutputTextureRelease(
                result.rawWorkspace.localRangeOverlayTexture);
            if (submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()) {
                m_HdrMergeSubmittedNodesByGeneration.erase(submittedIt);
            }
            continue;
        }
        m_GraphPerformanceStats.lastMainRenderMs = result.mainRenderMs;
        m_GraphPerformanceStats.lastMainGraphExecuteMs = result.mainRenderMs;
        m_GraphPerformanceStats.lastMainPostExecuteMs = 0.0;
        m_GraphPerformanceStats.lastRawWorkspaceRender =
            !result.rawWorkspace.sourceKey.empty();
        m_GraphPerformanceStats.lastRawInteractivePreview =
            !result.rawWorkspace.sourceKey.empty() && result.previewMaxDimension > 0;
        m_GraphPerformanceStats.lastRawAnalysisCaptured =
            result.rawWorkspace.analysisCaptured;
        m_GraphPerformanceStats.lastRawPreviewMaxDimension =
            result.previewMaxDimension;
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
        m_GraphPerformanceStats.lastMainRegionPlanAvailable = result.mainRegionPlanAvailable;
        m_GraphPerformanceStats.lastMainRegionPlanTileable = result.mainRegionPlanTileable;
        m_GraphPerformanceStats.lastMainRegionPlanHaloX = result.mainRegionPlanHaloX;
        m_GraphPerformanceStats.lastMainRegionPlanHaloY = result.mainRegionPlanHaloY;
        m_GraphPerformanceStats.lastMainRegionPlanReason = result.mainRegionPlanReason;
        m_GraphPerformanceStats.lastMainGraphStats = result.mainGraphStats;
        const Stack::RawWorkspace::SourceRecord* activeRawWorkspaceSource =
            result.rawWorkspace.sourceKey.empty()
                ? nullptr
                : FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
        const std::uint64_t activeRawWorkspaceSourceHash =
            activeRawWorkspaceSource == nullptr
                ? 0
                : BuildRawWorkspaceAutoBaseSourceHash(*activeRawWorkspaceSource);
        const bool rawWorkspaceResultMatchesActive =
            !result.rawWorkspace.sourceKey.empty() &&
            result.rawWorkspace.sourceKey == m_ActiveRawWorkspaceSourceKey &&
            Stack::EditorModuleTypes::RawStartingPointSourceHashesCompatible(
                result.rawWorkspace.sourceHash,
                activeRawWorkspaceSourceHash);
        auto markMainOutputAccepted = [&](RawWorkspacePreviewOutputKind outputKind) {
            m_RawWorkspacePreviewOutputKind = result.rawWorkspace.sourceKey.empty()
                ? RawWorkspacePreviewOutputKind::None
                : outputKind;
            m_ViewportOutputRawWorkspaceSourceKey = result.rawWorkspace.sourceKey;
            m_ViewportOutputPreviewMaxDimension = result.previewMaxDimension;
            m_ViewportOutputRenderGeneration = result.generation;
            if (!result.rawWorkspace.sourceKey.empty()) {
                m_RawWorkspaceStaleRenderStatusText.clear();
                m_RawWorkspaceLocalRangeTargetPreview.interactionEditing =
                    false;
                if (result.rawWorkspace.analysisCaptured) {
                    m_RawWorkspaceViewTransformInputStats = result.rawWorkspace.viewTransformInputStats;
                    m_RawWorkspaceFinalDisplayStats = result.rawWorkspace.finalDisplayStats;
                    m_RawWorkspaceStageStatsReadbacks = result.rawWorkspace.stageStatsReadbacks;
                    CacheRawWorkspaceGraphScopeReadback(
                        result.rawWorkspace.sourceKey,
                        result.rawWorkspace.graphScopeInputFingerprint,
                        result.rawWorkspace.graphScopeReadback);
                    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones ||
                        m_RawWorkspaceLabUi.activeTool == RawLabTool::Tone) {
                        if (!RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool()) {
                            MarkRenderRefreshDirty();
                        }
                    } else {
                        m_RawWorkspaceGraphScopeReadback = {};
                    }
                    m_RawWorkspaceStartPointDiagnostics = result.rawWorkspace.startPointDiagnostics;
                    Stack::EditorModuleTypes::StoreRawStartingPointCandidateRenderQueue(
                        m_RawWorkspaceStartPointCandidateRenderQueue,
                        result.rawWorkspace.sourceKey,
                        result.rawWorkspace.startPointCandidateRenderRequests,
                        result.generation,
                        result.rawWorkspace.sourceHash);
                    m_RawWorkspaceStartPointCandidateRenderResults =
                        result.rawWorkspace.startPointCandidateRenderResults;
                    m_RawWorkspaceAnalysis = result.rawWorkspace.analysis;
                    if (result.rawWorkspace.recommendations.localReport.valid ||
                        !result.rawWorkspace.recommendations.localSuggestionRationale.empty() ||
                        !result.rawWorkspace.recommendations.localAdjustments.empty()) {
                        m_RawWorkspaceAutoBaseUi.recommendations = result.rawWorkspace.recommendations;
                    }
                    TryContinueRawWorkspaceStartingPointOnAnalysis();
                }
                AdoptRawWorkspaceLocalRangeTargetSampleFromResult(result);
                AdoptRawWorkspaceLocalRangeOverlayFromResult(result);
                AdoptRawWorkspacePreciseAppliedRender();
            } else {
                m_RawWorkspaceViewTransformInputStats = {};
                m_RawWorkspaceFinalDisplayStats = {};
                m_RawWorkspaceStageStatsReadbacks.clear();
                m_RawWorkspaceGraphScopeReadback = {};
                m_RawWorkspaceStartPointDiagnostics =
                    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics();
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
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
            // Retain only read-only, already-computed uniform outputs. Wire
            // readouts consult this snapshot opportunistically and never
            // request evaluation merely to populate a label.
            m_LastGraphUniformOutputValues.clear();
            for (const ReductionExecutionStats& reduction : result.mainGraphStats.reductions) {
                if (!std::isfinite(reduction.value)) continue;
                m_LastGraphUniformOutputValues[
                    EditorNodeGraph::WireReadout::OutputIdentity(
                        reduction.nodeId,
                        EditorNodeGraph::kValueOutputSocketId)] =
                    Stack::NodeMath::MakeUniformScalar(reduction.value);
            }
            if (result.mainGraphStats.lastReductionFailureNodeId > 0 &&
                !result.mainGraphStats.lastReductionFailure.empty()) {
                m_LastGraphUniformOutputValues[
                    EditorNodeGraph::WireReadout::OutputIdentity(
                        result.mainGraphStats.lastReductionFailureNodeId,
                        EditorNodeGraph::kValueOutputSocketId)] =
                    Stack::NodeMath::MakeFailureValue(
                        Stack::NodeMath::LogicalValueType::Scalar,
                        Stack::NodeMath::ValueStorageClass::Uniform,
                        result.mainGraphStats.lastReductionFailure);
            }
            m_LastGraphUniformOutputGeneration = result.generation;
            m_LastCompletedRenderGeneration = result.generation;
            for (int nodeId : activeHdrMergeNodeIds) {
                m_HdrMergeCompletedGenerations[nodeId] = std::max(
                    m_HdrMergeCompletedGenerations[nodeId],
                    m_HdrMergeRequestedGenerations.count(nodeId) ? m_HdrMergeRequestedGenerations[nodeId] : GetNodeDirtyGeneration(nodeId));
                m_HdrMergeFailureMessages.erase(nodeId);
            }
        };
        const bool rawWorkspaceSourceKeyMismatch =
            !result.rawWorkspace.sourceKey.empty() &&
            result.rawWorkspace.sourceKey != m_ActiveRawWorkspaceSourceKey;
        const bool rawWorkspaceSourceMismatch =
            !result.rawWorkspace.sourceKey.empty() &&
            !rawWorkspaceResultMatchesActive;
        if (rawWorkspaceSourceMismatch) {
            if (Stack::EditorModuleTypes::ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
                    m_RawWorkspaceStartPointCandidateRenderQueue,
                    result.rawWorkspace.sourceKey,
                    m_ActiveRawWorkspaceSourceKey,
                    result.rawWorkspace.sourceHash,
                    activeRawWorkspaceSourceHash)) {
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
            }
            if (rawWorkspaceSourceKeyMismatch &&
                m_ViewportOutputRawWorkspaceSourceKey == result.rawWorkspace.sourceKey) {
                ClearViewportOutputTiles();
                m_Pipeline.ClearOutput();
                m_RawWorkspaceViewTransformInputStats = {};
                m_RawWorkspaceFinalDisplayStats = {};
                m_RawWorkspaceStageStatsReadbacks.clear();
                m_RawWorkspaceGraphScopeReadback = {};
                m_RawWorkspaceStartPointDiagnostics =
                    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics();
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
                m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
            }
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
        } else if (result.success &&
            !result.rawWorkspace.sourceKey.empty() &&
            !result.pixels.empty() &&
            result.width > 0 &&
            result.height > 0) {
            if (m_Pipeline.UploadOutputFromPixels(
                    result.pixels.data(),
                    result.width,
                    result.height,
                    4)) {
                EditorRenderWorker::SharedTextureResult uploadedTexture;
                uploadedTexture.texture =
                    m_Pipeline.TakeExternalOutputTexture(
                        uploadedTexture.width,
                        uploadedTexture.height);
                if (AdoptRawWorkspacePresentationTexture(
                        uploadedTexture)) {
                    QueueViewportOutputTileSetRelease(
                        m_ViewportOutputTiles);
                    markMainOutputAccepted(
                        RawWorkspacePreviewOutputKind::SingleTexture);
                } else {
                    result.error =
                        "Render produced no viewport texture.";
                }
            } else {
                result.error =
                    "Render output could not be uploaded.";
            }
        } else if (result.success && result.outputTexture.texture != 0) {
            if (!result.rawWorkspace.sourceKey.empty()) {
                if (AdoptRawWorkspacePresentationTexture(result.outputTexture)) {
                    QueueViewportOutputTileSetRelease(
                        m_ViewportOutputTiles);
                    markMainOutputAccepted(RawWorkspacePreviewOutputKind::SingleTexture);
                } else {
                    result.error = "Render produced no viewport texture.";
                }
            } else {
                ClearViewportOutputTiles();
                m_Pipeline.AdoptExternalOutputTexture(
                    result.outputTexture.texture,
                    result.outputTexture.width,
                    result.outputTexture.height);
                result.outputTexture.texture = 0;
                markMainOutputAccepted(RawWorkspacePreviewOutputKind::SingleTexture);
            }
        } else if (result.success && result.outputTiles.complete && !result.outputTiles.tiles.empty()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
            m_ViewportOutputTiles = std::move(result.outputTiles);
            result.outputTiles = {};
            markMainOutputAccepted(RawWorkspacePreviewOutputKind::Tiled);
        } else if (!result.success && !result.rawWorkspace.sourceKey.empty()) {
            if (rawWorkspaceResultMatchesActive &&
                result.error != "Render superseded by a newer snapshot.") {
                m_RawWorkspaceStaleRenderStatusText =
                    result.error.empty() ? "RAW render failed." : result.error;
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    "Preview retained as stale: " +
                        m_RawWorkspaceStaleRenderStatusText,
                    "raw-workspace-render-stale");
            }
            if (Stack::EditorModuleTypes::ShouldClearRawStartingPointCandidateRenderQueueForFailedResult(
                    m_RawWorkspaceStartPointCandidateRenderQueue,
                    result.rawWorkspace.sourceKey,
                    m_ActiveRawWorkspaceSourceKey,
                    result.rawWorkspace.sourceHash,
                    activeRawWorkspaceSourceHash)) {
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
            }
            if (rawWorkspaceResultMatchesActive &&
                Stack::EditorModuleTypes::MarkRawStartingPointRenderFailure(
                    m_RawWorkspaceAutoBaseUi,
                    result.error)) {
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    m_RawWorkspaceAutoBaseUi.summary,
                    "raw-workspace-starting-point-render-failed");
            }
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

        for (EditorRenderWorker::CompositeOutputResult& compositeResult : result.compositeOutputs) {
            if (!compositeResult.success || compositeResult.pixels.empty() || compositeResult.width <= 0 || compositeResult.height <= 0) {
                ResetCompositeOutputRequestForRetry(
                    compositeResult.outputNodeId);
                continue;
            }
            (void)PublishCompositeOutputPixels(
                compositeResult.outputNodeId,
                std::move(compositeResult.pixels),
                compositeResult.width,
                compositeResult.height,
                compositeResult.dirtyGeneration,
                compositeResult.chainFingerprint);
        }
        if (!m_RenderWorkerAvailable || !m_RenderWorker.IsBusy()) {
            ResetIncompleteCompositeOutputRequestsForRetry();
        }

        for (EditorRenderWorker::PreviewResult& previewResult : result.previews) {
            if (!previewResult.success ||
                previewResult.pixels.empty() ||
                previewResult.width <= 0 ||
                previewResult.height <= 0) {
                ResetPreviewRequestForRetry(
                    previewResult.previewNodeId,
                    previewResult.dirtyGeneration);
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
                ResetPreviewRequestForRetry(
                    previewResult.previewNodeId,
                    previewResult.dirtyGeneration);
                continue;
            }
            (void)PublishPreviewResultPixels(previewResult);
        }
        if (!m_RenderWorkerAvailable || !m_RenderWorker.IsBusy()) {
            ResetIncompletePreviewRequestsForRetry();
        }
        // Any texture not adopted by one of the accepted-result branches is
        // still owned by the result. Queue it here so rejected RAW sources,
        // failed outputs, and partial auxiliary surfaces cannot leak GL
        // resources.
        releaseDeferredResultResources(result);
        } catch (...) {
            releaseDeferredResultResources(result);
            ResetIncompleteCompositeOutputRequestsForRetry();
            ResetIncompletePreviewRequestsForRetry();
            m_HdrMergeSubmittedNodesByGeneration.erase(result.generation);
            m_HdrMergeRenderingNodeIds.clear();
            m_RenderPending =
                m_RenderWorkerAvailable && m_RenderWorker.IsBusy();
            m_RenderDirty = true;
        }
    }
}

void EditorModule::SubmitRenderIfReady() noexcept {
    try {
        SubmitRenderIfReadyImpl();
    } catch (...) {
        ResetIncompleteCompositeOutputRequestsForRetry();
        ResetIncompletePreviewRequestsForRetry();
        const auto submitted =
            m_HdrMergeSubmittedNodesByGeneration.find(
                m_RenderGeneration);
        if (submitted !=
            m_HdrMergeSubmittedNodesByGeneration.end()) {
            for (const int nodeId : submitted->second) {
                m_HdrMergeRequestedGenerations.erase(nodeId);
            }
            m_HdrMergeSubmittedNodesByGeneration.erase(
                submitted);
        }
        m_HdrMergeRenderingNodeIds.clear();
        m_RenderPending =
            m_RenderWorkerAvailable && m_RenderWorker.IsBusy();
        m_RenderDirty = true;
        m_LastSubmittedRenderRevision =
            m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
    }
}

void EditorModule::SubmitRenderIfReadyImpl() {
    const bool compositeMode = GetViewportMode() == ViewportMode::CompositeCanvas;
    const bool rawWorkspaceActive = IsRawWorkspaceProjectActive();
    m_Pipeline.SetRawRgbDenoiseAsyncEnabled(rawWorkspaceActive);
    if (rawWorkspaceActive &&
        m_Pipeline.ConsumeRawRgbDenoiseAsyncCompletion()) {
        // The recipe did not change, but its external model result did. Force
        // one follow-up graph evaluation so the provisional before-state is
        // replaced by the accepted denoised pixels.
        MarkRenderRefreshDirty();
    }
    const bool preciseRawSolveActive =
        rawWorkspaceActive &&
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint.active &&
        Stack::PreciseIntegration::IsRunning(
            m_RawWorkspaceAutoBaseUi.preciseStartingPoint.state);
    const bool allowBackgroundRenderWorker =
        m_RenderWorkerAvailable && (!rawWorkspaceActive || preciseRawSolveActive);
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
        try {
            previewRequests = BuildPreviewRequests();
        } catch (const std::bad_alloc&) {
            ResetIncompletePreviewRequestsForRetry();
            return;
        } catch (const std::length_error&) {
            ResetIncompletePreviewRequestsForRetry();
            return;
        }
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
        EditorRenderWorker::Snapshot snapshot;
        if (!TryBuildRenderSnapshot(
                m_RenderGeneration,
                snapshot)) {
            m_RenderPending = false;
            return;
        }
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
        if (!m_RenderWorker.Submit(std::move(snapshot))) {
            m_RenderPending = false;
        }
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
        std::vector<EditorRenderWorker::CompositeOutputRequest> requests;
        try {
            requests = BuildCompositeOutputRequests();
        } catch (const std::bad_alloc&) {
            ResetIncompleteCompositeOutputRequestsForRetry();
            m_RenderDirty = true;
            return;
        } catch (const std::length_error&) {
            ResetIncompleteCompositeOutputRequestsForRetry();
            m_RenderDirty = true;
            return;
        }
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
            EditorRenderWorker::Snapshot snapshot;
            if (!TryBuildRenderSnapshot(
                    m_RenderGeneration,
                    snapshot)) {
                m_RenderPending = false;
                m_RenderDirty = true;
                m_LastSubmittedRenderRevision =
                    m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
                return;
            }
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
            if (!m_RenderWorker.Submit(std::move(snapshot))) {
                m_RenderPending = false;
                m_RenderDirty = true;
                m_LastSubmittedRenderRevision =
                    m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
                ResetIncompleteCompositeOutputRequestsForRetry();
            }
        } else {
            m_RenderPending = false;
            for (const EditorRenderWorker::CompositeOutputRequest& request : requests) {
                int texW = 0;
                int texH = 0;
                std::vector<unsigned char> pixels;
                try {
                    pixels = GetCompositePixelsForOutputNode(
                        request.outputNodeId,
                        texW,
                        texH);
                } catch (const std::bad_alloc&) {
                    ResetCompositeOutputRequestForRetry(
                        request.outputNodeId);
                    continue;
                } catch (const std::length_error&) {
                    ResetCompositeOutputRequestForRetry(
                        request.outputNodeId);
                    continue;
                }
                (void)PublishCompositeOutputPixels(
                    request.outputNodeId,
                    std::move(pixels),
                    texW,
                    texH,
                    request.dirtyGeneration,
                    request.chainFingerprint);
            }
        }
        return;
    }
    if (!m_RenderDirty && !previewRequests.empty()) {
        submitPreviewOnlyRequests(previewRequests);
        return;
    }
    if (!m_NodeGraph.IsOutputConnected()) {
        const std::string outputDiagnostic = m_NodeGraph.GetOutputConnectionDiagnostic();
        if (!outputDiagnostic.empty() && outputDiagnostic != m_LastOutputConnectionDiagnostic) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                outputDiagnostic,
                "graph-output-compound-unresolved");
        }
        m_LastOutputConnectionDiagnostic = outputDiagnostic;
        m_RenderDirty = false;
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
        submitPreviewOnlyRequests(previewRequests);
        m_HdrMergeRenderingNodeIds.clear();
        return;
    }
    m_LastOutputConnectionDiagnostic.clear();
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
        EditorRenderWorker::Snapshot snapshot;
        if (!TryBuildRenderSnapshot(
                m_RenderGeneration,
                snapshot)) {
            m_RenderPending = false;
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision =
                m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            m_HdrMergeSubmittedNodesByGeneration.erase(
                m_RenderGeneration);
            for (const int nodeId : activeHdrMergeNodeIds) {
                m_HdrMergeRequestedGenerations.erase(nodeId);
                m_HdrMergeRenderingNodeIds.erase(nodeId);
            }
            return;
        }
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        snapshot.previews = std::move(previewRequests);
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = true;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
        if (!m_RenderWorker.Submit(std::move(snapshot))) {
            m_RenderPending = false;
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision =
                m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            m_HdrMergeSubmittedNodesByGeneration.erase(m_RenderGeneration);
            for (const int nodeId : activeHdrMergeNodeIds) {
                m_HdrMergeRequestedGenerations.erase(nodeId);
                m_HdrMergeRenderingNodeIds.erase(nodeId);
            }
        }
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
        EditorRenderWorker::Snapshot snapshot;
        if (!TryBuildRenderSnapshot(
                m_RenderGeneration,
                snapshot)) {
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision =
                m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            return;
        }
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        m_Pipeline.SetPreviewMaxDimension(snapshot.previewMaxDimension);
        m_Pipeline.SetRawDevelopmentAnalysisEnabled(snapshot.rawWorkspace.analysisRequested);
        m_Pipeline.SetRawDevelopmentGraphScopeReadbackRequest(
            snapshot.rawWorkspace.graphScopeStage,
            snapshot.rawWorkspace.graphScopeStage == RawDevelopmentGraphScopeStage::None
                ? 0
                : kRawWorkspaceGraphScopeMaxDimension);
        const auto mainRenderBegin = std::chrono::steady_clock::now();
        m_Pipeline.ExecuteGraph(snapshot.graph);
        const auto graphExecuteEnd = std::chrono::steady_clock::now();
        const std::string rawRgbDenoiseError =
            rawWorkspaceActive
                ? m_Pipeline.GetLastRawRgbDenoiseError()
                : std::string();
        const bool preserveRawPresentationForTargetHover =
            rawWorkspaceActive &&
            Stack::RawLocalRangeTargetInteraction::
                ShouldPreserveBasePresentation(
                    snapshot.rawWorkspace.localRangeTargetSampleRequested &&
                        snapshot.rawWorkspace.localRangeTargetHoverSample,
                    snapshot.rawWorkspace.localRangeOverlayMode ==
                        "target-outline",
                    snapshot.rawWorkspace.localRangeTargetPreview.enabled,
                    snapshot.rawWorkspace.localRangeTargetPreview
                        .interactionEditing);
        bool publishedRawPresentation = false;
        if (rawWorkspaceActive &&
            !preserveRawPresentationForTargetHover &&
            m_Pipeline.GetOutputTexture() != 0) {
            EditorRenderWorker::SharedTextureResult publishedTexture;
            publishedTexture.texture =
                m_Pipeline.PublishSharedOutputTexture(
                    publishedTexture.width,
                    publishedTexture.height,
                    true);
            if (AdoptRawWorkspacePresentationTexture(publishedTexture)) {
                // The presentation copy is owned by EditorModule rather than
                // RenderPipeline. Graph execution can now invalidate or
                // recycle its internal output without touching a texture
                // already referenced by the current ImGui draw list.
                QueueViewportOutputTileSetRelease(m_ViewportOutputTiles);
                publishedRawPresentation = true;
                if (snapshot.rawWorkspace.localRangeTargetPreview
                        .interactionEditing) {
                    m_RawWorkspaceLocalRangeTargetPreview.interactionEditing =
                        false;
                }
            }
        } else if (rawWorkspaceActive &&
                   !rawRgbDenoiseError.empty()) {
            m_RawWorkspaceStaleRenderStatusText =
                rawRgbDenoiseError;
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Preview retained as stale: " +
                    rawRgbDenoiseError,
                "raw-workspace-restormer-stale");
        }
        if (rawWorkspaceActive) {
            int overlayWidth = 0;
            int overlayHeight = 0;
            Stack::Renderer::ScopedGLTexture renderedOverlay(
                m_Pipeline.TakeRawDevelopmentLocalRangeOverlayTexture(
                    overlayWidth,
                    overlayHeight));

            const bool renderedOverlayMatches =
                renderedOverlay &&
                overlayWidth > 0 &&
                overlayHeight > 0 &&
                !snapshot.rawWorkspace.localRangeOverlayMode.empty() &&
                snapshot.rawWorkspace.localRangeOverlayMode != "none" &&
                snapshot.rawWorkspace.localRangeOverlayMode ==
                    m_RawWorkspaceLocalRangeOverlayMode &&
                (snapshot.rawWorkspace.localRangeOverlayMode != "target-outline" ||
                    snapshot.rawWorkspace.localRangeTargetPreview.generation ==
                        m_RawWorkspaceLocalRangeTargetPreview.generation);
            if (renderedOverlayMatches) {
                std::string acceptedSourceKey =
                    m_ActiveRawWorkspaceSourceKey;
                std::string acceptedMode =
                    snapshot.rawWorkspace.localRangeOverlayMode;
                ClearRawWorkspaceLocalRangeOverlayState();
                m_RawWorkspaceLocalRangeOverlayTexture =
                    renderedOverlay.Release();
                m_RawWorkspaceLocalRangeOverlayWidth = overlayWidth;
                m_RawWorkspaceLocalRangeOverlayHeight = overlayHeight;
                m_RawWorkspaceLocalRangeOverlaySourceKey =
                    std::move(acceptedSourceKey);
                m_RawWorkspaceLocalRangeOverlayAcceptedMode =
                    std::move(acceptedMode);
                m_RawWorkspaceLocalRangeOverlayGeneration =
                    m_RenderGeneration;
                m_RawWorkspaceLocalRangeOverlayTargetPreviewGeneration =
                    snapshot.rawWorkspace.localRangeTargetPreview.generation;
                if (snapshot.rawWorkspace.localRangeOverlayMode ==
                    "target-outline") {
                    m_RawWorkspaceLocalRangeTargetPreviewRefined =
                        m_Pipeline
                            .IsRawDevelopmentLocalRangeTargetPreviewRefined();
                    m_RawWorkspaceLocalRangeTargetPreviewRefinementPending =
                        m_Pipeline
                            .IsRawDevelopmentLocalRangeTargetPreviewRefinementPending();
                }
            } else if (!preserveRawPresentationForTargetHover) {
                ClearRawWorkspaceLocalRangeOverlayState();
            }
        }
        if (rawWorkspaceActive && snapshot.rawWorkspace.analysisRequested) {
            m_RawWorkspaceViewTransformInputStats = m_Pipeline.GetRawDevelopmentViewTransformInputStats();
            m_RawWorkspaceFinalDisplayStats = m_Pipeline.GetRawDevelopmentFinalDisplayStats();
            m_RawWorkspaceStageStatsReadbacks = m_Pipeline.GetRawDevelopmentStageStatsReadbacks();
            m_RawWorkspaceGraphScopeReadback =
                m_Pipeline.GetRawDevelopmentGraphScopeReadback();
            if (publishedRawPresentation) {
                CacheRawWorkspaceGraphScopeReadback(
                    snapshot.rawWorkspace.sourceKey,
                    snapshot.rawWorkspace.graphScopeInputFingerprint,
                    m_RawWorkspaceGraphScopeReadback);
            }
            m_RawWorkspaceStartPointDiagnostics =
                m_Pipeline.BuildRawDevelopmentStartPointDiagnostics(m_ActiveRawWorkspaceSourceKey);
            m_RawWorkspaceAnalysis =
                Stack::RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
                    ToRawCurrentFrameInputStats(m_RawWorkspaceViewTransformInputStats),
                    m_ActiveRawWorkspaceSourceKey);
            // The automatic starting-point UI is archived. Keep its backend
            // callable for explicit validation/precise jobs, but ordinary
            // manual renders must not synthesize recommendations or render
            // hidden candidates after every edit.
            Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                m_RawWorkspaceStartPointCandidateRenderQueue);
            m_RawWorkspaceStartPointCandidateRenderResults.clear();
            AdoptRawWorkspacePreciseAppliedRender();
        } else if (!rawWorkspaceActive) {
            m_RawWorkspaceViewTransformInputStats = {};
            m_RawWorkspaceFinalDisplayStats = {};
            m_RawWorkspaceStageStatsReadbacks.clear();
            m_RawWorkspaceGraphScopeReadback = {};
            m_RawWorkspaceStartPointDiagnostics =
                Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics();
            Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                m_RawWorkspaceStartPointCandidateRenderQueue);
            m_RawWorkspaceStartPointCandidateRenderResults.clear();
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
            std::uint32_t authoredZoneHitBits = 0;
            float strongestAuthoredZoneWeight = 0.0f;
            if (m_Pipeline.GetRawDevelopmentLocalRangeTargetSample(
                    sceneEv,
                    sceneLuma,
                    sampleU,
                    sampleV,
                    &sceneRgb,
                    &authoredZoneHitBits,
                    &strongestAuthoredZoneWeight)) {
                targetSampleResult.rawWorkspace.localRangeTargetSample.valid = true;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneEv = sceneEv;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneLuma = sceneLuma;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneR = sceneRgb[0];
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneG = sceneRgb[1];
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneB = sceneRgb[2];
                targetSampleResult.rawWorkspace.localRangeTargetSample.u = sampleU;
                targetSampleResult.rawWorkspace.localRangeTargetSample.v = sampleV;
                targetSampleResult.rawWorkspace.localRangeTargetSample
                    .authoredZoneHitBits = authoredZoneHitBits;
                targetSampleResult.rawWorkspace.localRangeTargetSample
                    .strongestAuthoredZoneWeight =
                    strongestAuthoredZoneWeight;
            }
            AdoptRawWorkspaceLocalRangeTargetSampleFromResult(targetSampleResult);
        }
        const auto mainRenderEnd = std::chrono::steady_clock::now();
        m_GraphPerformanceStats.lastMainRenderMs =
            MillisecondsBetween(mainRenderBegin, mainRenderEnd);
        m_GraphPerformanceStats.lastMainGraphExecuteMs =
            MillisecondsBetween(mainRenderBegin, graphExecuteEnd);
        m_GraphPerformanceStats.lastMainPostExecuteMs =
            MillisecondsBetween(graphExecuteEnd, mainRenderEnd);
        m_GraphPerformanceStats.lastRawWorkspaceRender = rawWorkspaceActive;
        m_GraphPerformanceStats.lastRawInteractivePreview =
            rawWorkspaceActive && snapshot.previewMaxDimension > 0;
        m_GraphPerformanceStats.lastRawAnalysisCaptured =
            rawWorkspaceActive && snapshot.rawWorkspace.analysisRequested;
        m_GraphPerformanceStats.lastRawPreviewMaxDimension =
            snapshot.previewMaxDimension;
        m_GraphPerformanceStats.lastMainGraphStats = m_Pipeline.GetLastGraphExecutionStats();
        m_GraphPerformanceStats.lastPreviewRenderMs = 0.0f;
        m_GraphPerformanceStats.lastCompositeRenderMs = 0.0f;
        m_GraphPerformanceStats.lastRenderedPreviewCount = 0;
        m_GraphPerformanceStats.lastRenderedCompositeCount = 0;
        m_GraphPerformanceStats.lastMainOutputTiled = false;
        m_GraphPerformanceStats.lastMainOutputTileCount = 0;
        m_GraphPerformanceStats.lastMainRegionPlanAvailable = false;
        m_GraphPerformanceStats.lastMainRegionPlanTileable = false;
        m_GraphPerformanceStats.lastMainRegionPlanHaloX = 0;
        m_GraphPerformanceStats.lastMainRegionPlanHaloY = 0;
        m_GraphPerformanceStats.lastMainRegionPlanReason.clear();
        m_RenderPending = false;
        if (rawWorkspaceActive) {
            if (publishedRawPresentation) {
                m_RawWorkspacePreviewOutputKind =
                    RawWorkspacePreviewOutputKind::SingleTexture;
                m_ViewportOutputRawWorkspaceSourceKey =
                    m_ActiveRawWorkspaceSourceKey;
                m_ViewportOutputPreviewMaxDimension =
                    snapshot.previewMaxDimension;
                m_ViewportOutputRenderGeneration = m_RenderGeneration;
            }
            if (snapshot.previewMaxDimension == 0 &&
                !m_RenderDirty &&
                !m_Pipeline.IsRawRgbDenoiseAsyncPending() &&
                rawRgbDenoiseError.empty()) {
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
