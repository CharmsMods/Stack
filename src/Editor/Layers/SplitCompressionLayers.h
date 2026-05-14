#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

class SplitCompressionLayerBase : public LayerBase {
public:
    ~SplitCompressionLayerBase() override;

    const char* GetCategory() const override { return "Damage"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

protected:
    virtual int GetMethod() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    float m_Quality = 50.0f;
    float m_BlockSize = 8.0f;
    float m_Blend = 100.0f;
    int m_Iterations = 1;
};

class DctCompressionLayer : public SplitCompressionLayerBase {
public:
    const char* GetDefaultName() const override { return "DCT Compression"; }

protected:
    int GetMethod() const override { return 0; }
    const char* GetTypeId() const override { return "DctCompression"; }
};

class ChromaSubsampleCompressionLayer : public SplitCompressionLayerBase {
public:
    const char* GetDefaultName() const override { return "Chroma Subsample Compression"; }

protected:
    int GetMethod() const override { return 1; }
    const char* GetTypeId() const override { return "ChromaSubsampleCompression"; }
};

class WaveletCompressionLayer : public SplitCompressionLayerBase {
public:
    const char* GetDefaultName() const override { return "Wavelet Compression"; }

protected:
    int GetMethod() const override { return 2; }
    const char* GetTypeId() const override { return "WaveletCompression"; }
};
