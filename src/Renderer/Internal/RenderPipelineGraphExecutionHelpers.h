#pragma once

#include "Renderer/GLHelpers.h"
#include "Renderer/MaskRenderTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Stack::Renderer::GraphExecution {

inline constexpr std::size_t kFnvOffsetBasis = 1469598103934665603ull;
inline constexpr std::size_t kFnvPrime = 1099511628211ull;
inline constexpr std::size_t kMaxRawDevelopStageCacheEntriesPerKey = 8;
inline constexpr std::uint64_t kRawDevelopStageCacheBytesPerPixel = 8;
inline constexpr std::uint64_t kRawDevelopStageCacheSoftByteBudget = 512ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kRawDevelopStageCacheMediumEntryBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kRawDevelopStageCacheLargeEntryBytes = 128ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kRawDevelopStageCacheHugeEntryBytes = 256ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kRawDevelopStageCacheSingleEntryByteLimit = 384ull * 1024ull * 1024ull;

inline void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

std::size_t HashBytes(const unsigned char* data, std::size_t size);
std::size_t HashJson(const nlohmann::json& value);

std::uint64_t EstimateRawDevelopStageCacheTextureBytes(int width, int height);
std::size_t ResolveRawDevelopStageCacheMaxEntries(int width, int height);

float SafeLog2(float value);
float PercentileFromSorted(const std::vector<float>& sorted, float percentile);
float MedianFloat(std::vector<float> values);

struct ScopedFramebufferState {
    GLint framebuffer = 0;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint readBuffer = 0;
    GLint drawBuffer = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };

    explicit ScopedFramebufferState(bool captureViewport = false);
    void Restore(bool restoreViewport = false) const;
};

struct QuickTextureStats {
    bool valid = false;
    float maxRgb = 0.0f;
    float maxLuma = 0.0f;
    float p99Luma = 0.0f;
};

QuickTextureStats ProbeTextureStats(unsigned int texture, int width, int height);
bool IsDefaultToneCurvePayload(const nlohmann::json& layerJson);

bool HasHdrMergeCaptureExposure(const Raw::RawMetadata& metadata);
float ComputeHdrMergeCaptureExposureEv(const Raw::RawMetadata& metadata);
float SelectRepresentativeExposureAnchor(
    const std::array<float, 3>& absoluteExposureEv,
    const std::array<bool, 3>& activeInputs);
float EstimateHdrMergeIsoMultiplier(const Raw::RawMetadata& metadata);
float EstimateHdrMergeRangeStep(const Raw::RawMetadata& metadata);

std::string MakeNodeSocketKey(int nodeId, std::string_view socketId);
int ExtractNodeIdFromCacheKey(const std::string& key);

struct DataMathInputLinkInfo {
    const RenderGraphLink* link = nullptr;
    std::string socketId;
};

struct GraphExecutionContext {
    explicit GraphExecutionContext(const RenderGraphSnapshot& graphSnapshot);

    const RenderGraphLink* FindInputLink(int nodeId, std::string_view socketId) const;
    bool IsActiveNode(int nodeId) const;

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

std::vector<DataMathInputLinkInfo> CollectDataMathAverageInputs(const GraphExecutionContext& executionContext, int nodeId);
const RenderGraphLink* FindFirstDataMathAverageInput(const GraphExecutionContext& executionContext, int nodeId);
bool IsChannelSocketId(std::string_view socketId);
bool IsScalarRenderSocket(const GraphExecutionContext& executionContext, int nodeId, std::string_view socketId);

} // namespace Stack::Renderer::GraphExecution
