#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Utils/PixelBufferUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif

namespace {

using ScopedFramebufferState =
    Stack::Renderer::GLState::FramebufferState;
using ScopedPixelPackState =
    Stack::Renderer::GLState::PixelPackState;

bool FlipInterleavedRows(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels) {
    return Stack::PixelBuffer::FlipInterleavedRowsInPlace(
        pixels, width, height, channels);
}

std::vector<unsigned char> ReadTexturePixelsRgba8(
    unsigned int texture,
    int sourceWidth,
    int sourceHeight,
    int& outW,
    int& outH,
    int maxDimension,
    const char* context,
    bool flipRows = true) {
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

    std::size_t byteCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            outW, outH, 4, byteCount)) {
        outW = 0;
        outH = 0;
        return {};
    }
    std::vector<unsigned char> pixels;
    try {
        pixels.resize(byteCount);
    } catch (const std::bad_alloc&) {
        outW = 0;
        outH = 0;
        return {};
    } catch (const std::length_error&) {
        outW = 0;
        outH = 0;
        return {};
    }

    const ScopedFramebufferState savedState(true);
    const ScopedPixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();
    unsigned int readFBO = GLHelpers::CreateFBO(texture);
    unsigned int targetFBO = 0;
    unsigned int targetTex = 0;
    bool readbackReady = readFBO != 0;

    if (readbackReady &&
        (outW != sourceWidth || outH != sourceHeight)) {
        targetTex = GLHelpers::CreateEmptyTexture(outW, outH);
        targetFBO = GLHelpers::CreateFBO(targetTex);
        readbackReady = targetTex != 0 && targetFBO != 0;
        if (readbackReady) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            while (glGetError() != GL_NO_ERROR) {}
            glBlitFramebuffer(
                0, 0, sourceWidth, sourceHeight,
                0, 0, outW, outH,
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
            if (GLenum err = glGetError(); err != GL_NO_ERROR) {
                std::cerr << "[RenderPipeline] glBlitFramebuffer error in " << context << ": " << err << std::endl;
                readbackReady = false;
            }
        }
    }

    if (readbackReady) {
        glBindFramebuffer(
            GL_FRAMEBUFFER,
            targetFBO != 0 ? targetFBO : readFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(
            0, 0, outW, outH,
            GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (GLenum err = glGetError(); err != GL_NO_ERROR) {
            std::cerr << "[RenderPipeline] glReadPixels error in "
                      << context << ": " << err << std::endl;
            readbackReady = false;
        }
    }

    savedPackState.Restore();
    savedState.Restore(true);
    if (readFBO != 0) {
        glDeleteFramebuffers(1, &readFBO);
    }
    if (targetFBO != 0) {
        glDeleteFramebuffers(1, &targetFBO);
    }
    if (targetTex != 0) {
        glDeleteTextures(1, &targetTex);
    }

    if (!readbackReady) {
        outW = 0;
        outH = 0;
        return {};
    }

    if (flipRows) {
        if (!FlipInterleavedRows(pixels, outW, outH, 4)) {
            outW = 0;
            outH = 0;
            return {};
        }
    }
    return pixels;
}

std::vector<float> ReadTexturePixelsRgbaFloat(
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

    const int targetMax = maxDimension > 0
        ? std::max(1, maxDimension)
        : std::max(sourceWidth, sourceHeight);
    const float scale = std::min(
        1.0f,
        static_cast<float>(targetMax) /
            static_cast<float>(std::max(sourceWidth, sourceHeight)));
    outW = std::max(
        1,
        static_cast<int>(
            std::round(static_cast<float>(sourceWidth) * scale)));
    outH = std::max(
        1,
        static_cast<int>(
            std::round(static_cast<float>(sourceHeight) * scale)));

    std::size_t elementCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            outW, outH, 4, elementCount)) {
        outW = 0;
        outH = 0;
        return {};
    }
    std::vector<float> pixels;
    try {
        pixels.resize(elementCount);
    } catch (const std::bad_alloc&) {
        outW = 0;
        outH = 0;
        return {};
    } catch (const std::length_error&) {
        outW = 0;
        outH = 0;
        return {};
    }

    const ScopedFramebufferState savedState(true);
    const ScopedPixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();
    unsigned int readFbo = GLHelpers::CreateFBO(texture);
    unsigned int probeTexture = 0;
    unsigned int probeFbo = 0;
    bool ready = readFbo != 0;
    if (ready && (outW != sourceWidth || outH != sourceHeight)) {
        probeTexture = GLHelpers::CreateEmptyTexture(outW, outH);
        probeFbo = GLHelpers::CreateFBO(probeTexture);
        ready = probeTexture != 0 && probeFbo != 0;
        if (ready) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probeFbo);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            while (glGetError() != GL_NO_ERROR) {}
            glBlitFramebuffer(
                0, 0, sourceWidth, sourceHeight,
                0, 0, outW, outH,
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
            if (const GLenum error = glGetError();
                error != GL_NO_ERROR) {
                std::cerr
                    << "[RenderPipeline] glBlitFramebuffer error in "
                    << (context ? context : "ReadTexturePixelsRgbaFloat")
                    << ": " << error << std::endl;
                ready = false;
            }
        }
    }

    if (ready) {
        glBindFramebuffer(
            GL_FRAMEBUFFER,
            probeFbo != 0 ? probeFbo : readFbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(
            0, 0, outW, outH,
            GL_RGBA, GL_FLOAT, pixels.data());
        if (const GLenum error = glGetError();
            error != GL_NO_ERROR) {
            std::cerr
                << "[RenderPipeline] glReadPixels error in "
                << (context ? context : "ReadTexturePixelsRgbaFloat")
                << ": " << error << std::endl;
            ready = false;
        }
    }

    savedPackState.Restore();
    savedState.Restore(true);
    if (probeFbo != 0) glDeleteFramebuffers(1, &probeFbo);
    if (probeTexture != 0) glDeleteTextures(1, &probeTexture);
    if (readFbo != 0) glDeleteFramebuffers(1, &readFbo);
    if (!ready) {
        outW = 0;
        outH = 0;
        return {};
    }
    return pixels;
}
} // namespace

