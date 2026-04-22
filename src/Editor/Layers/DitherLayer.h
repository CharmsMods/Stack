#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"
#include <array>

class DitherLayer : public LayerBase {
public:
    DitherLayer();
    ~DitherLayer() override;

    const char* GetDefaultName() const override { return "Dithering"; }
    const char* GetCategory() const override { return "Stylize"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    int m_BitDepth = 4;
    int m_PaletteSize = 8;
    float m_Strength = 100.0f;
    float m_Scale = 1.0f;
    int m_Type = 0;
    bool m_UseGamma = false;
    bool m_UsePaletteBank = false;
    float m_Seed = 0.0f;
    std::array<float, 24> m_PaletteBank {};
};
