#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Crop / Rotate / Flip Layer (cropTransform.frag equivalent)
// Per the Shader Registry: Non-destructively crops, rotates, and mirrors the image.
// Parameters: Crop Left/Right/Top/Bottom (%), Rotation (deg), Flip H, Flip V
class CropTransformLayer : public LayerBase {
public:
    CropTransformLayer();
    ~CropTransformLayer() override;

    const char* GetDefaultName() const override { return "Crop / Rotate / Flip"; }
    const char* GetCategory() const override { return "Base"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_CropLeft   = 0.0f;  // 0-100%
    float m_CropRight  = 0.0f;
    float m_CropTop    = 0.0f;
    float m_CropBottom = 0.0f;
    float m_Rotation   = 0.0f;  // degrees
    bool  m_FlipH      = false;
    bool  m_FlipV      = false;
};
