#include "App/Validation/ValidationSuites.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Persistence/ProjectSessionController.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/MultiFrameGraph.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RawWorkspace.h"
#include "Raw/MultiFrame/GraphExecution.h"
#include "Raw/MultiFrameDenoise/Contracts.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Multi-source project validation failed: " << message << '\n';
    }
    return condition;
}

class ScopedValidationDirectory {
public:
    explicit ScopedValidationDirectory(std::filesystem::path path)
        : m_Path(std::move(path)) {}

    ~ScopedValidationDirectory() {
        if (m_Path.filename().string().find("stack-multi-source-validation-") != 0u) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(m_Path, error);
    }

    const std::filesystem::path& Path() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};

bool WriteBytes(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.close();
    return output.good();
}

std::vector<unsigned char> ReadStream(Stack::Project::ProjectAssetStream asset) {
    if (!asset) return {};
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(*asset.stream),
        std::istreambuf_iterator<char>());
}

std::vector<unsigned char> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

Stack::Project::RawProjectSnapshot BuildBootstrap() {
    Stack::Project::RawProjectSnapshot snapshot;
    snapshot.projectId = Stack::Project::GenerateStableUuid();
    snapshot.projectName = "Validation Multi-Frame Project";
    snapshot.rawWorkspaceData = {
        { "schema", "stack.rawWorkspace.project" },
        { "schemaVersion", Stack::Project::kRawWorkspaceProjectSchemaVersion },
        { "rawProjectModel", Stack::Project::kRawProjectModelSourceSets }
    };
    return snapshot;
}

Stack::Project::RawCaptureCompatibilitySummary BuildMosaicSummary() {
    Stack::Project::RawCaptureCompatibilitySummary summary;
    summary.inputDomain = Stack::Project::MfdInputDomain::MosaicCfa;
    summary.pixelLayout = "bayer";
    summary.cfaPattern = "RGGB";
    summary.cameraMake = "Stack Validation";
    summary.cameraModel = "Mosaic Camera";
    summary.uniqueCameraModel = "stack-validation-mosaic-camera";
    summary.rawWidth = 6120;
    summary.rawHeight = 4080;
    summary.visibleWidth = 6000;
    summary.visibleHeight = 4000;
    summary.leftMargin = 60;
    summary.topMargin = 40;
    summary.bitDepth = 14;
    summary.orientation = 1;
    summary.exposureTimeSeconds = 1.0 / 40.0;
    summary.isoSpeed = 3200.0;
    summary.captureTimestamp = 1770000000;
    summary.supported = true;
    return summary;
}

Stack::Project::RawProjectSnapshot BuildMfdSnapshot() {
    using namespace Stack::Project;
    RawProjectSnapshot snapshot = BuildBootstrap();
    snapshot.projectName = "Validation MFD Project";
    MultiFrameSourceSet sourceSet;
    sourceSet.sourceSetId = GenerateStableUuid();
    sourceSet.name = "Mosaic Burst";
    sourceSet.inputFamily = MultiFrameInputFamily::Raw;
    sourceSet.operationIntent = MultiFrameOperationIntent::RawBurstDenoise;
    sourceSet.operationSchemaVersion = kMfdOperationSchemaVersion;
    sourceSet.settings = MakeDefaultMfdOperationSettings();
    sourceSet.graphBindingNodeId = GenerateStableUuid();

    for (std::size_t index = 0; index < 2u; ++index) {
        const std::string sha(64u, index == 0u ? '1' : '2');
        EmbeddedAssetRecord asset;
        asset.sha256 = sha;
        asset.byteLength = 1000u + static_cast<std::uint64_t>(index);
        asset.assetId = MakeAssetId(asset.sha256, asset.byteLength);
        asset.originalFilename = "mfd-frame-" + std::to_string(index + 1u) + ".dng";
        asset.displayName = asset.originalFilename;
        asset.originalFileFingerprint = asset.sha256;
        asset.inputFamily = MultiFrameInputFamily::Raw;
        RawCaptureCompatibilitySummary summary = BuildMosaicSummary();
        summary.captureTimestamp += static_cast<std::int64_t>(index);
        asset.captureMetadataSummary = SerializeRawCaptureCompatibilitySummary(summary);
        snapshot.embeddedAssets.push_back(asset);

        SourceSetFrame frame;
        frame.frameId = GenerateStableUuid();
        frame.assetId = asset.assetId;
        frame.userLabel = asset.originalFilename;
        sourceSet.frames.push_back(std::move(frame));
    }
    sourceSet.referenceFrameId = sourceSet.frames.front().frameId;
    snapshot.activeSourceSetId = sourceSet.sourceSetId;
    snapshot.activeFrameId = sourceSet.frames.back().frameId;
    snapshot.mfdInputRevision = 7u;
    snapshot.postRecipeRevision = 3u;
    snapshot.sourceSets.push_back(std::move(sourceSet));
    snapshot.multiFrameGraph = BuildOperationMultiFrameGraph(snapshot);
    return snapshot;
}

Stack::Project::RawProjectSnapshot BuildNeutralCaptureSetSnapshot() {
    using namespace Stack::Project;
    RawProjectSnapshot snapshot = BuildMfdSnapshot();
    snapshot.projectName = "Validation Neutral Capture Set";
    MultiFrameSourceSet& sourceSet = snapshot.sourceSets.front();
    sourceSet.name = "Capture Set";
    sourceSet.operationIntent = MultiFrameOperationIntent::RawCaptureSet;
    sourceSet.operationSchemaVersion = kMultiFrameOperationSchemaVersion;
    sourceSet.settings = {
        { "schemaVersion", kMultiFrameOperationSchemaVersion },
        { "inputDomain", "mosaic-cfa" },
        { "processingNode", nullptr }
    };
    snapshot.mfdInputRevision = 0u;
    snapshot.multiFrameGraph = BuildManualMultiFrameGraph(snapshot, sourceSet);
    return snapshot;
}