bool RenderPipeline::RecordConsumerBoundary(
    Stack::NodeMath::SpecializedStageKind kind,
    int maximumDimension) {
    Stack::NodeMath::SpatialDescriptor input;
    input.kind = Stack::NodeMath::SpatialExtentKind::Finite;
    input.fullWindow = { 0, 0, m_Width, m_Height };
    input.dataWindow = input.fullWindow;
    input.rasterOrigin = Stack::NodeMath::RasterOrigin::BottomLeft;
    input.pixelAspect = 1.0;
    const Stack::NodeMath::ConsumerBoundaryPlan plan =
        Stack::NodeMath::PlanConsumerBoundary(kind, input, maximumDimension);
    if (!plan.valid) return false;
    m_LastGraphExecutionStats.specializedBoundaries.push_back({
        Stack::NodeMath::SpecializedStageKindName(kind),
        static_cast<int>(plan.inputSpatial.fullWindow.width),
        static_cast<int>(plan.inputSpatial.fullWindow.height),
        static_cast<int>(plan.outputSpatial.fullWindow.width),
        static_cast<int>(plan.outputSpatial.fullWindow.height),
        plan.scalePolicy == Stack::NodeMath::RenderScalePolicy::FullQualityOnly,
        plan.changesGraphResult
    });
    return true;
}

std::vector<unsigned char> RenderPipeline::GetOutputPixels(int& outW, int& outH) {
    outW = 0;
    outH = 0;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};
    if (!RecordConsumerBoundary(Stack::NodeMath::SpecializedStageKind::ExportReadback)) {
        return {};
    }
    return ReadTexturePixelsRgba8(
        m_OutputTexture,
        m_Width,
        m_Height,
        outW,
        outH,
        0,
        "GetOutputPixels",
        true);
}

