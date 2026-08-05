#include "App/Validation/ValidationSuites.h"

#include "Editor/EditorModule.h"
#include "Editor/Layers/LayerBase.h"
#include "NodeMath/SourceColorMetadata.h"
#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawWorkspace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

bool TransactionCheck(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "Editor graph transaction validation failed: "
            << message << '\n';
    }
    return condition;
}

class ScopedValidationDirectory {
public:
    explicit ScopedValidationDirectory(std::filesystem::path path)
        : m_Path(std::move(path)) {}

    ~ScopedValidationDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_Path, error);
    }

    const std::filesystem::path& Path() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};

bool LinksMatch(
    const EditorNodeGraph::Link& lhs,
    const EditorNodeGraph::Link& rhs) {
    return lhs.fromNodeId == rhs.fromNodeId &&
        lhs.fromSocketId == rhs.fromSocketId &&
        lhs.toNodeId == rhs.toNodeId &&
        lhs.toSocketId == rhs.toSocketId;
}

bool LinkVectorsMatch(
    const std::vector<EditorNodeGraph::Link>& lhs,
    const std::vector<EditorNodeGraph::Link>& rhs) {
    return lhs.size() == rhs.size() &&
        std::equal(
            lhs.begin(),
            lhs.end(),
            rhs.begin(),
            LinksMatch);
}

std::vector<std::pair<int, EditorNodeGraph::Vec2>>
CaptureNodePositions(const EditorNodeGraph::Graph& graph) {
    std::vector<std::pair<int, EditorNodeGraph::Vec2>> result;
    result.reserve(graph.GetNodes().size());
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        result.emplace_back(node.id, node.position);
    }
    return result;
}

bool PositionsMatch(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::pair<int, EditorNodeGraph::Vec2>>&
        positions) {
    for (const auto& entry : positions) {
        const EditorNodeGraph::Node* node =
            graph.FindNode(entry.first);
        if (!node ||
            node->position.x != entry.second.x ||
            node->position.y != entry.second.y) {
            return false;
        }
    }
    return true;
}

EditorNodeGraph::ImagePayload MakeSentinelImage(
    const std::string& label,
    unsigned char value) {
    EditorNodeGraph::ImagePayload payload;
    payload.label = label;
    payload.width = 8;
    payload.height = 8;
    payload.channels = 4;
    payload.originalChannels = 4;
    payload.pixels.resize(8u * 8u * 4u, value);
    payload.sourceColorMetadata = Stack::NodeMath::InspectSourceColorMetadata(
        {},
        payload.width,
        payload.height,
        payload.originalChannels,
        Stack::NodeMath::LogicalPrecision::UInt8,
        label);
    return payload;
}

bool ValidateImageAverageRollback() {
    EditorModule editor;
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.Clear();

    const int sourceAId = graph.AddImageNode(
        MakeSentinelImage("Average A", 32u),
        { 0.0f, -100.0f })->id;
    const int sourceBId = graph.AddImageNode(
        MakeSentinelImage("Average B", 224u),
        { 0.0f, 100.0f })->id;
    const int averageId = graph.AddDataMathNode(
        EditorNodeGraph::DataMathMode::ImageAverage,
        { 280.0f, 0.0f })->id;
    const int outputId = graph.AddOutputNode(
        { 560.0f, 0.0f },
        true)->id;
    std::string error;
    if (!graph.TryConnectSockets(
            sourceAId,
            EditorNodeGraph::kImageOutputSocketId,
            averageId,
            EditorNodeGraph::DataMathInputSocketId(0),
            &error) ||
        !graph.TryConnectSockets(
            sourceBId,
            EditorNodeGraph::kImageOutputSocketId,
            averageId,
            EditorNodeGraph::DataMathInputSocketId(1),
            &error) ||
        !graph.TryConnectSockets(
            averageId,
            EditorNodeGraph::kImageOutputSocketId,
            outputId,
            EditorNodeGraph::kImageInputSocketId,
            &error)) {
        return TransactionCheck(
            false,
            "could not author the Image Average rollback fixture: " +
                error);
    }

    graph.EditLinks().push_back(EditorNodeGraph::Link{
        averageId,
        EditorNodeGraph::kImageOutputSocketId,
        graph.GetNextNodeId() + 1000,
        EditorNodeGraph::kImageInputSocketId
    });
    graph.SelectLink(
        averageId,
        EditorNodeGraph::kImageOutputSocketId,
        outputId,
        EditorNodeGraph::kImageInputSocketId);

    const std::vector<EditorNodeGraph::Link> linksBefore =
        graph.GetLinks();
    const auto positionsBefore = CaptureNodePositions(graph);
    const std::size_t nodeCountBefore = graph.GetNodes().size();
    const int nextNodeIdBefore = graph.GetNextNodeId();
    const unsigned char* pixelsBefore =
        graph.FindNode(sourceAId)->image.pixels.data();

    const bool split =
        editor.SplitImageAverageNodeIntoChannelAverages(
            averageId);
    const EditorNodeGraph::Link* selectedLink =
        graph.GetSelectedLink();
    return TransactionCheck(
        !split,
        "a malformed late Image Average output unexpectedly split") &&
        TransactionCheck(
            graph.GetNodes().size() == nodeCountBefore &&
            graph.GetNextNodeId() == nextNodeIdBefore &&
            graph.FindNode(averageId) != nullptr,
            "failed Image Average expansion changed nodes or IDs") &&
        TransactionCheck(
            LinkVectorsMatch(graph.GetLinks(), linksBefore),
            "failed Image Average expansion changed link order") &&
        TransactionCheck(
            PositionsMatch(graph, positionsBefore),
            "failed Image Average expansion moved downstream nodes") &&
        TransactionCheck(
            selectedLink &&
            LinksMatch(*selectedLink, linksBefore[2]),
            "failed Image Average expansion lost link selection") &&
        TransactionCheck(
            graph.FindNode(sourceAId)->image.pixels.data() ==
                pixelsBefore,
            "failed Image Average expansion replaced embedded pixels");
}