bool ValidateMultiFrameMeasurementGraph() {
    using namespace Stack::Project;
    RawProjectSnapshot snapshot = BuildNeutralCaptureSetSnapshot();
    bool ok = true;
    const MultiFrameGraphValidationResult initial = ValidateMultiFrameGraph(
        snapshot.multiFrameGraph, snapshot, false);
    ok &= Check(initial.valid,
        initial.errors.empty() ? "the manual RAW-file graph is invalid"
                               : initial.errors.front());
    ok &= Check(std::count_if(
            snapshot.multiFrameGraph.nodes.begin(),
            snapshot.multiFrameGraph.nodes.end(),
            [](const MultiFrameGraphNode& node) {
                return node.kind == MultiFrameGraphNodeKind::CaptureSubset &&
                    node.frameIds.size() == 1u &&
                    node.settings.value("source", std::string()) == "raw-file";
            }) == 2 &&
            snapshot.multiFrameGraph.links.empty() &&
            std::none_of(
                snapshot.multiFrameGraph.nodes.begin(),
                snapshot.multiFrameGraph.nodes.end(),
                [](const MultiFrameGraphNode& node) {
                    return node.kind == MultiFrameGraphNodeKind::BurstDenoise ||
                        node.kind == MultiFrameGraphNodeKind::HdrMerge ||
                        node.kind == MultiFrameGraphNodeKind::CaptureSet;
                }),
        "the default graph did not expose exactly one ungrouped node per RAW file");
    ok &= Check(!ValidateMultiFrameGraph(
            snapshot.multiFrameGraph, snapshot, true).valid,
        "an unconnected manual graph was incorrectly executable");
    const Raw::MultiFrame::GraphExecutionPlan initialPlan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(snapshot);
    ok &= Check(!initialPlan.valid && !initialPlan.executableWithCurrentAdapters,
        "the planner accepted a graph before the user connected Output");

    const auto addLink = [](MultiFrameGraphDocument& graph,
                            const MultiFrameGraphNode& from,
                            const MultiFrameGraphNode& to,
                            std::uint32_t order) {
        MultiFrameGraphLink link;
        link.linkId = GenerateStableUuid();
        link.fromNodeId = from.nodeId;
        link.fromPortId =
            from.kind == MultiFrameGraphNodeKind::CaptureSubset ||
                from.kind == MultiFrameGraphNodeKind::CaptureSet
            ? "measurements"
            : "estimate";
        link.toNodeId = to.nodeId;
        link.toPortId = "measurements";
        link.resourceType =
            from.kind == MultiFrameGraphNodeKind::CaptureSubset ||
                from.kind == MultiFrameGraphNodeKind::CaptureSet
            ? MultiFrameGraphResourceType::RawMeasurementSet
            : MultiFrameGraphResourceType::RawMeasurement;
        link.variadicOrder = order;
        graph.links.push_back(std::move(link));
    };
    const auto connectProcessor = [&](RawProjectSnapshot& project,
                                      MultiFrameGraphNodeKind kind,
                                      const char* title) {
        MultiFrameGraphDocument& graph = project.multiFrameGraph;
        MultiFrameGraphNode processor;
        processor.nodeId = GenerateStableUuid();
        processor.kind = kind;
        processor.title = title;
        processor.positionX = 420.0;
        processor.positionY = 128.0;
        processor.settings = {
            { "evidencePolicy", "disjoint-original-evidence-v1" }
        };
        const std::string processorId = processor.nodeId;
        graph.nodes.push_back(std::move(processor));
        MultiFrameGraphNode* processorNode = FindMultiFrameGraphNode(
            graph, processorId);
        MultiFrameGraphNode* output = FindMultiFrameGraphNode(
            graph, graph.outputNodeId);
        std::vector<std::string> sourceIds;
        for (const MultiFrameGraphNode& node : graph.nodes) {
            if (node.kind == MultiFrameGraphNodeKind::CaptureSubset &&
                node.frameIds.size() == 1u) {
                sourceIds.push_back(node.nodeId);
            }
        }
        for (std::size_t index = 0; processorNode && index < sourceIds.size(); ++index) {
            const MultiFrameGraphNode* source = FindMultiFrameGraphNode(
                graph, sourceIds[index]);
            if (source) addLink(
                graph, *source, *processorNode, static_cast<std::uint32_t>(index));
        }
        processorNode = FindMultiFrameGraphNode(graph, processorId);
        output = FindMultiFrameGraphNode(graph, graph.outputNodeId);
        if (processorNode && output) addLink(graph, *processorNode, *output, 0u);
        graph.userEdited = true;
    };

    connectProcessor(snapshot, MultiFrameGraphNodeKind::BurstDenoise, "Burst Denoise");
    ok &= Check(ValidateMultiFrameGraph(
            snapshot.multiFrameGraph, snapshot, true).valid,
        "a manually wired Burst Denoise tree is invalid");
    const Raw::MultiFrame::GraphExecutionPlan authoredPlan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(snapshot);
    ok &= Check(authoredPlan.valid && authoredPlan.executableWithCurrentAdapters &&
            authoredPlan.steps.size() == 4u &&
            authoredPlan.contentIdentitySha256.size() == 64u,
        "the manually wired RAW-file tree did not produce a stable execution plan");

    MultiFrameGraphDocument graphRoundTrip;
    std::string graphError;
    nlohmann::json graphJson = SerializeMultiFrameGraph(snapshot.multiFrameGraph);
    graphJson["futureGraphField"] = 42;
    graphJson["nodes"].front()["futureNodeField"] = true;
    graphJson["links"].front()["futureLinkField"] = "drop";
    ok &= Check(DeserializeMultiFrameGraph(
            graphJson,
            graphRoundTrip,
            &graphError),
        "measurement graph did not round-trip: " + graphError);
    const nlohmann::json canonicalGraph = SerializeMultiFrameGraph(graphRoundTrip);
    ok &= Check(!canonicalGraph.contains("futureGraphField") &&
            !canonicalGraph["nodes"].front().contains("futureNodeField") &&
            !canonicalGraph["links"].front().contains("futureLinkField"),
        "measurement graph retained fields outside the current schema");

    MultiFrameGraphDocument duplicate = snapshot.multiFrameGraph;
    MultiFrameGraphLink duplicateLink = duplicate.links.front();
    duplicateLink.linkId = GenerateStableUuid();
    duplicateLink.variadicOrder += 10u;
    duplicate.links.push_back(std::move(duplicateLink));
    ok &= Check(!ValidateMultiFrameGraph(duplicate, snapshot, false).valid,
        "a duplicate wire was incorrectly accepted as independent evidence");

    MultiFrameGraphDocument cyclic = snapshot.multiFrameGraph;
    const auto burst = std::find_if(
        cyclic.nodes.begin(), cyclic.nodes.end(), [](const MultiFrameGraphNode& node) {
            return node.kind == MultiFrameGraphNodeKind::BurstDenoise;
        });
    if (burst != cyclic.nodes.end()) {
        MultiFrameGraphLink self;
        self.linkId = GenerateStableUuid();
        self.fromNodeId = burst->nodeId;
        self.fromPortId = "estimate";
        self.toNodeId = burst->nodeId;
        self.toPortId = "measurements";
        self.resourceType = MultiFrameGraphResourceType::RawMeasurement;
        self.variadicOrder = 99u;
        cyclic.links.push_back(std::move(self));
        ok &= Check(!ValidateMultiFrameGraph(cyclic, snapshot, false).valid,
            "a self-cycle was accepted in the MultiFrame graph");
    }

    RawProjectSnapshot bracket = BuildNeutralCaptureSetSnapshot();
    RawCaptureCompatibilitySummary shortExposure;
    RawCaptureCompatibilitySummary longExposure;
    DeserializeRawCaptureCompatibilitySummary(
        bracket.embeddedAssets[0].captureMetadataSummary,
        shortExposure,
        nullptr);
    DeserializeRawCaptureCompatibilitySummary(
        bracket.embeddedAssets[1].captureMetadataSummary,
        longExposure,
        nullptr);
    shortExposure.exposureTimeSeconds = 1.0 / 1000.0;
    longExposure.exposureTimeSeconds = 1.0 / 30.0;
    bracket.embeddedAssets[0].captureMetadataSummary =
        SerializeRawCaptureCompatibilitySummary(shortExposure);
    bracket.embeddedAssets[1].captureMetadataSummary =
        SerializeRawCaptureCompatibilitySummary(longExposure);
    bracket.multiFrameGraph = BuildManualMultiFrameGraph(
        bracket, bracket.sourceSets.front());
    ok &= Check(std::count_if(
            bracket.multiFrameGraph.nodes.begin(),
            bracket.multiFrameGraph.nodes.end(),
            [](const MultiFrameGraphNode& node) {
                return node.kind == MultiFrameGraphNodeKind::HdrMerge;
            }) == 0 &&
            std::count_if(
                bracket.multiFrameGraph.nodes.begin(),
                bracket.multiFrameGraph.nodes.end(),
                [](const MultiFrameGraphNode& node) {
                    return node.kind == MultiFrameGraphNodeKind::CaptureSubset;
                }) == 2 && bracket.multiFrameGraph.links.empty(),
        "exposure metadata incorrectly authored HDR or exposure-group topology");
    connectProcessor(bracket, MultiFrameGraphNodeKind::HdrMerge, "HDR Merge");
    ok &= Check(ValidateMultiFrameGraph(
            bracket.multiFrameGraph, bracket, true).valid,
        "the manually connected HDR graph is invalid");
    const Raw::MultiFrame::GraphExecutionPlan bracketPlan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(bracket);
    ok &= Check(bracketPlan.valid && bracketPlan.executableWithCurrentAdapters &&
            std::count_if(
                bracketPlan.steps.begin(), bracketPlan.steps.end(),
                [](const Raw::MultiFrame::GraphExecutionStep& step) {
                    return step.adapter == Raw::MultiFrame::GraphExecutionAdapter::HdrV4;
                }) == 1,
        "the manually wired file nodes did not plan into HDR");

    RawProjectSnapshot overlap = snapshot;
    const auto firstBurst = std::find_if(
        overlap.multiFrameGraph.nodes.begin(), overlap.multiFrameGraph.nodes.end(),
        [](const MultiFrameGraphNode& node) {
            return node.kind == MultiFrameGraphNodeKind::BurstDenoise;
        });
    const auto capture = std::find_if(
        overlap.multiFrameGraph.nodes.begin(), overlap.multiFrameGraph.nodes.end(),
        [](const MultiFrameGraphNode& node) {
            return node.kind == MultiFrameGraphNodeKind::CaptureSubset &&
                node.frameIds.size() == 1u;
        });
    if (firstBurst != overlap.multiFrameGraph.nodes.end() &&
        capture != overlap.multiFrameGraph.nodes.end()) {
        const std::string firstBurstId = firstBurst->nodeId;
        const std::string captureId = capture->nodeId;
        MultiFrameGraphNode secondBurst;
        secondBurst.nodeId = GenerateStableUuid();
        secondBurst.kind = MultiFrameGraphNodeKind::BurstDenoise;
        secondBurst.title = "Overlapping evidence test";
        overlap.multiFrameGraph.nodes.push_back(secondBurst);
        const std::string secondBurstId = secondBurst.nodeId;
        for (MultiFrameGraphLink& link : overlap.multiFrameGraph.links) {
            if (link.toNodeId == overlap.multiFrameGraph.outputNodeId)
                link.fromNodeId = secondBurstId;
        }
        MultiFrameGraphLink originalInput;
        originalInput.linkId = GenerateStableUuid();
        originalInput.fromNodeId = captureId;
        originalInput.fromPortId = "measurements";
        originalInput.toNodeId = secondBurstId;
        originalInput.toPortId = "measurements";
        originalInput.resourceType = MultiFrameGraphResourceType::RawMeasurementSet;
        originalInput.variadicOrder = 0u;
        overlap.multiFrameGraph.links.push_back(originalInput);
        MultiFrameGraphLink derivedInput;
        derivedInput.linkId = GenerateStableUuid();
        derivedInput.fromNodeId = firstBurstId;
        derivedInput.fromPortId = "estimate";
        derivedInput.toNodeId = secondBurstId;
        derivedInput.toPortId = "measurements";
        derivedInput.resourceType = MultiFrameGraphResourceType::RawMeasurement;
        derivedInput.variadicOrder = 1u;
        overlap.multiFrameGraph.links.push_back(derivedInput);
        const Raw::MultiFrame::GraphExecutionPlan overlapPlan =
            Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(overlap);
        const auto* overlapStep = Raw::MultiFrame::FindGraphExecutionStep(
            overlapPlan, secondBurstId);
        ok &= Check(overlapPlan.valid && !overlapPlan.executableWithCurrentAdapters &&
                overlapStep && overlapStep->requiresCovarianceAwareFusion &&
                overlapStep->overlappingOriginalFrameIds.size() == 1u,
            "derived plus original evidence was not gated as covariance-dependent");
    }

    const RawProjectSnapshot obsoleteSource = BuildMfdSnapshot();
    nlohmann::json obsolete = SerializeRawProjectSnapshot(obsoleteSource);
    obsolete["schemaVersion"] = 5u;
    obsolete.erase("multiFrameGraph");
    const nlohmann::json untouchedObsolete = obsolete;
    RawProjectSnapshot rejectedSnapshot;
    std::string rejectionError;
    ok &= Check(
        !DeserializeRawProjectSnapshot(obsolete, rejectedSnapshot, &rejectionError) &&
            rejectionError.find("obsolete") != std::string::npos &&
            obsolete == untouchedObsolete,
        "an obsolete project schema was migrated or mutated instead of being rejected");
    return ok;
}

nlohmann::json LogicalSnapshotJson(Stack::Project::RawProjectSnapshot snapshot) {
    snapshot.persistedStorageRevision = 0;
    return Stack::Project::SerializeRawProjectSnapshot(snapshot);
}

