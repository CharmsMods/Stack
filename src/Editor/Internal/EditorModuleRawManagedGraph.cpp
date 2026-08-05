#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

namespace {

std::string ManagedRawSectionTitle(const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const std::string displayName = Stack::RawRecipe::RecipeDisplayName(recipe);
    return "RAW Development: " + (displayName.empty() ? std::string("source") : displayName);
}

std::string ManagedRawProjectLocalId(const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    if (!recipe.source.relativePathKey.empty()) {
        return recipe.source.relativePathKey;
    }
    if (!recipe.source.fingerprint.empty()) {
        return recipe.source.fingerprint;
    }
    return recipe.source.sourcePath;
}

std::string ManagedRawSectionId(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    int rawSourceNodeId,
    int rawDecodeNodeId) {
    std::ostringstream out;
    out << "managed-raw:";
    const std::string key = ManagedRawProjectLocalId(recipe);
    out << (key.empty() ? std::string("source") : key);
    out << ":" << rawSourceNodeId << "-" << rawDecodeNodeId;
    return out.str();
}

bool ApplyRawRecipeLayerStateToGraphNode(
    const EditorNodeGraph::Graph& graph,
    std::vector<std::shared_ptr<LayerBase>>& layers,
    int nodeId,
    const nlohmann::json& layerJson) {
    const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
    if (!node ||
        node->kind != EditorNodeGraph::NodeKind::Layer ||
        node->layerIndex < 0 ||
        node->layerIndex >= static_cast<int>(layers.size()) ||
        !layers[static_cast<std::size_t>(node->layerIndex)] ||
        !layerJson.is_object()) {
        return false;
    }
    layers[static_cast<std::size_t>(node->layerIndex)]->Deserialize(layerJson);
    return true;
}

bool CopyManagedLayerStateToRecipe(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers,
    const Stack::RawWorkspace::ManagedRawSection& section,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const auto serializeLayer = [&](int nodeId, nlohmann::json& outJson) {
        const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
        if (!node ||
            node->kind != EditorNodeGraph::NodeKind::Layer ||
            node->layerIndex < 0 ||
            node->layerIndex >= static_cast<int>(layers.size()) ||
            !layers[static_cast<std::size_t>(node->layerIndex)]) {
            return false;
        }
        outJson = layers[static_cast<std::size_t>(node->layerIndex)]->Serialize();
        return true;
    };

    return serializeLayer(section.toneCurveNodeId, recipe.finishTone.layerJson) &&
        serializeLayer(section.viewTransformNodeId, recipe.viewTransform.layerJson);
}

} // namespace

