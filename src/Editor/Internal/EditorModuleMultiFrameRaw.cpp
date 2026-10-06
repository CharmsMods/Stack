#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"

#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "Raw/MultiFrame/GraphExecution.h"
#include "Persistence/BracketingProject.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/MultiFrameProjectCreation.h"
#include "Editor/Bracketing/BracketingSession.h"

#include "App/PlatformHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/MultiFrameDenoise/Contracts.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Utils/FileDialogs.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <exception>
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

RawProjectSnapshot CopyProjectSnapshotWithoutPipeline(
    const RawProjectSnapshot& source) {
    RawProjectSnapshot copy;
    copy.schemaVersion = source.schemaVersion;
    copy.projectId = source.projectId;
    copy.projectName = source.projectName;
    copy.projectKindHint = source.projectKindHint;
    copy.lifecycle = source.lifecycle;
    copy.embeddedAssets = source.embeddedAssets;
    copy.sourceSets = source.sourceSets;
    copy.multiFrameGraph = source.multiFrameGraph;
    copy.rawWorkspaceData = source.rawWorkspaceData;
    copy.coverThumbnailBytes = source.coverThumbnailBytes;
    copy.activeSourceSetId = source.activeSourceSetId;
    copy.activeFrameId = source.activeFrameId;
    copy.mfdInputRevision = source.mfdInputRevision;
    copy.hdrInputRevision = source.hdrInputRevision;
    copy.postRecipeRevision = source.postRecipeRevision;
    copy.dirtyRevision = source.dirtyRevision;
    copy.persistedStorageRevision = source.persistedStorageRevision;
    copy.timestamp = source.timestamp;
    copy.sourceWidth = source.sourceWidth;
    copy.sourceHeight = source.sourceHeight;
    copy.adoptedFrom = source.adoptedFrom;
    copy.sourceAssetId = source.sourceAssetId;
    copy.nodeBrowserThumbnails = source.nodeBrowserThumbnails;
    return copy;
}

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

bool ProbeRawBurstSources(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet,
    const std::vector<std::filesystem::path>& sourcePaths,
    std::vector<nlohmann::json>& captureSummaries,
    std::string& error,
    const std::function<bool()>& shouldCancel = {}) {
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
        if(shouldCancel && shouldCancel()) {error="Canceled.";return false;}
        std::error_code pathError;
        const std::filesystem::path normalized =
            std::filesystem::weakly_canonical(path, pathError);
        const std::string pathKey = Lower(
            (pathError ? path.lexically_normal() : normalized).string());
        if (!normalizedPaths.insert(pathKey).second) {
            error = "The same source file cannot be selected twice for one RAW burst.";
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
            auto comparison = candidate;
            if (sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet ||
                Stack::Project::IsBracketing(sourceSet)) comparison.orientation = reference.orientation;
            const bool compatible =
                sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr
                ? Stack::Project::AreHdrCapturesStructurallyCompatible(
                    reference, comparison, &compatibilityReason, nullptr)
                : Stack::Project::AreMfdCapturesStructurallyCompatible(
                    reference, comparison, &compatibilityReason);
            if (!compatible) {
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
    if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
        !Stack::Project::IsBracketing(sourceSet)) {
        std::vector<double> exposures;
        for (const nlohmann::json& value : captureSummaries) {
            RawCaptureCompatibilitySummary summary;
            if (!Stack::Project::DeserializeRawCaptureCompatibilitySummary(
                    value, summary, nullptr)) continue;
            if (summary.exposureTimeSeconds <= 0.0) continue;
            const double shutter = summary.exposureTimeSeconds;
            const double iso = summary.isoSpeed > 0.0 ? summary.isoSpeed : 100.0;
            const double aperture = summary.apertureFNumber > 0.0
                ? summary.apertureFNumber : 1.0;
            exposures.push_back(shutter * iso / (aperture * aperture));
        }
        if (exposures.size() >= 2u) {
            const auto limits = std::minmax_element(exposures.begin(), exposures.end());
            const double spanEv = std::log2(*limits.second / std::max(1.0e-12, *limits.first));
            if (!std::isfinite(spanEv) || spanEv < 0.5) {
                error = "The selected frames span less than 0.5 EV. Reopen this selection as Multi-Frame Denoise.";
                return false;
            }
        }
    }
    return true;
}

nlohmann::json DefaultMfdSettings() {
    return Stack::Project::MakeDefaultMfdOperationSettings();
}

nlohmann::json DefaultHdrSettings() {
    return Stack::Project::MakeDefaultHdrOperationSettings();
}

nlohmann::json DefaultCaptureSetSettings() {
    return {
        { "schemaVersion", Stack::Project::kMultiFrameOperationSchemaVersion },
        { "inputDomain", "mosaic-cfa" },
        { "processingNode", nullptr }
    };
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

void EnsureHdrPostRecipe(MultiFrameSourceSet& sourceSet) {
    const auto existing = sourceSet.settings.find("sharedPostHdrRecipe");
    if (existing != sourceSet.settings.end() && existing->is_object() &&
        existing->contains("rawRecipeVersion")) {
        Stack::RawRecipe::RawDevelopmentRecipe recipe =
            Stack::RawRecipe::DeserializeRecipe(*existing);
        if (recipe.technical.applyBaselineExposure) {
            recipe.technical.applyBaselineExposure = false;
            sourceSet.settings["sharedPostHdrRecipe"] =
                Stack::RawRecipe::SerializeRecipe(recipe);
        }
        return;
    }
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(
            "hdr://source-set/" + sourceSet.sourceSetId,
            sourceSet.name + " HDR result");
    recipe.technical.applyBaselineExposure = false;
    recipe.technical.mosaicDenoise.enabled = false;
    sourceSet.settings["sharedPostHdrRecipe"] =
        Stack::RawRecipe::SerializeRecipe(recipe);
}

bool StageFrames(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    RawProjectSnapshot& snapshot,
    MultiFrameSourceSet& sourceSet,
    const std::vector<std::filesystem::path>& sourcePaths,
    bool requireExistingFamily,
    std::string& error,
    const std::function<bool()>& shouldCancel = {},
    Stack::Project::ProjectCreationFailure* failure = nullptr) {
    if (!store || !transaction) {
        error = "The project store transaction is unavailable.";
        return false;
    }
    std::vector<nlohmann::json> captureSummaries;
    const bool rawBurst =
        sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet ||
        sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
        sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr;
    if (rawBurst && !ProbeRawBurstSources(
            snapshot, sourceSet, sourcePaths, captureSummaries, error, shouldCancel)) {
        return false;
    }
    std::unordered_set<std::string> setAssetIds;
    std::unordered_map<std::string, std::string> assetLabels;
    for (const SourceSetFrame& frame : sourceSet.frames) {
        setAssetIds.insert(frame.assetId);
        const EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
        assetLabels[frame.assetId] = asset && !asset->originalFilename.empty()
            ? asset->originalFilename
            : frame.userLabel;
    }
    const bool excludeIdenticalCopies =
        sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet ||
        sourceSet.settings.contains("bracketing");
    for (std::size_t sourceIndex = 0; sourceIndex < sourcePaths.size(); ++sourceIndex) {
        if(shouldCancel && shouldCancel()) {error="Canceled.";return false;}
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
            rawBurst
            ? captureSummaries[sourceIndex]
            : nlohmann::json::object();
        if (!store->StageAssetFile(
                transaction,
                path,
                family,
                captureSummary,
                asset,
                &error)) {
            if (failure) *failure = Stack::Project::ProjectCreationFailure::Storage;
            return false;
        }
        if (!setAssetIds.insert(asset.assetId).second) {
            if (excludeIdenticalCopies) {
                nlohmann::json& exclusions =
                    sourceSet.settings["identicalCaptureExclusions"];
                if (!exclusions.is_array()) exclusions = nlohmann::json::array();
                exclusions.push_back({
                    { "excluded", path.filename().string() },
                    { "kept", assetLabels[asset.assetId] },
                    { "assetId", asset.assetId }
                });
                continue;
            }
            error = "The same original cannot appear twice in one source set.";
            return false;
        }
        assetLabels[asset.assetId] = path.filename().string();
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
        for (const char* key : { "editorComposite", "editorTimeline", "rawLayerStack", "rawLayerSourceNodeUuid" }) {
            if (existingPipeline.contains(key)) pipeline[key] = existingPipeline[key];
        }
    }
    if (!pipeline.contains("rawLayerStack"))
        pipeline["rawLayerStack"] = Stack::Project::SerializeRawLayerStack(Stack::Project::RawLayerStackState{});
    return pipeline;
}

void ApplyManagedGraphImageReferences(
    EditorNodeGraph::Graph& graph,
    const nlohmann::json& pipeline) {
    if (!pipeline.is_object() ||
        !pipeline.contains("nodeGraph") ||
        !pipeline["nodeGraph"].is_object() ||
        !pipeline["nodeGraph"].contains("nodes") ||
        !pipeline["nodeGraph"]["nodes"].is_array()) {
        return;
    }
    for (const nlohmann::json& value : pipeline["nodeGraph"]["nodes"]) {
        if (!value.is_object() ||
            value.value("kind", std::string()) != "Image") {
            continue;
        }
        const std::string assetId =
            value.value("managedAssetId", std::string());
        if (assetId.empty()) continue;
        EditorNodeGraph::Node* node =
            graph.FindNode(value.value("id", 0));
        if (!node || node->kind != EditorNodeGraph::NodeKind::Image) {
            continue;
        }
        node->image.managedAssetId = assetId;
        node->image.projectAssetPath =
            value.value("projectAssetPath", std::string());
    }
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

EditorNodeGraph::Node* FindHdrNode(
    EditorNodeGraph::Graph& graph,
    const std::string& sourceSetId,
    const std::string& graphBindingNodeId = {}) {
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::MultiFrameHdr) continue;
        if ((!graphBindingNodeId.empty() && node.instanceUuid == graphBindingNodeId) ||
            node.multiFrameHdr.sourceSetId == sourceSetId) return &node;
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
        : (asset ? asset->originalFilename : std::string("RAW Frame"));
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
    mfd->title = "Shared Burst - " + sourceSet.name;
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

bool SyncHdrGraphTopology(
    EditorNodeGraph::Graph& graph,
    const RawProjectSnapshot& snapshot,
    MultiFrameSourceSet& sourceSet,
    bool ensureOutput,
    std::string& error) {
    EditorNodeGraph::Node* hdr = FindHdrNode(
        graph, sourceSet.sourceSetId, sourceSet.graphBindingNodeId);
    if (!hdr) {
        EditorNodeGraph::MultiFrameHdrPayload payload;
        payload.sourceSetId = sourceSet.sourceSetId;
        hdr = graph.AddMultiFrameHdrNode(std::move(payload), { 760.0f, 180.0f });
        if (!hdr) {
            error = "Could not create the managed Multi-Frame HDR graph node.";
            return false;
        }
    }
    hdr->title = "HDR - " + sourceSet.name;
    hdr->multiFrameHdr.sourceSetId = sourceSet.sourceSetId;
    hdr->multiFrameHdr.managed = true;
    hdr->multiFrameHdr.quarantined = false;
    const nlohmann::json resultState = sourceSet.settings.value(
        "result", nlohmann::json::object());
    const bool resultCurrent = resultState.is_object() &&
        resultState.value("state", std::string()) == "ready" &&
        resultState.value("inputRevision", std::uint64_t { 0 }) ==
            snapshot.hdrInputRevision;
    hdr->multiFrameHdr.presentationStatus = resultCurrent
        ? "Scene-linear HDR result ready."
        : EditorNodeGraph::kHdrAwaitingProcessingStatus;
    hdr->multiFrameHdr.resultState = resultCurrent ? "ready" : "unavailable";
    hdr->multiFrameHdr.internalViewTransformEnabled =
        sourceSet.settings.value(
            "viewTransformPlacement", std::string("internal")) != "graph";
    const nlohmann::json anchor = sourceSet.settings.value(
        "radiometricAnchorFrameId", nlohmann::json(nullptr));
    hdr->multiFrameHdr.radiometricAnchorFrameId =
        anchor.is_string() ? anchor.get<std::string>() : std::string();
    hdr->multiFrameHdr.frameBindings.clear();
    sourceSet.graphBindingNodeId = hdr->instanceUuid;
    const int hdrNodeId = hdr->id;

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
                error = "Could not create a managed HDR frame graph node.";
                return false;
            }
        }
        frameNode->rawProjectFrame.assetId = frame.assetId;
        frameNode->rawProjectFrame.displayLabel = frame.userLabel;
        frameNode->rawProjectFrame.enabled = frame.enabled;
        frameNode->rawProjectFrame.reference =
            sourceSet.referenceFrameId == frame.frameId;
        frameNode->rawProjectFrame.compatibilityStatus = frame.enabled
            ? "Mosaic CFA HDR bracket - embedded"
            : "Excluded from HDR";
        frameNode->title = frame.userLabel.empty() ? "RAW Frame" : frame.userLabel;
        hdr = graph.FindNode(hdrNodeId);
        if (!hdr) {
            error = "The managed HDR node disappeared while building the graph.";
            return false;
        }
        hdr->multiFrameHdr.frameBindings.push_back(
            BuildMfdFrameBinding(snapshot, sourceSet, frame));
    }

    std::vector<int> staleFrameNodes;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
            node.rawProjectFrame.sourceSetId == sourceSet.sourceSetId &&
            node.rawProjectFrame.managed && !node.rawProjectFrame.quarantined &&
            currentFrameIds.find(node.rawProjectFrame.frameId) == currentFrameIds.end()) {
            staleFrameNodes.push_back(node.id);
        }
    }
    for (int nodeId : staleFrameNodes) graph.RemoveNode(nodeId);
    std::vector<EditorNodeGraph::Link>& links = graph.EditLinks();
    links.erase(std::remove_if(links.begin(), links.end(), [&](const EditorNodeGraph::Link& link) {
        return link.toNodeId == hdrNodeId &&
            link.ownership == EditorNodeGraph::Link::Ownership::ManagedSourceBinding;
    }), links.end());
    for (const SourceSetFrame& frame : sourceSet.frames) {
        EditorNodeGraph::Node* frameNode = FindMfdFrameNode(
            graph, sourceSet.sourceSetId, frame.frameId);
        if (!frameNode) continue;
        const std::string socketId = EditorNodeGraph::MfdFrameInputSocketId(frame.frameId);
        if (!graph.TryConnectSockets(frameNode->id, EditorNodeGraph::kRawOutputSocketId,
                hdrNodeId, socketId, &error)) return false;
        MarkManagedMfdLink(graph, frameNode->id, hdrNodeId, socketId, frame.frameId);
    }
    if (ensureOutput) {
        EditorNodeGraph::Node* output = graph.EnsureOutputNode();
        if (!output) {
            error = "Could not create the HDR project Output node.";
            return false;
        }
        if (!graph.FindInputLink(output->id, EditorNodeGraph::kImageInputSocketId) &&
            !graph.TryConnectSockets(hdrNodeId, EditorNodeGraph::kImageOutputSocketId,
                output->id, EditorNodeGraph::kImageInputSocketId, &error)) return false;
    }
    return true;
}

void UpdateWorkspaceManifestFields(RawProjectSnapshot& snapshot) {
    if (!snapshot.rawWorkspaceData.is_object()) {
        snapshot.rawWorkspaceData = nlohmann::json::object();
    }
    snapshot.rawWorkspaceData["schema"] = "stack.rawWorkspace.project";
    snapshot.rawWorkspaceData["schemaVersion"] =
        Stack::Project::kRawWorkspaceProjectSchemaVersion;
    snapshot.rawWorkspaceData.erase("rawWorkspaceSchemaVersion");
    snapshot.rawWorkspaceData["rawProjectModel"] =
        Stack::Project::kRawProjectModelSourceSets;
    snapshot.rawWorkspaceData["projectId"] = snapshot.projectId;
    snapshot.rawWorkspaceData["activeSourceSetId"] = snapshot.activeSourceSetId;
    snapshot.rawWorkspaceData["activeFrameId"] = snapshot.activeFrameId;
}

} // namespace

