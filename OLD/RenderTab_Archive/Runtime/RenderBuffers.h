#pragma once

#include <cstddef>

class RenderBuffers {
public:
    RenderBuffers() = default;
    ~RenderBuffers();

    bool EnsureStorage(int width, int height);
    void ResetAccumulation();
    void MarkSampleRendered(bool accumulationEnabled);
    void Release();

    unsigned int GetAccumulationTexture() const { return m_AccumulationTexture; }
    unsigned int GetDisplayTexture() const { return m_DisplayTexture; }
    unsigned int GetMomentTexture() const { return m_MomentTexture; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    unsigned int GetSampleCount() const { return m_SampleCount; }
    unsigned int GetResetCount() const { return m_ResetCount; }
    std::size_t GetTotalByteSize() const;

private:
    unsigned int m_AccumulationTexture = 0;
    unsigned int m_DisplayTexture = 0;
    unsigned int m_MomentTexture = 0;
    int m_Width = 0;
    int m_Height = 0;
    unsigned int m_SampleCount = 0;
    unsigned int m_ResetCount = 0;
};
