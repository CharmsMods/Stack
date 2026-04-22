#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Hankel Blur (Radial optical blur) Layer
class HankelBlurLayer : public LayerBase {
public:
    HankelBlurLayer();
    ~HankelBlurLayer() override;

    const char* GetDefaultName() const override { return "Hankel Blur"; }
    const char* GetCategory() const override { return "Blur"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Radius = 5.0f;
    float m_Quality = 8.0f;
    float m_Intensity = 1.0f;
};
