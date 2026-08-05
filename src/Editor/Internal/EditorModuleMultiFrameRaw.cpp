#include "Editor/EditorModule.h"

#include "App/PlatformHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Raw/MultiFrameDenoise/Contracts.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Utils/FileDialogs.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

using Stack::Project::EmbeddedAssetRecord;
using Stack::Project::MultiFrameInputFamily;
using Stack::Project::MultiFrameOperationIntent;
using Stack::Project::MultiFrameSourceSet;
using Stack::Project::RawCaptureCompatibilitySummary;
using Stack::Project::RawProjectSnapshot;
using Stack::Project::SourceSetFrame;

bool Finish(std::string* output, const std::string& message, bool result) {
    if (output) *output = message;
    return result;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string FormatMfdElapsed(std::chrono::steady_clock::duration duration) {
    const auto totalSeconds = std::max<std::int64_t>(
        0,
        std::chrono::duration_cast<std::chrono::seconds>(duration).count());
    const std::int64_t hours = totalSeconds / 3600;
    const std::int64_t minutes = (totalSeconds % 3600) / 60;
    const std::int64_t seconds = totalSeconds % 60;
    std::ostringstream output;
    if (hours > 0) output << hours << "h ";
    if (hours > 0 || minutes > 0) output << minutes << "m ";
    output << seconds << "s";
    return output.str();
}

bool ClassifySourcePath(
    const std::filesystem::path& path,
    MultiFrameInputFamily& family,
    std::string& error) {
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError) || filesystemError) {
        error = "A selected frame is not a readable regular file: " + path.string();
        return false;
    }
    if (Raw::RawLoader::IsRawPath(path.string())) {
        family = MultiFrameInputFamily::Raw;
        return true;
    }
    const std::string extension = Lower(path.extension().string());
    static const std::unordered_set<std::string> kSupportedRasterExtensions {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga"
    };
    if (kSupportedRasterExtensions.find(extension) != kSupportedRasterExtensions.end()) {
        family = MultiFrameInputFamily::Raster;
        return true;
    }
    error = "The selected file is neither a supported RAW nor raster frame: " +
        path.filename().string();
    return false;
}

RawCaptureCompatibilitySummary BuildMfdCaptureSummary(
    const Raw::RawMetadata& metadata) {
    return Stack::Project::BuildRawCaptureCompatibilitySummary(metadata);
}

bool ProbeMfdSources(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet,
    const std::vector<std::filesystem::path>& sourcePaths,
    std::vector<nlohmann::json>& captureSummaries,
    std::string& error) {
    captureSummaries.clear();
    captureSummaries.reserve(sourcePaths.size());
    RawCaptureCompatibilitySummary reference;
    bool haveReference = false;
    for (const SourceSetFrame& frame : sourceSet.frames) {
        const EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
        if (asset && Stack::Project::DeserializeRawCaptureCompatibilitySummary(
                asset->captureMetadataSummary, reference, nullptr)) {
            haveReference = true;
            break;
        }
    }
    std::unordered_set<std::string> normalizedPaths;
    for (const std::filesystem::path& path : sourcePaths) {
        std::error_code pathError;
        const std::filesystem::path normalized =
            std::filesystem::weakly_canonical(path, pathError);
        const std::string pathKey = Lower(
            (pathError ? path.lexically_normal() : normalized).string());
        if (!normalizedPaths.insert(pathKey).second) {
            error = "The same source file cannot be selected twice for one MFD burst.";
            return false;
        }
        Raw::RawMetadata metadata;
        if (!Raw::RawLoader::LoadMetadata(path.string(), metadata)) {
            error = metadata.error.empty()
                ? "The RAW header could not be inspected: " + path.filename().string()
                : path.filename().string() + ": " + metadata.error;
            return false;
        }
        RawCaptureCompatibilitySummary candidate = BuildMfdCaptureSummary(metadata);
        if (!candidate.supported) {
            error = path.filename().string() + ": " + candidate.rejectionReason;
            return false;
        }
        if (haveReference) {
            std::string compatibilityReason;
            if (!Stack::Project::AreMfdCapturesStructurallyCompatible(
                    reference, candidate, &compatibilityReason)) {
                error = path.filename().string() + ": " + compatibilityReason;
                return false;
            }
        } else {
            reference = candidate;
            haveReference = true;
        }
        captureSummaries.push_back(
            Stack::Project::SerializeRawCaptureCompatibilitySummary(candidate));
    }
    return true;
}

nlohmann::json DefaultMfdSettings() {
    return Stack::Project::MakeDefaultMfdOperationSettings();
}

void EnsureMfdPostRecipe(MultiFrameSourceSet& sourceSet) {
    const auto existing = sourceSet.settings.find("sharedPostMfdRecipe");
    if (existing != sourceSet.settings.end() &&
        existing->is_object() &&
        existing->contains("rawRecipeVersion")) {
        return;
    }
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(
            "mfd://source-set/" + sourceSet.sourceSetId,
            sourceSet.name + " developed result");
    // The upstream MFD result has already completed CFA-domain denoising.
    // Leave the ordinary single-frame mosaic denoiser off by default so it
    // cannot silently process the fused result a second time.
    recipe.technical.mosaicDenoise.enabled = false;
    sourceSet.settings["sharedPostMfdRecipe"] =
        Stack::RawRecipe::SerializeRecipe(recipe);
}

bool StageFrames(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    RawProjectSnapshot& snapshot,
    MultiFrameSourceSet& sourceSet,
    const std::vector<std::filesystem::path>& sourcePaths,
    bool requireExistingFamily,
    std::string& error) {
    if (!store || !transaction) {
        error = "The project store transaction is unavailable.";
        return false;
    }
    std::vector<nlohmann::json> captureSummaries;
    if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !ProbeMfdSources(
            snapshot, sourceSet, sourcePaths, captureSummaries, error)) {
        return false;
    }
    std::unordered_set<std::string> setAssetIds;
    for (const SourceSetFrame& frame : sourceSet.frames) {
        setAssetIds.insert(frame.assetId);
    }
    for (std::size_t sourceIndex = 0; sourceIndex < sourcePaths.size(); ++sourceIndex) {
        const std::filesystem::path& path = sourcePaths[sourceIndex];
        MultiFrameInputFamily family;
        if (!ClassifySourcePath(path, family, error)) return false;
        if ((!sourceSet.frames.empty() || requireExistingFamily) &&
            family != sourceSet.inputFamily) {
            error = "A source set cannot mix RAW and raster frames.";
            return false;
        }
        if (sourceSet.frames.empty() && !requireExistingFamily) {
            sourceSet.inputFamily = family;
        }
        EmbeddedAssetRecord asset;
        const nlohmann::json captureSummary =
            sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise
            ? captureSummaries[sourceIndex]
            : nlohmann::json::object();
        if (!store->StageAssetFile(
                transaction,
                path,
                family,
                captureSummary,
                asset,
                &error)) {
            return false;
        }
        if (!setAssetIds.insert(asset.assetId).second) {
            error = "The same original cannot appear twice in one source set.";
            return false;
        }
        if (!Stack::Project::FindEmbeddedAsset(snapshot, asset.assetId)) {
            snapshot.embeddedAssets.push_back(asset);
        }
        SourceSetFrame frame;
        frame.frameId = Stack::Project::GenerateStableUuid();
        frame.assetId = asset.assetId;
        frame.userLabel = path.filename().string();
        sourceSet.frames.push_back(std::move(frame));
    }
    if (sourceSet.referenceFrameId.empty() && !sourceSet.frames.empty()) {
        sourceSet.referenceFrameId = sourceSet.frames.front().frameId;
    }
    return true;
}

nlohmann::json PipelineForGraph(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers,
    const nlohmann::json& existingPipeline) {
    nlohmann::json layerArray = nlohmann::json::array();
    for (const std::shared_ptr<LayerBase>& layer : layers) {
        if (layer) layerArray.push_back(layer->Serialize());
    }
    nlohmann::json pipeline =
        EditorNodeGraph::SerializeGraphPayload(layerArray, graph);
    if (existingPipeline.is_object()) {
        for (const char* key : { "editorComposite", "editorTimeline" }) {
            if (existingPipeline.contains(key)) pipeline[key] = existingPipeline[key];
        }
    }
    return pipeline;
}

EditorNodeGraph::Node* FindSourceSetNode(
    EditorNodeGraph::Graph& graph,
    const std::string& sourceSetId,
    const std::string& graphBindingNodeId = {}) {
    if (!graphBindingNodeId.empty()) {
        for (EditorNodeGraph::Node& node : graph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
                node.instanceUuid == graphBindingNodeId) {
                return &node;
            }
        }
    }
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
            node.rawProjectSourceSet.sourceSetId == sourceSetId &&
            node.rawProjectSourceSet.managed &&
            !node.rawProjectSourceSet.quarantined) {
            return &node;
        }
    }
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
            node.rawProjectSourceSet.sourceSetId == sourceSetId) {
            return &node;
        }
    }
    return nullptr;
}

const EditorNodeGraph::Node* FindSourceSetNode(
    const EditorNodeGraph::Graph& graph,
    const std::string& sourceSetId,
    const std::string& graphBindingNodeId = {}) {
    return FindSourceSetNode(
        const_cast<EditorNodeGraph::Graph&>(graph),
        sourceSetId,
        graphBindingNodeId);
}

EditorNodeGraph::Node* FindMfdNode(
    EditorNodeGraph::Graph& graph,
    const std::string& sourceSetId,
    const std::string& graphBindingNodeId = {}) {
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::MultiFrameDenoise) continue;
        if ((!graphBindingNodeId.empty() &&
             node.instanceUuid == graphBindingNodeId) ||
            node.multiFrameDenoise.sourceSetId == sourceSetId) {
            return &node;
        }
    }
    return nullptr;
}

EditorNodeGraph::Node* FindMfdFrameNode(
    EditorNodeGraph::Graph& graph,
    const std::string& sourceSetId,
    const std::string& frameId) {
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
            node.rawProjectFrame.sourceSetId == sourceSetId &&
            node.rawProjectFrame.frameId == frameId &&
            node.rawProjectFrame.managed &&
            !node.rawProjectFrame.quarantined) {
            return &node;
        }
    }
    return nullptr;
}

EditorNodeGraph::MfdFrameBinding BuildMfdFrameBinding(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet,
    const SourceSetFrame& frame) {
    EditorNodeGraph::MfdFrameBinding binding;
    binding.frameId = frame.frameId;
    binding.socketId = EditorNodeGraph::MfdFrameInputSocketId(frame.frameId);
    const EmbeddedAssetRecord* asset =
        Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
    binding.label = !frame.userLabel.empty()
        ? frame.userLabel
        : (asset ? asset->originalFileName : std::string("RAW Frame"));
    binding.enabled = frame.enabled;
    binding.reference = sourceSet.referenceFrameId == frame.frameId;
    return binding;
}

void MarkManagedMfdLink(
    EditorNodeGraph::Graph& graph,
    int frameNodeId,
    int mfdNodeId,
    const std::string& socketId,
    const std::string& frameId) {
    for (EditorNodeGraph::Link& link : graph.EditLinks()) {
        if (link.fromNodeId == frameNodeId &&
            link.fromSocketId == EditorNodeGraph::kRawOutputSocketId &&
            link.toNodeId == mfdNodeId &&
            link.toSocketId == socketId) {
            link.ownership =
                EditorNodeGraph::Link::Ownership::ManagedSourceBinding;
            link.bindingId = frameId;
            return;
        }
    }
}

bool SyncMfdGraphTopology(
    EditorNodeGraph::Graph& graph,
    const RawProjectSnapshot& snapshot,
    MultiFrameSourceSet& sourceSet,
    bool ensureOutput,
    std::string& error) {
    EditorNodeGraph::Node* mfd = FindMfdNode(
        graph, sourceSet.sourceSetId, sourceSet.graphBindingNodeId);
    if (!mfd) {
        EditorNodeGraph::MultiFrameDenoisePayload payload;
        payload.sourceSetId = sourceSet.sourceSetId;
        mfd = graph.AddMultiFrameDenoiseNode(
            std::move(payload), { 760.0f, 180.0f });
        if (!mfd) {
            error = "Could not create the managed MFD graph node.";
            return false;
        }
    }
    mfd->title = "MFD - " + sourceSet.name;
    mfd->multiFrameDenoise.sourceSetId = sourceSet.sourceSetId;
    mfd->multiFrameDenoise.managed = true;
    mfd->multiFrameDenoise.quarantined = false;
    mfd->multiFrameDenoise.presentationStatus =
        EditorNodeGraph::kMfdAwaitingProcessingStatus;
    mfd->multiFrameDenoise.resultState = "unavailable";
    mfd->multiFrameDenoise.internalViewTransformEnabled =
        sourceSet.settings.value(
            "viewTransformPlacement", std::string("internal")) != "graph";
    mfd->multiFrameDenoise.frameBindings.clear();
    sourceSet.graphBindingNodeId = mfd->instanceUuid;
    const int mfdNodeId = mfd->id;

    std::unordered_set<std::string> currentFrameIds;
    for (std::size_t index = 0; index < sourceSet.frames.size(); ++index) {
        const SourceSetFrame& frame = sourceSet.frames[index];
        currentFrameIds.insert(frame.frameId);
        EditorNodeGraph::Node* frameNode = FindMfdFrameNode(
            graph, sourceSet.sourceSetId, frame.frameId);
        if (!frameNode) {
            EditorNodeGraph::RawProjectFramePayload payload;
            payload.sourceSetId = sourceSet.sourceSetId;
            payload.frameId = frame.frameId;
            payload.assetId = frame.assetId;
            payload.displayLabel = frame.userLabel;
            const float column = static_cast<float>(index % 3u);
            const float row = static_cast<float>(index / 3u);
            frameNode = graph.AddRawProjectFrameNode(
                std::move(payload),
                { 40.0f + column * 220.0f, 70.0f + row * 170.0f });
            if (!frameNode) {
                error = "Could not create a managed RAW frame graph node.";
                return false;
            }
        }
        frameNode->rawProjectFrame.assetId = frame.assetId;
        frameNode->rawProjectFrame.displayLabel = frame.userLabel;
        frameNode->rawProjectFrame.enabled = frame.enabled;
        frameNode->rawProjectFrame.reference =
            sourceSet.referenceFrameId == frame.frameId;
        frameNode->rawProjectFrame.compatibilityStatus =
            frame.enabled ? "Mosaic CFA - embedded" : "Excluded from MFD";
        frameNode->title = frame.userLabel.empty() ? "RAW Frame" : frame.userLabel;
        mfd = graph.FindNode(mfdNodeId);
        if (!mfd) {
            error = "The managed MFD node disappeared while building the graph.";
            return false;
        }
        mfd->multiFrameDenoise.frameBindings.push_back(
            BuildMfdFrameBinding(snapshot, sourceSet, frame));
    }

    std::vector<int> staleFrameNodes;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
            node.rawProjectFrame.sourceSetId == sourceSet.sourceSetId &&
            node.rawProjectFrame.managed &&
            !node.rawProjectFrame.quarantined &&
            currentFrameIds.find(node.rawProjectFrame.frameId) ==
                currentFrameIds.end()) {
            staleFrameNodes.push_back(node.id);
        }
    }
    for (int nodeId : staleFrameNodes) graph.RemoveNode(nodeId);

    std::vector<EditorNodeGraph::Link>& links = graph.EditLinks();
    links.erase(
        std::remove_if(
            links.begin(), links.end(),
            [&](const EditorNodeGraph::Link& link) {
                return link.toNodeId == mfdNodeId &&
                    link.ownership ==
                        EditorNodeGraph::Link::Ownership::ManagedSourceBinding;
            }),
        links.end());
    for (const SourceSetFrame& frame : sourceSet.frames) {
        EditorNodeGraph::Node* frameNode = FindMfdFrameNode(
            graph, sourceSet.sourceSetId, frame.frameId);
        if (!frameNode) continue;
        const std::string socketId =
            EditorNodeGraph::MfdFrameInputSocketId(frame.frameId);
        if (!graph.TryConnectSockets(
                frameNode->id,
                EditorNodeGraph::kRawOutputSocketId,
                mfdNodeId,
                socketId,
                &error)) {
            return false;
        }
        MarkManagedMfdLink(
            graph, frameNode->id, mfdNodeId, socketId, frame.frameId);
    }

    if (ensureOutput) {
        EditorNodeGraph::Node* output = graph.EnsureOutputNode();
        if (!output) {
            error = "Could not create the project Output node.";
            return false;
        }
        if (!graph.FindInputLink(output->id, EditorNodeGraph::kImageInputSocketId) &&
            !graph.TryConnectSockets(
                mfdNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                output->id,
                EditorNodeGraph::kImageInputSocketId,
                &error)) {
            return false;
        }
    }
    return true;
}

