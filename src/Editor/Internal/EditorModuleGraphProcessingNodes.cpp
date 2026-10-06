#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphEditorCommands.h"

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

using Stack::Editor::AddGraphNode;
using Stack::Editor::ApplyGraphCommand;

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

void ConnectCandidate(EditorNodeGraph::Graph& graph, int from, const std::string& output,
    int to, const std::string& input) {
    std::string error;
    if (!graph.TryConnectSockets(from,output,to,input,&error)) throw std::runtime_error(error);
}

template<class Factory>
int AddWithSelectedImage(EditorModule& editor, const std::string& input, Factory&& create) {
    return AddGraphNode(editor,[&](auto& graph) {
        const auto* selected = graph.FindNode(graph.GetSelectedNodeId());
        const auto output = selected ? FindImageOutputSocket(graph,*selected) : std::string{};
        const int upstream = output.empty() ? 0 : selected->id;
        auto* node = create(graph);
        if (node && upstream && graph.CanConnectSockets(upstream,output,node->id,input))
            ConnectCandidate(graph,upstream,output,node->id,input);
        return node;
    });
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
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddRawDevelopmentNode(std::move(payload), graphPosition); }) > 0;
}

void EditorModule::AddRawNeuralDenoiseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawNeuralDenoisePayload payload;
    AddRawNeuralDenoiseNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawNeuralDenoiseNodeFromPayload(EditorNodeGraph::RawNeuralDenoisePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddRawNeuralDenoiseNode(std::move(payload), graphPosition); }) > 0;
}

void EditorModule::AddRawDevelopNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDevelopPayload payload;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Manual;
    if (const EditorNodeGraph::Node* selected = GetNodeGraph().FindNode(GetNodeGraph().GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
        }
    }
    AddRawDevelopNodeFromPayload(std::move(payload), graphPosition);
}

void EditorModule::AddRawDecodeNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDecodePayload payload;
    if (const EditorNodeGraph::Node* selected = GetNodeGraph().FindNode(GetNodeGraph().GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
        } else if (selected->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            if (const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(GetNodeGraph(), *selected)) {
                payload.settings = BuildRawDevelopSettingsFromMetadata(rawSourceNode->rawSource.metadata);
            }
        }
    }
    AddRawDecodeNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDecodeNodeFromPayload(EditorNodeGraph::RawDecodePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddRawDecodeNode(std::move(payload), graphPosition); }) > 0;
}

bool EditorModule::AddRawDevelopNodeFromPayload(EditorNodeGraph::RawDevelopPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddRawDevelopNode(std::move(payload), graphPosition); }) > 0;
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
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddRawDetailAutoMaskNode(std::move(payload), graphPosition); }) > 0;
}

void EditorModule::AddRawDetailFusionNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddWithSelectedImage(*this,EditorNodeGraph::kImageInputSocketId,[&](auto& graph) {
        return graph.AddRawDetailFusionNode({},graphPosition);
    });
}

bool EditorModule::AddRawDetailFusionNodeFromPayload(EditorNodeGraph::RawDetailFusionPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddRawDetailFusionNode(std::move(payload), graphPosition); }) > 0;
}

void EditorModule::AddHdrMergeNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddWithSelectedImage(*this,EditorNodeGraph::kHdrMergeInput1SocketId,[&](auto& graph) {
        return graph.AddHdrMergeNode({},graphPosition);
    });
}

bool EditorModule::AddHdrMergeNodeFromPayload(EditorNodeGraph::HdrMergePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddHdrMergeNode(std::move(payload), graphPosition); }) > 0;
}

void EditorModule::AddMfsrNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddWithSelectedImage(*this,EditorNodeGraph::kMfsrReferenceInputSocketId,[&](auto& graph) {
        return graph.AddMfsrNode({},graphPosition);
    });
}

bool EditorModule::AddMfsrNodeFromPayload(EditorNodeGraph::MfsrPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddMfsrNode(std::move(payload), graphPosition); }) > 0;
}

void EditorModule::AddLutNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const int nodeId = AddWithSelectedImage(*this,EditorNodeGraph::kImageInputSocketId,[&](auto& graph) {
        const auto* lut = graph.AddLutNode({},graphPosition);
        if (!lut) throw std::runtime_error("The LUT could not be created.");
        const int id = lut->id;
        const auto* mask = graph.AddMaskGeneratorNode(EditorNodeGraph::MaskGeneratorKind::Solid,
            {graphPosition.x-172.f,graphPosition.y+78.f});
        if (!mask) throw std::runtime_error("The LUT mask could not be created.");
        ConnectCandidate(graph,mask->id,EditorNodeGraph::kMaskOutputSocketId,id,EditorNodeGraph::kMaskInputSocketId);
        return graph.FindNode(id);
    });
    if (nodeId > 0) SwitchToComplexNodeSubWindow(nodeId);
}

bool EditorModule::AddLutNodeFromPayload(EditorNodeGraph::LutPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    return AddGraphNode(*this,[&](auto& graph) { return graph.AddLutNode(std::move(payload), graphPosition); }) > 0;
}

