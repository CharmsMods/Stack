#include "RenderPipeline.h"
#include "Composite/EmbeddedCompositeFont.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawLoader.h"
#include "ThirdParty/stb_image.h"
#include <imstb_truetype.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <functional>
#include <iostream>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif

#ifndef GL_RGB32F
#define GL_RGB32F 0x8815
#endif

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif

#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif

#ifndef GL_RED
#define GL_RED 0x1903
#endif

namespace {

constexpr std::size_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr std::size_t kFnvPrime = 1099511628211ull;
constexpr std::size_t kMaxRawDevelopStageCacheEntriesPerKey = 6;
constexpr std::uint64_t kRawDevelopStageCacheBytesPerPixel = 8; // GLHelpers::CreateEmptyTexture uses RGBA16F.
constexpr std::uint64_t kRawDevelopStageCacheSoftByteBudget = 512ull * 1024ull * 1024ull;
constexpr std::uint64_t kRawDevelopStageCacheMediumEntryBytes = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t kRawDevelopStageCacheLargeEntryBytes = 128ull * 1024ull * 1024ull;
constexpr std::uint64_t kRawDevelopStageCacheHugeEntryBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kRawDevelopStageCacheSingleEntryByteLimit = 384ull * 1024ull * 1024ull;

inline void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}
template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