bool ValidateImageAverageSuccess() {
    EditorModule editor;
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.Clear();

    const int sourceAId = graph.AddImageNode(
        MakeSentinelImage("Average success A", 48u),
        { 0.0f, -100.0f })->id;
    const int sourceBId = graph.AddImageNode(
        MakeSentinelImage("Average success B", 192u),
        { 0.0f, 100.0f })->id;
    const int averageId = graph.AddDataMathNode(
        EditorNodeGraph::DataMathMode::ImageAverage,
        { 280.0f, 0.0f })->id;
    const int outputId = graph.AddOutputNode(
        { 560.0f, 0.0f },
        true)->id;
    if (!graph.TryConnectSockets(
            sourceAId,
            EditorNodeGraph::kImageOutputSocketId,
            averageId,
            EditorNodeGraph::DataMathInputSocketId(0)) ||
        !graph.TryConnectSockets(
            sourceBId,
            EditorNodeGraph::kImageOutputSocketId,
            averageId,
            EditorNodeGraph::DataMathInputSocketId(1)) ||
        !graph.TryConnectSockets(
            averageId,
            EditorNodeGraph::kImageOutputSocketId,
            outputId,
            EditorNodeGraph::kImageInputSocketId)) {
        return TransactionCheck(
            false,
            "could not author the Image Average success fixture");
    }

    const unsigned char* pixelsBefore =
        graph.FindNode(sourceAId)->image.pixels.data();
    const bool split =
        editor.SplitImageAverageNodeIntoChannelAverages(
            averageId);
    const EditorNodeGraph::Link* outputInput =
        graph.FindInputLink(
            outputId,
            EditorNodeGraph::kImageInputSocketId);
    const EditorNodeGraph::Node* outputSource =
        outputInput
            ? graph.FindNode(outputInput->fromNodeId)
            : nullptr;
    return TransactionCheck(
        split && graph.FindNode(averageId) == nullptr,
        "valid Image Average expansion failed") &&
        TransactionCheck(
            outputSource &&
            outputSource->kind ==
                EditorNodeGraph::NodeKind::ChannelCombine,
            "Image Average expansion did not reconnect its output") &&
        TransactionCheck(
            graph.FindNode(sourceAId)->image.pixels.data() ==
                pixelsBefore,
            "Image Average expansion replaced source pixels");
}

bool AuthorLayerSplitFixture(
    EditorModule& editor,
    int& layerNodeId,
    int& outputNodeId,
    int& sentinelImageId) {
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.Clear();
    EditorNodeGraph::Node* generator =
        graph.AddImageGeneratorNode(
            EditorNodeGraph::ImageGeneratorKind::SolidColor,
            { 0.0f, 0.0f });
    if (!generator) {
        return false;
    }
    // Graph node storage may reallocate as the rest of the fixture is added;
    // retain the stable ID rather than dereferencing a stale node pointer.
    const int generatorId = generator->id;
    editor.AddLayerNodeAt(
        LayerType::Brightness,
        { 280.0f, 0.0f });
    layerNodeId = graph.GetSelectedNodeId();
    EditorNodeGraph::Node* output =
        graph.AddOutputNode({ 560.0f, 0.0f }, true);
    if (!output) {
        return false;
    }
    outputNodeId = output->id;
    sentinelImageId = graph.AddImageNode(
        MakeSentinelImage("Layer split sentinel", 91u),
        { 0.0f, 260.0f })->id;
    const EditorNodeGraph::Node* layerNode = graph.FindNode(layerNodeId);
    if (!layerNode || layerNode->kind != EditorNodeGraph::NodeKind::Layer) {
        std::cerr << "Layer split fixture did not create a Brightness layer node.\n";
        return false;
    }
    std::string error;
    if (!graph.TryConnectSockets(
            generatorId,
            EditorNodeGraph::kImageOutputSocketId,
            layerNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &error)) {
        std::cerr << "Layer split fixture input connection failed: " << error << '\n';
        return false;
    }
    if (!graph.TryConnectSockets(
            layerNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &error)) {
        std::cerr << "Layer split fixture output connection failed: " << error << '\n';
        return false;
    }
    return true;
}