bool EditorModule::MigrateLoadedManagedRawProject(
    LoadedProjectData& projectData,
    std::string* outError) {
    StackBinaryFormat::ProjectDocument metadataDocument;
    metadataDocument.rawWorkspaceData = projectData.rawWorkspaceData;
    Stack::RawWorkspace::ProjectInfo projectInfo;
    Stack::RawRecipe::RawDevelopmentRecipe baseRecipe;
    if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(
            metadataDocument,
            projectInfo,
            &baseRecipe) ||
        projectInfo.mode != Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        return true;
    }

    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::DeserializeManagedRawSection(
            projectData.rawWorkspaceData.value(
                "managedRawSection",
                nlohmann::json::object()));
    const Stack::RawWorkspace::ManagedRawValidationResult validation =
        Stack::RawWorkspace::ValidateManagedRawSection(
            m_NodeGraph,
            section,
            baseRecipe);
    if (!validation.valid) {
        if (outError) {
            *outError = validation.message.empty()
                ? "The legacy managed RAW graph cannot be converted exactly. The original project was not changed."
                : validation.message + " The original project was not changed.";
        }
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe compactRecipe = validation.recipe;
    if (!CopyManagedLayerStateToRecipe(
            m_NodeGraph,
            m_Layers,
            section,
            compactRecipe)) {
        if (outError) {
            *outError = "The legacy RAW tone/view layers cannot be converted exactly. The original project was not changed.";
        }
        return false;
    }

    const EditorNodeGraph::Node* sourceNode =
        m_NodeGraph.FindNode(section.rawSourceNodeId);
    const EditorNodeGraph::Node* toneNode =
        m_NodeGraph.FindNode(section.toneCurveNodeId);
    const EditorNodeGraph::Node* viewNode =
        m_NodeGraph.FindNode(section.viewTransformNodeId);
    if (!sourceNode || !toneNode || !viewNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        viewNode->kind != EditorNodeGraph::NodeKind::Layer) {
        if (outError) {
            *outError = "The legacy managed RAW section is incomplete. The original project was not changed.";
        }
        return false;
    }

    EditorNodeGraph::Graph compactGraph = m_NodeGraph;
    std::vector<std::shared_ptr<LayerBase>> compactLayers = m_Layers;
    std::vector<EditorNodeGraph::Link> downstreamLinks;
    for (const EditorNodeGraph::Link& link : compactGraph.GetLinks()) {
        if (link.fromNodeId == section.viewTransformNodeId) {
            downstreamLinks.push_back(link);
        }
    }

    std::vector<int> removedLayerIndices{
        toneNode->layerIndex,
        viewNode->layerIndex
    };
    std::sort(removedLayerIndices.begin(), removedLayerIndices.end(), std::greater<int>());
    removedLayerIndices.erase(
        std::unique(removedLayerIndices.begin(), removedLayerIndices.end()),
        removedLayerIndices.end());
    for (int layerIndex : removedLayerIndices) {
        if (layerIndex < 0 ||
            layerIndex >= static_cast<int>(compactLayers.size())) {
            if (outError) {
                *outError = "The legacy managed RAW layer mapping is invalid. The original project was not changed.";
            }
            return false;
        }
        compactLayers.erase(compactLayers.begin() + layerIndex);
        compactGraph.RemoveLayerNode(layerIndex);
    }
    compactGraph.RemoveNode(section.rawDecodeNodeId);
    compactGraph.RemoveNode(section.rawSourceNodeId);
    if (section.groupId > 0) {
        compactGraph.RemoveGroup(section.groupId);
    }

    EditorNodeGraph::RawDevelopmentPayload compactPayload;
    compactPayload.recipe = compactRecipe;
    compactPayload.projectStatus = "Edited";
    compactPayload.edited = true;
    compactPayload.autosaved = true;
    EditorNodeGraph::Node* compactNode = compactGraph.AddRawDevelopmentNode(
        std::move(compactPayload),
        sourceNode->position);
    if (!compactNode) {
        if (outError) {
            *outError = "The compact RAW Development node could not be created. The original project was not changed.";
        }
        return false;
    }
    const int compactNodeId = compactNode->id;
    for (const EditorNodeGraph::Link& link : downstreamLinks) {
        std::string connectionError;
        if (!compactGraph.TryConnectSockets(
                compactNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                link.toNodeId,
                link.toSocketId,
                &connectionError)) {
            if (outError) {
                *outError = connectionError.empty()
                    ? "The compact RAW node could not be reconnected without changing the graph. The original project was not changed."
                    : connectionError + " The original project was not changed.";
            }
            return false;
        }
    }
    compactGraph.SelectNode(compactNodeId, false);

    nlohmann::json layerArray = nlohmann::json::array();
    for (const std::shared_ptr<LayerBase>& layer : compactLayers) {
        if (!layer) {
            if (outError) {
                *outError = "The legacy project contains a missing layer. The original project was not changed.";
            }
            return false;
        }
        layerArray.push_back(layer->Serialize());
    }
    nlohmann::json compactPipeline =
        EditorNodeGraph::SerializeGraphPayload(layerArray, compactGraph);
    if (projectData.pipelineData.is_object()) {
        for (const char* key : { "editorComposite", "editorTimeline" }) {
            if (projectData.pipelineData.contains(key)) {
                compactPipeline[key] = projectData.pipelineData[key];
            }
        }
    }

    const std::filesystem::path originalPath =
        std::filesystem::path(projectData.projectFileName).lexically_normal();
    const std::filesystem::path migrationDirectory =
        originalPath.parent_path() / "Stack Migrated Projects";
    std::error_code filesystemError;
    std::filesystem::create_directories(migrationDirectory, filesystemError);
    if (filesystemError) {
        if (outError) {
            *outError = "Stack could not create the sibling migration folder. The original project was not changed.";
        }
        return false;
    }
    const std::string baseStem = originalPath.stem().string() + " (Compact)";
    std::filesystem::path migratedPath = migrationDirectory / (baseStem + ".stack");
    for (int suffix = 2; std::filesystem::exists(migratedPath) && suffix < 10000; ++suffix) {
        migratedPath = migrationDirectory /
            (baseStem + " " + std::to_string(suffix) + ".stack");
    }

    StackBinaryFormat::ProjectDocument migratedDocument;
    if (!StackBinaryFormat::ReadProjectFile(originalPath, migratedDocument)) {
        if (outError) {
            *outError = "The original legacy RAW project could not be reread for migration. It was not changed.";
        }
        return false;
    }
    migratedDocument.metadata.projectKind = StackBinaryFormat::kRawProjectKind;
    migratedDocument.pipelineData = compactPipeline;
    migratedDocument.nodeBrowserThumbnailEntries.clear();
    migratedDocument.rawWorkspaceData = projectData.rawWorkspaceData;
    migratedDocument.rawWorkspaceData["schema"] = "stack.rawWorkspace.project";
    migratedDocument.rawWorkspaceData["rawWorkspaceSchemaVersion"] = 2;
    migratedDocument.rawWorkspaceData["rawWorkspaceMode"] = "recipe-backed";
    migratedDocument.rawWorkspaceData["rawRecipe"] =
        Stack::RawRecipe::SerializeRecipe(compactRecipe);
    migratedDocument.rawWorkspaceData["managedRawSection"] = nullptr;
    migratedDocument.rawWorkspaceData["customRawSection"] = nullptr;
    migratedDocument.rawWorkspaceData["readOnlyReason"] = nullptr;
    migratedDocument.rawWorkspaceData.erase("downstreamGraph");

    const std::filesystem::path temporaryPath = migratedPath.string() + ".tmp";
    if (!StackBinaryFormat::WriteProjectFile(temporaryPath, migratedDocument)) {
        if (outError) {
            *outError = "The compact migrated copy could not be written. The original project was not changed.";
        }
        return false;
    }
    std::filesystem::rename(temporaryPath, migratedPath, filesystemError);
    if (filesystemError) {
        std::filesystem::remove(temporaryPath, filesystemError);
        if (outError) {
            *outError = "The compact migrated copy could not be finalized. The original project was not changed.";
        }
        return false;
    }

    m_NodeGraph = std::move(compactGraph);
    m_Layers = std::move(compactLayers);
    ResetNodeBrowserThumbnailState();
    RefreshGraphLayerMetadata();
    ApplyGraphLayerOrder();
    projectData.pipelineData = std::move(compactPipeline);
    projectData.rawWorkspaceData = std::move(migratedDocument.rawWorkspaceData);
    projectData.projectKind = StackBinaryFormat::kRawProjectKind;
    projectData.projectFileName = migratedPath.string();
    projectData.nodeBrowserThumbnailEntries.clear();
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "Legacy RAW project migrated to a compact copy. The original project was left unchanged.",
        "raw-workspace-managed-migrated");
    return true;
}

