#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitBlurLayerBase : public LayerBase {
public:
    ~SplitBlurLayerBase() override;

    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual int GetBlurType() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    float m_Amount = 2.0f;
};

class BoxBlurLayer : public SplitBlurLayerBase {
public:
    const char* GetDefaultName() const override { return "Box Blur"; }

protected:
    int GetBlurType() const override { return 0; }
    const char* GetTypeId() const override { return "BoxBlur"; }
};

class GaussianBlurLayer : public SplitBlurLayerBase {
public:
    const char* GetDefaultName() const override { return "Gaussian Blur"; }

protected:
    int GetBlurType() const override { return 1; }
    const char* GetTypeId() const override { return "GaussianBlur"; }
};
