#pragma once

#include "LayerBase.h"
#include "NeuralDenoise/NeuralDenoiseTypes.h"

class LinearRgbNeuralDenoiseLayer : public LayerBase {
public:
    ~LinearRgbNeuralDenoiseLayer() override;

    const char* GetDefaultName() const override { return "Legacy Neural Denoise (Bypass)"; }
    const char* GetCategory() const override { return "Legacy"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;
    void RenderUI(EditorModule* editor) override;
    NodeSurfaceSpec GetNodeSurfaceSpec() const override;
    void RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    void DrawCopy(unsigned int inputTexture, FullscreenQuad& quad);

    unsigned int m_CopyProgram = 0;
    NeuralDenoise::NeuralDenoiseSettings m_Settings;
};
