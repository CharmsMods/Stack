#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

// Noise Layer (noise.frag + composite.frag equivalent)
class NoiseLayer : public LayerBase {
public:
    NoiseLayer();
    ~NoiseLayer() override;

    const char* GetDefaultName() const override { return "Noise / Film Grain"; }
    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;

    float m_Strength = 50.0f; // 0-150 range
    int   m_Type = 1;         // Grayscale (Default)
    int   m_BlendMode = 1;    // Overlay (Default)
    
    float m_SatStrength = 1.0f;
    float m_SatImpact = 0.0f; // -100 to 100
    
    float m_ParamA = 0.5f; 
    float m_ParamB = 0.5f;
    float m_ParamC = 0.5f;
    
    float m_Scale = 1.0f;
    float m_Opacity = 0.5f;
    float m_Blurriness = 0.0f; // 0-100 range in web
    float m_Seed = 0.5f;
};
