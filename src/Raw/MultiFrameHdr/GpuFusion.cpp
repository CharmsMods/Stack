#include "Raw/MultiFrameHdr/GpuFusion.h"

#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_R8UI
#define GL_R8UI 0x8232
#endif
#ifndef GL_RGBA8UI
#define GL_RGBA8UI 0x8D7C
#endif
#ifndef GL_RGBA_INTEGER
#define GL_RGBA_INTEGER 0x8D99
#endif
#ifndef GL_MAX_ARRAY_TEXTURE_LAYERS
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#endif
#ifndef GL_MAX_IMAGE_UNITS
#define GL_MAX_IMAGE_UNITS 0x8F38
#endif
#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_TEXTURE_UPDATE_BARRIER_BIT
#define GL_TEXTURE_UPDATE_BARRIER_BIT 0x00000100
#endif

namespace Raw::Hdr {
namespace {

constexpr std::uint32_t kMaximumFrames = 20u;
constexpr std::uint32_t kWorkgroupSize = 8u;
constexpr std::uint32_t kMaximumDispatchTile = 512u;
constexpr std::uint32_t kContributionScale = 4096u;
constexpr std::uint32_t kEffectiveSampleScale = 128u;
constexpr std::size_t kCounterCount = 8u;
constexpr std::size_t kContributionSlotsPerFrame = 4u;

struct alignas(16) GpuFrameParameters {
    std::array<float, 4> exposureTranslation {};
    std::array<float, 4> confidenceVariance {};
    std::array<float, 4> shot {};
    std::array<float, 4> offset {};
};

struct GlResources {
    GLuint program = 0;
    std::array<GLuint, 5> inputTextures {};
    std::array<GLuint, 3> outputTextures {};
    GLuint frameParameters = 0;
    GLuint diagnostics = 0;

    ~GlResources() {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
        for (GLuint unit = 0; unit < 8; ++unit)
            glBindImageTexture(unit, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        if (diagnostics != 0) glDeleteBuffers(1, &diagnostics);
        if (frameParameters != 0) glDeleteBuffers(1, &frameParameters);
        glDeleteTextures(
            static_cast<GLsizei>(outputTextures.size()), outputTextures.data());
        glDeleteTextures(
            static_cast<GLsizei>(inputTextures.size()), inputTextures.data());
        if (program != 0) glDeleteProgram(program);
    }
};

bool Canceled(const GpuFusionRequest& request) {
    return request.shouldCancel && request.shouldCancel();
}

void ClearGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

bool CheckGl(const char* operation, std::string& error) {
    const GLenum code = glGetError();
    if (code == GL_NO_ERROR) return true;
    error = std::string(operation) + " failed with OpenGL error " +
        std::to_string(static_cast<unsigned int>(code)) + ".";
    while (glGetError() != GL_NO_ERROR) {
    }
    return false;
}

GLuint CreateTextureStorage(
    GLenum target,
    GLenum internalFormat,
    GLsizei width,
    GLsizei height,
    GLsizei layers,
    std::string& error) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        error = "OpenGL could not allocate an HDR texture name.";
        return 0;
    }
    glBindTexture(target, texture);
    ClearGlErrors();
    if (target == GL_TEXTURE_2D_ARRAY)
        glTexStorage3D(target, 1, internalFormat, width, height, layers);
    else
        glTexStorage2D(target, 1, internalFormat, width, height);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!CheckGl("HDR texture allocation", error)) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

bool UploadLayer(
    GLuint texture,
    GLint layer,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* values,
    std::string& error) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ClearGlErrors();
    glTexSubImage3D(
        GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
        width, height, 1, format, type, values);
    return CheckGl("HDR texture upload", error);
}

constexpr const char* kFusionShader = R"GLSL(
#version 430 core

#define MAX_FRAMES 20
#define FLAG_REFERENCE_FALLBACK 1u
#define FLAG_LOW_CONFIDENCE_OWNER 2u
#define FLAG_RECOVERED_HIGHLIGHT 4u
#define FLAG_SINGLE_EXPOSURE 8u
#define FLAG_HIGHLIGHT_SAFE_HANDOFF 16u

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(r32f, binding = 0) readonly uniform image2DArray uNormalized;
layout(r32f, binding = 1) readonly uniform image2DArray uGain;
layout(r8ui, binding = 2) readonly uniform uimage2DArray uFlags;
layout(r8ui, binding = 3) readonly uniform uimage2DArray uClipped;
layout(r32f, binding = 4) readonly uniform image2DArray uExplicitVariance;
layout(rgba32f, binding = 5) writeonly uniform image2D uOutput;
layout(r32f, binding = 6) writeonly uniform image2D uHeadroom;
layout(rgba8ui, binding = 7) writeonly uniform uimage2D uMeta;

struct FrameParameters {
    vec4 exposureTranslation; // exposure, uncertainty EV, translation x/y
    vec4 confidenceVariance;  // inflation, low confidence, explicit variance, unused
    vec4 shot;
    vec4 offset;
};
layout(std430, binding = 0) readonly buffer FrameParameterBuffer {
    FrameParameters frameParameters[];
};
layout(std430, binding = 1) buffer DiagnosticBuffer {
    uint diagnostics[];
};

uniform ivec2 uExtent;
uniform ivec2 uTileOrigin;
uniform ivec2 uTileExtent;
uniform int uFrameCount;
uniform int uReferenceFrame;
uniform int uCfaPattern;
uniform float uNumericalFloor;
uniform float uUnverifiedCap;
uniform float uSaturationSigma;
uniform float uRobustCutoff;
uniform float uDisagreementSigma;
uniform float uHighlightHoleCeiling;
uniform int uHighlightHoleNeighbors;

