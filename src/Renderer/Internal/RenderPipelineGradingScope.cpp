#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif

namespace {
using ScopedFramebufferState = Stack::Renderer::GLState::FramebufferState;
using ScopedPixelPackState = Stack::Renderer::GLState::PixelPackState;

bool ReadGradingScopePixels(unsigned int pbo, RawDevelopmentGradingScopeReadback& readback) {
    std::size_t count = 0;
    if (pbo == 0 || !Stack::PixelBuffer::TryComputePixelElementCount(readback.width, readback.height, 4, count) ||
        count > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(float)) return false;
    std::vector<float> rgba(count);
    const ScopedPixelPackState savedState;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    while (glGetError() != GL_NO_ERROR) {}
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, count * sizeof(float), rgba.data());
    const bool ok = glGetError() == GL_NO_ERROR;
    savedState.Restore();
    if (!ok) return false;
    readback.pixels.resize(count / 4 * 3);
    readback.coverage.resize(count / 4);
    for (int y = 0; y < readback.height; ++y) {
        const int sourceY = readback.height - 1 - y;
        for (int x = 0; x < readback.width; ++x) {
            const auto source = (static_cast<std::size_t>(sourceY) * readback.width + x) * 4;
            const auto destination = (static_cast<std::size_t>(y) * readback.width + x) * 3;
            readback.coverage[destination / 3] = rgba[source + 3];
            for (int c = 0; c < 3; ++c) {
                const float value = rgba[source + c];
                readback.pixels[destination + c] = std::isfinite(value) ? value : 0.0f;
            }
        }
    }
    return true;
}
} // namespace

void RenderPipeline::SetRawDevelopmentGradingScopeReadbackRequest(
    RawDevelopmentGradingScopeSource source,
    int maxDimension,
    std::uint64_t generation,
    std::string sourceKey,
    bool includePixels) {
    const int sanitizedMaximum = std::max(0, maxDimension);
    if (source == RawDevelopmentGradingScopeSource::None ||
        sanitizedMaximum == 0 || sourceKey.empty()) {
        ClearRawDevelopmentGradingScopeReadback();
        return;
    }
    const bool identityChanged =
        source != m_RawDevelopmentGradingScopeSource ||
        sourceKey != m_RawDevelopmentGradingScopeRequestSourceKey ||
        sanitizedMaximum != m_RawDevelopmentGradingScopeReadbackMaxDimension ||
        includePixels != m_RawDevelopmentGradingScopeIncludePixels;
    m_RawDevelopmentGradingScopeIncludePixels = includePixels;
    m_RawDevelopmentGradingScopeSource = source;
    m_RawDevelopmentGradingScopeReadbackMaxDimension = sanitizedMaximum;
    m_RawDevelopmentGradingScopeRequestGeneration = generation;
    m_RawDevelopmentGradingScopeRequestSourceKey = std::move(sourceKey);
    if (identityChanged) {
        ++m_RawDevelopmentGradingScopeRequestRevision;
        m_RawDevelopmentGradingScopeReadback = {};
        m_RawDevelopmentGradingScopeLastIssueTime = {};
    }
}

bool RenderPipeline::IsRawDevelopmentGradingScopeRequested(
    RawDevelopmentGradingScopeSource source) const {
    return source != RawDevelopmentGradingScopeSource::None &&
        source == m_RawDevelopmentGradingScopeSource &&
        m_RawDevelopmentGradingScopeReadbackMaxDimension > 0 &&
        !m_RawDevelopmentGradingScopeRequestSourceKey.empty();
}

