#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class ImageBreaksLayer : public LayerBase {
public:
    ImageBreaksLayer();
    ~ImageBreaksLayer() override;

    const char* GetDefaultName() const override { return "Image Breaks"; }
    const char* GetCategory() const override { return "Damage"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Columns = 10.0f;
    float m_Rows = 10.0f;
    float m_ShiftX = 0.2f;
    float m_ShiftY = 0.0f;
    float m_ShiftBlur = 0.0f;
    float m_Seed = 0.0f;
    float m_SquareDensity = 0.0f;
    float m_GridSize = 20.0f;
    float m_SquareDistance = 0.1f;
    float m_SquareBlur = 0.0f;
};