Stack::Project::ProjectStoreOpenResult Stack::Project::CreateMultiFrameProject(
    const MultiFrameProjectCreation& request, ProjectCreationFailure* failure) {
    if (failure) *failure = ProjectCreationFailure::Input;
    const auto& requestedPath=request.path;
    const auto storageKind=request.storageKind;
    const auto& projectName=request.projectName;
    const auto& sourceSetName=request.sourceSetName;
    auto operationIntent=request.operationIntent;
    const auto& sourcePaths=request.sources;
    const auto referenceFrameIndex=request.referenceFrameIndex;
    const auto& orientationOverrides=request.orientationOverrides;
    const auto fail=[](const std::string& error) {
        ProjectStoreOpenResult result; result.message=error; return result;
    };
    if(request.shouldCancel && request.shouldCancel()) return fail("Canceled.");
    if (requestedPath.empty() || projectName.empty() || sourceSetName.empty()) {
        return fail("Project path, project name, and source-set name are required.");
    }

    std::filesystem::path path = requestedPath.lexically_normal();
    if (storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle) {
        const std::string extension = Lower(path.extension().string());
        const std::string fileName = Lower(path.filename().string());
        if (fileName == "project.stack") {
            path = path.parent_path();
        } else if (extension == ".stack" || extension == ".stackbundle") {
            path = path.parent_path() / path.stem();
        }
    } else if (Lower(path.extension().string()) != ".stack") {
        path += ".stack";
    }
    std::error_code filesystemError;
    ProjectStoreOpenResult created;
    if (std::filesystem::exists(path, filesystemError)) {
        if (request.resumeEmptyProject) created = OpenProjectStore(path);
        if (!created || request.projectId.empty() || created.snapshot.projectId != request.projectId ||
            created.snapshot.dirtyRevision != 0 || !created.snapshot.sourceSets.empty() ||
            !created.snapshot.embeddedAssets.empty())
            return fail("A project already exists at that path.");
    }

    RawProjectSnapshot bootstrap;
    bootstrap.projectId = request.projectId.empty() ? Stack::Project::GenerateStableUuid() : request.projectId;
    bootstrap.projectName = projectName;
    bootstrap.projectKindHint = StackBinaryFormat::kRawProjectKind;
    bootstrap.lifecycle.creationOrigin =
        Stack::Project::ProjectCreationOrigin::MultiSelection;
    bootstrap.pipelineData = nlohmann::json::object();
    UpdateWorkspaceManifestFields(bootstrap);
    if (!created) created = Stack::Project::CreateProjectStore(path, storageKind, bootstrap);
    if (!created) {
        if (failure) *failure = ProjectCreationFailure::Storage;
        return fail(created.message);
    }

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
    if (operationIntent == MultiFrameOperationIntent::RawCaptureSet) {
        if (sourcePaths.empty()) {
            cleanupCreatedStore();
            return fail("Select at least one RAW capture.");
        }
        sourceSet.operationSchemaVersion =
            Stack::Project::kMultiFrameOperationSchemaVersion;
        sourceSet.settings = DefaultCaptureSetSettings();
    } else if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (sourcePaths.size() < 2u) {
            cleanupCreatedStore();
            return fail("Select at least two RAW frames for an MFD project.");
        }
        sourceSet.operationSchemaVersion = Stack::Project::kMfdOperationSchemaVersion;
        sourceSet.settings = DefaultMfdSettings();
        EnsureMfdPostRecipe(sourceSet);
    } else if (operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        if (sourcePaths.size() < Raw::Hdr::kMinimumFrameCount ||
            sourcePaths.size() > Raw::Hdr::kMaximumFrameCount) {
            cleanupCreatedStore();
            return fail("Select between two and twenty RAW frames for an HDR project.");
        }
        sourceSet.operationSchemaVersion = Stack::Project::kHdrOperationSchemaVersion;
        sourceSet.settings = DefaultHdrSettings();
        EnsureHdrPostRecipe(sourceSet);
    }

    const Stack::Project::ProjectStoreTransaction transaction =
        created.store->BeginTransaction(snapshot.persistedStorageRevision);
    if (!transaction) {
        cleanupCreatedStore();
        if (failure) *failure = ProjectCreationFailure::Storage;
        return fail("Could not begin source ingestion.");
    }
    std::string error;
    if (!StageFrames(
            created.store,
            transaction,
            snapshot,
            sourceSet,
            sourcePaths,
            false,
            error, request.shouldCancel, failure)) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return fail(error);
    }
    if ((sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet ||
         sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
         sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr) &&
        sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        return fail("MultiFrame capture sets accept RAW sources only.");
    }
    if (!sourceSet.frames.empty()) {
        const std::size_t clampedReference = std::min(
            referenceFrameIndex, sourceSet.frames.size() - 1u);
        sourceSet.referenceFrameId = sourceSet.frames[clampedReference].frameId;
    }

    if (operationIntent == MultiFrameOperationIntent::RawCaptureSet) {
        Stack::Project::InitializeBracketing(sourceSet, snapshot);
        auto bracket = Stack::Project::SuggestBracketingGroups(snapshot, sourceSet);
        bracket.orientationOverrides = orientationOverrides;
        sourceSet.settings["bracketing"] = Raw::Bracketing::Serialize(bracket);
        operationIntent = MultiFrameOperationIntent::RawBurstHdr;
        EnsureHdrPostRecipe(sourceSet);
    }

    EditorNodeGraph::Graph graph;
    graph.Clear();
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (!SyncMfdGraphTopology(graph, snapshot, sourceSet, true, error)) {
            created.store->Abort(transaction);
            cleanupCreatedStore();
            return fail(error);
        }
    } else if (operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        if (!SyncHdrGraphTopology(graph, snapshot, sourceSet, true, error)) {
            created.store->Abort(transaction);
            cleanupCreatedStore();
            return fail(error);
        }
    } else {
        EditorNodeGraph::RawProjectSourceSetPayload payload;
        payload.sourceSetId = sourceSet.sourceSetId;
        EditorNodeGraph::Node* node = graph.AddRawProjectSourceSetNode(
            std::move(payload), { 80.0f, 120.0f });
        if (!node) {
            created.store->Abort(transaction);
            cleanupCreatedStore();
            return fail("Could not create the managed source-set graph node.");
        }
        node->title = "RAW Project Source Set - " + sourceSet.name;
        sourceSet.graphBindingNodeId = node->instanceUuid;
    }
    snapshot.sourceSets.push_back(std::move(sourceSet));
    snapshot.lifecycle.initialAssetIds.clear();
    for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
        snapshot.lifecycle.initialAssetIds.push_back(asset.assetId);
    }
    snapshot.activeSourceSetId = snapshot.sourceSets.front().sourceSetId;
    snapshot.activeFrameId = snapshot.sourceSets.front().referenceFrameId;
    if (!Stack::Project::IsBracketing(snapshot.sourceSets.front()))
        snapshot.multiFrameGraph = Stack::Project::BuildOperationMultiFrameGraph(snapshot);
    snapshot.mfdInputRevision =
        operationIntent == MultiFrameOperationIntent::RawBurstDenoise ? 1u : 0u;
    snapshot.hdrInputRevision =
        operationIntent == MultiFrameOperationIntent::RawBurstHdr ? 1u : 0u;
    snapshot.dirtyRevision = 1;
    snapshot.pipelineData = PipelineForGraph(graph, {}, nlohmann::json::object());
    snapshot.pipelineData["rawLayerSourceNodeUuid"] = snapshot.sourceSets.front().graphBindingNodeId;
    UpdateWorkspaceManifestFields(snapshot);
    const Stack::Project::ProjectStoreCommitResult commit =
        created.store->Commit(transaction, snapshot);
    if (!commit) {
        created.store->Abort(transaction);
        cleanupCreatedStore();
        if (failure) *failure = ProjectCreationFailure::Storage;
        return fail(commit.message);
    }
    snapshot.persistedStorageRevision = commit.committedStorageRevision;

    created.snapshot=std::move(snapshot);
    return created;
}

bool EditorModule::CreateMultiFrameRawProject(
    const std::filesystem::path& requestedPath,
    Stack::Project::ProjectStorageKind storageKind,
    const std::string& projectName,
    const std::string& sourceSetName,
    MultiFrameOperationIntent operationIntent,
    const std::vector<std::filesystem::path>& sourcePaths,
    std::size_t referenceFrameIndex,
    std::string* errorMessage,
    const std::map<int, int>& orientationOverrides) {
    if (IsDirty() && !m_RawWorkspaceReplacementAuthorized) {
        return Finish(errorMessage, "Save or discard the current project before replacing it.", false);
    }
    Stack::Project::MultiFrameProjectCreation request;
    request.path=requestedPath; request.storageKind=storageKind;
    request.projectName=projectName; request.sourceSetName=sourceSetName;
    request.operationIntent=operationIntent; request.sources=sourcePaths;
    request.referenceFrameIndex=referenceFrameIndex;
    request.orientationOverrides=orientationOverrides;
    auto created=Stack::Project::CreateMultiFrameProject(request);
    if(!created) return Finish(errorMessage,created.message,false);
    const auto path=created.store->StoragePath();
    auto snapshot=std::move(created.snapshot);
    auto loaded = std::make_shared<LoadedProjectData>();
    loaded->sourceState = ProjectSourceState::LazyAsset;
    loaded->sourcePixels.clear();
    loaded->width = 0;
    loaded->height = 0;
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

bool EditorModule::CommitActiveMultiFrameMutation(
    RawProjectSnapshot snapshot,
    EditorNodeGraph::Graph graph,
    const Stack::Project::ProjectStoreTransaction& transaction,
    bool importInProgress,
    std::string* outError,
    bool noteEdit) {
    if (!m_Project->store || !m_Project->snapshot || !transaction) {
        return Finish(outError, "No multi-frame RAW project transaction is active.", false);
    }
    snapshot.pipelineData = PipelineForGraph(
        graph, m_Project->layers, SerializePipeline());
    if (const auto* active = Stack::Project::FindSourceSet(snapshot,snapshot.activeSourceSetId))
        snapshot.pipelineData["rawLayerSourceNodeUuid"] = active->graphBindingNodeId;
    UpdateWorkspaceManifestFields(snapshot);
    if (!StackBinaryFormat::ExternalizeManagedProjectAssets(
            m_Project->store, transaction, snapshot)) {
        m_Project->store->Abort(transaction);
        if (importInProgress) {
            m_Project->lifecycle.CompleteImport(false);
        }
        return Finish(
            outError,
            "Could not stage the project's managed image assets.",
            false);
    }
    if(m_Bracketing && m_Bracketing->result &&
        m_Bracketing->projectId==snapshot.projectId && m_Bracketing->completedRevision==snapshot.hdrInputRevision) {
        auto* resultSet=Stack::Project::FindSourceSet(snapshot,m_Bracketing->setId);
        std::string resultError;
        if(resultSet && resultSet->settings["bracketing"].dump()==m_Bracketing->completedRecipe &&
            !Stack::Project::StageBracketingResult(m_Project->store,transaction,snapshot,m_Bracketing->setId,
                *m_Bracketing->result,{},resultError)) {
            m_Project->store->Abort(transaction);return Finish(outError,resultError,false);
        }
    }
    const Stack::Project::ModelValidationResult validation =
        Stack::Project::ValidateRawProjectSnapshot(snapshot);
    if (!validation.valid) {
        m_Project->store->Abort(transaction);
        if (importInProgress) m_Project->lifecycle.CompleteImport(false);
        return Finish(
            outError,
            validation.errors.empty() ? "The source-set change is invalid."
                                      : validation.errors.front(),
            false);
    }

    if (!importInProgress && noteEdit) {
        // Ordinary UI edits are in-memory document revisions. Assets were not
        // staged by these mutations, so there is nothing to publish yet; the
        // shared save coordinator will persist the newest coalesced revision.
        m_Project->store->Abort(transaction);
        m_Project->graph = std::move(graph);
        m_Project->snapshot =
            std::make_shared<RawProjectSnapshot>(std::move(snapshot));
        MarkDirty();
        if (m_MfdAdoptedRawResult &&
            (m_MfdAdoptedRawResult->projectId !=
                 m_Project->snapshot->projectId ||
             m_MfdAdoptedRawResult->inputRevision !=
                 m_Project->snapshot->mfdInputRevision)) {
            m_MfdAdoptedRawResult.reset();
        }
        if (m_HdrAdoptedRawResult &&
            (m_HdrAdoptedRawResult->projectId !=
                 m_Project->snapshot->projectId ||
             (!IsBracketingActive() && m_HdrAdoptedRawResult->inputRevision !=
                 m_Project->snapshot->hdrInputRevision))) {
            m_HdrAdoptedRawResult.reset();
        }
        RefreshGraphLayerMetadata();
        ApplyGraphLayerOrder();
        MarkRenderRefreshDirty();
        return Finish(outError, std::string(), true);
    }
    snapshot.dirtyRevision = noteEdit
        ? m_Project->lifecycle.NoteEdit()
        : m_Project->lifecycle.DirtyRevision();

    Stack::Project::ProjectSaveToken saveToken;
    if (!importInProgress) {
        saveToken = m_Project->lifecycle.BeginSave();
        if (!saveToken) {
            m_Project->store->Abort(transaction);
            return Finish(outError, "The project is not ready to save this change.", false);
        }
    }
    const Stack::Project::ProjectStoreCommitResult commit =
        m_Project->store->Commit(transaction, snapshot);
    if (!commit) {
        m_Project->store->Abort(transaction);
        if (importInProgress) {
            m_Project->lifecycle.CompleteImport(false);
            m_Project->dirty = m_Project->lifecycle.IsDirty();
        } else {
            m_Project->lifecycle.CompleteSave(
                saveToken,
                false,
                snapshot.persistedStorageRevision,
                commit.status == Stack::Project::ProjectStoreCommitStatus::Conflict);
            // Non-import mutations remain valid in memory when persistence
            // fails, so preserve them for retry or Save Copy. Imports cannot
            // do this because their staged originals were never published.
            m_Project->graph = std::move(graph);
            m_Project->snapshot =
                std::make_shared<RawProjectSnapshot>(std::move(snapshot));
            if (m_MfdAdoptedRawResult &&
                (m_MfdAdoptedRawResult->projectId !=
                     m_Project->snapshot->projectId ||
                 m_MfdAdoptedRawResult->inputRevision !=
                     m_Project->snapshot->mfdInputRevision ||
                 !Stack::Project::FindSourceSet(
                     *m_Project->snapshot,
                     m_MfdAdoptedRawResult->sourceSetId))) {
                m_MfdAdoptedRawResult.reset();
            }
            if (m_HdrAdoptedRawResult &&
                (m_HdrAdoptedRawResult->projectId !=
                     m_Project->snapshot->projectId ||
                 (!IsBracketingActive() && m_HdrAdoptedRawResult->inputRevision !=
                     m_Project->snapshot->hdrInputRevision) ||
                 !Stack::Project::FindSourceSet(
                     *m_Project->snapshot,
                     m_HdrAdoptedRawResult->sourceSetId))) {
                m_HdrAdoptedRawResult.reset();
            }
            RefreshGraphLayerMetadata();
            ApplyGraphLayerOrder();
            MarkRenderDirty();
            m_Project->dirty = true;
        }
        return Finish(outError, commit.message, false);
    }

    snapshot.persistedStorageRevision = commit.committedStorageRevision;
    m_Project->graph = std::move(graph);
    m_Project->snapshot =
        std::make_shared<RawProjectSnapshot>(std::move(snapshot));
    if (m_MfdAdoptedRawResult &&
        (m_MfdAdoptedRawResult->projectId !=
             m_Project->snapshot->projectId ||
         m_MfdAdoptedRawResult->inputRevision !=
             m_Project->snapshot->mfdInputRevision ||
         !Stack::Project::FindSourceSet(
             *m_Project->snapshot,
             m_MfdAdoptedRawResult->sourceSetId))) {
        m_MfdAdoptedRawResult.reset();
    }
    if (m_HdrAdoptedRawResult &&
        (m_HdrAdoptedRawResult->projectId !=
             m_Project->snapshot->projectId ||
         (!IsBracketingActive() && m_HdrAdoptedRawResult->inputRevision !=
             m_Project->snapshot->hdrInputRevision) ||
         !Stack::Project::FindSourceSet(
             *m_Project->snapshot,
             m_HdrAdoptedRawResult->sourceSetId))) {
        m_HdrAdoptedRawResult.reset();
    }
    if (importInProgress) {
        m_Project->lifecycle.CompleteImport(true);
        saveToken = m_Project->lifecycle.BeginSave();
    }
    m_Project->lifecycle.CompleteSave(
        saveToken, true, commit.committedStorageRevision, false);
    m_Project->dirty = m_Project->lifecycle.IsDirty();
    RefreshGraphLayerMetadata();
    ApplyGraphLayerOrder();
    // Publishing a project mutation changes the rendered result, but the
    // successful commit above is already the authoritative saved revision.
    // MarkRenderDirty() also marks the Editor project dirty, which used to
    // make every successful RAW save immediately look unsaved again.
    MarkRenderRefreshDirty();
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
    if (!m_Project->lifecycle.BeginImport()) {
        return Finish(errorMessage, "The project is currently busy or read-only.", false);
    }
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    EditorNodeGraph::Graph graph = m_Project->graph;
    const Stack::Project::ProjectStoreTransaction transaction =
        m_Project->store->BeginTransaction(snapshot.persistedStorageRevision);
    if (!transaction) {
        m_Project->lifecycle.CompleteImport(false);
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
    } else if (operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        if (sourcePaths.size() < Raw::Hdr::kMinimumFrameCount ||
            sourcePaths.size() > Raw::Hdr::kMaximumFrameCount) {
            m_Project->store->Abort(transaction);
            m_Project->lifecycle.CompleteImport(false);
            return Finish(errorMessage, "HDR source sets require two to twenty frames.", false);
        }
        sourceSet.operationSchemaVersion = Stack::Project::kHdrOperationSchemaVersion;
        sourceSet.settings = DefaultHdrSettings();
        EnsureHdrPostRecipe(sourceSet);
    }
    std::string error;
    if (!StageFrames(
            m_Project->store,
            transaction,
            snapshot,
            sourceSet,
            sourcePaths,
            false,
            error) ||
        ((operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
          operationIntent == MultiFrameOperationIntent::RawBurstHdr) &&
         sourceSet.inputFamily != MultiFrameInputFamily::Raw)) {
        m_Project->store->Abort(transaction);
        m_Project->lifecycle.CompleteImport(false);
        if (error.empty()) error = "RAW burst source sets accept RAW frames only.";
        return Finish(errorMessage, error, false);
    }
    EditorNodeGraph::Node* node = nullptr;
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (!SyncMfdGraphTopology(graph, snapshot, sourceSet, true, error)) {
            m_Project->store->Abort(transaction);
            m_Project->lifecycle.CompleteImport(false);
            return Finish(errorMessage, error, false);
        }
        node = FindMfdNode(graph, sourceSet.sourceSetId, sourceSet.graphBindingNodeId);
    } else if (operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        if (!SyncHdrGraphTopology(graph, snapshot, sourceSet, true, error)) {
            m_Project->store->Abort(transaction);
            m_Project->lifecycle.CompleteImport(false);
            return Finish(errorMessage, error, false);
        }
        node = FindHdrNode(graph, sourceSet.sourceSetId, sourceSet.graphBindingNodeId);
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
        m_Project->store->Abort(transaction);
        m_Project->lifecycle.CompleteImport(false);
        return Finish(errorMessage, "Could not create the managed graph binding.", false);
    }
    snapshot.activeSourceSetId = sourceSet.sourceSetId;
    snapshot.sourceSets.push_back(std::move(sourceSet));
    if (!snapshot.sourceSets.back().frames.empty()) {
        snapshot.activeFrameId = snapshot.sourceSets.back().referenceFrameId;
    }
    if (operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        ++snapshot.mfdInputRevision;
    } else if (operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        ++snapshot.hdrInputRevision;
    }
    graph.SelectNode(node->id, false);
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, true, errorMessage);
}

