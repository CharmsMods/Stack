#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct ScopedFramebufferState {
    GLint framebuffer = 0;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint readBuffer = 0;
    GLint drawBuffer = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };

    explicit ScopedFramebufferState(bool captureViewport = false) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
        if (captureViewport) {
            glGetIntegerv(GL_VIEWPORT, viewport);
        }
    }

    void Restore(bool restoreViewport = false) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glReadBuffer(static_cast<GLenum>(readBuffer));
        glDrawBuffer(static_cast<GLenum>(drawBuffer));
        if (restoreViewport) {
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        }
    }
};

void FlipRgbaRows(std::vector<unsigned char>& pixels, int width, int height) {
    if (width <= 0 || height <= 1 || pixels.empty()) {
        return;
    }
    const int rowSize = width * 4;
    std::vector<unsigned char> tempRow(static_cast<std::size_t>(rowSize));
    for (int y = 0; y < height / 2; y++) {
        unsigned char* row1 = &pixels[static_cast<std::size_t>(y * rowSize)];
        unsigned char* row2 = &pixels[static_cast<std::size_t>((height - 1 - y) * rowSize)];
        std::memcpy(tempRow.data(), row1, static_cast<std::size_t>(rowSize));
        std::memcpy(row1, row2, static_cast<std::size_t>(rowSize));
        std::memcpy(row2, tempRow.data(), static_cast<std::size_t>(rowSize));
    }
}

std::vector<unsigned char> ReadTexturePixelsRgba8(
    unsigned int texture,
    int sourceWidth,
    int sourceHeight,
    int& outW,
    int& outH,
    int maxDimension,
    const char* context) {
    outW = 0;
    outH = 0;
    if (texture == 0 || sourceWidth <= 0 || sourceHeight <= 0) {
        return {};
    }

    const int targetMax = maxDimension > 0 ? std::max(1, maxDimension) : std::max(sourceWidth, sourceHeight);
    const float scale = std::min(
        1.0f,
        static_cast<float>(targetMax) / static_cast<float>(std::max(sourceWidth, sourceHeight)));
    outW = std::max(1, static_cast<int>(std::round(static_cast<float>(sourceWidth) * scale)));
    outH = std::max(1, static_cast<int>(std::round(static_cast<float>(sourceHeight) * scale)));

    std::vector<unsigned char> pixels(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u);

    GLint prevReadFBO = 0;
    GLint prevDrawFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int readFBO = 0;
    unsigned int targetFBO = 0;
    unsigned int targetTex = 0;

    glGenFramebuffers(1, &readFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (outW != sourceWidth || outH != sourceHeight) {
        targetTex = GLHelpers::CreateEmptyTexture(outW, outH);
        glGenFramebuffers(1, &targetFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
            glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[RenderPipeline] " << context << " FBO incomplete during downsampled readback." << std::endl;
            pixels.clear();
        } else {
            while (glGetError() != GL_NO_ERROR) {}
            glBlitFramebuffer(
                0, 0, sourceWidth, sourceHeight,
                0, 0, outW, outH,
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
            if (GLenum err = glGetError(); err != GL_NO_ERROR) {
                std::cerr << "[RenderPipeline] glBlitFramebuffer error in " << context << ": " << err << std::endl;
                pixels.clear();
            }
        }

        if (!pixels.empty()) {
            glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, readFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    if (!pixels.empty()) {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[RenderPipeline] " << context << " FBO incomplete." << std::endl;
            pixels.clear();
        } else {
            while (glGetError() != GL_NO_ERROR) {}
            glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (GLenum err = glGetError(); err != GL_NO_ERROR) {
                std::cerr << "[RenderPipeline] glReadPixels error in " << context << ": " << err << std::endl;
                pixels.clear();
            }
        }
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    if (readFBO != 0) {
        glDeleteFramebuffers(1, &readFBO);
    }
    if (targetFBO != 0) {
        glDeleteFramebuffers(1, &targetFBO);
    }
    if (targetTex != 0) {
        glDeleteTextures(1, &targetTex);
    }

    if (pixels.empty()) {
        outW = 0;
        outH = 0;
        return {};
    }

    FlipRgbaRows(pixels, outW, outH);
    return pixels;
}
} // namespace
std::vector<unsigned char> RenderPipeline::GetOutputPixels(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    std::vector<unsigned char> pixels(m_Width * m_Height * 4);
    const ScopedFramebufferState savedState;

    // Create a temporary FBO for reading
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderPipeline] Warning: GetOutputPixels FBO incomplete (status " << fboStatus << "). Texture: " << m_OutputTexture << ". Attempting read anyway." << std::endl;
    }

    // Clear previous errors
    while (glGetError() != GL_NO_ERROR) {}

    glReadPixels(0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[RenderPipeline] glReadPixels error in GetOutputPixels: " << err << std::endl;
        savedState.Restore();
        glDeleteFramebuffers(1, &tempFBO);

        // Before our strict error checking, the code would just return the zeroed pixels array
        // on failure. Return the zeroed array instead of empty {} so downstream doesn't completely abort.
        return pixels;
    }

    // Validate that we didn't just read empty pixels (if alpha is completely 0 for the entire image)
    bool hasData = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] > 0) {
            hasData = true;
            break;
        }
    }
    if (!hasData) {
        std::cerr << "[RenderPipeline] Warning: GetOutputPixels read completely transparent image." << std::endl;
    }

    // Flip vertically
    int rowSize = m_Width * 4;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; y++) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    savedState.Restore();
    glDeleteFramebuffers(1, &tempFBO);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetOutputPixels(int& outW, int& outH, int maxDimension) {
    if (maxDimension <= 0 || maxDimension >= std::max(m_Width, m_Height)) {
        return GetOutputPixels(outW, outH);
    }
    return ReadTexturePixelsRgba8(
        m_OutputTexture,
        m_Width,
        m_Height,
        outW,
        outH,
        maxDimension,
        "GetOutputPixels(maxDimension)");
}

