#include "Editor/Internal/RawLab/RawLabAreaMask.h"
#include "Editor/Internal/EditorRenderPresentationHelpers.h"
#include "Renderer/ViewportTextureCopy.h"
#include "Editor/EditorModule.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/Bracketing/BracketingGallery.h"

#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/RawRenderPlanning.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Editor/RawRenderSourceRecipe.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawGpuMemoryBudget.h"
#include "Raw/RawLoader.h"
#include "Raw/RawProcessingMath.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/RawGraphViewportWorkload.h"
#include "Renderer/ScopedGLObjects.h"
#include "Utils/PixelBufferUtils.h"
#include "Utils/PngEncodingUtils.h"
#include "Utils/HashUtils.h"
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

using namespace Stack::EditorRendering::Internal;
using Stack::EditorRendering::ReadMergedRenderingRecipe;

namespace {

constexpr int kRawDevelopComplexPreviewMaxDimension = 2048;
// Grading scopes need enough source samples to make the higher-resolution
// vectorscope and parade meaningful. This remains an asynchronous readback.
constexpr std::size_t kViewportOutputTileTextureDeletesPerFrame = 12;

std::size_t RawLayerScopeFingerprint(const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const std::string& layerId, std::uint64_t revision, RawDevelopmentGraphScopeStage stage, int dimension) {
    using namespace Stack::Renderer::RawDevelopmentCache;
    auto result = BuildStageFingerprint(recipe, dimension, Stage::PostOutputCrop);
    StackHash::HashCombine(result, StackHash::HashValue(layerId));
    StackHash::HashCombine(result, revision);
    StackHash::HashCombine(result, static_cast<int>(stage));
    return result == 0 ? 1 : result;
}

std::optional<Stack::Renderer::RawDevelopmentCache::Stage>
ResolvePreferredRawCacheInputStage(
    Stack::EditorModuleTypes::RawLabTool tool) {
    using Stage = Stack::Renderer::RawDevelopmentCache::Stage;
    using Tool = Stack::EditorModuleTypes::RawLabTool;
    switch (tool) {
        case Tool::Denoise: return Stage::RawBase;
        case Tool::RgbDenoise: return Stage::RawBase;
        case Tool::Light: return Stage::NeutralPlacement;
        case Tool::Exposure: return Stage::NeutralPlacement;
        case Tool::Zones: return Stage::RawPlacement;
        case Tool::Tone: return Stage::PostLocalRange;
        case Tool::Color: return Stage::PostFinishTone;
        case Tool::View: return Stage::PostColorWarp;
        default: return std::nullopt;
    }
}

void ResolveRawFullFrameDimensions(
    const Raw::RawImageData* activeSource,
    const RenderGraphSnapshot& graph,
    int& width,
    int& height) {
    width = 0;
    height = 0;
    const auto resolveMetadataDimensions = [&](const Raw::RawMetadata& metadata) {
        int visibleWidth = metadata.visibleWidth > 0
            ? metadata.visibleWidth
            : metadata.rawWidth;
        int visibleHeight = metadata.visibleHeight > 0
            ? metadata.visibleHeight
            : metadata.rawHeight;
        if (metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer) {
            const Raw::RawSensorRect activeArea =
                Raw::Processing::ResolveActiveArea(metadata);
            const int activeWidth = activeArea.right - activeArea.left;
            const int activeHeight = activeArea.bottom - activeArea.top;
            if (activeWidth > 0 && activeHeight > 0) {
                visibleWidth = activeWidth;
                visibleHeight = activeHeight;
            }
        }
        if (visibleWidth <= 0 || visibleHeight <= 0) {
            return;
        }
        const bool swapsDimensions =
            metadata.orientation >= 5 && metadata.orientation <= 8;
        width = swapsDimensions ? visibleHeight : visibleWidth;
        height = swapsDimensions ? visibleWidth : visibleHeight;
    };
    // Brackets arrive directly on RAW development, without a RawSource node
    // or a gallery source. Use the published mosaic before any older selection.
    for (const RenderGraphNode& node : graph.nodes) {
        if ((node.kind == RenderGraphNodeKind::RawDevelopment ||
             node.kind == RenderGraphNodeKind::RawProjectSourceSet) &&
            node.rawDevelopment.embeddedRawData) {
            resolveMetadataDimensions(node.rawDevelopment.embeddedRawData->metadata);
            if (width > 0 && height > 0) return;
        }
    }
    if (activeSource != nullptr) {
        resolveMetadataDimensions(activeSource->metadata);
    }
    if (width > 0 && height > 0) {
        return;
    }
    for (const RenderGraphNode& node : graph.nodes) {
        if (node.kind != RenderGraphNodeKind::RawSource) {
            continue;
        }
        resolveMetadataDimensions(node.rawSource.metadata);
        if (width > 0 && height > 0) {
            return;
        }
    }
}

std::size_t HashJsonValue(const nlohmann::json& value) {
    return std::hash<std::string>{}(value.dump());
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
    const auto* cached = GetCachedPreviewPixelsForNode(nodeId);
    outW = cached ? cached->width : 0;
    outH = cached ? cached->height : 0;
    return cached ? cached->pixels : std::vector<unsigned char>{};
}

std::vector<unsigned char> EditorModule::GetPreviewPixelsForNode(int nodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;

    const EditorNodeGraph::Node* previewNode = m_Project->graph.FindNode(nodeId);
    if (!previewNode || previewNode->kind != EditorNodeGraph::NodeKind::Preview) {
        return {};
    }

    const EditorNodeGraph::Link* input = m_Project->graph.FindAnyInputLink(nodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return {};
    }

    EditorNodeGraph::SocketDefinition sourceSocket;
    if (!m_Project->graph.FindSocket(input->fromNodeId, input->fromSocketId, &sourceSocket)) {
        return {};
    }

    const EditorNodeGraph::Node* sourceNode = m_Project->graph.FindNode(input->fromNodeId);
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
        for (const EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
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

bool EditorModule::CaptureSettledFullQualityPreviewRaster(
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int maxDimension) {
    outPixels.clear();
    outW = 0;
    outH = 0;
    if (!IsRenderSettledForFullQualityExport()) return false;
    if (IsRawWorkspaceProjectActive() &&
        m_RawWorkspacePresentationTexture.texture != 0) {
        outPixels = m_Pipeline.GetExternalTexturePixels(
            m_RawWorkspacePresentationTexture.texture,
            m_RawWorkspacePresentationTexture.width,
            m_RawWorkspacePresentationTexture.height,
            outW,
            outH,
            std::max(1, maxDimension));
    } else {
        outPixels = m_Pipeline.GetOutputPixels(
            outW, outH, std::max(1, maxDimension));
    }
    return Stack::PixelBuffer::HasCompletePixelBuffer(
        outPixels.size(), outW, outH, 4);
}

bool EditorModule::BuildSingleOutputTimelineFrameRaster(
    int timelineFrame,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH) {
    outW = 0;
    outH = 0;
    outPixels.clear();

    if (!m_Project->graph.IsOutputConnected()) {
        return false;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshotForTimelineFrame(timelineFrame);
    std::string availabilityError;
    if (!Stack::Project::IsRawLayerOutputAvailable(snapshot, availabilityError)) {
        m_RawLayerStatus = availabilityError;
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
        if (IsRawWorkspaceProjectActive() &&
            m_RawWorkspacePresentationTexture.texture != 0) {
            outPixels = m_Pipeline.GetExternalTexturePixels(
                m_RawWorkspacePresentationTexture.texture,
                m_RawWorkspacePresentationTexture.width,
                m_RawWorkspacePresentationTexture.height,
                outW,
                outH,
                0);
        } else {
            outPixels = m_Pipeline.GetOutputPixels(outW, outH);
        }
        if (Stack::PixelBuffer::HasCompletePixelBuffer(
                outPixels.size(), outW, outH, 4)) {
            return true;
        }
        outPixels.clear();
        outW = 0;
        outH = 0;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;

    if (TryResolveReferenceSourcePixelsForOutput(snapshot.outputNodeId, sourcePixels, sourceW, sourceH, sourceCh)) {
        // Use reference canvas.
    } else if (const EditorNodeGraph::Node* activeImage = m_Project->graph.FindNode(m_Project->graph.GetActiveImageNodeId())) {
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
        for (const EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
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
    CancelRawViewportCalibration();
    const bool wasAlreadyDirty = m_RenderDirty;
    m_RenderDirty = true;
    // A render-invalidating graph change is also a project-document edit.
    // Route it through the same revision authority used by RAW controls so
    // autosave and Ctrl+S cannot mistake a changed graph for a clean no-op.
    // Presentation-only invalidation must use MarkRenderRefreshDirty().
    MarkDirty();
    ++m_RenderRevision;
    if (!wasAlreadyDirty) {
        m_LastRenderDirtyTime = ImGui::GetTime();
    }
    if (touchedNodeId > 0) {
        const std::vector<int> downstreamNodeIds = m_Project->graph.GetDownstreamRenderNodeIds(touchedNodeId);
        const std::vector<int> downstreamOutputNodeIds = m_Project->graph.GetDownstreamOutputNodeIds(touchedNodeId);
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
        for (const EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
            if (m_Project->graph.IsRenderChainNode(node)) {
                ++m_GraphPerformanceStats.lastDirtyNodeCount;
            }
        }
        m_GraphPerformanceStats.lastDirtyOutputCount =
            static_cast<int>(m_Project->graph.GetConnectedOutputNodeIds().size());
        MarkAllRenderNodesDirty();
        MarkCompositeOutputsDirty(m_Project->graph.GetConnectedOutputNodeIds());
        m_PreviewDisplayedRevisions.clear();
        m_PreviewRequestedGenerations.clear();
        m_PreviewCompletedGenerations.clear();
        m_ScopeDisplayedRevisions.clear();
    }
}

void EditorModule::MarkRenderRefreshDirty() {
    CancelRawViewportCalibration();
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
    if (m_RawWorkspaceLabUi.zoneAreas.sourceKey==sourceKey)
        Stack::Editor::RawLabInternal::RememberRawLabAreaGuide(m_RawWorkspaceLabUi.zoneAreas,readback);

    RawWorkspaceGraphScopeCacheEntry* cache = nullptr;
    switch (readback.stage) {
        case RawDevelopmentGraphScopeStage::LocalRangeInput:
            cache = &m_RawWorkspaceLocalRangeInputGraphScopeCache;
            break;
        case RawDevelopmentGraphScopeStage::FinishToneInput:
            cache = &m_RawWorkspaceFinishToneInputGraphScopeCache;
            break;
        case RawDevelopmentGraphScopeStage::ColorWarpInput:
            cache = &m_RawWorkspaceColorWarpInputGraphScopeCache;
            break;
        case RawDevelopmentGraphScopeStage::None:
        default:
            return;
    }

    auto areaStats = readback.zoneAreas;
    if (cache->sourceKey == sourceKey && cache->inputFingerprint == inputFingerprint) {
        for (auto& stats : areaStats) {
            if (stats.fullResolution) continue;
            const auto prior = std::find_if(cache->readback.zoneAreas.begin(), cache->readback.zoneAreas.end(),
                [&](const auto& old) { return old.areaId == stats.areaId &&
                    old.maskFingerprint == stats.maskFingerprint && old.fullResolution; });
            if (prior != cache->readback.zoneAreas.end()) stats = *prior;
        }
    }
    cache->sourceKey = sourceKey;
    cache->inputFingerprint = inputFingerprint;
    cache->readback = readback;
    cache->readback.zoneAreas = std::move(areaStats);
}

bool EditorModule::RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool() {
    using Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint;
    using Stack::Renderer::RawDevelopmentCache::Stage;

    const RawWorkspaceGraphScopeCacheEntry* cache = nullptr;
    RawDevelopmentGraphScopeStage expectedReadbackStage =
        RawDevelopmentGraphScopeStage::None;
    Stage fingerprintStage = Stage::RawPlacement;
    switch (m_RawWorkspaceLabUi.activeTool) {
        case RawLabTool::Zones:
            cache = &m_RawWorkspaceLocalRangeInputGraphScopeCache;
            expectedReadbackStage =
                RawDevelopmentGraphScopeStage::LocalRangeInput;
            fingerprintStage = Stage::RawPlacement;
            break;
        case RawLabTool::Tone:
            cache = &m_RawWorkspaceFinishToneInputGraphScopeCache;
            expectedReadbackStage =
                RawDevelopmentGraphScopeStage::FinishToneInput;
            fingerprintStage = Stage::PostLocalRange;
            break;
        case RawLabTool::Color:
            cache = &m_RawWorkspaceColorWarpInputGraphScopeCache;
            expectedReadbackStage =
                RawDevelopmentGraphScopeStage::ColorWarpInput;
            fingerprintStage = Stage::PostFinishTone;
            break;
        default:
            m_RawWorkspaceGraphScopeReadback = {};
            return false;
    }

    const std::string activePreviewIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    if (!(IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) ||
        activePreviewIdentity.empty() ||
        m_ViewportOutputRawWorkspaceSourceKey != activePreviewIdentity) {
        m_RawWorkspaceGraphScopeReadback = {};
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe mergedSourceRecipe;
    const Stack::RawRecipe::RawDevelopmentRecipe* activeRecipe =
        &m_Project->rawRecipe;
    if (IsMultiFrameRawProjectActive()) {
        if (!ReadMergedRenderingRecipe(*m_Project->snapshot,activePreviewIdentity,
                GetActiveRawWorkspacePreviewSourceHash(),mergedSourceRecipe)) {
            m_RawWorkspaceGraphScopeReadback = {};
            return false;
        }
        activeRecipe = &mergedSourceRecipe;
    } else if (m_Project->rawInteractionDraft.active &&
               m_Project->rawInteractionDraft.sourceKey ==
                   m_Project->rawSourceKey) {
        activeRecipe = &m_Project->rawInteractionDraft.recipe;
    }
    const int scopeSamplingDimension =
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Color
        ? std::clamp(
            m_RawWorkspaceLabUi.colorWarpDiagnosticTargetLongEdge,
            kRawWorkspaceGraphScopeMaxDimension,
            2048)
        : kRawWorkspaceGraphScopeMaxDimension;
    const auto* scopeOperation = SelectedRawOperation();
    const auto* scopeLayer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    const std::size_t expectedFingerprint = scopeOperation && scopeLayer
        ? RawLayerScopeFingerprint(*activeRecipe, scopeLayer->id + "/" + scopeOperation->instanceUuid, m_Project->rawLayers.ProcessingRevision(),
            expectedReadbackStage, scopeSamplingDimension)
        : BuildStageFingerprint(
        *activeRecipe,
        // A scope's cache identity follows its own fixed sampling dimension,
        // never the adaptively changing presentation resolution.
        scopeSamplingDimension,
        fingerprintStage);
    if (cache->sourceKey != activePreviewIdentity ||
        cache->inputFingerprint != expectedFingerprint ||
        !cache->readback.valid ||
        cache->readback.stage != expectedReadbackStage) {
        // During exposure scrubbing, keep the latest completed distribution
        // visible while requesting the next one. Accept only an exposure-only
        // difference from this same source and upstream processing recipe.
        // This is measured renderer feedback, never a shifted approximation.
        if (!scopeOperation && m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
            !m_RawWorkspaceLabUi.zonesTargetedView &&
            cache->sourceKey == activePreviewIdentity && cache->readback.valid &&
            cache->readback.stage == expectedReadbackStage) {
            auto measuredRecipe = *activeRecipe;
            measuredRecipe.preToneExposureEv = cache->readback.inputExposureEv;
            if (cache->inputFingerprint == BuildStageFingerprint(
                    measuredRecipe, scopeSamplingDimension, fingerprintStage)) {
                m_RawWorkspaceGraphScopeReadback = cache->readback;
                return false;
            }
        }
        m_RawWorkspaceGraphScopeReadback = {};
        return false;
    }

    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones && m_RawWorkspaceLabUi.zonesTargetedView) {
        const auto measuredRecipe = scopeOperation && scopeLayer
            ? Stack::Project::ReadRawLayerOperation(*scopeLayer,scopeOperation->instanceUuid) : *activeRecipe;
        for (const auto& area : measuredRecipe.localRange.areas) {
            const auto stats = std::find_if(cache->readback.zoneAreas.begin(), cache->readback.zoneAreas.end(),
                [&](const auto& value) { return value.areaId == area.id && value.maskFingerprint == Stack::RawRecipe::ZoneAreaMaskFingerprint(area); });
            if (stats == cache->readback.zoneAreas.end() || !stats->fullResolution) {
                m_RawWorkspaceGraphScopeReadback = cache->readback;
                return false;
            }
        }
    }
    m_RawWorkspaceGraphScopeReadback = cache->readback;
    return true;
}

void EditorModule::ClearRawWorkspaceGraphScopeReadbackCaches() {
    m_RawWorkspaceLocalRangeInputGraphScopeCache = {};
    m_RawWorkspaceFinishToneInputGraphScopeCache = {};
    m_RawWorkspaceColorWarpInputGraphScopeCache = {};
    m_RawWorkspaceGraphScopeReadback = {};
    m_RawWorkspaceLabUi.colorWarpCloudInputFingerprint = 0;
    m_RawWorkspaceLabUi.colorWarpCloud.clear();
}

void EditorModule::ClearViewportOutputTiles() {
    QueueViewportOutputTileSetRelease(m_ViewportOutputTiles);
    ClearRawWorkspacePresentationTexture();
    ClearRawWorkspaceLocalRangeOverlayState();
    m_RawWorkspacePreviewOutputKind = RawWorkspacePreviewOutputKind::None;
    m_ViewportOutputRawWorkspaceSourceKey.clear();
    m_ViewportOutputPreviewMaxDimension = 0;
    m_ViewportOutputExpectedNativeWidth = 0;
    m_ViewportOutputExpectedNativeHeight = 0;
    m_ViewportOutputNativeExtentVerified = false;
    m_ViewportOutputRenderGeneration = 0;
}

void EditorModule::RefreshPendingMultiFrameProjectCover() {
    if (!m_MultiFrameProjectCoverRefreshPending ||
        m_ProjectCoverEncodeInFlight ||
        !IsRawWorkspaceProjectActive() ||
        !m_Project->snapshot ||
        m_RawWorkspacePresentationTexture.texture == 0 ||
        m_RawWorkspacePresentationTexture.width <= 0 ||
        m_RawWorkspacePresentationTexture.height <= 0) {
        return;
    }
    if (m_Project->rawInteractionDraft.active ||
        m_RawWorkspaceLabUi.previewPanning ||
        m_RawWorkspaceLabUi.colorWarpInteractionActive ||
        (ImGui::GetCurrentContext() && ImGui::IsAnyItemActive())) {
        // The cover is rebuildable cache data. Never make its synchronous,
        // reduced readback compete with a RAW control or viewport gesture.
        return;
    }

    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels =
        m_Pipeline.GetExternalTexturePixels(
            m_RawWorkspacePresentationTexture.texture,
            m_RawWorkspacePresentationTexture.width,
            m_RawWorkspacePresentationTexture.height,
            width,
            height,
            320);
    if (pixels.empty() || width <= 0 || height <= 0) return;

    // Thumbnail encoding is rebuildable presentation work.  It must not run
    // on the UI thread, create a document revision, or force an immediate
    // project save after every settled RAW render.
    const std::string projectId = m_Project->documentId;
    const std::uint64_t editorRevision = m_Project->editRevision;
    m_ProjectCoverEncodeInFlight = true;
    m_MultiFrameProjectCoverRefreshPending = false;
    const bool submitted = ProjectTasks().Submit("Updating preview", [
        this,
        projectId,
        editorRevision,
        pixels = std::move(pixels),
        width,
        height
    ]() mutable {
        std::vector<unsigned char> encoded =
            Stack::PngEncoding::EncodeInterleaved(
                pixels, width, height, 4);
        ProjectTasks().PostToMain([
            this,
            projectId,
            editorRevision,
            encoded = std::move(encoded)
        ]() mutable {
            m_ProjectCoverEncodeInFlight = false;
            if (encoded.empty() ||
                projectId != m_Project->documentId ||
                editorRevision != m_Project->editRevision ||
                !m_Project->snapshot) {
                return;
            }
            auto snapshot =
                std::make_shared<Stack::Project::RawProjectSnapshot>(
                    *m_Project->snapshot);
            snapshot->coverThumbnailBytes = std::move(encoded);
            m_Project->snapshot = std::move(snapshot);
            if(IsBracketingActive()) {
                Stack::Editor::UpdateBracketingGalleryProject(m_RawWorkspace,*m_Project->snapshot,GetCurrentProjectFileName());
                InvalidateRawWorkspaceGalleryPresentation();
            }
        });
    });
    if (!submitted) {
        m_ProjectCoverEncodeInFlight = false;
        m_MultiFrameProjectCoverRefreshPending = true;
    }
}

void EditorModule::QueueViewportOutputTextureRelease(EditorRenderWorker::SharedTextureResult& texture) {
    if (!texture.readyFence && texture.texture == 0) {
        texture.Reset();
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
        (void)texture.ReleaseTextureName();
        return;
    }

    try {
        m_DeferredViewportOutputTextureReleases.push_back(
            std::move(texture));
    } catch (const std::bad_alloc&) {
        texture.Reset();
    } catch (const std::length_error&) {
        texture.Reset();
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
    ClearRawViewportTransition();
    m_RawViewportLastPresentation = {};
    m_RawViewportPresentationTime = -1.0;
    m_RawViewportCadenceMs = 0;
    m_RawViewportFeedbackFixedMs = 0;
    m_RawViewportFeedbackEdge = 0;
    QueueViewportOutputTextureRelease(m_RawViewportOverviewTexture);
    QueueViewportOutputTextureRelease(m_RawViewportDetailTexture);
    m_RawViewportDetailRegion = {};
    m_RawViewportDetailContent = 0;
    m_RawViewportPresentedRegion = {};
    m_RawViewportPresentationContent = m_RawViewportOverviewContent = m_RawViewportOverviewFailedContent = 0;
    QueueViewportOutputTextureRelease(m_RawWorkspacePresentationTexture);
}

bool EditorModule::AdoptRawWorkspacePresentationTexture(
    EditorRenderWorker::SharedTextureResult& texture, const Raw::ViewportRegion& region) {
    if (!IsViewportTextureSafeForDrawing(texture.texture) ||
        texture.width <= 0 ||
        texture.height <= 0) {
        QueueViewportOutputTextureRelease(texture);
        return false;
    }
    if (!texture.EnsureLease(Raw::RawGpuImageFamily::Presentation)) {
        QueueViewportOutputTextureRelease(texture);
        return false;
    }

    const auto& previous = m_RawWorkspacePresentationTexture.texture
        ? m_RawWorkspacePresentationTexture : m_RawViewportPreviousTexture;
    if (region.Valid() && !m_RawViewportPresentedRegion.Valid() && previous.texture) {
        const double scale = std::min(1.0,512.0 / std::max(previous.width,previous.height));
        EditorRenderWorker::SharedTextureResult overview;
        overview.width = std::max(1,int(std::lround(previous.width*scale)));
        overview.height = std::max(1,int(std::lround(previous.height*scale)));
        overview.texture = Stack::Renderer::CopyViewportTexture(previous.texture,previous.width,previous.height,overview.width,overview.height);
        if (overview.texture && overview.EnsureLease()) {
            QueueViewportOutputTextureRelease(m_RawViewportOverviewTexture);
            m_RawViewportOverviewTexture = std::move(overview);
            m_RawViewportOverviewContent = m_RawViewportPresentationContent;
        }
    }
    QueueViewportOutputTextureRelease(m_RawWorkspacePresentationTexture);
    m_RawWorkspacePresentationTexture = std::move(texture);
    texture = {};
    return true;
}

void EditorModule::ClearRawWorkspaceLocalRangeOverlayState() {
    if (m_RawWorkspaceLocalRangeOverlayTextureLease.texture != 0) {
        QueueViewportOutputTextureRelease(
            m_RawWorkspaceLocalRangeOverlayTextureLease);
    } else if (m_RawWorkspaceLocalRangeOverlayTexture != 0) {
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
    const std::string activePreviewIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    if (result.rawWorkspace.sourceKey.empty() ||
        result.rawWorkspace.sourceKey != activePreviewIdentity ||
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
    m_RawWorkspaceLocalRangeOverlayTextureLease =
        std::move(acceptedTexture);
    m_RawWorkspaceLocalRangeOverlayTexture =
        m_RawWorkspaceLocalRangeOverlayTextureLease.texture;
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
        if (aliasesFontAtlas) {
            (void)texture.ReleaseTextureName();
        } else {
            texture.Reset();
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
    snapshot.telemetry.targetFps = GetRawViewportTargetFps();
    snapshot.telemetry.timingHistoryEpoch=m_RawViewportTimingHistory.Epoch();
    snapshot.telemetry.gestureId = m_RawViewportGestureId;
    snapshot.rawWorkspace.editStage = m_RawViewportEditStage;
    snapshot.rawWorkspace.viewport = m_RawViewportRequest;

    snapshot.telemetry.snapshotReadyAt =
        std::chrono::steady_clock::now();
    snapshot.generation = generation;
    snapshot.lastAcceptedGeneration = m_LastCompletedRenderGeneration;
    const bool rawStage = UsesRawWorkspaceStageRender();
    // A RAW viewport region is in RAW output coordinates. Downstream graph
    // nodes can change those coordinates; display their complete output and
    // zoom it locally, while retaining the request generation for adoption.
    if (!rawStage) snapshot.rawWorkspace.viewport.visible = {};
    const int rawStageOutput = rawStage ? ResolveRawWorkspaceStageOutputNodeId() : 0;
    snapshot.graph = rawStage
        ? (rawStageOutput > 0 ? BuildGraphSnapshotForTimelineFrame(m_TimelineUi.currentFrame, rawStageOutput) : RenderGraphSnapshot {})
        : BuildGraphSnapshot();
    // Layer masks use the developed Background canvas. A RAW-only region
    // preview would change generator coordinates and omit sampled pixels.
    if (snapshot.graph.rawLayerBackgroundNodeId > 0) snapshot.rawWorkspace.viewport.visible = {};
    if (IsEditingRawLayerMaskGraph() && !m_RawWorkspaceExportRenderRequested) {
        const auto& workspace = *m_RawLayerMaskWorkspace;
        if (workspace.previewOutputNodeId == 0) {
            std::string error;
            if (!Stack::Project::ConfigureRawLayerThumbnail(snapshot.graph, workspace.layerId, {}, error)) {
                snapshot.graph.outputNodeId = 0;
                m_RawLayerStatus = error;
            }
        } else {
            const auto owner = snapshot.graph.rawLayerMaskNodeIds.find(workspace.layerId);
            const auto* requested = workspace.graph.FindNode(workspace.previewOutputNodeId);
            if (owner == snapshot.graph.rawLayerMaskNodeIds.end() || !requested || !owner->second.count(requested->id)) {
                snapshot.graph.outputNodeId = 0;
                m_RawLayerStatus = "The requested output was deleted.";
            } else {
                if (!Stack::Project::ConfigureRawLayerOutputPreview(snapshot.graph,owner->second.at(requested->id),"imageOut",m_RawLayerStatus))
                    snapshot.graph.outputNodeId = 0;
            }
        }
    }
    if(m_Bracketing&&m_Bracketing->HasInteractivePreview()&&
        !m_RawWorkspaceExportRenderRequested&&IsBracketingActive()) {
        for(auto& node:snapshot.graph.nodes)if(node.rawProjectSourceSet.sourceSetId==m_Bracketing->setId) {
            node.rawDevelopment.embeddedRawData=m_Bracketing->interactiveRaw;
            node.rawDevelopment.recipe.source.fingerprint=std::to_string(m_Bracketing->interactiveRaw->contentIdentityHash);
            node.rawProjectSourceSet.contentHash=m_Bracketing->interactiveRaw->contentIdentityHash;
        }
    }
    if (IsRawWorkspaceProjectActive()) {
        snapshot.telemetry.interactionActive =
            IsRawWorkspaceUiInteractionActive();
        const std::string activePreviewIdentity =
            GetActiveRawWorkspacePreviewIdentity();
        snapshot.rawWorkspace.sourceKey = activePreviewIdentity;
        if (IsMultiFrameRawProjectActive()) {
            snapshot.rawWorkspace.sourceHash =
                GetActiveRawWorkspacePreviewSourceHash();
            snapshot.rawWorkspace.hasRecipe = ReadMergedRenderingRecipe(*m_Project->snapshot,
                activePreviewIdentity,snapshot.rawWorkspace.sourceHash,snapshot.rawWorkspace.recipe);
        } else {
            const Stack::RawWorkspace::SourceRecord* activeRawSource =
                FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
            snapshot.rawWorkspace.sourceHash =
                activeRawSource == nullptr
                    ? 0
                    : BuildRawWorkspaceAutoBaseSourceHash(*activeRawSource);
            snapshot.rawWorkspace.hasRecipe = !activePreviewIdentity.empty();
            snapshot.rawWorkspace.recipe = m_Project->rawRecipe;
            if (m_Project->rawInteractionDraft.active &&
                m_Project->rawInteractionDraft.sourceKey ==
                    m_Project->rawSourceKey) {
                snapshot.rawWorkspace.recipe =
                    m_Project->rawInteractionDraft.recipe;
            }
        }
        if (m_Project->rawMode ==
            Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
            snapshot.rawWorkspace.managedRawDecodeNodeId =
                m_Project->managedRaw.rawDecodeNodeId;
            snapshot.rawWorkspace.managedToneCurveNodeId =
                m_Project->managedRaw.toneCurveNodeId;
            snapshot.rawWorkspace.managedViewTransformNodeId =
                m_Project->managedRaw.viewTransformNodeId;
        }
        if (snapshot.rawWorkspace.hasRecipe) {
            if (snapshot.graph.rawLayerBackgroundNodeId > 0) {
                int changingNode = 0;
                const auto* operation = SelectedRawOperation();
                const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
                if (operation && layer) {
                    const auto ids = snapshot.graph.rawLayerMaskNodeIds.find(layer->id);
                    if (ids != snapshot.graph.rawLayerMaskNodeIds.end()) {
                        const auto id = ids->second.find(operation->id);
                        if (id != ids->second.end()) changingNode = id->second;
                    }
                }
                m_RawViewportGraphWorkloadKeys = Stack::Renderer::BuildRawGraphViewportWorkloadKeys(
                    snapshot.rawWorkspace.recipe,snapshot.graph,changingNode);
                snapshot.rawWorkspace.graphWorkloadKeys = m_RawViewportGraphWorkloadKeys;
            } else m_RawViewportGraphWorkloadKeys = {};
            UpdateRawViewportEditTiming(snapshot.rawWorkspace.recipe);
            snapshot.rawWorkspace.editStage = m_RawViewportEditStage;
            ApplyRawWorkspaceDenoiseViewOverride(
                snapshot.rawWorkspace.recipe);
            ApplyRawWorkspaceColorWarpViewOverride(
                snapshot.rawWorkspace.recipe);
            Stack::EditorRendering::ApplyRawRecipeOverlayToRenderGraph(
                snapshot.graph,
                snapshot.rawWorkspace.recipe,
                snapshot.rawWorkspace.managedRawDecodeNodeId,
                snapshot.rawWorkspace.managedToneCurveNodeId,
                snapshot.rawWorkspace.managedViewTransformNodeId);
        }
        snapshot.rawWorkspace.globalExposureInteractionActive =
            snapshot.telemetry.interactionActive &&
            m_RawWorkspaceLabUi.globalExposureInteractionActive &&
            snapshot.rawWorkspace.hasRecipe &&
            !Stack::RawRecipe::SanitizeRgbDenoiseRecipe(
                 snapshot.rawWorkspace.recipe.rgbDenoise).enabled;
        snapshot.rawWorkspace.preferredCacheInputStage =
            ResolvePreferredRawCacheInputStage(
                m_RawWorkspaceLabUi.activeTool);
        if (snapshot.graph.rawLayerBackgroundNodeId > 0 && SelectedRawOperation())
            snapshot.rawWorkspace.preferredCacheInputStage = Stack::Renderer::RawDevelopmentCache::Stage::RawPlacement;
        snapshot.rawWorkspace.graphScopeMaxDimension =
            m_RawWorkspaceLabUi.activeTool == RawLabTool::Color
            ? std::clamp(
                m_RawWorkspaceLabUi.colorWarpDiagnosticTargetLongEdge,
                kRawWorkspaceGraphScopeMaxDimension,
                2048)
            : kRawWorkspaceGraphScopeMaxDimension;
        if (m_RawWorkspaceLabUi.lowerShelfOpen) {
            snapshot.rawWorkspace.gradingScopeSource =
                m_RawWorkspaceLabUi.gradingScopesShowInput
                    ? RawDevelopmentGradingScopeSource::NeutralScene
                    : RawDevelopmentGradingScopeSource::DisplayCandidate;
        }
        const bool activeGraphScopeAlreadyCached =
            RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool();
        if (!activeGraphScopeAlreadyCached &&
            m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones) {
            snapshot.rawWorkspace.graphScopeStage =
                RawDevelopmentGraphScopeStage::LocalRangeInput;
        } else if (!activeGraphScopeAlreadyCached &&
                   m_RawWorkspaceLabUi.activeTool == RawLabTool::Tone) {
            snapshot.rawWorkspace.graphScopeStage =
                RawDevelopmentGraphScopeStage::FinishToneInput;
        } else if (!activeGraphScopeAlreadyCached &&
                   m_RawWorkspaceLabUi.activeTool == RawLabTool::Color) {
            snapshot.rawWorkspace.graphScopeStage =
                RawDevelopmentGraphScopeStage::ColorWarpInput;
        }
        Stack::PreciseIntegration::IntegrationState& preciseState =
            m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
        if ((m_RenderWorkerAvailable || m_RawRenderClientId != 0) &&
            preciseState.active &&
            preciseState.mode == Stack::PreciseIntegration::ProductMode::Precise &&
            Stack::PreciseIntegration::IsRunning(preciseState.state) &&
            preciseState.identity.sourceKey == activePreviewIdentity &&
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
            activePreviewIdentity,
            snapshot.rawWorkspace.sourceHash);
        if (Stack::EditorModuleTypes::RawStartingPointCandidateRenderQueueMatchesSource(
                m_RawWorkspaceStartPointCandidateRenderQueue,
                activePreviewIdentity,
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
            m_RawWorkspaceLocalRangeTargetSourceKey == activePreviewIdentity) {
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
    snapshot.outputConnected = (rawStage ? rawStageOutput > 0 : m_Project->graph.IsOutputConnected()) &&
        (IsRawWorkspaceProjectActive() ||
         GetViewportMode() == ViewportMode::SingleOutputPreview);
    if (snapshot.graph.rawLayerBackgroundNodeId > 0) {
        std::string availabilityError;
        if (!Stack::Project::IsRawLayerOutputAvailable(snapshot.graph, availabilityError)) {
            snapshot.outputConnected = false;
            m_RawLayerStatus = std::move(availabilityError);
            ClearRawWorkspaceGraphScopeReadbackCaches();
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        }
    }
    const EditorNodeGraph::Node* activeComplexNode =
        m_ActiveComplexNodeId > 0 ? m_Project->graph.FindNode(m_ActiveComplexNodeId) : nullptr;
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
    int fullFrameWidth = 0;
    int fullFrameHeight = 0;
    if (IsRawWorkspaceProjectActive() || rawDevelopComplexPreview) {
        ResolveRawFullFrameDimensions(
            m_Project->singleRawSource.get(),
            snapshot.graph,
            fullFrameWidth,
            fullFrameHeight);
        if ((fullFrameWidth <= 0 || fullFrameHeight <= 0) &&
            IsRawWorkspaceProjectActive()) {
            const Stack::RawWorkspace::SourceRecord* source =
                FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
            if (source != nullptr && !source->absolutePath.empty()) {
                Raw::RawMetadata metadata;
                if (Raw::RawLoader::LoadMetadata(
                        source->absolutePath.string(), metadata)) {
                    Raw::RawImageData metadataSource;
                    metadataSource.metadata = std::move(metadata);
                    ResolveRawFullFrameDimensions(
                        &metadataSource,
                        snapshot.graph,
                        fullFrameWidth,
                        fullFrameHeight);
                }
            }
        }
    }
    if (IsRawWorkspaceProjectActive()) {
        RefreshRawWorkspaceGpuMemoryBudget();
    }
    if (m_RawWorkspaceRootTabActive && IsRawWorkspaceProjectActive()) ResolveRawViewportInteractiveEdge();
    Stack::EditorRendering::RawRenderPlanInput planInput;
    if (snapshot.graph.rawLayerBackgroundNodeId > 0 ||
        std::any_of(snapshot.graph.nodes.begin(),snapshot.graph.nodes.end(),[](const auto& node) {
            return node.kind == RenderGraphNodeKind::RawDevelopment;
        })) planInput.surfaceBytesPerPixel = 16;
    planInput.exportRequested = m_RawWorkspaceExportRenderRequested;
    planInput.targetAuxiliary = rawWorkspaceTargetAuxiliary;
    planInput.explicitInspectionRequested =
        m_RawWorkspaceExplicitFullQualityRenderRequested;
    // Visible native detail has priority over optional scopes after release.
    planInput.fullResolutionPreviewRequested =
        m_RawWorkspaceFullResolutionPreviewRequested;
    const bool denoiseDiagnosticRequested =
        snapshot.rawWorkspace.hasRecipe &&
        snapshot.rawWorkspace.recipe.rgbDenoise.diagnosticMode !=
            Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
    planInput.fullResolutionDiagnosticRequested =
        denoiseDiagnosticRequested &&
        m_RawWorkspaceLabUi.denoiseMap.fullResolutionDiagnostics;
    planInput.proxyResolutionDiagnosticRequested =
        denoiseDiagnosticRequested &&
        !m_RawWorkspaceLabUi.denoiseMap.fullResolutionDiagnostics;
    planInput.analysisRequested = m_RawWorkspaceAnalysisRequested;
    if (m_RawViewportRequest.visible.Partial() &&
        snapshot.rawWorkspace.recipe.rgbDenoise.method == Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1)
        planInput.analysisTargetEdge = std::max(512,snapshot.rawWorkspace.graphScopeMaxDimension);

    planInput.complexNodePreview = rawDevelopComplexPreview;
    planInput.interactiveTargetEdge = IsRawWorkspaceProjectActive()
        ? m_RawWorkspaceInteractivePreviewMaxDimension
        : ((rawWorkspaceFastPreview || rawWorkspaceTargetAuxiliary)
            ? m_RawWorkspaceInteractivePreviewMaxDimension
            : 0);
    planInput.complexNodeTargetEdge =
        kRawDevelopComplexPreviewMaxDimension;
    planInput.fullFrameWidth = fullFrameWidth;
    planInput.fullFrameHeight = fullFrameHeight;
    planInput.maximumTextureSize = m_RawWorkspaceGlMaxTextureSize;
    planInput.workingBudgetBytes =
        m_RawWorkspaceVramWorkingBudgetBytes;
    planInput.forceMinimumMemoryTiling =
        m_RawWorkspaceMinimumMemoryTiling;
    if(m_Bracketing&&m_Bracketing->HasInteractivePreview()&&!m_RawWorkspaceExportRenderRequested) {
        planInput.analysisRequested=false;
        planInput.fullResolutionPreviewRequested=false;
        planInput.explicitInspectionRequested=false;
        planInput.fullResolutionDiagnosticRequested=false;
    }
    const auto viewportInput=BuildRawViewportDecisionInput();
    if (viewportInput.regionalAllowed && viewportInput.visible.Partial()) {
        planInput.visibleAreaFraction=double(viewportInput.visible.width)*viewportInput.visible.height /
            (double(viewportInput.visible.fullWidth)*viewportInput.visible.fullHeight);
        planInput.fullInputSurfaces=std::min(7,static_cast<int>(viewportInput.regionalFirst)+1);
    }
    const Stack::EditorRendering::RawRenderPlan renderPlan =
        Stack::EditorRendering::BuildRawRenderPlan(planInput);
    snapshot.rawRenderPurpose = renderPlan.purpose;
    snapshot.rawWorkspace.analysisRequested =
        snapshot.rawRenderPurpose == RawRenderPurpose::AnalysisScopes;
    if (!snapshot.rawWorkspace.analysisRequested) {
        const bool liveExposure = snapshot.graph.rawLayerBackgroundNodeId <= 0 &&
            m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
            m_RawWorkspaceLabUi.globalExposureInteractionActive;
        const bool settledToolScope = snapshot.graph.rawLayerBackgroundNodeId > 0 &&
            !snapshot.telemetry.interactionActive &&
            m_RawWorkspaceLabUi.activeTool != RawLabTool::Color &&
            (snapshot.rawRenderPurpose == RawRenderPurpose::InteractivePresentation ||
             snapshot.rawRenderPurpose == RawRenderPurpose::ViewportRefinement);
        if (!liveExposure && !settledToolScope) {
            snapshot.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::None;
            snapshot.rawWorkspace.gradingScopeSource = RawDevelopmentGradingScopeSource::None;
        } else if (snapshot.rawWorkspace.graphScopeStage == RawDevelopmentGraphScopeStage::LocalRangeInput) {
            snapshot.rawWorkspace.graphScopeInputFingerprint =
                Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
                    snapshot.rawWorkspace.recipe, snapshot.rawWorkspace.graphScopeMaxDimension,
                    Stack::Renderer::RawDevelopmentCache::Stage::RawPlacement);
        }
    }
    if (renderPlan.fullFrameRequested) {
        snapshot.telemetry.fullFrameRefinementRequested = true;
        snapshot.telemetry.fullFrameRefinementBudgetAllowed =
            renderPlan.fullFrameDecision.allowed;
        snapshot.telemetry.fullFrameEstimatedWorkingSetBytes =
            renderPlan.fullFrameDecision.estimatedWorkingSetBytes;
        if (renderPlan.disableViewportTiling) {
            // This is deliberately a monolithic, authoritative raster. The
            // accepted texture is cached and remains the zoom source; generic
            // viewport tiling stays out of the RAW path until exact parity is
            // proven for every spatial stage.
            snapshot.viewportTiling.mode = ViewportTilingMode::Off;
        }
    }
    snapshot.previewMaxDimension = renderPlan.targetEdge;
    snapshot.telemetry.predictedMs=m_RawViewportDecision.predictedMs;
    snapshot.telemetry.timingWorkload=m_RawViewportAdaptiveWorkload;
    snapshot.telemetry.timingContext=Raw::ViewportFeedbackKey(m_RawViewportAdaptiveWorkload,m_RawViewportDecision.firstStage);
    const auto* localOperation = SelectedRawOperation();
    const bool areaScope = m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
        m_RawWorkspaceLabUi.zonesTargetedView && localOperation &&
        !Stack::RawRecipe::ReadGraphOperation(localOperation->rawOperation).localRange.areas.empty();
    if (areaScope && !snapshot.telemetry.interactionActive) {
        snapshot.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::LocalRangeInput;
        snapshot.rawWorkspace.graphScopeInputFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            snapshot.rawWorkspace.recipe, snapshot.rawWorkspace.graphScopeMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::RawPlacement);
        if (snapshot.rawRenderPurpose == RawRenderPurpose::AnalysisScopes) {
            snapshot.previewMaxDimension = 0;
            snapshot.rawWorkspace.viewport = {};
            snapshot.viewportTiling.mode = ViewportTilingMode::Off;
        }
    }

    if (!m_RawWorkspaceRootTabActive &&
        !m_RawWorkspaceExportRenderRequested &&
        !m_RawWorkspaceExplicitFullQualityRenderRequested) {
        // The Graph viewport renders the whole authored raster, including in
        // RAW projects. RAW's viewport crop and proxy governor belong to Raw.
        snapshot.rawRenderPurpose = RawRenderPurpose::InteractivePresentation;
        snapshot.previewMaxDimension = 0;
        snapshot.rawWorkspace.viewport = {};
        snapshot.rawWorkspace.analysisRequested = false;
        snapshot.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::None;
        snapshot.rawWorkspace.gradingScopeSource = RawDevelopmentGradingScopeSource::None;
        snapshot.telemetry.interactionActive = false;
    }
    snapshot.rawWorkspace.viewport.samplingScale = snapshot.previewMaxDimension > 0
        ? std::min(1.0, double(snapshot.previewMaxDimension) / std::max(1, std::max(m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight))) : 1.0;
    if (IsRawWorkspaceProjectActive()) {
        snapshot.rawWorkspace.fullFrameWidth = fullFrameWidth;
        snapshot.rawWorkspace.fullFrameHeight = fullFrameHeight;
        snapshot.rawWorkspace.gpuWorkingBudgetBytes =
            m_RawWorkspaceVramWorkingBudgetBytes;
        snapshot.rawWorkspace.gpuCacheBudgetBytes =
            renderPlan.cacheBudgetBytes;
        snapshot.rawWorkspace.minimumRawStageCacheBytes =
            renderPlan.minimumRawStageCacheBytes;
        if (snapshot.rawWorkspace.hasRecipe) {
            snapshot.rawWorkspace.recipeRevision =
                Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
                    snapshot.rawWorkspace.recipe,
                    0,
                    Stack::Renderer::RawDevelopmentCache::Stage::PostOutputCrop);
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
        !rawStage && snapshot.outputConnected &&
        m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
        m_ActiveComplexNodeId == m_AutoGainMaskPreviewNodeId &&
        m_AutoGainMaskPreviewNodeId > 0 &&
        m_Project->graph.FindNode(m_AutoGainMaskPreviewNodeId) &&
        m_Project->graph.FindNode(m_AutoGainMaskPreviewNodeId)->kind == EditorNodeGraph::NodeKind::RawDetailFusion;
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
        } else if (const EditorNodeGraph::Node* activeImage = m_Project->graph.FindNode(m_Project->graph.GetActiveImageNodeId())) {
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
            snapshot.sourcePixels = m_Pipeline.ShareSourcePixels(
                snapshot.width, snapshot.height, snapshot.channels);
        }
        if (snapshot.sourcePixels.empty() && (snapshot.width <= 0 || snapshot.height <= 0)) {
            for (const EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
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
            // Empty pixels request a canvas; the worker prepares GPU storage.
        }
    }
    if (snapshot.outputConnected) {
        snapshot.developCandidateRenders =
            BuildDevelopCandidateRenderRequests(snapshot.graph, snapshot.width, snapshot.height);
    }
    if (m_RawWorkspaceRootTabActive && snapshot.rawWorkspace.graphScopeStage != RawDevelopmentGraphScopeStage::None) {
        const auto* operation = SelectedRawOperation();
        const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
        const auto ids = layer ? snapshot.graph.rawLayerMaskNodeIds.find(layer->id) : snapshot.graph.rawLayerMaskNodeIds.end();
        if (operation && ids != snapshot.graph.rawLayerMaskNodeIds.end()) {
            const auto compiled = ids->second.find(operation->id);
            if (compiled != ids->second.end()) {
                const char* port = operation->rawOperation.kind == Stack::RawRecipe::GraphOperationKind::LocalEv &&
                    layer->graph.FindInputLink(operation->id, "referenceIn") ? "referenceIn" : "imageIn";
                const auto input = std::find_if(snapshot.graph.links.begin(), snapshot.graph.links.end(), [&](const auto& link) {
                    return link.toNodeId == compiled->second && link.toSocketId == port;
                });
                if (input != snapshot.graph.links.end()) {
                    snapshot.graph.rawLayerScopeNodeId = input->fromNodeId;
                    snapshot.graph.rawLayerScopeSocketId = input->fromSocketId;
                    snapshot.graph.rawLayerScopeOperationId = compiled->second;
                    snapshot.rawWorkspace.graphScopeInputFingerprint = RawLayerScopeFingerprint(snapshot.rawWorkspace.recipe,
                        layer->id + "/" + operation->instanceUuid, m_Project->rawLayers.ProcessingRevision(), snapshot.rawWorkspace.graphScopeStage,
                        snapshot.rawWorkspace.graphScopeMaxDimension);
                }
            }
        }
    }
    return snapshot;
}

void EditorModule::ClearRawRenderSession() {
    CancelRawViewportCalibration();
    m_RawViewportCalibration = {};
    m_RawViewportTimingBank.Clear();
    m_RawViewportTimingHistory.Reset();
    m_RawViewportEditTimingWindow.Reset();
    m_RawViewportTimingViewGeneration = 0;
    m_RawViewportHasPreviousRecipe = false;
    m_RawViewportGraphWorkloadKeys = {};
    m_RawViewportCachedStages = {};
    m_RawViewportNativeCachedStages = {};
    m_RawViewportMaintenanceGeneration = 0;
    m_RawViewportWarmupEdge = 0;
    m_RawViewportCachedEdge = m_RawViewportCachedRequestEdge = 0;
    m_RawViewportAdaptiveHistory.clear();
    m_RawViewportController.Reset();
    m_RawViewportTimingRepresentation.clear();
    m_RawViewportAdaptiveWorkload = 0;
    // A backend switch can occur after the next snapshot was built. Keep
    // viewport identity and presentation geometry owned by the preview; a
    // session reset must not make that in-flight snapshot obsolete.
    CancelRawWorkspaceCachePrewarm(true);
    if (m_RawRenderClientId != 0) {
        Stack::EditorRendering::RawRenderService::Get().ClearSession(
            m_RawRenderClientId);
    }
    m_RawRenderSessionSourceIdentity.clear();
    m_RawRenderSessionSourceHash = 0;
    m_RawRenderSessionGraphStructureRevision = 0;
    m_RawRenderSessionContractRevision = 0;
    m_RawRenderSessionFullFrameWidth = 0;
    m_RawRenderSessionFullFrameHeight = 0;
}

bool EditorModule::TrySubmitRawRenderCommand(
    std::uint64_t generation,
    std::vector<EditorRenderWorker::PreviewRequest>& previews) {
    // The recipe-only command reuses the worker's previous graph. A layer edit
    // changes that graph without changing the Background recipe or root links.
    // Submit the full immutable layer snapshot until the command contract also
    // carries the layer document revision.
    if (IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) return false;
    if (!UsesRawWorkspaceStageRender() ||
        !IsRawWorkspaceProjectActive() ||
        m_RawRenderClientId == 0) {
        return false;
    }
    const std::string sourceIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    const std::uint64_t sourceHash = IsMultiFrameRawProjectActive()
        ? GetActiveRawWorkspacePreviewSourceHash()
        : ([&]() {
              const Stack::RawWorkspace::SourceRecord* source =
                  FindRawWorkspaceSourceByKey(
                      m_Project->rawSourceKey);
              return source == nullptr
                  ? std::uint64_t { 0 }
                  : BuildRawWorkspaceAutoBaseSourceHash(*source);
          })();
    const std::uint64_t graphStructureRevision =
        m_Project->graph.GetStructureRevision();
    if (sourceIdentity.empty() ||
        m_RawRenderSessionSourceIdentity != sourceIdentity ||
        m_RawRenderSessionSourceHash != sourceHash ||
        m_RawRenderSessionGraphStructureRevision !=
            graphStructureRevision ||
        m_RawRenderSessionContractRevision !=
            kRawRenderProcessingContractRevision) {
        return false;
    }

    Stack::EditorRendering::RawRenderCommand command;
    command.generation = generation;
    command.lastAcceptedGeneration = m_LastCompletedRenderGeneration;
    command.cancellationToken = generation;
    command.telemetry.snapshotReadyAt =
        std::chrono::steady_clock::now();
    command.telemetry.interactionActive =
        IsRawWorkspaceUiInteractionActive();
    command.telemetry.targetFps = GetRawViewportTargetFps();
    command.telemetry.timingHistoryEpoch=m_RawViewportTimingHistory.Epoch();
    command.telemetry.gestureId = m_RawViewportGestureId;
    command.rawWorkspace.editStage = m_RawViewportEditStage;
    command.rawWorkspace.viewport = m_RawViewportRequest;

    command.rawWorkspace.sourceKey = sourceIdentity;
    command.rawWorkspace.sourceHash = sourceHash;
    if (IsMultiFrameRawProjectActive()) {
        RawWorkspaceEditContext context;
        command.rawWorkspace.hasRecipe =
            BeginRawWorkspaceEditContext(nullptr, context);
        if (command.rawWorkspace.hasRecipe) {
            command.rawWorkspace.recipe = std::move(context.recipe);
        }
    } else {
        command.rawWorkspace.hasRecipe = true;
        command.rawWorkspace.recipe =
            m_Project->rawInteractionDraft.active &&
                m_Project->rawInteractionDraft.sourceKey ==
                    m_Project->rawSourceKey
            ? m_Project->rawInteractionDraft.recipe
            : m_Project->rawRecipe;
    }
    if (!command.rawWorkspace.hasRecipe) {
        return false;
    }
    UpdateRawViewportEditTiming(command.rawWorkspace.recipe);
    command.rawWorkspace.editStage = m_RawViewportEditStage;
    ApplyRawWorkspaceDenoiseViewOverride(command.rawWorkspace.recipe);
    ApplyRawWorkspaceColorWarpViewOverride(command.rawWorkspace.recipe);
    if (m_Project->rawMode ==
        Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        command.rawWorkspace.managedRawDecodeNodeId =
            m_Project->managedRaw.rawDecodeNodeId;
        command.rawWorkspace.managedToneCurveNodeId =
            m_Project->managedRaw.toneCurveNodeId;
        command.rawWorkspace.managedViewTransformNodeId =
            m_Project->managedRaw.viewTransformNodeId;
    }
    command.rawWorkspace.globalExposureInteractionActive =
        command.telemetry.interactionActive &&
        m_RawWorkspaceLabUi.globalExposureInteractionActive &&
        !Stack::RawRecipe::SanitizeRgbDenoiseRecipe(
             command.rawWorkspace.recipe.rgbDenoise).enabled;
    command.rawWorkspace.preferredCacheInputStage =
        ResolvePreferredRawCacheInputStage(
            m_RawWorkspaceLabUi.activeTool);
    command.rawWorkspace.graphScopeMaxDimension =
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Color
        ? std::clamp(
            m_RawWorkspaceLabUi.colorWarpDiagnosticTargetLongEdge,
            kRawWorkspaceGraphScopeMaxDimension,
            2048)
        : kRawWorkspaceGraphScopeMaxDimension;

    if (m_RawWorkspaceLabUi.lowerShelfOpen) {
        command.rawWorkspace.gradingScopeSource =
            m_RawWorkspaceLabUi.gradingScopesShowInput
            ? RawDevelopmentGradingScopeSource::NeutralScene
            : RawDevelopmentGradingScopeSource::DisplayCandidate;
    }
    const bool activeGraphScopeAlreadyCached =
        RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool();
    if (!activeGraphScopeAlreadyCached &&
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones) {
        command.rawWorkspace.graphScopeStage =
            RawDevelopmentGraphScopeStage::LocalRangeInput;
    } else if (!activeGraphScopeAlreadyCached &&
               m_RawWorkspaceLabUi.activeTool == RawLabTool::Tone) {
        command.rawWorkspace.graphScopeStage =
            RawDevelopmentGraphScopeStage::FinishToneInput;
    } else if (!activeGraphScopeAlreadyCached &&
               m_RawWorkspaceLabUi.activeTool == RawLabTool::Color) {
        command.rawWorkspace.graphScopeStage =
            RawDevelopmentGraphScopeStage::ColorWarpInput;
    }

    Stack::PreciseIntegration::IntegrationState& preciseState =
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
    if (preciseState.active &&
        preciseState.mode ==
            Stack::PreciseIntegration::ProductMode::Precise &&
        Stack::PreciseIntegration::IsRunning(preciseState.state) &&
        preciseState.identity.sourceKey == sourceIdentity &&
        preciseState.identity.sourceHash == sourceHash) {
        Stack::PreciseIntegration::NativeSolveRequest request;
        request.identity = preciseState.identity;
        request.identity.generation = generation;
        request.inputRecipe = preciseState.originalRecipe;
        command.rawWorkspace.preciseSolveRequest = std::move(request);
        Stack::PreciseIntegration::MarkRunning(preciseState, generation);
    }
    Stack::EditorModuleTypes::
        ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
            m_RawWorkspaceStartPointCandidateRenderQueue,
            sourceIdentity,
            sourceHash);
    if (Stack::EditorModuleTypes::
            RawStartingPointCandidateRenderQueueMatchesSource(
                m_RawWorkspaceStartPointCandidateRenderQueue,
                sourceIdentity,
                sourceHash)) {
        command.rawWorkspace.startPointCandidateRenderRequests =
            m_RawWorkspaceStartPointCandidateRenderQueue.requests;
    }
    if (!m_RawWorkspaceLocalRangeOverlayMode.empty() &&
        m_RawWorkspaceLocalRangeOverlayMode != "none") {
        command.rawWorkspace.localRangeOverlayMode =
            m_RawWorkspaceLocalRangeOverlayMode;
    }
    if (m_RawWorkspaceLocalRangeTargetMode) {
        command.rawWorkspace.localRangeTargetPreview =
            m_RawWorkspaceLocalRangeTargetPreview;
    }
    if (m_RawWorkspaceLocalRangeTargetSamplePending &&
        m_RawWorkspaceLocalRangeTargetSourceKey == sourceIdentity) {
        command.rawWorkspace.localRangeTargetSampleRequested = true;
        command.rawWorkspace.localRangeTargetHoverSample =
            m_RawWorkspaceLocalRangeTargetHoverSample;
        command.rawWorkspace.localRangeTargetSampleU =
            std::clamp(m_RawWorkspaceLocalRangeTargetU, 0.0f, 1.0f);
        command.rawWorkspace.localRangeTargetSampleV =
            std::clamp(m_RawWorkspaceLocalRangeTargetV, 0.0f, 1.0f);
    }

    const bool targetHover =
        command.rawWorkspace.localRangeTargetSampleRequested &&
        command.rawWorkspace.localRangeTargetHoverSample;
    const bool targetAuxiliary =
        Stack::RawLocalRangeTargetInteraction::ShouldPreserveBasePresentation(
            targetHover,
            command.rawWorkspace.localRangeOverlayMode == "target-outline",
            command.rawWorkspace.localRangeTargetPreview.enabled,
            command.rawWorkspace.localRangeTargetPreview.interactionEditing);
    RefreshRawWorkspaceGpuMemoryBudget();
    if (m_RawWorkspaceRootTabActive && IsRawWorkspaceProjectActive()) ResolveRawViewportInteractiveEdge();
    Stack::EditorRendering::RawRenderPlanInput planInput;
    planInput.exportRequested = m_RawWorkspaceExportRenderRequested;
    planInput.targetAuxiliary = targetAuxiliary;
    planInput.explicitInspectionRequested =
        m_RawWorkspaceExplicitFullQualityRenderRequested;
    // Keep the command-service path in step with the regular snapshot path:
    // Visible refinement keeps the same priority in compact session commands.
    planInput.fullResolutionPreviewRequested =
        m_RawWorkspaceFullResolutionPreviewRequested;
    const bool denoiseDiagnosticRequested =
        command.rawWorkspace.recipe.rgbDenoise.diagnosticMode !=
            Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
    planInput.fullResolutionDiagnosticRequested =
        denoiseDiagnosticRequested &&
        m_RawWorkspaceLabUi.denoiseMap.fullResolutionDiagnostics;
    planInput.proxyResolutionDiagnosticRequested =
        denoiseDiagnosticRequested &&
        !m_RawWorkspaceLabUi.denoiseMap.fullResolutionDiagnostics;
    planInput.analysisRequested = m_RawWorkspaceAnalysisRequested;
    if (m_RawViewportRequest.visible.Partial() &&
        command.rawWorkspace.recipe.rgbDenoise.method == Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1)
        planInput.analysisTargetEdge = std::max(512,command.rawWorkspace.graphScopeMaxDimension);

    planInput.interactiveTargetEdge =
        m_RawWorkspaceInteractivePreviewMaxDimension;
    planInput.complexNodeTargetEdge =
        kRawDevelopComplexPreviewMaxDimension;
    planInput.fullFrameWidth = m_RawRenderSessionFullFrameWidth;
    planInput.fullFrameHeight = m_RawRenderSessionFullFrameHeight;
    planInput.maximumTextureSize = m_RawWorkspaceGlMaxTextureSize;
    planInput.workingBudgetBytes =
        m_RawWorkspaceVramWorkingBudgetBytes;
    planInput.forceMinimumMemoryTiling =
        m_RawWorkspaceMinimumMemoryTiling;
    const auto viewportInput=BuildRawViewportDecisionInput();
    if (viewportInput.regionalAllowed && viewportInput.visible.Partial()) {
        planInput.visibleAreaFraction=double(viewportInput.visible.width)*viewportInput.visible.height /
            (double(viewportInput.visible.fullWidth)*viewportInput.visible.fullHeight);
        planInput.fullInputSurfaces=std::min(7,static_cast<int>(viewportInput.regionalFirst)+1);
    }
    const Stack::EditorRendering::RawRenderPlan renderPlan =
        Stack::EditorRendering::BuildRawRenderPlan(planInput);
    command.purpose = renderPlan.purpose;
    command.priority = RawRenderPurposePriority(command.purpose);
    command.rawWorkspace.analysisRequested =
        command.purpose == RawRenderPurpose::AnalysisScopes;
    if (!command.rawWorkspace.analysisRequested) {
        const bool liveExposure = m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
            m_RawWorkspaceLabUi.globalExposureInteractionActive;
        if (!liveExposure) {
            command.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::None;
            command.rawWorkspace.gradingScopeSource = RawDevelopmentGradingScopeSource::None;
        }
    }
    command.targetEdge = renderPlan.targetEdge;
    command.telemetry.predictedMs=m_RawViewportDecision.predictedMs;
    command.telemetry.timingWorkload=m_RawViewportAdaptiveWorkload;
    command.telemetry.timingContext=Raw::ViewportFeedbackKey(m_RawViewportAdaptiveWorkload,m_RawViewportDecision.firstStage);
    const bool areaScope = m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
        m_RawWorkspaceLabUi.zonesTargetedView && !command.rawWorkspace.recipe.localRange.areas.empty();
    if (areaScope) {
        command.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::LocalRangeInput;
        command.rawWorkspace.graphScopeInputFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            command.rawWorkspace.recipe, command.rawWorkspace.graphScopeMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::RawPlacement);
        if (command.purpose == RawRenderPurpose::AnalysisScopes) {
            command.targetEdge = 0;
            command.rawWorkspace.viewport = {};
            command.viewportTiling.mode = ViewportTilingMode::Off;
        }
    }

    command.rawWorkspace.viewport.samplingScale = command.targetEdge > 0
        ? std::min(1.0, double(command.targetEdge) / std::max(1, std::max(m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight))) : 1.0;
    command.rawWorkspace.fullFrameWidth =
        m_RawRenderSessionFullFrameWidth;
    command.rawWorkspace.fullFrameHeight =
        m_RawRenderSessionFullFrameHeight;
    command.rawWorkspace.gpuWorkingBudgetBytes =
        m_RawWorkspaceVramWorkingBudgetBytes;
    command.rawWorkspace.gpuCacheBudgetBytes =
        renderPlan.cacheBudgetBytes;
    command.rawWorkspace.minimumRawStageCacheBytes =
        renderPlan.minimumRawStageCacheBytes;
    command.rawWorkspace.recipeRevision =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            command.rawWorkspace.recipe,
            0,
            Stack::Renderer::RawDevelopmentCache::Stage::PostOutputCrop);
    if (renderPlan.fullFrameRequested) {
        command.telemetry.fullFrameRefinementRequested = true;
        command.telemetry.fullFrameRefinementBudgetAllowed =
            renderPlan.fullFrameDecision.allowed;
        command.telemetry.fullFrameEstimatedWorkingSetBytes =
            renderPlan.fullFrameDecision.estimatedWorkingSetBytes;
    }
    if (m_Appearance) {
        command.viewportTiling =
            m_Appearance->GetViewportTilingSettings();
    }
    if (renderPlan.disableViewportTiling || (areaScope && command.purpose == RawRenderPurpose::AnalysisScopes)) {
        command.viewportTiling.mode = ViewportTilingMode::Off;
    }
    command.previews = previews;

    const RawRenderPurpose commandPurpose = command.purpose;
    const std::size_t commandRecipeRevision =
        command.rawWorkspace.recipeRevision;
    const bool submitted =
        Stack::EditorRendering::RawRenderService::Get().SubmitCommand(
        m_RawRenderClientId,
        std::move(command));
    Stack::EditorRenderScheduling::CommitRawSubmissionGeneration(
        submitted,
        RawRenderPurposeMayPublishPresentation(commandPurpose),
        generation,
        m_LatestRawPresentationGeneration,
        m_LatestRawAuxiliaryGeneration);
    if (submitted) {
        m_ActivityRawRenderIsProxy = renderPlan.targetEdge > 0;
        if (commandPurpose == RawRenderPurpose::ViewportRefinement) {
            m_RawWorkspaceFullResolutionPreviewRequestGeneration =
                generation;
        }
        if (RawRenderPurposeMayPublishPresentation(commandPurpose)) {
            m_LatestRawPresentationRecipeRevision =
                commandRecipeRevision;
        } else {
            m_LatestRawAuxiliaryRecipeRevision =
                commandRecipeRevision;
        }
        if (commandPurpose == RawRenderPurpose::ExplicitExport) {
            m_RawWorkspaceExportRenderGeneration = generation;
        }
        previews.clear();
    }
    return submitted;
}

bool EditorModule::RefreshCompletedChainCacheIfNeeded() const noexcept {
    const std::uint64_t structureRevision = m_Project->graph.GetStructureRevision();
    if (m_CachedCompletedChainsStructureRevision == structureRevision) {
        return true;
    }

    try {
        const std::vector<EditorNodeGraph::CompletedChainInfo>& completedChains =
            m_Project->graph.GetCompletedChains();
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
    const std::uint64_t structureRevision = m_Project->graph.GetStructureRevision();
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
    for (int nodeId : m_Project->graph.GetDownstreamRenderNodeIds(touchedNodeId)) {
        m_NodeDirtyGenerations[nodeId] = ++m_NodeDirtyGenerationCounter;
    }
}

void EditorModule::MarkAllRenderNodesDirty() {
    for (const EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
        if (m_Project->graph.IsRenderChainNode(node)) {
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

        EditorNodeGraph::Node* node = m_Project->graph.FindNode(feedback.nodeId);
        if (!node) {
            continue;
        }

        try {
            if (node->kind == EditorNodeGraph::NodeKind::Layer) {
                if (node->layerIndex < 0 ||
                    node->layerIndex >= static_cast<int>(m_Project->layers.size()) ||
                    !m_Project->layers[node->layerIndex]) {
                    continue;
                }

                ToneCurveLayer* toneCurve =
                    dynamic_cast<ToneCurveLayer*>(m_Project->layers[node->layerIndex].get());
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
        MarkDirty();
    }
}
