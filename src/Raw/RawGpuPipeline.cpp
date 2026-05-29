#include "RawGpuPipeline.h"

#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>

namespace Raw {
namespace {

constexpr const char* kRawVertexShader = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 vUv;
void main() {
    vUv = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kRawDevelopShader = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 FragColor;

uniform usampler2D uRaw;
uniform sampler2D uCorrectedRaw;
uniform int uUseCorrectedRaw;
uniform ivec2 uRawSize;
uniform ivec2 uVisibleSize;
uniform ivec2 uCropOrigin;
uniform int uOrientation;
uniform int uRotateToFitFrame;
uniform int uCfaPattern;
uniform float uBlackLevel;
uniform vec4 uChannelBlack;
uniform float uWhiteLevel;
uniform vec3 uWhiteBalance;
uniform mat3 uCameraToWorking;
uniform int uUseCameraTransform;
uniform int uDebugView;
uniform float uExposure;
uniform int uHighlightMode;
uniform float uHighlightStrength;
uniform float uHighlightThreshold;
uniform int uMosaicDenoiseEnabled;
uniform int uMosaicHotPixelSuppression;
uniform float uMosaicHotPixelThreshold;
uniform float uMosaicLumaStrength;
uniform float uMosaicChromaStrength;
uniform int uMosaicRadius;
uniform float uMosaicEdgeProtection;
uniform int uMosaicIterations;

int cfaAt(ivec2 visibleP) {
    int x = visibleP.x & 1;
    int y = visibleP.y & 1;
    if (uCfaPattern == 1) { // RGGB
        if (y == 0 && x == 0) return 0;
        if (y == 1 && x == 1) return 2;
        return 1;
    }
    if (uCfaPattern == 2) { // BGGR
        if (y == 0 && x == 0) return 2;
        if (y == 1 && x == 1) return 0;
        return 1;
    }
    if (uCfaPattern == 3) { // GBRG
        if (y == 0 && x == 1) return 2;
        if (y == 1 && x == 0) return 0;
        return 1;
    }
    if (uCfaPattern == 4) { // GRBG
        if (y == 0 && x == 1) return 0;
        if (y == 1 && x == 0) return 2;
        return 1;
    }
    return 1;
}

float blackForColor(int color) {
    float channelBlack = uBlackLevel;
    if (color == 0) {
        channelBlack = uChannelBlack.r > 0.0 ? uChannelBlack.r : uBlackLevel;
    } else if (color == 1) {
        channelBlack = uChannelBlack.g > 0.0 ? uChannelBlack.g : uBlackLevel;
    } else if (color == 2) {
        channelBlack = uChannelBlack.b > 0.0 ? uChannelBlack.b : uBlackLevel;
    }
    return channelBlack;
}

float rawNormalizedAt(ivec2 rawP, ivec2 visibleP) {
    ivec2 q = clamp(rawP, ivec2(0), uRawSize - ivec2(1));
    if (uUseCorrectedRaw != 0) {
        return texelFetch(uCorrectedRaw, q, 0).r;
    }
    ivec2 visibleQ = clamp(visibleP, ivec2(0), uVisibleSize - ivec2(1));
    int color = cfaAt(visibleQ);
    float black = blackForColor(color);
    float white = max(black + 1.0, uWhiteLevel);
    float v = float(texelFetch(uRaw, q, 0).r);
    return (v - black) / max(1.0, white - black);
}

float sameCfaNeighborMean(ivec2 rawP, ivec2 visibleP, float center, int radius, out float neighborMin, out float neighborMax) {
    float sum = 0.0;
    float weightSum = 0.0;
    neighborMin = 1.0e20;
    neighborMax = -1.0e20;
    float edge = mix(0.85, 0.045, clamp(uMosaicEdgeProtection, 0.0, 1.0));
    int r = clamp(radius, 1, 4);
    for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
            if (x == 0 && y == 0) continue;
            if (abs(x) > r || abs(y) > r) continue;
            ivec2 stepOffset = ivec2(x * 2, y * 2);
            float v = rawNormalizedAt(rawP + stepOffset, visibleP + stepOffset);
            float spatial = exp(-float(x * x + y * y) * 0.18);
            float rangeW = exp(-abs(v - center) / max(0.0001, edge));
            float w = spatial * rangeW;
            sum += v * w;
            weightSum += w;
            neighborMin = min(neighborMin, v);
            neighborMax = max(neighborMax, v);
        }
    }
    if (weightSum <= 0.000001) {
        neighborMin = center;
        neighborMax = center;
        return center;
    }
    return sum / weightSum;
}

float hotPixelMaskAt(ivec2 rawP, ivec2 visibleP) {
    if (uMosaicHotPixelSuppression == 0) return 0.0;
    float center = rawNormalizedAt(rawP, visibleP);
    float neighborMin = center;
    float neighborMax = center;
    float mean = sameCfaNeighborMean(rawP, visibleP, center, 1, neighborMin, neighborMax);
    float threshold = max(0.0001, uMosaicHotPixelThreshold);
    float highOutlier = center > neighborMax + threshold ? 1.0 : 0.0;
    float lowOutlier = center < neighborMin - threshold ? 1.0 : 0.0;
    float meanOutlier = abs(center - mean) > threshold * 1.4 ? 1.0 : 0.0;
    return max(max(highOutlier, lowOutlier), meanOutlier);
}

float denoisedRawAt(ivec2 rawP, ivec2 visibleP) {
    float center = rawNormalizedAt(rawP, visibleP);
    if (uMosaicDenoiseEnabled == 0) {
        return center;
    }
    int color = cfaAt(clamp(visibleP, ivec2(0), uVisibleSize - ivec2(1)));
    float strength = color == 1 ? uMosaicLumaStrength : uMosaicChromaStrength;
    strength = clamp(strength, 0.0, 1.0);
    float value = center;
    int iterations = clamp(uMosaicIterations, 1, 2);
    for (int i = 0; i < 2; ++i) {
        if (i >= iterations) break;
        float neighborMin = value;
        float neighborMax = value;
        float mean = sameCfaNeighborMean(rawP, visibleP, value, uMosaicRadius, neighborMin, neighborMax);
        float cleaned = mean;
        if (uMosaicHotPixelSuppression != 0 && hotPixelMaskAt(rawP, visibleP) > 0.5) {
            cleaned = clamp(mean, neighborMin, neighborMax);
        }
        value = mix(value, cleaned, strength / float(i + 1));
    }
    return value;
}

float rawAt(ivec2 rawP, ivec2 visibleP) {
    return denoisedRawAt(rawP, visibleP);
}

vec3 clippedMaskAt(ivec2 rawP, ivec2 visibleP) {
    ivec2 q = clamp(rawP, ivec2(0), uRawSize - ivec2(1));
    ivec2 visibleQ = clamp(visibleP, ivec2(0), uVisibleSize - ivec2(1));
    int color = cfaAt(visibleQ);
    float black = blackForColor(color);
    float white = max(black + 1.0, uWhiteLevel);
    float threshold = mix(black, white, clamp(uHighlightThreshold, 0.0, 1.0));
    float clipped = float(texelFetch(uRaw, q, 0).r) >= threshold ? 1.0 : 0.0;
    if (color == 0) return vec3(clipped, 0.0, 0.0);
    if (color == 2) return vec3(0.0, 0.0, clipped);
    return vec3(0.0, clipped, 0.0);
}

vec3 cfaDebugColor(ivec2 visibleP) {
    int color = cfaAt(visibleP);
    if (color == 0) return vec3(1.0, 0.05, 0.05);
    if (color == 2) return vec3(0.05, 0.2, 1.0);
    return vec3(0.05, 1.0, 0.05);
}

vec3 bilinearDemosaic(ivec2 rawP, ivec2 visibleP) {
    float c = rawAt(rawP, visibleP);
    int color = cfaAt(visibleP);
    float l = rawAt(rawP + ivec2(-1, 0), visibleP + ivec2(-1, 0));
    float r = rawAt(rawP + ivec2( 1, 0), visibleP + ivec2( 1, 0));
    float u = rawAt(rawP + ivec2( 0,-1), visibleP + ivec2( 0,-1));
    float d = rawAt(rawP + ivec2( 0, 1), visibleP + ivec2( 0, 1));
    float ul = rawAt(rawP + ivec2(-1,-1), visibleP + ivec2(-1,-1));
    float ur = rawAt(rawP + ivec2( 1,-1), visibleP + ivec2( 1,-1));
    float dl = rawAt(rawP + ivec2(-1, 1), visibleP + ivec2(-1, 1));
    float dr = rawAt(rawP + ivec2( 1, 1), visibleP + ivec2( 1, 1));

    if (color == 0) {
        return vec3(c, (l + r + u + d) * 0.25, (ul + ur + dl + dr) * 0.25);
    }
    if (color == 2) {
        return vec3((ul + ur + dl + dr) * 0.25, (l + r + u + d) * 0.25, c);
    }

    bool horizontalRed =
        cfaAt(visibleP + ivec2(-1, 0)) == 0 ||
        cfaAt(visibleP + ivec2( 1, 0)) == 0;
    float red = horizontalRed ? (l + r) * 0.5 : (u + d) * 0.5;
    float blue = horizontalRed ? (u + d) * 0.5 : (l + r) * 0.5;
    return vec3(red, c, blue);
}

vec3 clippedDemosaicMask(ivec2 rawP, ivec2 visibleP) {
    vec3 c = clippedMaskAt(rawP, visibleP);
    int color = cfaAt(visibleP);
    vec3 l = clippedMaskAt(rawP + ivec2(-1, 0), visibleP + ivec2(-1, 0));
    vec3 r = clippedMaskAt(rawP + ivec2( 1, 0), visibleP + ivec2( 1, 0));
    vec3 u = clippedMaskAt(rawP + ivec2( 0,-1), visibleP + ivec2( 0,-1));
    vec3 d = clippedMaskAt(rawP + ivec2( 0, 1), visibleP + ivec2( 0, 1));
    vec3 ul = clippedMaskAt(rawP + ivec2(-1,-1), visibleP + ivec2(-1,-1));
    vec3 ur = clippedMaskAt(rawP + ivec2( 1,-1), visibleP + ivec2( 1,-1));
    vec3 dl = clippedMaskAt(rawP + ivec2(-1, 1), visibleP + ivec2(-1, 1));
    vec3 dr = clippedMaskAt(rawP + ivec2( 1, 1), visibleP + ivec2( 1, 1));

    if (color == 0) {
        return vec3(c.r, (l.g + r.g + u.g + d.g) * 0.25, (ul.b + ur.b + dl.b + dr.b) * 0.25);
    }
    if (color == 2) {
        return vec3((ul.r + ur.r + dl.r + dr.r) * 0.25, (l.g + r.g + u.g + d.g) * 0.25, c.b);
    }

    bool horizontalRed =
        cfaAt(visibleP + ivec2(-1, 0)) == 0 ||
        cfaAt(visibleP + ivec2( 1, 0)) == 0;
    float red = horizontalRed ? (l.r + r.r) * 0.5 : (u.r + d.r) * 0.5;
    float blue = horizontalRed ? (u.b + d.b) * 0.5 : (l.b + r.b) * 0.5;
    return vec3(red, c.g, blue);
}

vec3 reconstructHighlights(vec3 rgb, vec3 clipMask) {
    if (uHighlightMode == 0) {
        return rgb;
    }
    float clippedCount = clipMask.r + clipMask.g + clipMask.b;
    if (clippedCount <= 0.001) {
        return rgb;
    }

    float strength = clamp(uHighlightStrength, 0.0, 1.0);
    float luma = max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722)));
    vec3 neutral = vec3(luma);

    if (clippedCount >= 2.95 || uHighlightMode == 1) {
        return mix(rgb, neutral, strength);
    }

    vec3 repaired = rgb;
    float unclippedMean = 0.0;
    float unclippedCount = 0.0;
    if (clipMask.r < 0.5) { unclippedMean += rgb.r; unclippedCount += 1.0; }
    if (clipMask.g < 0.5) { unclippedMean += rgb.g; unclippedCount += 1.0; }
    if (clipMask.b < 0.5) { unclippedMean += rgb.b; unclippedCount += 1.0; }
    unclippedMean = unclippedCount > 0.0 ? unclippedMean / unclippedCount : luma;

    if (clipMask.r > 0.5) repaired.r = unclippedMean;
    if (clipMask.g > 0.5) repaired.g = unclippedMean;
    if (clipMask.b > 0.5) repaired.b = unclippedMean;

    if (uHighlightMode == 2) {
        repaired = mix(repaired, neutral, 0.35);
    }
    return mix(rgb, repaired, strength);
}