bool EditorModule::ConvertRawDetailFusionToHybrid(int fusionNodeId) {
    int levelsId = 0;
    std::string error;
    const bool applied = ApplyGraphCommand(*this,[&](auto& graph) {
        const auto* fusion = graph.FindNode(fusionNodeId);
        const auto* input = graph.FindInputLink(fusionNodeId,EditorNodeGraph::kMaskInputSocketId);
        const auto* automatic = input ? graph.FindNode(input->fromNodeId) : nullptr;
        if (!fusion || fusion->kind != EditorNodeGraph::NodeKind::RawDetailFusion || !automatic ||
            automatic->kind != EditorNodeGraph::NodeKind::RawDetailAutoMask ||
            input->fromSocketId != EditorNodeGraph::kMaskOutputSocketId)
            throw std::runtime_error("Connect an automatic detail mask before adding manual levels.");
        const int automaticId = automatic->id;
        const EditorNodeGraph::Vec2 position{(automatic->position.x+fusion->position.x)*0.5f,automatic->position.y};
        const auto* levels = graph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Levels,position);
        if (!levels) throw std::runtime_error("The mask levels operation could not be created.");
        levelsId = levels->id;
        ConnectCandidate(graph,automaticId,EditorNodeGraph::kMaskOutputSocketId,levelsId,EditorNodeGraph::kMaskUtilityInputSocketId);
        ConnectCandidate(graph,levelsId,EditorNodeGraph::kMaskOutputSocketId,fusionNodeId,EditorNodeGraph::kMaskInputSocketId);
        graph.SelectNode(levelsId);
    },&error);
    if (!applied) { PostNotification(UiNotificationSeverity::Error,error,"detail-hybrid"); return false; }
    SelectGraphNode(levelsId);
    return true;
}

bool EditorModule::AddFullRawTreeToSource(int rawSourceNodeId) {
    const auto* source = GetNodeGraph().FindNode(rawSourceNodeId);
    if (!source || source->kind != EditorNodeGraph::NodeKind::RawSource) return false;
    const int completedBefore = GetCompletedChainCount();
    auto settings = nlohmann::json::array();
    for (const auto& layer : GetLayers()) settings.push_back(layer->Serialize());
    const int toneIndex = static_cast<int>(settings.size());
    const auto tone = LayerRegistry::CreateLayer(LayerType::ToneCurve);
    const auto view = LayerRegistry::CreateLayer(LayerType::ViewTransform);
    if (!tone || !view) return false;
    settings.push_back(tone->Serialize());
    settings.push_back(view->Serialize());
    int outputId = 0;
    auto context = GetGraphEditorContext();
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& graph) {
        const auto* raw = graph.FindNode(rawSourceNodeId);
        const auto position = raw->position;
        EditorNodeGraph::RawDecodePayload payload;
        payload.settings = BuildRawDevelopSettingsFromMetadata(raw->rawSource.metadata);
        const auto require = [](const auto* node) {
            if (!node) throw std::runtime_error("The RAW processing branch could not be created.");
            return node->id;
        };
        const int decodeId = require(graph.AddRawDecodeNode(std::move(payload),{position.x+280.f,position.y}));
        const int toneId = require(graph.AddLayerNode(LayerType::ToneCurve,toneIndex,{position.x+560.f,position.y}));
        const int viewId = require(graph.AddLayerNode(LayerType::ViewTransform,toneIndex+1,{position.x+840.f,position.y}));
        outputId = require(graph.AddOutputNode({position.x+1120.f,position.y}));
        ConnectCandidate(graph,rawSourceNodeId,EditorNodeGraph::kRawOutputSocketId,decodeId,EditorNodeGraph::kRawInputSocketId);
        ConnectCandidate(graph,decodeId,EditorNodeGraph::kImageOutputSocketId,toneId,EditorNodeGraph::kImageInputSocketId);
        ConnectCandidate(graph,toneId,EditorNodeGraph::kImageOutputSocketId,viewId,EditorNodeGraph::kImageInputSocketId);
        ConnectCandidate(graph,viewId,EditorNodeGraph::kImageOutputSocketId,outputId,EditorNodeGraph::kImageInputSocketId);
        graph.SetOutputNodeId(outputId);
        graph.SelectNode(outputId);
    });
    std::string error;
    if (!context.applyDocumentEdit(std::move(proposal),std::move(settings),GetGraphAnimation(),error)) {
        PostNotification(UiNotificationSeverity::Error,error,"raw-tree-create");
        return false;
    }
    if (!IsEditingRawLayerMaskGraph()) {
        if (completedBefore < 2 && GetCompletedChainCount() >= 2) EnsureCompositeNode();
        EnsureCompositeSceneState(m_LastCompositeCanvasSize);
        MoveCompositeOutputToFront(outputId);
        m_CompositeSelectedOutputNodeId = outputId;
    }
    SelectGraphNode(outputId);
    return true;
}