bool ValidateLayerSplitRollback() {
    EditorModule editor;
    int layerNodeId = -1;
    int outputNodeId = -1;
    int sentinelImageId = -1;
    if (!AuthorLayerSplitFixture(
            editor,
            layerNodeId,
            outputNodeId,
            sentinelImageId)) {
        return TransactionCheck(
            false,
            "could not author the layer split rollback fixture");
    }
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.EditLinks().push_back(EditorNodeGraph::Link{
        layerNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        graph.GetNextNodeId() + 1000,
        EditorNodeGraph::kImageInputSocketId
    });
    graph.SelectLink(
        layerNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        outputNodeId,
        EditorNodeGraph::kImageInputSocketId);

    const std::vector<EditorNodeGraph::Link> linksBefore =
        graph.GetLinks();
    const auto positionsBefore = CaptureNodePositions(graph);
    const std::size_t nodeCountBefore = graph.GetNodes().size();
    const int nextNodeIdBefore = graph.GetNextNodeId();
    const unsigned char* pixelsBefore =
        graph.FindNode(sentinelImageId)->image.pixels.data();

    const bool split =
        editor.SplitLayerNodeIntoChannels(layerNodeId);
    const EditorNodeGraph::Link* selectedLink =
        graph.GetSelectedLink();
    return TransactionCheck(
        !split,
        "a malformed late layer output unexpectedly split") &&
        TransactionCheck(
            graph.GetNodes().size() == nodeCountBefore &&
            graph.GetNextNodeId() == nextNodeIdBefore &&
            graph.FindNode(layerNodeId) != nullptr,
            "failed layer split changed nodes or IDs") &&
        TransactionCheck(
            LinkVectorsMatch(graph.GetLinks(), linksBefore),
            "failed layer split changed link order") &&
        TransactionCheck(
            PositionsMatch(graph, positionsBefore),
            "failed layer split moved downstream nodes") &&
        TransactionCheck(
            selectedLink &&
            LinksMatch(*selectedLink, linksBefore[1]),
            "failed layer split lost link selection") &&
        TransactionCheck(
            graph.FindNode(sentinelImageId)->image.pixels.data() ==
                pixelsBefore,
            "failed layer split replaced unrelated embedded pixels");
}

bool ValidateLayerSplitSuccess() {
    EditorModule editor;
    int layerNodeId = -1;
    int outputNodeId = -1;
    int sentinelImageId = -1;
    if (!AuthorLayerSplitFixture(
            editor,
            layerNodeId,
            outputNodeId,
            sentinelImageId)) {
        return TransactionCheck(
            false,
            "could not author the layer split success fixture");
    }
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    const unsigned char* pixelsBefore =
        graph.FindNode(sentinelImageId)->image.pixels.data();
    const bool split =
        editor.SplitLayerNodeIntoChannels(layerNodeId);
    const EditorNodeGraph::Link* outputInput =
        graph.FindInputLink(
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId);
    const EditorNodeGraph::Node* outputSource =
        outputInput
            ? graph.FindNode(outputInput->fromNodeId)
            : nullptr;
    const std::size_t layerNodeCount = static_cast<std::size_t>(
        std::count_if(
            graph.GetNodes().begin(),
            graph.GetNodes().end(),
            [](const EditorNodeGraph::Node& node) {
                return node.kind ==
                    EditorNodeGraph::NodeKind::Layer;
            }));
    return TransactionCheck(
        split && graph.FindNode(layerNodeId) == nullptr,
        "valid layer split failed") &&
        TransactionCheck(
            outputSource &&
            outputSource->kind ==
                EditorNodeGraph::NodeKind::ChannelCombine &&
            layerNodeCount == 4u,
            "layer split did not create four channel layers") &&
        TransactionCheck(
            graph.FindNode(sentinelImageId)->image.pixels.data() ==
                pixelsBefore,
            "layer split replaced unrelated embedded pixels");
}

