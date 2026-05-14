#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"
#include <array>

class SplitDitherLayerBase : public LayerBase {
public:
    SplitDitherLayerBase();
    ~SplitDitherLayerBase() override;

    const char* GetCategory() const override { return "Stylize"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual int GetTypeIndex() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    int m_BitDepth = 4;
    int m_PaletteSize = 8;
    float m_Strength = 100.0f;
    float m_Scale = 1.0f;
    bool m_UseGamma = false;
    bool m_UsePaletteBank = false;
    float m_Seed = 0.0f;
    std::array<float, 24> m_PaletteBank {};
};

class OrderedDither8x8Layer : public SplitDitherLayerBase {
public:
    const char* GetDefaultName() const override { return "Ordered Dither 8x8"; }
protected:
    int GetTypeIndex() const override { return 0; }
    const char* GetTypeId() const override { return "OrderedDither8x8"; }
};

class ErrorDiffusionDitherLayer : public SplitDitherLayerBase {
public:
    const char* GetDefaultName() const override { return "Error Diffusion Dither"; }
protected:
    int GetTypeIndex() const override { return 1; }
    const char* GetTypeId() const override { return "ErrorDiffusionDither"; }
};

class WhiteNoiseDitherLayer : public SplitDitherLayerBase {
public:
    const char* GetDefaultName() const override { return "White Noise Dither"; }
protected:
    int GetTypeIndex() const override { return 2; }
    const char* GetTypeId() const override { return "WhiteNoiseDither"; }
};

class OrderedDither4x4Layer : public SplitDitherLayerBase {
public:
    const char* GetDefaultName() const override { return "Ordered Dither 4x4"; }
protected:
    int GetTypeIndex() const override { return 3; }
    const char* GetTypeId() const override { return "OrderedDither4x4"; }
};

class OrderedDither2x2Layer : public SplitDitherLayerBase {
public:
    const char* GetDefaultName() const override { return "Ordered Dither 2x2"; }
protected:
    int GetTypeIndex() const override { return 4; }
    const char* GetTypeId() const override { return "OrderedDither2x2"; }
};

class InterleavedGradientDitherLayer : public SplitDitherLayerBase {
public:
    const char* GetDefaultName() const override { return "Interleaved Gradient Dither"; }
protected:
    int GetTypeIndex() const override { return 5; }
    const char* GetTypeId() const override { return "InterleavedGradientDither"; }
};