void UpdateWorkspaceManifestFields(RawProjectSnapshot& snapshot) {
    if (!snapshot.rawWorkspaceData.is_object()) {
        snapshot.rawWorkspaceData = nlohmann::json::object();
    }
    snapshot.rawWorkspaceData["schema"] = "stack.rawWorkspace.project";
    snapshot.rawWorkspaceData["schemaVersion"] = 3;
    snapshot.rawWorkspaceData["rawWorkspaceSchemaVersion"] = 3;
    snapshot.rawWorkspaceData["rawProjectModel"] =
        Stack::Project::kRawProjectModelSourceSets;
    snapshot.rawWorkspaceData["projectId"] = snapshot.projectId;
    snapshot.rawWorkspaceData["activeSourceSetId"] = snapshot.activeSourceSetId;
    snapshot.rawWorkspaceData["activeFrameId"] = snapshot.activeFrameId;
}

} // namespace

bool EditorModule::CreateMultiFrameRawProject(
    const std::filesystem::path& requestedPath,
    Stack::Project::ProjectStorageKind storageKind,
    const std::string& projectName,
    const std::string& sourceSetName,
    MultiFrameOperationIntent operationIntent,
    const std::vector<std::filesystem::path>& sourcePaths,
    std::size_t referenceFrameIndex,
    std::string* errorMessage) {
    if (requestedPath.empty() || projectName.empty() || sourceSetName.empty()) {
        return Finish(errorMessage, "Project path, project name, and source-set name are required.", false);
    }
    if (m_Dirty && !m_RawWorkspaceReplacementAuthorized) {
        return Finish(errorMessage, "Save or discard the current project before replacing it.", false);
    }

    std::filesystem::path path = requestedPath;
    const std::string requiredExtension =
        storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle
        ? ".stackbundle"
        : ".stack";
    if (Lower(path.extension().string()) != requiredExtension) {
        path += requiredExtension;
    }
    std::error_code filesystemError;
    if (std::filesystem::exists(path, filesystemError)) {
        return Finish(errorMessage, "A project already exists at that path.", false);
    }

    RawProjectSnapshot bootstrap;
    bootstrap.projectId = Stack::Project::GenerateStableUuid();
    bootstrap.projectName = projectName;
    bootstrap.pipelineData = nlohmann::json::object();
    UpdateWorkspaceManifestFields(bootstrap);
    Stack::Project::ProjectStoreOpenResult created =
        Stack::Project::CreateProjectStore(path, storageKind, bootstrap);
    if (!created) return Finish(errorMessage, created.message, false);

    const auto cleanupCreatedStore = [&]() {
        created.store.reset();
        std::error_code cleanupError;
        if (storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle) {
            std::filesystem::remove_all(path, cleanupError);
        } else {
            std::filesystem::remove(path, cleanupError);
            std::filesystem::path staging = path;
            staging += ".staging";
            std::filesystem::remove_all(staging, cleanupError);
        }
    };

    RawProjectSnapshot snapshot = created.snapshot;
    MultiFrameSourceSet sourceSet;
    sourceSet.sourceSetId = Stack::Project::GenerateStableUuid();
    sourceSet.name = sourceSetName;
    sourceSet.operationIntent = operationIntent;
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (sourcePaths.size() < 2u) {
            cleanupCreatedStore();
            return Finish(errorMessage, "Select at least two RAW frames for an MFD project.", false);
        }
        sourceSet.operationSchemaVersion = Stack::Project::kMfdOperationSchemaVersion;
        sourceSet.settings = DefaultMfdSettings();
        EnsureMfdPostRecipe(sourceSet);
    }

    const Stack::Project::ProjectStoreTransaction transaction =
        created.store->BeginTransaction(snapshot.persistedStorageRevision);
    if (!transaction) {
        cleanupCreatedStore();
        return Finish(errorMessage, "Could not begin source ingestion.", false);
    }
    std::string error;
    if (!StageFrames(
            created.store,
            transaction,
            snapshot,
            sourceSet,
            sourcePaths,
            false,
            error)) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return Finish(errorMessage, error, false);
    }
    if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return Finish(errorMessage, "Burst Denoise accepts RAW source sets only.", false);
    }
    if (!sourceSet.frames.empty()) {
        const std::size_t clampedReference = std::min(
            referenceFrameIndex, sourceSet.frames.size() - 1u);
        sourceSet.referenceFrameId = sourceSet.frames[clampedReference].frameId;
    }

    EditorNodeGraph::Graph graph;
    graph.Clear();
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (!SyncMfdGraphTopology(graph, snapshot, sourceSet, true, error)) {
            created.store->Abort(transaction);
            cleanupCreatedStore();
            return Finish(errorMessage, error, false);
        }
    } else {
        EditorNodeGraph::RawProjectSourceSetPayload payload;
        payload.sourceSetId = sourceSet.sourceSetId;
        EditorNodeGraph::Node* node = graph.AddRawProjectSourceSetNode(
            std::move(payload), { 80.0f, 120.0f });
        if (!node) {
            created.store->Abort(transaction);
            cleanupCreatedStore();
            return Finish(errorMessage, "Could not create the managed source-set graph node.", false);
        }
        node->title = "RAW Project Source Set - " + sourceSet.name;
        sourceSet.graphBindingNodeId = node->instanceUuid;
    }
    snapshot.sourceSets.push_back(std::move(sourceSet));
    snapshot.activeSourceSetId = snapshot.sourceSets.front().sourceSetId;
    snapshot.activeFrameId = snapshot.sourceSets.front().referenceFrameId;
    snapshot.mfdInputRevision =
        operationIntent == MultiFrameOperationIntent::RawBurstDenoise ? 1u : 0u;
    snapshot.dirtyRevision = 1;
    snapshot.pipelineData = PipelineForGraph(graph, {}, nlohmann::json::object());
    UpdateWorkspaceManifestFields(snapshot);
    const Stack::Project::ProjectStoreCommitResult commit =
        created.store->Commit(transaction, snapshot);
    if (!commit) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return Finish(errorMessage, commit.message, false);
    }
    snapshot.persistedStorageRevision = commit.committedStorageRevision;

    auto loaded = std::make_shared<LoadedProjectData>();
    loaded->sourcePixels.assign(4, 0);
    loaded->width = 1;
    loaded->height = 1;
    loaded->channels = 4;
    loaded->pipelineData = snapshot.pipelineData;
    loaded->rawWorkspaceData = snapshot.rawWorkspaceData;
    loaded->projectKind = StackBinaryFormat::kRawProjectKind;
    loaded->projectName = projectName;
    loaded->projectFileName = path.lexically_normal().string();
    loaded->projectStore = created.store;
    loaded->rawProjectSnapshot =
        std::make_shared<RawProjectSnapshot>(snapshot);
    if (!BeginDeferredLoadedProjectApply(std::move(loaded))) {
        return Finish(
            errorMessage,
            "The project was created safely, but could not be activated in the editor.",
            false);
    }
    return Finish(errorMessage, std::string(), true);
}

bool EditorModule::UpgradeActiveLegacyRawProjectToMultiFrame(
    const std::filesystem::path& requestedDestination,
    std::string* errorMessage) {
    if (!IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive() ||
        m_ActiveRawWorkspaceProjectPath.empty()) {
        return Finish(errorMessage, "An active legacy RAW project is required.", false);
    }
    if (requestedDestination.empty()) {
        return Finish(errorMessage, "Choose a destination for the upgraded copy.", false);
    }

    StackBinaryFormat::ProjectDocument legacy;
    StackBinaryFormat::ProjectLoadOptions options;
    options.includeThumbnail = true;
    options.includeSourceImage = false;
    options.includePipelineData = true;
    options.includeNodeBrowserThumbnails = false;
    options.includeRawWorkspaceData = true;
    if (!StackBinaryFormat::ReadProjectFile(
            m_ActiveRawWorkspaceProjectPath, legacy, options)) {
        return Finish(errorMessage, "The legacy project could not be read for upgrade.", false);
    }

    std::filesystem::path destination = requestedDestination;
    if (Lower(destination.extension().string()) != ".stackbundle") {
        destination += ".stackbundle";
    }
    std::error_code filesystemError;
    if (std::filesystem::exists(destination, filesystemError)) {
        return Finish(errorMessage, "An upgraded project already exists at that path.", false);
    }

    RawProjectSnapshot bootstrap;
    bootstrap.projectId = Stack::Project::GenerateStableUuid();
    bootstrap.projectName = m_CurrentProjectName.empty()
        ? legacy.metadata.projectName + " Upgraded"
        : m_CurrentProjectName + " Upgraded";
    bootstrap.rawWorkspaceData = legacy.rawWorkspaceData.is_object()
        ? legacy.rawWorkspaceData
        : nlohmann::json::object();
    bootstrap.rawWorkspaceData.erase("embeddedRaw");
    bootstrap.rawWorkspaceData.erase("rawSourceRef");
    bootstrap.rawWorkspaceData["legacyUpgrade"] = {
        { "sourceProjectPath", m_ActiveRawWorkspaceProjectPath.string() },
        { "sourceSchemaVersion", legacy.rawWorkspaceData.value(
            "rawWorkspaceSchemaVersion", 1) }
    };
    UpdateWorkspaceManifestFields(bootstrap);
    Stack::Project::ProjectStoreOpenResult created =
        Stack::Project::CreateProjectStore(
            destination,
            Stack::Project::ProjectStorageKind::DirectoryBundle,
            bootstrap);
    if (!created) return Finish(errorMessage, created.message, false);

    const auto cleanupCreatedStore = [&]() {
        created.store.reset();
        std::error_code cleanupError;
        std::filesystem::remove_all(destination, cleanupError);
    };
    RawProjectSnapshot snapshot = created.snapshot;
    const auto transaction = created.store->BeginTransaction(
        snapshot.persistedStorageRevision);
    if (!transaction) {
        cleanupCreatedStore();
        return Finish(errorMessage, "Could not begin the upgrade transaction.", false);
    }

    EmbeddedAssetRecord asset;
    std::string error;
    bool staged = false;
    const nlohmann::json embedded = legacy.rawWorkspaceData.value(
        "embeddedRaw", nlohmann::json::object());
    const auto embeddedBytes = embedded.find("bytes");
    if (embedded.value("present", false) &&
        embeddedBytes != embedded.end() && embeddedBytes->is_binary()) {
        const auto& binary = embeddedBytes->get_binary();
        std::vector<unsigned char> bytes(binary.begin(), binary.end());
        const Stack::RawEvidence::SourceIdentity identity =
            Stack::RawEvidence::ComputeSourceIdentity(bytes);
        if (identity.valid) {
            asset.sha256 = identity.sha256;
            asset.byteLength = identity.byteSize;
            asset.assetId = Stack::Project::MakeAssetId(
                asset.sha256, asset.byteLength);
            asset.originalFileName = embedded.value(
                "fileName", m_ActiveRawWorkspaceRecipe.source.displayName);
            asset.originalExtension =
                std::filesystem::path(asset.originalFileName).extension().string();
            asset.inputFamily = MultiFrameInputFamily::Raw;
            asset.informationalOriginPath =
                m_ActiveRawWorkspaceRecipe.source.sourcePath;
            const std::string byteString(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::istringstream input(
                byteString, std::ios::in | std::ios::binary);
            staged = created.store->StageAssetStream(
                transaction, input, asset, &error);
        }
    } else {
        const std::filesystem::path sourcePath =
            m_ActiveRawWorkspaceRecipe.source.sourcePath;
        if (sourcePath.empty() ||
            !std::filesystem::is_regular_file(sourcePath, filesystemError)) {
            created.store->Abort(transaction);
            cleanupCreatedStore();
            return Finish(
                errorMessage,
                "The linked legacy RAW source is missing. Relink it before upgrading.",
                false);
        }
        staged = created.store->StageAssetFile(
            transaction,
            sourcePath,
            MultiFrameInputFamily::Raw,
            nlohmann::json::object(),
            asset,
            &error);
    }
    if (!staged) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return Finish(errorMessage,
            error.empty() ? "The legacy original could not be embedded." : error,
            false);
    }

    MultiFrameSourceSet sourceSet;
    sourceSet.sourceSetId = Stack::Project::GenerateStableUuid();
    sourceSet.name = "Legacy RAW Source";
    sourceSet.inputFamily = MultiFrameInputFamily::Raw;
    sourceSet.operationIntent = MultiFrameOperationIntent::Mfsr;
    SourceSetFrame frame;
    frame.frameId = Stack::Project::GenerateStableUuid();
    frame.assetId = asset.assetId;
    frame.userLabel = asset.originalFileName;
    sourceSet.frames.push_back(frame);
    sourceSet.referenceFrameId = frame.frameId;

    EditorNodeGraph::Graph graph = m_NodeGraph;
    EditorNodeGraph::RawProjectSourceSetPayload payload;
    payload.sourceSetId = sourceSet.sourceSetId;
    EditorNodeGraph::Node* node = graph.AddRawProjectSourceSetNode(
        std::move(payload), { 80.0f, 120.0f });
    if (!node) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return Finish(errorMessage, "The upgraded graph binding could not be created.", false);
    }
    node->title = "RAW Project Source Set - " + sourceSet.name;
    sourceSet.graphBindingNodeId = node->instanceUuid;
    snapshot.embeddedAssets.push_back(asset);
    snapshot.sourceSets.push_back(std::move(sourceSet));
    snapshot.activeSourceSetId = snapshot.sourceSets.front().sourceSetId;
    snapshot.pipelineData = PipelineForGraph(graph, m_Layers, legacy.pipelineData);
    snapshot.coverThumbnailBytes = legacy.thumbnailBytes;
    snapshot.dirtyRevision = 1u;
    UpdateWorkspaceManifestFields(snapshot);
    const Stack::Project::ProjectStoreCommitResult commit =
        created.store->Commit(transaction, snapshot);
    if (!commit) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return Finish(errorMessage, commit.message, false);
    }
    snapshot.persistedStorageRevision = commit.committedStorageRevision;

    LoadedProjectData loaded;
    loaded.sourcePixels.assign(4, 0);
    loaded.width = 1;
    loaded.height = 1;
    loaded.channels = 4;
    loaded.pipelineData = snapshot.pipelineData;
    loaded.rawWorkspaceData = snapshot.rawWorkspaceData;
    loaded.projectKind = StackBinaryFormat::kRawProjectKind;
    loaded.projectName = snapshot.projectName;
    loaded.projectFileName = destination.lexically_normal().string();
    loaded.projectStore = created.store;
    loaded.rawProjectSnapshot =
        std::make_shared<RawProjectSnapshot>(snapshot);
    if (!ApplyLoadedProject(loaded)) {
        return Finish(
            errorMessage,
            "The upgraded copy was written safely, but could not be activated.",
            false);
    }
    return Finish(errorMessage, std::string(), true);
}

