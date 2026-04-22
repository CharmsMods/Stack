#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"
#include <array>

class PaletteReconstructorLayer : public LayerBase {
public:
    PaletteReconstructorLayer();
    ~PaletteReconstructorLayer() override;

    const char* GetDefaultName() const override { return "Palette Reconstructor"; }
    const char* GetCategory() const override { return "Stylize"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    static constexpr int kMaxPaletteColors = 16;

    void ResetDefaultPalette();

    unsigned int m_ShaderProgram = 0;

    float m_Blend = 100.0f;
    float m_Smoothing = 0.0f;
    int m_SmoothingType = 0;
    int m_PaletteCount = 8;
    std::array<float, kMaxPaletteColors * 3> m_Palette {};
};