bool EditorModule::ApplyActiveRawWorkspaceModeDataToDocument(StackBinaryFormat::ProjectDocument& document) const {
    if (!document.rawWorkspaceData.is_object()) {
        return false;
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        document.rawWorkspaceData["managedRawSection"] =
            Stack::RawWorkspace::SerializeManagedRawSection(m_ActiveManagedRawSection);
        document.rawWorkspaceData["customRawSection"] = nullptr;
        document.rawWorkspaceData["readOnlyReason"] = nullptr;
        return true;
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::CustomGraph) {
        if (m_ActiveManagedRawSection.rawSourceNodeId > 0) {
            document.rawWorkspaceData["managedRawSection"] =
                Stack::RawWorkspace::SerializeManagedRawSection(m_ActiveManagedRawSection);
        }
        document.rawWorkspaceData["customRawSection"] = {
            { "schema", "stack.rawWorkspace.customRawSection" },
            { "schemaVersion", 1 },
            { "modeState", "custom-graph" },
            { "previousManagedSectionId", m_ActiveManagedRawSection.sectionId },
            { "reason", Stack::RawWorkspace::kCustomGraphReadOnlyReason }
        };
        document.rawWorkspaceData["readOnlyReason"] = Stack::RawWorkspace::kCustomGraphReadOnlyReason;
        return true;
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::RecipeBacked) {
        document.rawWorkspaceData["managedRawSection"] = nullptr;
        document.rawWorkspaceData["customRawSection"] = nullptr;
        document.rawWorkspaceData["readOnlyReason"] = nullptr;
        return true;
    }

    return true;
}