std::size_t HashBytes(const unsigned char* data, std::size_t size) {
    std::size_t hash = kFnvOffsetBasis;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::size_t>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

std::size_t HashBytes(const std::vector<unsigned char>& data) {
    return HashBytes(data.data(), data.size());
}

std::size_t HashJson(const nlohmann::json& value) {
    return HashValue(value.dump());
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::uint64_t EstimateRawDevelopStageCacheTextureBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const std::uint64_t w = static_cast<std::uint64_t>(width);
    const std::uint64_t h = static_cast<std::uint64_t>(height);
    if (w > std::numeric_limits<std::uint64_t>::max() / h) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t pixels = w * h;
    if (pixels > std::numeric_limits<std::uint64_t>::max() / kRawDevelopStageCacheBytesPerPixel) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return pixels * kRawDevelopStageCacheBytesPerPixel;
}

std::size_t ResolveRawDevelopStageCacheMaxEntries(int width, int height) {
    const std::uint64_t bytes = EstimateRawDevelopStageCacheTextureBytes(width, height);
    if (bytes == 0 || bytes > kRawDevelopStageCacheSingleEntryByteLimit) {
        return 0;
    }
    if (bytes >= kRawDevelopStageCacheHugeEntryBytes) {
        return 1;
    }
    if (bytes >= kRawDevelopStageCacheLargeEntryBytes) {
        return 2;
    }
    if (bytes >= kRawDevelopStageCacheMediumEntryBytes) {
        return 3;
    }
    return kMaxRawDevelopStageCacheEntriesPerKey;
}

float SafeLog2(float value) {
    return std::log2(std::max(value, 0.000001f));
}

float PercentileFromSorted(const std::vector<float>& sorted, float percentile) {
    if (sorted.empty()) {
        return 0.0f;
    }
    const float p = std::clamp(percentile, 0.0f, 1.0f);
    const float scaled = p * static_cast<float>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t hi = std::min<std::size_t>(lo + 1, sorted.size() - 1);
    const float t = scaled - static_cast<float>(lo);
    return sorted[lo] * (1.0f - t) + sorted[hi] * t;
}

float LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

float SmoothStepFloat(float edge0, float edge1, float value) {
    const float denom = std::max(0.000001f, edge1 - edge0);
    const float t = std::clamp((value - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float CombineMaskValue(float base, float value, RenderCustomMaskOperation operation) {
    base = Clamp01(base);
    value = Clamp01(value);
    switch (operation) {
        case RenderCustomMaskOperation::Add: return std::max(base, value);
        case RenderCustomMaskOperation::Subtract: return base * (1.0f - value);
        case RenderCustomMaskOperation::Intersect: return base * value;
        case RenderCustomMaskOperation::Exclude: return std::abs(base - value);
    }
    return base;
}

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

bool CustomMaskPointInPolygon(const std::vector<RenderCustomMaskPoint>& points, float x, float y) {
    if (points.size() < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const float xi = points[i].x;
        const float yi = points[i].y;
        const float xj = points[j].x;
        const float yj = points[j].y;
        const bool intersects = ((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / std::max(0.000001f, yj - yi) + xi);
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

float DistanceToSegment(float px, float py, const RenderCustomMaskPoint& a, const RenderCustomMaskPoint& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float denom = abx * abx + aby * aby;
    const float t = denom > 0.000001f
        ? std::clamp(((px - a.x) * abx + (py - a.y) * aby) / denom, 0.0f, 1.0f)
        : 0.0f;
    const float dx = px - (a.x + abx * t);
    const float dy = py - (a.y + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

float EvaluateCustomMaskObject(const RenderCustomMaskObject& object, float u, float v, int width, int height) {
    if (!object.enabled || object.points.empty()) {
        return 0.0f;
    }

    float value = 0.0f;
    const float feather = std::max(0.0f, object.feather);
    if ((object.type == RenderCustomMaskObjectType::Rectangle || object.type == RenderCustomMaskObjectType::Ellipse) &&
        object.points.size() >= 2) {
        const float minX = std::min(object.points[0].x, object.points[1].x);
        const float maxX = std::max(object.points[0].x, object.points[1].x);
        const float minY = std::min(object.points[0].y, object.points[1].y);
        const float maxY = std::max(object.points[0].y, object.points[1].y);
        if (maxX > minX && maxY > minY) {
            if (object.type == RenderCustomMaskObjectType::Rectangle) {
                const float outsideX = std::max(std::max(minX - u, 0.0f), u - maxX);
                const float outsideY = std::max(std::max(minY - v, 0.0f), v - maxY);
                const float outside = std::sqrt(outsideX * outsideX + outsideY * outsideY);
                const float insideDist = std::min(std::min(u - minX, maxX - u), std::min(v - minY, maxY - v));
                const bool inside = u >= minX && u <= maxX && v >= minY && v <= maxY;
                if (inside) {
                    value = feather > 0.0f ? std::clamp(insideDist / feather, 0.0f, 1.0f) : 1.0f;
                } else {
                    value = feather > 0.0f ? 1.0f - std::clamp(outside / feather, 0.0f, 1.0f) : 0.0f;
                }
            } else {
                const float cx = (minX + maxX) * 0.5f;
                const float cy = (minY + maxY) * 0.5f;
                const float rx = std::max(0.0001f, (maxX - minX) * 0.5f);
                const float ry = std::max(0.0001f, (maxY - minY) * 0.5f);
                const float d = std::sqrt(((u - cx) * (u - cx)) / (rx * rx) + ((v - cy) * (v - cy)) / (ry * ry));
                const float normFeather = feather / std::max(0.0001f, std::min(rx, ry));
                value = normFeather > 0.0f ? 1.0f - SmoothStepFloat(1.0f - normFeather, 1.0f + normFeather, d) : (d <= 1.0f ? 1.0f : 0.0f);
            }
        }
    } else if (object.type == RenderCustomMaskObjectType::Polygon) {
        const bool inside = CustomMaskPointInPolygon(object.points, u, v);
        value = inside ? 1.0f : 0.0f;
    } else if (object.type == RenderCustomMaskObjectType::FreeformPath && object.points.size() >= 2) {
        float minDistance = std::numeric_limits<float>::max();
        for (std::size_t i = 1; i < object.points.size(); ++i) {
            minDistance = std::min(minDistance, DistanceToSegment(u, v, object.points[i - 1], object.points[i]));
        }
        const float radius = std::max(1.0f / static_cast<float>(std::max(width, height)), object.blur / static_cast<float>(std::max(1, std::max(width, height))));
        const float softness = std::max(feather, radius);
        value = softness > 0.0f ? 1.0f - std::clamp((minDistance - radius) / softness, 0.0f, 1.0f) : (minDistance <= radius ? 1.0f : 0.0f);
    }

    value = Clamp01(value);
    if (object.invert) {
        value = 1.0f - value;
    }
    return Clamp01(value * std::clamp(object.strength, 0.0f, 1.0f));
}

void BoxBlurMask(std::vector<float>& values, int width, int height, int radius) {
    if (radius <= 0 || values.empty() || width <= 0 || height <= 0) {
        return;
    }

    std::vector<float> horizontal(values.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        std::vector<float> prefix(static_cast<std::size_t>(width + 1), 0.0f);
        for (int x = 0; x < width; ++x) {
            prefix[static_cast<std::size_t>(x + 1)] =
                prefix[static_cast<std::size_t>(x)] + values[static_cast<std::size_t>(y) * width + x];
        }
        for (int x = 0; x < width; ++x) {
            const int lo = std::max(0, x - radius);
            const int hi = std::min(width - 1, x + radius);
            const float sum = prefix[static_cast<std::size_t>(hi + 1)] - prefix[static_cast<std::size_t>(lo)];
            horizontal[static_cast<std::size_t>(y) * width + x] = sum / static_cast<float>(hi - lo + 1);
        }
    }

    for (int x = 0; x < width; ++x) {
        std::vector<float> prefix(static_cast<std::size_t>(height + 1), 0.0f);
        for (int y = 0; y < height; ++y) {
            prefix[static_cast<std::size_t>(y + 1)] =
                prefix[static_cast<std::size_t>(y)] + horizontal[static_cast<std::size_t>(y) * width + x];
        }
        for (int y = 0; y < height; ++y) {
            const int lo = std::max(0, y - radius);
            const int hi = std::min(height - 1, y + radius);
            const float sum = prefix[static_cast<std::size_t>(hi + 1)] - prefix[static_cast<std::size_t>(lo)];
            values[static_cast<std::size_t>(y) * width + x] = sum / static_cast<float>(hi - lo + 1);
        }
    }
}

void MorphMask(std::vector<float>& values, int width, int height, int radius, bool expand) {
    if (radius <= 0 || values.empty() || width <= 0 || height <= 0) {
        return;
    }

    auto better = [expand](float a, float b) {
        return expand ? a >= b : a <= b;
    };

    std::vector<float> horizontal(values.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        std::deque<int> window;
        int nextAdd = 0;
        for (int x = 0; x < width; ++x) {
            const int targetAdd = std::min(width - 1, x + radius);
            while (nextAdd <= targetAdd) {
                while (!window.empty()) {
                    const float backValue = values[static_cast<std::size_t>(y) * width + window.back()];
                    const float addValue = values[static_cast<std::size_t>(y) * width + nextAdd];
                    if (better(backValue, addValue)) {
                        break;
                    }
                    window.pop_back();
                }
                window.push_back(nextAdd);
                ++nextAdd;
            }
            while (!window.empty() && window.front() < x - radius) {
                window.pop_front();
            }
            horizontal[static_cast<std::size_t>(y) * width + x] =
                values[static_cast<std::size_t>(y) * width + window.front()];
        }
    }

    for (int x = 0; x < width; ++x) {
        std::deque<int> window;
        int nextAdd = 0;
        for (int y = 0; y < height; ++y) {
            const int targetAdd = std::min(height - 1, y + radius);
            while (nextAdd <= targetAdd) {
                while (!window.empty()) {
                    const float backValue = horizontal[static_cast<std::size_t>(window.back()) * width + x];
                    const float addValue = horizontal[static_cast<std::size_t>(nextAdd) * width + x];
                    if (better(backValue, addValue)) {
                        break;
                    }
                    window.pop_back();
                }
                window.push_back(nextAdd);
                ++nextAdd;
            }
            while (!window.empty() && window.front() < y - radius) {
                window.pop_front();
            }
            values[static_cast<std::size_t>(y) * width + x] =
                horizontal[static_cast<std::size_t>(window.front()) * width + x];
        }
    }
}

std::vector<float> FlattenCustomMaskPayload(const RenderCustomMaskPayload& payload, int width, int height) {
    std::vector<float> values(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    const int sourceW = std::max(1, payload.width);
    const int sourceH = std::max(1, payload.height);
    const bool hasRaster = payload.rasterLayer.size() >= static_cast<std::size_t>(sourceW) * static_cast<std::size_t>(sourceH);

    for (int y = 0; y < height; ++y) {
        const float v = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
        const float maskV = 1.0f - v;
        const int srcY = std::clamp(static_cast<int>(std::round(maskV * static_cast<float>(sourceH - 1))), 0, sourceH - 1);
        for (int x = 0; x < width; ++x) {
            const float u = width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.0f;
            const int srcX = std::clamp(static_cast<int>(std::round(u * static_cast<float>(sourceW - 1))), 0, sourceW - 1);
            float value = hasRaster ? Clamp01(payload.rasterLayer[static_cast<std::size_t>(srcY) * sourceW + srcX]) : 0.0f;
            for (const RenderCustomMaskObject& object : payload.objects) {
                const float objectValue = EvaluateCustomMaskObject(object, u, maskV, width, height);
                value = CombineMaskValue(value, objectValue, object.operation);
            }
            values[static_cast<std::size_t>(y) * width + x] = Clamp01(value);
        }
    }

    const int morphRadius = static_cast<int>(std::round(std::abs(payload.expandContract)));
    if (morphRadius > 0) {
        MorphMask(values, width, height, morphRadius, payload.expandContract > 0.0f);
    }
    const int blurRadius = static_cast<int>(std::round(std::max(0.0f, payload.blurRadius)));
    if (blurRadius > 0) {
        BoxBlurMask(values, width, height, blurRadius);
    }
    if (payload.invert) {
        for (float& value : values) {
            value = 1.0f - Clamp01(value);
        }
    }
    return values;
}

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

struct HdrMergeFeatureImage {
    int width = 0;
    int height = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float clipRatio = 0.0f;
    float blackRatio = 0.0f;
    float detailEnergy = 0.0f;
    std::vector<float> values;
    std::vector<std::uint8_t> thresholdBits;
    std::vector<std::uint8_t> excludeBits;
};

struct HdrMergeAlignmentAnalysis {
    bool valid = false;
    int referenceIndex = 0;
    std::array<float, 3> offsetX { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> offsetY { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> confidence { 1.0f, 1.0f, 1.0f };
};

struct HdrMergeInputContext {
    bool active = false;
    bool hasRawMetadata = false;
    bool hasCaptureExposure = false;
    float captureExposureEv = 0.0f;
    float developExposureStops = 0.0f;
    float developExposureScale = 1.0f;
    Raw::RawMetadata metadata;
};

struct QuickTextureStats {
    bool valid = false;
    float maxRgb = 0.0f;
    float maxLuma = 0.0f;
    float p99Luma = 0.0f;
};

QuickTextureStats ProbeTextureStats(unsigned int texture, int width, int height) {
    QuickTextureStats stats;
    if (!texture || width <= 0 || height <= 0) {
        return stats;
    }

    constexpr int kProbeMaxEdge = 128;
    const float scale = std::min(
        1.0f,
        static_cast<float>(kProbeMaxEdge) / static_cast<float>(std::max(width, height)));
    const int probeW = std::max(1, static_cast<int>(std::round(static_cast<float>(width) * scale)));
    const int probeH = std::max(1, static_cast<int>(std::round(static_cast<float>(height) * scale)));

    const ScopedFramebufferState savedState(true);

    unsigned int sourceFbo = GLHelpers::CreateFBO(texture);
    unsigned int probeTexture = 0;
    unsigned int probeFbo = 0;
    if (sourceFbo == 0) {
        savedState.Restore(true);
        return stats;
    }

    bool targetReady = true;
    if (probeW != width || probeH != height) {
        probeTexture = GLHelpers::CreateEmptyTexture(probeW, probeH);
        probeFbo = GLHelpers::CreateFBO(probeTexture);
        targetReady = probeTexture != 0 && probeFbo != 0;
    }

    if (targetReady) {
        if (probeFbo != 0) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probeFbo);
            glBlitFramebuffer(0, 0, width, height, 0, 0, probeW, probeH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, probeFbo);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0, 0, probeW, probeH);
        while (glGetError() != GL_NO_ERROR) {}

        std::vector<float> pixels(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH) * 4u, 0.0f);
        glReadPixels(0, 0, probeW, probeH, GL_RGBA, GL_FLOAT, pixels.data());
        if (glGetError() == GL_NO_ERROR) {
            std::vector<float> lumas;
            lumas.reserve(static_cast<std::size_t>(probeW) * static_cast<std::size_t>(probeH));
            for (std::size_t i = 0; i + 2 < pixels.size(); i += 4u) {
                const float r = std::isfinite(pixels[i + 0]) ? std::max(0.0f, pixels[i + 0]) : 0.0f;
                const float g = std::isfinite(pixels[i + 1]) ? std::max(0.0f, pixels[i + 1]) : 0.0f;
                const float b = std::isfinite(pixels[i + 2]) ? std::max(0.0f, pixels[i + 2]) : 0.0f;
                const float maxRgb = std::max(r, std::max(g, b));
                const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                stats.maxRgb = std::max(stats.maxRgb, maxRgb);
                stats.maxLuma = std::max(stats.maxLuma, luma);
                lumas.push_back(luma);
            }
            std::sort(lumas.begin(), lumas.end());
            stats.p99Luma = PercentileFromSorted(lumas, 0.99f);
            stats.valid = true;
        }
    }

    savedState.Restore(true);
    if (probeFbo) glDeleteFramebuffers(1, &probeFbo);
    if (probeTexture) glDeleteTextures(1, &probeTexture);
    if (sourceFbo) glDeleteFramebuffers(1, &sourceFbo);
    return stats;
}

bool IsDefaultToneCurvePayload(const nlohmann::json& layerJson) {
    if (!layerJson.is_object() || layerJson.value("type", std::string()) != "ToneCurve") {
        return false;
    }
    if (!layerJson.contains("points")) {
        return false;
    }

    auto nearValue = [](float a, float b) {
        return std::abs(a - b) <= 0.0001f;
    };

    auto isIdentityCurve = [&](const nlohmann::json& points) {
        return points.is_array() &&
            points.size() == 2 &&
            points[0].is_object() &&
            points[1].is_object() &&
            nearValue(points[0].value("x", 1.0f), 0.0f) &&
            nearValue(points[0].value("y", 1.0f), 0.0f) &&
            nearValue(points[1].value("x", 0.0f), 1.0f) &&
            nearValue(points[1].value("y", 0.0f), 1.0f);
    };

    const nlohmann::json& finishPoints = layerJson["points"];
    if (!isIdentityCurve(finishPoints)) {
        return false;
    }
    if (layerJson.contains("preparedPoints") && !isIdentityCurve(layerJson["preparedPoints"])) {
        return false;
    }

    const float localStrength = std::abs(layerJson.value("localBaselineStrength", 0.0f));
    const float shadowOpening = std::abs(layerJson.value("localShadowOpening", 0.0f));
    const float highlightCompression = std::abs(layerJson.value("localHighlightCompression", 0.0f));
    const float foundationMagnitude =
        std::abs(layerJson.value("foundationShadows", 0.0f)) +
        std::abs(layerJson.value("foundationDarks", 0.0f)) +
        std::abs(layerJson.value("foundationMidtones", 0.0f)) +
        std::abs(layerJson.value("foundationLights", 0.0f)) +
        std::abs(layerJson.value("foundationHighlights", 0.0f));
    return localStrength < 0.0001f &&
        shadowOpening < 0.0001f &&
        highlightCompression < 0.0001f &&
        foundationMagnitude < 0.0001f;
}

float MedianFloat(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    if ((values.size() & 1u) != 0u) {
        return values[mid];
    }
    return 0.5f * (values[mid - 1] + values[mid]);
}

bool HasHdrMergeCaptureExposure(const Raw::RawMetadata& metadata) {
    return metadata.hasExposureTime || metadata.hasIsoSpeed || metadata.hasApertureFNumber;
}

float ComputeHdrMergeCaptureExposureEv(const Raw::RawMetadata& metadata) {
    const float shutter = metadata.hasExposureTime ? std::max(0.000001f, metadata.exposureTimeSeconds) : 1.0f;
    const float isoFactor = metadata.hasIsoSpeed ? std::max(0.01f, metadata.isoSpeed / 100.0f) : 1.0f;
    const float aperture = metadata.hasApertureFNumber ? std::max(0.1f, metadata.apertureFNumber) : 1.0f;
    return SafeLog2((shutter * isoFactor) / std::max(0.01f, aperture * aperture));
}

float SelectRepresentativeExposureAnchor(
    const std::array<float, 3>& absoluteExposureEv,
    const std::array<bool, 3>& activeInputs) {
    std::vector<float> activeValues;
    activeValues.reserve(3);
    for (int i = 0; i < 3; ++i) {
        if (activeInputs[i]) {
            activeValues.push_back(absoluteExposureEv[i]);
        }
    }
    if (activeValues.empty()) {
        return 0.0f;
    }

    const float median = MedianFloat(activeValues);
    float bestDistance = std::numeric_limits<float>::infinity();
    float anchor = activeValues.front();
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            continue;
        }
        const float distance = std::abs(absoluteExposureEv[i] - median);
        if (distance < bestDistance) {
            bestDistance = distance;
            anchor = absoluteExposureEv[i];
        }
    }
    return anchor;
}

float EstimateHdrMergeIsoMultiplier(const Raw::RawMetadata& metadata) {
    if (!metadata.hasIsoSpeed) {
        return 1.0f;
    }
    return std::clamp(std::sqrt(std::max(1.0f, metadata.isoSpeed / 100.0f)), 1.0f, 6.0f);
}

float EstimateHdrMergeRangeStep(const Raw::RawMetadata& metadata) {
    const float range = std::max(256.0f, metadata.whiteLevel - metadata.blackLevel);
    return 1.0f / range;
}

bool ReadTextureToFloatRgba(unsigned int texture, int width, int height, std::vector<float>& outPixels) {
    outPixels.clear();
    if (!texture || width <= 0 || height <= 0) {
        return false;
    }

    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (!fbo) {
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    outPixels.assign(static_cast<std::size_t>(width * height * 4), 0.0f);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, outPixels.data());
    const GLenum readError = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return readError == GL_NO_ERROR;
}

HdrMergeFeatureImage BuildHdrMergeFeatureImage(
    const std::vector<float>& rgbaPixels,
    int sourceWidth,
    int sourceHeight,
    float exposureEv,
    float clipThreshold,
    float blackThreshold,
    int maxDimension) {
    HdrMergeFeatureImage feature;
    if (rgbaPixels.empty() || sourceWidth <= 0 || sourceHeight <= 0) {
        return feature;
    }

    const float scale = std::min(1.0f, static_cast<float>(std::max(32, maxDimension)) / static_cast<float>(std::max(sourceWidth, sourceHeight)));
    feature.width = std::max(24, static_cast<int>(std::round(sourceWidth * scale)));
    feature.height = std::max(24, static_cast<int>(std::round(sourceHeight * scale)));
    feature.scaleX = static_cast<float>(sourceWidth) / static_cast<float>(feature.width);
    feature.scaleY = static_cast<float>(sourceHeight) / static_cast<float>(feature.height);

    std::vector<float> logLuma(static_cast<std::size_t>(feature.width * feature.height), 0.0f);
    std::vector<float> sourceLuma(static_cast<std::size_t>(feature.width * feature.height), 0.0f);
    feature.values.assign(static_cast<std::size_t>(feature.width * feature.height), 0.0f);
    feature.thresholdBits.assign(static_cast<std::size_t>(feature.width * feature.height), 0u);
    feature.excludeBits.assign(static_cast<std::size_t>(feature.width * feature.height), 0u);
    float clippedCount = 0.0f;
    float blackCount = 0.0f;
    const float exposureScale = std::exp2(-exposureEv);

    for (int y = 0; y < feature.height; ++y) {
        const int srcY = std::clamp(static_cast<int>(std::round((static_cast<float>(y) + 0.5f) * feature.scaleY - 0.5f)), 0, sourceHeight - 1);
        for (int x = 0; x < feature.width; ++x) {
            const int srcX = std::clamp(static_cast<int>(std::round((static_cast<float>(x) + 0.5f) * feature.scaleX - 0.5f)), 0, sourceWidth - 1);
            const std::size_t srcIndex = static_cast<std::size_t>((srcY * sourceWidth + srcX) * 4);
            const float r = std::max(0.0f, rgbaPixels[srcIndex + 0]);
            const float g = std::max(0.0f, rgbaPixels[srcIndex + 1]);
            const float b = std::max(0.0f, rgbaPixels[srcIndex + 2]);
            const float sourceMax = std::max(r, std::max(g, b));
            const float sceneLuma = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
            const float normalizedLuma = std::max(0.0f, (0.2126f * r + 0.7152f * g + 0.0722f * b) * exposureScale);
            const std::size_t index = static_cast<std::size_t>(y * feature.width + x);
            sourceLuma[index] = sceneLuma;
            logLuma[index] = SafeLog2(normalizedLuma + 0.00003f);
            clippedCount += sourceMax >= clipThreshold ? 1.0f : 0.0f;
            blackCount += normalizedLuma <= blackThreshold ? 1.0f : 0.0f;
        }
    }

    const float pixelCount = static_cast<float>(feature.width * feature.height);
    feature.clipRatio = pixelCount > 0.0f ? clippedCount / pixelCount : 0.0f;
    feature.blackRatio = pixelCount > 0.0f ? blackCount / pixelCount : 0.0f;

    const float lumaMedian = MedianFloat(sourceLuma);
    const float excludeRange = std::max(lumaMedian * 0.08f, 0.0015f);
    for (std::size_t index = 0; index < sourceLuma.size(); ++index) {
        feature.thresholdBits[index] = sourceLuma[index] >= lumaMedian ? 1u : 0u;
        feature.excludeBits[index] = std::abs(sourceLuma[index] - lumaMedian) <= excludeRange ? 1u : 0u;
    }

    float detailEnergySum = 0.0f;
    for (int y = 0; y < feature.height; ++y) {
        for (int x = 0; x < feature.width; ++x) {
            float sum = 0.0f;
            float count = 0.0f;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int sx = std::clamp(x + ox, 0, feature.width - 1);
                    const int sy = std::clamp(y + oy, 0, feature.height - 1);
                    sum += logLuma[static_cast<std::size_t>(sy * feature.width + sx)];
                    count += 1.0f;
                }
            }
            const std::size_t index = static_cast<std::size_t>(y * feature.width + x);
            feature.values[index] = logLuma[index] - (count > 0.0f ? sum / count : 0.0f);
            detailEnergySum += std::abs(feature.values[index]);
        }
    }
    feature.detailEnergy = pixelCount > 0.0f ? detailEnergySum / pixelCount : 0.0f;

    return feature;
}

float HdrMergeTranslationScore(
    const HdrMergeFeatureImage& reference,
    const HdrMergeFeatureImage& candidate,
    int shiftX,
    int shiftY) {
    if (reference.width != candidate.width || reference.height != candidate.height ||
        reference.values.empty() || candidate.values.empty()) {
        return std::numeric_limits<float>::infinity();
    }

    const int xStart = std::max(0, -shiftX);
    const int yStart = std::max(0, -shiftY);
    const int xEnd = std::min(reference.width, reference.width - shiftX);
    const int yEnd = std::min(reference.height, reference.height - shiftY);
    if (xEnd - xStart < reference.width / 2 || yEnd - yStart < reference.height / 2) {
        return std::numeric_limits<float>::infinity();
    }

    float sum = 0.0f;
    float count = 0.0f;
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            const std::size_t refIndex = static_cast<std::size_t>(y * reference.width + x);
            const std::size_t candIndex = static_cast<std::size_t>((y + shiftY) * candidate.width + (x + shiftX));
            sum += std::abs(reference.values[refIndex] - candidate.values[candIndex]);
            count += 1.0f;
        }
    }
    return count > 0.0f ? sum / count : std::numeric_limits<float>::infinity();
}

float HdrMergeThresholdBitmapScore(
    const HdrMergeFeatureImage& reference,
    const HdrMergeFeatureImage& candidate,
    int shiftX,
    int shiftY) {
    if (reference.width != candidate.width || reference.height != candidate.height ||
        reference.thresholdBits.empty() || candidate.thresholdBits.empty() ||
        reference.excludeBits.empty() || candidate.excludeBits.empty()) {
        return std::numeric_limits<float>::infinity();
    }

    const int xStart = std::max(0, -shiftX);
    const int yStart = std::max(0, -shiftY);
    const int xEnd = std::min(reference.width, reference.width - shiftX);
    const int yEnd = std::min(reference.height, reference.height - shiftY);
    if (xEnd - xStart < reference.width / 2 || yEnd - yStart < reference.height / 2) {
        return std::numeric_limits<float>::infinity();
    }

    float mismatchCount = 0.0f;
    float validCount = 0.0f;
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            const std::size_t refIndex = static_cast<std::size_t>(y * reference.width + x);
            const std::size_t candIndex = static_cast<std::size_t>((y + shiftY) * candidate.width + (x + shiftX));
            if (reference.excludeBits[refIndex] != 0u || candidate.excludeBits[candIndex] != 0u) {
                continue;
            }
            mismatchCount += reference.thresholdBits[refIndex] != candidate.thresholdBits[candIndex] ? 1.0f : 0.0f;
            validCount += 1.0f;
        }
    }

    const float minimumValid = static_cast<float>(reference.width * reference.height) * 0.25f;
    if (validCount < minimumValid) {
        return std::numeric_limits<float>::infinity();
    }
    return mismatchCount / validCount;
}

HdrMergeAlignmentAnalysis AnalyzeHdrMergeAlignment(
    const std::array<unsigned int, 3>& textures,
    const std::array<bool, 3>& activeInputs,
    int width,
    int height,
    const Raw::HdrMergeSettings& settings,
    const std::array<float, 3>& effectiveExposureEv,
    const std::array<float, 3>& referenceExposureDistance,
    const std::array<float, 3>& clipThreshold,
    const std::array<float, 3>& blackThreshold) {
    HdrMergeAlignmentAnalysis analysis;
    analysis.valid = true;

    const int activeCount = static_cast<int>(activeInputs[0]) + static_cast<int>(activeInputs[1]) + static_cast<int>(activeInputs[2]);
    if (!activeInputs[0] || !activeInputs[1] || activeCount < 2) {
        analysis.valid = false;
        return analysis;
    }

    int requestedReference = 0;
    switch (settings.referenceMode) {
        case Raw::HdrMergeReferenceMode::Frame2: requestedReference = 1; break;
        case Raw::HdrMergeReferenceMode::Frame3: requestedReference = 2; break;
        case Raw::HdrMergeReferenceMode::Frame1:
        case Raw::HdrMergeReferenceMode::Auto:
        default:
            requestedReference = 0;
            break;
    }
    if (settings.referenceMode != Raw::HdrMergeReferenceMode::Auto && activeInputs[requestedReference]) {
        analysis.referenceIndex = requestedReference;
    }

    std::array<HdrMergeFeatureImage, 3> features {};
    bool gatheredAnyFeature = false;
    if (settings.alignmentMode != Raw::HdrMergeAlignmentMode::Off ||
        settings.referenceMode == Raw::HdrMergeReferenceMode::Auto) {
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i] || textures[i] == 0) {
                continue;
            }
            std::vector<float> rgbaPixels;
            if (!ReadTextureToFloatRgba(textures[i], width, height, rgbaPixels)) {
                continue;
            }
            features[i] = BuildHdrMergeFeatureImage(
                rgbaPixels,
                width,
                height,
                effectiveExposureEv[i],
                clipThreshold[i],
                blackThreshold[i],
                settings.alignmentMode == Raw::HdrMergeAlignmentMode::WideTranslation ? 192 : 160);
            gatheredAnyFeature = gatheredAnyFeature || !features[i].values.empty();
        }
    }

    if (settings.referenceMode == Raw::HdrMergeReferenceMode::Auto && gatheredAnyFeature) {
        float minExposureEv = std::numeric_limits<float>::infinity();
        float maxExposureEv = -std::numeric_limits<float>::infinity();
        float bestClipRatio = std::numeric_limits<float>::infinity();
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i] || features[i].values.empty()) {
                continue;
            }
            minExposureEv = std::min(minExposureEv, effectiveExposureEv[i]);
            maxExposureEv = std::max(maxExposureEv, effectiveExposureEv[i]);
            bestClipRatio = std::min(bestClipRatio, features[i].clipRatio);
        }
        const bool sameExposureBurst =
            std::isfinite(minExposureEv) &&
            std::isfinite(maxExposureEv) &&
            (maxExposureEv - minExposureEv) <= 0.35f;
        float bestScore = std::numeric_limits<float>::infinity();
        int bestIndex = 0;
        const float centerIndex = (activeInputs[0] && activeInputs[1] && activeInputs[2]) ? 1.0f : 0.5f;
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i] || features[i].values.empty()) {
                continue;
            }
            float score = 0.0f;
            if (sameExposureBurst) {
                score =
                    -features[i].detailEnergy +
                    features[i].clipRatio * 0.12f +
                    features[i].blackRatio * 0.10f +
                    std::abs(static_cast<float>(i) - centerIndex) * 0.03f;
            } else {
                const float extraClipPenalty = std::max(0.0f, features[i].clipRatio - (bestClipRatio + 0.02f));
                const float shadowPenalty = std::max(0.0f, features[i].blackRatio - 0.45f);
                score =
                    extraClipPenalty * 4.0f +
                    features[i].clipRatio * 0.60f +
                    shadowPenalty * 0.80f +
                    effectiveExposureEv[i] * 0.14f +
                    referenceExposureDistance[i] * 0.01f -
                    features[i].detailEnergy * 0.08f;
            }
            if (score < bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }
        analysis.referenceIndex = bestIndex;
    }

    if (settings.alignmentMode == Raw::HdrMergeAlignmentMode::Off || !gatheredAnyFeature) {
        for (int i = 0; i < 3; ++i) {
            analysis.confidence[i] = activeInputs[i] ? 1.0f : 0.0f;
        }
        return analysis;
    }

    const HdrMergeFeatureImage& reference = features[analysis.referenceIndex];
    if (reference.values.empty()) {
        return analysis;
    }
    const int maxShift = settings.alignmentMode == Raw::HdrMergeAlignmentMode::WideTranslation ? 20 : 12;
    float minActiveExposureEv = std::numeric_limits<float>::infinity();
    float maxActiveExposureEv = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            continue;
        }
        minActiveExposureEv = std::min(minActiveExposureEv, effectiveExposureEv[i]);
        maxActiveExposureEv = std::max(maxActiveExposureEv, effectiveExposureEv[i]);
    }
    const bool sameExposureBurst =
        std::isfinite(minActiveExposureEv) &&
        std::isfinite(maxActiveExposureEv) &&
        (maxActiveExposureEv - minActiveExposureEv) <= 0.35f;
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            analysis.confidence[i] = 0.0f;
            continue;
        }
        if (i == analysis.referenceIndex) {
            analysis.confidence[i] = 1.0f;
            continue;
        }
        const HdrMergeFeatureImage& candidate = features[i];
        if (candidate.values.empty()) {
            analysis.confidence[i] = 0.0f;
            continue;
        }

        float bestDetailScore = std::numeric_limits<float>::infinity();
        float bestThresholdScore = std::numeric_limits<float>::infinity();
        int bestShiftX = 0;
        int bestShiftY = 0;
        for (int dy = -maxShift; dy <= maxShift; ++dy) {
            for (int dx = -maxShift; dx <= maxShift; ++dx) {
                const float detailScore = HdrMergeTranslationScore(reference, candidate, dx, dy);
                const float thresholdScore = HdrMergeThresholdBitmapScore(reference, candidate, dx, dy);

                bool better = false;
                if (sameExposureBurst) {
                    if (detailScore < bestDetailScore - 0.0001f) {
                        better = true;
                    } else if (std::abs(detailScore - bestDetailScore) <= 0.0001f &&
                               thresholdScore < bestThresholdScore) {
                        better = true;
                    }
                } else {
                    if (thresholdScore < bestThresholdScore - 0.0025f) {
                        better = true;
                    } else if (std::abs(thresholdScore - bestThresholdScore) <= 0.0025f &&
                               detailScore < bestDetailScore) {
                        better = true;
                    }
                }

                if (better) {
                    bestDetailScore = detailScore;
                    bestThresholdScore = thresholdScore;
                    bestShiftX = dx;
                    bestShiftY = dy;
                }
            }
        }

        const float zeroDetailScore = HdrMergeTranslationScore(reference, candidate, 0, 0);
        const float zeroThresholdScore = HdrMergeThresholdBitmapScore(reference, candidate, 0, 0);
        const float scaleX = candidate.scaleX;
        const float scaleY = candidate.scaleY;
        analysis.offsetX[i] = -static_cast<float>(bestShiftX) * scaleX;
        analysis.offsetY[i] = -static_cast<float>(bestShiftY) * scaleY;
        const float detailGain = (std::isfinite(zeroDetailScore) && zeroDetailScore > 0.00001f &&
                                  std::isfinite(bestDetailScore))
            ? std::max(0.0f, (zeroDetailScore - bestDetailScore) / zeroDetailScore)
            : 0.0f;
        const float thresholdGain = (std::isfinite(zeroThresholdScore) && zeroThresholdScore > 0.00001f &&
                                     std::isfinite(bestThresholdScore))
            ? std::max(0.0f, (zeroThresholdScore - bestThresholdScore) / zeroThresholdScore)
            : 0.0f;
        const float relativeGain = sameExposureBurst
            ? (detailGain * 0.68f + thresholdGain * 0.32f)
            : (thresholdGain * 0.72f + detailGain * 0.28f);
        const float shiftPenalty = 1.0f - std::min(1.0f, static_cast<float>(std::abs(bestShiftX) + std::abs(bestShiftY)) / static_cast<float>(maxShift * 2));
        analysis.confidence[i] = Clamp01(relativeGain * 0.7f + shiftPenalty * 0.3f);
    }

    return analysis;
}

