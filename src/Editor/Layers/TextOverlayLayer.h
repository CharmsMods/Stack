#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Text Overlay (Compositor) Layer
class TextOverlayLayer : public LayerBase {
public:
    TextOverlayLayer();
    ~TextOverlayLayer() override;

    const char* GetDefaultName() const override { return "Text Overlay"; }
    const char* GetCategory() const override { return "Overlay"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;
    unsigned int m_OverlayTexture = 0;

    float m_PosX = 0.0f;
    float m_PosY = 0.0f;
    float m_SizeX = 100.0f;
    float m_SizeY = 100.0f;
    float m_Opacity = 1.0f;
    float m_Rotation = 0.0f;
};
