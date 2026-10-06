#include "Renderer/RenderPipeline.h"

#include "Raw/Denoise/RawDenoiseBandSchedule.h"
#include "Raw/Denoise/RawDenoiseControlMap.h"
#include "Raw/RawRestormerAdapter.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/ViewportTextureCopy.h"
#include "Renderer/Internal/RawRgbDenoiseIdentity.h"
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

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_TEXTURE4
#define GL_TEXTURE4 (GL_TEXTURE0 + 4)
#endif

namespace {

using Stack::Renderer::RawDenoise::RestormerNeutralFingerprint;
using Stack::Renderer::RawDenoise::RestormerApplicationFingerprint;

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

template <typename Value>
void CombineHash(std::size_t& seed, const Value& value) {
    seed ^= std::hash<Value>{}(value) +
        static_cast<std::size_t>(0x9e3779b9u) + (seed << 6u) + (seed >> 2u);
}

std::size_t RawDenoiseDecompositionFingerprint(
    std::size_t neutralInputFingerprint,
    const Stack::RawRecipe::RawRgbDenoiseRecipe& settings,
    Raw::RawWorkingSpace workingSpace,
    const Raw::Denoise::RawRgbNoiseModel& noiseModel,
    int renderWidth,
    int renderHeight,
    int sourceWidth,
    int sourceHeight) {
    std::size_t hash = neutralInputFingerprint;
    CombineHash(hash, settings.edgeSensitivity);
    CombineHash(hash, settings.maximumStructureSize);
    CombineHash(hash, static_cast<int>(workingSpace));
    CombineHash(hash, noiseModel.profiled);
    for (int component = 0; component < 3; ++component) {
        CombineHash(hash, noiseModel.shotScale[component]);
        CombineHash(hash, noiseModel.readNoiseVariance[component]);
    }
    CombineHash(hash, renderWidth);
    CombineHash(hash, renderHeight);
    CombineHash(hash, sourceWidth);
    CombineHash(hash, sourceHeight);
    return hash == 0 ? 1 : hash;
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

unsigned int UploadFloatMapTexture(
    int width,
    int height,
    const std::vector<float>& values) {
    if (width <= 1 || height <= 1 ||
        values.size() != static_cast<std::size_t>(width * height)) {
        return 0;
    }
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) return 0;
    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    glBindTexture(GL_TEXTURE_2D, texture);
    savedUnpackState.ConfigureTightCpuUpload();
    while (glGetError() != GL_NO_ERROR) {
    }
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        width,
        height,
        0,
        GL_RED,
        GL_FLOAT,
        values.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLenum error = glGetError();
    savedUnpackState.Restore();
    savedTexture.Restore();
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

const Stack::RawRecipe::RawDenoiseControlPoint* FindDiagnosticPoint(
    const Stack::RawRecipe::RawRgbDenoiseRecipe& settings) {
    const auto& points = settings.diagnosticLayer ==
            Stack::RawRecipe::RawDenoiseMapLayer::Chroma
        ? settings.chromaMap.points
        : settings.lumaMap.points;
    const auto item = std::find_if(
        points.begin(),
        points.end(),
        [&](const Stack::RawRecipe::RawDenoiseControlPoint& point) {
            return point.id == settings.diagnosticPointId;
        });
    return item == points.end() ? nullptr : &*item;
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

void RenderPipeline::SetRawRgbDenoiseState(
    std::shared_ptr<RawRgbDenoiseState> state, bool allowAsyncStart) {
    if (!state) {
        state = m_DefaultRawRgbDenoiseState;
    }
    const bool ownerChanged = state != m_RawRgbDenoiseState;
    const bool deferredNowAdmitted = state->deferred && allowAsyncStart;
    if (deferredNowAdmitted ||
        (ownerChanged && (m_HasProvisionalRawRgbDenoiseOutput || IsRawRgbDenoiseAsyncPending() ||
            state->pending || state->deferred))) {
        // Provisional pixels use the authored recipe fingerprint. They must
        // not conceal a newly admitted job, including an identical recipe in
        // another project. Settled owners can still share fingerprint caches.
        InvalidateGraphCaches();
    }
    m_RawRgbDenoiseState = std::move(state);
    m_RawRgbDenoiseAsyncStartAllowed = allowAsyncStart;
    if (deferredNowAdmitted) {
        m_RawRgbDenoiseState->deferred = false;
    }
}

bool RenderPipeline::IsRawRgbDenoiseAsyncCompletionReady() const {
    return m_RawRgbDenoiseState->IsCompletionReady();
}

bool RenderPipeline::ConsumeRawRgbDenoiseAsyncCompletion() {
    if (m_RawRgbDenoiseState->pending &&
        !m_RawRgbDenoiseState->future.valid()) {
        const std::size_t failedFingerprint =
            m_RawRgbDenoiseState->modelFingerprint;
        m_RawRgbDenoiseState->pending = false;
        m_RawRgbDenoiseState->modelFingerprint = 0;
        m_RawRgbDenoiseState->applicationFingerprint = 0;
        m_RawRgbDenoiseState->cancel.reset();
        m_RawRgbDenoiseState->lastCompletedModelFingerprint =
            failedFingerprint;
        SetStringNoThrow(
            m_RawRgbDenoiseState->lastCompletedError,
            "The asynchronous AI denoise task became unavailable.");
        m_RawRgbDenoiseState->status.clear();
        InvalidateGraphCaches();
        return true;
    }
    if (!IsRawRgbDenoiseAsyncCompletionReady()) {
        return false;
    }

    const std::size_t failedFingerprint =
        m_RawRgbDenoiseState->modelFingerprint;
    RawRgbDenoiseAsyncResult result;
    try {
        result = m_RawRgbDenoiseState->future.get();
    } catch (const std::exception& error) {
        m_RawRgbDenoiseState->pending = false;
        m_RawRgbDenoiseState->modelFingerprint = 0;
        m_RawRgbDenoiseState->applicationFingerprint = 0;
        m_RawRgbDenoiseState->cancel.reset();
        m_RawRgbDenoiseState->lastCompletedModelFingerprint =
            failedFingerprint;
        SetStringNoThrow(
            m_RawRgbDenoiseState->lastCompletedError,
            "Asynchronous AI denoise failed.");
        std::cerr << "[RAW] Asynchronous AI denoise threw: "
                  << error.what() << "\n";
        m_RawRgbDenoiseState->status.clear();
        InvalidateGraphCaches();
        return true;
    } catch (...) {
        m_RawRgbDenoiseState->pending = false;
        m_RawRgbDenoiseState->modelFingerprint = 0;
        m_RawRgbDenoiseState->applicationFingerprint = 0;
        m_RawRgbDenoiseState->cancel.reset();
        m_RawRgbDenoiseState->lastCompletedModelFingerprint =
            failedFingerprint;
        SetStringNoThrow(
            m_RawRgbDenoiseState->lastCompletedError,
            "Asynchronous AI denoise failed with an unknown exception.");
        m_RawRgbDenoiseState->status.clear();
        InvalidateGraphCaches();
        return true;
    }
    m_RawRgbDenoiseState->pending = false;
    m_RawRgbDenoiseState->modelFingerprint = 0;
    m_RawRgbDenoiseState->applicationFingerprint = 0;
    m_RawRgbDenoiseState->cancel.reset();
    m_RawRgbDenoiseState->lastCompletedModelFingerprint = result.modelFingerprint;
    m_RawRgbDenoiseState->lastCompletedError.clear();

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
        m_RawRgbDenoiseState->status = status.str();
        std::cerr << "[RAW] " << m_RawRgbDenoiseState->status << "\n";
    } else if (result.cancelled) {
        m_RawRgbDenoiseState->status =
            "AI denoise superseded; preparing the newest edit.";
    } else {
        m_RawRgbDenoiseState->lastCompletedError = result.error.empty()
            ? "Restormer inference failed."
            : result.error;
        m_RawRgbDenoiseState->status.clear();
        std::cerr << "[RAW] Restormer asynchronous denoise failed: "
                  << m_RawRgbDenoiseState->lastCompletedError << "\n";
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
        uniform int uHasNoiseProfile;
        uniform vec3 uNoiseShotScale;
        uniform vec3 uNoiseReadVariance;

        vec3 componentNoiseSigma(float sceneLuma) {
            if (uHasNoiseProfile != 0) {
                return sqrt(max(
                    vec3(0.000000000001),
                    uNoiseShotScale * max(sceneLuma, 0.0) +
                        uNoiseReadVariance));
            }
            float lumaSigma = max(
                0.0005,
                0.0015 + 0.018 * sqrt(max(sceneLuma, 0.0)));
            return vec3(lumaSigma, 1.35 * lumaSigma, 1.35 * lumaSigma);
        }

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            float y = dot(color.rgb, uLumaWeights);
            vec3 opponent = vec3(y, color.b - y, color.r - y);
            FragColor = vec4(opponent / componentNoiseSigma(y), y);
        }
    )";

    static const char* blurFragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform sampler2D uCoverageImage;
        uniform vec2 uTexelStep;
        uniform float uEdgeSensitivity;

        void main() {
            const float kernel[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);
            vec4 center = texture(uInputImage, vTexCoord);
            vec4 sum = vec4(0.0);
            float weightSum = 0.0;
            float sensitivity = clamp(uEdgeSensitivity, 0.0, 1.0);
            float deadZone = mix(3.0, 1.25, sensitivity);
            float falloff = mix(7.0, 1.35, sensitivity);
            for (int i = 0; i < 5; ++i) {
                float offset = float(i - 2);
                vec2 uv = clamp(vTexCoord + uTexelStep * offset, vec2(0.0), vec2(1.0));
                vec4 sampleValue = texture(uInputImage, uv);
                vec3 delta = sampleValue.rgb - center.rgb;
                float distanceSquared = dot(delta, delta) * 0.5;
                float outsideNoise = max(
                    0.0,
                    distanceSquared - deadZone * deadZone);
                float edgeWeight = exp(
                    -outsideNoise / max(2.0 * falloff * falloff, 0.000001));
                float weight = kernel[i] * edgeWeight * texture(uCoverageImage, uv).a;
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
        uniform sampler2D uCoverageImage;
        uniform sampler2D uCoarserImage;
        uniform sampler2D uAccumulatorImage;
        uniform sampler2D uLumaControlMap;
        uniform sampler2D uChromaControlMap;
        uniform vec2 uTexelSize;
        uniform vec4 uMapEvRanges;
        uniform int uHasAccumulator;
        uniform float uDetailProtection;
        uniform float uBandFrequency;
        uniform float uBandBandwidth;
        uniform float uWhiteNoiseSigma;
        uniform int uHasNoiseProfile;
        uniform vec3 uNoiseShotScale;
        uniform vec3 uNoiseReadVariance;
        uniform int uDiagnosticMode;
        uniform int uDiagnosticLayer;
        uniform int uHasSelectedPoint;
        uniform vec4 uSelectedPoint;
        uniform float uSelectedPointDelta;

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
            return log2(max(y, 0.0000152587890625) / 0.18);
        }

        vec3 componentNoiseSigma(float sceneLuma) {
            if (uHasNoiseProfile != 0) {
                return sqrt(max(
                    vec3(0.000000000001),
                    uNoiseShotScale * max(sceneLuma, 0.0) +
                        uNoiseReadVariance));
            }
            float lumaSigma = max(
                0.0005,
                0.0015 + 0.018 * sqrt(max(sceneLuma, 0.0)));
            return vec3(lumaSigma, 1.35 * lumaSigma, 1.35 * lumaSigma);
        }

        float mapAt(
            sampler2D controlMap,
            vec2 evRange,
            float frequency,
            float sceneEv) {
            const float weights[5] = float[5](0.06, 0.24, 0.40, 0.24, 0.06);
            float y = clamp(
                (sceneEv - evRange.x) / max(0.01, evRange.y - evRange.x),
                0.0,
                1.0);
            float value = 0.0;
            for (int i = 0; i < 5; ++i) {
                float offset = float(i - 2) * uBandBandwidth;
                value += weights[i] * texture(
                    controlMap,
                    vec2(clamp(frequency + offset, 0.0, 1.0), y)).r;
            }
            return clamp(value, 0.0, 4.0);
        }

        float selectedPointWeightAt(float frequency, float sceneEv) {
            if (uHasSelectedPoint == 0) return 0.0;
            vec2 evRange = uDiagnosticLayer == 0
                ? uMapEvRanges.xy
                : uMapEvRanges.zw;
            bool spansAllFrequencies =
                uSelectedPoint.x <= 0.0001 ||
                uSelectedPoint.x >= 0.9999;
            bool spansAllLuminances =
                uSelectedPoint.y <= evRange.x + 0.0001 ||
                uSelectedPoint.y >= evRange.y - 0.0001;
            float df = spansAllFrequencies
                ? 0.0
                : (frequency - uSelectedPoint.x) /
                    max(0.01, uSelectedPoint.z);
            float dl = spansAllLuminances
                ? 0.0
                : (sceneEv - uSelectedPoint.y) /
                    max(0.05, uSelectedPoint.w);
            float radius = sqrt(df * df + dl * dl);
            if (radius >= 1.0) return 0.0;
            float inverse = 1.0 - radius;
            return inverse * inverse * inverse * inverse * (4.0 * radius + 1.0);
        }

        float selectedPointWeight(float sceneEv) {
            const float weights[5] = float[5](0.06, 0.24, 0.40, 0.24, 0.06);
            float value = 0.0;
            for (int i = 0; i < 5; ++i) {
                float offset = float(i - 2) * uBandBandwidth;
                value += weights[i] * selectedPointWeightAt(
                    clamp(uBandFrequency + offset, 0.0, 1.0),
                    sceneEv);
            }
            return value;
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
                    if (texture(uCoverageImage, uv).a <= 0.0) uv = vTexCoord;
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
            float centerSceneLuma = texture(uCoarserImage, vTexCoord).a;
            float centerEv = guideEv(centerSceneLuma);
            float lumaMultiplier = mapAt(
                uLumaControlMap,
                uMapEvRanges.xy,
                uBandFrequency,
                centerEv);
            float chromaMultiplier = mapAt(
                uChromaControlMap,
                uMapEvRanges.zw,
                uBandFrequency,
                centerEv);
            vec3 noiseSigma = uHasNoiseProfile != 0
                ? vec3(uWhiteNoiseSigma)
                : max(sigma, vec3(uWhiteNoiseSigma * 0.0005));
            float lumaThreshold = lumaMultiplier *
                bayesThreshold(noiseSigma.r, variance.r);
            float chromaThreshold = 1.25 * chromaMultiplier * 0.5 * (
                bayesThreshold(noiseSigma.g, variance.g) +
                bayesThreshold(noiseSigma.b, variance.b));

            float edge = 0.0;
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord + vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).a) - centerEv));
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord - vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).a) - centerEv));
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord + vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).a) - centerEv));
            edge = max(edge, abs(guideEv(texture(
                uFinerImage,
                clamp(vTexCoord - vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).a) - centerEv));
            float structure = smoothstep(0.025, 0.30, edge);
            float protection = clamp(uDetailProtection, 0.0, 1.0) * structure;
            float thresholdProtection = mix(1.0, 0.08, protection);
            lumaThreshold *= thresholdProtection;
            chromaThreshold *= thresholdProtection;

            vec3 detail = detailAt(vTexCoord);
            vec3 filtered = detail;
            filtered.r = softThreshold(detail.r, lumaThreshold);
            float chromaLength = length(detail.gb);
            filtered.gb = max(
                0.0,
                1.0 - chromaThreshold / max(chromaLength, 0.0000001)) *
                detail.gb;
            vec4 accumulated = uHasAccumulator != 0
                ? texture(uAccumulatorImage, vTexCoord)
                : vec4(0.0);
            float pointWeight = selectedPointWeight(centerEv);
            float diagnostic = 0.0;
            if (uDiagnosticMode == 1) {
                diagnostic = pointWeight;
            } else if (uDiagnosticMode == 2) {
                float lumaWithoutMultiplier = clamp(
                    lumaMultiplier -
                        (uDiagnosticLayer == 0
                            ? uSelectedPointDelta * pointWeight
                            : 0.0),
                    0.0,
                    4.0);
                float chromaWithoutMultiplier = clamp(
                    chromaMultiplier -
                        (uDiagnosticLayer == 1
                            ? uSelectedPointDelta * pointWeight
                            : 0.0),
                    0.0,
                    4.0);
                float lumaThresholdWithout = thresholdProtection *
                    lumaWithoutMultiplier *
                    bayesThreshold(noiseSigma.r, variance.r);
                float chromaThresholdWithout = thresholdProtection * 1.25 *
                    chromaWithoutMultiplier * 0.5 * (
                        bayesThreshold(noiseSigma.g, variance.g) +
                        bayesThreshold(noiseSigma.b, variance.b));
                vec3 filteredWithout = detail;
                filteredWithout.r = softThreshold(
                    detail.r, lumaThresholdWithout);
                filteredWithout.gb = max(
                    0.0,
                    1.0 - chromaThresholdWithout /
                        max(chromaLength, 0.0000001)) * detail.gb;
                diagnostic = uDiagnosticLayer == 0
                    ? abs(filtered.r - filteredWithout.r)
                    : length(filtered.gb - filteredWithout.gb);
            } else if (uDiagnosticMode == 6) {
                vec3 sceneNoiseSigma = uWhiteNoiseSigma *
                    componentNoiseSigma(centerSceneLuma);
                diagnostic = uDiagnosticLayer == 0
                    ? sceneNoiseSigma.r
                    : length(sceneNoiseSigma.gb);
            }
            float accumulatedDiagnostic = uDiagnosticMode == 1
                ? max(accumulated.a, diagnostic)
                : accumulated.a + diagnostic;
            FragColor = vec4(
                accumulated.rgb + filtered,
                accumulatedDiagnostic);
        }
    )";

    static const char* reconstructFragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uCoarseImage;
        uniform sampler2D uAccumulatorImage;
        uniform sampler2D uInputImage;
        uniform vec3 uLumaWeights;
        uniform int uDiagnosticMode;
        uniform int uHasNoiseProfile;
        uniform vec3 uNoiseShotScale;
        uniform vec3 uNoiseReadVariance;

        vec3 componentNoiseSigma(float sceneLuma) {
            if (uHasNoiseProfile != 0) {
                return sqrt(max(
                    vec3(0.000000000001),
                    uNoiseShotScale * max(sceneLuma, 0.0) +
                        uNoiseReadVariance));
            }
            float lumaSigma = max(
                0.0005,
                0.0015 + 0.018 * sqrt(max(sceneLuma, 0.0)));
            return vec3(lumaSigma, 1.35 * lumaSigma, 1.35 * lumaSigma);
        }

        void main() {
            vec4 coarse = texture(uCoarseImage, vTexCoord);
            vec4 inputColor = texture(uInputImage, vTexCoord);
            float inputLuma = dot(inputColor.rgb, uLumaWeights);
            vec3 opponent =
                (coarse.rgb + texture(uAccumulatorImage, vTexCoord).rgb) *
                componentNoiseSigma(inputLuma);
            float y = opponent.r;
            float b = y + opponent.g;
            float r = y + opponent.b;
            float g = (y - uLumaWeights.r * r - uLumaWeights.b * b) /
                max(uLumaWeights.g, 0.000001);
            // Thresholding signed opponent details can otherwise remove much
            // more positive luma detail than negative detail on an outlier
            // pixel. That turns an optional denoise operation into an
            // exposure shift (and wide-gamut negative channels can make the
            // view transform exaggerate it). Keep ordinary luminance
            // denoising, but reject only catastrophic luma excursions.
            vec3 rgb = vec3(r, g, b);
            float reconstructedLuma = dot(rgb, uLumaWeights);
            float lumaDelta = inputLuma - reconstructedLuma;
            float lumaGuard = max(abs(inputLuma) * 0.5, 0.0001);
            if (abs(lumaDelta) > lumaGuard) {
                rgb += vec3(lumaDelta);
            }
            if (uDiagnosticMode == 1) {
                float diagnostic = texture(uAccumulatorImage, vTexCoord).a;
                FragColor = vec4(vec3(clamp(diagnostic, 0.0, 1.0)), 1.0);
                return;
            }
            if (uDiagnosticMode == 2 || uDiagnosticMode == 6) {
                float diagnostic = texture(uAccumulatorImage, vTexCoord).a;
                FragColor = vec4(vec3(diagnostic * 6.0), 1.0);
                return;
            }
            vec3 difference = abs(rgb - inputColor.rgb);
            if (uDiagnosticMode == 3) {
                FragColor = vec4(difference * 6.0, 1.0);
                return;
            }
            if (uDiagnosticMode == 4) {
                FragColor = vec4(vec3(abs(dot(
                    rgb - inputColor.rgb,
                    uLumaWeights)) * 8.0), 1.0);
                return;
            }
            if (uDiagnosticMode == 5) {
                float lumaDifference = dot(
                    rgb - inputColor.rgb,
                    uLumaWeights);
                vec3 chromaDifference =
                    rgb - inputColor.rgb - vec3(lumaDifference);
                FragColor = vec4(vec3(length(chromaDifference) * 6.0), 1.0);
                return;
            }
            FragColor = vec4(rgb, inputColor.a);
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

bool RenderPipeline::ValidateRawRgbDenoiseProgramsForTesting() {
    EnsureRawDevelopmentRgbDenoisePrograms();
    const bool programsValid =
        m_RawDevelopmentRgbDenoiseConvertProgram != 0 &&
        m_RawDevelopmentRgbDenoiseBlurProgram != 0 &&
        m_RawDevelopmentRgbDenoiseBandProgram != 0 &&
        m_RawDevelopmentRgbDenoiseReconstructProgram != 0;
    if (!programsValid) {
        return false;
    }

    constexpr int width = 16;
    constexpr int height = 12;
    std::vector<float> input(
        static_cast<std::size_t>(width * height * 4), 1.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>((y * width + x) * 4);
            input[index + 0] = 0.02f + 0.75f *
                static_cast<float>(x) / static_cast<float>(width - 1);
            input[index + 1] = 0.03f + 0.60f *
                static_cast<float>(y) / static_cast<float>(height - 1);
            input[index + 2] = 0.04f + 0.35f *
                static_cast<float>((x + 2 * y) % width) /
                    static_cast<float>(width - 1);
        }
    }
    const unsigned int inputTexture =
        UploadFloatRgbaTexture(width, height, input);
    if (inputTexture == 0) {
        return false;
    }

    m_Width = width;
    m_Height = height;
    Stack::RawRecipe::RawRgbDenoiseRecipe settings;
    settings.enabled = true;
    settings.method =
        Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    settings.maximumStructureSize = 16.0f;
    settings.lumaMap.baseMultiplier = 0.0f;
    settings.chromaMap.baseMultiplier = 0.0f;
    Raw::Denoise::RawRgbNoiseModel noiseModel;
    noiseModel.profiled = true;
    noiseModel.shotScale = { 0.003f, 0.004f, 0.004f };
    noiseModel.readNoiseVariance = { 0.000004f, 0.000006f, 0.000006f };
    const unsigned int skippedTexture = RenderRawDevelopmentRgbDenoise(
        inputTexture,
        settings,
        Raw::RawWorkingSpace::LinearSrgbD65,
        1,
        noiseModel,
        {},
        width,
        height);
    const bool neutralStageSkipped = skippedTexture == 0;
    if (skippedTexture != 0) {
        glDeleteTextures(1, &skippedTexture);
    }

    settings.lumaMap.baseMultiplier = 0.5f;
    settings.chromaMap.baseMultiplier = 0.5f;
    const unsigned int outputTexture = RenderRawDevelopmentRgbDenoise(
        inputTexture,
        settings,
        Raw::RawWorkingSpace::LinearSrgbD65,
        1,
        noiseModel,
        {},
        width,
        height);
    std::vector<float> output;
    const bool readbackValid = outputTexture != 0 &&
        ReadTextureToFloatRgba(outputTexture, width, height, output) &&
        output.size() == input.size();
    bool outputValid = readbackValid;
    if (readbackValid) {
        for (std::size_t index = 0; index < input.size(); ++index) {
            if (!std::isfinite(output[index])) {
                outputValid = false;
                break;
            }
        }
    }

    settings.enabled = false;
    settings.diagnosticMode =
        Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
    const unsigned int bypassedTexture = RenderRawDevelopmentRgbDenoise(
        inputTexture,
        settings,
        Raw::RawWorkingSpace::LinearSrgbD65,
        1,
        noiseModel,
        {},
        width,
        height);
    const bool bypassSkipped = bypassedTexture == 0;
    if (bypassedTexture != 0) {
        glDeleteTextures(1, &bypassedTexture);
    }

    Stack::RawRecipe::RawDenoiseControlPoint diagnosticPoint;
    diagnosticPoint.id = 7;
    diagnosticPoint.enabled = true;
    diagnosticPoint.frequency = 0.0f;
    diagnosticPoint.sceneEv = settings.lumaMap.minimumEv;
    diagnosticPoint.frequencyRadius = 1.0f;
    diagnosticPoint.luminanceRadiusEv =
        settings.lumaMap.maximumEv - settings.lumaMap.minimumEv;
    diagnosticPoint.multiplierDelta = 1.0f;
    settings.lumaMap.points = { diagnosticPoint };
    settings.diagnosticLayer =
        Stack::RawRecipe::RawDenoiseMapLayer::Luma;
    settings.diagnosticPointId = diagnosticPoint.id;
    bool diagnosticsValid = true;
    for (int mode = 1; mode <= 6; ++mode) {
        settings.diagnosticMode =
            static_cast<Stack::RawRecipe::RawDenoiseDiagnosticMode>(mode);
        const unsigned int diagnosticTexture = RenderRawDevelopmentRgbDenoise(
            inputTexture,
            settings,
            Raw::RawWorkingSpace::LinearSrgbD65,
            1,
            noiseModel,
            {},
            width,
            height);
        std::vector<float> diagnosticOutput;
        const bool diagnosticReadback = diagnosticTexture != 0 &&
            ReadTextureToFloatRgba(
                diagnosticTexture,
                width,
                height,
                diagnosticOutput) &&
            diagnosticOutput.size() == input.size();
        float maximumRgb = 0.0f;
        if (diagnosticReadback) {
            for (std::size_t index = 0; index < diagnosticOutput.size();
                 index += 4U) {
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    const float value = diagnosticOutput[index + channel];
                    diagnosticsValid &= std::isfinite(value);
                    maximumRgb = std::max(maximumRgb, value);
                }
            }
        }
        diagnosticsValid &= diagnosticReadback && maximumRgb > 0.000001f;
        if (diagnosticTexture != 0) {
            glDeleteTextures(1, &diagnosticTexture);
        }
    }
    glDeleteTextures(1, &inputTexture);
    if (outputTexture != 0) {
        glDeleteTextures(1, &outputTexture);
    }
    return neutralStageSkipped && outputValid && bypassSkipped &&
        diagnosticsValid;
}