std::vector<int> Utf8ToCodepoints(const std::string& text) {
    std::vector<int> codepoints;
    codepoints.reserve(text.size());

    for (std::size_t index = 0; index < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead < 0x80) {
            codepoints.push_back(static_cast<int>(lead));
            ++index;
            continue;
        }

        int codepoint = 0;
        std::size_t count = 0;
        if ((lead & 0xE0u) == 0xC0u) {
            codepoint = lead & 0x1Fu;
            count = 2;
        } else if ((lead & 0xF0u) == 0xE0u) {
            codepoint = lead & 0x0Fu;
            count = 3;
        } else if ((lead & 0xF8u) == 0xF0u) {
            codepoint = lead & 0x07u;
            count = 4;
        } else {
            codepoints.push_back('?');
            ++index;
            continue;
        }

        if (index + count > text.size()) {
            codepoints.push_back('?');
            break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | static_cast<int>(continuation & 0x3Fu);
        }

        codepoints.push_back(valid ? codepoint : '?');
        index += valid ? count : 1;
    }

    return codepoints;
}

const std::vector<unsigned char>& DefaultGeneratorTextFontBytes() {
    static const std::vector<unsigned char> kFontBytes(
        std::begin(EmbeddedCompositeFont::kRobotoMediumTtf),
        std::end(EmbeddedCompositeFont::kRobotoMediumTtf));
    return kFontBytes;
}

