#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class AiryBloomLayer : public LayerBase {
public:
    AiryBloomLayer();
    ~AiryBloomLayer() override;

    const char* GetDefaultName() const override { return "Airy Bloom"; }
    const char* GetCategory() const override { return "Optics"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Intensity = 0.5f;
    float m_Aperture = 8.0f;
    float m_Threshold = 0.7f;
    float m_ThresholdFade = 0.1f;
    float m_Cutoff = 0.1f;
};