bool EditorModule::AddFramesToMultiFrameSourceSet(
    const std::string& sourceSetId,
    const std::vector<std::filesystem::path>& sourcePaths,
    std::string* errorMessage,
    bool updateBracketRecipe) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame RAW project is active.", false);
    }
    if (IsMfdExperimentalProcessingBusy() || IsHdrProcessingBusy()) {
        return Finish(
            errorMessage,
            "Finish or cancel multi-frame processing before adding frames to the current burst.",
            false);
    }
    if (!m_Project->lifecycle.BeginImport()) {
        return Finish(errorMessage, "The project is currently busy or read-only.", false);
    }
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) {
        m_Project->lifecycle.CompleteImport(false);
        return Finish(errorMessage, "The selected source set no longer exists.", false);
    }
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
        !Stack::Project::IsBracketing(*sourceSet) &&
        sourceSet->frames.size() + sourcePaths.size() > Raw::Hdr::kMaximumFrameCount) {
        m_Project->lifecycle.CompleteImport(false);
        return Finish(errorMessage, "HDR source sets support at most twenty frames.", false);
    }
    const Stack::Project::ProjectStoreTransaction transaction =
        m_Project->store->BeginTransaction(snapshot.persistedStorageRevision);
    if (!transaction) {
        m_Project->lifecycle.CompleteImport(false);
        return Finish(errorMessage, "Could not begin source ingestion.", false);
    }
    std::string error;
    if (!StageFrames(
            m_Project->store,
            transaction,
            snapshot,
            *sourceSet,
            sourcePaths,
            true,
            error)) {
        m_Project->store->Abort(transaction);
        m_Project->lifecycle.CompleteImport(false);
        return Finish(errorMessage, error, false);
    }
    const bool bracketDraftAssets = Stack::Project::IsBracketing(*sourceSet) && !updateBracketRecipe;
    if (Stack::Project::IsBracketing(*sourceSet) && updateBracketRecipe) {
        Raw::Bracketing::BracketingRecipe recipe;
        if (!Raw::Bracketing::Deserialize(sourceSet->settings["bracketing"], recipe, error)) {
            m_Project->store->Abort(transaction);
            m_Project->lifecycle.CompleteImport(false);
            return Finish(errorMessage, error, false);
        }
        const auto previousGroups = recipe.groups;
        auto suggested = Stack::Project::SuggestBracketingGroups(snapshot, *sourceSet);
        std::unordered_set<std::string> existing;
        for (const auto& group : recipe.groups) for (const auto& frame : group.frames) existing.insert(frame.id);
        for (auto& group : suggested.groups) {
            group.frames.erase(std::remove_if(group.frames.begin(), group.frames.end(),
                [&](const auto& frame) { return existing.count(frame.id) != 0; }), group.frames.end());
            if (!group.frames.empty()) recipe.groups.push_back(std::move(group));
        }
        Raw::Bracketing::RemapCurves(recipe, previousGroups);
        sourceSet->settings["algorithmVersion"] = Raw::Bracketing::RecipeVersion;
        sourceSet->settings["bracketing"] = Raw::Bracketing::Serialize(recipe);
    }
    snapshot.activeSourceSetId = sourceSetId;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        ++snapshot.mfdInputRevision;
    } else if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr && !bracketDraftAssets) {
        ++snapshot.hdrInputRevision;
    }
    EditorNodeGraph::Graph graph = m_Project->graph;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, error)) {
        m_Project->store->Abort(transaction);
        m_Project->lifecycle.CompleteImport(false);
        return Finish(errorMessage, error, false);
    }
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
        !SyncHdrGraphTopology(graph, snapshot, *sourceSet, true, error)) {
        m_Project->store->Abort(transaction);
        m_Project->lifecycle.CompleteImport(false);
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
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
    EditorNodeGraph::Graph graph = m_Project->graph;
    EditorNodeGraph::Node* node = nullptr;
    std::string topologyError;
    if (duplicate.operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (!SyncMfdGraphTopology(
                graph, snapshot, duplicate, true, topologyError)) {
            return Finish(errorMessage, topologyError, false);
        }
        node = FindMfdNode(graph, duplicate.sourceSetId, duplicate.graphBindingNodeId);
        ++snapshot.mfdInputRevision;
    } else if (duplicate.operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        duplicate.settings["result"] = nlohmann::json {
            { "state", "unavailable" }
        };
        duplicate.settings["automaticGeometricReference"] = true;
        duplicate.settings["automaticRadiometricAnchor"] = true;
        duplicate.settings["radiometricAnchorFrameId"] = nullptr;
        if (!SyncHdrGraphTopology(
                graph, snapshot, duplicate, true, topologyError)) {
            return Finish(errorMessage, topologyError, false);
        }
        node = FindHdrNode(graph, duplicate.sourceSetId, duplicate.graphBindingNodeId);
        ++snapshot.hdrInputRevision;
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
    const auto transaction = m_Project->store->BeginTransaction(
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The source set no longer exists.", false);
    sourceSet->name = name;
    EditorNodeGraph::Graph graph = m_Project->graph;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        if (EditorNodeGraph::Node* node = FindMfdNode(
                graph, sourceSetId, sourceSet->graphBindingNodeId)) {
            node->title = "MFD - " + name;
        }
    } else if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        if (EditorNodeGraph::Node* node = FindHdrNode(
                graph, sourceSetId, sourceSet->graphBindingNodeId)) {
            node->title = "HDR - " + name;
        }
    } else if (EditorNodeGraph::Node* node = FindSourceSetNode(
                   graph, sourceSetId, sourceSet->graphBindingNodeId)) {
        node->title = "RAW Project Source Set - " + name;
    }
    const auto transaction = m_Project->store->BeginTransaction(
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
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
    const bool deletingHdr =
        found->operationIntent == MultiFrameOperationIntent::RawBurstHdr;
    snapshot.sourceSets.erase(found);
    snapshot.activeSourceSetId = snapshot.sourceSets.empty()
        ? std::string()
        : snapshot.sourceSets.front().sourceSetId;
    EditorNodeGraph::Graph graph = m_Project->graph;
    if (deletingMfd || deletingHdr) {
        std::vector<int> nodesToRemove;
        for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
            if ((node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                 node.multiFrameDenoise.sourceSetId == sourceSetId) ||
                (node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr &&
                 node.multiFrameHdr.sourceSetId == sourceSetId) ||
                (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
                 node.rawProjectFrame.sourceSetId == sourceSetId)) {
                nodesToRemove.push_back(node.id);
            }
        }
        for (int nodeId : nodesToRemove) graph.RemoveNode(nodeId);
        if (deletingMfd) ++snapshot.mfdInputRevision;
        if (deletingHdr) ++snapshot.hdrInputRevision;
    } else if (const EditorNodeGraph::Node* node = FindSourceSetNode(
                   graph, sourceSetId, binding)) {
        graph.RemoveNode(node->id);
    }
    const auto transaction = m_Project->store->BeginTransaction(
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
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
    } else if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        ++snapshot.hdrInputRevision;
    }
    EditorNodeGraph::Graph graph = m_Project->graph;
    std::string topologyError;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
        !SyncHdrGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_Project->store->BeginTransaction(
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
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
    } else if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        ++snapshot.hdrInputRevision;
    }
    EditorNodeGraph::Graph graph = m_Project->graph;
    std::string topologyError;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
        !SyncHdrGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_Project->store->BeginTransaction(snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameReferenceFrame(
    const std::string& sourceSetId,
    const std::string& frameId,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) return Finish(errorMessage, "No project is active.", false);
    RawProjectSnapshot snapshot = *m_Project->snapshot;
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
    } else if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        sourceSet->settings["automaticGeometricReference"] = false;
        ++snapshot.hdrInputRevision;
    }
    EditorNodeGraph::Graph graph = m_Project->graph;
    std::string topologyError;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
        !SyncHdrGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_Project->store->BeginTransaction(snapshot.persistedStorageRevision);
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The MFD burst no longer exists.", false);
    auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) {
        return Finish(errorMessage, "The frame no longer exists.", false);
    }
    frame->userLabel = label;
    EditorNodeGraph::Graph graph = m_Project->graph;
    std::string topologyError;
    const bool topologyOk =
        sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr
        ? SyncHdrGraphTopology(graph, snapshot, *sourceSet, true, topologyError)
        : SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError);
    if (!topologyOk) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_Project->store->BeginTransaction(
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
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
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr)
        ++snapshot.hdrInputRevision;
    else
        ++snapshot.mfdInputRevision;
    snapshot.activeFrameId = frameId;
    EditorNodeGraph::Graph graph = m_Project->graph;
    std::string topologyError;
    const bool topologyOk =
        sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr
        ? SyncHdrGraphTopology(graph, snapshot, *sourceSet, true, topologyError)
        : SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError);
    if (!topologyOk) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_Project->store->BeginTransaction(
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
    RawProjectSnapshot snapshot = *m_Project->snapshot;
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
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr)
        ++snapshot.hdrInputRevision;
    else
        ++snapshot.mfdInputRevision;
    EditorNodeGraph::Graph graph = m_Project->graph;
    std::string topologyError;
    const bool topologyOk =
        sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr
        ? SyncHdrGraphTopology(graph, snapshot, *sourceSet, true, topologyError)
        : SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError);
    if (!topologyOk) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_Project->store->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameInternalViewTransformEnabled(
    const std::string& sourceSetId,
    bool enabled,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame project is active.", false);
    }
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    Stack::Project::RawProjectEditRecipeBinding binding;
    std::string bindingError;
    if (!sourceSet ||
        !Stack::Project::ResolveRawProjectEditRecipe(
            snapshot, sourceSetId, binding, &bindingError) ||
        !binding.multiFrameResult) {
        return Finish(
            errorMessage,
            bindingError.empty()
                ? "The multi-frame result no longer exists."
                : bindingError,
            false);
    }
    const std::string currentPlacement = sourceSet->settings.value(
        "viewTransformPlacement", std::string("internal"));
    if ((enabled && currentPlacement == "internal") ||
        (!enabled && currentPlacement == "graph")) {
        return Finish(errorMessage, std::string(), true);
    }

    const EditorNodeGraph::Graph graphBefore = m_Project->graph;
    const std::vector<std::shared_ptr<LayerBase>> layersBefore = m_Project->layers;
    const bool hdrResult = binding.hdrResult;
    EditorNodeGraph::Node* multiFrameNode = hdrResult
        ? FindHdrNode(m_Project->graph, sourceSetId, sourceSet->graphBindingNodeId)
        : FindMfdNode(m_Project->graph, sourceSetId, sourceSet->graphBindingNodeId);
    EditorNodeGraph::Node* output = m_Project->graph.FindNode(
        m_Project->graph.GetOutputNodeId());
    if (!multiFrameNode || !output || output->kind != EditorNodeGraph::NodeKind::Output) {
        return Finish(errorMessage, "The multi-frame output path is incomplete.", false);
    }
    if (hdrResult) {
        multiFrameNode->multiFrameHdr.internalViewTransformEnabled = enabled;
    } else {
        multiFrameNode->multiFrameDenoise.internalViewTransformEnabled = enabled;
    }
    std::string mutationError;
    if (!enabled) {
        const EditorNodeGraph::Link* outputInput = m_Project->graph.FindInputLink(
            output->id, EditorNodeGraph::kImageInputSocketId);
        if (!outputInput) {
            m_Project->graph = graphBefore;
            return Finish(errorMessage, "Connect the multi-frame result path to Output before using graph View Transform mode.", false);
        }
        const EditorNodeGraph::Link inputSnapshot = *outputInput;
        if (!m_Project->graph.RemoveLink(
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
            m_Project->graph = graphBefore;
            m_Project->layers = layersBefore;
            return Finish(
                errorMessage,
                mutationError.empty()
                    ? "Could not insert the graph View Transform."
                    : mutationError,
                false);
        }
        const EditorNodeGraph::Link* insertedInput = m_Project->graph.FindInputLink(
            output->id, EditorNodeGraph::kImageInputSocketId);
        EditorNodeGraph::Node* viewNode = insertedInput
            ? m_Project->graph.FindNode(insertedInput->fromNodeId)
            : nullptr;
        if (!viewNode || viewNode->kind != EditorNodeGraph::NodeKind::Layer ||
            viewNode->layerType != LayerType::ViewTransform ||
            viewNode->layerIndex < 0 ||
            viewNode->layerIndex >= static_cast<int>(m_Project->layers.size())) {
            m_Project->graph = graphBefore;
            m_Project->layers = layersBefore;
            return Finish(
                errorMessage,
                "The graph connection did not create the required View Transform.",
                false);
        }
        sourceSet->settings["graphViewTransformNodeUuid"] =
            viewNode->instanceUuid;
        if (m_Project->layers[viewNode->layerIndex]) {
            nlohmann::json graphViewSettings = sourceSet->settings.value(
                "graphViewTransformSettings", nlohmann::json::object());
            if (!graphViewSettings.is_object() || graphViewSettings.empty()) {
                const char* recipeKey = hdrResult
                    ? "sharedPostHdrRecipe"
                    : "sharedPostMfdRecipe";
                const nlohmann::json storedRecipe = sourceSet->settings.value(
                    recipeKey, nlohmann::json::object());
                if (storedRecipe.is_object() &&
                    storedRecipe.contains("rawRecipeVersion")) {
                    graphViewSettings = Stack::RawRecipe::DeserializeRecipe(
                        storedRecipe).viewTransform.layerJson;
                    graphViewSettings.erase("enabled");
                }
            }
            if (graphViewSettings.is_object() && !graphViewSettings.empty()) {
                m_Project->layers[viewNode->layerIndex]->Deserialize(graphViewSettings);
            }
        }
        sourceSet->settings["viewTransformPlacement"] = "graph";
    } else {
        const EditorNodeGraph::Link* outputInput = m_Project->graph.FindInputLink(
            output->id, EditorNodeGraph::kImageInputSocketId);
        EditorNodeGraph::Node* viewNode = outputInput
            ? m_Project->graph.FindNode(outputInput->fromNodeId)
            : nullptr;
        if (!viewNode || viewNode->kind != EditorNodeGraph::NodeKind::Layer ||
            viewNode->layerType != LayerType::ViewTransform ||
            viewNode->layerIndex < 0 ||
            viewNode->layerIndex >= static_cast<int>(m_Project->layers.size())) {
            m_Project->graph = graphBefore;
            return Finish(
                errorMessage,
                "Internal View Transform can only be restored when exactly one View Transform is immediately before Output.",
                false);
        }
        const std::string managedViewNodeUuid = sourceSet->settings.value(
            "graphViewTransformNodeUuid", std::string());
        if (managedViewNodeUuid.empty() ||
            viewNode->instanceUuid != managedViewNodeUuid) {
            m_Project->graph = graphBefore;
            return Finish(
                errorMessage,
                "The View Transform before Output is no longer the one placed by the multi-frame workflow. Preserve the user-authored graph before moving display mapping internally.",
                false);
        }
        const EditorNodeGraph::Link* viewInput = m_Project->graph.FindInputLink(
            viewNode->id, EditorNodeGraph::kImageInputSocketId);
        if (!viewInput) {
            m_Project->graph = graphBefore;
            return Finish(errorMessage, "The external View Transform has no input.", false);
        }
        const EditorNodeGraph::Link upstream = *viewInput;
        std::size_t viewOutgoingCount = 0;
        for (const EditorNodeGraph::Link& link : m_Project->graph.GetLinks()) {
            if (link.fromNodeId == viewNode->id) ++viewOutgoingCount;
        }
        if (viewOutgoingCount != 1u) {
            m_Project->graph = graphBefore;
            return Finish(
                errorMessage,
                "The external View Transform has additional consumers and cannot be removed safely.",
                false);
        }
        if (m_Project->layers[viewNode->layerIndex]) {
            const nlohmann::json graphViewSettings =
                m_Project->layers[viewNode->layerIndex]->Serialize();
            sourceSet->settings["graphViewTransformSettings"] =
                graphViewSettings;
            const char* recipeKey = hdrResult
                ? "sharedPostHdrRecipe"
                : "sharedPostMfdRecipe";
            const nlohmann::json storedRecipe = sourceSet->settings.value(
                recipeKey, nlohmann::json::object());
            Stack::RawRecipe::RawDevelopmentRecipe internalRecipe =
                storedRecipe.is_object() &&
                    storedRecipe.contains("rawRecipeVersion")
                ? Stack::RawRecipe::DeserializeRecipe(storedRecipe)
                : Stack::RawRecipe::MakeDefaultRecipe(
                    std::string(hdrResult ? "hdr://" : "mfd://") +
                        snapshot.projectId + "/" + sourceSetId,
                    sourceSet->name + " developed result");
            internalRecipe.viewTransform.layerJson = graphViewSettings;
            internalRecipe.viewTransform.layerJson["enabled"] = true;
            sourceSet->settings[recipeKey] =
                Stack::RawRecipe::SerializeRecipe(internalRecipe);
        }
        const int viewLayerIndex = viewNode->layerIndex;
        RemoveLayer(viewLayerIndex);
        output = m_Project->graph.FindNode(m_Project->graph.GetOutputNodeId());
        if (!output || !m_Project->graph.TryConnectSockets(
                upstream.fromNodeId,
                upstream.fromSocketId,
                output->id,
                EditorNodeGraph::kImageInputSocketId,
                &mutationError)) {
            m_Project->graph = graphBefore;
            m_Project->layers = layersBefore;
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

    ++snapshot.postRecipeRevision;
    const auto transaction = m_Project->store->BeginTransaction(
        snapshot.persistedStorageRevision);
    if (!transaction) {
        m_Project->graph = graphBefore;
        m_Project->layers = layersBefore;
        return Finish(errorMessage, "Could not begin the View Transform project transaction.", false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot), m_Project->graph, transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameOperationIntent(
    const std::string& sourceSetId,
    MultiFrameOperationIntent intent,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) return Finish(errorMessage, "No project is active.", false);
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet) return Finish(errorMessage, "The source set no longer exists.", false);
    if (sourceSet->operationIntent == intent) {
        return Finish(errorMessage, std::string(), true);
    }
    if (intent != MultiFrameOperationIntent::RawBurstDenoise &&
        intent != MultiFrameOperationIntent::RawBurstHdr) {
        return Finish(
            errorMessage,
            "Choose a Burst Denoise or HDR Merge processing node.",
            false);
    }
    if (sourceSet->operationIntent != MultiFrameOperationIntent::RawCaptureSet &&
        sourceSet->operationIntent != MultiFrameOperationIntent::Mfsr &&
        sourceSet->operationIntent != MultiFrameOperationIntent::RawBurstDenoise &&
        sourceSet->operationIntent != MultiFrameOperationIntent::RawBurstHdr) {
        return Finish(errorMessage,
            "This capture set has an unsupported compatibility binding.", false);
    }
    if (sourceSet->inputFamily != MultiFrameInputFamily::Raw) {
        return Finish(errorMessage, "MultiFrame processing accepts RAW capture sets only.", false);
    }
    const std::size_t enabledFrameCount = static_cast<std::size_t>(std::count_if(
        sourceSet->frames.begin(),
        sourceSet->frames.end(),
        [](const SourceSetFrame& frame) { return frame.enabled; }));
    if (enabledFrameCount < 2u) {
        return Finish(
            errorMessage,
            "This processing node requires at least two enabled RAW captures.",
            false);
    }
    if (intent == MultiFrameOperationIntent::RawBurstHdr &&
        snapshot.multiFrameGraph.nodes.empty() &&
        enabledFrameCount > Raw::Hdr::kMaximumFrameCount) {
        return Finish(
            errorMessage,
            "HDR Merge currently supports at most twenty enabled captures.",
            false);
    }
    if (intent == MultiFrameOperationIntent::RawBurstDenoise &&
        snapshot.multiFrameGraph.nodes.empty() &&
        enabledFrameCount > Raw::Mfd::kSharedBurstMaximumEnabledCaptures) {
        return Finish(
            errorMessage,
            "Shared Burst supports at most thirty enabled captures. Disable extras without removing them from the capture set.",
            false);
    }
    if (intent == MultiFrameOperationIntent::RawBurstHdr) {
        RawCaptureCompatibilitySummary reference;
        bool haveReference = false;
        for (const SourceSetFrame& frame : sourceSet->frames) {
            if (!frame.enabled) continue;
            const EmbeddedAssetRecord* asset =
                Stack::Project::FindEmbeddedAsset(snapshot, frame.assetId);
            RawCaptureCompatibilitySummary candidate;
            std::string compatibilityError;
            if (!asset ||
                !Stack::Project::DeserializeRawCaptureCompatibilitySummary(
                    asset->captureMetadataSummary,
                    candidate,
                    &compatibilityError)) {
                return Finish(
                    errorMessage,
                    compatibilityError.empty()
                        ? "A selected capture has no usable RAW metadata."
                        : compatibilityError,
                    false);
            }
            if (haveReference &&
                !Stack::Project::AreHdrCapturesStructurallyCompatible(
                    reference,
                    candidate,
                    &compatibilityError,
                    nullptr)) {
                return Finish(errorMessage, compatibilityError, false);
            }
            if (!haveReference) {
                reference = candidate;
                haveReference = true;
            }
    }
    }
    sourceSet->operationIntent = intent;
    if (intent == MultiFrameOperationIntent::RawBurstDenoise) {
        sourceSet->operationSchemaVersion =
            Stack::Project::kMfdOperationSchemaVersion;
        sourceSet->settings = DefaultMfdSettings();
        EnsureMfdPostRecipe(*sourceSet);
    } else {
        sourceSet->operationSchemaVersion =
            Stack::Project::kHdrOperationSchemaVersion;
        sourceSet->settings = DefaultHdrSettings();
        EnsureHdrPostRecipe(*sourceSet);
    }
    ++snapshot.mfdInputRevision;
    ++snapshot.hdrInputRevision;
    EditorNodeGraph::Graph graph = m_Project->graph;
    std::vector<int> neutralBindingNodes;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if ((node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
             node.rawProjectSourceSet.sourceSetId == sourceSetId) ||
            (node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
             node.multiFrameDenoise.sourceSetId == sourceSetId) ||
            (node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr &&
             node.multiFrameHdr.sourceSetId == sourceSetId)) {
            neutralBindingNodes.push_back(node.id);
        }
    }
    for (const int nodeId : neutralBindingNodes) {
        graph.RemoveNode(nodeId);
    }
    sourceSet->graphBindingNodeId.clear();
    std::string topologyError;
    if (intent == MultiFrameOperationIntent::RawBurstDenoise &&
        !SyncMfdGraphTopology(graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    } else if (intent == MultiFrameOperationIntent::RawBurstHdr &&
               !SyncHdrGraphTopology(
                   graph, snapshot, *sourceSet, true, topologyError)) {
        return Finish(errorMessage, topologyError, false);
    }
    const auto transaction = m_Project->store->BeginTransaction(snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot), std::move(graph), transaction, false, errorMessage);
}

bool EditorModule::SetMultiFrameGraphDocument(
    Stack::Project::MultiFrameGraphDocument graph,
    std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive() || !m_Project->snapshot ||
        !m_Project->store) {
        return Finish(errorMessage, "No MultiFrame project is active.", false);
    }
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    const Stack::Project::MultiFrameGraphValidationResult validation =
        Stack::Project::ValidateMultiFrameGraph(graph, snapshot, false);
    if (!validation.valid) {
        return Finish(
            errorMessage,
            validation.errors.empty()
                ? "The MultiFrame graph edit is invalid."
                : validation.errors.front(),
            false);
    }
    const Raw::MultiFrame::GraphExecutionPlan previousPlan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(snapshot);
    snapshot.multiFrameGraph = std::move(graph);
    const Raw::MultiFrame::GraphExecutionPlan editedPlan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(snapshot);
    if (previousPlan.contentIdentitySha256 !=
            editedPlan.contentIdentitySha256 ||
        previousPlan.valid != editedPlan.valid) {
        // Rewiring, membership, or processor settings retire pixels. View-only
        // canvas edits retain the same execution identity and current result.
        ++snapshot.mfdInputRevision;
        ++snapshot.hdrInputRevision;
    }
    EditorNodeGraph::Graph rendererGraph = m_Project->graph;
    const Raw::MultiFrame::GraphExecutionStep* terminal =
        Raw::MultiFrame::FindGraphExecutionStep(
            editedPlan, editedPlan.outputProducerNodeId);
    const bool finalHdr = terminal && terminal->adapter ==
        Raw::MultiFrame::GraphExecutionAdapter::HdrV4;
    const bool finalBurst = terminal && terminal->adapter ==
        Raw::MultiFrame::GraphExecutionAdapter::SharedBurstV1;
    MultiFrameSourceSet* activeSet = Stack::Project::FindSourceSet(
        snapshot, snapshot.activeSourceSetId);
    if (!activeSet && snapshot.sourceSets.size() == 1u) {
        activeSet = &snapshot.sourceSets.front();
    }
    if ((finalHdr || finalBurst) && activeSet) {
        if (finalHdr) EnsureHdrPostRecipe(*activeSet);
        else EnsureMfdPostRecipe(*activeSet);
        const bool hasMatchingBridge = finalHdr
            ? FindHdrNode(
                rendererGraph,
                activeSet->sourceSetId,
                activeSet->graphBindingNodeId) != nullptr
            : FindMfdNode(
                rendererGraph,
                activeSet->sourceSetId,
                activeSet->graphBindingNodeId) != nullptr;
        std::vector<int> obsoleteBridgeNodes;
        for (const EditorNodeGraph::Node& node : rendererGraph.GetNodes()) {
            const bool neutral =
                node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
                node.rawProjectSourceSet.sourceSetId == activeSet->sourceSetId;
            const bool oppositeHdr = finalBurst &&
                node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr &&
                node.multiFrameHdr.sourceSetId == activeSet->sourceSetId;
            const bool oppositeBurst = finalHdr &&
                node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                node.multiFrameDenoise.sourceSetId == activeSet->sourceSetId;
            if (neutral || oppositeHdr || oppositeBurst) {
                obsoleteBridgeNodes.push_back(node.id);
            }
        }
        for (int nodeId : obsoleteBridgeNodes) {
            rendererGraph.RemoveNode(nodeId);
        }
        if (!hasMatchingBridge || !obsoleteBridgeNodes.empty()) {
            std::string bridgeError;
            const bool bridgeReady = finalHdr
                ? SyncHdrGraphTopology(
                    rendererGraph, snapshot, *activeSet, true, bridgeError)
                : SyncMfdGraphTopology(
                    rendererGraph, snapshot, *activeSet, true, bridgeError);
            if (!bridgeReady) {
                return Finish(
                    errorMessage,
                    bridgeError.empty()
                        ? "The typed MultiFrame Output could not create its RAW renderer bridge."
                        : bridgeError,
                    false);
            }
        }
    }
    const auto transaction = m_Project->store->BeginTransaction(
        snapshot.persistedStorageRevision);
    if (!transaction) {
        return Finish(
            errorMessage,
            "Could not begin the MultiFrame graph transaction.",
            false);
    }
    return CommitActiveMultiFrameMutation(
        std::move(snapshot),
        std::move(rendererGraph),
        transaction,
        false,
        errorMessage,
        true);
}

