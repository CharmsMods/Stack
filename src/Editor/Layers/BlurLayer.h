#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Blur Layer (blur.frag equivalent)
// Per the Shader Registry: Gaussian and box blur with masking support.
// Parameters: Amount, Type
class BlurLayer : public LayerBase {
public:
    BlurLayer();
    ~BlurLayer() override;

    const char* GetDefaultName() const override { return "Blur"; }
    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Amount = 2.0f; // radius in pixels
    int   m_Type   = 0;    // 0: Box, 1: Gaussian
};