vec2 orientVisibleUv(vec2 uv) {
    vec2 q = clamp(uv, vec2(0.0), vec2(0.999999));
    if (uOrientation == 2) {
        return vec2(1.0 - q.x, q.y);
    }
    if (uOrientation == 3) {
        return vec2(1.0 - q.x, 1.0 - q.y);
    }
    if (uOrientation == 4) {
        return vec2(q.x, 1.0 - q.y);
    }
    if (uOrientation == 5) {
        return vec2(q.y, q.x);
    }
    if (uOrientation == 6) {
        return vec2(q.y, 1.0 - q.x);
    }
    if (uOrientation == 7) {
        return vec2(1.0 - q.y, 1.0 - q.x);
    }
    if (uOrientation == 8) {
        return vec2(1.0 - q.y, q.x);
    }
    return q;
}

void main() {
    vec2 visibleUv = orientVisibleUv(vUv);
    ivec2 visibleP = ivec2(clamp(visibleUv * vec2(uVisibleSize), vec2(0.0), vec2(uVisibleSize) - vec2(1.0)));
    ivec2 rawP = uCropOrigin + visibleP;
    if (uDebugView == 1) {
        float v = rawAt(rawP, visibleP);
        FragColor = vec4(vec3(v), 1.0);
        return;
    }
    if (uDebugView == 2) {
        FragColor = vec4(cfaDebugColor(visibleP), 1.0);
        return;
    }
    if (uDebugView == 6) {
        FragColor = vec4(clamp(clippedDemosaicMask(rawP, visibleP), 0.0, 1.0), 1.0);
        return;
    }
    if (uDebugView == 7) {
        float v = rawNormalizedAt(rawP, visibleP);
        FragColor = vec4(vec3(v), 1.0);
        return;
    }
    if (uDebugView == 8) {
        float v = denoisedRawAt(rawP, visibleP);
        FragColor = vec4(vec3(v), 1.0);
        return;
    }
    if (uDebugView == 9) {
        float mask = hotPixelMaskAt(rawP, visibleP);
        FragColor = vec4(mask, mask * 0.18, 0.0, 1.0);
        return;
    }
    if (uDebugView == 10) {
        float before = rawNormalizedAt(rawP, visibleP);
        float after = denoisedRawAt(rawP, visibleP);
        float diff = abs(after - before) * 8.0;
        FragColor = vec4(vec3(diff), 1.0);
        return;
    }

    vec3 rgb = bilinearDemosaic(rawP, visibleP);
    vec3 clipMask = clippedDemosaicMask(rawP, visibleP);
    rgb = reconstructHighlights(rgb, clipMask);
    if (uDebugView == 3) {
        FragColor = vec4(rgb, 1.0);
        return;
    }

    rgb *= uWhiteBalance;
    if (uDebugView == 4) {
        FragColor = vec4(rgb, 1.0);
        return;
    }

    if (uUseCameraTransform != 0) {
        rgb = uCameraToWorking * rgb;
    }
    if (uDebugView == 5) {
        FragColor = vec4(rgb, 1.0);
        return;
    }

    rgb *= uExposure;
    FragColor = vec4(rgb, 1.0);
}
)GLSL";