bool EditorModule::ActivateMultiFrameSourceSet(const std::string& sourceSetId) {
    if (!IsMultiFrameRawProjectActive()) return false;
    MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(*m_Project->snapshot, sourceSetId);
    if (!sourceSet) return false;
    const bool changed = m_Project->snapshot->activeSourceSetId != sourceSetId;
    m_Project->snapshot->activeSourceSetId = sourceSetId;
    m_Project->snapshot->rawWorkspaceData["activeSourceSetId"] = sourceSetId;
    const EditorNodeGraph::Node* node = nullptr;
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise) {
        node = FindMfdNode(m_Project->graph, sourceSetId, sourceSet->graphBindingNodeId);
    } else if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        node = FindHdrNode(m_Project->graph, sourceSetId, sourceSet->graphBindingNodeId);
    } else {
        node = FindSourceSetNode(m_Project->graph, sourceSetId, sourceSet->graphBindingNodeId);
    }
    if (node) {
        m_Project->graph.SelectNode(node->id, false);
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
        Stack::Project::FindSourceSet(*m_Project->snapshot, sourceSetId);
    if (!sourceSet) return false;
    const auto frame = std::find_if(
        sourceSet->frames.begin(), sourceSet->frames.end(),
        [&](const SourceSetFrame& candidate) { return candidate.frameId == frameId; });
    if (frame == sourceSet->frames.end()) return false;
    const bool changed =
        m_Project->snapshot->activeSourceSetId != sourceSetId ||
        m_Project->snapshot->activeFrameId != frameId;
    m_Project->snapshot->activeSourceSetId = sourceSetId;
    m_Project->snapshot->activeFrameId = frameId;
    UpdateWorkspaceManifestFields(*m_Project->snapshot);
    if (selectGraphNode) {
        if (const EditorNodeGraph::Node* node = FindMfdFrameNode(
                m_Project->graph, sourceSetId, frameId)) {
            m_Project->graph.SelectNode(node->id, false);
        }
    }
    if (changed) MarkDirty();
    return true;
}

