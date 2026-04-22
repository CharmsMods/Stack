#include "RenderBuffers.h"

#include "Renderer/GLHelpers.h"

RenderBuffers::~RenderBuffers() {
    Release();
}

bool RenderBuffers::EnsureStorage(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (m_Width == width && m_Height == height && m_AccumulationTexture != 0 && m_DisplayTexture != 0 && m_MomentTexture != 0) {
        return false;
    }

    Release();

    m_Width = width;
    m_Height = height;
    m_AccumulationTexture = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
    m_DisplayTexture = GLHelpers::CreateStorageTexture(width, height, GL_RGBA8);
    m_MomentTexture = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
    m_SampleCount = 0;
    return true;
}

void RenderBuffers::ResetAccumulation() {
    m_SampleCount = 0;
    ++m_ResetCount;
}

void RenderBuffers::MarkSampleRendered(bool accumulationEnabled) {
    if (accumulationEnabled) {
        ++m_SampleCount;
    } else {
        m_SampleCount = 1;
    }
}

void RenderBuffers::Release() {
    if (m_AccumulationTexture != 0) {
        glDeleteTextures(1, &m_AccumulationTexture);
        m_AccumulationTexture = 0;
    }

    if (m_DisplayTexture != 0) {
        glDeleteTextures(1, &m_DisplayTexture);
        m_DisplayTexture = 0;
    }
    if (m_MomentTexture != 0) {
        glDeleteTextures(1, &m_MomentTexture);
        m_MomentTexture = 0;
    }

    m_Width = 0;
    m_Height = 0;
    m_SampleCount = 0;
}

std::size_t RenderBuffers::GetTotalByteSize() const {
    const std::size_t pixelCount = static_cast<std::size_t>(m_Width) * static_cast<std::size_t>(m_Height);
    return pixelCount * (sizeof(float) * 8 + sizeof(unsigned char) * 4);
}