std::vector<unsigned char> RenderPipeline::GetRawDevelopmentLocalRangeOverlayPixels(int& outW, int& outH) {
    return ReadTexturePixelsRgba8(
        m_RawDevelopmentLocalRangeOverlayTexture,
        m_RawDevelopmentLocalRangeOverlayWidth,
        m_RawDevelopmentLocalRangeOverlayHeight,
        outW,
        outH,
        0,
        "GetRawDevelopmentLocalRangeOverlayPixels");
}

bool RenderPipeline::CaptureRawDevelopmentLocalRangeTargetSample(
    unsigned int texture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange) {
    m_RawDevelopmentLocalRangeTargetSampleValid = false;
    if (!m_RawDevelopmentLocalRangeTargetSampleRequested ||
        texture == 0 ||
        m_Width <= 0 ||
        m_Height <= 0) {
        return false;
    }

    constexpr int kPatchRadius = 4;
    const float sampleU = std::clamp(m_RawDevelopmentLocalRangeTargetSampleRequestU, 0.0f, 1.0f);
    const float sampleV = std::clamp(m_RawDevelopmentLocalRangeTargetSampleRequestV, 0.0f, 1.0f);
    const int centerX = std::clamp(
        static_cast<int>(std::round(sampleU * static_cast<float>(std::max(0, m_Width - 1)))),
        0,
        m_Width - 1);
    const int centerDisplayY = std::clamp(
        static_cast<int>(std::round(sampleV * static_cast<float>(std::max(0, m_Height - 1)))),
        0,
        m_Height - 1);
    const int centerReadY = std::clamp(m_Height - 1 - centerDisplayY, 0, m_Height - 1);
    const int minX = std::max(0, centerX - kPatchRadius);
    const int maxX = std::min(m_Width - 1, centerX + kPatchRadius);
    const int minY = std::max(0, centerReadY - kPatchRadius);
    const int maxY = std::min(m_Height - 1, centerReadY + kPatchRadius);
    const int readW = maxX - minX + 1;
    const int readH = maxY - minY + 1;
    if (readW <= 0 || readH <= 0) {
        return false;
    }

    std::vector<float> rgba(static_cast<std::size_t>(readW) * static_cast<std::size_t>(readH) * 4u, 0.0f);
    const ScopedFramebufferState savedState;

    unsigned int readFBO = 0;
    glGenFramebuffers(1, &readFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    bool readOk = false;
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(minX, minY, readW, readH, GL_RGBA, GL_FLOAT, rgba.data());
        readOk = glGetError() == GL_NO_ERROR;
    }

    savedState.Restore();
    if (readFBO != 0) {
        glDeleteFramebuffers(1, &readFBO);
    }
    if (!readOk) {
        return false;
    }

    struct TargetSamplePixel {
        float luma = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };
    std::vector<TargetSamplePixel> samples;
    samples.reserve(static_cast<std::size_t>(readW) * static_cast<std::size_t>(readH));
    for (std::size_t i = 0; i + 2 < rgba.size(); i += 4) {
        const float r = rgba[i + 0];
        const float g = rgba[i + 1];
        const float b = rgba[i + 2];
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
            continue;
        }
        const float luma = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
        if (std::isfinite(luma)) {
            samples.push_back({
                luma,
                std::max(0.0f, r),
                std::max(0.0f, g),
                std::max(0.0f, b)
            });
        }
    }
    if (samples.empty()) {
        return false;
    }

    std::sort(samples.begin(), samples.end(), [](const TargetSamplePixel& a, const TargetSamplePixel& b) {
        return a.luma < b.luma;
    });
    float robustLuma = 0.0f;
    float robustR = 0.0f;
    float robustG = 0.0f;
    float robustB = 0.0f;
    if (samples.size() < 5) {
        const TargetSamplePixel& median = samples[samples.size() / 2u];
        robustLuma = median.luma;
        robustR = median.r;
        robustG = median.g;
        robustB = median.b;
    } else {
        const std::size_t trim = std::max<std::size_t>(1, samples.size() / 5u);
        const std::size_t begin = std::min(trim, samples.size() - 1u);
        const std::size_t end = std::max(begin + 1u, samples.size() - trim);
        double lumaSum = 0.0;
        double rSum = 0.0;
        double gSum = 0.0;
        double bSum = 0.0;
        for (std::size_t i = begin; i < end; ++i) {
            lumaSum += static_cast<double>(samples[i].luma);
            rSum += static_cast<double>(samples[i].r);
            gSum += static_cast<double>(samples[i].g);
            bSum += static_cast<double>(samples[i].b);
        }
        const double sampleCount = static_cast<double>(end - begin);
        robustLuma = static_cast<float>(lumaSum / sampleCount);
        robustR = static_cast<float>(rSum / sampleCount);
        robustG = static_cast<float>(gSum / sampleCount);
        robustB = static_cast<float>(bSum / sampleCount);
    }

    const float middleGrey = std::clamp(localRange.middleGrey, 0.01f, 1.0f);
    m_RawDevelopmentLocalRangeTargetSampleValid = true;
    m_RawDevelopmentLocalRangeTargetSampleSceneLuma = robustLuma;
    m_RawDevelopmentLocalRangeTargetSampleSceneR = robustR;
    m_RawDevelopmentLocalRangeTargetSampleSceneG = robustG;
    m_RawDevelopmentLocalRangeTargetSampleSceneB = robustB;
    m_RawDevelopmentLocalRangeTargetSampleSceneEv =
        std::log2(std::max(robustLuma, 0.00000001f) / middleGrey);
    m_RawDevelopmentLocalRangeTargetSampleU = sampleU;
    m_RawDevelopmentLocalRangeTargetSampleV = sampleV;
    return true;
}

