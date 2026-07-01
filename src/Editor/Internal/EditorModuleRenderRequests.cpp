#include "Editor/EditorModule.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <vector>

namespace {

constexpr int kScalableGeneratorBaseRaster = 1024;
constexpr int kScalableGeneratorMaxRaster = 4096;
constexpr double kPreviewLikeRefreshQuietSeconds = 0.18;

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width * height * 4), 0);
}

} // namespace

bool EditorModule::IsRecentRawDevelopInteraction(double now) const {
    if (now < 0.0) {
        if (ImGui::GetCurrentContext() == nullptr) {
            return false;
        }
        now = ImGui::GetTime();
    }
    return m_LastRawDevelopInteractionTime >= 0.0 &&
        (now - m_LastRawDevelopInteractionTime) < 0.35;
}

bool EditorModule::CanRefreshPreviewLikeNodes() const {
    return !ShouldDeferPreviewLikeWork();
}

bool EditorModule::ShouldDeferPreviewLikeWork(double now) const {
    if (m_RenderDirty || m_RenderPending || m_RenderWorker.IsBusy()) {
        return true;
    }

    if (now < 0.0) {
        now = ImGui::GetCurrentContext() ? ImGui::GetTime() : -1.0;
    }
    if (now >= 0.0) {
        if (now - m_LastRenderDirtyTime < kPreviewLikeRefreshQuietSeconds) {
            return true;
        }
        if (IsRecentRawDevelopInteraction(now)) {
            return true;
        }
    } else if (IsRecentRawDevelopInteraction()) {
        return true;
    }

    return false;
}

bool EditorModule::HasPendingPreviewRefreshes() const {
    if (!CanRefreshPreviewLikeNodes()) {
        return true;
    }

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Preview &&
            node.kind != EditorNodeGraph::NodeKind::RawDetailAutoMask) {
            continue;
        }
        if (GetPreviewNodeRevision(node.id) == 0) {
            continue;
        }
        const std::uint64_t desiredRevision = GetPreviewNodeRevision(node.id);
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
    SharedPixelBuffer pipelineSourcePixels;
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
        const bool scalableGenerator = CompletedChainSourceUsesScalableGenerator(chainState.info.outputNodeId);
        const float scaleX = std::max(0.01f, std::abs(item->scale.x));
        const float scaleY = std::max(0.01f, std::abs(item->scale.y));
        const int desiredRasterWidth = scalableGenerator
            ? std::clamp(
                static_cast<int>(std::ceil(std::max(
                    static_cast<float>(kScalableGeneratorBaseRaster),
                    std::max(1.0f, static_cast<float>(item->textureWidth <= 0 ? kScalableGeneratorBaseRaster : item->textureWidth)) * scaleX))),
                256,
                kScalableGeneratorMaxRaster)
            : std::max(1, item->textureWidth);
        const int desiredRasterHeight = scalableGenerator
            ? std::clamp(
                static_cast<int>(std::ceil(std::max(
                    static_cast<float>(kScalableGeneratorBaseRaster),
                    std::max(1.0f, static_cast<float>(item->textureHeight <= 0 ? kScalableGeneratorBaseRaster : item->textureHeight)) * scaleY))),
                256,
                kScalableGeneratorMaxRaster)
            : std::max(1, item->textureHeight);
        const bool needsTextureRefresh =
            item->texture == 0 ||
            completedGeneration < dirtyGeneration ||
            item->cachedChainFingerprint != chainState.fingerprint ||
            (scalableGenerator &&
             (item->textureWidth < desiredRasterWidth || item->textureHeight < desiredRasterHeight));
        const bool requestAlreadyPending =
            requestedGeneration >= dirtyGeneration &&
            item->requestedChainFingerprint == chainState.fingerprint &&
            (!scalableGenerator ||
             (item->requestedRasterWidth >= desiredRasterWidth &&
              item->requestedRasterHeight >= desiredRasterHeight));
        if (!needsTextureRefresh || requestAlreadyPending) {
            continue;
        }

        EditorRenderWorker::CompositeOutputRequest request;
        request.outputNodeId = chainState.info.outputNodeId;
        request.sourceNodeId = m_NodeGraph.ResolveReferenceSourceNodeIdForOutput(chainState.info.outputNodeId);
        if (request.sourceNodeId <= 0) {
            request.sourceNodeId = chainState.info.sourceNodeId;
        }

        if (TryResolveReferenceSourceBufferForOutput(
                chainState.info.outputNodeId,
                request.sourcePixels,
                request.width,
                request.height,
                request.channels)) {
            // Render this output against its own reference canvas.
        } else if (TryCopyImageNodeSharedPixels(
            request.sourceNodeId,
            request.sourcePixels,
            request.width,
            request.height,
            request.channels)) {
            // Reuse source-node pixels or a transparent RAW reference buffer.
        } else if (!m_Pipeline.GetSourcePixelsRaw().empty() &&
            m_Pipeline.GetCanvasWidth() > 0 &&
            m_Pipeline.GetCanvasHeight() > 0) {
            if (pipelineSourcePixels.empty()) {
                pipelineSourcePixels = MakeSharedSourcePixelBufferCopy(m_Pipeline.GetSourcePixelsRaw());
            }
            request.sourcePixels = pipelineSourcePixels;
            request.width = m_Pipeline.GetCanvasWidth();
            request.height = m_Pipeline.GetCanvasHeight();
            request.channels = std::max(1, m_Pipeline.GetSourceChannels());
        } else if (scalableGenerator) {
            request.width = desiredRasterWidth;
            request.height = desiredRasterHeight;
            request.channels = 4;
            request.sourcePixels = MakeSharedPixelBufferOwned(BuildTransparentPixels(request.width, request.height));
        } else {
            request.width = 256;
            request.height = 256;
            request.channels = 4;
            request.sourcePixels = MakeSharedPixelBufferOwned(BuildTransparentPixels(request.width, request.height));
        }

        request.dirtyGeneration = dirtyGeneration;
        request.chainFingerprint = chainState.fingerprint;
        item->requestedRenderRevision = dirtyGeneration;
        item->requestedChainFingerprint = chainState.fingerprint;
        item->requestedRasterWidth = request.width;
        item->requestedRasterHeight = request.height;
        m_CompositeOutputRequestedGenerations[chainState.info.outputNodeId] = dirtyGeneration;
        requests.push_back(std::move(request));
    }

    return requests;
}

