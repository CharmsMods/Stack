#include "Renderer/RenderPipeline.h"

#include "Raw/RawRestormerAdapter.h"
#include "Renderer/GLStateGuards.h"
#include "Restormer/RestormerClient.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void SetStringNoThrow(
    std::string& target,
    const char* message) noexcept {
    try {
        target = message;
    } catch (...) {
        target.clear();
    }
}

constexpr const char* kFullscreenVertexShader = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aTex;
    out vec2 vTexCoord;
    void main() {
        vTexCoord = aTex;
        gl_Position = vec4(aPos, 0.0, 1.0);
    }
)";

std::array<float, 3> WorkingSpaceLumaWeights(Raw::RawWorkingSpace workingSpace) {
    if (workingSpace == Raw::RawWorkingSpace::LinearSrgbD65) {
        return { 0.2126f, 0.7152f, 0.0722f };
    }
    return { 0.2627f, 0.6780f, 0.0593f };
}

bool ReadTextureToFloatRgba(
    unsigned int texture,
    int width,
    int height,
    std::vector<float>& outPixels) {
    if (texture == 0 || width <= 0 || height <= 0) {
        return false;
    }
    std::size_t elementCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            width, height, 4, elementCount)) {
        return false;
    }
    try {
        outPixels.assign(elementCount, 0.0f);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }

    const Stack::Renderer::GLState::FramebufferState savedFramebufferState;
    const Stack::Renderer::GLState::PixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();
    const unsigned int framebuffer = GLHelpers::CreateFBO(texture);
    if (framebuffer == 0) {
        savedPackState.Restore();
        outPixels.clear();
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(
        0, 0, width, height, GL_RGBA, GL_FLOAT, outPixels.data());
    const GLenum error = glGetError();
    savedPackState.Restore();
    savedFramebufferState.Restore();
    glDeleteFramebuffers(1, &framebuffer);
    if (error != GL_NO_ERROR) {
        outPixels.clear();
    }
    return error == GL_NO_ERROR;
}

unsigned int UploadFloatRgbaTexture(
    int width,
    int height,
    const std::vector<float>& pixels) {
    std::size_t elementCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            width, height, 4, elementCount) ||
        pixels.size() != elementCount) {
        return 0;
    }
    const unsigned int texture = GLHelpers::CreateEmptyTexture(width, height);
    if (texture == 0) {
        return 0;
    }
    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    glBindTexture(GL_TEXTURE_2D, texture);
    savedUnpackState.ConfigureTightCpuUpload();
    while (glGetError() != GL_NO_ERROR) {
    }
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        width,
        height,
        GL_RGBA,
        GL_FLOAT,
        pixels.data());
    const GLenum error = glGetError();
    savedUnpackState.Restore();
    savedTexture.Restore();
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

std::size_t RestormerNeutralFingerprint(
    std::size_t inputFingerprint,
    const Stack::RawRecipe::RawRgbDenoiseRecipe& settings) {
    std::size_t result = inputFingerprint;
    const auto combine = [&](const std::string& value) {
        const std::size_t hash = std::hash<std::string> {}(value);
        result ^= hash + 0x9e3779b97f4a7c15ULL +
            (result << 6U) + (result >> 2U);
    };
    combine(Stack::RawRecipe::RgbDenoiseMethodStableString(settings.method));
    combine(settings.packageId);
    combine(settings.packageVersion);
    combine(settings.modelSha256);
    combine(settings.adapterVersion);
    return result == 0 ? 1 : result;
}

std::size_t RestormerApplicationFingerprint(
    std::size_t modelFingerprint,
    Raw::RawWorkingSpace workingSpace,
    const Stack::RawRecipe::RawRgbDenoiseRecipe& settings) {
    std::size_t result = modelFingerprint;
    const auto combine = [&](std::size_t value) {
        result ^= value + 0x9e3779b97f4a7c15ULL +
            (result << 6U) + (result >> 2U);
    };
    combine(static_cast<std::size_t>(workingSpace));
    combine(static_cast<std::size_t>(settings.mapping));
    combine(std::hash<float> {}(settings.colorNoise));
    combine(std::hash<float> {}(settings.luminanceNoise));
    combine(std::hash<float> {}(settings.detailProtection));
    return result == 0 ? 1 : result;
}

