#include "Raw/MultiFrame/GraphProcessor.h"

#include "Persistence/RawProjectModel.h"
#include "Raw/MultiFrameDenoise/Processor.h"
#include "Raw/MultiFrameHdr/Processor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Raw::MultiFrame {
namespace {

std::filesystem::path GraphNodeWorkingDirectory(
    const std::filesystem::path& root,
    const std::string& contentIdentity) {
    // Processor caches add another full SHA-256 directory below this point.
    // A compact 128-bit node segment avoids MAX_PATH failures without using a
    // truncated identity as an authoritative result/cache validity check.
    return root / ("n-" + contentIdentity.substr(
        0u, std::min<std::size_t>(32u, contentIdentity.size())));
}

std::uint64_t IdentityHash64(const std::string& identity) {
    if (identity.size() < 16u) return std::hash<std::string> {}(identity);
    try {
        return std::stoull(identity.substr(0u, 16u), nullptr, 16);
    } catch (...) {
        return std::hash<std::string> {}(identity);
    }
}

using Stack::Project::EmbeddedAssetRecord;
using Stack::Project::FindEmbeddedAsset;
using Stack::Project::FindMultiFrameGraphNode;
using Stack::Project::FindSourceSet;
using Stack::Project::MultiFrameGraphNode;
using Stack::Project::MultiFrameGraphNodeKind;
using Stack::Project::MultiFrameSourceSet;
using Stack::Project::RawCaptureCompatibilitySummary;
using Stack::Project::SourceSetFrame;

struct ValueItem {
    std::string stableId;
    std::filesystem::path sourcePath;
    std::string sourceSha256;
    std::uint64_t sourceByteLength = 0u;
    PixelExtent extent;
    std::shared_ptr<RawImageData> raw;
    std::shared_ptr<const RawMeasurementHandle> measurement;
    std::shared_ptr<const Raw::Hdr::Result> hdrResult;
    std::filesystem::path hdrCacheDirectory;
    std::vector<std::string> originalFrameIds;
};

using NodeValue = std::vector<ValueItem>;

bool Canceled(const GraphProcessingRequest& request) {
    return request.shouldCancel && request.shouldCancel();
}

void Report(
    const GraphProcessingRequest& request,
    const GraphExecutionPlan& plan,
    const GraphExecutionStep& step,
    std::size_t stepIndex,
    double nodeFraction,
    std::string message) {
    if (!request.reportProgress) return;
    const auto stepWeight = [](GraphExecutionAdapter adapter) {
        switch (adapter) {
            case GraphExecutionAdapter::SharedBurstV1:
            case GraphExecutionAdapter::HdrV4:
                return 1.0;
            case GraphExecutionAdapter::CaptureSource:
            case GraphExecutionAdapter::CaptureSubset:
                return 0.005;
            case GraphExecutionAdapter::PublishOutput:
                return 0.01;
        }
        return 0.01;
    };
    double totalWeight = 0.0;
    double completedWeight = 0.0;
    for (std::size_t index = 0u; index < plan.steps.size(); ++index) {
        const double weight = stepWeight(plan.steps[index].adapter);
        totalWeight += weight;
        if (index < stepIndex) completedWeight += weight;
    }
    const double currentWeight = stepWeight(step.adapter);
    GraphProcessingProgress progress;
    progress.nodeId = step.nodeId;
    progress.adapter = step.adapter;
    progress.completedNodeCount = static_cast<std::uint32_t>(stepIndex);
    progress.totalNodeCount = static_cast<std::uint32_t>(plan.steps.size());
    progress.nodeFraction = std::clamp(nodeFraction, 0.0, 1.0);
    progress.overallFraction = totalWeight <= 0.0
        ? 1.0
        : std::clamp(
            (completedWeight + currentWeight * progress.nodeFraction) /
                totalWeight,
            0.0, 1.0);
    progress.message = std::move(message);
    request.reportProgress(progress);
}

bool FindFrame(
    const Stack::Project::RawProjectSnapshot& snapshot,
    const std::string& sourceSetId,
    const std::string& frameId,
    const SourceSetFrame*& frame,
    const EmbeddedAssetRecord*& asset,
    RawCaptureCompatibilitySummary& summary) {
    frame = nullptr;
    asset = nullptr;
    const MultiFrameSourceSet* sourceSet = FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return false;
    const auto found = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) {
            return candidate.frameId == frameId;
        });
    if (found == sourceSet->frames.end()) return false;
    frame = &*found;
    asset = FindEmbeddedAsset(snapshot, frame->assetId);
    return asset && Stack::Project::DeserializeRawCaptureCompatibilitySummary(
        asset->captureMetadataSummary, summary, nullptr);
}