std::vector<EditorRenderWorker::PreviewRequest> EditorModule::BuildPreviewRequests() {
    std::vector<EditorRenderWorker::PreviewRequest> requests;
    SharedPixelBuffer pipelineSourcePixels;
    SharedPixelBuffer fallbackImagePixels;
    int fallbackImageWidth = 0;
    int fallbackImageHeight = 0;
    int fallbackImageChannels = 4;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Preview &&
            node.kind != EditorNodeGraph::NodeKind::RawDetailAutoMask) {
            continue;
        }

        const bool generatedAutoMaskPreview = node.kind == EditorNodeGraph::NodeKind::RawDetailAutoMask;
        const EditorNodeGraph::Link* input = generatedAutoMaskPreview
            ? m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId)
            : m_NodeGraph.FindAnyInputLink(node.id, EditorNodeGraph::kPreviewInputSocketId);
        if (!input) {
            m_PreviewRequestedGenerations.erase(node.id);
            m_PreviewCompletedGenerations.erase(node.id);
            m_PreviewPixelCache.erase(node.id);
            continue;
        }

        EditorNodeGraph::SocketDefinition sourceSocket;
        const int sourceNodeId = generatedAutoMaskPreview ? node.id : input->fromNodeId;
        const std::string sourceSocketId = generatedAutoMaskPreview
            ? EditorNodeGraph::kMaskOutputSocketId
            : input->fromSocketId;
        if (!m_NodeGraph.FindSocket(sourceNodeId, sourceSocketId, &sourceSocket)) {
            continue;
        }
        if (sourceSocket.type != EditorNodeGraph::SocketType::Image &&
            sourceSocket.type != EditorNodeGraph::SocketType::Mask) {
            continue;
        }

        const std::uint64_t dirtyGeneration = GetPreviewNodeRevision(node.id);
        if (dirtyGeneration == 0) {
            continue;
        }
        const std::uint64_t requestedGeneration =
            m_PreviewRequestedGenerations.count(node.id) ? m_PreviewRequestedGenerations[node.id] : 0;
        const std::uint64_t completedGeneration =
            m_PreviewCompletedGenerations.count(node.id) ? m_PreviewCompletedGenerations[node.id] : 0;
        if (requestedGeneration >= dirtyGeneration || completedGeneration >= dirtyGeneration) {
            continue;
        }

        EditorRenderWorker::PreviewRequest request;
        request.previewNodeId = node.id;
        request.sourceNodeId = sourceNodeId;
        request.sourceSocketId = sourceSocketId;
        request.maskInput = sourceSocket.type == EditorNodeGraph::SocketType::Mask;
        request.directSourceOutput = true;
        request.dirtyGeneration = dirtyGeneration;

        if (TryResolveReferenceSourceBuffer(
                sourceNodeId,
                sourceSocketId,
                request.sourcePixels,
                request.width,
                request.height,
                request.channels)) {
            // Preview channel/combined streams on their resolved reference canvas.
        } else if (TryCopyImageNodeSharedPixels(
            sourceNodeId,
            request.sourcePixels,
            request.width,
            request.height,
            request.channels)) {
            // Direct image/RAW source preview.
        } else {
            if (pipelineSourcePixels.empty() && !m_Pipeline.GetSourcePixelsRaw().empty()) {
                pipelineSourcePixels = MakeSharedSourcePixelBufferCopy(m_Pipeline.GetSourcePixelsRaw());
            }
            request.sourcePixels = pipelineSourcePixels;
            request.width = m_Pipeline.GetCanvasWidth();
            request.height = m_Pipeline.GetCanvasHeight();
            request.channels = std::max(1, m_Pipeline.GetSourceChannels());
        }
        if (request.sourcePixels.empty()) {
            if (fallbackImagePixels.empty()) {
                for (const EditorNodeGraph::Node& graphNode : m_NodeGraph.GetNodes()) {
                    if (graphNode.kind != EditorNodeGraph::NodeKind::Image ||
                        graphNode.image.pixels.empty() ||
                        graphNode.image.width <= 0 ||
                        graphNode.image.height <= 0) {
                        continue;
                    }
                    fallbackImagePixels = EnsureSharedImagePixels(graphNode.image);
                    fallbackImageWidth = graphNode.image.width;
                    fallbackImageHeight = graphNode.image.height;
                    fallbackImageChannels = std::max(1, graphNode.image.channels);
                    break;
                }
            }
            request.sourcePixels = fallbackImagePixels;
            request.width = fallbackImageWidth;
            request.height = fallbackImageHeight;
            request.channels = fallbackImageChannels;
        }
        if (request.sourcePixels.empty() || request.width <= 0 || request.height <= 0) {
            request.width = 256;
            request.height = 256;
            request.channels = 4;
            request.sourcePixels = MakeSharedPixelBufferOwned(
                BuildTransparentPixels(request.width, request.height));
        }

        m_PreviewRequestedGenerations[node.id] = dirtyGeneration;
        requests.push_back(std::move(request));
    }
    return requests;
}