std::vector<unsigned char> RenderPipeline::GetOutputPixelsTiledPbo(
    int& outW,
    int& outH,
    int rowsPerTile) {
    outW = 0;
    outH = 0;
    if (m_OutputTexture == 0 || m_Width <= 0 || m_Height <= 0 ||
        !RecordConsumerBoundary(
            Stack::NodeMath::SpecializedStageKind::ExportReadback)) {
        return {};
    }

    std::size_t outputByteCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            m_Width, m_Height, 4, outputByteCount)) {
        return {};
    }
    std::vector<unsigned char> pixels;
    try {
        pixels.resize(outputByteCount);
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }

    struct ReadbackSlot {
        unsigned int pbo = 0;
        GLsync fence = nullptr;
        int sourceY = 0;
        int rowCount = 0;
        bool occupied = false;
    };
    std::array<ReadbackSlot, 3> slots;
    const ScopedFramebufferState savedFramebufferState(true);
    const ScopedPixelPackState savedPackState;
    const unsigned int framebuffer = GLHelpers::CreateFBO(m_OutputTexture);
    bool ready = framebuffer != 0;
    rowsPerTile = std::clamp(rowsPerTile, 1, m_Height);
    const std::size_t rowBytes =
        static_cast<std::size_t>(m_Width) * 4u;
    std::vector<unsigned char> tileBytes;
    try {
        tileBytes.resize(rowBytes * static_cast<std::size_t>(rowsPerTile));
    } catch (const std::bad_alloc&) {
        ready = false;
    } catch (const std::length_error&) {
        ready = false;
    }

    if (ready) {
        for (ReadbackSlot& slot : slots) {
            glGenBuffers(1, &slot.pbo);
            ready = ready && slot.pbo != 0;
        }
    }

    savedPackState.ConfigureTightCpuReadback();
    if (ready) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    auto collectSlot = [&](ReadbackSlot& slot) {
        if (!slot.occupied) {
            return true;
        }
        GLenum waitResult = GL_TIMEOUT_EXPIRED;
        while (waitResult == GL_TIMEOUT_EXPIRED) {
            waitResult = glClientWaitSync(
                slot.fence,
                GL_SYNC_FLUSH_COMMANDS_BIT,
                1000000000ull);
        }
        bool collected = waitResult == GL_ALREADY_SIGNALED ||
            waitResult == GL_CONDITION_SATISFIED;
        const std::size_t tileByteCount =
            rowBytes * static_cast<std::size_t>(slot.rowCount);
        if (collected) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
            while (glGetError() != GL_NO_ERROR) {}
            glGetBufferSubData(
                GL_PIXEL_PACK_BUFFER,
                0,
                static_cast<GLsizeiptr>(tileByteCount),
                tileBytes.data());
            collected = glGetError() == GL_NO_ERROR;
        }
        if (collected) {
            for (int localRow = 0; localRow < slot.rowCount; ++localRow) {
                const int destinationRow =
                    m_Height - 1 - (slot.sourceY + localRow);
                std::copy_n(
                    tileBytes.data() +
                        static_cast<std::size_t>(localRow) * rowBytes,
                    rowBytes,
                    pixels.data() +
                        static_cast<std::size_t>(destinationRow) * rowBytes);
            }
        }
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
        slot.occupied = false;
        return collected;
    };

    int tileIndex = 0;
    for (int sourceY = 0; ready && sourceY < m_Height;
         sourceY += rowsPerTile, ++tileIndex) {
        ReadbackSlot& slot = slots[static_cast<std::size_t>(
            tileIndex % static_cast<int>(slots.size()))];
        ready = collectSlot(slot);
        if (!ready) break;
        slot.sourceY = sourceY;
        slot.rowCount = std::min(rowsPerTile, m_Height - sourceY);
        const std::size_t tileByteCount =
            rowBytes * static_cast<std::size_t>(slot.rowCount);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        while (glGetError() != GL_NO_ERROR) {}
        glBufferData(
            GL_PIXEL_PACK_BUFFER,
            static_cast<GLsizeiptr>(tileByteCount),
            nullptr,
            GL_STREAM_READ);
        glReadPixels(
            0,
            sourceY,
            m_Width,
            slot.rowCount,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr);
        ready = glGetError() == GL_NO_ERROR;
        if (ready) {
            slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            ready = slot.fence != nullptr;
            slot.occupied = ready;
            if (ready) glFlush();
        }
    }
    for (ReadbackSlot& slot : slots) {
        if (ready) {
            ready = collectSlot(slot);
        }
        if (slot.fence != nullptr) {
            glDeleteSync(slot.fence);
            slot.fence = nullptr;
        }
        if (slot.pbo != 0) {
            glDeleteBuffers(1, &slot.pbo);
            slot.pbo = 0;
        }
    }
    savedPackState.Restore();
    savedFramebufferState.Restore(true);
    if (framebuffer != 0) glDeleteFramebuffers(1, &framebuffer);
    if (!ready) {
        return {};
    }
    outW = m_Width;
    outH = m_Height;
    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetOutputPixels(int& outW, int& outH, int maxDimension) {
    if (maxDimension <= 0 || maxDimension >= std::max(m_Width, m_Height)) {
        return GetOutputPixels(outW, outH);
    }
    if (!RecordConsumerBoundary(
            Stack::NodeMath::SpecializedStageKind::PreviewReadback,
            maxDimension)) {
        outW = outH = 0;
        return {};
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

std::vector<unsigned char> RenderPipeline::GetExternalTexturePixels(
    unsigned int texture,
    int width,
    int height,
    int& outW,
    int& outH,
    int maxDimension) {
    return ReadTexturePixelsRgba8(
        texture,
        width,
        height,
        outW,
        outH,
        maxDimension,
        "GetExternalTexturePixels");
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
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    Raw::RawWorkingSpace workingSpace,
    Raw::RawProcessingVersion processingVersion,
    std::size_t inputStageFingerprint) {
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

    std::size_t rgbaElementCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            readW, readH, 4, rgbaElementCount)) {
        return false;
    }
    std::vector<float> rgba;
    try {
        rgba.assign(rgbaElementCount, 0.0f);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    const ScopedFramebufferState savedState;
    const ScopedPixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();

    const unsigned int readFBO = GLHelpers::CreateFBO(texture);
    if (readFBO == 0) {
        savedPackState.Restore();
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(
        minX, minY, readW, readH,
        GL_RGBA, GL_FLOAT, rgba.data());
    const bool readOk = glGetError() == GL_NO_ERROR;

    savedPackState.Restore();
    savedState.Restore();
    glDeleteFramebuffers(1, &readFBO);
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
        const bool rec2020 =
            workingSpace == Raw::RawWorkingSpace::LinearRec2020D65;
        const float luma = std::max(
            0.0f,
            (rec2020 ? 0.2627f : 0.2126f) * r +
                (rec2020 ? 0.6780f : 0.7152f) * g +
                (rec2020 ? 0.0593f : 0.0722f) * b);
        if (std::isfinite(luma)) {
            samples.push_back({
                luma,
                r,
                g,
                b
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

    const Stack::RawRecipe::RawLocalRangeRecipe sanitized =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    const float middleGrey = std::clamp(sanitized.middleGrey, 0.01f, 1.0f);
    const float robustSceneEv =
        std::log2(std::max(robustLuma, 0.00000001f) / middleGrey);
    std::uint32_t authoredZoneHitBits = 0;
    const unsigned int selectionBitsTexture =
        BuildRawDevelopmentLocalRangeSelectionBits(
            texture,
            sanitized,
            workingSpace,
            processingVersion,
            inputStageFingerprint);
    if (selectionBitsTexture != 0 &&
        m_RawDevelopmentLocalRangeSelectionBitsTextureWidth > 0 &&
        m_RawDevelopmentLocalRangeSelectionBitsTextureHeight > 0) {
        const int maskWidth = m_RawDevelopmentLocalRangeSelectionBitsTextureWidth;
        const int maskHeight = m_RawDevelopmentLocalRangeSelectionBitsTextureHeight;
        const int maskCenterX = std::clamp(
            static_cast<int>(std::lround(sampleU * static_cast<float>(maskWidth - 1))),
            0,
            maskWidth - 1);
        const int maskDisplayY = std::clamp(
            static_cast<int>(std::lround(sampleV * static_cast<float>(maskHeight - 1))),
            0,
            maskHeight - 1);
        const int maskCenterY = maskHeight - 1 - maskDisplayY;
        const int radiusX = std::max(
            1,
            static_cast<int>(std::ceil(
                std::max(
                    m_RawDevelopmentLocalRangeTargetPreviewRequest.hitRadiusU,
                    12.0f / static_cast<float>(std::max(1, m_Width))) *
                static_cast<float>(maskWidth))));
        const int radiusY = std::max(
            1,
            static_cast<int>(std::ceil(
                std::max(
                    m_RawDevelopmentLocalRangeTargetPreviewRequest.hitRadiusV,
                    12.0f / static_cast<float>(std::max(1, m_Height))) *
                static_cast<float>(maskHeight))));
        const int hitMinX = std::max(0, maskCenterX - radiusX);
        const int hitMaxX = std::min(maskWidth - 1, maskCenterX + radiusX);
        const int hitMinY = std::max(0, maskCenterY - radiusY);
        const int hitMaxY = std::min(maskHeight - 1, maskCenterY + radiusY);
        const int hitWidth = hitMaxX - hitMinX + 1;
        const int hitHeight = hitMaxY - hitMinY + 1;
        std::size_t hitPixelCount = 0;
        if (!Stack::PixelBuffer::TryComputePixelElementCount(
                hitWidth, hitHeight, 1, hitPixelCount)) {
            return false;
        }
        std::vector<std::uint32_t> hitPixels;
        try {
            hitPixels.assign(hitPixelCount, 0u);
        } catch (const std::bad_alloc&) {
            return false;
        } catch (const std::length_error&) {
            return false;
        }
        const ScopedFramebufferState hitSavedState;
        const ScopedPixelPackState hitSavedPackState;
        hitSavedPackState.ConfigureTightCpuReadback();
        const unsigned int hitFbo =
            GLHelpers::CreateFBO(selectionBitsTexture);
        if (hitFbo == 0) {
            hitSavedPackState.Restore();
            return false;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, hitFbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(
            hitMinX,
            hitMinY,
            hitWidth,
            hitHeight,
            GL_RED_INTEGER,
            GL_UNSIGNED_INT,
            hitPixels.data());
        const bool hitReadOk = glGetError() == GL_NO_ERROR;
        hitSavedPackState.Restore();
        hitSavedState.Restore();
        glDeleteFramebuffers(1, &hitFbo);
        if (hitReadOk) {
            for (const std::uint32_t bits : hitPixels) {
                authoredZoneHitBits |= bits;
            }
        }
    }

    float strongestAuthoredZoneWeight = 0.0f;
    const int zoneCount = std::min<int>(
        static_cast<int>(sanitized.targetZones.size()),
        static_cast<int>(Stack::RawRecipe::kMaxRawLocalRangeTargetZones));
    for (int zoneIndex = 0; zoneIndex < zoneCount; ++zoneIndex) {
        const Stack::RawRecipe::RawLocalRangeTargetZone& zone =
            sanitized.targetZones[static_cast<std::size_t>(zoneIndex)];
        if (!zone.enabled) {
            continue;
        }
        const float effectiveWeight =
            Stack::RawRecipe::EvaluateLocalRangeTargetZoneTonalWeight(
                zone,
                robustSceneEv) *
            Stack::RawRecipe::EvaluateLocalRangeTargetZoneColorWeight(
                zone,
                robustR,
                robustG,
                robustB,
                workingSpace);
        const std::uint32_t zoneBit = std::uint32_t(1u) << zoneIndex;
        if (zone.scope == Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches &&
            effectiveWeight >= 0.10f) {
            authoredZoneHitBits |= zoneBit;
        }
        if ((authoredZoneHitBits & zoneBit) != 0u) {
            strongestAuthoredZoneWeight =
                std::max(strongestAuthoredZoneWeight, effectiveWeight);
        }
    }

    m_RawDevelopmentLocalRangeTargetSampleValid = true;
    m_RawDevelopmentLocalRangeTargetSampleSceneLuma = robustLuma;
    m_RawDevelopmentLocalRangeTargetSampleSceneR = robustR;
    m_RawDevelopmentLocalRangeTargetSampleSceneG = robustG;
    m_RawDevelopmentLocalRangeTargetSampleSceneB = robustB;
    m_RawDevelopmentLocalRangeTargetSampleSceneEv = robustSceneEv;
    m_RawDevelopmentLocalRangeTargetSampleU = sampleU;
    m_RawDevelopmentLocalRangeTargetSampleV = sampleV;
    m_RawDevelopmentLocalRangeTargetSampleAuthoredZoneHitBits =
        authoredZoneHitBits;
    m_RawDevelopmentLocalRangeTargetSampleStrongestAuthoredZoneWeight =
        strongestAuthoredZoneWeight;
    return true;
}

bool RenderPipeline::GetRawDevelopmentLocalRangeTargetSample(
    float& outSceneEv,
    float& outSceneLuma,
    float& outU,
    float& outV,
    std::array<float, 3>* outSceneRgb,
    std::uint32_t* outAuthoredZoneHitBits,
    float* outStrongestAuthoredZoneWeight) const {
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
    if (outAuthoredZoneHitBits) {
        *outAuthoredZoneHitBits =
            m_RawDevelopmentLocalRangeTargetSampleAuthoredZoneHitBits;
    }
    if (outStrongestAuthoredZoneWeight) {
        *outStrongestAuthoredZoneWeight =
            m_RawDevelopmentLocalRangeTargetSampleStrongestAuthoredZoneWeight;
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

    const int sourceWidth =
        cached->second.width > 0 ? cached->second.width : m_Width;
    const int sourceHeight =
        cached->second.height > 0 ? cached->second.height : m_Height;
    return ReadTexturePixelsRgba8(
        cached->second.texture,
        sourceWidth,
        sourceHeight,
        outW,
        outH,
        0,
        "GetCachedGraphImagePixels",
        true);
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
    outW = 0;
    outH = 0;
    unsigned int compareTex = GetCompareSourceTexture();
    const int compareWidth =
        m_GraphSourceTexture != 0 ? m_GraphSourceWidth : m_BaseCanvasWidth;
    const int compareHeight =
        m_GraphSourceTexture != 0 ? m_GraphSourceHeight : m_BaseCanvasHeight;
    if (compareTex == 0 || compareWidth <= 0 || compareHeight <= 0) return {};
    return ReadTexturePixelsRgba8(
        compareTex,
        compareWidth,
        compareHeight,
        outW,
        outH,
        0,
        "GetCompareSourcePixels",
        true);
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
    const ScopedPixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();

    const unsigned int readFBO = GLHelpers::CreateFBO(m_OutputTexture);
    if (readFBO == 0) {
        savedPackState.Restore();
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(px, readY, 1, 1, GL_RGBA, GL_FLOAT, outRgba.data());
    const bool success = glGetError() == GL_NO_ERROR;

    savedPackState.Restore();
    savedState.Restore();
    glDeleteFramebuffers(1, &readFBO);
    return success;
}

void RenderPipeline::CaptureRawDevelopmentStageImageReadback(
    Stack::RawAutoStartPoint::RawAutoStartPointStage stage,
    Stack::RawAutoStartPoint::RawAutoStartPointStageStatus status,
    unsigned int texture,
    int width,
    int height,
    const std::string& measurementDomain,
    bool sceneLinearBeforeViewTransform,
    bool displayMappedLinearRgb) {
    if (m_RawDevelopmentStageImageReadbackMaxDimension <= 0 ||
        texture == 0 || width <= 0 || height <= 0) {
        return;
    }

    RawDevelopmentStageImageReadback readback;
    readback.stage = stage;
    readback.status = status;
    readback.stageId = Stack::RawAutoStartPoint::StageStableString(stage);
    readback.measurementDomain = measurementDomain;
    readback.sceneLinearBeforeViewTransform = sceneLinearBeforeViewTransform;
    readback.displayMappedLinearRgb = displayMappedLinearRgb;
    readback.sourceWidth = width;
    readback.sourceHeight = height;

    std::vector<float> rgba = ReadTexturePixelsRgbaFloat(
        texture,
        width,
        height,
        readback.width,
        readback.height,
        m_RawDevelopmentStageImageReadbackMaxDimension,
        "CaptureRawDevelopmentStageImageReadback");

    if (!rgba.empty()) {
        std::size_t rgbElementCount = 0;
        if (Stack::PixelBuffer::TryComputePixelElementCount(
                readback.width,
                readback.height,
                3,
                rgbElementCount)) {
            try {
                readback.pixels.assign(rgbElementCount, 0.0f);
            } catch (const std::bad_alloc&) {
                readback.pixels.clear();
            } catch (const std::length_error&) {
                readback.pixels.clear();
            }
        }
        if (!readback.pixels.empty()) {
            for (int y = 0; y < readback.height; ++y) {
                const int sourceY = readback.height - 1 - y;
                for (int x = 0; x < readback.width; ++x) {
                    const std::size_t source =
                        (static_cast<std::size_t>(sourceY) *
                             static_cast<std::size_t>(readback.width) +
                         static_cast<std::size_t>(x)) *
                        4u;
                    const std::size_t destination =
                        (static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(readback.width) +
                         static_cast<std::size_t>(x)) *
                        3u;
                    readback.pixels[destination] = rgba[source];
                    readback.pixels[destination + 1] = rgba[source + 1];
                    readback.pixels[destination + 2] = rgba[source + 2];
                }
            }
            readback.valid = true;
        }
    }

    const auto existing = std::find_if(
        m_RawDevelopmentStageImageReadbacks.begin(),
        m_RawDevelopmentStageImageReadbacks.end(),
        [&](const RawDevelopmentStageImageReadback& item) { return item.stage == stage; });
    if (existing == m_RawDevelopmentStageImageReadbacks.end()) {
        m_RawDevelopmentStageImageReadbacks.push_back(std::move(readback));
    } else {
        *existing = std::move(readback);
    }
}

void RenderPipeline::CaptureRawDevelopmentGraphScopeReadback(
    RawDevelopmentGraphScopeStage stage,
    unsigned int texture,
    int width,
    int height,
    const std::string& measurementDomain,
    bool sceneLinearBeforeViewTransform,
    const std::string& controlSignalDomain) {
    if (stage == RawDevelopmentGraphScopeStage::None ||
        stage != m_RawDevelopmentGraphScopeStage ||
        m_RawDevelopmentGraphScopeReadbackMaxDimension <= 0 ||
        texture == 0 || width <= 0 || height <= 0) {
        return;
    }

    RawDevelopmentGraphScopeReadback readback;
    readback.stage = stage;
    readback.measurementDomain = measurementDomain;
    readback.controlSignalDomain = controlSignalDomain;
    readback.sceneLinearBeforeViewTransform = sceneLinearBeforeViewTransform;
    readback.sourceWidth = width;
    readback.sourceHeight = height;

    std::vector<float> rgba = ReadTexturePixelsRgbaFloat(
        texture,
        width,
        height,
        readback.width,
        readback.height,
        m_RawDevelopmentGraphScopeReadbackMaxDimension,
        "CaptureRawDevelopmentGraphScopeReadback");

    if (!rgba.empty()) {
        std::size_t rgbElementCount = 0;
        if (Stack::PixelBuffer::TryComputePixelElementCount(
                readback.width,
                readback.height,
                3,
                rgbElementCount)) {
            try {
                readback.pixels.assign(rgbElementCount, 0.0f);
            } catch (const std::bad_alloc&) {
                readback.pixels.clear();
            } catch (const std::length_error&) {
                readback.pixels.clear();
            }
        }
        if (!readback.pixels.empty()) {
            const std::size_t pixelCount =
                static_cast<std::size_t>(readback.width) *
                static_cast<std::size_t>(readback.height);
            if (!controlSignalDomain.empty()) {
                try {
                    readback.controlSignal.assign(pixelCount, 0.0f);
                } catch (const std::bad_alloc&) {
                    readback.controlSignal.clear();
                } catch (const std::length_error&) {
                    readback.controlSignal.clear();
                }
            }
            for (int y = 0; y < readback.height; ++y) {
                const int sourceY = readback.height - 1 - y;
                for (int x = 0; x < readback.width; ++x) {
                    const std::size_t source =
                        (static_cast<std::size_t>(sourceY) *
                             static_cast<std::size_t>(readback.width) +
                         static_cast<std::size_t>(x)) *
                        4u;
                    const std::size_t destination =
                        (static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(readback.width) +
                         static_cast<std::size_t>(x)) *
                        3u;
                    readback.pixels[destination] = rgba[source];
                    readback.pixels[destination + 1] = rgba[source + 1];
                    readback.pixels[destination + 2] = rgba[source + 2];
                    if (!readback.controlSignal.empty()) {
                        readback.controlSignal[destination / 3u] =
                            rgba[source + 3];
                    }
                }
            }
            readback.valid = controlSignalDomain.empty() ||
                readback.controlSignal.size() == pixelCount;
        }
    }

    m_RawDevelopmentGraphScopeReadback = std::move(readback);
}

RenderTextureStats RenderPipeline::ReadTextureStats(unsigned int texture, int width, int height, const char* context,
    bool linearRec2020) {
    RenderTextureStats stats;
    if (texture == 0 || width <= 0 || height <= 0) {
        return stats;
    }

    constexpr int kMaxProbeEdge = 512;
    int probeW = 0;
    int probeH = 0;
    const std::vector<float> pixels = ReadTexturePixelsRgbaFloat(
        texture,
        width,
        height,
        probeW,
        probeH,
        kMaxProbeEdge,
        context ? context : "ReadTextureStats");
    if (pixels.empty()) {
        return stats;
    }

    stats.valid = true;
    stats.minRgb = std::numeric_limits<float>::max();
    stats.maxRgb = -std::numeric_limits<float>::max();
    stats.minLuma = std::numeric_limits<float>::max();
    stats.maxLuma = -std::numeric_limits<float>::max();

    std::vector<float> lumas;
    try {
        lumas.reserve(
            static_cast<std::size_t>(probeW) *
            static_cast<std::size_t>(probeH));
    } catch (const std::bad_alloc&) {
        return RenderTextureStats {};
    } catch (const std::length_error&) {
        return RenderTextureStats {};
    }
    float logLumaSum = 0.0f;
    int hdrPixels = 0;
    int displayEdgePixels = 0;
    int displayHighEdgePixels = 0;
    int displayLowEdgePixels = 0;
    int validPixels = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (!(pixels[i + 3] > 0.0f)) continue;
        float r = pixels[i + 0];
        float g = pixels[i + 1];
        float b = pixels[i + 2];
        if (!std::isfinite(r)) r = 0.0f;
        if (!std::isfinite(g)) g = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        const float minChannel = std::min({ r, g, b });
        const float maxChannel = std::max({ r, g, b });
        const float luma = std::max(0.0f, linearRec2020
            ? 0.2627002f * r + 0.6779981f * g + 0.0593017f * b
            : 0.2126f * r + 0.7152f * g + 0.0722f * b);
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
        if (maxChannel >= 0.999f) {
            ++displayHighEdgePixels;
        }
        if (minChannel <= 0.001f) {
            ++displayLowEdgePixels;
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
    stats.p10Luma = percentile(0.10f);
    stats.p25Luma = percentile(0.25f);
    stats.p50Luma = percentile(0.50f);
    stats.p75Luma = percentile(0.75f);
    stats.p90Luma = percentile(0.90f);
    stats.p95Luma = percentile(0.95f);
    stats.p99Luma = percentile(0.99f);
    stats.p999Luma = percentile(0.999f);
    stats.logAverageLuma = std::exp(logLumaSum / static_cast<float>(validPixels));
    stats.dynamicRangeEv =
        std::log2(std::max(1.0e-8f, stats.p99Luma)) -
        std::log2(std::max(1.0e-8f, stats.p01Luma));
    stats.validPixelPercent = 100.0f * validPixels / std::max<std::size_t>(1, pixels.size() / 4);
    stats.hdrPixelPercent = 100.0f * static_cast<float>(hdrPixels) / static_cast<float>(validPixels);
    stats.displayClipPercent = 100.0f * static_cast<float>(displayEdgePixels) / static_cast<float>(validPixels);
    stats.displayClipHighPercent =
        100.0f * static_cast<float>(displayHighEdgePixels) / static_cast<float>(validPixels);
    stats.displayClipLowPercent =
        100.0f * static_cast<float>(displayLowEdgePixels) / static_cast<float>(validPixels);
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

    int probeW = 0;
    int probeH = 0;
    const std::vector<float> rgba = ReadTexturePixelsRgbaFloat(
        texture,
        width,
        height,
        probeW,
        probeH,
        maxDimension,
        context ? context : "ReadLocalSuggestionAnalysisImage");
    if (rgba.empty()) {
        image.statusMessage = "Local suggestion readback produced no pixels.";
        return image;
    }

    image.width = probeW;
    image.height = probeH;
    std::size_t pixelCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            probeW, probeH, 1, pixelCount)) {
        image.statusMessage = "Local suggestion dimensions exceed CPU buffer limits.";
        return image;
    }
    try {
        image.pixels.resize(pixelCount);
    } catch (const std::bad_alloc&) {
        image.pixels.clear();
        image.statusMessage = "Local suggestion readback ran out of memory.";
        return image;
    } catch (const std::length_error&) {
        image.pixels.clear();
        image.statusMessage = "Local suggestion dimensions exceed CPU buffer limits.";
        return image;
    }
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
            pixel.valid = rgba[src + 3] > 0.0f &&
                std::isfinite(pixel.r) &&
                std::isfinite(pixel.g) &&
                std::isfinite(pixel.b);
        }
    }
    image.valid = true;
    return image;
}

RenderTextureStats RenderPipeline::GetOutputTextureStats() {
    return ReadTextureStats(m_OutputTexture, m_Width, m_Height, "GetOutputTextureStats");
}

std::vector<unsigned char> RenderPipeline::GetScopesPixels(int& outW, int& outH) {
    outW = 0;
    outH = 0;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};
    if (!RecordConsumerBoundary(
            Stack::NodeMath::SpecializedStageKind::ScopeAnalysis,
            256)) {
        return {};
    }
    return ReadTexturePixelsRgba8(
        m_OutputTexture,
        m_Width,
        m_Height,
        outW,
        outH,
        256,
        "GetScopesPixels",
        false);
}

std::vector<unsigned char> RenderPipeline::GetPreviewPixels(int& outW, int& outH, int maxDimension) {
    outW = 0;
    outH = 0;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) {
        return {};
    }
    if (!RecordConsumerBoundary(
            Stack::NodeMath::SpecializedStageKind::PreviewReadback,
            maxDimension)) {
        return {};
    }
    return ReadTexturePixelsRgba8(
        m_OutputTexture,
        m_Width,
        m_Height,
        outW,
        outH,
        maxDimension,
        "GetPreviewPixels",
        false);
}

std::vector<unsigned char> RenderPipeline::GetSourcePixels(int& outW, int& outH) {
    const std::vector<unsigned char>& sourcePixels = GetSourcePixelsRaw();
    if (m_SourceTexture == 0 ||
        m_BaseCanvasWidth <= 0 ||
        m_BaseCanvasHeight <= 0 ||
        sourcePixels.empty()) {
        outW = outH = 0;
        return {};
    }

    outW = m_BaseCanvasWidth;
    outH = m_BaseCanvasHeight;

    std::vector<unsigned char> pixels;
    try {
        pixels = sourcePixels;
    } catch (const std::bad_alloc&) {
        outW = outH = 0;
        return {};
    } catch (const std::length_error&) {
        outW = outH = 0;
        return {};
    }
    if (!FlipInterleavedRows(
            pixels,
            m_BaseCanvasWidth,
            m_BaseCanvasHeight,
            m_SourceChannels)) {
        outW = outH = 0;
        return {};
    }
    return pixels;
}
