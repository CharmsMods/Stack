#pragma once

#include "Renderer/GLHelpers.h"
#include "NodeMath/PointwiseIR.h"
#include "NodeMath/SpecializedPlanning.h"
#include "Renderer/FullscreenQuad.h"
#include "Renderer/Frequency/GpuFft.h"
#include "Editor/Layers/LayerBase.h"
#include "Renderer/MaskRenderTypes.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGpuPipeline.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <type_traits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <memory>

namespace Stack::Renderer::GraphExecution {
struct GraphExecutionContext;
struct GraphTopologyIndex;
} // namespace Stack::Renderer::GraphExecution

struct RenderTextureStats {
    bool valid = false;
    float minRgb = 0.0f;
    float maxRgb = 0.0f;
    float minLuma = 0.0f;
    float maxLuma = 0.0f;
    float p001Luma = 0.0f;
    float p01Luma = 0.0f;
    float p05Luma = 0.0f;
    float p10Luma = 0.0f;
    float p25Luma = 0.0f;
    float p50Luma = 0.0f;
    float p75Luma = 0.0f;
    float p90Luma = 0.0f;
    float p95Luma = 0.0f;
    float p99Luma = 0.0f;
    float p999Luma = 0.0f;
    float logAverageLuma = 0.0f;
    float dynamicRangeEv = 0.0f;
    float validPixelPercent = 0.0f;
    float hdrPixelPercent = 0.0f;
    float displayClipPercent = 0.0f;
    float displayClipHighPercent = 0.0f;
    float displayClipLowPercent = 0.0f;
};

struct RawLocalRangeTargetPreviewMetrics {
    float qualifierIssueMs = 0.0f;
    float readbackCopyMs = 0.0f;
    float floodFillMs = 0.0f;
    float uploadMs = 0.0f;
    int maximumDimension = 0;
    bool cacheHit = false;
};

struct RawLocalRangeTargetPreviewCpuResult {
    std::uint64_t generation = 0;
    int width = 0;
    int height = 0;
    float floodFillMs = 0.0f;
    std::vector<std::uint32_t> selectedBits;
};

struct RawRgbDenoiseAsyncResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string provider;
    double inferenceMilliseconds = 0.0;
    int completedTiles = 0;
    int totalTiles = 0;
    std::size_t modelFingerprint = 0;
    std::size_t applicationFingerprint = 0;
    int width = 0;
    int height = 0;
    float inputExposureGain = 1.0f;
    double meanAbsoluteModelDelta = 0.0;
    double meanAbsoluteSceneDelta = 0.0;
    std::shared_ptr<const std::vector<float>> modelOutputSrgbProxy;
    std::shared_ptr<const std::vector<float>> outputRgba;
};

struct RawDevelopmentStageStatsReadback {
    bool valid = false;
    Stack::RawAutoStartPoint::RawAutoStartPointStage stage =
        Stack::RawAutoStartPoint::RawAutoStartPointStage::RawTechnical;
    Stack::RawAutoStartPoint::RawAutoStartPointStageStatus status =
        Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Unavailable;
    std::string stageId;
    std::string label;
    std::string sourceDescription;
    std::string measurementDomain;
    bool sceneLinearBeforeViewTransform = false;
    bool displayMappedLinearRgb = false;
    bool rawSafetyStats = false;
    Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats rawSafety;
    RenderTextureStats textureStats;
};

using RawDevelopmentStageImageReadback =
    Stack::RawAutoStartPoint::RawAutoStartPointStageImage;

enum class RawDevelopmentGraphScopeStage {
    None = 0,
    LocalRangeInput = 1,
    FinishToneInput = 2
};

struct RawDevelopmentGraphScopeReadback {
    bool valid = false;
    RawDevelopmentGraphScopeStage stage = RawDevelopmentGraphScopeStage::None;
    std::string measurementDomain;
    std::string controlSignalDomain;
    bool sceneLinearBeforeViewTransform = false;
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::vector<float> pixels;
    std::vector<float> controlSignal;
};

struct PointwiseExecutionGroupStats {
    std::vector<int> authoredNodeIds;
    int operationCount = 0;
    int avoidedPassCount = 0;
    std::uint64_t targetBytes = 0;
    double cpuSubmitMilliseconds = 0.0;
    bool programCacheHit = false;
    std::string targetFormat = "RGBA16F";
    std::string semanticFingerprint;
    std::string programFingerprint;
};

struct ReductionExecutionStats {
    int nodeId = -1;
    double value = 0.0;
    std::uint64_t sampleCount = 0;
    bool cacheHit = false;
    std::string definitionId;
};

struct SpecializedBoundaryExecutionStats {
    std::string kind;
    int inputWidth = 0;
    int inputHeight = 0;
    int outputWidth = 0;
    int outputHeight = 0;
    bool fullQuality = false;
    bool changesGraphResult = false;
};

struct GraphExecutionStats {
    bool allocationFailed = false;
    int imageCacheHits = 0;
    int imageCacheMisses = 0;
    int maskCacheHits = 0;
    int maskCacheMisses = 0;
    int frequencyCacheHits = 0;
    int frequencyCacheMisses = 0;
    int rawStageCacheHits = 0;
    int rawStageCacheMisses = 0;
    int fusedPointwiseGroups = 0;
    int fusedPointwiseNodes = 0;
    int avoidedPointwisePasses = 0;
    int pointwiseProgramCacheHits = 0;
    int pointwiseProgramCacheMisses = 0;
    int pointwiseFallbacks = 0;
    int reductionPasses = 0;
    int reductionCacheHits = 0;
    int reductionCacheMisses = 0;
    int transientTargetAllocations = 0;
    int transientTargetReuses = 0;
    int transientTargetEvictions = 0;
    int persistentCacheEvictions = 0;
    std::uint64_t transientPoolBytes = 0;
    std::uint64_t transientPoolBudgetBytes = 0;
    std::uint64_t persistentCacheBytes = 0;
    std::uint64_t persistentCacheBudgetBytes = 0;
    std::string lastPointwiseFailure;
    std::vector<int> lastPointwiseFailureNodeIds;
    std::vector<PointwiseExecutionGroupStats> pointwiseGroups;
    std::string lastReductionFailure;
    int lastReductionFailureNodeId = -1;
    std::vector<ReductionExecutionStats> reductions;
    std::vector<SpecializedBoundaryExecutionStats> specializedBoundaries;
    std::string lastSpecializedFailure;
    int lastSpecializedFailureNodeId = -1;
};