bool EditorModule::OpenManagedMfdGraphNode(int nodeId) {
    if (!IsMultiFrameRawProjectActive()) return false;
    const EditorNodeGraph::Node* node = m_Project->graph.FindNode(nodeId);
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
    } else if (node->kind == EditorNodeGraph::NodeKind::MultiFrameHdr) {
        if (!ActivateMultiFrameSourceSet(node->multiFrameHdr.sourceSetId)) {
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
    return RequestCreateMultiFrameProjectFromGallerySelection(
        MultiFrameOperationIntent::RawCaptureSet);
}

bool EditorModule::StartActiveMultiFrameProcessingForQueue(
    std::string* errorMessage) {
    if (IsBracketingActive()) {
        if(HasPendingBracketingDraft()){if(errorMessage)*errorMessage="Process or discard pending Bracketing input changes before export.";return false;}
        return StartBracketingProcessing(true, errorMessage);
    }
    if (!IsMultiFrameRawProjectActive() || !m_Project->snapshot) {
        if (errorMessage) *errorMessage =
            "The queued document is not an active multi-frame project.";
        return false;
    }
    const std::string sourceSetId =
        m_Project->snapshot->activeSourceSetId;
    const Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(
            *m_Project->snapshot, sourceSetId);
    if (!sourceSet) {
        if (errorMessage) *errorMessage =
            "The multi-frame project has no active source set.";
        return false;
    }
    const bool restoredHdrResult =
        sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
        m_HdrAdoptedRawResult && m_HdrAdoptedRawResult->rawData &&
        m_HdrAdoptedRawResult->projectId ==
            m_Project->snapshot->projectId &&
        m_HdrAdoptedRawResult->sourceSetId == sourceSetId &&
        m_HdrAdoptedRawResult->inputRevision ==
            m_Project->snapshot->hdrInputRevision;
    const bool restoredBurstResult =
        sourceSet->operationIntent ==
            MultiFrameOperationIntent::RawBurstDenoise &&
        m_MfdAdoptedRawResult && m_MfdAdoptedRawResult->rawData &&
        m_MfdAdoptedRawResult->projectId ==
            m_Project->snapshot->projectId &&
        m_MfdAdoptedRawResult->sourceSetId == sourceSetId &&
        m_MfdAdoptedRawResult->inputRevision ==
            m_Project->snapshot->mfdInputRevision;
    if (restoredHdrResult || restoredBurstResult) {
        if (errorMessage) errorMessage->clear();
        m_MultiFrameGraphProcessingTaskState = Async::TaskState::Ready;
        m_MultiFrameGraphProcessingStatusText =
            "Using the verified saved MultiFrame result.";
        return true;
    }
    if (!m_Project->snapshot->multiFrameGraph.nodes.empty()) {
        return StartMultiFrameGraphProcessing(sourceSetId, errorMessage);
    }
    if (sourceSet->operationIntent ==
        MultiFrameOperationIntent::RawBurstHdr) {
        return StartHdrProcessing(sourceSetId, errorMessage);
    }
    if (sourceSet->operationIntent ==
        MultiFrameOperationIntent::RawBurstDenoise) {
        return StartMfdExperimentalProcessing(sourceSetId, errorMessage);
    }
    if (errorMessage) *errorMessage =
        "This capture set has no executable Bracket processing operation.";
    return false;
}

bool EditorModule::IsActiveMultiFrameProcessingForQueueBusy() const {
    if (IsBracketingActive()) {
        const_cast<EditorModule*>(this)->TickBracketing();
        return m_MultiFrameGraphProcessingTaskState == Async::TaskState::Failed ||
            Async::IsBusy(m_MultiFrameGraphProcessingTaskState);
    }
    return Async::IsBusy(m_MultiFrameGraphProcessingTaskState) ||
        Async::IsBusy(m_MfdExperimentalProcessingTaskState) ||
        Async::IsBusy(m_HdrProcessingTaskState);
}

bool EditorModule::DidActiveMultiFrameProcessingForQueueFail(
    std::string* errorMessage) const {
    if (IsBracketingActive()) const_cast<EditorModule*>(this)->TickBracketing();
    const auto copyFailure = [&](Async::TaskState state,
                                 const std::string& status) {
        if (state != Async::TaskState::Failed) return false;
        if (errorMessage) {
            *errorMessage = status.empty()
                ? "The Bracket pipeline failed."
                : status;
        }
        return true;
    };
    return copyFailure(
               m_MultiFrameGraphProcessingTaskState,
               m_MultiFrameGraphProcessingStatusText) ||
        copyFailure(
               m_MfdExperimentalProcessingTaskState,
               m_MfdExperimentalProcessingStatusText) ||
        copyFailure(
               m_HdrProcessingTaskState,
               m_HdrProcessingStatusText);
}

bool EditorModule::RequestCreateMultiFrameProjectFromGallerySelection(
    MultiFrameOperationIntent intent) {
    if(intent==MultiFrameOperationIntent::RawCaptureSet) {
        BeginBracketingDraft(true);OpenBracketingTool();RequestOpenRawLabTab();return true;
    }
    if (IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        IsMfdExperimentalProcessingBusy() ||
        IsHdrProcessingBusy()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finish the current RAW load or multi-frame processing run before creating a new project.",
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
    if (paths.empty()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "At least one selected RAW source must still be available to create a capture set.",
            "multi-frame-create-selection-unavailable");
        return false;
    }
    if (intent != MultiFrameOperationIntent::RawCaptureSet &&
        intent != MultiFrameOperationIntent::RawBurstDenoise &&
        intent != MultiFrameOperationIntent::RawBurstHdr) {
        PostNotification(
            UiNotificationSeverity::Error,
            "The requested multi-frame project type is not supported.",
            "multi-frame-create-unsupported-intent");
        return false;
    }
    m_PendingMultiFrameCreationIntent = intent;
    m_PendingMultiFrameGallerySourcePaths = std::move(paths);
    m_PopulateMultiFrameCreationFromGallery = true;
    m_OpenMultiFrameCreationPopup = true;
    m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
    m_MfdBrowseWorkspace = false;
    RequestOpenRawLabTab();
    return true;
}

bool EditorModule::HandleMultiFrameFileDrop(
    const std::vector<std::string>& paths,
    float screenX,
    float screenY) {
    if(IsBracketingToolActive()) {
        std::vector<std::filesystem::path> files;for(const auto& path:paths)files.emplace_back(path);
        AddBracketingDraftFiles(files);return true;
    }
    if (paths.empty()) {
        return false;
    }

    std::vector<std::filesystem::path> rawPaths;
    rawPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        if (!path.empty() && Raw::RawLoader::IsRawPath(path)) {
            rawPaths.emplace_back(path);
        }
    }
    if (rawPaths.empty()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "MultiFrame accepts mosaiced RAW captures such as DNG, CR3, NEF, ARW, RAF, and RW2 files.",
            "multi-frame-file-drop-raw-only");
        return true;
    }

    if (!IsMultiFrameRawProjectActive() || !m_Project->snapshot) {
        m_PendingMultiFrameCreationIntent =
            MultiFrameOperationIntent::RawCaptureSet;
        m_PendingMultiFrameGallerySourcePaths = std::move(rawPaths);
        m_PopulateMultiFrameCreationFromGallery = true;
        m_OpenMultiFrameCreationPopup = true;
        m_ReturnToMultiFrameAfterGalleryCreationCancel = true;
        m_MultiFrameWorkspaceStatusText =
            "Choose where to save the capture-set project for the dropped RAW files.";
        return true;
    }

    if (IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        IsMultiFrameGraphProcessingBusy() ||
        IsMfdExperimentalProcessingBusy() ||
        IsHdrProcessingBusy()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finish the current RAW load or MultiFrame processing run before dropping more captures.",
            "multi-frame-file-drop-busy");
        return true;
    }

    RawProjectSnapshot& beforeSnapshot = *m_Project->snapshot;
    MultiFrameSourceSet* beforeSourceSet = Stack::Project::FindSourceSet(
        beforeSnapshot,
        beforeSnapshot.activeSourceSetId);
    if (!beforeSourceSet) {
        PostNotification(
            UiNotificationSeverity::Error,
            "The active MultiFrame project has no capture set to receive the dropped RAW files.",
            "multi-frame-file-drop-missing-set");
        return true;
    }

    const std::string sourceSetId = beforeSourceSet->sourceSetId;
    std::unordered_set<std::string> previousFrameIds;
    previousFrameIds.reserve(beforeSourceSet->frames.size());
    for (const SourceSetFrame& frame : beforeSourceSet->frames) {
        previousFrameIds.insert(frame.frameId);
    }

    std::string error;
    if (!AddFramesToMultiFrameSourceSet(sourceSetId, rawPaths, &error)) {
        m_MultiFrameWorkspaceStatusText = error.empty()
            ? "The dropped RAW captures could not be added."
            : error;
        PostNotification(
            UiNotificationSeverity::Error,
            m_MultiFrameWorkspaceStatusText,
            "multi-frame-file-drop-failed");
        return true;
    }

    RawProjectSnapshot& snapshot = *m_Project->snapshot;
    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(
        snapshot,
        sourceSetId);
    if (!sourceSet) {
        m_MultiFrameWorkspaceStatusText =
            "The RAW captures were imported, but the active capture set could not be refreshed.";
        return true;
    }

    if (Stack::Project::IsBracketing(*sourceSet)) return true;
    Stack::Project::MultiFrameGraphDocument edited = snapshot.multiFrameGraph;
    const bool hasDropRect =
        m_MultiFrameDropMaxX > m_MultiFrameDropMinX &&
        m_MultiFrameDropMaxY > m_MultiFrameDropMinY;
    const bool dropInside = hasDropRect &&
        screenX >= m_MultiFrameDropMinX && screenX <= m_MultiFrameDropMaxX &&
        screenY >= m_MultiFrameDropMinY && screenY <= m_MultiFrameDropMaxY;
    const double anchorScreenX = dropInside
        ? static_cast<double>(screenX)
        : static_cast<double>(m_MultiFrameDropMinX + m_MultiFrameDropMaxX) * 0.5;
    const double anchorScreenY = dropInside
        ? static_cast<double>(screenY)
        : static_cast<double>(m_MultiFrameDropMinY + m_MultiFrameDropMaxY) * 0.5;
    const double safeZoom = std::clamp(m_MultiFrameDropZoom, 0.25, 4.0);
    const double graphX = hasDropRect
        ? (anchorScreenX - m_MultiFrameDropMinX) / safeZoom -
              m_MultiFrameDropPanX
        : 260.0;
    const double graphY = hasDropRect
        ? (anchorScreenY - m_MultiFrameDropMinY) / safeZoom -
              m_MultiFrameDropPanY
        : 220.0;

    std::vector<std::string> addedNodeIds;
    std::size_t addedFrameIndex = 0u;
    for (const SourceSetFrame& frame : sourceSet->frames) {
        if (previousFrameIds.count(frame.frameId) != 0u) {
            continue;
        }
        const EmbeddedAssetRecord* asset = Stack::Project::FindEmbeddedAsset(
            snapshot,
            frame.assetId);
        Stack::Project::MultiFrameGraphNode node;
        node.nodeId = Stack::Project::GenerateStableUuid();
        node.kind = Stack::Project::MultiFrameGraphNodeKind::CaptureSubset;
        node.title = asset && !asset->originalFilename.empty()
            ? asset->originalFilename
            : (frame.userLabel.empty() ? "RAW Capture" : frame.userLabel);
        node.sourceSetId = sourceSetId;
        node.frameIds = { frame.frameId };
        node.positionX = graphX;
        node.positionY = graphY +
            static_cast<double>(addedFrameIndex) * 138.0;
        node.settings = {
            { "source", "raw-file" },
            { "import", "explorer-drop" },
            { "frameId", frame.frameId },
            { "assetId", frame.assetId },
            { "provisional", false }
        };
        addedNodeIds.push_back(node.nodeId);
        edited.nodes.push_back(std::move(node));
        ++addedFrameIndex;
    }

    if (!addedNodeIds.empty()) {
        edited.userEdited = true;
        if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
            m_MultiFrameWorkspaceStatusText =
                "The RAW captures were added to the project, but their canvas nodes could not be created: " +
                error;
            PostNotification(
                UiNotificationSeverity::Error,
                m_MultiFrameWorkspaceStatusText,
                "multi-frame-file-drop-node-failed");
            return true;
        }
        m_MultiFrameWorkspaceSelectedNodeId = addedNodeIds.front();
        m_MultiFrameWorkspaceSelection =
            MultiFrameWorkspaceSelection::CaptureSubset;
    }

    m_MultiFrameWorkspaceStatusText = addedFrameIndex == 1u
        ? "Added 1 RAW capture. Drag its output socket to Burst Denoise, HDR Merge, or another compatible node."
        : "Added " + std::to_string(addedFrameIndex) +
              " RAW captures as movable source nodes. Connect their output sockets manually.";
    PostNotification(
        UiNotificationSeverity::Success,
        m_MultiFrameWorkspaceStatusText,
        "multi-frame-file-drop-complete");
    return true;
}

bool EditorModule::SaveActiveMultiFrameRawProject(std::string* errorMessage) {
    if(m_Bracketing&&!m_Bracketing->newProject&&m_Bracketing->processRequired&&!m_Bracketing->savingDraft)
        return CommitBracketingDraft(false,errorMessage);
    if (!m_DocumentPersistenceEnabled) {
        return Finish(
            errorMessage,
            "Document persistence is disabled for this isolated render session.",
            false);
    }
    if (!IsUnifiedProjectStoreActive()) {
        return Finish(errorMessage, "No managed project is active.", false);
    }
    if (!IsDirty()) return Finish(errorMessage, std::string(), true);
    RawProjectSnapshot snapshot = *m_Project->snapshot;
    const auto transaction = m_Project->store->BeginTransaction(
        snapshot.persistedStorageRevision);
    return transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot),
        m_Project->graph,
        transaction,
        false,
        errorMessage,
        false);
}

