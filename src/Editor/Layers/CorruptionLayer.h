#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Corruption (Digital breakup/glitch) Layer
class CorruptionLayer : public LayerBase {
public:
    CorruptionLayer();
    ~CorruptionLayer() override;

    const char* GetDefaultName() const override { return "Corruption"; }
    const char* GetCategory() const override { return "Damage"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    int   m_Algorithm = 0;   // 0=JPEG, 1=Pixelation, 2=Color Bleed
    float m_ResScale = 50.0f;
};