constexpr const char* kLinearDngShader = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 FragColor;

uniform sampler2D uLinearRgb;
uniform ivec2 uVisibleSize;
uniform int uOrientation;
uniform int uRotateToFitFrame;
uniform mat3 uCameraToWorking;
uniform int uUseCameraTransform;
uniform int uDebugView;
uniform float uExposure;

vec2 orientVisibleUv(vec2 uv) {
    vec2 q = clamp(uv, vec2(0.0), vec2(0.999999));
    if (uOrientation == 2) {
        return vec2(1.0 - q.x, q.y);
    }
    if (uOrientation == 3) {
        return vec2(1.0 - q.x, 1.0 - q.y);
    }
    if (uOrientation == 4) {
        return vec2(q.x, 1.0 - q.y);
    }
    if (uOrientation == 5) {
        return vec2(q.y, q.x);
    }
    if (uOrientation == 6) {
        return vec2(q.y, 1.0 - q.x);
    }
    if (uOrientation == 7) {
        return vec2(1.0 - q.y, 1.0 - q.x);
    }
    if (uOrientation == 8) {
        return vec2(1.0 - q.y, q.x);
    }
    return q;
}

void main() {
    vec2 uv = orientVisibleUv(vUv);
    vec3 rgb = texture(uLinearRgb, uv).rgb;
    if (uUseCameraTransform != 0) {
        rgb = uCameraToWorking * rgb;
    }
    rgb *= uExposure;
    FragColor = vec4(rgb, 1.0);
}
)GLSL";

std::size_t HashRawBuffer(const std::vector<std::uint16_t>& data) {
    std::size_t hash = 1469598103934665603ull;
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const std::size_t byteCount = data.size() * sizeof(std::uint16_t);
    for (std::size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<std::size_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
std::size_t HashBuffer(const std::vector<T>& data) {
    std::size_t hash = 1469598103934665603ull;
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const std::size_t byteCount = data.size() * sizeof(T);
    for (std::size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<std::size_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

void MixHash(std::size_t& hash, std::size_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
}

int PatternUniform(CfaPattern pattern) {
    switch (pattern) {
        case CfaPattern::RGGB: return 1;
        case CfaPattern::BGGR: return 2;
        case CfaPattern::GBRG: return 3;
        case CfaPattern::GRBG: return 4;
        case CfaPattern::Unknown:
        default:
            return 0;
    }
}

int DebugViewUniform(RawDebugView view) {
    switch (view) {
        case RawDebugView::FinalOutput: return 0;
        case RawDebugView::NormalizedMosaic: return 1;
        case RawDebugView::CfaFalseColor: return 2;
        case RawDebugView::DemosaicedCameraRgb: return 3;
        case RawDebugView::WhiteBalancedCameraRgb: return 4;
        case RawDebugView::CameraTransformedRgb: return 5;
        case RawDebugView::ClippedRawChannels: return 6;
        case RawDebugView::PreDenoiseMosaic: return 7;
        case RawDebugView::PostDenoiseMosaic: return 8;
        case RawDebugView::HotPixelMask: return 9;
        case RawDebugView::DenoiseDifference: return 10;
    }
    return 0;
}

std::array<float, 3> ResolveWhiteBalance(const RawMetadata& metadata, const RawDevelopSettings& settings) {
    if (settings.whiteBalanceMode == WhiteBalanceMode::Manual) {
        return settings.manualWhiteBalance;
    }
    if (settings.whiteBalanceMode == WhiteBalanceMode::Neutral) {
        return { 1.0f, 1.0f, 1.0f };
    }
    if (settings.whiteBalanceMode == WhiteBalanceMode::Auto) {
        std::array<float, 3> wb {
            std::max(0.001f, metadata.daylightWhiteBalance[0]),
            std::max(0.001f, metadata.daylightWhiteBalance[1]),
            std::max(0.001f, metadata.daylightWhiteBalance[2])
        };
        const float green = std::max(0.001f, wb[1]);
        return { wb[0] / green, 1.0f, wb[2] / green };
    }
    std::array<float, 3> wb {
        std::max(0.001f, metadata.cameraWhiteBalance[0]),
        std::max(0.001f, metadata.cameraWhiteBalance[1]),
        std::max(0.001f, metadata.cameraWhiteBalance[2])
    };
    const float green = std::max(0.001f, wb[1]);
    return { wb[0] / green, 1.0f, wb[2] / green };
}

std::array<float, 9> IdentityMatrix3() {
    return {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
}

std::array<float, 9> MultiplyMatrix3(const std::array<float, 9>& a, const std::array<float, 9>& b) {
    std::array<float, 9> result {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k) {
                v += a[static_cast<std::size_t>(r * 3 + k)] * b[static_cast<std::size_t>(k * 3 + c)];
            }
            result[static_cast<std::size_t>(r * 3 + c)] = v;
        }
    }
    return result;
}

std::array<float, 9> LerpMatrix3(const std::array<float, 9>& a, const std::array<float, 9>& b, float t) {
    std::array<float, 9> result {};
    const float u = std::clamp(t, 0.0f, 1.0f);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = a[i] * (1.0f - u) + b[i] * u;
    }
    return result;
}

std::array<float, 9> ApplyDiagonalRight(const std::array<float, 9>& matrix, const std::array<float, 3>& diagonal) {
    std::array<float, 9> result = matrix;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            result[static_cast<std::size_t>(r * 3 + c)] *= diagonal[static_cast<std::size_t>(c)];
        }
    }
    return result;
}

