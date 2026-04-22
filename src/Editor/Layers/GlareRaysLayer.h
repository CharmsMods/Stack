#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Glare Rays (Directional bloom streaks) Layer
class GlareRaysLayer : public LayerBase {
public:
    GlareRaysLayer();
    ~GlareRaysLayer() override;

    const char* GetDefaultName() const override { return "Glare Rays"; }
    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Intensity = 50.0f;
    float m_Rays = 4.0f;
    float m_Length = 50.0f;
    float m_Blur = 20.0f;
};
