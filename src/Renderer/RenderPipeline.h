#pragma once

#include "Renderer/GLHelpers.h"
#include "Renderer/FullscreenQuad.h"
#include "Editor/Layers/LayerBase.h"
#include "Renderer/MaskRenderTypes.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGpuPipeline.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

namespace Stack::Renderer::GraphExecution {
struct GraphExecutionContext;
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
    float p50Luma = 0.0f;
    float p95Luma = 0.0f;
    float p99Luma = 0.0f;
    float p999Luma = 0.0f;
    float logAverageLuma = 0.0f;
    float dynamicRangeEv = 0.0f;
    float validPixelPercent = 0.0f;
    float hdrPixelPercent = 0.0f;
    float displayClipPercent = 0.0f;
};

struct GraphExecutionStats {
    int imageCacheHits = 0;
    int imageCacheMisses = 0;
    int maskCacheHits = 0;
    int maskCacheMisses = 0;
    int rawStageCacheHits = 0;
    int rawStageCacheMisses = 0;
};

// The sequential rendering pipeline.
// Enforces the core architectural rule: Layer N+1 only sees Layer N's output.
// Uses ping-pong FBOs to chain processing stages.
class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    void Initialize();
    void Resize(int width, int height);

    // Load a source image from disk into the pipeline
    bool LoadSourceImage(const std::string& filepath);
    void LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch);
    void LoadSourceFromSharedPixels(const SharedPixelBuffer& data, int w, int h, int ch);
    void Clear();
    void ClearOutput();
    unsigned int TakeExternalOutputTexture(int& outW, int& outH);
    void UploadOutputFromPixels(const unsigned char* data, int w, int h, int ch);
    void AdoptExternalOutputTexture(unsigned int texture, int w, int h);
    unsigned int PublishSharedOutputTexture(int& outW, int& outH);
    void SetPreviewMaxDimension(int maxDimension) { m_PreviewMaxDimension = std::max(0, maxDimension); }

    // Execute the full layer stack sequentially (ping-pong rendering)
    void Execute(const std::vector<std::shared_ptr<LayerBase>>& layers);
    void ExecuteMasked(const std::vector<RenderLayerStep>& steps, const std::vector<RenderMaskSource>& masks);
    void ExecuteGraph(const RenderGraphSnapshot& graph);

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
    RenderTextureStats GetRawDevelopmentViewTransformInputStats() const { return m_RawDevelopmentViewTransformInputStats; }
    const Stack::RawAutoBase::LocalSuggestionAnalysisImage& GetRawDevelopmentLocalSuggestionImage() const {
        return m_RawDevelopmentLocalSuggestionImage;
    }
    bool GetRawDevelopmentLocalRangeTargetSample(
        float& outSceneEv,
        float& outSceneLuma,
        float& outU,
        float& outV,
        std::array<float, 3>* outSceneRgb = nullptr) const;
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

    // Read-only access to raw source pixels
    const std::vector<unsigned char>& GetSourcePixelsRaw() const { return m_SourcePixelsShared ? *m_SourcePixelsShared : m_SourcePixels; }
    int GetSourceChannels() const { return m_SourceChannels; }
    const GraphExecutionStats& GetLastGraphExecutionStats() const { return m_LastGraphExecutionStats; }

    FullscreenQuad& GetQuad() { return m_Quad; }