bool BuildTextRgba(
    const std::vector<unsigned char>& fontBytes,
    const std::string& text,
    float fontSize,
    const std::array<float, 4>& color,
    const float stretchX,
    const float stretchY,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight) {
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;
    if (fontBytes.empty()) {
        return false;
    }

    stbtt_fontinfo fontInfo {};
    if (!stbtt_InitFont(&fontInfo, fontBytes.data(), 0)) {
        return false;
    }

    struct GlyphPlacement {
        int codepoint = 0;
        float penX = 0.0f;
        float baselineY = 0.0f;
    };

    const std::vector<int> codepoints = Utf8ToCodepoints(text.empty() ? std::string("Text") : text);
    const uint8_t r = static_cast<uint8_t>(std::round(Clamp01(color[0]) * 255.0f));
    const uint8_t g = static_cast<uint8_t>(std::round(Clamp01(color[1]) * 255.0f));
    const uint8_t b = static_cast<uint8_t>(std::round(Clamp01(color[2]) * 255.0f));
    const float baseAlpha = Clamp01(color[3]);
    const int textureLimit = 8192;
    const float requestedStretchX = std::max(0.01f, stretchX);
    const float requestedStretchY = std::max(0.01f, stretchY);
    float requestedPixelSize = std::max(1.0f, fontSize);

    for (int attempt = 0; attempt < 6; ++attempt) {
        const float pixelSize = std::max(1.0f, requestedPixelSize);
        const float baseScale = stbtt_ScaleForPixelHeight(&fontInfo, pixelSize);
        const float scaleX = baseScale * requestedStretchX;
        const float scaleY = baseScale * requestedStretchY;
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
        const float lineAdvance = std::max(1.0f, (ascent - descent + lineGap) * scaleY);
        const float baseline = ascent * scaleY;

        std::vector<GlyphPlacement> placements;
        placements.reserve(codepoints.size());

        float penX = 0.0f;
        float penY = baseline;
        float maxLineWidth = 0.0f;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        int previousCodepoint = 0;

        for (int codepoint : codepoints) {
            if (codepoint == '\r') {
                continue;
            }
            if (codepoint == '\n') {
                maxLineWidth = std::max(maxLineWidth, penX);
                penX = 0.0f;
                penY += lineAdvance;
                previousCodepoint = 0;
                continue;
            }

            if (previousCodepoint != 0) {
                penX += stbtt_GetCodepointKernAdvance(&fontInfo, previousCodepoint, codepoint) * scaleX;
            }

            placements.push_back({ codepoint, penX, penY });

            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetCodepointBitmapBox(&fontInfo, codepoint, scaleX, scaleY, &x0, &y0, &x1, &y1);
            minX = std::min(minX, penX + static_cast<float>(x0));
            minY = std::min(minY, penY + static_cast<float>(y0));
            maxX = std::max(maxX, penX + static_cast<float>(x1));
            maxY = std::max(maxY, penY + static_cast<float>(y1));

            int advance = 0;
            int leftSideBearing = 0;
            stbtt_GetCodepointHMetrics(&fontInfo, codepoint, &advance, &leftSideBearing);
            penX += advance * scaleX;
            maxLineWidth = std::max(maxLineWidth, penX);
            previousCodepoint = codepoint;
        }

        const float padding = std::max(2.0f, pixelSize * 0.18f);
        const bool hasVisibleBounds = minX <= maxX && minY <= maxY;
        const float fallbackWidth = std::max(1.0f, maxLineWidth);
        const float fallbackHeight = std::max(1.0f, penY + lineAdvance - baseline);
        const float contentWidth = hasVisibleBounds ? (maxX - minX) : fallbackWidth;
        const float contentHeight = hasVisibleBounds ? (maxY - minY) : fallbackHeight;
        const int candidateWidth = std::max(1, static_cast<int>(std::ceil(contentWidth + padding * 2.0f)));
        const int candidateHeight = std::max(1, static_cast<int>(std::ceil(contentHeight + padding * 2.0f)));

        if ((candidateWidth > textureLimit || candidateHeight > textureLimit) && pixelSize > 1.0f) {
            const float fit = std::min(
                static_cast<float>(textureLimit) / static_cast<float>(std::max(1, candidateWidth)),
                static_cast<float>(textureLimit) / static_cast<float>(std::max(1, candidateHeight)));
            if (fit < 0.999f) {
                requestedPixelSize = std::max(1.0f, pixelSize * std::clamp(fit * 0.98f, 0.1f, 0.98f));
                continue;
            }
        }

        outWidth = std::min(candidateWidth, textureLimit);
        outHeight = std::min(candidateHeight, textureLimit);
        outPixels.assign(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4, 0);
        for (std::size_t pixelIndex = 0; pixelIndex < outPixels.size(); pixelIndex += 4) {
            outPixels[pixelIndex + 0] = r;
            outPixels[pixelIndex + 1] = g;
            outPixels[pixelIndex + 2] = b;
        }

        const float originX = hasVisibleBounds ? (padding - minX) : padding;
        const float originY = hasVisibleBounds ? (padding - minY) : padding;
        for (const GlyphPlacement& placement : placements) {
            int glyphW = 0;
            int glyphH = 0;
            int glyphXOff = 0;
            int glyphYOff = 0;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(
                &fontInfo,
                scaleX,
                scaleY,
                placement.codepoint,
                &glyphW,
                &glyphH,
                &glyphXOff,
                &glyphYOff);
            if (!bitmap || glyphW <= 0 || glyphH <= 0) {
                if (bitmap) {
                    stbtt_FreeBitmap(bitmap, nullptr);
                }
                continue;
            }

            const int destX = static_cast<int>(std::floor(placement.penX + static_cast<float>(glyphXOff) + originX));
            const int destY = static_cast<int>(std::floor(placement.baselineY + static_cast<float>(glyphYOff) + originY));
            for (int y = 0; y < glyphH; ++y) {
                const int py = destY + y;
                if (py < 0 || py >= outHeight) {
                    continue;
                }
                for (int x = 0; x < glyphW; ++x) {
                    const int px = destX + x;
                    if (px < 0 || px >= outWidth) {
                        continue;
                    }

                    const float glyphAlpha = static_cast<float>(bitmap[y * glyphW + x]) / 255.0f;
                    if (glyphAlpha <= 0.0f) {
                        continue;
                    }

                    const std::size_t pixelIndex =
                        (static_cast<std::size_t>(py) * static_cast<std::size_t>(outWidth) + static_cast<std::size_t>(px)) * 4;
                    const float srcAlpha = baseAlpha * glyphAlpha;
                    const float dstAlpha = static_cast<float>(outPixels[pixelIndex + 3]) / 255.0f;
                    const float outAlpha = srcAlpha + dstAlpha * (1.0f - srcAlpha);
                    if (outAlpha <= 0.0f) {
                        continue;
                    }

                    auto blendChannel = [&](const uint8_t srcChannel, const uint8_t dstChannel) {
                        const float srcNorm = static_cast<float>(srcChannel) / 255.0f;
                        const float dstNorm = static_cast<float>(dstChannel) / 255.0f;
                        return static_cast<uint8_t>(std::round(((srcNorm * srcAlpha) + (dstNorm * dstAlpha * (1.0f - srcAlpha))) / outAlpha * 255.0f));
                    };

                    outPixels[pixelIndex + 0] = blendChannel(r, outPixels[pixelIndex + 0]);
                    outPixels[pixelIndex + 1] = blendChannel(g, outPixels[pixelIndex + 1]);
                    outPixels[pixelIndex + 2] = blendChannel(b, outPixels[pixelIndex + 2]);
                    outPixels[pixelIndex + 3] = static_cast<uint8_t>(std::round(outAlpha * 255.0f));
                }
            }

            stbtt_FreeBitmap(bitmap, nullptr);
        }

        return !outPixels.empty();
    }

    return false;
}

bool BuildTextGeneratorCanvas(const RenderGraphNode& node, int canvasWidth, int canvasHeight, std::vector<unsigned char>& outPixels) {
    outPixels.clear();
    if (canvasWidth <= 0 || canvasHeight <= 0) {
        return false;
    }

    const std::array<float, 4> color = {
        node.imageGeneratorSettings.colorA[0],
        node.imageGeneratorSettings.colorA[1],
        node.imageGeneratorSettings.colorA[2],
        node.imageGeneratorSettings.colorA[3]
    };

    std::vector<uint8_t> textPixels;
    int textWidth = 0;
    int textHeight = 0;
    float effectiveFontSize = std::max(8.0f, node.imageGeneratorSettings.fontSize) *
        std::max(0.35f, std::min(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight)) / 256.0f);

    for (int attempt = 0; attempt < 6; ++attempt) {
        if (!BuildTextRgba(
                DefaultGeneratorTextFontBytes(),
                node.imageGeneratorSettings.text,
                effectiveFontSize,
                color,
                1.0f,
                1.0f,
                textPixels,
                textWidth,
                textHeight)) {
            return false;
        }

        if (textWidth <= std::max(1, canvasWidth - 6) && textHeight <= std::max(1, canvasHeight - 6)) {
            break;
        }

        const float fit = std::min(
            static_cast<float>(std::max(1, canvasWidth - 6)) / static_cast<float>(std::max(1, textWidth)),
            static_cast<float>(std::max(1, canvasHeight - 6)) / static_cast<float>(std::max(1, textHeight)));
        effectiveFontSize = std::max(6.0f, effectiveFontSize * std::clamp(fit * 0.97f, 0.1f, 0.97f));
    }

    outPixels.assign(static_cast<std::size_t>(canvasWidth) * static_cast<std::size_t>(canvasHeight) * 4, 0);
    const int offsetX = std::max(0, (canvasWidth - textWidth) / 2);
    const int offsetY = std::max(0, (canvasHeight - textHeight) / 2);
    for (int y = 0; y < textHeight; ++y) {
        const int dstY = offsetY + y;
        if (dstY < 0 || dstY >= canvasHeight) {
            continue;
        }
        for (int x = 0; x < textWidth; ++x) {
            const int dstX = offsetX + x;
            if (dstX < 0 || dstX >= canvasWidth) {
                continue;
            }
            const std::size_t srcIndex =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(textWidth) + static_cast<std::size_t>(x)) * 4;
            const std::size_t dstIndex =
                (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(canvasWidth) + static_cast<std::size_t>(dstX)) * 4;
            outPixels[dstIndex + 0] = textPixels[srcIndex + 0];
            outPixels[dstIndex + 1] = textPixels[srcIndex + 1];
            outPixels[dstIndex + 2] = textPixels[srcIndex + 2];
            outPixels[dstIndex + 3] = textPixels[srcIndex + 3];
        }
    }

    return true;
}

} // namespace

