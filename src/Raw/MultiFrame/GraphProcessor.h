#pragma once

#include "Raw/MultiFrame/GraphExecution.h"
#include "Raw/MultiFrame/MeasurementHandle.h"
#include "Raw/MultiFrameHdr/Processor.h"
#include "Raw/RawImageData.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Raw::MultiFrame {

class GraphProcessingCache;

enum class GraphProcessingStatus : std::uint8_t {
    Completed = 0,
    Canceled,
    Failed
};

struct GraphProcessingProgress {
    std::string nodeId;
    GraphExecutionAdapter adapter = GraphExecutionAdapter::CaptureSource;
    std::uint32_t completedNodeCount = 0;
    std::uint32_t totalNodeCount = 0;
    double nodeFraction = 0.0;
    double overallFraction = 0.0;
    std::string message;
};

struct GraphProcessingRequest {
    std::shared_ptr<const GraphProcessingCache> cache;
    Stack::Project::RawProjectSnapshot snapshot;
    // Every original frame reachable from Output must have an immutable,
    // size-verified materialized source path.
    std::unordered_map<std::string, std::filesystem::path>
        materializedSourcePathsByFrameId;
    std::filesystem::path workingDirectory;
    std::uint64_t memoryBudgetBytes =
        2ull * 1024ull * 1024ull * 1024ull;
    bool enforceMemoryBudget = true;
    std::uint32_t workerCount = 1u;
    // Optional injected decoder used by validation and specialized hosts.
    // Virtual measurements are still resolved internally before this hook.
    std::function<bool(
        const std::filesystem::path&,
        RawImageData&,
        const std::function<bool()>&,
        std::string&)> loadRawFrame;
    Raw::OpenGlTaskExecutor executeOpenGlTask;
    std::function<bool()> shouldCancel;
    std::function<void(const GraphProcessingProgress&)> reportProgress;
};

struct GraphProcessedNode {
    std::string nodeId;
    GraphExecutionAdapter adapter = GraphExecutionAdapter::CaptureSource;
    std::string contentIdentitySha256;
    std::vector<std::string> originalFrameIds;
};

struct GraphProcessingResult {
    std::shared_ptr<const GraphProcessingCache> cache;
    std::unordered_map<std::string, std::shared_ptr<const Raw::Hdr::Result>> hdrNodeResults;
    GraphProcessingStatus status = GraphProcessingStatus::Failed;
    std::string message;
    std::string executionBackend;
    std::string gpuFallbackReason;
    std::uint32_t gpuDispatchedTileCount = 0u;
    std::string registrationBackend;
    std::string registrationGpuFallbackReason;
    std::uint32_t registrationGpuDispatchCount = 0u;
    std::uint64_t registrationGpuScoredCandidateCount = 0u;
    GraphExecutionPlan plan;
    std::vector<GraphProcessedNode> processedNodes;
    std::shared_ptr<const RawMeasurementHandle> outputMeasurement;
    std::shared_ptr<RawImageData> outputRawData;
    // Retain the complete terminal HDR publication instead of reducing it to
    // only its Bayer buffer. The RAW workspace needs the diagnostics,
    // metadata, sidecars, and cache identity used by the standalone HDR path.
    std::shared_ptr<const Raw::Hdr::Result> outputHdrResult;
    std::filesystem::path outputHdrCacheDirectory;
};

GraphProcessingResult ProcessMultiFrameGraph(
    const GraphProcessingRequest& request);

} // namespace Raw::MultiFrame
