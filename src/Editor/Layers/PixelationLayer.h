#pragma once
#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class PixelationLayer : public LayerBase {
public:
    ~PixelationLayer() override;
    const char* GetDefaultName() const override { return "Pixelation"; }
    const char* GetCategory() const override { return "Damage"; }
    void InitializeGL() override;
    void Execute(unsigned int inputTexture,int width,int height,FullscreenQuad& quad) override;
    void RenderUI() override;
    json Serialize() const override;
    void Deserialize(const json& value) override;
private:
    unsigned int m_ShaderProgram=0;
    float m_ResScale=50.f;
};