RenderPipeline::RenderPipeline()
    : m_Width(0), m_Height(0),
      m_SourceChannels(4),
      m_SourceTexture(0), m_PingTexture(0), m_PongTexture(0),
      m_PingFBO(0), m_PongFBO(0), m_OutputTexture(0), m_ExternalOutputTexture(0), m_GraphSourceTexture(0),
      m_MaskProgram(0), m_MaskCombineProgram(0), m_MaskBlendProgram(0), m_MixProgram(0),
      m_MaskUtilityProgram(0), m_ImageToMaskProgram(0), m_ImageGeneratorProgram(0),
      m_DataMathProgram(0), m_ChannelSplitProgram(0), m_ChannelCombineProgram(0), m_LutProgram(0),
      m_HdrMergeProgram(0),
      m_RawDetailFusionAnalysisProgram(0), m_RawDetailFusionMetricsProgram(0), m_RawDetailFusionSmoothProgram(0), m_RawDetailFusionApplyProgram(0),
      m_AutoGainStatsProgram(0)
{}

RenderPipeline::~RenderPipeline() {
    CleanupFBOs();
    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    if (m_ExternalOutputTexture) glDeleteTextures(1, &m_ExternalOutputTexture);
    if (m_MaskProgram) glDeleteProgram(m_MaskProgram);
    if (m_MaskCombineProgram) glDeleteProgram(m_MaskCombineProgram);
    if (m_MaskBlendProgram) glDeleteProgram(m_MaskBlendProgram);
    if (m_MixProgram) glDeleteProgram(m_MixProgram);
    if (m_MaskUtilityProgram) glDeleteProgram(m_MaskUtilityProgram);
    if (m_ImageToMaskProgram) glDeleteProgram(m_ImageToMaskProgram);
    if (m_ImageGeneratorProgram) glDeleteProgram(m_ImageGeneratorProgram);
    if (m_DataMathProgram) glDeleteProgram(m_DataMathProgram);
    if (m_ChannelSplitProgram) glDeleteProgram(m_ChannelSplitProgram);
    if (m_ChannelCombineProgram) glDeleteProgram(m_ChannelCombineProgram);
    if (m_LutProgram) glDeleteProgram(m_LutProgram);
    if (m_HdrMergeProgram) glDeleteProgram(m_HdrMergeProgram);
    if (m_RawDetailFusionAnalysisProgram) glDeleteProgram(m_RawDetailFusionAnalysisProgram);
    if (m_RawDetailFusionMetricsProgram) glDeleteProgram(m_RawDetailFusionMetricsProgram);
    if (m_RawDetailFusionSmoothProgram) glDeleteProgram(m_RawDetailFusionSmoothProgram);
    if (m_RawDetailFusionApplyProgram) glDeleteProgram(m_RawDetailFusionApplyProgram);
    if (m_AutoGainStatsProgram) glDeleteProgram(m_AutoGainStatsProgram);
}

void RenderPipeline::Initialize() {
    m_Quad.Initialize();
}

void RenderPipeline::CleanupFBOs() {
    if (m_PingFBO)     { glDeleteFramebuffers(1, &m_PingFBO);  m_PingFBO = 0; }
    if (m_PongFBO)     { glDeleteFramebuffers(1, &m_PongFBO);  m_PongFBO = 0; }
    if (m_PingTexture) { glDeleteTextures(1, &m_PingTexture); m_PingTexture = 0; }
    if (m_PongTexture) { glDeleteTextures(1, &m_PongTexture); m_PongTexture = 0; }
}

void RenderPipeline::DestroyGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache) {
    for (auto& [key, entry] : cache) {
        if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &entry.texture);
        }
    }
    cache.clear();
}

void RenderPipeline::DestroyRawDevelopStageCache() {
    for (auto& [key, entries] : m_RawDevelopStageImageCache) {
        for (CachedGraphTexture& entry : entries) {
            if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
                glDeleteTextures(1, &entry.texture);
            }
        }
    }
    m_RawDevelopStageImageCache.clear();
}

std::uint64_t RenderPipeline::EstimateRawDevelopStageCacheTextureBytesForValidation(int width, int height) {
    return EstimateRawDevelopStageCacheTextureBytes(width, height);
}

std::size_t RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(int width, int height) {
    return ResolveRawDevelopStageCacheMaxEntries(width, height);
}

bool RenderPipeline::ShouldCacheRawDevelopStageTextureForValidation(int width, int height) {
    return ResolveRawDevelopStageCacheMaxEntries(width, height) > 0;
}

void RenderPipeline::InvalidateGraphCaches() {
    DestroyGraphCache(m_GraphImageCache);
    DestroyGraphCache(m_GraphMaskCache);
    DestroyGraphCache(m_LutTextureCache);
    DestroyRawDevelopStageCache();
    m_LastGraphImageCacheHits.clear();
    m_AutoGainSceneStatsCache.clear();
}

void RenderPipeline::Resize(int width, int height) {
    if (width == m_Width && height == m_Height) return;
    m_Width = width;
    m_Height = height;

    CleanupFBOs();

    m_PingTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PongTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PingFBO = GLHelpers::CreateFBO(m_PingTexture);
    m_PongFBO = GLHelpers::CreateFBO(m_PongTexture);
}

bool RenderPipeline::LoadSourceImage(const std::string& filepath) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(filepath.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "[RenderPipeline] Failed to load image: " << filepath
                  << " (" << stbi_failure_reason() << ")\n";
        return false;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = GLHelpers::CreateTextureFromPixels(data, w, h, 4);
    m_SourceChannels = 4;
    m_SourcePixelsShared.reset();
    m_SourcePixels.assign(data, data + (w * h * 4));
    m_SourceFingerprint = HashBytes(m_SourcePixels);
    stbi_image_free(data);

    Resize(w, h);

    std::cout << "[RenderPipeline] Loaded image " << w << "x" << h << " from: " << filepath << "\n";
    return true;
}

void RenderPipeline::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    const int clampedChannels = std::max(1, ch);
    int targetWidth = w;
    int targetHeight = h;
    if (!data && m_PreviewMaxDimension > 0 && targetWidth > 0 && targetHeight > 0) {
        const int longestSide = std::max(targetWidth, targetHeight);
        if (longestSide > m_PreviewMaxDimension) {
            targetWidth = std::max(1, static_cast<int>(
                (static_cast<long long>(targetWidth) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
            targetHeight = std::max(1, static_cast<int>(
                (static_cast<long long>(targetHeight) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
        }
    }
    const std::size_t pixelCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const std::size_t incomingSize = data ? (pixelCount * static_cast<std::size_t>(clampedChannels)) : 0;
    const std::size_t incomingFingerprint = (!data || incomingSize == 0) ? 0 : HashBytes(data, incomingSize);
    if (m_SourceTexture != 0 &&
        m_Width == targetWidth &&
        m_Height == targetHeight &&
        m_SourceChannels == ch &&
        m_SourceFingerprint == incomingFingerprint &&
        m_SourcePixels.size() == incomingSize) {
        return;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = data ? GLHelpers::CreateTextureFromPixels(data, w, h, ch) : 0;
    m_SourceChannels = ch;
    m_SourcePixelsShared.reset();
    if (data && incomingSize > 0) {
        m_SourcePixels.assign(data, data + incomingSize);
    } else {
        m_SourcePixels.clear();
    }
    m_SourceFingerprint = incomingFingerprint;
    Resize(targetWidth, targetHeight);
}

void RenderPipeline::LoadSourceFromSharedPixels(const SharedPixelBuffer& data, int w, int h, int ch) {
    const int clampedChannels = std::max(1, ch);
    int targetWidth = w;
    int targetHeight = h;
    if (data.empty() && m_PreviewMaxDimension > 0 && targetWidth > 0 && targetHeight > 0) {
        const int longestSide = std::max(targetWidth, targetHeight);
        if (longestSide > m_PreviewMaxDimension) {
            targetWidth = std::max(1, static_cast<int>(
                (static_cast<long long>(targetWidth) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
            targetHeight = std::max(1, static_cast<int>(
                (static_cast<long long>(targetHeight) * m_PreviewMaxDimension + longestSide / 2) / longestSide));
        }
    }

    const std::size_t incomingFingerprint =
        data.fingerprint != 0
            ? data.fingerprint
            : (data.empty() ? 0 : StackHash::HashBytes(*data.bytes));
    if (m_SourceTexture != 0 &&
        m_Width == targetWidth &&
        m_Height == targetHeight &&
        m_SourceChannels == ch &&
        m_SourceFingerprint == incomingFingerprint &&
        m_SourcePixelsShared == data.bytes &&
        m_SourcePixels.empty()) {
        return;
    }

    InvalidateGraphCaches();
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
    }
    m_SourceTexture = !data.empty()
        ? GLHelpers::CreateTextureFromPixels(data.data(), w, h, ch)
        : 0;
    m_SourceChannels = ch;
    m_SourcePixels.clear();
    m_SourcePixelsShared = data.bytes;
    m_SourceFingerprint = incomingFingerprint;
    Resize(targetWidth, targetHeight);
}

void RenderPipeline::Clear() {
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
        m_SourceTexture = 0;
    }
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_OutputTexture = 0;
    m_GraphSourceTexture = 0;
    m_SourcePixelsShared.reset();
    m_SourcePixels.clear();
    m_SourceFingerprint = 0;
    m_SourceChannels = 4;
    m_Width = 0;
    m_Height = 0;
    CleanupFBOs();
    InvalidateGraphCaches();
    m_RawPipelines.clear();
    m_RawDataCache.clear();
    m_RawDataCachePaths.clear();
}

void RenderPipeline::ClearOutput() {
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_OutputTexture = 0;
    m_GraphSourceTexture = 0;
}

void RenderPipeline::UploadOutputFromPixels(const unsigned char* data, int w, int h, int ch) {
    if (!data || w <= 0 || h <= 0) {
        ClearOutput();
        return;
    }
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_ExternalOutputTexture = GLHelpers::CreateTextureFromPixels(data, w, h, ch);
    m_OutputTexture = m_ExternalOutputTexture;
    m_Width = w;
    m_Height = h;
}

void RenderPipeline::AdoptExternalOutputTexture(unsigned int texture, int w, int h) {
    if (m_ExternalOutputTexture && m_ExternalOutputTexture != texture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
    }
    m_ExternalOutputTexture = texture;
    m_OutputTexture = texture;
    m_Width = w;
    m_Height = h;
}

unsigned int RenderPipeline::PublishSharedOutputTexture(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    if (m_OutputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    const unsigned int publishedTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (publishedTexture == 0) {
        return 0;
    }

    GLint prevReadFBO = 0;
    GLint prevDrawFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int srcFBO = 0;
    unsigned int dstFBO = 0;
    glGenFramebuffers(1, &srcFBO);
    glGenFramebuffers(1, &dstFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, publishedTexture, 0);
    glBlitFramebuffer(
        0, 0, m_Width, m_Height,
        0, 0, m_Width, m_Height,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &dstFBO);
    return publishedTexture;
}

void RenderPipeline::Execute(const std::vector<std::shared_ptr<LayerBase>>& layers) {
    m_GraphSourceTexture = 0;
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture; // Nothing to process
        return;
    }

    // Save previous GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    // Save and disable states that might interfere with full-screen quad rendering
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glViewport(0, 0, m_Width, m_Height);

    int activeCount = 0;
    for (auto& layer : layers) {
        if (layer->IsVisible()) activeCount++;
    }

    if (activeCount == 0) {
        // No layers active, output is just the source
        m_OutputTexture = m_SourceTexture;
        
        // Restore
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        if (prevScissor) glEnable(GL_SCISSOR_TEST);
        if (prevDepth) glEnable(GL_DEPTH_TEST);
        if (prevStencil) glEnable(GL_STENCIL_TEST);
        if (prevBlend) glEnable(GL_BLEND);
        return;
    }

    // Sequential ping-pong execution:
    // Layer 1 reads Source → writes Ping
    // Layer 2 reads Ping → writes Pong
    // Layer 3 reads Pong → writes Ping ... etc.
    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;

    for (auto& layer : layers) {
        if (!layer->IsVisible()) continue;

        unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
        unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glClear(GL_COLOR_BUFFER_BIT);

        layer->ExecuteWithSource(currentInput, m_SourceTexture, m_Width, m_Height, m_Quad);

        currentInput = targetTex;
        usePing = !usePing;
    }

    m_OutputTexture = currentInput;

    // Restore previous GL state
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

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

unsigned int RenderPipeline::GenerateMaskTexture(const RenderMaskSource& mask) {
    EnsureMaskPrograms();
    if (!m_MaskProgram || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    unsigned int texture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    unsigned int fbo = GLHelpers::CreateFBO(texture);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_Width, m_Height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_MaskProgram);
    glUniform1i(glGetUniformLocation(m_MaskProgram, "uKind"), static_cast<int>(mask.kind));
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uValue"), std::clamp(mask.settings.value, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uAngle"), mask.settings.angle);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uOffset"), mask.settings.offset);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uScale"), mask.settings.scale);
    glUniform2f(glGetUniformLocation(m_MaskProgram, "uCenter"), mask.settings.centerX, mask.settings.centerY);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uRadius"), mask.settings.radius);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uFeather"), mask.settings.feather);
    glUniform1i(glGetUniformLocation(m_MaskProgram, "uInvert"), mask.settings.invert ? 1 : 0);
    m_Quad.Draw();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return texture;
}

unsigned int RenderPipeline::GenerateCustomMaskTexture(const RenderCustomMaskPayload& payload) {
    if (m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    const std::vector<float> values = FlattenCustomMaskPayload(payload, m_Width, m_Height);
    if (values.empty()) {
        return 0;
    }

    std::vector<float> red(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        red[i] = Clamp01(values[i]);
    }

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m_Width, m_Height, 0, GL_RED, GL_FLOAT, red.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void RenderPipeline::RenderMaskBlend(unsigned int originalTexture, unsigned int processedTexture, unsigned int maskTexture, unsigned int targetFBO) {
    EnsureMaskPrograms();
    if (!m_MaskBlendProgram || !originalTexture || !processedTexture || !maskTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskBlendProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, originalTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uOriginal"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, processedTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uProcessed"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, maskTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uMask"), 2);

    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderMaskCombine(unsigned int maskA, unsigned int maskB, RenderMaskCombineMode mode, unsigned int targetFBO) {
    EnsureMaskPrograms();
    if (!m_MaskCombineProgram || !maskA || !maskB) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskCombineProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, maskA);
    glUniform1i(glGetUniformLocation(m_MaskCombineProgram, "uMaskA"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, maskB);
    glUniform1i(glGetUniformLocation(m_MaskCombineProgram, "uMaskB"), 1);

    glUniform1i(glGetUniformLocation(m_MaskCombineProgram, "uMode"), static_cast<int>(mode));
    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
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
                float outA = b.a + a.a * (1.0 - b.a);
                vec3 outRgb = b.rgb * b.a + a.rgb * (1.0 - b.a);
                if (outA > 0.0001) {
                    outRgb /= outA;
                }
                blended = vec4(outRgb, outA);
            }
            FragColor = mix(a, blended, factor);
        }
    )";

    if (!m_MixProgram) {
        m_MixProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::RenderMixBlend(unsigned int textureA, unsigned int textureB, unsigned int factorTexture, float factor, RenderMixBlendMode mode, unsigned int targetFBO) {
    EnsureMixProgram();
    if (!m_MixProgram || !textureA || !textureB) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MixProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureA);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uImageA"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureB);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uImageB"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, factorTexture);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uFactorMask"), 2);

    glUniform1i(glGetUniformLocation(m_MixProgram, "uHasFactorMask"), factorTexture ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_MixProgram, "uFactor"), factor);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uBlendMode"), static_cast<int>(mode));
    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
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
            vec4 source = texture(uImage, vTexCoord);
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

void RenderPipeline::RenderDataMath(
    unsigned int textureA,
    unsigned int textureB,
    bool hasA,
    bool hasB,
    bool scalarA,
    bool scalarB,
    RenderDataMathMode mode,
    const RenderDataMathSettings& settings,
    bool scalarOutput,
    unsigned int targetFBO) {
    EnsureDataMathProgram();
    if (!m_DataMathProgram || (!textureA && hasA) || (!textureB && hasB)) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_DataMathProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureA);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uDataA"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureB);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uDataB"), 1);

    const float minValue = std::min(settings.minValue, settings.maxValue);
    const float maxValue = std::max(settings.minValue, settings.maxValue);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uHasA"), hasA ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uHasB"), hasB ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uScalarA"), scalarA ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uScalarB"), scalarB ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uMode"), static_cast<int>(mode));
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uConstantA"), settings.constantA);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uConstantB"), settings.constantB);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uMinValue"), minValue);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uMaxValue"), maxValue);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uOutMin"), settings.outMin);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uOutMax"), settings.outMax);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uScalarOutput"), scalarOutput ? 1 : 0);
    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
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

