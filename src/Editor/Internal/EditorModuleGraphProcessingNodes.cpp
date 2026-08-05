#include "Editor/EditorModule.h"

#include "Editor/Internal/EditorModuleDevelopDefaults.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"

#include <algorithm>
#include <functional>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

using Stack::Editor::DevelopDefaults::BuildDefaultIntegratedToneLayerJson;
using Stack::Editor::DevelopDefaults::BuildRawDevelopSettingsFromMetadata;

namespace {

const EditorNodeGraph::Node* FindUpstreamRawSourceNode(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& rawDomainNode) {
    const EditorNodeGraph::Link* rawInput = graph.FindInputLink(rawDomainNode.id, EditorNodeGraph::kRawInputSocketId);
    std::unordered_set<int> visited;
    while (rawInput) {
        if (!visited.insert(rawInput->fromNodeId).second) {
            return nullptr;
        }

        const EditorNodeGraph::Node* upstream = graph.FindNode(rawInput->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        rawInput = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

std::string FindImageOutputSocket(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const std::string preferred = graph.DefaultOutputSocket(node);
    for (const EditorNodeGraph::SocketDefinition& output :
            graph.GetSockets(node, false)) {
        if (output.direction ==
                EditorNodeGraph::SocketDirection::Output &&
            output.type == EditorNodeGraph::SocketType::Image &&
            output.id == preferred) {
            return output.id;
        }
    }
    for (const EditorNodeGraph::SocketDefinition& output :
            graph.GetSockets(node, false)) {
        if (output.direction ==
                EditorNodeGraph::SocketDirection::Output &&
            output.type == EditorNodeGraph::SocketType::Image) {
            return output.id;
        }
    }
    return {};
}

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
}

void HashDevelopSubjectImportance(std::size_t& hash, const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    HashCombine(hash, HashValue(importance.enabled));
    HashCombine(hash, HashValue(importance.regions.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        HashCombine(hash, HashValue(region.id));
        HashCombine(hash, HashValue(static_cast<int>(region.mode)));
        HashCombine(hash, HashValue(region.enabled));
        HashCombine(hash, HashValue(region.centerX));
        HashCombine(hash, HashValue(region.centerY));
        HashCombine(hash, HashValue(region.radiusX));
        HashCombine(hash, HashValue(region.radiusY));
        HashCombine(hash, HashValue(region.feather));
        HashCombine(hash, HashValue(region.strength));
    }
    HashCombine(hash, HashValue(importance.strokes.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        HashCombine(hash, HashValue(stroke.id));
        HashCombine(hash, HashValue(static_cast<int>(stroke.mode)));
        HashCombine(hash, HashValue(stroke.enabled));
        HashCombine(hash, HashValue(stroke.subtract));
        HashCombine(hash, HashValue(stroke.radius));
        HashCombine(hash, HashValue(stroke.feather));
        HashCombine(hash, HashValue(stroke.strength));
        HashCombine(hash, HashValue(stroke.points.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            HashCombine(hash, HashValue(point.x));
            HashCombine(hash, HashValue(point.y));
        }
    }
}

std::size_t BuildDevelopAutoSolveTriggerHash(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata) {
    std::size_t hash = HashValue(metadata.sourcePath);
    HashCombine(hash, HashValue(metadata.hasDngBaselineExposure));
    HashCombine(hash, HashValue(metadata.dngBaselineExposure));
    HashCombine(hash, HashValue(metadata.blackLevel));
    HashCombine(hash, HashValue(metadata.whiteLevel));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[2]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[2]));
    HashCombine(hash, HashValue(static_cast<int>(payload.uiMode)));
    HashCombine(hash, HashValue(static_cast<int>(payload.autoGuidance.intent)));
    HashCombine(hash, HashValue(payload.autoGuidance.autoStrength));
    HashCombine(hash, HashValue(payload.autoGuidance.exposureBias));
    HashCombine(hash, HashValue(payload.autoGuidance.dynamicRange));
    HashCombine(hash, HashValue(payload.autoGuidance.shadowLift));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightGuard));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightCharacter));
    HashCombine(hash, HashValue(payload.autoGuidance.contrastBias));
    HashCombine(hash, HashValue(payload.autoGuidance.subjectSceneBias));
    HashCombine(hash, HashValue(payload.autoGuidance.moodReadabilityBias));
    HashDevelopSubjectImportance(hash, payload.subjectImportance);
    return hash;
}

