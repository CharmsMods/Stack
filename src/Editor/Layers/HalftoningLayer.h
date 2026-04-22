#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Halftoning (Dot/Screen print) Layer
class HalftoningLayer : public LayerBase {
public:
    HalftoningLayer();
    ~HalftoningLayer() override;

    const char* GetDefaultName() const override { return "Halftoning"; }
    const char* GetCategory() const override { return "Artistic"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Size = 4.0f;
    float m_Intensity = 1.0f;
    float m_Sharpness = 0.8f;
    int   m_Pattern = 0;   // 0=Circ, 1=Line, 2=Cross, 3=Diamond
    int   m_ColorMode = 0; // 0=Luma, 1=RGB, 2=CMY, 3=CMYK
    bool  m_Gray = false;
    bool  m_Invert = false;
};