struct Sample {
    float comparison;
    float normalized;
    float gain;
    float shot;
    float offset;
    float explicitVariance;
    bool hasExplicitVariance;
    bool valid;
};

bool finiteValue(float value) { return !isnan(value) && !isinf(value); }

int siteAtParity(int parityIndex) {
    if (uCfaPattern == 1) {
        const int sites[4] = int[4](0, 1, 2, 3);
        return sites[parityIndex];
    }
    if (uCfaPattern == 2) {
        const int sites[4] = int[4](3, 1, 2, 0);
        return sites[parityIndex];
    }
    if (uCfaPattern == 3) {
        const int sites[4] = int[4](1, 3, 0, 2);
        return sites[parityIndex];
    }
    const int sites[4] = int[4](1, 0, 3, 2);
    return sites[parityIndex];
}

int siteAt(ivec2 raw) {
    return siteAtParity((raw.y & 1) * 2 + (raw.x & 1));
}

ivec2 offsetForSite(int site) {
    for (int parity = 0; parity < 4; ++parity) {
        if (siteAtParity(parity) == site)
            return ivec2(parity & 1, parity >> 1);
    }
    return ivec2(0);
}

ivec2 planeExtent(ivec2 offset) {
    return ivec2(
        uExtent.x <= offset.x ? 0 : ((uExtent.x - 1 - offset.x) / 2 + 1),
        uExtent.y <= offset.y ? 0 : ((uExtent.y - 1 - offset.y) / 2 + 1));
}

bool rejected(uint flags) { return (flags & 15u) != 0u; }

bool exactSample(int frame, int site, ivec2 raw, out Sample measurement) {
    measurement.valid = false;
    uint flags = imageLoad(uFlags, ivec3(raw, frame)).x;
    if (rejected(flags)) return false;
    float normalized = imageLoad(uNormalized, ivec3(raw, frame)).x;
    float gain = imageLoad(uGain, ivec3(raw, frame)).x;
    float comparison = normalized * gain;
    if (!finiteValue(normalized) || !finiteValue(gain) || gain <= 0.0 ||
        !finiteValue(comparison)) return false;
    FrameParameters fp = frameParameters[frame];
    measurement.comparison = comparison;
    measurement.normalized = normalized;
    measurement.gain = gain;
    measurement.shot = gain * fp.shot[site];
    measurement.offset = gain * gain * fp.offset[site];
    measurement.hasExplicitVariance = fp.confidenceVariance.z > 0.5;
    measurement.explicitVariance = -1.0;
    if (measurement.hasExplicitVariance) {
        float variance = imageLoad(uExplicitVariance, ivec3(raw, frame)).x;
        if (!finiteValue(variance) || variance < 0.0) return false;
        measurement.explicitVariance = gain * gain * variance;
    }
    measurement.valid = finiteValue(measurement.shot) && finiteValue(measurement.offset);
    return measurement.valid;
}

bool exactPolicyFallbackSample(
    int frame,
    int site,
    ivec2 raw,
    out Sample measurement) {
    measurement.valid = false;
    float normalized = imageLoad(uNormalized, ivec3(raw, frame)).x;
    float gain = imageLoad(uGain, ivec3(raw, frame)).x;
    float comparison = normalized * gain;
    if (!finiteValue(normalized) || !finiteValue(gain) || gain <= 0.0 ||
        !finiteValue(comparison)) return false;
    FrameParameters fp = frameParameters[frame];
    measurement.comparison = comparison;
    measurement.normalized = normalized;
    measurement.gain = gain;
    measurement.shot = gain * fp.shot[site];
    measurement.offset = gain * gain * fp.offset[site];
    measurement.hasExplicitVariance = false;
    measurement.explicitVariance = -1.0;
    if (fp.confidenceVariance.z > 0.5) {
        float variance = imageLoad(uExplicitVariance, ivec3(raw, frame)).x;
        if (finiteValue(variance) && variance >= 0.0) {
            measurement.hasExplicitVariance = true;
            measurement.explicitVariance = gain * gain * variance;
        }
    }
    measurement.valid = finiteValue(measurement.shot) &&
        finiteValue(measurement.offset);
    return measurement.valid;
}

bool sameCfaSample(int frame, int site, vec2 raw, out Sample measurement) {
    measurement.valid = false;
    ivec2 offset = offsetForSite(site);
    vec2 plane = (raw - vec2(offset)) * 0.5;
    ivec2 p0 = ivec2(floor(plane));
    ivec2 pe = planeExtent(offset);
    if (p0.x < 0 || p0.y < 0 || p0.x >= pe.x || p0.y >= pe.y)
        return false;
    vec2 f = plane - vec2(p0);
    float weights[4] = float[4](
        (1.0 - f.x) * (1.0 - f.y),
        f.x * (1.0 - f.y),
        (1.0 - f.x) * f.y,
        f.x * f.y);
    ivec2 taps[4] = ivec2[4](
        p0, p0 + ivec2(1, 0), p0 + ivec2(0, 1), p0 + ivec2(1, 1));
    FrameParameters fp = frameParameters[frame];
    measurement.comparison = 0.0;
    measurement.normalized = 0.0;
    measurement.gain = 0.0;
    measurement.shot = 0.0;
    measurement.offset = 0.0;
    measurement.hasExplicitVariance = fp.confidenceVariance.z > 0.5;
    measurement.explicitVariance = measurement.hasExplicitVariance ? 0.0 : -1.0;
    for (int tap = 0; tap < 4; ++tap) {
        if (weights[tap] <= 0.0) continue;
        if (taps[tap].x >= pe.x || taps[tap].y >= pe.y) return false;
        ivec2 coordinate = taps[tap] * 2 + offset;
        uint flags = imageLoad(uFlags, ivec3(coordinate, frame)).x;
        if (rejected(flags)) return false;
        float normalized = imageLoad(uNormalized, ivec3(coordinate, frame)).x;
        float gain = imageLoad(uGain, ivec3(coordinate, frame)).x;
        float comparison = normalized * gain;
        if (!finiteValue(normalized) || !finiteValue(gain) || gain <= 0.0 ||
            !finiteValue(comparison)) return false;
        float w = weights[tap];
        float w2 = w * w;
        measurement.comparison += w * comparison;
        measurement.normalized += w * normalized;
        measurement.gain += w * gain;
        measurement.shot += w2 * gain * fp.shot[site];
        measurement.offset += w2 * gain * gain * fp.offset[site];
        if (measurement.hasExplicitVariance) {
            float variance = imageLoad(
                uExplicitVariance, ivec3(coordinate, frame)).x;
            if (!finiteValue(variance) || variance < 0.0) return false;
            measurement.explicitVariance += w2 * gain * gain * variance;
        }
    }
    measurement.valid = finiteValue(measurement.comparison) && finiteValue(measurement.shot) &&
        finiteValue(measurement.offset) && measurement.gain > 0.0;
    return measurement.valid;
}

