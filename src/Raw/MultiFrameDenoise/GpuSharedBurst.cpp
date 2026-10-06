#include "Raw/MultiFrameDenoise/GpuSharedBurst.h"

#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_R8UI
#define GL_R8UI 0x8232
#endif
#ifndef GL_RGBA32UI
#define GL_RGBA32UI 0x8D70
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

namespace Raw::Mfd {
namespace {

constexpr std::uint32_t kMaximumFrames =
    static_cast<std::uint32_t>(kSharedBurstMaximumEnabledCaptures);
constexpr std::uint32_t kWorkgroupSize = 8u;
constexpr std::uint32_t kMaximumDispatchTile = 512u;

struct alignas(16) GpuFrameParameters {
    std::array<float, 4> exposureQuality {};
    std::array<float, 4> usableSpan {};
    std::array<float, 4> shot {};
    std::array<float, 4> offset {};
    std::array<float, 4> tau0 {};
    std::array<float, 4> tau1 {};
    std::array<float, 4> motionOriginSpacing {};
    std::array<std::int32_t, 4> motionShapeOffset {};
    std::array<float, 4> affine0 {};
    std::array<float, 4> affine1 {};
    std::array<float, 4> sourceReliability {};
    std::array<float, 4> misc {};
};

struct alignas(16) GpuMotionNode {
    std::array<float, 4> residualConfidenceValid {};
    std::array<float, 4> covariance {};
};

static_assert(sizeof(GpuFrameParameters) == 192u);
static_assert(sizeof(GpuMotionNode) == 32u);

struct GlResources {
    GLuint program = 0;
    std::array<GLuint, 4> inputTextures {};
    std::array<GLuint, 4> outputTextures {};
    GLuint frameParameters = 0;
    GLuint motionNodes = 0;

