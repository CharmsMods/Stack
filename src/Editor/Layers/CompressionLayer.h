#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class CompressionLayer : public LayerBase {
public:
    CompressionLayer();
    ~CompressionLayer() override;

    const char* GetDefaultName() const override { return "Compression"; }
    const char* GetCategory() const override { return "Damage"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    int m_Method = 0;
    float m_Quality = 50.0f;
    float m_BlockSize = 8.0f;
    float m_Blend = 100.0f;
    int m_Iterations = 1;
};
