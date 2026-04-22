#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class EdgeEffectsLayer : public LayerBase {
public:
    EdgeEffectsLayer();
    ~EdgeEffectsLayer() override;

    const char* GetDefaultName() const override { return "Edge Effects"; }
    const char* GetCategory() const override { return "Stylize"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Blend = 100.0f;
    int m_Mode = 0;
    float m_Strength = 500.0f;
    float m_Tolerance = 10.0f;
    float m_ForegroundSaturation = 150.0f;
    float m_BackgroundSaturation = 0.0f;
    float m_BloomSpread = 10.0f;
    float m_BloomSmoothness = 50.0f;
};
