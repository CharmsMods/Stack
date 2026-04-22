#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Expander (Canvas padding) Layer
class ExpanderLayer : public LayerBase {
public:
    ExpanderLayer();
    ~ExpanderLayer() override;

    const char* GetDefaultName() const override { return "Expander"; }
    const char* GetCategory() const override { return "Base"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Padding = 0.0f; // in pixels
    float m_FillColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // RGBA
};
