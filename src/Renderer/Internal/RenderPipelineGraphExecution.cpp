#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/RenderTiling.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Utils/PixelBufferUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace Stack::Renderer::GraphExecution;

namespace {

struct ScopedGraphExecutionState {
    Stack::Renderer::GLState::FramebufferState framebuffer{ true };
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean blend = glIsEnabled(GL_BLEND);

    ~ScopedGraphExecutionState() {
        Restore();
    }

    void Restore() const {
        framebuffer.Restore(true);
        SetCapability(GL_SCISSOR_TEST, scissor);
        SetCapability(GL_DEPTH_TEST, depth);
        SetCapability(GL_STENCIL_TEST, stencil);
        SetCapability(GL_BLEND, blend);
    }

private:
    static void SetCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    }
};

void CanonicalizeFrequencyResponseSettings(
    RenderFrequencyResponseSettings& response) {
    const auto finiteOr = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    response.lowCutoff = std::clamp(
        finiteOr(response.lowCutoff, 0.08f), 0.0f, 0.5f);
    response.highCutoff = std::clamp(
        finiteOr(response.highCutoff, 0.25f), 0.0f, 0.5f);
    if (response.lowCutoff > response.highCutoff) {
        std::swap(response.lowCutoff, response.highCutoff);
    }
    response.transitionWidth = std::clamp(
        finiteOr(response.transitionWidth, 0.025f),
        0.000001f,
        0.5f);
    response.butterworthOrder = std::clamp(
        finiteOr(response.butterworthOrder, 2.0f),
        1.0f,
        12.0f);
    if (response.notches.size() > 16u) {
        response.notches.resize(16u);
    }
    for (RenderFrequencyNotch& notch : response.notches) {
        notch.frequency = std::clamp(
            finiteOr(notch.frequency, 0.25f), 0.0f, 0.5f);
        notch.directionDegrees = std::clamp(
            finiteOr(notch.directionDegrees, 0.0f),
            -180.0f,
            180.0f);
        notch.width = std::clamp(
            finiteOr(notch.width, 0.025f),
            0.001f,
            0.25f);
    }
}

} // namespace

