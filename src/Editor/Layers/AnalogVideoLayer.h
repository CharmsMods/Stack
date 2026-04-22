#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Analog Video (VHS/CRT) Layer
class AnalogVideoLayer : public LayerBase {
public:
    AnalogVideoLayer();
    ~AnalogVideoLayer() override;

    const char* GetDefaultName() const override { return "Analog Video (VHS/CRT)"; }
    const char* GetCategory() const override { return "Damage"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Wobble = 30.0f;
    float m_Bleed = 50.0f;
    float m_Curve = 20.0f;
    float m_Noise = 40.0f;
    float m_Time = 0.0f;
};