uint clippedNeighborCount(int frame, int site, vec2 raw) {
    ivec2 offset = offsetForSite(site);
    vec2 plane = (raw - vec2(offset)) * 0.5;
    ivec2 center = ivec2(floor(plane + vec2(0.5)));
    ivec2 pe = planeExtent(offset);
    if (center.x < 0 || center.y < 0 || center.x >= pe.x || center.y >= pe.y)
        return 0u;
    return imageLoad(uClipped, ivec3(center * 2 + offset, frame)).x;
}

float smoothStepExact(float edge0, float edge1, float value) {
    if (!(edge1 > edge0)) return value >= edge1 ? 1.0 : 0.0;
    float t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float sampleVariance(int frame, Sample measurement, float predictedComparison) {
    FrameParameters fp = frameParameters[frame];
    float modeled = fp.confidenceVariance.x *
        (measurement.hasExplicitVariance
            ? measurement.explicitVariance
            : measurement.shot * max(0.0, predictedComparison) + measurement.offset);
    if (!finiteValue(modeled)) modeled = max(1.0e-6, uNumericalFloor);
    return max(uNumericalFloor, modeled);
}

void insertionSort(inout float values[MAX_FRAMES], int count) {
    for (int i = 1; i < count; ++i) {
        float value = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > value) {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = value;
    }
}

void insertionSortPairs(
    inout float values[MAX_FRAMES],
    inout float weights[MAX_FRAMES],
    int count) {
    for (int i = 1; i < count; ++i) {
        float value = values[i];
        float weight = weights[i];
        int j = i - 1;
        while (j >= 0 && values[j] > value) {
            values[j + 1] = values[j];
            weights[j + 1] = weights[j];
            --j;
        }
        values[j + 1] = value;
        weights[j + 1] = weight;
    }
}

void main() {
    ivec2 localPixel = ivec2(gl_GlobalInvocationID.xy);
    if (localPixel.x >= uTileExtent.x || localPixel.y >= uTileExtent.y) return;
    ivec2 pixel = uTileOrigin + localPixel;
    if (pixel.x >= uExtent.x || pixel.y >= uExtent.y) return;

    const float quietNaN = uintBitsToFloat(0x7fc00000u);
    const float positiveInfinity = uintBitsToFloat(0x7f800000u);
    imageStore(uOutput, pixel, vec4(quietNaN, positiveInfinity, 0.0, 0.0));
    imageStore(uHeadroom, pixel, vec4(0.0));
    imageStore(uMeta, pixel, uvec4(0u));

    int site = siteAt(pixel);
    float referenceGain = max(
        1.0e-8, imageLoad(uGain, ivec3(pixel, uReferenceFrame)).x);

    int frameIndex[MAX_FRAMES];
    float scene[MAX_FRAMES];
    float variance[MAX_FRAMES];
    float exposure[MAX_FRAMES];
    float normalized[MAX_FRAMES];
    float gain[MAX_FRAMES];
    float baseWeight[MAX_FRAMES];
    float weight[MAX_FRAMES];
    float fusionWeight[MAX_FRAMES];
    bool lowConfidence[MAX_FRAMES];
    bool highlightHole[MAX_FRAMES];
    Sample samples[MAX_FRAMES];
    int candidateCount = 0;
    bool referenceAvailable = false;
    float referenceScene = 0.0;
    float referenceVariance = uNumericalFloor;

    for (int frame = 0; frame < uFrameCount; ++frame) {
        Sample measurement;
        vec2 translated = vec2(pixel) + frameParameters[frame].exposureTranslation.zw;
        bool valid = frame == uReferenceFrame
            ? exactSample(frame, site, pixel, measurement)
            : sameCfaSample(frame, site, translated, measurement);
        if (!valid || !measurement.valid) continue;
        float frameExposure = frameParameters[frame].exposureTranslation.x;
        float candidateScene = measurement.comparison / frameExposure;
        if (!finiteValue(candidateScene)) continue;
        int c = candidateCount++;
        frameIndex[c] = frame;
        samples[c] = measurement;
        scene[c] = candidateScene;
        exposure[c] = frameExposure;
        normalized[c] = measurement.normalized;
        gain[c] = max(1.0e-8, measurement.gain);
        baseWeight[c] = 0.0;
        weight[c] = 0.0;
        fusionWeight[c] = 0.0;
        lowConfidence[c] = frameParameters[frame].confidenceVariance.y > 0.5;
        highlightHole[c] =
            measurement.normalized < uHighlightHoleCeiling &&
            clippedNeighborCount(frame, site, translated) >=
                uint(uHighlightHoleNeighbors);
        if (frame == uReferenceFrame) {
            referenceAvailable = true;
            referenceScene = candidateScene;
        }
    }
    if (candidateCount == 0) {
        Sample fallbackSample;
        float fallbackExposure =
            frameParameters[uReferenceFrame].exposureTranslation.x;
        if (!exactPolicyFallbackSample(
                uReferenceFrame, site, pixel, fallbackSample) ||
            !finiteValue(fallbackExposure) || fallbackExposure <= 0.0) return;
        float fallbackScene = fallbackSample.comparison / fallbackExposure;
        float outputValue = fallbackSample.normalized / fallbackExposure;
        float outputVariance = sampleVariance(
            uReferenceFrame,
            fallbackSample,
            fallbackSample.comparison) /
            (fallbackExposure * fallbackExposure *
             fallbackSample.gain * fallbackSample.gain);
        float headroom = max(0.0, log2(max(1.0, fallbackScene)));
        int range = fallbackScene < 0.10 ? 0 :
            (fallbackScene < 0.75 ? 1 : 2);
        uint resultFlags = FLAG_REFERENCE_FALLBACK | FLAG_SINGLE_EXPOSURE;
        if (frameParameters[uReferenceFrame].confidenceVariance.y > 0.5)
            resultFlags |= FLAG_LOW_CONFIDENCE_OWNER;
        if (headroom > 0.001) resultFlags |= FLAG_RECOVERED_HIGHLIGHT;
        imageStore(uOutput, pixel,
            vec4(outputValue, outputVariance, 0.0, 1.0));
        imageStore(uHeadroom, pixel, vec4(headroom));
        imageStore(uMeta, pixel,
            uvec4(1u, uint(uReferenceFrame), resultFlags, 0u));
        atomicAdd(diagnostics[0], 1u);
        atomicAdd(diagnostics[1], 1u);
        if (headroom > 0.001) atomicAdd(diagnostics[3], 1u);
        atomicAdd(diagnostics[4 + range], 1u);
        atomicAdd(diagnostics[7], 128u);
        atomicAdd(diagnostics[8 + uReferenceFrame * 4], 4096u);
        atomicAdd(diagnostics[8 + uReferenceFrame * 4 + 1 + range], 4096u);
        return;
    }

    bool haveHole = false;
    bool haveNonHole = false;
    for (int i = 0; i < candidateCount; ++i) {
        haveHole = haveHole || highlightHole[i];
        haveNonHole = haveNonHole || !highlightHole[i];
    }
    bool highlightSafeHandoff = haveHole && haveNonHole;

    float initialScenes[MAX_FRAMES];
    int eligibleCount = 0;
    int referenceCandidate = -1;
    for (int i = 0; i < candidateCount; ++i) {
        bool eligible = !highlightSafeHandoff || !highlightHole[i];
        if (!eligible) continue;
        initialScenes[eligibleCount++] = scene[i];
        if (frameIndex[i] == uReferenceFrame) referenceCandidate = i;
    }
    insertionSort(initialScenes, eligibleCount);
    float estimate = (eligibleCount & 1) != 0
        ? initialScenes[eligibleCount / 2]
        : 0.5 * (initialScenes[eligibleCount / 2 - 1] +
                 initialScenes[eligibleCount / 2]);
    if (!finiteValue(estimate)) estimate = scene[0];

    float roughScenes[MAX_FRAMES];
    float roughWeights[MAX_FRAMES];
    int roughCount = 0;
    float roughSum = 0.0;
    for (int i = 0; i < candidateCount; ++i) {
        bool eligible = !highlightSafeHandoff || !highlightHole[i];
        if (!eligible) continue;
        float scale = max(1.0e-12, exposure[i]);
        float sensorVariance = sampleVariance(
            frameIndex[i], samples[i], estimate * exposure[i]) / (scale * scale);
        float sceneFloor = uNumericalFloor / (scale * scale);
        float exposureSigma = log(2.0) *
            frameParameters[frameIndex[i]].exposureTranslation.y;
        float scaleVariance = min(
            sensorVariance, estimate * estimate * exposureSigma * exposureSigma);
        variance[i] = max(sceneFloor, sensorVariance + scaleVariance);
        baseWeight[i] = 1.0 / variance[i];
        if (lowConfidence[i]) baseWeight[i] *= 0.35;
        roughScenes[roughCount] = scene[i];
        roughWeights[roughCount] = baseWeight[i];
        roughSum += baseWeight[i];
        ++roughCount;
    }
    insertionSortPairs(roughScenes, roughWeights, roughCount);
    float target = 0.5 * max(1.0e-30, roughSum);
    float cumulative = 0.0;
    estimate = roughScenes[roughCount - 1];
    for (int i = 0; i < roughCount; ++i) {
        cumulative += roughWeights[i];
        if (cumulative >= target) {
            estimate = roughScenes[i];
            break;
        }
    }

    roughSum = 0.0;
    for (int i = 0; i < candidateCount; ++i) {
        bool eligible = !highlightSafeHandoff || !highlightHole[i];
        if (!eligible) continue;
        float scale = max(1.0e-12, exposure[i]);
        float sensorVariance = sampleVariance(
            frameIndex[i], samples[i], estimate * exposure[i]) / (scale * scale);
        float sceneFloor = uNumericalFloor / (scale * scale);
        float exposureSigma = log(2.0) *
            frameParameters[frameIndex[i]].exposureTranslation.y;
        float scaleVariance = min(
            sensorVariance, estimate * estimate * exposureSigma * exposureSigma);
        variance[i] = max(sceneFloor, sensorVariance + scaleVariance);
        baseWeight[i] = 1.0 / variance[i];
        if (lowConfidence[i]) baseWeight[i] *= 0.35;
        roughSum += baseWeight[i];
        if (i == referenceCandidate) referenceVariance = variance[i];
    }

    float sumWeight = 0.0;
    for (int i = 0; i < candidateCount; ++i) {
        bool eligible = !highlightSafeHandoff || !highlightHole[i];
        if (!eligible) continue;
        float predictedNormalized = estimate * exposure[i] / gain[i];
        float sigmaNormalized = max(
            1.0e-8, sqrt(variance[i]) * exposure[i] / gain[i]);
        float headroomSigma = (0.995 - predictedNormalized) / sigmaNormalized;
        float saturationConfidence = smoothStepExact(
            uSaturationSigma, uSaturationSigma + 4.0, headroomSigma);
        weight[i] = baseWeight[i] * saturationConfidence;
        sumWeight += weight[i];
    }
    float verifiedWeight = 0.0;
    for (int i = 0; i < candidateCount; ++i) {
        bool eligible = !highlightSafeHandoff || !highlightHole[i];
        if (eligible && !lowConfidence[i]) verifiedWeight += weight[i];
    }
    if (verifiedWeight > 0.0) {
        float cap = verifiedWeight * uUnverifiedCap / (1.0 - uUnverifiedCap);
        for (int i = 0; i < candidateCount; ++i) {
            bool eligible = !highlightSafeHandoff || !highlightHole[i];
            if (eligible && lowConfidence[i]) {
                sumWeight += min(weight[i], cap) - weight[i];
                weight[i] = min(weight[i], cap);
            }
        }
    }
    if (sumWeight <= 1.0e-30) {
        int shortest = -1;
        for (int i = 0; i < candidateCount; ++i) {
            bool eligible = !highlightSafeHandoff || !highlightHole[i];
            if (eligible && (shortest < 0 || exposure[i] < exposure[shortest]))
                shortest = i;
        }
        weight[shortest] = baseWeight[shortest];
        sumWeight = weight[shortest];
        estimate = scene[shortest];
    }
    for (int i = 0; i < candidateCount; ++i) fusionWeight[i] = weight[i];

    bool strongDisagreement = false;
    for (int iteration = 0; iteration < 2; ++iteration) {
        float nextWeights[MAX_FRAMES];
        for (int i = 0; i < MAX_FRAMES; ++i) nextWeights[i] = 0.0;
        float robustSum = 0.0;
        float robustValue = 0.0;
        for (int i = 0; i < candidateCount; ++i) {
            if (weight[i] <= 0.0) continue;
            float z = abs(scene[i] - estimate) /
                sqrt(variance[i] + 1.0 / max(1.0e-30, sumWeight));
            strongDisagreement = strongDisagreement || z > uDisagreementSigma;
            float u = z / uRobustCutoff;
            float robust = u < 1.0 ? (1.0 - u * u) * (1.0 - u * u) : 0.0;
            nextWeights[i] = weight[i] * robust;
            robustSum += nextWeights[i];
            robustValue += nextWeights[i] * scene[i];
        }
        if (robustSum <= 1.0e-30) break;
        for (int i = 0; i < candidateCount; ++i)
            fusionWeight[i] = nextWeights[i];
        sumWeight = robustSum;
        estimate = robustValue / robustSum;
    }

    int contributorCount = 0;
    for (int i = 0; i < candidateCount; ++i) {
        if (fusionWeight[i] > max(1.0e-30, weight[i] * 0.01))
            ++contributorCount;
    }
    bool fallback = strongDisagreement && referenceAvailable &&
        !highlightSafeHandoff && (eligibleCount <= 2 || contributorCount < 2);
    if (fallback) estimate = referenceScene;

    int range = estimate < 0.10 ? 0 : (estimate < 0.75 ? 1 : 2);
    float weightSquared = 0.0;
    float ownerWeight = -1.0;
    int owner = uReferenceFrame;
    for (int i = 0; i < candidateCount; ++i) {
        float normalizedWeight = fallback
            ? (frameIndex[i] == uReferenceFrame ? 1.0 : 0.0)
            : fusionWeight[i] / max(1.0e-30, sumWeight);
        weightSquared += normalizedWeight * normalizedWeight;
        if (normalizedWeight > ownerWeight) {
            ownerWeight = normalizedWeight;
            owner = frameIndex[i];
        }
        uint fixedContribution = uint(round(
            max(0.0, normalizedWeight) * 4096.0));
        atomicAdd(diagnostics[8 + frameIndex[i] * 4], fixedContribution);
        atomicAdd(diagnostics[8 + frameIndex[i] * 4 + 1 + range],
            fixedContribution);
    }
    float neff = 1.0 / max(1.0e-30, weightSquared);
    float outputValue = estimate / referenceGain;
    float outputVariance =
        (fallback ? referenceVariance : 1.0 / max(1.0e-30, sumWeight)) /
        (referenceGain * referenceGain);
    float confidence = fallback ? 0.0 : clamp(neff - 1.0, 0.0, 1.0);
    float headroom = max(0.0, log2(max(1.0, estimate)));
    uint resultFlags = 0u;
    if (fallback) resultFlags |= FLAG_REFERENCE_FALLBACK;
    if (highlightSafeHandoff) resultFlags |= FLAG_HIGHLIGHT_SAFE_HANDOFF;
    if (frameParameters[owner].confidenceVariance.y > 0.5)
        resultFlags |= FLAG_LOW_CONFIDENCE_OWNER;
    if (headroom > 0.001) resultFlags |= FLAG_RECOVERED_HIGHLIGHT;
    if (contributorCount <= 1) resultFlags |= FLAG_SINGLE_EXPOSURE;

    imageStore(uOutput, pixel, vec4(outputValue, outputVariance, confidence, neff));
    imageStore(uHeadroom, pixel, vec4(headroom));
    imageStore(uMeta, pixel, uvec4(1u, uint(owner), resultFlags, 0u));

    atomicAdd(diagnostics[0], 1u);
    if (fallback) atomicAdd(diagnostics[1], 1u);
    if (highlightSafeHandoff) atomicAdd(diagnostics[2], 1u);
    if (headroom > 0.001) atomicAdd(diagnostics[3], 1u);
    atomicAdd(diagnostics[4 + range], 1u);
    atomicAdd(diagnostics[7], uint(round(neff * 128.0)));
}
)GLSL";