bool ValidateRawWorkspaceEditorRoundTrips() {
    namespace RawRecipe = Stack::RawRecipe;
    namespace RawWorkspace = Stack::RawWorkspace;

    const auto uniqueSuffix = std::chrono::steady_clock::now()
        .time_since_epoch()
        .count();
    ScopedValidationDirectory testDirectory(
        std::filesystem::temp_directory_path() /
        ("stack-raw-editor-round-trip-validation-" +
         std::to_string(uniqueSuffix)));
    std::error_code filesystemError;
    std::filesystem::create_directories(
        testDirectory.Path(),
        filesystemError);
    if (!TransactionCheck(
            !filesystemError,
            "could not create the RAW/editor round-trip fixture directory")) {
        return false;
    }

    const std::filesystem::path sourcePath =
        testDirectory.Path() / "round-trip-source.dng";
    {
        std::ofstream sourceFile(sourcePath, std::ios::binary);
        constexpr unsigned char sourceBytes[] = {
            0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00
        };
        sourceFile.write(
            reinterpret_cast<const char*>(sourceBytes),
            sizeof(sourceBytes));
    }
    if (!TransactionCheck(
            std::filesystem::exists(sourcePath),
            "could not create the RAW/editor round-trip source fixture")) {
        return false;
    }

    EditorModule editor;
    RawWorkspace::WorkspaceState& workspace =
        const_cast<RawWorkspace::WorkspaceState&>(
            editor.GetRawWorkspaceStateForValidation());
    workspace.workspaceRoot = testDirectory.Path();

    RawWorkspace::SourceRecord source;
    source.absolutePath = sourcePath;
    source.relativePath = sourcePath.filename();
    source.relativePathKey = sourcePath.filename().generic_string();
    source.fileName = sourcePath.filename().string();
    source.stem = sourcePath.stem().string();
    source.extension = sourcePath.extension().string();
    source.fileSizeBytes = std::filesystem::file_size(sourcePath);
    source.modifiedTimeTicks = 737373;
    source.fingerprint = "round-trip-source";
    source.project.status = RawWorkspace::ProjectStatus::NoProject;
    workspace.sources.push_back(source);
    workspace.selectedSourceKey = source.relativePathKey;

    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.Clear();
    const int editorImageId = graph.AddImageNode(
        MakeSentinelImage("Editor round-trip sentinel", 91u),
        { -180.0f, 35.0f })->id;
    const int editorOutputId = graph.AddOutputNode(
        { 180.0f, 35.0f },
        true)->id;
    std::string graphError;
    if (!TransactionCheck(
            graph.TryConnectSockets(
                editorImageId,
                EditorNodeGraph::kImageOutputSocketId,
                editorOutputId,
                EditorNodeGraph::kImageInputSocketId,
                &graphError),
            "could not author the Editor round-trip graph: " + graphError)) {
        return false;
    }

    const std::string editorName = "Editor round-trip project";
    const std::string editorFile =
        (testDirectory.Path() / "editor-round-trip.stack").string();
    editor.SetCurrentProjectName(editorName);
    editor.SetCurrentProjectFileName(editorFile);
    editor.MarkDirty();
    const nlohmann::json editorGraph = editor.SerializePipeline();
    const EditorModule::ProjectFileCommandContext dirtyEditorCommands =
        editor.GetProjectFileCommandContext();
    if (!TransactionCheck(
            dirtyEditorCommands.sessionKind ==
                    EditorModule::ProjectSessionKind::EditorProject &&
                dirtyEditorCommands.dirty &&
                dirtyEditorCommands.canSave &&
                dirtyEditorCommands.canSaveAs &&
                dirtyEditorCommands.canClose &&
                dirtyEditorCommands.canOpen &&
                dirtyEditorCommands.canCreateEditorProject,
            "the File command context did not expose the dirty Editor project actions")) {
        return false;
    }

    EditorNodeGraph::Node* editorImage = graph.FindNode(editorImageId);
    editorImage->image.isEmbedding = true;
    if (!TransactionCheck(
            editor.EnterRawWorkspaceRootTab(),
            "could not present the locked RAW root while an Editor image import was pending") ||
        !TransactionCheck(
            editor.IsRawWorkspaceLockedByEditorProject(),
            "RAW controls were not locked while the Editor project remained active") ||
        !TransactionCheck(
            editor.SerializePipeline() == editorGraph &&
                editor.GetCurrentProjectName() == editorName &&
                editor.GetCurrentProjectFileName() == editorFile &&
                editor.IsDirty(),
            "locked RAW presentation changed the in-flight Editor project")) {
        return false;
    }
    editorImage->image.isEmbedding = false;

    if (!TransactionCheck(
            editor.LeaveRawWorkspaceRootTab(true),
            "could not return to the Editor root from the locked RAW presentation")) {
        return false;
    }
    editor.ClearDirty();
    if (!TransactionCheck(
            editor.EnterRawWorkspaceRootTab() &&
                editor.IsRawWorkspaceLockedByEditorProject(),
            "a clean Editor project did not keep the RAW surfaces locked") ||
        !TransactionCheck(
            editor.SerializePipeline() == editorGraph &&
                editor.GetCurrentProjectName() == editorName &&
                editor.GetCurrentProjectFileName() == editorFile &&
                !editor.IsDirty(),
            "locked RAW presentation changed the clean Editor project")) {
        return false;
    }

    if (!TransactionCheck(
            editor.CloseEditorProjectAndActivateRawWorkspace(),
            "could not close the Editor project and activate the RAW workspace") ||
        !TransactionCheck(
            !editor.IsRawWorkspaceLockedByEditorProject() &&
                editor.GetProjectSessionKind() ==
                    EditorModule::ProjectSessionKind::Empty &&
                workspace.selectedSourceKey == source.relativePathKey &&
                !editor.IsRawWorkspaceProjectLoadBusy(),
            "closing the Editor project did not leave an empty unlocked session with browsing selection preserved")) {
        return false;
    }
    const EditorModule::ProjectFileCommandContext emptyCommands =
        editor.GetProjectFileCommandContext();
    if (!TransactionCheck(
            emptyCommands.sessionKind ==
                    EditorModule::ProjectSessionKind::Empty &&
                !emptyCommands.dirty &&
                !emptyCommands.canSave &&
                !emptyCommands.canSaveAs &&
                !emptyCommands.canClose &&
                emptyCommands.canOpen &&
                emptyCommands.canCreateEditorProject,
            "the File command context did not disable persistence commands for an empty session")) {
        return false;
    }

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe(sourcePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes =
        static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.preToneExposureEv = 0.625f;
    if (!TransactionCheck(
            editor.EnsureRawWorkspaceProjectForSelectedRecipeEdit(recipe),
            "could not author the RAW round-trip graph")) {
        return false;
    }

    const nlohmann::json rawGraph = editor.SerializePipeline();
    const std::string rawName = editor.GetCurrentProjectName();
    const std::string rawFile = editor.GetCurrentProjectFileName();
    int rawOwnerNodeId = -1;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawDevelopment) {
            rawOwnerNodeId = node.id;
            break;
        }
    }
    if (!TransactionCheck(
            rawGraph != editorGraph,
            "the Editor and RAW round-trip fixtures were not distinct") ||
        !TransactionCheck(
            editor.IsRawWorkspaceProjectActive() && editor.IsDirty(),
            "the authored RAW graph did not own the active dirty pipeline") ||
        !TransactionCheck(
            rawOwnerNodeId > 0 &&
                !editor.RemoveGraphNode(rawOwnerNodeId) &&
                editor.SerializePipeline() == rawGraph,
            "the compact RAW Development owner node was not protected from deletion")) {
        return false;
    }
    const EditorModule::ProjectFileCommandContext dirtyRawCommands =
        editor.GetProjectFileCommandContext();
    if (!TransactionCheck(
            dirtyRawCommands.sessionKind ==
                    EditorModule::ProjectSessionKind::RawProject &&
                dirtyRawCommands.dirty &&
                dirtyRawCommands.canSave &&
                dirtyRawCommands.canSaveAs &&
                dirtyRawCommands.canClose,
            "the File command context did not expose the dirty RAW project actions")) {
        return false;
    }

    constexpr int kRoundTripCycles = 4;
    for (int cycle = 0; cycle < kRoundTripCycles; ++cycle) {
        if (!TransactionCheck(
                editor.LeaveRawWorkspaceRootTab(true),
                "could not leave the RAW root during round-trip cycle " +
                    std::to_string(cycle))) {
            return false;
        }

        const nlohmann::json editorSurfaceGraph = editor.SerializePipeline();
        const bool editorSurfaceGraphPreserved = editorSurfaceGraph == rawGraph;
        const bool editorSurfaceNamePreserved = editor.GetCurrentProjectName() == rawName;
        const bool editorSurfaceFilePreserved = editor.GetCurrentProjectFileName() == rawFile;
        const bool editorSurfaceDirtyPreserved = editor.IsDirty();
        const bool editorSurfaceRawOwnershipPreserved = editor.IsRawWorkspaceProjectActive();
        if (!TransactionCheck(
                editorSurfaceGraphPreserved &&
                    editorSurfaceNamePreserved &&
                    editorSurfaceFilePreserved &&
                    editorSurfaceDirtyPreserved &&
                    editorSurfaceRawOwnershipPreserved,
                "the Editor surface changed the active RAW project during cycle " +
                    std::to_string(cycle) +
                    " [graph=" + (editorSurfaceGraphPreserved ? "ok" : "changed") +
                    ", name=" + (editorSurfaceNamePreserved ? "ok" : "changed") +
                    ", file=" + (editorSurfaceFilePreserved ? "ok" : "changed") +
                    ", dirty=" + (editorSurfaceDirtyPreserved ? "yes" : "no") +
                    ", ownership=" + (editorSurfaceRawOwnershipPreserved ? "raw" : "editor") + "]")) {
            if (!editorSurfaceGraphPreserved) {
                std::cerr << "Expected RAW graph: " << rawGraph.dump() << '\n'
                          << "Editor-surface graph: " << editorSurfaceGraph.dump() << '\n';
            }
            return false;
        }

        if (!TransactionCheck(
                editor.EnterRawWorkspaceRootTab(),
                "could not re-enter the RAW root during round-trip cycle " +
                    std::to_string(cycle))) {
            return false;
        }

        const nlohmann::json rawSurfaceGraph = editor.SerializePipeline();
        const bool rawSurfaceGraphPreserved = rawSurfaceGraph == rawGraph;
        const bool rawSurfaceNamePreserved = editor.GetCurrentProjectName() == rawName;
        const bool rawSurfaceFilePreserved = editor.GetCurrentProjectFileName() == rawFile;
        const bool rawSurfaceDirtyPreserved = editor.IsDirty();
        const bool rawSurfaceOwnershipPreserved = editor.IsRawWorkspaceProjectActive();
        if (!TransactionCheck(
                rawSurfaceGraphPreserved &&
                    rawSurfaceNamePreserved &&
                    rawSurfaceFilePreserved &&
                    rawSurfaceDirtyPreserved &&
                    rawSurfaceOwnershipPreserved &&
                    !editor.IsRawWorkspaceLockedByEditorProject(),
                "the RAW surface changed the active RAW project during cycle " +
                    std::to_string(cycle) +
                    " [graph=" + (rawSurfaceGraphPreserved ? "ok" : "changed") +
                    ", name=" + (rawSurfaceNamePreserved ? "ok" : "changed") +
                    ", file=" + (rawSurfaceFilePreserved ? "ok" : "changed") +
                    ", dirty=" + (rawSurfaceDirtyPreserved ? "yes" : "no") +
                    ", ownership=" + (rawSurfaceOwnershipPreserved ? "raw" : "editor") + "]")) {
            if (!rawSurfaceGraphPreserved) {
                std::cerr << "Expected RAW graph: " << rawGraph.dump() << '\n'
                          << "RAW-surface graph: " << rawSurfaceGraph.dump() << '\n';
            }
            return false;
        }
    }

    if (!TransactionCheck(
            !editor.CloseActiveRawWorkspaceProject(false) &&
                editor.IsRawWorkspaceProjectActive() &&
                editor.IsDirty(),
            "a dirty RAW project closed without an explicit discard") ||
        !TransactionCheck(
            editor.CloseActiveRawWorkspaceProject(true) &&
                editor.GetProjectSessionKind() ==
                    EditorModule::ProjectSessionKind::Empty &&
                !editor.IsRawWorkspaceProjectActive() &&
                workspace.selectedSourceKey == source.relativePathKey &&
                !editor.IsRawWorkspaceProjectLoadBusy(),
            "discard-and-close did not return to an empty RAW browsing session")) {
        return false;
    }

    return true;
}

