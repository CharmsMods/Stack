#include "Renderer/RenderPipeline.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawLoader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#ifndef GL_RGB32F
#define GL_RGB32F 0x8815
#endif

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif

#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif

namespace {

constexpr std::size_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr std::size_t kFnvPrime = 1099511628211ull;
constexpr std::size_t kMaxRawDevelopStageCacheEntriesPerKey = 6;
constexpr std::uint64_t kRawDevelopStageCacheBytesPerPixel = 8;
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

struct HdrMergeInputContext {
    bool active = false;
    bool hasRawMetadata = false;
    bool hasCaptureExposure = false;
    float captureExposureEv = 0.0f;
    float developExposureStops = 0.0f;
    float developExposureScale = 1.0f;
    Raw::RawMetadata metadata;
};

std::string MakeNodeSocketKey(int nodeId, std::string_view socketId) {
    std::string key = std::to_string(nodeId);
    key.push_back(':');
    key.append(socketId.data(), socketId.size());
    return key;
}

int ExtractNodeIdFromCacheKey(const std::string& key) {
    const std::size_t colonPos = key.find(':');
    if (colonPos == std::string::npos) {
        return -1;
    }
    return std::atoi(key.substr(0, colonPos).c_str());
}

struct GraphExecutionContext {
    explicit GraphExecutionContext(const RenderGraphSnapshot& graphSnapshot)
        : graph(graphSnapshot) {
        nodes.reserve(graph.nodes.size());
        for (const RenderGraphNode& node : graph.nodes) {
            nodes[node.nodeId] = &node;
        }

        inputLinks.reserve(graph.nodes.size());
        for (const RenderGraphLink& link : graph.links) {
            auto& socketLinks = inputLinks[link.toNodeId];
            socketLinks.emplace(std::string_view(link.toSocketId), &link);
        }
    }

    const RenderGraphLink* FindInputLink(int nodeId, std::string_view socketId) const {
        const auto nodeIt = inputLinks.find(nodeId);
        if (nodeIt == inputLinks.end()) {
            return nullptr;
        }
        const auto socketIt = nodeIt->second.find(socketId);
        return socketIt != nodeIt->second.end() ? socketIt->second : nullptr;
    }

    bool IsActiveNode(int nodeId) const {
        return nodes.find(nodeId) != nodes.end();
    }

    const RenderGraphSnapshot& graph;
    std::unordered_map<int, const RenderGraphNode*> nodes;
    std::unordered_map<int, std::unordered_map<std::string_view, const RenderGraphLink*>> inputLinks;
    std::unordered_map<std::string, unsigned int> imageCache;
    std::unordered_map<std::string, unsigned int> maskCache;
    std::unordered_map<std::string, std::size_t> imageFingerprintCache;
    std::unordered_map<std::string, std::size_t> maskFingerprintCache;
    std::set<std::string> visitingImages;
    std::set<std::string> visitingMasks;
    std::set<std::string> fingerprintingImages;
    std::set<std::string> fingerprintingMasks;
};

} // namespace

