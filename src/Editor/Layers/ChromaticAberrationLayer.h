#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Chromatic Aberration layer adapted from the web ca/chroma pass.
// The native version exposes center coordinates directly in the inspector
// instead of relying on an in-canvas pin control.
class ChromaticAberrationLayer : public LayerBase {
public:
    ChromaticAberrationLayer();
    ~ChromaticAberrationLayer() override;

    const char* GetDefaultName() const override { return "Chromatic Aberration"; }
    const char* GetCategory() const override { return "Optics"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Amount = 0.0f;
    float m_EdgeBlur = 0.0f;
    float m_ZoomBlur = 0.0f;
    bool m_LinkFalloffToBlur = false;
    float m_CenterX = 0.5f;
    float m_CenterY = 0.5f;
    float m_Radius = 50.0f;
    float m_Falloff = 50.0f;
};
