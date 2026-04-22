#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Alpha Handling (Transparency protection) Layer
class AlphaHandlingLayer : public LayerBase {
public:
    AlphaHandlingLayer();
    ~AlphaHandlingLayer() override;

    const char* GetDefaultName() const override { return "Alpha Handling"; }
    const char* GetCategory() const override { return "Base"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override { (void)inputTexture; (void)width; (void)height; (void)quad; }
    void ExecuteWithSource(unsigned int inputTexture, unsigned int sourceTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    int m_Mode = 1; // 1=Protect Transparent, 2=Protect Opaque
};
