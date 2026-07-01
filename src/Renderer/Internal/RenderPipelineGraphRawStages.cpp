#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Raw/RawLoader.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <utility>

using namespace Stack::Renderer::GraphExecution;

const RenderGraphNode* RenderPipeline::FindUpstreamRawSourceNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& rawConsumer) {
    const RenderGraphLink* rawInput = executionContext.FindInputLink(rawConsumer.nodeId, "rawIn");
    std::set<int> rawVisit;
    while (rawInput) {
        if (!rawVisit.insert(rawInput->fromNodeId).second) {
            break;
        }
        const auto rawIt = executionContext.nodes.find(rawInput->fromNodeId);
        if (rawIt == executionContext.nodes.end() || !rawIt->second) {
            break;
        }
        if (rawIt->second->kind == RenderGraphNodeKind::RawSource) {
            return rawIt->second;
        }
        if (rawIt->second->kind != RenderGraphNodeKind::RawNeuralDenoise) {
            break;
        }
        rawInput = executionContext.FindInputLink(rawIt->second->nodeId, "rawIn");
    }
    return nullptr;
}

RenderPipeline::SharedRawBaseStageResult RenderPipeline::RenderSharedRawBaseStage(
    GraphExecutionContext& executionContext,
    const RenderGraphNode& rawConsumer,
    const Raw::RawDevelopSettings& settings,
    const std::string& rawBaseKey,
    std::size_t rawBaseFingerprint,
    const std::function<std::size_t(int, const std::string&)>& fingerprintImage) {
    SharedRawBaseStageResult stage;
    stage.rawSource = FindUpstreamRawSourceNode(executionContext, rawConsumer);
    if (!stage.rawSource) {
        return stage;
    }

    const bool hasEmbeddedRaw =
        !stage.rawSource->rawSource.embeddedRawData.rawBuffer.empty() ||
        !stage.rawSource->rawSource.embeddedRawData.linearUInt16Buffer.empty() ||
        !stage.rawSource->rawSource.embeddedRawData.linearFloatBuffer.empty();
    stage.sourcePath = stage.rawSource->rawSource.sourcePath.empty()
        ? stage.rawSource->rawSource.metadata.sourcePath
        : stage.rawSource->rawSource.sourcePath;

    Raw::RawImageData& rawData = m_RawDataCache[stage.rawSource->nodeId];
    std::string& cachedPath = m_RawDataCachePaths[stage.rawSource->nodeId];
    std::string cacheKeyPath = hasEmbeddedRaw
        ? std::string("__embedded_raw__:") + std::to_string(fingerprintImage(stage.rawSource->nodeId, "rawOut"))
        : stage.sourcePath;
    if (!hasEmbeddedRaw) {
        std::error_code statError;
        const std::uintmax_t fileSize = std::filesystem::file_size(stage.sourcePath, statError);
        const std::int64_t modifiedTicks = statError
            ? 0
            : static_cast<std::int64_t>(
                std::filesystem::last_write_time(stage.sourcePath, statError).time_since_epoch().count());
        cacheKeyPath += "#" + std::to_string(statError ? 0 : fileSize);
        cacheKeyPath += "#" + std::to_string(statError ? 0 : modifiedTicks);
    }
    if (hasEmbeddedRaw) {
        if (cachedPath != cacheKeyPath) {
            rawData = stage.rawSource->rawSource.embeddedRawData;
            cachedPath = cacheKeyPath;
        }
    } else if (cachedPath != cacheKeyPath ||
        (rawData.rawBuffer.empty() && rawData.linearUInt16Buffer.empty() && rawData.linearFloatBuffer.empty())) {
        Raw::RawImageData loadedRaw;
        if (Raw::RawLoader::LoadFile(stage.sourcePath, loadedRaw)) {
            rawData = std::move(loadedRaw);
            cachedPath = cacheKeyPath;
        } else {
            rawData = std::move(loadedRaw);
            cachedPath = cacheKeyPath;
        }
    }

    const bool rawDataHasPixels =
        !rawData.rawBuffer.empty() ||
        !rawData.linearUInt16Buffer.empty() ||
        !rawData.linearFloatBuffer.empty();
    if (!rawDataHasPixels || !rawData.metadata.error.empty()) {
        const std::string error = !rawData.metadata.error.empty()
            ? rawData.metadata.error
            : "LibRaw did not produce a usable raw buffer.";
        std::cerr << "[RAW] Load failed for source node " << stage.rawSource->nodeId
                  << " (" << stage.sourcePath << "): " << error << "\n";
        return stage;
    }

    if (stage.rawSource->rawSource.metadata.visibleWidth > 0) {
        rawData.metadata.visibleWidth = stage.rawSource->rawSource.metadata.visibleWidth;
    }
    if (stage.rawSource->rawSource.metadata.visibleHeight > 0) {
        rawData.metadata.visibleHeight = stage.rawSource->rawSource.metadata.visibleHeight;
    }

    if (const auto cachedBase = m_GraphImageCache.find(rawBaseKey);
        cachedBase != m_GraphImageCache.end() &&
        cachedBase->second.fingerprint == rawBaseFingerprint &&
        cachedBase->second.texture != 0 &&
        cachedBase->second.owned &&
        cachedBase->second.width > 0 &&
        cachedBase->second.height > 0) {
        ++m_LastGraphExecutionStats.rawStageCacheHits;
        stage.texture = cachedBase->second.texture;
        stage.width = cachedBase->second.width;
        stage.height = cachedBase->second.height;
        m_Width = stage.width;
        m_Height = stage.height;
        m_LastGraphImageCacheHits.insert(rawBaseKey);
    } else if (const CachedGraphTexture stageCachedBase = FindRawDevelopStageCacheEntry(rawBaseKey, rawBaseFingerprint);
        stageCachedBase.texture != 0 &&
        stageCachedBase.width > 0 &&
        stageCachedBase.height > 0) {
        ++m_LastGraphExecutionStats.rawStageCacheHits;
        stage.width = stageCachedBase.width;
        stage.height = stageCachedBase.height;
        m_Width = stage.width;
        m_Height = stage.height;
        const unsigned int ownedStageCopy =
            CloneTextureForGraphCache(stageCachedBase.texture, stage.width, stage.height);
        if (ownedStageCopy != 0) {
            stage.texture = ownedStageCopy;
            StoreGraphCacheEntry(m_GraphImageCache, rawBaseKey, ownedStageCopy, rawBaseFingerprint, true);
        } else {
            stage.texture = stageCachedBase.texture;
        }
        m_LastGraphImageCacheHits.insert(rawBaseKey);
    }

    if (stage.texture == 0) {
        ++m_LastGraphExecutionStats.rawStageCacheMisses;
        const Raw::RawImageData& renderRawData =
            ResolveRawPreviewRenderData(stage.rawSource->nodeId, rawData, cacheKeyPath);
        stage.texture = m_RawPipelines[rawConsumer.nodeId].Render(renderRawData, settings, m_PreviewMaxDimension);
        if (stage.texture == 0) {
            ReleaseGraphCacheEntry(m_GraphImageCache, rawBaseKey);
            const std::string& error = m_RawPipelines[rawConsumer.nodeId].GetLastError();
            std::cerr << "[RAW] Render failed for "
                      << (rawConsumer.kind == RenderGraphNodeKind::RawDecode ? "decode" : "develop")
                      << " node " << rawConsumer.nodeId
                      << " (" << stage.sourcePath << "): "
                      << (error.empty() ? "unknown RAW GPU failure" : error)
                      << "\n";
            return stage;
        }
        stage.width = m_RawPipelines[rawConsumer.nodeId].GetOutputWidth();
        stage.height = m_RawPipelines[rawConsumer.nodeId].GetOutputHeight();
        stage.renderedThisPass = true;
    }

    if (stage.texture != 0) {
        if (stage.width <= 0) {
            stage.width = m_RawPipelines[rawConsumer.nodeId].GetOutputWidth();
        }
        if (stage.height <= 0) {
            stage.height = m_RawPipelines[rawConsumer.nodeId].GetOutputHeight();
        }
        if (stage.width > 0 && stage.height > 0) {
            m_Width = stage.width;
            m_Height = stage.height;
        }
        if (stage.renderedThisPass) {
            StoreRawDevelopStageCacheEntry(rawBaseKey, stage.texture, rawBaseFingerprint);
        }
        executionContext.imageCache[rawBaseKey] = stage.texture;
    }

    return stage;
}