bool ValidateUnboundedModelAndRules() {
    using namespace Stack::Project;
    RawProjectSnapshot snapshot = BuildBootstrap();
    MultiFrameSourceSet set;
    set.sourceSetId = GenerateStableUuid();
    set.name = "Large ordered draft";
    set.inputFamily = MultiFrameInputFamily::Raw;
    set.operationIntent = MultiFrameOperationIntent::Mfsr;
    set.graphBindingNodeId = GenerateStableUuid();

    constexpr std::size_t kFrameCount = 1024u;
    snapshot.embeddedAssets.reserve(kFrameCount);
    set.frames.reserve(kFrameCount);
    for (std::size_t index = 0; index < kFrameCount; ++index) {
        EmbeddedAssetRecord asset;
        asset.sha256 = std::string(64u, static_cast<char>('a' + (index % 6u)));
        asset.byteLength = index == 0u
            ? (5ull * 1024ull * 1024ull * 1024ull + 17ull)
            : static_cast<std::uint64_t>(index + 1u);
        asset.assetId = MakeAssetId(asset.sha256, asset.byteLength);
        asset.originalFilename = "frame-" + std::to_string(index) + ".dng";
        asset.displayName = asset.originalFilename;
        asset.originalFileFingerprint = asset.sha256;
        asset.inputFamily = MultiFrameInputFamily::Raw;
        snapshot.embeddedAssets.push_back(asset);

        SourceSetFrame frame;
        frame.frameId = GenerateStableUuid();
        frame.assetId = asset.assetId;
        frame.userLabel = asset.originalFilename;
        set.frames.push_back(std::move(frame));
    }
    set.referenceFrameId = set.frames[317u].frameId;
    snapshot.activeSourceSetId = set.sourceSetId;
    snapshot.sourceSets.push_back(set);

    bool ok = true;
    const RawProjectSnapshot neutralCaptureSet =
        BuildNeutralCaptureSetSnapshot();
    ok &= Check(ValidateRawProjectSnapshot(neutralCaptureSet).valid,
        "a neutral RAW capture set was rejected before a processor was chosen");
    ok &= Check(std::string(MultiFrameOperationIntentName(
            MultiFrameOperationIntent::RawCaptureSet)) == "raw-capture-set",
        "the neutral capture-set intent does not have stable serialization");
    RawProjectSnapshot neutralRoundTrip;
    std::string neutralError;
    ok &= Check(DeserializeRawProjectSnapshot(
            SerializeRawProjectSnapshot(neutralCaptureSet),
            neutralRoundTrip,
            &neutralError),
        "a neutral capture set did not round-trip: " + neutralError);
    ok &= Check(!neutralRoundTrip.sourceSets.empty() &&
            neutralRoundTrip.sourceSets.front().operationIntent ==
                MultiFrameOperationIntent::RawCaptureSet,
        "neutral capture-set intent changed during serialization");
    ok &= Check(EvaluateSourceSetStatus(
            neutralCaptureSet,
            neutralCaptureSet.sourceSets.front()) ==
                MultiFrameSetStatus::ReadyForFutureProcessing,
        "a valid neutral capture set was not ready for graph-node selection");
    ok &= Check(ValidateRawProjectSnapshot(snapshot).valid,
        "an arbitrary-length ordered set with a >4 GB 64-bit asset was rejected");
    nlohmann::json serialized = SerializeRawProjectSnapshot(snapshot);
    nlohmann::json unknownFields = serialized;
    unknownFields["futureProjectField"] = true;
    unknownFields["metadata"]["futureMetadataField"] = 17;
    unknownFields["uiState"]["futureUiField"] = "drop";
    RawProjectSnapshot rejectedUnknownFields;
    std::string unknownFieldsError;
    ok &= Check(!DeserializeRawProjectSnapshot(
            unknownFields,
            rejectedUnknownFields,
            &unknownFieldsError),
        "project JSON with fields outside the current schema was accepted");
    RawProjectSnapshot roundTrip;
    std::string error;
    const bool deserialized = DeserializeRawProjectSnapshot(
        serialized,
        roundTrip,
        &error);
    ok &= Check(deserialized,
        "large model JSON did not deserialize: " + error);
    ok &= Check(roundTrip.embeddedAssets.front().byteLength ==
        snapshot.embeddedAssets.front().byteLength,
        "64-bit byte length did not survive serialization");
    ok &= Check(roundTrip.sourceSets.front().frames[317u].frameId ==
        roundTrip.sourceSets.front().referenceFrameId,
        "ordering/reference identity did not survive serialization");
    ok &= Check(SerializeRawProjectSnapshot(roundTrip).dump() == serialized.dump(),
        "current project JSON changed during serialization");

    RawProjectSnapshot duplicate = snapshot;
    duplicate.sourceSets.front().frames.push_back(
        duplicate.sourceSets.front().frames.front());
    duplicate.sourceSets.front().frames.back().frameId = GenerateStableUuid();
    ok &= Check(!ValidateRawProjectSnapshot(duplicate).valid,
        "duplicate asset inclusion within one set was accepted");

    RawProjectSnapshot rasterBurst = BuildBootstrap();
    MultiFrameSourceSet rasterSet;
    rasterSet.sourceSetId = GenerateStableUuid();
    rasterSet.name = "Raster Burst";
    rasterSet.inputFamily = MultiFrameInputFamily::Raster;
    rasterSet.operationIntent = MultiFrameOperationIntent::RawBurstDenoise;
    rasterBurst.sourceSets.push_back(std::move(rasterSet));
    ok &= Check(!ValidateRawProjectSnapshot(rasterBurst).valid,
        "Burst Denoise accepted a raster set");

    RawProjectSnapshot draft = BuildBootstrap();
    MultiFrameSourceSet draftSet;
    draftSet.sourceSetId = GenerateStableUuid();
    draftSet.name = "Draft";
    draft.sourceSets.push_back(draftSet);
    ok &= Check(ValidateRawProjectSnapshot(draft).valid &&
        EvaluateSourceSetStatus(draft, draft.sourceSets.front()) ==
            MultiFrameSetStatus::Draft,
        "a source set with fewer than two frames was not retained as Draft");

    RawProjectSnapshot disabledDraft = snapshot;
    for (SourceSetFrame& frame : disabledDraft.sourceSets.front().frames) {
        frame.enabled = false;
    }
    disabledDraft.sourceSets.front().frames.front().enabled = true;
    ok &= Check(EvaluateSourceSetStatus(
        disabledDraft, disabledDraft.sourceSets.front()) == MultiFrameSetStatus::Draft,
        "a set with fewer than two enabled frames was reported future-renderable");

    RawProjectSnapshot oversizedBurst = BuildMfdSnapshot();
    MultiFrameSourceSet& oversizedSet = oversizedBurst.sourceSets.front();
    for (std::size_t index = oversizedSet.frames.size(); index < 31u; ++index) {
        EmbeddedAssetRecord asset;
        asset.sha256 = std::string(64u,
            static_cast<char>('a' + (index % 6u)));
        asset.byteLength = 2000u + static_cast<std::uint64_t>(index);
        asset.assetId = MakeAssetId(asset.sha256, asset.byteLength);
        asset.originalFilename =
            "mfd-frame-" + std::to_string(index + 1u) + ".dng";
        asset.displayName = asset.originalFilename;
        asset.originalFileFingerprint = asset.sha256;
        asset.inputFamily = MultiFrameInputFamily::Raw;
        asset.captureMetadataSummary =
            SerializeRawCaptureCompatibilitySummary(BuildMosaicSummary());
        oversizedBurst.embeddedAssets.push_back(asset);
        SourceSetFrame frame;
        frame.frameId = GenerateStableUuid();
        frame.assetId = asset.assetId;
        frame.userLabel = asset.originalFilename;
        oversizedSet.frames.push_back(std::move(frame));
    }
    std::string oversizedReason;
    const Raw::MultiFrame::GraphExecutionPlan oversizedPlan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(oversizedBurst);
    ok &= Check(
        ValidateRawProjectSnapshot(oversizedBurst).valid &&
            EvaluateSourceSetStatus(
                oversizedBurst, oversizedBurst.sourceSets.front(),
                &oversizedReason) ==
                    MultiFrameSetStatus::ReadyForFutureProcessing &&
            oversizedPlan.valid && oversizedPlan.executableWithCurrentAdapters,
        "a 31-frame source set could not keep a valid bounded graph subset executable");

    RawProjectSnapshot malformedMfd = BuildMfdSnapshot();
    malformedMfd.sourceSets.front().settings["parameters"]["registration"].erase(
        "keysBicubicParameter");
    ok &= Check(!ValidateRawProjectSnapshot(malformedMfd).valid,
        "schema-3 MFD settings accepted an incomplete RA-CFA parameter contract");

    RawProjectSnapshot nonObjectMfd = BuildMfdSnapshot();
    nonObjectMfd.sourceSets.front().settings = nlohmann::json::array();
    ok &= Check(!ValidateRawProjectSnapshot(nonObjectMfd).valid,
        "non-object MFD settings were not rejected safely");

    RawProjectSnapshot wrongTypeMfd = BuildMfdSnapshot();
    wrongTypeMfd.sourceSets.front().settings["algorithmVersion"] = "one";
    ok &= Check(!ValidateRawProjectSnapshot(wrongTypeMfd).valid,
        "wrong-typed MFD algorithm identity was not rejected safely");

    RawProjectSnapshot graphlessMfd = BuildMfdSnapshot();
    graphlessMfd.multiFrameGraph = {};
    ok &= Check(!ValidateRawProjectSnapshot(graphlessMfd).valid,
        "an obsolete graphless MFD project was accepted");

    RawProjectSnapshot legacyMfd = BuildMfdSnapshot();
    legacyMfd.sourceSets.front().operationSchemaVersion = 2u;
    legacyMfd.sourceSets.front().settings = {
        { "schemaVersion", 2u },
        { "inputDomain", "mosaic-cfa" },
        { "algorithmVersion", nullptr },
        { "sharedPostMfdRecipe", nlohmann::json::object() },
        { "viewTransformPlacement", "internal" },
        { "processingImplemented", false }
    };
    ok &= Check(!ValidateRawProjectSnapshot(legacyMfd).valid,
        "obsolete schema-2 MFD project was accepted");

    RawProjectSnapshot legacyRaCfa = BuildMfdSnapshot();
    legacyRaCfa.sourceSets.front().operationSchemaVersion = 4u;
    legacyRaCfa.sourceSets.front().settings["schemaVersion"] = 4u;
    legacyRaCfa.sourceSets.front().settings["algorithmId"] =
        Raw::Mfd::kAlgorithmId;
    legacyRaCfa.sourceSets.front().settings["algorithmVersion"] =
        Raw::Mfd::kAlgorithmVersion;
    ok &= Check(!ValidateRawProjectSnapshot(legacyRaCfa).valid,
        "obsolete schema-4 MFD project was accepted");
    return ok;
}

