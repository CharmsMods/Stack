#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SceneDenoiseLayer : public LayerBase {
public:
    ~SceneDenoiseLayer() override;

    const char* GetDefaultName() const override { return "Scene Denoise"; }
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
    unsigned int m_ShaderProgram = 0;
    bool m_DenoiseEnabled = true;
    int m_Radius = 5;
    float m_LumaStrength = 0.42f;
    float m_ChromaStrength = 0.70f;
    float m_EdgeProtection = 0.70f;
    float m_ChromaEdgeProtection = 0.58f;
    float m_DetailPreservation = 0.78f;
    float m_ShadowBoost = 0.34f;
    float m_HighlightProtection = 0.55f;
    float m_Blend = 1.0f;
    bool m_AutoAnalyze = true;
    float m_AutoAmount = 0.60f;
    int m_PreviewMode = 0;
    float m_DifferenceAmount = 1.0f;
    float m_SplitPosition = 0.5f;

    void ClampParameters();
    void RenderCompactSummary() const;
    bool RenderControls(EditorModule* editor, float controlWidth);
    bool RenderPresetSection(float controlWidth);
    bool RenderAutoSection(float controlWidth);
    bool RenderCoreSection(float controlWidth);
    bool RenderProtectionSection(float controlWidth);
    bool RenderPreviewSection(float controlWidth);
    void ApplyPreset(int presetIndex);
};
