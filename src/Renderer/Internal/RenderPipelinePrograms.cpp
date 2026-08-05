#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

#ifndef GL_R32UI
#define GL_R32UI 0x8236
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_PIXEL_PACK_BUFFER_BINDING
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif

namespace {

unsigned int CreateRawLocalRangeSelectionTexture(
    int width,
    int height,
    const std::vector<std::uint32_t>& selectedBits) {
    std::size_t requiredElements = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            width, height, 1, requiredElements) ||
        selectedBits.size() != requiredElements) {
        return 0;
    }

    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    savedUnpackState.ConfigureTightCpuUpload();
    while (glGetError() != GL_NO_ERROR) {
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32UI,
        width,
        height,
        0,
        GL_RED_INTEGER,
        GL_UNSIGNED_INT,
        selectedBits.data());
    const GLenum uploadError = glGetError();
    savedUnpackState.Restore();
    savedTexture.Restore();
    if (uploadError != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

RawLocalRangeTargetPreviewCpuResult BuildRawLocalRangeTargetPreviewConnectedArea(
    std::vector<unsigned char> qualifier,
    int width,
    int height,
    float seedU,
    float seedV,
    std::uint64_t generation) {
    using PreviewClock = std::chrono::steady_clock;
    const auto floodFillStart = PreviewClock::now();
    RawLocalRangeTargetPreviewCpuResult result;
    result.generation = generation;
    result.width = width;
    result.height = height;
    if (width <= 0 || height <= 0 ||
        qualifier.size() !=
            static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height)) {
        return result;
    }

    const std::size_t pixelCount = qualifier.size();
    // The qualifier is intentionally rendered at a small proxy resolution,
    // but high-ISO RAW noise can still leave a salt-and-pepper boundary.
    // Smooth only this transient visualization mask before connectivity. The
    // authored zone and final Local Range math remain untouched.
    std::vector<unsigned char> smoothedQualifier(pixelCount, 0u);
    std::vector<std::uint32_t> integral(
        static_cast<std::size_t>(width + 1) *
            static_cast<std::size_t>(height + 1),
        0u);
    for (int y = 0; y < height; ++y) {
        std::uint32_t rowSum = 0u;
        for (int x = 0; x < width; ++x) {
            rowSum +=
                qualifier[static_cast<std::size_t>(y * width + x)];
            integral[
                static_cast<std::size_t>(y + 1) *
                    static_cast<std::size_t>(width + 1) +
                static_cast<std::size_t>(x + 1)] =
                integral[
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(width + 1) +
                    static_cast<std::size_t>(x + 1)] +
                rowSum;
        }
    }
    constexpr int kQualifierSmoothingRadius = 2;
    const std::size_t integralStride =
        static_cast<std::size_t>(width + 1);
    for (int y = 0; y < height; ++y) {
        const int y0 = std::max(0, y - kQualifierSmoothingRadius);
        const int y1 = std::min(height - 1, y + kQualifierSmoothingRadius);
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - kQualifierSmoothingRadius);
            const int x1 = std::min(width - 1, x + kQualifierSmoothingRadius);
            const std::uint32_t sum =
                integral[static_cast<std::size_t>(y1 + 1) *
                        integralStride +
                    static_cast<std::size_t>(x1 + 1)] -
                integral[static_cast<std::size_t>(y0) * integralStride +
                    static_cast<std::size_t>(x1 + 1)] -
                integral[static_cast<std::size_t>(y1 + 1) *
                        integralStride +
                    static_cast<std::size_t>(x0)] +
                integral[static_cast<std::size_t>(y0) * integralStride +
                    static_cast<std::size_t>(x0)];
            const std::uint32_t sampleCount =
                static_cast<std::uint32_t>(
                    (x1 - x0 + 1) * (y1 - y0 + 1));
            smoothedQualifier[
                static_cast<std::size_t>(y * width + x)] =
                static_cast<unsigned char>(
                    (sum + sampleCount / 2u) / sampleCount);
        }
    }

    std::vector<unsigned char> visited(pixelCount, 0u);
    result.selectedBits.assign(pixelCount, 0u);
    std::vector<int> queue;
    queue.reserve(std::min<std::size_t>(pixelCount, 512u * 512u));
    constexpr unsigned char kGrowthThreshold = 38u;
    constexpr unsigned char kStrongThreshold = 128u;
    const int searchRadius = std::max(4, std::min(width, height) / 100);
    const int seedX = std::clamp(
        static_cast<int>(std::lround(
            seedU * static_cast<float>(width - 1))),
        0,
        width - 1);
    const int seedY = std::clamp(
        static_cast<int>(std::lround(
            (1.0f - seedV) * static_cast<float>(height - 1))),
        0,
        height - 1);
    int startIndex = seedY * width + seedX;
    if (smoothedQualifier[static_cast<std::size_t>(startIndex)] <
        kStrongThreshold) {
        int bestIndex = -1;
        int bestDistanceSquared = searchRadius * searchRadius + 1;
        for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
            const int y = seedY + dy;
            if (y < 0 || y >= height) {
                continue;
            }
            for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
                const int distanceSquared = dx * dx + dy * dy;
                if (distanceSquared >= bestDistanceSquared) {
                    continue;
                }
                const int x = seedX + dx;
                if (x < 0 || x >= width) {
                    continue;
                }
                const int candidateIndex = y * width + x;
                if (smoothedQualifier[
                        static_cast<std::size_t>(candidateIndex)] >=
                    kStrongThreshold) {
                    bestIndex = candidateIndex;
                    bestDistanceSquared = distanceSquared;
                }
            }
        }
        startIndex = bestIndex;
    }

    if (startIndex >= 0) {
        queue.push_back(startIndex);
        visited[static_cast<std::size_t>(startIndex)] = 1u;
        for (std::size_t queueIndex = 0;
             queueIndex < queue.size();
             ++queueIndex) {
            const int index = queue[queueIndex];
            result.selectedBits[static_cast<std::size_t>(index)] = 1u;
            const int x = index % width;
            const int y = index / width;
            for (int dy = -1; dy <= 1; ++dy) {
                const int neighborY = y + dy;
                if (neighborY < 0 || neighborY >= height) {
                    continue;
                }
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int neighborX = x + dx;
                    if (neighborX < 0 || neighborX >= width) {
                        continue;
                    }
                    const int neighborIndex = neighborY * width + neighborX;
                    const std::size_t neighborOffset =
                        static_cast<std::size_t>(neighborIndex);
                    if (visited[neighborOffset] != 0u ||
                        smoothedQualifier[neighborOffset] <
                            kGrowthThreshold) {
                        continue;
                    }
                    visited[neighborOffset] = 1u;
                    queue.push_back(neighborIndex);
                }
            }
        }
    }
    // One majority pass removes proxy-sized holes and protrusions that would
    // otherwise turn a one-pixel contour into visible stippling. Keep the
    // original result if cleanup would erase a small legitimate component.
    const std::vector<std::uint32_t> connectedBits = result.selectedBits;
    std::size_t connectedCount = 0;
    for (std::uint32_t bit : connectedBits) {
        connectedCount += bit != 0u ? 1u : 0u;
    }
    if (connectedCount >= 16u) {
        std::size_t cleanedCount = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int selectedNeighbors = 0;
                int neighborCount = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int neighborY = y + dy;
                    if (neighborY < 0 || neighborY >= height) {
                        continue;
                    }
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int neighborX = x + dx;
                        if (neighborX < 0 || neighborX >= width) {
                            continue;
                        }
                        ++neighborCount;
                        selectedNeighbors +=
                            connectedBits[
                                static_cast<std::size_t>(
                                    neighborY * width + neighborX)] != 0u
                            ? 1
                            : 0;
                    }
                }
                const bool selected =
                    selectedNeighbors * 2 >= neighborCount;
                result.selectedBits[
                    static_cast<std::size_t>(y * width + x)] =
                    selected ? 1u : 0u;
                cleanedCount += selected ? 1u : 0u;
            }
        }
        if (cleanedCount == 0u) {
            result.selectedBits = connectedBits;
        }
    }

    result.floodFillMs =
        std::chrono::duration<float, std::milli>(
            PreviewClock::now() - floodFillStart)
            .count();
    return result;
}

int RawLocalRangeRegionMaskModeToShader(const std::string& mode) {
    if (mode == "linear-gradient") {
        return 1;
    }
    if (mode == "radial-gradient") {
        return 2;
    }
    if (mode == "luminance-range") {
        return 3;
    }
    return 0;
}

void UploadRawLocalRangeRegionMaskUniforms(
    unsigned int program,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    int width,
    int height) {
    constexpr float kDegreesToRadians = 0.01745329251994329577f;
    const int mode = localRange.regionMaskEnabled
        ? RawLocalRangeRegionMaskModeToShader(localRange.regionMaskMode)
        : 0;
    glUniform1i(glGetUniformLocation(program, "uRegionMaskEnabled"), localRange.regionMaskEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(program, "uRegionMaskMode"), mode);
    glUniform1i(glGetUniformLocation(program, "uRegionMaskInvert"), localRange.regionMaskInvert ? 1 : 0);
    glUniform2f(
        glGetUniformLocation(program, "uRegionMaskCenter"),
        localRange.regionMaskCenterX,
        localRange.regionMaskCenterY);
    glUniform1f(
        glGetUniformLocation(program, "uRegionMaskAngleRadians"),
        localRange.regionMaskAngleDegrees * kDegreesToRadians);
    glUniform1f(glGetUniformLocation(program, "uRegionMaskSize"), localRange.regionMaskSize);
    glUniform1f(glGetUniformLocation(program, "uRegionMaskFeather"), localRange.regionMaskFeather);
    glUniform2f(
        glGetUniformLocation(program, "uRegionMaskEvRange"),
        localRange.regionMaskLowEv,
        localRange.regionMaskHighEv);
    glUniform1f(
        glGetUniformLocation(program, "uImageAspect"),
        height > 0 ? static_cast<float>(std::max(width, 1)) / static_cast<float>(height) : 1.0f);
    glUniform1i(glGetUniformLocation(program, "uColorMaskEnabled"), localRange.colorMaskEnabled ? 1 : 0);
    glUniform3f(
        glGetUniformLocation(program, "uColorMaskTarget"),
        localRange.colorMaskTargetR,
        localRange.colorMaskTargetG,
        localRange.colorMaskTargetB);
    glUniform1f(glGetUniformLocation(program, "uColorMaskHueWidth"), localRange.colorMaskHueWidth);
    glUniform1f(glGetUniformLocation(program, "uColorMaskFeather"), localRange.colorMaskFeather);
    glUniform1f(glGetUniformLocation(program, "uColorMaskMinChroma"), localRange.colorMaskMinChroma);
}

void UploadRawLocalRangeTargetZoneUniforms(
    unsigned int program,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    Raw::RawWorkingSpace workingSpace) {
    const int count = std::min<int>(
        static_cast<int>(localRange.targetZones.size()),
        static_cast<int>(Stack::RawRecipe::kMaxRawLocalRangeTargetZones));
    glUniform1i(glGetUniformLocation(program, "uTargetZoneCount"), count);
    glUniform1i(
        glGetUniformLocation(program, "uTargetZoneCombineMode"),
        localRange.targetZoneCombineMode == Stack::RawRecipe::RawLocalRangeZoneCombineMode::Strongest
            ? 1
            : (localRange.targetZoneCombineMode == Stack::RawRecipe::RawLocalRangeZoneCombineMode::Blend
                    ? 2
                    : 0));
    glUniform1i(
        glGetUniformLocation(program, "uTargetZoneWorkingSpace"),
        workingSpace == Raw::RawWorkingSpace::LinearRec2020D65 ? 1 : 0);
    glUniform1f(glGetUniformLocation(program, "uLocalRangeMaxEv"), localRange.maxEv);
    for (int i = 0; i < count; ++i) {
        const Stack::RawRecipe::RawLocalRangeTargetZone& zone =
            localRange.targetZones[static_cast<std::size_t>(i)];
        char uniformName[64];
        std::snprintf(uniformName, sizeof(uniformName), "uTargetZoneTone[%d]", i);
        glUniform4f(
            glGetUniformLocation(program, uniformName),
            zone.centerEv,
            zone.coreHalfWidthEv,
            zone.featherEv,
            zone.deltaEv);
        std::snprintf(uniformName, sizeof(uniformName), "uTargetZoneColor[%d]", i);
        glUniform4f(
            glGetUniformLocation(program, uniformName),
            zone.targetUPrime,
            zone.targetVPrime,
            zone.colorRadius,
            zone.colorFeather);
        std::snprintf(uniformName, sizeof(uniformName), "uTargetZoneMeta[%d]", i);
        glUniform4f(
            glGetUniformLocation(program, uniformName),
            zone.targetChroma,
            zone.enabled ? 1.0f : 0.0f,
            zone.colorEnabled ? 1.0f : 0.0f,
            zone.scope == Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches ? 1.0f : 0.0f);
    }
}

} // namespace

void RenderPipeline::EnsureMaskPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* maskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform float uValue;
        uniform float uAngle;
        uniform float uOffset;
        uniform float uScale;
        uniform vec2 uCenter;
        uniform float uRadius;
        uniform float uFeather;
        uniform int uInvert;
        float hash(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
        }
        float noise(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            vec2 u = f * f * (3.0 - 2.0 * f);
            return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
                       mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
        }
        void main() {
            vec2 uv = vTexCoord;
            float maskValue = clamp(uValue, 0.0, 1.0);
            if (uKind == 1) {
                float radiansAngle = radians(uAngle);
                vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
                maskValue = dot(uv - vec2(0.5), dir) * max(uScale, 0.001) + 0.5 + uOffset;
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 2) {
                float d = distance(uv, uCenter);
                float feather = max(uFeather, 0.0001);
                maskValue = 1.0 - smoothstep(max(0.0, uRadius - feather), uRadius + feather, d);
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 3) {
                float n = noise(uv * max(uScale * 96.0, 1.0) + vec2(uOffset * 37.0, uAngle * 0.071));
                maskValue = clamp((n - 0.5) * max(uValue * 4.0, 0.001) + 0.5, 0.0, 1.0);
            }
            if (uInvert != 0) {
                maskValue = 1.0 - maskValue;
            }
            FragColor = vec4(maskValue, maskValue, maskValue, 1.0);
        }
    )";

    static const char* blendFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uOriginal;
        uniform sampler2D uProcessed;
        uniform sampler2D uMask;
        void main() {
            vec4 originalColor = texture(uOriginal, vTexCoord);
            vec4 processedColor = texture(uProcessed, vTexCoord);
            float maskValue = clamp(texture(uMask, vTexCoord).r, 0.0, 1.0);
            FragColor = mix(originalColor, processedColor, maskValue);
        }
    )";

    static const char* maskCombineFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uMaskA;
        uniform sampler2D uMaskB;
        uniform int uMode;
        void main() {
            float a = clamp(texture(uMaskA, vTexCoord).r, 0.0, 1.0);
            float b = clamp(texture(uMaskB, vTexCoord).r, 0.0, 1.0);
            float v = 0.0;
            if (uMode == 0) {
                v = max(a, b);
            } else if (uMode == 1) {
                v = a * (1.0 - b);
            } else if (uMode == 2) {
                v = a * b;
            } else {
                v = abs(a - b);
            }
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    if (!m_MaskProgram) {
        m_MaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskFragSrc);
    }
    if (!m_MaskCombineProgram) {
        m_MaskCombineProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskCombineFragSrc);
    }
    if (!m_MaskBlendProgram) {
        m_MaskBlendProgram = GLHelpers::CreateShaderProgram(vertexSrc, blendFragSrc);
    }
}

