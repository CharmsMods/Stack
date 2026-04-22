#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class TiltShiftBlurLayer : public LayerBase {
public:
    TiltShiftBlurLayer();
    ~TiltShiftBlurLayer() override;

    const char* GetDefaultName() const override { return "Tilt-Shift Blur"; }
    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    void EnsureIntermediateTarget(int width, int height);

    unsigned int m_ShaderProgram = 0;
    unsigned int m_IntermediateTexture = 0;
    unsigned int m_IntermediateFbo = 0;
    int m_IntermediateWidth = 0;
    int m_IntermediateHeight = 0;

    int m_BlurType = 0;
    float m_BlurStrength = 10.0f;
    float m_FocusRadius = 30.0f;
    float m_Transition = 30.0f;
    float m_CenterX = 0.5f;
    float m_CenterY = 0.5f;
};
