#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitTransformLayerBase : public LayerBase {
public:
    ~SplitTransformLayerBase() override;

    const char* GetCategory() const override { return "Base"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    float m_CropLeft = 0.0f;
    float m_CropRight = 0.0f;
    float m_CropTop = 0.0f;
    float m_CropBottom = 0.0f;
    float m_Rotation = 0.0f;
    bool m_FlipH = false;
    bool m_FlipV = false;
};

class CropLayer : public SplitTransformLayerBase {
public:
    const char* GetDefaultName() const override { return "Crop"; }
    void RenderUI() override;

protected:
    const char* GetTypeId() const override { return "Crop"; }
};

class RotateLayer : public SplitTransformLayerBase {
public:
    const char* GetDefaultName() const override { return "Rotate"; }
    void RenderUI() override;

protected:
    const char* GetTypeId() const override { return "Rotate"; }
};

class FlipLayer : public SplitTransformLayerBase {
public:
    const char* GetDefaultName() const override { return "Flip"; }
    void RenderUI() override;

protected:
    const char* GetTypeId() const override { return "Flip"; }
};