bool InvertMatrix3(const std::array<float, 9>& m, std::array<float, 9>& out) {
    const float det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 0.000001f) {
        return false;
    }
    const float invDet = 1.0f / det;
    out = {
        (m[4] * m[8] - m[5] * m[7]) * invDet,
        (m[2] * m[7] - m[1] * m[8]) * invDet,
        (m[1] * m[5] - m[2] * m[4]) * invDet,
        (m[5] * m[6] - m[3] * m[8]) * invDet,
        (m[0] * m[8] - m[2] * m[6]) * invDet,
        (m[2] * m[3] - m[0] * m[5]) * invDet,
        (m[3] * m[7] - m[4] * m[6]) * invDet,
        (m[1] * m[6] - m[0] * m[7]) * invDet,
        (m[0] * m[4] - m[1] * m[3]) * invDet
    };
    return true;
}

std::array<float, 9> DngXyzD50ToLinearSrgb() {
    return {
         3.1338561f, -1.6168667f, -0.4906146f,
        -0.9787684f,  1.9161415f,  0.0334540f,
         0.0719453f, -0.2289914f,  1.4052427f
    };
}

float DngWarmthFromAsShotNeutral(const RawMetadata& metadata) {
    if (!metadata.hasDngAsShotNeutral) {
        return 0.5f;
    }
    // Most phone DNGs store set 1 near daylight and set 2 near tungsten/Standard A.
    // AsShotNeutral blue falls as the captured illuminant gets warmer.
    const float blueNeutral = metadata.dngAsShotNeutral[2];
    if (metadata.dngIlluminant1 == 17 && metadata.dngIlluminant2 != 17) {
        return 1.0f - std::clamp((blueNeutral - 0.35f) / 0.55f, 0.0f, 1.0f);
    }
    return std::clamp((0.85f - blueNeutral) / 0.50f, 0.0f, 1.0f);
}

std::array<float, 9> SelectDngForwardMatrix(const RawMetadata& metadata, RawCameraTransformSource source) {
    if (source == RawCameraTransformSource::DngForwardMatrix1 && metadata.hasDngForwardMatrix1) {
        return metadata.dngForwardMatrix1;
    }
    if (source == RawCameraTransformSource::DngForwardMatrix2 && metadata.hasDngForwardMatrix2) {
        return metadata.dngForwardMatrix2;
    }
    if (metadata.hasDngForwardMatrix1 && metadata.hasDngForwardMatrix2) {
        return LerpMatrix3(metadata.dngForwardMatrix1, metadata.dngForwardMatrix2, DngWarmthFromAsShotNeutral(metadata));
    }
    if (metadata.hasDngForwardMatrix2) {
        return metadata.dngForwardMatrix2;
    }
    return metadata.dngForwardMatrix1;
}

std::array<float, 9> SelectDngColorMatrix(const RawMetadata& metadata) {
    std::array<float, 9> color = IdentityMatrix3();
    std::array<float, 9> calibration = IdentityMatrix3();
    if (metadata.hasDngColorMatrix1 && metadata.hasDngColorMatrix2) {
        color = LerpMatrix3(metadata.dngColorMatrix1, metadata.dngColorMatrix2, DngWarmthFromAsShotNeutral(metadata));
        calibration = LerpMatrix3(metadata.dngCameraCalibration1, metadata.dngCameraCalibration2, DngWarmthFromAsShotNeutral(metadata));
    } else if (metadata.hasDngColorMatrix2) {
        color = metadata.dngColorMatrix2;
        calibration = metadata.dngCameraCalibration2;
    } else {
        color = metadata.dngColorMatrix1;
        calibration = metadata.dngCameraCalibration1;
    }
    if (metadata.hasDngAnalogBalance) {
        color = ApplyDiagonalRight(color, metadata.dngAnalogBalance);
    }
    if (metadata.hasDngCameraCalibration1 || metadata.hasDngCameraCalibration2) {
        color = MultiplyMatrix3(color, calibration);
    }
    return color;
}

std::array<float, 9> BuildCameraToWorking(const RawMetadata& metadata, const RawDevelopSettings& settings) {
    if (metadata.isDng) {
        if ((settings.cameraTransformSource == RawCameraTransformSource::DngAuto ||
            settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix1 ||
            settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix2) &&
            (metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2)) {
            return MultiplyMatrix3(DngXyzD50ToLinearSrgb(), SelectDngForwardMatrix(metadata, settings.cameraTransformSource));
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix1 && metadata.hasDngForwardMatrix1) {
            return MultiplyMatrix3(DngXyzD50ToLinearSrgb(), metadata.dngForwardMatrix1);
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix2 && metadata.hasDngForwardMatrix2) {
            return MultiplyMatrix3(DngXyzD50ToLinearSrgb(), metadata.dngForwardMatrix2);
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngColorMatrixInverse ||
            settings.cameraTransformSource == RawCameraTransformSource::DngAuto) {
            std::array<float, 9> inverse {};
            if ((metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2) &&
                InvertMatrix3(SelectDngColorMatrix(metadata), inverse)) {
                return MultiplyMatrix3(DngXyzD50ToLinearSrgb(), inverse);
            }
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix1 && metadata.hasDngForwardMatrix2) {
            return MultiplyMatrix3(DngXyzD50ToLinearSrgb(), metadata.dngForwardMatrix2);
        }
    }

    // Stack does not have a full color-management module yet. For V1 this uses
    // LibRaw's camera-to-sRGB matrix as the approximate working transform.
    if (!metadata.hasCameraMatrix) {
        return IdentityMatrix3();
    }
    return metadata.cameraToSrgb;
}