bool RenderPipeline::GetRawDevelopmentLocalRangeTargetSample(
    float& outSceneEv,
    float& outSceneLuma,
    float& outU,
    float& outV,
    std::array<float, 3>* outSceneRgb) const {
    outSceneEv = m_RawDevelopmentLocalRangeTargetSampleSceneEv;
    outSceneLuma = m_RawDevelopmentLocalRangeTargetSampleSceneLuma;
    outU = m_RawDevelopmentLocalRangeTargetSampleU;
    outV = m_RawDevelopmentLocalRangeTargetSampleV;
    if (outSceneRgb) {
        *outSceneRgb = {
            m_RawDevelopmentLocalRangeTargetSampleSceneR,
            m_RawDevelopmentLocalRangeTargetSampleSceneG,
            m_RawDevelopmentLocalRangeTargetSampleSceneB
        };
    }
    return m_RawDevelopmentLocalRangeTargetSampleValid;
}

std::vector<unsigned char> RenderPipeline::GetCachedGraphImagePixels(
    int nodeId,
    const std::string& socketId,
    int& outW,
    int& outH) const {
    outW = 0;
    outH = 0;
    if (nodeId <= 0 || socketId.empty()) {
        return {};
    }

    const std::string key = std::to_string(nodeId) + ":" + socketId;
    const auto cached = m_GraphImageCache.find(key);
    if (cached == m_GraphImageCache.end() || cached->second.texture == 0) {
        return {};
    }

    outW = cached->second.width > 0 ? cached->second.width : m_Width;
    outH = cached->second.height > 0 ? cached->second.height : m_Height;
    if (outW <= 0 || outH <= 0) {
        return {};
    }

    std::vector<unsigned char> pixels(outW * outH * 4);
    const ScopedFramebufferState savedState;

    unsigned int tempFBO = 0;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cached->second.texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        savedState.Restore();
        glDeleteFramebuffers(1, &tempFBO);
        return {};
    }

    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        savedState.Restore();
        glDeleteFramebuffers(1, &tempFBO);
        return {};
    }

    const int rowSize = outW * 4;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < outH / 2; y++) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(outH - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    savedState.Restore();
    glDeleteFramebuffers(1, &tempFBO);
    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetCachedGraphImagePixels(
    int nodeId,
    const std::string& socketId,
    int& outW,
    int& outH,
    int maxDimension) const {
    outW = 0;
    outH = 0;
    if (maxDimension <= 0) {
        return GetCachedGraphImagePixels(nodeId, socketId, outW, outH);
    }
    if (nodeId <= 0 || socketId.empty()) {
        return {};
    }

    const std::string key = std::to_string(nodeId) + ":" + socketId;
    const auto cached = m_GraphImageCache.find(key);
    if (cached == m_GraphImageCache.end() || cached->second.texture == 0) {
        return {};
    }

    const int sourceW = cached->second.width > 0 ? cached->second.width : m_Width;
    const int sourceH = cached->second.height > 0 ? cached->second.height : m_Height;
    if (sourceW <= 0 || sourceH <= 0) {
        return {};
    }
    if (maxDimension >= std::max(sourceW, sourceH)) {
        return GetCachedGraphImagePixels(nodeId, socketId, outW, outH);
    }

    return ReadTexturePixelsRgba8(
        cached->second.texture,
        sourceW,
        sourceH,
        outW,
        outH,
        maxDimension,
        "GetCachedGraphImagePixels(maxDimension)");
}

bool RenderPipeline::WasGraphImageCacheHit(int nodeId, const std::string& socketId) const {
    if (nodeId <= 0 || socketId.empty()) {
        return false;
    }
    const std::string key = std::to_string(nodeId) + ":" + socketId;
    return m_LastGraphImageCacheHits.find(key) != m_LastGraphImageCacheHits.end();
}

std::vector<unsigned char> RenderPipeline::GetCompareSourcePixels(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    unsigned int compareTex = GetCompareSourceTexture();
    if (compareTex == 0 || m_Width == 0 || m_Height == 0) return {};

    std::vector<unsigned char> pixels(m_Width * m_Height * 4);
    const ScopedFramebufferState savedState;

    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, compareTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderPipeline] GetCompareSourcePixels FBO incomplete. Texture: " << compareTex << std::endl;
        savedState.Restore();
        glDeleteFramebuffers(1, &tempFBO);
        return {};
    }

    while (glGetError() != GL_NO_ERROR) {}

    glReadPixels(0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[RenderPipeline] glReadPixels error in GetCompareSourcePixels: " << err << std::endl;
        savedState.Restore();
        glDeleteFramebuffers(1, &tempFBO);
        return {};
    }

    int rowSize = m_Width * 4;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; y++) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    savedState.Restore();
    glDeleteFramebuffers(1, &tempFBO);

    return pixels;
}