void SetUniform1i(GLuint program, const char* name, GLint value) {
    const GLint location = glGetUniformLocation(program, name);
    if (location >= 0) glUniform1i(location, value);
}

void SetUniform1f(GLuint program, const char* name, GLfloat value) {
    const GLint location = glGetUniformLocation(program, name);
    if (location >= 0) glUniform1f(location, value);
}

bool ValidateRequest(const GpuFusionRequest& request, std::string& error) {
    if (request.width == 0u || request.height == 0u || request.frames.size() < 2u ||
        request.frames.size() > kMaximumFrames ||
        request.referenceFrame >= request.frames.size() ||
        request.cfaPattern == CfaPattern::Unknown) {
        error = "The OpenGL HDR fusion request is incomplete.";
        return false;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(request.width) *
        static_cast<std::size_t>(request.height);
    if (request.width != 0u && pixelCount / request.width != request.height) {
        error = "The OpenGL HDR dimensions overflow host addressing.";
        return false;
    }
    for (const GpuFusionFrameView& frame : request.frames) {
        if (!frame.normalized || !frame.gain || !frame.flags ||
            !frame.clippedNeighborCount || frame.normalized->size() != pixelCount ||
            frame.gain->size() != pixelCount || frame.flags->size() != pixelCount ||
            frame.clippedNeighborCount->size() != pixelCount ||
            (frame.explicitVariance && frame.explicitVariance->size() != pixelCount) ||
            !(frame.exposureRelativeToAnchor > 0.0f) ||
            !std::isfinite(frame.exposureRelativeToAnchor)) {
            error = "An OpenGL HDR frame view violates the prepared-frame contract.";
            return false;
        }
    }
    return true;
}

} // namespace