bool OrientationSwapsDimensions(int orientation) {
    return orientation == 5 || orientation == 6 || orientation == 7 || orientation == 8;
}

} // namespace

RawGpuPipeline::~RawGpuPipeline() {
    Clear();
}

void RawGpuPipeline::Clear() {
    if (m_Program) glDeleteProgram(m_Program);
    if (m_LinearProgram) glDeleteProgram(m_LinearProgram);
    if (m_RawTexture) glDeleteTextures(1, &m_RawTexture);
    if (m_CorrectedRawTexture) glDeleteTextures(1, &m_CorrectedRawTexture);
    if (m_LinearTexture) glDeleteTextures(1, &m_LinearTexture);
    if (m_OutputTexture) glDeleteTextures(1, &m_OutputTexture);
    if (m_OutputFbo) glDeleteFramebuffers(1, &m_OutputFbo);
    m_Program = 0;
    m_LinearProgram = 0;
    m_RawTexture = 0;
    m_CorrectedRawTexture = 0;
    m_LinearTexture = 0;
    m_OutputTexture = 0;
    m_OutputFbo = 0;
    m_RawWidth = 0;
    m_RawHeight = 0;
    m_OutputWidth = 0;
    m_OutputHeight = 0;
    m_RawFingerprint = 0;
    m_CorrectedRawFingerprint = 0;
    m_LinearFingerprint = 0;
}

bool RawGpuPipeline::EnsureProgram() {
    if (m_Program != 0) {
        return true;
    }
    m_Program = GLHelpers::CreateShaderProgram(kRawVertexShader, kRawDevelopShader);
    if (m_Program == 0) {
        m_LastError = "RAW GPU shader compile/link failed.";
    }
    return m_Program != 0;
}

bool RawGpuPipeline::EnsureLinearProgram() {
    if (m_LinearProgram != 0) {
        return true;
    }
    m_LinearProgram = GLHelpers::CreateShaderProgram(kRawVertexShader, kLinearDngShader);
    if (m_LinearProgram == 0) {
        m_LastError = "Linear DNG GPU shader compile/link failed.";
    }
    return m_LinearProgram != 0;
}

bool RawGpuPipeline::UploadRawTexture(const RawImageData& raw) {
    const int width = raw.metadata.rawWidth;
    const int height = raw.metadata.rawHeight;
    if (width <= 0 || height <= 0 || raw.rawBuffer.empty()) {
        m_LastError = "RAW upload failed: missing raw dimensions or sensor buffer.";
        return false;
    }
    const std::size_t fingerprint = HashRawBuffer(raw.rawBuffer);
    if (m_RawTexture != 0 && m_RawWidth == width && m_RawHeight == height && m_RawFingerprint == fingerprint) {
        return true;
    }

    if (m_RawTexture) {
        glDeleteTextures(1, &m_RawTexture);
        m_RawTexture = 0;
    }

    glGenTextures(1, &m_RawTexture);
    glBindTexture(GL_TEXTURE_2D, m_RawTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, raw.rawBuffer.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (m_RawTexture == 0) {
        m_LastError = "RAW upload failed: GPU texture allocation returned 0.";
        return false;
    }

    m_RawWidth = width;
    m_RawHeight = height;
    m_RawFingerprint = fingerprint;
    return true;
}

namespace {

int CfaColorAt(CfaPattern pattern, int x, int y) {
    const int px = x & 1;
    const int py = y & 1;
    switch (pattern) {
        case CfaPattern::RGGB:
            if (py == 0 && px == 0) return 0;
            if (py == 1 && px == 1) return 2;
            return 1;
        case CfaPattern::BGGR:
            if (py == 0 && px == 0) return 2;
            if (py == 1 && px == 1) return 0;
            return 1;
        case CfaPattern::GBRG:
            if (py == 0 && px == 1) return 2;
            if (py == 1 && px == 0) return 0;
            return 1;
        case CfaPattern::GRBG:
            if (py == 0 && px == 1) return 0;
            if (py == 1 && px == 0) return 2;
            return 1;
        case CfaPattern::Unknown:
        default:
            return 1;
    }
}

float BlackForColor(const std::array<float, 4>& channelBlack, float fallback, int color) {
    if (color == 0 && channelBlack[0] > 0.0f) return channelBlack[0];
    if (color == 1 && channelBlack[1] > 0.0f) return channelBlack[1];
    if (color == 2 && channelBlack[2] > 0.0f) return channelBlack[2];
    return fallback;
}

float SampleGainMap(const DngGainMapOpcode& map, int visibleX, int visibleY) {
    if (visibleY < map.top || visibleY >= map.bottom ||
        visibleX < map.left || visibleX >= map.right ||
        ((visibleY - map.top) % std::max(1, map.rowPitch)) != 0 ||
        ((visibleX - map.left) % std::max(1, map.colPitch)) != 0 ||
        map.mapPointsV <= 0 || map.mapPointsH <= 0 || map.gains.empty()) {
        return 1.0f;
    }

    const float height = static_cast<float>(std::max(1, map.bottom - map.top - 1));
    const float width = static_cast<float>(std::max(1, map.right - map.left - 1));
    float gy = (static_cast<float>(visibleY - map.top) / height - static_cast<float>(map.mapOriginV)) *
        static_cast<float>(std::max(0, map.mapPointsV - 1));
    float gx = (static_cast<float>(visibleX - map.left) / width - static_cast<float>(map.mapOriginH)) *
        static_cast<float>(std::max(0, map.mapPointsH - 1));
    if (map.mapSpacingV > 0.0) {
        gy = (static_cast<float>(visibleY - map.top) / height - static_cast<float>(map.mapOriginV)) /
            static_cast<float>(map.mapSpacingV);
    }
    if (map.mapSpacingH > 0.0) {
        gx = (static_cast<float>(visibleX - map.left) / width - static_cast<float>(map.mapOriginH)) /
            static_cast<float>(map.mapSpacingH);
    }
    gy = std::clamp(gy, 0.0f, static_cast<float>(map.mapPointsV - 1));
    gx = std::clamp(gx, 0.0f, static_cast<float>(map.mapPointsH - 1));
    const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, map.mapPointsV - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, map.mapPointsH - 1);
    const int y1 = std::min(y0 + 1, map.mapPointsV - 1);
    const int x1 = std::min(x0 + 1, map.mapPointsH - 1);
    const float ty = gy - static_cast<float>(y0);
    const float tx = gx - static_cast<float>(x0);
    const auto at = [&map](int y, int x) {
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(map.mapPointsH) + static_cast<std::size_t>(x);
        return index < map.gains.size() ? map.gains[index] : 1.0f;
    };
    const float a = at(y0, x0) * (1.0f - tx) + at(y0, x1) * tx;
    const float b = at(y1, x0) * (1.0f - tx) + at(y1, x1) * tx;
    return std::max(0.0f, a * (1.0f - ty) + b * ty);
}

std::size_t HashDngGainMaps(const std::vector<DngGainMapOpcode>& maps) {
    std::size_t hash = 1469598103934665603ull;
    for (const DngGainMapOpcode& map : maps) {
        MixHash(hash, static_cast<std::size_t>(map.top));
        MixHash(hash, static_cast<std::size_t>(map.left));
        MixHash(hash, static_cast<std::size_t>(map.bottom));
        MixHash(hash, static_cast<std::size_t>(map.right));
        MixHash(hash, static_cast<std::size_t>(map.rowPitch));
        MixHash(hash, static_cast<std::size_t>(map.colPitch));
        MixHash(hash, HashBuffer(map.gains));
    }
    return hash;
}

} // namespace

