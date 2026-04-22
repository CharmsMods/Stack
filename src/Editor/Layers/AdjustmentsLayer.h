#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Adjustments Layer (adjust.frag equivalent)
// Per the Shader Registry: Core tonal and sharpening controls.
// Parameters: Brightness, Contrast, Saturation, Warmth, Sharpening, Sharpen Threshold
class AdjustmentsLayer : public LayerBase {
public:
    AdjustmentsLayer();
    ~AdjustmentsLayer() override;

    const char* GetDefaultName() const override { return "Adjustments"; }
    const char* GetCategory() const override { return "Color"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    // Controllable engine parameters from the registry
    float m_Brightness = 0.0f;    // -1.0 to 1.0
    float m_Contrast   = 0.0f;    // -1.0 to 1.0
    float m_Saturation = 0.0f;    // -1.0 to 1.0
    float m_Warmth     = 0.0f;    // -1.0 to 1.0
    float m_Sharpening = 0.0f;    // 0.0 to 1.0
    float m_SharpenThreshold = 0.0f; // 0.0 to 1.0
};