void EditorModule::MarkActiveRawWorkspaceProjectAsCustomGraph(std::string reason) {
    if (reason.empty()) {
        reason = Stack::RawWorkspace::kCustomGraphReadOnlyReason;
    }
    m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::CustomGraph;
    if (Stack::RawWorkspace::SourceRecord* source = FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey)) {
        source->project.mode = Stack::RawWorkspace::RawProjectMode::CustomGraph;
        source->project.readOnlyReason = reason;
    }
    m_RawWorkspaceRecipePreviewCache.erase(m_ActiveRawWorkspaceSourceKey);
    MarkDirty();
}

bool EditorModule::ValidateActiveRawWorkspaceManagedGraph(bool transitionOnFailure) {
    if (!IsRawWorkspaceProjectActive() ||
        m_ActiveRawWorkspaceMode != Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        return true;
    }

    const Stack::RawWorkspace::ManagedRawValidationResult validation =
        Stack::RawWorkspace::ValidateManagedRawSection(
            m_NodeGraph,
            m_ActiveManagedRawSection,
            m_ActiveRawWorkspaceRecipe);
    if (validation.valid) {
        Stack::RawRecipe::RawDevelopmentRecipe recipeFromGraph = validation.recipe;
        if (!CopyManagedLayerStateToRecipe(
                m_NodeGraph,
                m_Layers,
                m_ActiveManagedRawSection,
                recipeFromGraph)) {
            if (transitionOnFailure) {
                MarkActiveRawWorkspaceProjectAsCustomGraph(Stack::RawWorkspace::kCustomGraphReadOnlyReason);
                QueueUiNotification(
                    UiNotificationSeverity::Info,
                    "The managed RAW tone/view layers cannot round-trip through the RAW recipe. RAW tab editing is now read-only for this image.",
                    "raw-workspace-managed-layer-validation");
            }
            return false;
        }
        if (Stack::RawRecipe::SerializeRecipe(recipeFromGraph) !=
            Stack::RawRecipe::SerializeRecipe(m_ActiveRawWorkspaceRecipe)) {
            m_ActiveRawWorkspaceRecipe = std::move(recipeFromGraph);
            MarkDirty();
        }
        return true;
    }

    if (transitionOnFailure) {
        MarkActiveRawWorkspaceProjectAsCustomGraph(Stack::RawWorkspace::kCustomGraphReadOnlyReason);
        QueueUiNotification(
            UiNotificationSeverity::Info,
            validation.message.empty()
                ? Stack::RawWorkspace::kCustomGraphReadOnlyReason
                : validation.message + " RAW tab editing is now read-only for this image.",
            "raw-workspace-managed-validation");
    }
    return false;
}