std::shared_ptr<RawImageData::MultiFrameMeasurementSidecars> MakeSidecars(
    const std::shared_ptr<const std::vector<float>>& variance,
    const std::shared_ptr<const std::vector<float>>& support,
    const std::shared_ptr<const std::vector<std::uint8_t>>& validity,
    const std::shared_ptr<const std::vector<std::uint8_t>>& clipping,
    const GraphExecutionStep& step) {
    auto sidecars =
        std::make_shared<RawImageData::MultiFrameMeasurementSidecars>();
    sidecars->variance = variance;
    sidecars->effectiveSupport = support;
    sidecars->validity = validity;
    sidecars->clipping = clipping;
    sidecars->originalFrameIds = step.originalFrameIds;
    sidecars->evidenceIdentitySha256 = step.contentIdentitySha256;
    return sidecars;
}

std::shared_ptr<RawMeasurementHandle> MakeMeasurement(
    const GraphExecutionStep& step,
    const MultiFrameGraphNode& node,
    const std::shared_ptr<RawImageData>& raw,
    const std::string& algorithmId,
    std::uint32_t algorithmVersion) {
    if (!raw || !raw->normalizedMosaicBuffer) return {};
    auto measurement = std::make_shared<RawMeasurementHandle>();
    measurement->contentHash = step.contentIdentitySha256;
    measurement->producerNodeId = step.nodeId;
    measurement->producerAlgorithmId = algorithmId;
    measurement->producerAlgorithmVersion = algorithmVersion;
    measurement->domain = MeasurementDomain::VirtualCfa;
    const PixelExtent extent {
        static_cast<std::uint64_t>(std::max(0, raw->metadata.visibleWidth)),
        static_cast<std::uint64_t>(std::max(0, raw->metadata.visibleHeight))
    };
    const RawSensorRect active { 0, 0,
        static_cast<int>(extent.height), static_cast<int>(extent.width) };
    if (!TryCreateCfaDomainIdentity(
            raw->metadata.cfaPattern, active, extent,
            measurement->cfa, nullptr)) {
        return {};
    }
    measurement->radiometricAnchorId = raw->metadata.sourcePath;
    measurement->geometryId = "identity-cfa-" + std::to_string(static_cast<int>(raw->metadata.cfaPattern)) +
        "-" + std::to_string(extent.width) + "x" + std::to_string(extent.height);
    measurement->planes.extent = extent;
    measurement->planes.mosaic = raw->normalizedMosaicBuffer;
    if (raw->multiFrameMeasurementSidecars) {
        measurement->planes.variance =
            raw->multiFrameMeasurementSidecars->variance;
        measurement->planes.effectiveSupport =
            raw->multiFrameMeasurementSidecars->effectiveSupport;
        measurement->planes.validity =
            raw->multiFrameMeasurementSidecars->validity;
        measurement->planes.clipping =
            raw->multiFrameMeasurementSidecars->clipping;
    }
    measurement->evidence.identitySha256 = step.contentIdentitySha256;
    const double coefficient = step.originalFrameIds.empty()
        ? 0.0 : 1.0 / static_cast<double>(step.originalFrameIds.size());
    for (const std::string& frameId : step.originalFrameIds) {
        measurement->evidence.contributions.push_back(
            { frameId, coefficient, {} });
    }
    (void)node;
    return measurement;
}

void NormalizeVirtualMetadata(
    RawImageData& raw,
    const GraphExecutionStep& step,
    CfaPattern pattern,
    PixelExtent extent,
    NormalizedMosaicInputContract contract) {
    raw.metadata.sourcePath = "multiframe://" + step.nodeId;
    raw.metadata.sourceContentSha256 = step.contentIdentitySha256;
    raw.metadata.sourceByteSize = extent.width * extent.height * sizeof(float);
    raw.metadata.rawWidth = static_cast<int>(extent.width);
    raw.metadata.rawHeight = static_cast<int>(extent.height);
    raw.metadata.visibleWidth = raw.metadata.rawWidth;
    raw.metadata.visibleHeight = raw.metadata.rawHeight;
    raw.metadata.leftMargin = 0;
    raw.metadata.topMargin = 0;
    raw.metadata.pixelLayout = RawPixelLayout::MosaicBayer;
    raw.metadata.mosaiced = true;
    raw.metadata.cfaPattern = pattern;
    raw.metadata.uploadFormat = "R32F";
    raw.metadata.dngActiveArea = {
        0, 0, raw.metadata.rawHeight, raw.metadata.rawWidth };
    raw.metadata.hasDngActiveArea = true;
    raw.metadata.dngMaskedAreas.clear();
    raw.metadata.hasDngBaselineExposure = false;
    raw.metadata.dngBaselineExposure = 0.0f;
    raw.normalizedMosaicInputContract = contract;
}