void RenderPipeline::ExecuteGraphImpl(
    const RenderGraphSnapshot& graph,
    const GraphTopologyIndex& topology) {
    m_GraphSourceTexture = 0;
    m_GraphSourceWidth = 0;
    m_GraphSourceHeight = 0;
    if (m_BaseCanvasWidth > 0 && m_BaseCanvasHeight > 0) {
        m_Width = m_BaseCanvasWidth;
        m_Height = m_BaseCanvasHeight;
    }
    m_LastGraphImageCacheHits.clear();
    m_LastGraphExecutionStats = {};
    m_LastGraphExecutionStats.persistentCacheBudgetBytes =
        kGraphPersistentCacheSoftByteBudget;
    m_LastGraphExecutionStats.transientPoolBudgetBytes =
        kGraphTransientTargetSoftByteBudget;
    m_AutoGainSceneStatsCache.clear();
    m_PreLocalExposureSummaries.clear();
    m_ToneCurveAutoRewriteFeedback.clear();
    ClearRawDevelopmentStageStatsReadbacks();
    ClearRawDevelopmentLocalRangeOverlay();
    ClearRawDevelopmentLocalRangeTargetSample();
    m_RawDevelopmentLocalSuggestionImage = {};
    m_RawDevelopmentLocalRangeOverlayRequestMode = graph.rawWorkspaceLocalRangeOverlayMode;
    m_RawDevelopmentLocalRangeTargetPreviewRequest =
        graph.rawWorkspaceLocalRangeTargetPreview;
    m_RawDevelopmentLocalRangeTargetSampleRequested =
        graph.rawWorkspaceLocalRangeTargetSampleRequested;
    m_RawDevelopmentLocalRangeTargetSampleRequestU =
        std::clamp(graph.rawWorkspaceLocalRangeTargetSampleU, 0.0f, 1.0f);
    m_RawDevelopmentLocalRangeTargetSampleRequestV =
        std::clamp(graph.rawWorkspaceLocalRangeTargetSampleV, 0.0f, 1.0f);
    if (!topology.IsBoundTo(graph) || !topology.valid) {
        m_OutputTexture = 0;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId =
            graph.outputNodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure =
            !topology.IsBoundTo(graph)
                ? "Graph scheduling failed: the render topology index is not bound to this graph snapshot."
                : "Graph scheduling failed: " +
                    (topology.error.empty()
                        ? std::string("the render topology index is invalid.")
                        : topology.error);
        return;
    }
    const bool hasOutputNode =
        topology.nodes.count(graph.outputNodeId) != 0;
    if (m_Width == 0 || m_Height == 0 || !hasOutputNode) {
        m_OutputTexture = 0;
        return;
    }

    const bool rawDevelopmentSideEffectsRequested =
        graph.rawWorkspaceLocalRangeTargetSampleRequested ||
        (!graph.rawWorkspaceLocalRangeOverlayMode.empty() &&
         graph.rawWorkspaceLocalRangeOverlayMode != "none");
    std::vector<ScheduledGraphOutput> scheduleExtraRoots;
    if (rawDevelopmentSideEffectsRequested) {
        for (const RenderGraphNode& node : graph.nodes) {
            if (node.kind == RenderGraphNodeKind::RawDevelopment) {
                scheduleExtraRoots.push_back(
                    ScheduledGraphOutput{
                        node.nodeId,
                        EditorNodeGraph::kImageOutputSocketId
                    });
            }
        }
    }
    const GraphEvaluationSchedule evaluationSchedule =
        BuildGraphEvaluationSchedule(
            graph,
            topology,
            ScheduledGraphOutput{
                graph.outputNodeId,
                graph.outputSocketId
            },
            scheduleExtraRoots);
    if (!evaluationSchedule.valid) {
        m_OutputTexture = 0;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId =
            graph.outputNodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure =
            "Graph scheduling failed: " + evaluationSchedule.error;
        return;
    }

    const RenderGraphRegionPlan regionPlan =
        RenderTiling::PlanGraphRegions(graph, m_Width, m_Height);
    if (!regionPlan.valid) {
        m_OutputTexture = 0;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId = graph.outputNodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure =
            "Graph planning failed: " + regionPlan.reason;
        return;
    }

    const ScopedGraphExecutionState savedExecutionState;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }

    const std::size_t executionEntryCapacity =
        std::max<std::size_t>(1u, evaluationSchedule.outputs.size());
    GraphExecutionContext executionContext(
        graph,
        topology,
        executionEntryCapacity);
    auto& nodes = executionContext.nodes;
    auto& imageCache = executionContext.imageCache;
    auto& maskCache = executionContext.maskCache;
    auto& imageFingerprintCache = executionContext.imageFingerprintCache;
    auto& maskFingerprintCache = executionContext.maskFingerprintCache;
    auto& visitingImages = executionContext.visitingImages;
    auto& visitingMasks = executionContext.visitingMasks;
    auto& fingerprintingImages = executionContext.fingerprintingImages;
    auto& fingerprintingMasks = executionContext.fingerprintingMasks;
    std::unordered_map<std::string, double> scalarCache;
    std::unordered_map<std::string, std::pair<int, int>> localTextureExtents;
    std::unordered_map<std::string, std::size_t> scalarSampleCounts;
    std::unordered_map<std::string, std::size_t> scalarFingerprintCache;
    std::unordered_set<std::string> visitingScalars;
    std::unordered_set<std::string> fingerprintingScalars;
    std::unordered_map<std::string, RenderFrequencyResource> frequencyCache;
    std::unordered_map<int, RenderFrequencyResponseSettings> responseCache;
    std::unordered_map<std::string, std::size_t> frequencyFingerprintCache;
    std::unordered_map<int, std::size_t> responseFingerprintCache;
    std::unordered_set<std::string> visitingFrequency;
    std::unordered_set<std::string> fingerprintingFrequency;
    std::set<int> visitingResponses;
    std::set<int> fingerprintingResponses;
    std::unordered_map<int, RenderSpectrumAnalysis> spectrumAnalysisCache;
    scalarCache.reserve(executionEntryCapacity);
    localTextureExtents.reserve(executionEntryCapacity);
    scalarSampleCounts.reserve(executionEntryCapacity);
    scalarFingerprintCache.reserve(executionEntryCapacity);
    visitingScalars.reserve(executionEntryCapacity);
    fingerprintingScalars.reserve(executionEntryCapacity);
    frequencyCache.reserve(executionEntryCapacity);
    frequencyFingerprintCache.reserve(executionEntryCapacity);
    visitingFrequency.reserve(executionEntryCapacity);
    fingerprintingFrequency.reserve(executionEntryCapacity);
    responseCache.reserve(graph.nodes.size());
    responseFingerprintCache.reserve(graph.nodes.size());
    spectrumAnalysisCache.reserve(graph.nodes.size());

    auto findInputLink = [&](int nodeId, const std::string& socketId) -> const RenderGraphLink* {
        return executionContext.FindInputLink(nodeId, socketId);
    };

    auto createTarget = [&]() {
        return CreateGraphRenderTargetTexture();
    };

    auto renderToTexture = [&](unsigned int& texture, auto&& renderFn) -> bool {
        if (RenderIntoGraphTargetTexture(
                texture,
                std::forward<decltype(renderFn)>(renderFn))) {
            return true;
        }
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        return false;
    };
    auto renderPassToTexture = [&](
        unsigned int& texture,
        auto&& renderFn) -> bool {
        bool passExecuted = false;
        const bool targetRendered = RenderIntoGraphTargetTexture(
            texture,
            [&](unsigned int fbo) {
                passExecuted = renderFn(fbo);
            });
        if (targetRendered && passExecuted) {
            return true;
        }
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        return false;
    };

    std::function<unsigned int(int, const std::string&)> evalMask;
    std::function<unsigned int(int, const std::string&)> evalImage;
    std::function<bool(int, const std::string&, double&)> evalScalar;
    std::function<std::size_t(int, const std::string&)> fingerprintMask;
    std::function<std::size_t(int, const std::string&)> fingerprintImage;
    std::function<std::size_t(int, const std::string&)> fingerprintScalar;
    std::function<std::size_t(int, const std::string&)> fingerprintFrequency;
    std::function<std::size_t(int)> fingerprintResponse;
    std::function<RenderFrequencyResource(int, const std::string&)> evalFrequency;
    std::function<bool(int, RenderFrequencyResponseSettings&)> evalResponse;
    std::function<bool(int, RenderSpectrumAnalysis&)> evalSpectrumAnalysis;
    std::unordered_set<int> pointwiseFusionDisabledNodes;
    pointwiseFusionDisabledNodes.reserve(graph.nodes.size());

    fingerprintMask = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = maskFingerprintCache.find(key);
        if (cached != maskFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingMasks.count(key)) {
            return 0;
        }
        fingerprintingMasks.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawDevelopment ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::HdrMerge ||
            node.kind == RenderGraphNodeKind::Mfsr ||
            node.kind == RenderGraphNodeKind::RawProjectSourceSet ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            node.kind == RenderGraphNodeKind::TechnicalImage ||
            node.kind == RenderGraphNodeKind::Reformat ||
            (node.kind == RenderGraphNodeKind::DataMath && !IsScalarRenderSocket(executionContext, nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::FrequencyFft ||
            node.kind == RenderGraphNodeKind::SpectrumView ||
            node.kind == RenderGraphNodeKind::SpectrumMath ||
            (node.kind == RenderGraphNodeKind::MagnitudePhase && socketId != EditorNodeGraph::kMaskOutputSocketId) ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            std::size_t imgFp = fingerprintImage(nodeId, socketId);
            fingerprintingMasks.erase(key);
            maskFingerprintCache[key] = imgFp;
            return imgFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        HashCombine(fingerprint, HashValue(node.semanticDescriptorIdentity));

        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskKind)));
            HashCombine(fingerprint, HashValue(node.maskSettings.value));
            HashCombine(fingerprint, HashValue(node.maskSettings.angle));
            HashCombine(fingerprint, HashValue(node.maskSettings.offset));
            HashCombine(fingerprint, HashValue(node.maskSettings.scale));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerX));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerY));
            HashCombine(fingerprint, HashValue(node.maskSettings.radius));
            HashCombine(fingerprint, HashValue(node.maskSettings.feather));
            HashCombine(fingerprint, HashValue(node.maskSettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind ==
                   RenderGraphNodeKind::ConstantChannel) {
            HashCombine(
                fingerprint,
                HashValue(node.constantChannelValue));
            const RenderGraphLink* extentInput =
                findInputLink(
                    node.nodeId,
                    EditorNodeGraph::kMatchExtentInputSocketId);
            HashCombine(
                fingerprint,
                extentInput
                    ? fingerprintMask(
                        extentInput->fromNodeId,
                        extentInput->fromSocketId)
                    : 0);
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            HashCombine(fingerprint, inputA ? fingerprintMask(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintMask(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskCombineMode)));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::CustomMask) {
            const RenderCustomMaskPayload& payload = node.customMask;
            HashCombine(fingerprint, HashValue(payload.width));
            HashCombine(fingerprint, HashValue(payload.height));
            HashCombine(fingerprint, HashValue(payload.invert));
            HashCombine(fingerprint, HashValue(payload.blurRadius));
            HashCombine(fingerprint, HashValue(payload.expandContract));
            HashCombine(fingerprint, HashValue(payload.rasterLayer.size()));
            if (!payload.rasterLayer.empty()) {
                HashCombine(
                    fingerprint,
                    HashBytes(
                        reinterpret_cast<const unsigned char*>(payload.rasterLayer.data()),
                        payload.rasterLayer.size() * sizeof(float)));
            }
            HashCombine(fingerprint, HashValue(payload.objects.size()));
            for (const RenderCustomMaskObject& object : payload.objects) {
                HashCombine(fingerprint, HashValue(object.id));
                HashCombine(fingerprint, HashValue(static_cast<int>(object.type)));
                HashCombine(fingerprint, HashValue(static_cast<int>(object.operation)));
                HashCombine(fingerprint, HashValue(object.enabled));
                HashCombine(fingerprint, HashValue(object.invert));
                HashCombine(fingerprint, HashValue(object.strength));
                HashCombine(fingerprint, HashValue(object.feather));
                HashCombine(fingerprint, HashValue(object.blur));
                HashCombine(fingerprint, HashValue(object.points.size()));
                for (const RenderCustomMaskPoint& point : object.points) {
                    HashCombine(fingerprint, HashValue(point.x));
                    HashCombine(fingerprint, HashValue(point.y));
                }
            }
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, input ? fingerprintMask(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskUtilityKind)));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.blackPoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.whitePoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.gamma));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.threshold));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.softness));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.enabled));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageToMaskKind)));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.low));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.high));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.softness));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.invert));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleCount));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[0]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[1]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[2]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleLuma));
            for (int i = 0; i < 4; ++i) {
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][0]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][1]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][2]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleLuma[i]));
            }
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleU));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleV));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.toneSimilarity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.colorSimilarity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.regionRadius));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.regionFeather));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.edgeSensitivity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.localCoherence));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, node.nodeId)) {
                HashCombine(fingerprint, HashValue(input.socketId));
                HashCombine(fingerprint, fingerprintMask(input.link->fromNodeId, input.link->fromSocketId));
            }
            if (const RenderGraphLink* baseInput = findInputLink(node.nodeId, EditorNodeGraph::kDataMathBaseInputSocketId)) {
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kDataMathBaseInputSocketId)));
                HashCombine(fingerprint, fingerprintMask(baseInput->fromNodeId, baseInput->fromSocketId));
            }
            if (const RenderGraphLink* maskInput = findInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kMaskInputSocketId)));
                HashCombine(fingerprint, fingerprintMask(maskInput->fromNodeId, maskInput->fromSocketId));
            }
            HashCombine(fingerprint, HashValue(static_cast<int>(node.dataMathMode)));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantA));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantB));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.minValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.maxValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMin));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMax));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::FrequencyMask) {
            const RenderFrequencyMaskSettings& settings = node.frequencyMaskSettings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.shape)));
            HashCombine(fingerprint, HashValue(settings.cutoff));
            HashCombine(fingerprint, HashValue(settings.width));
            HashCombine(fingerprint, HashValue(settings.feather));
            HashCombine(fingerprint, HashValue(settings.order));
            HashCombine(fingerprint, HashValue(settings.centerX));
            HashCombine(fingerprint, HashValue(settings.centerY));
            HashCombine(fingerprint, HashValue(settings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MagnitudePhase) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.magnitudePhaseMode)));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.exposure));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.gamma));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailAutoMask.settings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.mode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.baseRadiusPercent));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailFusion.settings;
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.baseRadiusPercent));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::FrequencyFilter) {
            const RenderGraphLink* channel = findInputLink(
                node.nodeId, EditorNodeGraph::kChannelInputSocketId);
            const RenderGraphLink* response = findInputLink(
                node.nodeId, EditorNodeGraph::kFrequencyResponseInputSocketId);
            const RenderGraphLink* strength = findInputLink(
                node.nodeId,
                EditorNodeGraph::ParameterInputSocketId(
                    EditorNodeGraph::kStrengthParameterId));
            HashCombine(fingerprint, channel
                ? fingerprintMask(channel->fromNodeId, channel->fromSocketId) : 0);
            HashCombine(fingerprint, response
                ? fingerprintResponse(response->fromNodeId)
                : HashValue(static_cast<int>(node.frequencyFilterSettings.localResponse.mode)));
            if (!response) {
                const auto& settings = node.frequencyFilterSettings.localResponse;
                HashCombine(fingerprint, HashValue(static_cast<int>(settings.profile)));
                HashCombine(fingerprint, HashValue(settings.lowCutoff));
                HashCombine(fingerprint, HashValue(settings.highCutoff));
                HashCombine(fingerprint, HashValue(settings.transitionWidth));
                HashCombine(fingerprint, HashValue(settings.butterworthOrder));
                for (const RenderFrequencyNotch& notch : settings.notches) {
                    HashCombine(fingerprint, HashValue(notch.id));
                    HashCombine(fingerprint, HashValue(notch.frequency));
                    HashCombine(fingerprint, HashValue(notch.directionDegrees));
                    HashCombine(fingerprint, HashValue(notch.width));
                }
            }
            HashCombine(fingerprint, strength
                ? fingerprintScalar(strength->fromNodeId, strength->fromSocketId)
                : HashValue(node.frequencyFilterSettings.strength));
            HashCombine(fingerprint, HashValue(
                static_cast<int>(node.frequencyFilterSettings.edgePolicy)));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::FrequencyIfft) {
            const RenderGraphLink* spectrum = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, spectrum
                ? fingerprintFrequency(spectrum->fromNodeId, spectrum->fromSocketId) : 0);
        }

        fingerprintingMasks.erase(key);
        maskFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    fingerprintScalar = [&](int nodeId, const std::string& socketId) -> std::size_t {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = scalarFingerprintCache.find(key);
            cached != scalarFingerprintCache.end()) {
            return cached->second;
        }
        if (!fingerprintingScalars.insert(key).second) return 0;
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr) {
            fingerprintingScalars.erase(key);
            return 0;
        }
        const RenderGraphNode& node = *nodeIt->second;
        const bool fieldMeanOutput =
            node.kind == RenderGraphNodeKind::FieldMean &&
            socketId == EditorNodeGraph::kValueOutputSocketId;
        const bool analyzerOutput =
            node.kind == RenderGraphNodeKind::SpectrumAnalyzer &&
            (socketId == EditorNodeGraph::kBandPowerOutputSocketId ||
             socketId == EditorNodeGraph::kPeakFrequencyOutputSocketId ||
             socketId == EditorNodeGraph::kPeakDirectionOutputSocketId);
        if (!fieldMeanOutput && !analyzerOutput) {
            fingerprintingScalars.erase(key);
            return 0;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        if (fieldMeanOutput) {
            HashCombine(fingerprint, HashValue(Stack::NodeMath::kFieldMeanAlgorithmVersion));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kReductionFieldInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintMask(input->fromNodeId, input->fromSocketId)
                : 0);
        } else {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintFrequency(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.innerRadius));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.outerRadius));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.excludeDc));
            for (const char* parameterId : {
                     EditorNodeGraph::kAnalyzerLowParameterId,
                     EditorNodeGraph::kAnalyzerHighParameterId}) {
                const RenderGraphLink* parameterInput = findInputLink(
                    node.nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
                HashCombine(fingerprint, parameterInput
                    ? fingerprintScalar(
                        parameterInput->fromNodeId,
                        parameterInput->fromSocketId)
                    : 0);
            }
        }
        fingerprintingScalars.erase(key);
        scalarFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    fingerprintImage = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = imageFingerprintCache.find(key);
        if (cached != imageFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingImages.count(key)) {
            return 0;
        }
        fingerprintingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            node.kind == RenderGraphNodeKind::ConstantChannel ||
            node.kind == RenderGraphNodeKind::FrequencyFilter ||
            node.kind == RenderGraphNodeKind::FrequencyIfft ||
            node.kind == RenderGraphNodeKind::FrequencyMask ||
            (node.kind == RenderGraphNodeKind::MagnitudePhase && socketId == EditorNodeGraph::kMaskOutputSocketId) ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            std::size_t maskFp = fingerprintMask(nodeId, socketId);
            fingerprintingImages.erase(key);
            imageFingerprintCache[key] = maskFp;
            return maskFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        HashCombine(fingerprint, HashValue(node.semanticDescriptorIdentity));
        auto hashRawDevelopSettings = [&](const Raw::RawDevelopSettings& settings) {
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.processingVersion)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.workingSpace)));
            HashCombine(fingerprint, HashValue(settings.applyBaselineExposure));
            HashCombine(fingerprint, HashValue(settings.encodeSrgbOutput));
            HashCombine(fingerprint, HashValue(settings.exposureStops));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.whiteBalanceMode)));
            for (float value : settings.manualWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(settings.overrideBlackLevel));
            HashCombine(fingerprint, HashValue(settings.blackLevelOverride));
            HashCombine(fingerprint, HashValue(settings.overrideWhiteLevel));
            HashCombine(fingerprint, HashValue(settings.whiteLevelOverride));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.highlightMode)));
            HashCombine(fingerprint, HashValue(settings.highlightStrength));
            HashCombine(fingerprint, HashValue(settings.highlightThreshold));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.demosaicMethod)));
            HashCombine(fingerprint, HashValue(settings.cameraTransformEnabled));
            HashCombine(fingerprint, HashValue(settings.debugBypassCameraTransform));
            HashCombine(fingerprint, HashValue(settings.debugTransposeCameraMatrix));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(settings.rotationDegrees));
            HashCombine(fingerprint, HashValue(settings.rotateToFitFrame));
            HashCombine(fingerprint, HashValue(settings.flipHorizontally));
            HashCombine(fingerprint, HashValue(settings.flipVertically));
            HashCombine(fingerprint, HashValue(settings.falseColorSuppression));
            HashCombine(fingerprint, HashValue(settings.defringeStrength));
            HashCombine(fingerprint, HashValue(settings.highlightEdgeCleanup));
            HashCombine(fingerprint, HashValue(settings.chromaRadius));
            HashCombine(fingerprint, HashValue(settings.preserveRealColor));
            HashCombine(fingerprint, HashValue(settings.lateralRedCyan));
            HashCombine(fingerprint, HashValue(settings.lateralBlueYellow));
            HashCombine(fingerprint, HashValue(settings.toneCurvePoints.size()));
            for (const Raw::RawToneCurvePoint& point : settings.toneCurvePoints) {
                HashCombine(fingerprint, HashValue(point.input));
                HashCombine(fingerprint, HashValue(point.output));
            }
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.cameraTransformSource)));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.enabled));
            HashCombine(
                fingerprint,
                HashValue(static_cast<int>(settings.mosaicDenoise.mode)));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.hotPixelSuppression));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.hotPixelThreshold));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.lumaStrength));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.chromaStrength));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.radius));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.edgeProtection));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.iterations));
        };
        auto hashDngGainMaps = [&](const std::vector<Raw::DngGainMapOpcode>& maps) {
            HashCombine(fingerprint, HashValue(maps.size()));
            for (const Raw::DngGainMapOpcode& map : maps) {
                HashCombine(fingerprint, HashValue(map.top));
                HashCombine(fingerprint, HashValue(map.left));
                HashCombine(fingerprint, HashValue(map.bottom));
                HashCombine(fingerprint, HashValue(map.right));
                HashCombine(fingerprint, HashValue(map.plane));
                HashCombine(fingerprint, HashValue(map.planes));
                HashCombine(fingerprint, HashValue(map.rowPitch));
                HashCombine(fingerprint, HashValue(map.colPitch));
                HashCombine(fingerprint, HashValue(map.mapPointsV));
                HashCombine(fingerprint, HashValue(map.mapPointsH));
                HashCombine(fingerprint, HashValue(map.mapPlanes));
                HashCombine(fingerprint, HashValue(map.mapSpacingV));
                HashCombine(fingerprint, HashValue(map.mapSpacingH));
                HashCombine(fingerprint, HashValue(map.mapOriginV));
                HashCombine(fingerprint, HashValue(map.mapOriginH));
                HashCombine(fingerprint, HashValue(map.gains.size()));
                if (!map.gains.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(map.gains.data()),
                            map.gains.size() * sizeof(float)));
                }
            }
        };
        auto hashRawMetadata = [&](const Raw::RawMetadata& metadata) {
            HashCombine(fingerprint, HashValue(metadata.sourcePath));
            HashCombine(fingerprint, HashValue(metadata.rawWidth));
            HashCombine(fingerprint, HashValue(metadata.rawHeight));
            HashCombine(fingerprint, HashValue(metadata.visibleWidth));
            HashCombine(fingerprint, HashValue(metadata.visibleHeight));
            HashCombine(fingerprint, HashValue(metadata.leftMargin));
            HashCombine(fingerprint, HashValue(metadata.topMargin));
            HashCombine(fingerprint, HashValue(metadata.orientation));
            HashCombine(fingerprint, HashValue(metadata.bitDepth));
            HashCombine(fingerprint, HashValue(static_cast<int>(metadata.cfaPattern)));
            HashCombine(fingerprint, HashValue(static_cast<int>(metadata.pixelLayout)));
            HashCombine(fingerprint, HashValue(metadata.mosaiced));
            HashCombine(fingerprint, HashValue(metadata.isDng));
            HashCombine(fingerprint, HashValue(metadata.blackLevel));
            for (float value : metadata.perChannelBlack) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(metadata.whiteLevel));
            HashCombine(fingerprint, HashValue(metadata.rawMinimum));
            HashCombine(fingerprint, HashValue(metadata.rawMaximum));
            HashCombine(fingerprint, HashValue(metadata.defaultWhiteClipPercent));
            for (float value : metadata.cameraWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            for (float value : metadata.daylightWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            for (float value : metadata.cameraToSrgb) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(metadata.hasCameraMatrix));
            HashCombine(fingerprint, HashValue(metadata.hasDngAsShotNeutral));
            for (float value : metadata.dngAsShotNeutral) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(metadata.dngGainMapCount));
            HashCombine(fingerprint, HashValue(metadata.dngUnsupportedOpcodeCount));
            hashDngGainMaps(metadata.dngGainMaps);
            HashCombine(fingerprint, HashValue(metadata.uploadFormat));
            HashCombine(fingerprint, HashValue(metadata.linearChannels));
            HashCombine(fingerprint, HashValue(static_cast<int>(metadata.linearSampleFormat)));
        };

        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                HashCombine(fingerprint, HashValue(node.image.width));
                HashCombine(fingerprint, HashValue(node.image.height));
                HashCombine(fingerprint, HashValue(node.image.channels));
                HashCombine(
                    fingerprint,
                    node.image.pixels.fingerprint != 0
                        ? node.image.pixels.fingerprint
                        : StackHash::HashBytes(*node.image.pixels.bytes));
            } else {
                HashCombine(fingerprint, m_SourceFingerprint);
                HashCombine(fingerprint, HashValue(m_BaseCanvasWidth));
                HashCombine(fingerprint, HashValue(m_BaseCanvasHeight));
                HashCombine(fingerprint, HashValue(m_SourceChannels));
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            HashCombine(fingerprint, HashValue(node.rawSource.sourcePath));
            const bool hasEmbeddedRaw =
                !node.rawSource.embeddedRawData.rawBuffer.empty() ||
                !node.rawSource.embeddedRawData.linearUInt16Buffer.empty() ||
                !node.rawSource.embeddedRawData.linearFloatBuffer.empty() ||
                (node.rawSource.embeddedRawData.normalizedMosaicBuffer &&
                 !node.rawSource.embeddedRawData
                      .normalizedMosaicBuffer->empty());
            HashCombine(fingerprint, HashValue(hasEmbeddedRaw));
            if (!hasEmbeddedRaw && !node.rawSource.sourcePath.empty()) {
                std::error_code sizeError;
                const std::uintmax_t fileSize = std::filesystem::file_size(node.rawSource.sourcePath, sizeError);
                std::error_code timeError;
                const auto modifiedTime = std::filesystem::last_write_time(node.rawSource.sourcePath, timeError);
                HashCombine(fingerprint, HashValue(!sizeError));
                HashCombine(fingerprint, HashValue(sizeError ? 0ull : static_cast<unsigned long long>(fileSize)));
                HashCombine(fingerprint, HashValue(!timeError));
                HashCombine(
                    fingerprint,
                    HashValue(timeError
                        ? 0ll
                        : static_cast<long long>(modifiedTime.time_since_epoch().count())));
            }
            if (hasEmbeddedRaw) {
                const Raw::RawImageData& embedded = node.rawSource.embeddedRawData;
                hashRawMetadata(embedded.metadata);
                HashCombine(fingerprint, HashValue(embedded.rawBuffer.size()));
                if (!embedded.rawBuffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.rawBuffer.data()),
                            embedded.rawBuffer.size() * sizeof(std::uint16_t)));
                }
                HashCombine(fingerprint, HashValue(embedded.linearUInt16Buffer.size()));
                if (!embedded.linearUInt16Buffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.linearUInt16Buffer.data()),
                            embedded.linearUInt16Buffer.size() * sizeof(std::uint16_t)));
                }
                HashCombine(fingerprint, HashValue(embedded.linearFloatBuffer.size()));
                if (!embedded.linearFloatBuffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.linearFloatBuffer.data()),
                            embedded.linearFloatBuffer.size() * sizeof(float)));
                }
                HashCombine(
                    fingerprint,
                    HashValue(embedded.normalizedMosaicContentHash));
                HashCombine(
                    fingerprint,
                    HashValue(
                        embedded.normalizedMosaicBuffer
                            ? embedded.normalizedMosaicBuffer->size()
                            : 0u));
            }
            hashRawMetadata(node.rawSource.metadata);
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            HashCombine(fingerprint, HashValue(Stack::RawRecipe::SerializeRecipe(node.rawDevelopment.recipe).dump()));
            HashCombine(fingerprint, HashValue(m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(m_RawDevelopmentAnalysisEnabled));
            HashCombine(fingerprint, HashValue(m_RawDevelopmentStageImageReadbackMaxDimension));
            HashCombine(fingerprint, HashValue(static_cast<int>(m_RawDevelopmentGraphScopeStage)));
            HashCombine(fingerprint, HashValue(m_RawDevelopmentGraphScopeReadbackMaxDimension));
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(NeuralDenoise::SerializeSettings(node.rawNeuralDenoise.settings)));
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
            hashRawDevelopSettings(node.rawDecode.settings);
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
            hashRawDevelopSettings(node.rawDevelop.settings);
            const bool rawBaseSocket = socketId == "__rawDevelopBase";
            if (!rawBaseSocket) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.scenePrepEnabled));
            }
            if (!rawBaseSocket && node.rawDevelop.scenePrepEnabled) {
                const Raw::RawDetailFusionSettings& prep = node.rawDevelop.scenePrepSettings;
                HashCombine(fingerprint, HashValue(prep.autoSafetyEnabled));
                HashCombine(fingerprint, HashValue(prep.overrideMinEv));
                HashCombine(fingerprint, HashValue(prep.overrideMaxEv));
                HashCombine(fingerprint, HashValue(prep.overrideBaseEv));
                HashCombine(fingerprint, HashValue(prep.overrideNoiseProtection));
                HashCombine(fingerprint, HashValue(prep.overrideHighlightProtection));
                HashCombine(fingerprint, HashValue(prep.overrideShadowLiftLimit));
                HashCombine(fingerprint, HashValue(prep.overrideWellExposedTarget));
                HashCombine(fingerprint, HashValue(prep.minEvBias));
                HashCombine(fingerprint, HashValue(prep.maxEvBias));
                HashCombine(fingerprint, HashValue(prep.baseEvBias));
                HashCombine(fingerprint, HashValue(prep.noiseProtectionBias));
                HashCombine(fingerprint, HashValue(prep.highlightProtectionBias));
                HashCombine(fingerprint, HashValue(prep.shadowLiftLimitBias));
                HashCombine(fingerprint, HashValue(prep.wellExposedTargetBias));
                HashCombine(fingerprint, HashValue(prep.minEv));
                HashCombine(fingerprint, HashValue(prep.maxEv));
                HashCombine(fingerprint, HashValue(prep.baseEv));
                HashCombine(fingerprint, HashValue(prep.strength));
                HashCombine(fingerprint, HashValue(prep.sampleCount));
                HashCombine(fingerprint, HashValue(prep.baseRadiusPercent));
                HashCombine(fingerprint, HashValue(prep.highlightProtection));
                HashCombine(fingerprint, HashValue(prep.shadowLiftLimit));
                HashCombine(fingerprint, HashValue(prep.noiseProtection));
                HashCombine(fingerprint, HashValue(prep.detailWeight));
                HashCombine(fingerprint, HashValue(prep.wellExposedTarget));
                HashCombine(fingerprint, HashValue(prep.smoothGradientProtection));
                HashCombine(fingerprint, HashValue(prep.textureSensitivity));
                HashCombine(fingerprint, HashValue(prep.skyBias));
                HashCombine(fingerprint, HashValue(prep.smoothnessRadius));
                HashCombine(fingerprint, HashValue(prep.smoothAreaRadius));
                HashCombine(fingerprint, HashValue(prep.edgeAwareness));
                HashCombine(fingerprint, HashValue(prep.haloGuard));
                HashCombine(fingerprint, HashValue(prep.maskDebandDither));
            }
            const bool preFinishSocket = socketId == EditorNodeGraph::kPreFinishImageOutputSocketId;
            if (!preFinishSocket && !rawBaseSocket) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.integratedToneEnabled));
            }
            if (!preFinishSocket && !rawBaseSocket && node.rawDevelop.integratedToneEnabled) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.integratedToneLayerJson.dump()));
                HashCombine(fingerprint, fingerprintMask(node.nodeId, "maskIn"));
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, fingerprintMask(node.nodeId, "maskOut"));
            const Raw::RawDetailFusionSettings settings = ResolveRawDetailFusionApplySettings(executionContext, node);
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::HdrMerge) {
            const RenderGraphLink* input1 = findInputLink(node.nodeId, "image1");
            const RenderGraphLink* input2 = findInputLink(node.nodeId, "image2");
            const RenderGraphLink* input3 = findInputLink(node.nodeId, "image3");
            HashCombine(fingerprint, input1 ? fingerprintImage(input1->fromNodeId, input1->fromSocketId) : 0);
            HashCombine(fingerprint, input2 ? fingerprintImage(input2->fromNodeId, input2->fromSocketId) : 0);
            HashCombine(fingerprint, input3 ? fingerprintImage(input3->fromNodeId, input3->fromSocketId) : 0);
            const Raw::HdrMergeSettings& settings = node.hdrMerge.settings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.alignmentMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.exposureMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.referenceMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.deghostMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.motionPriority)));
            for (float exposureEv : settings.manualExposureEv) {
                HashCombine(fingerprint, HashValue(exposureEv));
            }
            for (float exposureEv : settings.exposureOffsetEv) {
                HashCombine(fingerprint, HashValue(exposureEv));
            }
            HashCombine(fingerprint, HashValue(settings.autoReliability));
            HashCombine(fingerprint, HashValue(settings.clipThreshold));
            HashCombine(fingerprint, HashValue(settings.clipFeather));
            HashCombine(fingerprint, HashValue(settings.blackThreshold));
            HashCombine(fingerprint, HashValue(settings.blackFeather));
            HashCombine(fingerprint, HashValue(settings.readNoise));
            HashCombine(fingerprint, HashValue(settings.noiseAware));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Mfsr) {
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxMfsrInputCount; ++inputIndex) {
                const std::string inputSocketId = EditorNodeGraph::MfsrInputSocketId(inputIndex);
                const RenderGraphLink* input = findInputLink(node.nodeId, inputSocketId);
                HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            }
            const Stack::Mfsr::MfsrSettings& settings = node.mfsr.settings;
            HashCombine(fingerprint, HashValue(settings.schemaVersion));
            HashCombine(fingerprint, HashValue(settings.algorithmVersion));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.scalePreset)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.qualityPreset)));
            HashCombine(fingerprint, HashValue(settings.preferRawMosaicPath));
            HashCombine(fingerprint, HashValue(settings.maxInputFrames));
        } else if (node.kind == RenderGraphNodeKind::RawProjectSourceSet) {
            HashCombine(fingerprint, HashValue(node.rawProjectSourceSet.sourceSetId));
            HashCombine(fingerprint, HashValue(node.rawProjectSourceSet.unavailableStatus));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.inputRevision));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.postRecipeRevision));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.contentHash));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.resultAvailable));
            if (node.rawProjectSourceSet.resultAvailable) {
                HashCombine(
                    fingerprint,
                    HashValue(Stack::RawRecipe::SerializeRecipe(
                        node.rawDevelopment.recipe).dump()));
                HashCombine(fingerprint, HashValue(m_PreviewMaxDimension));
                HashCombine(
                    fingerprint,
                    HashValue(m_RawDevelopmentAnalysisEnabled));
            }
            HashCombine(fingerprint, HashValue(node.rawProjectSourceSet.quarantined));
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.importFormat)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.useMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.inputTransform)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.outputTransform)));
            HashCombine(fingerprint, HashLut1DStage(node.lut.lut1D));
            HashCombine(fingerprint, HashLut1DStage(node.lut.shaper1D));
            HashCombine(fingerprint, HashLut3DStage(node.lut.lut3D));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageGeneratorKind)));
            for (float channel : node.imageGeneratorSettings.colorA) {
                HashCombine(fingerprint, HashValue(channel));
            }
            for (float channel : node.imageGeneratorSettings.colorB) {
                HashCombine(fingerprint, HashValue(channel));
            }
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.angle));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.offset));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.text));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.fontSize));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.textBackdropBlur));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.textBackdropOpacity));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.textBackdropPadding));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(node.layerJson));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::TechnicalImage) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.technicalImageOperation)));
            const RenderGraphLink* exposureInput = findInputLink(
                node.nodeId, EditorNodeGraph::kExposureValueInputSocketId);
            HashCombine(fingerprint, exposureInput
                ? fingerprintScalar(exposureInput->fromNodeId, exposureInput->fromSocketId)
                : HashValue(node.technicalExposureValue));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Reformat) {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(Stack::NodeMath::kReformatAlgorithmVersion));
            HashCombine(fingerprint, HashValue(node.reformatSettings.width));
            HashCombine(fingerprint, HashValue(node.reformatSettings.height));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.reformatSettings.filter)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.reformatSettings.border)));
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
            HashCombine(fingerprint, inputA ? fingerprintImage(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintImage(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, factorLink ? fingerprintMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.mixBlendMode)));
            HashCombine(fingerprint, HashValue(node.mixFactor));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, node.nodeId)) {
                const bool scalarInput = IsScalarRenderSocket(executionContext, input.link->fromNodeId, input.link->fromSocketId);
                HashCombine(fingerprint, HashValue(input.socketId));
                HashCombine(
                    fingerprint,
                    scalarInput
                        ? fingerprintMask(input.link->fromNodeId, input.link->fromSocketId)
                        : fingerprintImage(input.link->fromNodeId, input.link->fromSocketId));
                HashCombine(fingerprint, HashValue(scalarInput));
            }
            if (const RenderGraphLink* baseInput = findInputLink(node.nodeId, EditorNodeGraph::kDataMathBaseInputSocketId)) {
                const bool scalarBase = IsScalarRenderSocket(executionContext, baseInput->fromNodeId, baseInput->fromSocketId);
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kDataMathBaseInputSocketId)));
                HashCombine(
                    fingerprint,
                    scalarBase
                        ? fingerprintMask(baseInput->fromNodeId, baseInput->fromSocketId)
                        : fingerprintImage(baseInput->fromNodeId, baseInput->fromSocketId));
                HashCombine(fingerprint, HashValue(scalarBase));
            } else {
                HashCombine(fingerprint, HashValue(false));
            }
            if (const RenderGraphLink* maskInput = findInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kMaskInputSocketId)));
                HashCombine(fingerprint, fingerprintMask(maskInput->fromNodeId, maskInput->fromSocketId));
            } else {
                HashCombine(fingerprint, std::size_t{ 0 });
            }
            HashCombine(fingerprint, HashValue(IsScalarRenderSocket(executionContext, node.nodeId, socketId)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.dataMathMode)));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantA));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantB));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.minValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.maxValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMin));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMax));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::SpectrumView) {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintFrequency(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumViewSettings.mode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumViewSettings.lut)));
            HashCombine(fingerprint, HashValue(node.spectrumViewSettings.exposure));
            HashCombine(fingerprint, HashValue(node.spectrumViewSettings.gamma));
            HashCombine(fingerprint, HashValue(node.spectrumViewSettings.centerDc));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::SpectrumMath) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, EditorNodeGraph::kMixInputASocketId);
            const RenderGraphLink* inputB = findInputLink(node.nodeId, EditorNodeGraph::kMixInputBSocketId);
            const RenderGraphLink* maskInput = findInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId);
            HashCombine(fingerprint, inputA ? fingerprintImage(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintImage(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, maskInput ? fingerprintMask(maskInput->fromNodeId, maskInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumMathMode)));
            HashCombine(fingerprint, HashValue(node.spectrumMathSettings.amount));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MagnitudePhase) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.magnitudePhaseMode)));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.exposure));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.gamma));
            if (node.magnitudePhaseMode == RenderMagnitudePhaseMode::Recombine &&
                socketId == EditorNodeGraph::kImageOutputSocketId) {
                const RenderGraphLink* magInput = findInputLink(node.nodeId, "magnitude");
                const RenderGraphLink* phaseInput = findInputLink(node.nodeId, "phase");
                HashCombine(fingerprint, magInput ? fingerprintMask(magInput->fromNodeId, magInput->fromSocketId) : 0);
                HashCombine(fingerprint, phaseInput ? fingerprintMask(phaseInput->fromNodeId, phaseInput->fromSocketId) : 0);
            } else {
                const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
                HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            }
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::SpectrumAnalyzer) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumAnalyzerMode)));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.innerRadius));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.outerRadius));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                const bool channelInput = IsScalarRenderSocket(
                    executionContext,
                    input->fromNodeId,
                    input->fromSocketId);
                HashCombine(
                    fingerprint,
                    channelInput
                        ? fingerprintMask(
                            input->fromNodeId,
                            input->fromSocketId)
                        : fingerprintImage(
                            input->fromNodeId,
                            input->fromSocketId));
            }
            HashCombine(
                fingerprint,
                HashValue(static_cast<int>(node.outputChannelViewMode)));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
            HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
            HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
            HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
            HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        }

        fingerprintingImages.erase(key);
        imageFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    const auto hashResponseSettings = [&](std::size_t& fingerprint,
                                          const RenderFrequencyResponseSettings& settings) {
        HashCombine(fingerprint, HashValue(static_cast<int>(settings.mode)));
        HashCombine(fingerprint, HashValue(static_cast<int>(settings.profile)));
        HashCombine(fingerprint, HashValue(settings.lowCutoff));
        HashCombine(fingerprint, HashValue(settings.highCutoff));
        HashCombine(fingerprint, HashValue(settings.transitionWidth));
        HashCombine(fingerprint, HashValue(settings.butterworthOrder));
        HashCombine(fingerprint, HashValue(settings.notches.size()));
        for (const RenderFrequencyNotch& notch : settings.notches) {
            HashCombine(fingerprint, HashValue(notch.id));
            HashCombine(fingerprint, HashValue(notch.frequency));
            HashCombine(fingerprint, HashValue(notch.directionDegrees));
            HashCombine(fingerprint, HashValue(notch.width));
        }
    };

    fingerprintResponse = [&](int nodeId) -> std::size_t {
        if (const auto cached = responseFingerprintCache.find(nodeId);
            cached != responseFingerprintCache.end()) return cached->second;
        if (!fingerprintingResponses.insert(nodeId).second) return 0;
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr ||
            nodeIt->second->kind != RenderGraphNodeKind::FrequencyResponse) {
            fingerprintingResponses.erase(nodeId);
            return 0;
        }
        const RenderGraphNode& node = *nodeIt->second;
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        hashResponseSettings(fingerprint, node.frequencyResponseSettings);
        for (const std::string& parameterId : {
                 std::string(EditorNodeGraph::kLowCutoffParameterId),
                 std::string(EditorNodeGraph::kHighCutoffParameterId),
                 std::string(EditorNodeGraph::kTransitionWidthParameterId),
                 std::string(EditorNodeGraph::kButterworthOrderParameterId)}) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
            HashCombine(fingerprint, input
                ? fingerprintScalar(input->fromNodeId, input->fromSocketId) : 0);
        }
        for (std::size_t notchIndex = 0;
             notchIndex < node.frequencyResponseSettings.notches.size();
             ++notchIndex) {
            for (const char* field : { "frequency", "direction", "width" }) {
                const RenderGraphLink* input = findInputLink(
                    nodeId,
                        EditorNodeGraph::ParameterInputSocketId(
                            EditorNodeGraph::FrequencyNotchParameterId(
                                node.frequencyResponseSettings.notches[notchIndex].id,
                                field)));
                HashCombine(fingerprint, input
                    ? fingerprintScalar(
                        input->fromNodeId, input->fromSocketId)
                    : 0);
            }
        }
        fingerprintingResponses.erase(nodeId);
        responseFingerprintCache[nodeId] = fingerprint;
        return fingerprint;
    };

    fingerprintFrequency = [&](int nodeId, const std::string& socketId) -> std::size_t {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = frequencyFingerprintCache.find(key);
            cached != frequencyFingerprintCache.end()) return cached->second;
        if (!fingerprintingFrequency.insert(key).second) return 0;
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr) {
            fingerprintingFrequency.erase(key);
            return 0;
        }
        const RenderGraphNode& node = *nodeIt->second;
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        if (node.kind == RenderGraphNodeKind::FrequencyFft) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::kChannelInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintMask(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(
                static_cast<int>(node.frequencyFftSettings.edgePolicy)));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ApplyFrequencyResponse) {
            const RenderGraphLink* spectrum = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            const RenderGraphLink* response = findInputLink(
                nodeId, EditorNodeGraph::kFrequencyResponseInputSocketId);
            const RenderGraphLink* strength = findInputLink(
                nodeId,
                EditorNodeGraph::ParameterInputSocketId(
                    EditorNodeGraph::kStrengthParameterId));
            HashCombine(fingerprint, spectrum
                ? fingerprintFrequency(spectrum->fromNodeId, spectrum->fromSocketId) : 0);
            HashCombine(fingerprint, response
                ? fingerprintResponse(response->fromNodeId) : 0);
            HashCombine(fingerprint, strength
                ? fingerprintScalar(strength->fromNodeId, strength->fromSocketId)
                : HashValue(node.applyFrequencyResponseSettings.strength));
        } else if (node.kind == RenderGraphNodeKind::CombineSpectra) {
            const RenderGraphLink* a = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputASocketId);
            const RenderGraphLink* b = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputBSocketId);
            HashCombine(fingerprint, a
                ? fingerprintFrequency(a->fromNodeId, a->fromSocketId) : 0);
            HashCombine(fingerprint, b
                ? fingerprintFrequency(b->fromNodeId, b->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(
                static_cast<int>(node.combineSpectraSettings.mode)));
        } else if (node.kind == RenderGraphNodeKind::SpectrumSeparate) {
            const RenderGraphLink* spectrum = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, spectrum
                ? fingerprintFrequency(spectrum->fromNodeId, spectrum->fromSocketId) : 0);
        } else if (node.kind == RenderGraphNodeKind::SpectrumRecombine) {
            const RenderGraphLink* magnitude = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumMagnitudeInputSocketId);
            const RenderGraphLink* phase = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumPhaseInputSocketId);
            HashCombine(fingerprint, magnitude
                ? fingerprintFrequency(magnitude->fromNodeId, magnitude->fromSocketId) : 0);
            HashCombine(fingerprint, phase
                ? fingerprintFrequency(phase->fromNodeId, phase->fromSocketId) : 0);
        } else {
            fingerprint = 0;
        }
        fingerprintingFrequency.erase(key);
        frequencyFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    evalResponse = [&](int nodeId, RenderFrequencyResponseSettings& response) -> bool {
        if (const auto cached = responseCache.find(nodeId); cached != responseCache.end()) {
            response = cached->second;
            return true;
        }
        if (!visitingResponses.insert(nodeId).second) return false;
        const auto finish = [&](bool ok) {
            visitingResponses.erase(nodeId);
            return ok;
        };
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr ||
            nodeIt->second->kind != RenderGraphNodeKind::FrequencyResponse) return finish(false);
        response = nodeIt->second->frequencyResponseSettings;
        const auto applyScalar = [&](const char* parameterId, float& destination) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
            if (input == nullptr) return true;
            double value = destination;
            if (!evalScalar(input->fromNodeId, input->fromSocketId, value)) return false;
            destination = static_cast<float>(value);
            return true;
        };
        if (!applyScalar(EditorNodeGraph::kLowCutoffParameterId, response.lowCutoff) ||
            !applyScalar(EditorNodeGraph::kHighCutoffParameterId, response.highCutoff) ||
            !applyScalar(EditorNodeGraph::kTransitionWidthParameterId, response.transitionWidth) ||
            !applyScalar(EditorNodeGraph::kButterworthOrderParameterId, response.butterworthOrder)) {
            return finish(false);
        }
        for (std::size_t notchIndex = 0;
             notchIndex < response.notches.size();
             ++notchIndex) {
            const auto applyNotchScalar = [&](const char* field, float& target) {
                const std::string parameterId =
                    EditorNodeGraph::FrequencyNotchParameterId(
                        response.notches[notchIndex].id, field);
                const RenderGraphLink* input = findInputLink(
                    nodeId,
                    EditorNodeGraph::ParameterInputSocketId(parameterId));
                if (input == nullptr) return true;
                double value = target;
                if (!evalScalar(
                        input->fromNodeId, input->fromSocketId, value)) return false;
                target = static_cast<float>(value);
                return true;
            };
            RenderFrequencyNotch& notch = response.notches[notchIndex];
            if (!applyNotchScalar("frequency", notch.frequency) ||
                !applyNotchScalar("direction", notch.directionDegrees) ||
                !applyNotchScalar("width", notch.width)) {
                return finish(false);
            }
        }
        CanonicalizeFrequencyResponseSettings(response);
        responseCache[nodeId] = response;
        return finish(true);
    };

    const auto resolveChannelRole = [&](int nodeId, const std::string& socketId) -> std::string {
        int currentNodeId = nodeId;
        std::string currentSocketId = socketId;
        std::unordered_set<std::string> visited;
        visited.reserve(nodes.size());

        while (visited.insert(
                MakeNodeSocketKey(
                    currentNodeId,
                    currentSocketId)).second) {
            const auto nodeIt = nodes.find(currentNodeId);
            if (nodeIt == nodes.end() || nodeIt->second == nullptr) {
                break;
            }
            const RenderGraphNode& node = *nodeIt->second;
            if (node.kind == RenderGraphNodeKind::ChannelSplit) {
                return currentSocketId;
            }

            const RenderGraphLink* input = nullptr;
            switch (node.kind) {
                case RenderGraphNodeKind::FrequencyFilter:
                case RenderGraphNodeKind::FrequencyFft:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kChannelInputSocketId);
                    break;
                case RenderGraphNodeKind::FrequencyIfft:
                case RenderGraphNodeKind::SpectrumView:
                case RenderGraphNodeKind::SpectrumSeparate:
                case RenderGraphNodeKind::SpectrumAnalyzer:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumInputSocketId);
                    break;
                case RenderGraphNodeKind::ApplyFrequencyResponse:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumInputSocketId);
                    break;
                case RenderGraphNodeKind::CombineSpectra:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumInputASocketId);
                    if (!input) {
                        input = findInputLink(
                            node.nodeId,
                            EditorNodeGraph::kSpectrumInputBSocketId);
                    }
                    break;
                case RenderGraphNodeKind::SpectrumRecombine:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumMagnitudeInputSocketId);
                    if (!input) {
                        input = findInputLink(
                            node.nodeId,
                            EditorNodeGraph::kSpectrumPhaseInputSocketId);
                    }
                    break;
                default:
                    break;
            }
            if (!input) {
                break;
            }
            currentNodeId = input->fromNodeId;
            currentSocketId = input->fromSocketId;
        }
        return currentSocketId.empty()
            ? std::string("channel")
            : currentSocketId;
    };

    evalFrequency = [&](int nodeId, const std::string& socketId) -> RenderFrequencyResource {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = frequencyCache.find(key); cached != frequencyCache.end()) {
            ++m_LastGraphExecutionStats.frequencyCacheHits;
            m_Width = cached->second.sourceWidth;
            m_Height = cached->second.sourceHeight;
            return cached->second;
        }
        if (!visitingFrequency.insert(key).second) return {};
        const auto finish = [&](RenderFrequencyResource resource) {
            visitingFrequency.erase(key);
            return resource;
        };
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr) return finish({});
        const RenderGraphNode& node = *nodeIt->second;
        const std::size_t fingerprint = fingerprintFrequency(nodeId, socketId);
        if (fingerprint == 0) return finish({});
        if (const auto persistent = m_GraphFrequencyCache.find(key);
            persistent != m_GraphFrequencyCache.end() &&
            persistent->second.fingerprint == fingerprint &&
            persistent->second.resource.valid &&
            persistent->second.resource.texture != 0) {
            persistent->second.lastUseSerial = ++m_GraphResourceUseSerial;
            ++m_LastGraphExecutionStats.frequencyCacheHits;
            frequencyCache[key] = persistent->second.resource;
            m_Width = persistent->second.resource.sourceWidth;
            m_Height = persistent->second.resource.sourceHeight;
            return finish(persistent->second.resource);
        }
        ++m_LastGraphExecutionStats.frequencyCacheMisses;

        RenderFrequencyResource result;
        if (node.kind == RenderGraphNodeKind::FrequencyFft) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::kChannelInputSocketId);
            const unsigned int channel = input
                ? evalMask(input->fromNodeId, input->fromSocketId) : 0;
            const int sourceWidth = m_Width;
            const int sourceHeight = m_Height;
            if (channel != 0) {
                result = RenderFourierTransform(
                    channel, sourceWidth, sourceHeight,
                    node.frequencyFftSettings.edgePolicy,
                    input
                        ? resolveChannelRole(
                            input->fromNodeId, input->fromSocketId)
                        : std::string("channel"));
            }
        } else if (node.kind == RenderGraphNodeKind::ApplyFrequencyResponse) {
            const RenderGraphLink* spectrumLink = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            const RenderGraphLink* responseLink = findInputLink(
                nodeId, EditorNodeGraph::kFrequencyResponseInputSocketId);
            RenderFrequencyResponseSettings response;
            double strength = node.applyFrequencyResponseSettings.strength;
            const RenderGraphLink* strengthLink = findInputLink(
                nodeId,
                EditorNodeGraph::ParameterInputSocketId(
                    EditorNodeGraph::kStrengthParameterId));
            const bool strengthReady = strengthLink == nullptr ||
                evalScalar(strengthLink->fromNodeId, strengthLink->fromSocketId, strength);
            if (spectrumLink != nullptr && responseLink != nullptr &&
                strengthReady && evalResponse(responseLink->fromNodeId, response)) {
                const double canonicalStrength =
                    std::isfinite(strength)
                        ? std::clamp(strength, 0.0, 1.0)
                        : 1.0;
                result = RenderApplyFrequencyResponse(
                    evalFrequency(spectrumLink->fromNodeId, spectrumLink->fromSocketId),
                    response,
                    static_cast<float>(canonicalStrength));
            }
        } else if (node.kind == RenderGraphNodeKind::CombineSpectra) {
            const RenderGraphLink* a = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputASocketId);
            const RenderGraphLink* b = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputBSocketId);
            if (a != nullptr && b != nullptr) {
                const RenderFrequencyResource spectrumA =
                    evalFrequency(a->fromNodeId, a->fromSocketId);
                const RenderFrequencyResource spectrumB =
                    evalFrequency(b->fromNodeId, b->fromSocketId);
                result = RenderCombineSpectra(
                    spectrumA, spectrumB, node.combineSpectraSettings.mode);
                if (spectrumA.valid && spectrumB.valid && !result.valid) {
                    m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
                    m_LastGraphExecutionStats.lastSpecializedFailure =
                        "Combine Spectra requires matching source role, source and padded "
                        "extents, padding origin, edge policy, precision, normalization, "
                        "and coordinate convention. Rebuild both inputs from compatible Channels.";
                }
            }
        } else if (node.kind == RenderGraphNodeKind::SpectrumSeparate) {
            const RenderGraphLink* spectrum = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            if (spectrum != nullptr) {
                const RenderFrequencyResourceKind componentKind =
                    socketId == EditorNodeGraph::kSpectrumMagnitudeOutputSocketId
                        ? RenderFrequencyResourceKind::Magnitude
                        : RenderFrequencyResourceKind::Phase;
                result = RenderSpectrumComponent(
                    evalFrequency(spectrum->fromNodeId, spectrum->fromSocketId),
                    componentKind);
            }
        } else if (node.kind == RenderGraphNodeKind::SpectrumRecombine) {
            const RenderGraphLink* magnitude = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumMagnitudeInputSocketId);
            const RenderGraphLink* phase = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumPhaseInputSocketId);
            if (magnitude != nullptr && phase != nullptr) {
                const RenderFrequencyResource magnitudeResource =
                    evalFrequency(magnitude->fromNodeId, magnitude->fromSocketId);
                const RenderFrequencyResource phaseResource =
                    evalFrequency(phase->fromNodeId, phase->fromSocketId);
                result = RenderRecombineSpectrum(
                    magnitudeResource, phaseResource);
                if (magnitudeResource.valid && phaseResource.valid && !result.valid) {
                    m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
                    m_LastGraphExecutionStats.lastSpecializedFailure =
                        "Recombine Spectrum requires Magnitude and Phase from compatible "
                        "transform metadata. Use outputs from matching Separate Spectrum chains.";
                }
            }
        }

        if (result.valid) {
            if (StoreFrequencyCacheEntry(
                    key, result, fingerprint, true)) {
                try {
                    frequencyCache[key] = result;
                } catch (const std::bad_alloc&) {
                    frequencyCache.erase(key);
                } catch (const std::length_error&) {
                    frequencyCache.erase(key);
                }
                m_Width = result.sourceWidth;
                m_Height = result.sourceHeight;
            } else {
                if (result.texture != 0) {
                    glDeleteTextures(1, &result.texture);
                }
                result = {};
            }
        } else {
            const auto stale = m_GraphFrequencyCache.find(key);
            if (stale != m_GraphFrequencyCache.end()) {
                DeleteFrequencyCacheEntry(stale->second);
                m_GraphFrequencyCache.erase(stale);
            }
            if (m_LastGraphExecutionStats.lastSpecializedFailureNodeId != nodeId) {
                m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
                m_LastGraphExecutionStats.lastSpecializedFailure =
                    "The typed frequency stage could not satisfy its spectrum contract.";
            }
        }
        return finish(result);
    };

    evalMask = [&](int nodeId, const std::string& socketId) -> unsigned int {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = maskCache.find(key); cached != maskCache.end()) {
            if (const auto extent = localTextureExtents.find(key);
                extent != localTextureExtents.end()) {
                m_Width = extent->second.first;
                m_Height = extent->second.second;
            }
            return cached->second;
        }
        if (visitingMasks.count(key)) {
            return 0;
        }
        visitingMasks.insert(key);
        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawDevelopment ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::HdrMerge ||
            node.kind == RenderGraphNodeKind::Mfsr ||
            node.kind == RenderGraphNodeKind::RawProjectSourceSet ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            node.kind == RenderGraphNodeKind::TechnicalImage ||
            node.kind == RenderGraphNodeKind::Reformat ||
            (node.kind == RenderGraphNodeKind::DataMath && !IsScalarRenderSocket(executionContext, nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::FrequencyFft ||
            node.kind == RenderGraphNodeKind::SpectrumView ||
            node.kind == RenderGraphNodeKind::SpectrumMath ||
            (node.kind == RenderGraphNodeKind::MagnitudePhase && socketId != EditorNodeGraph::kMaskOutputSocketId) ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            unsigned int imgTex = evalImage(nodeId, socketId);
            visitingMasks.erase(key);
            return imgTex;
        }
        const std::size_t fingerprint = fingerprintMask(nodeId, socketId);
        if (const auto cached = m_GraphMaskCache.find(key);
            cached != m_GraphMaskCache.end() &&
            cached->second.fingerprint == fingerprint &&
            cached->second.texture != 0) {
            ++m_LastGraphExecutionStats.maskCacheHits;
            TouchGraphCacheEntry(cached->second);
            if (cached->second.width > 0 && cached->second.height > 0) {
                m_Width = cached->second.width;
                m_Height = cached->second.height;
            }
            maskCache[key] = cached->second.texture;
            localTextureExtents[key] = { m_Width, m_Height };
            visitingMasks.erase(key);
            return cached->second.texture;
        }
        ++m_LastGraphExecutionStats.maskCacheMisses;

        unsigned int result = 0;
        bool resultOwned = false;
        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            RenderMaskSource mask;
            mask.nodeId = node.nodeId;
            mask.kind = node.maskKind;
            mask.settings = node.maskSettings;
            result = GenerateMaskTexture(mask);
            resultOwned = result != 0;
        } else if (node.kind ==
                   RenderGraphNodeKind::ConstantChannel) {
            const RenderGraphLink* extentInput =
                findInputLink(
                    node.nodeId,
                    EditorNodeGraph::kMatchExtentInputSocketId);
            const unsigned int extentTexture = extentInput
                ? evalMask(
                    extentInput->fromNodeId,
                    extentInput->fromSocketId)
                : 0;
            const int extentWidth = m_Width;
            const int extentHeight = m_Height;
            if (extentTexture != 0 &&
                extentWidth > 0 &&
                extentHeight > 0) {
                m_Width = extentWidth;
                m_Height = extentHeight;
                result = createTarget();
                const float value =
                    std::isfinite(node.constantChannelValue)
                        ? node.constantChannelValue
                        : 1.0f;
                renderToTexture(result, [&](unsigned int) {
                    GLfloat previousClearColor[4];
                    glGetFloatv(
                        GL_COLOR_CLEAR_VALUE,
                        previousClearColor);
                    glClearColor(value, value, value, value);
                    glClear(GL_COLOR_BUFFER_BIT);
                    glClearColor(
                        previousClearColor[0],
                        previousClearColor[1],
                        previousClearColor[2],
                        previousClearColor[3]);
                });
                resultOwned = result != 0;
            } else {
                m_LastGraphExecutionStats
                    .lastSpecializedFailureNodeId = node.nodeId;
                m_LastGraphExecutionStats
                    .lastSpecializedFailure =
                    "Constant Channel requires a resolvable Match Extent Channel input.";
            }
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            const unsigned int maskA = inputA ? evalMask(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const int referenceWidth = m_Width;
            const int referenceHeight = m_Height;
            const unsigned int maskB = inputB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (maskA && maskB) {
                m_Width = referenceWidth;
                m_Height = referenceHeight;
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return RenderMaskCombine(maskA, maskB, node.maskCombineMode, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::CustomMask) {
            result = GenerateCustomMaskTexture(node.customMask);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            const unsigned int inputMask = input ? evalMask(input->fromNodeId, input->fromSocketId) : 0;
            if (inputMask) {
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return RenderMaskUtility(inputMask, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return RenderImageToMask(inputImage, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                int channelIdx = 0;
                if (socketId == "g") channelIdx = 1;
                else if (socketId == "b") channelIdx = 2;
                else if (socketId == "a") channelIdx = 3;
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return RenderChannelSplit(inputImage, channelIdx, fbo);
                });
                resultOwned = result != 0;
            } else {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    GLfloat previousClearColor[4];
                    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
                    if (socketId == "a") {
                        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
                    } else {
                        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    }
                    glClear(GL_COLOR_BUFFER_BIT);
                    glClearColor(
                        previousClearColor[0],
                        previousClearColor[1],
                        previousClearColor[2],
                        previousClearColor[3]);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::FrequencyFilter) {
            const RenderGraphLink* channelLink = findInputLink(
                node.nodeId, EditorNodeGraph::kChannelInputSocketId);
            const unsigned int channel = channelLink
                ? evalMask(channelLink->fromNodeId, channelLink->fromSocketId) : 0;
            const int sourceWidth = m_Width;
            const int sourceHeight = m_Height;
            RenderFrequencyResponseSettings response =
                node.frequencyFilterSettings.localResponse;
            const RenderGraphLink* responseLink = findInputLink(
                node.nodeId, EditorNodeGraph::kFrequencyResponseInputSocketId);
            const bool responseReady = responseLink == nullptr ||
                evalResponse(responseLink->fromNodeId, response);
            double strength = node.frequencyFilterSettings.strength;
            const RenderGraphLink* strengthLink = findInputLink(
                node.nodeId,
                EditorNodeGraph::ParameterInputSocketId(
                    EditorNodeGraph::kStrengthParameterId));
            const bool strengthReady = strengthLink == nullptr ||
                evalScalar(strengthLink->fromNodeId, strengthLink->fromSocketId, strength);
            strength = std::isfinite(strength)
                ? std::clamp(strength, 0.0, 1.0)
                : 1.0;
            if (responseReady) {
                CanonicalizeFrequencyResponseSettings(response);
            }
            if (channel != 0 && responseReady && strengthReady &&
                (response.mode == RenderFrequencyFilterMode::AllPass || strength == 0.0)) {
                // The default blank node is a bit-exact graph bypass. No FFT
                // resource is allocated or cached for this path.
                result = channel;
                resultOwned = false;
            } else if (channel != 0 && responseReady && strengthReady) {
                RenderFrequencyResource spectrum = RenderFourierTransform(
                    channel,
                    sourceWidth,
                    sourceHeight,
                    node.frequencyFilterSettings.edgePolicy,
                    channelLink
                        ? resolveChannelRole(
                            channelLink->fromNodeId,
                            channelLink->fromSocketId)
                        : std::string("channel"));
                RenderFrequencyResource filtered = RenderApplyFrequencyResponse(
                    spectrum, response, static_cast<float>(strength));
                const GraphNodeRenderResult inverse = RenderInverseFourierTransform(filtered);
                result = inverse.texture;
                resultOwned = inverse.owned;
                if (spectrum.texture != 0) glDeleteTextures(1, &spectrum.texture);
                if (filtered.texture != 0) glDeleteTextures(1, &filtered.texture);
                m_Width = sourceWidth;
                m_Height = sourceHeight;
            }
        } else if (node.kind == RenderGraphNodeKind::FrequencyIfft) {
            const RenderGraphLink* spectrumLink = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            if (spectrumLink != nullptr) {
                const RenderFrequencyResource spectrum = evalFrequency(
                    spectrumLink->fromNodeId, spectrumLink->fromSocketId);
                const GraphNodeRenderResult inverse = RenderInverseFourierTransform(spectrum);
                result = inverse.texture;
                resultOwned = inverse.owned;
                m_Width = spectrum.sourceWidth;
                m_Height = spectrum.sourceHeight;
                if (spectrum.valid && !spectrum.hermitian && result == 0) {
                    m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
                    m_LastGraphExecutionStats.lastSpecializedFailure =
                        "Inverse Fourier Transform rejected a non-Hermitian spectrum; "
                        "it cannot produce a real Channel.";
                }
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const GraphNodeRenderResult dataMathResult =
                RenderDataMathGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = dataMathResult.texture;
            resultOwned = dataMathResult.owned;
        } else if (node.kind == RenderGraphNodeKind::FrequencyMask ||
                   node.kind == RenderGraphNodeKind::MagnitudePhase) {
            const GraphNodeRenderResult frequencyResult =
                RenderFrequencyGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = frequencyResult.texture;
            resultOwned = frequencyResult.owned;
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask ||
                   node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const GraphNodeRenderResult rawDetailResult =
                RenderRawDetailGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = rawDetailResult.texture;
            resultOwned = rawDetailResult.owned;
        }

        if (result) {
            if (resultOwned &&
                !StoreGraphCacheEntry(
                    m_GraphMaskCache,
                    key,
                    result,
                    fingerprint,
                    true)) {
                glDeleteTextures(1, &result);
                result = 0;
            } else if (!resultOwned) {
                // Borrowed pass-through textures are valid only while their
                // owning upstream cache entry survives. Persisting the alias
                // would let budget pruning delete the owner independently.
                ReleaseGraphCacheEntry(m_GraphMaskCache, key);
            }
            if (result != 0) {
                try {
                    maskCache[key] = result;
                    localTextureExtents[key] = { m_Width, m_Height };
                } catch (const std::bad_alloc&) {
                    maskCache.erase(key);
                    localTextureExtents.erase(key);
                } catch (const std::length_error&) {
                    maskCache.erase(key);
                    localTextureExtents.erase(key);
                }
            }
        } else {
            ReleaseGraphCacheEntry(m_GraphMaskCache, key);
        }
        visitingMasks.erase(key);
        return result;
    };

    evalSpectrumAnalysis = [&](int nodeId, RenderSpectrumAnalysis& analysis) -> bool {
        if (const auto local = spectrumAnalysisCache.find(nodeId);
            local != spectrumAnalysisCache.end()) {
            analysis = local->second;
            return analysis.valid;
        }
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr ||
            nodeIt->second->kind != RenderGraphNodeKind::SpectrumAnalyzer) return false;
        const RenderGraphNode& node = *nodeIt->second;
        const RenderGraphLink* spectrumLink = findInputLink(
            nodeId, EditorNodeGraph::kSpectrumInputSocketId);
        if (spectrumLink == nullptr) return false;

        RenderSpectrumAnalyzerSettings settings = node.spectrumAnalyzerSettings;
        const auto applyBandInput = [&](const char* parameterId, float& destination) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
            if (input == nullptr) return true;
            double value = destination;
            if (!evalScalar(input->fromNodeId, input->fromSocketId, value)) return false;
            destination = static_cast<float>(value);
            return true;
        };
        if (!applyBandInput(EditorNodeGraph::kAnalyzerLowParameterId, settings.innerRadius) ||
            !applyBandInput(EditorNodeGraph::kAnalyzerHighParameterId, settings.outerRadius)) {
            return false;
        }
        if (!std::isfinite(settings.innerRadius)) settings.innerRadius = 0.0f;
        if (!std::isfinite(settings.outerRadius)) settings.outerRadius = 0.5f;
        settings.innerRadius = std::clamp(settings.innerRadius, 0.0f, 0.70710678f);
        settings.outerRadius = std::clamp(settings.outerRadius, 0.0f, 0.70710678f);
        if (settings.innerRadius > settings.outerRadius)
            std::swap(settings.innerRadius, settings.outerRadius);

        std::size_t fingerprint = HashValue(node.definitionId);
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, fingerprintFrequency(
            spectrumLink->fromNodeId, spectrumLink->fromSocketId));
        HashCombine(fingerprint, HashValue(settings.innerRadius));
        HashCombine(fingerprint, HashValue(settings.outerRadius));
        HashCombine(fingerprint, HashValue(settings.excludeDc));
        const std::string persistentKey =
            MakeNodeSocketKey(nodeId, EditorNodeGraph::kRadialPowerOutputSocketId);
        if (const auto persistent = m_GraphFrequencyAnalysisCache.find(persistentKey);
            persistent != m_GraphFrequencyAnalysisCache.end() &&
            persistent->second.fingerprint == fingerprint) {
            analysis = persistent->second;
            spectrumAnalysisCache[nodeId] = analysis;
            return analysis.valid;
        }

        const RenderFrequencyResource spectrum = evalFrequency(
            spectrumLink->fromNodeId, spectrumLink->fromSocketId);
        analysis = AnalyzeSpectrum(spectrum, settings, fingerprint);
        spectrumAnalysisCache[nodeId] = analysis;
        if (analysis.valid) {
            m_GraphFrequencyAnalysisCache[persistentKey] = analysis;
        } else {
            m_GraphFrequencyAnalysisCache.erase(persistentKey);
            m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
            m_LastGraphExecutionStats.lastSpecializedFailure = analysis.error;
        }
        return analysis.valid;
    };

    evalScalar = [&](int nodeId, const std::string& socketId, double& value) -> bool {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto local = scalarCache.find(key); local != scalarCache.end()) {
            value = local->second;
            ++m_LastGraphExecutionStats.reductionCacheHits;
            return true;
        }
        if (!visitingScalars.insert(key).second) {
            m_LastGraphExecutionStats.lastReductionFailureNodeId = nodeId;
            m_LastGraphExecutionStats.lastReductionFailure =
                "Reduction dependency contains a cycle.";
            return false;
        }
        const auto finishFailure = [&](std::string message) {
            m_LastGraphExecutionStats.lastReductionFailureNodeId = nodeId;
            m_LastGraphExecutionStats.lastReductionFailure = std::move(message);
            visitingScalars.erase(key);
            return false;
        };
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr)
            return finishFailure("The connected uniform value has no executable source.");
        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::SpectrumAnalyzer) {
            if (socketId != EditorNodeGraph::kBandPowerOutputSocketId &&
                socketId != EditorNodeGraph::kPeakFrequencyOutputSocketId &&
                socketId != EditorNodeGraph::kPeakDirectionOutputSocketId) {
                return finishFailure("Spectrum Analyzer's radial curve is Data, not a scalar Value.");
            }
            RenderSpectrumAnalysis analysis;
            if (!evalSpectrumAnalysis(nodeId, analysis)) {
                return finishFailure(analysis.error.empty()
                    ? "Spectrum Analyzer could not evaluate its spectrum." : analysis.error);
            }
            if (socketId == EditorNodeGraph::kBandPowerOutputSocketId)
                value = analysis.bandPower;
            else if (socketId == EditorNodeGraph::kPeakFrequencyOutputSocketId)
                value = analysis.peakFrequency;
            else
                value = analysis.peakDirectionDegrees;
            scalarCache[key] = value;
            CachedGraphScalar cached;
            cached.fingerprint = fingerprintScalar(nodeId, socketId);
            cached.value = value;
            cached.lastUseSerial = ++m_GraphResourceUseSerial;
            m_GraphScalarCache[key] = cached;
            visitingScalars.erase(key);
            return true;
        }
        if (node.kind != RenderGraphNodeKind::FieldMean ||
            socketId != EditorNodeGraph::kValueOutputSocketId) {
            return finishFailure(
                "The connected uniform value is not an executable reduction output.");
        }
        const std::size_t fingerprint = fingerprintScalar(nodeId, socketId);
        if (const auto persistent = m_GraphScalarCache.find(key);
            persistent != m_GraphScalarCache.end() &&
            persistent->second.fingerprint == fingerprint) {
            persistent->second.lastUseSerial = ++m_GraphResourceUseSerial;
            value = persistent->second.value;
            scalarCache[key] = value;
            scalarSampleCounts[key] = static_cast<std::size_t>(persistent->second.sampleCount);
            ++m_LastGraphExecutionStats.reductionCacheHits;
            m_LastGraphExecutionStats.reductions.push_back({
                nodeId, value, persistent->second.sampleCount, true, node.definitionId });
            visitingScalars.erase(key);
            return true;
        }
        ++m_LastGraphExecutionStats.reductionCacheMisses;
        const RenderGraphLink* input = findInputLink(
            nodeId, EditorNodeGraph::kReductionFieldInputSocketId);
        if (input == nullptr) {
            m_GraphScalarCache.erase(key);
            return finishFailure("Field Mean requires a connected scalar field.");
        }
        const unsigned int texture = evalMask(input->fromNodeId, input->fromSocketId);
        if (texture == 0 || m_Width <= 0 || m_Height <= 0) {
            m_GraphScalarCache.erase(key);
            return finishFailure("Field Mean could not materialize its scalar-field input.");
        }

        constexpr int kReadbackRows = 64;
        const int maximumRows = std::min(kReadbackRows, m_Height);
        std::size_t sampleCapacity = 0;
        if (!Stack::PixelBuffer::TryComputePixelElementCount(
                m_Width, maximumRows, 1, sampleCapacity)) {
            return finishFailure(
                "Field Mean dimensions exceed CPU readback limits.");
        }
        std::vector<float> samples;
        try {
            samples.resize(sampleCapacity);
        } catch (const std::bad_alloc&) {
            return finishFailure(
                "Field Mean could not allocate its CPU readback.");
        } catch (const std::length_error&) {
            return finishFailure(
                "Field Mean dimensions exceed CPU readback limits.");
        }

        const ScopedFramebufferState savedState(true);
        const Stack::Renderer::GLState::PixelPackState savedPackState;
        savedPackState.ConfigureTightCpuReadback();
        const unsigned int fbo = GLHelpers::CreateFBO(texture);
        if (fbo == 0) {
            savedPackState.Restore();
            savedState.Restore(true);
            return finishFailure("Field Mean could not create a readback target.");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        while (glGetError() != GL_NO_ERROR) {}
        Stack::NodeMath::FieldMeanAccumulator accumulator;
        bool readbackOk = true;
        for (int y = 0; y < m_Height && readbackOk; y += kReadbackRows) {
            const int rows = std::min(kReadbackRows, m_Height - y);
            glReadPixels(0, y, m_Width, rows, GL_RED, GL_FLOAT, samples.data());
            if (glGetError() != GL_NO_ERROR) {
                readbackOk = false;
                break;
            }
            readbackOk = accumulator.Add(
                samples.data(),
                static_cast<std::size_t>(m_Width) * static_cast<std::size_t>(rows));
        }
        savedPackState.Restore();
        savedState.Restore(true);
        glDeleteFramebuffers(1, &fbo);
        if (!readbackOk) {
            m_GraphScalarCache.erase(key);
            const Stack::NodeMath::FieldMeanResult failed = accumulator.Finish();
            return finishFailure(failed.error.empty()
                ? "Field Mean texture readback failed." : failed.error);
        }
        const Stack::NodeMath::FieldMeanResult mean = accumulator.Finish();
        if (!mean.valid) {
            m_GraphScalarCache.erase(key);
            return finishFailure(mean.error);
        }

        value = mean.value;
        scalarCache[key] = value;
        scalarSampleCounts[key] = mean.sampleCount;
        CachedGraphScalar cached;
        cached.fingerprint = fingerprint;
        cached.value = mean.value;
        cached.sampleCount = static_cast<std::uint64_t>(mean.sampleCount);
        cached.lastUseSerial = ++m_GraphResourceUseSerial;
        m_GraphScalarCache[key] = cached;
        ++m_LastGraphExecutionStats.reductionPasses;
        m_LastGraphExecutionStats.reductions.push_back({
            nodeId, value, cached.sampleCount, false, node.definitionId });
        visitingScalars.erase(key);
        return true;
    };

    evalImage = [&](int nodeId, const std::string& socketId) -> unsigned int {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = imageCache.find(key); cached != imageCache.end()) {
            if (const auto extent = localTextureExtents.find(key);
                extent != localTextureExtents.end()) {
                m_Width = extent->second.first;
                m_Height = extent->second.second;
            }
            return cached->second;
        }
        if (visitingImages.count(key)) {
            return 0;
        }
        visitingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            node.kind == RenderGraphNodeKind::ConstantChannel ||
            node.kind == RenderGraphNodeKind::FrequencyMask ||
            (node.kind == RenderGraphNodeKind::MagnitudePhase && socketId == EditorNodeGraph::kMaskOutputSocketId) ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            unsigned int maskTex = evalMask(nodeId, socketId);
            visitingImages.erase(key);
            return maskTex;
        }
        const std::size_t fingerprint = fingerprintImage(nodeId, socketId);
        const bool rawDevelopStageSocket =
            node.kind == RenderGraphNodeKind::RawDevelop &&
            (socketId == "__rawDevelopBase" ||
             socketId == EditorNodeGraph::kPreFinishImageOutputSocketId);
        const bool rawBorrowedResultNeedsOwnedCache =
            node.kind == RenderGraphNodeKind::RawDevelopment ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop;
        const bool rawDevelopmentSideEffectsActive =
            node.kind == RenderGraphNodeKind::RawDevelopment &&
            (graph.rawWorkspaceLocalRangeTargetSampleRequested ||
             (!graph.rawWorkspaceLocalRangeOverlayMode.empty() &&
              graph.rawWorkspaceLocalRangeOverlayMode != "none"));
        const bool passThroughOutput =
            node.kind == RenderGraphNodeKind::Output &&
            findInputLink(node.nodeId, "imageIn") != nullptr &&
            !IsScalarRenderSocket(
                executionContext,
                findInputLink(node.nodeId, "imageIn")->fromNodeId,
                findInputLink(node.nodeId, "imageIn")->fromSocketId);
        if (!rawDevelopStageSocket &&
            !rawDevelopmentSideEffectsActive &&
            !passThroughOutput) {
            if (const auto cached = m_GraphImageCache.find(key);
                cached != m_GraphImageCache.end() &&
                cached->second.fingerprint == fingerprint &&
                cached->second.texture != 0) {
                ++m_LastGraphExecutionStats.imageCacheHits;
                TouchGraphCacheEntry(cached->second);
                if (cached->second.width > 0 && cached->second.height > 0) {
                    m_Width = cached->second.width;
                    m_Height = cached->second.height;
                }
                imageCache[key] = cached->second.texture;
                localTextureExtents[key] = { m_Width, m_Height };
                m_LastGraphImageCacheHits.insert(key);
                visitingImages.erase(key);
                return cached->second.texture;
            }
        }
        ++m_LastGraphExecutionStats.imageCacheMisses;

        unsigned int result = 0;
        bool resultOwned = false;
        bool pointwiseFused = false;
        const bool hasDynamicExposureInput =
            node.kind == RenderGraphNodeKind::TechnicalImage &&
            node.technicalImageOperation == Stack::NodeMath::TechnicalImageOperation::Exposure &&
            findInputLink(node.nodeId, EditorNodeGraph::kExposureValueInputSocketId) != nullptr;
        if ((node.kind == RenderGraphNodeKind::DataMath ||
             node.kind == RenderGraphNodeKind::TechnicalImage) &&
            !hasDynamicExposureInput &&
            pointwiseFusionDisabledNodes.find(node.nodeId) == pointwiseFusionDisabledNodes.end()) {
            const PointwiseFusionPlan fusionPlan = BuildPointwiseFusionPlan(
                executionContext,
                node.nodeId,
                socketId,
                pointwiseFusionDisabledNodes);
            if (!fusionPlan.failure.empty()) {
                m_LastGraphExecutionStats.lastPointwiseFailure = fusionPlan.failure;
                m_LastGraphExecutionStats.lastPointwiseFailureNodeIds = fusionPlan.failureNodeIds;
                ++m_LastGraphExecutionStats.pointwiseFallbacks;
                pointwiseFusionDisabledNodes.insert(
                    fusionPlan.failureNodeIds.begin(), fusionPlan.failureNodeIds.end());
            } else if (fusionPlan.valid) {
                const unsigned int pointwiseInput = evalImage(
                    fusionPlan.inputNodeId,
                    fusionPlan.inputSocketId);
                const GraphNodeRenderResult fused = RenderPointwiseFusionPlan(
                    fusionPlan,
                    pointwiseInput,
                    graph.executionInspectionEnabled);
                if (fused.texture != 0) {
                    result = fused.texture;
                    resultOwned = fused.owned;
                    pointwiseFused = true;

                    std::unordered_set<int> virtualNodeIds(
                        fusionPlan.authoredNodeIds.begin(),
                        fusionPlan.authoredNodeIds.end());
                    virtualNodeIds.erase(node.nodeId);
                    for (auto cacheIt = m_GraphImageCache.begin(); cacheIt != m_GraphImageCache.end(); ) {
                        if (virtualNodeIds.find(ExtractNodeIdFromCacheKey(cacheIt->first)) != virtualNodeIds.end()) {
                            DeleteGraphCacheEntry(cacheIt->second);
                            cacheIt = m_GraphImageCache.erase(cacheIt);
                        } else {
                            ++cacheIt;
                        }
                    }
                } else {
                    ++m_LastGraphExecutionStats.pointwiseFallbacks;
                    pointwiseFusionDisabledNodes.insert(
                        fusionPlan.authoredNodeIds.begin(), fusionPlan.authoredNodeIds.end());
                }
            }
        }
        if (pointwiseFused) {
            // The semantic chain was emitted as one physical pass above.
        } else if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                m_Width = node.image.width;
                m_Height = node.image.height;
                result = GLHelpers::CreateTextureFromPixels(
                    node.image.pixels.data(),
                    node.image.width,
                    node.image.height,
                    node.image.channels);
                resultOwned = result != 0;
            } else {
                m_Width = m_BaseCanvasWidth;
                m_Height = m_BaseCanvasHeight;
                result = m_SourceTexture;
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            const GraphNodeRenderResult rawDevelopmentResult =
                RenderRawDevelopmentGraphNode(node, fingerprint);
            result = rawDevelopmentResult.texture;
            resultOwned = rawDevelopmentResult.owned;
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            result = 0;
            m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
            m_LastGraphExecutionStats.lastSpecializedFailure =
                "Legacy RAW Neural Denoise is retired and passes RAW data through unchanged.";
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const std::string rawBaseKey = std::to_string(node.nodeId) + ":__rawDecodeBase";
            const std::size_t rawBaseFingerprint = fingerprintImage(node.nodeId, "__rawDecodeBase");
            const SharedRawBaseStageResult rawBaseStage =
                RenderSharedRawBaseStage(executionContext, node, node.rawDecode.settings, rawBaseKey, rawBaseFingerprint, fingerprintImage);
            result = rawBaseStage.texture;
            resultOwned = false;
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const GraphNodeRenderResult rawDevelopResult = RenderRawDevelopGraphNode(
                executionContext,
                node,
                socketId,
                fingerprint,
                imageCache,
                evalImage,
                evalMask,
                fingerprintImage);
            result = rawDevelopResult.texture;
            resultOwned = rawDevelopResult.owned;
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const GraphNodeRenderResult rawDetailResult =
                RenderRawDetailGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = rawDetailResult.texture;
            resultOwned = rawDetailResult.owned;
        } else if (node.kind == RenderGraphNodeKind::HdrMerge) {
            const RenderGraphLink* input1 = findInputLink(node.nodeId, "image1");
            const RenderGraphLink* input2 = findInputLink(node.nodeId, "image2");
            const RenderGraphLink* input3 = findInputLink(node.nodeId, "image3");
            const unsigned int texture1 = input1 ? evalImage(input1->fromNodeId, input1->fromSocketId) : 0;
            const int referenceWidth = m_Width;
            const int referenceHeight = m_Height;
            const unsigned int texture2 = input2 ? evalImage(input2->fromNodeId, input2->fromSocketId) : 0;
            const unsigned int texture3 = input3 ? evalImage(input3->fromNodeId, input3->fromSocketId) : 0;
            const bool hasGap = input3 != nullptr && input2 == nullptr;
            const bool hasRequiredInputs = texture1 != 0 &&
                texture2 != 0 &&
                (input3 == nullptr || texture3 != 0);
            if (!hasGap && hasRequiredInputs) {
                m_Width = referenceWidth;
                m_Height = referenceHeight;
                std::array<bool, 3> activeInputs { input1 != nullptr, input2 != nullptr, input3 != nullptr };
                std::array<HdrMergeInputContext, 3> inputContexts {};
                if (input1) inputContexts[0] = ResolveHdrMergeInputContext(executionContext, input1->fromNodeId);
                if (input2) inputContexts[1] = ResolveHdrMergeInputContext(executionContext, input2->fromNodeId);
                if (input3) inputContexts[2] = ResolveHdrMergeInputContext(executionContext, input3->fromNodeId);
                const HdrMergeResolvedSettings resolved = ResolveHdrMergeSettings(node.hdrMerge.settings, inputContexts, activeInputs);
                result = createTarget();
                bool mergeRendered = false;
                renderToTexture(result, [&](unsigned int fbo) {
                    mergeRendered = RenderHdrMerge(
                        texture1,
                        texture2,
                        texture3,
                        input2 != nullptr,
                        input3 != nullptr,
                        node.hdrMerge.settings,
                        resolved,
                        fbo);
                });
                if (!mergeRendered && result != 0) {
                    glDeleteTextures(1, &result);
                    result = 0;
                }
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::Mfsr) {
            const RenderGraphLink* reference = findInputLink(node.nodeId, EditorNodeGraph::kMfsrReferenceInputSocketId);
            if (reference) {
                result = evalImage(reference->fromNodeId, reference->fromSocketId);
                resultOwned = false;
            }
        } else if (node.kind == RenderGraphNodeKind::RawProjectSourceSet) {
            if (node.rawProjectSourceSet.resultAvailable &&
                node.rawDevelopment.embeddedRawData &&
                !node.rawProjectSourceSet.quarantined) {
                RenderGraphNode developedNode = node;
                developedNode.kind =
                    RenderGraphNodeKind::RawDevelopment;
                const GraphNodeRenderResult developedResult =
                    RenderRawDevelopmentGraphNode(
                        developedNode, fingerprint);
                result = developedResult.texture;
                resultOwned = developedResult.owned;
            } else {
                // Never substitute the reference frame for a missing, stale,
                // or failed fused result.
                result = 0;
                resultOwned = false;
            }
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const GraphNodeRenderResult lutResult = RenderLutGraphNode(executionContext, node, evalImage, evalMask);
            result = lutResult.texture;
            resultOwned = lutResult.owned;
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            result = GenerateImageTexture(node);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const GraphNodeRenderResult layerResult = RenderLayerGraphNode(executionContext, node, evalImage, evalMask);
            result = layerResult.texture;
            resultOwned = layerResult.owned;
        } else if (node.kind == RenderGraphNodeKind::TechnicalImage) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            const unsigned int inputTexture = input
                ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            double exposureValue = static_cast<double>(node.technicalExposureValue);
            const RenderGraphLink* exposureInput =
                node.technicalImageOperation == Stack::NodeMath::TechnicalImageOperation::Exposure
                    ? findInputLink(node.nodeId, EditorNodeGraph::kExposureValueInputSocketId)
                    : nullptr;
            const bool exposureReady = exposureInput == nullptr ||
                evalScalar(exposureInput->fromNodeId, exposureInput->fromSocketId, exposureValue);
            if (inputTexture && exposureReady) {
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return RenderTechnicalImage(
                        inputTexture,
                        node.technicalImageOperation,
                        static_cast<float>(exposureValue),
                        fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::Reformat) {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kImageInputSocketId);
            const unsigned int inputTexture = input
                ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            const int inputWidth = m_Width;
            const int inputHeight = m_Height;
            if (inputTexture &&
                Stack::NodeMath::ValidateReformatSettings(node.reformatSettings).empty()) {
                m_Width = node.reformatSettings.width;
                m_Height = node.reformatSettings.height;
                result = createTarget();
                bool reformatRendered = false;
                const bool targetRendered = result != 0 && renderToTexture(result, [&](unsigned int fbo) {
                    reformatRendered = RenderReformat(
                        inputTexture, inputWidth, inputHeight,
                        node.reformatSettings, fbo);
                });
                if (!targetRendered || !reformatRendered) {
                    if (result != 0) glDeleteTextures(1, &result);
                    result = 0;
                    m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
                    m_LastGraphExecutionStats.lastSpecializedFailure =
                        "Reformat could not execute its declared sampling stage.";
                }
                resultOwned = result != 0;
            } else {
                m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
                m_LastGraphExecutionStats.lastSpecializedFailure = inputTexture == 0
                    ? "Reformat could not materialize its required image input."
                    : "Reformat settings do not satisfy the declared geometry contract.";
            }
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const unsigned int textureA = inputA ? evalImage(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const int referenceWidth = m_Width;
            const int referenceHeight = m_Height;
            const unsigned int textureB = inputB ? evalImage(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (textureA && textureB) {
                const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
                m_Width = referenceWidth;
                m_Height = referenceHeight;
                const unsigned int factorTexture = factorLink ? evalMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0;
                m_Width = referenceWidth;
                m_Height = referenceHeight;
                if (!factorLink || factorTexture != 0) {
                    result = createTarget();
                    renderPassToTexture(result, [&](unsigned int fbo) {
                        return RenderMixBlend(
                            textureA,
                            textureB,
                            factorTexture,
                            node.mixFactor,
                            node.mixBlendMode,
                            fbo);
                    });
                    resultOwned = result != 0;
                } else {
                    std::cerr << "[RenderPipeline] Mix node " << node.nodeId
                              << " could not materialize its connected factor texture.\n";
                }
            } else if (inputA || inputB) {
                std::cerr << "[RenderPipeline] Mix node " << node.nodeId
                          << " could not materialize its connected image textures (A="
                          << textureA << ", B=" << textureB << ").\n";
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const GraphNodeRenderResult dataMathResult =
                RenderDataMathGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = dataMathResult.texture;
            resultOwned = dataMathResult.owned;
        } else if (node.kind == RenderGraphNodeKind::SpectrumView) {
            const RenderGraphLink* spectrumLink = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            if (spectrumLink != nullptr) {
                const RenderFrequencyResource spectrum = evalFrequency(
                    spectrumLink->fromNodeId, spectrumLink->fromSocketId);
                const GraphNodeRenderResult view =
                    RenderSpectrumVisualization(spectrum, node.spectrumViewSettings);
                result = view.texture;
                resultOwned = view.owned;
                m_Width = spectrum.sourceWidth;
                m_Height = spectrum.sourceHeight;
            }
            if (result == 0) {
                m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
                m_LastGraphExecutionStats.lastSpecializedFailure =
                    "Spectrum View could not materialize its typed Spectrum input.";
            }
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

            int referenceWidth = 0;
            int referenceHeight = 0;
            const auto evaluateChannel = [&](const RenderGraphLink* link) {
                if (!link) {
                    return 0u;
                }
                const unsigned int texture =
                    evalMask(link->fromNodeId, link->fromSocketId);
                if (texture != 0 && referenceWidth <= 0) {
                    referenceWidth = m_Width;
                    referenceHeight = m_Height;
                }
                return texture;
            };
            const unsigned int texR = evaluateChannel(linkR);
            const unsigned int texG = evaluateChannel(linkG);
            const unsigned int texB = evaluateChannel(linkB);
            const unsigned int texA = evaluateChannel(linkA);
            const bool inputsReady =
                (!linkR || texR != 0) &&
                (!linkG || texG != 0) &&
                (!linkB || texB != 0) &&
                (!linkA || texA != 0);
            if (inputsReady && referenceWidth > 0 && referenceHeight > 0) {
                m_Width = referenceWidth;
                m_Height = referenceHeight;
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return RenderChannelCombine(
                        texR, texG, texB, texA,
                        linkR != nullptr, linkG != nullptr,
                        linkB != nullptr, linkA != nullptr,
                        fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                const bool channelInput = IsScalarRenderSocket(
                    executionContext,
                    input->fromNodeId,
                    input->fromSocketId);
                if (!channelInput) {
                    result =
                        evalImage(
                            input->fromNodeId,
                            input->fromSocketId);
                } else {
                    const unsigned int channelTexture =
                        evalMask(
                            input->fromNodeId,
                            input->fromSocketId);
                    if (channelTexture != 0) {
                        unsigned int texR = 0;
                        unsigned int texG = 0;
                        unsigned int texB = 0;
                        bool hasR = false;
                        bool hasG = false;
                        bool hasB = false;
                        switch (node.outputChannelViewMode) {
                            case Stack::NodeMath::OutputChannelViewMode::Neutral:
                                texR = channelTexture;
                                texG = channelTexture;
                                texB = channelTexture;
                                hasR = hasG = hasB = true;
                                break;
                            case Stack::NodeMath::OutputChannelViewMode::Red:
                                texR = channelTexture;
                                hasR = true;
                                break;
                            case Stack::NodeMath::OutputChannelViewMode::Green:
                                texG = channelTexture;
                                hasG = true;
                                break;
                            case Stack::NodeMath::OutputChannelViewMode::Blue:
                                texB = channelTexture;
                                hasB = true;
                                break;
                        }
                        result = createTarget();
                        renderPassToTexture(result, [&](unsigned int fbo) {
                            return RenderChannelCombine(
                                texR,
                                texG,
                                texB,
                                0,
                                hasR,
                                hasG,
                                hasB,
                                false,
                                fbo);
                        });
                        resultOwned = result != 0;
                    }
                }
            }
        }

        if (result) {
            unsigned int borrowedRawFallback = 0;
            if (!resultOwned && rawBorrowedResultNeedsOwnedCache) {
                borrowedRawFallback = result;
                const unsigned int ownedCopy = CloneTextureForGraphCache(result, m_Width, m_Height);
                if (ownedCopy != 0) {
                    result = ownedCopy;
                    resultOwned = true;
                } else {
                    ReleaseGraphCacheEntry(m_GraphImageCache, key);
                }
            }
            if (passThroughOutput || !resultOwned) {
                // A borrowed pass-through owns no texture. Persistently caching
                // its GLuint creates a dangling alias if budget pruning or an
                // upstream replacement deletes the actual owner.
                ReleaseGraphCacheEntry(m_GraphImageCache, key);
            } else if (!StoreGraphCacheEntry(
                    m_GraphImageCache,
                    key,
                    result,
                    fingerprint,
                    true)) {
                glDeleteTextures(1, &result);
                result = borrowedRawFallback;
                resultOwned = false;
            }
            if (result != 0) {
                try {
                    imageCache[key] = result;
                    localTextureExtents[key] = { m_Width, m_Height };
                } catch (const std::bad_alloc&) {
                    imageCache.erase(key);
                    localTextureExtents.erase(key);
                } catch (const std::length_error&) {
                    imageCache.erase(key);
                    localTextureExtents.erase(key);
                }
            }
        } else {
            ReleaseGraphCacheEntry(m_GraphImageCache, key);
        }
        visitingImages.erase(key);
        return result;
    };

    for (const ScheduledGraphOutput& scheduled :
            evaluationSchedule.outputs) {
        const auto scheduledNode = nodes.find(scheduled.nodeId);
        if (scheduledNode == nodes.end() || !scheduledNode->second) {
            continue;
        }
        const RenderGraphNode& node = *scheduledNode->second;
        const bool scalarValueOutput =
            (node.kind == RenderGraphNodeKind::FieldMean &&
             scheduled.socketId == EditorNodeGraph::kValueOutputSocketId) ||
            (node.kind == RenderGraphNodeKind::SpectrumAnalyzer &&
             (scheduled.socketId ==
                  EditorNodeGraph::kBandPowerOutputSocketId ||
              scheduled.socketId ==
                  EditorNodeGraph::kPeakFrequencyOutputSocketId ||
              scheduled.socketId ==
                  EditorNodeGraph::kPeakDirectionOutputSocketId));
        if (scalarValueOutput) {
            double ignoredValue = 0.0;
            (void)evalScalar(
                scheduled.nodeId,
                scheduled.socketId,
                ignoredValue);
            continue;
        }
        if (node.kind == RenderGraphNodeKind::FrequencyResponse) {
            RenderFrequencyResponseSettings ignoredResponse;
            (void)evalResponse(scheduled.nodeId, ignoredResponse);
            continue;
        }
        const bool frequencyOutput =
            node.kind == RenderGraphNodeKind::FrequencyFft ||
            node.kind == RenderGraphNodeKind::ApplyFrequencyResponse ||
            node.kind == RenderGraphNodeKind::CombineSpectra ||
            node.kind == RenderGraphNodeKind::SpectrumSeparate ||
            node.kind == RenderGraphNodeKind::SpectrumRecombine;
        if (frequencyOutput) {
            (void)evalFrequency(
                scheduled.nodeId,
                scheduled.socketId);
            continue;
        }
        if (IsScalarRenderSocket(
                executionContext,
                scheduled.nodeId,
                scheduled.socketId)) {
            (void)evalMask(
                scheduled.nodeId,
                scheduled.socketId);
        } else {
            (void)evalImage(
                scheduled.nodeId,
                scheduled.socketId);
        }
    }

    if (rawDevelopmentSideEffectsRequested) {
        // Target samples and diagnostic overlays are auxiliary outputs. Force
        // the active RAW Development node to execute even when the visible
        // Output pixels are cacheable and unchanged.
        for (const auto& [nodeId, node] : nodes) {
            if (node != nullptr &&
                node->kind == RenderGraphNodeKind::RawDevelopment &&
                executionContext.IsActiveNode(nodeId)) {
                (void)evalImage(
                    nodeId,
                    EditorNodeGraph::kImageOutputSocketId);
            }
        }
    }

    unsigned int finalTexture = 0;
    const auto outputIt = nodes.find(graph.outputNodeId);
    if (outputIt != nodes.end() &&
        (outputIt->second->kind == RenderGraphNodeKind::MaskGenerator ||
         outputIt->second->kind == RenderGraphNodeKind::MaskUtility ||
         outputIt->second->kind == RenderGraphNodeKind::ImageToMask ||
         outputIt->second->kind == RenderGraphNodeKind::MaskCombine ||
         outputIt->second->kind == RenderGraphNodeKind::CustomMask ||
         outputIt->second->kind == RenderGraphNodeKind::ChannelSplit ||
         outputIt->second->kind == RenderGraphNodeKind::ConstantChannel ||
         outputIt->second->kind == RenderGraphNodeKind::FrequencyFilter ||
         outputIt->second->kind == RenderGraphNodeKind::FrequencyIfft ||
         outputIt->second->kind == RenderGraphNodeKind::FrequencyMask ||
         (outputIt->second->kind == RenderGraphNodeKind::DataMath && IsScalarRenderSocket(executionContext, graph.outputNodeId, graph.outputSocketId)) ||
         (outputIt->second->kind == RenderGraphNodeKind::MagnitudePhase && graph.outputSocketId == EditorNodeGraph::kMaskOutputSocketId) ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailAutoMask && graph.outputSocketId == "maskOut") ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailFusion && graph.outputSocketId == "maskOut"))) {
        finalTexture = evalMask(graph.outputNodeId, graph.outputSocketId);
    } else {
        finalTexture = evalImage(graph.outputNodeId, graph.outputSocketId);
    }
    const int finalWidth = m_Width;
    const int finalHeight = m_Height;
    m_OutputTexture = finalTexture ? finalTexture : 0;
    m_GraphSourceTexture = 0;
    const int referenceSourceNodeId = FindReferenceSourceNode(executionContext, graph.outputNodeId);
    if (referenceSourceNodeId > 0) {
        const auto referenceIt = nodes.find(referenceSourceNodeId);
        if (referenceIt != nodes.end() &&
            referenceIt->second &&
            referenceIt->second->kind == RenderGraphNodeKind::RawSource) {
            m_GraphSourceTexture = m_SourceTexture;
            m_GraphSourceWidth = m_BaseCanvasWidth;
            m_GraphSourceHeight = m_BaseCanvasHeight;
        } else {
            m_GraphSourceTexture = evalImage(referenceSourceNodeId, "imageOut");
            if (m_GraphSourceTexture != 0) {
                m_GraphSourceWidth = m_Width;
                m_GraphSourceHeight = m_Height;
            }
        }
    }
    if (finalTexture != 0 && finalWidth > 0 && finalHeight > 0) {
        m_Width = finalWidth;
        m_Height = finalHeight;
    }

    PruneInactiveGraphCache(m_GraphImageCache, executionContext);
    PruneInactiveGraphCache(m_GraphMaskCache, executionContext);
    PruneInactiveFrequencyCache(executionContext);
    for (auto scalarIt = m_GraphScalarCache.begin(); scalarIt != m_GraphScalarCache.end(); ) {
        if (!executionContext.IsActiveNode(ExtractNodeIdFromCacheKey(scalarIt->first))) {
            scalarIt = m_GraphScalarCache.erase(scalarIt);
        } else {
            ++scalarIt;
        }
    }
    PruneInactiveLutTextureCache(executionContext);
    PruneInactiveRawDevelopStageCache(executionContext);
    TrimGraphPersistentCachesToBudget();
    TrimGraphTransientTargetsToBudget();
    m_LastGraphExecutionStats.persistentCacheBytes = GraphPersistentCacheBytes();
    m_LastGraphExecutionStats.transientPoolBytes = GraphTransientTargetBytes();

    savedExecutionState.Restore();
}
