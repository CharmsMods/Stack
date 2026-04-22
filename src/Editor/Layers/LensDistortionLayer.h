#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Lens Distortion layer adapted from the web lensDistort pass.
class LensDistortionLayer : public LayerBase {
public:
    LensDistortionLayer();
    ~LensDistortionLayer() override;

    const char* GetDefaultName() const override { return "Lens Distortion"; }
    const char* GetCategory() const override { return "Optics"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Amount = 0.0f;
    float m_Scale = 100.0f;
};