bool EditorModule::CommitActiveMultiFrameMutation(
    RawProjectSnapshot snapshot,
    EditorNodeGraph::Graph graph,
    const Stack::Project::ProjectStoreTransaction& transaction,
    bool importInProgress,
    std::string* outError,
    bool noteEdit) {
    if (!m_ActiveRawProjectStore || !m_ActiveRawProjectSnapshot || !transaction) {
        return Finish(outError, "No multi-frame RAW project transaction is active.", false);
    }
    snapshot.pipelineData = PipelineForGraph(
        graph, m_Layers, SerializePipeline());
    UpdateWorkspaceManifestFields(snapshot);
    const Stack::Project::ModelValidationResult validation =
        Stack::Project::ValidateRawProjectSnapshot(snapshot);
    if (!validation.valid) {
        m_ActiveRawProjectStore->Abort(transaction);
        if (importInProgress) m_ProjectSessionController.CompleteImport(false);
        return Finish(
            outError,
            validation.errors.empty() ? "The source-set change is invalid."
                                      : validation.errors.front(),
            false);
    }
    snapshot.dirtyRevision = noteEdit
        ? m_ProjectSessionController.NoteEdit()
        : m_ProjectSessionController.DirtyRevision();

    Stack::Project::ProjectSaveToken saveToken;
    if (!importInProgress) {
        saveToken = m_ProjectSessionController.BeginSave();
        if (!saveToken) {
            m_ActiveRawProjectStore->Abort(transaction);
            return Finish(outError, "The project is not ready to save this change.", false);
        }
    }
    const Stack::Project::ProjectStoreCommitResult commit =
        m_ActiveRawProjectStore->Commit(transaction, snapshot);
    if (!commit) {
        m_ActiveRawProjectStore->Abort(transaction);
        if (importInProgress) {
            m_ProjectSessionController.CompleteImport(false);
            m_Dirty = m_ProjectSessionController.IsDirty();
        } else {
            m_ProjectSessionController.CompleteSave(
                saveToken,
                false,
                snapshot.persistedStorageRevision,
                commit.status == Stack::Project::ProjectStoreCommitStatus::Conflict);
            // Non-import mutations remain valid in memory when persistence
            // fails, so preserve them for retry or Save Copy. Imports cannot
            // do this because their staged originals were never published.
            m_NodeGraph = std::move(graph);
            m_ActiveRawProjectSnapshot =
                std::make_shared<RawProjectSnapshot>(std::move(snapshot));
            if (m_MfdAdoptedRawResult &&
                (m_MfdAdoptedRawResult->projectId !=
                     m_ActiveRawProjectSnapshot->projectId ||
                 m_MfdAdoptedRawResult->inputRevision !=
                     m_ActiveRawProjectSnapshot->mfdInputRevision ||
                 !Stack::Project::FindSourceSet(
                     *m_ActiveRawProjectSnapshot,
                     m_MfdAdoptedRawResult->sourceSetId))) {
                m_MfdAdoptedRawResult.reset();
            }
            RefreshGraphLayerMetadata();
            ApplyGraphLayerOrder();
            MarkRenderDirty();
            m_Dirty = true;
        }
        return Finish(outError, commit.message, false);
    }

    snapshot.persistedStorageRevision = commit.committedStorageRevision;
    m_NodeGraph = std::move(graph);
    m_ActiveRawProjectSnapshot =
        std::make_shared<RawProjectSnapshot>(std::move(snapshot));
    if (m_MfdAdoptedRawResult &&
        (m_MfdAdoptedRawResult->projectId !=
             m_ActiveRawProjectSnapshot->projectId ||
         m_MfdAdoptedRawResult->inputRevision !=
             m_ActiveRawProjectSnapshot->mfdInputRevision ||
         !Stack::Project::FindSourceSet(
             *m_ActiveRawProjectSnapshot,
             m_MfdAdoptedRawResult->sourceSetId))) {
        m_MfdAdoptedRawResult.reset();
    }
    if (importInProgress) {
        m_ProjectSessionController.CompleteImport(true);
        saveToken = m_ProjectSessionController.BeginSave();
    }
    m_ProjectSessionController.CompleteSave(
        saveToken, true, commit.committedStorageRevision, false);
    m_Dirty = m_ProjectSessionController.IsDirty();
    RefreshGraphLayerMetadata();
    ApplyGraphLayerOrder();
    MarkRenderDirty();
    return Finish(outError, std::string(), true);
}

bool EditorModule::AddMultiFrameSourceSet(
    const std::string& sourceSetName,
    MultiFrameOperationIntent operationIntent,
    const std::vector<std::filesystem::path>& sourcePaths,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "Source sets require an active multi-frame RAW project.", false);
    }
    if (sourceSetName.empty()) {
        return Finish(errorMessage, "Source-set name is required.", false);
    }
    if (!m_ProjectSessionController.BeginImport()) {
        return Finish(errorMessage, "The project is currently busy or read-only.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    EditorNodeGraph::Graph graph = m_NodeGraph;
    const Stack::Project::ProjectStoreTransaction transaction =
        m_ActiveRawProjectStore->BeginTransaction(snapshot.persistedStorageRevision);
    if (!transaction) {
        m_ProjectSessionController.CompleteImport(false);
        return Finish(errorMessage, "Could not begin source ingestion.", false);
    }

    MultiFrameSourceSet sourceSet;
    sourceSet.sourceSetId = Stack::Project::GenerateStableUuid();
    sourceSet.name = sourceSetName;
    sourceSet.operationIntent = operationIntent;
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        sourceSet.operationSchemaVersion = Stack::Project::kMfdOperationSchemaVersion;
        sourceSet.settings = DefaultMfdSettings();
        EnsureMfdPostRecipe(sourceSet);
    }
    std::string error;
    if (!StageFrames(
            m_ActiveRawProjectStore,
            transaction,
            snapshot,
            sourceSet,
            sourcePaths,
            false,
            error) ||
        (operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
         sourceSet.inputFamily != MultiFrameInputFamily::Raw)) {
        m_ActiveRawProjectStore->Abort(transaction);
        m_ProjectSessionController.CompleteImport(false);
        if (error.empty()) error = "Burst Denoise accepts RAW source sets only.";
        return Finish(errorMessage, error, false);
    }
    EditorNodeGraph::Node* node = nullptr;
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (!SyncMfdGraphTopology(graph, snapshot, sourceSet, true, error)) {
            m_ActiveRawProjectStore->Abort(transaction);
            m_ProjectSessionController.CompleteImport(false);
            return Finish(errorMessage, error, false);
        }
        node = FindMfdNode(graph, sourceSet.sourceSetId, sourceSet.graphBindingNodeId);
    } else {
        EditorNodeGraph::RawProjectSourceSetPayload payload;
        payload.sourceSetId = sourceSet.sourceSetId;
        node = graph.AddRawProjectSourceSetNode(
            std::move(payload),
            { 80.0f, 120.0f + 150.0f * static_cast<float>(snapshot.sourceSets.size()) });
        if (node) {
            node->title = "RAW Project Source Set - " + sourceSet.name;
            sourceSet.graphBindingNodeId = node->instanceUuid;
        }
    }
    if (!node) {
        m_ActiveRawProjectStore->Abort(transaction);
        m_ProjectSessionController.CompleteImport(false);
        return Finish(errorMessage, "Could not create the managed graph binding.", false);
    }
    snapshot.activeSourceSetId = sourceSet.sourceSetId;
    snapshot.sourceSets.push_back(std::move(sourceSet));
    if (!snapshot.sourceSets.back().frames.empty()) {
        snapshot.activeFrameId = snapshot.sourceSets.back().referenceFrameId;
    }
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        ++snapshot.mfdInputRevision;
    }
    graph.SelectNode(node->id, false);
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, true, errorMessage);
}

bool EditorModule::AddFramesToMultiFrameSourceSet(
    const std::string& sourceSetId,
    const std::vector<std::filesystem::path>& sourcePaths,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame RAW project is active.", false);
    }
    if (IsMfdExperimentalProcessingBusy()) {
        return Finish(
            errorMessage,
            "Finish or cancel MFD processing before adding frames to the current burst.",
            false);
    }
    if (!m_ProjectSessionController.BeginImport()) {
        return Finish(errorMessage, "The project is currently busy or read-only.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) {
        m_ProjectSessionController.CompleteImport(false);
        return Finish(errorMessage, "The selected source set no longer exists.", false);
    }
    const Stack::Project::ProjectStoreTransaction transaction =
        m_ActiveRawProjectStore->BeginTransaction(snapshot.persistedStorageRevision);
    if (!transaction) {
        m_ProjectSessionController.CompleteImport(false);
        return Finish(errorMessage, "Could not begin source ingestion.", false);
    }
    std::string error;
    if (!StageFrames(
            m_ActiveRawProjectStore,
            transaction,
            snapshot,
            *sourceSet,
            sourcePaths,
            true,
            error)) {
        m_ActiveRawProjectStore->Abort(transaction);
        m_ProjectSessionController.CompleteImport(false);
        return Finish(errorMessage, error, false);
    }
    snapshot.activeSourceSetId = sourceSetId;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        ++snapshot.mfdInputRevision;
    }
    EditorNodeGraph::Graph graph = m_NodeGraph;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, error)) {
        m_ActiveRawProjectStore->Abort(transaction);
        m_ProjectSessionController.CompleteImport(false);
        return Finish(errorMessage, error, false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, true, errorMessage);
}

bool EditorModule::DuplicateMultiFrameSourceSet(
    const std::string& sourceSetId,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame RAW project is active.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    const MultiFrameSourceSet* original = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!original) return Finish(errorMessage, "The source set no longer exists.", false);
    MultiFrameSourceSet duplicate = *original;
    duplicate.sourceSetId = Stack::Project::GenerateStableUuid();
    duplicate.name += " Copy";
    duplicate.referenceFrameId.clear();
    for (SourceSetFrame& frame : duplicate.frames) {
        const bool wasReference = frame.frameId == original->referenceFrameId;
        frame.frameId = Stack::Project::GenerateStableUuid();
        if (wasReference) duplicate.referenceFrameId = frame.frameId;
    }
    EditorNodeGraph::Graph graph = m_NodeGraph;
    EditorNodeGraph::Node* node = nullptr;
    std::string topologyError;
    if (duplicate.operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (!SyncMfdGraphTopology(
                graph, snapshot, duplicate, true, topologyError)) {
            return Finish(errorMessage, topologyError, false);
        }
        node = FindMfdNode(graph, duplicate.sourceSetId, duplicate.graphBindingNodeId);
        ++snapshot.mfdInputRevision;
    } else {
        EditorNodeGraph::RawProjectSourceSetPayload payload;
        payload.sourceSetId = duplicate.sourceSetId;
        node = graph.AddRawProjectSourceSetNode(
            std::move(payload),
            { 80.0f, 120.0f + 150.0f * static_cast<float>(snapshot.sourceSets.size()) });
        if (node) {
            node->title = "RAW Project Source Set - " + duplicate.name;
            duplicate.graphBindingNodeId = node->instanceUuid;
        }
    }
    if (!node) return Finish(errorMessage, "Could not create the duplicate graph binding.", false);
    snapshot.activeSourceSetId = duplicate.sourceSetId;
    snapshot.sourceSets.push_back(std::move(duplicate));
    graph.SelectNode(node->id, false);
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::RenameMultiFrameSourceSet(
    const std::string& sourceSetId,
    const std::string& name,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive() || name.empty()) {
        return Finish(errorMessage, "An active source set and non-empty name are required.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The source set no longer exists.", false);
    sourceSet->name = name;
    EditorNodeGraph::Graph graph = m_NodeGraph;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (EditorNodeGraph::Node* node = FindMfdNode(
                graph, sourceSetId, sourceSet->graphBindingNodeId)) {
            node->title = "MFD - " + name;
        }
    } else if (EditorNodeGraph::Node* node = FindSourceSetNode(
                   graph, sourceSetId, sourceSet->graphBindingNodeId)) {
        node->title = "RAW Project Source Set - " + name;
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::DeleteMultiFrameSourceSet(
    const std::string& sourceSetId,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame RAW project is active.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    const auto found = std::find_if(
        snapshot.sourceSets.begin(), snapshot.sourceSets.end(),
        [&](const MultiFrameSourceSet& sourceSet) {
            return sourceSet.sourceSetId == sourceSetId;
        });
    if (found == snapshot.sourceSets.end()) {
        return Finish(errorMessage, "The source set no longer exists.", false);
    }
    const std::string binding = found->graphBindingNodeId;
    const bool deletingMfd =
        found->operationIntent == MultiFrameOperationIntent::RawBurstDenoise;
    snapshot.sourceSets.erase(found);
    snapshot.activeSourceSetId = snapshot.sourceSets.empty()
        ? std::string()
        : snapshot.sourceSets.front().sourceSetId;
    EditorNodeGraph::Graph graph = m_NodeGraph;
    if (deletingMfd) {
        std::vector<int> nodesToRemove;
        for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
            if ((node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                 node.multiFrameDenoise.sourceSetId == sourceSetId) ||
                (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
                 node.rawProjectFrame.sourceSetId == sourceSetId)) {
                nodesToRemove.push_back(node.id);
            }
        }
        for (int nodeId : nodesToRemove) graph.RemoveNode(nodeId);
        ++snapshot.mfdInputRevision;
    } else if (const EditorNodeGraph::Node* node = FindSourceSetNode(
                   graph, sourceSetId, binding)) {
        graph.RemoveNode(node->id);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::MoveMultiFrameFrame(
    const std::string& sourceSetId,
    std::size_t frameIndex,
    int direction,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive() || direction == 0) {
        return Finish(errorMessage, "Frame move is unavailable.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet || frameIndex >= sourceSet->frames.size()) {
        return Finish(errorMessage, "The selected frame no longer exists.", false);
    }
    const std::int64_t target = static_cast<std::int64_t>(frameIndex) +
        (direction < 0 ? -1 : 1);
    if (target < 0 || target >= static_cast<std::int64_t>(sourceSet->frames.size())) {
        return Finish(errorMessage, std::string(), true);
    }
    std::swap(sourceSet->frames[frameIndex], sourceSet->frames[static_cast<std::size_t>(target)]);
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        ++snapshot.mfdInputRevision;
    }
    EditorNodeGraph::Graph graph = m_NodeGraph;
    std::string topologyError;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage, false);
}