void RenderPipeline::EnsureMixProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uImageA;
        uniform sampler2D uImageB;
        uniform sampler2D uFactorMask;
        uniform int uHasFactorMask;
        uniform float uFactor;
        uniform int uBlendMode;
        void main() {
            vec4 a = texture(uImageA, vTexCoord);
            vec4 b = texture(uImageB, vTexCoord);
            float factor = uFactor;
            if (uHasFactorMask != 0) {
                factor *= clamp(texture(uFactorMask, vTexCoord).r, 0.0, 1.0);
            }
            factor = clamp(factor, 0.0, 1.0);

            vec4 blended = b;
            if (uBlendMode == 1) {
                blended = (a + b) * 0.5;
            } else if (uBlendMode == 2) {
                blended = a + b;
            } else if (uBlendMode == 3) {
                blended = a * b;
            } else if (uBlendMode == 4) {
                blended = 1.0 - (1.0 - a) * (1.0 - b);
            } else if (uBlendMode == 5) {
                float sourceAlpha = b.a * factor;
                float outA = sourceAlpha + a.a * (1.0 - sourceAlpha);
                vec3 outRgb = vec3(0.0);
                if (outA > 0.000001) {
                    outRgb = (b.rgb * sourceAlpha + a.rgb * a.a * (1.0 - sourceAlpha)) / outA;
                }
                FragColor = vec4(outRgb, outA);
                return;
            } else if (uBlendMode == 6) {
                float sourceAlpha = b.a * factor;
                vec3 outRgb = b.rgb * factor + a.rgb * (1.0 - sourceAlpha);
                float outA = sourceAlpha + a.a * (1.0 - sourceAlpha);
                FragColor = vec4(outRgb, outA);
                return;
            }
            FragColor = mix(a, blended, factor);
        }
    )";

    if (!m_MixProgram) {
        m_MixProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureTechnicalImageProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";
    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uImage;
        uniform int uOperation;
        uniform float uExposureValue;

        float signedPow(float value, float exponentValue) {
            return sign(value) * pow(abs(value), exponentValue);
        }
        float decodeSrgb(float value) {
            float magnitude = abs(value);
            return magnitude <= 0.04045
                ? value / 12.92
                : sign(value) * pow((magnitude + 0.055) / 1.055, 2.4);
        }
        float encodeSrgb(float value) {
            float magnitude = abs(value);
            return magnitude <= 0.0031308
                ? 12.92 * value
                : sign(value) * (1.055 * pow(magnitude, 1.0 / 2.4) - 0.055);
        }
        void main() {
            vec4 value = texture(uImage, vTexCoord);
            vec3 rgb = value.rgb;
            if (uOperation == 3) {
                rgb = vec3(decodeSrgb(rgb.r), decodeSrgb(rgb.g), decodeSrgb(rgb.b));
            } else if (uOperation == 4) {
                rgb = vec3(encodeSrgb(rgb.r), encodeSrgb(rgb.g), encodeSrgb(rgb.b));
            } else if (uOperation == 5) {
                rgb = vec3(
                    0.82259287 * value.r + 0.17753395 * value.g,
                    0.03319951 * value.r + 0.96678350 * value.g,
                    0.01708535 * value.r + 0.07239572 * value.g + 0.91030148 * value.b);
            } else if (uOperation == 6) {
                rgb = vec3(
                    1.22474527 * value.r - 0.22490472 * value.g,
                    -0.04205797 * value.r + 1.04208100 * value.g,
                    -0.01964227 * value.r - 0.07865400 * value.g + 1.09853700 * value.b);
            } else if (uOperation == 7) {
                rgb *= exp2(uExposureValue);
            } else if (uOperation == 8) {
                rgb *= value.a;
            } else if (uOperation == 9) {
                rgb = value.a <= 0.000001 ? vec3(0.0) : rgb / value.a;
            }
            FragColor = vec4(rgb, value.a);
        }
    )";
    if (!m_TechnicalImageProgram) {
        m_TechnicalImageProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureReformatProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";
    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uImage;
        uniform ivec2 uInputSize;
        uniform int uFilter;

        ivec2 clampCoord(ivec2 value) {
            return clamp(value, ivec2(0), uInputSize - ivec2(1));
        }
        vec4 fetchClamp(ivec2 value) {
            return texelFetch(uImage, clampCoord(value), 0);
        }
        void main() {
            vec2 source = vTexCoord * vec2(uInputSize) - vec2(0.5);
            if (uFilter == 0) {
                FragColor = fetchClamp(ivec2(floor(source + vec2(0.5))));
                return;
            }
            ivec2 base = ivec2(floor(source));
            vec2 fraction = source - vec2(base);
            vec4 lower = mix(fetchClamp(base), fetchClamp(base + ivec2(1, 0)), fraction.x);
            vec4 upper = mix(fetchClamp(base + ivec2(0, 1)), fetchClamp(base + ivec2(1, 1)), fraction.x);
            FragColor = mix(lower, upper, fraction.y);
        }
    )";
    if (!m_ReformatProgram) {
        m_ReformatProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureLutProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;

        uniform sampler2D uImage;
        uniform sampler2D uLut1D;
        uniform sampler2D uShaper1D;
        uniform sampler3D uLut3D;
        uniform int uHasLut1D;
        uniform int uHasShaper1D;
        uniform int uHasLut3D;
        uniform int uInputTransform;
        uniform int uOutputTransform;
        uniform vec3 uLut1DDomainMin;
        uniform vec3 uLut1DDomainMax;
        uniform vec3 uShaperDomainMin;
        uniform vec3 uShaperDomainMax;
        uniform vec3 uLut3DDomainMin;
        uniform vec3 uLut3DDomainMax;

        float linearToSrgbChannel(float value) {
            float v = max(value, 0.0);
            if (v <= 0.0031308) {
                return 12.92 * v;
            }
            return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
        }

        float srgbToLinearChannel(float value) {
            float v = clamp(value, 0.0, 1.0);
            if (v <= 0.04045) {
                return v / 12.92;
            }
            return pow((v + 0.055) / 1.055, 2.4);
        }

        float gammaEncode22(float value) {
            return pow(max(value, 0.0), 1.0 / 2.2);
        }

        float gammaDecode22(float value) {
            return pow(clamp(value, 0.0, 1.0), 2.2);
        }

        vec3 applyTransfer(vec3 color, int mode) {
            if (mode == 1) {
                return vec3(
                    linearToSrgbChannel(color.r),
                    linearToSrgbChannel(color.g),
                    linearToSrgbChannel(color.b));
            }
            if (mode == 2) {
                return vec3(
                    gammaEncode22(color.r),
                    gammaEncode22(color.g),
                    gammaEncode22(color.b));
            }
            if (mode == 3) {
                return vec3(
                    srgbToLinearChannel(color.r),
                    srgbToLinearChannel(color.g),
                    srgbToLinearChannel(color.b));
            }
            if (mode == 4) {
                return vec3(
                    gammaDecode22(color.r),
                    gammaDecode22(color.g),
                    gammaDecode22(color.b));
            }
            return color;
        }

        float remapToUnit(float value, float domainMin, float domainMax) {
            float span = domainMax - domainMin;
            if (abs(span) < 1e-6) {
                return 0.0;
            }
            return clamp((value - domainMin) / span, 0.0, 1.0);
        }

        float lutCoord1D(float value, float domainMin, float domainMax, sampler2D lutTex) {
            float t = remapToUnit(value, domainMin, domainMax);
            int size = textureSize(lutTex, 0).x;
            return ((t * float(max(size - 1, 0))) + 0.5) / float(max(size, 1));
        }

        vec3 apply1DStage(vec3 color, sampler2D lutTex, vec3 domainMin, vec3 domainMax) {
            float coordR = lutCoord1D(color.r, domainMin.r, domainMax.r, lutTex);
            float coordG = lutCoord1D(color.g, domainMin.g, domainMax.g, lutTex);
            float coordB = lutCoord1D(color.b, domainMin.b, domainMax.b, lutTex);
            vec3 sampleR = texture(lutTex, vec2(coordR, 0.5)).rgb;
            vec3 sampleG = texture(lutTex, vec2(coordG, 0.5)).rgb;
            vec3 sampleB = texture(lutTex, vec2(coordB, 0.5)).rgb;
            return vec3(sampleR.r, sampleG.g, sampleB.b);
        }

        vec3 apply3DStage(vec3 color, sampler3D lutTex, vec3 domainMin, vec3 domainMax) {
            vec3 coord = vec3(
                remapToUnit(color.r, domainMin.r, domainMax.r),
                remapToUnit(color.g, domainMin.g, domainMax.g),
                remapToUnit(color.b, domainMin.b, domainMax.b));
            vec3 size = vec3(textureSize(lutTex, 0));
            coord = ((coord * (size - 1.0)) + 0.5) / size;
            return texture(lutTex, coord).rgb;
        }

        void main() {
            ivec2 imageSize = textureSize(uImage, 0);
            ivec2 imagePixel = clamp(
                ivec2(gl_FragCoord.xy),
                ivec2(0),
                max(imageSize - ivec2(1), ivec2(0)));
            vec4 source = texelFetch(uImage, imagePixel, 0);
            vec3 color = applyTransfer(source.rgb, uInputTransform);

            if (uHasLut1D != 0) {
                color = apply1DStage(color, uLut1D, uLut1DDomainMin, uLut1DDomainMax);
            }
            if (uHasShaper1D != 0) {
                color = apply1DStage(color, uShaper1D, uShaperDomainMin, uShaperDomainMax);
            }
            if (uHasLut3D != 0) {
                color = apply3DStage(color, uLut3D, uLut3DDomainMin, uLut3DDomainMax);
            }

            color = applyTransfer(color, uOutputTransform);
            FragColor = vec4(color, source.a);
        }
    )";

    if (!m_LutProgram) {
        m_LutProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureDataMathProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uDataA;
        uniform sampler2D uDataB;
        uniform int uHasA;
        uniform int uHasB;
        uniform int uScalarA;
        uniform int uScalarB;
        uniform int uMode;
        uniform float uConstantA;
        uniform float uConstantB;
        uniform float uMinValue;
        uniform float uMaxValue;
        uniform float uOutMin;
        uniform float uOutMax;
        uniform int uScalarOutput;

        vec4 readData(sampler2D tex, int hasInput, int scalarInput, float fallbackValue) {
            if (hasInput == 0) {
                return vec4(fallbackValue, fallbackValue, fallbackValue, fallbackValue);
            }
            vec4 value = texture(tex, vTexCoord);
            if (scalarInput != 0) {
                return vec4(value.r, value.r, value.r, 1.0);
            }
            return value;
        }

        void main() {
            vec4 a = readData(uDataA, uHasA, uScalarA, uConstantA);
            vec4 b = readData(uDataB, uHasB, uScalarB, uConstantB);
            vec4 result = a;

            if (uMode == 0) {
                result = clamp(a, vec4(uMinValue), vec4(uMaxValue));
            } else if (uMode == 1) {
                result = a + b;
            } else if (uMode == 2) {
                result = a - b;
            } else if (uMode == 3) {
                result = a * b;
            } else if (uMode == 4) {
                result = a / max(abs(b), vec4(0.00001));
            } else if (uMode == 5) {
                result = (a + b) * 0.5;
            } else if (uMode == 6) {
                result = min(a, b);
            } else if (uMode == 7) {
                result = max(a, b);
            } else if (uMode == 8) {
                result = abs(a - b);
            } else if (uMode == 9) {
                float span = max(uMaxValue - uMinValue, 0.00001);
                result = mix(vec4(uOutMin), vec4(uOutMax), clamp((a - vec4(uMinValue)) / span, 0.0, 1.0));
            }

            if (uScalarOutput != 0) {
                float v = result.r;
                FragColor = vec4(v, v, v, 1.0);
            } else {
                FragColor = result;
            }
        }
    )";

    if (!m_DataMathProgram) {
        m_DataMathProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureHdrMergeProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInput1;
        uniform sampler2D uInput2;
        uniform sampler2D uInput3;
        uniform int uHasInput2;
        uniform int uHasInput3;
        uniform vec3 uExposureEv;
        uniform vec2 uTexelSize;
        uniform vec2 uInputOffsetPx[3];
        uniform int uReferenceIndex;
        uniform vec3 uAlignmentConfidence;
        uniform float uDeghostStrength;
        uniform int uMotionPriority;
        uniform vec3 uClipThreshold;
        uniform vec3 uClipFeather;
        uniform vec3 uBlackThreshold;
        uniform vec3 uBlackFeather;
        uniform vec3 uReadNoise;
        uniform int uNoiseAware;
        uniform int uDebugView;

        float luminance(vec3 rgb) {
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        vec2 shiftedUv(vec2 uv, int index) {
            return clamp(uv + uInputOffsetPx[index] * uTexelSize, vec2(0.0), vec2(1.0));
        }

        vec4 mergeSample(vec3 sourceRgb, int index, float exposureEv, out float clipRisk, out float blackLimit) {
            vec3 positiveRgb = max(sourceRgb, vec3(0.0));
            float sourceMax = max(max(positiveRgb.r, positiveRgb.g), positiveRgb.b);
            float sourceLum = luminance(positiveRgb);
            float clipThreshold = max(uClipThreshold[index], 0.0);
            float clipFeather = max(uClipFeather[index], 0.0001);
            float blackThreshold = max(uBlackThreshold[index], 0.0);
            float blackFeather = max(uBlackFeather[index], 0.0001);
            clipRisk = smoothstep(clipThreshold - clipFeather, clipThreshold + clipFeather, sourceMax);
            blackLimit = 1.0 - smoothstep(blackThreshold, blackThreshold + blackFeather, sourceLum);

            float clipWeight = 1.0 - clipRisk;
            float blackWeight = 1.0 - blackLimit;
            float exposureScale = exp2(-exposureEv);
            vec3 normalizedRgb = positiveRgb * exposureScale;
            float weight = clipWeight * blackWeight;

            if (uNoiseAware != 0) {
                float normalizedLum = max(luminance(normalizedRgb), 0.0);
                float readNoise = max(uReadNoise[index], 0.0);
                float variance = readNoise * readNoise * exposureScale * exposureScale + normalizedLum + 0.000001;
                weight *= clamp(1.0 / variance, 0.0, 1000000.0);
            }
            return vec4(normalizedRgb * weight, weight);
        }

        float motionWeight(vec3 referenceRgb, vec3 candidateRgb, float confidence, float disagreementStrength) {
            float refLum = max(luminance(referenceRgb), 0.00003);
            float candLum = max(luminance(candidateRgb), 0.00003);
            float lumDelta = abs(candLum - refLum) / max(max(refLum, candLum), 0.03);
            float colorDelta = length(candidateRgb - referenceRgb) / max(max(refLum, candLum), 0.05);
            float disagreement = smoothstep(0.08, 0.30, lumDelta * 0.80 + colorDelta * 0.45);
            disagreement = max(disagreement, (1.0 - confidence) * 0.55);
            float weight = 1.0 - disagreement * disagreementStrength * (uMotionPriority == 0 ? 1.0 : 0.72);
            if (uMotionPriority == 0) {
                weight = mix(weight, 0.0, smoothstep(0.55, 0.92, disagreement) * disagreementStrength);
            }
            return clamp(weight, 0.0, 1.0);
        }

        void main() {
            vec3 source1 = texture(uInput1, shiftedUv(vTexCoord, 0)).rgb;
            vec3 source2 = texture(uInput2, shiftedUv(vTexCoord, 1)).rgb;
            vec3 source3 = texture(uInput3, shiftedUv(vTexCoord, 2)).rgb;

            vec3 normalized1 = max(source1, vec3(0.0)) * exp2(-uExposureEv.x);
            vec3 normalized2 = max(source2, vec3(0.0)) * exp2(-uExposureEv.y);
            vec3 normalized3 = max(source3, vec3(0.0)) * exp2(-uExposureEv.z);

            float clip1 = 0.0;
            float clip2 = 0.0;
            float clip3 = 0.0;
            float black1 = 0.0;
            float black2 = 0.0;
            float black3 = 0.0;

            vec4 merged1 = mergeSample(source1, 0, uExposureEv.x, clip1, black1);
            vec4 merged2 = (uHasInput2 != 0) ? mergeSample(source2, 1, uExposureEv.y, clip2, black2) : vec4(0.0);
            vec4 merged3 = (uHasInput3 != 0) ? mergeSample(source3, 2, uExposureEv.z, clip3, black3) : vec4(0.0);

            vec3 referenceRgb = normalized1;
            if (uReferenceIndex == 1) {
                referenceRgb = normalized2;
            } else if (uReferenceIndex == 2) {
                referenceRgb = normalized3;
            }

            float motionMask1 = 0.0;
            float motionMask2 = 0.0;
            float motionMask3 = 0.0;
            float rejected1 = 0.0;
            float rejected2 = 0.0;
            float rejected3 = 0.0;
            if (uDeghostStrength > 0.0001) {
                if (uReferenceIndex != 0) {
                    float weight = motionWeight(referenceRgb, normalized1, uAlignmentConfidence.x, uDeghostStrength);
                    motionMask1 = 1.0 - weight;
                    rejected1 = motionMask1;
                    merged1.rgb *= weight;
                    merged1.a *= weight;
                }
                if (uHasInput2 != 0 && uReferenceIndex != 1) {
                    float weight = motionWeight(referenceRgb, normalized2, uAlignmentConfidence.y, uDeghostStrength);
                    motionMask2 = 1.0 - weight;
                    rejected2 = motionMask2;
                    merged2.rgb *= weight;
                    merged2.a *= weight;
                }
                if (uHasInput3 != 0 && uReferenceIndex != 2) {
                    float weight = motionWeight(referenceRgb, normalized3, uAlignmentConfidence.z, uDeghostStrength);
                    motionMask3 = 1.0 - weight;
                    rejected3 = motionMask3;
                    merged3.rgb *= weight;
                    merged3.a *= weight;
                }
            }

            vec3 weightedRgb = merged1.rgb + merged2.rgb + merged3.rgb;
            float weightSum = merged1.a + merged2.a + merged3.a;
            vec3 result = weightSum > 0.000001 ? weightedRgb / weightSum : referenceRgb;
            if (uDeghostStrength > 0.0001) {
                float strongestMotion = 0.0;
                if (uReferenceIndex != 0) {
                    strongestMotion = max(strongestMotion, motionMask1);
                }
                if (uHasInput2 != 0 && uReferenceIndex != 1) {
                    strongestMotion = max(strongestMotion, motionMask2);
                }
                if (uHasInput3 != 0 && uReferenceIndex != 2) {
                    strongestMotion = max(strongestMotion, motionMask3);
                }
                float fallbackStrength = smoothstep(0.58, 0.90, strongestMotion) * uDeghostStrength;
                if (uMotionPriority != 0) {
                    fallbackStrength *= 0.60;
                }
                result = mix(result, referenceRgb, clamp(fallbackStrength, 0.0, 1.0));
            }

            if (uDebugView == 1) {
                float denom = max(weightSum, 0.000001);
                FragColor = vec4(merged1.a / denom, merged2.a / denom, merged3.a / denom, 1.0);
            } else if (uDebugView == 2) {
                FragColor = vec4(clip1, (uHasInput2 != 0) ? clip2 : 0.0, (uHasInput3 != 0) ? clip3 : 0.0, 1.0);
            } else if (uDebugView == 3) {
                FragColor = vec4(black1, (uHasInput2 != 0) ? black2 : 0.0, (uHasInput3 != 0) ? black3 : 0.0, 1.0);
            } else if (uDebugView == 4) {
                FragColor = vec4(uAlignmentConfidence.x, uAlignmentConfidence.y, uAlignmentConfidence.z, 1.0);
            } else if (uDebugView == 5) {
                FragColor = vec4(motionMask1, motionMask2, motionMask3, 1.0);
            } else if (uDebugView == 6) {
                FragColor = vec4(rejected1, rejected2, rejected3, 1.0);
            } else {
                FragColor = vec4(result, 1.0);
            }
        }
    )";

    if (!m_HdrMergeProgram) {
        m_HdrMergeProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureUtilityPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* maskUtilityFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputMask;
        uniform int uKind;
        uniform float uBlackPoint;
        uniform float uWhitePoint;
        uniform float uGamma;
        uniform float uThreshold;
        uniform float uSoftness;
        uniform int uEnabled;
        uniform int uInvert;
        void main() {
            float v = clamp(texture(uInputMask, vTexCoord).r, 0.0, 1.0);
            if (uKind == 0) {
                if (uEnabled != 0) v = 1.0 - v;
            } else if (uKind == 1) {
                float denom = max(uWhitePoint - uBlackPoint, 0.0001);
                v = clamp((v - uBlackPoint) / denom, 0.0, 1.0);
                v = pow(v, 1.0 / max(uGamma, 0.001));
                if (uInvert != 0) v = 1.0 - v;
            } else if (uKind == 2) {
                float softness = max(uSoftness, 0.0001);
                v = smoothstep(uThreshold - softness, uThreshold + softness, v);
                if (uInvert != 0) v = 1.0 - v;
            }
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageToMaskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uKind;
        uniform float uLow;
        uniform float uHigh;
        uniform float uSoftness;
        uniform int uInvert;
        uniform int uSampleCount;
        uniform vec3 uSampleRgb;
        uniform float uSampleLuma;
        uniform vec3 uExtraSampleRgb[4];
        uniform float uExtraSampleLuma[4];
        uniform vec2 uSampleUv;
        uniform float uToneSimilarity;
        uniform float uColorSimilarity;
        uniform float uRegionRadius;
        uniform float uRegionFeather;
        uniform float uEdgeSensitivity;
        uniform float uLocalCoherence;
        uniform vec2 uTexelSize;
        vec3 chromaOf(vec3 rgb) {
            float sum = max(rgb.r + rgb.g + rgb.b, 0.00001);
            return rgb / sum;
        }
        float matchAgainstSeed(vec3 rgb, float lum, vec3 sampleChroma, float softnessScale, float sampleLuma, float toneSimilarity, float colorSimilarity) {
            float toneDistance = abs(lum - sampleLuma) / max(toneSimilarity, 0.0001);
            vec3 pixelChroma = chromaOf(max(rgb, vec3(0.0)));
            float colorDistance = distance(pixelChroma, sampleChroma) / max(colorSimilarity, 0.0001);
            float toneMatch = 1.0 - smoothstep(1.0, 1.0 + softnessScale, toneDistance);
            float colorMatch = 1.0 - smoothstep(1.0, 1.0 + softnessScale, colorDistance);
            return toneMatch * colorMatch;
        }
        float bestSampleMatch(vec3 rgb, float lum, float softnessScale) {
            float best = 0.0;
            vec3 primaryChroma = chromaOf(max(uSampleRgb, vec3(0.0)));
            best = max(best, matchAgainstSeed(rgb, lum, primaryChroma, softnessScale, uSampleLuma, uToneSimilarity, uColorSimilarity));
            for (int i = 0; i < 4; ++i) {
                if (i + 1 >= uSampleCount) {
                    break;
                }
                vec3 sampleChroma = chromaOf(max(uExtraSampleRgb[i], vec3(0.0)));
                best = max(best, matchAgainstSeed(rgb, lum, sampleChroma, softnessScale, uExtraSampleLuma[i], uToneSimilarity, uColorSimilarity));
            }
            return best;
        }
        void main() {
            vec4 c = texture(uInputImage, vTexCoord);
            float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
            float v = 0.0;
            if (uKind == 0) {
                float denom = max(uHigh - uLow, 0.0001);
                v = clamp((lum - uLow) / denom, 0.0, 1.0);
                if (uSoftness > 0.0001) {
                    v = smoothstep(0.5 - uSoftness, 0.5 + uSoftness, v);
                }
            } else {
                float softnessScale = max(uSoftness, 0.0001) * 3.0;
                float baseMatch = bestSampleMatch(c.rgb, lum, softnessScale);
                float spatialDistance = distance(vTexCoord, uSampleUv);
                float spatialRadius = clamp(uRegionRadius, 0.05, 1.0);
                float spatialSoftness = mix(0.08, 0.30, clamp(uRegionFeather, 0.0, 1.0));
                float spatialMatch = 1.0 - smoothstep(spatialRadius, spatialRadius + spatialSoftness, spatialDistance);
                vec3 rgbX = texture(uInputImage, clamp(vTexCoord + vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb -
                            texture(uInputImage, clamp(vTexCoord - vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbY = texture(uInputImage, clamp(vTexCoord + vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).rgb -
                            texture(uInputImage, clamp(vTexCoord - vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).rgb;
                float edgeStrength = length(rgbX) + length(rgbY);
                float edgeThreshold = mix(0.75, 0.08, clamp(uEdgeSensitivity, 0.0, 1.0));
                float edgePenalty = smoothstep(edgeThreshold, edgeThreshold + 0.25, edgeStrength);
                float edgeAware = 1.0 - edgePenalty * (1.0 - baseMatch) * clamp(uEdgeSensitivity, 0.0, 1.0);
                float coherenceRadius = mix(1.0, 5.0, clamp(uRegionFeather, 0.0, 1.0));
                vec2 coherenceOffset = uTexelSize * coherenceRadius;
                vec3 rgbPx = texture(uInputImage, clamp(vTexCoord + vec2(coherenceOffset.x, 0.0), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbNx = texture(uInputImage, clamp(vTexCoord - vec2(coherenceOffset.x, 0.0), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbPy = texture(uInputImage, clamp(vTexCoord + vec2(0.0, coherenceOffset.y), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbNy = texture(uInputImage, clamp(vTexCoord - vec2(0.0, coherenceOffset.y), vec2(0.0), vec2(1.0))).rgb;
                float lumPx = dot(rgbPx, vec3(0.2126, 0.7152, 0.0722));
                float lumNx = dot(rgbNx, vec3(0.2126, 0.7152, 0.0722));
                float lumPy = dot(rgbPy, vec3(0.2126, 0.7152, 0.0722));
                float lumNy = dot(rgbNy, vec3(0.2126, 0.7152, 0.0722));
                float coherenceSum = baseMatch;
                coherenceSum += bestSampleMatch(rgbPx, lumPx, softnessScale);
                coherenceSum += bestSampleMatch(rgbNx, lumNx, softnessScale);
                coherenceSum += bestSampleMatch(rgbPy, lumPy, softnessScale);
                coherenceSum += bestSampleMatch(rgbNy, lumNy, softnessScale);
                float coherenceAvg = coherenceSum / 5.0;
                float coherenceBoost = mix(1.0, smoothstep(0.25, 0.70, coherenceAvg), clamp(uLocalCoherence, 0.0, 1.0));
                v = baseMatch * spatialMatch * edgeAware * coherenceBoost;
            }
            if (uInvert != 0) v = 1.0 - v;
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageGeneratorFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform vec4 uColorA;
        uniform vec4 uColorB;
        uniform float uAngle;
        uniform float uOffset;
        void main() {
            if (uKind == 0) {
                FragColor = uColorA;
                return;
            }
            if (uKind == 2) {
                float dist = max(abs(vTexCoord.x - 0.5), abs(vTexCoord.y - 0.5)) - 0.34;
                float delta = fwidth(dist);
                float mask = 1.0 - smoothstep(-0.5 * delta, 0.5 * delta, dist);
                FragColor = vec4(uColorA.rgb, uColorA.a * mask);
                return;
            }
            if (uKind == 3) {
                float d = distance(vTexCoord, vec2(0.5));
                float dist = d - 0.34;
                float delta = fwidth(dist);
                float mask = 1.0 - smoothstep(-0.5 * delta, 0.5 * delta, dist);
                FragColor = vec4(uColorA.rgb, uColorA.a * mask);
                return;
            }
            float radiansAngle = radians(uAngle);
            vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
            float t = clamp(dot(vTexCoord - vec2(0.5), dir) + 0.5 + uOffset, 0.0, 1.0);
            FragColor = mix(uColorA, uColorB, t);
        }
    )";

    if (!m_MaskUtilityProgram) {
        m_MaskUtilityProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskUtilityFragSrc);
    }
    if (!m_ImageToMaskProgram) {
        m_ImageToMaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageToMaskFragSrc);
    }
    if (!m_ImageGeneratorProgram) {
        m_ImageGeneratorProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageGeneratorFragSrc);
    }
}

void RenderPipeline::EnsureChannelPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* splitFragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uChannel; // 0 = R, 1 = G, 2 = B, 3 = A
        void main() {
            vec4 col = texture(uInputImage, vTexCoord);
            float v = col.r;
            if (uChannel == 1) v = col.g;
            else if (uChannel == 2) v = col.b;
            else if (uChannel == 3) v = col.a;
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* combineFragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uTexR;
        uniform sampler2D uTexG;
        uniform sampler2D uTexB;
        uniform sampler2D uTexA;
        uniform int uHasR;
        uniform int uHasG;
        uniform int uHasB;
        uniform int uHasA;
        void main() {
            float r = (uHasR != 0) ? texture(uTexR, vTexCoord).r : 0.0;
            float g = (uHasG != 0) ? texture(uTexG, vTexCoord).r : 0.0;
            float b = (uHasB != 0) ? texture(uTexB, vTexCoord).r : 0.0;
            float a = (uHasA != 0) ? texture(uTexA, vTexCoord).r : 1.0;
            FragColor = vec4(r, g, b, a);
        }
    )";

    if (!m_ChannelSplitProgram) {
        m_ChannelSplitProgram = GLHelpers::CreateShaderProgram(vertexSrc, splitFragmentSrc);
    }
    if (!m_ChannelCombineProgram) {
        m_ChannelCombineProgram = GLHelpers::CreateShaderProgram(vertexSrc, combineFragmentSrc);
    }
}

void RenderPipeline::EnsureAutoGainStatsProgram() {
    if (m_AutoGainStatsProgram) {
        return;
    }

    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform vec2 uSourceTexelSize;

        float luma(vec3 rgb) {
            return dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
        }

        float logLuma(vec2 uv) {
            return log2(max(luma(texture(uInputImage, uv).rgb), 0.00003));
        }

        void main() {
            vec3 rgb = max(texture(uInputImage, vTexCoord).rgb, vec3(0.0));
            float lum = luma(rgb);
            float maxChannel = max(max(rgb.r, rgb.g), rgb.b);
            float minChannel = min(min(rgb.r, rgb.g), rgb.b);
            float saturation = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float gx = abs(logLuma(vTexCoord + vec2(uSourceTexelSize.x, 0.0)) - logLuma(vTexCoord - vec2(uSourceTexelSize.x, 0.0)));
            float gy = abs(logLuma(vTexCoord + vec2(0.0, uSourceTexelSize.y)) - logLuma(vTexCoord - vec2(0.0, uSourceTexelSize.y)));
            float textureProxy = clamp((gx + gy) * 0.5, 0.0, 1.0);
            FragColor = vec4(lum, maxChannel, saturation, textureProxy);
        }
    )";

    m_AutoGainStatsProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
}

void RenderPipeline::EnsureRawDetailFusionPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* metricsFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uSmoothGradientProtection;
        uniform float uTextureSensitivity;
        uniform float uSkyBias;
        uniform float uEstimatedNoiseFloor;
        uniform float uAutoNoiseProtection;
        uniform float uAutoHighlightProtection;
        uniform float uChannelSaturationRisk;
        uniform vec2 uTexelSize;

        vec3 rgbAt(vec2 uv) {
            return max(texture(uInputImage, uv).rgb, vec3(0.0));
        }

        float luma(vec3 rgb) {
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        float lumaAt(vec2 uv) {
            return luma(rgbAt(uv));
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        void main() {
            vec3 centerRgb = rgbAt(vTexCoord);
            float centerLum = luma(centerRgb);
            float centerLog = log2(max(centerLum, 0.00003));
            vec2 texel = uTexelSize;

            float l = logLumaAt(vTexCoord - vec2(texel.x, 0.0));
            float r = logLumaAt(vTexCoord + vec2(texel.x, 0.0));
            float d = logLumaAt(vTexCoord - vec2(0.0, texel.y));
            float u = logLumaAt(vTexCoord + vec2(0.0, texel.y));
            float l2 = logLumaAt(vTexCoord - vec2(texel.x * 2.0, 0.0));
            float r2 = logLumaAt(vTexCoord + vec2(texel.x * 2.0, 0.0));
            float d2 = logLumaAt(vTexCoord - vec2(0.0, texel.y * 2.0));
            float u2 = logLumaAt(vTexCoord + vec2(0.0, texel.y * 2.0));

            float gradient = length(vec2(r - l, u - d));
            float second = abs(l - centerLog * 2.0 + r) + abs(d - centerLog * 2.0 + u);
            float broadGradient = length(vec2(r2 - l2, u2 - d2));
            float broadSecond = abs(l2 - centerLog * 2.0 + r2) + abs(d2 - centerLog * 2.0 + u2);

            vec3 meanRgb = vec3(0.0);
            float meanLum = 0.0;
            float meanLog = 0.0;
            float samples = 0.0;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    vec2 uv = vTexCoord + vec2(x, y) * texel;
                    vec3 rgb = rgbAt(uv);
                    float lum = luma(rgb);
                    meanRgb += rgb;
                    meanLum += lum;
                    meanLog += log2(max(lum, 0.00003));
                    samples += 1.0;
                }
            }
            meanRgb /= max(samples, 1.0);
            meanLum /= max(samples, 1.0);
            meanLog /= max(samples, 1.0);

            float variance = 0.0;
            float chromaVariance = 0.0;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    vec2 uv = vTexCoord + vec2(x, y) * texel;
                    vec3 rgb = rgbAt(uv);
                    float logLum = log2(max(luma(rgb), 0.00003));
                    variance += pow(logLum - meanLog, 2.0);
                    chromaVariance += length((rgb - vec3(luma(rgb))) - (meanRgb - vec3(meanLum)));
                }
            }
            variance /= max(samples, 1.0);
            chromaVariance /= max(samples, 1.0);

            float textureSensitivity = clamp(uTextureSensitivity, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);
            float skyBias = clamp(uSkyBias, 0.0, 1.0);

            float trueEdge = smoothstep(mix(0.08, 0.025, textureSensitivity), mix(0.42, 0.15, textureSensitivity), gradient + second * 3.25);
            trueEdge = max(trueEdge, smoothstep(0.18, 0.95, broadSecond * 4.0));

            float textureDetail = smoothstep(mix(0.010, 0.003, textureSensitivity), mix(0.090, 0.030, textureSensitivity), sqrt(max(variance, 0.0)) + second * 1.5);
            textureDetail *= 1.0 - smoothstep(0.035, 0.22, broadGradient) * 0.55;

            float lowTexture = 1.0 - smoothstep(mix(0.005, 0.018, textureSensitivity), mix(0.060, 0.16, textureSensitivity), sqrt(max(variance, 0.0)) + chromaVariance);
            float rampLike = smoothstep(0.010, 0.18, broadGradient) * (1.0 - smoothstep(0.050, 0.34, broadSecond * 2.0));
            float lowSaturation = 1.0 - smoothstep(0.08, 0.42, length(centerRgb - vec3(centerLum)));
            float blueSkyHint = smoothstep(0.0, 0.14, centerRgb.b - max(centerRgb.r, centerRgb.g) * 0.72);
            float brightEnough = smoothstep(0.010, 0.22, centerLum);
            float smoothGradient = max(lowTexture * rampLike, lowTexture * mix(lowSaturation, max(lowSaturation, blueSkyHint), skyBias) * brightEnough);
            smoothGradient = clamp(smoothGradient * mix(0.35, 1.35, smoothProtect) * (1.0 - trueEdge * 0.92), 0.0, 1.0);

            float debandRisk = smoothGradient * (1.0 - textureDetail) * smoothstep(0.012, 0.30, broadGradient);
            float centerChroma = length(centerRgb - vec3(centerLum));
            float maxChannel = max(max(centerRgb.r, centerRgb.g), centerRgb.b);
            float minChannel = min(min(centerRgb.r, centerRgb.g), centerRgb.b);
            float saturation = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float shadowNoise = (1.0 - smoothstep(uEstimatedNoiseFloor * 2.0, uEstimatedNoiseFloor * 9.0, centerLum)) *
                (1.0 - trueEdge * 0.65);
            float sceneSaturationRisk = smoothstep(0.35, 0.92, saturation) * smoothstep(0.18, 1.08, maxChannel);
            float chromaArtifact = trueEdge *
                smoothstep(0.010 + centerLum * 0.025, 0.16 + centerLum * 0.20, centerChroma + chromaVariance * 1.8) *
                (1.0 - smoothstep(0.08, 0.85, lowTexture));
            chromaArtifact = max(chromaArtifact, sceneSaturationRisk * clamp(uChannelSaturationRisk, 0.0, 1.0) * 0.75);
            textureDetail *= 1.0 - chromaArtifact * mix(0.45, 0.85, clamp(uAutoHighlightProtection, 0.0, 1.0));
            textureDetail *= 1.0 - shadowNoise * mix(0.25, 0.95, clamp(uAutoNoiseProtection, 0.0, 1.0));
            FragColor = vec4(clamp(trueEdge, 0.0, 1.0), clamp(textureDetail, 0.0, 1.0), smoothGradient, clamp(max(max(debandRisk, chromaArtifact), shadowNoise * 0.75), 0.0, 1.0));
        }
    )";

    static const char* analysisFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform sampler2D uMetrics;
        uniform sampler2D uManualMask;
        uniform int uHasManualMask;
        uniform int uMode;
        uniform float uMinEv;
        uniform float uMaxEv;
        uniform float uBaseEv;
        uniform int uSampleCount;
        uniform float uBaseRadiusPercent;
        uniform float uHighlightProtection;
        uniform float uShadowLiftLimit;
        uniform float uNoiseProtection;
        uniform float uDetailWeight;
        uniform float uWellExposedTarget;
        uniform float uSmoothGradientProtection;
        uniform float uSkyBias;
        uniform float uEstimatedNoiseFloor;
        uniform float uChannelSaturationRisk;
        uniform float uClippingRatio;
        uniform int uInvertMask;
        uniform float uMaskBlackPoint;
        uniform float uMaskWhitePoint;
        uniform float uMaskGamma;
        uniform float uManualBlend;
        uniform vec2 uTexelSize;

        float lumaAt(vec2 uv) {
            vec3 rgb = max(texture(uInputImage, uv).rgb, vec3(0.0));
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        vec3 rgbAt(vec2 uv) {
            return max(texture(uInputImage, uv).rgb, vec3(0.0));
        }

        float shapedMask() {
            float v = uHasManualMask != 0 ? texture(uManualMask, vTexCoord).r : 0.5;
            float denom = max(uMaskWhitePoint - uMaskBlackPoint, 0.0001);
            v = clamp((v - uMaskBlackPoint) / denom, 0.0, 1.0);
            v = pow(v, 1.0 / max(uMaskGamma, 0.001));
            if (uInvertMask != 0) v = 1.0 - v;
            return v;
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        float edgeAwareBaseLogLuma(float centerLog, float centerLum, vec4 centerMetrics) {
            float longEdge = max(1.0 / max(uTexelSize.x, 0.000001), 1.0 / max(uTexelSize.y, 0.000001));
            float desiredRadius = max(2.0, longEdge * clamp(uBaseRadiusPercent, 0.002, 0.030));
            int sampleExtent = clamp((uSampleCount + 3) / 4, 2, 8);
            float sampleStep = max(1.0, desiredRadius / float(sampleExtent));
            float radius2 = desiredRadius * desiredRadius;
            float centerEdge = clamp(centerMetrics.r, 0.0, 1.0);
            float centerSmooth = clamp(centerMetrics.b, 0.0, 1.0);
            float sum = 0.0;
            float weightSum = 0.0;
            for (int y = -8; y <= 8; ++y) {
                for (int x = -8; x <= 8; ++x) {
                    if (abs(x) > sampleExtent || abs(y) > sampleExtent) continue;
                    vec2 offsetPx = vec2(x, y) * sampleStep;
                    float dist2 = dot(offsetPx, offsetPx);
                    if (dist2 > radius2) continue;
                    vec2 uv = vTexCoord + offsetPx * uTexelSize;
                    float sampleLog = logLumaAt(uv);
                    float sampleLum = lumaAt(uv);
                    vec4 sampleMetrics = texture(uMetrics, uv);
                    float sampleEdge = clamp(sampleMetrics.r, 0.0, 1.0);
                    float sampleSmooth = clamp(sampleMetrics.b, 0.0, 1.0);
                    float spatial = exp(-dist2 / max(1.0, radius2 * 0.42));
                    float edgeStop = max(centerEdge, sampleEdge);
                    float rangeScale = mix(1.35, 0.28, edgeStop);
                    float logRange = exp(-abs(sampleLog - centerLog) / max(0.0001, rangeScale));
                    float linearRange = exp(-abs(sampleLum - centerLum) / max(0.0001, mix(0.42, 0.055, edgeStop)));
                    float smoothAffinity = 1.0 - abs(sampleSmooth - centerSmooth);
                    float w = spatial * logRange * linearRange;
                    w = mix(w, max(w, 0.25 + smoothAffinity * 0.35), centerSmooth * clamp(uSmoothGradientProtection, 0.0, 1.0) * (1.0 - edgeStop));
                    w *= 1.0 - edgeStop * 0.72;
                    sum += sampleLog * w;
                    weightSum += w;
                }
            }
            return weightSum > 0.0 ? sum / weightSum : centerLog;
        }

        void main() {
            vec3 centerRgb = rgbAt(vTexCoord);
            float lum = dot(centerRgb, vec3(0.2126, 0.7152, 0.0722));
            float maxChannel = max(max(centerRgb.r, centerRgb.g), centerRgb.b);
            float minChannel = min(min(centerRgb.r, centerRgb.g), centerRgb.b);
            float channelDominance = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float saturatedBright = smoothstep(0.45, 0.92, channelDominance) * smoothstep(0.28, 1.08, maxChannel);
            float globalHighlightPressure = clamp(uClippingRatio * 8.0 + uChannelSaturationRisk * 2.2, 0.0, 1.0);
            vec4 metrics = texture(uMetrics, vTexCoord);
            float trueEdge = clamp(metrics.r, 0.0, 1.0);
            float textureDetail = clamp(metrics.g, 0.0, 1.0);
            float smoothGradient = clamp(metrics.b, 0.0, 1.0);
            float chromaArtifact = clamp(metrics.a, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);

            float evSpan = max(0.0001, uMaxEv - uMinEv);
            float minAbsEv = min(uMinEv, uMaxEv) + uBaseEv;
            float maxAbsEv = max(uMinEv, uMaxEv) + uBaseEv;
            float target = clamp(uWellExposedTarget, 0.10, 0.55);
            float safeLum = max(lum, 0.00003);
            float centerLog = log2(safeLum);
            float baseLog = edgeAwareBaseLogLuma(centerLog, safeLum, metrics);
            float baseLum = exp2(baseLog);
            float targetLog = log2(max(target, 0.00003));
            float zone = baseLog - targetLog;

            float shadowCurve = smoothstep(0.15, 2.25, -zone);
            float highlightCurve = smoothstep(0.10, 2.10, zone);
            float maxShadowBoost = max(0.0, uMaxEv);
            float maxHighlightCompress = max(0.0, -uMinEv);

            float clipRisk = smoothstep(0.82, 1.18, baseLum);
            float saturatedClipRisk = max(clipRisk, saturatedBright * mix(0.35, 1.0, globalHighlightPressure));
            float adaptiveNoiseFloor = max(0.00003, uEstimatedNoiseFloor);
            float deepShadow = 1.0 - smoothstep(adaptiveNoiseFloor * 1.5, mix(adaptiveNoiseFloor * 5.0, adaptiveNoiseFloor * 18.0, clamp(uNoiseProtection, 0.0, 1.0)), safeLum);
            float blackRisk = 1.0 - smoothstep(0.004, mix(0.018, 0.12, clamp(uNoiseProtection, 0.0, 1.0)), baseLum);
            float snrConfidence = smoothstep(adaptiveNoiseFloor * 3.0, adaptiveNoiseFloor * 18.0, safeLum);
            snrConfidence *= 1.0 - deepShadow * clamp(uNoiseProtection, 0.0, 1.0) * 0.35;

            float specularOrLuminous = max(
                saturatedClipRisk,
                smoothstep(0.78, 1.25, baseLum) * (1.0 - textureDetail * 0.70) * mix(0.55, 1.0, globalHighlightPressure));
            float gradientGate = mix(1.0, 1.0 - max(smoothGradient * 0.70, chromaArtifact * 0.55), smoothProtect);
            float haloGate = mix(1.0, 1.0 - trueEdge * 0.35, clamp(uSkyBias, 0.0, 1.0));
            float shadowGate = snrConfidence * gradientGate * haloGate * (1.0 - chromaArtifact * 0.80) * (1.0 - saturatedBright * 0.85);
            shadowGate *= mix(1.0, 0.20, deepShadow * clamp(uShadowLiftLimit, 0.0, 1.0) * clamp(uNoiseProtection, 0.0, 1.0));

            float highlightGate = mix(1.0, 1.0 - specularOrLuminous * 0.92, clamp(uHighlightProtection, 0.0, 1.0));
            highlightGate *= mix(1.0, 1.0 - trueEdge * 0.25, clamp(uSkyBias, 0.0, 1.0));
            highlightGate *= mix(1.0, 1.0 - smoothGradient * 0.35, smoothProtect);

            float detailConfidence = mix(0.85, 1.15, clamp(uDetailWeight, 0.0, 1.0) * textureDetail * (1.0 - chromaArtifact));
            float delta = shadowCurve * maxShadowBoost * shadowGate * detailConfidence -
                highlightCurve * maxHighlightCompress * highlightGate;
            float autoEv = clamp(uBaseEv + delta, minAbsEv, maxAbsEv);
            float bestEv = autoEv;
            float highlightSafety = 1.0 - saturatedClipRisk;
            float shadowProtection = clamp(snrConfidence * (1.0 - blackRisk * 0.45), 0.0, 1.0);
            float confidence = clamp(mix(shadowGate, highlightGate, highlightCurve) * (1.0 - chromaArtifact * 0.50), 0.0, 1.0);
            float manualEv = mix(uMinEv, uMaxEv, shapedMask()) + uBaseEv;
            float ev = autoEv;
            if (uMode == 0) {
                ev = manualEv;
            } else if (uMode == 2) {
                ev = mix(autoEv, manualEv, clamp(uManualBlend, 0.0, 1.0));
            }
            ev = clamp(ev, minAbsEv, maxAbsEv);
            float evNorm = clamp((ev - (uMinEv + uBaseEv)) / evSpan, 0.0, 1.0);
            float sampleNorm = clamp((bestEv - (uMinEv + uBaseEv)) / evSpan, 0.0, 1.0);
            FragColor = vec4(evNorm, confidence, highlightSafety, mix(shadowProtection, sampleNorm, 0.45));
        }
    )";

    static const char* smoothFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uAnalysis;
        uniform sampler2D uMetrics;
        uniform sampler2D uInputImage;
        uniform int uRadius;
        uniform int uSmoothAreaRadius;
        uniform float uEdgeAwareness;
        uniform float uHaloGuard;
        uniform float uSmoothGradientProtection;
        uniform float uMaskDebandDither;
        uniform vec2 uTexelSize;

        float lumaAt(vec2 uv) {
            vec3 rgb = max(texture(uInputImage, uv).rgb, vec3(0.0));
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        void main() {
            vec4 center = texture(uAnalysis, vTexCoord);
            vec4 centerMetrics = texture(uMetrics, vTexCoord);
            int radius = clamp(uRadius, 0, 16);
            int smoothAreaRadius = clamp(uSmoothAreaRadius, 0, 32);
            float smoothGradient = clamp(centerMetrics.b, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);
            int effectiveRadius = max(radius, int(round(float(smoothAreaRadius) * smoothGradient * smoothProtect)));
            if (effectiveRadius <= 0) {
                FragColor = center;
                return;
            }
            float centerLum = lumaAt(vTexCoord);
            float centerLogLum = logLumaAt(vTexCoord);
            float edgeAware = clamp(uEdgeAwareness, 0.0, 1.0);
            float haloGuard = clamp(uHaloGuard, 0.0, 1.0);
            float localEdge = max(centerMetrics.r, clamp((abs(logLumaAt(vTexCoord + vec2(uTexelSize.x, 0.0)) - logLumaAt(vTexCoord - vec2(uTexelSize.x, 0.0))) +
                abs(logLumaAt(vTexCoord + vec2(0.0, uTexelSize.y)) - logLumaAt(vTexCoord - vec2(0.0, uTexelSize.y)))) * 0.55, 0.0, 1.0));
            float edgeScale = mix(2.2, 0.22, edgeAware);
            float linearEdgeScale = mix(0.55, 0.035, edgeAware);
            float haloScale = mix(3.0, 0.75, haloGuard);
            float smoothRadiusScale = mix(1.0, mix(1.0, 0.20, localEdge), haloGuard);
            smoothRadiusScale *= mix(1.0, 1.85, smoothGradient * smoothProtect);
            float sum = 0.0;
            float weightSum = 0.0;
            for (int y = -32; y <= 32; ++y) {
                for (int x = -32; x <= 32; ++x) {
                    if (abs(x) > effectiveRadius || abs(y) > effectiveRadius) continue;
                    vec2 uv = vTexCoord + vec2(x, y) * uTexelSize;
                    vec4 sampleMetrics = texture(uMetrics, uv);
                    float distance2 = float(x * x + y * y);
                    float spatial = exp(-distance2 / max(1.0, float(effectiveRadius * effectiveRadius) * smoothRadiusScale) * haloScale);
                    float logDiff = abs(logLumaAt(uv) - centerLogLum);
                    float linearDiff = abs(lumaAt(uv) - centerLum);
                    float sameSmoothRegion = 1.0 - abs(clamp(sampleMetrics.b, 0.0, 1.0) - smoothGradient);
                    float edgeStop = max(localEdge, clamp(sampleMetrics.r, 0.0, 1.0));
                    float rangeW = exp(-logDiff / max(0.0001, edgeScale)) *
                        exp(-linearDiff / max(0.0001, linearEdgeScale));
                    rangeW = mix(rangeW, max(rangeW, 0.35 + sameSmoothRegion * 0.45), smoothGradient * smoothProtect * (1.0 - edgeStop));
                    rangeW *= 1.0 - smoothstep(mix(3.0, 0.75, edgeAware), mix(5.0, 1.55, edgeAware), logDiff) * haloGuard;
                    rangeW *= 1.0 - edgeStop * haloGuard * 0.75;
                    float w = spatial * rangeW;
                    sum += texture(uAnalysis, uv).r * w;
                    weightSum += w;
                }
            }
            float smoothed = weightSum > 0.0 ? sum / weightSum : center.r;
            float preserve = smoothstep(0.12, 0.88, localEdge) * haloGuard;
            preserve *= 1.0 - smoothGradient * smoothProtect * 0.75;
            center.r = mix(smoothed, center.r, preserve);
            if (uMaskDebandDither > 0.0 && centerMetrics.a > 0.0) {
                float n = fract(sin(dot(vTexCoord * vec2(8192.0, 4096.0), vec2(12.9898, 78.233))) * 43758.5453);
                center.r = clamp(center.r + (n - 0.5) * uMaskDebandDither * centerMetrics.a * 0.006, 0.0, 1.0);
            }
            FragColor = center;
        }
    )";

    static const char* applyFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform sampler2D uExposureMap;
        uniform sampler2D uMetrics;
        uniform int uHasMask;
        uniform float uMinEv;
        uniform float uMaxEv;
        uniform float uBaseEv;
        uniform float uStrength;
        uniform float uEstimatedNoiseFloor;
        uniform float uChannelSaturationRisk;
        uniform int uDebugView;
        uniform int uMaskOutput;

        void main() {
            vec4 inputColor = texture(uInputImage, vTexCoord);
            vec4 map = texture(uExposureMap, vTexCoord);
            vec4 metrics = texture(uMetrics, vTexCoord);
            float ev = uHasMask != 0 ? mix(uMinEv + uBaseEv, uMaxEv + uBaseEv, clamp(map.r, 0.0, 1.0)) : 0.0;
            float gain = exp2(ev * clamp(uStrength, 0.0, 1.0));
            vec3 fused = max(inputColor.rgb, vec3(0.0)) * gain;
            if (uMaskOutput != 0 || uDebugView == 1) {
                FragColor = vec4(vec3(map.r), 1.0);
            } else if (uDebugView == 2) {
                FragColor = vec4(vec3(map.g), 1.0);
            } else if (uDebugView == 3) {
                FragColor = vec4(vec3(map.b), 1.0);
            } else if (uDebugView == 4) {
                FragColor = vec4(vec3(map.a), 1.0);
            } else if (uDebugView == 5) {
                float bands = floor(map.r * 8.999) / 8.0;
                FragColor = vec4(bands, 1.0 - bands, abs(0.5 - bands) * 2.0, 1.0);
            } else if (uDebugView == 6) {
                FragColor = vec4(vec3(metrics.b), 1.0);
            } else if (uDebugView == 7) {
                FragColor = vec4(vec3(metrics.r), 1.0);
            } else if (uDebugView == 8) {
                FragColor = vec4(vec3(metrics.g), 1.0);
            } else if (uDebugView == 9) {
                FragColor = vec4(vec3(metrics.a), 1.0);
            } else if (uDebugView == 10) {
                float rangePreview = clamp((uMaxEv - uMinEv) / 12.0, 0.0, 1.0);
                FragColor = vec4(map.r, rangePreview, clamp((uBaseEv + 4.0) / 8.0, 0.0, 1.0), 1.0);
            } else if (uDebugView == 11) {
                float lum = dot(max(inputColor.rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
                float snr = smoothstep(uEstimatedNoiseFloor * 2.0, uEstimatedNoiseFloor * 18.0, lum);
                FragColor = vec4(vec3(snr * (1.0 - metrics.a * 0.45)), 1.0);
            } else if (uDebugView == 12) {
                FragColor = vec4(vec3(map.b), 1.0);
            } else if (uDebugView == 13) {
                float maxChannel = max(max(inputColor.r, inputColor.g), inputColor.b);
                float minChannel = min(min(inputColor.r, inputColor.g), inputColor.b);
                float sat = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
                FragColor = vec4(vec3(clamp(max(sat, uChannelSaturationRisk) * smoothstep(0.25, 1.05, maxChannel), 0.0, 1.0)), 1.0);
            } else if (uDebugView == 14) {
                FragColor = vec4(vec3(clamp(metrics.a * (1.0 - metrics.g), 0.0, 1.0)), 1.0);
            } else {
                FragColor = vec4(fused, inputColor.a);
            }
        }
    )";

    if (!m_RawDetailFusionAnalysisProgram) {
        m_RawDetailFusionAnalysisProgram = GLHelpers::CreateShaderProgram(vertexSrc, analysisFragSrc);
    }
    if (!m_RawDetailFusionMetricsProgram) {
        m_RawDetailFusionMetricsProgram = GLHelpers::CreateShaderProgram(vertexSrc, metricsFragSrc);
    }
    if (!m_RawDetailFusionSmoothProgram) {
        m_RawDetailFusionSmoothProgram = GLHelpers::CreateShaderProgram(vertexSrc, smoothFragSrc);
    }
    if (!m_RawDetailFusionApplyProgram) {
        m_RawDetailFusionApplyProgram = GLHelpers::CreateShaderProgram(vertexSrc, applyFragSrc);
    }
}

void RenderPipeline::EnsureRawDevelopmentToneCurveProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uToneCurvePointCount;
        uniform vec2 uToneCurvePoints[12];

        float applyToneCurveChannel(float value) {
            if (uToneCurvePointCount < 2 || value <= 0.0) {
                return value;
            }

            vec2 previous = uToneCurvePoints[0];
            for (int i = 1; i < 12; ++i) {
                if (i >= uToneCurvePointCount) {
                    break;
                }
                vec2 current = uToneCurvePoints[i];
                if (value <= current.x) {
                    float span = max(current.x - previous.x, 0.00001);
                    float t = clamp((value - previous.x) / span, 0.0, 1.0);
                    return mix(previous.y, current.y, t);
                }
                previous = current;
            }
            return value + (previous.y - previous.x);
        }

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            vec3 rgb = vec3(
                applyToneCurveChannel(color.r),
                applyToneCurveChannel(color.g),
                applyToneCurveChannel(color.b));
            FragColor = vec4(rgb, color.a);
        }
    )";

    if (!m_RawDevelopmentToneCurveProgram) {
        m_RawDevelopmentToneCurveProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

unsigned int RenderPipeline::RenderRawDevelopmentToneCurve(
    unsigned int inputTexture,
    const std::vector<Raw::RawToneCurvePoint>& points) {
    if (!inputTexture || points.size() < 2) {
        return 0;
    }

    EnsureRawDevelopmentToneCurveProgram();
    if (!m_RawDevelopmentToneCurveProgram) {
        return 0;
    }

    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    if (!outputTexture) {
        return 0;
    }

    const bool rendered = RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDevelopmentToneCurveProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentToneCurveProgram, "uInputImage"), 0);

        const int count = std::min<int>(static_cast<int>(points.size()), 12);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentToneCurveProgram, "uToneCurvePointCount"), count);
        for (int i = 0; i < count; ++i) {
            char uniformName[64];
            std::snprintf(uniformName, sizeof(uniformName), "uToneCurvePoints[%d]", i);
            glUniform2f(
                glGetUniformLocation(m_RawDevelopmentToneCurveProgram, uniformName),
                std::clamp(points[static_cast<std::size_t>(i)].input, 0.0f, 1.0f),
                std::clamp(points[static_cast<std::size_t>(i)].output, 0.0f, 1.0f));
        }

        m_Quad.Draw();
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    });

    glActiveTexture(GL_TEXTURE0);
    if (!rendered) {
        glDeleteTextures(1, &outputTexture);
        return 0;
    }
    return outputTexture;
}