    ~GlResources() {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
        for (GLuint unit = 0; unit < 8; ++unit)
            glBindImageTexture(unit, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        if (motionNodes != 0) glDeleteBuffers(1, &motionNodes);
        if (frameParameters != 0) glDeleteBuffers(1, &frameParameters);
        glDeleteTextures(
            static_cast<GLsizei>(outputTextures.size()), outputTextures.data());
        glDeleteTextures(
            static_cast<GLsizei>(inputTextures.size()), inputTextures.data());
        if (program != 0) glDeleteProgram(program);
    }
};

bool Canceled(const GpuSharedBurstRequest& request) {
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
        error = "OpenGL could not allocate a Shared Burst texture name.";
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
    if (!CheckGl("Shared Burst texture allocation", error)) {
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
    return CheckGl("Shared Burst texture upload", error);
}

constexpr const char* kSharedBurstShader = R"GLSL(
#version 430 core

#define MAX_FRAMES 30
#define REJECT_REASON_COUNT 12

#define FLAG_EXACT_REFERENCE 1u
#define FLAG_REFERENCE_INCLUDED 2u

#define DECISION_NONE 0u
#define DECISION_REFERENCE_CLIPPED 16u
#define DECISION_NO_VALID_CANDIDATE 18u
#define DECISION_ALTERNATE_WEIGHT_INSUFFICIENT 19u
#define DECISION_NUMERICAL_FALLBACK 21u

#define REJECT_CANDIDATE_UNAVAILABLE 1u
#define REJECT_NOISE_UNAVAILABLE 3u
#define REJECT_INVALID_NUMERIC 4u
#define REJECT_RELIABILITY_ZERO 5u
#define REJECT_PIXEL_OUTLIER 6u
#define REJECT_ABSOLUTE_SAFETY 7u
#define REJECT_INVALID_VARIANCE 8u

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(r32f, binding = 0) readonly uniform image2DArray uNormalized;
layout(r32f, binding = 1) readonly uniform image2DArray uGain;
layout(r8ui, binding = 2) readonly uniform uimage2DArray uFlags;
layout(r32f, binding = 3) readonly uniform image2DArray uReliability;
layout(rgba32f, binding = 4) writeonly uniform image2D uOutputA;
layout(rgba32f, binding = 5) writeonly uniform image2D uOutputB;
layout(rgba32f, binding = 6) writeonly uniform image2D uOutputC;
layout(rgba32ui, binding = 7) writeonly uniform uimage2D uMeta;

struct FrameParameters {
    vec4 exposureQuality;       // scale, scale variance, trust, noise quality
    vec4 usableSpan;
    vec4 shot;
    vec4 offset;
    vec4 tau0;
    vec4 tau1;
    vec4 motionOriginSpacing;   // origin x/y, spacing x/y
    ivec4 motionShapeOffset;    // width, height, SSBO offset, unused
    vec4 affine0;               // m00, m01, m10, m11
    vec4 affine1;               // center x/y, translation x/y
    vec4 sourceReliability;     // source width/height, reliability width/height
    vec4 misc;                  // CFA pattern, unused
};

struct MotionNode {
    vec4 residualConfidenceValid; // residual x/y, confidence, valid
    vec4 covariance;              // xx, xy, yy, unused
};

layout(std430, binding = 0) readonly buffer FrameParameterBuffer {
    FrameParameters frames[];
};
layout(std430, binding = 1) readonly buffer MotionNodeBuffer {
    MotionNode motionNodes[];
};

uniform ivec2 uExtent;
uniform ivec2 uTileOrigin;
uniform ivec2 uTileExtent;
uniform int uFrameCount;
uniform float uNumericalFloor;
uniform float uMaximumComparisonGain;
uniform float uInterpolationEpsilon;
uniform float uMotionDisagreementScale;
uniform float uMotionDisagreementLimit;
uniform float uWarpCovarianceMinimum;
uniform float uWarpCovarianceMaximum;
uniform float uTrustedFullSigma;
uniform float uTrustedZeroSigma;
uniform float uLowConfidenceFullSigma;
uniform float uLowConfidenceZeroSigma;
uniform float uAbsoluteDnMultiplier;
uniform float uAbsoluteNoiseMultiplier;
uniform float uAbsoluteRelativeFraction;
uniform float uExactFallbackRatio;
uniform float uHuberThreshold;
uniform int uHuberIterations;
uniform float uAbsoluteHuberTolerance;
uniform float uRelativeHuberTolerance;

bool Finite(float value) {
    return !isnan(value) && !isinf(value);
}

int SiteAt(ivec2 raw, int pattern) {
    int index = (raw.x & 1) + ((raw.y & 1) << 1);
    if (pattern == 1) {
        const int sites[4] = int[4](0, 1, 2, 3);
        return sites[index];
    }
    if (pattern == 2) {
        const int sites[4] = int[4](3, 1, 2, 0);
        return sites[index];
    }
    if (pattern == 3) {
        const int sites[4] = int[4](1, 3, 0, 2);
        return sites[index];
    }
    const int sites[4] = int[4](1, 0, 3, 2);
    return sites[index];
}

ivec2 SiteOffset(int site, int pattern) {
    if (pattern == 1) {
        const ivec2 offsets[4] = ivec2[4](
            ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
        return offsets[site];
    }
    if (pattern == 2) {
        const ivec2 offsets[4] = ivec2[4](
            ivec2(1, 1), ivec2(1, 0), ivec2(0, 1), ivec2(0, 0));
        return offsets[site];
    }
    if (pattern == 3) {
        const ivec2 offsets[4] = ivec2[4](
            ivec2(0, 1), ivec2(0, 0), ivec2(1, 1), ivec2(1, 0));
        return offsets[site];
    }
    const ivec2 offsets[4] = ivec2[4](
        ivec2(1, 0), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
    return offsets[site];
}

float Component(vec4 value, int index) {
    return index == 0 ? value.x : index == 1 ? value.y :
        index == 2 ? value.z : value.w;
}

float Keys(float distance) {
    float t = abs(distance);
    const float a = -0.5;
    if (t <= 1.0)
        return (a + 2.0) * t * t * t - (a + 3.0) * t * t + 1.0;
    if (t < 2.0)
        return a * t * t * t - 5.0 * a * t * t + 8.0 * a * t - 4.0 * a;
    return 0.0;
}

float KeysDerivative(float distance) {
    float t = abs(distance);
    if (t == 0.0 || t >= 2.0) return 0.0;
    float signValue = distance < 0.0 ? -1.0 : 1.0;
    const float a = -0.5;
    if (t <= 1.0)
        return signValue * (3.0 * (a + 2.0) * t * t -
            2.0 * (a + 3.0) * t);
    return signValue * (3.0 * a * t * t - 10.0 * a * t + 8.0 * a);
}

float FlatTopGate(float magnitude, float inner, float outer) {
    float absoluteValue = abs(magnitude);
    if (absoluteValue <= inner) return 1.0;
    if (absoluteValue >= outer) return 0.0;
    float t = (absoluteValue - inner) / (outer - inner);
    float smoother = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    return 1.0 - smoother;
}

bool Eigenvalues(vec3 covariance, out float low, out float high) {
    float trace = covariance.x + covariance.z;
    float difference = covariance.x - covariance.z;
    float radius = sqrt(max(0.0,
        0.25 * difference * difference + covariance.y * covariance.y));
    low = 0.5 * trace - radius;
    high = 0.5 * trace + radius;
    return Finite(low) && Finite(high);
}

bool ClampCovariance(vec3 inputValue, out vec3 result) {
    float low = 0.0;
    float high = 0.0;
    if (!Eigenvalues(inputValue, low, high)) return false;
    if (high < -1.0e-10 || low < -max(1.0e-10, 1.0e-8 * abs(high)))
        return false;
    float minimumValue = uWarpCovarianceMinimum * uWarpCovarianceMinimum;
    float maximumValue = uWarpCovarianceMaximum * uWarpCovarianceMaximum;
    float angle = 0.5 * atan(2.0 * inputValue.y, inputValue.x - inputValue.z);
    float clampedHigh = clamp(high, minimumValue, maximumValue);
    float clampedLow = clamp(low, minimumValue, maximumValue);
    float c = cos(angle);
    float s = sin(angle);
    result = vec3(
        clampedHigh * c * c + clampedLow * s * s,
        (clampedHigh - clampedLow) * c * s,
        clampedHigh * s * s + clampedLow * c * c);
    return Finite(result.x) && Finite(result.y) && Finite(result.z);
}

bool EvaluateMotion(int frameIndex, vec2 referenceRaw,
                    out vec2 sourceRaw, out vec3 covariance,
                    out float alignmentConfidence) {
    FrameParameters frame = frames[frameIndex];
    int gridWidth = frame.motionShapeOffset.x;
    int gridHeight = frame.motionShapeOffset.y;
    if (gridWidth <= 0 || gridHeight <= 0) return false;
    vec2 grid = (referenceRaw - frame.motionOriginSpacing.xy) /
        frame.motionOriginSpacing.zw;
    if (grid.x < -1.0e-12 || grid.y < -1.0e-12 ||
        grid.x > float(gridWidth - 1) + 1.0e-12 ||
        grid.y > float(gridHeight - 1) + 1.0e-12) return false;
    int x0 = gridWidth == 1 ? 0 : min(int(floor(max(0.0, grid.x))), gridWidth - 2);
    int y0 = gridHeight == 1 ? 0 : min(int(floor(max(0.0, grid.y))), gridHeight - 2);
    int x1 = gridWidth == 1 ? 0 : x0 + 1;
    int y1 = gridHeight == 1 ? 0 : y0 + 1;
    float tx = gridWidth == 1 ? 0.0 : clamp(grid.x - float(x0), 0.0, 1.0);
    float ty = gridHeight == 1 ? 0.0 : clamp(grid.y - float(y0), 0.0, 1.0);
    int xs[4] = int[4](x0, x1, x0, x1);
    int ys[4] = int[4](y0, y0, y1, y1);
    float bilinear[4] = float[4](
        (1.0 - tx) * (1.0 - ty), tx * (1.0 - ty),
        (1.0 - tx) * ty, tx * ty);
    float alpha[4];
    vec2 residuals[4];
    vec3 covariances[4];
    float alphaSum = 0.0;
    float gridConfidence = 0.0;
    vec2 mean = vec2(0.0);
    int base = frame.motionShapeOffset.z;
    for (int corner = 0; corner < 4; ++corner) {
        MotionNode node = motionNodes[base + ys[corner] * gridWidth + xs[corner]];
        float confidence = node.residualConfidenceValid.w > 0.5
            ? clamp(node.residualConfidenceValid.z, 0.0, 1.0) : 0.0;
        residuals[corner] = node.residualConfidenceValid.xy;
        covariances[corner] = node.covariance.xyz;
        alpha[corner] = bilinear[corner] * confidence;
        alphaSum += alpha[corner];
        gridConfidence += bilinear[corner] * confidence;
        mean += alpha[corner] * residuals[corner];
    }
    if (!Finite(alphaSum) || alphaSum <= uInterpolationEpsilon) return false;
    mean /= alphaSum;
    vec3 combined = vec3(0.0);
    vec3 disagreement = vec3(0.0);
    for (int corner = 0; corner < 4; ++corner) {
        if (alpha[corner] <= 0.0) continue;
        float weight = alpha[corner] / alphaSum;
        vec2 delta = residuals[corner] - mean;
        vec3 scatter = vec3(delta.x * delta.x, delta.x * delta.y,
                            delta.y * delta.y);
        combined += weight * (covariances[corner] + scatter);
        disagreement += weight * scatter;
    }
    float disagreementLow = 0.0;
    float disagreementHigh = 0.0;
    if (!Eigenvalues(disagreement, disagreementLow, disagreementHigh)) return false;
    float disagreementSigma = sqrt(max(0.0, disagreementHigh));
    if (disagreementSigma > uMotionDisagreementLimit ||
        !ClampCovariance(combined, covariance)) return false;
    float disagreementRatio = disagreementSigma / uMotionDisagreementScale;
    alignmentConfidence = clamp(gridConfidence, 0.0, 1.0) *
        exp(-0.5 * disagreementRatio * disagreementRatio);
    vec2 centered = referenceRaw - frame.affine1.xy;
    vec2 globalMapped = frame.affine1.xy + vec2(
        frame.affine0.x * centered.x + frame.affine0.y * centered.y,
        frame.affine0.z * centered.x + frame.affine0.w * centered.y) +
        frame.affine1.zw;
    sourceRaw = globalMapped + mean;
    return Finite(sourceRaw.x) && Finite(sourceRaw.y) &&
        sourceRaw.x >= 0.0 && sourceRaw.y >= 0.0 &&
        sourceRaw.x <= frame.sourceReliability.x - 1.0 &&
        sourceRaw.y <= frame.sourceReliability.y - 1.0;
}

bool SampleSameCfa(int frameIndex, vec2 sourceRaw, int site,
                   float referencePilot, vec3 warpCovariance,
                   out float value, out float gateVariance,
                   out float fusionVariance, out float darkVariance,
                   out float effectiveDnStep) {
    FrameParameters frame = frames[frameIndex];
    int pattern = int(round(frame.misc.x));
    ivec2 offset = SiteOffset(site, pattern);
    vec2 sourcePlane = (sourceRaw - vec2(offset)) * 0.5;
    ivec2 base = ivec2(floor(sourcePlane));
    ivec2 planeExtent = ivec2(
        max(0, (uExtent.x - offset.x + 1) / 2),
        max(0, (uExtent.y - offset.y + 1) / 2));
    if (base.x - 1 < 0 || base.y - 1 < 0 ||
        base.x + 2 >= planeExtent.x || base.y + 2 >= planeExtent.y)
        return false;
    float exposureScale = frame.exposureQuality.x;
    float exposureScaleVariance = frame.exposureQuality.y;
    float shot = Component(frame.shot, site);
    float offsetVariance = Component(frame.offset, site);
    float span = Component(frame.usableSpan, site);
    float tau0 = Component(frame.tau0, site);
    float tau1 = Component(frame.tau1, site);
    if (!(exposureScale > 0.0) || !(span > 0.0)) return false;
    value = 0.0;
    darkVariance = 0.0;
    float interpolationVariance = 0.0;
    float dnStepSquared = 0.0;
    vec2 gradientPlane = vec2(0.0);
    float pilot = max(0.0, referencePilot);
    for (int oy = -1; oy <= 2; ++oy) {
        int tapPlaneY = base.y + oy;
        float dy = sourcePlane.y - float(tapPlaneY);
        float wy = Keys(dy);
        float dwy = KeysDerivative(dy);
        for (int ox = -1; ox <= 2; ++ox) {
            int tapPlaneX = base.x + ox;
            float dx = sourcePlane.x - float(tapPlaneX);
            float wx = Keys(dx);
            float dwx = KeysDerivative(dx);
            float coefficient = wx * wy;
            ivec2 raw = ivec2(tapPlaneX * 2 + offset.x,
                              tapPlaneY * 2 + offset.y);
            uint flags = imageLoad(uFlags, ivec3(raw, frameIndex)).r;
            if ((flags & 15u) != 0u) return false;
            float normalized = imageLoad(uNormalized, ivec3(raw, frameIndex)).r;
            float pointGain = imageLoad(uGain, ivec3(raw, frameIndex)).r;
            float gain = exposureScale * pointGain;
            if (!Finite(normalized) || !Finite(gain) || !(gain > 0.0)) return false;
            float comparison = gain * normalized;
            float sampleDark = gain * gain * offsetVariance;
            float sampleVariance = gain * shot * pilot + sampleDark;
            if (!Finite(sampleVariance) || sampleVariance < 0.0) return false;
            value += coefficient * comparison;
            interpolationVariance += coefficient * coefficient * sampleVariance;
            darkVariance += coefficient * coefficient * sampleDark;
            float dnStep = gain / span;
            dnStepSquared += coefficient * coefficient * dnStep * dnStep;
            gradientPlane.x += dwx * wy * comparison;
            gradientPlane.y += wx * dwy * comparison;
        }
    }
    vec2 gradientRaw = gradientPlane * 0.5;
    float registrationVariance =
        gradientRaw.x * gradientRaw.x * warpCovariance.x +
        2.0 * gradientRaw.x * gradientRaw.y * warpCovariance.y +
        gradientRaw.y * gradientRaw.y * warpCovariance.z;
    registrationVariance = max(0.0, registrationVariance);
    float scaleVariance = exposureScaleVariance /
        (exposureScale * exposureScale) * value * value;
    float residualVariance = tau0 * tau0 + tau1 * tau1 * pilot * pilot;
    gateVariance = interpolationVariance + registrationVariance +
        residualVariance + uNumericalFloor;
    fusionVariance = gateVariance + scaleVariance;
    effectiveDnStep = sqrt(max(0.0, dnStepSquared));
    return Finite(value) && Finite(gateVariance) && gateVariance > 0.0 &&
        Finite(fusionVariance) && fusionVariance >= gateVariance &&
        Finite(darkVariance) && darkVariance >= 0.0 &&
        Finite(effectiveDnStep) && effectiveDnStep > 0.0;
}

uint DominantReject(in int rejectionCounts[REJECT_REASON_COUNT]) {
    int largest = 0;
    for (int index = 1; index < REJECT_REASON_COUNT; ++index)
        if (rejectionCounts[index] > rejectionCounts[largest]) largest = index;
    return uint(largest);
}

void WriteResult(ivec2 pixel, float normalized,
                 float referenceWeight, float alternateWeight, float ratio,
                 float effectiveSupport, float outputVariance,
                 float ownerContribution, float robustAttenuation,
                 float modelVariance, float policyVariance, float finiteVariance,
                 uint decision, uint dominant, uint flags,
                 uint eligible, uint contributing, uint rejected,
                 uint rawSupport, uint ownerIndex) {
    imageStore(uOutputA, pixel,
        vec4(normalized, referenceWeight, alternateWeight, ratio));
    imageStore(uOutputB, pixel,
        vec4(effectiveSupport, outputVariance, ownerContribution,
             robustAttenuation));
    imageStore(uOutputC, pixel,
        vec4(modelVariance, policyVariance, finiteVariance, 0.0));
    uint identity = decision | (dominant << 8u) | (flags << 16u);
    uint counts = min(eligible, 255u) |
        (min(contributing, 255u) << 8u) |
        (min(rejected, 255u) << 16u);
    uint supportOwner = min(rawSupport, 65535u) |
        (min(ownerIndex, 65535u) << 16u);
    imageStore(uMeta, pixel, uvec4(identity, counts, supportOwner, 0u));
}

void WriteExact(ivec2 pixel, float normalized, float variance,
                uint decision, uint dominant, uint rejected) {
    WriteResult(pixel, normalized, 0.0, 0.0, 0.0,
        1.0, variance, 1.0, 1.0, variance, variance, variance,
        decision, dominant, FLAG_EXACT_REFERENCE | FLAG_REFERENCE_INCLUDED,
        0u, 0u, rejected, 1u, 0u);
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, uTileExtent))) return;
    ivec2 pixel = uTileOrigin + local;
    if (any(greaterThanEqual(pixel, uExtent))) return;

    FrameParameters referenceFrame = frames[0];
    float referenceNormalized = imageLoad(uNormalized, ivec3(pixel, 0)).r;
    float referenceGain = imageLoad(uGain, ivec3(pixel, 0)).r;
    uint referenceFlags = imageLoad(uFlags, ivec3(pixel, 0)).r;
    int referencePattern = int(round(referenceFrame.misc.x));
    int site = SiteAt(pixel, referencePattern);
    float span = Component(referenceFrame.usableSpan, site);
    float shot = Component(referenceFrame.shot, site);
    float offsetVariance = Component(referenceFrame.offset, site);
    float tau0 = Component(referenceFrame.tau0, site);
    float tau1 = Component(referenceFrame.tau1, site);
    float referenceValue = referenceGain * referenceNormalized;
    float pilot = max(0.0, referenceValue);
    float referenceDark = referenceGain * referenceGain * offsetVariance;
    float referenceInterpolated = referenceGain * shot * pilot + referenceDark;
    float referenceResidual = tau0 * tau0 + tau1 * tau1 * pilot * pilot;
    float referenceGate = referenceInterpolated + referenceResidual + uNumericalFloor;
    float referenceFusion = referenceGate;
    float referenceDnStep = referenceGain / span;
    float referenceFloor = max(uNumericalFloor,
        referenceDnStep * referenceDnStep / 12.0);
    float referenceFrozen = max(referenceFusion, referenceFloor);
    bool validReference = Finite(referenceNormalized) &&
        Finite(referenceGain) && referenceGain > 0.0 &&
        referenceGain <= uMaximumComparisonGain &&
        Finite(referenceValue) && Finite(referenceGate) &&
        referenceGate > 0.0 && Finite(referenceFusion) &&
        referenceFusion > 0.0 && Finite(referenceDark) &&
        referenceDark >= 0.0 && Finite(referenceDnStep) &&
        referenceDnStep > 0.0;
    if (!validReference) {
        WriteExact(pixel, referenceNormalized,
            max(uNumericalFloor, Finite(referenceFusion) ? referenceFusion : uNumericalFloor),
            DECISION_NUMERICAL_FALLBACK, 0u, 0u);
        return;
    }
    if ((referenceFlags & 9u) != 0u) {
        WriteExact(pixel, referenceNormalized, referenceFusion,
            DECISION_REFERENCE_CLIPPED, 0u, 0u);
        return;
    }

    float values[MAX_FRAMES];
    float variances[MAX_FRAMES];
    float reliabilities[MAX_FRAMES];
    float robustFactors[MAX_FRAMES];
    int rejectionCounts[REJECT_REASON_COUNT];
    for (int index = 0; index < REJECT_REASON_COUNT; ++index)
        rejectionCounts[index] = 0;
    values[0] = referenceValue;
    variances[0] = referenceFrozen;
    reliabilities[0] = 1.0;
    int observationCount = 1;
    uint eligibleCount = 0u;
    uint rejectedCount = 0u;

    for (int frameIndex = 1; frameIndex < uFrameCount; ++frameIndex) {
        vec2 sourceRaw;
        vec3 warpCovariance;
        float alignmentConfidence = 0.0;
        uint rejection = REJECT_CANDIDATE_UNAVAILABLE;
        if (EvaluateMotion(frameIndex, vec2(pixel), sourceRaw,
                           warpCovariance, alignmentConfidence)) {
            float candidateValue = 0.0;
            float candidateGate = 0.0;
            float candidateFusion = 0.0;
            float candidateDark = 0.0;
            float candidateDnStep = 0.0;
            if (SampleSameCfa(frameIndex, sourceRaw, site, referenceValue,
                    warpCovariance, candidateValue, candidateGate,
                    candidateFusion, candidateDark, candidateDnStep)) {
                FrameParameters candidateFrame = frames[frameIndex];
                int noiseQuality = int(round(candidateFrame.exposureQuality.w));
                ivec2 reliabilityPixel = min(pixel / 2,
                    ivec2(candidateFrame.sourceReliability.zw) - ivec2(1));
                float cellReliability = imageLoad(
                    uReliability, ivec3(reliabilityPixel, frameIndex)).r;
                float reliability = clamp(
                    cellReliability * alignmentConfidence, 0.0, 1.0);
                float divisionFloor = max(uNumericalFloor,
                    candidateDnStep * candidateDnStep / 12.0);
                float frozenVariance = max(candidateFusion, divisionFloor);
                bool validCandidate = noiseQuality != 4 &&
                    Finite(candidateValue) && Finite(candidateGate) &&
                    candidateGate > 0.0 && Finite(candidateFusion) &&
                    candidateFusion > 0.0 && Finite(candidateDark) &&
                    candidateDark >= 0.0 && Finite(reliability) &&
                    reliability >= 0.0 && reliability <= 1.0 &&
                    Finite(candidateFrame.exposureQuality.z) &&
                    candidateFrame.exposureQuality.z >= 0.0 &&
                    candidateFrame.exposureQuality.z <= 1.0 &&
                    Finite(frozenVariance) && frozenVariance > 0.0;
                if (!validCandidate) {
                    rejection = REJECT_INVALID_VARIANCE;
                } else if (reliability <= 0.0) {
                    rejection = REJECT_RELIABILITY_ZERO;
                } else {
                    float difference = candidateValue - referenceValue;
                    float gateVariance = referenceGate + candidateGate +
                        uNumericalFloor;
                    float standardized = difference / sqrt(gateVariance);
                    bool trusted = noiseQuality == 0 || noiseQuality == 1;
                    float inner = trusted ? uTrustedFullSigma : uLowConfidenceFullSigma;
                    float outer = trusted ? uTrustedZeroSigma : uLowConfidenceZeroSigma;
                    float pixelGate = FlatTopGate(standardized, inner, outer);
                    if (pixelGate <= 0.0) {
                        rejection = REJECT_PIXEL_OUTLIER;
                    } else {
                        bool absolutePassed = true;
                        if (!trusted) {
                            float absoluteTerm = max(
                                uAbsoluteDnMultiplier *
                                    (referenceDnStep + candidateDnStep),
                                uAbsoluteNoiseMultiplier *
                                    sqrt(referenceDark + candidateDark));
                            float absoluteLimit = absoluteTerm +
                                uAbsoluteRelativeFraction * max(
                                    abs(referenceValue), abs(candidateValue));
                            absolutePassed = abs(difference) <= absoluteLimit;
                        }
                        if (!absolutePassed) {
                            rejection = REJECT_ABSOLUTE_SAFETY;
                        } else {
                            float gate = clamp(reliability * pixelGate, 0.0, 1.0);
                            float resolvedReliability = pow(gate, 0.25) *
                                candidateFrame.exposureQuality.z;
                            if (!Finite(resolvedReliability) ||
                                resolvedReliability <= 0.0) {
                                rejection = REJECT_RELIABILITY_ZERO;
                            } else {
                                values[observationCount] = candidateValue;
                                variances[observationCount] = frozenVariance;
                                reliabilities[observationCount] = resolvedReliability;
                                ++observationCount;
                                ++eligibleCount;
                                continue;
                            }
                        }
                    }
                }
            }
        }
        ++rejectionCounts[int(rejection)];
        ++rejectedCount;
    }

    uint dominant = DominantReject(rejectionCounts);
    if (observationCount < 2) {
        WriteExact(pixel, referenceNormalized, referenceFusion,
            DECISION_NO_VALID_CANDIDATE, dominant, rejectedCount);
        return;
    }

    float current = referenceValue;
    float numerator = 0.0;
    float totalPrecision = 0.0;
    float precisionSquares = 0.0;
    float policyNumerator = 0.0;
    bool solveValid = true;
    for (int iteration = 0; iteration < uHuberIterations; ++iteration) {
        numerator = 0.0;
        totalPrecision = 0.0;
        precisionSquares = 0.0;
        policyNumerator = 0.0;
        for (int index = 0; index < observationCount; ++index) {
            float residual = (values[index] - current) / sqrt(variances[index]);
            float magnitude = abs(residual);
            float robust = magnitude <= uHuberThreshold || magnitude == 0.0
                ? 1.0 : uHuberThreshold / magnitude;
            float resolved = reliabilities[index] * robust;
            float precisionTerm = resolved / variances[index];
            numerator += precisionTerm * values[index];
            totalPrecision += precisionTerm;
            precisionSquares += precisionTerm * precisionTerm;
            policyNumerator += resolved * resolved / variances[index];
        }
        if (!Finite(numerator) || !Finite(totalPrecision) ||
            totalPrecision <= 0.0) {
            solveValid = false;
            break;
        }
        float nextValue = numerator / totalPrecision;
        float tolerance = uAbsoluteHuberTolerance +
            uRelativeHuberTolerance * max(abs(current), abs(nextValue));
        float change = abs(nextValue - current);
        current = nextValue;
        if (change <= tolerance) break;
    }
    numerator = 0.0;
    totalPrecision = 0.0;
    precisionSquares = 0.0;
    policyNumerator = 0.0;
    for (int index = 0; index < observationCount && solveValid; ++index) {
        float residual = (values[index] - current) / sqrt(variances[index]);
        float magnitude = abs(residual);
        float robust = magnitude <= uHuberThreshold || magnitude == 0.0
            ? 1.0 : uHuberThreshold / magnitude;
        robustFactors[index] = robust;
        float resolved = reliabilities[index] * robust;
        float precisionTerm = resolved / variances[index];
        numerator += precisionTerm * values[index];
        totalPrecision += precisionTerm;
        precisionSquares += precisionTerm * precisionTerm;
        policyNumerator += resolved * resolved / variances[index];
    }
    if (!solveValid || !Finite(totalPrecision) || totalPrecision <= 0.0 ||
        !Finite(precisionSquares) || precisionSquares <= 0.0) {
        WriteExact(pixel, referenceNormalized, referenceFusion,
            DECISION_NUMERICAL_FALLBACK, 0u, 0u);
        return;
    }

    float modelVariance = 1.0 / totalPrecision;
    float policyVariance = policyNumerator /
        (totalPrecision * totalPrecision);
    float effectiveSupport = totalPrecision * totalPrecision /
        precisionSquares;
    float bread = 0.0;
    float meat = 0.0;
    for (int index = 0; index < observationCount; ++index) {
        float residual = (values[index] - current) / sqrt(variances[index]);
        if (abs(residual) <= uHuberThreshold)
            bread += reliabilities[index] / variances[index];
        float psi = clamp(residual, -uHuberThreshold, uHuberThreshold);
        float score = reliabilities[index] / sqrt(variances[index]) * psi;
        meat += score * score;
    }
    float finiteVariance = modelVariance;
    bool finiteAvailable = bread > 0.0 && effectiveSupport > 1.0;
    if (finiteAvailable) {
        finiteVariance = meat / (bread * bread) *
            (effectiveSupport / (effectiveSupport - 1.0));
        finiteAvailable = Finite(finiteVariance) && finiteVariance >= 0.0;
        if (!finiteAvailable) finiteVariance = modelVariance;
    }
    float publishedVariance = max(modelVariance, policyVariance);
    if (finiteAvailable) publishedVariance = max(publishedVariance, finiteVariance);

    float totalWeight = 0.0;
    float referenceWeight = 0.0;
    float alternateWeight = 0.0;
    float robustWeighted = 0.0;
    float baseWeight = 0.0;
    float ownerWeight = -1.0;
    uint ownerIndex = 0u;
    for (int index = 0; index < observationCount; ++index) {
        float base = reliabilities[index] / variances[index];
        float weight = base * robustFactors[index];
        baseWeight += base;
        robustWeighted += weight;
        totalWeight += weight;
        if (index == 0) referenceWeight = weight;
        else alternateWeight += weight;
        if (weight > ownerWeight) {
            ownerWeight = weight;
            ownerIndex = uint(index);
        }
    }
    if (!Finite(totalWeight) || totalWeight <= 0.0 ||
        !Finite(referenceWeight) || referenceWeight <= 0.0 ||
        alternateWeight / referenceWeight < uExactFallbackRatio) {
        WriteExact(pixel, referenceNormalized, referenceFusion,
            DECISION_ALTERNATE_WEIGHT_INSUFFICIENT, 0u, 0u);
        return;
    }
    float normalized = current / referenceGain;
    if (!Finite(normalized) || !Finite(modelVariance) ||
        !Finite(policyVariance) || !Finite(publishedVariance) ||
        !Finite(effectiveSupport) || effectiveSupport <= 0.0) {
        WriteExact(pixel, referenceNormalized, referenceFusion,
            DECISION_NUMERICAL_FALLBACK, 0u, 0u);
        return;
    }
    WriteResult(pixel, normalized, referenceWeight, alternateWeight,
        alternateWeight / referenceWeight, effectiveSupport, publishedVariance,
        clamp(ownerWeight / totalWeight, 0.0, 1.0),
        baseWeight > 0.0 ? clamp(robustWeighted / baseWeight, 0.0, 1.0) : 0.0,
        modelVariance, policyVariance, finiteVariance,
        DECISION_NONE, dominant, FLAG_REFERENCE_INCLUDED,
        eligibleCount, uint(observationCount - 1), rejectedCount,
        uint(observationCount), ownerIndex);
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

bool ValidFrame(
    const GpuSharedBurstFrameView& frame,
    std::size_t pixelCount) {
    if (!frame.normalized || !frame.comparisonGain || !frame.sampleFlags ||
        frame.explicitVariance || frame.normalized->size() != pixelCount ||
        frame.comparisonGain->size() != pixelCount ||
        frame.sampleFlags->size() != pixelCount ||
        frame.noiseQuality == NoiseModelQuality::Unavailable ||
        frame.cfaPattern == CfaPattern::Unknown) {
        return false;
    }
    for (std::size_t site = 0u; site < 4u; ++site) {
        if (!ValidateSiteNoiseProfile(frame.noise[site], nullptr) ||
            !std::isfinite(frame.usableCodeSpanDn[site]) ||
            frame.usableCodeSpanDn[site] <= 0.0) {
            return false;
        }
    }
    return true;
}

bool ValidateRequest(
    const GpuSharedBurstRequest& request,
    std::string& error) {
    if (request.width == 0u || request.height == 0u ||
        request.alternates.empty() ||
        request.alternates.size() + 1u > kMaximumFrames ||
        !ValidateParameters(request.parameters, nullptr) ||
        !ValidateSharedBurstSettings(request.settings, nullptr)) {
        error = "The OpenGL Shared Burst request is incomplete.";
        return false;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(request.width) *
        static_cast<std::size_t>(request.height);
    if (pixelCount / request.width != request.height ||
        !ValidFrame(request.reference, pixelCount)) {
        error = "The OpenGL Shared Burst reference violates its prepared-frame contract.";
        return false;
    }
    for (const GpuSharedBurstAlternateView& alternate : request.alternates) {
        const std::uint64_t expectedReliabilityWidth =
            (request.width + 1u) / 2u;
        const std::uint64_t expectedReliabilityHeight =
            (request.height + 1u) / 2u;
        const std::uint64_t reliabilityCount =
            alternate.reliabilityExtent.width * alternate.reliabilityExtent.height;
        if (!ValidFrame(alternate.frame, pixelCount) || !alternate.motion ||
            !alternate.motion->valid || !alternate.reliability ||
            alternate.reliabilityExtent.width == 0u ||
            alternate.reliabilityExtent.height == 0u ||
            alternate.reliabilityExtent.width > expectedReliabilityWidth ||
            alternate.reliabilityExtent.height > expectedReliabilityHeight ||
            reliabilityCount != alternate.reliability->size() ||
            alternate.motion->sourceRawExtent.width != request.width ||
            alternate.motion->sourceRawExtent.height != request.height ||
            !std::isfinite(alternate.exposureScale) ||
            alternate.exposureScale <= 0.0 ||
            !std::isfinite(alternate.exposureScaleVariance) ||
            alternate.exposureScaleVariance < 0.0 ||
            !std::isfinite(alternate.trustAttenuation) ||
            alternate.trustAttenuation < 0.0 ||
            alternate.trustAttenuation > 1.0) {
            error = "An OpenGL Shared Burst alternate violates its prepared evidence contract.";
            return false;
        }
    }
    return true;
}

} // namespace

bool FuseSharedBurstOpenGl(
    const GpuSharedBurstRequest& request,
    GpuSharedBurstOutput& output,
    std::string& error) {
    output = {};
    error.clear();
    if (!ValidateRequest(request, error)) return false;
    if (Canceled(request)) {
        error = "Shared Burst GPU fusion was canceled.";
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
    const std::size_t frameCount = request.alternates.size() + 1u;
    if (major < 4 || (major == 4 && minor < 3) ||
        request.width > static_cast<std::uint32_t>(maximumTextureSize) ||
        request.height > static_cast<std::uint32_t>(maximumTextureSize) ||
        frameCount > static_cast<std::size_t>(maximumLayers) ||
        maximumImageUnits < 8) {
        error = "OpenGL 4.3 image compute cannot represent this Shared Burst working set.";
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
    const auto restoreState = [&]() {
        glUseProgram(static_cast<GLuint>(std::max(0, previousProgram)));
        glBindTexture(GL_TEXTURE_2D,
            static_cast<GLuint>(std::max(0, previousTexture2D)));
        glBindTexture(GL_TEXTURE_2D_ARRAY,
            static_cast<GLuint>(std::max(0, previousTextureArray)));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLuint>(std::max(0, previousSsbo)));
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    };

    GlResources resources;
    resources.program = GLHelpers::CreateComputeProgram(kSharedBurstShader);
    if (resources.program == 0u) {
        error = "The Shared Burst OpenGL compute shader did not compile.";
        restoreState();
        return false;
    }

    const GLsizei width = static_cast<GLsizei>(request.width);
    const GLsizei height = static_cast<GLsizei>(request.height);
    const GLsizei layers = static_cast<GLsizei>(frameCount);
    const std::uint32_t reliabilityWidth = static_cast<std::uint32_t>(
        (request.width + 1u) / 2u);
    const std::uint32_t reliabilityHeight = static_cast<std::uint32_t>(
        (request.height + 1u) / 2u);
    resources.inputTextures[0] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R32F, width, height, layers, error);
    resources.inputTextures[1] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R32F, width, height, layers, error);
    resources.inputTextures[2] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R8UI, width, height, layers, error);
    resources.inputTextures[3] = CreateTextureStorage(
        GL_TEXTURE_2D_ARRAY, GL_R32F,
        static_cast<GLsizei>(reliabilityWidth),
        static_cast<GLsizei>(reliabilityHeight), layers, error);
    resources.outputTextures[0] = CreateTextureStorage(
        GL_TEXTURE_2D, GL_RGBA32F, width, height, 1, error);
    resources.outputTextures[1] = CreateTextureStorage(
        GL_TEXTURE_2D, GL_RGBA32F, width, height, 1, error);
    resources.outputTextures[2] = CreateTextureStorage(
        GL_TEXTURE_2D, GL_RGBA32F, width, height, 1, error);
    resources.outputTextures[3] = CreateTextureStorage(
        GL_TEXTURE_2D, GL_RGBA32UI, width, height, 1, error);
    if (std::any_of(resources.inputTextures.begin(), resources.inputTextures.end(),
            [](GLuint texture) { return texture == 0u; }) ||
        std::any_of(resources.outputTextures.begin(), resources.outputTextures.end(),
            [](GLuint texture) { return texture == 0u; })) {
        if (error.empty())
            error = "The GPU could not allocate the Shared Burst working textures.";
        restoreState();
        return false;
    }