bool EditorModule::SetMultiFrameFrameEnabled(
    const std::string& sourceSetId,
    const std::string& frameId,
    bool enabled,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) return Finish(errorMessage, "No project is active.", false);
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The source set no longer exists.", false);
    const auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) return Finish(errorMessage, "The frame no longer exists.", false);
    frame->enabled = enabled;
    snapshot.activeFrameId = frameId;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        ++snapshot.mfdInputRevision;
    }
    EditorNodeGraph::Graph graph = m_NodeGraph;
    std::string topologyError;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameReferenceFrame(
    const std::string& sourceSetId,
    const std::string& frameId,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) return Finish(errorMessage, "No project is active.", false);
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The source set no longer exists.", false);
    const auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) return Finish(errorMessage, "The frame no longer exists.", false);
    sourceSet->referenceFrameId = frameId;
    snapshot.activeFrameId = frameId;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        ++snapshot.mfdInputRevision;
    }
    EditorNodeGraph::Graph graph = m_NodeGraph;
    std::string topologyError;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameFrameLabel(
    const std::string& sourceSetId,
    const std::string& frameId,
    const std::string& label,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No MFD project is active.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The MFD burst no longer exists.", false);
    auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) {
        return Finish(errorMessage, "The frame no longer exists.", false);
    }
    frame->userLabel = label;
    EditorNodeGraph::Graph graph = m_NodeGraph;
    std::string topologyError;
    if (!SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameFrameOrientation(
    const std::string& sourceSetId,
    const std::string& frameId,
    int orientation,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive() || orientation < 0 || orientation > 8) {
        return Finish(errorMessage, "Choose a valid CFA-preserving orientation interpretation.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The MFD burst no longer exists.", false);
    auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) {
        return Finish(errorMessage, "The frame no longer exists.", false);
    }
    frame->metadataOverrides["orientation"] = orientation;
    frame->metadataOverrides["orientationPolicy"] = "cfa-preserving-interpretation";
    ++snapshot.mfdInputRevision;
    snapshot.activeFrameId = frameId;
    EditorNodeGraph::Graph graph = m_NodeGraph;
    std::string topologyError;
    if (!SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::RemoveMultiFrameFrame(
    const std::string& sourceSetId,
    const std::string& frameId,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No MFD project is active.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The MFD burst no longer exists.", false);
    const auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) {
        return Finish(errorMessage, "The frame no longer exists.", false);
    }
    const bool removedReference = sourceSet->referenceFrameId == frameId;
    sourceSet->frames.erase(frame);
    if (removedReference) {
        sourceSet->referenceFrameId = sourceSet->frames.empty()
            ? std::string()
            : sourceSet->frames.front().frameId;
    }
    snapshot.activeFrameId = sourceSet->referenceFrameId;
    ++snapshot.mfdInputRevision;
    EditorNodeGraph::Graph graph = m_NodeGraph;
    std::string topologyError;
    if (!SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::SetMfdInternalViewTransformEnabled(
    const std::string& sourceSetId,
    bool enabled,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No MFD project is active.", false);
    }
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet ||
        sourceSet->operationIntent != MultiFrameOperationIntent::RawBurstDenoise) {
        return Finish(errorMessage, "The MFD burst no longer exists.", false);
    }
    const std::string currentPlacement = sourceSet->settings.value(
        "viewTransformPlacement", std::string("internal"));
    if ((enabled && currentPlacement == "internal") ||
        (!enabled && currentPlacement == "graph")) {
        return Finish(errorMessage, std::string(), true);
    }

    const EditorNodeGraph::Graph graphBefore = m_NodeGraph;
    const std::vector<std::shared_ptr<LayerBase>> layersBefore = m_Layers;
    EditorNodeGraph::Node* mfd = FindMfdNode(
        m_NodeGraph, sourceSetId, sourceSet->graphBindingNodeId);
    EditorNodeGraph::Node* output = m_NodeGraph.FindNode(
        m_NodeGraph.GetOutputNodeId());
    if (!mfd || !output || output->kind != EditorNodeGraph::NodeKind::Output) {
        return Finish(errorMessage, "The MFD output path is incomplete.", false);
    }
    mfd->multiFrameDenoise.internalViewTransformEnabled = enabled;
    std::string mutationError;
    if (!enabled) {
        const EditorNodeGraph::Link* outputInput = m_NodeGraph.FindInputLink(
            output->id, EditorNodeGraph::kImageInputSocketId);
        if (!outputInput) {
            m_NodeGraph = graphBefore;
            return Finish(errorMessage, "Connect the MFD result path to Output before using graph View Transform mode.", false);
        }
        const EditorNodeGraph::Link inputSnapshot = *outputInput;
        if (!m_NodeGraph.RemoveLink(
                inputSnapshot.fromNodeId,
                inputSnapshot.fromSocketId,
                inputSnapshot.toNodeId,
                inputSnapshot.toSocketId) ||
            !ConnectGraphSockets(
                inputSnapshot.fromNodeId,
                inputSnapshot.fromSocketId,
                output->id,
                EditorNodeGraph::kImageInputSocketId,
                &mutationError)) {
            m_NodeGraph = graphBefore;
            m_Layers = layersBefore;
            return Finish(
                errorMessage,
                mutationError.empty()
                    ? "Could not insert the graph View Transform."
                    : mutationError,
                false);
        }
        const EditorNodeGraph::Link* insertedInput = m_NodeGraph.FindInputLink(
            output->id, EditorNodeGraph::kImageInputSocketId);
        EditorNodeGraph::Node* viewNode = insertedInput
            ? m_NodeGraph.FindNode(insertedInput->fromNodeId)
            : nullptr;
        if (!viewNode || viewNode->kind != EditorNodeGraph::NodeKind::Layer ||
            viewNode->layerType != LayerType::ViewTransform ||
            viewNode->layerIndex < 0 ||
            viewNode->layerIndex >= static_cast<int>(m_Layers.size())) {
            m_NodeGraph = graphBefore;
            m_Layers = layersBefore;
            return Finish(
                errorMessage,
                "The graph connection did not create the required View Transform.",
                false);
        }
        sourceSet->settings["graphViewTransformNodeUuid"] =
            viewNode->instanceUuid;
        if (sourceSet->settings.contains("graphViewTransformSettings") &&
            m_Layers[viewNode->layerIndex]) {
            m_Layers[viewNode->layerIndex]->Deserialize(
                sourceSet->settings["graphViewTransformSettings"]);
        }
        sourceSet->settings["viewTransformPlacement"] = "graph";
    } else {
        const EditorNodeGraph::Link* outputInput = m_NodeGraph.FindInputLink(
            output->id, EditorNodeGraph::kImageInputSocketId);
        EditorNodeGraph::Node* viewNode = outputInput
            ? m_NodeGraph.FindNode(outputInput->fromNodeId)
            : nullptr;
        if (!viewNode || viewNode->kind != EditorNodeGraph::NodeKind::Layer ||
            viewNode->layerType != LayerType::ViewTransform ||
            viewNode->layerIndex < 0 ||
            viewNode->layerIndex >= static_cast<int>(m_Layers.size())) {
            m_NodeGraph = graphBefore;
            return Finish(
                errorMessage,
                "Internal View Transform can only be restored when exactly one View Transform is immediately before Output.",
                false);
        }
        const std::string managedViewNodeUuid = sourceSet->settings.value(
            "graphViewTransformNodeUuid", std::string());
        if (managedViewNodeUuid.empty() ||
            viewNode->instanceUuid != managedViewNodeUuid) {
            m_NodeGraph = graphBefore;
            return Finish(
                errorMessage,
                "The View Transform before Output is no longer the one placed by MFD. Preserve the user-authored graph and reconnect the managed MFD View before moving it internally.",
                false);
        }
        const EditorNodeGraph::Link* viewInput = m_NodeGraph.FindInputLink(
            viewNode->id, EditorNodeGraph::kImageInputSocketId);
        if (!viewInput) {
            m_NodeGraph = graphBefore;
            return Finish(errorMessage, "The external View Transform has no input.", false);
        }
        const EditorNodeGraph::Link upstream = *viewInput;
        std::size_t viewOutgoingCount = 0;
        for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
            if (link.fromNodeId == viewNode->id) ++viewOutgoingCount;
        }
        if (viewOutgoingCount != 1u) {
            m_NodeGraph = graphBefore;
            return Finish(
                errorMessage,
                "The external View Transform has additional consumers and cannot be removed safely.",
                false);
        }
        if (m_Layers[viewNode->layerIndex]) {
            sourceSet->settings["graphViewTransformSettings"] =
                m_Layers[viewNode->layerIndex]->Serialize();
        }
        const int viewLayerIndex = viewNode->layerIndex;
        RemoveLayer(viewLayerIndex);
        output = m_NodeGraph.FindNode(m_NodeGraph.GetOutputNodeId());
        if (!output || !m_NodeGraph.TryConnectSockets(
                upstream.fromNodeId,
                upstream.fromSocketId,
                output->id,
                EditorNodeGraph::kImageInputSocketId,
                &mutationError)) {
            m_NodeGraph = graphBefore;
            m_Layers = layersBefore;
            return Finish(
                errorMessage,
                mutationError.empty()
                    ? "Could not restore the internal View Transform output path."
                    : mutationError,
                false);
        }
        sourceSet->settings["viewTransformPlacement"] = "internal";
        sourceSet->settings.erase("graphViewTransformNodeUuid");
    }

    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    if (!transaction) {
        m_NodeGraph = graphBefore;
        m_Layers = layersBefore;
        return Finish(errorMessage, "Could not begin the View Transform project transaction.", false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), m_NodeGraph, transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameOperationIntent(
    const std::string& sourceSetId,
    MultiFrameOperationIntent intent,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) return Finish(errorMessage, "No project is active.", false);
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The source set no longer exists.", false);
    if (intent == MultiFrameOperationIntent::RawBurstDenoise &&
        sourceSet->inputFamily != MultiFrameInputFamily::Raw) {
        return Finish(errorMessage, "Burst Denoise accepts RAW source sets only.", false);
    }
    sourceSet->operationIntent = intent;
    sourceSet->operationSchemaVersion = intent == MultiFrameOperationIntent::RawBurstDenoise
        ? Stack::Project::kMfdOperationSchemaVersion
        : Stack::Project::kMultiFrameOperationSchemaVersion;
    sourceSet->settings = intent == MultiFrameOperationIntent::RawBurstDenoise
        ? DefaultMfdSettings()
        : nlohmann::json::object();
    if (intent == MultiFrameOperationIntent::RawBurstDenoise) {
        EnsureMfdPostRecipe(*sourceSet);
    }
    ++snapshot.mfdInputRevision;
    EditorNodeGraph::Graph graph = m_NodeGraph;
    std::string topologyError;
    if (intent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::ActivateMultiFrameSourceSet(const std::string& sourceSetId) {
    if (!IsMultiFrameRawProjectActive()) return false;
    MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(*m_ActiveRawProjectSnapshot, sourceSetId);
    if (!sourceSet) return false;
    const bool changed = m_ActiveRawProjectSnapshot->activeSourceSetId != sourceSetId;
    m_ActiveRawProjectSnapshot->activeSourceSetId = sourceSetId;
    m_ActiveRawProjectSnapshot->rawWorkspaceData["activeSourceSetId"] = sourceSetId;
    const EditorNodeGraph::Node* node =
        sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise
        ? FindMfdNode(m_NodeGraph, sourceSetId, sourceSet->graphBindingNodeId)
        : FindSourceSetNode(m_NodeGraph, sourceSetId, sourceSet->graphBindingNodeId);
    if (node) {
        m_NodeGraph.SelectNode(node->id, false);
    }
    if (changed) MarkDirty();
    return true;
}

bool EditorModule::ActivateMultiFrameFrame(
    const std::string& sourceSetId,
    const std::string& frameId,
    bool selectGraphNode) {
    if (!IsMultiFrameRawProjectActive()) return false;
    MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(*m_ActiveRawProjectSnapshot, sourceSetId);
    if (!sourceSet) return false;
    const auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) return false;
    const bool changed =
        m_ActiveRawProjectSnapshot->activeSourceSetId != sourceSetId ||
        m_ActiveRawProjectSnapshot->activeFrameId != frameId;
    m_ActiveRawProjectSnapshot->activeSourceSetId = sourceSetId;
    m_ActiveRawProjectSnapshot->activeFrameId = frameId;
    UpdateWorkspaceManifestFields(*m_ActiveRawProjectSnapshot);
    if (selectGraphNode) {
        if (const EditorNodeGraph::Node* node = FindMfdFrameNode(
                m_NodeGraph, sourceSetId, frameId)) {
            m_NodeGraph.SelectNode(node->id, false);
        }
    }
    if (changed) MarkDirty();
    return true;
}

bool EditorModule::OpenManagedMfdGraphNode(int nodeId) {
    if (!IsMultiFrameRawProjectActive()) return false;
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) return false;
    if (node->kind == EditorNodeGraph::NodeKind::RawProjectFrame) {
        if (!ActivateMultiFrameFrame(
                node->rawProjectFrame.sourceSetId,
                node->rawProjectFrame.frameId,
                true)) {
            return false;
        }
    } else if (node->kind == EditorNodeGraph::NodeKind::MultiFrameDenoise) {
        if (!ActivateMultiFrameSourceSet(node->multiFrameDenoise.sourceSetId)) {
            return false;
        }
    } else {
        return false;
    }
    m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
    RequestOpenRawLabTab();
    return true;
}

bool EditorModule::RequestCreateMfdProjectFromGallerySelection() {
    if (IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        IsMfdExperimentalProcessingBusy()) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Finish the current RAW load or MFD processing run before creating a new project.",
            "mfd-create-new-busy");
        return false;
    }
    std::vector<std::filesystem::path> paths;
    paths.reserve(m_RawWorkspace.selectedSourceKeys.size());
    for (const std::string& key : m_RawWorkspace.selectedSourceKeys) {
        const auto source = std::find_if(
            m_RawWorkspace.sources.begin(), m_RawWorkspace.sources.end(),
            [&](const Stack::RawWorkspace::SourceRecord& candidate) {
                return candidate.relativePathKey == key;
            });
        if (source != m_RawWorkspace.sources.end()) {
            paths.push_back(source->absolutePath);
        }
    }
    if (paths.size() < 2u) return false;
    m_PendingMultiFrameGallerySourcePaths = std::move(paths);
    m_PopulateMultiFrameCreationFromGallery = true;
    m_OpenMultiFrameCreationPopup = true;
    m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
    m_MfdBrowseWorkspace = false;
    RequestOpenRawLabTab();
    return true;
}

bool EditorModule::SaveActiveMultiFrameRawProject(std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame RAW project is active.", false);
    }
    if (!m_Dirty) return Finish(errorMessage, std::string(), true);
    RawProjectSnapshot snapshot = *m_ActiveRawProjectSnapshot;
    const auto transaction = m_ActiveRawProjectStore->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), m_NodeGraph, transaction, false, errorMessage);
}

bool EditorModule::SaveActiveMultiFrameRawProjectAs(
    const std::filesystem::path& destination,
    Stack::Project::ProjectStorageKind storageKind,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive() || destination.empty()) {
        return Finish(errorMessage, "No multi-frame project or destination is available.", false);
    }
    std::filesystem::path normalizedDestination = destination;
    const char* requiredExtension =
        storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle
        ? ".stackbundle"
        : ".stack";
    if (Lower(normalizedDestination.extension().string()) != requiredExtension) {
        normalizedDestination += requiredExtension;
    }
    RawProjectSnapshot snapshotForCopy = *m_ActiveRawProjectSnapshot;
    snapshotForCopy.pipelineData = PipelineForGraph(
        m_NodeGraph, m_Layers, snapshotForCopy.pipelineData);
    const bool repairedCopy =
        m_ProjectSessionController.Phase() ==
            Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery ||
        snapshotForCopy.rawWorkspaceData.value("repairRequired", false);
    if (repairedCopy) {
        snapshotForCopy.rawWorkspaceData["repairRequired"] = false;
        snapshotForCopy.rawWorkspaceData["repairedCopy"] = true;
    }
    UpdateWorkspaceManifestFields(snapshotForCopy);
    Stack::Project::ProjectStoreOpenResult converted =
        Stack::Project::ConvertProjectStore(
            m_ActiveRawProjectStore,
            snapshotForCopy,
            normalizedDestination,
            storageKind);
    if (!converted) return Finish(errorMessage, converted.message, false);
    m_ActiveRawProjectStore = converted.store;
    m_ActiveRawProjectSnapshot =
        std::make_shared<RawProjectSnapshot>(std::move(converted.snapshot));
    m_ActiveRawWorkspaceProjectPath = normalizedDestination.lexically_normal();
    SetCurrentProjectFileName(m_ActiveRawWorkspaceProjectPath.string());
    const auto replacement = m_ProjectSessionController.BeginReplacement();
    m_ProjectSessionController.CompleteReplacement(
        replacement,
        m_ActiveRawProjectSnapshot->projectId,
        m_ActiveRawProjectSnapshot->dirtyRevision,
        m_ActiveRawProjectSnapshot->persistedStorageRevision,
        m_ActiveRawProjectStore->IsReadOnlyRecovery());
    m_Dirty = false;
    return Finish(errorMessage, std::string(), true);
}

bool EditorModule::OptimizeActiveMultiFrameRawProject(std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame RAW project is active.", false);
    }
    if (m_Dirty && !SaveActiveMultiFrameRawProject(errorMessage)) return false;
    const Stack::Project::ProjectSaveToken maintenanceToken =
        m_ProjectSessionController.BeginSave();
    if (!maintenanceToken) {
        return Finish(
            errorMessage,
            "The project cannot be optimized in its current session state.",
            false);
    }
    std::string optimizeError;
    const bool optimized = m_ActiveRawProjectStore->Optimize(&optimizeError);
    const bool conflict = !optimized &&
        optimizeError.find("changed while it was being optimized") != std::string::npos;
    m_ProjectSessionController.CompleteSave(
        maintenanceToken,
        optimized,
        m_ActiveRawProjectStore->StorageRevision(),
        conflict);
    if (!optimized) return Finish(errorMessage, optimizeError, false);
    m_ActiveRawProjectSnapshot->persistedStorageRevision =
        m_ActiveRawProjectStore->StorageRevision();
    return Finish(errorMessage, std::string(), true);
}