// The sequential rendering pipeline.
// Enforces the core architectural rule: Layer N+1 only sees Layer N's output.
// Uses ping-pong FBOs to chain processing stages.
class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    void Initialize();
    // Releases every CPU/GPU resource while the owning OpenGL context is
    // still current. Safe to call more than once; the destructor is a
    // fallback for owners whose lifetime already matches their context.
    void Shutdown();
    bool Resize(int width, int height);

    // Load a source image from disk into the pipeline
    bool LoadSourceImage(const std::string& filepath);
    void LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch);
    void LoadSourceFromSharedPixels(const SharedPixelBuffer& data, int w, int h, int ch);
    void Clear();
    void ClearOutput();
    unsigned int TakeExternalOutputTexture(int& outW, int& outH);
    bool UploadOutputFromPixels(
        const unsigned char* data,
        int w,
        int h,
        int ch);
    void AdoptExternalOutputTexture(unsigned int texture, int w, int h);
    unsigned int PublishSharedOutputTexture(
        int& outW,
        int& outH,
        bool forceOpaqueSampling = false);
    void SetPreviewMaxDimension(int maxDimension) { m_PreviewMaxDimension = std::max(0, maxDimension); }
    void SetRenderCancellationContext(
        std::uint64_t generation,
        std::function<bool()> shouldCancel) {
        m_RenderGeneration = generation;
        m_ShouldCancelRender = std::move(shouldCancel);
    }
    // Interactive RAW previews must remain image-only work. Expensive texture
    // readbacks and automatic-analysis evidence are observational and may be
    // deferred until the settled render without changing the rendered pixels.
    void SetRawDevelopmentAnalysisEnabled(bool enabled) {
        m_RawDevelopmentAnalysisEnabled = enabled;
    }
    bool IsRawDevelopmentAnalysisEnabled() const {
        return m_RawDevelopmentAnalysisEnabled;
    }
    // Live RAW editing owns the OpenGL context on the UI thread, but the
    // external Restormer inference and CPU adapter must not block that thread.
    // Export/validation pipelines leave this disabled and retain synchronous,
    // settled execution.
    void SetRawRgbDenoiseAsyncEnabled(bool enabled) {
        m_RawRgbDenoiseAsyncEnabled = enabled;
    }
    bool ConsumeRawRgbDenoiseAsyncCompletion();
    bool IsRawRgbDenoiseAsyncPending() const {
        return m_RestormerAsyncPending;
    }
    bool IsRawRgbDenoiseAsyncCompletionReady() const;
    const std::string& GetRawRgbDenoiseStatus() const {
        return m_LastRawRgbDenoiseStatus;
    }

    // Execute the full layer stack sequentially (ping-pong rendering)
    void Execute(const std::vector<std::shared_ptr<LayerBase>>& layers);
    void ExecuteMasked(const std::vector<RenderLayerStep>& steps, const std::vector<RenderMaskSource>& masks);
    void ExecuteGraph(const RenderGraphSnapshot& graph);
    void ExecuteGraph(
        const RenderGraphSnapshot& graph,
        const Stack::Renderer::GraphExecution::GraphTopologyIndex& topology);

    // Returns the final output texture ID for display in the ImGui viewport
    unsigned int GetOutputTexture() const { return m_OutputTexture; }
    unsigned int GetSourceTexture() const { return m_SourceTexture; }
    unsigned int GetCompareSourceTexture() const { return m_GraphSourceTexture != 0 ? m_GraphSourceTexture : m_SourceTexture; }
    int GetCanvasWidth() const { return m_Width; }
    int GetCanvasHeight() const { return m_Height; }
    bool HasSourceImage() const { return m_SourceTexture != 0; }

    // Read final output pixels (usually for thumbnails)
    std::vector<unsigned char> GetOutputPixels(int& outW, int& outH);
    std::vector<unsigned char> GetOutputPixels(int& outW, int& outH, int maxDimension);
    std::vector<unsigned char> GetCachedGraphImagePixels(int nodeId, const std::string& socketId, int& outW, int& outH) const;
    std::vector<unsigned char> GetCachedGraphImagePixels(int nodeId, const std::string& socketId, int& outW, int& outH, int maxDimension) const;
    bool WasGraphImageCacheHit(int nodeId, const std::string& socketId) const;
    // Read compare source pixels
    std::vector<unsigned char> GetCompareSourcePixels(int& outW, int& outH);
    // Read original source pixels
    std::vector<unsigned char> GetSourcePixels(int& outW, int& outH);
    // Read downsampled pixels for scopes (fast)
    std::vector<unsigned char> GetScopesPixels(int& outW, int& outH);
    // Read aspect-preserving preview pixels for graph preview nodes.
    std::vector<unsigned char> GetPreviewPixels(int& outW, int& outH, int maxDimension = 512);
    std::vector<unsigned char> GetRawDevelopmentLocalRangeOverlayPixels(int& outW, int& outH);
    unsigned int TakeRawDevelopmentLocalRangeOverlayTexture(int& outW, int& outH);
    RenderTextureStats GetRawDevelopmentViewTransformInputStats() const { return m_RawDevelopmentViewTransformInputStats; }
    RenderTextureStats GetRawDevelopmentFinalDisplayStats() const { return m_RawDevelopmentFinalDisplayStats; }
    const std::vector<RawDevelopmentStageStatsReadback>& GetRawDevelopmentStageStatsReadbacks() const {
        return m_RawDevelopmentStageStatsReadbacks;
    }
    void SetRawDevelopmentStageImageReadbackMaxDimension(int maxDimension) {
        m_RawDevelopmentStageImageReadbackMaxDimension = std::max(0, maxDimension);
    }
    const std::vector<RawDevelopmentStageImageReadback>& GetRawDevelopmentStageImageReadbacks() const {
        return m_RawDevelopmentStageImageReadbacks;
    }
    void SetRawDevelopmentGraphScopeReadbackRequest(
        RawDevelopmentGraphScopeStage stage,
        int maxDimension) {
        m_RawDevelopmentGraphScopeStage = stage;
        m_RawDevelopmentGraphScopeReadbackMaxDimension = std::max(0, maxDimension);
    }
    const RawDevelopmentGraphScopeReadback& GetRawDevelopmentGraphScopeReadback() const {
        return m_RawDevelopmentGraphScopeReadback;
    }
    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics BuildRawDevelopmentStartPointDiagnostics(
        const std::string& sourceKey) const;
    const Stack::RawAutoBase::LocalSuggestionAnalysisImage& GetRawDevelopmentLocalSuggestionImage() const {
        return m_RawDevelopmentLocalSuggestionImage;
    }
    bool GetRawDevelopmentLocalRangeTargetSample(
        float& outSceneEv,
        float& outSceneLuma,
        float& outU,
        float& outV,
        std::array<float, 3>* outSceneRgb = nullptr,
        std::uint32_t* outAuthoredZoneHitBits = nullptr,
        float* outStrongestAuthoredZoneWeight = nullptr) const;
    bool IsRawDevelopmentLocalRangeTargetPreviewRefined() const {
        return m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture != 0 &&
            m_RawDevelopmentLocalRangeTargetPreviewSelectionReadyGeneration ==
                m_RawDevelopmentLocalRangeTargetPreviewRequest.generation;
    }
    bool IsRawDevelopmentLocalRangeTargetPreviewRefinementPending() const {
        return m_RawDevelopmentLocalRangeTargetPreviewSelectionPending;
    }
    RawLocalRangeTargetPreviewMetrics
    GetRawDevelopmentLocalRangeTargetPreviewMetrics() const {
        return m_RawDevelopmentLocalRangeTargetPreviewMetrics;
    }
    RenderTextureStats GetOutputTextureStats();
    bool SampleOutputPixel(float u, float v, std::array<float, 4>& outRgba) const;
    static std::uint64_t EstimateRawDevelopStageCacheTextureBytesForValidation(int width, int height);
    static std::size_t ResolveRawDevelopStageCacheMaxEntriesForValidation(int width, int height);
    static bool ShouldCacheRawDevelopStageTextureForValidation(int width, int height);

    struct PreLocalExposureSummary {
        bool valid = false;
        Raw::RawDetailFusionSettings effectiveSettings;
        float clippingRatio = 0.0f;
        float channelSaturationRatio = 0.0f;
        float estimatedNoiseFloor = 0.0f;
        float shadowPercentile = 0.0f;
        float highlightPercentile = 0.0f;
        float textureConfidence = 0.0f;
        bool noiseLimited = false;
        bool highlightLimited = false;
        bool gradientProtected = false;
        bool legacyMaskActive = false;
        bool legacyManualMode = false;
    };

    const PreLocalExposureSummary* GetPreLocalExposureSummary(int nodeId) const;
    const std::vector<ToneCurveAutoRewriteFeedback>& GetToneCurveAutoRewriteFeedback() const { return m_ToneCurveAutoRewriteFeedback; }
    std::vector<ToneCurveAutoRewriteFeedback> TakeToneCurveAutoRewriteFeedback() noexcept {
        return std::move(m_ToneCurveAutoRewriteFeedback);
    }

    // Read-only access to raw source pixels
    const std::vector<unsigned char>& GetSourcePixelsRaw() const { return m_SourcePixelsShared ? *m_SourcePixelsShared : m_SourcePixels; }
    // Transfers the owned CPU source buffer into shared immutable storage when
    // needed, allowing workspace snapshots to retain it without a full copy.
    SharedPixelBuffer ShareSourcePixels(int& outW, int& outH, int& outChannels);
    int GetSourceChannels() const { return m_SourceChannels; }
    const GraphExecutionStats& GetLastGraphExecutionStats() const { return m_LastGraphExecutionStats; }
    bool GetCachedSpectrumAnalysis(
        int nodeId,
        RenderSpectrumAnalysis& outAnalysis) const {
        const auto found = m_GraphFrequencyAnalysisCache.find(
            std::to_string(nodeId) + ":radialPowerOut");
        if (found == m_GraphFrequencyAnalysisCache.end() ||
            !found->second.valid) {
            return false;
        }
        outAnalysis = found->second;
        return true;
    }
    const std::string& GetLastRawRgbDenoiseError() const {
        return m_LastRawRgbDenoiseError;
    }

    FullscreenQuad& GetQuad() { return m_Quad; }