void RenderPipeline::EnsureRawDevelopmentLocalRangeProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uLocalRangePointCount;
        uniform vec2 uLocalRangePoints[12];
        uniform float uStrength;
        uniform float uMiddleGrey;
        uniform float uSmoothness;
        uniform float uEdgeProtection;
        uniform float uDetailProtection;
        uniform float uHighlightProtection;
        uniform vec2 uTexelSize;
        uniform int uRegionMaskEnabled;
        uniform int uRegionMaskMode;
        uniform int uRegionMaskInvert;
        uniform vec2 uRegionMaskCenter;
        uniform float uRegionMaskAngleRadians;
        uniform float uRegionMaskSize;
        uniform float uRegionMaskFeather;
        uniform vec2 uRegionMaskEvRange;
        uniform float uImageAspect;
        uniform int uColorMaskEnabled;
        uniform vec3 uColorMaskTarget;
        uniform float uColorMaskHueWidth;
        uniform float uColorMaskFeather;
        uniform float uColorMaskMinChroma;
        uniform int uTargetZoneCount;
        uniform int uTargetZoneCombineMode;
        uniform int uTargetZoneWorkingSpace;
        uniform float uLocalRangeMaxEv;
        uniform vec4 uTargetZoneTone[32];
        uniform vec4 uTargetZoneColor[32];
        uniform vec4 uTargetZoneMeta[32];
        uniform usampler2D uTargetZoneSelectionBits;
        uniform int uHasTargetZoneSelectionBits;
        uniform int uScopeOutput;

        float evaluateLocalRangeDelta(float sceneEv) {
            if (uLocalRangePointCount < 2 || uStrength <= 0.0001) {
                return 0.0;
            }

            vec2 previous = uLocalRangePoints[0];
            if (sceneEv <= previous.x) {
                return previous.y;
            }

            for (int i = 1; i < 12; ++i) {
                if (i >= uLocalRangePointCount) {
                    break;
                }
                vec2 current = uLocalRangePoints[i];
                if (sceneEv <= current.x) {
                    float span = max(current.x - previous.x, 0.0001);
                    float t = clamp((sceneEv - previous.x) / span, 0.0, 1.0);
                    return mix(previous.y, current.y, t);
                }
                previous = current;
            }
            return previous.y;
        }

        float lumaOf(vec3 rgb) {
            return max(dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.000001);
        }

        float colorChroma(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float maxChannel = max(max(positive.r, positive.g), positive.b);
            if (maxChannel <= 0.000001) {
                return 0.0;
            }
            float minChannel = min(min(positive.r, positive.g), positive.b);
            return clamp((maxChannel - minChannel) / maxChannel, 0.0, 1.0);
        }

        vec3 colorDirection(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float len = length(positive);
            if (len <= 0.000001) {
                return vec3(0.57735026);
            }
            return positive / len;
        }

        float sceneEvAt(vec2 uv) {
            vec3 rgb = texture(uInputImage, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
            return log2(lumaOf(rgb) / max(uMiddleGrey, 0.000001));
        }

        float edgeAwareWeight(float evDifference) {
            float diff = abs(evDifference);
            float edgeProtection = clamp(uEdgeProtection, 0.0, 1.0);
            float detailProtection = clamp(uDetailProtection, 0.0, 1.0);
            float sigma = max(mix(2.40, 0.45, edgeProtection), 0.05);
            float rangeWeight = exp(-(diff * diff) / (2.0 * sigma * sigma));
            float textureLow = mix(0.12, 0.35, detailProtection);
            float textureHigh = mix(0.55, 1.25, detailProtection);
            float textureInclusion = 1.0 - smoothstep(textureLow, textureHigh, diff);
            rangeWeight = max(rangeWeight, textureInclusion * detailProtection);
            return mix(1.0, clamp(rangeWeight, 0.0, 1.0), edgeProtection);
        }

        void accumulateSceneEv(vec2 offset, float spatialWeight, float centerSceneEv, inout float weightedEv, inout float weightSum) {
            float sampleSceneEv = sceneEvAt(vTexCoord + offset);
            float weight = spatialWeight * edgeAwareWeight(sampleSceneEv - centerSceneEv);
            weightedEv += sampleSceneEv * weight;
            weightSum += weight;
        }

        float edgeAwareSceneEv(float centerSceneEv) {
            float smoothness = clamp(uSmoothness, 0.0, 1.0);
            if (smoothness <= 0.0001) {
                return centerSceneEv;
            }

            vec2 nearRadius = uTexelSize * mix(2.0, 18.0, smoothness);
            vec2 farRadius = uTexelSize * mix(5.0, 44.0, smoothness);
            float weightedEv = centerSceneEv;
            float weightSum = 1.0;

            accumulateSceneEv(vec2( nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);

            float smoothedSceneEv = weightedEv / max(weightSum, 0.0001);
            return mix(centerSceneEv, smoothedSceneEv, smoothness);
        }

        float protectedLocalDeltaEv(float mapSceneEv) {
            float deltaEv = uStrength * evaluateLocalRangeDelta(mapSceneEv);
            if (deltaEv > 0.0) {
                float highlightEnd = max(uLocalRangePoints[max(uLocalRangePointCount - 1, 0)].x, 1.5001);
                float highlightZone = smoothstep(1.5, highlightEnd, mapSceneEv);
                deltaEv *= 1.0 - clamp(uHighlightProtection, 0.0, 1.0) * highlightZone * 0.85;
            }
            return deltaEv;
        }

        float regionMaskValue(float mapSceneEv) {
            if (uRegionMaskEnabled == 0 || uRegionMaskMode == 0) {
                return 1.0;
            }

            float mask = 1.0;
            if (uRegionMaskMode == 1) {
                vec2 direction = vec2(cos(uRegionMaskAngleRadians), sin(uRegionMaskAngleRadians));
                float projection = dot(vTexCoord - uRegionMaskCenter, direction);
                float softWidth = max(uRegionMaskSize * mix(0.08, 1.0, clamp(uRegionMaskFeather, 0.0, 1.0)), 0.001);
                mask = smoothstep(-softWidth, softWidth, projection);
            } else if (uRegionMaskMode == 2) {
                vec2 delta = vTexCoord - uRegionMaskCenter;
                delta.x *= max(uImageAspect, 0.001);
                float distanceFromCenter = length(delta);
                float feather = uRegionMaskSize * clamp(uRegionMaskFeather, 0.0, 1.0);
                float inner = max(uRegionMaskSize - feather, 0.0);
                float outer = uRegionMaskSize + feather;
                mask = 1.0 - smoothstep(inner, outer, distanceFromCenter);
            } else if (uRegionMaskMode == 3) {
                float featherEv = max(clamp(uRegionMaskFeather, 0.0, 1.0) * 4.0, 0.02);
                float lowMask = smoothstep(uRegionMaskEvRange.x - featherEv, uRegionMaskEvRange.x, mapSceneEv);
                float highMask = 1.0 - smoothstep(uRegionMaskEvRange.y, uRegionMaskEvRange.y + featherEv, mapSceneEv);
                mask = lowMask * highMask;
            }

            mask = clamp(mask, 0.0, 1.0);
            return uRegionMaskInvert != 0 ? 1.0 - mask : mask;
        }

        float colorMaskValue(vec3 rgb) {
            if (uColorMaskEnabled == 0) {
                return 1.0;
            }

            vec3 targetDirection = colorDirection(uColorMaskTarget);
            vec3 sampleDirection = colorDirection(rgb);
            float directionDistance = length(targetDirection - sampleDirection);
            float feather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.65, 0.015);
            float hueMask = 1.0 - smoothstep(
                clamp(uColorMaskHueWidth, 0.02, 1.20),
                clamp(uColorMaskHueWidth, 0.02, 1.20) + feather,
                directionDistance);

            float targetChroma = colorChroma(uColorMaskTarget);
            float sampleChroma = colorChroma(rgb);
            float minChroma = clamp(uColorMaskMinChroma, 0.0, 1.0);
            float chromaMask = 1.0;
            if (targetChroma >= 0.08) {
                chromaMask = smoothstep(minChroma, min(minChroma + 0.12, 1.0), sampleChroma);
            } else {
                float neutralFeather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.25, 0.04);
                chromaMask = 1.0 - smoothstep(minChroma, min(minChroma + neutralFeather, 1.0), sampleChroma);
            }
            return clamp(hueMask * chromaMask, 0.0, 1.0);
        }
    )"
    R"(
        vec3 sceneRgbToUvChroma(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            vec3 xyz;
            if (uTargetZoneWorkingSpace == 1) {
                xyz = vec3(
                    dot(positive, vec3(0.63695805, 0.14461690, 0.16888098)),
                    dot(positive, vec3(0.26270021, 0.67799807, 0.05930172)),
                    dot(positive, vec3(0.00000000, 0.02807269, 1.06098506)));
            } else {
                xyz = vec3(
                    dot(positive, vec3(0.41245640, 0.35757610, 0.18043750)),
                    dot(positive, vec3(0.21267290, 0.71515220, 0.07217500)),
                    dot(positive, vec3(0.01933390, 0.11919200, 0.95030410)));
            }
            float denominator = xyz.x + 15.0 * xyz.y + 3.0 * xyz.z;
            vec2 whiteUv = vec2(0.19783001, 0.46831999);
            vec2 uv = denominator > 0.0000001
                ? vec2(4.0 * xyz.x / denominator, 9.0 * xyz.y / denominator)
                : whiteUv;
            return vec3(uv, length(uv - whiteUv));
        }

        float targetZoneWeight(int index, float mapSceneEv, vec3 rgb) {
            vec4 tone = uTargetZoneTone[index];
            vec4 colorTarget = uTargetZoneColor[index];
            vec4 meta = uTargetZoneMeta[index];
            if (meta.y < 0.5) {
                return 0.0;
            }
            if (meta.w < 0.5) {
                if (uHasTargetZoneSelectionBits == 0) {
                    return 0.0;
                }
                uint selectedBits = texture(uTargetZoneSelectionBits, vTexCoord).r;
                uint zoneBit = 1u << uint(index);
                if ((selectedBits & zoneBit) == 0u) {
                    return 0.0;
                }
            }
            float distanceEv = abs(mapSceneEv - tone.x);
            float tonalWeight =
                1.0 - smoothstep(max(tone.y, 0.05), max(tone.y, 0.05) + max(tone.z, 0.02), distanceEv);
            float colorWeight = 1.0;
            if (meta.z > 0.5) {
                vec3 sample = sceneRgbToUvChroma(rgb);
                float colorDistance = distance(sample.xy, colorTarget.xy);
                colorWeight =
                    1.0 - smoothstep(max(colorTarget.z, 0.002), max(colorTarget.z, 0.002) + max(colorTarget.w, 0.002), colorDistance);
                if (meta.x < 0.018) {
                    colorWeight *=
                        1.0 - smoothstep(max(0.018, colorTarget.z), max(0.018, colorTarget.z) + max(colorTarget.w, 0.002), sample.z);
                } else {
                    colorWeight *= smoothstep(0.006, 0.020, sample.z);
                }
            }
            return clamp(tonalWeight * colorWeight, 0.0, 1.0);
        }

        float protectedTargetZoneDelta(float deltaEv, float mapSceneEv) {
            if (deltaEv > 0.0) {
                float highlightZone = smoothstep(1.5, max(uLocalRangeMaxEv, 1.5001), mapSceneEv);
                deltaEv *= 1.0 - clamp(uHighlightProtection, 0.0, 1.0) * highlightZone * 0.85;
            }
            return deltaEv;
        }

        float combinedTargetZoneDelta(float targetSceneEv, float protectedSceneEv, vec3 rgb) {
            float sum = 0.0;
            float weightSum = 0.0;
            float strongest = 0.0;
            for (int i = 0; i < 32; ++i) {
                if (i >= uTargetZoneCount) {
                    break;
                }
                float weight = targetZoneWeight(i, targetSceneEv, rgb);
                float weightedDelta =
                    protectedTargetZoneDelta(uTargetZoneTone[i].w * weight * uStrength, protectedSceneEv);
                if (uTargetZoneCombineMode == 1) {
                    if (abs(weightedDelta) > abs(strongest)) {
                        strongest = weightedDelta;
                    }
                } else {
                    sum += weightedDelta;
                    weightSum += weight;
                }
            }
            if (uTargetZoneCombineMode == 1) {
                return strongest;
            }
            if (uTargetZoneCombineMode == 2) {
                sum /= max(1.0, weightSum);
            }
            return clamp(sum, -4.0, 4.0);
        }

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            vec3 rgb = max(color.rgb, vec3(0.0));
            float luma = lumaOf(rgb);
            float sceneEv = log2(luma / max(uMiddleGrey, 0.000001));
            float mapSceneEv = edgeAwareSceneEv(sceneEv);
            if (uScopeOutput != 0) {
                // Keep the scene RGB available for the optional channel
                // histogram and carry the exact edge-aware EV control signal
                // in alpha for the primary Zones distribution.
                FragColor = vec4(color.rgb, mapSceneEv);
                return;
            }
            float baseDeltaEv =
                protectedLocalDeltaEv(mapSceneEv) *
                regionMaskValue(mapSceneEv) *
                colorMaskValue(rgb);
            float deltaEv = clamp(
                baseDeltaEv + combinedTargetZoneDelta(sceneEv, mapSceneEv, rgb),
                -4.0,
                4.0);
            float scale = exp2(deltaEv);
            FragColor = vec4(color.rgb * scale, color.a);
        }
    )";

    if (!m_RawDevelopmentLocalRangeProgram) {
        m_RawDevelopmentLocalRangeProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureRawDevelopmentLocalRangeOverlayProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uLocalRangePointCount;
        uniform vec2 uLocalRangePoints[12];
        uniform float uStrength;
        uniform float uMiddleGrey;
        uniform int uOverlayMode;
        uniform float uSmoothness;
        uniform float uEdgeProtection;
        uniform float uDetailProtection;
        uniform float uHighlightProtection;
        uniform vec2 uTexelSize;
        uniform int uRegionMaskEnabled;
        uniform int uRegionMaskMode;
        uniform int uRegionMaskInvert;
        uniform vec2 uRegionMaskCenter;
        uniform float uRegionMaskAngleRadians;
        uniform float uRegionMaskSize;
        uniform float uRegionMaskFeather;
        uniform vec2 uRegionMaskEvRange;
        uniform float uImageAspect;
        uniform int uColorMaskEnabled;
        uniform vec3 uColorMaskTarget;
        uniform float uColorMaskHueWidth;
        uniform float uColorMaskFeather;
        uniform float uColorMaskMinChroma;
        uniform int uTargetZoneCount;
        uniform int uTargetZoneCombineMode;
        uniform int uTargetZoneWorkingSpace;
        uniform float uLocalRangeMaxEv;
        uniform vec4 uTargetZoneTone[32];
        uniform vec4 uTargetZoneColor[32];
        uniform vec4 uTargetZoneMeta[32];
        uniform usampler2D uTargetZoneSelectionBits;
        uniform int uHasTargetZoneSelectionBits;
        uniform vec2 uTargetZoneSelectionTexelSize;
        uniform int uTargetOutlineProvisional;

        float evaluateLocalRangeDelta(float sceneEv) {
            if (uLocalRangePointCount < 2 || uStrength <= 0.0001) {
                return 0.0;
            }

            vec2 previous = uLocalRangePoints[0];
            if (sceneEv <= previous.x) {
                return previous.y;
            }

            for (int i = 1; i < 12; ++i) {
                if (i >= uLocalRangePointCount) {
                    break;
                }
                vec2 current = uLocalRangePoints[i];
                if (sceneEv <= current.x) {
                    float span = max(current.x - previous.x, 0.0001);
                    float t = clamp((sceneEv - previous.x) / span, 0.0, 1.0);
                    return mix(previous.y, current.y, t);
                }
                previous = current;
            }
            return previous.y;
        }

        float lumaOf(vec3 rgb) {
            return max(dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.000001);
        }

        float colorChroma(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float maxChannel = max(max(positive.r, positive.g), positive.b);
            if (maxChannel <= 0.000001) {
                return 0.0;
            }
            float minChannel = min(min(positive.r, positive.g), positive.b);
            return clamp((maxChannel - minChannel) / maxChannel, 0.0, 1.0);
        }

        vec3 colorDirection(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float len = length(positive);
            if (len <= 0.000001) {
                return vec3(0.57735026);
            }
            return positive / len;
        }

        float sceneEvAt(vec2 uv) {
            vec3 rgb = texture(uInputImage, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
            return log2(lumaOf(rgb) / max(uMiddleGrey, 0.000001));
        }

        float edgeAwareWeight(float evDifference) {
            float diff = abs(evDifference);
            float edgeProtection = clamp(uEdgeProtection, 0.0, 1.0);
            float detailProtection = clamp(uDetailProtection, 0.0, 1.0);
            float sigma = max(mix(2.40, 0.45, edgeProtection), 0.05);
            float rangeWeight = exp(-(diff * diff) / (2.0 * sigma * sigma));
            float textureLow = mix(0.12, 0.35, detailProtection);
            float textureHigh = mix(0.55, 1.25, detailProtection);
            float textureInclusion = 1.0 - smoothstep(textureLow, textureHigh, diff);
            rangeWeight = max(rangeWeight, textureInclusion * detailProtection);
            return mix(1.0, clamp(rangeWeight, 0.0, 1.0), edgeProtection);
        }

        void accumulateSceneEv(vec2 offset, float spatialWeight, float centerSceneEv, inout float weightedEv, inout float weightSum) {
            float sampleSceneEv = sceneEvAt(vTexCoord + offset);
            float weight = spatialWeight * edgeAwareWeight(sampleSceneEv - centerSceneEv);
            weightedEv += sampleSceneEv * weight;
            weightSum += weight;
        }

        float edgeAwareSceneEv(float centerSceneEv) {
            float smoothness = clamp(uSmoothness, 0.0, 1.0);
            if (smoothness <= 0.0001) {
                return centerSceneEv;
            }

            vec2 nearRadius = uTexelSize * mix(2.0, 18.0, smoothness);
            vec2 farRadius = uTexelSize * mix(5.0, 44.0, smoothness);
            float weightedEv = centerSceneEv;
            float weightSum = 1.0;

            accumulateSceneEv(vec2( nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);

            float smoothedSceneEv = weightedEv / max(weightSum, 0.0001);
            return mix(centerSceneEv, smoothedSceneEv, smoothness);
        }

        float protectedLocalDeltaEv(float mapSceneEv) {
            float deltaEv = uStrength * evaluateLocalRangeDelta(mapSceneEv);
            if (deltaEv > 0.0) {
                float highlightEnd = max(uLocalRangePoints[max(uLocalRangePointCount - 1, 0)].x, 1.5001);
                float highlightZone = smoothstep(1.5, highlightEnd, mapSceneEv);
                deltaEv *= 1.0 - clamp(uHighlightProtection, 0.0, 1.0) * highlightZone * 0.85;
            }
            return deltaEv;
        }

        float regionMaskValue(float mapSceneEv) {
            if (uRegionMaskEnabled == 0 || uRegionMaskMode == 0) {
                return 1.0;
            }

            float mask = 1.0;
            if (uRegionMaskMode == 1) {
                vec2 direction = vec2(cos(uRegionMaskAngleRadians), sin(uRegionMaskAngleRadians));
                float projection = dot(vTexCoord - uRegionMaskCenter, direction);
                float softWidth = max(uRegionMaskSize * mix(0.08, 1.0, clamp(uRegionMaskFeather, 0.0, 1.0)), 0.001);
                mask = smoothstep(-softWidth, softWidth, projection);
            } else if (uRegionMaskMode == 2) {
                vec2 delta = vTexCoord - uRegionMaskCenter;
                delta.x *= max(uImageAspect, 0.001);
                float distanceFromCenter = length(delta);
                float feather = uRegionMaskSize * clamp(uRegionMaskFeather, 0.0, 1.0);
                float inner = max(uRegionMaskSize - feather, 0.0);
                float outer = uRegionMaskSize + feather;
                mask = 1.0 - smoothstep(inner, outer, distanceFromCenter);
            } else if (uRegionMaskMode == 3) {
                float featherEv = max(clamp(uRegionMaskFeather, 0.0, 1.0) * 4.0, 0.02);
                float lowMask = smoothstep(uRegionMaskEvRange.x - featherEv, uRegionMaskEvRange.x, mapSceneEv);
                float highMask = 1.0 - smoothstep(uRegionMaskEvRange.y, uRegionMaskEvRange.y + featherEv, mapSceneEv);
                mask = lowMask * highMask;
            }

            mask = clamp(mask, 0.0, 1.0);
            return uRegionMaskInvert != 0 ? 1.0 - mask : mask;
        }

        float colorMaskValue(vec3 rgb) {
            if (uColorMaskEnabled == 0) {
                return 1.0;
            }

            vec3 targetDirection = colorDirection(uColorMaskTarget);
            vec3 sampleDirection = colorDirection(rgb);
            float directionDistance = length(targetDirection - sampleDirection);
            float feather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.65, 0.015);
            float hueMask = 1.0 - smoothstep(
                clamp(uColorMaskHueWidth, 0.02, 1.20),
                clamp(uColorMaskHueWidth, 0.02, 1.20) + feather,
                directionDistance);

            float targetChroma = colorChroma(uColorMaskTarget);
            float sampleChroma = colorChroma(rgb);
            float minChroma = clamp(uColorMaskMinChroma, 0.0, 1.0);
            float chromaMask = 1.0;
            if (targetChroma >= 0.08) {
                chromaMask = smoothstep(minChroma, min(minChroma + 0.12, 1.0), sampleChroma);
            } else {
                float neutralFeather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.25, 0.04);
                chromaMask = 1.0 - smoothstep(minChroma, min(minChroma + neutralFeather, 1.0), sampleChroma);
            }
            return clamp(hueMask * chromaMask, 0.0, 1.0);
        }

        vec3 sceneRgbToUvChroma(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            vec3 xyz;
            if (uTargetZoneWorkingSpace == 1) {
                xyz = vec3(
                    dot(positive, vec3(0.63695805, 0.14461690, 0.16888098)),
                    dot(positive, vec3(0.26270021, 0.67799807, 0.05930172)),
                    dot(positive, vec3(0.00000000, 0.02807269, 1.06098506)));
            } else {
                xyz = vec3(
                    dot(positive, vec3(0.41245640, 0.35757610, 0.18043750)),
                    dot(positive, vec3(0.21267290, 0.71515220, 0.07217500)),
                    dot(positive, vec3(0.01933390, 0.11919200, 0.95030410)));
            }
            float denominator = xyz.x + 15.0 * xyz.y + 3.0 * xyz.z;
            vec2 whiteUv = vec2(0.19783001, 0.46831999);
            vec2 uv = denominator > 0.0000001
                ? vec2(4.0 * xyz.x / denominator, 9.0 * xyz.y / denominator)
                : whiteUv;
            return vec3(uv, length(uv - whiteUv));
        }
    )"
    R"(
        float targetZoneWeight(int index, float targetSceneEv, vec3 rgb) {
            vec4 tone = uTargetZoneTone[index];
            vec4 colorTarget = uTargetZoneColor[index];
            vec4 meta = uTargetZoneMeta[index];
            if (meta.y < 0.5) {
                return 0.0;
            }
            if (meta.w < 0.5) {
                if (uHasTargetZoneSelectionBits == 0) {
                    return 0.0;
                }
                uint selectedBits = texture(uTargetZoneSelectionBits, vTexCoord).r;
                uint zoneBit = 1u << uint(index);
                if ((selectedBits & zoneBit) == 0u) {
                    return 0.0;
                }
            }
            float distanceEv = abs(targetSceneEv - tone.x);
            float tonalWeight =
                1.0 - smoothstep(max(tone.y, 0.05), max(tone.y, 0.05) + max(tone.z, 0.02), distanceEv);
            float colorWeight = 1.0;
            if (meta.z > 0.5) {
                vec3 sample = sceneRgbToUvChroma(rgb);
                float colorDistance = distance(sample.xy, colorTarget.xy);
                colorWeight =
                    1.0 - smoothstep(max(colorTarget.z, 0.002), max(colorTarget.z, 0.002) + max(colorTarget.w, 0.002), colorDistance);
                if (meta.x < 0.018) {
                    colorWeight *=
                        1.0 - smoothstep(max(0.018, colorTarget.z), max(0.018, colorTarget.z) + max(colorTarget.w, 0.002), sample.z);
                } else {
                    colorWeight *= smoothstep(0.006, 0.020, sample.z);
                }
            }
            return clamp(tonalWeight * colorWeight, 0.0, 1.0);
        }

        float protectedTargetZoneDelta(float deltaEv, float mapSceneEv) {
            if (deltaEv > 0.0) {
                float highlightZone = smoothstep(1.5, max(uLocalRangeMaxEv, 1.5001), mapSceneEv);
                deltaEv *= 1.0 - clamp(uHighlightProtection, 0.0, 1.0) * highlightZone * 0.85;
            }
            return deltaEv;
        }

        float combinedTargetZoneDelta(
            float targetSceneEv,
            float mapSceneEv,
            vec3 rgb,
            out float targetMask) {
            float sum = 0.0;
            float weightSum = 0.0;
            float strongest = 0.0;
            targetMask = 0.0;
            for (int i = 0; i < 32; ++i) {
                if (i >= uTargetZoneCount) {
                    break;
                }
                float weight = targetZoneWeight(i, targetSceneEv, rgb);
                targetMask = max(targetMask, weight);
                float weightedDelta =
                    protectedTargetZoneDelta(uTargetZoneTone[i].w * weight * uStrength, mapSceneEv);
                if (uTargetZoneCombineMode == 1) {
                    if (abs(weightedDelta) > abs(strongest)) {
                        strongest = weightedDelta;
                    }
                } else {
                    sum += weightedDelta;
                    weightSum += weight;
                }
            }
            if (uTargetZoneCombineMode == 1) {
                return strongest;
            }
            if (uTargetZoneCombineMode == 2) {
                sum /= max(1.0, weightSum);
            }
            return clamp(sum, -4.0, 4.0);
        }

        float targetSelectionMembership(vec2 uv) {
            if (uHasTargetZoneSelectionBits == 0) {
                return 0.0;
            }
            uint selectedBits =
                texture(uTargetZoneSelectionBits, clamp(uv, vec2(0.0), vec2(1.0))).r;
            return (selectedBits & 1u) != 0u ? 1.0 : 0.0;
        }

        float targetSelectionBoundary(float radius) {
            vec2 stepUv = uTargetZoneSelectionTexelSize * radius;
            float center = targetSelectionMembership(vTexCoord);
            float boundary = 0.0;
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord + vec2(stepUv.x, 0.0))));
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord - vec2(stepUv.x, 0.0))));
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord + vec2(0.0, stepUv.y))));
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord - vec2(0.0, stepUv.y))));
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord + stepUv)));
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord - stepUv)));
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord + vec2(stepUv.x, -stepUv.y))));
            boundary = max(
                boundary,
                abs(center - targetSelectionMembership(
                    vTexCoord + vec2(-stepUv.x, stepUv.y))));
            return boundary;
        }

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            vec3 rgb = max(color.rgb, vec3(0.0));
            float luma = lumaOf(rgb);
            float sceneEv = log2(luma / max(uMiddleGrey, 0.000001));
            float mapSceneEv = edgeAwareSceneEv(sceneEv);
            float regionMask = regionMaskValue(mapSceneEv);
            float colorMask = colorMaskValue(rgb);
            float qualificationMask = regionMask * colorMask;

            if (uOverlayMode == 4) {
                if (uTargetOutlineProvisional != 0 ||
                    uHasTargetZoneSelectionBits == 0) {
                    FragColor = vec4(0.0);
                    return;
                }
                float coreBoundary = targetSelectionBoundary(1.0);
                float softBoundary = targetSelectionBoundary(2.25);
                float alpha =
                    max(coreBoundary * 0.88, softBoundary * 0.24);
                if (alpha <= 0.001) {
                    FragColor = vec4(0.0);
                    return;
                }
                FragColor = vec4(vec3(0.04, 0.86, 0.72), alpha);
                return;
            }

            float targetMask = 0.0;
            float targetDeltaEv =
                combinedTargetZoneDelta(
                    sceneEv,
                    mapSceneEv,
                    rgb,
                    targetMask);

            if (uOverlayMode == 3) {
                if ((uRegionMaskEnabled == 0 || uRegionMaskMode == 0) &&
                    uColorMaskEnabled == 0 &&
                    uTargetZoneCount == 0) {
                    FragColor = vec4(0.0);
                    return;
                }
                if ((uRegionMaskEnabled == 0 || uRegionMaskMode == 0) &&
                    uColorMaskEnabled == 0) {
                    qualificationMask = targetMask;
                } else {
                    qualificationMask = max(qualificationMask, targetMask);
                }
                vec3 offColor = vec3(0.02, 0.08, 0.10);
                vec3 onColor = vec3(0.04, 0.86, 0.72);
                float alpha = 0.18 + smoothstep(0.02, 0.85, qualificationMask) * 0.48;
                FragColor = vec4(mix(offColor, onColor, qualificationMask), alpha);
                return;
            }

            float deltaEv = clamp(
                protectedLocalDeltaEv(mapSceneEv) * qualificationMask + targetDeltaEv,
                -4.0,
                4.0);
            float magnitude = clamp(abs(deltaEv) / 2.0, 0.0, 1.0);

            if (magnitude <= 0.0001) {
                FragColor = vec4(0.0);
                return;
            }

            vec3 liftColor = vec3(0.04, 0.82, 0.82);
            vec3 compressColor = vec3(1.0, 0.24, 0.48);
            vec3 neutralColor = vec3(0.92, 0.96, 1.0);

            float alpha = smoothstep(0.02, 0.45, magnitude);
            vec3 overlayColor = deltaEv >= 0.0 ? liftColor : compressColor;
            if (uOverlayMode == 1) {
                overlayColor = mix(neutralColor, overlayColor, 0.35);
                alpha *= 0.42;
            } else {
                overlayColor = mix(neutralColor, overlayColor, clamp(magnitude * 1.4, 0.0, 1.0));
                alpha *= 0.72;
            }

            FragColor = vec4(overlayColor, alpha);
        }
    )";

    if (!m_RawDevelopmentLocalRangeOverlayProgram) {
        m_RawDevelopmentLocalRangeOverlayProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureRawDevelopmentLocalRangeQualifierProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";
    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uMiddleGrey;
        uniform int uWorkingSpace;
        uniform vec4 uTone;
        uniform vec4 uColor;
        uniform vec2 uMeta;

        float lumaOf(vec3 rgb) {
            return max(dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.000001);
        }

        vec3 sceneRgbToUvChroma(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            vec3 xyz;
            if (uWorkingSpace == 1) {
                xyz = vec3(
                    dot(positive, vec3(0.63695805, 0.14461690, 0.16888098)),
                    dot(positive, vec3(0.26270021, 0.67799807, 0.05930172)),
                    dot(positive, vec3(0.00000000, 0.02807269, 1.06098506)));
            } else {
                xyz = vec3(
                    dot(positive, vec3(0.41245640, 0.35757610, 0.18043750)),
                    dot(positive, vec3(0.21267290, 0.71515220, 0.07217500)),
                    dot(positive, vec3(0.01933390, 0.11919200, 0.95030410)));
            }
            float denominator = xyz.x + 15.0 * xyz.y + 3.0 * xyz.z;
            vec2 whiteUv = vec2(0.19783001, 0.46831999);
            vec2 uv = denominator > 0.0000001
                ? vec2(4.0 * xyz.x / denominator, 9.0 * xyz.y / denominator)
                : whiteUv;
            return vec3(uv, length(uv - whiteUv));
        }

        void main() {
            vec3 rgb = max(texture(uInputImage, vTexCoord).rgb, vec3(0.0));
            float sceneEv = log2(lumaOf(rgb) / max(uMiddleGrey, 0.000001));
            float distanceEv = abs(sceneEv - uTone.x);
            float weight =
                1.0 - smoothstep(max(uTone.y, 0.05), max(uTone.y, 0.05) + max(uTone.z, 0.02), distanceEv);
            if (uMeta.y > 0.5) {
                vec3 sample = sceneRgbToUvChroma(rgb);
                float colorDistance = distance(sample.xy, uColor.xy);
                float colorWeight =
                    1.0 - smoothstep(max(uColor.z, 0.002), max(uColor.z, 0.002) + max(uColor.w, 0.002), colorDistance);
                if (uMeta.x < 0.018) {
                    colorWeight *=
                        1.0 - smoothstep(max(0.018, uColor.z), max(0.018, uColor.z) + max(uColor.w, 0.002), sample.z);
                } else {
                    colorWeight *= smoothstep(0.006, 0.020, sample.z);
                }
                weight *= colorWeight;
            }
            weight = clamp(weight, 0.0, 1.0);
            FragColor = vec4(weight, weight, weight, 1.0);
        }
    )";
    if (!m_RawDevelopmentLocalRangeQualifierProgram) {
        m_RawDevelopmentLocalRangeQualifierProgram =
            GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

unsigned int RenderPipeline::BuildRawDevelopmentLocalRangeSelectionBits(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRangeInput,
    Raw::RawWorkingSpace workingSpace,
    std::size_t inputStageFingerprint,
    int maxSelectionDimension) {
    const Stack::RawRecipe::RawLocalRangeRecipe localRange =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRangeInput);
    const bool hasSelectedZones = std::any_of(
        localRange.targetZones.begin(),
        localRange.targetZones.end(),
        [](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
            return zone.enabled &&
                zone.scope == Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas &&
                !zone.seeds.empty();
        });
    if (!inputTexture || !hasSelectedZones || m_Width <= 0 || m_Height <= 0) {
        ClearRawDevelopmentLocalRangeSelectionBits();
        return 0;
    }

    const int boundedMaximumDimension =
        std::clamp(maxSelectionDimension, 64, 1536);
    const std::size_t fingerprint =
        Stack::Renderer::RawDevelopmentCache::
            BuildLocalRangeSelectionFingerprint(
                localRange,
                workingSpace,
                inputStageFingerprint,
                boundedMaximumDimension);
    const bool inputMatches = inputStageFingerprint != 0
        ? m_RawDevelopmentLocalRangeSelectionBitsInputFingerprint ==
            inputStageFingerprint
        : m_RawDevelopmentLocalRangeSelectionBitsInputTexture == inputTexture;
    if (m_RawDevelopmentLocalRangeSelectionBitsTexture != 0 &&
        inputMatches &&
        m_RawDevelopmentLocalRangeSelectionBitsWidth == m_Width &&
        m_RawDevelopmentLocalRangeSelectionBitsHeight == m_Height &&
        m_RawDevelopmentLocalRangeSelectionBitsFingerprint == fingerprint) {
        return m_RawDevelopmentLocalRangeSelectionBitsTexture;
    }

    EnsureRawDevelopmentLocalRangeQualifierProgram();
    if (!m_RawDevelopmentLocalRangeQualifierProgram) {
        return 0;
    }

    // Connected-component growth is a topology operation, not a final-image
    // detail pass. Keep it bounded on full-resolution settles; the resulting
    // integer mask is sampled with normalized coordinates by the full-size
    // Local Range shader.
    const float selectionScale = std::min(
        1.0f,
        static_cast<float>(boundedMaximumDimension) /
            static_cast<float>(std::max(m_Width, m_Height)));
    const int selectionWidth = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(m_Width) * selectionScale)));
    const int selectionHeight = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(m_Height) * selectionScale)));
    std::size_t pixelCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            selectionWidth, selectionHeight, 1, pixelCount)) {
        return 0;
    }
    std::vector<std::uint32_t> selectedBits;
    std::vector<unsigned char> qualifier;
    std::vector<unsigned char> visited;
    std::vector<int> queue;
    try {
        selectedBits.assign(pixelCount, 0u);
        qualifier.assign(pixelCount, 0u);
        visited.assign(pixelCount, 0u);
        queue.reserve(
            std::min<std::size_t>(
                pixelCount, 1024u * 1024u));
    } catch (const std::bad_alloc&) {
        return 0;
    } catch (const std::length_error&) {
        return 0;
    }

    const int zoneCount = std::min<int>(
        static_cast<int>(localRange.targetZones.size()),
        static_cast<int>(Stack::RawRecipe::kMaxRawLocalRangeTargetZones));
    for (int zoneIndex = 0; zoneIndex < zoneCount; ++zoneIndex) {
        const Stack::RawRecipe::RawLocalRangeTargetZone& zone =
            localRange.targetZones[static_cast<std::size_t>(zoneIndex)];
        if (!zone.enabled ||
            zone.scope != Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas ||
            zone.seeds.empty()) {
            continue;
        }

        const unsigned int qualifierTexture =
            GLHelpers::CreateEmptyTexture(selectionWidth, selectionHeight);
        if (!qualifierTexture) {
            continue;
        }
        const Stack::Renderer::GLState::FramebufferState
            savedFramebufferState(true);
        const unsigned int qualifierFramebuffer =
            GLHelpers::CreateFBO(qualifierTexture);
        bool rendered = qualifierFramebuffer != 0;
        if (rendered) {
            while (glGetError() != GL_NO_ERROR) {}
            glBindFramebuffer(GL_FRAMEBUFFER, qualifierFramebuffer);
            glViewport(0, 0, selectionWidth, selectionHeight);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(m_RawDevelopmentLocalRangeQualifierProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentLocalRangeQualifierProgram, "uInputImage"),
                0);
            glUniform1f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeQualifierProgram, "uMiddleGrey"),
                localRange.middleGrey);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentLocalRangeQualifierProgram, "uWorkingSpace"),
                workingSpace == Raw::RawWorkingSpace::LinearRec2020D65 ? 1 : 0);
            glUniform4f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeQualifierProgram, "uTone"),
                zone.centerEv,
                zone.coreHalfWidthEv,
                zone.featherEv,
                zone.deltaEv);
            glUniform4f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeQualifierProgram, "uColor"),
                zone.targetUPrime,
                zone.targetVPrime,
                zone.colorRadius,
                zone.colorFeather);
            glUniform2f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeQualifierProgram, "uMeta"),
                zone.targetChroma,
                zone.colorEnabled ? 1.0f : 0.0f);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
            rendered = glGetError() == GL_NO_ERROR;
        }
        if (!rendered) {
            savedFramebufferState.Restore(true);
            if (qualifierFramebuffer != 0) {
                glDeleteFramebuffers(1, &qualifierFramebuffer);
            }
            glDeleteTextures(1, &qualifierTexture);
            continue;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, qualifierFramebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        const Stack::Renderer::GLState::PixelPackState savedPackState;
        savedPackState.ConfigureTightCpuReadback();
        while (glGetError() != GL_NO_ERROR) {}
        bool readbackOk =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE;
        if (readbackOk) {
            glReadPixels(
                0,
                0,
                selectionWidth,
                selectionHeight,
                GL_RED,
                GL_UNSIGNED_BYTE,
                qualifier.data());
            readbackOk = glGetError() == GL_NO_ERROR;
        }
        savedPackState.Restore();
        savedFramebufferState.Restore(true);
        glDeleteFramebuffers(1, &qualifierFramebuffer);
        glDeleteTextures(1, &qualifierTexture);
        if (!readbackOk) {
            continue;
        }

        std::fill(visited.begin(), visited.end(), 0u);
        constexpr unsigned char kGrowthThreshold = 38u;
        constexpr unsigned char kStrongThreshold = 128u;
        const int searchRadius =
            std::max(4, std::min(selectionWidth, selectionHeight) / 100);
        const std::uint32_t zoneBit = std::uint32_t(1u) << zoneIndex;

        for (const Stack::RawRecipe::RawLocalRangeTargetSeed& seed : zone.seeds) {
            int seedX = std::clamp(
                static_cast<int>(std::lround(
                    seed.sourceU * static_cast<float>(selectionWidth - 1))),
                0,
                selectionWidth - 1);
            int seedY = std::clamp(
                static_cast<int>(std::lround(
                    (1.0f - seed.sourceV) *
                    static_cast<float>(selectionHeight - 1))),
                0,
                selectionHeight - 1);
            int startIndex = seedY * selectionWidth + seedX;
            if (qualifier[static_cast<std::size_t>(startIndex)] < kStrongThreshold) {
                int bestIndex = -1;
                int bestDistanceSquared = searchRadius * searchRadius + 1;
                for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
                    const int y = seedY + dy;
                    if (y < 0 || y >= selectionHeight) {
                        continue;
                    }
                    for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
                        const int distanceSquared = dx * dx + dy * dy;
                        if (distanceSquared >= bestDistanceSquared) {
                            continue;
                        }
                        const int x = seedX + dx;
                        if (x < 0 || x >= selectionWidth) {
                            continue;
                        }
                        const int candidateIndex = y * selectionWidth + x;
                        if (qualifier[static_cast<std::size_t>(candidateIndex)] >= kStrongThreshold) {
                            bestIndex = candidateIndex;
                            bestDistanceSquared = distanceSquared;
                        }
                    }
                }
                if (bestIndex < 0) {
                    continue;
                }
                startIndex = bestIndex;
            }
            if (visited[static_cast<std::size_t>(startIndex)] != 0u) {
                continue;
            }

            queue.clear();
            queue.push_back(startIndex);
            visited[static_cast<std::size_t>(startIndex)] = 1u;
            for (std::size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex) {
                const int index = queue[queueIndex];
                selectedBits[static_cast<std::size_t>(index)] |= zoneBit;
                const int x = index % selectionWidth;
                const int y = index / selectionWidth;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int neighborY = y + dy;
                    if (neighborY < 0 || neighborY >= selectionHeight) {
                        continue;
                    }
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int neighborX = x + dx;
                        if (neighborX < 0 || neighborX >= selectionWidth) {
                            continue;
                        }
                        const int neighborIndex =
                            neighborY * selectionWidth + neighborX;
                        const std::size_t neighborOffset =
                            static_cast<std::size_t>(neighborIndex);
                        if (visited[neighborOffset] != 0u ||
                            qualifier[neighborOffset] < kGrowthThreshold) {
                            continue;
                        }
                        visited[neighborOffset] = 1u;
                        queue.push_back(neighborIndex);
                    }
                }
            }
        }
    }

    const unsigned int replacement =
        CreateRawLocalRangeSelectionTexture(
            selectionWidth, selectionHeight, selectedBits);
    if (replacement == 0) {
        return 0;
    }
    const unsigned int previous =
        m_RawDevelopmentLocalRangeSelectionBitsTexture;
    m_RawDevelopmentLocalRangeSelectionBitsTexture = replacement;
    m_RawDevelopmentLocalRangeSelectionBitsInputTexture = inputTexture;
    m_RawDevelopmentLocalRangeSelectionBitsWidth = m_Width;
    m_RawDevelopmentLocalRangeSelectionBitsHeight = m_Height;
    m_RawDevelopmentLocalRangeSelectionBitsTextureWidth = selectionWidth;
    m_RawDevelopmentLocalRangeSelectionBitsTextureHeight = selectionHeight;
    m_RawDevelopmentLocalRangeSelectionBitsInputFingerprint =
        inputStageFingerprint;
    m_RawDevelopmentLocalRangeSelectionBitsFingerprint = fingerprint;
    if (previous != 0) {
        glDeleteTextures(1, &previous);
    }
    return m_RawDevelopmentLocalRangeSelectionBitsTexture;
}