bool RenderPipeline::RenderHdrMerge(
    unsigned int texture1,
    unsigned int texture2,
    unsigned int texture3,
    bool hasTexture2,
    bool hasTexture3,
    const Raw::HdrMergeSettings& settings,
    const HdrMergeResolvedSettings& resolved,
    unsigned int targetFBO) {
    EnsureHdrMergeProgram();
    if (!m_HdrMergeProgram || !texture1) {
        return false;
    }

    const bool useTexture2 = hasTexture2 && texture2 != 0;
    const bool useTexture3 = hasTexture3 && texture3 != 0;
    if (!useTexture2) {
        return false;
    }

    const auto queryTextureSize = [](unsigned int texture, int& outWidth, int& outHeight) {
        outWidth = 0;
        outHeight = 0;
        if (!texture) {
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &outWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &outHeight);
        return outWidth > 0 && outHeight > 0;
    };

    int width1 = 0;
    int height1 = 0;
    int width2 = 0;
    int height2 = 0;
    int width3 = 0;
    int height3 = 0;
    if (!queryTextureSize(texture1, width1, height1) ||
        !queryTextureSize(texture2, width2, height2) ||
        (useTexture3 && !queryTextureSize(texture3, width3, height3))) {
        return false;
    }
    if (width2 != width1 || height2 != height1 ||
        (useTexture3 && (width3 != width1 || height3 != height1))) {
        return false;
    }

    std::array<unsigned int, 3> textures { texture1, texture2, texture3 };
    std::array<bool, 3> activeInputs { true, true, useTexture3 };
    const HdrMergeAlignmentAnalysis alignment = AnalyzeHdrMergeAlignment(
        textures,
        activeInputs,
        width1,
        height1,
        settings,
        resolved.exposureEv,
        resolved.referenceExposureDistance,
        resolved.clipThreshold,
        resolved.blackThreshold);
    float deghostStrength = 0.0f;
    switch (settings.deghostMode) {
        case Raw::HdrMergeDeghostMode::Low: deghostStrength = 0.35f; break;
        case Raw::HdrMergeDeghostMode::Medium: deghostStrength = 0.65f; break;
        case Raw::HdrMergeDeghostMode::High: deghostStrength = 0.90f; break;
        case Raw::HdrMergeDeghostMode::Off:
        default:
            deghostStrength = 0.0f;
            break;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_HdrMergeProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture1);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uInput1"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, useTexture2 ? texture2 : texture1);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uInput2"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, useTexture3 ? texture3 : texture1);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uInput3"), 2);

    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uHasInput2"), useTexture2 ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uHasInput3"), useTexture3 ? 1 : 0);
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uTexelSize"), 1.0f / static_cast<float>(width1), 1.0f / static_cast<float>(height1));
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uInputOffsetPx[0]"), alignment.offsetX[0], alignment.offsetY[0]);
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uInputOffsetPx[1]"), alignment.offsetX[1], alignment.offsetY[1]);
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uInputOffsetPx[2]"), alignment.offsetX[2], alignment.offsetY[2]);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uReferenceIndex"), std::clamp(alignment.referenceIndex, 0, useTexture3 ? 2 : 1));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uAlignmentConfidence"), alignment.confidence[0], alignment.confidence[1], alignment.confidence[2]);
    glUniform1f(glGetUniformLocation(m_HdrMergeProgram, "uDeghostStrength"), deghostStrength);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uMotionPriority"),
        settings.motionPriority == Raw::HdrMergeMotionPriority::PreserveReference ? 0 : 1);
    glUniform3f(
        glGetUniformLocation(m_HdrMergeProgram, "uExposureEv"),
        resolved.exposureEv[0],
        resolved.exposureEv[1],
        resolved.exposureEv[2]);
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uClipThreshold"),
        std::max(0.0f, resolved.clipThreshold[0]),
        std::max(0.0f, resolved.clipThreshold[1]),
        std::max(0.0f, resolved.clipThreshold[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uClipFeather"),
        std::max(0.0001f, resolved.clipFeather[0]),
        std::max(0.0001f, resolved.clipFeather[1]),
        std::max(0.0001f, resolved.clipFeather[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uBlackThreshold"),
        std::max(0.0f, resolved.blackThreshold[0]),
        std::max(0.0f, resolved.blackThreshold[1]),
        std::max(0.0f, resolved.blackThreshold[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uBlackFeather"),
        std::max(0.0001f, resolved.blackFeather[0]),
        std::max(0.0001f, resolved.blackFeather[1]),
        std::max(0.0001f, resolved.blackFeather[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uReadNoise"),
        std::max(0.0f, resolved.readNoise[0]),
        std::max(0.0f, resolved.readNoise[1]),
        std::max(0.0f, resolved.readNoise[2]));
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uNoiseAware"), settings.noiseAware ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uDebugView"), static_cast<int>(settings.debugView));

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
    return true;
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
        uniform int uInvert;
        void main() {
            float v = clamp(texture(uInputMask, vTexCoord).r, 0.0, 1.0);
            if (uKind == 0) {
                v = 1.0 - v;
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

void RenderPipeline::RenderMaskUtility(unsigned int inputMask, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_MaskUtilityProgram || !inputMask) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskUtilityProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputMask);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uInputMask"), 0);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uKind"), static_cast<int>(node.maskUtilityKind));
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uBlackPoint"), node.maskUtilitySettings.blackPoint);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uWhitePoint"), node.maskUtilitySettings.whitePoint);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uGamma"), node.maskUtilitySettings.gamma);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uThreshold"), node.maskUtilitySettings.threshold);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uSoftness"), node.maskUtilitySettings.softness);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uInvert"), node.maskUtilitySettings.invert ? 1 : 0);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderImageToMask(unsigned int inputImage, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_ImageToMaskProgram || !inputImage) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ImageToMaskProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputImage);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uInputImage"), 0);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uKind"), static_cast<int>(node.imageToMaskKind));
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uLow"), node.imageToMaskSettings.low);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uHigh"), node.imageToMaskSettings.high);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uSoftness"), node.imageToMaskSettings.softness);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uInvert"), node.imageToMaskSettings.invert ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uSampleCount"), std::clamp(node.imageToMaskSettings.sampleCount, 1, 5));
    glUniform3f(
        glGetUniformLocation(m_ImageToMaskProgram, "uSampleRgb"),
        node.imageToMaskSettings.sampleRgb[0],
        node.imageToMaskSettings.sampleRgb[1],
        node.imageToMaskSettings.sampleRgb[2]);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uSampleLuma"), node.imageToMaskSettings.sampleLuma);
    glUniform3fv(
        glGetUniformLocation(m_ImageToMaskProgram, "uExtraSampleRgb"),
        4,
        &node.imageToMaskSettings.extraSampleRgb[0][0]);
    glUniform1fv(
        glGetUniformLocation(m_ImageToMaskProgram, "uExtraSampleLuma"),
        4,
        node.imageToMaskSettings.extraSampleLuma);
    glUniform2f(glGetUniformLocation(m_ImageToMaskProgram, "uSampleUv"), node.imageToMaskSettings.sampleU, node.imageToMaskSettings.sampleV);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uToneSimilarity"), node.imageToMaskSettings.toneSimilarity);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uColorSimilarity"), node.imageToMaskSettings.colorSimilarity);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uRegionRadius"), node.imageToMaskSettings.regionRadius);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uRegionFeather"), node.imageToMaskSettings.regionFeather);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uEdgeSensitivity"), node.imageToMaskSettings.edgeSensitivity);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uLocalCoherence"), node.imageToMaskSettings.localCoherence);
    glUniform2f(
        glGetUniformLocation(m_ImageToMaskProgram, "uTexelSize"),
        m_Width > 0 ? 1.0f / static_cast<float>(m_Width) : 0.0f,
        m_Height > 0 ? 1.0f / static_cast<float>(m_Height) : 0.0f);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

