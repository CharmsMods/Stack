#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"
#include <array>

class CellShadingLayer : public LayerBase {
public:
    CellShadingLayer();
    ~CellShadingLayer() override;

    const char* GetDefaultName() const override { return "Cell Shading"; }
    const char* GetCategory() const override { return "Stylize"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;
    bool m_ExecutionScalar = false;
    std::array<float,3> m_ExecutionLuma {.2126f,.7152f,.0722f};
    std::array<float,3> m_ExecutionComponents {1,1,1};
    std::array<int,2> m_ExecutionSourceSize {0,0};

    int m_Levels = 4;
    float m_Bias = 0.0f;
    float m_Gamma = 1.0f;
    int m_QuantMode = 0;
    int m_BandMap = 0;
    int m_EdgeMethod = 0;
    float m_EdgeStrength = 50.0f;
    float m_EdgeThickness = 1.0f;
    float m_ColorPreserve = 50.0f;
    bool m_ShowEdges = true;
};