    std::vector<GpuFrameParameters> gpuFrames(frameCount);
    std::vector<GpuMotionNode> gpuNodes;
    const std::size_t reliabilityCount = static_cast<std::size_t>(
        reliabilityWidth) * reliabilityHeight;
    std::vector<float> zeroReliability(reliabilityCount, 0.0f);
    const auto uploadFrame = [&](std::size_t index,
                                 const GpuSharedBurstFrameView& frame,
                                 const std::vector<float>& reliability) {
        return UploadLayer(resources.inputTextures[0], static_cast<GLint>(index),
                   width, height, GL_RED, GL_FLOAT, frame.normalized->data(), error) &&
            UploadLayer(resources.inputTextures[1], static_cast<GLint>(index),
                   width, height, GL_RED, GL_FLOAT,
                   frame.comparisonGain->data(), error) &&
            UploadLayer(resources.inputTextures[2], static_cast<GLint>(index),
                   width, height, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                   frame.sampleFlags->data(), error) &&
            UploadLayer(resources.inputTextures[3], static_cast<GLint>(index),
                   static_cast<GLsizei>(reliabilityWidth),
                   static_cast<GLsizei>(reliabilityHeight),
                   GL_RED, GL_FLOAT, reliability.data(), error);
    };
    if (!uploadFrame(0u, request.reference, zeroReliability)) {
        restoreState();
        return false;
    }
    const auto fillFrameNoise = [](const GpuSharedBurstFrameView& frame,
                                   GpuFrameParameters& gpu) {
        for (std::size_t site = 0u; site < 4u; ++site) {
            gpu.usableSpan[site] = static_cast<float>(frame.usableCodeSpanDn[site]);
            gpu.shot[site] = static_cast<float>(frame.noise[site].shotScale);
            gpu.offset[site] = static_cast<float>(frame.noise[site].offsetVariance);
            gpu.tau0[site] = static_cast<float>(frame.noise[site].residualModelTau0);
            gpu.tau1[site] = static_cast<float>(frame.noise[site].residualModelTau1);
        }
        gpu.exposureQuality[3] = static_cast<float>(frame.noiseQuality);
        gpu.misc[0] = static_cast<float>(frame.cfaPattern);
    };
    fillFrameNoise(request.reference, gpuFrames[0]);
    gpuFrames[0].exposureQuality[0] = 1.0f;
    gpuFrames[0].exposureQuality[2] = 1.0f;