bool EditorModule::ApplyActiveRawWorkspaceRecipeToManagedGraph() {
    if (!IsRawWorkspaceProjectActive() ||
        m_ActiveRawWorkspaceMode != Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        return true;
    }

    std::string reason;
    if (!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(m_ActiveRawWorkspaceRecipe, &reason)) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            reason.empty() ? "This RAW edit cannot be represented by the managed graph." : reason,
            "raw-workspace-managed-recipe-blocked");
        return false;
    }

    EditorNodeGraph::Node* decodeNode = m_NodeGraph.FindNode(m_ActiveManagedRawSection.rawDecodeNodeId);
    if (!decodeNode || decodeNode->kind != EditorNodeGraph::NodeKind::RawDecode) {
        ValidateActiveRawWorkspaceManagedGraph(true);
        return false;
    }

    decodeNode->rawDecode.settings = Stack::RawRecipe::ToRawDevelopSettings(m_ActiveRawWorkspaceRecipe);
    if (!ApplyRawRecipeLayerStateToGraphNode(
            m_NodeGraph,
            m_Layers,
            m_ActiveManagedRawSection.toneCurveNodeId,
            m_ActiveRawWorkspaceRecipe.finishTone.layerJson) ||
        !ApplyRawRecipeLayerStateToGraphNode(
            m_NodeGraph,
            m_Layers,
            m_ActiveManagedRawSection.viewTransformNodeId,
            m_ActiveRawWorkspaceRecipe.viewTransform.layerJson)) {
        ValidateActiveRawWorkspaceManagedGraph(true);
        return false;
    }
    MarkRenderDirty(decodeNode->id);
    MarkRenderDirty(m_ActiveManagedRawSection.toneCurveNodeId);
    MarkRenderDirty(m_ActiveManagedRawSection.viewTransformNodeId);
    MarkDirty();
    return ValidateActiveRawWorkspaceManagedGraph(true);
}

