#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

#include <cstdint>
#include <string>

class ClassicalRgbDenoiseLayer : public LayerBase {
public:
    struct FloatImage;

    ~ClassicalRgbDenoiseLayer() override;

    const char* GetDefaultName() const override { return "Classical RGB Denoise"; }
    const char* GetCategory() const override { return "Blur / Focus"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;
    void RenderUI(EditorModule* editor) override;
    NodeSurfaceSpec GetNodeSurfaceSpec() const override;
    void RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_CopyProgram = 0;
    unsigned int m_ResultTexture = 0;
    int m_ResultWidth = 0;
    int m_ResultHeight = 0;
    bool m_HasCachedResult = false;
    bool m_RunRequested = false;
    bool m_AllowLargeCpuRunOnce = false;
    int m_HandledRunRequestRevision = 0;
    std::uint64_t m_CachedResultKey = 0;

    bool m_DenoiseEnabled = true;
    int m_QualityMode = 1;
    int m_Iterations = 3;
    bool m_Conservative = true;
    float m_Strength = 0.62f;
    float m_FineNoise = 0.72f;
    float m_OutputMix = 0.88f;
    float m_Chroma = 0.78f;
    float m_DetailProtect = 0.60f;
    float m_ShadowBoost = 0.55f;
    float m_Sharpen = 0.10f;
    float m_Grain = 0.0f;
    int m_RunRequestRevision = 0;
    bool m_RunRequestAllowLargeCpu = false;
    int m_RenderNodeId = -1;

    std::string m_LastExecutionStatus = "Bypassed";
    std::string m_LastWinnerSummary;
    std::string m_LastError;
    double m_LastRunSeconds = 0.0;
    int m_LastPreviewCandidates = 0;
    int m_LastWinnerCount = 0;
    float m_LastBestScore = 0.0f;
    int m_LastInputWidth = 0;
    int m_LastInputHeight = 0;
    int m_ActiveUiNodeId = -1;

    void DrawCopy(unsigned int inputTexture, FullscreenQuad& quad);
    bool ReadTextureToImage(unsigned int inputTexture, int width, int height, FloatImage& outImage);
    bool UploadAndDrawResult(const FloatImage& image, FullscreenQuad& quad);
    std::uint64_t BuildCacheKey(unsigned int inputTexture, int width, int height) const;
    bool CpuRunNeedsConfirmation(int width, int height) const;
    void MarkDenoiseUiDirty(EditorModule* editor) const;
    void PublishRenderStatus() const;
    void RefreshPublishedRenderStatus();
    void ClearCachedResult(const char* status = nullptr);
};
