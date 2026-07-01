#include "Editor/EditorModule.h"

#include <algorithm>
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
