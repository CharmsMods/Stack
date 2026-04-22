#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Bilateral Filter (Edge-aware smoothing) Layer
class BilateralFilterLayer : public LayerBase {
public:
    BilateralFilterLayer();
    ~BilateralFilterLayer() override;

    const char* GetDefaultName() const override { return "Bilateral Filter"; }
    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    int   m_Radius = 3;
    float m_SigmaCol = 0.1f;
    float m_SigmaSpace = 3.0f;
    int   m_Kernel = 0;   // 0=Gauss, 1=Box
    int   m_EdgeMode = 0; // 0=Luma, 1=RGB
};