std::size_t BuildDevelopAutoRawSolveTriggerHash(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata) {
    std::size_t hash = HashValue(metadata.sourcePath);
    HashCombine(hash, HashValue(metadata.hasDngBaselineExposure));
    HashCombine(hash, HashValue(metadata.dngBaselineExposure));
    HashCombine(hash, HashValue(metadata.blackLevel));
    HashCombine(hash, HashValue(metadata.whiteLevel));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[2]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[2]));
    HashCombine(hash, HashValue(static_cast<int>(payload.uiMode)));
    HashCombine(hash, HashValue(static_cast<int>(payload.autoGuidance.intent)));
    HashCombine(hash, HashValue(payload.autoGuidance.autoStrength));
    HashCombine(hash, HashValue(payload.autoGuidance.exposureBias));
    HashCombine(hash, HashValue(payload.autoGuidance.dynamicRange));
    HashCombine(hash, HashValue(payload.autoGuidance.shadowLift));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightGuard));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightCharacter));
    HashCombine(hash, HashValue(payload.autoGuidance.contrastBias));
    HashCombine(hash, HashValue(payload.autoGuidance.subjectSceneBias));
    HashCombine(hash, HashValue(payload.autoGuidance.moodReadabilityBias));
    HashDevelopSubjectImportance(hash, payload.subjectImportance);
    return hash;
}

} // namespace

void EditorModule::AddRawDevelopmentNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = Stack::RawRecipe::MakeDefaultRecipe({});
    AddRawDevelopmentNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDevelopmentNodeFromPayload(EditorNodeGraph::RawDevelopmentPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDevelopmentNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawNeuralDenoiseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawNeuralDenoisePayload payload;
    AddRawNeuralDenoiseNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawNeuralDenoiseNodeFromPayload(EditorNodeGraph::RawNeuralDenoisePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawNeuralDenoiseNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawDevelopNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDevelopPayload payload;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Manual;
    if (const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
        }
    }
    AddRawDevelopNodeFromPayload(std::move(payload), graphPosition);
}

