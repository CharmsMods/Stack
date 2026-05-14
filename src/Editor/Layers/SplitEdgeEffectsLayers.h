#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitEdgeEffectsLayerBase : public LayerBase {
public:
    ~SplitEdgeEffectsLayerBase() override;

    const char* GetCategory() const override { return "Stylize"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual int GetMode() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    float m_Blend = 100.0f;
    float m_Strength = 500.0f;
    float m_Tolerance = 10.0f;
    float m_ForegroundSaturation = 150.0f;
    float m_BackgroundSaturation = 0.0f;
    float m_BloomSpread = 10.0f;
    float m_BloomSmoothness = 50.0f;
};

class EdgeOverlayLayer : public SplitEdgeEffectsLayerBase {
public:
    const char* GetDefaultName() const override { return "Edge Overlay"; }
protected:
    int GetMode() const override { return 0; }
    const char* GetTypeId() const override { return "EdgeOverlay"; }
};

class EdgeSaturationMaskLayer : public SplitEdgeEffectsLayerBase {
public:
    const char* GetDefaultName() const override { return "Edge Saturation Mask"; }
protected:
    int GetMode() const override { return 1; }
    const char* GetTypeId() const override { return "EdgeSaturationMask"; }
};
