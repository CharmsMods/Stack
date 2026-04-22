#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// HDR Emulation layer adapted from the standalone web hdr pass.
// This keeps the current web behavior, even though the effect is a tonal adaptation
// rather than a physically correct HDR reconstruction.
class HDRLayer : public LayerBase {
public:
    HDRLayer();
    ~HDRLayer() override;

    const char* GetDefaultName() const override { return "HDR Emulation"; }
    const char* GetCategory() const override { return "Color"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Tolerance = 50.0f;
    float m_Amount = 0.0f;
};