bool ValidateProjectAwareGalleryPresentation() {
    Stack::RawWorkspace::WorkspaceState workspace;
    Stack::RawWorkspace::SourceRecord member;
    member.relativePathKey = "burst/frame-1.dng";
    member.fileName = "frame-1.dng";
    Stack::RawWorkspace::SourceSetProjectMembership membership;
    membership.projectId = "project-1";
    membership.projectName = "Night Burst";
    membership.projectPath = "C:/Stack Projects/Night Burst--project1";
    member.sourceSetProjectMemberships.push_back(membership);
    // A source can retain the old per-source project pointer while the shared
    // ProjectIndex also supplies its canonical membership.  They describe one
    // saved version and must not produce the duplicate "2 saved versions"
    // badge seen in the RAW Gallery.
    member.project.status = Stack::RawWorkspace::ProjectStatus::Existing;
    member.project.absolutePath = membership.projectPath;
    workspace.sources.push_back(member);

    Stack::RawWorkspace::SourceRecord standalone;
    standalone.relativePathKey = "single.dng";
    standalone.fileName = "single.dng";
    workspace.sources.push_back(standalone);

    Stack::RawWorkspace::SourceSetProjectCatalogEntry project;
    project.projectId = "project-1";
    project.projectName = "Night Burst";
    project.absolutePath = membership.projectPath;
    project.totalFrameCount = 4u;
    project.multiFrameProject = true;
    project.referenceSourceKey = member.relativePathKey;
    workspace.sourceSetProjects.push_back(project);

    const Stack::RawWorkspace::GalleryPresentation presentation =
        Stack::RawWorkspace::BuildGalleryPresentation(workspace);
    std::size_t visibleSources = 0u;
    for (const auto& group : presentation.groups) {
        visibleSources += group.sources.size();
    }
    const Stack::RawWorkspace::GallerySourceView* representedMember = nullptr;
    for (const auto& group : presentation.groups) {
        const auto found = std::find_if(
            group.sources.begin(), group.sources.end(),
            [&](const auto& source) {
                return source.relativePathKey == member.relativePathKey;
            });
        if (found != group.sources.end()) {
            representedMember = &*found;
            break;
        }
    }
    return Check(
        presentation.projects.size() == 1u &&
            presentation.projects.front().projectName == "Night Burst" &&
            presentation.projects.front().frameCount == 4u &&
            visibleSources == 2u &&
            representedMember != nullptr &&
            representedMember->representsProject &&
            representedMember->projectIsMultiFrame &&
            representedMember->savedProjectCount == 1u &&
            representedMember->projectPath == project.absolutePath,
        "project-aware Gallery did not retain the source position while overlaying its saved Burst project");
}

bool ValidateGraphBindingRoundTrip() {
    EditorNodeGraph::Graph graph;
    graph.Clear();
    EditorNodeGraph::RawProjectSourceSetPayload payload;
    payload.sourceSetId = "source-set-validation";
    payload.presentationStatus = "Result unavailable: processing is not implemented yet.";
    EditorNodeGraph::Node* source = graph.AddRawProjectSourceSetNode(
        payload, { 40.0f, 60.0f });
    bool ok = Check(source != nullptr, "managed graph source node could not be created");
    if (!source) return false;
    ok &= Check(
        EditorNodeGraphDefinitions::FindLiveNodeDefinition(*source) != nullptr &&
            source->definitionResolved,
        "managed graph source node did not receive an exact internal definition");
    const nlohmann::json serialized = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    EditorNodeGraph::Graph restored;
    EditorNodeGraph::DeserializeGraphPayload(
        serialized, restored, 0, {}, 0, 0, 0);
    const auto found = std::find_if(
        restored.GetNodes().begin(), restored.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) {
            return node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet;
        });
    ok &= Check(found != restored.GetNodes().end(),
        "managed graph source node did not deserialize");
    if (found != restored.GetNodes().end()) {
        ok &= Check(found->rawProjectSourceSet.sourceSetId == payload.sourceSetId &&
                found->definitionResolved,
            "managed graph source-set binding or definition changed during round trip");
        const std::vector<EditorNodeGraph::SocketDefinition> sockets =
            restored.GetSockets(*found, false);
        const std::size_t outputCount = static_cast<std::size_t>(std::count_if(
            sockets.begin(), sockets.end(),
            [](const EditorNodeGraph::SocketDefinition& socket) {
                return socket.direction == EditorNodeGraph::SocketDirection::Output;
            }));
        ok &= Check(outputCount == 1u,
            "managed graph source node does not expose exactly one output");
    }

    nlohmann::json obsoleteManaged = serialized;
    obsoleteManaged["nodeGraph"]["nodes"][0]["definition"] = {
        { "id", "" }, { "version", "" }, { "contentHash", "" }
    };
    EditorNodeGraph::Graph rejectedManaged;
    EditorNodeGraph::DeserializeGraphPayload(
        obsoleteManaged, rejectedManaged, 0, {}, 0, 0, 0);
    ok &= Check(rejectedManaged.GetNodes().empty(),
        "a managed source-set node with an obsolete empty identity was accepted");
    return ok;
}

bool ValidateMfdModelContract() {
    using namespace Stack::Project;
    RawProjectSnapshot snapshot = BuildMfdSnapshot();
    bool ok = true;
    std::string reason;
    ok &= Check(ValidateRawProjectSnapshot(snapshot).valid,
        "a compatible mosaiced MFD snapshot was rejected");
    ok &= Check(EvaluateSourceSetStatus(
            snapshot, snapshot.sourceSets.front(), &reason) ==
            MultiFrameSetStatus::ReadyForFutureProcessing,
        "a compatible two-frame MFD set was not ready for future processing: " + reason);

    const nlohmann::json serialized = SerializeRawProjectSnapshot(snapshot);
    RawProjectSnapshot roundTrip;
    std::string error;
    ok &= Check(DeserializeRawProjectSnapshot(serialized, roundTrip, &error),
        "MFD snapshot did not deserialize: " + error);
    ok &= Check(roundTrip.activeFrameId == snapshot.activeFrameId &&
            roundTrip.mfdInputRevision == snapshot.mfdInputRevision &&
            roundTrip.postRecipeRevision == snapshot.postRecipeRevision &&
            roundTrip.sourceSets.front().referenceFrameId ==
                snapshot.sourceSets.front().referenceFrameId,
        "MFD active-frame, reference, or revision identity changed during round trip");
    ok &= Check(roundTrip.sourceSets.front().settings.value(
            "processingImplemented", true) == false,
        "MFD placeholder state stopped reporting that processing is unavailable");

    RawCaptureCompatibilitySummary reference = BuildMosaicSummary();
    RawCaptureCompatibilitySummary differentCamera = reference;
    differentCamera.uniqueCameraModel = "different-camera";
    ok &= Check(!AreMfdCapturesStructurallyCompatible(
            reference, differentCamera, &reason) && !reason.empty(),
        "different camera models were accepted into one MFD set");

    RawProjectSnapshot incompatible = snapshot;
    incompatible.embeddedAssets.back().captureMetadataSummary =
        SerializeRawCaptureCompatibilitySummary(differentCamera);
    ok &= Check(!ValidateRawProjectSnapshot(incompatible).valid &&
            EvaluateSourceSetStatus(
                incompatible, incompatible.sourceSets.front(), &reason) ==
                MultiFrameSetStatus::Incompatible,
        "a structurally incompatible MFD frame was not rejected");

    RawProjectSnapshot linear = snapshot;
    RawCaptureCompatibilitySummary linearSummary = reference;
    linearSummary.inputDomain = MfdInputDomain::LinearRgbUnsupported;
    linearSummary.supported = false;
    linearSummary.rejectionReason =
        "Linear RAW multi-frame denoise is planned for a future workflow.";
    linear.embeddedAssets.back().captureMetadataSummary =
        SerializeRawCaptureCompatibilitySummary(linearSummary);
    ok &= Check(!ValidateRawProjectSnapshot(linear).valid &&
            EvaluateSourceSetStatus(
                linear, linear.sourceSets.front(), &reason) ==
                MultiFrameSetStatus::Incompatible,
        "a demosaiced/Linear RGB RAW was accepted into the mosaiced MFD path");

    RawProjectSnapshot draft = snapshot;
    draft.sourceSets.front().frames.back().enabled = false;
    ok &= Check(EvaluateSourceSetStatus(
            draft, draft.sourceSets.front(), &reason) ==
            MultiFrameSetStatus::Draft,
        "an MFD set with fewer than two enabled frames was not retained as Draft");
    return ok;
}

