#pragma once

#include "LayerBase.h"
#include "NeuralDenoise/NeuralDenoiseTypes.h"
#include "Renderer/GLHelpers.h"

#include <cstdint>
#include <string>

class LinearRgbNeuralDenoiseLayer : public LayerBase {
public:
    ~LinearRgbNeuralDenoiseLayer() override;

    const char* GetDefaultName() const override { return "Linear RGB Neural Denoise"; }
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
    std::string m_LastExecutionStatus = "Bypassed";
    NeuralDenoise::NeuralDenoiseSettings m_Settings;
    std::uint64_t m_CachedResultKey = 0;
    std::uint64_t m_LastFailedKey = 0;
    std::string m_LastError;
    bool m_HasCachedResult = false;
    bool m_RunRequested = false;
    bool m_AllowLargeCpuRunOnce = false;
    int m_LastInputWidth = 0;
    int m_LastInputHeight = 0;
    int m_LastTileCount = 0;
    int m_HandledRunRequestRevision = 0;
    double m_LastInferenceSeconds = 0.0;
    std::string m_LastProvider = "None";
    int m_ActiveUiNodeId = -1;
    int m_RenderNodeId = -1;

    void DrawCopy(unsigned int inputTexture, FullscreenQuad& quad);
    bool ReadTextureToImage(unsigned int inputTexture, int width, int height, NeuralDenoise::NeuralDenoiseImage& outImage);
    bool UploadAndDrawResult(const NeuralDenoise::NeuralDenoiseImage& image, FullscreenQuad& quad);
    bool RunTiledInference(const NeuralDenoise::NeuralDenoiseModelInfo& model, NeuralDenoise::NeuralDenoiseImage& image, int& outTileCount);
    std::uint64_t BuildCacheKey(unsigned int inputTexture, int width, int height, const NeuralDenoise::NeuralDenoiseModelInfo& model) const;
    bool CpuRunNeedsConfirmation(int width, int height, const NeuralDenoise::NeuralDenoiseModelInfo& model) const;
    int EstimateTileCount(int width, int height, const NeuralDenoise::NeuralDenoiseModelInfo* model) const;
    std::string ExpectedProviderLabel(const NeuralDenoise::NeuralDenoiseModelInfo* model) const;
    std::string ReadinessLabel(const NeuralDenoise::NeuralDenoiseModelInfo* model, const NeuralDenoise::ModelAvailability& availability) const;
    void MarkDenoiseUiDirty(EditorModule* editor) const;
    void PublishRenderStatus() const;
    void RefreshPublishedRenderStatus();
    void ClearCachedResult(const char* status = nullptr);

    void RenderModelSection(float controlWidth, EditorModule* editor);
    void RenderExecutionSection(float controlWidth, EditorModule* editor);
    void RenderBlendSection(float controlWidth);
    void RenderNoiseSection(float controlWidth);
    void RenderLinearRgbSection(float controlWidth);
    void RenderPreviewSection(float controlWidth);
    void RenderTilingSection(float controlWidth);
};