    for (std::size_t alternateIndex = 0u;
         alternateIndex < request.alternates.size(); ++alternateIndex) {
        if (Canceled(request)) {
            error = "Shared Burst GPU upload was canceled.";
            restoreState();
            return false;
        }
        const std::size_t frameIndex = alternateIndex + 1u;
        const GpuSharedBurstAlternateView& alternate =
            request.alternates[alternateIndex];
        std::vector<float> reliability(reliabilityCount, 0.0f);
        for (std::uint32_t y = 0u;
             y < static_cast<std::uint32_t>(alternate.reliabilityExtent.height);
             ++y) {
            const std::size_t source = static_cast<std::size_t>(
                y * alternate.reliabilityExtent.width);
            const std::size_t destination = static_cast<std::size_t>(
                y * reliabilityWidth);
            std::copy_n(alternate.reliability->begin() + source,
                static_cast<std::size_t>(alternate.reliabilityExtent.width),
                reliability.begin() + destination);
        }
        if (!uploadFrame(frameIndex, alternate.frame, reliability)) {
            restoreState();
            return false;
        }
        GpuFrameParameters& gpu = gpuFrames[frameIndex];
        fillFrameNoise(alternate.frame, gpu);
        gpu.exposureQuality[0] = static_cast<float>(alternate.exposureScale);
        gpu.exposureQuality[1] =
            static_cast<float>(alternate.exposureScaleVariance);
        gpu.exposureQuality[2] = static_cast<float>(alternate.trustAttenuation);
        const LocalMotionGrid& motion = *alternate.motion;
        gpu.motionOriginSpacing = {
            static_cast<float>(motion.originRawX),
            static_cast<float>(motion.originRawY),
            static_cast<float>(motion.spacingRawX),
            static_cast<float>(motion.spacingRawY) };
        gpu.motionShapeOffset = {
            static_cast<std::int32_t>(motion.width),
            static_cast<std::int32_t>(motion.height),
            static_cast<std::int32_t>(gpuNodes.size()), 0 };
        gpu.affine0 = {
            static_cast<float>(motion.globalWarp.linear[0]),
            static_cast<float>(motion.globalWarp.linear[1]),
            static_cast<float>(motion.globalWarp.linear[2]),
            static_cast<float>(motion.globalWarp.linear[3]) };
        gpu.affine1 = {
            static_cast<float>(motion.globalWarp.centerRaw.x),
            static_cast<float>(motion.globalWarp.centerRaw.y),
            static_cast<float>(motion.globalWarp.translationRaw.x),
            static_cast<float>(motion.globalWarp.translationRaw.y) };
        gpu.sourceReliability = {
            static_cast<float>(motion.sourceRawExtent.width),
            static_cast<float>(motion.sourceRawExtent.height),
            static_cast<float>(alternate.reliabilityExtent.width),
            static_cast<float>(alternate.reliabilityExtent.height) };
        for (const MotionNode& node : motion.nodes) {
            GpuMotionNode packed;
            packed.residualConfidenceValid = {
                static_cast<float>(node.residualRaw.x),
                static_cast<float>(node.residualRaw.y),
                static_cast<float>(node.confidence),
                node.state == MotionNodeState::Rejected ? 0.0f : 1.0f };
            packed.covariance = {
                static_cast<float>(node.covarianceRaw.xxRawPixelsSquared),
                static_cast<float>(node.covarianceRaw.xyRawPixelsSquared),
                static_cast<float>(node.covarianceRaw.yyRawPixelsSquared), 0.0f };
            gpuNodes.push_back(packed);
        }
    }
    if (gpuNodes.empty()) {
        error = "Shared Burst GPU motion evidence is empty.";
        restoreState();
        return false;
    }