bool ValidateMfdGraphContract() {
    EditorNodeGraph::Graph graph;
    graph.Clear();
    const std::string sourceSetId = "mfd-source-set-validation";
    EditorNodeGraph::MultiFrameDenoisePayload mfdPayload;
    mfdPayload.sourceSetId = sourceSetId;
    mfdPayload.presentationStatus =
        EditorNodeGraph::kMfdAwaitingProcessingStatus;
    mfdPayload.resultState = "unavailable";
    mfdPayload.internalViewTransformEnabled = true;

    std::vector<int> frameNodeIds;
    for (int index = 0; index < 2; ++index) {
        EditorNodeGraph::RawProjectFramePayload framePayload;
        framePayload.sourceSetId = sourceSetId;
        framePayload.frameId = "frame-" + std::to_string(index + 1);
        framePayload.assetId = "asset-" + std::to_string(index + 1);
        framePayload.displayLabel = "Frame " + std::to_string(index + 1);
        framePayload.reference = index == 0;
        EditorNodeGraph::MfdFrameBinding binding;
        binding.frameId = framePayload.frameId;
        binding.socketId = EditorNodeGraph::MfdFrameInputSocketId(framePayload.frameId);
        binding.label = framePayload.displayLabel;
        binding.reference = framePayload.reference;
        mfdPayload.frameBindings.push_back(binding);
        EditorNodeGraph::Node* frameNode = graph.AddRawProjectFrameNode(
            std::move(framePayload), { 40.0f, 60.0f + index * 160.0f });
        frameNodeIds.push_back(frameNode ? frameNode->id : -1);
    }
    EditorNodeGraph::Node* mfd = graph.AddMultiFrameDenoiseNode(
        std::move(mfdPayload), { 500.0f, 140.0f });
    bool ok = Check(frameNodeIds[0] > 0 && frameNodeIds[1] > 0 && mfd,
        "managed MFD graph nodes could not be created");
    if (!ok) return false;
    const int mfdNodeId = mfd->id;
    ok &= Check(
        EditorNodeGraphDefinitions::FindLiveNodeDefinition(*mfd) != nullptr &&
            mfd->definitionResolved,
        "the managed MFD node did not receive an exact internal definition");
    for (int frameNodeId : frameNodeIds) {
        EditorNodeGraph::Node* frame = graph.FindNode(frameNodeId);
        if (!frame) {
            ok &= Check(false, "managed RAW frame disappeared from the graph");
            continue;
        }
        ok &= Check(
            EditorNodeGraphDefinitions::FindLiveNodeDefinition(*frame) != nullptr &&
                frame->definitionResolved,
            "a managed RAW frame did not receive an exact internal definition");
        std::string error;
        const std::string socketId =
            EditorNodeGraph::MfdFrameInputSocketId(frame->rawProjectFrame.frameId);
        ok &= Check(graph.TryConnectSockets(
                frameNodeId,
                EditorNodeGraph::kRawOutputSocketId,
                mfdNodeId,
                socketId,
                &error),
            "managed RAW frame could not connect to its MFD socket: " + error);
        for (EditorNodeGraph::Link& link : graph.EditLinks()) {
            if (link.fromNodeId == frameNodeId &&
                link.toNodeId == mfdNodeId &&
                link.toSocketId == socketId) {
                link.ownership =
                    EditorNodeGraph::Link::Ownership::ManagedSourceBinding;
                link.bindingId = frame->rawProjectFrame.frameId;
            }
        }
    }
    EditorNodeGraph::Node* output = graph.EnsureOutputNode();
    std::string outputError;
    ok &= Check(output && graph.TryConnectSockets(
            mfdNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            output->id,
            EditorNodeGraph::kImageInputSocketId,
            &outputError),
        "MFD unavailable output could not retain a serializable downstream connection: " +
            outputError);
    ok &= Check(
        !graph.IsOutputConnected() &&
            graph.GetOutputConnectionDiagnostic().empty(),
        "an unprocessed MFD result was reported as a graph error instead of a normal waiting state");

    graph.EditNodes();
    graph.FindNode(mfdNodeId)->multiFrameDenoise.resultState = "ready";
    ok &= Check(
        graph.IsOutputConnected(),
        "an adopted MFD result did not become an executable graph source");
    graph.EditNodes();
    graph.FindNode(mfdNodeId)->multiFrameDenoise.resultState = "unavailable";

    const EditorNodeGraph::ScenePathInfo internalPath =
        EditorNodeGraph::AnalyzeScenePath(graph, mfdNodeId);
    ok &= Check(!internalPath.sceneReferred && internalPath.hasViewTransform,
        "the default internal MFD view was not treated as display-mapped");
    graph.FindNode(mfdNodeId)->multiFrameDenoise.internalViewTransformEnabled = false;
    const EditorNodeGraph::ScenePathInfo externalPath =
        EditorNodeGraph::AnalyzeScenePath(graph, mfdNodeId);
    ok &= Check(externalPath.sceneReferred && !externalPath.hasViewTransform,
        "disabling the internal MFD view did not expose a scene-linear graph path");

    const std::vector<EditorNodeGraph::SocketDefinition> mfdSockets =
        graph.GetSockets(*graph.FindNode(mfdNodeId), false);
    ok &= Check(std::count_if(
            mfdSockets.begin(), mfdSockets.end(),
            [](const EditorNodeGraph::SocketDefinition& socket) {
                return socket.direction == EditorNodeGraph::SocketDirection::Input &&
                    socket.type == EditorNodeGraph::SocketType::Raw;
            }) == 2,
        "MFD did not expose one stable RAW input socket per bound project frame");

    const nlohmann::json serialized = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    EditorNodeGraph::Graph restored;
    EditorNodeGraph::DeserializeGraphPayload(serialized, restored, 0, {}, 0, 0, 0);
    const auto restoredMfd = std::find_if(
        restored.GetNodes().begin(), restored.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) {
            return node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise;
        });
    ok &= Check(restoredMfd != restored.GetNodes().end() &&
            restoredMfd->multiFrameDenoise.frameBindings.size() == 2u &&
            restoredMfd->multiFrameDenoise.resultState == "unavailable" &&
            !restoredMfd->multiFrameDenoise.internalViewTransformEnabled &&
            restoredMfd->definitionResolved,
        "MFD bindings, definition, unavailable result, or View placement changed during graph round trip");
    ok &= Check(std::count_if(
            restored.GetLinks().begin(), restored.GetLinks().end(),
            [](const EditorNodeGraph::Link& link) {
                return link.ownership ==
                    EditorNodeGraph::Link::Ownership::ManagedSourceBinding &&
                    !link.bindingId.empty();
            }) == 2,
        "managed frame-to-MFD link ownership did not survive graph serialization");

    nlohmann::json obsoleteManaged = serialized;
    for (nlohmann::json& item : obsoleteManaged["nodeGraph"]["nodes"]) {
        const std::string kind = item.value("kind", std::string());
        if (kind == "RawProjectFrame" ||
            kind == "MultiFrameDenoise" ||
            kind == "RawProjectSourceSet") {
            item["definition"] = {
                { "id", "" }, { "version", "" }, { "contentHash", "" }
            };
        }
    }
    EditorNodeGraph::Graph rejectedManaged;
    EditorNodeGraph::DeserializeGraphPayload(
        obsoleteManaged, rejectedManaged, 0, {}, 0, 0, 0);
    ok &= Check(rejectedManaged.GetNodes().empty(),
        "managed RAW nodes with obsolete empty identities were accepted");
    return ok;
}