bool EditorModule::DecomposeActiveRawWorkspaceProjectToManagedGraph() {
    if (!IsRawWorkspaceProjectActive()) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Open or edit this RAW project before decomposing it.",
            "raw-workspace-decompose-no-active-project");
        return false;
    }
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::CustomGraph) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            Stack::RawWorkspace::kCustomGraphReadOnlyReason,
            "raw-workspace-decompose-custom");
        return false;
    }
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        return ValidateActiveRawWorkspaceManagedGraph(true);
    }

    std::string reason;
    if (!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(m_ActiveRawWorkspaceRecipe, &reason)) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            reason.empty() ? "This RAW recipe cannot be decomposed without losing edits." : reason,
            "raw-workspace-decompose-blocked");
        return false;
    }

    EditorNodeGraph::Node* compactNode = nullptr;
    for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) {
            continue;
        }
        const std::string& key = node.rawDevelopment.recipe.source.relativePathKey;
        if (key.empty() || key == m_ActiveRawWorkspaceSourceKey) {
            compactNode = &node;
            break;
        }
    }
    if (!compactNode) {
        return AdoptActiveRawWorkspaceGraphAsManagedRaw();
    }

    const int compactNodeId = compactNode->id;
    const EditorNodeGraph::Vec2 sourcePosition = compactNode->position;
    std::vector<EditorNodeGraph::Link> downstreamLinks;
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId == compactNodeId &&
            link.fromSocketId == EditorNodeGraph::kImageOutputSocketId &&
            m_NodeGraph.IsRenderLink(link)) {
            downstreamLinks.push_back(link);
        }
    }

    constexpr float kNodeSpacing = 280.0f;
    if (!AddRawSourceNodeFromFile(m_ActiveRawWorkspaceRecipe.source.sourcePath, sourcePosition)) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Failed to create the managed RAW Source node.",
            "raw-workspace-decompose-source");
        return false;
    }
    const int rawSourceNodeId = m_NodeGraph.GetSelectedNodeId();

    EditorNodeGraph::RawDecodePayload decodePayload;
    decodePayload.settings = Stack::RawRecipe::ToRawDevelopSettings(m_ActiveRawWorkspaceRecipe);
    if (!AddRawDecodeNodeFromPayload(std::move(decodePayload), EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing, sourcePosition.y })) {
        return false;
    }
    const int rawDecodeNodeId = m_NodeGraph.GetSelectedNodeId();

    AddLayerNodeAt(LayerType::ToneCurve, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 2.0f, sourcePosition.y });
    const int toneCurveNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::ViewTransform, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 3.0f, sourcePosition.y });
    const int viewTransformNodeId = m_NodeGraph.GetSelectedNodeId();
    if (rawSourceNodeId <= 0 || rawDecodeNodeId <= 0 || toneCurveNodeId <= 0 || viewTransformNodeId <= 0) {
        return false;
    }
    if (!ApplyRawRecipeLayerStateToGraphNode(m_NodeGraph, m_Layers, toneCurveNodeId, m_ActiveRawWorkspaceRecipe.finishTone.layerJson) ||
        !ApplyRawRecipeLayerStateToGraphNode(m_NodeGraph, m_Layers, viewTransformNodeId, m_ActiveRawWorkspaceRecipe.viewTransform.layerJson)) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Failed to create the managed RAW finish layers.",
            "raw-workspace-decompose-finish-layers");
        return false;
    }

    std::string errorMessage;
    const bool chainConnected =
        ConnectGraphSockets(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, rawDecodeNodeId, EditorNodeGraph::kRawInputSocketId, &errorMessage) &&
        ConnectGraphSockets(rawDecodeNodeId, EditorNodeGraph::kImageOutputSocketId, toneCurveNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId, viewTransformNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    if (!chainConnected) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            errorMessage.empty() ? "Failed to connect the managed RAW chain." : errorMessage,
            "raw-workspace-decompose-connect");
        return false;
    }

    if (downstreamLinks.empty()) {
        EditorNodeGraph::Node* outputNode =
            m_NodeGraph.AddOutputNode(EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 4.0f, sourcePosition.y }, true);
        if (outputNode) {
            downstreamLinks.push_back(EditorNodeGraph::Link{
                compactNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                outputNode->id,
                EditorNodeGraph::kImageInputSocketId
            });
        }
    }

    for (const EditorNodeGraph::Link& link : downstreamLinks) {
        errorMessage.clear();
        if (!ConnectGraphSockets(
                viewTransformNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                link.toNodeId,
                link.toSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to reconnect the downstream RAW graph." : errorMessage,
                "raw-workspace-decompose-reconnect");
            return false;
        }
    }

    RemoveGraphNode(compactNodeId);
    EditorNodeGraph::NodeGroup* group = m_NodeGraph.AddGroup(
        ManagedRawSectionTitle(m_ActiveRawWorkspaceRecipe),
        EditorNodeGraph::Vec2{ sourcePosition.x - 28.0f, sourcePosition.y - 62.0f },
        EditorNodeGraph::Vec2{ kNodeSpacing * 3.0f + 250.0f, 230.0f });
    const int groupId = group ? group->id : -1;

    m_ActiveManagedRawSection = Stack::RawWorkspace::BuildManagedRawSection(
        ManagedRawSectionId(m_ActiveRawWorkspaceRecipe, rawSourceNodeId, rawDecodeNodeId),
        ManagedRawProjectLocalId(m_ActiveRawWorkspaceRecipe),
        m_ActiveRawWorkspaceRecipe.source.relativePathKey,
        m_ActiveRawWorkspaceRecipe.source.fingerprint,
        groupId,
        rawSourceNodeId,
        rawDecodeNodeId,
        toneCurveNodeId,
        viewTransformNodeId);
    m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::ManagedDecomposed;

    if (!ValidateActiveRawWorkspaceManagedGraph(true)) {
        return false;
    }

    SelectGraphNode(rawDecodeNodeId);
    RequestOpenEditorTab();
    MarkDirty();
    SaveActiveRawWorkspaceProject(false);
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "RAW project decomposed to managed nodes.",
        "raw-workspace-decompose-success");
    return true;
}