bool RawGpuPipeline::UploadCorrectedRawTexture(const RawImageData& raw, const RawDevelopSettings& settings, bool& outHasCorrectedRaw) {
    outHasCorrectedRaw = false;
    const RawMetadata& metadata = raw.metadata;
    if (metadata.dngGainMaps.empty()) {
        return true;
    }
    const int width = metadata.rawWidth;
    const int height = metadata.rawHeight;
    if (width <= 0 || height <= 0 || raw.rawBuffer.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        m_LastError = "DNG GainMap correction failed: missing raw mosaic.";
        return false;
    }

    std::size_t fingerprint = HashRawBuffer(raw.rawBuffer);
    MixHash(fingerprint, HashDngGainMaps(metadata.dngGainMaps));
    MixHash(fingerprint, static_cast<std::size_t>(settings.overrideBlackLevel));
    MixHash(fingerprint, static_cast<std::size_t>(settings.overrideWhiteLevel));
    MixHash(fingerprint, std::hash<float>{}(settings.blackLevelOverride));
    MixHash(fingerprint, std::hash<float>{}(settings.whiteLevelOverride));
    if (m_CorrectedRawTexture != 0 && m_RawWidth == width && m_RawHeight == height && m_CorrectedRawFingerprint == fingerprint) {
        outHasCorrectedRaw = true;
        return true;
    }

    const float black = settings.overrideBlackLevel ? settings.blackLevelOverride : metadata.blackLevel;
    const float white = std::max(black + 1.0f, settings.overrideWhiteLevel ? settings.whiteLevelOverride : metadata.whiteLevel);
    std::array<float, 4> channelBlack = metadata.perChannelBlack;
    if (settings.overrideBlackLevel) {
        channelBlack = { 0.0f, 0.0f, 0.0f, 0.0f };
    } else if (channelBlack[1] > 0.0f && channelBlack[3] > 0.0f) {
        channelBlack[1] = (channelBlack[1] + channelBlack[3]) * 0.5f;
    }

    std::vector<float> corrected(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    const int cropX = std::max(0, metadata.leftMargin);
    const int cropY = std::max(0, metadata.topMargin);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int visibleX = x - cropX;
            const int visibleY = y - cropY;
            const int color = CfaColorAt(metadata.cfaPattern, std::max(0, visibleX), std::max(0, visibleY));
            const float b = BlackForColor(channelBlack, black, color);
            const float w = std::max(b + 1.0f, white);
            float value = (static_cast<float>(raw.rawBuffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]) - b) /
                std::max(1.0f, w - b);
            if (visibleX >= 0 && visibleY >= 0) {
                for (const DngGainMapOpcode& map : metadata.dngGainMaps) {
                    value *= SampleGainMap(map, visibleX, visibleY);
                }
            }
            corrected[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = value;
        }
    }

    if (m_CorrectedRawTexture) {
        glDeleteTextures(1, &m_CorrectedRawTexture);
        m_CorrectedRawTexture = 0;
    }
    std::vector<float> correctedRgba(corrected.size() * 4, 1.0f);
    for (std::size_t i = 0; i < corrected.size(); ++i) {
        correctedRgba[i * 4] = corrected[i];
        correctedRgba[i * 4 + 1] = corrected[i];
        correctedRgba[i * 4 + 2] = corrected[i];
    }

    glGenTextures(1, &m_CorrectedRawTexture);
    glBindTexture(GL_TEXTURE_2D, m_CorrectedRawTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, correctedRgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (m_CorrectedRawTexture == 0) {
        m_LastError = "DNG GainMap correction failed: GPU texture allocation returned 0.";
        return false;
    }
    m_RawWidth = width;
    m_RawHeight = height;
    m_CorrectedRawFingerprint = fingerprint;
    outHasCorrectedRaw = true;
    return true;
}