bool EditorModule::ValidateAndRepairActiveRawProjectGraphBindings(
    bool* requiresRepairedCopy,
    std::string* outError) {
    if (requiresRepairedCopy) *requiresRepairedCopy = false;
    if (!m_ActiveRawProjectSnapshot) return true;

    bool changed = false;
    bool quarantined = false;
    std::unordered_map<std::string, std::vector<EditorNodeGraph::Node*>> nodesBySet;
    for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
            node.rawProjectSourceSet.managed &&
            !node.rawProjectSourceSet.quarantined) {
            nodesBySet[node.rawProjectSourceSet.sourceSetId].push_back(&node);
        }
    }
    std::unordered_set<std::string> knownSetIds;
    float nextY = 120.0f;
    for (MultiFrameSourceSet& sourceSet : m_ActiveRawProjectSnapshot->sourceSets) {
        knownSetIds.insert(sourceSet.sourceSetId);
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
            sourceSet.operationSchemaVersion >= Stack::Project::kMfdMosaicPlaceholderSchemaVersion) {
            std::vector<EditorNodeGraph::Node*> mfdNodes;
            std::unordered_map<std::string, std::vector<EditorNodeGraph::Node*>> frameNodes;
            for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
                if (node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                    node.multiFrameDenoise.sourceSetId == sourceSet.sourceSetId &&
                    !node.multiFrameDenoise.quarantined) {
                    mfdNodes.push_back(&node);
                } else if (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
                           node.rawProjectFrame.sourceSetId == sourceSet.sourceSetId &&
                           !node.rawProjectFrame.quarantined) {
                    frameNodes[node.rawProjectFrame.frameId].push_back(&node);
                }
            }
            EditorNodeGraph::Node* primaryMfd = nullptr;
            for (EditorNodeGraph::Node* candidate : mfdNodes) {
                if (!primaryMfd || candidate->instanceUuid == sourceSet.graphBindingNodeId) {
                    primaryMfd = candidate;
                }
            }
            for (EditorNodeGraph::Node* candidate : mfdNodes) {
                if (candidate == primaryMfd) continue;
                candidate->multiFrameDenoise.managed = false;
                candidate->multiFrameDenoise.quarantined = true;
                candidate->multiFrameDenoise.presentationStatus =
                    "Quarantined duplicate MFD binding. Save Repaired Copy is required.";
                changed = true;
                quarantined = true;
            }
            std::unordered_set<std::string> manifestFrameIds;
            bool topologyMissing = primaryMfd == nullptr;
            for (const SourceSetFrame& frame : sourceSet.frames) {
                manifestFrameIds.insert(frame.frameId);
                auto foundFrames = frameNodes.find(frame.frameId);
                if (foundFrames == frameNodes.end() || foundFrames->second.empty()) {
                    topologyMissing = true;
                    continue;
                }
                for (std::size_t duplicateIndex = 1;
                     duplicateIndex < foundFrames->second.size();
                     ++duplicateIndex) {
                    EditorNodeGraph::Node* duplicate =
                        foundFrames->second[duplicateIndex];
                    duplicate->rawProjectFrame.managed = false;
                    duplicate->rawProjectFrame.quarantined = true;
                    duplicate->rawProjectFrame.compatibilityStatus =
                        "Quarantined duplicate frame binding.";
                    changed = true;
                    quarantined = true;
                }
            }
            for (auto& [frameId, candidates] : frameNodes) {
                if (manifestFrameIds.find(frameId) != manifestFrameIds.end()) continue;
                for (EditorNodeGraph::Node* orphan : candidates) {
                    orphan->rawProjectFrame.managed = false;
                    orphan->rawProjectFrame.quarantined = true;
                    orphan->rawProjectFrame.compatibilityStatus =
                        "Quarantined orphan frame binding.";
                    changed = true;
                    quarantined = true;
                }
            }
            if (primaryMfd) {
                for (const SourceSetFrame& frame : sourceSet.frames) {
                    const auto foundFrames = frameNodes.find(frame.frameId);
                    if (foundFrames == frameNodes.end() || foundFrames->second.empty()) continue;
                    const EditorNodeGraph::Node* frameNode = foundFrames->second.front();
                    const EditorNodeGraph::Link* link = m_NodeGraph.FindInputLink(
                        primaryMfd->id,
                        EditorNodeGraph::MfdFrameInputSocketId(frame.frameId));
                    if (!link || link->fromNodeId != frameNode->id ||
                        link->ownership != EditorNodeGraph::Link::Ownership::ManagedSourceBinding) {
                        topologyMissing = true;
                    }
                }
            }
            if (topologyMissing) {
                std::string topologyError;
                if (!SyncMfdGraphTopology(
                        m_NodeGraph,
                        *m_ActiveRawProjectSnapshot,
                        sourceSet,
                        true,
                        topologyError)) {
                    return Finish(outError, topologyError, false);
                }
                changed = true;
            }
            nextY += 150.0f;
            continue;
        }
        auto found = nodesBySet.find(sourceSet.sourceSetId);
        if (found == nodesBySet.end() || found->second.empty()) {
            EditorNodeGraph::RawProjectSourceSetPayload payload;
            payload.sourceSetId = sourceSet.sourceSetId;
            EditorNodeGraph::Node* node = m_NodeGraph.AddRawProjectSourceSetNode(
                std::move(payload), { 80.0f, nextY });
            if (!node) {
                return Finish(outError, "A missing managed source-set node could not be recreated.", false);
            }
            node->title = "RAW Project Source Set · " + sourceSet.name;
            sourceSet.graphBindingNodeId = node->instanceUuid;
            changed = true;
        } else {
            EditorNodeGraph::Node* primary = found->second.front();
            const auto bound = std::find_if(
                found->second.begin(), found->second.end(),
                [&](const EditorNodeGraph::Node* node) {
                    return node->instanceUuid == sourceSet.graphBindingNodeId;
                });
            if (bound != found->second.end()) primary = *bound;
            if (!primary->rawProjectSourceSet.managed ||
                primary->rawProjectSourceSet.quarantined ||
                primary->rawProjectSourceSet.presentationStatus !=
                    EditorNodeGraph::kRawProjectSourceSetUnavailableStatus) {
                changed = true;
            }
            primary->rawProjectSourceSet.managed = true;
            primary->rawProjectSourceSet.quarantined = false;
            primary->rawProjectSourceSet.presentationStatus =
                EditorNodeGraph::kRawProjectSourceSetUnavailableStatus;
            if (sourceSet.graphBindingNodeId != primary->instanceUuid) {
                sourceSet.graphBindingNodeId = primary->instanceUuid;
                changed = true;
            }
            for (EditorNodeGraph::Node* duplicate : found->second) {
                if (duplicate == primary) continue;
                duplicate->rawProjectSourceSet.managed = false;
                duplicate->rawProjectSourceSet.quarantined = true;
                duplicate->rawProjectSourceSet.presentationStatus =
                    "Quarantined duplicate binding. Save Repaired Copy is required.";
                changed = true;
                quarantined = true;
            }
        }
        nextY += 150.0f;
    }
    for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawProjectSourceSet) continue;
        if (node.rawProjectSourceSet.managed &&
            !node.rawProjectSourceSet.quarantined &&
            knownSetIds.find(node.rawProjectSourceSet.sourceSetId) == knownSetIds.end()) {
            node.rawProjectSourceSet.managed = false;
            node.rawProjectSourceSet.quarantined = true;
            node.rawProjectSourceSet.presentationStatus =
                "Quarantined orphan binding. Save Repaired Copy is required.";
            changed = true;
            quarantined = true;
        }
    }
    for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        std::string sourceSetId;
        if (node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
            node.multiFrameDenoise.managed &&
            !node.multiFrameDenoise.quarantined) {
            sourceSetId = node.multiFrameDenoise.sourceSetId;
            if (knownSetIds.find(sourceSetId) == knownSetIds.end()) {
                node.multiFrameDenoise.managed = false;
                node.multiFrameDenoise.quarantined = true;
                node.multiFrameDenoise.presentationStatus =
                    "Quarantined orphan MFD binding. Save Repaired Copy is required.";
                changed = true;
                quarantined = true;
            }
        } else if (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
                   node.rawProjectFrame.managed &&
                   !node.rawProjectFrame.quarantined) {
            sourceSetId = node.rawProjectFrame.sourceSetId;
            if (knownSetIds.find(sourceSetId) == knownSetIds.end()) {
                node.rawProjectFrame.managed = false;
                node.rawProjectFrame.quarantined = true;
                node.rawProjectFrame.compatibilityStatus =
                    "Quarantined orphan frame binding. Save Repaired Copy is required.";
                changed = true;
                quarantined = true;
            }
        }
    }
    if (changed) {
        m_ActiveRawProjectSnapshot->pipelineData = PipelineForGraph(
            m_NodeGraph, m_Layers, m_ActiveRawProjectSnapshot->pipelineData);
        MarkDirty();
    }
    if (quarantined) {
        m_ActiveRawProjectSnapshot->rawWorkspaceData["repairRequired"] = true;
        m_ProjectSessionController.MarkReadOnlyRecovery();
        if (requiresRepairedCopy) *requiresRepairedCopy = true;
    }
    return true;
}