bool RenderPipeline::SampleOutputPixel(float u, float v, std::array<float, 4>& outRgba) const {
    outRgba = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (m_OutputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return false;
    }

    const float clampedU = std::clamp(u, 0.0f, 1.0f);
    const float clampedV = std::clamp(v, 0.0f, 1.0f);
    const int px = std::clamp(static_cast<int>(std::round(clampedU * static_cast<float>(std::max(0, m_Width - 1)))), 0, m_Width - 1);
    const int py = std::clamp(static_cast<int>(std::round(clampedV * static_cast<float>(std::max(0, m_Height - 1)))), 0, m_Height - 1);
    const int readY = std::clamp(m_Height - 1 - py, 0, m_Height - 1);

    const ScopedFramebufferState savedState;

    unsigned int readFBO = 0;
    glGenFramebuffers(1, &readFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    bool success = false;
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(px, readY, 1, 1, GL_RGBA, GL_FLOAT, outRgba.data());
        success = glGetError() == GL_NO_ERROR;
    }

    savedState.Restore();
    if (readFBO != 0) {
        glDeleteFramebuffers(1, &readFBO);
    }
    return success;
}

RenderTextureStats RenderPipeline::ReadTextureStats(unsigned int texture, int width, int height, const char* context) {
    RenderTextureStats stats;
    if (texture == 0 || width <= 0 || height <= 0) {
        return stats;
    }

    constexpr int kMaxProbeEdge = 512;
    const float scale = std::min(
        1.0f,
        static_cast<float>(kMaxProbeEdge) / static_cast<float>(std::max(width, height)));
    const int probeW = std::max(1, static_cast<int>(std::round(static_cast<float>(width) * scale)));
    const int probeH = std::max(1, static_cast<int>(std::round(static_cast<float>(height) * scale)));

    const ScopedFramebufferState savedState;

    unsigned int readFBO = 0;
    unsigned int probeFBO = 0;
    unsigned int probeTex = 0;
    glGenFramebuffers(1, &readFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (probeW == width && probeH == height) {
        glBindFramebuffer(GL_FRAMEBUFFER, readFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        probeTex = GLHelpers::CreateEmptyTexture(probeW, probeH);
        glGenFramebuffers(1, &probeFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probeFBO);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(
            0, 0, width, height,
            0, 0, probeW, probeH,
            GL_COLOR_BUFFER_BIT,
            GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, probeFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    std::vector<float> pixels(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH) * 4u, 0.0f);

    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(0, 0, probeW, probeH, GL_RGBA, GL_FLOAT, pixels.data());
        if (GLenum err = glGetError(); err != GL_NO_ERROR) {
            std::cerr << "[RenderPipeline] glReadPixels error in " << (context ? context : "ReadTextureStats") << ": " << err << std::endl;
            pixels.clear();
        }
    } else {
        std::cerr << "[RenderPipeline] " << (context ? context : "ReadTextureStats") << " FBO incomplete." << std::endl;
        pixels.clear();
    }

    savedState.Restore();
    if (probeFBO != 0) {
        glDeleteFramebuffers(1, &probeFBO);
    }
    if (readFBO != 0) {
        glDeleteFramebuffers(1, &readFBO);
    }
    if (probeTex != 0) {
        glDeleteTextures(1, &probeTex);
    }

    if (pixels.empty()) {
        return stats;
    }

    stats.valid = true;
    stats.minRgb = std::numeric_limits<float>::max();
    stats.maxRgb = -std::numeric_limits<float>::max();
    stats.minLuma = std::numeric_limits<float>::max();
    stats.maxLuma = -std::numeric_limits<float>::max();

    std::vector<float> lumas;
    lumas.reserve(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH));
    float logLumaSum = 0.0f;
    int hdrPixels = 0;
    int displayEdgePixels = 0;
    int validPixels = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        float r = pixels[i + 0];
        float g = pixels[i + 1];
        float b = pixels[i + 2];
        if (!std::isfinite(r)) r = 0.0f;
        if (!std::isfinite(g)) g = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        const float minChannel = std::min({ r, g, b });
        const float maxChannel = std::max({ r, g, b });
        const float luma = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
        stats.minRgb = std::min(stats.minRgb, minChannel);
        stats.maxRgb = std::max(stats.maxRgb, maxChannel);
        stats.minLuma = std::min(stats.minLuma, luma);
        stats.maxLuma = std::max(stats.maxLuma, luma);
        lumas.push_back(luma);
        logLumaSum += std::log(1.0e-8f + luma);
        if (maxChannel > 1.0f) {
            ++hdrPixels;
        }
        if (maxChannel >= 0.999f || minChannel <= 0.001f) {
            ++displayEdgePixels;
        }
        ++validPixels;
    }

    if (validPixels <= 0 || lumas.empty()) {
        stats.valid = false;
        return stats;
    }

    std::sort(lumas.begin(), lumas.end());
    auto percentile = [&](float p) {
        const float clamped = std::clamp(p, 0.0f, 1.0f);
        const std::size_t index = static_cast<std::size_t>(
            std::round(clamped * static_cast<float>(lumas.size() - 1)));
        return lumas[index];
    };
    stats.p001Luma = percentile(0.001f);
    stats.p01Luma = percentile(0.01f);
    stats.p05Luma = percentile(0.05f);
    stats.p50Luma = percentile(0.50f);
    stats.p95Luma = percentile(0.95f);
    stats.p99Luma = percentile(0.99f);
    stats.p999Luma = percentile(0.999f);
    stats.logAverageLuma = std::exp(logLumaSum / static_cast<float>(validPixels));
    stats.dynamicRangeEv =
        std::log2(std::max(1.0e-8f, stats.p99Luma)) -
        std::log2(std::max(1.0e-8f, stats.p01Luma));
    stats.validPixelPercent = 100.0f;
    stats.hdrPixelPercent = 100.0f * static_cast<float>(hdrPixels) / static_cast<float>(validPixels);
    stats.displayClipPercent = 100.0f * static_cast<float>(displayEdgePixels) / static_cast<float>(validPixels);
    return stats;
}

