#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class HeatwaveLayer : public LayerBase {
public:
    HeatwaveLayer();
    ~HeatwaveLayer() override;

    const char* GetDefaultName() const override { return "Heatwave & Ripples"; }
    const char* GetCategory() const override { return "Optics"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Intensity = 30.0f;
    float m_Phase = 50.0f;
    float m_Scale = 20.0f;
    int m_Direction = 0;
};
