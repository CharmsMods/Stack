#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// 3-Way Color Grade layer adapted from the web colorGrade pass.
// Uses white-balanced wheels where pure white means no tonal tint.
class ColorGradeLayer : public LayerBase {
public:
    ColorGradeLayer();
    ~ColorGradeLayer() override;

    const char* GetDefaultName() const override { return "3-Way Color Grade"; }
    const char* GetCategory() const override { return "Color"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Strength = 100.0f;
    float m_Shadows[3] = {1.0f, 1.0f, 1.0f};
    float m_Midtones[3] = {1.0f, 1.0f, 1.0f};
    float m_Highlights[3] = {1.0f, 1.0f, 1.0f};
};
