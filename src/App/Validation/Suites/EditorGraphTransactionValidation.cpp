#include "App/Validation/ValidationSuites.h"

#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Editor/Layers/LayerBase.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "NodeMath/SourceColorMetadata.h"
#include "Notifications/NotificationStore.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawWorkspace.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

float SavedExposure(const nlohmann::json& pipeline) {
    Stack::Project::RawLayerStackState layers;
    std::string error;
    if (!Stack::Project::ReadRawLayersFromPipeline(pipeline,layers,error,true)) {
        std::cerr << "Saved layer graph validation failed: " << error << '\n';
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto* exposure = Stack::Project::FindRawOperation(layers.background,RawRecipe::GraphOperationKind::Exposure);
    return exposure ? RawRecipe::ReadGraphOperation(exposure->rawOperation).preToneExposureEv : 0.f;
}

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
    ScopedValidationDirectory() = default;

    explicit ScopedValidationDirectory(std::filesystem::path path)
        : m_Path(std::move(path)) {}

    ~ScopedValidationDirectory() {
        if (m_Path.empty()) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(m_Path, error);
    }

    const std::filesystem::path& Path() const { return m_Path; }
    void SetPath(std::filesystem::path path) { m_Path = std::move(path); }

private:
    std::filesystem::path m_Path;
};

template <typename Predicate>
bool PumpMainThreadUntil(Predicate&& predicate) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        Async::TaskSystem::Get().PumpMainThreadTasks();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Async::TaskSystem::Get().PumpMainThreadTasks();
    return predicate();
}

// AppShell retains a closing editor until its project jobs and completions
// drain. Standalone validation editors must obey the same lifetime contract.
class TransactionEditor final : public EditorModule {
public:
    ~TransactionEditor() {
        RequestWorkerShutdownForAppClose();
        if (!PumpMainThreadUntil([this]() {
                return IsWorkerShutdownReadyForAppClose();
            })) {
            std::cerr << "Transaction editor jobs did not drain before shutdown.\n";
            std::terminate();
        }
    }
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
    TransactionEditor editor;
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
    TransactionEditor editor;
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
    TransactionEditor editor;
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
    TransactionEditor editor;
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
    ScopedValidationDirectory projectDirectory;
    TransactionEditor editor;
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