bool FuseOpenGl(
    const GpuFusionRequest& request,
    GpuFusionOutput& output,
    std::string& error) {
    output = {};
    error.clear();
    if (!ValidateRequest(request, error)) return false;
    if (Canceled(request)) {
        error = "HDR GPU fusion was canceled.";
        return false;
    }

    GLint major = 0;
    GLint minor = 0;
    GLint maximumTextureSize = 0;
    GLint maximumLayers = 0;
    GLint maximumImageUnits = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maximumLayers);
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &maximumImageUnits);
    if (major < 4 || (major == 4 && minor < 3) ||
        request.width > static_cast<std::uint32_t>(maximumTextureSize) ||
        request.height > static_cast<std::uint32_t>(maximumTextureSize) ||
        request.frames.size() > static_cast<std::size_t>(maximumLayers) ||
        maximumImageUnits < 8) {
        error = "OpenGL 4.3 image compute cannot represent this HDR working set.";
        return false;
    }
    const auto glText = [](GLenum name) {
        const GLubyte* value = glGetString(name);
        return value ? std::string(reinterpret_cast<const char*>(value))
                     : std::string("unknown");
    };
    const std::string deviceIdentity = glText(GL_VENDOR) + " | " +
        glText(GL_RENDERER) + " | " + glText(GL_VERSION);

    GLint previousProgram = 0;
    GLint previousTexture2D = 0;
    GLint previousTextureArray = 0;
    GLint previousSsbo = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture2D);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &previousTextureArray);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previousSsbo);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    GlResources resources;
    const auto restoreState = [&]() {
        glUseProgram(static_cast<GLuint>(std::max(0, previousProgram)));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(std::max(0, previousTexture2D)));
        glBindTexture(
            GL_TEXTURE_2D_ARRAY,
            static_cast<GLuint>(std::max(0, previousTextureArray)));
        glBindBuffer(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<GLuint>(std::max(0, previousSsbo)));
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    };

    resources.program = GLHelpers::CreateComputeProgram(kFusionShader);
    if (resources.program == 0) {
        error = "The HDR v4 OpenGL compute shader did not compile.";
        restoreState();
        return false;
    }

    const GLsizei width = static_cast<GLsizei>(request.width);
    const GLsizei height = static_cast<GLsizei>(request.height);
    const GLsizei layers = static_cast<GLsizei>(request.frames.size());
    resources.inputTextures[0] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R32F, width, height, layers, error);
    resources.inputTextures[1] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R32F, width, height, layers, error);
    resources.inputTextures[2] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R8UI, width, height, layers, error);
    resources.inputTextures[3] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R8UI, width, height, layers, error);
    resources.inputTextures[4] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R32F, width, height, layers, error);
    resources.outputTextures[0] = CreateTextureStorage(
        GL_TEXTURE_2D, GL_RGBA32F, width, height, 1, error);
    resources.outputTextures[1] = CreateTextureStorage(
        GL_TEXTURE_2D, GL_R32F, width, height, 1, error);
    resources.outputTextures[2] = CreateTextureStorage(
        GL_TEXTURE_2D, GL_RGBA8UI, width, height, 1, error);
    if (std::any_of(resources.inputTextures.begin(), resources.inputTextures.end(),
            [](GLuint texture) { return texture == 0; }) ||
        std::any_of(resources.outputTextures.begin(), resources.outputTextures.end(),
            [](GLuint texture) { return texture == 0; })) {
        if (error.empty()) error = "The GPU could not allocate the HDR working textures.";
        restoreState();
        return false;
    }

    std::vector<GpuFrameParameters> gpuParameters(request.frames.size());
    for (std::size_t frameIndex = 0; frameIndex < request.frames.size(); ++frameIndex) {
        if (Canceled(request)) {
            error = "HDR GPU upload was canceled.";
            restoreState();
            return false;
        }
        const GpuFusionFrameView& frame = request.frames[frameIndex];
        if (!UploadLayer(resources.inputTextures[0], static_cast<GLint>(frameIndex),
                width, height, GL_RED, GL_FLOAT, frame.normalized->data(), error) ||
            !UploadLayer(resources.inputTextures[1], static_cast<GLint>(frameIndex),
                width, height, GL_RED, GL_FLOAT, frame.gain->data(), error) ||
            !UploadLayer(resources.inputTextures[2], static_cast<GLint>(frameIndex),
                width, height, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                frame.flags->data(), error) ||
            !UploadLayer(resources.inputTextures[3], static_cast<GLint>(frameIndex),
                width, height, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                frame.clippedNeighborCount->data(), error) ||
            (frame.explicitVariance &&
             !UploadLayer(resources.inputTextures[4], static_cast<GLint>(frameIndex),
                width, height, GL_RED, GL_FLOAT,
                frame.explicitVariance->data(), error))) {
            restoreState();
            return false;
        }
        GpuFrameParameters& parameters = gpuParameters[frameIndex];
        parameters.exposureTranslation = {
            frame.exposureRelativeToAnchor,
            frame.exposureUncertaintyEv,
            frame.translationX,
            frame.translationY };
        parameters.confidenceVariance = {
            frame.noiseVarianceInflation,
            frame.lowConfidence || !frame.exposureVerified ? 1.0f : 0.0f,
            frame.explicitVariance ? 1.0f : 0.0f,
            0.0f };
        for (std::size_t site = 0; site < 4; ++site) {
            parameters.shot[site] = static_cast<float>(frame.noise[site].shotScale);
            parameters.offset[site] = static_cast<float>(
                frame.noise[site].offsetVariance +
                frame.noise[site].quantizationVariance);
        }
    }

    glGenBuffers(1, &resources.frameParameters);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.frameParameters);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(gpuParameters.size() * sizeof(GpuFrameParameters)),
        gpuParameters.data(), GL_STATIC_DRAW);

    const std::size_t diagnosticWordCount = kCounterCount +
        request.frames.size() * kContributionSlotsPerFrame;
    std::vector<std::uint32_t> diagnosticWords(diagnosticWordCount, 0u);
    glGenBuffers(1, &resources.diagnostics);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.diagnostics);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(diagnosticWords.size() * sizeof(std::uint32_t)),
        diagnosticWords.data(), GL_DYNAMIC_DRAW);
    if (!CheckGl("HDR compute buffer allocation", error)) {
        restoreState();
        return false;
    }

    glUseProgram(resources.program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, resources.frameParameters);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, resources.diagnostics);
    for (GLuint unit = 0; unit < resources.inputTextures.size(); ++unit) {
        const GLenum format = unit == 2 || unit == 3 ? GL_R8UI : GL_R32F;
        glBindImageTexture(
            unit, resources.inputTextures[unit], 0, GL_TRUE, 0,
            GL_READ_ONLY, format);
    }
    glBindImageTexture(
        5, resources.outputTextures[0], 0, GL_FALSE, 0,
        GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(
        6, resources.outputTextures[1], 0, GL_FALSE, 0,
        GL_WRITE_ONLY, GL_R32F);
    glBindImageTexture(
        7, resources.outputTextures[2], 0, GL_FALSE, 0,
        GL_WRITE_ONLY, GL_RGBA8UI);

    glUniform2i(glGetUniformLocation(resources.program, "uExtent"), width, height);
    SetUniform1i(resources.program, "uFrameCount", layers);
    SetUniform1i(resources.program, "uReferenceFrame",
        static_cast<GLint>(request.referenceFrame));
    SetUniform1i(resources.program, "uCfaPattern",
        static_cast<GLint>(request.cfaPattern));
    SetUniform1f(resources.program, "uNumericalFloor",
        static_cast<float>(request.parameters.numericalVarianceFloor));
    SetUniform1f(resources.program, "uUnverifiedCap",
        static_cast<float>(request.parameters.unverifiedContributionCap));
    SetUniform1f(resources.program, "uSaturationSigma",
        static_cast<float>(request.parameters.saturationHeadroomSigma));
    SetUniform1f(resources.program, "uRobustCutoff",
        static_cast<float>(request.parameters.robustCutoffSigma));
    SetUniform1f(resources.program, "uDisagreementSigma",
        static_cast<float>(request.parameters.disagreementFallbackSigma));
    SetUniform1f(resources.program, "uHighlightHoleCeiling",
        static_cast<float>(request.parameters.highlightHoleNormalizedCeiling));
    SetUniform1i(resources.program, "uHighlightHoleNeighbors",
        static_cast<GLint>(request.parameters.highlightHoleMinimumClippedNeighbors));

    GpuFusionDiagnostics aggregate;
    aggregate.deviceIdentity = deviceIdentity;
    aggregate.contribution.assign(request.frames.size(), 0.0);
    aggregate.contributionByRange.resize(request.frames.size());
    const std::uint32_t tileSize = std::clamp(
        request.tileRawPixels, 64u, kMaximumDispatchTile);
    const std::uint32_t tileColumns = (request.width + tileSize - 1u) / tileSize;
    const std::uint32_t tileRows = (request.height + tileSize - 1u) / tileSize;
    const std::uint32_t tileCount = tileColumns * tileRows;

    for (std::uint32_t tileY = 0; tileY < tileRows; ++tileY) {
        for (std::uint32_t tileX = 0; tileX < tileColumns; ++tileX) {
            if (Canceled(request)) {
                error = "HDR GPU fusion was canceled.";
                restoreState();
                return false;
            }
            const std::uint32_t originX = tileX * tileSize;
            const std::uint32_t originY = tileY * tileSize;
            const std::uint32_t tileWidth = std::min(tileSize, request.width - originX);
            const std::uint32_t tileHeight = std::min(tileSize, request.height - originY);
            std::fill(diagnosticWords.begin(), diagnosticWords.end(), 0u);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.diagnostics);
            glBufferSubData(
                GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GLsizeiptr>(diagnosticWords.size() * sizeof(std::uint32_t)),
                diagnosticWords.data());
            glUniform2i(glGetUniformLocation(resources.program, "uTileOrigin"),
                static_cast<GLint>(originX), static_cast<GLint>(originY));
            glUniform2i(glGetUniformLocation(resources.program, "uTileExtent"),
                static_cast<GLint>(tileWidth), static_cast<GLint>(tileHeight));
            glDispatchCompute(
                (tileWidth + kWorkgroupSize - 1u) / kWorkgroupSize,
                (tileHeight + kWorkgroupSize - 1u) / kWorkgroupSize,
                1u);
            glMemoryBarrier(
                GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                GL_SHADER_STORAGE_BARRIER_BIT |
                GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            if (!CheckGl("HDR compute dispatch", error)) {
                restoreState();
                return false;
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.diagnostics);
            glGetBufferSubData(
                GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GLsizeiptr>(diagnosticWords.size() * sizeof(std::uint32_t)),
                diagnosticWords.data());
            if (!CheckGl("HDR diagnostic readback", error)) {
                restoreState();
                return false;
            }
            aggregate.finitePixelCount += diagnosticWords[0];
            aggregate.referenceFallbackPixelCount += diagnosticWords[1];
            aggregate.highlightSafeHandoffPixelCount += diagnosticWords[2];
            aggregate.recoveredHighlightPixelCount += diagnosticWords[3];
            for (std::size_t range = 0; range < 3; ++range)
                aggregate.rangePixelCount[range] += diagnosticWords[4 + range];
            aggregate.effectiveSampleSum += static_cast<double>(diagnosticWords[7]) /
                static_cast<double>(kEffectiveSampleScale);
            for (std::size_t frame = 0; frame < request.frames.size(); ++frame) {
                const std::size_t offset = kCounterCount +
                    frame * kContributionSlotsPerFrame;
                aggregate.contribution[frame] +=
                    static_cast<double>(diagnosticWords[offset]) /
                    static_cast<double>(kContributionScale);
                for (std::size_t range = 0; range < 3; ++range) {
                    aggregate.contributionByRange[frame][range] +=
                        static_cast<double>(diagnosticWords[offset + 1 + range]) /
                        static_cast<double>(kContributionScale);
                }
            }
            ++aggregate.dispatchedTileCount;
            if (request.reportProgress) {
                request.reportProgress(static_cast<double>(aggregate.dispatchedTileCount) /
                    std::max(1u, tileCount));
            }
        }
    }

    const std::size_t pixelCount = static_cast<std::size_t>(request.width) *
        static_cast<std::size_t>(request.height);
    std::vector<float> rgba(pixelCount * 4u);
    std::vector<float> headroom(pixelCount);
    std::vector<std::uint8_t> meta(pixelCount * 4u);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glBindTexture(GL_TEXTURE_2D, resources.outputTextures[0]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rgba.data());
    glBindTexture(GL_TEXTURE_2D, resources.outputTextures[1]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, headroom.data());
    glBindTexture(GL_TEXTURE_2D, resources.outputTextures[2]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, meta.data());
    if (!CheckGl("HDR result readback", error)) {
        restoreState();
        return false;
    }
    if (Canceled(request)) {
        error = "HDR GPU readback was canceled.";
        restoreState();
        return false;
    }

    GpuFusionOutput completed;
    try {
        completed.virtualAnchorMosaic.resize(pixelCount);
        completed.varianceProxy.resize(pixelCount);
        completed.mergeConfidence.resize(pixelCount);
        completed.effectiveSampleCount.resize(pixelCount);
        completed.recoveredHeadroomStops = std::move(headroom);
        completed.validityMask.resize(pixelCount);
        completed.ownerFrame.resize(pixelCount);
        completed.flags.resize(pixelCount);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
            completed.virtualAnchorMosaic[pixel] = rgba[pixel * 4u];
            completed.varianceProxy[pixel] = rgba[pixel * 4u + 1u];
            completed.mergeConfidence[pixel] = rgba[pixel * 4u + 2u];
            completed.effectiveSampleCount[pixel] = std::max(
                1.0f, rgba[pixel * 4u + 3u]);
            completed.validityMask[pixel] = meta[pixel * 4u];
            completed.ownerFrame[pixel] = meta[pixel * 4u + 1u];
            completed.flags[pixel] = meta[pixel * 4u + 2u];
        }
        completed.diagnostics = std::move(aggregate);
    } catch (const std::bad_alloc&) {
        error = "Host memory could not hold the verified HDR GPU readback.";
        restoreState();
        return false;
    }

    restoreState();
    output = std::move(completed);
    return true;
}

} // namespace Raw::Hdr