unsigned int RenderPipeline::GenerateImageTexture(const RenderGraphNode& node) {
    EnsureUtilityPrograms();
    if (m_Width <= 0 || m_Height <= 0) {
        return 0;
    }
    if (node.imageGeneratorKind == RenderImageGeneratorKind::Text) {
        std::vector<unsigned char> pixels;
        if (!BuildTextGeneratorCanvas(node, m_Width, m_Height, pixels) || pixels.empty()) {
            return 0;
        }
        // Flip vertically to align with OpenGL's bottom-left standard
        int rowSize = m_Width * 4;
        std::vector<unsigned char> tempRow(rowSize);
        for (int y = 0; y < m_Height / 2; y++) {
            unsigned char* row1 = &pixels[y * rowSize];
            unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
            std::memcpy(tempRow.data(), row1, rowSize);
            std::memcpy(row1, row2, rowSize);
            std::memcpy(row2, tempRow.data(), rowSize);
        }
        return GLHelpers::CreateTextureFromPixels(pixels.data(), m_Width, m_Height, 4);
    }
    if (!m_ImageGeneratorProgram) {
        return 0;
    }
    unsigned int texture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    unsigned int fbo = GLHelpers::CreateFBO(texture);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_Width, m_Height);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ImageGeneratorProgram);
    glUniform1i(glGetUniformLocation(m_ImageGeneratorProgram, "uKind"), static_cast<int>(node.imageGeneratorKind));
    glUniform4f(
        glGetUniformLocation(m_ImageGeneratorProgram, "uColorA"),
        node.imageGeneratorSettings.colorA[0],
        node.imageGeneratorSettings.colorA[1],
        node.imageGeneratorSettings.colorA[2],
        node.imageGeneratorSettings.colorA[3]);
    glUniform4f(
        glGetUniformLocation(m_ImageGeneratorProgram, "uColorB"),
        node.imageGeneratorSettings.colorB[0],
        node.imageGeneratorSettings.colorB[1],
        node.imageGeneratorSettings.colorB[2],
        node.imageGeneratorSettings.colorB[3]);
    glUniform1f(glGetUniformLocation(m_ImageGeneratorProgram, "uAngle"), node.imageGeneratorSettings.angle);
    glUniform1f(glGetUniformLocation(m_ImageGeneratorProgram, "uOffset"), node.imageGeneratorSettings.offset);
    m_Quad.Draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return texture;
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

void RenderPipeline::RenderChannelSplit(unsigned int inputTexture, int channel, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelSplitProgram || !inputTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ChannelSplitProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ChannelSplitProgram, "uInputImage"), 0);
    glUniform1i(glGetUniformLocation(m_ChannelSplitProgram, "uChannel"), channel);

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderChannelCombine(unsigned int texR, unsigned int texG, unsigned int texB, unsigned int texA,
                                         bool hasR, bool hasG, bool hasB, bool hasA, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelCombineProgram) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ChannelCombineProgram);

    int textureUnit = 0;
    if (hasR && texR) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texR);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexR"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasR"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasR"), 0);
    }

    if (hasG && texG) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texG);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexG"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasG"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasG"), 0);
    }

    if (hasB && texB) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texB);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexB"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasB"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasB"), 0);
    }

    if (hasA && texA) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texA);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexA"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasA"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasA"), 0);
    }

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
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

RenderPipeline::AutoGainSceneStats RenderPipeline::ComputeAutoGainSceneStats(unsigned int inputTexture) {
    AutoGainSceneStats fallback;
    if (!inputTexture || m_Width <= 0 || m_Height <= 0) {
        return fallback;
    }

    std::size_t cacheKey = HashValue(inputTexture);
    HashCombine(cacheKey, HashValue(m_Width));
    HashCombine(cacheKey, HashValue(m_Height));
    if (const auto cached = m_AutoGainSceneStatsCache.find(cacheKey);
        cached != m_AutoGainSceneStatsCache.end()) {
        return cached->second;
    }

    EnsureAutoGainStatsProgram();
    if (!m_AutoGainStatsProgram) {
        return fallback;
    }

    const int statsWidth = std::clamp(m_Width / 32, 64, 160);
    const int statsHeight = std::clamp(m_Height / 32, 36, 120);
    const unsigned int statsTexture = GLHelpers::CreateEmptyTexture(statsWidth, statsHeight);
    if (!statsTexture) {
        return fallback;
    }
    const unsigned int statsFbo = GLHelpers::CreateFBO(statsTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, statsFbo);
    glViewport(0, 0, statsWidth, statsHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_AutoGainStatsProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_AutoGainStatsProgram, "uInputImage"), 0);
    glUniform2f(glGetUniformLocation(m_AutoGainStatsProgram, "uSourceTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
    m_Quad.Draw();

    std::vector<float> pixels(static_cast<std::size_t>(statsWidth) * static_cast<std::size_t>(statsHeight) * 4u, 0.0f);
    glReadPixels(0, 0, statsWidth, statsHeight, GL_RGBA, GL_FLOAT, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glDeleteFramebuffers(1, &statsFbo);
    glDeleteTextures(1, &statsTexture);

    std::vector<float> lumas;
    lumas.reserve(pixels.size() / 4);
    float clipped = 0.0f;
    float saturated = 0.0f;
    float textureSum = 0.0f;
    float darkTextureSum = 0.0f;
    float darkCount = 0.0f;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const float lum = pixels[i + 0];
        const float maxChannel = pixels[i + 1];
        const float saturation = pixels[i + 2];
        const float texture = pixels[i + 3];
        if (!std::isfinite(lum) || lum <= 0.0f) {
            continue;
        }
        lumas.push_back(lum);
        clipped += maxChannel > 1.0f ? 1.0f : 0.0f;
        saturated += (maxChannel > 0.35f && saturation > 0.58f) ? 1.0f : 0.0f;
        textureSum += std::clamp(texture, 0.0f, 1.0f);
        if (lum < 0.08f) {
            darkTextureSum += std::clamp(texture, 0.0f, 1.0f);
            darkCount += 1.0f;
        }
    }

    AutoGainSceneStats stats = fallback;
    if (lumas.size() < 16) {
        m_AutoGainSceneStatsCache[cacheKey] = stats;
        return stats;
    }
    std::sort(lumas.begin(), lumas.end());
    const float count = static_cast<float>(lumas.size());
    stats.shadowPercentile = std::max(PercentileFromSorted(lumas, 0.10f), 0.00001f);
    stats.midtonePercentile = std::max(PercentileFromSorted(lumas, 0.50f), 0.00001f);
    stats.highlightPercentile = std::max(PercentileFromSorted(lumas, 0.98f), 0.00001f);
    const float p01 = std::max(PercentileFromSorted(lumas, 0.01f), 0.000001f);
    const float p05 = std::max(PercentileFromSorted(lumas, 0.05f), 0.000001f);
    stats.clippingRatio = Clamp01(clipped / count);
    stats.channelSaturationRatio = Clamp01(saturated / count);
    stats.textureConfidence = Clamp01(textureSum / count);

    const float darkTexture = darkCount > 0.0f ? darkTextureSum / darkCount : stats.textureConfidence;
    stats.estimatedNoiseFloor = std::clamp(std::max(p01, p05 * 0.52f) * (1.0f + darkTexture * 2.85f), 0.00003f, 0.10f);
    const float noiseRisk = Clamp01((stats.estimatedNoiseFloor * 7.0f) / std::max(stats.shadowPercentile, 0.00003f));
    const float highlightPressure = Clamp01(std::max(stats.clippingRatio * 8.0f, stats.channelSaturationRatio * 2.5f) +
        std::max(0.0f, SafeLog2(stats.highlightPercentile / 0.85f)) * 0.30f);

    const float target = 0.30f;
    const float adaptiveTarget = std::clamp(
        target + LerpFloat(0.01f, -0.07f, highlightPressure) - noiseRisk * 0.04f,
        0.22f,
        0.42f);
    const float highlightTarget = LerpFloat(0.80f, 0.62f, highlightPressure);
    stats.recommendedMinEv = std::clamp(SafeLog2(highlightTarget / stats.highlightPercentile) - highlightPressure * 0.35f, -2.25f, 0.5f);
    const float shadowTarget = LerpFloat(0.18f, 0.30f, 1.0f - noiseRisk);
    float maxLift = SafeLog2(shadowTarget / std::max(stats.shadowPercentile, stats.estimatedNoiseFloor * 8.0f));
    maxLift *= LerpFloat(0.85f, 0.45f, noiseRisk);
    const float conservativeLiftCap = LerpFloat(1.75f, 1.05f, noiseRisk);
    stats.recommendedMaxEv = std::clamp(maxLift, 0.25f, conservativeLiftCap);
    if (stats.recommendedMaxEv < stats.recommendedMinEv + 0.25f) {
        stats.recommendedMaxEv = std::min(2.0f, stats.recommendedMinEv + 0.25f);
    }
    // Keep the auto baseline scene-referred and restrained. Pre-Local Exposure should provide
    // a usable local-exposure starting point, not behave like aggressive global exposure
    // compensation or promise true clipped-highlight recovery.
    float recommendedBaseEv = SafeLog2(adaptiveTarget / stats.midtonePercentile);
    recommendedBaseEv *= LerpFloat(0.95f, 0.70f, std::max(highlightPressure, noiseRisk));
    stats.recommendedBaseEv = std::clamp(recommendedBaseEv, -1.0f, 1.0f);
    stats.recommendedNoiseProtection = Clamp01(0.55f + noiseRisk * 0.45f + darkTexture * 0.20f);
    stats.recommendedHighlightProtection = Clamp01(0.78f + highlightPressure * 0.22f + stats.channelSaturationRatio * 0.18f);
    stats.recommendedShadowLiftLimit = Clamp01(0.68f - noiseRisk * 0.34f);
    stats.recommendedTarget = adaptiveTarget;
    stats.valid = true;

    m_AutoGainSceneStatsCache[cacheKey] = stats;
    return stats;
}

Raw::RawDetailFusionSettings RenderPipeline::ResolveAutoGainEffectiveSettings(
    unsigned int inputTexture,
    const Raw::RawDetailFusionSettings& settings) {
    const auto sanitizeNaturalSettings = [](Raw::RawDetailFusionSettings value) {
        value.minEv = std::clamp(value.minEv, -2.5f, 0.5f);
        value.maxEv = std::clamp(value.maxEv, std::max(value.minEv + 0.01f, 0.25f), 2.5f);
        value.baseEv = std::clamp(value.baseEv, -1.0f, 1.0f);
        value.minEvBias = std::clamp(value.minEvBias, -2.0f, 2.0f);
        value.maxEvBias = std::clamp(value.maxEvBias, -2.0f, 2.0f);
        value.baseEvBias = std::clamp(value.baseEvBias, -1.25f, 1.25f);
        value.wellExposedTargetBias = std::clamp(value.wellExposedTargetBias, -1.0f, 1.0f);
        value.strength = std::clamp(value.strength, 0.0f, 1.25f);
        value.baseRadiusPercent = std::clamp(value.baseRadiusPercent, 0.002f, 0.030f);
        value.wellExposedTarget = std::clamp(value.wellExposedTarget, 0.10f, 0.55f);
        return value;
    };

    Raw::RawDetailFusionSettings effective = settings;
    if (!settings.autoSafetyEnabled) {
        return sanitizeNaturalSettings(effective);
    }

    const AutoGainSceneStats stats = ComputeAutoGainSceneStats(inputTexture);
    if (!stats.valid) {
        return sanitizeNaturalSettings(effective);
    }

    effective.minEv = settings.overrideMinEv
        ? settings.minEv
        : std::clamp(stats.recommendedMinEv + settings.minEvBias, -2.25f, 0.5f);
    effective.maxEv = settings.overrideMaxEv
        ? settings.maxEv
        : std::clamp(stats.recommendedMaxEv + settings.maxEvBias, 0.25f, 2.5f);
    if (effective.maxEv < effective.minEv + 0.25f) {
        effective.maxEv = std::min(2.5f, effective.minEv + 0.25f);
    }
    effective.baseEv = settings.overrideBaseEv
        ? settings.baseEv
        : std::clamp(stats.recommendedBaseEv + settings.baseEvBias, -1.0f, 1.0f);
    effective.noiseProtection = settings.overrideNoiseProtection
        ? settings.noiseProtection
        : Clamp01(stats.recommendedNoiseProtection + settings.noiseProtectionBias);
    effective.highlightProtection = settings.overrideHighlightProtection
        ? settings.highlightProtection
        : Clamp01(stats.recommendedHighlightProtection + settings.highlightProtectionBias);
    effective.shadowLiftLimit = settings.overrideShadowLiftLimit
        ? settings.shadowLiftLimit
        : Clamp01(stats.recommendedShadowLiftLimit - settings.shadowLiftLimitBias);
    effective.wellExposedTarget = settings.overrideWellExposedTarget
        ? settings.wellExposedTarget
        : std::clamp(stats.recommendedTarget + settings.wellExposedTargetBias, 0.10f, 0.55f);
    return sanitizeNaturalSettings(effective);
}

RenderPipeline::PreLocalExposureSummary RenderPipeline::BuildPreLocalExposureSummary(
    unsigned int inputTexture,
    const Raw::RawDetailFusionSettings& settings,
    bool legacyMaskActive,
    bool legacyManualMode) {
    PreLocalExposureSummary summary;
    if (!inputTexture) {
        return summary;
    }

    const AutoGainSceneStats stats = ComputeAutoGainSceneStats(inputTexture);
    if (!stats.valid) {
        return summary;
    }

    const Raw::RawDetailFusionSettings effective = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    const float noiseRisk = Clamp01((stats.estimatedNoiseFloor * 7.0f) / std::max(stats.shadowPercentile, 0.00003f));
    const float highlightPressure = Clamp01(
        std::max(stats.clippingRatio * 8.0f, stats.channelSaturationRatio * 2.5f) +
        std::max(0.0f, SafeLog2(stats.highlightPercentile / 0.85f)) * 0.30f);
    const float gradientPressure = Clamp01((1.0f - stats.textureConfidence) * 0.85f + effective.smoothGradientProtection * 0.35f);

    summary.valid = true;
    summary.effectiveSettings = effective;
    summary.clippingRatio = stats.clippingRatio;
    summary.channelSaturationRatio = stats.channelSaturationRatio;
    summary.estimatedNoiseFloor = stats.estimatedNoiseFloor;
    summary.shadowPercentile = stats.shadowPercentile;
    summary.highlightPercentile = stats.highlightPercentile;
    summary.textureConfidence = stats.textureConfidence;
    summary.noiseLimited = noiseRisk > 0.35f;
    summary.highlightLimited = highlightPressure > 0.25f;
    summary.gradientProtected = effective.smoothGradientProtection > 0.60f && gradientPressure > 0.55f;
    summary.legacyMaskActive = legacyMaskActive;
    summary.legacyManualMode = legacyManualMode;
    return summary;
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

unsigned int RenderPipeline::RenderRawDetailAutoMask(
    unsigned int inputTexture,
    const RenderGraphNode& node,
    unsigned int manualMaskTexture,
    bool debugPreview) {
    EnsureRawDetailFusionPrograms();
    if (!inputTexture || !m_RawDetailFusionAnalysisProgram || !m_RawDetailFusionMetricsProgram ||
        !m_RawDetailFusionSmoothProgram || (debugPreview && !m_RawDetailFusionApplyProgram)) {
        return 0;
    }
    const Raw::RawDetailFusionSettings& settings = node.kind == RenderGraphNodeKind::RawDetailFusion
        ? node.rawDetailFusion.settings
        : node.rawDetailAutoMask.settings;
    const AutoGainSceneStats sceneStats = ComputeAutoGainSceneStats(inputTexture);
    const Raw::RawDetailFusionSettings effectiveSettings = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    const unsigned int metricsTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    const unsigned int analysisTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    const unsigned int smoothTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (!metricsTexture || !analysisTexture || !smoothTexture) {
        if (metricsTexture) glDeleteTextures(1, &metricsTexture);
        if (analysisTexture) glDeleteTextures(1, &analysisTexture);
        if (smoothTexture) glDeleteTextures(1, &smoothTexture);
        return 0;
    }

    auto renderPass = [&](unsigned int texture, const std::function<void(unsigned int)>& fn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        fn(fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    };

    renderPass(metricsTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionMetricsProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uInputImage"), 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uTextureSensitivity"), settings.textureSensitivity);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uSkyBias"), settings.skyBias);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uAutoNoiseProtection"), effectiveSettings.noiseProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uAutoHighlightProtection"), effectiveSettings.highlightProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    renderPass(analysisTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionAnalysisProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, metricsTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMetrics"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, manualMaskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uManualMask"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uHasManualMask"), manualMaskTexture ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMode"),
            static_cast<int>(manualMaskTexture ? Raw::RawDetailFusionMode::Hybrid : Raw::RawDetailFusionMode::AutoAnalyze));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMinEv"), effectiveSettings.minEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaxEv"), effectiveSettings.maxEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uBaseEv"), effectiveSettings.baseEv);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSampleCount"), std::clamp(effectiveSettings.sampleCount, 2, 33));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uBaseRadiusPercent"), effectiveSettings.baseRadiusPercent);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uHighlightProtection"), effectiveSettings.highlightProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uShadowLiftLimit"), effectiveSettings.shadowLiftLimit);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uNoiseProtection"), effectiveSettings.noiseProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uDetailWeight"), settings.detailWeight);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uWellExposedTarget"), effectiveSettings.wellExposedTarget);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSkyBias"), settings.skyBias);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uClippingRatio"), sceneStats.clippingRatio);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uInvertMask"), settings.invertMask ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskBlackPoint"), settings.maskBlackPoint);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskWhitePoint"), settings.maskWhitePoint);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskGamma"), settings.maskGamma);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uManualBlend"), settings.manualBlend);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    renderPass(smoothTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionSmoothProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, analysisTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uAnalysis"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, metricsTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uMetrics"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uInputImage"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uRadius"), std::clamp(settings.smoothnessRadius, 0, 16));
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uSmoothAreaRadius"), std::clamp(settings.smoothAreaRadius, 0, 32));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uEdgeAwareness"), settings.edgeAwareness);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uHaloGuard"), settings.haloGuard);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uMaskDebandDither"), settings.maskDebandDither);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    if (debugPreview) {
        const unsigned int previewTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
        if (previewTexture) {
            renderPass(previewTexture, [&](unsigned int) {
                glUseProgram(m_RawDetailFusionApplyProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uInputImage"), 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, smoothTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uExposureMap"), 1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, metricsTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMetrics"), 2);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uHasMask"), 1);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMinEv"), effectiveSettings.minEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaxEv"), effectiveSettings.maxEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uBaseEv"), effectiveSettings.baseEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uStrength"), effectiveSettings.strength);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uDebugView"), static_cast<int>(settings.debugView));
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaskOutput"), 1);
                m_Quad.Draw();
            });
            glDeleteTextures(1, &smoothTexture);
            glDeleteTextures(1, &analysisTexture);
            glDeleteTextures(1, &metricsTexture);
            glUseProgram(0);
            glActiveTexture(GL_TEXTURE0);
            return previewTexture;
        }
    }
    glDeleteTextures(1, &analysisTexture);
    glDeleteTextures(1, &metricsTexture);
    return smoothTexture;
}