void EditorModule::RenderMultiFrameRawLabCreationPopup() {
    static char projectName[160] = "Multi-Frame Denoise Project";
    static std::filesystem::path destinationFolder;
    static std::vector<std::filesystem::path> selectedPaths;
    static int referenceFrameIndex = 0;

    if (m_PopulateMultiFrameCreationFromGallery) {
        selectedPaths = m_PendingMultiFrameGallerySourcePaths;
        m_PendingMultiFrameGallerySourcePaths.clear();
        m_PopulateMultiFrameCreationFromGallery = false;
    }
    if (m_OpenMultiFrameCreationPopup) {
        ImGui::OpenPopup("Create New MFD Project");
        m_OpenMultiFrameCreationPopup = false;
    }

    if (!ImGui::BeginPopupModal(
            "Create New MFD Project",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (destinationFolder.empty() && !m_RawWorkspace.workspaceRoot.empty()) {
        destinationFolder = Stack::RawWorkspace::BuildManagedLayout(
            m_RawWorkspace.workspaceRoot).projectsDirectory;
    }
    ImGui::TextWrapped(
        "Create one MFD project from compatible, still-mosaiced Bayer RAW "
        "captures. Every selected original is embedded exactly.");
    ImGui::Separator();
    ImGui::InputText("Project name", projectName, sizeof(projectName));
    ImGui::TextDisabled("Format: Directory bundle (.stackbundle)");
    ImGui::TextDisabled("Operation: MFD / Mosaic CFA Burst Denoise");

    if (ImGui::Button("Choose project folder")) {
        const std::string folder = FileDialogs::OpenFolderDialog(
            "Choose Multi-Frame Project Folder");
        if (!folder.empty()) destinationFolder = folder;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s",
        destinationFolder.empty()
            ? "No folder selected"
            : destinationFolder.string().c_str());

    if (ImGui::Button("Choose source frames")) {
        const std::vector<std::string> selected =
            FileDialogs::OpenMultipleFilesDialog(
                "Choose MFD RAW Frames",
                "RAW Frames\0*.dng;*.cr2;*.cr3;*.nef;*.nrw;*.arw;*.srf;*.sr2;*.raf;*.rw2;*.orf;*.pef;*.3fr;*.fff;*.iiq;*.rwl;*.raw\0All Files\0*.*\0");
        selectedPaths.clear();
        for (const std::string& path : selected) selectedPaths.emplace_back(path);
        referenceFrameIndex = 0;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%llu frame(s)",
        static_cast<unsigned long long>(selectedPaths.size()));

    if (!selectedPaths.empty()) {
        referenceFrameIndex = std::clamp(
            referenceFrameIndex,
            0,
            static_cast<int>(selectedPaths.size() - 1u));
        const std::string referenceLabel =
            selectedPaths[static_cast<std::size_t>(referenceFrameIndex)]
                .filename().string();
        if (ImGui::BeginCombo("Reference frame", referenceLabel.c_str())) {
            for (std::size_t index = 0; index < selectedPaths.size(); ++index) {
                const bool selected =
                    static_cast<int>(index) == referenceFrameIndex;
                if (ImGui::Selectable(
                        selectedPaths[index].filename().string().c_str(),
                        selected)) {
                    referenceFrameIndex = static_cast<int>(index);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled(
            "Compatibility is checked from RAW headers before any original is copied.");
    }

    const bool ready = projectName[0] != '\0' &&
        !destinationFolder.empty() && selectedPaths.size() >= 2u;
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Create")) {
        std::filesystem::path destination =
            destinationFolder / std::filesystem::path(projectName).filename();
        std::string error;
        const std::string requestedProjectName(projectName);
        const std::vector<std::filesystem::path> requestedPaths = selectedPaths;
        const std::size_t requestedReferenceIndex =
            static_cast<std::size_t>(referenceFrameIndex);
        auto createAction = [
            this,
            destination,
            requestedProjectName,
            requestedPaths,
            requestedReferenceIndex
        ](std::string* actionError) {
            return CreateMultiFrameRawProject(
                destination,
                Stack::Project::ProjectStorageKind::DirectoryBundle,
                requestedProjectName,
                "MFD Burst",
                Stack::Project::MultiFrameOperationIntent::RawBurstDenoise,
                requestedPaths,
                requestedReferenceIndex,
                actionError);
        };

        if (HasProjectContent() && m_Dirty) {
            QueueRawWorkspaceProjectReplacement(
                "create a new MFD project",
                requestedProjectName,
                std::move(createAction));
            m_RawWorkspaceLabUi.multiFrameStatusText =
                "Choose how to handle the current project's unsaved changes.";
            selectedPaths.clear();
            ImGui::CloseCurrentPopup();
        } else if (createAction(&error)) {
            m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
            m_RawWorkspaceLabUi.multiFrameStatusText =
                "New MFD project created and opening.";
            selectedPaths.clear();
            ImGui::CloseCurrentPopup();
        } else {
            m_RawWorkspaceLabUi.multiFrameStatusText = error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        selectedPaths.clear();
        ImGui::CloseCurrentPopup();
    }
    if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
        ImGui::TextWrapped("%s", m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
    }
    ImGui::EndPopup();
}

void EditorModule::RenderMultiFrameRawLabTool() {
    if (!IsMultiFrameRawProjectActive()) {
        if (IsRawWorkspaceProjectActive()) {
            ImGui::TextWrapped(
                "This single-RAW project can be upgraded into a separate "
                "multi-frame bundle without changing the original.");
            if (ImGui::Button("Save As Upgraded Project")) {
                const std::string folder = FileDialogs::OpenFolderDialog(
                    "Choose Directory for Upgraded RAW Project");
                if (!folder.empty()) {
                    const std::string baseName = m_CurrentProjectName.empty()
                        ? "RAW Project"
                        : m_CurrentProjectName;
                    std::string error;
                    if (!UpgradeActiveLegacyRawProjectToMultiFrame(
                            std::filesystem::path(folder) /
                                (baseName + " Upgraded"),
                            &error)) {
                        m_RawWorkspaceLabUi.multiFrameStatusText = error;
                    } else {
                        m_RawWorkspaceLabUi.multiFrameStatusText =
                            "The upgraded embedded copy is now active.";
                    }
                }
            }
        } else {
            ImGui::TextDisabled("No multi-frame RAW project is active.");
            ImGui::Spacing();
            if (ImGui::Button("Create New MFD Project")) {
                m_RawWorkspaceLabUi.multiFrameStatusText.clear();
                m_OpenMultiFrameCreationPopup = true;
            }
            ImGui::TextWrapped(
                "Select two or more RAW images in Gallery to prefill the burst.");
        }
        RenderMultiFrameRawLabCreationPopup();
        if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
            ImGui::TextWrapped(
                "%s",
                m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
        }
        return;
    }

    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (const EditorNodeGraph::Node* selectedNode =
            m_NodeGraph.FindNode(selectedNodeId);
        selectedNode) {
        if (selectedNode->kind ==
            EditorNodeGraph::NodeKind::RawProjectFrame) {
            ActivateMultiFrameFrame(
                selectedNode->rawProjectFrame.sourceSetId,
                selectedNode->rawProjectFrame.frameId,
                false);
        } else if (selectedNode->kind ==
                   EditorNodeGraph::NodeKind::MultiFrameDenoise) {
            ActivateMultiFrameSourceSet(
                selectedNode->multiFrameDenoise.sourceSetId);
        }
    }

    RawProjectSnapshot& snapshot = *m_ActiveRawProjectSnapshot;
    MultiFrameSourceSet* active = Stack::Project::FindSourceSet(
        snapshot,
        snapshot.activeSourceSetId);
    if (active == nullptr) {
        ImGui::TextDisabled("This project has no active source set.");
        return;
    }
    const std::string activeSetId = active->sourceSetId;
    std::string setReason;
    const Stack::Project::MultiFrameSetStatus setStatus =
        Stack::Project::EvaluateSourceSetStatus(
            snapshot,
            *active,
            &setReason);

    ImGui::TextUnformatted(active->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%llu frames",
        static_cast<unsigned long long>(active->frames.size()));
    if (setStatus == Stack::Project::MultiFrameSetStatus::Incompatible) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
            "%s",
            setReason.c_str());
    } else if (setStatus == Stack::Project::MultiFrameSetStatus::Draft) {
        ImGui::TextColored(
            ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
            "%s",
            setReason.c_str());
    } else {
        ImGui::TextDisabled("Ready for Mosaic CFA burst processing");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Frames");
    for (const SourceSetFrame& frame : active->frames) {
        const EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
        ImGui::PushID(frame.frameId.c_str());
        bool enabled = frame.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            std::string error;
            SetMultiFrameFrameEnabled(
                activeSetId,
                frame.frameId,
                enabled,
                &error);
            if (!error.empty()) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            }
            ImGui::PopID();
            return;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(enabled ? "Frame included" : "Frame excluded");
        }
        ImGui::SameLine(0.0f, 5.0f);
        const bool reference =
            active->referenceFrameId == frame.frameId;
        if (ImGui::RadioButton("##reference", reference)) {
            std::string error;
            SetMultiFrameReferenceFrame(
                activeSetId,
                frame.frameId,
                &error);
            if (!error.empty()) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            }
            ImGui::PopID();
            return;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                reference ? "Reference frame" : "Make this the reference frame");
        }
        ImGui::SameLine(0.0f, 6.0f);
        const char* frameLabel = frame.userLabel.empty()
            ? (asset ? asset->originalFileName.c_str() : "Missing asset")
            : frame.userLabel.c_str();
        if (ImGui::Selectable(
                frameLabel,
                snapshot.activeFrameId == frame.frameId,
                ImGuiSelectableFlags_None,
                ImVec2(-1.0f, 0.0f))) {
            ActivateMultiFrameFrame(
                activeSetId,
                frame.frameId,
                true);
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Add Frames")) {
        const std::vector<std::string> selected =
            FileDialogs::OpenMultipleFilesDialog("Add Frames to MFD Burst");
        std::vector<std::filesystem::path> paths;
        paths.reserve(selected.size());
        for (const std::string& path : selected) {
            paths.emplace_back(path);
        }
        if (!paths.empty()) {
            std::string error;
            const bool added = AddFramesToMultiFrameSourceSet(
                activeSetId,
                paths,
                &error);
            m_RawWorkspaceLabUi.multiFrameStatusText = added
                ? "Frames embedded and verified."
                : error;
            return;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Manage...")) {
        m_RawWorkspaceLabUi.secondarySheetOpen = true;
        m_RawLabMultiFrameAdvancedOpenedThisFrame = true;
    }

    if (active->operationIntent !=
            MultiFrameOperationIntent::RawBurstDenoise ||
        active->operationSchemaVersion <
            Stack::Project::kMfdMosaicPlaceholderSchemaVersion) {
        ImGui::Spacing();
        ImGui::TextColored(
            ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
            "Processing is not implemented for this source-set operation.");
        return;
    }

    Raw::Mfd::Parameters displayedParameters;
    std::string displayedParameterError;
    const auto parameterValue = active->settings.find("parameters");
    const bool haveParameters =
        parameterValue != active->settings.end() &&
        Raw::Mfd::DeserializeParameters(
            *parameterValue,
            displayedParameters,
            &displayedParameterError);
    if (haveParameters &&
        (!m_MfdExperimentalParameterDraft.initialized ||
         m_MfdExperimentalParameterDraft.sourceSetId != activeSetId ||
         m_MfdExperimentalParameterDraft.inputRevision !=
             snapshot.mfdInputRevision)) {
        m_MfdExperimentalParameterDraft.sourceSetId = activeSetId;
        m_MfdExperimentalParameterDraft.inputRevision =
            snapshot.mfdInputRevision;
        m_MfdExperimentalParameterDraft
            .motionDisagreementHardLimitRawPixels =
            displayedParameters.registration
                .motionDisagreementHardLimitRawPixels;
        m_MfdExperimentalParameterDraft.trustedPixelZeroWeightSigma =
            displayedParameters.reliability.trustedPixelZeroWeightSigma;
        m_MfdExperimentalParameterDraft
            .oneAlternateWeightCapRelativeToReference =
            displayedParameters.fusion
                .oneAlternateWeightCapRelativeToReference;
        m_MfdExperimentalParameterDraft
            .exactFallbackAlternateToReferenceRatio =
            displayedParameters.fusion
                .exactFallbackAlternateToReferenceRatio;
        m_MfdExperimentalParameterDraft.memoryBudgetGiB =
            active->settings.value(
                "experimentalMemoryBudgetGiB",
                0.0);
        Raw::Mfd::MfdAlignmentMode storedAlignmentMode =
            Raw::Mfd::MfdAlignmentMode::Full;
        Raw::Mfd::ParseMfdAlignmentMode(
            active->settings.value(
                "experimentalAlignmentMode",
                std::string("full")),
            storedAlignmentMode);
        m_MfdExperimentalParameterDraft.alignmentMode =
            static_cast<int>(storedAlignmentMode);
        m_MfdExperimentalParameterDraft.initialized = true;
    }

    const bool processingBusy = IsMfdExperimentalProcessingBusy();
    const bool processingThisSet =
        m_MfdExperimentalProcessingProjectId == snapshot.projectId &&
        m_MfdExperimentalProcessingSourceSetId == activeSetId;
    ImGui::Spacing();
    ImGui::SeparatorText("Processing");
    if (haveParameters) {
        static constexpr const char* alignmentModes[] {
            "Full alignment",
            "Translation only",
            "None - fixed camera"
        };
        ImGui::BeginDisabled(processingBusy);
        ImGui::SetNextItemWidth(-1.0f);
        const bool alignmentChanged = ImGui::Combo(
            "##MfdAlignmentMode",
            &m_MfdExperimentalParameterDraft.alignmentMode,
            alignmentModes,
            IM_ARRAYSIZE(alignmentModes));
        ImGui::EndDisabled();
        if (alignmentChanged) {
            std::string error;
            if (!SetMfdExperimentalAlignmentMode(
                    activeSetId,
                    static_cast<Raw::Mfd::MfdAlignmentMode>(
                        m_MfdExperimentalParameterDraft.alignmentMode),
                    &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText =
                    "Alignment mode saved. Process the burst again.";
            }
            return;
        }
        switch (static_cast<Raw::Mfd::MfdAlignmentMode>(
            m_MfdExperimentalParameterDraft.alignmentMode)) {
            case Raw::Mfd::MfdAlignmentMode::Full:
                ImGui::TextDisabled("Global, affine, and local motion");
                break;
            case Raw::Mfd::MfdAlignmentMode::TranslationOnly:
                ImGui::TextDisabled("Whole-frame translation only");
                break;
            case Raw::Mfd::MfdAlignmentMode::Identity:
                ImGui::TextColored(
                    ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
                    "No alignment; coordinates and exposure must match");
                break;
        }
    } else {
        ImGui::TextColored(
            ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
            "%s",
            displayedParameterError.empty()
                ? "The RA-CFA V1 parameter object is invalid."
                : displayedParameterError.c_str());
    }

    const bool canProcess =
        setStatus ==
            Stack::Project::MultiFrameSetStatus::ReadyForFutureProcessing &&
        !processingBusy;
    if (processingBusy && processingThisSet) {
        if (ImGui::Button("Cancel Processing", ImVec2(-1.0f, 0.0f))) {
            CancelMfdExperimentalProcessing();
        }
    } else {
        ImGui::BeginDisabled(!canProcess);
        if (ImGui::Button("Process Burst", ImVec2(-1.0f, 0.0f))) {
            std::string error;
            if (!StartMfdExperimentalProcessing(activeSetId, &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText.clear();
            }
        }
        ImGui::EndDisabled();
    }
    if (processingBusy && !processingThisSet) {
        ImGui::TextDisabled("Another source set is processing.");
    }
    if (processingBusy && processingThisSet &&
        m_MfdExperimentalProcessingProgress) {
        std::string stageLabel;
        std::string progressMessage;
        double overallFraction = 0.0;
        {
            std::lock_guard<std::mutex> lock(
                m_MfdExperimentalProcessingProgress->mutex);
            stageLabel =
                m_MfdExperimentalProcessingProgress->stageLabel;
            progressMessage =
                m_MfdExperimentalProcessingProgress->message;
            overallFraction =
                m_MfdExperimentalProcessingProgress->overallFraction;
        }
        char overlay[32] {};
        std::snprintf(
            overlay,
            sizeof(overlay),
            "%.1f%%",
            overallFraction * 100.0);
        ImGui::ProgressBar(
            static_cast<float>(overallFraction),
            ImVec2(-1.0f, 0.0f),
            overlay);
        if (!stageLabel.empty()) {
            ImGui::TextUnformatted(stageLabel.c_str());
        }
        if (!progressMessage.empty()) {
            ImGui::TextDisabled("%s", progressMessage.c_str());
        }
    }

    const bool adoptedResultAvailable =
        m_MfdAdoptedRawResult &&
        m_MfdAdoptedRawResult->projectId == snapshot.projectId &&
        m_MfdAdoptedRawResult->sourceSetId == activeSetId &&
        m_MfdAdoptedRawResult->inputRevision ==
            snapshot.mfdInputRevision &&
        m_MfdAdoptedRawResult->rawData;
    ImGui::TextColored(
        adoptedResultAvailable
            ? ImVec4(0.58f, 0.86f, 0.66f, 1.0f)
            : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        adoptedResultAvailable
            ? "Developed result ready"
            : "No current processed result");

    const MfdExperimentalProcessingReport* report =
        m_MfdExperimentalProcessingReport &&
            m_MfdExperimentalProcessingReport->projectId == snapshot.projectId &&
            m_MfdExperimentalProcessingReport->sourceSetId == activeSetId
        ? &(*m_MfdExperimentalProcessingReport)
        : nullptr;
    if (report &&
        report->inputRevision == snapshot.mfdInputRevision) {
        ImGui::TextDisabled(
            "%llu/%llu alternates accepted - %.1f%% pixels combined",
            static_cast<unsigned long long>(report->acceptedAlternateCount),
            static_cast<unsigned long long>(report->compatibleAlternateCount),
            report->contributingPixelFraction * 100.0);
        ImGui::TextDisabled(
            "Mean effective samples per pixel: %.2f",
            report->meanEffectiveSampleCount);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Accepted frames can still receive conservative weights. "
                "This number is the average noise-reduction depth actually used per pixel.");
        }
        if (report->compatibleAlternateCount > 0u &&
            (report->acceptedAlternateCount == 0u ||
             report->contributingPixelFraction <= 0.000001)) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                "No denoise contribution: the output is the reference frame.");
            ImGui::TextWrapped(
                "Every alternate was rejected by compatibility, alignment, or fusion safety checks. "
                "Open details to see each frame decision.");
        }
    }
    if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "%s",
            m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
    }
}

