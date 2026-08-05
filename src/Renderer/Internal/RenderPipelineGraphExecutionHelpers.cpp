#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace Stack::Renderer::GraphExecution {

std::size_t HashBytes(const unsigned char* data, std::size_t size) {
    std::size_t hash = kFnvOffsetBasis;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::size_t>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

std::size_t HashJson(const nlohmann::json& value) {
    return HashValue(value.dump());
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

    std::size_t pixelElementCount = 0;
    std::size_t pixelCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            probeW, probeH, 4, pixelElementCount) ||
        !Stack::PixelBuffer::TryComputePixelElementCount(
            probeW, probeH, 1, pixelCount)) {
        return stats;
    }
    std::vector<float> pixels;
    std::vector<float> lumas;
    try {
        pixels.assign(pixelElementCount, 0.0f);
        lumas.reserve(pixelCount);
    } catch (const std::bad_alloc&) {
        return stats;
    } catch (const std::length_error&) {
        return stats;
    }

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
            while (glGetError() != GL_NO_ERROR) {}
            glBlitFramebuffer(0, 0, width, height, 0, 0, probeW, probeH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            targetReady = glGetError() == GL_NO_ERROR;
            glBindFramebuffer(GL_FRAMEBUFFER, probeFbo);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
        }
    }
    if (targetReady) {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0, 0, probeW, probeH);
        const Stack::Renderer::GLState::PixelPackState savedPackState;
        savedPackState.ConfigureTightCpuReadback();
        while (glGetError() != GL_NO_ERROR) {}

        glReadPixels(0, 0, probeW, probeH, GL_RGBA, GL_FLOAT, pixels.data());
        const bool readbackOk = glGetError() == GL_NO_ERROR;
        savedPackState.Restore();
        if (readbackOk) {
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

std::string MakeNodeSocketKey(int nodeId, std::string_view socketId) {
    std::string key = std::to_string(nodeId);
    key.push_back(':');
    key.append(socketId.data(), socketId.size());
    return key;
}

int ExtractNodeIdFromCacheKey(const std::string& key) {
    const std::size_t colonPos = key.find(':');
    if (colonPos == std::string::npos || colonPos == 0) {
        return -1;
    }
    std::size_t digitIndex = 0;
    const bool negative = key[0] == '-';
    if (negative) {
        digitIndex = 1;
        if (digitIndex == colonPos) return -1;
    }
    const std::uint64_t limit = negative
        ? static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1u
        : static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    std::uint64_t magnitude = 0;
    for (; digitIndex < colonPos; ++digitIndex) {
        const char digit = key[digitIndex];
        if (digit < '0' || digit > '9') return -1;
        const unsigned int value = static_cast<unsigned int>(digit - '0');
        if (magnitude > (limit - value) / 10u) return -1;
        magnitude = magnitude * 10u + value;
    }
    if (!negative) return static_cast<int>(magnitude);
    if (magnitude == limit) return std::numeric_limits<int>::min();
    return -static_cast<int>(magnitude);
}

GraphExecutionContext::GraphExecutionContext(
    const RenderGraphSnapshot& graphSnapshot,
    const GraphTopologyIndex& topologyIndex,
    std::size_t executionEntryCapacity)
    : graph(graphSnapshot),
      nodes(topologyIndex.nodes),
      inputLinks(topologyIndex.inputLinks),
      outputUseCounts(topologyIndex.outputUseCounts) {
    imageCache.reserve(executionEntryCapacity);
    maskCache.reserve(executionEntryCapacity);
    imageFingerprintCache.reserve(executionEntryCapacity);
    maskFingerprintCache.reserve(executionEntryCapacity);
    scalarSocketCache.reserve(executionEntryCapacity);
    visitingImages.reserve(executionEntryCapacity);
    visitingMasks.reserve(executionEntryCapacity);
    fingerprintingImages.reserve(executionEntryCapacity);
    fingerprintingMasks.reserve(executionEntryCapacity);
}

const RenderGraphLink* GraphExecutionContext::FindInputLink(int nodeId, std::string_view socketId) const {
    const auto nodeIt = inputLinks.find(nodeId);
    if (nodeIt == inputLinks.end()) {
        return nullptr;
    }
    const auto socketIt = nodeIt->second.find(socketId);
    return socketIt != nodeIt->second.end() ? socketIt->second : nullptr;
}

bool GraphExecutionContext::IsActiveNode(int nodeId) const {
    return nodes.find(nodeId) != nodes.end();
}

int GraphExecutionContext::OutputUseCount(int nodeId, std::string_view socketId) const {
    const auto found = outputUseCounts.find(MakeNodeSocketKey(nodeId, socketId));
    return found == outputUseCounts.end() ? 0 : found->second;
}

} // namespace Stack::Renderer::GraphExecution
