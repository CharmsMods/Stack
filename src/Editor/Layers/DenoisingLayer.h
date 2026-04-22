#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Denoising (NLM, Median, Mean) Layer
class DenoisingLayer : public LayerBase {
public:
    DenoisingLayer();
    ~DenoisingLayer() override;

    const char* GetDefaultName() const override { return "Denoising"; }
    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    int   m_Mode = 0;           // 0=NLM, 1=Median, 2=Mean
    int   m_SearchRadius = 5;
    int   m_PatchRadius = 2;
    float m_H = 0.5f;           // filter strength
    float m_Strength = 100.0f;  // 0-100 blend
};