void EditorModule::RenderMultiFrameRawLabAdvanced() {
    if (!IsMultiFrameRawProjectActive()) {
        if (IsRawWorkspaceProjectActive()) {
            ImGui::TextWrapped(
                "This is a legacy single-RAW project. It opens unchanged. "
                "Adding source sets requires an explicit upgraded copy.");
            if (ImGui::Button("Save As Upgraded Project")) {
                const std::string folder = FileDialogs::OpenFolderDialog(
                    "Choose Directory for Upgraded RAW Project");
                if (!folder.empty()) {
                    const std::string baseName = m_CurrentProjectName.empty()
                        ? "RAW Project"
                        : m_CurrentProjectName;
                    std::string error;
                    if (!UpgradeActiveLegacyRawProjectToMultiFrame(
                            std::filesystem::path(folder) /
                                (baseName + " Upgraded"),
                            &error)) {
                        m_RawWorkspaceLabUi.multiFrameStatusText = error;
                    } else {
                        m_RawWorkspaceLabUi.multiFrameStatusText =
                            "Legacy project upgraded into a separate embedded bundle.";
                    }
                }
            }
        } else {
            ImGui::TextDisabled("No multi-frame RAW project is active.");
            if (ImGui::Button("Create New MFD Project")) {
                m_RawWorkspaceLabUi.multiFrameStatusText.clear();
                m_OpenMultiFrameCreationPopup = true;
            }
        }
        RenderMultiFrameRawLabCreationPopup();
        if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
            ImGui::TextWrapped(
                "%s", m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
        }
        return;
    }

    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (const EditorNodeGraph::Node* selectedNode =
            m_NodeGraph.FindNode(selectedNodeId);
        selectedNode) {
        if (selectedNode->kind == EditorNodeGraph::NodeKind::RawProjectFrame) {
            ActivateMultiFrameFrame(
                selectedNode->rawProjectFrame.sourceSetId,
                selectedNode->rawProjectFrame.frameId,
                false);
        } else if (selectedNode->kind ==
                   EditorNodeGraph::NodeKind::MultiFrameDenoise) {
            ActivateMultiFrameSourceSet(
                selectedNode->multiFrameDenoise.sourceSetId);
        } else if (selectedNode->kind ==
                   EditorNodeGraph::NodeKind::RawProjectSourceSet &&
                   Stack::Project::FindSourceSet(
                       *m_ActiveRawProjectSnapshot,
                       selectedNode->rawProjectSourceSet.sourceSetId)) {
            ActivateMultiFrameSourceSet(
                selectedNode->rawProjectSourceSet.sourceSetId);
        }
    }

    RawProjectSnapshot& snapshot = *m_ActiveRawProjectSnapshot;
    ImGui::TextUnformatted(snapshot.projectName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s · %llu set(s) · %llu frame(s)",
        Stack::Project::ProjectStorageKindName(
            m_ActiveRawProjectStore->StorageKind()),
        static_cast<unsigned long long>(snapshot.sourceSets.size()),
        static_cast<unsigned long long>([&]() {
            std::uint64_t count = 0;
            for (const MultiFrameSourceSet& set : snapshot.sourceSets) {
                count += static_cast<std::uint64_t>(set.frames.size());
            }
            return count;
        }()));
    ImGui::TextDisabled("%s", m_ActiveRawWorkspaceProjectPath.string().c_str());
    const Stack::Project::ProjectLifecyclePhase phase =
        m_ProjectSessionController.Phase();
    if (phase == Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.68f, 0.25f, 1.0f),
            "Recovery mode: use Save Repaired Copy; in-place saves are disabled.");
    } else if (phase == Stack::Project::ProjectLifecyclePhase::SaveFailed) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
            "The last save failed. The project remains open and dirty.");
    }
    if (phase == Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery) {
        if (ImGui::SmallButton("Save Repaired Copy")) {
            const std::string folder = FileDialogs::OpenFolderDialog(
                "Choose Directory for Repaired Project Bundle");
            if (!folder.empty()) {
                std::string error;
                const std::filesystem::path destination =
                    std::filesystem::path(folder) / snapshot.projectName;
                if (!SaveActiveMultiFrameRawProjectAs(
                        destination,
                        Stack::Project::ProjectStorageKind::DirectoryBundle,
                        &error)) {
                    m_RawWorkspaceLabUi.multiFrameStatusText = error;
                } else {
                    m_RawWorkspaceLabUi.multiFrameStatusText =
                        "Repaired project copy saved.";
                }
            }
        }
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(
        m_ActiveRawProjectStore->StorageKind() !=
            Stack::Project::ProjectStorageKind::PortableFile ||
        phase == Stack::Project::ProjectLifecyclePhase::Conflict ||
        phase == Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery);
    if (ImGui::SmallButton("Optimize Project")) {
        std::string error;
        if (!OptimizeActiveMultiFrameRawProject(&error)) {
            m_RawWorkspaceLabUi.multiFrameStatusText = error;
        } else {
            m_RawWorkspaceLabUi.multiFrameStatusText = "Portable project compacted successfully.";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Close Project")) {
        m_ShowRawWorkspaceCloseProjectPopup = true;
    }

    ImGui::Separator();

    if (ImGui::SmallButton("Project Frames")) {
        m_MfdBrowseWorkspace = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse Workspace")) {
        m_MfdBrowseWorkspace = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s",
        m_MfdBrowseWorkspace
            ? "Browsing never replaces the active MFD project"
            : "Project gallery");
    if (m_MfdBrowseWorkspace) {
        ImGui::SeparatorText("Workspace RAW Files");
        RenderRawWorkspaceLabGalleryContent(false);
        return;
    }

    MultiFrameSourceSet* active = Stack::Project::FindSourceSet(
        snapshot, snapshot.activeSourceSetId);
    const bool focusedMfdProject =
        snapshot.sourceSets.size() == 1u && active &&
        active->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        active->operationSchemaVersion >= Stack::Project::kMfdMosaicPlaceholderSchemaVersion;
    const char* activeLabel = active ? active->name.c_str() : "No source set";
    if (ImGui::BeginCombo("Source set", activeLabel)) {
        for (const MultiFrameSourceSet& sourceSet : snapshot.sourceSets) {
            const bool selected = active &&
                active->sourceSetId == sourceSet.sourceSetId;
            if (ImGui::Selectable(sourceSet.name.c_str(), selected)) {
                ActivateMultiFrameSourceSet(sourceSet.sourceSetId);
                active = Stack::Project::FindSourceSet(
                    *m_ActiveRawProjectSnapshot, sourceSet.sourceSetId);
            }
        }
        ImGui::EndCombo();
    }

    static char newSetName[160] = "New Source Set";
    static int newSetIntent = 0;
    ImGui::BeginDisabled(focusedMfdProject);
    if (ImGui::Button("New Set")) ImGui::OpenPopup("New Multi-Frame Source Set");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(active == nullptr || focusedMfdProject);
    if (ImGui::Button("Duplicate")) {
        std::string error;
        if (!DuplicateMultiFrameSourceSet(active->sourceSetId, &error)) {
            m_RawWorkspaceLabUi.multiFrameStatusText = error;
        } else {
            m_RawWorkspaceLabUi.multiFrameStatusText = "Source set duplicated without duplicating media bytes.";
        }
        ImGui::EndDisabled();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename")) ImGui::OpenPopup("Rename Multi-Frame Source Set");
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        m_PendingDeleteMultiFrameSourceSetId = active->sourceSetId;
        m_OpenMultiFrameDeletePopup = true;
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal(
            "New Multi-Frame Source Set", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", newSetName, sizeof(newSetName));
        ImGui::RadioButton("MFSR", &newSetIntent, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Burst Denoise", &newSetIntent, 1);
        if (ImGui::Button("Choose Frames and Create")) {
            const std::vector<std::string> selected =
                FileDialogs::OpenMultipleFilesDialog("Add Source Set Frames");
            std::vector<std::filesystem::path> paths;
            for (const std::string& path : selected) paths.emplace_back(path);
            std::string error;
            const bool created = AddMultiFrameSourceSet(
                newSetName,
                newSetIntent == 0
                    ? MultiFrameOperationIntent::Mfsr
                    : MultiFrameOperationIntent::RawBurstDenoise,
                paths,
                &error);
            m_RawWorkspaceLabUi.multiFrameStatusText = created
                ? "Source set created."
                : error;
            if (created) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static char renameSetName[160] = {};
    if (ImGui::BeginPopupModal(
            "Rename Multi-Frame Source Set", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (renameSetName[0] == '\0' && active) {
            std::snprintf(renameSetName, sizeof(renameSetName), "%s", active->name.c_str());
        }
        ImGui::InputText("Name", renameSetName, sizeof(renameSetName));
        if (ImGui::Button("Rename") && active) {
            std::string error;
            const bool renamed = RenameMultiFrameSourceSet(
                active->sourceSetId, renameSetName, &error);
            m_RawWorkspaceLabUi.multiFrameStatusText = renamed
                ? "Source set renamed."
                : error;
            if (renamed) {
                renameSetName[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            renameSetName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    RenderMultiFrameSourceSetDeletePopup();
    RenderMultiFrameFrameDeletePopup();
    if (!active) {
        ImGui::TextDisabled("Create a source set to begin organizing frames.");
        return;
    }

    if (focusedMfdProject) {
        ImGui::TextDisabled("Operation: MFD / Mosaic CFA Burst Denoise");
    } else {
        int intent = active->operationIntent == MultiFrameOperationIntent::Mfsr ? 0 : 1;
        const char* intentLabels[] = { "MFSR", "Burst Denoise" };
        if (ImGui::Combo("Operation", &intent, intentLabels, 2)) {
            std::string error;
            if (!SetMultiFrameOperationIntent(
                    active->sourceSetId,
                    intent == 0 ? MultiFrameOperationIntent::Mfsr
                                : MultiFrameOperationIntent::RawBurstDenoise,
                    &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            }
            return;
        }
    }
    if (active->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        active->operationSchemaVersion >= Stack::Project::kMfdMosaicPlaceholderSchemaVersion) {
        bool internalView = active->settings.value(
            "viewTransformPlacement", std::string("internal")) != "graph";
        if (ImGui::Checkbox("Apply View Transform inside the MFD RAW workflow", &internalView)) {
            std::string error;
            if (!SetMfdInternalViewTransformEnabled(
                    active->sourceSetId, internalView, &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText = internalView
                    ? "View Transform moved back inside the MFD RAW workflow."
                    : "One View Transform was inserted immediately before Output.";
            }
            return;
        }
        ImGui::TextDisabled(
            internalView
                ? "Display mapping is internal; reconnecting Output will not create a duplicate transform."
                : "MFD output is scene-linear and exactly one graph View Transform owns display mapping.");
    }
    std::string setReason;
    const Stack::Project::MultiFrameSetStatus setStatus =
        Stack::Project::EvaluateSourceSetStatus(snapshot, *active, &setReason);
    if (setStatus == Stack::Project::MultiFrameSetStatus::Incompatible) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "%s", setReason.c_str());
    } else if (setStatus == Stack::Project::MultiFrameSetStatus::Draft) {
        ImGui::TextColored(ImVec4(0.92f, 0.76f, 0.42f, 1.0f), "%s", setReason.c_str());
    } else {
        ImGui::TextDisabled("%s", setReason.c_str());
    }

    if (ImGui::Button("Add Frames")) {
        const std::vector<std::string> selected =
            FileDialogs::OpenMultipleFilesDialog("Add Frames to Source Set");
        std::vector<std::filesystem::path> paths;
        for (const std::string& path : selected) paths.emplace_back(path);
        if (!paths.empty()) {
            std::string error;
            const bool added = AddFramesToMultiFrameSourceSet(
                active->sourceSetId, paths, &error);
            m_RawWorkspaceLabUi.multiFrameStatusText = added
                ? "Frames embedded and verified."
                : error;
            return;
        }
    }

    ImGui::SeparatorText("Frames");
    const std::string activeSetId = active->sourceSetId;
    for (std::size_t index = 0; index < active->frames.size(); ++index) {
        const SourceSetFrame& frame = active->frames[index];
        const EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
        ImGui::PushID(frame.frameId.c_str());
        bool enabled = frame.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            std::string error;
            SetMultiFrameFrameEnabled(activeSetId, frame.frameId, enabled, &error);
            if (!error.empty()) m_RawWorkspaceLabUi.multiFrameStatusText = error;
            ImGui::PopID();
            return;
        }
        ImGui::SameLine();
        const char* frameLabel = frame.userLabel.empty()
            ? (asset ? asset->originalFileName.c_str() : "Missing asset")
            : frame.userLabel.c_str();
        const bool activeFrame = snapshot.activeFrameId == frame.frameId;
        if (ImGui::Selectable(
                frameLabel,
                activeFrame,
                ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(std::min(220.0f, ImGui::GetContentRegionAvail().x * 0.34f), 0.0f))) {
            ActivateMultiFrameFrame(activeSetId, frame.frameId, true);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s · embedded · %llu bytes",
            asset ? Stack::Project::MultiFrameInputFamilyName(asset->inputFamily) : "missing",
            static_cast<unsigned long long>(asset ? asset->byteLength : 0));
        ImGui::SameLine();
        const bool reference = active->referenceFrameId == frame.frameId;
        if (ImGui::RadioButton("Reference", reference)) {
            std::string error;
            SetMultiFrameReferenceFrame(activeSetId, frame.frameId, &error);
            if (!error.empty()) m_RawWorkspaceLabUi.multiFrameStatusText = error;
            ImGui::PopID();
            return;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Up")) {
            MoveMultiFrameFrame(activeSetId, index, -1, nullptr);
            ImGui::PopID();
            return;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down")) {
            MoveMultiFrameFrame(activeSetId, index, 1, nullptr);
            ImGui::PopID();
            return;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            m_PendingDeleteMultiFrameFrameSetId = activeSetId;
            m_PendingDeleteMultiFrameFrameId = frame.frameId;
            m_OpenMultiFrameFrameDeletePopup = true;
        }
        int orientation = frame.metadataOverrides.value("orientation", 0);
        const char* orientationLabels[] = {
            "Use capture metadata", "Normal", "Mirror horizontal",
            "Rotate 180", "Mirror vertical", "Mirror + rotate 90 CW",
            "Rotate 90 CW", "Mirror + rotate 90 CCW", "Rotate 90 CCW"
        };
        if (ImGui::Combo(
                "Orientation interpretation",
                &orientation,
                orientationLabels,
                IM_ARRAYSIZE(orientationLabels))) {
            std::string error;
            SetMultiFrameFrameOrientation(
                activeSetId, frame.frameId, orientation, &error);
            if (!error.empty()) m_RawWorkspaceLabUi.multiFrameStatusText = error;
            ImGui::PopID();
            return;
        }
        if (asset) {
            RawCaptureCompatibilitySummary summary;
            if (Stack::Project::DeserializeRawCaptureCompatibilitySummary(
                    asset->captureMetadataSummary, summary, nullptr)) {
                ImGui::TextDisabled(
                    "%s %s - %s - %dx%d visible - %d-bit - ISO %.0f",
                    summary.cameraMake.c_str(),
                    summary.cameraModel.c_str(),
                    summary.cfaPattern.c_str(),
                    summary.visibleWidth,
                    summary.visibleHeight,
                    summary.bitDepth,
                    summary.isoSpeed);
            }
        }
        ImGui::PopID();
    }
    if (active->operationIntent !=
            MultiFrameOperationIntent::RawBurstDenoise ||
        active->operationSchemaVersion <
            Stack::Project::kMfdMosaicPlaceholderSchemaVersion) {
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
            "Processing is not implemented for this operation yet");
        ImGui::TextWrapped(
            "The experimental processing bridge currently accepts only a "
            "Mosaic CFA Burst Denoise source set.");
        if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
            ImGui::TextWrapped(
                "%s", m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
        }
        return;
    }
    ImGui::SeparatorText("Experimental Bayer Processing");
    const bool processingBusy = IsMfdExperimentalProcessingBusy();
    const bool processingThisSet =
        m_MfdExperimentalProcessingProjectId == snapshot.projectId &&
        m_MfdExperimentalProcessingSourceSetId == activeSetId;
    const bool canProcess =
        setStatus == Stack::Project::MultiFrameSetStatus::ReadyForFutureProcessing &&
        !processingBusy;
    ImGui::BeginDisabled(!canProcess);
    if (ImGui::Button("Process Bayer burst (experimental)")) {
        std::string error;
        if (!StartMfdExperimentalProcessing(activeSetId, &error)) {
            m_RawWorkspaceLabUi.multiFrameStatusText = error;
        } else {
            m_RawWorkspaceLabUi.multiFrameStatusText.clear();
        }
    }
    ImGui::EndDisabled();
    if (processingBusy) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel processing")) {
            CancelMfdExperimentalProcessing();
        }
    }
    if (processingBusy && !processingThisSet) {
        ImGui::TextDisabled(
            "Another source set is currently processing. Cancel it before starting this set.");
    }
    if (processingBusy && processingThisSet &&
        m_MfdExperimentalProcessingProgress) {
        std::string stageLabel;
        std::string progressMessage;
        double overallFraction = 0.0;
        double stageFraction = 0.0;
        std::uint64_t completedUnits = 0u;
        std::uint64_t totalUnits = 0u;
        std::uint32_t frameOrdinal = 0u;
        std::uint32_t frameCount = 0u;
        std::chrono::steady_clock::time_point startedAt;
        std::chrono::steady_clock::time_point lastAdvancedAt;
        {
            std::lock_guard<std::mutex> lock(
                m_MfdExperimentalProcessingProgress->mutex);
            stageLabel =
                m_MfdExperimentalProcessingProgress->stageLabel;
            progressMessage =
                m_MfdExperimentalProcessingProgress->message;
            overallFraction =
                m_MfdExperimentalProcessingProgress->overallFraction;
            stageFraction =
                m_MfdExperimentalProcessingProgress->stageFraction;
            completedUnits =
                m_MfdExperimentalProcessingProgress->completedUnits;
            totalUnits =
                m_MfdExperimentalProcessingProgress->totalUnits;
            frameOrdinal =
                m_MfdExperimentalProcessingProgress->frameOrdinal;
            frameCount =
                m_MfdExperimentalProcessingProgress->frameCount;
            startedAt = m_MfdExperimentalProcessingProgress->startedAt;
            lastAdvancedAt =
                m_MfdExperimentalProcessingProgress->lastAdvancedAt;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - startedAt;
        const auto sinceAdvance = now - lastAdvancedAt;
        const float availableWidth = std::max(
            160.0f, ImGui::GetContentRegionAvail().x);
        char overlay[32] {};
        std::snprintf(
            overlay,
            sizeof(overlay),
            "%.1f%%",
            overallFraction * 100.0);
        ImGui::ProgressBar(
            static_cast<float>(overallFraction),
            ImVec2(availableWidth, 0.0f),
            overlay);
        ImGui::Text("%s", stageLabel.c_str());
        if (!progressMessage.empty()) {
            ImGui::TextWrapped("%s", progressMessage.c_str());
        }
        if (totalUnits > 0u) {
            ImGui::TextDisabled(
                "Stage progress: %llu / %llu (%.1f%%)",
                static_cast<unsigned long long>(completedUnits),
                static_cast<unsigned long long>(totalUnits),
                stageFraction * 100.0);
        } else if (frameOrdinal > 0u && frameCount > 0u) {
            ImGui::TextDisabled(
                "Frame %u of %u - stage %.1f%%",
                frameOrdinal,
                frameCount,
                stageFraction * 100.0);
        } else {
            ImGui::TextDisabled(
                "Current stage: %.1f%%",
                stageFraction * 100.0);
        }
        ImGui::TextDisabled(
            "Elapsed: %s - last checkpoint: %s ago",
            FormatMfdElapsed(elapsed).c_str(),
            FormatMfdElapsed(sinceAdvance).c_str());
        if (sinceAdvance >= std::chrono::seconds(30)) {
            ImGui::TextColored(
                ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
                "No checkpoint has completed recently. This is not a failure by itself; Cancel remains safe.");
        }
    }

    Raw::Mfd::Parameters displayedParameters;
    std::string displayedParameterError;
    const auto parameterValue = active->settings.find("parameters");
    const bool haveParameters =
        parameterValue != active->settings.end() &&
        Raw::Mfd::DeserializeParameters(
            *parameterValue,
            displayedParameters,
            &displayedParameterError);
    if (haveParameters &&
        (!m_MfdExperimentalParameterDraft.initialized ||
         m_MfdExperimentalParameterDraft.sourceSetId != activeSetId ||
         m_MfdExperimentalParameterDraft.inputRevision !=
             snapshot.mfdInputRevision)) {
        m_MfdExperimentalParameterDraft.sourceSetId = activeSetId;
        m_MfdExperimentalParameterDraft.inputRevision =
            snapshot.mfdInputRevision;
        m_MfdExperimentalParameterDraft.motionDisagreementHardLimitRawPixels =
            displayedParameters.registration
                .motionDisagreementHardLimitRawPixels;
        m_MfdExperimentalParameterDraft.trustedPixelZeroWeightSigma =
            displayedParameters.reliability.trustedPixelZeroWeightSigma;
        m_MfdExperimentalParameterDraft
            .oneAlternateWeightCapRelativeToReference =
            displayedParameters.fusion
                .oneAlternateWeightCapRelativeToReference;
        m_MfdExperimentalParameterDraft
            .exactFallbackAlternateToReferenceRatio =
            displayedParameters.fusion
                .exactFallbackAlternateToReferenceRatio;
        m_MfdExperimentalParameterDraft.memoryBudgetGiB =
            active->settings.value(
                "experimentalMemoryBudgetGiB", 0.0);
        Raw::Mfd::MfdAlignmentMode storedAlignmentMode =
            Raw::Mfd::MfdAlignmentMode::Full;
        Raw::Mfd::ParseMfdAlignmentMode(
            active->settings.value(
                "experimentalAlignmentMode", std::string("full")),
            storedAlignmentMode);
        m_MfdExperimentalParameterDraft.alignmentMode =
            static_cast<int>(storedAlignmentMode);
        m_MfdExperimentalParameterDraft.initialized = true;
    }

    if (!haveParameters) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
            "%s",
            displayedParameterError.empty()
                ? "The RA-CFA V1 parameter object is invalid."
                : displayedParameterError.c_str());
    } else if (ImGui::CollapsingHeader(
            "Processing resources",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        static constexpr const char* alignmentModes[] {
            "Full - global + local",
            "Translation only",
            "None - identity coordinates"
        };
        ImGui::BeginDisabled(processingBusy);
        ImGui::SetNextItemWidth(260.0f);
        const bool alignmentChanged = ImGui::Combo(
            "Alignment mode",
            &m_MfdExperimentalParameterDraft.alignmentMode,
            alignmentModes,
            IM_ARRAYSIZE(alignmentModes));
        ImGui::EndDisabled();
        switch (static_cast<Raw::Mfd::MfdAlignmentMode>(
            m_MfdExperimentalParameterDraft.alignmentMode)) {
            case Raw::Mfd::MfdAlignmentMode::Full:
                ImGui::TextDisabled(
                    "Estimates global translation, exposure, affine warp, and bidirectional local motion.");
                break;
            case Raw::Mfd::MfdAlignmentMode::TranslationOnly:
                ImGui::TextDisabled(
                    "Estimates whole-frame translation and exposure; skips affine and local motion search.");
                break;
            case Raw::Mfd::MfdAlignmentMode::Identity:
                ImGui::TextColored(
                    ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
                    "Testing mode: assumes identical coordinates and exposure. Tripod, crop, orientation, CFA, and camera settings must match.");
                ImGui::TextDisabled(
                    "Registration pyramids and all geometric/exposure fitting are skipped; residual and fusion safety checks remain active.");
                break;
        }
        if (alignmentChanged) {
            std::string error;
            if (!SetMfdExperimentalAlignmentMode(
                    activeSetId,
                    static_cast<Raw::Mfd::MfdAlignmentMode>(
                        m_MfdExperimentalParameterDraft.alignmentMode),
                    &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText =
                    "MFD alignment mode saved. Any earlier result is now stale.";
            }
            return;
        }
        ImGui::Separator();
        const Raw::Mfd::MfdProcessingMemoryBudgetDecision memoryDecision =
            Raw::Mfd::ResolveMfdProcessingMemoryBudget(
                m_MfdExperimentalParameterDraft.memoryBudgetGiB,
                Raw::Mfd::QueryPhysicalMemorySnapshot());
        if (memoryDecision.valid) {
            ImGui::Text(
                "Effective memory budget: %.2f GiB%s",
                static_cast<double>(memoryDecision.budgetBytes) /
                    Raw::Mfd::kMemoryPolicyGibibyte,
                memoryDecision.automatic ? " (automatic)" : "");
            if (memoryDecision.physicalMemory.valid) {
                ImGui::TextDisabled(
                    "Available now: %.2f GiB - protected reserve: %.2f GiB",
                    static_cast<double>(memoryDecision.physicalMemory
                        .availablePhysicalBytes) /
                        Raw::Mfd::kMemoryPolicyGibibyte,
                    static_cast<double>(memoryDecision.reserveBytes) /
                        Raw::Mfd::kMemoryPolicyGibibyte);
            }
            if (memoryDecision.constrainedToSafeCeiling) {
                ImGui::TextColored(
                    ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
                    "The requested manual budget is currently bounded to the safe ceiling.");
            }
        } else {
            ImGui::TextColored(
                ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
                "%s",
                memoryDecision.message.c_str());
        }
        ImGui::BeginDisabled(processingBusy);
        ImGui::SetNextItemWidth(150.0f);
        const bool commitMemoryBudget = ImGui::InputDouble(
            "Memory budget (GiB, 0 = automatic)",
            &m_MfdExperimentalParameterDraft.memoryBudgetGiB,
            0.0,
            0.0,
            "%.2f",
            ImGuiInputTextFlags_EnterReturnsTrue);
        bool applyMemoryBudget = commitMemoryBudget;
        if (ImGui::Button("Apply memory budget")) {
            applyMemoryBudget = true;
        }
        ImGui::EndDisabled();
        if (applyMemoryBudget) {
            std::string error;
            if (!SetMfdExperimentalMemoryBudgetGiB(
                    activeSetId,
                    m_MfdExperimentalParameterDraft.memoryBudgetGiB,
                    &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText =
                    "MFD memory budget saved. It does not stale the last valid result.";
            }
            return;
        }
    }

    if (haveParameters && ImGui::CollapsingHeader("RA-CFA V1 parameters")) {
        ImGui::TextDisabled(
            "Only four high-impact parameters are exposed in this first pass. Press Enter to commit a value.");
        ImGui::BeginDisabled(processingBusy);
        bool commitParameters = false;
        ImGui::SetNextItemWidth(150.0f);
        commitParameters |= ImGui::InputDouble(
            "Motion disagreement hard limit (raw px)",
            &m_MfdExperimentalParameterDraft
                .motionDisagreementHardLimitRawPixels,
            0.0,
            0.0,
            "%.3f",
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Rejects locally inconsistent motion above this raw-pixel distance. Research range: 0.4-1.2; default: 0.75.");
        }
        ImGui::SetNextItemWidth(150.0f);
        commitParameters |= ImGui::InputDouble(
            "Trusted-pixel zero-weight residual (noise sigma)",
            &m_MfdExperimentalParameterDraft.trustedPixelZeroWeightSigma,
            0.0,
            0.0,
            "%.3f",
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "An alternate receives zero trusted-pixel weight at this standardized residual. Research range: 4-7; default: 5.");
        }
        ImGui::SetNextItemWidth(150.0f);
        commitParameters |= ImGui::InputDouble(
            "Single alternate weight cap (x reference)",
            &m_MfdExperimentalParameterDraft
                .oneAlternateWeightCapRelativeToReference,
            0.0,
            0.0,
            "%.3f",
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Caps one alternate's fusion weight relative to the reference. Research range: 2-8; default: 4.");
        }
        ImGui::SetNextItemWidth(150.0f);
        commitParameters |= ImGui::InputDouble(
            "Exact-fallback alternate/reference ratio",
            &m_MfdExperimentalParameterDraft
                .exactFallbackAlternateToReferenceRatio,
            0.0,
            0.0,
            "%.3f",
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Below this alternate/reference support ratio, the output copies the reference sample exactly. Research range: 0.01-0.10; default: 0.05.");
        }
        if (ImGui::Button("Apply parameter values")) {
            commitParameters = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset RA-CFA V1 defaults")) {
            const Raw::Mfd::Parameters defaults;
            m_MfdExperimentalParameterDraft
                .motionDisagreementHardLimitRawPixels =
                defaults.registration.motionDisagreementHardLimitRawPixels;
            m_MfdExperimentalParameterDraft.trustedPixelZeroWeightSigma =
                defaults.reliability.trustedPixelZeroWeightSigma;
            m_MfdExperimentalParameterDraft
                .oneAlternateWeightCapRelativeToReference =
                defaults.fusion.oneAlternateWeightCapRelativeToReference;
            m_MfdExperimentalParameterDraft
                .exactFallbackAlternateToReferenceRatio =
                defaults.fusion.exactFallbackAlternateToReferenceRatio;
            commitParameters = true;
        }
        ImGui::EndDisabled();
        if (commitParameters) {
            std::string error;
            if (!SetMfdExperimentalParameters(
                    activeSetId,
                    m_MfdExperimentalParameterDraft
                        .motionDisagreementHardLimitRawPixels,
                    m_MfdExperimentalParameterDraft
                        .trustedPixelZeroWeightSigma,
                    m_MfdExperimentalParameterDraft
                        .oneAlternateWeightCapRelativeToReference,
                    m_MfdExperimentalParameterDraft
                        .exactFallbackAlternateToReferenceRatio,
                    &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText =
                    "RA-CFA V1 parameters saved. Any earlier result is now stale.";
            }
            return;
        }
    }

    if (!m_MfdExperimentalProcessingStatusText.empty() &&
        processingThisSet) {
        ImGui::TextWrapped(
            "%s",
            m_MfdExperimentalProcessingStatusText.c_str());
    }

    const MfdExperimentalProcessingReport* report =
        m_MfdExperimentalProcessingReport &&
            m_MfdExperimentalProcessingReport->projectId == snapshot.projectId &&
            m_MfdExperimentalProcessingReport->sourceSetId == activeSetId
        ? &(*m_MfdExperimentalProcessingReport)
        : nullptr;
    if (report) {
        const bool stale =
            report->inputRevision != snapshot.mfdInputRevision;
        ImGui::SeparatorText(stale
            ? "Last valid inspection (stale)"
            : "Last valid inspection");
        ImGui::TextDisabled(
            "%s - input revision %llu%s",
            report->statusName.c_str(),
            static_cast<unsigned long long>(report->inputRevision),
            stale ? " - inputs or parameters changed" : "");
        ImGui::TextWrapped("%s", report->message.c_str());
        ImGui::Text(
            "Accepted alternates: %llu / %llu",
            static_cast<unsigned long long>(report->acceptedAlternateCount),
            static_cast<unsigned long long>(report->compatibleAlternateCount));
        ImGui::Text(
            "Pixels using alternates: %.2f%% - exact reference: %.2f%%",
            report->contributingPixelFraction * 100.0,
            report->exactReferencePixelFraction * 100.0);
        if (!stale && report->compatibleAlternateCount > 0u &&
            (report->acceptedAlternateCount == 0u ||
             report->contributingPixelFraction <= 0.000001)) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                "No frames contributed; this result is the exact reference image.");
        }
        ImGui::Text(
            "Mean |delta|: %.7f - P99 |delta|: %.7f - mean effective samples: %.3f",
            report->meanAbsoluteDelta,
            report->percentile99AbsoluteDelta,
            report->meanEffectiveSampleCount);
        ImGui::TextDisabled(
            "Estimated peak: %.1f MiB - budget: %.2f GiB%s",
            static_cast<double>(report->estimatedPeakResidentBytes) /
                (1024.0 * 1024.0),
            static_cast<double>(report->memoryBudgetBytes) /
                Raw::Mfd::kMemoryPolicyGibibyte,
            report->automaticMemoryBudget ? " automatic" : " manual");
        ImGui::TextDisabled(
            "Computed tiles: %llu - cache hits: %llu",
            static_cast<unsigned long long>(report->computedTileCount),
            static_cast<unsigned long long>(report->cacheHitTileCount));
        if (ImGui::Button("Show output preview")) {
            std::string error;
            if (!PlatformHelpers::RevealPathInExplorer(
                    report->outputPreviewPath, &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Show reference preview")) {
            std::string error;
            if (!PlatformHelpers::RevealPathInExplorer(
                    report->referencePreviewPath, &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            }
        }
        if (ImGui::CollapsingHeader("Frame decisions")) {
            for (const MfdExperimentalFrameReport& frame : report->frames) {
                ImGui::BulletText(
                    "%s - %s%s",
                    frame.label.c_str(),
                    frame.reference
                        ? "reference"
                        : !frame.attempted
                            ? "not attempted"
                            : frame.acceptedForFusion ? "accepted" : "rejected",
                    frame.message.empty() ? "" : (" - " + frame.message).c_str());
            }
        }
    }

    const bool adoptedResultAvailable =
        m_MfdAdoptedRawResult &&
        m_MfdAdoptedRawResult->projectId == snapshot.projectId &&
        m_MfdAdoptedRawResult->sourceSetId == activeSetId &&
        m_MfdAdoptedRawResult->inputRevision ==
            snapshot.mfdInputRevision &&
        m_MfdAdoptedRawResult->rawData;
    ImGui::TextColored(
        adoptedResultAvailable
            ? ImVec4(0.58f, 0.86f, 0.66f, 1.0f)
            : ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
        adoptedResultAvailable
            ? "Developed MFD result is active in RAW Lab and the graph"
            : "Process the current burst to publish its RAW result");
    ImGui::TextWrapped(
        "RA-CFA V1 publishes one normalized float Bayer mosaic. Stack then "
        "develops it once with the reference frame's gain map, white balance, "
        "camera matrices, orientation, and the shared post-MFD RAW recipe. "
        "The neutral inspection PNG remains a diagnostic comparison only.");
    ImGui::TextDisabled(
        "MFD input revision %llu - post-recipe revision %llu",
        static_cast<unsigned long long>(snapshot.mfdInputRevision),
        static_cast<unsigned long long>(snapshot.postRecipeRevision));
    ImGui::TextDisabled(
        "Canceled or failed runs never replace the current valid result. Input "
        "or MFD parameter changes make the developed result unavailable until "
        "the burst is processed again.");
    if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
        ImGui::TextWrapped("%s", m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
    }
}

void EditorModule::RenderMultiFrameSourceSetDeletePopup() {
    if (m_OpenMultiFrameDeletePopup) {
        ImGui::OpenPopup("Delete Source Set and Managed Node?");
        m_OpenMultiFrameDeletePopup = false;
    }
    if (!ImGui::BeginPopupModal(
            "Delete Source Set and Managed Node?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped(
        "This removes the source set, its managed graph node, and all links "
        "from that node in one project transaction. Shared embedded originals "
        "remain available to other sets.");
    if (ImGui::Button("Delete")) {
        std::string error;
        const bool deleted = DeleteMultiFrameSourceSet(
            m_PendingDeleteMultiFrameSourceSetId, &error);
        m_RawWorkspaceLabUi.multiFrameStatusText = deleted
            ? "Source set and managed node deleted."
            : error;
        if (deleted) {
            m_PendingDeleteMultiFrameSourceSetId.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_PendingDeleteMultiFrameSourceSetId.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorModule::RenderMultiFrameFrameDeletePopup() {
    if (m_OpenMultiFrameFrameDeletePopup) {
        ImGui::OpenPopup("Remove Frame from MFD Project?");
        m_OpenMultiFrameFrameDeletePopup = false;
    }
    if (!ImGui::BeginPopupModal(
            "Remove Frame from MFD Project?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped(
        "This removes the frame membership, its managed graph node, and its "
        "protected MFD link. The embedded original remains recoverable in "
        "the project asset store until a future compact/cleanup pass.");
    if (ImGui::Button("Remove Frame")) {
        std::string error;
        const bool removed = RemoveMultiFrameFrame(
            m_PendingDeleteMultiFrameFrameSetId,
            m_PendingDeleteMultiFrameFrameId,
            &error);
        m_RawWorkspaceLabUi.multiFrameStatusText = removed
            ? "Frame removed from the MFD burst."
            : error;
        if (removed) {
            m_PendingDeleteMultiFrameFrameSetId.clear();
            m_PendingDeleteMultiFrameFrameId.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_PendingDeleteMultiFrameFrameSetId.clear();
        m_PendingDeleteMultiFrameFrameId.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
