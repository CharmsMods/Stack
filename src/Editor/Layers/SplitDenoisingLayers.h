#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitDenoiseLayerBase : public LayerBase {
public:
    ~SplitDenoiseLayerBase() override;

    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual int GetMode() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    int m_SearchRadius = 5;
    int m_PatchRadius = 2;
    float m_H = 0.5f;
    float m_Strength = 100.0f;
};

class NonLocalMeansDenoiseLayer : public SplitDenoiseLayerBase {
public:
    const char* GetDefaultName() const override { return "Non-Local Means Denoise"; }

protected:
    int GetMode() const override { return 0; }
    const char* GetTypeId() const override { return "NonLocalMeansDenoise"; }
};

class MedianDenoiseLayer : public SplitDenoiseLayerBase {
public:
    const char* GetDefaultName() const override { return "Median Denoise"; }

protected:
    int GetMode() const override { return 1; }
    const char* GetTypeId() const override { return "MedianDenoise"; }
};

class MeanDenoiseLayer : public SplitDenoiseLayerBase {
public:
    const char* GetDefaultName() const override { return "Mean Denoise"; }

protected:
    int GetMode() const override { return 2; }
    const char* GetTypeId() const override { return "MeanDenoise"; }
};