void RenderPipeline::ExecuteGraphImpl(const RenderGraphSnapshot& graph) {
    m_GraphSourceTexture = 0;
    m_LastGraphImageCacheHits.clear();
    m_LastGraphExecutionStats = {};
    m_AutoGainSceneStatsCache.clear();
    m_PreLocalExposureSummaries.clear();
    m_ToneCurveAutoRewriteFeedback.clear();
    if (m_Width == 0 || m_Height == 0 || graph.outputNodeId <= 0) {
        m_OutputTexture = 0;
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

    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }

    GraphExecutionContext executionContext(graph);
    auto& nodes = executionContext.nodes;
    auto& imageCache = executionContext.imageCache;
    auto& maskCache = executionContext.maskCache;
    auto& imageFingerprintCache = executionContext.imageFingerprintCache;
    auto& maskFingerprintCache = executionContext.maskFingerprintCache;
    auto& visitingImages = executionContext.visitingImages;
    auto& visitingMasks = executionContext.visitingMasks;
    auto& fingerprintingImages = executionContext.fingerprintingImages;
    auto& fingerprintingMasks = executionContext.fingerprintingMasks;

    auto findInputLink = [&](int nodeId, const std::string& socketId) -> const RenderGraphLink* {
        return executionContext.FindInputLink(nodeId, socketId);
    };

    auto isChannelSocketId = [](const std::string& socketId) {
        return socketId == "r" || socketId == "g" || socketId == "b" || socketId == "a";
    };

    std::set<std::string> scalarSocketVisiting;
    std::function<bool(int, const std::string&)> isScalarRenderSocket = [&](int nodeId, const std::string& socketId) -> bool {
        const std::string key = std::to_string(nodeId) + ":" + socketId;
        if (!scalarSocketVisiting.insert(key).second) {
            return false;
        }
        auto finish = [&](bool result) {
            scalarSocketVisiting.erase(key);
            return result;
        };
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || !nodeIt->second) {
            return finish(false);
        }
        const RenderGraphNode& node = *nodeIt->second;
        auto inputIsScalar = [&](const char* inputSocketId) {
            const RenderGraphLink* input = findInputLink(node.nodeId, inputSocketId);
            return input ? isScalarRenderSocket(input->fromNodeId, input->fromSocketId) : false;
        };

        bool result = false;
        if (isChannelSocketId(socketId)) {
            result = true;
        } else {
            switch (node.kind) {
                case RenderGraphNodeKind::MaskGenerator:
                case RenderGraphNodeKind::MaskCombine:
                case RenderGraphNodeKind::MaskUtility:
                case RenderGraphNodeKind::CustomMask:
                case RenderGraphNodeKind::ImageToMask:
                    result = socketId == "maskOut";
                    break;
                case RenderGraphNodeKind::RawDetailAutoMask:
                case RenderGraphNodeKind::RawDetailFusion:
                    result = socketId == "maskOut" || (socketId == "imageOut" && inputIsScalar("imageIn"));
                    break;
                case RenderGraphNodeKind::Layer:
                case RenderGraphNodeKind::Lut:
                    result = socketId == "imageOut" && inputIsScalar("imageIn");
                    break;
                case RenderGraphNodeKind::Mix: {
                    const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
                    const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
                    const bool hasA = inputA != nullptr;
                    const bool hasB = inputB != nullptr;
                    result = socketId == "imageOut" &&
                        (hasA || hasB) &&
                        (!hasA || isScalarRenderSocket(inputA->fromNodeId, inputA->fromSocketId)) &&
                        (!hasB || isScalarRenderSocket(inputB->fromNodeId, inputB->fromSocketId));
                    break;
                }
                case RenderGraphNodeKind::DataMath: {
                    const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
                    const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
                    const bool hasA = inputA != nullptr;
                    const bool hasB = inputB != nullptr;
                    result = socketId == "imageOut" &&
                        (hasA || hasB) &&
                        (!hasA || isScalarRenderSocket(inputA->fromNodeId, inputA->fromSocketId)) &&
                        (!hasB || isScalarRenderSocket(inputB->fromNodeId, inputB->fromSocketId));
                    break;
                }
                case RenderGraphNodeKind::ChannelSplit:
                    result = isChannelSocketId(socketId);
                    break;
                default:
                    result = false;
                    break;
            }
        }
        return finish(result);
    };

    std::function<int(int)> findReferenceSourceNode = [&](int nodeId) -> int {
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || !nodeIt->second) {
            return -1;
        }
        const RenderGraphNode& node = *nodeIt->second;
        switch (node.kind) {
            case RenderGraphNodeKind::Image:
            case RenderGraphNodeKind::RawSource:
            case RenderGraphNodeKind::ImageGenerator:
            case RenderGraphNodeKind::RawDecode:
            case RenderGraphNodeKind::RawDevelop:
                return node.nodeId;
            case RenderGraphNodeKind::RawDetailFusion: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::HdrMerge: {
                const char* sockets[] = { "image1", "image2", "image3" };
                for (const char* socket : sockets) {
                    if (const RenderGraphLink* input = findInputLink(node.nodeId, socket)) {
                        const int source = findReferenceSourceNode(input->fromNodeId);
                        if (source > 0) return source;
                    }
                }
                return -1;
            }
            case RenderGraphNodeKind::RawDetailAutoMask: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::RawNeuralDenoise: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "rawIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::Lut: {
                if (const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn")) {
                    return findReferenceSourceNode(input->fromNodeId);
                }
                for (const char* socket : { "r", "g", "b", "a" }) {
                    if (const RenderGraphLink* input = findInputLink(node.nodeId, socket)) {
                        const int source = findReferenceSourceNode(input->fromNodeId);
                        if (source > 0) return source;
                    }
                }
                return -1;
            }
            case RenderGraphNodeKind::Layer:
            case RenderGraphNodeKind::Output:
            case RenderGraphNodeKind::ImageToMask:
            case RenderGraphNodeKind::MaskCombine:
            case RenderGraphNodeKind::MaskUtility:
            case RenderGraphNodeKind::ChannelSplit: {
                const RenderGraphLink* input = findInputLink(node.nodeId, node.kind == RenderGraphNodeKind::Output ? "imageIn" : "imageIn");
                if (!input && node.kind == RenderGraphNodeKind::MaskCombine) input = findInputLink(node.nodeId, "maskA");
                if (!input && node.kind == RenderGraphNodeKind::MaskUtility) input = findInputLink(node.nodeId, "maskIn");
                if (!input && node.kind == RenderGraphNodeKind::ChannelSplit) input = findInputLink(node.nodeId, "imageIn");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::Mix: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "imageA");
                if (!input) input = findInputLink(node.nodeId, "imageB");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::DataMath: {
                const RenderGraphLink* input = findInputLink(node.nodeId, "imageA");
                if (!input) input = findInputLink(node.nodeId, "imageB");
                return input ? findReferenceSourceNode(input->fromNodeId) : -1;
            }
            case RenderGraphNodeKind::ChannelCombine: {
                const char* sockets[] = { "r", "g", "b", "a" };
                for (const char* socket : sockets) {
                    if (const RenderGraphLink* input = findInputLink(node.nodeId, socket)) {
                        const int source = findReferenceSourceNode(input->fromNodeId);
                        if (source > 0) return source;
                    }
                }
                return -1;
            }
            case RenderGraphNodeKind::MaskGenerator:
            default:
                return -1;
        }
    };

    std::function<int(int, const std::string&)> findRawDetailAutoMaskSource = [&](int nodeId, const std::string& socketId) -> int {
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || !nodeIt->second) {
            return -1;
        }
        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") {
            return node.nodeId;
        }
        if (node.kind == RenderGraphNodeKind::MaskCombine && socketId == "maskOut") {
            if (const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA")) {
                const int source = findRawDetailAutoMaskSource(inputA->fromNodeId, inputA->fromSocketId);
                if (source > 0) {
                    return source;
                }
            }
            if (const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB")) {
                return findRawDetailAutoMaskSource(inputB->fromNodeId, inputB->fromSocketId);
            }
        }
        if (node.kind == RenderGraphNodeKind::MaskUtility && socketId == "maskOut") {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            return input ? findRawDetailAutoMaskSource(input->fromNodeId, input->fromSocketId) : -1;
        }
        return -1;
    };

    auto resolveRawDetailFusionApplySettings = [&](const RenderGraphNode& node) {
        Raw::RawDetailFusionSettings settings = node.rawDetailFusion.settings;
        if (const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn")) {
            const int autoMaskNodeId = findRawDetailAutoMaskSource(maskLink->fromNodeId, maskLink->fromSocketId);
            const auto autoIt = nodes.find(autoMaskNodeId);
            if (autoIt != nodes.end() && autoIt->second) {
                const Raw::RawDetailFusionSettings& autoSettings = autoIt->second->rawDetailAutoMask.settings;
                settings.minEv = autoSettings.minEv;
                settings.maxEv = autoSettings.maxEv;
                settings.baseEv = autoSettings.baseEv;
            }
        }
        return settings;
    };

    auto resolveHdrMergeInputContext = [&](int sourceNodeId) {
        HdrMergeInputContext context;
        const int referenceNodeId = findReferenceSourceNode(sourceNodeId);
        if (referenceNodeId <= 0) {
            return context;
        }

        const auto referenceIt = nodes.find(referenceNodeId);
        if (referenceIt == nodes.end() || !referenceIt->second) {
            return context;
        }

        const RenderGraphNode& referenceNode = *referenceIt->second;
        context.active = true;
        if (referenceNode.kind == RenderGraphNodeKind::RawSource) {
            context.hasRawMetadata = true;
            context.metadata = referenceNode.rawSource.metadata;
        } else if (referenceNode.kind == RenderGraphNodeKind::RawDecode ||
                   referenceNode.kind == RenderGraphNodeKind::RawDevelop) {
            const Raw::RawDevelopSettings& settings =
                referenceNode.kind == RenderGraphNodeKind::RawDecode
                    ? referenceNode.rawDecode.settings
                    : referenceNode.rawDevelop.settings;
            context.developExposureStops = settings.exposureStops;
            context.developExposureScale = std::exp2(context.developExposureStops);

            const RenderGraphLink* rawInput = findInputLink(referenceNode.nodeId, "rawIn");
            std::set<int> rawVisit;
            while (rawInput) {
                if (!rawVisit.insert(rawInput->fromNodeId).second) {
                    break;
                }
                const auto rawIt = nodes.find(rawInput->fromNodeId);
                if (rawIt == nodes.end() || !rawIt->second) {
                    break;
                }
                if (rawIt->second->kind == RenderGraphNodeKind::RawSource) {
                    context.hasRawMetadata = true;
                    context.metadata = rawIt->second->rawSource.metadata;
                    break;
                }
                if (rawIt->second->kind != RenderGraphNodeKind::RawNeuralDenoise) {
                    break;
                }
                rawInput = findInputLink(rawIt->second->nodeId, "rawIn");
            }
        }

        if (context.hasRawMetadata && HasHdrMergeCaptureExposure(context.metadata)) {
            context.hasCaptureExposure = true;
            context.captureExposureEv = ComputeHdrMergeCaptureExposureEv(context.metadata);
        }
        return context;
    };

    auto resolveHdrMergeSettings = [&](const Raw::HdrMergeSettings& settings,
                                       const std::array<HdrMergeInputContext, 3>& contexts,
                                       const std::array<bool, 3>& activeInputs) {
        HdrMergeResolvedSettings resolved;
        std::array<float, 3> absoluteExposureEv {
            settings.manualExposureEv[0],
            settings.manualExposureEv[1],
            settings.manualExposureEv[2]
        };

        bool metadataExposureValid = settings.exposureMode == Raw::HdrMergeExposureMode::Metadata;
        std::vector<float> referenceDistances;
        referenceDistances.reserve(3);
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i]) {
                continue;
            }
            if (metadataExposureValid) {
                if (!contexts[i].hasCaptureExposure) {
                    metadataExposureValid = false;
                    break;
                }
                absoluteExposureEv[i] = contexts[i].captureExposureEv + contexts[i].developExposureStops + settings.exposureOffsetEv[i];
            }
            referenceDistances.push_back(absoluteExposureEv[i]);
        }

        if (metadataExposureValid) {
            resolved.metadataExposureValid = true;
            const float exposureAnchor = SelectRepresentativeExposureAnchor(absoluteExposureEv, activeInputs);
            const float medianExposure = MedianFloat(referenceDistances);
            for (int i = 0; i < 3; ++i) {
                if (!activeInputs[i]) {
                    continue;
                }
                resolved.exposureEv[i] = absoluteExposureEv[i] - exposureAnchor;
                resolved.referenceExposureDistance[i] = std::abs(absoluteExposureEv[i] - medianExposure);
            }
        } else {
            referenceDistances.clear();
            for (int i = 0; i < 3; ++i) {
                if (activeInputs[i]) {
                    referenceDistances.push_back(settings.manualExposureEv[i]);
                    resolved.exposureEv[i] = settings.manualExposureEv[i];
                }
            }
            const float medianExposure = MedianFloat(referenceDistances);
            for (int i = 0; i < 3; ++i) {
                if (!activeInputs[i]) {
                    continue;
                }
                resolved.referenceExposureDistance[i] = std::abs(settings.manualExposureEv[i] - medianExposure);
            }
        }

        for (int i = 0; i < 3; ++i) {
            resolved.clipThreshold[i] = settings.clipThreshold;
            resolved.clipFeather[i] = settings.clipFeather;
            resolved.blackThreshold[i] = settings.blackThreshold;
            resolved.blackFeather[i] = settings.blackFeather;
            resolved.readNoise[i] = settings.readNoise;

            if (!activeInputs[i] || !settings.autoReliability || !contexts[i].hasRawMetadata) {
                continue;
            }

            const float sourceScale = std::max(0.0625f, contexts[i].developExposureScale);
            const float rangeStep = EstimateHdrMergeRangeStep(contexts[i].metadata);
            const float isoMultiplier = EstimateHdrMergeIsoMultiplier(contexts[i].metadata);

            resolved.clipThreshold[i] = std::clamp(0.98f * sourceScale, 0.50f, 4.0f);
            resolved.clipFeather[i] = std::clamp(std::max(0.04f * sourceScale, rangeStep * 48.0f * sourceScale), 0.001f, 1.0f);
            resolved.blackThreshold[i] = std::clamp(rangeStep * (12.0f + 4.0f * isoMultiplier) * sourceScale, 0.0f, 0.25f);
            resolved.blackFeather[i] = std::clamp(std::max(resolved.blackThreshold[i] * 6.0f, rangeStep * 24.0f * sourceScale), 0.001f, 0.50f);
            resolved.readNoise[i] = std::clamp(rangeStep * (6.0f + 2.0f * isoMultiplier) * sourceScale, 0.0f, 0.10f);
        }

        return resolved;
    };

    auto releaseCacheEntry = [&](std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key) {
        auto it = cache.find(key);
        if (it == cache.end()) {
            return;
        }
        if (it->second.owned && it->second.texture != 0 && it->second.texture != m_SourceTexture && it->second.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &it->second.texture);
        }
        cache.erase(it);
    };

    auto storeCacheEntry = [&](std::unordered_map<std::string, CachedGraphTexture>& cache, const std::string& key, unsigned int texture, std::size_t fingerprint, bool owned) {
        auto& entry = cache[key];
        if (entry.owned && entry.texture != 0 && entry.texture != texture && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &entry.texture);
        }
        entry.texture = texture;
        entry.fingerprint = fingerprint;
        entry.width = m_Width;
        entry.height = m_Height;
        entry.owned = owned;
    };

    auto cloneTextureForStageCache = [&](unsigned int sourceTexture) -> unsigned int {
        if (sourceTexture == 0 || m_Width <= 0 || m_Height <= 0) {
            return 0;
        }

        unsigned int copyTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
        if (copyTexture == 0) {
            return 0;
        }

        const ScopedFramebufferState savedState(true);
        unsigned int sourceFbo = GLHelpers::CreateFBO(sourceTexture);
        unsigned int copyFbo = GLHelpers::CreateFBO(copyTexture);
        if (sourceFbo == 0 || copyFbo == 0) {
            if (sourceFbo != 0) glDeleteFramebuffers(1, &sourceFbo);
            if (copyFbo != 0) glDeleteFramebuffers(1, &copyFbo);
            glDeleteTextures(1, &copyTexture);
            savedState.Restore(true);
            return 0;
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copyFbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(
            0, 0, m_Width, m_Height,
            0, 0, m_Width, m_Height,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST);

        const GLenum copyError = glGetError();
        savedState.Restore(true);
        glDeleteFramebuffers(1, &sourceFbo);
        glDeleteFramebuffers(1, &copyFbo);
        if (copyError != GL_NO_ERROR) {
            glDeleteTextures(1, &copyTexture);
            return 0;
        }
        return copyTexture;
    };

    auto findRawDevelopStageCacheEntry = [&](const std::string& key, std::size_t fingerprint) -> CachedGraphTexture {
        if (fingerprint == 0) {
            return {};
        }
        auto cacheIt = m_RawDevelopStageImageCache.find(key);
        if (cacheIt == m_RawDevelopStageImageCache.end()) {
            return {};
        }
        auto& entries = cacheIt->second;
        for (auto entryIt = entries.begin(); entryIt != entries.end(); ++entryIt) {
            if (entryIt->fingerprint != fingerprint || entryIt->texture == 0) {
                continue;
            }
            const CachedGraphTexture hit = *entryIt;
            if (entryIt != entries.begin()) {
                entries.erase(entryIt);
                entries.insert(entries.begin(), hit);
            }
            return hit;
        }
        return {};
    };

    auto deleteRawDevelopStageCacheEntry = [&](CachedGraphTexture& entry) {
        if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &entry.texture);
        }
        entry.texture = 0;
        entry.owned = false;
    };

    auto rawDevelopStageCacheEntryBytes = [](const CachedGraphTexture& entry) -> std::uint64_t {
        if (!entry.owned || entry.texture == 0) {
            return 0;
        }
        return EstimateRawDevelopStageCacheTextureBytes(entry.width, entry.height);
    };

    auto trimRawDevelopStageCacheVector = [&](std::vector<CachedGraphTexture>& entries, std::size_t maxEntries) {
        while (entries.size() > maxEntries) {
            CachedGraphTexture& stale = entries.back();
            deleteRawDevelopStageCacheEntry(stale);
            entries.pop_back();
        }
    };

    auto rawDevelopStageCacheTotalBytes = [&]() -> std::uint64_t {
        std::uint64_t total = 0;
        for (const auto& [cacheKey, entries] : m_RawDevelopStageImageCache) {
            (void)cacheKey;
            for (const CachedGraphTexture& entry : entries) {
                const std::uint64_t bytes = rawDevelopStageCacheEntryBytes(entry);
                if (total > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                total += bytes;
            }
        }
        return total;
    };

    auto trimRawDevelopStageCacheToBudget = [&](std::uint64_t currentTotalBytes) {
        while (currentTotalBytes > kRawDevelopStageCacheSoftByteBudget) {
            std::string victimKey;
            std::uint64_t victimBytes = 0;
            for (const auto& [cacheKey, entries] : m_RawDevelopStageImageCache) {
                if (entries.empty()) {
                    continue;
                }
                const std::uint64_t bytes = rawDevelopStageCacheEntryBytes(entries.back());
                if (bytes > victimBytes) {
                    victimKey = cacheKey;
                    victimBytes = bytes;
                }
            }
            if (victimKey.empty() || victimBytes == 0) {
                break;
            }
            auto victimIt = m_RawDevelopStageImageCache.find(victimKey);
            if (victimIt == m_RawDevelopStageImageCache.end() || victimIt->second.empty()) {
                break;
            }
            CachedGraphTexture& stale = victimIt->second.back();
            deleteRawDevelopStageCacheEntry(stale);
            victimIt->second.pop_back();
            currentTotalBytes =
                currentTotalBytes > victimBytes
                    ? currentTotalBytes - victimBytes
                    : 0;
            if (victimIt->second.empty()) {
                m_RawDevelopStageImageCache.erase(victimIt);
            }
        }
    };

    auto storeRawDevelopStageCacheEntry = [&](const std::string& key, unsigned int texture, std::size_t fingerprint) {
        if (key.empty() || texture == 0 || fingerprint == 0 || m_Width <= 0 || m_Height <= 0) {
            return;
        }

        const std::size_t maxEntriesForDimensions = ResolveRawDevelopStageCacheMaxEntries(m_Width, m_Height);
        if (maxEntriesForDimensions == 0) {
            return;
        }

        unsigned int copyTexture = cloneTextureForStageCache(texture);
        if (copyTexture == 0) {
            return;
        }

        CachedGraphTexture newEntry;
        newEntry.texture = copyTexture;
        newEntry.fingerprint = fingerprint;
        newEntry.width = m_Width;
        newEntry.height = m_Height;
        newEntry.owned = true;

        auto& entries = m_RawDevelopStageImageCache[key];
        for (auto entryIt = entries.begin(); entryIt != entries.end(); ++entryIt) {
            if (entryIt->fingerprint != fingerprint) {
                continue;
            }
            deleteRawDevelopStageCacheEntry(*entryIt);
            entries.erase(entryIt);
            break;
        }

        entries.insert(entries.begin(), newEntry);
        // Large RAWs can make each RGBA16F boundary snapshot hundreds of MB.
        // Keep reuse generous for small files, but trade cache hits for stability on large candidate runs.
        trimRawDevelopStageCacheVector(entries, maxEntriesForDimensions);
        trimRawDevelopStageCacheToBudget(rawDevelopStageCacheTotalBytes());
    };

    auto createTarget = [&]() {
        return GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    };

    auto renderToTexture = [&](unsigned int texture, const std::function<void(unsigned int)>& renderFn) -> bool {
        if (texture == 0) {
            return false;
        }
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        if (fbo == 0) {
            return false;
        }
        GLint prevFBO = 0;
        GLint prevViewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        renderFn(fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glDeleteFramebuffers(1, &fbo);
        return true;
    };

    auto deleteCachedTextureEntry = [&](CachedGraphTexture& entry) {
        if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
            glDeleteTextures(1, &entry.texture);
        }
        entry.texture = 0;
        entry.owned = false;
        entry.width = 0;
        entry.height = 0;
        entry.fingerprint = 0;
    };

    auto clearLutTextureKey = [&](const std::string& key) {
        const auto it = m_LutTextureCache.find(key);
        if (it == m_LutTextureCache.end()) {
            return;
        }
        deleteCachedTextureEntry(it->second);
        m_LutTextureCache.erase(it);
    };

    auto hashLut1DStage = [&](const ColorLut::Lut1DStage& stage) -> std::size_t {
        if (stage.size <= 0 || stage.values.empty()) {
            return 0;
        }
        std::size_t fingerprint = HashValue(stage.size);
        for (float value : stage.domainMin) {
            HashCombine(fingerprint, HashValue(value));
        }
        for (float value : stage.domainMax) {
            HashCombine(fingerprint, HashValue(value));
        }
        HashCombine(
            fingerprint,
            HashBytes(
                reinterpret_cast<const unsigned char*>(stage.values.data()),
                stage.values.size() * sizeof(float)));
        return fingerprint;
    };

    auto hashLut3DStage = [&](const ColorLut::Lut3DStage& stage) -> std::size_t {
        if (stage.size <= 0 || stage.values.empty()) {
            return 0;
        }
        std::size_t fingerprint = HashValue(stage.size);
        for (float value : stage.domainMin) {
            HashCombine(fingerprint, HashValue(value));
        }
        for (float value : stage.domainMax) {
            HashCombine(fingerprint, HashValue(value));
        }
        HashCombine(
            fingerprint,
            HashBytes(
                reinterpret_cast<const unsigned char*>(stage.values.data()),
                stage.values.size() * sizeof(float)));
        return fingerprint;
    };

    auto getOrCreateLut1DTexture = [&](const std::string& key, const ColorLut::Lut1DStage& stage, std::size_t fingerprint) -> unsigned int {
        if (fingerprint == 0 || stage.size <= 0 || stage.values.empty()) {
            return 0;
        }

        auto cacheIt = m_LutTextureCache.find(key);
        if (cacheIt != m_LutTextureCache.end() &&
            cacheIt->second.texture != 0 &&
            cacheIt->second.fingerprint == fingerprint) {
            return cacheIt->second.texture;
        }

        if (cacheIt != m_LutTextureCache.end()) {
            deleteCachedTextureEntry(cacheIt->second);
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB32F,
            stage.size,
            1,
            0,
            GL_RGB,
            GL_FLOAT,
            stage.values.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        CachedGraphTexture entry;
        entry.texture = texture;
        entry.fingerprint = fingerprint;
        entry.width = stage.size;
        entry.height = 1;
        entry.owned = texture != 0;
        m_LutTextureCache[key] = entry;
        return texture;
    };

    auto getOrCreateLut3DTexture = [&](const std::string& key, const ColorLut::Lut3DStage& stage, std::size_t fingerprint) -> unsigned int {
        if (fingerprint == 0 || stage.size <= 0 || stage.values.empty()) {
            return 0;
        }

        auto cacheIt = m_LutTextureCache.find(key);
        if (cacheIt != m_LutTextureCache.end() &&
            cacheIt->second.texture != 0 &&
            cacheIt->second.fingerprint == fingerprint) {
            return cacheIt->second.texture;
        }

        if (cacheIt != m_LutTextureCache.end()) {
            deleteCachedTextureEntry(cacheIt->second);
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_3D, texture);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGB32F, stage.size, stage.size, stage.size);
        glTexSubImage3D(
            GL_TEXTURE_3D,
            0,
            0,
            0,
            0,
            stage.size,
            stage.size,
            stage.size,
            GL_RGB,
            GL_FLOAT,
            stage.values.data());
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);

        CachedGraphTexture entry;
        entry.texture = texture;
        entry.fingerprint = fingerprint;
        entry.width = stage.size;
        entry.height = stage.size;
        entry.owned = texture != 0;
        m_LutTextureCache[key] = entry;
        return texture;
    };

    std::function<unsigned int(int, const std::string&)> evalMask;
    std::function<unsigned int(int, const std::string&)> evalImage;
    std::function<std::size_t(int, const std::string&)> fingerprintMask;
    std::function<std::size_t(int, const std::string&)> fingerprintImage;

    fingerprintMask = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = maskFingerprintCache.find(key);
        if (cached != maskFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingMasks.count(key)) {
            return 0;
        }
        fingerprintingMasks.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::HdrMerge ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            (node.kind == RenderGraphNodeKind::DataMath && !isScalarRenderSocket(nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            std::size_t imgFp = fingerprintImage(nodeId, socketId);
            fingerprintingMasks.erase(key);
            maskFingerprintCache[key] = imgFp;
            return imgFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(socketId));

        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskKind)));
            HashCombine(fingerprint, HashValue(node.maskSettings.value));
            HashCombine(fingerprint, HashValue(node.maskSettings.angle));
            HashCombine(fingerprint, HashValue(node.maskSettings.offset));
            HashCombine(fingerprint, HashValue(node.maskSettings.scale));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerX));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerY));
            HashCombine(fingerprint, HashValue(node.maskSettings.radius));
            HashCombine(fingerprint, HashValue(node.maskSettings.feather));
            HashCombine(fingerprint, HashValue(node.maskSettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            HashCombine(fingerprint, inputA ? fingerprintMask(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintMask(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskCombineMode)));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::CustomMask) {
            const RenderCustomMaskPayload& payload = node.customMask;
            HashCombine(fingerprint, HashValue(payload.width));
            HashCombine(fingerprint, HashValue(payload.height));
            HashCombine(fingerprint, HashValue(payload.invert));
            HashCombine(fingerprint, HashValue(payload.blurRadius));
            HashCombine(fingerprint, HashValue(payload.expandContract));
            HashCombine(fingerprint, HashValue(payload.rasterLayer.size()));
            if (!payload.rasterLayer.empty()) {
                HashCombine(
                    fingerprint,
                    HashBytes(
                        reinterpret_cast<const unsigned char*>(payload.rasterLayer.data()),
                        payload.rasterLayer.size() * sizeof(float)));
            }
            HashCombine(fingerprint, HashValue(payload.objects.size()));
            for (const RenderCustomMaskObject& object : payload.objects) {
                HashCombine(fingerprint, HashValue(object.id));
                HashCombine(fingerprint, HashValue(static_cast<int>(object.type)));
                HashCombine(fingerprint, HashValue(static_cast<int>(object.operation)));
                HashCombine(fingerprint, HashValue(object.enabled));
                HashCombine(fingerprint, HashValue(object.invert));
                HashCombine(fingerprint, HashValue(object.strength));
                HashCombine(fingerprint, HashValue(object.feather));
                HashCombine(fingerprint, HashValue(object.blur));
                HashCombine(fingerprint, HashValue(object.points.size()));
                for (const RenderCustomMaskPoint& point : object.points) {
                    HashCombine(fingerprint, HashValue(point.x));
                    HashCombine(fingerprint, HashValue(point.y));
                }
            }
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, input ? fingerprintMask(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskUtilityKind)));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.blackPoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.whitePoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.gamma));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.threshold));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.softness));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageToMaskKind)));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.low));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.high));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.softness));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.invert));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleCount));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[0]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[1]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[2]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleLuma));
            for (int i = 0; i < 4; ++i) {
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][0]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][1]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][2]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleLuma[i]));
            }
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleU));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleV));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.toneSimilarity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.colorSimilarity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.regionRadius));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.regionFeather));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.edgeSensitivity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.localCoherence));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            HashCombine(fingerprint, inputA ? fingerprintMask(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintMask(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.dataMathMode)));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantA));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantB));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.minValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.maxValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMin));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMax));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailAutoMask.settings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.mode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.baseRadiusPercent));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailFusion.settings;
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.baseRadiusPercent));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        }

        fingerprintingMasks.erase(key);
        maskFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    fingerprintImage = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = imageFingerprintCache.find(key);
        if (cached != imageFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingImages.count(key)) {
            return 0;
        }
        fingerprintingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            std::size_t maskFp = fingerprintMask(nodeId, socketId);
            fingerprintingImages.erase(key);
            imageFingerprintCache[key] = maskFp;
            return maskFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(socketId));
        auto hashRawDevelopSettings = [&](const Raw::RawDevelopSettings& settings) {
            HashCombine(fingerprint, HashValue(settings.exposureStops));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.whiteBalanceMode)));
            for (float value : settings.manualWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(settings.overrideBlackLevel));
            HashCombine(fingerprint, HashValue(settings.blackLevelOverride));
            HashCombine(fingerprint, HashValue(settings.overrideWhiteLevel));
            HashCombine(fingerprint, HashValue(settings.whiteLevelOverride));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.highlightMode)));
            HashCombine(fingerprint, HashValue(settings.highlightStrength));
            HashCombine(fingerprint, HashValue(settings.highlightThreshold));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.demosaicMethod)));
            HashCombine(fingerprint, HashValue(settings.cameraTransformEnabled));
            HashCombine(fingerprint, HashValue(settings.debugBypassCameraTransform));
            HashCombine(fingerprint, HashValue(settings.debugTransposeCameraMatrix));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(settings.rotationDegrees));
            HashCombine(fingerprint, HashValue(settings.rotateToFitFrame));
            HashCombine(fingerprint, HashValue(settings.flipHorizontally));
            HashCombine(fingerprint, HashValue(settings.flipVertically));
            HashCombine(fingerprint, HashValue(settings.falseColorSuppression));
            HashCombine(fingerprint, HashValue(settings.defringeStrength));
            HashCombine(fingerprint, HashValue(settings.highlightEdgeCleanup));
            HashCombine(fingerprint, HashValue(settings.chromaRadius));
            HashCombine(fingerprint, HashValue(settings.preserveRealColor));
            HashCombine(fingerprint, HashValue(settings.lateralRedCyan));
            HashCombine(fingerprint, HashValue(settings.lateralBlueYellow));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.cameraTransformSource)));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.enabled));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.hotPixelSuppression));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.hotPixelThreshold));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.lumaStrength));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.chromaStrength));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.radius));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.edgeProtection));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.iterations));
        };

        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                HashCombine(fingerprint, HashValue(node.image.width));
                HashCombine(fingerprint, HashValue(node.image.height));
                HashCombine(fingerprint, HashValue(node.image.channels));
                HashCombine(
                    fingerprint,
                    node.image.pixels.fingerprint != 0
                        ? node.image.pixels.fingerprint
                        : StackHash::HashBytes(*node.image.pixels.bytes));
            } else {
                HashCombine(fingerprint, m_SourceFingerprint);
                HashCombine(fingerprint, HashValue(m_Width));
                HashCombine(fingerprint, HashValue(m_Height));
                HashCombine(fingerprint, HashValue(m_SourceChannels));
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            HashCombine(fingerprint, HashValue(node.rawSource.sourcePath));
            const bool hasEmbeddedRaw =
                !node.rawSource.embeddedRawData.rawBuffer.empty() ||
                !node.rawSource.embeddedRawData.linearUInt16Buffer.empty() ||
                !node.rawSource.embeddedRawData.linearFloatBuffer.empty();
            HashCombine(fingerprint, HashValue(hasEmbeddedRaw));
            if (hasEmbeddedRaw) {
                const Raw::RawImageData& embedded = node.rawSource.embeddedRawData;
                HashCombine(fingerprint, HashValue(embedded.metadata.rawWidth));
                HashCombine(fingerprint, HashValue(embedded.metadata.rawHeight));
                HashCombine(fingerprint, HashValue(static_cast<int>(embedded.metadata.pixelLayout)));
                HashCombine(fingerprint, HashValue(embedded.metadata.bitDepth));
                HashCombine(fingerprint, HashValue(embedded.rawBuffer.size()));
                if (!embedded.rawBuffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.rawBuffer.data()),
                            embedded.rawBuffer.size() * sizeof(std::uint16_t)));
                }
                HashCombine(fingerprint, HashValue(embedded.linearUInt16Buffer.size()));
                if (!embedded.linearUInt16Buffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.linearUInt16Buffer.data()),
                            embedded.linearUInt16Buffer.size() * sizeof(std::uint16_t)));
                }
                HashCombine(fingerprint, HashValue(embedded.linearFloatBuffer.size()));
                if (!embedded.linearFloatBuffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.linearFloatBuffer.data()),
                            embedded.linearFloatBuffer.size() * sizeof(float)));
                }
            }
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.rawWidth));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.rawHeight));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.visibleWidth));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.visibleHeight));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.orientation));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawSource.metadata.cfaPattern)));
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.blackLevel));
            for (float value : node.rawSource.metadata.perChannelBlack) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(node.rawSource.metadata.whiteLevel));
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(NeuralDenoise::SerializeSettings(node.rawNeuralDenoise.settings)));
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
            hashRawDevelopSettings(node.rawDecode.settings);
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
            hashRawDevelopSettings(node.rawDevelop.settings);
            const bool rawBaseSocket = socketId == "__rawDevelopBase";
            if (!rawBaseSocket) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.scenePrepEnabled));
            }
            if (!rawBaseSocket && node.rawDevelop.scenePrepEnabled) {
                const Raw::RawDetailFusionSettings& prep = node.rawDevelop.scenePrepSettings;
                HashCombine(fingerprint, HashValue(prep.autoSafetyEnabled));
                HashCombine(fingerprint, HashValue(prep.overrideMinEv));
                HashCombine(fingerprint, HashValue(prep.overrideMaxEv));
                HashCombine(fingerprint, HashValue(prep.overrideBaseEv));
                HashCombine(fingerprint, HashValue(prep.overrideNoiseProtection));
                HashCombine(fingerprint, HashValue(prep.overrideHighlightProtection));
                HashCombine(fingerprint, HashValue(prep.overrideShadowLiftLimit));
                HashCombine(fingerprint, HashValue(prep.overrideWellExposedTarget));
                HashCombine(fingerprint, HashValue(prep.minEvBias));
                HashCombine(fingerprint, HashValue(prep.maxEvBias));
                HashCombine(fingerprint, HashValue(prep.baseEvBias));
                HashCombine(fingerprint, HashValue(prep.noiseProtectionBias));
                HashCombine(fingerprint, HashValue(prep.highlightProtectionBias));
                HashCombine(fingerprint, HashValue(prep.shadowLiftLimitBias));
                HashCombine(fingerprint, HashValue(prep.wellExposedTargetBias));
                HashCombine(fingerprint, HashValue(prep.minEv));
                HashCombine(fingerprint, HashValue(prep.maxEv));
                HashCombine(fingerprint, HashValue(prep.baseEv));
                HashCombine(fingerprint, HashValue(prep.strength));
                HashCombine(fingerprint, HashValue(prep.sampleCount));
                HashCombine(fingerprint, HashValue(prep.baseRadiusPercent));
                HashCombine(fingerprint, HashValue(prep.highlightProtection));
                HashCombine(fingerprint, HashValue(prep.shadowLiftLimit));
                HashCombine(fingerprint, HashValue(prep.noiseProtection));
                HashCombine(fingerprint, HashValue(prep.detailWeight));
                HashCombine(fingerprint, HashValue(prep.wellExposedTarget));
                HashCombine(fingerprint, HashValue(prep.smoothGradientProtection));
                HashCombine(fingerprint, HashValue(prep.textureSensitivity));
                HashCombine(fingerprint, HashValue(prep.skyBias));
                HashCombine(fingerprint, HashValue(prep.smoothnessRadius));
                HashCombine(fingerprint, HashValue(prep.smoothAreaRadius));
                HashCombine(fingerprint, HashValue(prep.edgeAwareness));
                HashCombine(fingerprint, HashValue(prep.haloGuard));
                HashCombine(fingerprint, HashValue(prep.maskDebandDither));
            }
            const bool preFinishSocket = socketId == EditorNodeGraph::kPreFinishImageOutputSocketId;
            if (!preFinishSocket && !rawBaseSocket) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.integratedToneEnabled));
            }
            if (!preFinishSocket && !rawBaseSocket && node.rawDevelop.integratedToneEnabled) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.integratedToneLayerJson.dump()));
                HashCombine(fingerprint, fingerprintMask(node.nodeId, "maskIn"));
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, fingerprintMask(node.nodeId, "maskOut"));
            const Raw::RawDetailFusionSettings settings = resolveRawDetailFusionApplySettings(node);
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::HdrMerge) {
            const RenderGraphLink* input1 = findInputLink(node.nodeId, "image1");
            const RenderGraphLink* input2 = findInputLink(node.nodeId, "image2");
            const RenderGraphLink* input3 = findInputLink(node.nodeId, "image3");
            HashCombine(fingerprint, input1 ? fingerprintImage(input1->fromNodeId, input1->fromSocketId) : 0);
            HashCombine(fingerprint, input2 ? fingerprintImage(input2->fromNodeId, input2->fromSocketId) : 0);
            HashCombine(fingerprint, input3 ? fingerprintImage(input3->fromNodeId, input3->fromSocketId) : 0);
            const Raw::HdrMergeSettings& settings = node.hdrMerge.settings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.alignmentMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.exposureMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.referenceMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.deghostMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.motionPriority)));
            for (float exposureEv : settings.manualExposureEv) {
                HashCombine(fingerprint, HashValue(exposureEv));
            }
            for (float exposureEv : settings.exposureOffsetEv) {
                HashCombine(fingerprint, HashValue(exposureEv));
            }
            HashCombine(fingerprint, HashValue(settings.autoReliability));
            HashCombine(fingerprint, HashValue(settings.clipThreshold));
            HashCombine(fingerprint, HashValue(settings.clipFeather));
            HashCombine(fingerprint, HashValue(settings.blackThreshold));
            HashCombine(fingerprint, HashValue(settings.blackFeather));
            HashCombine(fingerprint, HashValue(settings.readNoise));
            HashCombine(fingerprint, HashValue(settings.noiseAware));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.importFormat)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.useMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.inputTransform)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.outputTransform)));
            HashCombine(fingerprint, hashLut1DStage(node.lut.lut1D));
            HashCombine(fingerprint, hashLut1DStage(node.lut.shaper1D));
            HashCombine(fingerprint, hashLut3DStage(node.lut.lut3D));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageGeneratorKind)));
            for (float channel : node.imageGeneratorSettings.colorA) {
                HashCombine(fingerprint, HashValue(channel));
            }
            for (float channel : node.imageGeneratorSettings.colorB) {
                HashCombine(fingerprint, HashValue(channel));
            }
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.angle));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.offset));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.text));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.fontSize));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(node.layerJson));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
            HashCombine(fingerprint, inputA ? fingerprintImage(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintImage(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, factorLink ? fingerprintMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.mixBlendMode)));
            HashCombine(fingerprint, HashValue(node.mixFactor));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const bool scalarA = inputA && isScalarRenderSocket(inputA->fromNodeId, inputA->fromSocketId);
            const bool scalarB = inputB && isScalarRenderSocket(inputB->fromNodeId, inputB->fromSocketId);
            HashCombine(fingerprint, inputA ? (scalarA ? fingerprintMask(inputA->fromNodeId, inputA->fromSocketId) : fingerprintImage(inputA->fromNodeId, inputA->fromSocketId)) : 0);
            HashCombine(fingerprint, inputB ? (scalarB ? fingerprintMask(inputB->fromNodeId, inputB->fromSocketId) : fingerprintImage(inputB->fromNodeId, inputB->fromSocketId)) : 0);
            HashCombine(fingerprint, HashValue(scalarA));
            HashCombine(fingerprint, HashValue(scalarB));
            HashCombine(fingerprint, HashValue(isScalarRenderSocket(node.nodeId, socketId)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.dataMathMode)));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantA));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantB));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.minValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.maxValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMin));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMax));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                HashCombine(fingerprint, fingerprintImage(input->fromNodeId, input->fromSocketId));
            } else {
                const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
                const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
                const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
                const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
                HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
                HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
                HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
                HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            }
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
            HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
            HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
            HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
            HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        }

        fingerprintingImages.erase(key);
        imageFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    evalMask = [&](int nodeId, const std::string& socketId) -> unsigned int {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        if (maskCache.count(key)) {
            return maskCache[key];
        }
        if (visitingMasks.count(key)) {
            return 0;
        }
        visitingMasks.insert(key);
        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::HdrMerge ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            (node.kind == RenderGraphNodeKind::DataMath && !isScalarRenderSocket(nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            unsigned int imgTex = evalImage(nodeId, socketId);
            visitingMasks.erase(key);
            return imgTex;
        }
        const std::size_t fingerprint = fingerprintMask(nodeId, socketId);
        if (const auto cached = m_GraphMaskCache.find(key);
            cached != m_GraphMaskCache.end() &&
            cached->second.fingerprint == fingerprint &&
            cached->second.texture != 0) {
            ++m_LastGraphExecutionStats.maskCacheHits;
            maskCache[key] = cached->second.texture;
            visitingMasks.erase(key);
            return cached->second.texture;
        }
        ++m_LastGraphExecutionStats.maskCacheMisses;

        unsigned int result = 0;
        bool resultOwned = false;
        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            RenderMaskSource mask;
            mask.nodeId = node.nodeId;
            mask.kind = node.maskKind;
            mask.settings = node.maskSettings;
            result = GenerateMaskTexture(mask);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            const unsigned int maskA = inputA ? evalMask(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const unsigned int maskB = inputB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (maskA && maskB) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMaskCombine(maskA, maskB, node.maskCombineMode, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::CustomMask) {
            result = GenerateCustomMaskTexture(node.customMask);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            const unsigned int inputMask = input ? evalMask(input->fromNodeId, input->fromSocketId) : 0;
            if (inputMask) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMaskUtility(inputMask, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderImageToMask(inputImage, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                int channelIdx = 0;
                if (socketId == "g") channelIdx = 1;
                else if (socketId == "b") channelIdx = 2;
                else if (socketId == "a") channelIdx = 3;
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderChannelSplit(inputImage, channelIdx, fbo);
                });
                resultOwned = result != 0;
            } else {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    GLfloat previousClearColor[4];
                    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
                    if (socketId == "a") {
                        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
                    } else {
                        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    }
                    glClear(GL_COLOR_BUFFER_BIT);
                    glClearColor(
                        previousClearColor[0],
                        previousClearColor[1],
                        previousClearColor[2],
                        previousClearColor[3]);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const bool scalarA = inputA && isScalarRenderSocket(inputA->fromNodeId, inputA->fromSocketId);
            const bool scalarB = inputB && isScalarRenderSocket(inputB->fromNodeId, inputB->fromSocketId);
            const unsigned int textureA = inputA ? evalMask(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const unsigned int textureB = inputB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if ((!inputA || textureA) && (!inputB || textureB)) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderDataMath(
                        textureA,
                        textureB,
                        inputA != nullptr,
                        inputB != nullptr,
                        scalarA,
                        scalarB,
                        node.dataMathMode,
                        node.dataMathSettings,
                        true,
                        fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                const bool debugPreview = graph.autoGainMaskPreview &&
                    graph.outputNodeId == node.nodeId &&
                    graph.outputSocketId == "maskOut";
                result = RenderRawDetailAutoMask(inputImage, node, 0, debugPreview);
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            const unsigned int manualMask = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
            if (inputImage) {
                const bool debugPreview = graph.autoGainMaskPreview &&
                    graph.outputNodeId == node.nodeId &&
                    graph.outputSocketId == "maskOut";
                result = RenderRawDetailAutoMask(inputImage, node, manualMask, debugPreview);
                resultOwned = result != 0;
            }
        }

        if (result) {
            maskCache[key] = result;
            storeCacheEntry(m_GraphMaskCache, key, result, fingerprint, resultOwned);
        } else {
            releaseCacheEntry(m_GraphMaskCache, key);
        }
        visitingMasks.erase(key);
        return result;
    };

    evalImage = [&](int nodeId, const std::string& socketId) -> unsigned int {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        if (imageCache.count(key)) {
            return imageCache[key];
        }
        if (visitingImages.count(key)) {
            return 0;
        }
        visitingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            unsigned int maskTex = evalMask(nodeId, socketId);
            visitingImages.erase(key);
            return maskTex;
        }
        const std::size_t fingerprint = fingerprintImage(nodeId, socketId);
        const bool rawDevelopStageSocket =
            node.kind == RenderGraphNodeKind::RawDevelop &&
            (socketId == "__rawDevelopBase" ||
             socketId == EditorNodeGraph::kPreFinishImageOutputSocketId);
        if (!rawDevelopStageSocket) {
            if (const auto cached = m_GraphImageCache.find(key);
                cached != m_GraphImageCache.end() &&
                cached->second.fingerprint == fingerprint &&
                cached->second.texture != 0) {
                ++m_LastGraphExecutionStats.imageCacheHits;
                if (cached->second.width > 0 && cached->second.height > 0) {
                    m_Width = cached->second.width;
                    m_Height = cached->second.height;
                }
                imageCache[key] = cached->second.texture;
                m_LastGraphImageCacheHits.insert(key);
                visitingImages.erase(key);
                return cached->second.texture;
            }
        }
        ++m_LastGraphExecutionStats.imageCacheMisses;

        struct SharedRawBaseStageResult {
            const RenderGraphNode* rawSource = nullptr;
            std::string sourcePath;
            unsigned int texture = 0;
            int width = 0;
            int height = 0;
            bool renderedThisPass = false;
        };

        auto findUpstreamRawSourceNode = [&](const RenderGraphNode& rawConsumer) -> const RenderGraphNode* {
            const RenderGraphLink* rawInput = findInputLink(rawConsumer.nodeId, "rawIn");
            std::set<int> rawVisit;
            while (rawInput) {
                if (!rawVisit.insert(rawInput->fromNodeId).second) {
                    break;
                }
                const auto rawIt = nodes.find(rawInput->fromNodeId);
                if (rawIt == nodes.end() || !rawIt->second) {
                    break;
                }
                if (rawIt->second->kind == RenderGraphNodeKind::RawSource) {
                    return rawIt->second;
                }
                if (rawIt->second->kind != RenderGraphNodeKind::RawNeuralDenoise) {
                    break;
                }
                rawInput = findInputLink(rawIt->second->nodeId, "rawIn");
            }
            return nullptr;
        };

        auto renderSharedRawBaseStage = [&](const RenderGraphNode& rawConsumer,
                                            const Raw::RawDevelopSettings& settings,
                                            const std::string& rawBaseKey,
                                            std::size_t rawBaseFingerprint) -> SharedRawBaseStageResult {
            SharedRawBaseStageResult stage;
            stage.rawSource = findUpstreamRawSourceNode(rawConsumer);
            if (!stage.rawSource) {
                return stage;
            }

            const bool hasEmbeddedRaw =
                !stage.rawSource->rawSource.embeddedRawData.rawBuffer.empty() ||
                !stage.rawSource->rawSource.embeddedRawData.linearUInt16Buffer.empty() ||
                !stage.rawSource->rawSource.embeddedRawData.linearFloatBuffer.empty();
            stage.sourcePath = stage.rawSource->rawSource.sourcePath.empty()
                ? stage.rawSource->rawSource.metadata.sourcePath
                : stage.rawSource->rawSource.sourcePath;

            Raw::RawImageData& rawData = m_RawDataCache[stage.rawSource->nodeId];
            std::string& cachedPath = m_RawDataCachePaths[stage.rawSource->nodeId];
            const std::string cacheKeyPath = hasEmbeddedRaw
                ? std::string("__embedded_raw__:") + std::to_string(fingerprintImage(stage.rawSource->nodeId, "rawOut"))
                : stage.sourcePath;
            if (hasEmbeddedRaw) {
                if (cachedPath != cacheKeyPath) {
                    rawData = stage.rawSource->rawSource.embeddedRawData;
                    cachedPath = cacheKeyPath;
                }
            } else if (cachedPath != cacheKeyPath || rawData.rawBuffer.empty()) {
                Raw::RawImageData loadedRaw;
                if (Raw::RawLoader::LoadFile(stage.sourcePath, loadedRaw)) {
                    rawData = std::move(loadedRaw);
                    cachedPath = cacheKeyPath;
                } else {
                    rawData = std::move(loadedRaw);
                    cachedPath = cacheKeyPath;
                }
            }

            const bool rawDataHasPixels =
                !rawData.rawBuffer.empty() ||
                !rawData.linearUInt16Buffer.empty() ||
                !rawData.linearFloatBuffer.empty();
            if (!rawDataHasPixels || !rawData.metadata.error.empty()) {
                const std::string error = !rawData.metadata.error.empty()
                    ? rawData.metadata.error
                    : "LibRaw did not produce a usable raw buffer.";
                std::cerr << "[RAW] Load failed for source node " << stage.rawSource->nodeId
                          << " (" << stage.sourcePath << "): " << error << "\n";
                return stage;
            }

            if (stage.rawSource->rawSource.metadata.visibleWidth > 0) {
                rawData.metadata.visibleWidth = stage.rawSource->rawSource.metadata.visibleWidth;
            }
            if (stage.rawSource->rawSource.metadata.visibleHeight > 0) {
                rawData.metadata.visibleHeight = stage.rawSource->rawSource.metadata.visibleHeight;
            }

            const CachedGraphTexture stageCachedBase =
                findRawDevelopStageCacheEntry(rawBaseKey, rawBaseFingerprint);
            if (stageCachedBase.texture != 0 &&
                stageCachedBase.width > 0 &&
                stageCachedBase.height > 0) {
                ++m_LastGraphExecutionStats.rawStageCacheHits;
                stage.texture = stageCachedBase.texture;
                stage.width = stageCachedBase.width;
                stage.height = stageCachedBase.height;
                m_Width = stage.width;
                m_Height = stage.height;
                storeCacheEntry(m_GraphImageCache, rawBaseKey, stage.texture, rawBaseFingerprint, false);
                m_LastGraphImageCacheHits.insert(rawBaseKey);
            } else if (const auto cachedBase = m_GraphImageCache.find(rawBaseKey);
                cachedBase != m_GraphImageCache.end() &&
                cachedBase->second.fingerprint == rawBaseFingerprint &&
                cachedBase->second.texture != 0 &&
                cachedBase->second.owned &&
                cachedBase->second.width > 0 &&
                cachedBase->second.height > 0) {
                ++m_LastGraphExecutionStats.rawStageCacheHits;
                stage.texture = cachedBase->second.texture;
                stage.width = cachedBase->second.width;
                stage.height = cachedBase->second.height;
                m_Width = stage.width;
                m_Height = stage.height;
                m_LastGraphImageCacheHits.insert(rawBaseKey);
            }

            if (stage.texture == 0) {
                ++m_LastGraphExecutionStats.rawStageCacheMisses;
                stage.texture = m_RawPipelines[rawConsumer.nodeId].Render(rawData, settings, m_PreviewMaxDimension);
                if (stage.texture == 0) {
                    releaseCacheEntry(m_GraphImageCache, rawBaseKey);
                    const std::string& error = m_RawPipelines[rawConsumer.nodeId].GetLastError();
                    std::cerr << "[RAW] Render failed for "
                              << (rawConsumer.kind == RenderGraphNodeKind::RawDecode ? "decode" : "develop")
                              << " node " << rawConsumer.nodeId
                              << " (" << stage.sourcePath << "): "
                              << (error.empty() ? "unknown RAW GPU failure" : error)
                              << "\n";
                    return stage;
                }
                stage.width = m_RawPipelines[rawConsumer.nodeId].GetOutputWidth();
                stage.height = m_RawPipelines[rawConsumer.nodeId].GetOutputHeight();
                stage.renderedThisPass = true;
            }

            if (stage.texture != 0) {
                if (stage.width <= 0) {
                    stage.width = m_RawPipelines[rawConsumer.nodeId].GetOutputWidth();
                }
                if (stage.height <= 0) {
                    stage.height = m_RawPipelines[rawConsumer.nodeId].GetOutputHeight();
                }
                if (stage.width > 0 && stage.height > 0) {
                    m_Width = stage.width;
                    m_Height = stage.height;
                }
                if (stage.renderedThisPass) {
                    storeRawDevelopStageCacheEntry(rawBaseKey, stage.texture, rawBaseFingerprint);
                }
                imageCache[rawBaseKey] = stage.texture;
            }

            return stage;
        };

        unsigned int result = 0;
        bool resultOwned = false;
        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                result = GLHelpers::CreateTextureFromPixels(
                    node.image.pixels.data(),
                    node.image.width,
                    node.image.height,
                    node.image.channels);
                resultOwned = result != 0;
            } else {
                result = m_SourceTexture;
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const std::string rawBaseKey = std::to_string(node.nodeId) + ":__rawDecodeBase";
            const std::size_t rawBaseFingerprint = fingerprintImage(node.nodeId, "__rawDecodeBase");
            const SharedRawBaseStageResult rawBaseStage =
                renderSharedRawBaseStage(node, node.rawDecode.settings, rawBaseKey, rawBaseFingerprint);
            result = rawBaseStage.texture;
            resultOwned = false;
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const std::string preFinishKey = std::to_string(node.nodeId) + ":" + EditorNodeGraph::kPreFinishImageOutputSocketId;
            const std::string rawBaseKey = std::to_string(node.nodeId) + ":__rawDevelopBase";
            const std::size_t rawBaseFingerprint = fingerprintImage(node.nodeId, "__rawDevelopBase");
            const bool wantsPreFinishSocket = socketId == EditorNodeGraph::kPreFinishImageOutputSocketId;
            const bool wantsIntegratedFinal =
                !wantsPreFinishSocket &&
                node.rawDevelop.integratedToneEnabled &&
                node.rawDevelop.settings.debugView == Raw::RawDebugView::FinalOutput &&
                node.rawDevelop.integratedToneLayerJson.is_object();

            if (wantsPreFinishSocket) {
                const CachedGraphTexture cachedPreFinish =
                    findRawDevelopStageCacheEntry(preFinishKey, fingerprint);
                if (cachedPreFinish.texture != 0 &&
                    cachedPreFinish.width > 0 &&
                    cachedPreFinish.height > 0) {
                    ++m_LastGraphExecutionStats.rawStageCacheHits;
                    m_Width = cachedPreFinish.width;
                    m_Height = cachedPreFinish.height;
                    imageCache[key] = cachedPreFinish.texture;
                    storeCacheEntry(m_GraphImageCache, key, cachedPreFinish.texture, fingerprint, false);
                    m_LastGraphImageCacheHits.insert(key);
                    visitingImages.erase(key);
                    return cachedPreFinish.texture;
                }
            }

            const SharedRawBaseStageResult rawBaseStage =
                renderSharedRawBaseStage(node, node.rawDevelop.settings, rawBaseKey, rawBaseFingerprint);
            const unsigned int rawDevelopBase = rawBaseStage.texture;
            if (rawDevelopBase != 0) {
                if (wantsIntegratedFinal) {
                    const unsigned int preToneTexture = evalImage(node.nodeId, EditorNodeGraph::kPreFinishImageOutputSocketId);
                    if (preToneTexture != 0) {
                        std::shared_ptr<LayerBase> integratedToneLayer = LayerRegistry::CreateLayerFromTypeId("ToneCurve");
                        if (integratedToneLayer) {
                            integratedToneLayer->InitializeGL();
                            integratedToneLayer->Deserialize(node.rawDevelop.integratedToneLayerJson);
                            if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(integratedToneLayer.get())) {
                                toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
                                toneCurve->SetDevelopScenePrepToneBudget(
                                    node.rawDevelop.scenePrepEnabled,
                                    node.rawDevelop.scenePrepSettings.strength,
                                    node.rawDevelop.scenePrepSettings.maxEvBias);
                            }

                            unsigned int finishedResult = createTarget();
                            const bool renderedIntegratedTone = renderToTexture(finishedResult, [&](unsigned int) {
                                integratedToneLayer->ExecuteWithSource(preToneTexture, preToneTexture, m_Width, m_Height, m_Quad);
                            });
                            bool useFinishedResult = renderedIntegratedTone && finishedResult != 0;
                            if (useFinishedResult) {
                                const QuickTextureStats inputStats = ProbeTextureStats(preToneTexture, m_Width, m_Height);
                                const QuickTextureStats outputStats = ProbeTextureStats(finishedResult, m_Width, m_Height);
                                const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
                                const bool outputIsBlank =
                                    outputStats.valid &&
                                    outputStats.p99Luma <= 0.000001f &&
                                    outputStats.maxRgb <= 0.00001f;
                                if (inputHasSignal && outputIsBlank) {
                                    glDeleteTextures(1, &finishedResult);
                                    finishedResult = 0;
                                    useFinishedResult = false;
                                    std::cerr << "[RenderPipeline] Integrated Develop ToneCurve produced a blank output for RawDevelop node "
                                              << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                                              << ", output p99 luma " << outputStats.p99Luma
                                              << "); passing pre-finish texture through.\n";
                                }
                            }
                            if (useFinishedResult) {
                                result = finishedResult;
                                resultOwned = true;
                            } else {
                                if (finishedResult != 0) {
                                    glDeleteTextures(1, &finishedResult);
                                }
                                result = preToneTexture;
                                resultOwned = false;
                            }

                            if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(integratedToneLayer.get());
                                toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
                                m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
                            }

                            if (useFinishedResult && finishedResult != 0) {
                                const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
                                const unsigned int finishMaskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
                                if (finishMaskTexture != 0) {
                                    unsigned int blended = createTarget();
                                    renderToTexture(blended, [&](unsigned int fbo) {
                                        RenderMaskBlend(preToneTexture, finishedResult, finishMaskTexture, fbo);
                                    });
                                    if (blended != 0 && finishedResult != 0) {
                                        glDeleteTextures(1, &finishedResult);
                                    }
                                    result = blended != 0 ? blended : finishedResult;
                                    resultOwned = true;
                                }
                            }
                        } else {
                            result = preToneTexture;
                            resultOwned = false;
                        }
                    }
                } else {
                    result = rawDevelopBase;
                    resultOwned = false;
                    if (result != 0 &&
                        node.rawDevelop.scenePrepEnabled &&
                        node.rawDevelop.settings.debugView == Raw::RawDebugView::FinalOutput) {
                        Raw::RawDetailFusionSettings prepSettings = node.rawDevelop.scenePrepSettings;
                        prepSettings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
                        prepSettings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
                        prepSettings.invertMask = false;
                        prepSettings.maskBlackPoint = 0.0f;
                        prepSettings.maskWhitePoint = 1.0f;
                        prepSettings.maskGamma = 1.0f;
                        prepSettings.manualBlend = 0.0f;
                        if (prepSettings.autoSafetyEnabled && !prepSettings.overrideBaseEv) {
                            // Develop already chose the RAW baseline exposure. Keep Scene Prep from
                            // recomputing a second global base that can cancel that authored intent.
                            prepSettings.baseEv = std::clamp(prepSettings.baseEvBias, -1.0f, 1.0f);
                            prepSettings.overrideBaseEv = true;
                        }

                        m_PreLocalExposureSummaries[node.nodeId] = BuildPreLocalExposureSummary(
                            result,
                            prepSettings,
                            false,
                            !prepSettings.autoSafetyEnabled);

                        RenderGraphNode prepMapNode;
                        prepMapNode.nodeId = node.nodeId;
                        prepMapNode.kind = RenderGraphNodeKind::RawDetailFusion;
                        prepMapNode.rawDetailFusion.settings = prepSettings;
                        const unsigned int preScenePrepTexture = result;
                        unsigned int prepExposureMap = RenderRawDetailAutoMask(result, prepMapNode, 0, false);
                        if (unsigned int preparedResult = prepExposureMap != 0
                            ? RenderRawDetailFusion(result, prepExposureMap, prepSettings)
                            : 0) {
                            const QuickTextureStats inputStats =
                                ProbeTextureStats(preScenePrepTexture, m_Width, m_Height);
                            const QuickTextureStats outputStats =
                                ProbeTextureStats(preparedResult, m_Width, m_Height);
                            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
                            const bool outputIsBlank =
                                outputStats.valid &&
                                outputStats.p99Luma <= 0.000001f &&
                                outputStats.maxRgb <= 0.00001f;
                            if (inputHasSignal && outputIsBlank) {
                                glDeleteTextures(1, &preparedResult);
                                std::cerr << "[RenderPipeline] Develop scene prep produced a blank output for RawDevelop node "
                                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                                          << ", output p99 luma " << outputStats.p99Luma
                                          << "); passing RAW base texture through.\n";
                            } else {
                                result = preparedResult;
                                resultOwned = true;
                            }
                        }
                        if (prepExposureMap != 0) {
                            glDeleteTextures(1, &prepExposureMap);
                        }
                    }
                    imageCache[preFinishKey] = result;
                }

                if (wantsPreFinishSocket) {
                    // The hidden pre-finish output intentionally exposes Develop after RAW conversion
                    // and scene prep, but before integrated finish tone or finish-mask blending.
                    if (result != 0) {
                        const std::size_t preFinishFingerprint = fingerprintImage(node.nodeId, socketId);
                        storeCacheEntry(m_GraphImageCache, key, result, preFinishFingerprint, resultOwned);
                        storeRawDevelopStageCacheEntry(key, result, preFinishFingerprint);
                    } else {
                        releaseCacheEntry(m_GraphImageCache, key);
                    }
                    visitingImages.erase(key);
                    return result;
                }
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            const unsigned int generatedMask = inputImage ? evalMask(node.nodeId, "maskOut") : 0;
            if (inputImage) {
                const Raw::RawDetailFusionSettings applySettings = resolveRawDetailFusionApplySettings(node);
                m_PreLocalExposureSummaries[node.nodeId] = BuildPreLocalExposureSummary(
                    inputImage,
                    applySettings,
                    maskLink != nullptr,
                    !node.rawDetailFusion.settings.autoSafetyEnabled);
                result = RenderRawDetailFusion(inputImage, generatedMask, applySettings);
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::HdrMerge) {
            const RenderGraphLink* input1 = findInputLink(node.nodeId, "image1");
            const RenderGraphLink* input2 = findInputLink(node.nodeId, "image2");
            const RenderGraphLink* input3 = findInputLink(node.nodeId, "image3");
            const unsigned int texture1 = input1 ? evalImage(input1->fromNodeId, input1->fromSocketId) : 0;
            const unsigned int texture2 = input2 ? evalImage(input2->fromNodeId, input2->fromSocketId) : 0;
            const unsigned int texture3 = input3 ? evalImage(input3->fromNodeId, input3->fromSocketId) : 0;
            const bool hasGap = input3 != nullptr && input2 == nullptr;
            const bool hasRequiredInputs = texture1 != 0 &&
                texture2 != 0 &&
                (input3 == nullptr || texture3 != 0);
            if (!hasGap && hasRequiredInputs) {
                std::array<bool, 3> activeInputs { input1 != nullptr, input2 != nullptr, input3 != nullptr };
                std::array<HdrMergeInputContext, 3> inputContexts {};
                if (input1) inputContexts[0] = resolveHdrMergeInputContext(input1->fromNodeId);
                if (input2) inputContexts[1] = resolveHdrMergeInputContext(input2->fromNodeId);
                if (input3) inputContexts[2] = resolveHdrMergeInputContext(input3->fromNodeId);
                const HdrMergeResolvedSettings resolved = resolveHdrMergeSettings(node.hdrMerge.settings, inputContexts, activeInputs);
                result = createTarget();
                bool mergeRendered = false;
                renderToTexture(result, [&](unsigned int fbo) {
                    mergeRendered = RenderHdrMerge(
                        texture1,
                        texture2,
                        texture3,
                        input2 != nullptr,
                        input3 != nullptr,
                        node.hdrMerge.settings,
                        resolved,
                        fbo);
                });
                if (!mergeRendered && result != 0) {
                    glDeleteTextures(1, &result);
                    result = 0;
                }
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            unsigned int combinedInputTexture = 0;
            bool combinedInputOwned = false;
            const unsigned int inputTexture = [&]() -> unsigned int {
                if (input) {
                    return evalImage(input->fromNodeId, input->fromSocketId);
                }

                const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
                const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
                const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
                const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
                if (!linkR && !linkG && !linkB && !linkA) {
                    return 0;
                }

                const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
                const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
                const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
                const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

                combinedInputTexture = createTarget();
                renderToTexture(combinedInputTexture, [&](unsigned int fbo) {
                    RenderChannelCombine(
                        texR,
                        texG,
                        texB,
                        texA,
                        linkR != nullptr,
                        linkG != nullptr,
                        linkB != nullptr,
                        linkA != nullptr,
                        fbo);
                });
                combinedInputOwned = combinedInputTexture != 0;
                return combinedInputTexture;
            }();
            const std::string lut1DKey = std::to_string(node.nodeId) + ":lut1d";
            const std::string shaperKey = std::to_string(node.nodeId) + ":shaper1d";
            const std::string lut3DKey = std::to_string(node.nodeId) + ":lut3d";
            if (inputTexture) {
                if (!ColorLut::HasAnyLutData(node.lut)) {
                    clearLutTextureKey(lut1DKey);
                    clearLutTextureKey(shaperKey);
                    clearLutTextureKey(lut3DKey);
                    result = inputTexture;
                    resultOwned = false;
                } else {
                    EnsureLutProgram();
                    const bool hasLut1D = ColorLut::HasLut1D(node.lut);
                    const bool hasShaper1D = ColorLut::HasShaper1D(node.lut);
                    const bool hasLut3D = ColorLut::HasLut3D(node.lut);
                    if (!hasLut1D) clearLutTextureKey(lut1DKey);
                    if (!hasShaper1D) clearLutTextureKey(shaperKey);
                    if (!hasLut3D) clearLutTextureKey(lut3DKey);

                    const unsigned int lut1DTexture = hasLut1D
                        ? getOrCreateLut1DTexture(
                            lut1DKey,
                            node.lut.lut1D,
                            hashLut1DStage(node.lut.lut1D))
                        : 0;
                    const unsigned int shaperTexture = hasShaper1D
                        ? getOrCreateLut1DTexture(
                            shaperKey,
                            node.lut.shaper1D,
                            hashLut1DStage(node.lut.shaper1D))
                        : 0;
                    const unsigned int lut3DTexture = hasLut3D
                        ? getOrCreateLut3DTexture(
                            lut3DKey,
                            node.lut.lut3D,
                            hashLut3DStage(node.lut.lut3D))
                        : 0;

                    const bool missingRequiredTexture =
                        (hasLut1D && lut1DTexture == 0) ||
                        (hasShaper1D && shaperTexture == 0) ||
                        (hasLut3D && lut3DTexture == 0) ||
                        m_LutProgram == 0;
                    if (missingRequiredTexture) {
                        result = inputTexture;
                        resultOwned = combinedInputOwned;
                    } else {
                        unsigned int processed = createTarget();
                        const bool renderedLut = renderToTexture(processed, [&](unsigned int) {
                            glUseProgram(m_LutProgram);

                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, inputTexture);
                            glUniform1i(glGetUniformLocation(m_LutProgram, "uImage"), 0);

                            glActiveTexture(GL_TEXTURE1);
                            glBindTexture(GL_TEXTURE_2D, lut1DTexture);
                            glUniform1i(glGetUniformLocation(m_LutProgram, "uLut1D"), 1);

                            glActiveTexture(GL_TEXTURE2);
                            glBindTexture(GL_TEXTURE_2D, shaperTexture);
                            glUniform1i(glGetUniformLocation(m_LutProgram, "uShaper1D"), 2);

                            glActiveTexture(GL_TEXTURE3);
                            glBindTexture(GL_TEXTURE_3D, lut3DTexture);
                            glUniform1i(glGetUniformLocation(m_LutProgram, "uLut3D"), 3);

                            glUniform1i(glGetUniformLocation(m_LutProgram, "uHasLut1D"), hasLut1D ? 1 : 0);
                            glUniform1i(glGetUniformLocation(m_LutProgram, "uHasShaper1D"), hasShaper1D ? 1 : 0);
                            glUniform1i(glGetUniformLocation(m_LutProgram, "uHasLut3D"), hasLut3D ? 1 : 0);
                            glUniform1i(
                                glGetUniformLocation(m_LutProgram, "uInputTransform"),
                                static_cast<int>(node.lut.inputTransform));
                            glUniform1i(
                                glGetUniformLocation(m_LutProgram, "uOutputTransform"),
                                static_cast<int>(node.lut.outputTransform));
                            glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut1DDomainMin"), 1, node.lut.lut1D.domainMin.data());
                            glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut1DDomainMax"), 1, node.lut.lut1D.domainMax.data());
                            glUniform3fv(glGetUniformLocation(m_LutProgram, "uShaperDomainMin"), 1, node.lut.shaper1D.domainMin.data());
                            glUniform3fv(glGetUniformLocation(m_LutProgram, "uShaperDomainMax"), 1, node.lut.shaper1D.domainMax.data());
                            glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut3DDomainMin"), 1, node.lut.lut3D.domainMin.data());
                            glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut3DDomainMax"), 1, node.lut.lut3D.domainMax.data());
                            m_Quad.Draw();

                            glActiveTexture(GL_TEXTURE3);
                            glBindTexture(GL_TEXTURE_3D, 0);
                            glActiveTexture(GL_TEXTURE2);
                            glBindTexture(GL_TEXTURE_2D, 0);
                            glActiveTexture(GL_TEXTURE1);
                            glBindTexture(GL_TEXTURE_2D, 0);
                            glActiveTexture(GL_TEXTURE0);
                        });

                        if (!renderedLut || processed == 0) {
                            if (processed != 0) {
                                glDeleteTextures(1, &processed);
                            }
                            result = inputTexture;
                            resultOwned = combinedInputOwned;
                        } else {
                            result = processed;
                            resultOwned = true;
                            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
                            const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
                            if (maskTexture) {
                                unsigned int blended = createTarget();
                                renderToTexture(blended, [&](unsigned int fbo) {
                                    RenderMaskBlend(inputTexture, processed, maskTexture, fbo);
                                });
                                if (processed != 0) {
                                    glDeleteTextures(1, &processed);
                                }
                                result = blended != 0 ? blended : inputTexture;
                                if (blended != 0) {
                                    if (combinedInputOwned && combinedInputTexture != 0) {
                                        glDeleteTextures(1, &combinedInputTexture);
                                        combinedInputTexture = 0;
                                        combinedInputOwned = false;
                                    }
                                    resultOwned = true;
                                } else {
                                    resultOwned = combinedInputOwned;
                                }
                            }
                            if (maskTexture == 0 && combinedInputOwned && combinedInputTexture != 0) {
                                glDeleteTextures(1, &combinedInputTexture);
                                combinedInputTexture = 0;
                                combinedInputOwned = false;
                            }
                        }
                    }
                }
            }
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            result = GenerateImageTexture(node);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputTexture && node.layerJson.is_object()) {
                const std::string type = node.layerJson.value("type", std::string());
                std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
                if (layer) {
                    layer->InitializeGL();
                    layer->Deserialize(node.layerJson);
                    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get())) {
                        toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
                    }
                    unsigned int processed = createTarget();
                    const unsigned int sourceTexture = m_SourceTexture != 0 ? m_SourceTexture : inputTexture;
                    const bool renderedLayer = renderToTexture(processed, [&](unsigned int) {
                        layer->ExecuteWithSource(inputTexture, sourceTexture, m_Width, m_Height, m_Quad);
                    });
                    if (!renderedLayer || processed == 0) {
                        if (processed != 0) {
                            glDeleteTextures(1, &processed);
                        }
                        result = inputTexture;
                        resultOwned = false;
                        std::cerr << "[RenderPipeline] Layer target allocation failed for graph node "
                                  << node.nodeId << "; passing input texture through.\n";
                        imageCache[key] = result;
                        storeCacheEntry(m_GraphImageCache, key, result, fingerprint, false);
                        visitingImages.erase(key);
                        return result;
                    }
                    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get());
                        toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
                        m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
                    }
                    result = processed;
                    resultOwned = result != 0;
                    if (type == "ToneCurve" && IsDefaultToneCurvePayload(node.layerJson)) {
                        const QuickTextureStats inputStats = ProbeTextureStats(inputTexture, m_Width, m_Height);
                        const QuickTextureStats outputStats = ProbeTextureStats(processed, m_Width, m_Height);
                        const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
                        const bool outputIsBlank = outputStats.valid && outputStats.p99Luma <= 0.000001f && outputStats.maxRgb <= 0.00001f;
                        if (inputHasSignal && outputIsBlank) {
                            if (processed != 0) {
                                glDeleteTextures(1, &processed);
                            }
                            result = inputTexture;
                            resultOwned = false;
                            std::cerr << "[RenderPipeline] Default Tone Curve produced a blank output for graph node "
                                      << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                                      << ", output p99 luma " << outputStats.p99Luma
                                      << "); passing input texture through.\n";
                            imageCache[key] = result;
                            storeCacheEntry(m_GraphImageCache, key, result, fingerprint, false);
                            visitingImages.erase(key);
                            return result;
                        }
                    }
                    const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
                    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
                    if (maskTexture) {
                        unsigned int blended = createTarget();
                        renderToTexture(blended, [&](unsigned int fbo) {
                            RenderMaskBlend(inputTexture, processed, maskTexture, fbo);
                        });
                        if (processed != 0) {
                            glDeleteTextures(1, &processed);
                        }
                        result = blended;
                        resultOwned = result != 0;
                    }
                }
            }
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const unsigned int textureA = inputA ? evalImage(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const unsigned int textureB = inputB ? evalImage(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (textureA && textureB) {
                const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
                const unsigned int factorTexture = factorLink ? evalMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0;
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMixBlend(textureA, textureB, factorTexture, node.mixFactor, node.mixBlendMode, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const bool scalarA = inputA && isScalarRenderSocket(inputA->fromNodeId, inputA->fromSocketId);
            const bool scalarB = inputB && isScalarRenderSocket(inputB->fromNodeId, inputB->fromSocketId);
            const unsigned int textureA = inputA
                ? (scalarA ? evalMask(inputA->fromNodeId, inputA->fromSocketId) : evalImage(inputA->fromNodeId, inputA->fromSocketId))
                : 0;
            const unsigned int textureB = inputB
                ? (scalarB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : evalImage(inputB->fromNodeId, inputB->fromSocketId))
                : 0;
            if ((!inputA || textureA) && (!inputB || textureB)) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderDataMath(
                        textureA,
                        textureB,
                        inputA != nullptr,
                        inputB != nullptr,
                        scalarA,
                        scalarB,
                        node.dataMathMode,
                        node.dataMathSettings,
                        isScalarRenderSocket(node.nodeId, socketId),
                        fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

            const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
            const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
            const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
            const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

            result = createTarget();
            renderToTexture(result, [&](unsigned int fbo) {
                RenderChannelCombine(texR, texG, texB, texA,
                                     linkR != nullptr, linkG != nullptr, linkB != nullptr, linkA != nullptr,
                                     fbo);
            });
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                result = evalImage(input->fromNodeId, input->fromSocketId);
            } else {
                const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
                const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
                const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
                const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

                const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
                const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
                const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
                const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderChannelCombine(texR, texG, texB, texA,
                                         linkR != nullptr, linkG != nullptr, linkB != nullptr, linkA != nullptr,
                                         fbo);
                });
                resultOwned = result != 0;
            }
        }

        if (result) {
            imageCache[key] = result;
            storeCacheEntry(m_GraphImageCache, key, result, fingerprint, resultOwned);
        } else {
            releaseCacheEntry(m_GraphImageCache, key);
        }
        visitingImages.erase(key);
        return result;
    };

    unsigned int finalTexture = 0;
    const auto outputIt = nodes.find(graph.outputNodeId);
    if (outputIt != nodes.end() &&
        (outputIt->second->kind == RenderGraphNodeKind::MaskGenerator ||
         outputIt->second->kind == RenderGraphNodeKind::MaskUtility ||
         outputIt->second->kind == RenderGraphNodeKind::ImageToMask ||
         outputIt->second->kind == RenderGraphNodeKind::MaskCombine ||
         outputIt->second->kind == RenderGraphNodeKind::CustomMask ||
         outputIt->second->kind == RenderGraphNodeKind::ChannelSplit ||
         (outputIt->second->kind == RenderGraphNodeKind::DataMath && isScalarRenderSocket(graph.outputNodeId, graph.outputSocketId)) ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailAutoMask && graph.outputSocketId == "maskOut") ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailFusion && graph.outputSocketId == "maskOut"))) {
        finalTexture = evalMask(graph.outputNodeId, graph.outputSocketId);
    } else {
        finalTexture = evalImage(graph.outputNodeId, graph.outputSocketId);
    }
    m_OutputTexture = finalTexture ? finalTexture : 0;
    m_GraphSourceTexture = 0;
    const int referenceSourceNodeId = findReferenceSourceNode(graph.outputNodeId);
    if (referenceSourceNodeId > 0) {
        const auto referenceIt = nodes.find(referenceSourceNodeId);
        if (referenceIt != nodes.end() &&
            referenceIt->second &&
            referenceIt->second->kind == RenderGraphNodeKind::RawSource) {
            m_GraphSourceTexture = m_SourceTexture;
        } else {
            m_GraphSourceTexture = evalImage(referenceSourceNodeId, "imageOut");
        }
    }

    for (auto it = m_GraphImageCache.begin(); it != m_GraphImageCache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            if (it->second.owned && it->second.texture != 0 && it->second.texture != m_SourceTexture && it->second.texture != m_ExternalOutputTexture) {
                glDeleteTextures(1, &it->second.texture);
            }
            it = m_GraphImageCache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_GraphMaskCache.begin(); it != m_GraphMaskCache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            if (it->second.owned && it->second.texture != 0 && it->second.texture != m_SourceTexture && it->second.texture != m_ExternalOutputTexture) {
                glDeleteTextures(1, &it->second.texture);
            }
            it = m_GraphMaskCache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_LutTextureCache.begin(); it != m_LutTextureCache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            if (it->second.owned && it->second.texture != 0 && it->second.texture != m_SourceTexture && it->second.texture != m_ExternalOutputTexture) {
                glDeleteTextures(1, &it->second.texture);
            }
            it = m_LutTextureCache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_RawDevelopStageImageCache.begin(); it != m_RawDevelopStageImageCache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            for (CachedGraphTexture& entry : it->second) {
                if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
                    glDeleteTextures(1, &entry.texture);
                }
            }
            it = m_RawDevelopStageImageCache.erase(it);
        } else {
            ++it;
        }
    }

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}