    glGenBuffers(1, &resources.frameParameters);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.frameParameters);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(gpuFrames.size() * sizeof(GpuFrameParameters)),
        gpuFrames.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &resources.motionNodes);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.motionNodes);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(gpuNodes.size() * sizeof(GpuMotionNode)),
        gpuNodes.data(), GL_STATIC_DRAW);
    if (!CheckGl("Shared Burst compute buffer allocation", error)) {
        restoreState();
        return false;
    }

    glUseProgram(resources.program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, resources.frameParameters);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, resources.motionNodes);
    glBindImageTexture(0, resources.inputTextures[0], 0, GL_TRUE, 0,
        GL_READ_ONLY, GL_R32F);
    glBindImageTexture(1, resources.inputTextures[1], 0, GL_TRUE, 0,
        GL_READ_ONLY, GL_R32F);
    glBindImageTexture(2, resources.inputTextures[2], 0, GL_TRUE, 0,
        GL_READ_ONLY, GL_R8UI);
    glBindImageTexture(3, resources.inputTextures[3], 0, GL_TRUE, 0,
        GL_READ_ONLY, GL_R32F);
    for (GLuint unit = 4u; unit <= 6u; ++unit)
        glBindImageTexture(unit, resources.outputTextures[unit - 4u], 0,
            GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(7, resources.outputTextures[3], 0,
        GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32UI);

    glUniform2i(glGetUniformLocation(resources.program, "uExtent"), width, height);
    SetUniform1i(resources.program, "uFrameCount", layers);
    SetUniform1f(resources.program, "uNumericalFloor",
        static_cast<float>(request.parameters.fusion.numericalVarianceFloor));
    SetUniform1f(resources.program, "uMaximumComparisonGain",
        static_cast<float>(request.parameters.radiometric.maximumComparisonGain));
    SetUniform1f(resources.program, "uInterpolationEpsilon",
        static_cast<float>(request.parameters.registration.interpolationWeightEpsilon));
    SetUniform1f(resources.program, "uMotionDisagreementScale",
        static_cast<float>(request.parameters.registration.motionDisagreementConfidenceScaleRawPixels));
    SetUniform1f(resources.program, "uMotionDisagreementLimit",
        static_cast<float>(request.parameters.registration.motionDisagreementHardLimitRawPixels));
    SetUniform1f(resources.program, "uWarpCovarianceMinimum",
        static_cast<float>(request.parameters.registration.warpCovarianceEigenvalueMinRawPixels));
    SetUniform1f(resources.program, "uWarpCovarianceMaximum",
        static_cast<float>(request.parameters.registration.warpCovarianceEigenvalueMaxRawPixels));
    SetUniform1f(resources.program, "uTrustedFullSigma",
        static_cast<float>(request.parameters.reliability.trustedPixelFullWeightSigma));
    SetUniform1f(resources.program, "uTrustedZeroSigma",
        static_cast<float>(request.parameters.reliability.trustedPixelZeroWeightSigma));
    SetUniform1f(resources.program, "uLowConfidenceFullSigma",
        static_cast<float>(request.parameters.reliability.lowConfidencePixelFullWeightSigma));
    SetUniform1f(resources.program, "uLowConfidenceZeroSigma",
        static_cast<float>(request.parameters.reliability.lowConfidencePixelZeroWeightSigma));
    SetUniform1f(resources.program, "uAbsoluteDnMultiplier",
        static_cast<float>(request.parameters.reliability.absoluteSafetyDnMultiplier));
    SetUniform1f(resources.program, "uAbsoluteNoiseMultiplier",
        static_cast<float>(request.parameters.reliability.absoluteSafetyNoiseSigmaMultiplier));
    SetUniform1f(resources.program, "uAbsoluteRelativeFraction",
        static_cast<float>(request.parameters.reliability.absoluteSafetyRelativeFraction));
    SetUniform1f(resources.program, "uExactFallbackRatio",
        static_cast<float>(request.parameters.fusion.exactFallbackAlternateToReferenceRatio));
    SetUniform1f(resources.program, "uHuberThreshold",
        static_cast<float>(request.settings.huberThreshold));
    SetUniform1i(resources.program, "uHuberIterations",
        static_cast<GLint>(request.settings.maximumHuberIterations));
    SetUniform1f(resources.program, "uAbsoluteHuberTolerance",
        static_cast<float>(request.settings.absoluteHuberTolerance));
    SetUniform1f(resources.program, "uRelativeHuberTolerance",
        static_cast<float>(request.settings.relativeHuberTolerance));

    GpuSharedBurstDiagnostics diagnostics;
    diagnostics.deviceIdentity = deviceIdentity;
    const std::uint32_t tileSize = std::clamp(
        request.tileRawPixels, 64u, kMaximumDispatchTile);
    const std::uint32_t tileColumns = (request.width + tileSize - 1u) / tileSize;
    const std::uint32_t tileRows = (request.height + tileSize - 1u) / tileSize;
    const std::uint32_t tileCount = tileColumns * tileRows;
    for (std::uint32_t tileY = 0u; tileY < tileRows; ++tileY) {
        for (std::uint32_t tileX = 0u; tileX < tileColumns; ++tileX) {
            if (Canceled(request)) {
                error = "Shared Burst GPU fusion was canceled.";
                restoreState();
                return false;
            }
            const std::uint32_t originX = tileX * tileSize;
            const std::uint32_t originY = tileY * tileSize;
            const std::uint32_t tileWidth =
                std::min(tileSize, request.width - originX);
            const std::uint32_t tileHeight =
                std::min(tileSize, request.height - originY);
            glUniform2i(glGetUniformLocation(resources.program, "uTileOrigin"),
                static_cast<GLint>(originX), static_cast<GLint>(originY));
            glUniform2i(glGetUniformLocation(resources.program, "uTileExtent"),
                static_cast<GLint>(tileWidth), static_cast<GLint>(tileHeight));
            glDispatchCompute(
                (tileWidth + kWorkgroupSize - 1u) / kWorkgroupSize,
                (tileHeight + kWorkgroupSize - 1u) / kWorkgroupSize, 1u);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            if (!CheckGl("Shared Burst compute dispatch", error)) {
                restoreState();
                return false;
            }
            ++diagnostics.dispatchedTileCount;
            if (request.reportProgress) {
                request.reportProgress(
                    static_cast<double>(diagnostics.dispatchedTileCount) /
                    std::max(1u, tileCount));
            }
        }
    }

    const std::size_t pixelCount = static_cast<std::size_t>(request.width) *
        static_cast<std::size_t>(request.height);
    std::array<std::vector<float>, 3> floatOutput;
    std::vector<std::uint32_t> meta;
    try {
        for (auto& plane : floatOutput) plane.resize(pixelCount * 4u);
        meta.resize(pixelCount * 4u);
    } catch (const std::bad_alloc&) {
        error = "Host memory could not hold the Shared Burst GPU readback.";
        restoreState();
        return false;
    }
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT |
        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    for (std::size_t index = 0u; index < floatOutput.size(); ++index) {
        glBindTexture(GL_TEXTURE_2D, resources.outputTextures[index]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
            floatOutput[index].data());
    }
    glBindTexture(GL_TEXTURE_2D, resources.outputTextures[3]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT,
        meta.data());
    if (!CheckGl("Shared Burst result readback", error)) {
        restoreState();
        return false;
    }
    if (Canceled(request)) {
        error = "Shared Burst GPU readback was canceled.";
        restoreState();
        return false;
    }

    GpuSharedBurstOutput completed;
    try {
        completed.normalizedMosaic.resize(pixelCount);
        completed.diagnostics.resize(pixelCount);
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel) {
            const std::size_t component = pixel * 4u;
            const float normalized = floatOutput[0][component];
            FusionPixelDiagnostics diagnostic;
            diagnostic.referenceWeight = floatOutput[0][component + 1u];
            diagnostic.alternateWeight = floatOutput[0][component + 2u];
            diagnostic.alternateToReferenceWeightRatio =
                floatOutput[0][component + 3u];
            diagnostic.effectiveSampleCount = floatOutput[1][component];
            diagnostic.outputVarianceComparisonDomain =
                floatOutput[1][component + 1u];
            diagnostic.ownerContribution = floatOutput[1][component + 2u];
            diagnostic.robustAttenuation = floatOutput[1][component + 3u];
            diagnostic.modelQuadraticVariance = floatOutput[2][component];
            diagnostic.policyConditionalSamplingVariance =
                floatOutput[2][component + 1u];
            diagnostic.finiteSampleRobustVariance =
                floatOutput[2][component + 2u];
            const std::uint32_t identity = meta[component];
            const std::uint32_t counts = meta[component + 1u];
            const std::uint32_t supportOwner = meta[component + 2u];
            diagnostic.decisionReason = static_cast<DecisionReason>(
                identity & 0xffu);
            diagnostic.dominantRejectionReason =
                static_cast<FusionRejectReason>((identity >> 8u) & 0xffu);
            const std::uint32_t flags = (identity >> 16u) & 0xffu;
            diagnostic.exactReferenceCopy = (flags & 1u) != 0u;
            diagnostic.referenceIncluded = (flags & 2u) != 0u;
            diagnostic.eligibleAlternateCount = static_cast<std::uint16_t>(
                counts & 0xffu);
            diagnostic.contributingAlternateCount = static_cast<std::uint16_t>(
                (counts >> 8u) & 0xffu);
            diagnostic.rejectedAlternateCount = static_cast<std::uint16_t>(
                (counts >> 16u) & 0xffu);
            diagnostic.rawSupportCount = static_cast<std::uint16_t>(
                supportOwner & 0xffffu);
            diagnostic.ownerSourceIndex = static_cast<std::uint16_t>(
                (supportOwner >> 16u) & 0xffffu);
            if (!std::isfinite(normalized) ||
                !std::isfinite(diagnostic.outputVarianceComparisonDomain) ||
                diagnostic.outputVarianceComparisonDomain < 0.0 ||
                !std::isfinite(diagnostic.effectiveSampleCount) ||
                diagnostic.effectiveSampleCount <= 0.0) {
                error = "Shared Burst GPU readback contains invalid numerical output.";
                restoreState();
                return false;
            }
            completed.normalizedMosaic[pixel] = normalized;
            completed.diagnostics[pixel] = diagnostic;
        }
        completed.gpu = std::move(diagnostics);
    } catch (const std::bad_alloc&) {
        error = "Host memory could not publish the verified Shared Burst GPU result.";
        restoreState();
        return false;
    }

    restoreState();
    output = std::move(completed);
    return true;
}

} // namespace Raw::Mfd
