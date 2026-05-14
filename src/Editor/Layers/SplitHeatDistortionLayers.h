#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitHeatDistortionLayerBase : public LayerBase {
public:
    ~SplitHeatDistortionLayerBase() override;

    const char* GetCategory() const override { return "Optics"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual int GetDirection() const = 0;
    virtual bool HasDirectionSelector() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    float m_Intensity = 30.0f;
    float m_Phase = 50.0f;
    float m_Scale = 20.0f;
    int m_AxisDirection = 0;
};

class HeatwaveDistortionLayer : public SplitHeatDistortionLayerBase {
public:
    const char* GetDefaultName() const override { return "Heatwave Distortion"; }
protected:
    int GetDirection() const override { return m_AxisDirection; }
    bool HasDirectionSelector() const override { return true; }
    const char* GetTypeId() const override { return "HeatwaveDistortion"; }
};

class RippleDistortionLayer : public SplitHeatDistortionLayerBase {
public:
    const char* GetDefaultName() const override { return "Ripple Distortion"; }
protected:
    int GetDirection() const override { return 2; }
    bool HasDirectionSelector() const override { return false; }
    const char* GetTypeId() const override { return "RippleDistortion"; }
};