void EditorModule::AddRawDecodeNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDecodePayload payload;
    if (const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
        } else if (selected->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            if (const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *selected)) {
                payload.settings = BuildRawDevelopSettingsFromMetadata(rawSourceNode->rawSource.metadata);
            }
        }
    }
    AddRawDecodeNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDecodeNodeFromPayload(EditorNodeGraph::RawDecodePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDecodeNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::AddRawDevelopNodeFromPayload(EditorNodeGraph::RawDevelopPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDevelopNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::UpdateDevelopAutoState(
    int nodeId,
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    bool forceReanalysis,
    bool forceFullReanalysis) {
    if (payload.uiMode != EditorNodeGraph::RawDevelopUiMode::Auto) {
        m_DevelopAutoSolveTriggerHashes.erase(nodeId);
        m_DevelopAutoRawSolveTriggerHashes.erase(nodeId);
        m_DevelopAutoRawCalibrationHashes.erase(nodeId);
        return false;
    }

    const std::size_t triggerHash = BuildDevelopAutoSolveTriggerHash(payload, metadata);
    const std::size_t rawTriggerHash = BuildDevelopAutoRawSolveTriggerHash(payload, metadata);
    const auto it = m_DevelopAutoSolveTriggerHashes.find(nodeId);
    const auto rawIt = m_DevelopAutoRawSolveTriggerHashes.find(nodeId);
    const auto rawCalibrationIt = m_DevelopAutoRawCalibrationHashes.find(nodeId);
    const bool rawInputsChanged =
        rawIt == m_DevelopAutoRawSolveTriggerHashes.end() ||
        rawIt->second != rawTriggerHash;
    const bool explicitRawCalibrationNeeded =
        forceFullReanalysis &&
        (rawCalibrationIt == m_DevelopAutoRawCalibrationHashes.end() ||
         rawCalibrationIt->second != rawTriggerHash);
    const bool anySolveNeeded =
        forceReanalysis ||
        forceFullReanalysis ||
        it == m_DevelopAutoSolveTriggerHashes.end() ||
        it->second != triggerHash ||
        rawInputsChanged;
    if (!anySolveNeeded) {
        return false;
    }

    const bool fullSolveNeeded =
        forceFullReanalysis ||
        rawInputsChanged ||
        explicitRawCalibrationNeeded;
    ApplyDevelopAutoSolve(payload, metadata, true, fullSolveNeeded);
    m_DevelopAutoSolveTriggerHashes[nodeId] = BuildDevelopAutoSolveTriggerHash(payload, metadata);
    m_DevelopAutoRawSolveTriggerHashes[nodeId] = BuildDevelopAutoRawSolveTriggerHash(payload, metadata);
    if (fullSolveNeeded) {
        m_DevelopAutoRawCalibrationHashes[nodeId] = rawTriggerHash;
    }
    return true;
}

void EditorModule::AddRawDetailAutoMaskNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDetailAutoMaskPayload payload;
    AddRawDetailAutoMaskNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDetailAutoMaskNodeFromPayload(EditorNodeGraph::RawDetailAutoMaskPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDetailAutoMaskNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawDetailFusionNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const std::string upstreamSocketId = selected
        ? FindImageOutputSocket(m_NodeGraph, *selected)
        : std::string();
    const int upstreamNodeId =
        !upstreamSocketId.empty() ? selected->id : -1;

    EditorNodeGraph::RawDetailFusionPayload fusionPayload;
    EditorNodeGraph::Node* fusionNode = m_NodeGraph.AddRawDetailFusionNode(std::move(fusionPayload), graphPosition);
    if (!fusionNode) {
        return;
    }
    const int fusionNodeId = fusionNode->id;

    std::string errorMessage;
    if (upstreamNodeId > 0) {
        ConnectGraphSockets(upstreamNodeId, upstreamSocketId, fusionNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    }
    SelectGraphNode(fusionNodeId);
    MarkRenderDirty(fusionNodeId);
}

bool EditorModule::AddRawDetailFusionNodeFromPayload(EditorNodeGraph::RawDetailFusionPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDetailFusionNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddHdrMergeNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const std::string upstreamSocketId = selected
        ? FindImageOutputSocket(m_NodeGraph, *selected)
        : std::string();
    const int upstreamNodeId =
        !upstreamSocketId.empty() ? selected->id : -1;

    EditorNodeGraph::HdrMergePayload payload;
    EditorNodeGraph::Node* hdrNode = m_NodeGraph.AddHdrMergeNode(std::move(payload), graphPosition);
    if (!hdrNode) {
        return;
    }
    const int hdrNodeId = hdrNode->id;

    std::string errorMessage;
    if (upstreamNodeId > 0) {
        if (!ConnectGraphSockets(upstreamNodeId, upstreamSocketId, hdrNodeId, EditorNodeGraph::kHdrMergeInput1SocketId, &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "HDR Merge auto-connect failed: " + errorMessage,
                "hdr-merge-autoconnect");
        }
    }
    SelectGraphNode(hdrNodeId);
    MarkRenderDirty(hdrNodeId);
}

bool EditorModule::AddHdrMergeNodeFromPayload(EditorNodeGraph::HdrMergePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddHdrMergeNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddMfsrNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const std::string upstreamSocketId = selected
        ? FindImageOutputSocket(m_NodeGraph, *selected)
        : std::string();
    const int upstreamNodeId =
        !upstreamSocketId.empty() ? selected->id : -1;

    EditorNodeGraph::MfsrPayload payload;
    EditorNodeGraph::Node* mfsrNode = m_NodeGraph.AddMfsrNode(std::move(payload), graphPosition);
    if (!mfsrNode) {
        return;
    }
    const int mfsrNodeId = mfsrNode->id;

    std::string errorMessage;
    if (upstreamNodeId > 0) {
        if (!ConnectGraphSockets(upstreamNodeId, upstreamSocketId, mfsrNodeId, EditorNodeGraph::kMfsrReferenceInputSocketId, &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "MFSR auto-connect failed: " + errorMessage,
                "mfsr-autoconnect");
        }
    }
    SelectGraphNode(mfsrNodeId);
    MarkRenderDirty(mfsrNodeId);
}

bool EditorModule::AddMfsrNodeFromPayload(EditorNodeGraph::MfsrPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddMfsrNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddLutNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const std::string upstreamSocketId = selected
        ? FindImageOutputSocket(m_NodeGraph, *selected)
        : std::string();
    const int upstreamNodeId =
        !upstreamSocketId.empty() ? selected->id : -1;

    EditorNodeGraph::LutPayload payload;
    EditorNodeGraph::Node* lutNode = m_NodeGraph.AddLutNode(std::move(payload), graphPosition);
    if (!lutNode) {
        return;
    }
    const int lutNodeId = lutNode->id;
    std::string errorMessage;

    const EditorNodeGraph::Vec2 maskPosition{
        graphPosition.x - 172.0f,
        graphPosition.y + 78.0f
    };
    EditorNodeGraph::Node* solidMaskNode = m_NodeGraph.AddMaskGeneratorNode(EditorNodeGraph::MaskGeneratorKind::Solid, maskPosition);
    if (solidMaskNode) {
        if (!ConnectGraphSockets(
                solidMaskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                lutNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "LUT mask auto-connect failed: " + errorMessage,
                "lut-mask-autoconnect");
            errorMessage.clear();
        }
    }

    if (upstreamNodeId > 0) {
        if (!ConnectGraphSockets(upstreamNodeId, upstreamSocketId, lutNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "LUT auto-connect failed: " + errorMessage,
                "lut-autoconnect");
        }
    }

    SelectGraphNode(lutNodeId);
    MarkRenderDirty(lutNodeId);
    SwitchToComplexNodeSubWindow(lutNodeId);
}

bool EditorModule::AddLutNodeFromPayload(EditorNodeGraph::LutPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddLutNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::ConvertRawDetailFusionToHybrid(int fusionNodeId) {
    EditorNodeGraph::Node* fusionNode = m_NodeGraph.FindNode(fusionNodeId);
    if (!fusionNode || fusionNode->kind != EditorNodeGraph::NodeKind::RawDetailFusion) {
        return false;
    }
    const EditorNodeGraph::Link* maskInput = m_NodeGraph.FindInputLink(fusionNodeId, EditorNodeGraph::kMaskInputSocketId);
    if (!maskInput) {
        return false;
    }
    const EditorNodeGraph::Node* autoMaskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
    if (!autoMaskNode || autoMaskNode->kind != EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        maskInput->fromSocketId != EditorNodeGraph::kMaskOutputSocketId) {
        return false;
    }
    const int autoMaskNodeId = autoMaskNode->id;
    const EditorNodeGraph::Vec2 autoMaskPosition = autoMaskNode->position;
    const EditorNodeGraph::Vec2 fusionPosition = fusionNode->position;

    const EditorNodeGraph::Vec2 pos{
        (autoMaskPosition.x + fusionPosition.x) * 0.5f,
        autoMaskPosition.y
    };
    const int nextNodeIdBefore =
        m_NodeGraph.GetNextNodeId();
    const std::vector<int> selectionBefore =
        m_NodeGraph.GetSelectedNodeIds();
    const EditorNodeGraph::Link* selectedLinkBefore =
        m_NodeGraph.GetSelectedLink();
    const std::optional<EditorNodeGraph::Link>
        selectedLinkSnapshot = selectedLinkBefore
            ? std::optional<EditorNodeGraph::Link>(
                *selectedLinkBefore)
            : std::nullopt;
    EditorNodeGraph::Node* levelsNode = m_NodeGraph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Levels, pos);
    if (!levelsNode) {
        return false;
    }
    const int levelsNodeId = levelsNode->id;
    const auto rollbackLevelsInsertion = [&]() {
        m_NodeGraph.RemoveNode(levelsNodeId);
        m_NodeGraph.SetNextNodeId(nextNodeIdBefore);
        m_NodeGraph.ClearSelection();
        for (const int selectedNodeId : selectionBefore) {
            m_NodeGraph.SelectNode(selectedNodeId, true);
        }
        if (selectedLinkSnapshot) {
            const EditorNodeGraph::Link& selected =
                *selectedLinkSnapshot;
            m_NodeGraph.SelectLink(
                selected.fromNodeId,
                selected.fromSocketId,
                selected.toNodeId,
                selected.toSocketId);
        }
    };

    std::string errorMessage;
    if (!ConnectGraphSockets(
            autoMaskNodeId,
            EditorNodeGraph::kMaskOutputSocketId,
            levelsNodeId,
            EditorNodeGraph::kMaskUtilityInputSocketId,
            &errorMessage) ||
        !ConnectGraphSockets(
            levelsNodeId,
            EditorNodeGraph::kMaskOutputSocketId,
            fusionNodeId,
            EditorNodeGraph::kMaskInputSocketId,
            &errorMessage)) {
        rollbackLevelsInsertion();
        MarkRenderDirty(fusionNodeId);
        return false;
    }
    SelectGraphNode(levelsNodeId);
    MarkRenderDirty(fusionNodeId);
    return true;
}

bool EditorModule::AddFullRawTreeToSource(int rawSourceNodeId) {
    const EditorNodeGraph::Node* rawSourceNode = m_NodeGraph.FindNode(rawSourceNodeId);
    if (!rawSourceNode || rawSourceNode->kind != EditorNodeGraph::NodeKind::RawSource) {
        return false;
    }

    const int completedBefore = GetCompletedChainCount();
    const EditorNodeGraph::Vec2 sourcePosition = rawSourceNode->position;
    const int nextNodeIdBefore = m_NodeGraph.GetNextNodeId();
    const int outputNodeIdBefore = m_NodeGraph.GetOutputNodeId();
    const std::vector<int> selectionBefore =
        m_NodeGraph.GetSelectedNodeIds();
    const EditorNodeGraph::Link* selectedLinkBefore =
        m_NodeGraph.GetSelectedLink();
    const std::optional<EditorNodeGraph::Link>
        selectedLinkSnapshot = selectedLinkBefore
            ? std::optional<EditorNodeGraph::Link>(
                *selectedLinkBefore)
            : std::nullopt;
    const int selectedLayerBefore = m_SelectedLayerIndex;
    const bool focusSelectedTabBefore =
        m_FocusSelectedTabNextRender;
    const std::vector<std::shared_ptr<LayerBase>> layersSnapshot = m_Layers;
    std::vector<int> originalNodeIds;
    originalNodeIds.reserve(m_NodeGraph.GetNodes().size());
    std::vector<std::pair<int, int>> originalLayerNodeIndices;
    originalLayerNodeIndices.reserve(m_Layers.size());
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        originalNodeIds.push_back(node.id);
        if (node.kind == EditorNodeGraph::NodeKind::Layer) {
            originalLayerNodeIndices.emplace_back(
                node.id,
                node.layerIndex);
        }
    }
    const auto isOriginalNodeId =
        [&originalNodeIds](int nodeId) {
            return std::find(
                       originalNodeIds.begin(),
                       originalNodeIds.end(),
                       nodeId) != originalNodeIds.end();
        };
    const auto rollback = [&]() {
        std::vector<EditorNodeGraph::Link>& links =
            m_NodeGraph.EditLinks();
        links.erase(
            std::remove_if(
                links.begin(),
                links.end(),
                [&isOriginalNodeId](
                    const EditorNodeGraph::Link& link) {
                    return !isOriginalNodeId(link.fromNodeId) ||
                        !isOriginalNodeId(link.toNodeId);
                }),
            links.end());

        std::vector<EditorNodeGraph::Node>& nodes =
            m_NodeGraph.EditNodes();
        nodes.erase(
            std::remove_if(
                nodes.begin(),
                nodes.end(),
                [&isOriginalNodeId](
                    const EditorNodeGraph::Node& node) {
                    return !isOriginalNodeId(node.id);
                }),
            nodes.end());

        m_Layers = layersSnapshot;
        for (const std::pair<int, int>& entry :
                originalLayerNodeIndices) {
            if (EditorNodeGraph::Node* node =
                    m_NodeGraph.FindNode(entry.first)) {
                node->layerIndex = entry.second;
            }
        }
        m_NodeGraph.SetNextNodeId(nextNodeIdBefore);
        m_NodeGraph.SetOutputNodeId(outputNodeIdBefore);
        m_NodeGraph.ClearSelection();
        for (const int selectedNodeId : selectionBefore) {
            m_NodeGraph.SelectNode(selectedNodeId, true);
        }
        if (selectedLinkSnapshot) {
            const EditorNodeGraph::Link& selected =
                *selectedLinkSnapshot;
            m_NodeGraph.SelectLink(
                selected.fromNodeId,
                selected.fromSocketId,
                selected.toNodeId,
                selected.toSocketId);
        }
        m_SelectedLayerIndex = selectedLayerBefore;
        m_FocusSelectedTabNextRender =
            focusSelectedTabBefore;
        RefreshGraphLayerMetadata();
        MarkRenderDirty(rawSourceNodeId);
        return false;
    };

    int rawDecodeNodeId = -1;
    int toneCurveNodeId = -1;
    int viewTransformNodeId = -1;
    int outputNodeId = -1;
    try {
        SelectGraphNode(rawSourceNodeId);

        constexpr float kNodeSpacing = 280.0f;
        AddRawDecodeNodeAt(EditorNodeGraph::Vec2{
            sourcePosition.x + kNodeSpacing * 1.0f,
            sourcePosition.y });
        rawDecodeNodeId = m_NodeGraph.GetSelectedNodeId();
        const EditorNodeGraph::Node* rawDecodeNode =
            m_NodeGraph.FindNode(rawDecodeNodeId);
        if (!rawDecodeNode ||
            rawDecodeNode->kind !=
                EditorNodeGraph::NodeKind::RawDecode) {
            return rollback();
        }
        AddLayerNodeAt(
            LayerType::ToneCurve,
            EditorNodeGraph::Vec2{
                sourcePosition.x + kNodeSpacing * 2.0f,
                sourcePosition.y });
        toneCurveNodeId = m_NodeGraph.GetSelectedNodeId();
        const EditorNodeGraph::Node* toneCurveNode =
            m_NodeGraph.FindNode(toneCurveNodeId);
        if (!toneCurveNode ||
            toneCurveNode->kind !=
                EditorNodeGraph::NodeKind::Layer ||
            toneCurveNode->layerType != LayerType::ToneCurve) {
            return rollback();
        }
        AddLayerNodeAt(
            LayerType::ViewTransform,
            EditorNodeGraph::Vec2{
                sourcePosition.x + kNodeSpacing * 3.0f,
                sourcePosition.y });
        viewTransformNodeId = m_NodeGraph.GetSelectedNodeId();
        const EditorNodeGraph::Node* viewTransformNode =
            m_NodeGraph.FindNode(viewTransformNodeId);
        if (!viewTransformNode ||
            viewTransformNode->kind !=
                EditorNodeGraph::NodeKind::Layer ||
            viewTransformNode->layerType !=
                LayerType::ViewTransform) {
            return rollback();
        }
        AddOutputNodeAt(EditorNodeGraph::Vec2{
            sourcePosition.x + kNodeSpacing * 4.0f,
            sourcePosition.y });
        outputNodeId = m_NodeGraph.GetSelectedNodeId();
        const EditorNodeGraph::Node* outputNode =
            m_NodeGraph.FindNode(outputNodeId);
        if (!outputNode ||
            outputNode->kind != EditorNodeGraph::NodeKind::Output) {
            return rollback();
        }

        std::string errorMessage;
        const bool ok =
            ConnectGraphSockets(
                rawSourceNodeId,
                EditorNodeGraph::kRawOutputSocketId,
                rawDecodeNodeId,
                EditorNodeGraph::kRawInputSocketId,
                &errorMessage) &&
            ConnectGraphSockets(
                rawDecodeNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                toneCurveNodeId,
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage) &&
            ConnectGraphSockets(
                toneCurveNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                viewTransformNodeId,
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage) &&
            ConnectGraphSockets(
                viewTransformNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                outputNodeId,
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage);
        if (!ok) {
            return rollback();
        }
    } catch (const std::bad_alloc&) {
        return rollback();
    } catch (const std::length_error&) {
        return rollback();
    } catch (const std::exception&) {
        return rollback();
    }

    if (completedBefore < 2 && GetCompletedChainCount() >= 2) {
        EnsureCompositeNode();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MoveCompositeOutputToFront(outputNodeId);
    m_CompositeSelectedOutputNodeId = outputNodeId;
    m_NodeGraph.SetOutputNodeId(outputNodeId);
    MarkRenderDirty(outputNodeId);
    SelectGraphNode(outputNodeId);
    return true;
}