double MeanAbsoluteRgbDifference(
    const std::vector<float>& a,
    const std::vector<float>& b,
    int channels,
    int comparedChannels) {
    if (channels <= 0 || comparedChannels <= 0 ||
        a.size() != b.size() || a.empty()) {
        return 0.0;
    }
    const std::size_t pixelCount =
        a.size() / static_cast<std::size_t>(channels);
    if (pixelCount == 0) {
        return 0.0;
    }
    double total = 0.0;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const std::size_t base =
            pixel * static_cast<std::size_t>(channels);
        for (int channel = 0; channel < comparedChannels; ++channel) {
            total += std::abs(static_cast<double>(
                a[base + static_cast<std::size_t>(channel)] -
                b[base + static_cast<std::size_t>(channel)]));
        }
    }
    return total /
        static_cast<double>(pixelCount * static_cast<std::size_t>(comparedChannels));
}

} // namespace

bool RenderPipeline::IsRawRgbDenoiseAsyncCompletionReady() const {
    return m_RestormerAsyncPending &&
        m_RestormerAsyncFuture.valid() &&
        m_RestormerAsyncFuture.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready;
}

bool RenderPipeline::ConsumeRawRgbDenoiseAsyncCompletion() {
    if (m_RestormerAsyncPending &&
        !m_RestormerAsyncFuture.valid()) {
        const std::size_t failedFingerprint =
            m_RestormerAsyncModelFingerprint;
        m_RestormerAsyncPending = false;
        m_RestormerAsyncModelFingerprint = 0;
        m_RestormerAsyncApplicationFingerprint = 0;
        m_RestormerAsyncCancel.reset();
        m_RestormerLastCompletedModelFingerprint =
            failedFingerprint;
        SetStringNoThrow(
            m_RestormerLastCompletedError,
            "The asynchronous AI denoise task became unavailable.");
        m_LastRawRgbDenoiseStatus.clear();
        InvalidateGraphCaches();
        return true;
    }
    if (!IsRawRgbDenoiseAsyncCompletionReady()) {
        return false;
    }

    const std::size_t failedFingerprint =
        m_RestormerAsyncModelFingerprint;
    RawRgbDenoiseAsyncResult result;
    try {
        result = m_RestormerAsyncFuture.get();
    } catch (const std::exception& error) {
        m_RestormerAsyncPending = false;
        m_RestormerAsyncModelFingerprint = 0;
        m_RestormerAsyncApplicationFingerprint = 0;
        m_RestormerAsyncCancel.reset();
        m_RestormerLastCompletedModelFingerprint =
            failedFingerprint;
        SetStringNoThrow(
            m_RestormerLastCompletedError,
            "Asynchronous AI denoise failed.");
        std::cerr << "[RAW] Asynchronous AI denoise threw: "
                  << error.what() << "\n";
        m_LastRawRgbDenoiseStatus.clear();
        InvalidateGraphCaches();
        return true;
    } catch (...) {
        m_RestormerAsyncPending = false;
        m_RestormerAsyncModelFingerprint = 0;
        m_RestormerAsyncApplicationFingerprint = 0;
        m_RestormerAsyncCancel.reset();
        m_RestormerLastCompletedModelFingerprint =
            failedFingerprint;
        SetStringNoThrow(
            m_RestormerLastCompletedError,
            "Asynchronous AI denoise failed with an unknown exception.");
        m_LastRawRgbDenoiseStatus.clear();
        InvalidateGraphCaches();
        return true;
    }
    m_RestormerAsyncPending = false;
    m_RestormerAsyncModelFingerprint = 0;
    m_RestormerAsyncApplicationFingerprint = 0;
    m_RestormerAsyncCancel.reset();
    m_RestormerLastCompletedModelFingerprint = result.modelFingerprint;
    m_RestormerLastCompletedError.clear();

    if (result.modelOutputSrgbProxy &&
        !result.modelOutputSrgbProxy->empty()) {
        m_RestormerNeutralCacheFingerprint = result.modelFingerprint;
        m_RestormerNeutralCacheWidth = result.width;
        m_RestormerNeutralCacheHeight = result.height;
        m_RestormerNeutralCacheOutputProxy =
            std::move(result.modelOutputSrgbProxy);
    }

    if (result.ok && result.outputRgba && !result.outputRgba->empty()) {
        m_RestormerAppliedCacheFingerprint =
            result.applicationFingerprint;
        m_RestormerAppliedCacheWidth = result.width;
        m_RestormerAppliedCacheHeight = result.height;
        m_RestormerAppliedCacheRgba = std::move(result.outputRgba);

        std::ostringstream status;
        status << "AI denoise ready";
        if (!result.provider.empty()) {
            status << " on " << result.provider;
        }
        if (result.inferenceMilliseconds > 0.0) {
            status << " in " << std::fixed << std::setprecision(0)
                   << result.inferenceMilliseconds << " ms";
        }
        status << " (model change " << std::scientific
               << std::setprecision(2) << result.meanAbsoluteModelDelta
               << ", applied change " << result.meanAbsoluteSceneDelta;
        if (std::abs(result.inputExposureGain - 1.0f) > 0.01f) {
            status << ", model exposure " << std::fixed
                   << std::setprecision(2) << result.inputExposureGain << "x";
        }
        status
               << ").";
        m_LastRawRgbDenoiseStatus = status.str();
        std::cerr << "[RAW] " << m_LastRawRgbDenoiseStatus << "\n";
    } else if (result.cancelled) {
        m_LastRawRgbDenoiseStatus =
            "AI denoise superseded; preparing the newest edit.";
    } else {
        m_RestormerLastCompletedError = result.error.empty()
            ? "Restormer inference failed."
            : result.error;
        m_LastRawRgbDenoiseStatus.clear();
        std::cerr << "[RAW] Restormer asynchronous denoise failed: "
                  << m_RestormerLastCompletedError << "\n";
    }

    // The provisional pass-through may have populated downstream graph
    // caches with the same authored recipe fingerprint. Completion changes
    // the materialized image without changing the recipe, so invalidate those
    // caches before the follow-up render.
    InvalidateGraphCaches();
    return true;
}