bool ValidateHdrGraphContract() {
    EditorNodeGraph::Graph graph;
    graph.Clear();
    bool ok = Check(
        Stack::Project::MakeDefaultHdrOperationSettings().value(
            "viewTransformPlacement", std::string()) == "internal",
        "new HDR projects did not default to exactly one internal View Transform");
    const std::string sourceSetId = "hdr-source-set-validation";
    EditorNodeGraph::MultiFrameHdrPayload payload;
    payload.sourceSetId = sourceSetId;
    payload.presentationStatus = EditorNodeGraph::kHdrAwaitingProcessingStatus;
    payload.resultState = "unavailable";
    payload.radiometricAnchorFrameId = "frame-2";

    std::vector<int> frameNodeIds;
    for (int index = 0; index < 3; ++index) {
        EditorNodeGraph::RawProjectFramePayload framePayload;
        framePayload.sourceSetId = sourceSetId;
        framePayload.frameId = "frame-" + std::to_string(index + 1);
        framePayload.assetId = "asset-" + std::to_string(index + 1);
        framePayload.displayLabel = "Bracket " + std::to_string(index + 1);
        framePayload.reference = index == 0;
        EditorNodeGraph::MfdFrameBinding binding;
        binding.frameId = framePayload.frameId;
        binding.socketId = EditorNodeGraph::MfdFrameInputSocketId(binding.frameId);
        binding.label = framePayload.displayLabel;
        binding.reference = framePayload.reference;
        payload.frameBindings.push_back(binding);
        EditorNodeGraph::Node* frame = graph.AddRawProjectFrameNode(
            std::move(framePayload), { 40.0f, 60.0f + index * 150.0f });
        frameNodeIds.push_back(frame ? frame->id : -1);
    }
    EditorNodeGraph::Node* hdr = graph.AddMultiFrameHdrNode(
        std::move(payload), { 500.0f, 140.0f });
    ok &= Check(hdr != nullptr && std::all_of(
            frameNodeIds.begin(), frameNodeIds.end(), [](int id) { return id > 0; }),
        "managed HDR graph nodes could not be created");
    if (!ok) return false;
    const int hdrNodeId = hdr->id;
    ok &= Check(EditorNodeGraphDefinitions::FindLiveNodeDefinition(*hdr) != nullptr &&
            hdr->definitionResolved,
        "the managed HDR node did not receive an exact internal definition");
    for (int frameNodeId : frameNodeIds) {
        const EditorNodeGraph::Node* frame = graph.FindNode(frameNodeId);
        std::string error;
        ok &= Check(frame && graph.TryConnectSockets(
                frameNodeId, EditorNodeGraph::kRawOutputSocketId,
                hdrNodeId, EditorNodeGraph::MfdFrameInputSocketId(
                    frame->rawProjectFrame.frameId), &error),
            "managed RAW frame could not connect to its HDR socket: " + error);
    }
    EditorNodeGraph::Node* output = graph.EnsureOutputNode();
    std::string error;
    ok &= Check(output && graph.TryConnectSockets(
            hdrNodeId, EditorNodeGraph::kImageOutputSocketId,
            output->id, EditorNodeGraph::kImageInputSocketId, &error),
        "HDR result could not connect to Output: " + error);
    ok &= Check(!graph.IsOutputConnected() &&
            graph.GetOutputConnectionDiagnostic().empty(),
        "an unprocessed HDR project was reported as a graph error");
    graph.EditNodes();
    graph.FindNode(hdrNodeId)->multiFrameHdr.resultState = "ready";
    ok &= Check(graph.IsOutputConnected(),
        "a published HDR result did not become an executable graph source");

    const EditorNodeGraph::ScenePathInfo internalPath =
        EditorNodeGraph::AnalyzeScenePath(graph, hdrNodeId);
    ok &= Check(!internalPath.sceneReferred && internalPath.hasViewTransform,
        "the default internal HDR view was not treated as display-mapped");
    graph.FindNode(hdrNodeId)->multiFrameHdr.internalViewTransformEnabled = false;
    const EditorNodeGraph::ScenePathInfo externalPath =
        EditorNodeGraph::AnalyzeScenePath(graph, hdrNodeId);
    ok &= Check(externalPath.sceneReferred && !externalPath.hasViewTransform,
        "disabling the internal HDR view did not expose a scene-linear graph path");

    const std::vector<EditorNodeGraph::SocketDefinition> sockets =
        graph.GetSockets(*graph.FindNode(hdrNodeId), false);
    ok &= Check(std::count_if(sockets.begin(), sockets.end(),
            [](const EditorNodeGraph::SocketDefinition& socket) {
                return socket.direction == EditorNodeGraph::SocketDirection::Input &&
                    socket.type == EditorNodeGraph::SocketType::Raw;
            }) == 3,
        "HDR did not expose one stable RAW input per bracket frame");

    const nlohmann::json serialized = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    EditorNodeGraph::Graph restored;
    EditorNodeGraph::DeserializeGraphPayload(serialized, restored, 0, {}, 0, 0, 0);
    const auto restoredHdr = std::find_if(
        restored.GetNodes().begin(), restored.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) {
            return node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr;
        });
    ok &= Check(restoredHdr != restored.GetNodes().end() &&
            restoredHdr->multiFrameHdr.frameBindings.size() == 3u &&
            restoredHdr->multiFrameHdr.radiometricAnchorFrameId == "frame-2" &&
            restoredHdr->multiFrameHdr.resultState == "ready" &&
            !restoredHdr->multiFrameHdr.internalViewTransformEnabled &&
            restoredHdr->definitionResolved,
        "HDR bindings, anchor, result state, View placement, or definition changed during graph round trip");
    return ok;
}

bool ValidateSessionOrdering() {
    using namespace Stack::Project;
    ProjectSessionController controller;
    const ProjectReplacementToken replacement = controller.BeginReplacement();
    bool ok = Check(controller.CompleteReplacement(
        replacement, "project-a", 4u, 7u, false),
        "session replacement could not complete");
    controller.NoteEdit();
    const ProjectSaveToken older = controller.BeginSave();
    controller.NoteEdit();
    const ProjectSaveToken newer = controller.BeginSave();
    ok &= Check(!controller.CompleteSave(older, true, 8u, false),
        "a stale async save completion was accepted");
    ok &= Check(controller.CompleteSave(newer, true, 8u, false),
        "the current save completion was rejected");
    ok &= Check(!controller.IsDirty(),
        "the current save did not persist its captured dirty revision");

    controller.NoteEdit();
    const ProjectSaveToken orphaned = controller.BeginSave();
    ok &= Check(static_cast<bool>(orphaned) &&
            controller.Phase() == ProjectLifecyclePhase::Saving,
        "the orphaned-save fixture did not enter the saving phase");
    ok &= Check(controller.RecoverOrphanedSave() &&
            controller.Phase() == ProjectLifecyclePhase::ReadyDirty &&
            controller.IsDirty(),
        "an orphaned save did not return the session to retryable dirty state");
    ok &= Check(!controller.CompleteSave(orphaned, true, 9u, false),
        "an orphaned save completion remained current after recovery");
    const ProjectSaveToken retry = controller.BeginSave();
    ok &= Check(controller.CompleteSave(retry, true, 9u, false) &&
            !controller.IsDirty(),
        "the recovered session could not save successfully on retry");
    ok &= Check(controller.BeginImport(), "session refused a valid import");
    controller.NoteEdit();
    controller.CompleteImport(false);
    ok &= Check(!controller.IsDirty(),
        "a cancelled import published a dirty revision");

    ProjectSessionController guarded;
    const ProjectReplacementToken guardedLoad = guarded.BeginReplacement();
    ok &= Check(guarded.CompleteReplacement(
        guardedLoad, "project-guarded", 2u, 5u, false),
        "guarded session could not be initialized");
    guarded.MarkConflict();
    guarded.NoteEdit();
    ok &= Check(guarded.Phase() == ProjectLifecyclePhase::Conflict,
        "an edit incorrectly cleared the project conflict state");
    const ProjectReplacementToken olderReplacement = guarded.BeginReplacement();
    const ProjectReplacementToken currentReplacement = guarded.BeginReplacement();
    guarded.FailReplacement(olderReplacement);
    guarded.FailReplacement(currentReplacement);
    ok &= Check(guarded.Phase() == ProjectLifecyclePhase::Conflict,
        "a failed overlapping project load did not restore the prior lifecycle state");
    return ok;
}