bool RenderPipeline::PollRawDevelopmentGradingScopeReadback() {
    RawDevelopmentGradingScopeReadbackSlot* newest = nullptr;
    for (auto& slot : m_RawDevelopmentGradingScopeReadbackSlots) {
        if (!slot.occupied || slot.fence == nullptr) continue;
        const auto status = glClientWaitSync(slot.fence, 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED && status != GL_WAIT_FAILED) continue;
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
        slot.occupied = false;
        if (status != GL_WAIT_FAILED &&
            slot.requestRevision == m_RawDevelopmentGradingScopeRequestRevision &&
            slot.source == m_RawDevelopmentGradingScopeSource &&
            slot.sourceKey == m_RawDevelopmentGradingScopeRequestSourceKey &&
            slot.generation >= m_RawDevelopmentGradingScopeReadback.generation &&
            (!newest || slot.generation >= newest->generation)) newest = &slot;
    }
    // Retire all completed captures, but only transfer and unpack the newest one.
    if (!newest) return false;
    const auto& slot = *newest;
    RawDevelopmentGradingScopeReadback readback;
    readback.source = slot.source;
    readback.sourceKey = slot.sourceKey;
    readback.measurementDomain = slot.measurementDomain;
    readback.workingSpace = slot.workingSpace;
    readback.sceneLinear = slot.sceneLinear;
    readback.encodedSrgb = slot.encodedSrgb;
    readback.generation = slot.generation;
    readback.width = slot.width;
    readback.height = slot.height;
    readback.sourceWidth = slot.sourceWidth;
    readback.sourceHeight = slot.sourceHeight;
    readback.valid = true;
    try {
        if (slot.includesPixels && !ReadGradingScopePixels(slot.pbo, readback)) return false;
        if (slot.gpuReduced) {
            if (!RawGradingScopeGpu::Read(slot.plotBuffer, readback)) return false;
        } else {
            readback.visualization = Raw::BuildGradingScopeVisualization(readback);
            if (!m_RawDevelopmentGradingScopeIncludePixels) std::vector<float>().swap(readback.pixels);
        }
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    if (!readback.visualization) return false;
    m_RawDevelopmentGradingScopeReadback = std::move(readback);
    return true;
}

void RenderPipeline::CaptureRawDevelopmentGradingScopeReadback(
    RawDevelopmentGradingScopeSource source,
    unsigned int texture,
    int width,
    int height,
    const std::string& measurementDomain,
    Raw::RawWorkingSpace workingSpace,
    bool sceneLinear,
    bool encodedSrgb) {
    if (!IsRawDevelopmentGradingScopeRequested(source) ||
        texture == 0 || width <= 0 || height <= 0) {
        return;
    }

    (void)PollRawDevelopmentGradingScopeReadback();
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kInteractiveCaptureInterval =
        std::chrono::milliseconds(33);
    if (!m_RawDevelopmentAnalysisEnabled &&
        m_RawDevelopmentGradingScopeLastIssueTime.time_since_epoch().count() != 0 &&
        now - m_RawDevelopmentGradingScopeLastIssueTime <
            kInteractiveCaptureInterval) {
        return;
    }
    const bool matchingGenerationPending = std::any_of(
        m_RawDevelopmentGradingScopeReadbackSlots.begin(),
        m_RawDevelopmentGradingScopeReadbackSlots.end(),
        [&](const RawDevelopmentGradingScopeReadbackSlot& slot) {
            return slot.occupied &&
                slot.requestRevision == m_RawDevelopmentGradingScopeRequestRevision &&
                slot.source == source &&
                slot.sourceKey == m_RawDevelopmentGradingScopeRequestSourceKey &&
                slot.generation == m_RawDevelopmentGradingScopeRequestGeneration;
        });
    if (matchingGenerationPending ||
        (m_RawDevelopmentGradingScopeReadback.valid &&
         m_RawDevelopmentGradingScopeReadback.source == source &&
         m_RawDevelopmentGradingScopeReadback.sourceKey ==
             m_RawDevelopmentGradingScopeRequestSourceKey &&
         m_RawDevelopmentGradingScopeReadback.generation >=
             m_RawDevelopmentGradingScopeRequestGeneration)) {
        return;
    }

    RawDevelopmentGradingScopeReadbackSlot* targetSlot = nullptr;
    const int slotCount = static_cast<int>(m_RawDevelopmentGradingScopeReadbackSlots.size());
    for (int offset = 0; offset < slotCount; ++offset) {
        const int index =
            (m_RawDevelopmentGradingScopeNextReadbackSlot + offset) % slotCount;
        RawDevelopmentGradingScopeReadbackSlot& slot =
            m_RawDevelopmentGradingScopeReadbackSlots[
                static_cast<std::size_t>(index)];
        if (!slot.occupied) {
            targetSlot = &slot;
            m_RawDevelopmentGradingScopeNextReadbackSlot = (index + 1) % slotCount;
            break;
        }
    }
    if (targetSlot == nullptr) {
        return;
    }

    const int targetMaximum = std::max(
        1,
        m_RawDevelopmentGradingScopeReadbackMaxDimension);
    const float scale = std::min(
        1.0f,
        static_cast<float>(targetMaximum) /
            static_cast<float>(std::max(width, height)));
    const int targetWidth = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(width) * scale)));
    const int targetHeight = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(height) * scale)));

    const ScopedFramebufferState savedFramebufferState(true);
    const ScopedPixelPackState savedPackState;
    const unsigned int readFramebuffer = GLHelpers::CreateFBO(texture);
    unsigned int targetTexture = 0;
    unsigned int targetFramebuffer = 0;
    bool ready = readFramebuffer != 0;
    if (ready && (targetWidth != width || targetHeight != height)) {
        targetTexture = GLHelpers::CreateEmptyTexture(targetWidth, targetHeight);
        targetFramebuffer = targetTexture != 0
            ? GLHelpers::CreateFBO(targetTexture)
            : 0;
        ready = targetTexture != 0 && targetFramebuffer != 0;
        if (ready) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFramebuffer);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            while (glGetError() != GL_NO_ERROR) {}
            glBlitFramebuffer(
                0, 0, width, height,
                0, 0, targetWidth, targetHeight,
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
            ready = glGetError() == GL_NO_ERROR;
        }
    }

    targetSlot->gpuReduced = ready && m_RawDevelopmentGradingScopeGpu.Dispatch(
        targetTexture != 0 ? targetTexture : texture, targetWidth, targetHeight,
        workingSpace, sceneLinear, encodedSrgb, targetSlot->plotBuffer);
    targetSlot->includesPixels = m_RawDevelopmentGradingScopeIncludePixels || !targetSlot->gpuReduced;
    if (ready && targetSlot->includesPixels) {
        std::size_t rgbaElementCount = 0;
        const bool elementCountValid =
            Stack::PixelBuffer::TryComputePixelElementCount(
                targetWidth,
                targetHeight,
                4,
                rgbaElementCount) &&
            rgbaElementCount <=
                std::numeric_limits<std::size_t>::max() / sizeof(float) &&
            rgbaElementCount * sizeof(float) <=
                static_cast<std::size_t>(
                    std::numeric_limits<GLsizeiptr>::max());
        if (targetSlot->pbo == 0) {
            glGenBuffers(1, &targetSlot->pbo);
        }
        savedPackState.ConfigureTightCpuReadback();
        ready = ready && elementCountValid && targetSlot->pbo != 0;
        if (ready) {
            glBindFramebuffer(
                GL_FRAMEBUFFER,
                targetFramebuffer != 0 ? targetFramebuffer : readFramebuffer);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, targetSlot->pbo);
            while (glGetError() != GL_NO_ERROR) {}
            glBufferData(
                GL_PIXEL_PACK_BUFFER,
                static_cast<GLsizeiptr>(rgbaElementCount * sizeof(float)),
                nullptr,
                GL_STREAM_READ);
            ready = glGetError() == GL_NO_ERROR;
        }
        if (ready) {
            glReadPixels(
                0,
                0,
                targetWidth,
                targetHeight,
                GL_RGBA,
                GL_FLOAT,
                nullptr);
            ready = glGetError() == GL_NO_ERROR;
        }
    } else if (targetSlot->pbo != 0) {
        glDeleteBuffers(1, &targetSlot->pbo);
        targetSlot->pbo = 0;
    }
    if (ready) {
        targetSlot->fence =
            glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        ready = targetSlot->fence != nullptr;
    }
    if (ready) {
        targetSlot->requestRevision = m_RawDevelopmentGradingScopeRequestRevision;
        targetSlot->source = source;
        targetSlot->sourceKey = m_RawDevelopmentGradingScopeRequestSourceKey;
        targetSlot->measurementDomain = measurementDomain;
        targetSlot->workingSpace = workingSpace;
        targetSlot->sceneLinear = sceneLinear;
        targetSlot->encodedSrgb = encodedSrgb;
        targetSlot->generation =
            m_RawDevelopmentGradingScopeRequestGeneration;
        targetSlot->width = targetWidth;
        targetSlot->height = targetHeight;
        targetSlot->sourceWidth = width;
        targetSlot->sourceHeight = height;
        targetSlot->occupied = true;
        m_RawDevelopmentGradingScopeLastIssueTime = now;
        glFlush();
    } else {
        if (targetSlot->fence != nullptr) {
            glDeleteSync(targetSlot->fence);
            targetSlot->fence = nullptr;
        }
        targetSlot->occupied = false;
    }

    savedPackState.Restore();
    savedFramebufferState.Restore(true);
    if (targetFramebuffer != 0) {
        glDeleteFramebuffers(1, &targetFramebuffer);
    }
    if (targetTexture != 0) {
        glDeleteTextures(1, &targetTexture);
    }
    if (readFramebuffer != 0) {
        glDeleteFramebuffers(1, &readFramebuffer);
    }
}

void RenderPipeline::ClearRawDevelopmentGradingScopeReadback() {
    ++m_RawDevelopmentGradingScopeRequestRevision;
    m_RawDevelopmentGradingScopeIncludePixels = false;
    m_RawDevelopmentGradingScopeSource =
        RawDevelopmentGradingScopeSource::None;
    m_RawDevelopmentGradingScopeReadbackMaxDimension = 0;
    m_RawDevelopmentGradingScopeRequestGeneration = 0;
    m_RawDevelopmentGradingScopeRequestSourceKey.clear();
    m_RawDevelopmentGradingScopeReadback = {};
}