void RenderPipeline::EnsureRawDevelopmentRgbDenoisePrograms() {
    static const char* convertFragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform vec3 uLumaWeights;

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            float y = dot(color.rgb, uLumaWeights);
            FragColor = vec4(y, color.b - y, color.r - y, color.a);
        }
    )";

    static const char* blurFragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform vec2 uTexelStep;
        uniform float uDetailProtection;

        float guideEv(float y) {
            return log2(max(y, 0.0000152587890625));
        }

        void main() {
            const float kernel[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);
            vec4 center = texture(uInputImage, vTexCoord);
            float centerEv = guideEv(center.r);
            float guideSigma = mix(0.75, 0.16, clamp(uDetailProtection, 0.0, 1.0));
            vec4 sum = vec4(0.0);
            float weightSum = 0.0;
            for (int i = 0; i < 5; ++i) {
                float offset = float(i - 2);
                vec2 uv = clamp(vTexCoord + uTexelStep * offset, vec2(0.0), vec2(1.0));
                vec4 sampleValue = texture(uInputImage, uv);
                float deltaEv = guideEv(sampleValue.r) - centerEv;
                float edgeWeight = exp(-0.5 * deltaEv * deltaEv /
                    max(guideSigma * guideSigma, 0.000001));
                float weight = kernel[i] * edgeWeight;
                sum += sampleValue * weight;
                weightSum += weight;
            }
            FragColor = sum / max(weightSum, 0.000001);
        }
    )";

    static const char* bandFragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uFinerImage;
        uniform sampler2D uCoarserImage;
        uniform sampler2D uAccumulatorImage;
        uniform vec2 uTexelSize;
        uniform int uHasAccumulator;
        uniform float uLuminanceNoise;
        uniform float uColorNoise;
        uniform float uDetailProtection;
        uniform float uScaleWeight;

        vec3 detailAt(vec2 uv) {
            return texture(uFinerImage, uv).rgb -
                texture(uCoarserImage, uv).rgb;
        }

        float median9(float values[9]) {
            for (int i = 0; i < 8; ++i) {
                for (int j = i + 1; j < 9; ++j) {
                    if (values[j] < values[i]) {
                        float temporary = values[i];
                        values[i] = values[j];
                        values[j] = temporary;
                    }
                }
            }
            return values[4];
        }

        float bayesThreshold(float sigma, float variance) {
            float signalSigma = sqrt(max(variance - sigma * sigma, 0.00000001));
            return min(3.0 * sigma, sigma * sigma / signalSigma);
        }

        float softThreshold(float value, float threshold) {
            return sign(value) * max(abs(value) - threshold, 0.0);
        }

        float guideEv(float y) {
            return log2(max(y, 0.0000152587890625));
        }

        void main() {
            float absY[9];
            float absCb[9];
            float absCr[9];
            vec3 squareSum = vec3(0.0);
            int sampleIndex = 0;
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    vec2 uv = clamp(
                        vTexCoord + vec2(float(x), float(y)) * uTexelSize,
                        vec2(0.0),
                        vec2(1.0));
                    vec3 detail = detailAt(uv);
                    absY[sampleIndex] = abs(detail.r);
                    absCb[sampleIndex] = abs(detail.g);
                    absCr[sampleIndex] = abs(detail.b);
                    squareSum += detail * detail;
                    ++sampleIndex;
                }
            }

            vec3 sigma = 1.4826 * vec3(
                median9(absY),
                median9(absCb),
                median9(absCr));
            vec3 variance = squareSum / 9.0;
            vec3 threshold = vec3(
                uLuminanceNoise * bayesThreshold(sigma.r, variance.r),
                1.25 * uColorNoise * bayesThreshold(sigma.g, variance.g),
                1.25 * uColorNoise * bayesThreshold(sigma.b, variance.b));
            threshold *= uScaleWeight;

            float centerEv = guideEv(texture(uFinerImage, vTexCoord).r);
            float edge = 0.0;
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord + vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).r) - centerEv));
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord - vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).r) - centerEv));
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord + vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).r) - centerEv));
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord - vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).r) - centerEv));
            float structure = smoothstep(0.025, 0.30, edge);
            float protection = clamp(uDetailProtection, 0.0, 1.0) * structure;
            threshold *= mix(1.0, 0.08, protection);

            vec3 detail = detailAt(vTexCoord);
            vec3 filtered = vec3(
                softThreshold(detail.r, threshold.r),
                softThreshold(detail.g, threshold.g),
                softThreshold(detail.b, threshold.b));
            vec3 accumulated = uHasAccumulator != 0
                ? texture(uAccumulatorImage, vTexCoord).rgb
                : vec3(0.0);
            FragColor = vec4(accumulated + filtered, 1.0);
        }
    )";

    static const char* reconstructFragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uCoarseImage;
        uniform sampler2D uAccumulatorImage;
        uniform vec3 uLumaWeights;

        void main() {
            vec4 coarse = texture(uCoarseImage, vTexCoord);
            vec3 opponent = coarse.rgb + texture(uAccumulatorImage, vTexCoord).rgb;
            float y = opponent.r;
            float b = y + opponent.g;
            float r = y + opponent.b;
            float g = (y - uLumaWeights.r * r - uLumaWeights.b * b) /
                max(uLumaWeights.g, 0.000001);
            FragColor = vec4(r, g, b, coarse.a);
        }
    )";

    if (!m_RawDevelopmentRgbDenoiseConvertProgram) {
        m_RawDevelopmentRgbDenoiseConvertProgram =
            GLHelpers::CreateShaderProgram(kFullscreenVertexShader, convertFragment);
    }
    if (!m_RawDevelopmentRgbDenoiseBlurProgram) {
        m_RawDevelopmentRgbDenoiseBlurProgram =
            GLHelpers::CreateShaderProgram(kFullscreenVertexShader, blurFragment);
    }
    if (!m_RawDevelopmentRgbDenoiseBandProgram) {
        m_RawDevelopmentRgbDenoiseBandProgram =
            GLHelpers::CreateShaderProgram(kFullscreenVertexShader, bandFragment);
    }
    if (!m_RawDevelopmentRgbDenoiseReconstructProgram) {
        m_RawDevelopmentRgbDenoiseReconstructProgram =
            GLHelpers::CreateShaderProgram(kFullscreenVertexShader, reconstructFragment);
    }
}