private:
    struct CachedGraphTexture {
        unsigned int texture = 0;
        std::size_t fingerprint = 0;
        int width = 0;
        int height = 0;
        bool owned = false;
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

    int m_Width;
    int m_Height;
    int m_SourceChannels;
    int m_PreviewMaxDimension = 0;

    unsigned int m_SourceTexture;   // The original loaded image
    unsigned int m_PingTexture;     // Ping FBO color attachment
    unsigned int m_PongTexture;     // Pong FBO color attachment
    unsigned int m_PingFBO;
    unsigned int m_PongFBO;
    unsigned int m_OutputTexture;   // Points to whichever is the final result
    unsigned int m_ExternalOutputTexture;
    unsigned int m_GraphSourceTexture;
    unsigned int m_MaskProgram;
    unsigned int m_MaskCombineProgram;
    unsigned int m_MaskBlendProgram;
    unsigned int m_MixProgram;
    unsigned int m_MaskUtilityProgram;
    unsigned int m_ImageToMaskProgram;
    unsigned int m_ImageGeneratorProgram;
    unsigned int m_DataMathProgram;
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
    unsigned int m_RawDevelopmentLocalRangeOverlayTexture = 0;
    int m_RawDevelopmentLocalRangeOverlayWidth = 0;
    int m_RawDevelopmentLocalRangeOverlayHeight = 0;
    std::string m_RawDevelopmentLocalRangeOverlayMode;
    std::string m_RawDevelopmentLocalRangeOverlayRequestMode;
    RenderTextureStats m_RawDevelopmentViewTransformInputStats;
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
    std::vector<unsigned char> m_SourcePixels;
    std::shared_ptr<const std::vector<unsigned char>> m_SourcePixelsShared;
    std::size_t m_SourceFingerprint = 0;
    GraphExecutionStats m_LastGraphExecutionStats;
    std::unordered_map<std::string, CachedGraphTexture> m_GraphImageCache;
    std::unordered_map<std::string, CachedGraphTexture> m_GraphMaskCache;
    std::unordered_map<std::string, CachedGraphTexture> m_LutTextureCache;
    std::unordered_map<std::string, std::vector<CachedGraphTexture>> m_RawDevelopStageImageCache;
    std::unordered_set<std::string> m_LastGraphImageCacheHits;
    std::unordered_map<std::size_t, AutoGainSceneStats> m_AutoGainSceneStatsCache;
    std::unordered_map<int, PreLocalExposureSummary> m_PreLocalExposureSummaries;
    std::vector<ToneCurveAutoRewriteFeedback> m_ToneCurveAutoRewriteFeedback;
    std::unordered_map<int, Raw::RawGpuPipeline> m_RawPipelines;
    std::unordered_map<int, Raw::RawImageData> m_RawDataCache;
    std::unordered_map<int, std::string> m_RawDataCachePaths;
    std::unordered_map<int, Raw::RawImageData> m_RawPreviewDataCache;
    std::unordered_map<int, std::string> m_RawPreviewDataCacheKeys;

    void CleanupFBOs();
    void DeleteGraphCacheEntry(CachedGraphTexture& entry);
    void DestroyGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache);
    void ReleaseGraphCacheEntry(std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key);
    unsigned int CloneTextureForGraphCache(unsigned int sourceTexture, int width, int height);
    void StoreGraphCacheEntry(std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key, unsigned int texture, std::size_t fingerprint, bool owned);
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
    void TrimRawDevelopStageCacheToBudget(std::uint64_t currentTotalBytes);
    void StoreRawDevelopStageCacheEntry(const std::string& key, unsigned int texture, std::size_t fingerprint);
    void InvalidateGraphCaches();
    unsigned int CreateGraphRenderTargetTexture() const;
    bool RenderIntoGraphTargetTexture(unsigned int texture, const std::function<void(unsigned int)>& renderFn);
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
    void ExecuteGraphImpl(const RenderGraphSnapshot& graph);
    void EnsureMaskPrograms();
    void EnsureMixProgram();
    void EnsureUtilityPrograms();
    void EnsureChannelPrograms();
    void EnsureDataMathProgram();
    void EnsureLutProgram();
    void EnsureHdrMergeProgram();
    void EnsureRawDetailFusionPrograms();
    void EnsureAutoGainStatsProgram();
    void EnsureRawDevelopmentToneCurveProgram();
    void EnsureRawDevelopmentLocalRangeProgram();
    void EnsureRawDevelopmentLocalRangeOverlayProgram();
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
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange);
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
    void RenderMaskCombine(unsigned int maskA, unsigned int maskB, RenderMaskCombineMode mode, unsigned int targetFBO);
    void RenderMaskUtility(unsigned int inputMask, const RenderGraphNode& node, unsigned int targetFBO);
    void RenderImageToMask(unsigned int inputImage, const RenderGraphNode& node, unsigned int targetFBO);
    void RenderMaskBlend(unsigned int originalTexture, unsigned int processedTexture, unsigned int maskTexture, unsigned int targetFBO);
    void RenderMixBlend(unsigned int textureA, unsigned int textureB, unsigned int factorTexture, float factor, RenderMixBlendMode mode, unsigned int targetFBO);
    void RenderChannelSplit(unsigned int inputTexture, int channel, unsigned int targetFBO);
    void RenderDataMath(unsigned int textureA, unsigned int textureB, bool hasA, bool hasB, bool scalarA, bool scalarB,
                        RenderDataMathMode mode, const RenderDataMathSettings& settings, bool scalarOutput, unsigned int targetFBO);
    void RenderChannelCombine(unsigned int texR, unsigned int texG, unsigned int texB, unsigned int texA,
                            bool hasR, bool hasG, bool hasB, bool hasA, unsigned int targetFBO);
    bool RenderHdrMerge(unsigned int texture1, unsigned int texture2, unsigned int texture3,
                        bool hasTexture2, bool hasTexture3, const Raw::HdrMergeSettings& settings,
                        const HdrMergeResolvedSettings& resolved, unsigned int targetFBO);
    unsigned int RenderRawDetailAutoMask(unsigned int inputTexture, const RenderGraphNode& node, unsigned int manualMaskTexture = 0, bool debugPreview = false);
    unsigned int RenderRawDetailFusion(unsigned int inputTexture, unsigned int maskTexture, const Raw::RawDetailFusionSettings& settings);
    unsigned int RenderRawDevelopmentToneCurve(unsigned int inputTexture, const std::vector<Raw::RawToneCurvePoint>& points);
    unsigned int RenderRawDevelopmentLocalRange(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange);
    unsigned int RenderRawDevelopmentLocalRangeOverlay(
        unsigned int inputTexture,
        const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
        const std::string& overlayMode);
};