unsigned int RenderPipeline::RenderRawDetailFusion(
    unsigned int inputTexture,
    unsigned int maskTexture,
    const Raw::RawDetailFusionSettings& settings) {
    EnsureRawDetailFusionPrograms();
    if (!inputTexture || !m_RawDetailFusionApplyProgram) {
        return 0;
    }
    const unsigned int outputTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (!outputTexture) {
        return 0;
    }
    const AutoGainSceneStats sceneStats = ComputeAutoGainSceneStats(inputTexture);
    const Raw::RawDetailFusionSettings effectiveSettings = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    auto renderPass = [&](unsigned int texture, const std::function<void(unsigned int)>& fn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        fn(fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    };

    renderPass(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionApplyProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, maskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uExposureMap"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, maskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMetrics"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uHasMask"), maskTexture ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMinEv"), effectiveSettings.minEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaxEv"), effectiveSettings.maxEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uBaseEv"), effectiveSettings.baseEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uStrength"), effectiveSettings.strength);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uDebugView"), static_cast<int>(Raw::RawDetailFusionDebugView::FinalImage));
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaskOutput"), 0);
        m_Quad.Draw();
    });

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    return outputTexture;
}

void RenderPipeline::ExecuteGraph(const RenderGraphSnapshot& graph) {
    ExecuteGraphImpl(graph);
}

void RenderPipeline::ExecuteMasked(const std::vector<RenderLayerStep>& steps, const std::vector<RenderMaskSource>& masks) {
    m_GraphSourceTexture = 0;
    bool hasConnectedMask = false;
    for (const RenderLayerStep& step : steps) {
        if (step.maskNodeId > 0) {
            hasConnectedMask = true;
            break;
        }
    }
    if (!hasConnectedMask) {
        std::vector<std::shared_ptr<LayerBase>> layers;
        layers.reserve(steps.size());
        for (const RenderLayerStep& step : steps) {
            if (step.layer) {
                layers.push_back(step.layer);
            }
        }
        Execute(layers);
        return;
    }

    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture;
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    std::vector<std::pair<int, unsigned int>> maskTextures;
    for (const RenderMaskSource& mask : masks) {
        unsigned int texture = GenerateMaskTexture(mask);
        if (texture) {
            maskTextures.push_back({ mask.nodeId, texture });
        }
    }

    auto findMaskTexture = [&](int nodeId) -> unsigned int {
        for (const auto& item : maskTextures) {
            if (item.first == nodeId) {
                return item.second;
            }
        }
        return 0;
    };

    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;
    int activeCount = 0;
    for (const RenderLayerStep& step : steps) {
        if (step.layer && step.layer->IsVisible()) {
            ++activeCount;
        }
    }

    if (activeCount == 0) {
        m_OutputTexture = m_SourceTexture;
    } else {
        for (const RenderLayerStep& step : steps) {
            if (!step.layer || !step.layer->IsVisible()) {
                continue;
            }

            const unsigned int originalInput = currentInput;
            unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
            unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;
            usePing = !usePing;

            glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
            glClear(GL_COLOR_BUFFER_BIT);
            step.layer->ExecuteWithSource(originalInput, m_SourceTexture, m_Width, m_Height, m_Quad);

            const unsigned int maskTexture = findMaskTexture(step.maskNodeId);
            if (maskTexture) {
                unsigned int blendFBO = usePing ? m_PingFBO : m_PongFBO;
                unsigned int blendTex = usePing ? m_PingTexture : m_PongTexture;
                usePing = !usePing;
                RenderMaskBlend(originalInput, targetTex, maskTexture, blendFBO);
                currentInput = blendTex;
            } else {
                currentInput = targetTex;
            }
        }
        m_OutputTexture = currentInput;
    }

    for (const auto& item : maskTextures) {
        unsigned int texture = item.second;
        glDeleteTextures(1, &texture);
    }

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

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

RenderTextureStats RenderPipeline::GetOutputTextureStats() {
    RenderTextureStats stats;
    if (m_OutputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return stats;
    }

    constexpr int kMaxProbeEdge = 512;
    const float scale = std::min(
        1.0f,
        static_cast<float>(kMaxProbeEdge) / static_cast<float>(std::max(m_Width, m_Height)));
    const int probeW = std::max(1, static_cast<int>(std::round(static_cast<float>(m_Width) * scale)));
    const int probeH = std::max(1, static_cast<int>(std::round(static_cast<float>(m_Height) * scale)));

    const ScopedFramebufferState savedState;

    unsigned int readFBO = 0;
    unsigned int probeFBO = 0;
    unsigned int probeTex = 0;
    glGenFramebuffers(1, &readFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (probeW == m_Width && probeH == m_Height) {
        glBindFramebuffer(GL_FRAMEBUFFER, readFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        probeTex = GLHelpers::CreateEmptyTexture(probeW, probeH);
        glGenFramebuffers(1, &probeFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probeFBO);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(
            0, 0, m_Width, m_Height,
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
            std::cerr << "[RenderPipeline] glReadPixels error in GetOutputTextureStats: " << err << std::endl;
            pixels.clear();
        }
    } else {
        std::cerr << "[RenderPipeline] GetOutputTextureStats FBO incomplete." << std::endl;
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
    stats.p01Luma = percentile(0.01f);
    stats.p50Luma = percentile(0.50f);
    stats.p99Luma = percentile(0.99f);
    stats.hdrPixelPercent = 100.0f * static_cast<float>(hdrPixels) / static_cast<float>(validPixels);
    stats.displayClipPercent = 100.0f * static_cast<float>(displayEdgePixels) / static_cast<float>(validPixels);
    return stats;
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

const RenderPipeline::PreLocalExposureSummary* RenderPipeline::GetPreLocalExposureSummary(int nodeId) const {
    const auto it = m_PreLocalExposureSummaries.find(nodeId);
    return it != m_PreLocalExposureSummaries.end() ? &it->second : nullptr;
}