Stack::RawAutoBase::LocalSuggestionAnalysisImage RenderPipeline::ReadLocalSuggestionAnalysisImage(
    unsigned int texture,
    int width,
    int height,
    int maxDimension,
    const char* context) {
    Stack::RawAutoBase::LocalSuggestionAnalysisImage image;
    image.sceneLinearBeforeLocalRange = true;
    if (texture == 0 || width <= 0 || height <= 0) {
        image.statusMessage = "Local suggestions need a rendered scene-linear RAW frame.";
        return image;
    }

    const int targetMax = maxDimension > 0
        ? std::max(1, maxDimension)
        : std::max(width, height);
    const float scale = std::min(
        1.0f,
        static_cast<float>(targetMax) / static_cast<float>(std::max(width, height)));
    const int probeW = std::max(1, static_cast<int>(std::round(static_cast<float>(width) * scale)));
    const int probeH = std::max(1, static_cast<int>(std::round(static_cast<float>(height) * scale)));

    const ScopedFramebufferState savedState(true);

    unsigned int readFBO = 0;
    unsigned int probeFBO = 0;
    unsigned int probeTex = 0;
    glGenFramebuffers(1, &readFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    bool framebufferReady = true;
    if (probeW == width && probeH == height) {
        glBindFramebuffer(GL_FRAMEBUFFER, readFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        probeTex = GLHelpers::CreateEmptyTexture(probeW, probeH);
        if (probeTex == 0) {
            framebufferReady = false;
            image.statusMessage = "Local suggestion readback could not allocate a scene-linear probe texture.";
        } else {
            glGenFramebuffers(1, &probeFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probeFBO);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTex, 0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glBlitFramebuffer(
                0, 0, width, height,
                0, 0, probeW, probeH,
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, probeFBO);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
    }

    std::vector<float> rgba;
    if (framebufferReady && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        rgba.assign(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH) * 4u, 0.0f);
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(0, 0, probeW, probeH, GL_RGBA, GL_FLOAT, rgba.data());
        if (GLenum err = glGetError(); err != GL_NO_ERROR) {
            std::cerr << "[RenderPipeline] glReadPixels error in "
                      << (context ? context : "ReadLocalSuggestionAnalysisImage")
                      << ": " << err << std::endl;
            rgba.clear();
            image.statusMessage = "Local suggestion readback failed.";
        }
    } else if (framebufferReady) {
        std::cerr << "[RenderPipeline] "
                  << (context ? context : "ReadLocalSuggestionAnalysisImage")
                  << " FBO incomplete." << std::endl;
        image.statusMessage = "Local suggestion readback framebuffer was incomplete.";
    }

    savedState.Restore(true);
    if (probeFBO != 0) {
        glDeleteFramebuffers(1, &probeFBO);
    }
    if (readFBO != 0) {
        glDeleteFramebuffers(1, &readFBO);
    }
    if (probeTex != 0) {
        glDeleteTextures(1, &probeTex);
    }

    if (rgba.empty()) {
        if (image.statusMessage.empty()) {
            image.statusMessage = "Local suggestion readback produced no pixels.";
        }
        return image;
    }

    image.valid = true;
    image.width = probeW;
    image.height = probeH;
    image.pixels.resize(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH));
    image.statusMessage = "Scene-linear pre-Local-Range analysis image ready.";
    for (int y = 0; y < probeH; ++y) {
        const int sourceY = probeH - 1 - y;
        for (int x = 0; x < probeW; ++x) {
            const std::size_t dst = static_cast<std::size_t>(y * probeW + x);
            const std::size_t src =
                (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(probeW) +
                 static_cast<std::size_t>(x)) * 4u;
            Stack::RawAutoBase::LocalSuggestionPixel& pixel = image.pixels[dst];
            pixel.r = rgba[src + 0];
            pixel.g = rgba[src + 1];
            pixel.b = rgba[src + 2];
            pixel.valid =
                std::isfinite(pixel.r) &&
                std::isfinite(pixel.g) &&
                std::isfinite(pixel.b);
        }
    }
    return image;
}

RenderTextureStats RenderPipeline::GetOutputTextureStats() {
    return ReadTextureStats(m_OutputTexture, m_Width, m_Height, "GetOutputTextureStats");
}

std::vector<unsigned char> RenderPipeline::GetScopesPixels(int& outW, int& outH) {
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    // Target a small size for analysis efficiency
    outW = 256;
    outH = 256;
    if (m_Width < outW) outW = m_Width;
    if (m_Height < outH) outH = m_Height;

    std::vector<unsigned char> pixels(outW * outH * 4);

    // Create a temporary FBO/Texture for downsampling
    unsigned int tempTex = GLHelpers::CreateEmptyTexture(outW, outH);
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tempTex, 0);

    // Blit from current output to small target (GPU downsample)
    GLint prevReadFBO, prevDrawFBO;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int srcFBO;
    glGenFramebuffers(1, &srcFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempFBO);
    glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, outW, outH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Read small pixels
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderPipeline] GetScopesPixels FBO incomplete." << std::endl;
        pixels.clear();
    } else {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (GLenum err = glGetError(); err != GL_NO_ERROR) {
            std::cerr << "[RenderPipeline] glReadPixels error in GetScopesPixels: " << err << std::endl;
            pixels.clear();
        }
    }

    // Cleanup
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &tempFBO);
    glDeleteTextures(1, &tempTex);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetPreviewPixels(int& outW, int& outH, int maxDimension) {
    outW = 0;
    outH = 0;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) {
        return {};
    }

    const int targetMax = std::max(1, maxDimension);
    const float scale = std::min(
        1.0f,
        static_cast<float>(targetMax) / static_cast<float>(std::max(m_Width, m_Height)));
    outW = std::max(1, static_cast<int>(std::round(static_cast<float>(m_Width) * scale)));
    outH = std::max(1, static_cast<int>(std::round(static_cast<float>(m_Height) * scale)));

    std::vector<unsigned char> pixels(static_cast<std::size_t>(outW * outH * 4));

    GLint prevReadFBO = 0;
    GLint prevDrawFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int tempTex = GLHelpers::CreateEmptyTexture(outW, outH);
    unsigned int tempFBO = 0;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tempTex, 0);

    unsigned int srcFBO = 0;
    glGenFramebuffers(1, &srcFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempFBO);
    glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, outW, outH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderPipeline] GetPreviewPixels FBO incomplete." << std::endl;
        pixels.clear();
    } else {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (GLenum err = glGetError(); err != GL_NO_ERROR) {
            std::cerr << "[RenderPipeline] glReadPixels error in GetPreviewPixels: " << err << std::endl;
            pixels.clear();
        }
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &tempFBO);
    glDeleteTextures(1, &tempTex);

    if (pixels.empty()) {
        outW = 0;
        outH = 0;
    }
    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetSourcePixels(int& outW, int& outH) {
    if (m_SourceTexture == 0 || m_Width == 0 || m_Height == 0 || m_SourcePixels.empty()) {
        outW = outH = 0;
        return {};
    }

    outW = m_Width;
    outH = m_Height;

    std::vector<unsigned char> pixels = m_SourcePixels;
    const int rowSize = m_Width * std::max(1, m_SourceChannels);
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; ++y) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    return pixels;
}