    // A document edit establishes project identity before a name, path, or
    // graph node necessarily exists. File commands must follow that identity
    // instead of briefly treating the edited document as no open project.
    {
        EditorModule identityOnlyEditor;
        identityOnlyEditor.MarkDirty();
        const EditorModule::ProjectFileCommandContext identityOnlyCommands =
            identityOnlyEditor.GetProjectFileCommandContext();
        if (!TransactionCheck(
                !identityOnlyEditor.GetProjectDocumentId().empty() &&
                    identityOnlyCommands.sessionKind ==
                        EditorModule::ProjectSessionKind::EditorProject &&
                    identityOnlyCommands.dirty &&
                    identityOnlyCommands.canSave &&
                    identityOnlyCommands.canSaveAs &&
                    identityOnlyCommands.canClose,
                "an edited unnamed document was incorrectly reported as having no open project")) {
            return false;
        }
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
    projectDirectory.SetPath(editor.GetCurrentProjectFileName());
    if (!TransactionCheck(
            editor.IsRawWorkspaceProjectActive() && !editor.IsDirty(),
            "the first RAW edit was not durably materialized as a clean project")) {
        return false;
    }

    RawRecipe::RawDevelopmentRecipe editedRecipe = recipe;
    editedRecipe.preToneExposureEv = 0.875f;
    if (!TransactionCheck(
            editor.ApplyRawWorkspaceRecipeEditForSelectedSource(editedRecipe),
            "could not author a newer dirty RAW edit for surface round trips")) {
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

    // Reproduce the real cross-view workflow: author downstream graph work,
    // move back through the RAW-owned recipe path, and then save the one
    // unified project. The managed manifest must retain both the RAW recipe
    // and the complete Editor graph; reopening must never fall back to only
    // RAW Development -> Output.
    editor.AddLayerNodeAt(LayerType::Flip, { 420.0f, 180.0f });
    const auto containsFlipLayer = [](const nlohmann::json& pipeline) {
        const nlohmann::json nodes = pipeline
            .value("nodeGraph", nlohmann::json::object())
            .value("nodes", nlohmann::json::array());
        return std::any_of(
            nodes.begin(),
            nodes.end(),
            [](const nlohmann::json& node) {
                return node.value("kind", std::string()) == "Layer";
            });
    };
    if (!TransactionCheck(
            containsFlipLayer(editor.SerializePipeline()) && editor.IsDirty(),
            "the authored RAW-project graph edit was not present in memory")) {
        return false;
    }

    RawRecipe::RawDevelopmentRecipe graphAndRawRecipe = editedRecipe;
    graphAndRawRecipe.preToneExposureEv = 1.125f;
    if (!TransactionCheck(
            editor.ApplyRawWorkspaceRecipeEditForSelectedSource(
                graphAndRawRecipe),
            "the RAW recipe edit following a graph edit could not be applied")) {
        return false;
    }

    bool unifiedSaveCompleted = false;
    bool unifiedSaveSucceeded = false;
    if (!TransactionCheck(
            editor.RequestSaveCurrentProject(
                rawName,
                [&](bool success) {
                    unifiedSaveCompleted = true;
                    unifiedSaveSucceeded = success;
                }),
            "the unified RAW/graph save could not be queued") ||
        !TransactionCheck(
            PumpMainThreadUntil([&]() { return unifiedSaveCompleted; }),
            "the unified RAW/graph save did not complete") ||
        !TransactionCheck(
            unifiedSaveSucceeded && !editor.IsDirty(),
            "the unified RAW/graph save did not become durable")) {
        return false;
    }

    const Stack::Project::ProjectStoreOpenResult reopened =
        Stack::Project::OpenProjectStore(
            std::filesystem::path(editor.GetCurrentProjectFileName()));
    if (!TransactionCheck(
            static_cast<bool>(reopened),
            "the saved unified RAW/graph bundle could not be reopened") ||
        !TransactionCheck(
            containsFlipLayer(reopened.snapshot.pipelineData),
            "the reopened RAW project discarded its authored graph")) {
        return false;
    }
    const RawRecipe::RawDevelopmentRecipe reopenedRecipe =
        RawRecipe::DeserializeRecipe(
            reopened.snapshot.rawWorkspaceData.value(
                "rawRecipe", nlohmann::json::object()));
    if (!TransactionCheck(
            std::abs(
                SavedExposure(reopened.snapshot.pipelineData) -
                graphAndRawRecipe.preToneExposureEv) < 0.0001f,
            "the reopened RAW project retained the graph but discarded its RAW edit")) {
        return false;
    }

    RawRecipe::RawDevelopmentRecipe dirtyCloseRecipe = graphAndRawRecipe;
    dirtyCloseRecipe.preToneExposureEv = 1.25f;
    if (!TransactionCheck(
            editor.ApplyRawWorkspaceRecipeEditForSelectedSource(
                dirtyCloseRecipe),
            "could not prepare the dirty-close RAW project fixture")) {
        return false;
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

bool ValidateRawWorkspaceDurableFlushUsesManagedAssets() {
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
            'r', 'a', 'w', '-', 'p', 'l', 'a', 'c', 'e', 'h', 'o', 'l', 'd', 'e', 'r'
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

    ScopedValidationDirectory projectDirectory;
    TransactionEditor editor;
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
    projectDirectory.SetPath(projectPath);
    Stack::Project::ProjectStoreOpenResult materialized =
        Stack::Project::OpenProjectStore(projectPath);
    std::vector<std::string> materializedErrors;
    if (!TransactionCheck(
            static_cast<bool>(materialized) &&
                materialized.snapshot.embeddedAssets.size() == 1u &&
                materialized.store->Verify(
                    materialized.snapshot,
                    &materializedErrors),
            materializedErrors.empty()
                ? "first RAW edit did not create one verifiable managed source asset"
                : materializedErrors.front())) {
        return false;
    }
    const Stack::Project::EmbeddedAssetRecord& managedAsset =
        materialized.snapshot.embeddedAssets.front();
    const std::filesystem::path managedSourcePath =
        projectPath / managedAsset.projectAssetPath;
    if (!TransactionCheck(
            managedSourcePath.lexically_normal().parent_path() ==
                    (projectPath / "assets").lexically_normal() &&
                std::filesystem::is_regular_file(managedSourcePath) &&
                std::filesystem::file_size(managedSourcePath) ==
                    source.fileSizeBytes,
            "the first RAW edit did not publish its exact managed source beside project.stack")) {
        return false;
    }
    materialized.store.reset();

    filesystemError.clear();
    std::filesystem::remove(sourcePath, filesystemError);
    if (!TransactionCheck(
            !filesystemError &&
                !std::filesystem::exists(sourcePath) &&
                std::filesystem::is_regular_file(managedSourcePath),
            "the managed project source did not survive removal of the external original fixture")) {
        return false;
    }

    RawRecipe::RawDevelopmentRecipe editedRecipe = initialRecipe;
    editedRecipe.preToneExposureEv = 1.375f;
    bool explicitSaveCompletionCalled = false;
    bool explicitSaveSucceeded = false;
    if (!TransactionCheck(
            editor.ApplyRawWorkspaceRecipeEditForSelectedSource(
                editedRecipe),
            "could not apply the RAW edit before explicit Ctrl+S") ||
        !TransactionCheck(
            editor.RequestSaveCurrentProject(
                source.stem,
                [&](bool success) {
                    explicitSaveCompletionCalled = true;
                    explicitSaveSucceeded = success;
                }),
            "explicit RAW Ctrl+S failed") ||
        !TransactionCheck(
            PumpMainThreadUntil([&]() {
                return explicitSaveCompletionCalled;
            }),
            "explicit RAW Ctrl+S did not finish its durable revision flush") ||
        !TransactionCheck(
            explicitSaveCompletionCalled && explicitSaveSucceeded &&
                !editor.IsDirty(),
            "explicit RAW Ctrl+S did not commit a clean durable revision")) {
        return false;
    }

    StackBinaryFormat::ProjectDocument explicitSaveDocument;
    if (!TransactionCheck(
            StackBinaryFormat::ReadProjectFile(
                projectPath,
                explicitSaveDocument),
            "explicit RAW Ctrl+S was not readable immediately from disk")) {
        return false;
    }
    const RawRecipe::RawDevelopmentRecipe explicitSaveRecipe =
        RawRecipe::DeserializeRecipe(
            explicitSaveDocument.rawWorkspaceData["rawRecipe"]);
    if (!TransactionCheck(
            std::abs(
                SavedExposure(explicitSaveDocument.pipelineData) -
                editedRecipe.preToneExposureEv) < 0.0001f &&
                std::filesystem::path(
                    explicitSaveRecipe.source.sourcePath).lexically_normal() ==
                    managedSourcePath.lexically_normal() &&
                std::filesystem::is_regular_file(
                    explicitSaveRecipe.source.sourcePath),
            "explicit RAW Ctrl+S did not persist the latest recipe edit")) {
        return false;
    }

    // Graph-tab mutations use MarkRenderDirty rather than the RAW recipe edit
    // helper. They must still advance the same document revision; otherwise
    // the save coordinator sees the already-persisted revision and treats
    // Ctrl+S/autosave as a successful no-op.
    const std::uint64_t revisionBeforeGraphEdit =
        editor.GetProjectEditRevision();
    editor.AddMixNodeAt({ 735.0f, 315.0f });
    const int authoredGraphNodeId =
        editor.GetNodeGraph().GetSelectedNodeId();
    const EditorNodeGraph::Node* authoredGraphNode =
        editor.GetNodeGraph().FindNode(authoredGraphNodeId);
    if (!TransactionCheck(
            authoredGraphNode != nullptr &&
                authoredGraphNode->kind == EditorNodeGraph::NodeKind::Mix &&
                editor.GetProjectEditRevision() > revisionBeforeGraphEdit &&
                editor.IsDirty(),
            "a Graph-tab node edit did not advance the unified RAW project revision")) {
        return false;
    }

    bool graphSaveCompletionCalled = false;
    bool graphSaveSucceeded = false;
    if (!TransactionCheck(
            editor.RequestSaveCurrentProject(
                source.stem,
                [&](bool success) {
                    graphSaveCompletionCalled = true;
                    graphSaveSucceeded = success;
                }),
            "Ctrl+S could not queue the single-RAW graph edit") ||
        !TransactionCheck(
            PumpMainThreadUntil([&]() {
                return graphSaveCompletionCalled;
            }),
            "Ctrl+S did not finish saving the single-RAW graph edit") ||
        !TransactionCheck(
            graphSaveSucceeded && !editor.IsDirty(),
            "the single-RAW graph revision did not become durable and clean")) {
        return false;
    }

    StackBinaryFormat::ProjectDocument graphSaveDocument;
    if (!TransactionCheck(
            StackBinaryFormat::ReadProjectFile(
                projectPath,
                graphSaveDocument),
            "the single-RAW project was unreadable after its graph save")) {
        return false;
    }
    EditorNodeGraph::Graph reloadedRawGraph;
    EditorNodeGraph::DeserializeGraphPayload(
        graphSaveDocument.pipelineData,
        reloadedRawGraph,
        0,
        {},
        0,
        0,
        0);
    const EditorNodeGraph::Node* reloadedAuthoredGraphNode =
        reloadedRawGraph.FindNode(authoredGraphNodeId);
    const RawRecipe::RawDevelopmentRecipe graphSaveRecipe =
        RawRecipe::DeserializeRecipe(
            graphSaveDocument.rawWorkspaceData["rawRecipe"]);
    if (!TransactionCheck(
            reloadedAuthoredGraphNode != nullptr &&
                reloadedAuthoredGraphNode->kind ==
                    EditorNodeGraph::NodeKind::Mix &&
                std::abs(reloadedAuthoredGraphNode->position.x - 735.0f) <
                    0.001f &&
                std::abs(reloadedAuthoredGraphNode->position.y - 315.0f) <
                    0.001f,
            "reopening the single-RAW project discarded its Graph-tab node") ||
        !TransactionCheck(
            std::abs(
                SavedExposure(graphSaveDocument.pipelineData) -
                editedRecipe.preToneExposureEv) < 0.0001f,
            "saving the Graph-tab edit replaced the current RAW recipe")) {
        return false;
    }

    editedRecipe.preToneExposureEv = 1.625f;
    if (!TransactionCheck(
            editor.ApplyRawWorkspaceRecipeEditForSelectedSource(
                editedRecipe),
            "could not apply the second RAW edit before durable flush") ||
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
            reloadedDocument.rawWorkspaceData.contains("rawRecipe") &&
                reloadedDocument.rawWorkspaceData["rawRecipe"].is_object(),
            "durable RAW autosave discarded its recipe")) {
        return false;
    }
    const RawRecipe::RawDevelopmentRecipe reloadedRecipe =
        RawRecipe::DeserializeRecipe(
            reloadedDocument.rawWorkspaceData["rawRecipe"]);
    // The original fixture was deliberately removed. A queued catalog scan
    // may remove its browser row while the open project keeps its managed RAW.
    const auto persistedSource = std::find_if(workspace.sources.begin(),workspace.sources.end(),
        [&](const auto& item) { return item.relativePathKey == source.relativePathKey; });
    const bool persistedStateValid = TransactionCheck(
               reloadedDocument.metadata.projectKind ==
                   StackBinaryFormat::kRawProjectKind &&
               reloadedDocument.rawWorkspaceData.value(
                   "schema",
                   std::string()) == "stack.rawWorkspace.project" &&
               reloadedDocument.rawWorkspaceData.value(
                   "schemaVersion",
                   0u) == Stack::Project::kRawWorkspaceProjectSchemaVersion &&
               !reloadedDocument.rawWorkspaceData.contains(
                   "rawWorkspaceSchemaVersion") &&
               reloadedDocument.rawWorkspaceData.value(
                   "rawProjectModel",
                   std::string()) ==
                   Stack::Project::kRawProjectModelSourceSets &&
               !reloadedDocument.rawWorkspaceData.contains(
                   "downstreamGraph"),
               "durable RAW autosave did not use the current unified RAW project contract") &&
        TransactionCheck(
            std::abs(
                SavedExposure(reloadedDocument.pipelineData) -
                editedRecipe.preToneExposureEv) < 0.0001f,
            "durable RAW autosave did not persist the latest recipe edit") &&
        TransactionCheck(
            !editor.IsDirty() &&
                editor.GetProjectFileCommandContext().lifecyclePhase == Stack::Project::ProjectLifecyclePhase::ReadyClean &&
                (persistedSource == workspace.sources.end() ||
                    (persistedSource->project.autosaved && !persistedSource->project.dirty)),
            "durable RAW autosave did not commit clean project state") &&
        TransactionCheck(
            !std::filesystem::exists(
                std::filesystem::path(projectPath.string() + ".tmp")),
            "durable RAW autosave left a temporary project file behind");
    return persistedStateValid;
}

bool ValidateAsyncProjectSaveKeepsNewerEditsDirty() {
    const auto uniqueSuffix = std::chrono::steady_clock::now()
        .time_since_epoch()
        .count();
    ScopedValidationDirectory testDirectory(
        std::filesystem::temp_directory_path() /
        ("stack-editor-save-revision-validation-" +
         std::to_string(uniqueSuffix)));
    std::error_code filesystemError;
    std::filesystem::create_directories(
        testDirectory.Path(),
        filesystemError);
    if (!TransactionCheck(
            !filesystemError,
            "could not create the Editor save-revision validation directory")) {
        return false;
    }

    TransactionEditor editor;
    editor.SetCurrentProjectName("Save Revision Validation");
    const auto notices = std::make_shared<Stack::Notifications::NotificationStore>();
    editor.SetNotificationScope(notices->ForOwner(1, "Save Revision Validation"));
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    EditorNodeGraph::Node* outputNode = graph.AddOutputNode(
        { 120.0f, 80.0f },
        true);
    if (!TransactionCheck(
            outputNode != nullptr,
            "could not create the Editor save-revision output node")) {
        return false;
    }
    const int outputNodeId = outputNode->id;
    graph.SetOutputNodeId(outputNodeId);
    editor.MarkDirty();

    const std::filesystem::path projectPath =
        testDirectory.Path() / "save-revision.stack";
    const std::filesystem::path projectRoot =
        testDirectory.Path() / "save-revision";
    bool firstCompletionCalled = false;
    bool firstCompletionCurrent = true;
    if (!TransactionCheck(
            editor.RequestSaveProjectAs(
                projectPath,
                [&](bool current) {
                    firstCompletionCalled = true;
                    firstCompletionCurrent = current;
                }),
            "could not queue the initial asynchronous project save")) {
        return false;
    }

    // This edit lands after the Save As snapshot has been captured but before
    // its completion callback is allowed to clear dirty state.
    // Snapshot preparation is allowed to rebuild graph storage, so reacquire
    // the node by stable ID instead of retaining a potentially stale pointer.
    outputNode = editor.GetNodeGraph().FindNode(outputNodeId);
    if (!TransactionCheck(
            outputNode != nullptr,
            "the output node disappeared while capturing the Save As snapshot")) {
        return false;
    }
    outputNode->position.x = 777.0f;
    editor.MarkDirty();
    const nlohmann::json editedPipeline = editor.SerializePipeline();
    float capturedEditedX = -1.0f;
    for (const nlohmann::json& node : editedPipeline
             .value("nodeGraph", nlohmann::json::object())
             .value("nodes", nlohmann::json::array())) {
        if (node.value("id", -1) == outputNodeId) {
            capturedEditedX = node.value("x", -1.0f);
            break;
        }
    }
    if (!TransactionCheck(
            std::abs(capturedEditedX - 777.0f) < 0.001f,
            "the newer graph edit was not present in the in-memory save snapshot")) {
        return false;
    }

    if (!TransactionCheck(
            PumpMainThreadUntil([&]() { return firstCompletionCalled; }),
            "the initial asynchronous project save did not complete") ||
        !TransactionCheck(
            !firstCompletionCurrent && editor.IsDirty(),
            "an older save snapshot incorrectly marked a newer edit clean") ||
        !TransactionCheck(
            std::filesystem::path(editor.GetCurrentProjectFileName()) ==
                projectRoot,
            "Save As did not adopt its written path while retaining newer dirty edits")) {
        return false;
    }

    bool currentSaveCompletionCalled = false;
    bool currentSaveSucceeded = false;
    if (!TransactionCheck(
            editor.RequestSaveCurrentProject(
                "Save Revision Validation",
                [&](bool success) {
                    currentSaveCompletionCalled = true;
                    currentSaveSucceeded = success;
                }),
            "could not queue Ctrl+S persistence of the newer edit") ||
        !TransactionCheck(
            PumpMainThreadUntil([&]() {
                return currentSaveCompletionCalled;
            }),
            "the current Ctrl+S project save did not complete") ||
        !TransactionCheck(
            currentSaveSucceeded && !editor.IsDirty(),
            "the current Ctrl+S snapshot did not become the clean revision")) {
        std::cerr << "[save-revision] success=" << currentSaveSucceeded
            << " dirty=" << editor.IsDirty() << '\n';
        for (const auto& notice : notices->Snapshot())
            std::cerr << "[save-revision] " << notice.content.message << '\n';
        return false;
    }

    StackBinaryFormat::ProjectDocument reloadedDocument;
    const Stack::Project::ProjectStoreOpenResult directOpen =
        Stack::Project::OpenProjectStore(projectRoot);
    if (directOpen.recoveredPreviousManifest) {
        std::cerr << "[save-revision] " << directOpen.message << "\n";
    }
    if (!TransactionCheck(
            StackBinaryFormat::ReadProjectFile(
                projectRoot,
                reloadedDocument),
            "the Ctrl+S project could not be reopened from disk")) {
        return false;
    }
    EditorNodeGraph::Graph reloadedGraph;
    EditorNodeGraph::DeserializeGraphPayload(
        reloadedDocument.pipelineData,
        reloadedGraph,
        0,
        {},
        0,
        0,
        0);
    const EditorNodeGraph::Node* reloadedOutput =
        reloadedGraph.FindNode(outputNodeId);
    if (!reloadedOutput ||
        std::abs(reloadedOutput->position.x - 777.0f) >= 0.001f) {
        const auto readStoredX = [&](const std::filesystem::path& manifestPath) {
            float value = -1.0f;
            try {
                std::ifstream stream(manifestPath);
                const nlohmann::json manifest = nlohmann::json::parse(stream);
                for (const nlohmann::json& node : manifest
                         .value("pipelineData", nlohmann::json::object())
                         .value("nodeGraph", nlohmann::json::object())
                         .value("nodes", nlohmann::json::array())) {
                    if (node.value("id", -1) == outputNodeId) {
                        value = node.value("x", -1.0f);
                        break;
                    }
                }
            } catch (...) {
            }
            return value;
        };
        std::cerr
            << "[save-revision] current manifest x="
            << readStoredX(projectRoot / "project.stack")
            << ", previous manifest x="
            << readStoredX(projectRoot / "project.stack.previous")
            << ", reopened x="
            << (reloadedOutput ? reloadedOutput->position.x : -1.0f)
            << "\n";
    }
    if (!TransactionCheck(
            reloadedOutput != nullptr &&
                std::abs(reloadedOutput->position.x - 777.0f) < 0.001f,
            "reopening after Ctrl+S did not restore the newest graph edit")) {
        return false;
    }

    // Missing manifests and external edits must not enter missing-store
    // recovery. Preserve the disk contents so Save As remains possible.
    const auto manifestPath = Stack::Project::WorkingProjectDocumentPath(projectRoot);
    const auto heldManifestPath = projectRoot / "manifest-held-for-validation";
    const auto expectSaveRejected = [&]() {
        bool completed = false;
        bool succeeded = true;
        return TransactionCheck(
            editor.RequestSaveCurrentProject(
                "Save Revision Validation",
                [&](bool success) { completed = true; succeeded = success; }),
            "could not queue the protected clean-project save check") &&
            TransactionCheck(PumpMainThreadUntil([&]() { return completed; }),
                "the protected clean-project save check did not complete") &&
            TransactionCheck(!succeeded && !editor.IsDirty(),
                "clean Ctrl+S replaced an existing damaged or changed project");
    };
    std::filesystem::rename(manifestPath, heldManifestPath, filesystemError);
    if (!TransactionCheck(!filesystemError,
            "could not hold the manifest for damaged-store validation") ||
        !expectSaveRejected() ||
        !TransactionCheck(!std::filesystem::exists(manifestPath) &&
                std::filesystem::exists(heldManifestPath),
            "clean Ctrl+S recreated a manifest inside a damaged existing store")) {
        return false;
    }
    std::filesystem::rename(heldManifestPath, manifestPath, filesystemError);
    if (!TransactionCheck(!filesystemError,
            "could not restore the held project manifest")) return false;

    const auto readManifest = [&]() {
        std::ifstream input(manifestPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    };
    const std::string originalManifest = readManifest();
    const auto writeManifest = [&](const std::string& text) {
        std::ofstream output(manifestPath, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        return static_cast<bool>(output);
    };
    for (const std::string& externalManifest :
            {originalManifest + "\n", std::string("invalid manifest")}) {
        if (!TransactionCheck(writeManifest(externalManifest),
                "could not modify the manifest for save-conflict validation") ||
            !expectSaveRejected() ||
            !TransactionCheck(readManifest() == externalManifest,
                "clean Ctrl+S modified an externally changed manifest")) {
            return false;
        }
    }
    if (!TransactionCheck(writeManifest(originalManifest),
            "could not restore the original project manifest")) return false;
    editor.SetCurrentProjectFileName(projectRoot.string());

    // Explicit Save is also a durability request for a clean project. If its
    // store disappears outside Stack, Ctrl+S must recreate it instead of
    // accepting the unchanged edit revision as proof that the file exists.
    filesystemError.clear();
    std::filesystem::remove_all(projectRoot, filesystemError);
    if (!TransactionCheck(
            !filesystemError && !std::filesystem::exists(projectRoot),
            "could not remove the clean project store for explicit-save recovery validation")) {
        return false;
    }
    bool recoverySaveCompleted = false;
    bool recoverySaveSucceeded = false;
    if (!TransactionCheck(
            editor.RequestSaveCurrentProject(
                "Save Revision Validation",
                [&](bool success) {
                    recoverySaveCompleted = true;
                    recoverySaveSucceeded = success;
                }),
            "clean Ctrl+S could not queue recreation of its missing project store") ||
        !TransactionCheck(
            PumpMainThreadUntil([&]() { return recoverySaveCompleted; }),
            "clean Ctrl+S did not finish recreating its missing project store") ||
        !TransactionCheck(
            recoverySaveSucceeded &&
                static_cast<bool>(Stack::Project::OpenProjectStore(projectRoot)),
            "clean Ctrl+S reported success without recreating a readable project store")) {
        for (const auto& notice : notices->Snapshot())
            std::cerr << "[save-recovery] " << notice.content.message << '\n';
        return false;
    }
    return true;
}

bool ValidateAtomicExactImageExport() {
    const auto uniqueSuffix = std::chrono::steady_clock::now()
        .time_since_epoch()
        .count();
    ScopedValidationDirectory testDirectory(
        std::filesystem::temp_directory_path() /
        ("stack-editor-export-validation-" +
         std::to_string(uniqueSuffix)));
    std::error_code filesystemError;
    std::filesystem::create_directories(
        testDirectory.Path(), filesystemError);
    if (!TransactionCheck(
            !filesystemError,
            "could not create the atomic export validation directory")) {
        return false;
    }

    TransactionEditor editor;
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.Clear();
    EditorNodeGraph::Node* image = graph.AddImageNode(
        MakeSentinelImage("Atomic Export", 96u),
        { 0.0f, 0.0f });
    const int imageId = image ? image->id : -1;
    EditorNodeGraph::Node* output = graph.AddOutputNode(
        { 320.0f, 0.0f }, true);
    const int outputId = output ? output->id : -1;
    if (!TransactionCheck(
            image && output &&
                graph.TryConnectSockets(
                    imageId,
                    EditorNodeGraph::kImageOutputSocketId,
                    outputId,
                    EditorNodeGraph::kImageInputSocketId),
            "could not author the atomic export validation graph")) {
        return false;
    }
    graph.SetOutputNodeId(outputId);

    const std::filesystem::path destination =
        testDirectory.Path() / "requested-export.png";
    if (!TransactionCheck(
            editor.RequestExportImage(destination.string()),
            "the exact-path PNG export could not be queued") ||
        !TransactionCheck(
            PumpMainThreadUntil([&]() {
                std::error_code sizeError;
                return !editor.IsExportBusy() &&
                    std::filesystem::is_regular_file(
                           destination, sizeError) &&
                    !sizeError &&
                    std::filesystem::file_size(
                        destination, sizeError) > 8u &&
                    !sizeError;
            }),
            "the exact-path PNG export was not atomically published")) {
        return false;
    }
    Async::TaskSystem::Get().PumpMainThreadTasks();

    std::array<unsigned char, 8> signature {};
    std::ifstream input(destination, std::ios::binary);
    input.read(
        reinterpret_cast<char*>(signature.data()),
        static_cast<std::streamsize>(signature.size()));
    const std::array<unsigned char, 8> pngSignature {
        0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au
    };
    return TransactionCheck(
        input.gcount() == static_cast<std::streamsize>(signature.size()) &&
            signature == pngSignature &&
            !std::filesystem::exists(
                std::filesystem::path(destination.string() +
                    ".stack-exporting")) &&
            !std::filesystem::exists(
                testDirectory.Path() / "fallback_export.png"),
        "PNG export did not produce one verified requested file without fallback artifacts");
}

bool ValidateRawDevelopmentViewTransformConnectionContract() {
    TransactionEditor editor;
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
            "could not connect Flip to Output")) {
        return false;
    }

    bool ok = TransactionCheck(editor.GetLayers().size() == 1u &&
        graph.FindInputLink(outputId, "imageIn") && graph.FindInputLink(outputId, "imageIn")->fromNodeId == flipId,
        "A connection silently inserted a presentation transform");
    editor.AddLayerNodeAt(LayerType::ViewTransform, {390.f,100.f});
    const int viewId = graph.GetSelectedNodeId();
    ok = TransactionCheck(editor.ConnectGraphSockets(flipId,"imageOut",viewId,"imageIn",&error) &&
        editor.ConnectGraphSockets(viewId,"imageOut",outputId,"imageIn",&error),
        "An explicitly authored View Transform could not be connected") && ok;

    TransactionEditor internalViewEditor;
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
               "connections preserve authored presentation placement and explicit View nodes remain available.\n";
    }
    return ok;
}

} // namespace

bool ValidateEditorGraphTransactions() {
    const auto run = [](const char* name, bool (*check)()) {
        std::cerr << "[transactions] " << name << std::endl;
        return check();
    };
    const bool ok =
        run("View connections",ValidateRawDevelopmentViewTransformConnectionContract) &&
        run("Average rollback",ValidateImageAverageRollback) &&
        run("Average expansion",ValidateImageAverageSuccess) &&
        run("Layer split rollback",ValidateLayerSplitRollback) &&
        run("Layer split",ValidateLayerSplitSuccess) &&
        run("Save revisions and recovery",ValidateAsyncProjectSaveKeepsNewerEditsDirty) &&
        run("Exact image export",ValidateAtomicExactImageExport) &&
        run("RAW/Graph round trips",ValidateRawWorkspaceEditorRoundTrips) &&
        run("Managed RAW source durability",ValidateRawWorkspaceDurableFlushUsesManagedAssets);
    if (ok) {
        std::cout
            << "Editor graph transaction validation passed: "
               "channel expansions roll back exactly, asynchronous saves cannot "
               "clear newer edits, one RAW project remains stable across "
               "RAW/Editor surfaces, and explicit plus durable RAW saves remain "
               "clean and independent of the external original.\n";
    }
    return ok;
}

} // namespace Stack::Validation