void RenderPipeline::EnsureRawDevelopmentExposureProgram() {
    static const char* fragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uExposureScale;

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            FragColor = vec4(color.rgb * uExposureScale, color.a);
        }
    )";
    if (!m_RawDevelopmentExposureProgram) {
        m_RawDevelopmentExposureProgram =
            GLHelpers::CreateShaderProgram(kFullscreenVertexShader, fragment);
    }
}

unsigned int RenderPipeline::RenderRawDevelopmentRgbDenoise(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawRgbDenoiseRecipe& requestedSettings,
    Raw::RawWorkingSpace workingSpace,
    std::size_t neutralInputFingerprint) {
    const Stack::RawRecipe::RawRgbDenoiseRecipe settings =
        Stack::RawRecipe::SanitizeRgbDenoiseRecipe(requestedSettings);
    m_LastRawRgbDenoiseError.clear();
    if (inputTexture == 0 || !settings.enabled || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    const bool restormer =
        settings.method !=
        Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    if (restormer) {
        const std::size_t modelFingerprint =
            RestormerNeutralFingerprint(
                neutralInputFingerprint, settings);
        const std::size_t applicationFingerprint =
            RestormerApplicationFingerprint(
                modelFingerprint, workingSpace, settings);

        if (m_RawRgbDenoiseAsyncEnabled) {
            const bool appliedCacheHit =
                m_RestormerAppliedCacheFingerprint ==
                    applicationFingerprint &&
                m_RestormerAppliedCacheWidth == m_Width &&
                m_RestormerAppliedCacheHeight == m_Height &&
                m_RestormerAppliedCacheRgba &&
                m_RestormerAppliedCacheRgba->size() ==
                    static_cast<std::size_t>(m_Width) *
                        static_cast<std::size_t>(m_Height) * 4U;
            if (appliedCacheHit) {
                return UploadFloatRgbaTexture(
                    m_Width,
                    m_Height,
                    *m_RestormerAppliedCacheRgba);
            }

            if (m_RestormerAsyncPending) {
                if (m_RestormerAsyncModelFingerprint !=
                        modelFingerprint &&
                    m_RestormerAsyncCancel) {
                    m_RestormerAsyncCancel->store(
                        true, std::memory_order_relaxed);
                    m_LastRawRgbDenoiseStatus =
                        "Cancelling stale AI denoise; the newest edit is queued.";
                } else {
                    m_LastRawRgbDenoiseStatus =
                        "AI denoise updating in the background...";
                }
                // Preserve a responsive, truthful before-state until the
                // external model and adapter finish. This copy is deliberately
                // transient and is not accepted into persistent graph caches.
                return RenderRawDevelopmentExposure(inputTexture, 0.0f);
            }

            if (m_RestormerLastCompletedModelFingerprint ==
                    modelFingerprint &&
                !m_RestormerLastCompletedError.empty()) {
                m_LastRawRgbDenoiseError =
                    m_RestormerLastCompletedError;
                return 0;
            }
        }

        std::vector<float> sourceRgba;
        if (!ReadTextureToFloatRgba(
                inputTexture, m_Width, m_Height, sourceRgba)) {
            m_LastRawRgbDenoiseError =
                "Stack could not read the neutral scene-linear texture.";
            return 0;
        }
        std::vector<float> inputProxy;
        const Stack::RawRestormer::AdapterResult proxyResult =
            Stack::RawRestormer::BuildInputProxy(
                sourceRgba,
                m_Width,
                m_Height,
                workingSpace,
                inputProxy);
        if (!proxyResult.ok) {
            m_LastRawRgbDenoiseError = proxyResult.error;
            return 0;
        }

        const bool modelCacheHit =
            m_RestormerNeutralCacheFingerprint == modelFingerprint &&
            m_RestormerNeutralCacheWidth == m_Width &&
            m_RestormerNeutralCacheHeight == m_Height &&
            m_RestormerNeutralCacheOutputProxy &&
            m_RestormerNeutralCacheOutputProxy->size() ==
                inputProxy.size();

        if (m_RawRgbDenoiseAsyncEnabled) {
            auto source =
                std::make_shared<const std::vector<float>>(
                    std::move(sourceRgba));
            auto proxy =
                std::make_shared<const std::vector<float>>(
                    std::move(inputProxy));
            std::shared_ptr<const std::vector<float>> cachedModel =
                modelCacheHit
                    ? m_RestormerNeutralCacheOutputProxy
                    : nullptr;
            const int width = m_Width;
            const int height = m_Height;
            const Stack::Restormer::Quality quality =
                m_PreviewMaxDimension > 0
                    ? Stack::Restormer::Quality::InteractivePreview
                    : Stack::Restormer::Quality::Settled;
            const float inputExposureGain =
                proxyResult.inputExposureGain;
            const std::uint64_t generation = m_RenderGeneration;
            auto cancel = std::make_shared<std::atomic<bool>>(false);

            m_RestormerAsyncModelFingerprint = modelFingerprint;
            m_RestormerAsyncApplicationFingerprint =
                applicationFingerprint;
            m_RestormerAsyncCancel = cancel;
            m_RestormerLastCompletedModelFingerprint = 0;
            m_RestormerLastCompletedError.clear();
            m_LastRawRgbDenoiseStatus =
                modelCacheHit
                    ? "Applying AI denoise controls in the background..."
                    : "AI denoise updating in the background...";

            try {
                m_RestormerAsyncFuture = std::async(
                    std::launch::async,
                    [settings,
                     workingSpace,
                     modelFingerprint,
                     applicationFingerprint,
                     width,
                     height,
                     inputExposureGain,
                     quality,
                     generation,
                     cancel,
                     source,
                     proxy,
                     cachedModel]() mutable {
                    RawRgbDenoiseAsyncResult result;
                    result.modelFingerprint = modelFingerprint;
                    result.applicationFingerprint =
                        applicationFingerprint;
                    result.width = width;
                    result.height = height;
                    result.inputExposureGain = inputExposureGain;

                    std::shared_ptr<const std::vector<float>> modelOutput =
                        std::move(cachedModel);
                    if (!modelOutput) {
                        const Stack::Restormer::DenoiseResult modelResult =
                            Stack::Restormer::Client::Instance().Denoise(
                                settings,
                                *proxy,
                                width,
                                height,
                                quality,
                                generation,
                                [cancel]() {
                                    return cancel->load(
                                        std::memory_order_relaxed);
                                });
                        result.provider = modelResult.provider;
                        result.inferenceMilliseconds =
                            modelResult.inferenceMilliseconds;
                        result.completedTiles = modelResult.completedTiles;
                        result.totalTiles = modelResult.totalTiles;
                        if (!modelResult.ok) {
                            result.cancelled =
                                modelResult.cancelled ||
                                cancel->load(
                                    std::memory_order_relaxed);
                            result.error = modelResult.error;
                            return result;
                        }
                        modelOutput =
                            std::make_shared<const std::vector<float>>(
                                std::move(modelResult.outputSrgbProxy));
                    }
                    result.modelOutputSrgbProxy = modelOutput;
                    result.meanAbsoluteModelDelta =
                        MeanAbsoluteRgbDifference(
                            *proxy, *modelOutput, 3, 3);

                    if (cancel->load(std::memory_order_relaxed)) {
                        result.cancelled = true;
                        return result;
                    }

                    std::vector<float> applied;
                    const Stack::RawRestormer::AdapterResult applyResult =
                        Stack::RawRestormer::ApplyOutput(
                            *source,
                            *proxy,
                            *modelOutput,
                            width,
                            height,
                            workingSpace,
                            settings,
                            applied);
                    if (!applyResult.ok) {
                        result.error = applyResult.error;
                        return result;
                    }
                    result.meanAbsoluteSceneDelta =
                        MeanAbsoluteRgbDifference(
                            *source, applied, 4, 3);
                    result.outputRgba =
                        std::make_shared<const std::vector<float>>(
                            std::move(applied));
                    result.ok = true;
                    return result;
                    });
                m_RestormerAsyncPending = true;
            } catch (const std::exception& error) {
                m_RestormerAsyncPending = false;
                m_RestormerAsyncModelFingerprint = 0;
                m_RestormerAsyncApplicationFingerprint = 0;
                m_RestormerAsyncCancel.reset();
                m_RestormerLastCompletedModelFingerprint =
                    modelFingerprint;
                SetStringNoThrow(
                    m_RestormerLastCompletedError,
                    "Could not start asynchronous AI denoise.");
                std::cerr
                    << "[RAW] Could not start asynchronous AI denoise: "
                    << error.what() << "\n";
                m_LastRawRgbDenoiseStatus.clear();
            } catch (...) {
                m_RestormerAsyncPending = false;
                m_RestormerAsyncModelFingerprint = 0;
                m_RestormerAsyncApplicationFingerprint = 0;
                m_RestormerAsyncCancel.reset();
                m_RestormerLastCompletedModelFingerprint =
                    modelFingerprint;
                SetStringNoThrow(
                    m_RestormerLastCompletedError,
                    "Could not start asynchronous AI denoise.");
                m_LastRawRgbDenoiseStatus.clear();
            }

            return RenderRawDevelopmentExposure(inputTexture, 0.0f);
        }

        if (!modelCacheHit) {
            const Stack::Restormer::DenoiseResult modelResult =
                Stack::Restormer::Client::Instance().Denoise(
                    settings,
                    inputProxy,
                    m_Width,
                    m_Height,
                    m_PreviewMaxDimension > 0
                        ? Stack::Restormer::Quality::InteractivePreview
                        : Stack::Restormer::Quality::Settled,
                    m_RenderGeneration,
                    m_ShouldCancelRender);
            if (!modelResult.ok) {
                m_LastRawRgbDenoiseError = modelResult.cancelled
                    ? "AI denoise was superseded by a newer edit."
                    : modelResult.error;
                return 0;
            }
            m_RestormerNeutralCacheFingerprint = modelFingerprint;
            m_RestormerNeutralCacheWidth = m_Width;
            m_RestormerNeutralCacheHeight = m_Height;
            m_RestormerNeutralCacheOutputProxy =
                std::make_shared<const std::vector<float>>(
                    modelResult.outputSrgbProxy);
        }

        std::vector<float> outputRgba;
        const Stack::RawRestormer::AdapterResult applyResult =
            Stack::RawRestormer::ApplyOutput(
                sourceRgba,
                inputProxy,
                *m_RestormerNeutralCacheOutputProxy,
                m_Width,
                m_Height,
                workingSpace,
                settings,
                outputRgba);
        if (!applyResult.ok) {
            m_LastRawRgbDenoiseError = applyResult.error;
            return 0;
        }
        const unsigned int outputTexture =
            UploadFloatRgbaTexture(m_Width, m_Height, outputRgba);
        if (outputTexture == 0) {
            m_LastRawRgbDenoiseError =
                "Stack could not upload the Restormer scene-linear result.";
        }
        return outputTexture;
    }

    EnsureRawDevelopmentRgbDenoisePrograms();
    if (!m_RawDevelopmentRgbDenoiseConvertProgram ||
        !m_RawDevelopmentRgbDenoiseBlurProgram ||
        !m_RawDevelopmentRgbDenoiseBandProgram ||
        !m_RawDevelopmentRgbDenoiseReconstructProgram) {
        return 0;
    }

    const std::array<float, 3> lumaWeights =
        WorkingSpaceLumaWeights(workingSpace);
    const float texelX = 1.0f / static_cast<float>(std::max(1, m_Width));
    const float texelY = 1.0f / static_cast<float>(std::max(1, m_Height));
    std::vector<unsigned int> liveTargets;
    auto acquireTarget = [&]() {
        const unsigned int texture = AcquireGraphTransientTarget();
        if (texture != 0) {
            liveTargets.push_back(texture);
        }
        return texture;
    };
    auto releaseTarget = [&](unsigned int texture) {
        if (texture == 0) {
            return;
        }
        ReleaseGraphTransientTarget(texture);
        liveTargets.erase(
            std::remove(liveTargets.begin(), liveTargets.end(), texture),
            liveTargets.end());
    };
    auto releaseAll = [&]() {
        for (const unsigned int texture : liveTargets) {
            ReleaseGraphTransientTarget(texture);
        }
        liveTargets.clear();
    };

    unsigned int finerTexture = acquireTarget();
    if (finerTexture == 0 ||
        !RenderIntoGraphTargetTexture(finerTexture, [&](unsigned int) {
            glUseProgram(m_RawDevelopmentRgbDenoiseConvertProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseConvertProgram, "uInputImage"),
                0);
            glUniform3f(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseConvertProgram, "uLumaWeights"),
                lumaWeights[0],
                lumaWeights[1],
                lumaWeights[2]);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        })) {
        releaseAll();
        return 0;
    }

    unsigned int accumulatorTexture = 0;
    constexpr int gaps[3] = { 1, 2, 4 };
    constexpr float scaleWeights[3] = { 0.75f, 1.0f, 1.25f };
    for (int scaleIndex = 0; scaleIndex < 3; ++scaleIndex) {
        const int gap = gaps[scaleIndex];
        unsigned int horizontalTexture = acquireTarget();
        if (horizontalTexture == 0 ||
            !RenderIntoGraphTargetTexture(horizontalTexture, [&](unsigned int) {
                glUseProgram(m_RawDevelopmentRgbDenoiseBlurProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, finerTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uInputImage"),
                    0);
                glUniform2f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uTexelStep"),
                    texelX * static_cast<float>(gap),
                    0.0f);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uDetailProtection"),
                    settings.detailProtection);
                m_Quad.Draw();
                glBindTexture(GL_TEXTURE_2D, 0);
                glUseProgram(0);
            })) {
            releaseAll();
            return 0;
        }

        unsigned int coarserTexture = acquireTarget();
        if (coarserTexture == 0 ||
            !RenderIntoGraphTargetTexture(coarserTexture, [&](unsigned int) {
                glUseProgram(m_RawDevelopmentRgbDenoiseBlurProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, horizontalTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uInputImage"),
                    0);
                glUniform2f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uTexelStep"),
                    0.0f,
                    texelY * static_cast<float>(gap));
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uDetailProtection"),
                    settings.detailProtection);
                m_Quad.Draw();
                glBindTexture(GL_TEXTURE_2D, 0);
                glUseProgram(0);
            })) {
            releaseAll();
            return 0;
        }
        releaseTarget(horizontalTexture);

        unsigned int nextAccumulator = acquireTarget();
        if (nextAccumulator == 0 ||
            !RenderIntoGraphTargetTexture(nextAccumulator, [&](unsigned int) {
                glUseProgram(m_RawDevelopmentRgbDenoiseBandProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, finerTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uFinerImage"),
                    0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, coarserTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uCoarserImage"),
                    1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, accumulatorTexture);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uAccumulatorImage"),
                    2);
                glUniform2f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uTexelSize"),
                    texelX,
                    texelY);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uHasAccumulator"),
                    accumulatorTexture != 0 ? 1 : 0);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uLuminanceNoise"),
                    settings.luminanceNoise);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uColorNoise"),
                    settings.colorNoise);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uDetailProtection"),
                    settings.detailProtection);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uScaleWeight"),
                    scaleWeights[scaleIndex]);
                m_Quad.Draw();
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                glUseProgram(0);
            })) {
            releaseAll();
            return 0;
        }

        releaseTarget(finerTexture);
        releaseTarget(accumulatorTexture);
        finerTexture = coarserTexture;
        accumulatorTexture = nextAccumulator;
    }

    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    const bool reconstructed =
        outputTexture != 0 &&
        RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
            glUseProgram(m_RawDevelopmentRgbDenoiseReconstructProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, finerTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uCoarseImage"),
                0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, accumulatorTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uAccumulatorImage"),
                1);
            glUniform3f(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uLumaWeights"),
                lumaWeights[0],
                lumaWeights[1],
                lumaWeights[2]);
            m_Quad.Draw();
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    releaseAll();
    if (!reconstructed) {
        if (outputTexture != 0) {
            glDeleteTextures(1, &outputTexture);
        }
        return 0;
    }
    return outputTexture;
}

unsigned int RenderPipeline::RenderRawDevelopmentExposure(
    unsigned int inputTexture,
    float exposureEv) {
    if (inputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }
    EnsureRawDevelopmentExposureProgram();
    if (!m_RawDevelopmentExposureProgram) {
        return 0;
    }

    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    const float exposureScale =
        std::exp2(std::clamp(exposureEv, -24.0f, 24.0f));
    const bool rendered =
        outputTexture != 0 &&
        RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
            glUseProgram(m_RawDevelopmentExposureProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentExposureProgram, "uInputImage"),
                0);
            glUniform1f(
                glGetUniformLocation(m_RawDevelopmentExposureProgram, "uExposureScale"),
                exposureScale);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    if (!rendered) {
        if (outputTexture != 0) {
            glDeleteTextures(1, &outputTexture);
        }
        return 0;
    }
    return outputTexture;
}
