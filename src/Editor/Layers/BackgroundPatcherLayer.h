#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Background Patcher (Background removal and out-filling) Layer
class BackgroundPatcherLayer : public LayerBase {
public:
    BackgroundPatcherLayer();
    ~BackgroundPatcherLayer() override;

    const char* GetDefaultName() const override { return "Background Patcher"; }
    const char* GetCategory() const override { return "Base"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override {}
    void RenderUI(class EditorModule* editor) override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;
    
    // Masks (Placeholders for now)
    unsigned int m_FloodMask = 0;
    unsigned int m_BrushMask = 0;

    float m_TargetColor[3] = { 0.0f, 0.0f, 0.0f };
    float m_TargetAlpha = 0.0f;
    float m_Tolerance = 0.1f;
    float m_Smoothing = 0.05f;
    float m_Defringe = 0.0f;
    float m_EdgeShift = 0.0f;
    bool  m_KeepSelected = false;
    bool  m_AAEnabled = false;
    float m_AARadius = 1.0f;
    
    bool  m_PatchEnabled = false;
    bool  m_ShowDebugOverlay = false;
};
