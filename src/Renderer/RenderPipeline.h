#pragma once

#include "Renderer/GLHelpers.h"
#include "Renderer/FullscreenQuad.h"
#include "Editor/Layers/LayerBase.h"
#include "Renderer/MaskRenderTypes.h"
#include "Raw/RawGpuPipeline.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

struct RenderTextureStats {
    bool valid = false;
    float minRgb = 0.0f;
    float maxRgb = 0.0f;
    float minLuma = 0.0f;
    float maxLuma = 0.0f;
    float p01Luma = 0.0f;
    float p50Luma = 0.0f;
    float p99Luma = 0.0f;
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
 
    void CleanupFBOs();
    void DestroyGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache);
    void DestroyRawDevelopStageCache();
    void InvalidateGraphCaches();
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
};