unsigned int RenderPipeline::BuildRawDevelopmentLocalRangeTargetPreviewSelectionBits(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRangeInput,
    Raw::RawWorkingSpace workingSpace,
    const RawLocalRangeTargetPreviewRequest& request) {
    const Stack::RawRecipe::RawLocalRangeRecipe localRange =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRangeInput);
    if (!inputTexture ||
        !request.enabled ||
        !request.requestConnectedRefinement ||
        request.generation == 0 ||
        localRange.targetZones.empty() ||
        m_Width <= 0 ||
        m_Height <= 0) {
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = false;
        return 0;
    }

    if (m_RawDevelopmentLocalRangeTargetPreviewCpuPending &&
        !m_RawDevelopmentLocalRangeTargetPreviewCpuFuture.valid()) {
        m_RawDevelopmentLocalRangeTargetPreviewCpuPending = false;
        m_RawDevelopmentLocalRangeTargetPreviewCpuGeneration = 0;
    }
    if (m_RawDevelopmentLocalRangeTargetPreviewCpuPending &&
        m_RawDevelopmentLocalRangeTargetPreviewCpuFuture.valid() &&
        m_RawDevelopmentLocalRangeTargetPreviewCpuFuture.wait_for(
            std::chrono::milliseconds(0)) == std::future_status::ready) {
        RawLocalRangeTargetPreviewCpuResult cpuResult;
        bool cpuResultReady = false;
        try {
            cpuResult =
                m_RawDevelopmentLocalRangeTargetPreviewCpuFuture.get();
            cpuResultReady = true;
        } catch (...) {
            // A failed optional contour calculation must not leave targeting
            // permanently pending. The next request may retry from the GPU
            // qualifier without changing authored Local Range math.
        }
        m_RawDevelopmentLocalRangeTargetPreviewCpuPending = false;
        m_RawDevelopmentLocalRangeTargetPreviewCpuGeneration = 0;
        std::size_t requiredElements = 0;
        if (cpuResultReady &&
            cpuResult.generation == request.generation &&
            cpuResult.width > 0 &&
            cpuResult.height > 0 &&
            Stack::PixelBuffer::TryComputePixelElementCount(
                cpuResult.width,
                cpuResult.height,
                1,
                requiredElements) &&
            cpuResult.selectedBits.size() ==
                requiredElements) {
            using PreviewClock = std::chrono::steady_clock;
            const auto uploadStart = PreviewClock::now();
            const unsigned int replacement =
                CreateRawLocalRangeSelectionTexture(
                    cpuResult.width,
                    cpuResult.height,
                    cpuResult.selectedBits);
            if (replacement != 0) {
                const unsigned int previous =
                    m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture;
                m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture =
                    replacement;
                m_RawDevelopmentLocalRangeTargetPreviewSelectionWidth =
                    cpuResult.width;
                m_RawDevelopmentLocalRangeTargetPreviewSelectionHeight =
                    cpuResult.height;
                m_RawDevelopmentLocalRangeTargetPreviewSelectionReadyGeneration =
                    cpuResult.generation;
                m_RawDevelopmentLocalRangeTargetPreviewMetrics.floodFillMs =
                    cpuResult.floodFillMs;
                m_RawDevelopmentLocalRangeTargetPreviewMetrics.uploadMs =
                    std::chrono::duration<float, std::milli>(
                        PreviewClock::now() - uploadStart)
                        .count();
                m_RawDevelopmentLocalRangeTargetPreviewMetrics.maximumDimension =
                    std::max(cpuResult.width, cpuResult.height);
                m_RawDevelopmentLocalRangeTargetPreviewMetrics.cacheHit = false;
                if (previous != 0) {
                    glDeleteTextures(1, &previous);
                }
            }
        }
    }
    if (m_RawDevelopmentLocalRangeTargetPreviewCpuPending) {
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = true;
        return 0;
    }

    for (RawLocalRangeTargetPreviewReadbackSlot& slot :
         m_RawDevelopmentLocalRangeTargetPreviewReadbackSlots) {
        if (!slot.occupied || slot.fence == nullptr) {
            continue;
        }
        const GLenum waitResult = glClientWaitSync(slot.fence, 0, 0);
        if (waitResult == GL_WAIT_FAILED) {
            glDeleteSync(slot.fence);
            slot.fence = nullptr;
            slot.occupied = false;
            slot.generation = 0;
            slot.width = 0;
            slot.height = 0;
            continue;
        }
        if (waitResult != GL_ALREADY_SIGNALED &&
            waitResult != GL_CONDITION_SATISFIED) {
            continue;
        }

        glDeleteSync(slot.fence);
        slot.fence = nullptr;
        const bool currentGeneration = slot.generation == request.generation;
        if (currentGeneration &&
            slot.pbo != 0 &&
            slot.width > 0 &&
            slot.height > 0) {
            using PreviewClock = std::chrono::steady_clock;
            std::size_t pixelCount = 0;
            std::vector<unsigned char> qualifier;
            if (Stack::PixelBuffer::TryComputePixelElementCount(
                    slot.width, slot.height, 1, pixelCount)) {
                try {
                    qualifier.assign(pixelCount, 0u);
                } catch (const std::bad_alloc&) {
                    qualifier.clear();
                } catch (const std::length_error&) {
                    qualifier.clear();
                }
            }
            bool copyOk = !qualifier.empty();
            if (copyOk) {
                GLint previousPackBuffer = 0;
                const auto readbackCopyStart = PreviewClock::now();
                glGetIntegerv(
                    GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
                while (glGetError() != GL_NO_ERROR) {}
                glGetBufferSubData(
                    GL_PIXEL_PACK_BUFFER,
                    0,
                    static_cast<GLsizeiptr>(qualifier.size()),
                    qualifier.data());
                copyOk = glGetError() == GL_NO_ERROR;
                m_RawDevelopmentLocalRangeTargetPreviewMetrics.readbackCopyMs =
                    std::chrono::duration<float, std::milli>(
                        PreviewClock::now() - readbackCopyStart)
                        .count();
                glBindBuffer(
                    GL_PIXEL_PACK_BUFFER,
                    static_cast<unsigned int>(previousPackBuffer));
            }

            if (copyOk) {
                const int jobWidth = slot.width;
                const int jobHeight = slot.height;
                const float jobSeedU = slot.seedU;
                const float jobSeedV = slot.seedV;
                const std::uint64_t jobGeneration = slot.generation;
                try {
                    m_RawDevelopmentLocalRangeTargetPreviewCpuFuture =
                        std::async(
                            std::launch::async,
                            BuildRawLocalRangeTargetPreviewConnectedArea,
                            std::move(qualifier),
                            jobWidth,
                            jobHeight,
                            jobSeedU,
                            jobSeedV,
                            jobGeneration);
                    m_RawDevelopmentLocalRangeTargetPreviewCpuPending = true;
                    m_RawDevelopmentLocalRangeTargetPreviewCpuGeneration =
                        jobGeneration;
                } catch (...) {
                    m_RawDevelopmentLocalRangeTargetPreviewCpuPending = false;
                    m_RawDevelopmentLocalRangeTargetPreviewCpuGeneration = 0;
                }
            }
        }
        slot.occupied = false;
        slot.generation = 0;
        slot.width = 0;
        slot.height = 0;
    }

    if (m_RawDevelopmentLocalRangeTargetPreviewCpuPending) {
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = true;
        return 0;
    }

    if (m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture != 0 &&
        m_RawDevelopmentLocalRangeTargetPreviewSelectionReadyGeneration ==
            request.generation) {
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = false;
        m_RawDevelopmentLocalRangeTargetPreviewMetrics.cacheHit = true;
        return m_RawDevelopmentLocalRangeTargetPreviewSelectionTexture;
    }

    const bool matchingReadbackPending = std::any_of(
        m_RawDevelopmentLocalRangeTargetPreviewReadbackSlots.begin(),
        m_RawDevelopmentLocalRangeTargetPreviewReadbackSlots.end(),
        [&](const RawLocalRangeTargetPreviewReadbackSlot& slot) {
            return slot.occupied && slot.generation == request.generation;
        });
    if (matchingReadbackPending) {
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = true;
        return 0;
    }

    RawLocalRangeTargetPreviewReadbackSlot* targetSlot = nullptr;
    for (int offset = 0; offset < 2; ++offset) {
        const int index =
            (m_RawDevelopmentLocalRangeTargetPreviewNextReadbackSlot + offset) %
            2;
        RawLocalRangeTargetPreviewReadbackSlot& slot =
            m_RawDevelopmentLocalRangeTargetPreviewReadbackSlots[
                static_cast<std::size_t>(index)];
        if (!slot.occupied) {
            targetSlot = &slot;
            m_RawDevelopmentLocalRangeTargetPreviewNextReadbackSlot =
                (index + 1) % 2;
            break;
        }
    }
    if (targetSlot == nullptr) {
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = true;
        return 0;
    }

    const auto qualifierIssueStart = std::chrono::steady_clock::now();
    EnsureRawDevelopmentLocalRangeQualifierProgram();
    if (!m_RawDevelopmentLocalRangeQualifierProgram) {
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = false;
        return 0;
    }
    const float selectionScale = std::min(
        1.0f,
        768.0f / static_cast<float>(std::max(m_Width, m_Height)));
    const int selectionWidth = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<float>(m_Width) * selectionScale)));
    const int selectionHeight = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<float>(m_Height) * selectionScale)));
    const Stack::RawRecipe::RawLocalRangeTargetZone& zone =
        localRange.targetZones.front();
    const unsigned int qualifierTexture =
        GLHelpers::CreateEmptyTexture(selectionWidth, selectionHeight);
    const unsigned int qualifierFramebuffer =
        qualifierTexture != 0 ? GLHelpers::CreateFBO(qualifierTexture) : 0;
    if (qualifierFramebuffer == 0) {
        if (qualifierTexture != 0) {
            glDeleteTextures(1, &qualifierTexture);
        }
        m_RawDevelopmentLocalRangeTargetPreviewSelectionPending = false;
        return 0;
    }

    const Stack::Renderer::GLState::FramebufferState
        savedFramebufferState(true);
    const Stack::Renderer::GLState::PixelPackState savedPackState;
    glBindFramebuffer(GL_FRAMEBUFFER, qualifierFramebuffer);
    glViewport(0, 0, selectionWidth, selectionHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_RawDevelopmentLocalRangeQualifierProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(
        glGetUniformLocation(
            m_RawDevelopmentLocalRangeQualifierProgram,
            "uInputImage"),
        0);
    glUniform1f(
        glGetUniformLocation(
            m_RawDevelopmentLocalRangeQualifierProgram,
            "uMiddleGrey"),
        localRange.middleGrey);
    glUniform1i(
        glGetUniformLocation(
            m_RawDevelopmentLocalRangeQualifierProgram,
            "uWorkingSpace"),
        workingSpace == Raw::RawWorkingSpace::LinearRec2020D65 ? 1 : 0);
    glUniform4f(
        glGetUniformLocation(
            m_RawDevelopmentLocalRangeQualifierProgram,
            "uTone"),
        zone.centerEv,
        zone.coreHalfWidthEv,
        zone.featherEv,
        zone.deltaEv);
    glUniform4f(
        glGetUniformLocation(
            m_RawDevelopmentLocalRangeQualifierProgram,
            "uColor"),
        zone.targetUPrime,
        zone.targetVPrime,
        zone.colorRadius,
        zone.colorFeather);
    glUniform2f(
        glGetUniformLocation(
            m_RawDevelopmentLocalRangeQualifierProgram,
            "uMeta"),
        zone.targetChroma,
        zone.colorEnabled ? 1.0f : 0.0f);
    m_Quad.Draw();

    if (targetSlot->pbo == 0) {
        glGenBuffers(1, &targetSlot->pbo);
    }
    std::size_t readbackBytes = 0;
    const bool readbackSizeValid =
        Stack::PixelBuffer::TryComputePixelByteCount(
            selectionWidth, selectionHeight, 1, readbackBytes) &&
        readbackBytes <=
            static_cast<std::size_t>(
                std::numeric_limits<GLsizeiptr>::max());
    savedPackState.ConfigureTightCpuReadback();
    bool issued = targetSlot->pbo != 0 && readbackSizeValid;
    if (issued) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, targetSlot->pbo);
        while (glGetError() != GL_NO_ERROR) {}
        glBufferData(
            GL_PIXEL_PACK_BUFFER,
            static_cast<GLsizeiptr>(readbackBytes),
            nullptr,
            GL_STREAM_READ);
        issued = glGetError() == GL_NO_ERROR;
    }
    if (issued) {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(
            0,
            0,
            selectionWidth,
            selectionHeight,
            GL_RED,
            GL_UNSIGNED_BYTE,
            nullptr);
        issued = glGetError() == GL_NO_ERROR;
    }
    if (issued) {
        targetSlot->fence =
            glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        targetSlot->generation = request.generation;
        targetSlot->width = selectionWidth;
        targetSlot->height = selectionHeight;
        targetSlot->seedU = request.sourceU;
        targetSlot->seedV = request.sourceV;
        targetSlot->occupied = targetSlot->fence != nullptr;
        glFlush();
    }
    savedPackState.Restore();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    savedFramebufferState.Restore(true);
    glDeleteFramebuffers(1, &qualifierFramebuffer);
    glDeleteTextures(1, &qualifierTexture);

    m_RawDevelopmentLocalRangeTargetPreviewSelectionPending =
        targetSlot->occupied;
    m_RawDevelopmentLocalRangeTargetPreviewMetrics.qualifierIssueMs =
        std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - qualifierIssueStart)
            .count();
    m_RawDevelopmentLocalRangeTargetPreviewMetrics.maximumDimension =
        std::max(selectionWidth, selectionHeight);
    m_RawDevelopmentLocalRangeTargetPreviewMetrics.cacheHit = false;
    return 0;
}

