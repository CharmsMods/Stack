#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitAdjustmentsLayerBase : public LayerBase {
public:
    ~SplitAdjustmentsLayerBase() override;

    const char* GetCategory() const override { return "Color"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void Deserialize(const json& j) override;

protected:
    unsigned int m_ShaderProgram = 0;
    float m_Brightness = 0.0f;
    float m_Contrast = 0.0f;
    float m_Saturation = 0.0f;
    float m_Warmth = 0.0f;
    float m_Sharpening = 0.0f;
    float m_SharpenThreshold = 0.0f;
};

class BrightnessLayer : public SplitAdjustmentsLayerBase {
public:
    const char* GetDefaultName() const override { return "Brightness"; }
    void RenderUI() override;
    json Serialize() const override;
};

class ContrastLayer : public SplitAdjustmentsLayerBase {
public:
    const char* GetDefaultName() const override { return "Contrast"; }
    void RenderUI() override;
    json Serialize() const override;
};

class SaturationLayer : public SplitAdjustmentsLayerBase {
public:
    const char* GetDefaultName() const override { return "Saturation"; }
    void RenderUI() override;
    json Serialize() const override;
};

class WarmthLayer : public SplitAdjustmentsLayerBase {
public:
    const char* GetDefaultName() const override { return "Warmth"; }
    void RenderUI() override;
    json Serialize() const override;
};

class SharpenLayer : public SplitAdjustmentsLayerBase {
public:
    const char* GetDefaultName() const override { return "Sharpen"; }
    void RenderUI() override;
    json Serialize() const override;
};