bool RawGpuPipeline::UploadLinearTexture(const RawImageData& raw, const RawDevelopSettings& settings) {
    const RawMetadata& metadata = raw.metadata;
    const int width = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    const int height = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    const int channels = std::clamp(metadata.linearChannels, 3, 4);
    if (width <= 0 || height <= 0 || channels < 3) {
        m_LastError = "Linear DNG upload failed: invalid dimensions or channel count.";
        return false;
    }
    if (raw.linearUInt16Buffer.empty() && raw.linearFloatBuffer.empty()) {
        m_LastError = "Linear DNG upload failed: missing RGB buffer.";
        return false;
    }

    std::size_t fingerprint = 1469598103934665603ull;
    MixHash(fingerprint, static_cast<std::size_t>(width));
    MixHash(fingerprint, static_cast<std::size_t>(height));
    MixHash(fingerprint, static_cast<std::size_t>(channels));
    if (!raw.linearUInt16Buffer.empty()) {
        MixHash(fingerprint, HashBuffer(raw.linearUInt16Buffer));
    } else {
        MixHash(fingerprint, HashBuffer(raw.linearFloatBuffer));
    }
    if (m_LinearTexture != 0 && m_RawWidth == width && m_RawHeight == height && m_LinearFingerprint == fingerprint) {
        return true;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> rgba(pixelCount * 4, 1.0f);
    const float black = settings.overrideBlackLevel ? settings.blackLevelOverride : metadata.blackLevel;
    const float white = std::max(black + 1.0f, settings.overrideWhiteLevel ? settings.whiteLevelOverride : metadata.whiteLevel);
    if (!raw.linearUInt16Buffer.empty()) {
        if (raw.linearUInt16Buffer.size() < pixelCount * static_cast<std::size_t>(channels)) {
            m_LastError = "Linear DNG upload failed: UInt16 RGB buffer is shorter than expected.";
            return false;
        }
        for (std::size_t i = 0; i < pixelCount; ++i) {
            for (int c = 0; c < 3; ++c) {
                const float v = static_cast<float>(raw.linearUInt16Buffer[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]);
                rgba[i * 4 + static_cast<std::size_t>(c)] = (v - black) / std::max(1.0f, white - black);
            }
        }
    } else {
        if (raw.linearFloatBuffer.size() < pixelCount * static_cast<std::size_t>(channels)) {
            m_LastError = "Linear DNG upload failed: Float RGB buffer is shorter than expected.";
            return false;
        }
        for (std::size_t i = 0; i < pixelCount; ++i) {
            for (int c = 0; c < 3; ++c) {
                rgba[i * 4 + static_cast<std::size_t>(c)] =
                    raw.linearFloatBuffer[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
            }
        }
    }

    if (m_LinearTexture) {
        glDeleteTextures(1, &m_LinearTexture);
        m_LinearTexture = 0;
    }
    glGenTextures(1, &m_LinearTexture);
    glBindTexture(GL_TEXTURE_2D, m_LinearTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (m_LinearTexture == 0) {
        m_LastError = "Linear DNG upload failed: GPU texture allocation returned 0.";
        return false;
    }
    m_RawWidth = width;
    m_RawHeight = height;
    m_LinearFingerprint = fingerprint;
    return true;
}

bool RawGpuPipeline::EnsureOutput(int width, int height) {
    if (width <= 0 || height <= 0) {
        m_LastError = "RAW output allocation failed: invalid output dimensions.";
        return false;
    }
    if (m_OutputTexture != 0 && m_OutputFbo != 0 && m_OutputWidth == width && m_OutputHeight == height) {
        return true;
    }
    if (m_OutputFbo) {
        glDeleteFramebuffers(1, &m_OutputFbo);
        m_OutputFbo = 0;
    }
    if (m_OutputTexture) {
        glDeleteTextures(1, &m_OutputTexture);
        m_OutputTexture = 0;
    }

    m_OutputTexture = 0;
    glGenTextures(1, &m_OutputTexture);
    glBindTexture(GL_TEXTURE_2D, m_OutputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_OutputFbo = GLHelpers::CreateFBO(m_OutputTexture);
    m_OutputWidth = width;
    m_OutputHeight = height;
    if (m_OutputTexture == 0 || m_OutputFbo == 0) {
        m_LastError = "RAW output allocation failed: RGBA16F texture or framebuffer was not created.";
        return false;
    }
    return true;
}

unsigned int RawGpuPipeline::Render(const RawImageData& raw, const RawDevelopSettings& settings) {
    m_LastError.clear();
    const RawMetadata& metadata = raw.metadata;

    // Combine EXIF orientation and manual rotation settings to compute effectiveOrientation
    int exifRotationSteps = 0;
    if (metadata.orientation == 3) exifRotationSteps = 2;       // 180°
    else if (metadata.orientation == 5) exifRotationSteps = 1;  // approximate transposed+mirrored as 90° CW
    else if (metadata.orientation == 6) exifRotationSteps = 1;  // 90° CW
    else if (metadata.orientation == 8) exifRotationSteps = 3;  // 270° CW

    // Map user-facing CW rotation degrees to shader rotation steps, inverting for
    // OpenGL's bottom-left origin which causes a Y-flip that reverses visible rotation direction.
    // 0° -> 0 steps, 90° CW (visual) -> 3 steps (270° CW in shader), 180° -> 2 steps, 270° CW (visual) -> 1 step.
    int manualRotationSteps = 0;
    if (settings.rotationDegrees == 90)       manualRotationSteps = 3;
    else if (settings.rotationDegrees == 180) manualRotationSteps = 2;
    else if (settings.rotationDegrees == 270) manualRotationSteps = 1;
    int totalRotationSteps = (exifRotationSteps + manualRotationSteps) % 4;

    int effectiveOrientation = metadata.orientation; // default fallback (preserves mirroring in 2, 4, 5, 7 if any)
    if (metadata.orientation <= 1 || metadata.orientation == 3 || metadata.orientation == 6 || metadata.orientation == 8) {
        if (totalRotationSteps == 0) effectiveOrientation = 1;
        else if (totalRotationSteps == 1) effectiveOrientation = 6;
        else if (totalRotationSteps == 2) effectiveOrientation = 3;
        else if (totalRotationSteps == 3) effectiveOrientation = 8;
    } else if (metadata.orientation == 2 || metadata.orientation == 4 || metadata.orientation == 5 || metadata.orientation == 7) {
        // Mirrored orientations
        int baseSteps = 0;
        if (metadata.orientation == 2) baseSteps = 0;
        else if (metadata.orientation == 7) baseSteps = 1;
        else if (metadata.orientation == 4) baseSteps = 2;
        else if (metadata.orientation == 5) baseSteps = 3;
        
        int finalSteps = (baseSteps + manualRotationSteps) % 4;
        if (finalSteps == 0) effectiveOrientation = 2;
        else if (finalSteps == 1) effectiveOrientation = 7;
        else if (finalSteps == 2) effectiveOrientation = 4;
        else if (finalSteps == 3) effectiveOrientation = 5;
    }

    const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    const bool swapsDimensions = OrientationSwapsDimensions(effectiveOrientation);
    const int outWidth = settings.rotateToFitFrame ? visibleWidth : (swapsDimensions ? visibleHeight : visibleWidth);
    const int outHeight = settings.rotateToFitFrame ? visibleHeight : (swapsDimensions ? visibleWidth : visibleHeight);
    if (metadata.pixelLayout == RawPixelLayout::LinearRgb) {
        if (!EnsureLinearProgram() ||
            !UploadLinearTexture(raw, settings) ||
            !EnsureOutput(outWidth, outHeight)) {
            return 0;
        }

        GLint prevViewport[4];
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        glViewport(0, 0, outWidth, outHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFbo);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(m_LinearProgram);

        const std::array<float, 9> cameraToWorking = BuildCameraToWorking(metadata, settings);
        const float exposure = std::pow(2.0f, settings.exposureStops);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_LinearTexture);
        glUniform1i(glGetUniformLocation(m_LinearProgram, "uLinearRgb"), 0);
        glUniform2i(glGetUniformLocation(m_LinearProgram, "uVisibleSize"), visibleWidth, visibleHeight);
        glUniform1i(glGetUniformLocation(m_LinearProgram, "uOrientation"), effectiveOrientation);
        glUniform1i(glGetUniformLocation(m_LinearProgram, "uRotateToFitFrame"), settings.rotateToFitFrame ? 1 : 0);
        glUniformMatrix3fv(glGetUniformLocation(m_LinearProgram, "uCameraToWorking"), 1, settings.debugTransposeCameraMatrix ? GL_FALSE : GL_TRUE, cameraToWorking.data());
        glUniform1i(glGetUniformLocation(m_LinearProgram, "uUseCameraTransform"), settings.cameraTransformEnabled && !settings.debugBypassCameraTransform ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_LinearProgram, "uDebugView"), DebugViewUniform(settings.debugView));
        glUniform1f(glGetUniformLocation(m_LinearProgram, "uExposure"), exposure);

        static unsigned int vao = 0;
        static unsigned int vbo = 0;
        if (vao == 0) {
            const float vertices[] = {
                -1.0f, -1.0f, 0.0f, 0.0f,
                 1.0f, -1.0f, 1.0f, 0.0f,
                 1.0f,  1.0f, 1.0f, 1.0f,
                -1.0f, -1.0f, 0.0f, 0.0f,
                 1.0f,  1.0f, 1.0f, 1.0f,
                -1.0f,  1.0f, 0.0f, 1.0f
            };
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        }
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);

        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        return m_OutputTexture;
    }
    if (metadata.pixelLayout != RawPixelLayout::MosaicBayer) {
        m_LastError = "RAW render failed: unsupported RAW pixel layout.";
        return 0;
    }
    if (metadata.cfaPattern == CfaPattern::Unknown) {
        m_LastError = "RAW render failed: missing or unsupported CFA pattern.";
        return 0;
    }
    bool hasCorrectedRaw = false;
    if (!EnsureProgram() ||
        !UploadRawTexture(raw) ||
        !UploadCorrectedRawTexture(raw, settings, hasCorrectedRaw) ||
        !EnsureOutput(outWidth, outHeight)) {
        return 0;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glViewport(0, 0, outWidth, outHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFbo);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_Program);

    const float black = settings.overrideBlackLevel ? settings.blackLevelOverride : metadata.blackLevel;
    const float white = std::max(black + 1.0f, settings.overrideWhiteLevel ? settings.whiteLevelOverride : metadata.whiteLevel);
    const std::array<float, 3> wb = ResolveWhiteBalance(metadata, settings);
    const std::array<float, 9> cameraToWorking = BuildCameraToWorking(metadata, settings);
    const float exposure = std::pow(2.0f, settings.exposureStops);
    std::array<float, 4> channelBlack = metadata.perChannelBlack;
    if (settings.overrideBlackLevel) {
        channelBlack = { 0.0f, 0.0f, 0.0f, 0.0f };
    } else if (channelBlack[1] > 0.0f && channelBlack[3] > 0.0f) {
        channelBlack[1] = (channelBlack[1] + channelBlack[3]) * 0.5f;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_RawTexture);
    glUniform1i(glGetUniformLocation(m_Program, "uRaw"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hasCorrectedRaw ? m_CorrectedRawTexture : 0);
    glUniform1i(glGetUniformLocation(m_Program, "uCorrectedRaw"), 1);
    glUniform1i(glGetUniformLocation(m_Program, "uUseCorrectedRaw"), hasCorrectedRaw ? 1 : 0);
    glUniform2i(glGetUniformLocation(m_Program, "uRawSize"), metadata.rawWidth, metadata.rawHeight);
    glUniform2i(glGetUniformLocation(m_Program, "uVisibleSize"), visibleWidth, visibleHeight);
    glUniform2i(glGetUniformLocation(m_Program, "uCropOrigin"), std::max(0, metadata.leftMargin), std::max(0, metadata.topMargin));
    glUniform1i(glGetUniformLocation(m_Program, "uOrientation"), effectiveOrientation);
    glUniform1i(glGetUniformLocation(m_Program, "uRotateToFitFrame"), settings.rotateToFitFrame ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_Program, "uCfaPattern"), PatternUniform(metadata.cfaPattern));
    glUniform1f(glGetUniformLocation(m_Program, "uBlackLevel"), black);
    glUniform4f(glGetUniformLocation(m_Program, "uChannelBlack"),
        channelBlack[0],
        channelBlack[1],
        channelBlack[2],
        channelBlack[3]);
    glUniform1f(glGetUniformLocation(m_Program, "uWhiteLevel"), white);
    glUniform3f(glGetUniformLocation(m_Program, "uWhiteBalance"), wb[0], wb[1], wb[2]);
    glUniformMatrix3fv(glGetUniformLocation(m_Program, "uCameraToWorking"), 1, settings.debugTransposeCameraMatrix ? GL_FALSE : GL_TRUE, cameraToWorking.data());
    glUniform1i(glGetUniformLocation(m_Program, "uUseCameraTransform"), settings.cameraTransformEnabled && !settings.debugBypassCameraTransform ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_Program, "uDebugView"), DebugViewUniform(settings.debugView));
    glUniform1f(glGetUniformLocation(m_Program, "uExposure"), exposure);
    glUniform1i(glGetUniformLocation(m_Program, "uHighlightMode"), static_cast<int>(settings.highlightMode));
    glUniform1f(glGetUniformLocation(m_Program, "uHighlightStrength"), settings.highlightStrength);
    glUniform1f(glGetUniformLocation(m_Program, "uHighlightThreshold"), settings.highlightThreshold);
    glUniform1i(glGetUniformLocation(m_Program, "uMosaicDenoiseEnabled"), settings.mosaicDenoise.enabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_Program, "uMosaicHotPixelSuppression"), settings.mosaicDenoise.hotPixelSuppression ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_Program, "uMosaicHotPixelThreshold"), settings.mosaicDenoise.hotPixelThreshold);
    glUniform1f(glGetUniformLocation(m_Program, "uMosaicLumaStrength"), settings.mosaicDenoise.lumaStrength);
    glUniform1f(glGetUniformLocation(m_Program, "uMosaicChromaStrength"), settings.mosaicDenoise.chromaStrength);
    glUniform1i(glGetUniformLocation(m_Program, "uMosaicRadius"), std::clamp(settings.mosaicDenoise.radius, 1, 4));
    glUniform1f(glGetUniformLocation(m_Program, "uMosaicEdgeProtection"), settings.mosaicDenoise.edgeProtection);
    glUniform1i(glGetUniformLocation(m_Program, "uMosaicIterations"), std::clamp(settings.mosaicDenoise.iterations, 1, 2));

    static unsigned int vao = 0;
    static unsigned int vbo = 0;
    if (vao == 0) {
        const float vertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f
        };
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    }
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    return m_OutputTexture;
}

} // namespace Raw