void EditorModule::StartManagedProjectSaveAsync(
    std::uint64_t capturedEditorRevision,
    Stack::Project::ProjectSaveReason reason,
    Stack::Project::ProjectSaveCoordinator::Completion completion) {
    using Stack::Project::ProjectSaveResult;
    using Stack::Project::ProjectSaveStatus;

    auto failImmediately = [&](std::string message) {
        ProjectSaveResult result;
        result.status = ProjectSaveStatus::Failed;
        result.projectId = m_Project->documentId;
        result.message = std::move(message);
        if (completion) completion(std::move(result));
    };
    if (!IsUnifiedProjectStoreActive()) {
        failImmediately("No managed project is active.");
        return;
    }
    if(m_Bracketing&&!m_Bracketing->newProject&&m_Bracketing->processRequired) {
        std::string error;
        if(!CommitBracketingDraft(false,&error)){failImmediately(error);return;}
        ProjectSaveResult result;
        result.status=ProjectSaveStatus::Saved;
        result.projectId=m_Project->documentId;
        result.persistedEditRevision=GetProjectEditRevision();
        result.storageRevision=m_Project->snapshot->persistedStorageRevision;
        result.path=GetCurrentProjectFileName();
        if(completion)completion(std::move(result));
        return;
    }

    const std::string projectId = EnsureProjectDocumentId();
    const auto snapshotCaptureBegin = std::chrono::steady_clock::now();
    // The live snapshot already owns the last serialized graph. Copying it
    // here duplicated the largest JSON payload on the UI thread immediately
    // before SerializePipeline() replaced it. Preserve every other project
    // field, then serialize the current graph exactly once below.
    RawProjectSnapshot snapshot =
        CopyProjectSnapshotWithoutPipeline(*m_Project->snapshot);
    snapshot.projectId = projectId;
    if (!m_Project->name.empty()) snapshot.projectName = m_Project->name;
    const bool rawProject = IsRawWorkspaceProjectActive();
    if (rawProject && !IsMultiFrameRawProjectActive()) {
        if (!snapshot.rawWorkspaceData.is_object()) {
            snapshot.rawWorkspaceData = nlohmann::json::object();
        }
        snapshot.rawWorkspaceData["rawRecipe"] =
            Stack::RawRecipe::SerializeWorkspaceSourceRecipe(m_Project->rawRecipe);
        snapshot.rawWorkspaceData["rawWorkspaceMode"] =
            Stack::RawWorkspace::RawProjectModeToString(
                m_Project->rawMode);
        StackBinaryFormat::ProjectDocument modeDocument;
        modeDocument.rawWorkspaceData = snapshot.rawWorkspaceData;
        ApplyActiveRawWorkspaceModeDataToDocument(modeDocument);
        snapshot.rawWorkspaceData = std::move(modeDocument.rawWorkspaceData);
    }
    // Serialize the active graph once. PipelineForGraph is required when a
    // detached mutation graph is supplied, but using it here serialized the
    // same live document twice before every autosave.
    snapshot.pipelineData = SerializePipeline();
    if (rawProject) UpdateWorkspaceManifestFields(snapshot);
    m_GraphPerformanceStats.lastProjectSaveSnapshotMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - snapshotCaptureBegin).count();

    const Stack::Project::ProjectSaveToken saveToken =
        m_Project->lifecycle.BeginSave();
    if (!saveToken) {
        failImmediately("The project is not ready to save.");
        return;
    }
    snapshot.dirtyRevision = saveToken.snapshotDirtyRevision;

    const Stack::Project::ProjectStoreHandle sourceStore =
        m_Project->store;
    const bool adopting =
        !m_Project->adoptionSourcePath.empty() ||
        sourceStore->StorageKind() ==
            Stack::Project::ProjectStorageKind::PortableFile;
    const std::filesystem::path adoptionSource =
        !m_Project->adoptionSourcePath.empty()
            ? m_Project->adoptionSourcePath
            : sourceStore->StoragePath();
    std::filesystem::path destination;
    if (adopting) {
        const std::filesystem::path destinationRoot =
            IsRawWorkspaceProjectActive() &&
                    !m_RawWorkspace.workspaceRoot.empty()
                ? Stack::RawWorkspace::BuildManagedLayout(
                      m_RawWorkspace.workspaceRoot).projectsDirectory
                : AppPaths::GetProjectsDirectory();
        destination = Stack::Project::ProjectIndex::BuildUniqueProjectPath(
            destinationRoot,
            snapshot.projectName.empty() ? "Untitled Project" : snapshot.projectName,
            projectId);
        snapshot.adoptedFrom = adoptionSource.lexically_normal().string();
    }

    std::shared_ptr<const Raw::Bracketing::BracketingResult> bracketResult;
    std::string bracketSetId;
    if(m_Bracketing && m_Bracketing->result &&
        m_Bracketing->projectId==snapshot.projectId && m_Bracketing->completedRevision==snapshot.hdrInputRevision) {
        const auto* set=Stack::Project::FindSourceSet(snapshot,m_Bracketing->setId);
        if(set && set->settings["bracketing"].dump()==m_Bracketing->completedRecipe) {
            bracketResult=m_Bracketing->result;bracketSetId=m_Bracketing->setId;
        }
    }
    struct ManagedSaveWork {
        RawProjectSnapshot snapshot;
        Stack::Project::ProjectStoreHandle savedStore;
        Stack::Project::ProjectStoreCommitResult commit;
        std::string error;
        bool success = false;
        bool conflict = false;
        double commitMs = 0.0;
    };
    auto work = std::make_shared<ManagedSaveWork>();
    work->snapshot = std::move(snapshot);

    auto saveWork = [
        sourceStore,
        bracketResult, bracketSetId,
        adopting,
        destination,
        reason,
        work,
        this,
        projectId,
        capturedEditorRevision,
        saveToken,
        adoptionSource,
        completion
    ]() mutable {
        const auto commitBegin = std::chrono::steady_clock::now();
        try {
        if (adopting) {
            Stack::Project::ProjectStoreOpenResult converted =
                Stack::Project::ConvertProjectStore(
                    sourceStore,
                    work->snapshot,
                    destination,
                    Stack::Project::ProjectStorageKind::DirectoryBundle,
                    [bracketResult, bracketSetId](const auto& store, const auto& transaction, auto& snapshot, auto& error) {
                        return !bracketResult || Stack::Project::StageBracketingResult(
                            store, transaction, snapshot, bracketSetId, *bracketResult, {}, error);
                    });
            if (converted) {
                work->success = true;
                work->savedStore = std::move(converted.store);
                work->snapshot = std::move(converted.snapshot);
            } else {
                work->error = converted.message.empty()
                    ? "The adopted project bundle could not be committed."
                    : converted.message;
            }
        } else {
            const Stack::Project::ProjectStoreTransaction transaction =
                sourceStore->BeginTransaction(
                    work->snapshot.persistedStorageRevision);
            if (!transaction) {
                work->error = "Could not begin the project save transaction.";
            } else if (!StackBinaryFormat::ExternalizeManagedProjectAssets(
                           sourceStore,
                           transaction,
                           work->snapshot)) {
                sourceStore->Abort(transaction);
                work->error = "Could not stage the project's managed image assets.";
            } else if (bracketResult && !Stack::Project::StageBracketingResult(
                sourceStore,transaction,work->snapshot,bracketSetId,*bracketResult,{},work->error)) {
                sourceStore->Abort(transaction);
            } else {
                const Stack::Project::ModelValidationResult validation =
                    Stack::Project::ValidateRawProjectSnapshot(work->snapshot);
                if (!validation.valid) {
                    sourceStore->Abort(transaction);
                    work->error = validation.errors.empty()
                        ? "The project snapshot is invalid."
                        : validation.errors.front();
                } else {
                    work->commit = sourceStore->Commit(
                        transaction,
                        work->snapshot);
                    work->success = static_cast<bool>(work->commit);
                    work->conflict = work->commit.status ==
                        Stack::Project::ProjectStoreCommitStatus::Conflict;
                    if (!work->success) {
                        sourceStore->Abort(transaction);
                        work->error = work->commit.message.empty()
                            ? "The project manifest could not be published."
                            : work->commit.message;
                    } else {
                        work->snapshot.persistedStorageRevision =
                            work->commit.committedStorageRevision;
                        work->savedStore = sourceStore;
                    }
                }
            }
        }
        } catch (const std::exception& error) {
            work->success = false;
            work->conflict = false;
            work->error = error.what();
        } catch (...) {
            work->success = false;
            work->conflict = false;
            work->error = "An unknown error interrupted the project save.";
        }
        work->commitMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - commitBegin).count();

        ProjectTasks().PostToMain([
            this,
            projectId,
            capturedEditorRevision,
            saveToken,
            adopting,
            destination,
            adoptionSource,
            sourceStore,
            reason,
            work,
            completion = std::move(completion)
        ]() mutable {
            m_GraphPerformanceStats.lastProjectSaveCommitMs = work->commitMs;
            ProjectSaveResult result;
            result.projectId = projectId;
            result.persistedEditRevision =
                work->success ? capturedEditorRevision : 0;
            result.storageRevision = work->success
                ? work->snapshot.persistedStorageRevision
                : 0;
            result.path = (adopting
                ? destination
                : sourceStore->StoragePath()).string();
            result.message = work->error;

            try {
            const bool currentProject =
                m_Project->documentId == projectId &&
                m_Project->store == sourceStore;
            if (!currentProject) {
                result.status = ProjectSaveStatus::Canceled;
                result.message = "The project changed before the save completed.";
                if (completion) completion(std::move(result));
                return;
            }

            m_Project->lifecycle.CompleteSave(
                saveToken,
                work->success,
                work->success ? work->snapshot.persistedStorageRevision : 0,
                work->conflict);
            if (work->success) {
                if (adopting) {
                    m_Project->store = work->savedStore;
                    m_Project->storePath = destination;
                    SetCurrentProjectFileName(destination.string());
                    m_Project->adoptionSourcePath.clear();
                }
                if (m_Project->snapshot) {
                    if (m_Project->lifecycle.DirtyRevision() ==
                        saveToken.snapshotDirtyRevision) {
                        m_Project->snapshot =
                            std::make_shared<RawProjectSnapshot>(work->snapshot);
                        ApplyManagedGraphImageReferences(
                            m_Project->graph,
                            work->snapshot.pipelineData);
                    } else {
                        // A newer in-memory snapshot remains authoritative;
                        // only advance the storage fence it must save against.
                        m_Project->snapshot->persistedStorageRevision =
                            work->snapshot.persistedStorageRevision;
                        // Keep an immutable result committed by this save when the
                        // newer editor revision still uses the same bracket inputs.
                        auto& current=*m_Project->snapshot;
                        if(current.hdrInputRevision==work->snapshot.hdrInputRevision) {
                            for(const auto& savedSet:work->snapshot.sourceSets) {
                                auto* liveSet=Stack::Project::FindSourceSet(current,savedSet.sourceSetId);
                                if(!liveSet || !savedSet.settings.contains("bracketingResult") ||
                                    liveSet->settings.value("bracketing",nlohmann::json())!=savedSet.settings.value("bracketing",nlohmann::json()))continue;
                                liveSet->settings["bracketingResult"]=savedSet.settings.at("bracketingResult");
                                const auto* asset=Stack::Project::FindEmbeddedAsset(work->snapshot,
                                    savedSet.settings.at("bracketingResult").at("assetId").get<std::string>());
                                if(asset&&!Stack::Project::FindEmbeddedAsset(current,asset->assetId))current.embeddedAssets.push_back(*asset);
                            }
                        }
                        if (adopting) {
                            m_Project->snapshot->adoptedFrom =
                                adoptionSource.lexically_normal().string();
                        }
                    }
                }
                if (!ClearDirtyIfRevision(capturedEditorRevision)) {
                    m_Project->dirty = true;
                }
                result.status = ProjectSaveStatus::Saved;
                SetCurrentProjectFileName(m_Project->store->StoragePath().string());
                RefreshUnifiedProjectViewsAfterSave(
                    adopting,
                    reason != Stack::Project::ProjectSaveReason::Autosave);
            } else {
                m_Project->dirty = true;
                result.status = work->conflict
                    ? ProjectSaveStatus::Conflict
                    : ProjectSaveStatus::Failed;
                PostNotification(
                    UiNotificationSeverity::Error,
                    work->error.empty()
                        ? "Failed to save the project."
                        : work->error,
                    "unified-project-save");
            }
            } catch (const std::exception& error) {
                m_Project->lifecycle.CompleteSave(
                    saveToken, false, 0, false);
                m_Project->dirty = true;
                result.status = ProjectSaveStatus::Failed;
                result.persistedEditRevision = 0;
                result.storageRevision = 0;
                result.message = error.what();
            } catch (...) {
                m_Project->lifecycle.CompleteSave(
                    saveToken, false, 0, false);
                m_Project->dirty = true;
                result.status = ProjectSaveStatus::Failed;
                result.persistedEditRevision = 0;
                result.storageRevision = 0;
                result.message =
                    "An unknown error interrupted project save finalization.";
            }
            if (completion) completion(std::move(result));
        });
    };
    // Explicit flushes should start promptly. Autosaves remain ordinary
    // background work so asset I/O cannot jump ahead of interactive tasks.
    const bool submitted =
        reason == Stack::Project::ProjectSaveReason::Autosave
        ? ProjectTasks().Submit("Saving",std::move(saveWork))
        : ProjectTasks().SubmitHighPriority("Saving",std::move(saveWork));

    if (!submitted) {
        m_Project->lifecycle.CancelSave(saveToken);
        failImmediately("The project save could not be queued.");
    }
}

bool EditorModule::SaveActiveMultiFrameRawProjectAs(
    const std::filesystem::path& destination,
    Stack::Project::ProjectStorageKind storageKind,
    std::string* errorMessage) {
    if (!IsUnifiedProjectStoreActive() || destination.empty()) {
        return Finish(errorMessage, "No managed project or destination is available.", false);
    }
    // Save As is also the recovery path for a conflicted, read-only, or
    // otherwise unwritable current store.  Do not require a commit back to
    // that store first: the active snapshot and graph below are the complete
    // in-memory project state and are written directly to the new store.
    std::filesystem::path normalizedDestination = destination.lexically_normal();
    if (storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle) {
        const std::string extension =
            Lower(normalizedDestination.extension().string());
        const std::string fileName =
            Lower(normalizedDestination.filename().string());
        if (fileName == "project.stack") {
            normalizedDestination = normalizedDestination.parent_path();
        } else if (extension == ".stack" || extension == ".stackbundle") {
            normalizedDestination = normalizedDestination.parent_path() /
                normalizedDestination.stem();
        }
    } else if (Lower(normalizedDestination.extension().string()) != ".stack") {
        normalizedDestination += ".stack";
    }
    RawProjectSnapshot snapshotForCopy = CopyProjectSnapshotWithoutPipeline(*m_Project->snapshot);
    if (!m_Project->name.empty()) snapshotForCopy.projectName = m_Project->name;
    if (storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle) {
        snapshotForCopy.projectId = Stack::Project::GenerateStableUuid();
        snapshotForCopy.lifecycle.creationOrigin = Stack::Project::ProjectCreationOrigin::Manual;
        snapshotForCopy.lifecycle.cleanupWhenUntouched = false;
        snapshotForCopy.lifecycle.explicitlyRetained = true;
        snapshotForCopy.lifecycle.autoCreatedAtDirtyRevision = 0;
        snapshotForCopy.adoptedFrom.clear();
    }
    snapshotForCopy.pipelineData = SerializePipeline();
    const bool repairedCopy =
        m_Project->lifecycle.Phase() ==
            Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery ||
        snapshotForCopy.rawWorkspaceData.value("repairRequired", false);
    if (repairedCopy) {
        snapshotForCopy.rawWorkspaceData["repairRequired"] = false;
        snapshotForCopy.rawWorkspaceData["repairedCopy"] = true;
    }
    if (IsRawWorkspaceProjectActive()) UpdateWorkspaceManifestFields(snapshotForCopy);
    Stack::Project::ProjectStoreOpenResult converted =
        Stack::Project::ConvertProjectStore(
            m_Project->store,
            snapshotForCopy,
            normalizedDestination,
            storageKind,
            [this](const auto& store, const auto& transaction, auto& snapshot, auto& error) {
                if (!CaptureBracketingDraftForCopy(store, transaction, snapshot, error)) return false;
                if (!StackBinaryFormat::ExternalizeManagedProjectAssets(store, transaction, snapshot)) {
                    error = "Could not stage the copied project's image assets.";
                    return false;
                }
                if (m_Bracketing && m_Bracketing->result &&
                    m_Bracketing->completedRevision == snapshot.hdrInputRevision) {
                    const auto* set = Stack::Project::FindSourceSet(snapshot, m_Bracketing->setId);
                    if (set && set->settings.at("bracketing").dump() == m_Bracketing->completedRecipe)
                        return Stack::Project::StageBracketingResult(store, transaction, snapshot,
                            m_Bracketing->setId, *m_Bracketing->result, {}, error);
                }
                return true;
            });
    if (!converted) return Finish(errorMessage, converted.message, false);
    if (storageKind == Stack::Project::ProjectStorageKind::PortableFile) {
        // Packing is an export. Continue editing the ordinary working folder.
        return Finish(errorMessage, std::string(), true);
    }
    // Save As starts a separate project. Completed immutable results still
    // describe the copied inputs, but workers from the old project cannot publish.
    CancelMfdExperimentalProcessing({}, false);
    CancelHdrProcessing();
    CancelMultiFrameGraphProcessing();
    InvalidateRenderSnapshotsBefore(m_RenderGeneration + 1);
    ClearRawRenderSession();
    m_Project->store = converted.store;
    m_Project->snapshot =
        std::make_shared<RawProjectSnapshot>(std::move(converted.snapshot));
    ApplyManagedGraphImageReferences(m_Project->graph, m_Project->snapshot->pipelineData);
    m_Project->storePath = normalizedDestination.lexically_normal();
    SetCurrentProjectFileName(m_Project->storePath.string());
    const auto replacement = m_Project->lifecycle.BeginReplacement();
    m_Project->lifecycle.CompleteReplacement(
        replacement,
        m_Project->snapshot->projectId,
        m_Project->snapshot->dirtyRevision,
        m_Project->snapshot->persistedStorageRevision,
        m_Project->store->IsReadOnlyRecovery());
    m_Project->dirty = false;
    const auto& copiedProjectId = m_Project->snapshot->projectId;
    m_Project->documentId = copiedProjectId;
    m_Project->adoptionSourcePath.clear();
    m_Project->saves.Reset(copiedProjectId, m_Project->editRevision);
    if (m_HdrAdoptedRawResult) m_HdrAdoptedRawResult->projectId = copiedProjectId;
    if (m_MfdAdoptedRawResult) m_MfdAdoptedRawResult->projectId = copiedProjectId;
    if (m_Bracketing && !m_Bracketing->newProject) {
        m_Bracketing->projectId = copiedProjectId;
        m_Bracketing->job.reset();
        if (m_Bracketing->presentation) m_Bracketing->presentation->active = false;
        // The copy may have remapped duplicate draft captures to managed IDs.
        m_Bracketing->storedRecipe.clear();
        m_Bracketing->selectionRestored = false;
    }
    RefreshBracketingProjectCard();
    MarkRenderRefreshDirty();
    return Finish(errorMessage, std::string(), true);
}

bool EditorModule::OptimizeActiveMultiFrameRawProject(std::string* errorMessage) {
    if (!IsMultiFrameRawProjectActive()) {
        return Finish(errorMessage, "No multi-frame RAW project is active.", false);
    }
    if (IsDirty() && !SaveActiveMultiFrameRawProject(errorMessage)) return false;
    const Stack::Project::ProjectSaveToken maintenanceToken =
        m_Project->lifecycle.BeginSave();
    if (!maintenanceToken) {
        return Finish(
            errorMessage,
            "The project cannot be optimized in its current session state.",
            false);
    }
    std::string optimizeError;
    const bool optimized = m_Project->store->Optimize(&optimizeError);
    const bool conflict = !optimized &&
        optimizeError.find("changed while it was being optimized") != std::string::npos;
    m_Project->lifecycle.CompleteSave(
        maintenanceToken,
        optimized,
        m_Project->store->StorageRevision(),
        conflict);
    if (!optimized) return Finish(errorMessage, optimizeError, false);
    m_Project->snapshot->persistedStorageRevision =
        m_Project->store->StorageRevision();
    return Finish(errorMessage, std::string(), true);
}