private:
    struct CachedGraphTexture {
        unsigned int texture = 0;
        std::size_t fingerprint = 0;
        int width = 0;
        int height = 0;
        bool owned = false;
        std::uint64_t bytes = 0;
        std::uint64_t lastUseSerial = 0;
    };

    struct CachedGraphScalar {
        std::size_t fingerprint = 0;
        double value = 0.0;
        std::uint64_t sampleCount = 0;
        std::uint64_t lastUseSerial = 0;
    };

    struct CachedGraphFrequency {
        RenderFrequencyResource resource;
        std::size_t fingerprint = 0;
        bool owned = false;
        std::uint64_t bytes = 0;
        std::uint64_t lastUseSerial = 0;
    };

    struct CachedPointwiseProgram {
        unsigned int program = 0;
        std::uint64_t lastUseSerial = 0;
        std::size_t sourceBytes = 0;
    };

    struct CachedRawDevelopmentRecipeLayer {
        std::shared_ptr<LayerBase> layer;
        nlohmann::json defaultPayload = nlohmann::json::object();
        std::string type;
    };

    struct GraphTransientTarget {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool inUse = false;
        std::uint64_t lastUseSerial = 0;
    };

    struct PointwiseFusionPlan {
        bool valid = false;
        int inputNodeId = -1;
        std::string inputSocketId;
        std::vector<int> authoredNodeIds;
        Stack::NodeMath::PointwiseOptimizationResult optimized;
        Stack::NodeMath::GeneratedPointwiseShader shader;
        Stack::NodeMath::PointwisePhysicalPlan physicalPlan;
        std::string failure;
        std::vector<int> failureNodeIds;
    };

    struct AutoGainSceneStats {
        bool valid = false;
        float shadowPercentile = 0.02f;
        float midtonePercentile = 0.18f;
        float highlightPercentile = 0.85f;
        float clippingRatio = 0.0f;
        float channelSaturationRatio = 0.0f;
        float estimatedNoiseFloor = 0.002f;
        float textureConfidence = 0.5f;
        float recommendedMinEv = -1.25f;
        float recommendedMaxEv = 1.50f;
        float recommendedBaseEv = 0.0f;
        float recommendedNoiseProtection = 0.60f;
        float recommendedHighlightProtection = 0.90f;
        float recommendedShadowLiftLimit = 0.65f;
        float recommendedTarget = 0.30f;
    };

    struct HdrMergeInputContext {
        bool active = false;
        bool hasRawMetadata = false;
        bool hasCaptureExposure = false;
        float captureExposureEv = 0.0f;
        float developExposureStops = 0.0f;
        float developExposureScale = 1.0f;
        Raw::RawMetadata metadata;
    };

    struct HdrMergeResolvedSettings {
        std::array<float, 3> exposureEv { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> referenceExposureDistance { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> clipThreshold { 0.98f, 0.98f, 0.98f };
        std::array<float, 3> clipFeather { 0.08f, 0.08f, 0.08f };
        std::array<float, 3> blackThreshold { 0.002f, 0.002f, 0.002f };
        std::array<float, 3> blackFeather { 0.018f, 0.018f, 0.018f };
        std::array<float, 3> readNoise { 0.002f, 0.002f, 0.002f };
        bool metadataExposureValid = false;
    };

    struct SharedRawBaseStageResult {
        const RenderGraphNode* rawSource = nullptr;
        std::string sourcePath;
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool renderedThisPass = false;
    };

    struct GraphNodeRenderResult {
        unsigned int texture = 0;
        bool owned = false;
    };

    FullscreenQuad m_Quad;
    bool m_Shutdown = false;

    int m_Width;
    int m_Height;
    int m_BaseCanvasWidth;
    int m_BaseCanvasHeight;
    int m_SourceChannels;
    int m_PreviewMaxDimension = 0;
    bool m_RawDevelopmentAnalysisEnabled = true;

    unsigned int m_SourceTexture;   // The original loaded image
    unsigned int m_PingTexture;     // Ping FBO color attachment
    unsigned int m_PongTexture;     // Pong FBO color attachment
    unsigned int m_PingFBO;
    unsigned int m_PongFBO;
    unsigned int m_OutputTexture;   // Points to whichever is the final result
    unsigned int m_ExternalOutputTexture;
    unsigned int m_GraphSourceTexture;
    int m_GraphSourceWidth = 0;
    int m_GraphSourceHeight = 0;
    unsigned int m_MaskProgram;
    unsigned int m_MaskCombineProgram;
    unsigned int m_MaskBlendProgram;
    unsigned int m_MixProgram;
    unsigned int m_MaskUtilityProgram;
    unsigned int m_ImageToMaskProgram;
    unsigned int m_ImageGeneratorProgram;
    unsigned int m_DataMathProgram;
    unsigned int m_TechnicalImageProgram;
    unsigned int m_ReformatProgram;
    unsigned int m_SpectrumViewProgram;
    unsigned int m_FrequencyMaskProgram;
    unsigned int m_SpectrumMathProgram;
    unsigned int m_MagnitudePhaseProgram;
    unsigned int m_FrequencyIfftProjectProgram;
    unsigned int m_ChannelSplitProgram;
    unsigned int m_ChannelCombineProgram;
    unsigned int m_LutProgram;
    unsigned int m_HdrMergeProgram;
    unsigned int m_RawDetailFusionAnalysisProgram;
    unsigned int m_RawDetailFusionMetricsProgram;
    unsigned int m_RawDetailFusionSmoothProgram;
    unsigned int m_RawDetailFusionApplyProgram;
    unsigned int m_AutoGainStatsProgram;
    unsigned int m_RawDevelopmentToneCurveProgram;
    unsigned int m_RawDevelopmentLocalRangeProgram;
    unsigned int m_RawDevelopmentLocalRangeOverlayProgram;
    unsigned int m_RawDevelopmentLocalRangeQualifierProgram;
    unsigned int m_RawDevelopmentRgbDenoiseConvertProgram;
    unsigned int m_RawDevelopmentRgbDenoiseBlurProgram;
    unsigned int m_RawDevelopmentRgbDenoiseBandProgram;
    unsigned int m_RawDevelopmentRgbDenoiseReconstructProgram;
    unsigned int m_RawDevelopmentExposureProgram;
    unsigned int m_RawDevelopmentLocalRangeSelectionBitsTexture = 0;
    unsigned int m_RawDevelopmentLocalRangeSelectionBitsInputTexture = 0;
    int m_RawDevelopmentLocalRangeSelectionBitsWidth = 0;
    int m_RawDevelopmentLocalRangeSelectionBitsHeight = 0;
    int m_RawDevelopmentLocalRangeSelectionBitsTextureWidth = 0;
    int m_RawDevelopmentLocalRangeSelectionBitsTextureHeight = 0;
    std::size_t m_RawDevelopmentLocalRangeSelectionBitsInputFingerprint = 0;
    std::size_t m_RawDevelopmentLocalRangeSelectionBitsFingerprint = 0;
    unsigned int m_RawDevelopmentLocalRangeOverlayTexture = 0;
    int m_RawDevelopmentLocalRangeOverlayWidth = 0;
    int m_RawDevelopmentLocalRangeOverlayHeight = 0;
    std::string m_RawDevelopmentLocalRangeOverlayMode;
    std::string m_RawDevelopmentLocalRangeOverlayRequestMode;
    RawLocalRangeTargetPreviewRequest m_RawDevelopmentLocalRangeTargetPreviewRequest;
    RenderTextureStats m_RawDevelopmentViewTransformInputStats;
    RenderTextureStats m_RawDevelopmentFinalDisplayStats;
    std::vector<RawDevelopmentStageStatsReadback> m_RawDevelopmentStageStatsReadbacks;
    int m_RawDevelopmentStageImageReadbackMaxDimension = 0;
    std::vector<RawDevelopmentStageImageReadback> m_RawDevelopmentStageImageReadbacks;
    RawDevelopmentGraphScopeStage m_RawDevelopmentGraphScopeStage =
        RawDevelopmentGraphScopeStage::None;
    int m_RawDevelopmentGraphScopeReadbackMaxDimension = 0;
    RawDevelopmentGraphScopeReadback m_RawDevelopmentGraphScopeReadback;
    Stack::RawAutoBase::LocalSuggestionAnalysisImage m_RawDevelopmentLocalSuggestionImage;
    bool m_RawDevelopmentLocalRangeTargetSampleRequested = false;
    float m_RawDevelopmentLocalRangeTargetSampleRequestU = 0.0f;
    float m_RawDevelopmentLocalRangeTargetSampleRequestV = 0.0f;
    bool m_RawDevelopmentLocalRangeTargetSampleValid = false;
    float m_RawDevelopmentLocalRangeTargetSampleSceneEv = 0.0f;
    float m_RawDevelopmentLocalRangeTargetSampleSceneLuma = 0.0f;
    float m_RawDevelopmentLocalRangeTargetSampleSceneR = 0.0f;
    float m_RawDevelopmentLocalRangeTargetSampleSceneG = 0.0f;
    float m_RawDevelopmentLocalRangeTargetSampleSceneB = 0.0f;
    float m_RawDevelopmentLocalRangeTargetSampleU = 0.0f;
    float m_RawDevelopmentLocalRangeTargetSampleV = 0.0f;
    std::uint32_t m_RawDevelopmentLocalRangeTargetSampleAuthoredZoneHitBits = 0;
    float m_RawDevelopmentLocalRangeTargetSampleStrongestAuthoredZoneWeight = 0.0f;
    struct RawLocalRangeTargetPreviewReadbackSlot {
        unsigned int pbo = 0;
        GLsync fence = nullptr;
        std::uint64_t generation = 0;
        int width = 0;
        int height = 0;
        float seedU = 0.5f;
        float seedV = 0.5f;
        bool occupied = false;
    };
    std::array<RawLocalRangeTargetPreviewReadbackSlot, 2>
        m_RawDevelopmentLocalRangeTargetPreviewReadbackSlots;
    int m_RawDevelopmentLocalRangeTargetPreviewNextReadbackSlot = 0;
    std::future<RawLocalRangeTargetPreviewCpuResult>
        m_RawDevelopmentLocalRangeTargetPreviewCpuFuture;
    bool m_RawDevelopmentLocalRangeTargetPreviewCpuPending = false;
    std::uint64_t m_RawDevelopmentLocalRangeTargetPreviewCpuGeneration = 0;
    unsigned int m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture = 0;
    int m_RawDevelopmentLocalRangeTargetPreviewSelectionWidth = 0;
    int m_RawDevelopmentLocalRangeTargetPreviewSelectionHeight = 0;
    std::uint64_t
        m_RawDevelopmentLocalRangeTargetPreviewSelectionReadyGeneration = 0;
    bool m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = false;
    RawLocalRangeTargetPreviewMetrics
        m_RawDevelopmentLocalRangeTargetPreviewMetrics;
    std::vector<unsigned char> m_SourcePixels;
    std::shared_ptr<const std::vector<unsigned char>> m_SourcePixelsShared;
    std::size_t m_SourceFingerprint = 0;
    GraphExecutionStats m_LastGraphExecutionStats;
    std::uint64_t m_RenderGeneration = 0;
    std::function<bool()> m_ShouldCancelRender;
    std::string m_LastRawRgbDenoiseError;
    std::string m_LastRawRgbDenoiseStatus;
    bool m_RawRgbDenoiseAsyncEnabled = false;
    std::size_t m_RestormerNeutralCacheFingerprint = 0;
    int m_RestormerNeutralCacheWidth = 0;
    int m_RestormerNeutralCacheHeight = 0;
    std::shared_ptr<const std::vector<float>>
        m_RestormerNeutralCacheOutputProxy;
    std::size_t m_RestormerAppliedCacheFingerprint = 0;
    int m_RestormerAppliedCacheWidth = 0;
    int m_RestormerAppliedCacheHeight = 0;
    std::shared_ptr<const std::vector<float>> m_RestormerAppliedCacheRgba;
    std::future<RawRgbDenoiseAsyncResult> m_RestormerAsyncFuture;
    bool m_RestormerAsyncPending = false;
    std::size_t m_RestormerAsyncModelFingerprint = 0;
    std::size_t m_RestormerAsyncApplicationFingerprint = 0;
    std::shared_ptr<std::atomic<bool>> m_RestormerAsyncCancel;
    std::size_t m_RestormerLastCompletedModelFingerprint = 0;
    std::string m_RestormerLastCompletedError;
    std::unordered_map<std::string, CachedGraphTexture> m_GraphImageCache;
    std::unordered_map<std::string, CachedGraphTexture> m_GraphMaskCache;
    std::unordered_map<std::string, CachedGraphScalar> m_GraphScalarCache;
    std::unordered_map<std::string, CachedGraphFrequency> m_GraphFrequencyCache;
    std::unordered_map<std::string, RenderSpectrumAnalysis> m_GraphFrequencyAnalysisCache;
    std::unordered_map<std::string, CachedGraphTexture> m_LutTextureCache;
    std::unordered_map<std::string, std::vector<CachedGraphTexture>> m_RawDevelopStageImageCache;
    std::unordered_map<std::string, CachedRawDevelopmentRecipeLayer>
        m_RawDevelopmentRecipeLayerCache;
    std::unordered_set<std::string> m_LastGraphImageCacheHits;
    std::unordered_map<std::string, CachedPointwiseProgram> m_PointwiseProgramCache;
    std::vector<GraphTransientTarget> m_GraphTransientTargets;
    std::uint64_t m_GraphResourceUseSerial = 0;
    std::unordered_map<std::size_t, AutoGainSceneStats> m_AutoGainSceneStatsCache;
    std::unordered_map<int, PreLocalExposureSummary> m_PreLocalExposureSummaries;
    std::vector<ToneCurveAutoRewriteFeedback> m_ToneCurveAutoRewriteFeedback;
    std::unordered_map<int, Raw::RawGpuPipeline> m_RawPipelines;
    std::unordered_map<int, Raw::RawImageData> m_RawDataCache;
    std::unordered_map<int, std::string> m_RawDataCachePaths;
    std::unordered_map<int, Raw::RawImageData> m_RawPreviewDataCache;
    std::unordered_map<int, std::string> m_RawPreviewDataCacheKeys;
    Stack::Renderer::Frequency::GpuFft m_GpuFft;

    void CleanupFBOs();
    void DeleteGraphCacheEntry(CachedGraphTexture& entry);
    void DestroyGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache);
    void ReleaseGraphCacheEntry(std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key);
    unsigned int CloneTextureForGraphCache(unsigned int sourceTexture, int width, int height);
    bool StoreGraphCacheEntry(std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key, unsigned int texture, std::size_t fingerprint, bool owned);
    void TouchGraphCacheEntry(CachedGraphTexture& entry);
    void DeleteFrequencyCacheEntry(CachedGraphFrequency& entry);
    void DestroyFrequencyCache();
    bool StoreFrequencyCacheEntry(
        const std::string& key,
        const RenderFrequencyResource& resource,
        std::size_t fingerprint,
        bool owned);
    void PruneInactiveFrequencyCache(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext);
    std::uint64_t GraphPersistentCacheBytes() const;
    void TrimGraphPersistentCachesToBudget();
    void PruneInactiveGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache, const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext);
    void DestroyRawDevelopStageCache();
    void PruneInactiveRawDevelopStageCache(const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext);
    void DeleteLutTextureEntry(CachedGraphTexture& entry);
    void ClearLutTextureKey(const std::string& key);
    static std::size_t HashLut1DStage(const ColorLut::Lut1DStage& stage);
    static std::size_t HashLut3DStage(const ColorLut::Lut3DStage& stage);
    unsigned int GetOrCreateLut1DTexture(const std::string& key, const ColorLut::Lut1DStage& stage, std::size_t fingerprint);
    unsigned int GetOrCreateLut3DTexture(const std::string& key, const ColorLut::Lut3DStage& stage, std::size_t fingerprint);
    void PruneInactiveLutTextureCache(const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext);
    CachedGraphTexture FindRawDevelopStageCacheEntry(const std::string& key, std::size_t fingerprint);
    unsigned int CloneTextureForRawDevelopStageCache(unsigned int sourceTexture);
    void DeleteRawDevelopStageCacheEntry(CachedGraphTexture& entry);
    std::uint64_t RawDevelopStageCacheEntryBytes(const CachedGraphTexture& entry) const;
    std::uint64_t RawDevelopStageCacheTotalBytes() const;
    void TrimRawDevelopStageCacheVector(std::vector<CachedGraphTexture>& entries, std::size_t maxEntries);
    std::uint64_t TrimRawDevelopStageCacheToBudget(
        std::uint64_t currentTotalBytes,
        std::uint64_t maximumBytes,
        unsigned int protectedTexture = 0);
    void StoreRawDevelopStageCacheEntry(const std::string& key, unsigned int texture, std::size_t fingerprint);
    void InvalidateGraphCaches();
    unsigned int AcquireGraphTransientTarget();
    void ReleaseGraphTransientTarget(unsigned int texture);
    bool PromoteGraphTransientTarget(unsigned int texture);
    void TrimGraphTransientTargetsToBudget();
    void DestroyGraphTransientTargets();
    std::uint64_t GraphTransientTargetBytes() const;
    PointwiseFusionPlan BuildPointwiseFusionPlan(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        int nodeId,
        const std::string& socketId,
        const std::unordered_set<int>& disabledNodes) const;
    GraphNodeRenderResult RenderPointwiseFusionPlan(
        const PointwiseFusionPlan& plan,
        unsigned int inputTexture,
        bool executionInspectionEnabled);
    void DestroyPointwiseProgramCache();
    unsigned int CreateGraphRenderTargetTexture() const;
    bool RenderIntoGraphTargetTextureImpl(
        unsigned int texture,
        const void* renderContext,
        void (*renderFn)(const void*, unsigned int));
    template <typename RenderFn>
    bool RenderIntoGraphTargetTexture(unsigned int texture, RenderFn&& renderFn) {
        using Callable = std::remove_reference_t<RenderFn>;
        return RenderIntoGraphTargetTextureImpl(
            texture,
            std::addressof(renderFn),
            [](const void* context, unsigned int fbo) {
                auto* callable = static_cast<const Callable*>(context);
                (*const_cast<Callable*>(callable))(fbo);
            });
    }
    const Raw::RawImageData& ResolveRawPreviewRenderData(
        int cacheNodeId,
        const Raw::RawImageData& rawData,
        const std::string& sourceCacheKey);
    static int FindReferenceSourceNode(const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext, int nodeId);
    static HdrMergeInputContext ResolveHdrMergeInputContext(const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext, int sourceNodeId);
    static HdrMergeResolvedSettings ResolveHdrMergeSettings(
        const Raw::HdrMergeSettings& settings,
        const std::array<HdrMergeInputContext, 3>& contexts,
        const std::array<bool, 3>& activeInputs);
    static const RenderGraphNode* FindUpstreamRawSourceNode(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& rawConsumer);
    static int FindRawDetailAutoMaskSource(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        int nodeId,
        std::string_view socketId);
    static Raw::RawDetailFusionSettings ResolveRawDetailFusionApplySettings(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& node);
    SharedRawBaseStageResult RenderSharedRawBaseStage(
        Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& rawConsumer,
        const Raw::RawDevelopSettings& settings,
        const std::string& rawBaseKey,
        std::size_t rawBaseFingerprint,
        const std::function<std::size_t(int, const std::string&)>& fingerprintImage);
    GraphNodeRenderResult RenderLutGraphNode(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& node,
        const std::function<unsigned int(int, const std::string&)>& evalImage,
        const std::function<unsigned int(int, const std::string&)>& evalMask);
    GraphNodeRenderResult RenderRawDevelopGraphNode(
        Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& node,
        const std::string& socketId,
        std::size_t fingerprint,
        std::unordered_map<std::string, unsigned int>& imageCache,
        const std::function<unsigned int(int, const std::string&)>& evalImage,
        const std::function<unsigned int(int, const std::string&)>& evalMask,
        const std::function<std::size_t(int, const std::string&)>& fingerprintImage);
    GraphNodeRenderResult RenderRawDevelopmentGraphNode(
        const RenderGraphNode& node,
        std::size_t fingerprint);
    GraphNodeRenderResult RenderRawDetailGraphNode(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& node,
        const std::string& socketId,
        const std::function<unsigned int(int, const std::string&)>& evalImage,
        const std::function<unsigned int(int, const std::string&)>& evalMask);
    GraphNodeRenderResult RenderLayerGraphNode(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& node,
        const std::function<unsigned int(int, const std::string&)>& evalImage,
        const std::function<unsigned int(int, const std::string&)>& evalMask);
    GraphNodeRenderResult RenderDataMathGraphNode(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& node,
        const std::string& socketId,
        const std::function<unsigned int(int, const std::string&)>& evalImage,
        const std::function<unsigned int(int, const std::string&)>& evalMask);
    GraphNodeRenderResult RenderFrequencyGraphNode(
        const Stack::Renderer::GraphExecution::GraphExecutionContext& executionContext,
        const RenderGraphNode& node,
        const std::string& socketId,
        const std::function<unsigned int(int, const std::string&)>& evalImage,
        const std::function<unsigned int(int, const std::string&)>& evalMask);
    RenderFrequencyResource RenderFourierTransform(
        unsigned int channelTexture,
        int sourceWidth,
        int sourceHeight,
        RenderFrequencyEdgePolicy edgePolicy,
        std::string sourceRole);
    GraphNodeRenderResult RenderInverseFourierTransform(
        const RenderFrequencyResource& spectrum);
    unsigned int RenderFrequencyResponseTexture(
        const RenderFrequencyResponseSettings& settings,
        int paddedWidth,
        int paddedHeight);
    RenderFrequencyResource RenderApplyFrequencyResponse(
        const RenderFrequencyResource& spectrum,
        const RenderFrequencyResponseSettings& response,
        float strength);
    RenderFrequencyResource RenderCombineSpectra(
        const RenderFrequencyResource& a,
        const RenderFrequencyResource& b,
        RenderSpectrumCombineMode mode);
    RenderFrequencyResource RenderSpectrumComponent(
        const RenderFrequencyResource& spectrum,
        RenderFrequencyResourceKind componentKind);
    RenderFrequencyResource RenderRecombineSpectrum(
        const RenderFrequencyResource& magnitude,
        const RenderFrequencyResource& phase);
    GraphNodeRenderResult RenderSpectrumVisualization(
        const RenderFrequencyResource& spectrum,
        const RenderSpectrumViewSettings& settings);
    RenderSpectrumAnalysis AnalyzeSpectrum(
        const RenderFrequencyResource& spectrum,
        const RenderSpectrumAnalyzerSettings& settings,
        std::size_t fingerprint);
    void ExecuteGraphImpl(
        const RenderGraphSnapshot& graph,
        const Stack::Renderer::GraphExecution::GraphTopologyIndex& topology);
    void HandleGraphExecutionFailure(
        const RenderGraphSnapshot& graph,
        bool allocationFailed,
        const char* message) noexcept;
    bool RecordConsumerBoundary(
        Stack::NodeMath::SpecializedStageKind kind,
        int maximumDimension = 0);
    void EnsureMaskPrograms();
    void EnsureMixProgram();
    void EnsureUtilityPrograms();
    void EnsureChannelPrograms();
    void EnsureDataMathProgram();
    void EnsureTechnicalImageProgram();
    void EnsureReformatProgram();
    void EnsureFrequencyPrograms();
    void EnsureLutProgram();
    void EnsureHdrMergeProgram();
    void EnsureRawDetailFusionPrograms();
    void EnsureAutoGainStatsProgram();
    void EnsureRawDevelopmentToneCurveProgram();
    void EnsureRawDevelopmentLocalRangeProgram();
    void EnsureRawDevelopmentLocalRangeOverlayProgram();
    void EnsureRawDevelopmentLocalRangeQualifierProgram();
    void EnsureRawDevelopmentRgbDenoisePrograms();
    void EnsureRawDevelopmentExposureProgram();
    unsigned int BuildRawDevelopmentLocalRangeSelectionBits(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
        Raw::RawWorkingSpace workingSpace,
        std::size_t inputStageFingerprint,
        int maxSelectionDimension = 1536);
    unsigned int BuildRawDevelopmentLocalRangeTargetPreviewSelectionBits(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
        Raw::RawWorkingSpace workingSpace,
        const RawLocalRangeTargetPreviewRequest& request);
    void ClearRawDevelopmentLocalRangeSelectionBits();
    void ClearRawDevelopmentLocalRangeTargetPreviewSelection();
    void ClearRawDevelopmentStageStatsReadbacks();
    void CaptureRawDevelopmentStageImageReadback(
        Stack::RawAutoStartPoint::RawAutoStartPointStage stage,
        Stack::RawAutoStartPoint::RawAutoStartPointStageStatus status,
        unsigned int texture,
        int width,
        int height,
        const std::string& measurementDomain,
        bool sceneLinearBeforeViewTransform,
        bool displayMappedLinearRgb);
    void CaptureRawDevelopmentGraphScopeReadback(
        RawDevelopmentGraphScopeStage stage,
        unsigned int texture,
        int width,
        int height,
        const std::string& measurementDomain,
        bool sceneLinearBeforeViewTransform,
        const std::string& controlSignalDomain = {});
    void CaptureRawDevelopmentLocalRangeGraphScopeReadback(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
        int sourceWidth,
        int sourceHeight);
    void ClearRawDevelopmentLocalRangeOverlay();
    void ClearRawDevelopmentLocalRangeTargetSample();
    RenderTextureStats ReadTextureStats(unsigned int texture, int width, int height, const char* context);
    Stack::RawAutoBase::LocalSuggestionAnalysisImage ReadLocalSuggestionAnalysisImage(
        unsigned int texture,
        int width,
        int height,
        int maxDimension,
        const char* context);
    bool CaptureRawDevelopmentLocalRangeTargetSample(
        unsigned int texture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
        Raw::RawWorkingSpace workingSpace,
        std::size_t inputStageFingerprint);
    AutoGainSceneStats ComputeAutoGainSceneStats(unsigned int inputTexture);
    Raw::RawDetailFusionSettings ResolveAutoGainEffectiveSettings(unsigned int inputTexture, const Raw::RawDetailFusionSettings& settings);
    PreLocalExposureSummary BuildPreLocalExposureSummary(
        unsigned int inputTexture,
        const Raw::RawDetailFusionSettings& settings,
        bool legacyMaskActive,
        bool legacyManualMode);
    unsigned int GenerateMaskTexture(const RenderMaskSource& mask);
    unsigned int GenerateCustomMaskTexture(const RenderCustomMaskPayload& payload);
    unsigned int GenerateImageTexture(const RenderGraphNode& node);
    bool RenderMaskCombine(unsigned int maskA, unsigned int maskB, RenderMaskCombineMode mode, unsigned int targetFBO);
    bool RenderMaskUtility(unsigned int inputMask, const RenderGraphNode& node, unsigned int targetFBO);
    bool RenderImageToMask(unsigned int inputImage, const RenderGraphNode& node, unsigned int targetFBO);
    bool RenderMaskBlend(unsigned int originalTexture, unsigned int processedTexture, unsigned int maskTexture, unsigned int targetFBO);
    bool RenderMixBlend(unsigned int textureA, unsigned int textureB, unsigned int factorTexture, float factor, RenderMixBlendMode mode, unsigned int targetFBO);
    bool RenderChannelSplit(unsigned int inputTexture, int channel, unsigned int targetFBO);
    bool RenderDataMath(unsigned int textureA, unsigned int textureB, bool hasA, bool hasB, bool scalarA, bool scalarB,
                        RenderDataMathMode mode, const RenderDataMathSettings& settings, bool scalarOutput, unsigned int targetFBO);
    bool RenderTechnicalImage(
        unsigned int texture,
        Stack::NodeMath::TechnicalImageOperation operation,
        float exposureValue,
        unsigned int targetFBO);
    bool RenderReformat(
        unsigned int texture,
        int inputWidth,
        int inputHeight,
        const Stack::NodeMath::ReformatSettings& settings,
        unsigned int targetFBO);
    bool RenderChannelCombine(unsigned int texR, unsigned int texG, unsigned int texB, unsigned int texA,
                              bool hasR, bool hasG, bool hasB, bool hasA, unsigned int targetFBO);
    bool RenderHdrMerge(unsigned int texture1, unsigned int texture2, unsigned int texture3,
                        bool hasTexture2, bool hasTexture3, const Raw::HdrMergeSettings& settings,
                        const HdrMergeResolvedSettings& resolved, unsigned int targetFBO);
    unsigned int RenderRawDetailAutoMask(unsigned int inputTexture, const RenderGraphNode& node, unsigned int manualMaskTexture = 0, bool debugPreview = false);
    unsigned int RenderRawDetailFusion(unsigned int inputTexture, unsigned int maskTexture, const Raw::RawDetailFusionSettings& settings);
    unsigned int RenderRawDevelopmentToneCurve(unsigned int inputTexture, const std::vector<Raw::RawToneCurvePoint>& points);
    unsigned int RenderRawDevelopmentRgbDenoise(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawRgbDenoiseRecipe& settings,
        Raw::RawWorkingSpace workingSpace,
        std::size_t neutralInputFingerprint);
    unsigned int RenderRawDevelopmentExposure(
        unsigned int inputTexture,
        float exposureEv);
    unsigned int RenderRawDevelopmentLocalRange(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
        Raw::RawWorkingSpace workingSpace,
        std::size_t inputStageFingerprint);
    unsigned int RenderRawDevelopmentLocalRangeOverlay(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
        Raw::RawWorkingSpace workingSpace,
        const std::string& overlayMode,
        std::size_t inputStageFingerprint);
};