void RenderPipeline::CaptureRawDevelopmentLocalRangeGraphScopeReadback(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    int sourceWidth,
    int sourceHeight) {
    if (m_RawDevelopmentGraphScopeStage !=
            RawDevelopmentGraphScopeStage::LocalRangeInput ||
        m_RawDevelopmentGraphScopeReadbackMaxDimension <= 0 ||
        inputTexture == 0 || sourceWidth <= 0 || sourceHeight <= 0) {
        return;
    }

    EnsureRawDevelopmentLocalRangeProgram();
    if (m_RawDevelopmentLocalRangeProgram == 0) {
        return;
    }

    const Stack::RawRecipe::RawLocalRangeRecipe sanitized =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    const float scale = std::min(
        1.0f,
        static_cast<float>(m_RawDevelopmentGraphScopeReadbackMaxDimension) /
            static_cast<float>(std::max(sourceWidth, sourceHeight)));
    const int scopeWidth = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(sourceWidth) * scale)));
    const int scopeHeight = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(sourceHeight) * scale)));
    const unsigned int scopeTexture =
        GLHelpers::CreateEmptyTexture(scopeWidth, scopeHeight);
    if (scopeTexture == 0) {
        return;
    }

    // Render only the bounded sample grid, but keep neighbor offsets in the
    // full-resolution input domain. The same shader function therefore
    // computes the same edge-aware EV signal that drives Overall Tones at
    // each sampled image coordinate without another full-frame pass.
    const int savedWidth = m_Width;
    const int savedHeight = m_Height;
    m_Width = scopeWidth;
    m_Height = scopeHeight;
    const bool rendered = RenderIntoGraphTargetTexture(
        scopeTexture,
        [&](unsigned int) {
            glUseProgram(m_RawDevelopmentLocalRangeProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(
                    m_RawDevelopmentLocalRangeProgram,
                    "uInputImage"),
                0);
            glUniform1f(
                glGetUniformLocation(
                    m_RawDevelopmentLocalRangeProgram,
                    "uMiddleGrey"),
                sanitized.middleGrey);
            glUniform1f(
                glGetUniformLocation(
                    m_RawDevelopmentLocalRangeProgram,
                    "uSmoothness"),
                sanitized.smoothness);
            glUniform1f(
                glGetUniformLocation(
                    m_RawDevelopmentLocalRangeProgram,
                    "uEdgeProtection"),
                sanitized.edgeProtection);
            glUniform1f(
                glGetUniformLocation(
                    m_RawDevelopmentLocalRangeProgram,
                    "uDetailProtection"),
                sanitized.detailProtection);
            glUniform2f(
                glGetUniformLocation(
                    m_RawDevelopmentLocalRangeProgram,
                    "uTexelSize"),
                1.0f / static_cast<float>(sourceWidth),
                1.0f / static_cast<float>(sourceHeight));
            glUniform1i(
                glGetUniformLocation(
                    m_RawDevelopmentLocalRangeProgram,
                    "uScopeOutput"),
                1);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    m_Width = savedWidth;
    m_Height = savedHeight;

    if (rendered) {
        CaptureRawDevelopmentGraphScopeReadback(
            RawDevelopmentGraphScopeStage::LocalRangeInput,
            scopeTexture,
            scopeWidth,
            scopeHeight,
            "scene-linear-pre-local-range-rgb",
            true,
            "edge-aware-scene-ev");
        m_RawDevelopmentGraphScopeReadback.sourceWidth = sourceWidth;
        m_RawDevelopmentGraphScopeReadback.sourceHeight = sourceHeight;
    }
    glDeleteTextures(1, &scopeTexture);
}

unsigned int RenderPipeline::RenderRawDevelopmentLocalRange(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    Raw::RawWorkingSpace workingSpace,
    std::size_t inputStageFingerprint) {
    const Stack::RawRecipe::RawLocalRangeRecipe sanitized =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    if (!inputTexture || !Stack::RawRecipe::IsLocalRangeEnabled(sanitized) || sanitized.points.size() < 2) {
        return 0;
    }

    EnsureRawDevelopmentLocalRangeProgram();
    if (!m_RawDevelopmentLocalRangeProgram) {
        return 0;
    }
    const unsigned int selectionBitsTexture =
        BuildRawDevelopmentLocalRangeSelectionBits(
            inputTexture,
            sanitized,
            workingSpace,
            inputStageFingerprint);

    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    if (!outputTexture) {
        return 0;
    }

    const bool rendered = RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDevelopmentLocalRangeProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, selectionBitsTexture);
        glUniform1i(
            glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uTargetZoneSelectionBits"),
            1);
        glUniform1i(
            glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uHasTargetZoneSelectionBits"),
            selectionBitsTexture != 0 ? 1 : 0);
        glUniform1i(
            glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uScopeOutput"),
            0);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uStrength"), sanitized.strength);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uMiddleGrey"), sanitized.middleGrey);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uSmoothness"), sanitized.smoothness);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uEdgeProtection"), sanitized.edgeProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uDetailProtection"), sanitized.detailProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uHighlightProtection"), sanitized.highlightProtection);
        glUniform2f(
            glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uTexelSize"),
            m_Width > 0 ? 1.0f / static_cast<float>(m_Width) : 0.0f,
            m_Height > 0 ? 1.0f / static_cast<float>(m_Height) : 0.0f);
        UploadRawLocalRangeRegionMaskUniforms(
            m_RawDevelopmentLocalRangeProgram,
            sanitized,
            m_Width,
            m_Height);
        UploadRawLocalRangeTargetZoneUniforms(
            m_RawDevelopmentLocalRangeProgram,
            sanitized,
            workingSpace);

        const int count = std::min<int>(static_cast<int>(sanitized.points.size()), 12);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uLocalRangePointCount"), count);
        for (int i = 0; i < count; ++i) {
            char uniformName[64];
            std::snprintf(uniformName, sizeof(uniformName), "uLocalRangePoints[%d]", i);
            const Stack::RawRecipe::RawLocalRangePoint& point = sanitized.points[static_cast<std::size_t>(i)];
            glUniform2f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, uniformName),
                point.ev,
                point.deltaEv);
        }

        m_Quad.Draw();
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    });

    glActiveTexture(GL_TEXTURE0);
    if (!rendered) {
        glDeleteTextures(1, &outputTexture);
        return 0;
    }
    return outputTexture;
}