bool EditorModule::ValidateAndRepairActiveRawProjectGraphBindings(
    bool* requiresRepairedCopy,
    std::string* outError) {
    if (requiresRepairedCopy) *requiresRepairedCopy = false;
    if (!m_Project->snapshot) return true;
    if (!m_Project->snapshot->multiFrameGraph.nodes.empty()) {
        // The typed graph remains authoritative. The EditorGraph renderer
        // bridge is derived from it and may be rebuilt without rewriting the
        // saved typed topology.
        const Raw::MultiFrame::GraphExecutionPlan plan =
            Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(
                *m_Project->snapshot);
        const Raw::MultiFrame::GraphExecutionStep* terminal =
            Raw::MultiFrame::FindGraphExecutionStep(
                plan, plan.outputProducerNodeId);
        const bool finalHdr = terminal && terminal->adapter ==
            Raw::MultiFrame::GraphExecutionAdapter::HdrV4;
        const bool finalBurst = terminal && terminal->adapter ==
            Raw::MultiFrame::GraphExecutionAdapter::SharedBurstV1;
        MultiFrameSourceSet* activeSet = Stack::Project::FindSourceSet(
            *m_Project->snapshot,
            m_Project->snapshot->activeSourceSetId);
        if (!activeSet &&
            m_Project->snapshot->sourceSets.size() == 1u) {
            activeSet = &m_Project->snapshot->sourceSets.front();
        }
        if ((finalHdr || finalBurst) && activeSet) {
            if (finalHdr) EnsureHdrPostRecipe(*activeSet);
            else EnsureMfdPostRecipe(*activeSet);
            const bool bridgePresent = finalHdr
                ? FindHdrNode(
                    m_Project->graph,
                    activeSet->sourceSetId,
                    activeSet->graphBindingNodeId) != nullptr
                : FindMfdNode(
                    m_Project->graph,
                    activeSet->sourceSetId,
                    activeSet->graphBindingNodeId) != nullptr;
            if (!bridgePresent) {
                EditorNodeGraph::Graph bridgeGraph = m_Project->graph;
                std::vector<int> obsoleteNodes;
                for (const EditorNodeGraph::Node& node :
                     bridgeGraph.GetNodes()) {
                    const bool neutral =
                        node.kind ==
                            EditorNodeGraph::NodeKind::RawProjectSourceSet &&
                        node.rawProjectSourceSet.sourceSetId ==
                            activeSet->sourceSetId;
                    const bool oppositeHdr = finalBurst &&
                        node.kind ==
                            EditorNodeGraph::NodeKind::MultiFrameHdr &&
                        node.multiFrameHdr.sourceSetId ==
                            activeSet->sourceSetId;
                    const bool oppositeBurst = finalHdr &&
                        node.kind ==
                            EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                        node.multiFrameDenoise.sourceSetId ==
                            activeSet->sourceSetId;
                    if (neutral || oppositeHdr || oppositeBurst) {
                        obsoleteNodes.push_back(node.id);
                    }
                }
                for (int nodeId : obsoleteNodes) {
                    bridgeGraph.RemoveNode(nodeId);
                }
                std::string bridgeError;
                const bool bridgeReady = finalHdr
                    ? SyncHdrGraphTopology(
                        bridgeGraph,
                        *m_Project->snapshot,
                        *activeSet,
                        true,
                        bridgeError)
                    : SyncMfdGraphTopology(
                        bridgeGraph,
                        *m_Project->snapshot,
                        *activeSet,
                        true,
                        bridgeError);
                if (!bridgeReady) {
                    return Finish(
                        outError,
                        bridgeError.empty()
                            ? "The typed MultiFrame Output could not rebuild its RAW renderer bridge."
                            : bridgeError,
                        false);
                }
                m_Project->graph = std::move(bridgeGraph);
                RefreshGraphLayerMetadata();
                ApplyGraphLayerOrder();
                MarkRenderRefreshDirty();
            }
        }
        return true;
    }

    bool changed = false;
    bool quarantined = false;
    std::unordered_map<std::string, std::vector<EditorNodeGraph::Node*>> nodesBySet;
    for (EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
            node.rawProjectSourceSet.managed &&
            !node.rawProjectSourceSet.quarantined) {
            nodesBySet[node.rawProjectSourceSet.sourceSetId].push_back(&node);
        }
    }
    std::unordered_set<std::string> knownSetIds;
    float nextY = 120.0f;
    for (MultiFrameSourceSet& sourceSet : m_Project->snapshot->sourceSets) {
        knownSetIds.insert(sourceSet.sourceSetId);
        auto found = nodesBySet.find(sourceSet.sourceSetId);
        if (found == nodesBySet.end() || found->second.empty()) {
            EditorNodeGraph::RawProjectSourceSetPayload payload;
            payload.sourceSetId = sourceSet.sourceSetId;
            EditorNodeGraph::Node* node = m_Project->graph.AddRawProjectSourceSetNode(
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
    for (EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
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
    for (EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
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
        } else if (node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr &&
                   node.multiFrameHdr.managed &&
                   !node.multiFrameHdr.quarantined) {
            sourceSetId = node.multiFrameHdr.sourceSetId;
            if (knownSetIds.find(sourceSetId) == knownSetIds.end()) {
                node.multiFrameHdr.managed = false;
                node.multiFrameHdr.quarantined = true;
                node.multiFrameHdr.presentationStatus =
                    "Quarantined orphan HDR binding. Save Repaired Copy is required.";
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
        m_Project->snapshot->pipelineData = PipelineForGraph(
            m_Project->graph, m_Project->layers, m_Project->snapshot->pipelineData);
        MarkDirty();
    }
    if (quarantined) {
        m_Project->snapshot->rawWorkspaceData["repairRequired"] = true;
        m_Project->lifecycle.MarkReadOnlyRecovery();
        if (requiresRepairedCopy) *requiresRepairedCopy = true;
    }
    return true;
}

void EditorModule::RenderMultiFrameRawLabCreationPopup() {
    auto& dialog = m_ProjectInteractionUi.multiFrame;
    auto& projectName = dialog.projectName;
    auto& destinationFolder = dialog.destinationFolder;
    auto& selectedPaths = dialog.selectedPaths;
    auto& referenceFrameIndex = dialog.referenceFrameIndex;
    auto& selectedSummaries = dialog.selectedSummaries;
    auto& selectedSummaryWarnings = dialog.selectedSummaryWarnings;
    const auto refreshSelectedSummaries = [&]() {
        selectedSummaries.assign(selectedPaths.size(), {});
        selectedSummaryWarnings.assign(selectedPaths.size(), {});
        for (std::size_t i = 0; i < selectedPaths.size(); ++i) {
            try {
                Raw::RawMetadata metadata;
                if (!Raw::RawLoader::LoadMetadata(
                        selectedPaths[i].string(), metadata)) {
                    selectedSummaryWarnings[i] = metadata.error.empty()
                        ? "RAW metadata unavailable" : metadata.error;
                    continue;
                }
                selectedSummaries[i] = BuildMfdCaptureSummary(metadata);
                if (!selectedSummaries[i].supported) {
                    selectedSummaryWarnings[i] =
                        selectedSummaries[i].rejectionReason;
                }
            } catch (const std::exception& exception) {
                selectedSummaryWarnings[i] =
                    std::string("RAW metadata inspection failed: ") +
                    exception.what();
            } catch (...) {
                selectedSummaryWarnings[i] =
                    "RAW metadata inspection failed unexpectedly.";
            }
        }
        if (!selectedSummaries.empty()) {
            for (std::size_t i = 1; i < selectedSummaries.size(); ++i) {
                std::string compatibilityReason;
                if (!Stack::Project::AreMfdCapturesStructurallyCompatible(
                        selectedSummaries.front(),
                        selectedSummaries[i],
                        &compatibilityReason) &&
                    selectedSummaryWarnings[i].empty()) {
                    selectedSummaryWarnings[i] = compatibilityReason;
                }
            }
        }
    };

    if (m_PopulateMultiFrameCreationFromGallery) {
        selectedPaths = m_PendingMultiFrameGallerySourcePaths;
        m_PendingMultiFrameGallerySourcePaths.clear();
        m_PopulateMultiFrameCreationFromGallery = false;
        refreshSelectedSummaries();
        const char* defaultName =
            m_PendingMultiFrameCreationIntent == MultiFrameOperationIntent::RawBurstDenoise
                ? "Burst Project"
                : (m_PendingMultiFrameCreationIntent == MultiFrameOperationIntent::RawBurstHdr
                    ? "HDR Project"
                    : "Bracket Project");
        std::snprintf(projectName, sizeof(projectName), "%s", defaultName);
    }
    if (m_OpenMultiFrameCreationPopup) {
        ImGui::OpenPopup("Create Multi-Frame Project");
        m_OpenMultiFrameCreationPopup = false;
    }

    if (!ImGui::BeginPopupModal(
            "Create Multi-Frame Project",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (destinationFolder.empty() && !m_RawWorkspace.workspaceRoot.empty()) {
        destinationFolder = Stack::RawWorkspace::BuildManagedLayout(
            m_RawWorkspace.workspaceRoot).projectsDirectory;
    }
    const bool creatingBurst =
        m_PendingMultiFrameCreationIntent == MultiFrameOperationIntent::RawBurstDenoise;
    const bool creatingHdr =
        m_PendingMultiFrameCreationIntent == MultiFrameOperationIntent::RawBurstHdr;
    ImGui::TextWrapped(
        creatingBurst
            ? "Create a Burst project from compatible, still-mosaiced Bayer RAW captures. Every selected original is copied into managed project assets."
            : (creatingHdr
                ? "Create an HDR project from compatible bracketed RAW captures. Every selected original is copied into managed project assets."
                : "Create a bracket from Bayer RAW captures. Exposure groups and global RAW alignment are suggested automatically."));
    ImGui::Separator();
    ImGui::InputText("Project name", projectName, sizeof(projectName));
    ImGui::TextDisabled("Format: Working project folder (project.stack + assets/)");
    ImGui::TextDisabled(
        "%s",
        creatingBurst
            ? "Operation: Burst denoise"
            : (creatingHdr ? "Operation: HDR merge" : "Operation: Bracketing"));

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
                "Choose RAW Capture Set",
                "RAW Frames\0*.dng;*.cr2;*.cr3;*.nef;*.nrw;*.arw;*.srf;*.sr2;*.raf;*.rw2;*.orf;*.pef;*.3fr;*.fff;*.iiq;*.rwl;*.raw\0All Files\0*.*\0");
        selectedPaths.clear();
        for (const std::string& path : selected) selectedPaths.emplace_back(path);
        refreshSelectedSummaries();
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
        if ((creatingBurst || creatingHdr) && ImGui::BeginCombo("Reference frame", referenceLabel.c_str())) {
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
        std::vector<double> exposureMetrics;
        exposureMetrics.reserve(selectedSummaries.size());
        for (const RawCaptureCompatibilitySummary& summary : selectedSummaries) {
            if (summary.exposureTimeSeconds > 0.0) {
                const double aperture = summary.apertureFNumber > 0.0
                    ? summary.apertureFNumber : 1.0;
                const double iso = summary.isoSpeed > 0.0 ? summary.isoSpeed : 100.0;
                exposureMetrics.push_back(
                    summary.exposureTimeSeconds * iso / (aperture * aperture));
            }
        }
        double medianExposure = 0.0;
        if (!exposureMetrics.empty()) {
            std::sort(exposureMetrics.begin(), exposureMetrics.end());
            medianExposure = exposureMetrics[exposureMetrics.size() / 2u];
        }
        if (ImGui::BeginTable("##HdrCreationFrames", 2,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (std::size_t i = 0; i < selectedPaths.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(selectedPaths[i].filename().string().c_str());
                ImGui::TableSetColumnIndex(1);
                if (i < selectedSummaries.size() &&
                    selectedSummaries[i].exposureTimeSeconds > 0.0) {
                    const RawCaptureCompatibilitySummary& summary = selectedSummaries[i];
                    const double aperture = summary.apertureFNumber > 0.0
                        ? summary.apertureFNumber : 1.0;
                    const double iso = summary.isoSpeed > 0.0 ? summary.isoSpeed : 100.0;
                    const double metric = summary.exposureTimeSeconds * iso /
                        (aperture * aperture);
                    const double ev = medianExposure > 0.0
                        ? std::log2(metric / medianExposure) : 0.0;
                    ImGui::TextDisabled("%.6g s  f/%.1f  ISO %.0f  %+0.2f EV",
                        summary.exposureTimeSeconds,
                        summary.apertureFNumber,
                        summary.isoSpeed,
                        ev);
                } else {
                    ImGui::TextDisabled("Exposure metadata unavailable");
                }
                if (i < selectedSummaryWarnings.size() &&
                    !selectedSummaryWarnings[i].empty()) {
                    ImGui::TextColored(ImVec4(0.94f, 0.72f, 0.34f, 1.0f), "%s",
                        selectedSummaryWarnings[i].c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Camera and sensor compatibility is checked during import.");
        if(!creatingBurst&&!creatingHdr) ImGui::TextWrapped("The median exposure fixes the brightness origin. You can edit the suggested groups after import.");
    }

    const bool ready = projectName[0] != '\0' && !destinationFolder.empty() &&
        !selectedPaths.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Create")) {
        std::filesystem::path destination =
            destinationFolder / std::filesystem::path(projectName).filename();
        std::string error;
        const std::string requestedProjectName(projectName);
        const std::vector<std::filesystem::path> requestedPaths = selectedPaths;
        const std::size_t requestedReferenceIndex =
            static_cast<std::size_t>(referenceFrameIndex);
        const MultiFrameOperationIntent requestedIntent =
            m_PendingMultiFrameCreationIntent;
        const std::string requestedSourceSetName = creatingBurst
            ? "Burst"
            : (creatingHdr ? "HDR Bracket" : "Capture Set");
        auto createAction = [
            this,
            destination,
            requestedProjectName,
            requestedPaths,
            requestedReferenceIndex,
            requestedIntent,
            requestedSourceSetName
        ](std::string* actionError) {
            const bool created = CreateMultiFrameRawProject(
                destination,
                Stack::Project::ProjectStorageKind::DirectoryBundle,
                requestedProjectName,
                requestedSourceSetName,
                requestedIntent,
                requestedPaths,
                requestedReferenceIndex,
                actionError);
            if (created) {
                // Leave the windowed selection mode as soon as activation is
                // queued; the deferred project apply will then drill into the
                // new project's frames and present the Multi-Frame workspace.
                m_RawWorkspaceLabUi.galleryNavigationMode =
                    RawGalleryNavigationMode::ProjectRoot;
                m_RawWorkspaceLabUi.galleryProjectId.clear();
                CloseRawWorkspaceLabNativeGallery();
                m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
                m_ReturnToMultiFrameAfterGalleryCreationCancel = false;
                RequestOpenMultiFrameTab();
                InvalidateRawWorkspaceGalleryPresentation();
            }
            return created;
        };

        if (NeedsWorkspaceSaveBeforeTransition() || IsProjectFileSaveBusy()) {
            QueueRawWorkspaceProjectReplacement(
                creatingBurst
                    ? "create a new Burst project"
                    : (creatingHdr
                        ? "create a new HDR project"
                        : "create a new MultiFrame capture-set project"),
                requestedProjectName,
                std::move(createAction));
            m_RawWorkspaceLabUi.multiFrameStatusText =
                "Saving the current project before creating the capture set...";
            selectedPaths.clear();
            ImGui::CloseCurrentPopup();
        } else if (createAction(&error)) {
            m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
            m_RawWorkspaceLabUi.multiFrameStatusText =
                creatingBurst
                    ? "New Burst project created and opening."
                    : (creatingHdr
                        ? "New HDR project created and opening."
                        : "New capture-set project created and opening.");
            selectedPaths.clear();
            ImGui::CloseCurrentPopup();
        } else {
            m_RawWorkspaceLabUi.multiFrameStatusText = error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        const bool returnToMultiFrame =
            m_ReturnToMultiFrameAfterGalleryCreationCancel;
        selectedPaths.clear();
        if (m_RawWorkspaceLabUi.galleryNavigationMode ==
            RawGalleryNavigationMode::MultiFrameCreation) {
            m_RawWorkspace.selectedSourceKey =
                m_RawWorkspaceGallerySelectionRestoreKey;
            m_RawWorkspace.selectedSourceKeys =
                m_RawWorkspaceGallerySelectionRestoreKeys;
            m_RawWorkspaceLabUi.galleryNavigationMode =
                m_RawWorkspaceGalleryRestoreNavigationMode;
            m_RawWorkspaceLabUi.galleryProjectId =
                m_RawWorkspaceGalleryRestoreProjectId;
            m_RawWorkspaceLabUi.galleryHost =
                m_RawWorkspaceGalleryRestoreHost;
            m_ReturnToMultiFrameAfterGalleryCreationCancel = false;
            InvalidateRawWorkspaceGalleryPresentation();
        }
        if (returnToMultiFrame) {
            RequestOpenMultiFrameTab();
        }
        ImGui::CloseCurrentPopup();
    }
    if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
        ImGui::TextWrapped("%s", m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
    }
    ImGui::EndPopup();
}

void EditorModule::RenderMultiFrameRawLabTool() {
    if (IsBracketingActive()) {
        ImGui::TextWrapped("Edit exposure groups and contributions in Bracketing.");
        if (ImGui::Button("Open Bracketing")) RequestOpenMultiFrameTab();
        return;
    }
    if (!IsMultiFrameRawProjectActive()) {
        if (IsRawWorkspaceProjectActive()) {
            ImGui::TextWrapped(
                "This project contains one RAW source. Create Burst and HDR "
                "projects from a multi-selection in the Library.");
        } else {
            ImGui::TextDisabled("No multi-frame RAW project is active.");
            ImGui::Spacing();
            if (ImGui::Button("Create Capture Set")) {
                m_RawWorkspaceLabUi.multiFrameStatusText.clear();
                m_OpenMultiFrameCreationPopup = true;
            }
            ImGui::TextWrapped(
                "Select one or more RAW images in Gallery. Processing is chosen later in MultiFrame.");
        }
        if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
            ImGui::TextWrapped(
                "%s",
                m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
        }
        return;
    }

    const int selectedNodeId = m_Project->graph.GetSelectedNodeId();
    if (const EditorNodeGraph::Node* selectedNode =
            m_Project->graph.FindNode(selectedNodeId);
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
        } else if (selectedNode->kind ==
                   EditorNodeGraph::NodeKind::MultiFrameHdr) {
            ActivateMultiFrameSourceSet(
                selectedNode->multiFrameHdr.sourceSetId);
        }
    }

    RawProjectSnapshot& snapshot = *m_Project->snapshot;
    MultiFrameSourceSet* active = Stack::Project::FindSourceSet(
        snapshot,
        snapshot.activeSourceSetId);
    if (active == nullptr) {
        ImGui::TextDisabled("This project has no active source set.");
        return;
    }
    if (active->operationIntent == MultiFrameOperationIntent::RawCaptureSet) {
        ImGui::TextUnformatted(active->name.c_str());
        ImGui::TextDisabled(
            "%llu neutral RAW captures",
            static_cast<unsigned long long>(active->frames.size()));
        ImGui::Spacing();
        ImGui::TextWrapped(
            "This dataset has no processing objective yet. Add Burst Denoise or "
            "HDR Merge from the processing stage in the MultiFrame graph.");
        if (ImGui::Button("Open MultiFrame")) {
            RequestOpenMultiFrameTab();
        }
        return;
    }
    if (active->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        RenderHdrRawLabTool();
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
            ? (asset ? asset->originalFilename.c_str() : "Missing asset")
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
        RequestRawSettingsPanel();
        m_RawLabMultiFrameAdvancedOpenedThisFrame = true;
    }

    if (active->operationIntent !=
            MultiFrameOperationIntent::RawBurstDenoise ||
        active->operationSchemaVersion !=
            Stack::Project::kMfdOperationSchemaVersion) {
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
        m_MfdExperimentalParameterDraft.fusionMethod =
            displayedParameters.fusion.method == "robust" ? 0 : 1;
        m_MfdExperimentalParameterDraft.fusionSmoothing =
            displayedParameters.fusion.smoothing;
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
        Stack::UiActivity::BeginDisabledForWork(processingBusy);
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
                ? "The Burst preparation parameter object is invalid."
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
            "%llu/%llu alternates included - %.1f%% alternate coverage",
            static_cast<unsigned long long>(report->acceptedAlternateCount),
            static_cast<unsigned long long>(report->compatibleAlternateCount),
            report->contributingPixelFraction * 100.0);
        ImGui::TextDisabled(
            "Average effective frames: %.2f of %llu",
            report->meanEffectiveSampleCount,
            static_cast<unsigned long long>(
                report->acceptedAlternateCount + 1u));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Coverage says where alternates participated. Effective frames says how strongly they were actually averaged.");
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
    if (IsBracketingActive()) return;
    if (!IsMultiFrameRawProjectActive()) {
        if (IsRawWorkspaceProjectActive()) {
            ImGui::TextWrapped(
                "This project contains one RAW source. Create Burst and HDR "
                "projects from a multi-selection in the Library.");
        } else {
            ImGui::TextDisabled("No multi-frame RAW project is active.");
            if (ImGui::Button("Create New MFD Project")) {
                m_RawWorkspaceLabUi.multiFrameStatusText.clear();
                m_OpenMultiFrameCreationPopup = true;
            }
        }
        if (!m_RawWorkspaceLabUi.multiFrameStatusText.empty()) {
            ImGui::TextWrapped(
                "%s", m_RawWorkspaceLabUi.multiFrameStatusText.c_str());
        }
        return;
    }

    const int selectedNodeId = m_Project->graph.GetSelectedNodeId();
    if (const EditorNodeGraph::Node* selectedNode =
            m_Project->graph.FindNode(selectedNodeId);
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
                       *m_Project->snapshot,
                       selectedNode->rawProjectSourceSet.sourceSetId)) {
            ActivateMultiFrameSourceSet(
                selectedNode->rawProjectSourceSet.sourceSetId);
        }
    }

    RawProjectSnapshot& snapshot = *m_Project->snapshot;
    ImGui::TextUnformatted(snapshot.projectName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s · %llu set(s) · %llu frame(s)",
        Stack::Project::ProjectStorageKindName(
            m_Project->store->StorageKind()),
        static_cast<unsigned long long>(snapshot.sourceSets.size()),
        static_cast<unsigned long long>([&]() {
            std::uint64_t count = 0;
            for (const MultiFrameSourceSet& set : snapshot.sourceSets) {
                count += static_cast<std::uint64_t>(set.frames.size());
            }
            return count;
        }()));
    ImGui::TextDisabled("%s", m_Project->storePath.string().c_str());
    const Stack::Project::ProjectLifecyclePhase phase =
        m_Project->lifecycle.Phase();
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
        m_Project->store->StorageKind() !=
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
    const bool focusedManagedRawProject =
        snapshot.sourceSets.size() == 1u && active &&
        ((active->operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
          active->operationSchemaVersion == Stack::Project::kMfdOperationSchemaVersion) ||
         active->operationIntent == MultiFrameOperationIntent::RawBurstHdr);
    const char* activeLabel = active ? active->name.c_str() : "No source set";
    if (ImGui::BeginCombo("Source set", activeLabel)) {
        for (const MultiFrameSourceSet& sourceSet : snapshot.sourceSets) {
            const bool selected = active &&
                active->sourceSetId == sourceSet.sourceSetId;
            if (ImGui::Selectable(sourceSet.name.c_str(), selected)) {
                ActivateMultiFrameSourceSet(sourceSet.sourceSetId);
                active = Stack::Project::FindSourceSet(
                    *m_Project->snapshot, sourceSet.sourceSetId);
            }
        }
        ImGui::EndCombo();
    }

    auto& newSetName = m_ProjectInteractionUi.multiFrame.newSetName;
    auto& newSetIntent = m_ProjectInteractionUi.multiFrame.newSetIntent;
    ImGui::BeginDisabled(focusedManagedRawProject);
    if (ImGui::Button("New Set")) ImGui::OpenPopup("New Multi-Frame Source Set");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(active == nullptr || focusedManagedRawProject);
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
            if (paths.empty()) {
                m_RawWorkspaceLabUi.multiFrameStatusText = "No captures selected.";
            } else {
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
                PostNotification(created ? UiNotificationSeverity::Success : UiNotificationSeverity::Error,
                    created ? "Source set created." : error.empty() ? "The source set could not be created." : error);
                if (created) ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    auto& renameSetName = m_ProjectInteractionUi.multiFrame.renameSetName;
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
            if (!renamed) PostNotification(UiNotificationSeverity::Error,
                error.empty() ? "The source set could not be renamed." : error);
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

    if (focusedManagedRawProject) {
        ImGui::TextDisabled(
            active->operationIntent == MultiFrameOperationIntent::RawBurstHdr
                ? "Operation: Scene-linear Bayer HDR"
                : "Operation: MFD / Mosaic CFA Burst Denoise");
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
    if (active->operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
        active->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        const bool internalView = active->settings.value(
            "viewTransformPlacement", std::string("internal")) != "graph";
        ImGui::TextDisabled(
            internalView
                ? "HDR/denoise fusion remains scene-linear. Display Mapping is controlled once in the RAW View tab after the merge."
                : "HDR/denoise fusion and RAW edits remain scene-linear. One graph View Transform owns display mapping.");
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
            ? (asset ? asset->originalFilename.c_str() : "Missing asset")
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
        active->operationSchemaVersion !=
            Stack::Project::kMfdOperationSchemaVersion) {
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
        m_MfdExperimentalParameterDraft.fusionMethod =
            displayedParameters.fusion.method == "robust" ? 0 : 1;
        m_MfdExperimentalParameterDraft.fusionSmoothing =
            displayedParameters.fusion.smoothing;
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
                ? "The Burst preparation and safety parameter object is invalid."
                : displayedParameterError.c_str());
    } else if (ImGui::CollapsingHeader(
            "Processing resources",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        static constexpr const char* alignmentModes[] {
            "Full - global + local",
            "Translation only",
            "None - identity coordinates"
        };
        Stack::UiActivity::BeginDisabledForWork(processingBusy);
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
        Stack::UiActivity::BeginDisabledForWork(processingBusy);
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

    const bool invalidBurstSettings =
        active->operationSchemaVersion !=
            Stack::Project::kMfdOperationSchemaVersion ||
        active->settings.value("algorithmId", std::string()) !=
            Raw::Mfd::kSharedBurstAlgorithmId;
    if (haveParameters && !invalidBurstSettings && ImGui::CollapsingHeader(
            "Shared Burst V1",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Goal: Static Maximum");
        ImGui::TextWrapped(
            "Strongly averages trustworthy aligned measurements without spatial blur or HDR highlight replacement. Hard clipping, motion, geometry, and support gates remain authoritative.");
        Raw::Mfd::SharedBurstSettings burstSettings;
        std::string settingsError;
        const auto burstValue = active->settings.find("sharedBurstSettings");
        if (burstValue != active->settings.end()) {
            Raw::Mfd::DeserializeSharedBurstSettings(
                *burstValue, burstSettings, &settingsError);
        }
        float tolerance = static_cast<float>(
            burstSettings.exposureGroupToleranceEv);
        Stack::UiActivity::BeginDisabledForWork(processingBusy, invalidBurstSettings);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat(
                "Exposure group tolerance",
                &tolerance,
                0.1f,
                2.0f,
                "%.2f EV")) {
            std::string error;
            if (!SetMfdSharedBurstExposureTolerance(
                    activeSetId, tolerance, &error)) {
                m_RawWorkspaceLabUi.multiFrameStatusText = error;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText =
                    "Shared Burst exposure group updated. Reprocess to publish a current result.";
            }
            ImGui::EndDisabled();
            return;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "Adjust frame inclusion, temporal owner/reference, and per-frame trust in the MultiFrame tab.");
        if (invalidBurstSettings) {
            ImGui::TextColored(
                ImVec4(0.92f, 0.76f, 0.42f, 1.0f),
                "Reprocess required with Shared Burst V1.");
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
            "Exposure group: %llu selected - %llu routed toward HDR",
            static_cast<unsigned long long>(report->exposureGroupedCaptureCount),
            static_cast<unsigned long long>(report->exposureExcludedCaptureCount));
        ImGui::Text(
            "Alternate coverage: %.2f%% - exact reference: %.2f%%",
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
            "Average effective frames per pixel: %.2f of %llu",
            report->meanEffectiveSampleCount,
            static_cast<unsigned long long>(
                report->acceptedAlternateCount + 1u));
        ImGui::Text("Processing backend: %s",
            report->executionBackend.empty()
                ? "CPU reference"
                : report->executionBackend.c_str());
        ImGui::Text("Registration backend: %s",
            report->registrationBackend.empty()
                ? "CPU reference"
                : report->registrationBackend.c_str());
        if (!report->registrationGpuDeviceIdentity.empty()) {
            ImGui::TextDisabled(
                "Registration GPU: %s",
                report->registrationGpuDeviceIdentity.c_str());
            ImGui::TextDisabled(
                "Registration dispatches: %u - candidates scored: %llu",
                report->registrationGpuDispatchCount,
                static_cast<unsigned long long>(
                    report->registrationGpuScoredCandidateCount));
        }
        if (!report->registrationGpuFallbackReason.empty()) {
            ImGui::TextWrapped(
                "Registration GPU fallback: %s",
                report->registrationGpuFallbackReason.c_str());
        }
        if (!report->gpuDeviceIdentity.empty()) {
            ImGui::TextDisabled("GPU: %s", report->gpuDeviceIdentity.c_str());
            ImGui::TextDisabled("GPU dispatch tiles: %u",
                report->gpuDispatchedTileCount);
        }
        if (!report->gpuFallbackReason.empty()) {
            ImGui::TextWrapped("GPU fallback: %s",
                report->gpuFallbackReason.c_str());
        }
        ImGui::TextDisabled(
            "Coverage shows where alternates participated. Effective frames shows how strongly they were actually averaged.");
        ImGui::Text(
            "Robust evidence retained: %.1f%%",
            report->meanRobustAttenuation * 100.0);
        ImGui::Text(
            "Model-only independent-noise reduction: %.2fx",
            report->predictedIndependentNoiseReduction);
        if (!report->independentNoiseReductionClaimQualified) {
            ImGui::TextDisabled(
                "Cross-frame correlation is not calibrated yet, so this is not presented as a measured sqrt(N) result.");
        }
        ImGui::TextDisabled(
            "Mean |delta|: %.7f - P99 |delta|: %.7f",
            report->meanAbsoluteDelta,
            report->percentile99AbsoluteDelta);
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
                const char* status = frame.reference
                    ? "reference"
                    : !frame.attempted
                        ? "not attempted"
                        : frame.acceptedForFusion ? "included" : "rejected";
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0.0f);
                if (frame.message.empty()) {
                    ImGui::TextWrapped(
                        "%s - %s",
                        frame.label.c_str(),
                        status);
                } else {
                    ImGui::TextWrapped(
                        "%s - %s - noise: %s - %s",
                        frame.label.c_str(),
                        status,
                        frame.noiseModelQuality.c_str(),
                        frame.message.c_str());
                }
                ImGui::PopTextWrapPos();
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
        "Shared Burst V1 publishes one signed, positive-overrange float Bayer mosaic. Stack then "
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
    if (!m_OpenMultiFrameDeletePopup) return;
    m_OpenMultiFrameDeletePopup = false;
    namespace N = Stack::Notifications;
    const auto setId = m_PendingDeleteMultiFrameSourceSetId;
    const auto document = GetProjectDocumentId();
    const auto revision = GetProjectEditRevision();
    const auto valid = [this, setId, document, revision] {
        return GetProjectDocumentId() == document && GetProjectEditRevision() == revision &&
            m_PendingDeleteMultiFrameSourceSetId == setId && m_Project->snapshot &&
            Stack::Project::FindSourceSet(*m_Project->snapshot, setId) != nullptr;
    };
    N::NoticeSpec notice;
    notice.title = "Delete source set?";
    notice.message = "Remove this source set, its managed node, and downstream links?";
    notice.details = "Shared embedded originals remain available to other sets.";
    notice.route = N::Route::Center;
    notice.foreground = m_NotificationForeground;
    notice.operationId = GetNotifier().NewOperation();
    N::ActionSpec remove;
    remove.label = "Delete";
    remove.destructive = true;
    remove.canInvoke = valid;
    remove.invoke = [this, setId] {
        std::string error;
        if (!DeleteMultiFrameSourceSet(setId, &error))
            return N::ActionResult::Failure(error.empty() ? "The source set could not be deleted." : error);
        m_PendingDeleteMultiFrameSourceSetId.clear();
        m_RawWorkspaceLabUi.multiFrameStatusText = "Source set and managed node deleted.";
        return N::ActionResult::Success();
    };
    N::ActionSpec cancel;
    cancel.label = "Cancel";
    cancel.safeCancel = true;
    cancel.invoke = [this, setId] {
        if (m_PendingDeleteMultiFrameSourceSetId == setId) m_PendingDeleteMultiFrameSourceSetId.clear();
        return N::ActionResult::Success();
    };
    notice.actions = {std::move(remove), std::move(cancel)};
    RequestNotificationDecision(std::move(notice));
}

void EditorModule::RenderMultiFrameFrameDeletePopup() {
    if (!m_OpenMultiFrameFrameDeletePopup) return;
    m_OpenMultiFrameFrameDeletePopup = false;
    namespace N = Stack::Notifications;
    const auto setId = m_PendingDeleteMultiFrameFrameSetId;
    const auto frameId = m_PendingDeleteMultiFrameFrameId;
    const auto document = GetProjectDocumentId();
    const auto revision = GetProjectEditRevision();
    const auto valid = [this, setId, frameId, document, revision] {
        return GetProjectDocumentId() == document && GetProjectEditRevision() == revision &&
            m_PendingDeleteMultiFrameFrameSetId == setId && m_PendingDeleteMultiFrameFrameId == frameId;
    };
    N::NoticeSpec notice;
    notice.title = "Remove frame?";
    notice.message = "Remove this frame, its managed node, and its protected MFD link?";
    notice.details = "The embedded original remains in the project asset store.";
    notice.route = N::Route::Center;
    notice.foreground = m_NotificationForeground;
    notice.operationId = GetNotifier().NewOperation();
    N::ActionSpec remove;
    remove.label = "Remove frame";
    remove.destructive = true;
    remove.canInvoke = valid;
    remove.invoke = [this, setId, frameId] {
        std::string error;
        if (!RemoveMultiFrameFrame(setId, frameId, &error))
            return N::ActionResult::Failure(error.empty() ? "The frame could not be removed." : error);
        m_PendingDeleteMultiFrameFrameSetId.clear();
        m_PendingDeleteMultiFrameFrameId.clear();
        m_RawWorkspaceLabUi.multiFrameStatusText = "Frame removed from the MFD burst.";
        return N::ActionResult::Success();
    };
    N::ActionSpec cancel;
    cancel.label = "Cancel";
    cancel.safeCancel = true;
    cancel.invoke = [this, setId, frameId] {
        if (m_PendingDeleteMultiFrameFrameSetId == setId && m_PendingDeleteMultiFrameFrameId == frameId) {
            m_PendingDeleteMultiFrameFrameSetId.clear();
            m_PendingDeleteMultiFrameFrameId.clear();
        }
        return N::ActionResult::Success();
    };
    notice.actions = {std::move(remove), std::move(cancel)};
    RequestNotificationDecision(std::move(notice));
}
