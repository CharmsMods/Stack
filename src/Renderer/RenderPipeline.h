#pragma once

#include "Renderer/GLHelpers.h"
#include "Renderer/FullscreenQuad.h"
#include "Editor/Layers/LayerBase.h"
#include "Renderer/MaskRenderTypes.h"
#include "Raw/RawGpuPipeline.h"
#include <cstddef>
#include <unordered_map>
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
    void Clear();
    void ClearOutput();
    void UploadOutputFromPixels(const unsigned char* data, int w, int h, int ch);
    void AdoptExternalOutputTexture(unsigned int texture, int w, int h);
    unsigned int PublishSharedOutputTexture(int& outW, int& outH);

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
    // Read compare source pixels
    std::vector<unsigned char> GetCompareSourcePixels(int& outW, int& outH);
    // Read original source pixels
    std::vector<unsigned char> GetSourcePixels(int& outW, int& outH);
    // Read downsampled pixels for scopes (fast)
    std::vector<unsigned char> GetScopesPixels(int& outW, int& outH);
    RenderTextureStats GetOutputTextureStats();

    // Read-only access to raw source pixels
    const std::vector<unsigned char>& GetSourcePixelsRaw() const { return m_SourcePixels; }
    int GetSourceChannels() const { return m_SourceChannels; }

    FullscreenQuad& GetQuad() { return m_Quad; }

private:
    struct CachedGraphTexture {
        unsigned int texture = 0;
        std::size_t fingerprint = 0;
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
        float recommendedMinEv = -1.0f;
        float recommendedMaxEv = 4.0f;
        float recommendedBaseEv = 0.0f;
        float recommendedNoiseProtection = 0.55f;
        float recommendedHighlightProtection = 0.85f;
        float recommendedShadowLiftLimit = 0.70f;
        float recommendedTarget = 0.42f;
    };

    FullscreenQuad m_Quad;

    int m_Width;
    int m_Height;
    int m_SourceChannels;

    unsigned int m_SourceTexture;   // The original loaded image
    unsigned int m_PingTexture;     // Ping FBO color attachment
    unsigned int m_PongTexture;     // Pong FBO color attachment
    unsigned int m_PingFBO;
    unsigned int m_PongFBO;
    unsigned int m_OutputTexture;   // Points to whichever is the final result
    unsigned int m_ExternalOutputTexture;
    unsigned int m_GraphSourceTexture;
    unsigned int m_MaskProgram;
    unsigned int m_MaskBlendProgram;
    unsigned int m_MixProgram;
    unsigned int m_MaskUtilityProgram;
    unsigned int m_ImageToMaskProgram;
    unsigned int m_ImageGeneratorProgram;
    unsigned int m_ChannelSplitProgram;
    unsigned int m_ChannelCombineProgram;
    unsigned int m_RawDetailFusionAnalysisProgram;
    unsigned int m_RawDetailFusionMetricsProgram;
    unsigned int m_RawDetailFusionSmoothProgram;
    unsigned int m_RawDetailFusionApplyProgram;
    unsigned int m_AutoGainStatsProgram;
    std::vector<unsigned char> m_SourcePixels;
    std::size_t m_SourceFingerprint = 0;
    std::unordered_map<std::string, CachedGraphTexture> m_GraphImageCache;
    std::unordered_map<std::string, CachedGraphTexture> m_GraphMaskCache;
    std::unordered_map<std::size_t, AutoGainSceneStats> m_AutoGainSceneStatsCache;
    std::unordered_map<int, Raw::RawGpuPipeline> m_RawPipelines;
    std::unordered_map<int, Raw::RawImageData> m_RawDataCache;
    std::unordered_map<int, std::string> m_RawDataCachePaths;
 
    void CleanupFBOs();
    void DestroyGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache);
    void InvalidateGraphCaches();
    void EnsureMaskPrograms();
    void EnsureMixProgram();
    void EnsureUtilityPrograms();
    void EnsureChannelPrograms();
    void EnsureRawDetailFusionPrograms();
    void EnsureAutoGainStatsProgram();
    AutoGainSceneStats ComputeAutoGainSceneStats(unsigned int inputTexture);
    Raw::RawDetailFusionSettings ResolveAutoGainEffectiveSettings(unsigned int inputTexture, const Raw::RawDetailFusionSettings& settings);
    unsigned int GenerateMaskTexture(const RenderMaskSource& mask);
    unsigned int GenerateImageTexture(const RenderGraphNode& node);
    void RenderMaskUtility(unsigned int inputMask, const RenderGraphNode& node, unsigned int targetFBO);
    void RenderImageToMask(unsigned int inputImage, const RenderGraphNode& node, unsigned int targetFBO);
    void RenderMaskBlend(unsigned int originalTexture, unsigned int processedTexture, unsigned int maskTexture, unsigned int targetFBO);
    void RenderMixBlend(unsigned int textureA, unsigned int textureB, unsigned int factorTexture, float factor, RenderMixBlendMode mode, unsigned int targetFBO);
    void RenderChannelSplit(unsigned int inputTexture, int channel, unsigned int targetFBO);
    void RenderChannelCombine(unsigned int texR, unsigned int texG, unsigned int texB, unsigned int texA,
                            bool hasR, bool hasG, bool hasB, bool hasA, unsigned int targetFBO);
    unsigned int RenderRawDetailAutoMask(unsigned int inputTexture, const RenderGraphNode& node, unsigned int manualMaskTexture = 0, bool debugPreview = false);
    unsigned int RenderRawDetailFusion(unsigned int inputTexture, unsigned int maskTexture, const Raw::RawDetailFusionSettings& settings);
};