unsigned int RenderPipeline::RenderRawDevelopmentLocalRangeOverlay(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    Raw::RawWorkingSpace workingSpace,
    const std::string& overlayMode,
    std::size_t inputStageFingerprint) {
    const int mode = overlayMode == "affected-tones"
        ? 1
        : (overlayMode == "delta-map"
                ? 2
                : (overlayMode == "region-mask"
                        ? 3
                        : (overlayMode == "target-outline" ? 4 : 0)));
    const Stack::RawRecipe::RawLocalRangeRecipe sanitized =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    Stack::RawRecipe::RawLocalRangeRecipe overlayRange = sanitized;
    const bool targetOutlineActive =
        mode == 4 && m_RawDevelopmentLocalRangeTargetPreviewRequest.enabled;
    if (targetOutlineActive &&
        m_RawDevelopmentLocalRangeTargetPreviewRequest.provisional) {
        // Pointer motion is represented by the cursor/readout. Rendering the
        // raw qualifier at every move produces a noisy field of disconnected
        // pixels that reads like a flashing mask, not a useful outline.
        return 0;
    }
    const bool asynchronousTargetOutline =
        targetOutlineActive &&
        !m_RawDevelopmentLocalRangeTargetPreviewRequest.provisional &&
        m_RawDevelopmentLocalRangeTargetPreviewRequest
            .requestConnectedRefinement;
    const bool detachSelectionCache = targetOutlineActive;
    if (targetOutlineActive) {
        overlayRange.regionMaskEnabled = false;
        overlayRange.colorMaskEnabled = false;
        overlayRange.targetZones.clear();
        const int existingZoneIndex =
            m_RawDevelopmentLocalRangeTargetPreviewRequest.existingZoneIndex;
        if (existingZoneIndex >= 0 &&
            existingZoneIndex < static_cast<int>(sanitized.targetZones.size())) {
            overlayRange.targetZones.push_back(
                sanitized.targetZones[static_cast<std::size_t>(existingZoneIndex)]);
        } else {
            Stack::RawRecipe::RawLocalRangeTargetZone prospectiveZone =
                m_RawDevelopmentLocalRangeTargetPreviewRequest.prospectiveZone;
            prospectiveZone.id = "__target-outline-preview__";
            if (!m_RawDevelopmentLocalRangeTargetPreviewRequest
                     .requestConnectedRefinement) {
                prospectiveZone.scope =
                    Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches;
                prospectiveZone.seeds.clear();
            }
            overlayRange.targetZones.push_back(std::move(prospectiveZone));
        }
        overlayRange = Stack::RawRecipe::SanitizeLocalRangeRecipe(
            std::move(overlayRange));
    }
    const bool localRangeActive =
        Stack::RawRecipe::IsLocalRangeEnabled(sanitized) && sanitized.points.size() >= 2;
    const bool maskOverlayActive = mode == 3 &&
        (sanitized.regionMaskEnabled ||
            sanitized.colorMaskEnabled ||
            !sanitized.targetZones.empty());
    if (!inputTexture ||
        mode == 0 ||
        (mode == 4 && !targetOutlineActive) ||
        (!localRangeActive && !maskOverlayActive && !targetOutlineActive)) {
        return 0;
    }

    EnsureRawDevelopmentLocalRangeOverlayProgram();
    if (!m_RawDevelopmentLocalRangeOverlayProgram) {
        return 0;
    }
    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    if (!outputTexture) {
        return 0;
    }

    const unsigned int savedSelectionBitsTexture =
        m_RawDevelopmentLocalRangeSelectionBitsTexture;
    const unsigned int savedSelectionBitsInputTexture =
        m_RawDevelopmentLocalRangeSelectionBitsInputTexture;
    const int savedSelectionBitsWidth =
        m_RawDevelopmentLocalRangeSelectionBitsWidth;
    const int savedSelectionBitsHeight =
        m_RawDevelopmentLocalRangeSelectionBitsHeight;
    const int savedSelectionBitsTextureWidth =
        m_RawDevelopmentLocalRangeSelectionBitsTextureWidth;
    const int savedSelectionBitsTextureHeight =
        m_RawDevelopmentLocalRangeSelectionBitsTextureHeight;
    const std::size_t savedSelectionBitsInputFingerprint =
        m_RawDevelopmentLocalRangeSelectionBitsInputFingerprint;
    const std::size_t savedSelectionBitsFingerprint =
        m_RawDevelopmentLocalRangeSelectionBitsFingerprint;
    if (targetOutlineActive) {
        m_RawDevelopmentLocalRangeSelectionBitsTexture = 0;
        m_RawDevelopmentLocalRangeSelectionBitsInputTexture = 0;
        m_RawDevelopmentLocalRangeSelectionBitsWidth = 0;
        m_RawDevelopmentLocalRangeSelectionBitsHeight = 0;
        m_RawDevelopmentLocalRangeSelectionBitsTextureWidth = 0;
        m_RawDevelopmentLocalRangeSelectionBitsTextureHeight = 0;
        m_RawDevelopmentLocalRangeSelectionBitsInputFingerprint = 0;
        m_RawDevelopmentLocalRangeSelectionBitsFingerprint = 0;
    }
    unsigned int selectionBitsTexture = asynchronousTargetOutline
        ? BuildRawDevelopmentLocalRangeTargetPreviewSelectionBits(
              inputTexture,
              overlayRange,
              workingSpace,
              m_RawDevelopmentLocalRangeTargetPreviewRequest)
        : BuildRawDevelopmentLocalRangeSelectionBits(
              inputTexture,
              overlayRange,
              workingSpace,
              inputStageFingerprint,
              targetOutlineActive ? 768 : 1536);
    const bool awaitingTargetRefinement =
        asynchronousTargetOutline && selectionBitsTexture == 0;
    const int targetSelectionWidth = asynchronousTargetOutline
        ? m_RawDevelopmentLocalRangeTargetPreviewSelectionWidth
        : m_RawDevelopmentLocalRangeSelectionBitsTextureWidth;
    const int targetSelectionHeight = asynchronousTargetOutline
        ? m_RawDevelopmentLocalRangeTargetPreviewSelectionHeight
        : m_RawDevelopmentLocalRangeSelectionBitsTextureHeight;
    auto restoreSelectionCache = [&]() {
        if (!detachSelectionCache) {
            return;
        }
        if (m_RawDevelopmentLocalRangeSelectionBitsTexture != 0) {
            glDeleteTextures(
                1,
                &m_RawDevelopmentLocalRangeSelectionBitsTexture);
        }
        m_RawDevelopmentLocalRangeSelectionBitsTexture =
            savedSelectionBitsTexture;
        m_RawDevelopmentLocalRangeSelectionBitsInputTexture =
            savedSelectionBitsInputTexture;
        m_RawDevelopmentLocalRangeSelectionBitsWidth =
            savedSelectionBitsWidth;
        m_RawDevelopmentLocalRangeSelectionBitsHeight =
            savedSelectionBitsHeight;
        m_RawDevelopmentLocalRangeSelectionBitsTextureWidth =
            savedSelectionBitsTextureWidth;
        m_RawDevelopmentLocalRangeSelectionBitsTextureHeight =
            savedSelectionBitsTextureHeight;
        m_RawDevelopmentLocalRangeSelectionBitsInputFingerprint =
            savedSelectionBitsInputFingerprint;
        m_RawDevelopmentLocalRangeSelectionBitsFingerprint =
            savedSelectionBitsFingerprint;
    };
    if (awaitingTargetRefinement) {
        restoreSelectionCache();
        glDeleteTextures(1, &outputTexture);
        return 0;
    }

    const bool rendered = RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDevelopmentLocalRangeOverlayProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, selectionBitsTexture);
        glUniform1i(
            glGetUniformLocation(
                m_RawDevelopmentLocalRangeOverlayProgram,
                "uTargetZoneSelectionBits"),
            1);
        glUniform1i(
            glGetUniformLocation(
                m_RawDevelopmentLocalRangeOverlayProgram,
                "uHasTargetZoneSelectionBits"),
            selectionBitsTexture != 0 ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uStrength"), overlayRange.strength);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uMiddleGrey"), overlayRange.middleGrey);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uOverlayMode"), mode);
        glUniform1i(
            glGetUniformLocation(
                m_RawDevelopmentLocalRangeOverlayProgram,
                "uTargetOutlineProvisional"),
            (m_RawDevelopmentLocalRangeTargetPreviewRequest.provisional ||
                awaitingTargetRefinement)
                ? 1
                : 0);
        glUniform2f(
            glGetUniformLocation(
                m_RawDevelopmentLocalRangeOverlayProgram,
                "uTargetZoneSelectionTexelSize"),
            targetSelectionWidth > 0
                ? 1.0f / static_cast<float>(targetSelectionWidth)
                : 0.0f,
            targetSelectionHeight > 0
                ? 1.0f / static_cast<float>(targetSelectionHeight)
                : 0.0f);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uSmoothness"), overlayRange.smoothness);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uEdgeProtection"), overlayRange.edgeProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uDetailProtection"), overlayRange.detailProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uHighlightProtection"), overlayRange.highlightProtection);
        glUniform2f(
            glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uTexelSize"),
            m_Width > 0 ? 1.0f / static_cast<float>(m_Width) : 0.0f,
            m_Height > 0 ? 1.0f / static_cast<float>(m_Height) : 0.0f);
        UploadRawLocalRangeRegionMaskUniforms(
            m_RawDevelopmentLocalRangeOverlayProgram,
            overlayRange,
            m_Width,
            m_Height);
        UploadRawLocalRangeTargetZoneUniforms(
            m_RawDevelopmentLocalRangeOverlayProgram,
            overlayRange,
            workingSpace);

        const int count = std::min<int>(static_cast<int>(overlayRange.points.size()), 12);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uLocalRangePointCount"), count);
        for (int i = 0; i < count; ++i) {
            char uniformName[64];
            std::snprintf(uniformName, sizeof(uniformName), "uLocalRangePoints[%d]", i);
            const Stack::RawRecipe::RawLocalRangePoint& point = overlayRange.points[static_cast<std::size_t>(i)];
            glUniform2f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, uniformName),
                point.ev,
                point.deltaEv);
        }

        m_Quad.Draw();
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    });

    glActiveTexture(GL_TEXTURE0);
    restoreSelectionCache();
    if (!rendered) {
        glDeleteTextures(1, &outputTexture);
        return 0;
    }
    return outputTexture;
}
