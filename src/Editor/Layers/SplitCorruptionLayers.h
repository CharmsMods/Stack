#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitCorruptionLayerBase : public LayerBase {
public:
    ~SplitCorruptionLayerBase() override;

    const char* GetCategory() const override { return "Damage"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual int GetAlgorithm() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    float m_ResScale = 50.0f;
};

class JpegBlocksLayer : public SplitCorruptionLayerBase {
public:
    const char* GetDefaultName() const override { return "JPEG Blocks"; }
protected:
    int GetAlgorithm() const override { return 0; }
    const char* GetTypeId() const override { return "JpegBlocks"; }
};

class PixelationLayer : public SplitCorruptionLayerBase {
public:
    const char* GetDefaultName() const override { return "Pixelation"; }
protected:
    int GetAlgorithm() const override { return 1; }
    const char* GetTypeId() const override { return "Pixelation"; }
};

class ColorBleedLayer : public SplitCorruptionLayerBase {
public:
    const char* GetDefaultName() const override { return "Color Bleed"; }
protected:
    int GetAlgorithm() const override { return 2; }
    const char* GetTypeId() const override { return "ColorBleed"; }
};