bool ValidateRawWorkspaceDurableFlushPreservesUnknownMetadata() {
    namespace RawRecipe = Stack::RawRecipe;
    namespace RawWorkspace = Stack::RawWorkspace;

    const auto uniqueSuffix = std::chrono::steady_clock::now()
        .time_since_epoch()
        .count();
    ScopedValidationDirectory testDirectory(
        std::filesystem::temp_directory_path() /
        ("stack-raw-autosave-validation-" +
         std::to_string(uniqueSuffix)));
    std::error_code filesystemError;
    std::filesystem::create_directories(
        testDirectory.Path(),
        filesystemError);
    if (!TransactionCheck(
            !filesystemError,
            "could not create the RAW autosave validation directory")) {
        return false;
    }

    const std::filesystem::path sourcePath =
        testDirectory.Path() / "durable-flush.DNG";
    {
        std::ofstream sourceFile(sourcePath, std::ios::binary);
        const unsigned char sourceBytes[] = {
            0x49, 0x49, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00
        };
        sourceFile.write(
            reinterpret_cast<const char*>(sourceBytes),
            sizeof(sourceBytes));
    }
    if (!TransactionCheck(
            std::filesystem::exists(sourcePath),
            "could not create the RAW autosave source fixture")) {
        return false;
    }

    EditorModule editor;
    RawWorkspace::WorkspaceState& workspace =
        const_cast<RawWorkspace::WorkspaceState&>(
            editor.GetRawWorkspaceStateForValidation());
    workspace.workspaceRoot = testDirectory.Path();

    RawWorkspace::SourceRecord source;
    source.absolutePath = sourcePath;
    source.relativePath = sourcePath.filename();
    source.relativePathKey = sourcePath.filename().generic_string();
    source.fileName = sourcePath.filename().string();
    source.stem = sourcePath.stem().string();
    source.extension = sourcePath.extension().string();
    source.fileSizeBytes = std::filesystem::file_size(sourcePath);
    source.modifiedTimeTicks = 424242;
    source.fingerprint = "durable-flush-source";
    source.project.status = RawWorkspace::ProjectStatus::NoProject;
    workspace.sources.push_back(source);
    workspace.selectedSourceKey = source.relativePathKey;

    RawRecipe::RawDevelopmentRecipe initialRecipe =
        RawRecipe::MakeDefaultRecipe(
            sourcePath.string(),
            source.fileName);
    initialRecipe.source.relativePathKey = source.relativePathKey;
    initialRecipe.source.fingerprint = source.fingerprint;
    initialRecipe.source.fileSizeBytes =
        static_cast<std::uint64_t>(source.fileSizeBytes);
    initialRecipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    if (!TransactionCheck(
            editor.EnsureRawWorkspaceProjectForSelectedRecipeEdit(
                initialRecipe),
            "could not create an active RAW project for durable flush")) {
        return false;
    }

    const std::filesystem::path projectPath =
        editor.GetCurrentProjectFileName();
    StackBinaryFormat::ProjectDocument seededDocument;
    seededDocument.metadata.projectKind =
        StackBinaryFormat::kRawProjectKind;
    seededDocument.metadata.projectName = source.stem;
    seededDocument.metadata.sourceWidth = 1;
    seededDocument.metadata.sourceHeight = 1;
    seededDocument.pipelineData = editor.SerializePipeline();
    if (!TransactionCheck(
            RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
                workspace.sources.front(),
                initialRecipe,
                seededDocument.pipelineData,
                seededDocument),
            "could not seed RAW project metadata for durable flush")) {
        return false;
    }
    seededDocument.rawWorkspaceData["futureRawWorkspaceKey"] = {
        { "schema", 73 },
        { "payload", {
            { "preserve", true },
            { "label", "unknown-autosave-sentinel" }
        } }
    };
    seededDocument.rawWorkspaceData["rawRecipe"]["futureRecipeKey"] = {
        { "preserve", true },
        { "generation", 19 }
    };
    seededDocument.rawWorkspaceData["rawSourceRef"]["futureSourceRefKey"] =
        "unknown-source-ref-sentinel";
    std::filesystem::create_directories(
        projectPath.parent_path(),
        filesystemError);
    if (!TransactionCheck(
            !filesystemError &&
                StackBinaryFormat::WriteProjectFile(
                    projectPath,
                    seededDocument),
            "could not write the seeded RAW project")) {
        return false;
    }

    RawRecipe::RawDevelopmentRecipe editedRecipe = initialRecipe;
    editedRecipe.preToneExposureEv = 1.375f;
    if (!TransactionCheck(
            editor.ApplyRawWorkspaceRecipeEditForSelectedSource(
                editedRecipe),
            "could not apply the RAW edit before durable flush") ||
        !TransactionCheck(
            editor.FlushActiveRawWorkspaceProjectIfDirty(),
            "durable RAW autosave flush failed")) {
        return false;
    }

    StackBinaryFormat::ProjectDocument reloadedDocument;
    const bool reloaded = StackBinaryFormat::ReadProjectFile(
        projectPath,
        reloadedDocument);
    if (!TransactionCheck(
            reloaded,
            "durable RAW autosave was not readable immediately after flush")) {
        return false;
    }

    if (!TransactionCheck(
            reloadedDocument.rawWorkspaceData.is_object() &&
                reloadedDocument.rawWorkspaceData.contains(
                    "futureRawWorkspaceKey") &&
                reloadedDocument.rawWorkspaceData[
                    "futureRawWorkspaceKey"].is_object(),
            "durable RAW autosave discarded unknown workspace metadata") ||
        !TransactionCheck(
            reloadedDocument.rawWorkspaceData.contains("rawRecipe") &&
                reloadedDocument.rawWorkspaceData["rawRecipe"].is_object(),
            "durable RAW autosave discarded its recipe") ||
        !TransactionCheck(
            reloadedDocument.rawWorkspaceData["rawRecipe"].contains(
                "futureRecipeKey") &&
                reloadedDocument.rawWorkspaceData["rawRecipe"]
                    ["futureRecipeKey"].value("preserve", false) &&
                reloadedDocument.rawWorkspaceData["rawRecipe"]
                    ["futureRecipeKey"].value("generation", 0) == 19,
            "durable RAW autosave discarded nested unknown recipe metadata") ||
        !TransactionCheck(
            reloadedDocument.rawWorkspaceData.contains("rawSourceRef") &&
                reloadedDocument.rawWorkspaceData["rawSourceRef"].value(
                    "futureSourceRefKey",
                    std::string()) == "unknown-source-ref-sentinel",
            "durable RAW autosave discarded nested unknown source metadata")) {
        return false;
    }
    const nlohmann::json& sentinel =
        reloadedDocument.rawWorkspaceData["futureRawWorkspaceKey"];
    const nlohmann::json sentinelPayload = sentinel.value(
        "payload",
        nlohmann::json::object());
    const RawRecipe::RawDevelopmentRecipe reloadedRecipe =
        RawRecipe::DeserializeRecipe(
            reloadedDocument.rawWorkspaceData["rawRecipe"]);
    const RawWorkspace::SourceRecord& persistedSource =
        workspace.sources.front();
    return TransactionCheck(
               reloadedDocument.metadata.projectKind ==
                   StackBinaryFormat::kRawProjectKind &&
               reloadedDocument.rawWorkspaceData.value(
                   "rawWorkspaceSchemaVersion",
                   0) == 2 &&
               !reloadedDocument.rawWorkspaceData.contains(
                   "downstreamGraph"),
               "durable RAW autosave did not use the canonical RAW v2 project contract") &&
        TransactionCheck(
               sentinel.value("schema", 0) == 73 &&
               sentinelPayload.value("preserve", false) &&
               sentinelPayload.value("label", std::string()) ==
                   "unknown-autosave-sentinel",
               "durable RAW autosave discarded unknown workspace metadata") &&
        TransactionCheck(
            std::abs(
                reloadedRecipe.preToneExposureEv -
                editedRecipe.preToneExposureEv) < 0.0001f,
            "durable RAW autosave did not persist the latest recipe edit") &&
        TransactionCheck(
            !editor.IsDirty() &&
                persistedSource.project.autosaved &&
                !persistedSource.project.dirty,
            "durable RAW autosave did not commit clean project state") &&
        TransactionCheck(
            !std::filesystem::exists(
                std::filesystem::path(projectPath.string() + ".tmp")),
            "durable RAW autosave left a temporary project file behind");
}