std::vector<ValueItem> FlattenInputs(
    const GraphExecutionStep& step,
    const std::unordered_map<std::string, NodeValue>& values) {
    std::vector<ValueItem> inputs;
    for (const GraphExecutionInput& input : step.inputs) {
        const auto found = values.find(input.producerNodeId);
        if (found == values.end()) continue;
        inputs.insert(inputs.end(), found->second.begin(), found->second.end());
    }
    return inputs;
}

Raw::Mfd::MfdProcessingServices MakeMfdServices(
    const std::unordered_map<std::string, std::shared_ptr<RawImageData>>& virtualRaw,
    const GraphProcessingRequest& request) {
    auto filesystem = Raw::Mfd::MakeFilesystemMfdProcessingServices();
    Raw::Mfd::MfdProcessingServices services;
    services.executeOpenGlTask = request.executeOpenGlTask;
    services.loadRawFrame = [virtualRaw, filesystem](
        const std::filesystem::path& path,
        RawImageData& raw,
        const std::function<bool()>& shouldCancel,
        std::string& error) {
        const auto found = virtualRaw.find(path.generic_string());
        if (found != virtualRaw.end() && found->second) {
            raw = *found->second;
            error.clear();
            return true;
        }
        return filesystem.loadRawFrame(path, raw, shouldCancel, error);
    };
    if (request.loadRawFrame) {
        services.loadRawFrame = [virtualRaw, injected = request.loadRawFrame](
            const std::filesystem::path& path,
            RawImageData& raw,
            const std::function<bool()>& shouldCancel,
            std::string& error) {
            const auto found = virtualRaw.find(path.generic_string());
            if (found != virtualRaw.end() && found->second) {
                raw = *found->second;
                error.clear();
                return true;
            }
            return injected(path, raw, shouldCancel, error);
        };
    }
    return services;
}

Raw::Hdr::Services MakeHdrServices(
    const std::unordered_map<std::string, std::shared_ptr<RawImageData>>& virtualRaw,
    const GraphProcessingRequest& request) {
    auto filesystem = Raw::Hdr::MakeFilesystemServices();
    Raw::Hdr::Services services;
    services.executeOpenGlTask = request.executeOpenGlTask;
    services.loadRawFrame = [virtualRaw, filesystem](
        const std::filesystem::path& path,
        RawImageData& raw,
        const std::function<bool()>& shouldCancel,
        std::string& error) {
        const auto found = virtualRaw.find(path.generic_string());
        if (found != virtualRaw.end() && found->second) {
            raw = *found->second;
            error.clear();
            return true;
        }
        return filesystem.loadRawFrame(path, raw, shouldCancel, error);
    };
    if (request.loadRawFrame) {
        services.loadRawFrame = [virtualRaw, injected = request.loadRawFrame](
            const std::filesystem::path& path,
            RawImageData& raw,
            const std::function<bool()>& shouldCancel,
            std::string& error) {
            const auto found = virtualRaw.find(path.generic_string());
            if (found != virtualRaw.end() && found->second) {
                raw = *found->second;
                error.clear();
                return true;
            }
            return injected(path, raw, shouldCancel, error);
        };
    }
    return services;
}

} // namespace

class GraphProcessingCache {
public:
    std::unordered_map<std::string, std::pair<std::string, NodeValue>> nodes;
    std::unordered_map<std::string, std::shared_ptr<const Raw::Hdr::PreparedFusion>> hdrPreparation;
};