unsigned int RenderPipeline::RenderRawDevelopmentRgbDenoise(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawRgbDenoiseRecipe& requestedSettings,
    Raw::RawWorkingSpace workingSpace,
    std::size_t neutralInputFingerprint,
    const Raw::Denoise::RawRgbNoiseModel& noiseModel,
    const std::string& decompositionCacheKey,
    int sourceWidth,
    int sourceHeight,
    const Stack::RawRecipe::RawDevelopmentRecipe* viewportRecipe) {
    const Stack::RawRecipe::RawRgbDenoiseRecipe settings =
        Stack::RawRecipe::SanitizeRgbDenoiseRecipe(requestedSettings);
    m_RawRgbDenoiseState->error.clear();
    m_RawRgbDenoiseState->deferred = false;
    const bool diagnosticRequested =
        settings.diagnosticMode !=
            Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
    if (inputTexture == 0 || m_Width <= 0 || m_Height <= 0 ||
        (!Stack::RawRecipe::IsRgbDenoiseActive(settings) &&
            !diagnosticRequested)) {
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
            const auto nativeStages = viewportRecipe
                ? Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(*viewportRecipe,0)
                : Stack::Renderer::RawDevelopmentCache::StageFingerprints{};
            const std::size_t nativeModel = viewportRecipe
                ? RestormerNeutralFingerprint(nativeStages.rawBase,settings) : 0;
            const bool nativeJobMatches = nativeModel != 0 &&
                m_RawRgbDenoiseState->modelFingerprint == nativeModel;
            const std::size_t nativeApplication = nativeModel
                ? RestormerApplicationFingerprint(nativeModel,workingSpace,settings) : 0;
            if (m_PreviewMaxDimension > 0 && nativeApplication &&
                !diagnosticRequested &&
                m_RestormerAppliedCacheFingerprint == nativeApplication &&
                m_RestormerAppliedCacheWidth > 0 && m_RestormerAppliedCacheHeight > 0 &&
                m_RestormerAppliedCacheRgba && m_RestormerAppliedCacheRgba->size() ==
                    static_cast<std::size_t>(m_RestormerAppliedCacheWidth) * m_RestormerAppliedCacheHeight * 4u) {
                // Native inference can finish while a smaller preview owns
                // the latest command. Publish/cache that native dependency
                // once, then sample it for this edit instead of launching a
                // second model job at the preview's resolution.
                const int width = m_Width, height = m_Height;
                Stack::Renderer::ScopedGLTexture native(UploadFloatRgbaTexture(
                    m_RestormerAppliedCacheWidth,m_RestormerAppliedCacheHeight,*m_RestormerAppliedCacheRgba));
                if (!native) return 0;
                const unsigned int preview = Stack::Renderer::CopyViewportTexture(native.Get(),
                    m_RestormerAppliedCacheWidth,m_RestormerAppliedCacheHeight,width,height);
                m_Width = m_RestormerAppliedCacheWidth;
                m_Height = m_RestormerAppliedCacheHeight;
                if (StoreRawDevelopStageCacheEntry(decompositionCacheKey,native.Get(),nativeStages.neutralPlacement,true)) {
                    native.Release();
                    m_RawDevelopStageImageCache.at(decompositionCacheKey).front().viewportNativeDependency = true;
                }
                m_Width = width;
                m_Height = height;
                return preview;
            }
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

            if (m_RawRgbDenoiseState->pending) {
                if (m_RawRgbDenoiseState->modelFingerprint !=
                        modelFingerprint && !nativeJobMatches &&
                    m_RawRgbDenoiseState->cancel) {
                    m_RawRgbDenoiseState->cancel->store(
                        true, std::memory_order_relaxed);
                    m_RawRgbDenoiseState->status =
                        "Cancelling stale AI denoise; the newest edit is queued.";
                } else {
                    m_RawRgbDenoiseState->status =
                        "AI denoise updating in the background...";
                }
                // Preserve a responsive, truthful before-state until the
                // external model and adapter finish. Completion or owner
                // changes invalidate any caches populated by these pixels.
                m_HasProvisionalRawRgbDenoiseOutput = true;
                return RenderRawDevelopmentExposure(inputTexture, 0.0f);
            }

            if (m_RawRgbDenoiseState->lastCompletedModelFingerprint ==
                    modelFingerprint &&
                !m_RawRgbDenoiseState->lastCompletedError.empty()) {
                m_RawRgbDenoiseState->error =
                    m_RawRgbDenoiseState->lastCompletedError;
                return 0;
            }

            if (!m_RawRgbDenoiseAsyncStartAllowed) {
                // Defer before readback and proxy allocation. Only the owner
                // admitted by the worker may retain a full inference input.
                m_RawRgbDenoiseState->deferred = true;
                m_RawRgbDenoiseState->status =
                    "AI denoise queued; another project is processing.";
                m_HasProvisionalRawRgbDenoiseOutput = true;
                return RenderRawDevelopmentExposure(inputTexture, 0.0f);
            }
        }

        std::vector<float> sourceRgba;
        if (!ReadTextureToFloatRgba(
                inputTexture, m_Width, m_Height, sourceRgba)) {
            m_RawRgbDenoiseState->error =
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
            m_RawRgbDenoiseState->error = proxyResult.error;
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

            m_RawRgbDenoiseState->modelFingerprint = modelFingerprint;
            m_RawRgbDenoiseState->applicationFingerprint =
                applicationFingerprint;
            m_RawRgbDenoiseState->cancel = cancel;
            m_RawRgbDenoiseState->lastCompletedModelFingerprint = 0;
            m_RawRgbDenoiseState->lastCompletedError.clear();
            m_RawRgbDenoiseState->status =
                modelCacheHit
                    ? "Applying AI denoise controls in the background..."
                    : "AI denoise updating in the background...";

            try {
                m_RawRgbDenoiseState->future = std::async(
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
                m_RawRgbDenoiseState->pending = true;
            } catch (const std::exception& error) {
                m_RawRgbDenoiseState->pending = false;
                m_RawRgbDenoiseState->modelFingerprint = 0;
                m_RawRgbDenoiseState->applicationFingerprint = 0;
                m_RawRgbDenoiseState->cancel.reset();
                m_RawRgbDenoiseState->lastCompletedModelFingerprint =
                    modelFingerprint;
                SetStringNoThrow(
                    m_RawRgbDenoiseState->lastCompletedError,
                    "Could not start asynchronous AI denoise.");
                std::cerr
                    << "[RAW] Could not start asynchronous AI denoise: "
                    << error.what() << "\n";
                m_RawRgbDenoiseState->status.clear();
            } catch (...) {
                m_RawRgbDenoiseState->pending = false;
                m_RawRgbDenoiseState->modelFingerprint = 0;
                m_RawRgbDenoiseState->applicationFingerprint = 0;
                m_RawRgbDenoiseState->cancel.reset();
                m_RawRgbDenoiseState->lastCompletedModelFingerprint =
                    modelFingerprint;
                SetStringNoThrow(
                    m_RawRgbDenoiseState->lastCompletedError,
                    "Could not start asynchronous AI denoise.");
                m_RawRgbDenoiseState->status.clear();
            }

            m_HasProvisionalRawRgbDenoiseOutput = true;
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
                m_RawRgbDenoiseState->error = modelResult.cancelled
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
            m_RawRgbDenoiseState->error = applyResult.error;
            return 0;
        }
        const unsigned int outputTexture =
            UploadFloatRgbaTexture(m_Width, m_Height, outputRgba);
        if (outputTexture == 0) {
            m_RawRgbDenoiseState->error =
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
    bool hasNoiseProfile = noiseModel.profiled;
    for (int component = 0; component < 3; ++component) {
        hasNoiseProfile = hasNoiseProfile &&
            std::isfinite(noiseModel.shotScale[component]) &&
            std::isfinite(noiseModel.readNoiseVariance[component]) &&
            noiseModel.shotScale[component] >= 0.0f &&
            noiseModel.readNoiseVariance[component] >= 0.0f;
    }
    const float texelX = 1.0f / static_cast<float>(std::max(1, m_Width));
    const float texelY = 1.0f / static_cast<float>(std::max(1, m_Height));
    constexpr int mapWidth = 64;
    constexpr int mapHeight = 64;
    Stack::Renderer::ScopedGLTexture lumaControlTexture(
        UploadFloatMapTexture(
            mapWidth,
            mapHeight,
            Stack::RawRecipe::BakeRawDenoiseControlMap(
                settings.lumaMap, mapWidth, mapHeight)));
    Stack::Renderer::ScopedGLTexture chromaControlTexture(
        UploadFloatMapTexture(
            mapWidth,
            mapHeight,
            Stack::RawRecipe::BakeRawDenoiseControlMap(
                settings.chromaMap, mapWidth, mapHeight)));
    if (lumaControlTexture.Get() == 0 || chromaControlTexture.Get() == 0) {
        m_RawRgbDenoiseState->error =
            "Stack could not allocate the frequency-luminance denoise maps.";
        return 0;
    }
    const std::vector<Stack::RawRecipe::RawDenoiseBand> bands =
        Stack::RawRecipe::BuildRawDenoiseBandSchedule(
            m_Width,
            m_Height,
            sourceWidth,
            sourceHeight,
            settings.maximumStructureSize);
    const Stack::RawRecipe::RawDenoiseControlPoint* diagnosticPoint =
        FindDiagnosticPoint(settings);
    const bool retainDecomposition = !decompositionCacheKey.empty();
    const std::size_t decompositionFingerprint =
        RawDenoiseDecompositionFingerprint(
            neutralInputFingerprint,
            settings,
            workingSpace,
            noiseModel,
            m_Width,
            m_Height,
            sourceWidth,
            sourceHeight);
    const auto decompositionLevelKey = [&](int level) {
        return decompositionCacheKey + ":pyramid:" + std::to_string(level);
    };
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

    unsigned int finerTexture = 0;
    bool finerTextureIsTransient = false;
    if (retainDecomposition) {
        const CachedGraphTexture cached = FindRawDevelopStageCacheEntry(
            decompositionLevelKey(0), decompositionFingerprint);
        if (cached.texture != 0 &&
            cached.width == m_Width && cached.height == m_Height) {
            finerTexture = cached.texture;
        }
    }
    if (finerTexture == 0) {
        finerTexture = acquireTarget();
        finerTextureIsTransient = finerTexture != 0;
    }
    if (finerTexture == 0 ||
        (finerTextureIsTransient &&
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
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseConvertProgram, "uHasNoiseProfile"),
                hasNoiseProfile ? 1 : 0);
            glUniform3fv(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseConvertProgram, "uNoiseShotScale"),
                1,
                noiseModel.shotScale.data());
            glUniform3fv(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseConvertProgram, "uNoiseReadVariance"),
                1,
                noiseModel.readNoiseVariance.data());
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        }))) {
        releaseAll();
        return 0;
    }
    if (retainDecomposition && finerTextureIsTransient) {
        StoreRawDevelopStageCacheEntry(
            decompositionLevelKey(0),
            finerTexture,
            decompositionFingerprint);
    }

    unsigned int accumulatorTexture = 0;
    int bandIndex = 0;
    for (const Stack::RawRecipe::RawDenoiseBand& band : bands) {
        const int gap = band.renderGap;
        unsigned int coarserTexture = 0;
        bool coarserTextureIsTransient = false;
        if (retainDecomposition) {
            const CachedGraphTexture cached = FindRawDevelopStageCacheEntry(
                decompositionLevelKey(bandIndex + 1),
                decompositionFingerprint);
            if (cached.texture != 0 &&
                cached.width == m_Width && cached.height == m_Height) {
                coarserTexture = cached.texture;
            }
        }

        if (coarserTexture == 0) {
            unsigned int horizontalTexture = acquireTarget();
            if (horizontalTexture == 0 ||
                !RenderIntoGraphTargetTexture(horizontalTexture, [&](unsigned int) {
                glUseProgram(m_RawDevelopmentRgbDenoiseBlurProgram);
                glActiveTexture(GL_TEXTURE0 + 5);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                glUniform1i(glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uCoverageImage"), 5);
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
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uEdgeSensitivity"),
                    settings.edgeSensitivity);
                m_Quad.Draw();
                glActiveTexture(GL_TEXTURE0 + 5);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                glUseProgram(0);
            })) {
                releaseAll();
                return 0;
            }

            coarserTexture = acquireTarget();
            coarserTextureIsTransient = coarserTexture != 0;
            if (coarserTexture == 0 ||
                !RenderIntoGraphTargetTexture(coarserTexture, [&](unsigned int) {
                glUseProgram(m_RawDevelopmentRgbDenoiseBlurProgram);
                glActiveTexture(GL_TEXTURE0 + 5);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                glUniform1i(glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uCoverageImage"), 5);
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
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBlurProgram, "uEdgeSensitivity"),
                    settings.edgeSensitivity);
                m_Quad.Draw();
                glActiveTexture(GL_TEXTURE0 + 5);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                glUseProgram(0);
            })) {
                releaseAll();
                return 0;
            }
            releaseTarget(horizontalTexture);
        }

        unsigned int nextAccumulator = acquireTarget();
        if (nextAccumulator == 0 ||
            !RenderIntoGraphTargetTexture(nextAccumulator, [&](unsigned int) {
                glUseProgram(m_RawDevelopmentRgbDenoiseBandProgram);
                glActiveTexture(GL_TEXTURE0 + 5);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                glUniform1i(glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uCoverageImage"), 5);
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
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, lumaControlTexture.Get());
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uLumaControlMap"),
                    3);
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, chromaControlTexture.Get());
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uChromaControlMap"),
                    4);
                glUniform2f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uTexelSize"),
                    texelX,
                    texelY);
                glUniform4f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uMapEvRanges"),
                    settings.lumaMap.minimumEv,
                    settings.lumaMap.maximumEv,
                    settings.chromaMap.minimumEv,
                    settings.chromaMap.maximumEv);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uHasAccumulator"),
                    accumulatorTexture != 0 ? 1 : 0);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uDetailProtection"),
                    settings.detailProtection);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uBandFrequency"),
                    band.normalizedFrequency);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uBandBandwidth"),
                    band.normalizedBandwidth);
                glUniform1f(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uWhiteNoiseSigma"),
                    band.whiteNoiseSigma);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uHasNoiseProfile"),
                    hasNoiseProfile ? 1 : 0);
                glUniform3fv(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uNoiseShotScale"),
                    1,
                    noiseModel.shotScale.data());
                glUniform3fv(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uNoiseReadVariance"),
                    1,
                    noiseModel.readNoiseVariance.data());
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uDiagnosticMode"),
                    static_cast<int>(settings.diagnosticMode));
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uDiagnosticLayer"),
                    settings.diagnosticLayer == Stack::RawRecipe::RawDenoiseMapLayer::Chroma ? 1 : 0);
                glUniform1i(
                    glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uHasSelectedPoint"),
                    diagnosticPoint != nullptr ? 1 : 0);
                if (diagnosticPoint) {
                    glUniform4f(
                        glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uSelectedPoint"),
                        diagnosticPoint->frequency,
                        diagnosticPoint->sceneEv,
                        diagnosticPoint->frequencyRadius,
                        diagnosticPoint->luminanceRadiusEv);
                    glUniform1f(
                        glGetUniformLocation(m_RawDevelopmentRgbDenoiseBandProgram, "uSelectedPointDelta"),
                        diagnosticPoint->multiplierDelta);
                }
                m_Quad.Draw();
                glActiveTexture(GL_TEXTURE0 + 5);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, 0);
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

        if (retainDecomposition && coarserTextureIsTransient) {
            StoreRawDevelopStageCacheEntry(
                decompositionLevelKey(bandIndex + 1),
                coarserTexture,
                decompositionFingerprint);
        }

        if (finerTextureIsTransient) {
            releaseTarget(finerTexture);
        }
        releaseTarget(accumulatorTexture);
        finerTexture = coarserTexture;
        finerTextureIsTransient = coarserTextureIsTransient;
        accumulatorTexture = nextAccumulator;
        ++bandIndex;
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
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uInputImage"),
                2);
            glUniform3f(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uLumaWeights"),
                lumaWeights[0],
                lumaWeights[1],
                lumaWeights[2]);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uDiagnosticMode"),
                static_cast<int>(settings.diagnosticMode));
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uHasNoiseProfile"),
                hasNoiseProfile ? 1 : 0);
            glUniform3fv(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uNoiseShotScale"),
                1,
                noiseModel.shotScale.data());
            glUniform3fv(
                glGetUniformLocation(m_RawDevelopmentRgbDenoiseReconstructProgram, "uNoiseReadVariance"),
                1,
                noiseModel.readNoiseVariance.data());
            m_Quad.Draw();
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, 0);
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