bool ValidateRawDevelopmentViewTransformConnectionContract() {
    EditorModule editor;
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.Clear();

    EditorNodeGraph::RawDevelopmentPayload rawPayload;
    rawPayload.recipe = RawRecipe::MakeDefaultRecipe(
        "view-transform-connection-contract.dng");
    rawPayload.recipe.viewTransform.layerJson["contrast"] = 1.23f;
    rawPayload.recipe.viewTransform.layerJson["saturation"] = 0.87f;
    rawPayload.recipe.viewTransform.layerJson["enabled"] = false;
    const int rawId = graph.AddRawDevelopmentNode(
        rawPayload,
        { 0.0f, 0.0f })->id;
    editor.AddLayerNodeAt(LayerType::Flip, { 260.0f, 0.0f });
    const int flipId = graph.GetSelectedNodeId();
    const int outputId = graph.AddOutputNode(
        { 520.0f, 0.0f },
        true)->id;
    std::string error;
    if (!TransactionCheck(
            editor.ConnectGraphSockets(
                rawId,
                EditorNodeGraph::kImageOutputSocketId,
                flipId,
                EditorNodeGraph::kImageInputSocketId,
                &error),
            "could not connect scene-linear RAW Development to Flip") ||
        !TransactionCheck(
            editor.ConnectGraphSockets(
                flipId,
                EditorNodeGraph::kImageOutputSocketId,
                outputId,
                EditorNodeGraph::kImageInputSocketId,
                &error),
            "could not auto-insert View Transform after Flip")) {
        return false;
    }

    const EditorNodeGraph::Node* viewNode =
        graph.FindNodeByLayerIndex(1);
    const nlohmann::json inheritedView =
        editor.GetLayers().size() > 1u && editor.GetLayers()[1]
            ? editor.GetLayers()[1]->Serialize()
            : nlohmann::json::object();
    bool ok = TransactionCheck(
                        editor.GetLayers().size() == 2u &&
                            viewNode &&
                            viewNode->layerType == LayerType::ViewTransform,
                        "scene-linear RAW output should create exactly one external View Transform") &&
        TransactionCheck(
            std::abs(inheritedView.value("contrast", 0.0f) - 1.23f) < 0.001f &&
                std::abs(inheritedView.value("saturation", 0.0f) - 0.87f) < 0.001f &&
                inheritedView.value("inputWorkingSpace", std::string()) ==
                    "linear-rec2020-d65" &&
                inheritedView.value("encodeSrgbOutput", false),
            "external View Transform should inherit the disabled RAW Lab transform without changing its look; got " +
                inheritedView.dump()) &&
        TransactionCheck(
            graph.FindInputLink(
                outputId,
                EditorNodeGraph::kImageInputSocketId) &&
                graph.FindInputLink(
                    outputId,
                    EditorNodeGraph::kImageInputSocketId)->fromNodeId ==
                    viewNode->id,
            "the inherited View Transform should be connected immediately before Output");

    EditorModule internalViewEditor;
    EditorNodeGraph::Graph& internalViewGraph =
        internalViewEditor.GetNodeGraph();
    internalViewGraph.Clear();
    EditorNodeGraph::RawDevelopmentPayload internalViewPayload;
    internalViewPayload.recipe = RawRecipe::MakeDefaultRecipe(
        "internal-view-transform-connection-contract.dng");
    internalViewPayload.recipe.viewTransform.layerJson["enabled"] = true;
    const int internalRawId = internalViewGraph.AddRawDevelopmentNode(
        internalViewPayload,
        { 0.0f, 0.0f })->id;
    internalViewEditor.AddLayerNodeAt(LayerType::Flip, { 260.0f, 0.0f });
    const int internalFlipId = internalViewGraph.GetSelectedNodeId();
    const int internalOutputId = internalViewGraph.AddOutputNode(
        { 520.0f, 0.0f },
        true)->id;
    error.clear();
    ok = TransactionCheck(
             internalViewEditor.ConnectGraphSockets(
                 internalRawId,
                 EditorNodeGraph::kImageOutputSocketId,
                 internalFlipId,
                 EditorNodeGraph::kImageInputSocketId,
                 &error),
             "could not connect internally transformed RAW Development to Flip") &&
        TransactionCheck(
             internalViewEditor.ConnectGraphSockets(
                 internalFlipId,
                 EditorNodeGraph::kImageOutputSocketId,
                 internalOutputId,
                 EditorNodeGraph::kImageInputSocketId,
                 &error),
             "could not connect internally transformed RAW chain to Output") &&
        TransactionCheck(
             internalViewEditor.GetLayers().size() == 1u &&
                 internalViewGraph.FindInputLink(
                     internalOutputId,
                     EditorNodeGraph::kImageInputSocketId) &&
                 internalViewGraph.FindInputLink(
                     internalOutputId,
                     EditorNodeGraph::kImageInputSocketId)->fromNodeId ==
                     internalFlipId,
             "enabled built-in RAW View Transform should not spawn a duplicate graph transform") &&
        ok;
    if (ok) {
        std::cout
            << "RAW Development view-transform connection validation passed: "
               "enabled built-in View stayed singular, while disabling it kept "
               "Flip scene-linear and inserted one inherited transform before Output.\n";
    }
    return ok;
}

} // namespace

bool ValidateEditorGraphTransactions() {
    const bool ok =
        ValidateRawDevelopmentViewTransformConnectionContract() &&
        ValidateImageAverageRollback() &&
        ValidateImageAverageSuccess() &&
        ValidateLayerSplitRollback() &&
        ValidateLayerSplitSuccess() &&
        ValidateRawWorkspaceEditorRoundTrips() &&
        ValidateRawWorkspaceDurableFlushPreservesUnknownMetadata();
    if (ok) {
        std::cout
            << "Editor graph transaction validation passed: "
               "channel expansions roll back exactly, one RAW project remains "
               "stable across RAW/Editor surfaces, and durable RAW autosave "
               "preserves forward-compatible metadata.\n";
    }
    return ok;
}

} // namespace Stack::Validation