GraphProcessingResult ProcessMultiFrameGraph(
    const GraphProcessingRequest& request) {
    GraphProcessingResult result;
    result.plan = BuildMultiFrameGraphExecutionPlan(request.snapshot);
    if (!result.plan.valid) {
        result.message = result.plan.errors.empty()
            ? "The MultiFrame graph is invalid."
            : result.plan.errors.front();
        return result;
    }
    if (!result.plan.executableWithCurrentAdapters) {
        result.message = result.plan.warnings.empty()
            ? "This graph requires covariance-aware fusion that is not available."
            : result.plan.warnings.back();
        return result;
    }
    if (request.workingDirectory.empty() || request.memoryBudgetBytes == 0u ||
        request.workerCount == 0u) {
        result.message = "The MultiFrame graph processor request is incomplete.";
        return result;
    }

    auto cache = std::make_shared<GraphProcessingCache>();
    result.cache = cache;
    std::unordered_map<std::string, NodeValue> values;
    std::unordered_map<std::string, std::shared_ptr<RawImageData>> virtualRaw;
    for (std::size_t stepIndex = 0u;
         stepIndex < result.plan.steps.size(); ++stepIndex) {
        const GraphExecutionStep& step = result.plan.steps[stepIndex];
        const MultiFrameGraphNode* node = FindMultiFrameGraphNode(
            request.snapshot.multiFrameGraph, step.nodeId);
        if (!node) {
            result.message = "A scheduled MultiFrame node disappeared.";
            return result;
        }
        if (Canceled(request)) {
            result.status = GraphProcessingStatus::Canceled;
            result.message = "MultiFrame graph processing was canceled.";
            return result;
        }
        Report(request, result.plan, step, stepIndex, 0.0,
            "Starting " + node->title + ".");
        NodeValue output;
        if (request.cache && step.adapter == GraphExecutionAdapter::SharedBurstV1) {
            const auto hit = request.cache->nodes.find(step.nodeId);
            if (hit != request.cache->nodes.end() && hit->second.first == step.contentIdentitySha256) {
                values[step.nodeId] = hit->second.second;
                cache->nodes[step.nodeId] = hit->second;
                result.processedNodes.push_back({step.nodeId, step.adapter,
                    step.contentIdentitySha256, step.originalFrameIds});
                Report(request, result.plan, step, stepIndex, 1, "Reusing Burst Denoise measurement");
                continue;
            }
        }

        if (step.adapter == GraphExecutionAdapter::CaptureSource ||
            step.adapter == GraphExecutionAdapter::CaptureSubset) {
            for (const std::string& frameId : node->frameIds) {
                const SourceSetFrame* frame = nullptr;
                const EmbeddedAssetRecord* asset = nullptr;
                RawCaptureCompatibilitySummary summary;
                if (!FindFrame(
                        request.snapshot, node->sourceSetId, frameId,
                        frame, asset, summary)) {
                    result.message = "A Capture node references unavailable RAW evidence.";
                    return result;
                }
                if (!frame->enabled) continue;
                const auto path = request.materializedSourcePathsByFrameId.find(frameId);
                if (path == request.materializedSourcePathsByFrameId.end() ||
                    path->second.empty()) {
                    result.message = "A reachable original has not been materialized.";
                    return result;
                }
                ValueItem item;
                item.stableId = frameId;
                item.sourcePath = path->second;
                item.sourceSha256 = asset->sha256;
                item.sourceByteLength = asset->byteLength;
                item.extent = {
                    static_cast<std::uint64_t>(std::max(0, summary.visibleWidth)),
                    static_cast<std::uint64_t>(std::max(0, summary.visibleHeight))
                };
                item.originalFrameIds = { frameId };
                output.push_back(std::move(item));
            }
        } else if (step.adapter == GraphExecutionAdapter::SharedBurstV1) {
            auto inputs = FlattenInputs(step, values);
            if (inputs.size() == 1u && result.plan.outputProducerNodeId != step.nodeId) {
                // A partially excluded group still represents one physical
                // measurement. Keep its group ID stable for manual HDR bias.
                inputs.front().stableId = step.nodeId;
                values[step.nodeId] = inputs;
                cache->nodes[step.nodeId] = {step.contentIdentitySha256, inputs};
                result.processedNodes.push_back({step.nodeId, step.adapter, step.contentIdentitySha256, step.originalFrameIds});
                Report(request, result.plan, step, stepIndex, 1, "One included capture; passing original evidence to HDR Fusion");
                continue;
            }
            if (inputs.size() < 2u) {
                result.message = "Burst Denoise requires at least two input measurements.";
                return result;
            }
            Raw::Mfd::MfdProcessingRequest processor;
            processor.fusionBackend = Raw::Mfd::MfdFusionBackend::SharedBurstV1;
            processor.workingDirectory = GraphNodeWorkingDirectory(
                request.workingDirectory, step.contentIdentitySha256);
            processor.memoryBudgetBytes = request.memoryBudgetBytes;
            processor.workerCount = request.workerCount;
            processor.enforceMemoryBudget = request.enforceMemoryBudget;
            processor.shouldCancel = request.shouldCancel;
            const MultiFrameSourceSet* activeSet = FindSourceSet(
                request.snapshot, request.snapshot.activeSourceSetId);
            const nlohmann::json* parameters = node->settings.contains("parameters")
                ? &node->settings["parameters"]
                : (activeSet && activeSet->settings.contains("parameters")
                    ? &activeSet->settings["parameters"] : nullptr);
            if (parameters) {
                if (!Raw::Mfd::DeserializeParameters(*parameters, processor.parameters, &result.message)) return result;
            }
            const nlohmann::json* sharedSettings =
                node->settings.contains("sharedBurstSettings")
                ? &node->settings["sharedBurstSettings"]
                : (activeSet &&
                   activeSet->settings.contains("sharedBurstSettings")
                    ? &activeSet->settings["sharedBurstSettings"] : nullptr);
            if (sharedSettings) {
                if (!Raw::Mfd::DeserializeSharedBurstSettings(*sharedSettings, processor.sharedBurstSettings, &result.message)) return result;
            }
            if (activeSet) {
                Raw::Mfd::ParseMfdAlignmentMode(
                    activeSet->settings.value(
                        "experimentalAlignmentMode", std::string("full")),
                    processor.alignmentMode);
            }
            if (node->settings.contains("alignmentMode") && !Raw::Mfd::ParseMfdAlignmentMode(
                    node->settings["alignmentMode"].get<std::string>(), processor.alignmentMode)) {
                result.message = "Invalid Burst Denoise alignment mode."; return result;
            }
            processor.reportProgress = [&](const Raw::Mfd::MfdProcessingProgress& p) {
                Report(request, result.plan, step, stepIndex,
                    p.overallFraction, p.message);
            };
            for (std::size_t index = 0u; index < inputs.size(); ++index) {
                Raw::Mfd::MfdProcessingFrameInput frame;
                frame.stableFrameId = inputs[index].stableId;
                frame.sourcePath = inputs[index].sourcePath;
                frame.expectedSourceSha256 = inputs[index].sourceSha256;
                frame.expectedSourceByteLength = inputs[index].sourceByteLength;
                frame.expectedVisibleExtent = {
                    inputs[index].extent.width,
                    inputs[index].extent.height
                };
                processor.frames.push_back(std::move(frame));
                if (inputs[index].raw)
                    virtualRaw[inputs[index].sourcePath.generic_string()] = inputs[index].raw;
            }
            const auto burstAnchor = node->settings.value("geometryAnchor", std::string());
            for (std::size_t i = 0; i < inputs.size(); ++i)
                if (inputs[i].stableId == burstAnchor) processor.referenceFrameIndex = i;
            Raw::Mfd::MfdProcessingResult processed = Raw::Mfd::ProcessMfdBurst(
                processor, MakeMfdServices(virtualRaw, request));
            result.executionBackend = processed.diagnostics.executionBackend;
            result.gpuFallbackReason = processed.diagnostics.gpuFallbackReason;
            result.gpuDispatchedTileCount =
                processed.diagnostics.gpuDispatchedTileCount;
            result.registrationBackend =
                processed.diagnostics.registrationBackend;
            result.registrationGpuFallbackReason =
                processed.diagnostics.registrationGpuFallbackReason;
            result.registrationGpuDispatchCount +=
                processed.diagnostics.registrationGpuDispatchCount;
            result.registrationGpuScoredCandidateCount +=
                processed.diagnostics.registrationGpuScoredCandidateCount;
            if (processed.status == Raw::Mfd::MfdProcessingStatus::Canceled) {
                result.status = GraphProcessingStatus::Canceled;
                result.message = processed.message;
                return result;
            }
            if ((processed.status != Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
                 processed.status != Raw::Mfd::MfdProcessingStatus::ReferenceOnly) ||
                !processed.published.result) {
                result.message = processed.message.empty()
                    ? "Burst Denoise did not publish a virtual Bayer result."
                    : processed.message;
                return result;
            }
            auto raw = std::make_shared<RawImageData>();
            raw->metadata = processed.referenceMetadata;
            const PixelExtent extent {
                processed.published.result->extent.width,
                processed.published.result->extent.height
            };
            NormalizeVirtualMetadata(
                *raw, step, processed.outputCfaPattern, extent,
                NormalizedMosaicInputContract::MfdReferencePreGain);
            // Keep authored evidence semantic while binding downstream RAW
            // preparation caches to the actual CPU/GPU numeric producer.
            raw->metadata.sourceContentSha256 =
                processed.published.result->fusedResultCacheKey;
            raw->normalizedMosaicBuffer =
                std::make_shared<const std::vector<float>>(
                    processed.published.result->normalizedMosaic);
            auto variance = std::make_shared<const std::vector<float>>(
                std::move(processed.varianceProxy));
            auto support = std::make_shared<const std::vector<float>>(
                std::move(processed.effectiveSupport));
            auto validity = std::make_shared<const std::vector<std::uint8_t>>(
                std::move(processed.validityMask));
            auto clipping = std::make_shared<const std::vector<std::uint8_t>>(
                std::move(processed.clippingMask));
            raw->multiFrameMeasurementSidecars = MakeSidecars(
                variance, support, validity, clipping, step);
            raw->normalizedMosaicContentHash = IdentityHash64(
                processed.published.result->fusedResultCacheKey);
            auto measurement = MakeMeasurement(
                step, *node, raw,
                Raw::Mfd::kSharedBurstAlgorithmVersionId,
                Raw::Mfd::kSharedBurstAlgorithmVersion);
            if (measurement && processor.alignmentMode != Raw::Mfd::MfdAlignmentMode::Identity) {
                const auto& anchorInput = inputs[std::min<std::size_t>(processor.referenceFrameIndex, inputs.size() - 1)];
                measurement->geometryId = anchorInput.measurement ? anchorInput.measurement->geometryId
                    : "capture-geometry:" + anchorInput.stableId;
            }
            if (!measurement ||
                !ValidateRawMeasurementHandle(*measurement, &result.message)) {
                return result;
            }
            ValueItem item;
            item.stableId = step.nodeId;
            item.sourcePath = std::filesystem::path(
                "multiframe-virtual") / (step.contentIdentitySha256 + ".cfa");
            item.sourceSha256 =
                processed.published.result->fusedResultCacheKey;
            item.sourceByteLength = raw->metadata.sourceByteSize;
            item.extent = extent;
            item.raw = raw;
            item.measurement = measurement;
            item.originalFrameIds = step.originalFrameIds;
            virtualRaw[item.sourcePath.generic_string()] = raw;
            output.push_back(std::move(item));
        } else if (step.adapter == GraphExecutionAdapter::HdrV4) {
            auto inputs = FlattenInputs(step, values);
            if (inputs.size() < 2u) {
                result.message = "HDR Merge requires at least two input measurements.";
                return result;
            }
            Raw::Hdr::Request processor;
            processor.interactiveFusion = true;
            if (!Raw::Hdr::DeserializeFusionControls(node->settings.value("fusion", nlohmann::json::object()),
                    processor.fusion, &result.message)) return result;
            if (request.cache) {
                const auto cached = request.cache->hdrPreparation.find(step.nodeId);
                if (cached != request.cache->hdrPreparation.end()) processor.preparedFusion = cached->second;
            }
            processor.workingDirectory = GraphNodeWorkingDirectory(
                request.workingDirectory, step.contentIdentitySha256);
            processor.memoryBudgetBytes = request.memoryBudgetBytes;
            processor.workerCount = request.workerCount;
            processor.enforceMemoryBudget = request.enforceMemoryBudget;
            processor.inputRevision = request.snapshot.hdrInputRevision;
            processor.shouldCancel = request.shouldCancel;
            const MultiFrameSourceSet* activeSet = FindSourceSet(
                request.snapshot, request.snapshot.activeSourceSetId);
            const nlohmann::json* parameters = node->settings.contains("parameters")
                ? &node->settings["parameters"]
                : (activeSet && activeSet->settings.contains("parameters")
                    ? &activeSet->settings["parameters"] : nullptr);
            if (parameters) {
                if (!Raw::Hdr::DeserializeParameters(*parameters, processor.parameters, &result.message)) return result;
            }
            processor.reportProgress = [&](const Raw::Hdr::Progress& p) {
                Report(request, result.plan, step, stepIndex,
                    p.overallFraction, p.message);
            };
            for (const ValueItem& input : inputs) {
                Raw::Hdr::FrameInput frame;
                frame.stableFrameId = input.stableId;
                frame.sourcePath = input.sourcePath;
                frame.expectedSourceSha256 = input.sourceSha256;
                frame.expectedSourceByteLength = input.sourceByteLength;
                processor.frames.push_back(std::move(frame));
                if (input.raw)
                    virtualRaw[input.sourcePath.generic_string()] = input.raw;
            }
            const std::string referenceId = node->settings.value("geometryAnchor", std::string());
            if (processor.parameters.alignmentMode != Raw::Hdr::AlignmentMode::AutoTranslation) {
                RawMeasurementSetHandle measurements;
                for (const auto& input : inputs) if (input.measurement) measurements.measurements.push_back(input.measurement);
                if (!ValidateFusionMeasurementCompatibility(measurements, &result.message)) return result;
            }
            const std::string anchorId = node->settings.value("radiometricAnchor", std::string());
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                if (inputs[i].stableId == referenceId) processor.geometricReferenceFrameIndex = static_cast<std::int64_t>(i);
                if (inputs[i].stableId == anchorId) processor.radiometricAnchorFrameIndex = static_cast<std::int64_t>(i);
            }
            Raw::Hdr::Result processed = Raw::Hdr::ProcessBurst(
                processor, MakeHdrServices(virtualRaw, request));
            result.executionBackend = processed.diagnostics.executionBackend;
            result.gpuFallbackReason = processed.diagnostics.gpuFallbackReason;
            result.gpuDispatchedTileCount =
                processed.diagnostics.gpuDispatchedTileCount;
            if (processed.status == Raw::Hdr::ProcessingStatus::Canceled) {
                result.status = GraphProcessingStatus::Canceled;
                result.message = processed.message;
                return result;
            }
            if (processed.status != Raw::Hdr::ProcessingStatus::Published) {
                result.message = processed.message.empty()
                    ? "HDR Merge did not publish a virtual Bayer result."
                    : processed.message;
                return result;
            }
            auto published = std::make_shared<const Raw::Hdr::Result>(
                std::move(processed));
            if (published->fusionPreview) {
                auto preview = std::const_pointer_cast<Raw::Hdr::FusionPreview>(published->fusionPreview);
                preview->graphInputIdentity = MultiFrameFusionInputIdentity(request.snapshot, node->nodeId);
            }
            cache->hdrPreparation[step.nodeId] = published->preparedFusion;
            result.hdrNodeResults[step.nodeId] = published;
            auto raw = std::make_shared<RawImageData>();
            raw->metadata = published->radiometricAnchorMetadata;
            const PixelExtent extent { published->width, published->height };
            NormalizeVirtualMetadata(
                *raw, step, published->outputCfaPattern, extent,
                NormalizedMosaicInputContract::HdrVirtualAnchorPreGain);
            // The authored graph/evidence identity stays backend-neutral, but
            // prepared-tile caches must follow the actual numeric producer.
            // CPU double and OpenGL FP32 are parity-bounded, not byte-identical.
            raw->metadata.sourceContentSha256 =
                published->diagnostics.cacheKey;
            raw->normalizedMosaicBuffer =
                std::shared_ptr<const std::vector<float>>(
                    published, &published->virtualAnchorMosaic);
            raw->normalizedMosaicContentHash = IdentityHash64(
                published->diagnostics.cacheKey);
            auto variance = std::shared_ptr<const std::vector<float>>(
                published, &published->varianceProxy);
            auto support = std::shared_ptr<const std::vector<float>>(
                published, &published->effectiveSampleCount);
            auto validity = std::shared_ptr<const std::vector<std::uint8_t>>(
                published, &published->validityMask);
            auto clippingValues = std::make_shared<std::vector<std::uint8_t>>(
                raw->normalizedMosaicBuffer->size(), 0u);
            auto equalityValidity = std::make_shared<std::vector<std::uint8_t>>(published->validityMask);
            for (std::size_t i = 0; i < equalityValidity->size(); ++i) {
                if (published->flags[i] & Raw::Hdr::ResultFlagNoValidMeasurement) {
                    (*equalityValidity)[i] = 0;
                    (*clippingValues)[i] = 1;
                }
            }
            raw->multiFrameMeasurementSidecars = MakeSidecars(
                variance, support, equalityValidity, clippingValues, step);
            auto hdrSidecars = std::make_shared<RawImageData::HdrSidecars>();
            hdrSidecars->varianceProxy = variance;
            hdrSidecars->mergeConfidence =
                std::shared_ptr<const std::vector<float>>(
                    published, &published->mergeConfidence);
            hdrSidecars->effectiveSampleCount = support;
            hdrSidecars->recoveredHeadroomStops =
                std::shared_ptr<const std::vector<float>>(
                    published, &published->recoveredHeadroomStops);
            hdrSidecars->validityMask = validity;
            hdrSidecars->ownerFrame =
                std::shared_ptr<const std::vector<std::uint8_t>>(
                    published, &published->ownerFrame);
            hdrSidecars->flags =
                std::shared_ptr<const std::vector<std::uint8_t>>(
                    published, &published->flags);
            hdrSidecars->geometricReferenceFrameId =
                published->geometricReferenceFrameId;
            hdrSidecars->radiometricAnchorFrameId =
                published->radiometricAnchorFrameId;
            raw->hdrSidecars = std::move(hdrSidecars);
            auto measurement = MakeMeasurement(
                step, *node, raw, Raw::Hdr::kProcessorContractId,
                Raw::Hdr::kProcessorContractVersion);
            if (measurement && processor.parameters.alignmentMode == Raw::Hdr::AlignmentMode::AutoTranslation) {
                for (const auto& input : inputs) if (input.stableId == published->geometricReferenceFrameId)
                    measurement->geometryId = input.measurement ? input.measurement->geometryId
                        : "capture-geometry:" + input.stableId;
            }
            if (!measurement ||
                !ValidateRawMeasurementHandle(*measurement, &result.message)) {
                return result;
            }
            ValueItem item;
            item.stableId = step.nodeId;
            item.sourcePath = std::filesystem::path(
                "multiframe-virtual") / (step.contentIdentitySha256 + ".cfa");
            item.sourceSha256 = published->diagnostics.cacheKey;
            item.sourceByteLength = raw->metadata.sourceByteSize;
            item.extent = extent;
            item.raw = raw;
            item.measurement = measurement;
            item.hdrResult = std::move(published);
            item.hdrCacheDirectory = processor.workingDirectory / "results";
            item.originalFrameIds = step.originalFrameIds;
            virtualRaw[item.sourcePath.generic_string()] = raw;
            output.push_back(std::move(item));
        } else if (step.adapter == GraphExecutionAdapter::PublishOutput) {
            auto inputs = FlattenInputs(step, values);
            if (inputs.size() != 1u) {
                result.message =
                    "Output requires exactly one virtual Bayer measurement.";
                return result;
            }
            ValueItem item = std::move(inputs.front());
            if (!item.raw) {
                RawImageData decoded;
                std::string error;
                const auto filesystem = Raw::Mfd::
                    MakeFilesystemMfdProcessingServices();
                const auto& loader = request.loadRawFrame
                    ? request.loadRawFrame : filesystem.loadRawFrame;
                if (!loader(item.sourcePath, decoded,
                        request.shouldCancel, error)) {
                    result.message = error.empty()
                        ? "The Output source could not be decoded."
                        : error;
                    return result;
                }
                item.raw = std::make_shared<RawImageData>(std::move(decoded));
            }
            result.outputRawData = item.raw;
            result.outputMeasurement = item.measurement;
            result.outputHdrResult = item.hdrResult;
            result.outputHdrCacheDirectory = item.hdrCacheDirectory;
            output.push_back(std::move(item));
        }

        if (step.adapter == GraphExecutionAdapter::SharedBurstV1)
            cache->nodes[step.nodeId] = {step.contentIdentitySha256, output};
        values[step.nodeId] = std::move(output);
        result.processedNodes.push_back({
            step.nodeId, step.adapter, step.contentIdentitySha256,
            step.originalFrameIds });
        Report(request, result.plan, step, stepIndex, 1.0,
            node->title + " complete.");
    }
    if (!result.outputRawData) {
        result.message = "The MultiFrame graph completed without an Output value.";
        return result;
    }
    result.status = GraphProcessingStatus::Completed;
    result.message = "The connected MultiFrame Output was published atomically.";
    return result;
}

} // namespace Raw::MultiFrame