bool ValidateStores(const std::filesystem::path& root) {
    using namespace Stack::Project;
    const std::filesystem::path rawAPath = root / "frame-a.dng";
    const std::filesystem::path rawACopyPath = root / "frame-a-copy.dng";
    const std::filesystem::path rawBPath = root / "frame-b.dng";
    const std::vector<unsigned char> rawA { 0x49, 0x49, 0x2a, 0x00, 1, 3, 5, 7, 9 };
    const std::vector<unsigned char> rawB { 0x4d, 0x4d, 0x00, 0x2a, 2, 4, 6, 8, 10, 12 };
    bool ok = Check(WriteBytes(rawAPath, rawA) &&
        WriteBytes(rawACopyPath, rawA) && WriteBytes(rawBPath, rawB),
        "test originals could not be written");
    if (!ok) return false;

    RawProjectSnapshot bootstrap = BuildBootstrap();
    const std::filesystem::path obsoletePath = root / "obsolete-project";
    ProjectStoreOpenResult obsoleteCreated = CreateProjectStore(
        obsoletePath, ProjectStorageKind::DirectoryBundle, bootstrap);
    ok &= Check(static_cast<bool>(obsoleteCreated),
        "obsolete-project rejection fixture could not be created: " +
            obsoleteCreated.message);
    if (obsoleteCreated) {
        obsoleteCreated.store.reset();
        const std::filesystem::path obsoleteDocument =
            obsoletePath / "project.stack";
        nlohmann::json obsoleteManifest = nlohmann::json::parse(
            ReadBytes(obsoleteDocument), nullptr, false);
        obsoleteManifest["schemaVersion"] =
            kRawProjectSourceSetSchemaVersion - 1u;
        const std::string obsoleteText = obsoleteManifest.dump(2);
        const std::vector<unsigned char> obsoleteBytes(
            obsoleteText.begin(), obsoleteText.end());
        ok &= Check(WriteBytes(obsoleteDocument, obsoleteBytes),
            "obsolete-project rejection fixture could not be written");
        ProjectStoreOpenResult obsoleteOpened = OpenProjectStore(obsoletePath);
        ok &= Check(
            !obsoleteOpened &&
                std::filesystem::is_directory(obsoletePath) &&
                ReadBytes(obsoleteDocument) == obsoleteBytes,
            "opening an obsolete project did not reject it untouched");
    }

    const std::filesystem::path cancelledPath = root / "cancelled";
    ProjectStoreOpenResult cancelled = CreateProjectStore(
        cancelledPath, ProjectStorageKind::DirectoryBundle, bootstrap);
    ok &= Check(static_cast<bool>(cancelled),
        "cancellation test store creation failed: " + cancelled.message);
    if (cancelled) {
        const ProjectStoreTransaction cancelledTransaction =
            cancelled.store->BeginTransaction(
                cancelled.snapshot.persistedStorageRevision);
        EmbeddedAssetRecord cancelledAsset;
        std::string cancellationError;
        ok &= Check(cancelled.store->StageAssetFile(
            cancelledTransaction,
            rawAPath,
            MultiFrameInputFamily::Raw,
            nlohmann::json::object(),
            cancelledAsset,
            &cancellationError),
            "cancellation test asset staging failed: " + cancellationError);
        cancelled.store->Abort(cancelledTransaction);
        ProjectStoreOpenResult afterCancellation = OpenProjectStore(cancelledPath);
        ok &= Check(static_cast<bool>(afterCancellation) &&
                afterCancellation.snapshot.embeddedAssets.empty() &&
                afterCancellation.snapshot.sourceSets.empty(),
            "aborting source ingestion published a partial asset or source set");
    }

    const std::filesystem::path previewResiliencePath =
        root / "preview-resilience";
    ProjectStoreOpenResult previewStore = CreateProjectStore(
        previewResiliencePath,
        ProjectStorageKind::DirectoryBundle,
        bootstrap);
    ok &= Check(static_cast<bool>(previewStore),
        "preview resilience store creation failed: " + previewStore.message);
    if (previewStore) {
        RawProjectSnapshot previewSnapshot = previewStore.snapshot;
        previewSnapshot.coverThumbnailBytes = { 1u, 2u, 3u, 4u };
        ProjectStoreTransaction firstPreview = previewStore.store->BeginTransaction(
            previewSnapshot.persistedStorageRevision);
        ProjectStoreCommitResult firstPreviewCommit = previewStore.store->Commit(
            firstPreview, previewSnapshot);
        ok &= Check(static_cast<bool>(firstPreviewCommit),
            "first rebuildable preview commit failed: " +
                firstPreviewCommit.message);
        if (firstPreviewCommit) {
            previewSnapshot.persistedStorageRevision =
                firstPreviewCommit.committedStorageRevision;
            previewSnapshot.coverThumbnailBytes = { 5u, 6u, 7u, 8u, 9u };
            ProjectStoreTransaction secondPreview =
                previewStore.store->BeginTransaction(
                    previewSnapshot.persistedStorageRevision);
            ProjectStoreCommitResult secondPreviewCommit =
                previewStore.store->Commit(secondPreview, previewSnapshot);
            ok &= Check(static_cast<bool>(secondPreviewCommit),
                "replacement rebuildable preview commit failed: " +
                    secondPreviewCommit.message);
            ProjectStoreOpenResult replacedPreview = OpenProjectStore(
                previewResiliencePath);
            ok &= Check(static_cast<bool>(replacedPreview) &&
                    !replacedPreview.recoveredPreviousManifest &&
                    replacedPreview.snapshot.coverThumbnailBytes ==
                        previewSnapshot.coverThumbnailBytes,
                "replacing preview.png made the latest authoritative manifest unreadable: " +
                    replacedPreview.message + " (loaded bytes=" +
                    std::to_string(
                        replacedPreview.snapshot.coverThumbnailBytes.size()) +
                    ", expected bytes=" +
                    std::to_string(
                        previewSnapshot.coverThumbnailBytes.size()) + ")");

            const std::vector<unsigned char> corruptedPreview {
                0xdeu, 0xadu, 0xbeu, 0xefu
            };
            ok &= Check(WriteBytes(
                    previewResiliencePath / "preview.png",
                    corruptedPreview),
                "preview corruption fixture could not be written");
            ProjectStoreOpenResult missingPreview = OpenProjectStore(
                previewResiliencePath);
            ok &= Check(static_cast<bool>(missingPreview) &&
                    !missingPreview.recoveredPreviousManifest &&
                    missingPreview.snapshot.coverThumbnailBytes.empty() &&
                    !missingPreview.message.empty(),
                "a damaged rebuildable preview rejected or rolled back the authoritative project: " +
                    missingPreview.message + " (loaded=" +
                    std::to_string(static_cast<bool>(missingPreview)) +
                    ", recovered=" +
                    std::to_string(
                        missingPreview.recoveredPreviousManifest) +
                    ", cover bytes=" +
                    std::to_string(
                        missingPreview.snapshot.coverThumbnailBytes.size()) +
                    ")");
        }
    }

    const std::filesystem::path bundlePath = root / "roundtrip";
    std::cout << "[multi-source/store] create bundle" << std::endl;
    ProjectStoreOpenResult created = CreateProjectStore(
        bundlePath, ProjectStorageKind::DirectoryBundle, bootstrap);
    ok &= Check(static_cast<bool>(created), "directory store creation failed: " + created.message);
    if (!created) return false;

    RawProjectSnapshot snapshot = created.snapshot;
    std::cout << "[multi-source/store] stage assets" << std::endl;
    ProjectStoreTransaction transaction = created.store->BeginTransaction(
        snapshot.persistedStorageRevision);
    std::cout << "[multi-source/store] transaction begun" << std::endl;
    EmbeddedAssetRecord assetA;
    EmbeddedAssetRecord assetACopy;
    EmbeddedAssetRecord assetB;
    std::string error;
    const nlohmann::json mosaicCaptureMetadata =
        SerializeRawCaptureCompatibilitySummary(BuildMosaicSummary());
    ok &= Check(created.store->StageAssetFile(
        transaction, rawAPath, MultiFrameInputFamily::Raw,
        mosaicCaptureMetadata, assetA, &error), "asset A staging failed: " + error);
    std::cout << "[multi-source/store] asset A staged" << std::endl;
    ok &= Check(created.store->StageAssetFile(
        transaction, rawACopyPath, MultiFrameInputFamily::Raw,
        mosaicCaptureMetadata, assetACopy, &error), "duplicate asset staging failed: " + error);
    std::cout << "[multi-source/store] duplicate staged" << std::endl;
    ok &= Check(assetA.assetId == assetACopy.assetId,
        "content-addressed staging did not deduplicate identical originals");
    ok &= Check(created.store->StageAssetFile(
        transaction, rawBPath, MultiFrameInputFamily::Raw,
        mosaicCaptureMetadata, assetB, &error), "asset B staging failed: " + error);
    std::cout << "[multi-source/store] asset B staged" << std::endl;

    snapshot.embeddedAssets = { assetA, assetB };
    MultiFrameSourceSet first;
    first.sourceSetId = GenerateStableUuid();
    first.name = "Primary Burst";
    first.inputFamily = MultiFrameInputFamily::Raw;
    first.operationIntent = MultiFrameOperationIntent::RawBurstDenoise;
    first.operationSchemaVersion = kMfdOperationSchemaVersion;
    first.settings = MakeDefaultMfdOperationSettings();
    first.graphBindingNodeId = GenerateStableUuid();
    first.frames = {
        { GenerateStableUuid(), assetA.assetId, true, "A", nlohmann::json::object() },
        { GenerateStableUuid(), assetB.assetId, true, "B", nlohmann::json::object() }
    };
    first.referenceFrameId = first.frames[1].frameId;
    MultiFrameSourceSet second;
    second.sourceSetId = GenerateStableUuid();
    second.name = "Cross-set reuse draft";
    second.inputFamily = MultiFrameInputFamily::Raw;
    second.operationIntent = MultiFrameOperationIntent::Mfsr;
    second.graphBindingNodeId = GenerateStableUuid();
    second.frames = {
        { GenerateStableUuid(), assetA.assetId, true, "A reused", nlohmann::json::object() }
    };
    second.referenceFrameId = second.frames.front().frameId;
    snapshot.sourceSets = { first, second };
    snapshot.multiFrameGraph = BuildManualMultiFrameGraph(
        snapshot,
        snapshot.sourceSets.front());
    snapshot.activeSourceSetId = first.sourceSetId;
    snapshot.pipelineData["managedBindings"] = { first.graphBindingNodeId, second.graphBindingNodeId };
    snapshot.dirtyRevision = 2u;
    std::cout << "[multi-source/store] commit bundle" << std::endl;
    ProjectStoreCommitResult commit = created.store->Commit(transaction, snapshot);
    ok &= Check(static_cast<bool>(commit), "directory commit failed: " + commit.message);
    if (!commit) return false;
    snapshot.persistedStorageRevision = commit.committedStorageRevision;

    const std::uint64_t revisionBeforeNoOp = snapshot.persistedStorageRevision;
    ProjectStoreTransaction noOpSave = created.store->BeginTransaction(
        revisionBeforeNoOp);
    ProjectStoreCommitResult noOpResult = created.store->Commit(
        noOpSave, snapshot);
    ok &= Check(static_cast<bool>(noOpResult) &&
            noOpResult.committedStorageRevision == revisionBeforeNoOp &&
            created.store->StorageRevision() == revisionBeforeNoOp,
        "an identical in-session save advanced storage and made the active snapshot stale");

    ++snapshot.dirtyRevision;
    ProjectStoreTransaction followupSave = created.store->BeginTransaction(
        snapshot.persistedStorageRevision);
    ProjectStoreCommitResult followupResult = created.store->Commit(
        followupSave, snapshot);
    ok &= Check(static_cast<bool>(followupResult),
        "a real setting change after an identical save falsely conflicted: " +
            followupResult.message);
    if (!followupResult) return false;
    snapshot.persistedStorageRevision = followupResult.committedStorageRevision;

    std::cout << "[multi-source/store] reopen and verify bundle" << std::endl;
    ProjectStoreOpenResult reopened = OpenProjectStore(bundlePath);
    ok &= Check(static_cast<bool>(reopened), "directory reopen failed: " + reopened.message);
    if (!reopened) return false;
    ok &= Check(LogicalSnapshotJson(reopened.snapshot) == LogicalSnapshotJson(snapshot),
        "directory store logical snapshot changed during round trip");
    ok &= Check(ReadStream(reopened.store->OpenAssetStream(assetA.assetId, &error)) == rawA,
        "directory store did not return exact original bytes");
    std::filesystem::path longCopyParent = root / "graph-cache";
    const std::string longCopyName =
        assetA.sha256 + "-" + std::to_string(assetA.byteLength) + ".dng";
    while ((longCopyParent / longCopyName).native().size() < 242u) {
        longCopyParent /= "segment";
    }
    const std::filesystem::path longCopyPath = longCopyParent / longCopyName;
    error.clear();
    ok &= Check(
        longCopyPath.native().size() < 260u,
        "the Windows atomic-copy regression destination itself exceeded MAX_PATH");
    ok &= Check(
        reopened.store->CopyAssetToFile(assetA.assetId, longCopyPath, &error),
        "a valid long graph-cache destination could not stage its compact atomic copy: " +
            error);
    ok &= Check(
        ReadBytes(longCopyPath) == rawA,
        "the compact long-path atomic copy changed embedded asset bytes");
    std::vector<std::string> verificationErrors;
    ok &= Check(reopened.store->Verify(reopened.snapshot, &verificationErrors),
        verificationErrors.empty() ? "directory verification failed" : verificationErrors.front());

    const std::filesystem::path directoryCopyPath = root / "directory-copy";
    std::cout << "[multi-source/store] copy working directory" << std::endl;
    ProjectStoreOpenResult directoryCopy = ConvertProjectStore(
        reopened.store,
        reopened.snapshot,
        directoryCopyPath,
        ProjectStorageKind::DirectoryBundle);
    ok &= Check(static_cast<bool>(directoryCopy),
        "bundle Save As conversion failed: " + directoryCopy.message);
    if (directoryCopy) {
        ok &= Check(
            LogicalSnapshotJson(directoryCopy.snapshot) ==
                LogicalSnapshotJson(reopened.snapshot),
            "bundle Save As conversion changed logical project content");
        ok &= Check(
            ReadStream(directoryCopy.store->OpenAssetStream(assetA.assetId, &error)) == rawA,
            "bundle Save As conversion changed the first managed original");
        ok &= Check(
            ReadStream(directoryCopy.store->OpenAssetStream(assetB.assetId, &error)) == rawB,
            "bundle Save As conversion changed the second managed original");
        std::vector<std::string> copyVerificationErrors;
        ok &= Check(
            directoryCopy.store->Verify(
                directoryCopy.snapshot, &copyVerificationErrors),
            copyVerificationErrors.empty()
                ? "bundle Save As verification failed"
                : copyVerificationErrors.front());
    }

    const std::filesystem::path portablePath = root / "roundtrip.stack";
    std::cout << "[multi-source/store] convert portable" << std::endl;
    ProjectStoreOpenResult portable = ConvertProjectStore(
        reopened.store, reopened.snapshot, portablePath, ProjectStorageKind::PortableFile);
    ok &= Check(static_cast<bool>(portable), "bundle to portable conversion failed: " + portable.message);
    if (!portable) return false;
    ok &= Check(LogicalSnapshotJson(portable.snapshot) == LogicalSnapshotJson(reopened.snapshot),
        "portable conversion changed logical project content");
    ok &= Check(ReadStream(portable.store->OpenAssetStream(assetB.assetId, &error)) == rawB,
        "portable store did not stream exact original bytes");

    std::cout << "[multi-source/store] conflict revisions" << std::endl;
    ProjectStoreTransaction firstSave = portable.store->BeginTransaction(
        portable.snapshot.persistedStorageRevision);
    ProjectStoreTransaction staleSave = portable.store->BeginTransaction(
        portable.snapshot.persistedStorageRevision);
    RawProjectSnapshot updated = portable.snapshot;
    updated.dirtyRevision += 1u;
    ProjectStoreCommitResult firstSaveResult = portable.store->Commit(firstSave, updated);
    ok &= Check(static_cast<bool>(firstSaveResult),
        "first overlapping save failed: " + firstSaveResult.message);
    if (firstSaveResult) {
        updated.persistedStorageRevision =
            firstSaveResult.committedStorageRevision;
    }
    ProjectStoreCommitResult staleResult = portable.store->Commit(staleSave, updated);
    ok &= Check(staleResult.status == ProjectStoreCommitStatus::Conflict,
        "stale expected-revision save did not produce a conflict");
    portable.store->Abort(staleSave);

    std::cout << "[multi-source/store] interrupted append recovery" << std::endl;
    {
        std::ofstream trailing(portablePath, std::ios::binary | std::ios::app);
        trailing << "INTERRUPTED_UNCOMMITTED_APPEND";
    }
    std::error_code sizeError;
    const std::uint64_t unoptimizedSize = static_cast<std::uint64_t>(
        std::filesystem::file_size(portablePath, sizeError));
    ok &= Check(!sizeError, "portable size could not be measured before optimize");
    ProjectStoreOpenResult afterInterruptedAppend = OpenProjectStore(portablePath);
    ok &= Check(static_cast<bool>(afterInterruptedAppend),
        "portable recovery did not find the last valid footer: " +
            afterInterruptedAppend.message);
    if (afterInterruptedAppend) {
        ok &= Check(
            LogicalSnapshotJson(afterInterruptedAppend.snapshot) ==
                LogicalSnapshotJson(updated),
            "an interrupted portable append became authoritative");
        const std::uint64_t revisionBeforeOptimize =
            afterInterruptedAppend.store->StorageRevision();
        const bool optimized = afterInterruptedAppend.store->Optimize(&error);
        ok &= Check(optimized, "portable optimize failed: " + error);
        if (optimized) {
            ok &= Check(afterInterruptedAppend.store->StorageRevision() ==
                    revisionBeforeOptimize + 1u,
                "portable optimize did not monotonically advance the storage revision");
            ProjectStoreOpenResult optimizedStore = OpenProjectStore(portablePath);
            ok &= Check(static_cast<bool>(optimizedStore),
                "optimized portable project could not be reopened: " +
                    optimizedStore.message);
            if (optimizedStore) {
                ok &= Check(optimizedStore.snapshot.persistedStorageRevision ==
                        revisionBeforeOptimize + 1u,
                    "optimized portable footer did not retain the advanced revision");
                ok &= Check(
                    ReadStream(optimizedStore.store->OpenAssetStream(assetA.assetId, &error)) == rawA &&
                    ReadStream(optimizedStore.store->OpenAssetStream(assetB.assetId, &error)) == rawB,
                    "portable optimize changed exact original bytes");
                std::vector<std::string> optimizedErrors;
                ok &= Check(optimizedStore.store->Verify(
                        optimizedStore.snapshot, &optimizedErrors),
                    optimizedErrors.empty()
                        ? "optimized portable verification failed"
                        : optimizedErrors.front());
                afterInterruptedAppend = std::move(optimizedStore);
            }
            sizeError.clear();
            const std::uint64_t optimizedSize = static_cast<std::uint64_t>(
                std::filesystem::file_size(portablePath, sizeError));
            ok &= Check(!sizeError && optimizedSize < unoptimizedSize,
                "portable optimize did not remove obsolete generations and trailing data");
        }
    }

    std::cout << "[multi-source/store] convert back" << std::endl;
    const std::filesystem::path convertedBackPath = root / "converted-back";
    ProjectStoreOpenResult convertedBack = ConvertProjectStore(
        afterInterruptedAppend.store,
        afterInterruptedAppend.snapshot,
        convertedBackPath,
        ProjectStorageKind::DirectoryBundle);
    ok &= Check(static_cast<bool>(convertedBack),
        "portable to bundle conversion failed: " + convertedBack.message);
    if (convertedBack) {
        ok &= Check(ReadStream(convertedBack.store->OpenAssetStream(assetA.assetId, &error)) == rawA,
            "portable to bundle conversion changed original bytes");
    }

    std::cout << "[multi-source/store] previous manifest recovery" << std::endl;
    ok &= Check(WriteBytes(
        bundlePath / "project.stack",
        std::vector<unsigned char> { '{', 'b', 'a', 'd' }),
        "current bundle manifest could not be corrupted for recovery test");
    ProjectStoreOpenResult recovered = OpenProjectStore(bundlePath);
    ok &= Check(static_cast<bool>(recovered) && recovered.recoveredPreviousManifest &&
        recovered.store->IsReadOnlyRecovery(),
        "invalid current bundle manifest did not open the retained previous manifest read-only");
    if (recovered) {
        ProjectStoreTransaction blocked = recovered.store->BeginTransaction(
            recovered.snapshot.persistedStorageRevision);
        ProjectStoreCommitResult blockedCommit = recovered.store->Commit(
            blocked, recovered.snapshot);
        ok &= Check(blockedCommit.status == ProjectStoreCommitStatus::ReadOnlyRecovery,
            "recovered bundle allowed an in-place commit");
        recovered.store->Abort(blocked);

        const std::filesystem::path storedAsset =
            bundlePath / assetA.projectAssetPath;
        std::fstream corruptAsset(
            storedAsset, std::ios::binary | std::ios::in | std::ios::out);
        char changedByte = 0;
        corruptAsset.read(&changedByte, 1);
        changedByte ^= static_cast<char>(0x5a);
        corruptAsset.clear();
        corruptAsset.seekp(0, std::ios::beg);
        corruptAsset.write(&changedByte, 1);
        corruptAsset.close();
        std::vector<std::string> checksumErrors;
        ok &= Check(!reopened.store->Verify(
                reopened.snapshot, &checksumErrors) && !checksumErrors.empty(),
            "embedded-original checksum corruption was not detected");
    }
    return ok;
}

} // namespace

