#include "App/Validation/ValidationSuites.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Persistence/ProjectSessionController.h"
#include "Persistence/ProjectStore.h"
#include "Raw/RawTechnicalEvidence.h"

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

Stack::Project::RawProjectSnapshot BuildBootstrap() {
    Stack::Project::RawProjectSnapshot snapshot;
    snapshot.projectId = Stack::Project::GenerateStableUuid();
    snapshot.projectName = "Validation Multi-Frame Project";
    snapshot.rawWorkspaceData = {
        { "schema", "stack.rawWorkspace.project" },
        { "rawWorkspaceSchemaVersion", 3 },
        { "rawProjectModel", Stack::Project::kRawProjectModelSourceSets }
    };
    snapshot.unknownFields["futureProjectField"] = {
        { "preserve", true }
    };
    snapshot.metadataUnknownFields["futureMetadataField"] = 17;
    snapshot.uiStateUnknownFields["futureUiField"] = "preserve";
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
        asset.originalFileName = "mfd-frame-" + std::to_string(index + 1u) + ".dng";
        asset.originalExtension = ".dng";
        asset.inputFamily = MultiFrameInputFamily::Raw;
        RawCaptureCompatibilitySummary summary = BuildMosaicSummary();
        summary.captureTimestamp += static_cast<std::int64_t>(index);
        asset.captureMetadataSummary = SerializeRawCaptureCompatibilitySummary(summary);
        snapshot.embeddedAssets.push_back(asset);

        SourceSetFrame frame;
        frame.frameId = GenerateStableUuid();
        frame.assetId = asset.assetId;
        frame.userLabel = asset.originalFileName;
        sourceSet.frames.push_back(std::move(frame));
    }
    sourceSet.referenceFrameId = sourceSet.frames.front().frameId;
    snapshot.activeSourceSetId = sourceSet.sourceSetId;
    snapshot.activeFrameId = sourceSet.frames.back().frameId;
    snapshot.mfdInputRevision = 7u;
    snapshot.postRecipeRevision = 3u;
    snapshot.sourceSets.push_back(std::move(sourceSet));
    return snapshot;
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
        asset.originalFileName = "frame-" + std::to_string(index) + ".dng";
        asset.originalExtension = ".dng";
        asset.inputFamily = MultiFrameInputFamily::Raw;
        snapshot.embeddedAssets.push_back(asset);

        SourceSetFrame frame;
        frame.frameId = GenerateStableUuid();
        frame.assetId = asset.assetId;
        frame.userLabel = asset.originalFileName;
        set.frames.push_back(std::move(frame));
    }
    set.referenceFrameId = set.frames[317u].frameId;
    snapshot.activeSourceSetId = set.sourceSetId;
    snapshot.sourceSets.push_back(set);

    bool ok = true;
    ok &= Check(ValidateRawProjectSnapshot(snapshot).valid,
        "an arbitrary-length ordered set with a >4 GB 64-bit asset was rejected");
    const nlohmann::json serialized = SerializeRawProjectSnapshot(snapshot);
    RawProjectSnapshot roundTrip;
    std::string error;
    ok &= Check(DeserializeRawProjectSnapshot(serialized, roundTrip, &error),
        "large model JSON did not deserialize: " + error);
    ok &= Check(roundTrip.embeddedAssets.front().byteLength ==
        snapshot.embeddedAssets.front().byteLength,
        "64-bit byte length did not survive serialization");
    ok &= Check(roundTrip.sourceSets.front().frames[317u].frameId ==
        roundTrip.sourceSets.front().referenceFrameId,
        "ordering/reference identity did not survive serialization");
    ok &= Check(roundTrip.metadataUnknownFields == snapshot.metadataUnknownFields &&
        roundTrip.uiStateUnknownFields == snapshot.uiStateUnknownFields,
        "nested metadata or UI-state unknown fields did not survive serialization");

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

    RawProjectSnapshot legacyMfd = BuildMfdSnapshot();
    legacyMfd.sourceSets.front().operationSchemaVersion =
        kMfdMosaicPlaceholderSchemaVersion;
    legacyMfd.sourceSets.front().settings = {
        { "schemaVersion", kMfdMosaicPlaceholderSchemaVersion },
        { "inputDomain", "mosaic-cfa" },
        { "algorithmVersion", nullptr },
        { "sharedPostMfdRecipe", nlohmann::json::object() },
        { "viewTransformPlacement", "internal" },
        { "processingImplemented", false }
    };
    ok &= Check(ValidateRawProjectSnapshot(legacyMfd).valid,
        "schema-2 non-processing MFD projects no longer open unchanged");
    return ok;
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

    nlohmann::json legacyManaged = serialized;
    legacyManaged["nodeGraph"]["nodes"][0]["definition"] = {
        { "id", "" }, { "version", "" }, { "contentHash", "" }
    };
    EditorNodeGraph::Graph migratedManaged;
    EditorNodeGraph::DeserializeGraphPayload(
        legacyManaged, migratedManaged, 0, {}, 0, 0, 0);
    const auto migrated = std::find_if(
        migratedManaged.GetNodes().begin(),
        migratedManaged.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) {
            return node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet;
        });
    ok &= Check(migrated != migratedManaged.GetNodes().end() &&
            migrated->definitionResolved,
        "legacy managed source-set node with an empty identity was not migrated exactly");
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

    nlohmann::json legacyManaged = serialized;
    for (nlohmann::json& item : legacyManaged["nodeGraph"]["nodes"]) {
        const std::string kind = item.value("kind", std::string());
        if (kind == "RawProjectFrame" ||
            kind == "MultiFrameDenoise" ||
            kind == "RawProjectSourceSet") {
            item["definition"] = {
                { "id", "" }, { "version", "" }, { "contentHash", "" }
            };
        }
    }
    EditorNodeGraph::Graph migratedManaged;
    EditorNodeGraph::DeserializeGraphPayload(
        legacyManaged, migratedManaged, 0, {}, 0, 0, 0);
    ok &= Check(std::all_of(
            migratedManaged.GetNodes().begin(),
            migratedManaged.GetNodes().end(),
            [](const EditorNodeGraph::Node& node) {
                if (node.kind != EditorNodeGraph::NodeKind::RawProjectFrame &&
                    node.kind != EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                    node.kind != EditorNodeGraph::NodeKind::RawProjectSourceSet) {
                    return true;
                }
                return node.definitionResolved &&
                    !node.definitionId.empty() &&
                    !node.definitionVersion.empty() &&
                    !node.definitionHash.empty();
            }),
        "legacy managed RAW nodes with the formerly empty identity were not migrated exactly");
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
    const std::filesystem::path cancelledPath = root / "cancelled.stackbundle";
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

    const std::filesystem::path bundlePath = root / "roundtrip.stackbundle";
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
    ok &= Check(created.store->StageAssetFile(
        transaction, rawAPath, MultiFrameInputFamily::Raw,
        nlohmann::json::object(), assetA, &error), "asset A staging failed: " + error);
    std::cout << "[multi-source/store] asset A staged" << std::endl;
    ok &= Check(created.store->StageAssetFile(
        transaction, rawACopyPath, MultiFrameInputFamily::Raw,
        nlohmann::json::object(), assetACopy, &error), "duplicate asset staging failed: " + error);
    std::cout << "[multi-source/store] duplicate staged" << std::endl;
    ok &= Check(assetA.assetId == assetACopy.assetId,
        "content-addressed staging did not deduplicate identical originals");
    ok &= Check(created.store->StageAssetFile(
        transaction, rawBPath, MultiFrameInputFamily::Raw,
        nlohmann::json::object(), assetB, &error), "asset B staging failed: " + error);
    std::cout << "[multi-source/store] asset B staged" << std::endl;

    snapshot.embeddedAssets = { assetA, assetB };
    MultiFrameSourceSet first;
    first.sourceSetId = GenerateStableUuid();
    first.name = "Primary Burst";
    first.inputFamily = MultiFrameInputFamily::Raw;
    first.operationIntent = MultiFrameOperationIntent::RawBurstDenoise;
    first.graphBindingNodeId = GenerateStableUuid();
    first.frames = {
        { GenerateStableUuid(), assetA.assetId, true, "A", nlohmann::json::object(), nlohmann::json::object() },
        { GenerateStableUuid(), assetB.assetId, true, "B", nlohmann::json::object(), nlohmann::json::object() }
    };
    first.referenceFrameId = first.frames[1].frameId;
    MultiFrameSourceSet second;
    second.sourceSetId = GenerateStableUuid();
    second.name = "Cross-set reuse draft";
    second.inputFamily = MultiFrameInputFamily::Raw;
    second.operationIntent = MultiFrameOperationIntent::Mfsr;
    second.graphBindingNodeId = GenerateStableUuid();
    second.frames = {
        { GenerateStableUuid(), assetA.assetId, true, "A reused", nlohmann::json::object(), nlohmann::json::object() }
    };
    second.referenceFrameId = second.frames.front().frameId;
    snapshot.sourceSets = { first, second };
    snapshot.activeSourceSetId = first.sourceSetId;
    snapshot.pipelineData["managedBindings"] = { first.graphBindingNodeId, second.graphBindingNodeId };
    snapshot.rawWorkspaceData["futureWorkspaceField"] = "preserve";
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

    snapshot.rawWorkspaceData["followupSetting"] = "identity";
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
    std::vector<std::string> verificationErrors;
    ok &= Check(reopened.store->Verify(reopened.snapshot, &verificationErrors),
        verificationErrors.empty() ? "directory verification failed" : verificationErrors.front());

    const std::filesystem::path portablePath = root / "roundtrip.stack";
    std::cout << "[multi-source/store] convert portable" << std::endl;
    ProjectStoreOpenResult portable = ConvertProjectStore(
        reopened.store, reopened.snapshot, portablePath, ProjectStorageKind::PortableFile);
    ok &= Check(static_cast<bool>(portable), "bundle to portable conversion failed: " + portable.message);
    if (!portable) return false;
    ok &= Check(LogicalSnapshotJson(portable.snapshot) == LogicalSnapshotJson(reopened.snapshot),
        "portable conversion changed logical project content or unknown fields");
    ok &= Check(ReadStream(portable.store->OpenAssetStream(assetB.assetId, &error)) == rawB,
        "portable store did not stream exact original bytes");

    std::cout << "[multi-source/store] conflict revisions" << std::endl;
    ProjectStoreTransaction firstSave = portable.store->BeginTransaction(
        portable.snapshot.persistedStorageRevision);
    ProjectStoreTransaction staleSave = portable.store->BeginTransaction(
        portable.snapshot.persistedStorageRevision);
    RawProjectSnapshot updated = portable.snapshot;
    updated.dirtyRevision += 1u;
    updated.rawWorkspaceData["saveGeneration"] = 1;
    ProjectStoreCommitResult firstSaveResult = portable.store->Commit(firstSave, updated);
    ok &= Check(static_cast<bool>(firstSaveResult),
        "first overlapping save failed: " + firstSaveResult.message);
    updated.rawWorkspaceData["saveGeneration"] = 2;
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
        ok &= Check(afterInterruptedAppend.snapshot.rawWorkspaceData.value(
            "saveGeneration", 0) == 1,
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
    const std::filesystem::path convertedBackPath = root / "converted-back.stackbundle";
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
        bundlePath / "project.stackmanifest",
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

        const std::filesystem::path storedAsset = bundlePath / "media" /
            (assetA.sha256 + "-" + std::to_string(assetA.byteLength) + ".original");
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
    std::cout << "[multi-source] graph binding" << std::endl;
    ok &= ValidateGraphBindingRoundTrip();
    std::cout << "[multi-source] MFD model and graph contract" << std::endl;
    ok &= ValidateMfdModelContract();
    ok &= ValidateMfdGraphContract();
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
