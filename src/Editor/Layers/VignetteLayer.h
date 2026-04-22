#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Vignette & Focus Layer (vignette.frag equivalent)
// Per the Shader Registry: Framing, darkening, and focus shaping.
// Parameters: Intensity, Radius, Softness, Color
class VignetteLayer : public LayerBase {
public:
    VignetteLayer();
    ~VignetteLayer() override;

    const char* GetDefaultName() const override { return "Vignette & Focus"; }
    const char* GetCategory() const override { return "Optics"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Intensity = 0.3f;
    float m_Radius    = 0.75f;
    float m_Softness  = 0.45f;
    float m_Color[3]  = {0.0f, 0.0f, 0.0f};
};
