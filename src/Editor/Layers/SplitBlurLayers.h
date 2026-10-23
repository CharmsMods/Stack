#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"
#include <array>
#include <cstdint>

class SplitBlurLayerBase : public LayerBase {
public:
    ~SplitBlurLayerBase() override;

    const char* GetCategory() const override { return "Texture"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

    struct ResourceStats {
        std::uint64_t kernelUploads = 0;
        std::uint64_t scratchAllocations = 0;
        std::uint64_t scratchReuses = 0;
        std::uint64_t retainedScratchBytes = 0;
    };
    const ResourceStats& GetResourceStats() const { return m_ResourceStats; }

protected:
    virtual int GetBlurType() const = 0;
    virtual const char* GetTypeId() const = 0;

    unsigned int m_ShaderProgram = 0;
    struct Kernel {
        unsigned int texture = 0;
        float amount = 0;
        bool sigmaMode = false;
        int radius = 0;
        float sum = 1;
        std::uint64_t lastUse = 0;
    };
    // Execution is serial in the pipeline's GL context. Retain a small LRU so
    // independent blur nodes do not replace each other's prepared kernels.
    std::array<Kernel, 4> m_Kernels {};
    std::uint64_t m_KernelUseSerial = 0;
    int m_MaximumTextureSize = 0;
    std::array<int, 8> m_UniformLocations {};
    unsigned int m_ScratchTexture = 0;
    unsigned int m_ScratchFramebuffer = 0;
    int m_ScratchWidth = 0, m_ScratchHeight = 0;
    bool m_ScratchScalar = false;
    ResourceStats m_ResourceStats;
    float m_Amount = 2.0f;
    bool m_SigmaMode = false;
    float m_SigmaFraction = -1.f;
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