bool EditorModule::AdoptActiveRawWorkspaceGraphAsManagedRaw() {
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }

    Stack::RawWorkspace::ManagedRawSection section;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    std::string reason;
    if (!Stack::RawWorkspace::TryBuildManagedRawSectionFromGraph(
            m_NodeGraph,
            m_ActiveRawWorkspaceRecipe,
            section,
            recipe,
            &reason)) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            reason.empty() ? "No valid managed RAW chain was found." : reason,
            "raw-workspace-managed-adopt");
        return false;
    }

    m_ActiveManagedRawSection = std::move(section);
    CopyManagedLayerStateToRecipe(m_NodeGraph, m_Layers, m_ActiveManagedRawSection, recipe);
    m_ActiveRawWorkspaceRecipe = std::move(recipe);
    m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::ManagedDecomposed;
    MarkDirty();
    SaveActiveRawWorkspaceProject(false);
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "RAW graph adopted as a managed RAW chain.",
        "raw-workspace-managed-adopt-success");
    return true;
}

bool EditorModule::ReadoptActiveRawWorkspaceGraphAsRecipe() {
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        ValidateActiveRawWorkspaceManagedGraph(false)) {
        MarkDirty();
        SaveActiveRawWorkspaceProject(false);
        return true;
    }
    return AdoptActiveRawWorkspaceGraphAsManagedRaw();
}

bool EditorModule::RepairActiveRawWorkspaceManagedGraph() {
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed ||
        m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::CustomGraph) {
        const Stack::RawWorkspace::ManagedRawRepairResult repair =
            Stack::RawWorkspace::RepairManagedRawSectionGraph(
                m_NodeGraph,
                m_ActiveManagedRawSection,
                m_ActiveRawWorkspaceRecipe);
        if (repair.repaired) {
            Stack::RawRecipe::RawDevelopmentRecipe repairedRecipe = repair.validation.recipe;
            if (!CopyManagedLayerStateToRecipe(
                    m_NodeGraph,
                    m_Layers,
                    m_ActiveManagedRawSection,
                    repairedRecipe)) {
                QueueUiNotification(
                    UiNotificationSeverity::Info,
                    "The repaired RAW tone/view layers cannot round-trip through the RAW recipe.",
                    "raw-workspace-managed-repair-layer");
                return false;
            }
            m_ActiveRawWorkspaceRecipe = std::move(repairedRecipe);
            m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::ManagedDecomposed;
            if (Stack::RawWorkspace::SourceRecord* source = FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey)) {
                source->project.mode = Stack::RawWorkspace::RawProjectMode::ManagedDecomposed;
                source->project.readOnlyReason.clear();
            }
            MarkRenderDirty(m_ActiveManagedRawSection.rawDecodeNodeId);
            MarkRenderDirty(m_ActiveManagedRawSection.toneCurveNodeId);
            MarkRenderDirty(m_ActiveManagedRawSection.viewTransformNodeId);
            MarkDirty();
            SaveActiveRawWorkspaceProject(false);
            QueueUiNotification(
                repair.changed ? UiNotificationSeverity::Success : UiNotificationSeverity::Info,
                repair.message.empty() ? "Managed RAW chain repaired." : repair.message,
                "raw-workspace-managed-repair");
            return true;
        }

        if (!repair.message.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                repair.message,
                "raw-workspace-managed-repair-blocked");
        }
    }

    return AdoptActiveRawWorkspaceGraphAsManagedRaw();
}

bool EditorModule::DetachActiveRawWorkspaceGraphFromRawTab() {
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }
    MarkActiveRawWorkspaceProjectAsCustomGraph(Stack::RawWorkspace::kCustomGraphReadOnlyReason);
    SaveActiveRawWorkspaceProject(false);
    QueueUiNotification(
        UiNotificationSeverity::Info,
        "RAW graph detached from RAW tab editing.",
        "raw-workspace-managed-detach");
    return true;
}