bool ValidateMultiSourceProjectFoundation() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("stack-multi-source-validation-" + Stack::Project::GenerateStableUuid());
    ScopedValidationDirectory cleanup(root);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (!Check(!error, "validation directory could not be created: " + error.message())) {
        return false;
    }

    bool ok = true;
    std::cout << "[multi-source] model rules" << std::endl;
    ok &= ValidateUnboundedModelAndRules();
    std::cout << "[multi-source] measurement graph" << std::endl;
    ok &= ValidateMultiFrameMeasurementGraph();
    std::cout << "[multi-source] graph binding" << std::endl;
    ok &= ValidateGraphBindingRoundTrip();
    std::cout << "[multi-source] project-aware gallery" << std::endl;
    ok &= ValidateProjectAwareGalleryPresentation();
    std::cout << "[multi-source] MFD model and graph contract" << std::endl;
    ok &= ValidateMfdModelContract();
    ok &= ValidateMfdGraphContract();
    ok &= ValidateHdrGraphContract();
    std::cout << "[multi-source] session ordering" << std::endl;
    ok &= ValidateSessionOrdering();
    std::cout << "[multi-source] project stores" << std::endl;
    ok &= ValidateStores(root);
    if (ok) {
        std::cout << "Multi-source RAW project foundation validation passed." << std::endl;
    }
    return ok;
}

bool ValidateMfdProjectFoundation() {
    bool ok = true;
    std::cout << "[mfd-foundation] mosaiced RAW project model" << std::endl;
    ok &= ValidateMfdModelContract();
    std::cout << "[mfd-foundation] managed graph and View contract" << std::endl;
    ok &= ValidateMfdGraphContract();
    if (ok) {
        std::cout << "MFD project foundation validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
