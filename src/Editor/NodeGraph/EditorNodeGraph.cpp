#include "EditorNodeGraph.h"
#include "EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/SocketPresentation.h"
#include "UnifiedNodeDefinitionRegistry.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {

bool IsChannelSocketId(const std::string& socketId) {
    return socketId == "r" || socketId == "g" || socketId == "b" || socketId == "a";
}

bool SupportsDynamicChannelInputs(const EditorNodeGraph::Node& node) {
    return node.kind == EditorNodeGraph::NodeKind::Output ||
        node.kind == EditorNodeGraph::NodeKind::Lut;
}

bool IsAverageMaskPreviewActive(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) {
    return node.kind == EditorNodeGraph::NodeKind::DataMath &&
        node.dataMathMode == EditorNodeGraph::DataMathMode::Average &&
        graph.GetSocketPreviewIntent(node.id) == EditorNodeGraph::SocketPreviewIntent::MaskConnection;
}

bool IsAverageImagePreviewActive(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) {
    return node.kind == EditorNodeGraph::NodeKind::DataMath &&
        node.dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage &&
        graph.GetSocketPreviewIntent(node.id) == EditorNodeGraph::SocketPreviewIntent::ImageConnection;
}

bool IsDataMathAverageMode(EditorNodeGraph::DataMathMode mode) {
    return mode == EditorNodeGraph::DataMathMode::Average ||
        mode == EditorNodeGraph::DataMathMode::ImageAverage;
}

const char* ChannelLabel(const std::string& socketId) {
    if (socketId == "r") return "R";
    if (socketId == "g") return "G";
    if (socketId == "b") return "B";
    if (socketId == "a") return "A";
    return "";
}

std::string ScalarInputSocketLabel(int index) {
    if (index < 0) {
        return "Scalar";
    }
    if (index < 26) {
        std::string label = "Scalar ";
        label.push_back(static_cast<char>('A' + index));
        return label;
    }
    return "Scalar";
}

Link MakeSocketLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    Link link;
    link.fromNodeId = fromNodeId;
    link.fromSocketId = fromSocketId;
    link.toNodeId = toNodeId;
    link.toSocketId = toSocketId;
    return link;
}

} // namespace

void Graph::Clear() {
    m_Nodes.clear();
    m_Links.clear();
    m_Groups.clear();
    m_CompoundDefinitions.clear();
    m_NextNodeId = 1;
    m_NextGroupId = 1;
    m_SelectedNodeId = -1;
    m_SelectedNodeIds.clear();
    m_SelectedLink = {};
    m_HasSelectedLink = false;
    m_ActiveImageNodeId = -1;
    m_OutputNodeId = -1;
    m_ForceOutputFourPins = false;
    m_AllowNoOutput = false;
    m_SocketPreviewNodeId = -1;
    m_SocketPreviewIntent = SocketPreviewIntent::None;
    TouchStructure();
}

void Graph::ResetFromLayers(int layerCount, bool hasActiveImage) {
    Clear();

    if (hasActiveImage) {
        ImagePayload image;
        image.label = "Image";
        Node* imageNode = AddImageNode(std::move(image), Vec2{ 20.0f, 120.0f });
        m_ActiveImageNodeId = imageNode ? imageNode->id : -1;
    }

    for (int i = 0; i < layerCount; ++i) {
        Node node;
        node.id = AllocateNodeId();
        node.kind = NodeKind::Layer;
        node.layerIndex = i;
        node.title = "Layer";
        node.position = DefaultLayerPosition(i);
        m_Nodes.push_back(std::move(node));
    }

    RebuildLinks();
}

void Graph::SyncLayerNodes(int layerCount) {
    const std::size_t oldNodeCount = m_Nodes.size();
    bool structureChanged = false;
    m_Nodes.erase(
        std::remove_if(m_Nodes.begin(), m_Nodes.end(), [layerCount](const Node& node) {
            return node.kind == NodeKind::Layer && (node.layerIndex < 0 || node.layerIndex >= layerCount);
        }),
        m_Nodes.end());
    structureChanged = oldNodeCount != m_Nodes.size();

    for (int i = 0; i < layerCount; ++i) {
        if (FindNodeByLayerIndex(i)) {
            continue;
        }

        Node node;
        node.id = AllocateNodeId();
        node.kind = NodeKind::Layer;
        node.layerIndex = i;
        node.title = "Layer";
        node.position = DefaultLayerPosition(i);
        m_Nodes.push_back(std::move(node));
        structureChanged = true;
    }

    const std::size_t oldLinkCount = m_Links.size();
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return !FindNode(link.fromNodeId) ||
                !FindNode(link.toNodeId) ||
                !FindSocket(link.fromNodeId, link.fromSocketId) ||
                !FindSocket(link.toNodeId, link.toSocketId);
        }),
        m_Links.end());
    structureChanged = structureChanged || oldLinkCount != m_Links.size();
    m_SelectedNodeIds.erase(
        std::remove_if(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), [this](int nodeId) {
            return FindNode(nodeId) == nullptr;
        }),
        m_SelectedNodeIds.end());
    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
    if (m_HasSelectedLink && !HasLink(m_SelectedLink.fromNodeId, m_SelectedLink.fromSocketId, m_SelectedLink.toNodeId, m_SelectedLink.toSocketId)) {
        ClearSelectedLink();
    }
    if (m_ActiveImageNodeId > 0 && !FindNode(m_ActiveImageNodeId)) {
        m_ActiveImageNodeId = -1;
    }
    if (structureChanged) {
        TouchStructure();
    }
    Validate();
}

Node* Graph::AddImageNode(ImagePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Image;
    node.position = position;
    node.image = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawSourceNode(RawSourcePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawSource;
    node.position = position;
    node.rawSource = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDevelopmentNode(RawDevelopmentPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDevelopment;
    node.position = position;
    node.rawDevelopment = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawNeuralDenoiseNode(RawNeuralDenoisePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawNeuralDenoise;
    node.position = position;
    node.rawNeuralDenoise = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDecodeNode(RawDecodePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDecode;
    node.position = position;
    node.rawDecode = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDevelopNode(RawDevelopPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDevelop;
    node.position = position;
    node.rawDevelop = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDetailAutoMaskNode(RawDetailAutoMaskPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDetailAutoMask;
    node.position = position;
    node.rawDetailAutoMask = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDetailFusionNode(RawDetailFusionPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDetailFusion;
    node.position = position;
    node.rawDetailFusion = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddHdrMergeNode(HdrMergePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::HdrMerge;
    node.position = position;
    node.hdrMerge = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMfsrNode(MfsrPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Mfsr;
    node.position = position;
    node.mfsr = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddLutNode(LutPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Lut;
    node.position = position;
    node.lut = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddLayerNode(LayerType type, int layerIndex, Vec2 position) {
    const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(type);

    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Layer;
    node.layerType = type;
    node.layerIndex = layerIndex;
    node.typeId = descriptor ? descriptor->typeId : "";
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddScopeNode(ScopeKind scopeKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Scope;
    node.scopeKind = scopeKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMaskGeneratorNode(MaskGeneratorKind maskKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskGenerator;
    node.maskKind = maskKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMaskCombineNode(MaskCombineMode combineMode, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskCombine;
    node.maskCombineMode = combineMode;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMaskUtilityNode(MaskUtilityKind utilityKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskUtility;
    node.maskUtilityKind = utilityKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddCustomMaskNode(CustomMaskPayload payload, Vec2 position) {
    if (payload.width <= 0) payload.width = 1024;
    if (payload.height <= 0) payload.height = 1024;
    const std::size_t expected =
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height);
    if (payload.rasterLayer.size() != expected) {
        payload.rasterLayer.assign(expected, 0.0f);
    }

    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::CustomMask;
    node.customMask = std::move(payload);
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddImageToMaskNode(ImageToMaskKind converterKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ImageToMask;
    node.imageToMaskKind = converterKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddImageGeneratorNode(ImageGeneratorKind generatorKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ImageGenerator;
    node.imageGeneratorKind = generatorKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMixNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Mix;
    node.position = position;
    node.mixBlendMode = MixBlendMode::Normal;
    node.mixFactor = 0.5f;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddDataMathNode(DataMathMode mode, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::DataMath;
    node.position = position;
    node.dataMathMode = mode;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddValueNode(Stack::NodeMath::FirstClassValue value, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Value;
    node.position = position;
    node.value.value = std::move(value);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddFieldMeanNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::FieldMean;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddReformatNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Reformat;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddTechnicalImageNode(
    Stack::NodeMath::TechnicalImageOperation operation,
    Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::TechnicalImage;
    node.position = position;
    node.technicalImageSettings.operation = operation;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddCompoundNode(
    const Stack::NodeMath::DefinitionReference& definition,
    Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.instanceUuid = Stack::NodeMath::GenerateCanonicalUuid();
    node.kind = NodeKind::Compound;
    node.position = position;
    node.compound.instance.instanceUuid = node.instanceUuid;
    node.compound.instance.definition = definition;
    Stack::NodeMath::ResolveCompoundInstance(node.compound.instance, m_CompoundDefinitions);
    if (const Stack::NodeMath::CompoundDefinition* resolved = FindCompoundDefinition(definition)) {
        node.title = resolved->label;
        node.definitionId = resolved->identity.id;
        node.definitionVersion = Stack::NodeMath::ToString(resolved->identity.version);
        node.definitionHash = resolved->identity.contentHash;
        node.definitionResolved = node.compound.instance.resolution == Stack::NodeMath::CompoundResolutionStatus::Exact;
        node.definitionResolutionError = node.compound.instance.resolutionError;
    } else {
        node.title = "Unresolved Compound";
        node.definitionId = definition.id;
        node.definitionVersion = Stack::NodeMath::ToString(definition.version);
        node.definitionHash = definition.contentHash;
        node.definitionResolved = false;
        node.definitionResolutionError = node.compound.instance.resolutionError;
    }
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddFrequencyFftNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::FrequencyFft;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddFrequencyIfftNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::FrequencyIfft;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddSpectrumViewNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::SpectrumView;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddFrequencyMaskNode(FrequencyMaskShape shape, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::FrequencyMask;
    node.position = position;
    node.frequencyMaskShape = shape;
    node.frequencyMaskSettings.shape = shape;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddSpectrumMathNode(SpectrumMathMode mode, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::SpectrumMath;
    node.position = position;
    node.spectrumMathMode = mode;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMagnitudePhaseNode(MagnitudePhaseMode mode, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MagnitudePhase;
    node.position = position;
    node.magnitudePhaseMode = mode;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddSpectrumAnalyzerNode(SpectrumAnalyzerMode mode, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::SpectrumAnalyzer;
    node.position = position;
    node.spectrumAnalyzerMode = mode;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddPreviewNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Preview;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddChannelSplitNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ChannelSplit;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddChannelCombineNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ChannelCombine;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddOutputNode(Vec2 position, bool makePrimary) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Output;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    if (makePrimary || m_OutputNodeId <= 0) {
        m_OutputNodeId = m_Nodes.back().id;
    }
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddCompositeNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Composite;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}


Node* Graph::EnsureOutputNode() {
    if (Node* existing = FindNode(m_OutputNodeId)) {
        return existing;
    }

    for (Node& node : m_Nodes) {
        if (node.kind == NodeKind::Output) {
            m_OutputNodeId = node.id;
            return &node;
        }
    }

    return AddOutputNode(Vec2{ 520.0f, 120.0f }, true);
}

void Graph::RemoveLayerNode(int layerIndex) {
    std::vector<int> removedNodeIds;
    for (const Node& node : m_Nodes) {
        if (node.kind == NodeKind::Layer && node.layerIndex == layerIndex) {
            removedNodeIds.push_back(node.id);
        }
    }

    m_Nodes.erase(
        std::remove_if(m_Nodes.begin(), m_Nodes.end(), [layerIndex](const Node& node) {
            return node.kind == NodeKind::Layer && node.layerIndex == layerIndex;
        }),
        m_Nodes.end());

    for (Node& node : m_Nodes) {
        if (node.kind == NodeKind::Layer && node.layerIndex > layerIndex) {
            --node.layerIndex;
        }
    }

    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return !FindNode(link.fromNodeId) || !FindNode(link.toNodeId);
        }),
        m_Links.end());

    for (int id : removedNodeIds) {
        m_SelectedNodeIds.erase(
            std::remove(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), id),
            m_SelectedNodeIds.end());
    }
    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
    TouchStructure();
}

Node* Graph::FindNode(int nodeId) {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](const Node& node) {
        return node.id == nodeId;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

const Node* Graph::FindNode(int nodeId) const {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](const Node& node) {
        return node.id == nodeId;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

Node* Graph::FindNodeByLayerIndex(int layerIndex) {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [layerIndex](const Node& node) {
        return node.kind == NodeKind::Layer && node.layerIndex == layerIndex;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

const Node* Graph::FindNodeByLayerIndex(int layerIndex) const {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [layerIndex](const Node& node) {
        return node.kind == NodeKind::Layer && node.layerIndex == layerIndex;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

std::vector<SocketDefinition> Graph::GetSockets(const Node& node, bool visibleOnly) const {
    const auto finalize = [&](std::vector<SocketDefinition> sockets) {
        for (SocketDefinition& socket : sockets) {
            SocketPresentation::NormalizeSocketDefinition(node.kind, socket);
        }
        return sockets;
    };
    if (node.kind == NodeKind::Compound) {
        std::vector<SocketDefinition> sockets;
        auto socketType = [](Stack::NodeMath::LogicalValueType type) {
            using Logical = Stack::NodeMath::LogicalValueType;
            switch (type) {
                case Logical::Mask: return SocketType::Mask;
                case Logical::ScalarField: return SocketType::ScalarField;
                case Logical::Boolean: return SocketType::Boolean;
                case Logical::Integer: return SocketType::Integer;
                case Logical::Scalar: return SocketType::Scalar;
                case Logical::Vector2: return SocketType::Vector2;
                case Logical::Vector3: return SocketType::Vector3;
                case Logical::Vector4: return SocketType::Vector4;
                case Logical::Matrix3: return SocketType::Matrix3;
                case Logical::Matrix4: return SocketType::Matrix4;
                case Logical::Curve1D: return SocketType::Curve;
                case Logical::Coordinate2: return SocketType::Coordinate;
                case Logical::Histogram: return SocketType::Histogram;
                case Logical::Statistics: return SocketType::Statistics;
                case Logical::Metadata: return SocketType::Metadata;
                case Logical::SpecializedHandle: return SocketType::Handle;
                case Logical::Raw: return SocketType::Raw;
                case Logical::Analysis: return SocketType::Analysis;
                case Logical::ColorImage:
                case Logical::DataImage:
                case Logical::Vector2Field:
                case Logical::Vector3Field:
                case Logical::Vector4Field:
                case Logical::ComplexSpectrum:
                case Logical::Lut:
                default:
                    return SocketType::Image;
            }
        };
        for (const Stack::NodeMath::CompoundPortDefinition& port : node.compound.instance.interfaceSnapshot) {
            sockets.push_back({
                port.id,
                node.id,
                port.direction == Stack::NodeMath::PortDirection::Output
                    ? SocketDirection::Output : SocketDirection::Input,
                socketType(port.logicalType),
                port.label,
                port.optional,
                true
            });
            sockets.back().logicalType = port.logicalType;
        }
        return finalize(std::move(sockets));
    }
    if (SupportsDynamicChannelInputs(node)) {
        bool useFourPins = node.kind == NodeKind::Output && m_ForceOutputFourPins;
        if (!useFourPins) {
            for (const Link& link : m_Links) {
                if (link.toNodeId == node.id &&
                    (IsChannelSocketId(link.toSocketId) ||
                     (link.toSocketId == kImageInputSocketId && !ResolveSocketChannel(link.fromNodeId, link.fromSocketId).empty()))) {
                    useFourPins = true;
                    break;
                }
            }
        }

        std::vector<SocketDefinition> sockets;
        auto add = [&](const char* id, SocketDirection direction, SocketType type, const char* label, bool optional, bool visible) {
            if (visibleOnly && !visible) {
                return;
            }
            sockets.push_back(SocketDefinition{ id, node.id, direction, type, label, optional, visible });
        };

        if (useFourPins) {
            add("r", SocketDirection::Input, SocketType::ScalarField, "R", true, true);
            add("g", SocketDirection::Input, SocketType::ScalarField, "G", true, true);
            add("b", SocketDirection::Input, SocketType::ScalarField, "B", true, true);
            add("a", SocketDirection::Input, SocketType::ScalarField, "A", true, true);
            add(kImageInputSocketId, SocketDirection::Input, SocketType::Image, "Image", false, false);
        } else {
            add(kImageInputSocketId, SocketDirection::Input, SocketType::Image, "Image", false, true);
            add("r", SocketDirection::Input, SocketType::ScalarField, "R", true, false);
            add("g", SocketDirection::Input, SocketType::ScalarField, "G", true, false);
            add("b", SocketDirection::Input, SocketType::ScalarField, "B", true, false);
            add("a", SocketDirection::Input, SocketType::ScalarField, "A", true, false);
        }
        if (node.kind == NodeKind::Lut) {
            add(kMaskInputSocketId, SocketDirection::Input, SocketType::Mask, "Mask", true, true);
            add(kImageOutputSocketId, SocketDirection::Output, SocketType::Image, "Image", false, true);
        }
        return finalize(std::move(sockets));
    }

    if (node.kind == NodeKind::DataMath) {
        std::vector<SocketDefinition> sockets;
        auto add = [&](const std::string& id, SocketDirection direction, SocketType type, const std::string& label, bool optional, bool visible) {
            if (visibleOnly && !visible) {
                return;
            }
            sockets.push_back(SocketDefinition{ id, node.id, direction, type, label, optional, visible });
        };

        int highestConnectedInputIndex = 1;
        bool hasBaseLink = false;
        bool hasMaskLink = false;
        for (const Link& link : m_Links) {
            if (link.toNodeId != node.id) {
                continue;
            }
            const int inputIndex = DataMathInputSocketIndex(link.toSocketId);
            if (inputIndex >= 0) {
                highestConnectedInputIndex = std::max(highestConnectedInputIndex, inputIndex);
            } else if (link.toSocketId == kDataMathBaseInputSocketId) {
                hasBaseLink = true;
            } else if (link.toSocketId == kMaskInputSocketId) {
                hasMaskLink = true;
            }
        }

        const bool averageMode = IsDataMathAverageMode(node.dataMathMode);
        const bool scalarAverageMode = node.dataMathMode == DataMathMode::Average;
        const bool imageAverageMode = node.dataMathMode == DataMathMode::ImageAverage;
        int visibleInputCount = 2;
        if (averageMode) {
            visibleInputCount = std::max(3, highestConnectedInputIndex + 2);
        } else if (highestConnectedInputIndex >= 2) {
            visibleInputCount = highestConnectedInputIndex + 1;
        }
        if ((scalarAverageMode && IsAverageMaskPreviewActive(*this, node)) ||
            (imageAverageMode && IsAverageImagePreviewActive(*this, node))) {
            visibleInputCount += 1;
        }
        visibleInputCount = std::clamp(visibleInputCount, 2, kMaxDataMathInputCount);

        for (int inputIndex = 0; inputIndex < visibleInputCount; ++inputIndex) {
            add(
                DataMathInputSocketId(inputIndex),
                SocketDirection::Input,
                scalarAverageMode ? SocketType::ScalarField : SocketType::Image,
                scalarAverageMode ? ScalarInputSocketLabel(inputIndex) : DataMathInputSocketLabel(inputIndex),
                inputIndex != 0,
                true);
        }

        const bool revealMaskAwareSockets =
            hasBaseLink ||
            hasMaskLink;
        if (!scalarAverageMode && !imageAverageMode) {
            add(
                kDataMathBaseInputSocketId,
                SocketDirection::Input,
                SocketType::Image,
                "Base",
                true,
                revealMaskAwareSockets);
            add(
                kMaskInputSocketId,
                SocketDirection::Input,
                SocketType::ScalarField,
                "Mask",
                true,
                revealMaskAwareSockets);
        }
        add(kImageOutputSocketId, SocketDirection::Output, scalarAverageMode ? SocketType::ScalarField : SocketType::Image, scalarAverageMode ? "Scalar Out" : "Data Out", false, true);
        return finalize(std::move(sockets));
    }

    if (node.kind == NodeKind::Mfsr) {
        std::vector<SocketDefinition> sockets;
        auto add = [&](const std::string& id, SocketDirection direction, SocketType type, const std::string& label, bool optional, bool visible) {
            if (visibleOnly && !visible) {
                return;
            }
            sockets.push_back(SocketDefinition{ id, node.id, direction, type, label, optional, visible });
        };

        int highestConnectedInputIndex = 0;
        for (const Link& link : m_Links) {
            if (link.toNodeId != node.id) {
                continue;
            }
            const int inputIndex = MfsrInputSocketIndex(link.toSocketId);
            if (inputIndex >= 0) {
                highestConnectedInputIndex = std::max(highestConnectedInputIndex, inputIndex);
            }
        }

        const int visibleInputCount = std::clamp(highestConnectedInputIndex + 2, 2, kMaxMfsrInputCount);
        for (int inputIndex = 0; inputIndex < kMaxMfsrInputCount; ++inputIndex) {
            add(
                MfsrInputSocketId(inputIndex),
                SocketDirection::Input,
                SocketType::Image,
                MfsrInputSocketLabel(inputIndex),
                inputIndex != 0,
                inputIndex < visibleInputCount);
        }
        add(kImageOutputSocketId, SocketDirection::Output, SocketType::Image, "Image", false, true);
        return finalize(std::move(sockets));
    }

    if (node.kind == NodeKind::ChannelSplit) {
        std::vector<SocketDefinition> sockets = EditorNodeGraphDefinitions::BuildRegisteredSockets(node, visibleOnly);
        bool hasAlpha = true;
        const Link* inputLink = FindAnyInputLink(node.id, kImageInputSocketId);
        if (inputLink) {
            const Node* upstreamNode = FindNode(inputLink->fromNodeId);
            if (upstreamNode && upstreamNode->kind == NodeKind::Image) {
                if (upstreamNode->image.originalChannels < 4) {
                    hasAlpha = false;
                }
            }
        }
        if (!hasAlpha) {
            for (SocketDefinition& socket : sockets) {
                if (socket.id == "a") {
                    socket.label = "A (Generated)";
                }
            }
        }
        return finalize(std::move(sockets));
    }

    return finalize(EditorNodeGraphDefinitions::BuildRegisteredSockets(node, visibleOnly));
}

bool Graph::FindSocket(int nodeId, const std::string& socketId, SocketDefinition* outSocket) const {
    const Node* node = FindNode(nodeId);
    if (!node) {
        return false;
    }
    if (SupportsDynamicChannelInputs(*node) && IsChannelSocketId(socketId)) {
        if (outSocket) {
            *outSocket = SocketDefinition{
                socketId,
                node->id,
                SocketDirection::Input,
                SocketType::ScalarField,
                ChannelLabel(socketId),
                true,
                true
            };
            SocketPresentation::NormalizeSocketDefinition(node->kind, *outSocket);
        }
        return true;
    }
    for (const SocketDefinition& socket : GetSockets(*node)) {
        if (socket.id == socketId) {
            if (outSocket) {
                *outSocket = socket;
            }
            return true;
        }
    }
    return false;
}

std::string Graph::DefaultInputSocket(const Node& node) const {
    if (node.kind == NodeKind::Compound) {
        for (const SocketDefinition& socket : GetSockets(node, false)) {
            if (socket.direction == SocketDirection::Input) return socket.id;
        }
        return {};
    }
    return EditorNodeGraphDefinitions::DefaultInputSocket(node);
}

std::string Graph::DefaultOutputSocket(const Node& node) const {
    if (node.kind == NodeKind::Compound) {
        for (const SocketDefinition& socket : GetSockets(node, false)) {
            if (socket.direction == SocketDirection::Output) return socket.id;
        }
        return {};
    }
    return EditorNodeGraphDefinitions::DefaultOutputSocket(node);
}

std::string Graph::ResolveSocketChannel(int nodeId, const std::string& socketId) const {
    std::unordered_set<int> visited;
    std::function<std::string(int, const std::string&)> resolve = [&](int currentNodeId, const std::string& currentSocketId) -> std::string {
        if (IsChannelSocketId(currentSocketId)) {
            return currentSocketId;
        }
        if (!visited.insert(currentNodeId).second) {
            return {};
        }

        const Node* node = FindNode(currentNodeId);
        if (!node) {
            return {};
        }

        std::string upstreamSocketId;
        if ((node->kind == NodeKind::Layer || node->kind == NodeKind::TechnicalImage) &&
            currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::Lut && currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::RawDetailFusion &&
                   (currentSocketId == kImageOutputSocketId || currentSocketId == kMaskOutputSocketId)) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::HdrMerge && currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kHdrMergeInput1SocketId;
        } else if (node->kind == NodeKind::Mfsr && currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kMfsrReferenceInputSocketId;
        } else if (node->kind == NodeKind::RawDetailAutoMask && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::MaskUtility && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kMaskUtilityInputSocketId;
        } else if (node->kind == NodeKind::MaskCombine && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kMaskCombineInputASocketId;
        } else if (node->kind == NodeKind::ImageToMask && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kImageToMaskInputSocketId;
        } else if (node->kind == NodeKind::DataMath && currentSocketId == kImageOutputSocketId) {
            if (node->dataMathMode == DataMathMode::Average) {
                return {};
            }
            for (int inputIndex = 0; inputIndex < kMaxDataMathInputCount; ++inputIndex) {
                const std::string socketId = DataMathInputSocketId(inputIndex);
                if (FindAnyInputLink(currentNodeId, socketId)) {
                    upstreamSocketId = socketId;
                    break;
                }
            }
        }

        if (upstreamSocketId.empty()) {
            return {};
        }
        const Link* upstream = FindAnyInputLink(currentNodeId, upstreamSocketId);
        return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : std::string();
    };
    return resolve(nodeId, socketId);
}

bool Graph::IsScalarSocketStream(int nodeId, const std::string& socketId) const {
    std::unordered_set<std::string> visiting;
    std::function<bool(int, const std::string&)> resolve = [&](int currentNodeId, const std::string& currentSocketId) -> bool {
        const std::string key = std::to_string(currentNodeId) + ":" + currentSocketId;
        if (!visiting.insert(key).second) {
            return false;
        }

        auto finish = [&](bool result) {
            visiting.erase(key);
            return result;
        };

        const Node* node = FindNode(currentNodeId);
        if (!node) {
            return finish(false);
        }
        SocketDefinition currentSocket;
        if (node->kind == NodeKind::Compound &&
            FindSocket(currentNodeId, currentSocketId, &currentSocket) &&
            currentSocket.direction == SocketDirection::Output &&
            (currentSocket.type == SocketType::Mask ||
             currentSocket.type == SocketType::ScalarField)) {
            return finish(true);
        }
        if (IsChannelSocketId(currentSocketId) || currentSocketId == kMaskOutputSocketId) {
            return finish(true);
        }

        auto inputIsScalar = [&](const char* inputSocketId) {
            const Link* upstream = FindAnyInputLink(currentNodeId, inputSocketId);
            return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : false;
        };

        switch (node->kind) {
            case NodeKind::Layer:
            case NodeKind::Lut:
            case NodeKind::TechnicalImage:
                if (currentSocketId == kImageOutputSocketId) {
                    return finish(inputIsScalar(kImageInputSocketId));
                }
                break;
            case NodeKind::RawDetailFusion:
                if (currentSocketId == kImageOutputSocketId) {
                    return finish(inputIsScalar(kImageInputSocketId));
                }
                if (currentSocketId == kMaskOutputSocketId) {
                    return finish(true);
                }
                break;
            case NodeKind::RawDetailAutoMask:
            case NodeKind::MaskGenerator:
            case NodeKind::MaskCombine:
            case NodeKind::MaskUtility:
            case NodeKind::CustomMask:
            case NodeKind::ImageToMask:
                if (currentSocketId == kMaskOutputSocketId) {
                    return finish(true);
                }
                break;
            case NodeKind::Mix:
            case NodeKind::DataMath:
                if (currentSocketId == kImageOutputSocketId) {
                    if (node->kind == NodeKind::DataMath) {
                        if (node->dataMathMode == DataMathMode::Average) {
                            bool hasAverageInput = false;
                            for (int inputIndex = 0; inputIndex < kMaxDataMathInputCount; ++inputIndex) {
                                if (FindAnyInputLink(currentNodeId, DataMathInputSocketId(inputIndex))) {
                                    hasAverageInput = true;
                                    break;
                                }
                            }
                            return finish(hasAverageInput);
                        }
                        if (node->dataMathMode == DataMathMode::ImageAverage) {
                            return finish(false);
                        }
                    }
                    bool hasInput = false;
                    bool allScalar = true;
                    for (int inputIndex = 0; inputIndex < kMaxDataMathInputCount; ++inputIndex) {
                        const Link* input = FindAnyInputLink(currentNodeId, DataMathInputSocketId(inputIndex));
                        if (!input) {
                            continue;
                        }
                        hasInput = true;
                        allScalar = allScalar && resolve(input->fromNodeId, input->fromSocketId);
                    }
                    if (node->kind == NodeKind::DataMath) {
                        if (const Link* baseInput = FindAnyInputLink(currentNodeId, kDataMathBaseInputSocketId)) {
                            allScalar = allScalar && resolve(baseInput->fromNodeId, baseInput->fromSocketId);
                        }
                        if (const Link* maskInput = FindAnyInputLink(currentNodeId, kMaskInputSocketId)) {
                            allScalar = allScalar && resolve(maskInput->fromNodeId, maskInput->fromSocketId);
                        }
                    }
                    return finish(hasInput && allScalar);
                }
                break;
            case NodeKind::ChannelSplit:
                if (IsChannelSocketId(currentSocketId)) {
                    return finish(true);
                }
                break;
            default:
                break;
        }

        return finish(false);
    };
    return resolve(nodeId, socketId);
}

bool Graph::TryResolveUniformScalarInput(
    int nodeId,
    const std::string& socketId,
    double& value,
    std::string* errorMessage) const {
    const Link* link = FindAnyInputLink(nodeId, socketId);
    if (!link) {
        if (errorMessage) *errorMessage = "The typed input is not connected.";
        return false;
    }
    const Node* source = FindNode(link->fromNodeId);
    if (!source || source->kind != NodeKind::Value ||
        link->fromSocketId != kValueOutputSocketId) {
        if (errorMessage) *errorMessage = "The typed input is not driven by a Value node.";
        return false;
    }
    const Stack::NodeMath::FirstClassValue& typedValue = source->value.value;
    if (typedValue.logicalType != Stack::NodeMath::LogicalValueType::Scalar ||
        typedValue.storage != Stack::NodeMath::ValueStorageClass::Uniform) {
        if (errorMessage) *errorMessage = "The typed input requires a uniform Scalar value.";
        return false;
    }
    if (typedValue.availability != Stack::NodeMath::ValueAvailability::Known) {
        if (errorMessage) {
            *errorMessage = typedValue.message.empty()
                ? "The connected Scalar value is not known."
                : typedValue.message;
        }
        return false;
    }
    const double* scalar = std::get_if<double>(&typedValue.payload);
    if (!scalar) {
        if (errorMessage) *errorMessage = "The connected Scalar payload is invalid.";
        return false;
    }
    value = *scalar;
    if (errorMessage) errorMessage->clear();
    return true;
}

int Graph::ResolveReferenceSourceNodeId(int nodeId, const std::string& socketId) const {
    std::unordered_set<std::string> visited;
    std::function<int(int, const std::string&)> resolve = [&](int currentNodeId, const std::string& currentSocketId) -> int {
        const std::string visitKey =
            std::to_string(currentNodeId) + "\x1f" + currentSocketId;
        if (!visited.insert(visitKey).second) {
            return -1;
        }

        const Node* node = FindNode(currentNodeId);
        if (!node) {
            return -1;
        }

        switch (node->kind) {
            case NodeKind::Image:
            case NodeKind::RawSource:
            case NodeKind::RawDevelopment:
            case NodeKind::ImageGenerator:
            case NodeKind::MaskGenerator:
            case NodeKind::CustomMask:
                return currentNodeId;
            case NodeKind::RawDecode:
            case NodeKind::RawDevelop:
                return currentNodeId;
            case NodeKind::RawNeuralDenoise: {
                const Link* upstream = FindInputLink(currentNodeId, kRawInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Layer:
            case NodeKind::TechnicalImage: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Compound: {
                std::vector<std::string> dependencies;
                std::string dependencyError;
                if (!ResolveCompoundOutputInputDependencies(
                        currentNodeId, currentSocketId, dependencies, &dependencyError)) {
                    return -1;
                }
                for (const std::string& inputSocketId : dependencies) {
                    if (const Link* upstream = FindInputLink(currentNodeId, inputSocketId)) {
                        const int sourceId = resolve(upstream->fromNodeId, upstream->fromSocketId);
                        if (sourceId > 0) return sourceId;
                    }
                }
                for (const SocketDefinition& inputSocket : GetSockets(*node, false)) {
                    if (inputSocket.direction != SocketDirection::Input || !inputSocket.optional) {
                        continue;
                    }
                    if (const Link* upstream = FindInputLink(currentNodeId, inputSocket.id)) {
                        const int sourceId = resolve(upstream->fromNodeId, upstream->fromSocketId);
                        if (sourceId > 0) return sourceId;
                    }
                }
                return -1;
            }
            case NodeKind::Lut: {
                if (const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId)) {
                    return resolve(upstream->fromNodeId, upstream->fromSocketId);
                }
                for (const char* channelSocketId : { "r", "g", "b", "a" }) {
                    if (const Link* channelInput = FindInputLink(currentNodeId, channelSocketId)) {
                        const int sourceId = resolve(channelInput->fromNodeId, channelInput->fromSocketId);
                        if (sourceId > 0) {
                            return sourceId;
                        }
                    }
                }
                return -1;
            }
            case NodeKind::RawDetailFusion: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::HdrMerge: {
                const char* preferredSocketId = kHdrMergeInput1SocketId;
                if (currentSocketId == kHdrMergeInput2SocketId) {
                    preferredSocketId = kHdrMergeInput2SocketId;
                } else if (currentSocketId == kHdrMergeInput3SocketId) {
                    preferredSocketId = kHdrMergeInput3SocketId;
                }
                const Link* upstream = FindInputLink(currentNodeId, preferredSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Mfsr: {
                std::string preferredSocketId = kMfsrReferenceInputSocketId;
                if (IsMfsrInputSocketId(currentSocketId)) {
                    preferredSocketId = currentSocketId;
                }
                const Link* upstream = FindInputLink(currentNodeId, preferredSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::RawDetailAutoMask: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Mix: {
                const Link* inputA = FindInputLink(currentNodeId, kMixInputASocketId);
                const Link* inputB = FindInputLink(currentNodeId, kMixInputBSocketId);
                const int sourceA = inputA ? resolve(inputA->fromNodeId, inputA->fromSocketId) : -1;
                const int sourceB = inputB ? resolve(inputB->fromNodeId, inputB->fromSocketId) : -1;
                auto isGeneratedReference = [&](const int sourceId) {
                    const Node* source = FindNode(sourceId);
                    return source &&
                        (source->kind == NodeKind::ImageGenerator ||
                         source->kind == NodeKind::MaskGenerator ||
                         source->kind == NodeKind::CustomMask);
                };
                if (sourceA > 0 && sourceB > 0 && isGeneratedReference(sourceA) && !isGeneratedReference(sourceB)) {
                    return sourceB;
                }
                return sourceA > 0 ? sourceA : sourceB;
            }
            case NodeKind::DataMath: {
                for (int inputIndex = 0; inputIndex < kMaxDataMathInputCount; ++inputIndex) {
                    if (const Link* input = FindInputLink(currentNodeId, DataMathInputSocketId(inputIndex))) {
                        const int sourceId = resolve(input->fromNodeId, input->fromSocketId);
                        if (sourceId > 0) {
                            return sourceId;
                        }
                    }
                }
                return -1;
            }
            case NodeKind::ChannelSplit: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::ChannelCombine:
            case NodeKind::Output: {
                if (currentSocketId == kImageInputSocketId) {
                    if (const Link* input = FindInputLink(currentNodeId, kImageInputSocketId)) {
                        return resolve(input->fromNodeId, input->fromSocketId);
                    }
                }
                for (const char* channelSocketId : { "r", "g", "b", "a" }) {
                    if (const Link* channelInput = FindInputLink(currentNodeId, channelSocketId)) {
                        const int sourceId = resolve(channelInput->fromNodeId, channelInput->fromSocketId);
                        if (sourceId > 0) return sourceId;
                    }
                }
                return -1;
            }
            case NodeKind::MaskUtility: {
                const Link* upstream = FindInputLink(currentNodeId, kMaskUtilityInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::MaskCombine: {
                if (const Link* inputA = FindInputLink(currentNodeId, kMaskCombineInputASocketId)) {
                    const int sourceId = resolve(inputA->fromNodeId, inputA->fromSocketId);
                    if (sourceId > 0) return sourceId;
                }
                const Link* inputB = FindInputLink(currentNodeId, kMaskCombineInputBSocketId);
                return inputB ? resolve(inputB->fromNodeId, inputB->fromSocketId) : -1;
            }
            case NodeKind::ImageToMask: {
                const Link* upstream = FindInputLink(currentNodeId, kImageToMaskInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Composite:
            case NodeKind::Scope:
            case NodeKind::Preview:
                return -1;
        }
        return -1;
    };
    return resolve(nodeId, socketId);
}

int Graph::ResolveReferenceSourceNodeIdForOutput(int outputNodeId) const {
    const Node* output = FindNode(outputNodeId);
    if (!output || output->kind != NodeKind::Output) {
        return -1;
    }
    return ResolveReferenceSourceNodeId(outputNodeId, kImageInputSocketId);
}

void Graph::ConnectImageToOutput(int nodeId) {
    const Node* node = FindNode(nodeId);
    if (!node || (node->kind != NodeKind::Image &&
                  node->kind != NodeKind::RawDevelopment &&
                  node->kind != NodeKind::RawDecode &&
                  node->kind != NodeKind::RawDevelop &&
                  node->kind != NodeKind::RawDetailFusion &&
                  node->kind != NodeKind::Mfsr &&
                  node->kind != NodeKind::HdrMerge &&
                  node->kind != NodeKind::TechnicalImage &&
                  node->kind != NodeKind::Lut)) {
        return;
    }

    ActivateImageNode(nodeId);
    Node* output = EnsureOutputNode();
    if (output && output->id > 0) {
        TryConnectSockets(nodeId, kImageOutputSocketId, output->id, kImageInputSocketId);
    }
}

void Graph::DisconnectOutput() {
    m_ActiveImageNodeId = -1;
    const std::size_t oldLinkCount = m_Links.size();
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return IsRenderLink(link);
        }),
        m_Links.end());
    if (oldLinkCount != m_Links.size()) {
        TouchStructure();
    }
}

void Graph::RebuildLinks() {
    const std::vector<Link> scopeLinks = [&]() {
        std::vector<Link> links;
        for (const Link& link : m_Links) {
            if (GetLinkRole(link) == LinkRole::Scope) {
                links.push_back(link);
            }
        }
        return links;
    }();

    m_Links = scopeLinks;
    const bool hadNoActiveImage = m_ActiveImageNodeId <= 0 || !FindNode(m_ActiveImageNodeId);

    if (hadNoActiveImage) {
        TouchStructure();
        return;
    }

    std::vector<Node*> layers;
    for (Node& node : m_Nodes) {
        if (node.kind == NodeKind::Layer) {
            layers.push_back(&node);
        }
    }
    std::sort(layers.begin(), layers.end(), [](const Node* a, const Node* b) {
        return a->layerIndex < b->layerIndex;
    });

    int previous = m_ActiveImageNodeId;
    for (const Node* layer : layers) {
        m_Links.push_back(MakeSocketLink(previous, kImageOutputSocketId, layer->id, kImageInputSocketId));
        previous = layer->id;
    }

    Node* output = EnsureOutputNode();
    if (output) {
        m_Links.push_back(MakeSocketLink(previous, kImageOutputSocketId, output->id, kImageInputSocketId));
    }
    TouchStructure();
}

void Graph::TouchStructure() {
    ++m_StructureRevision;
}

int Graph::AllocateNodeId() {
    return m_NextNodeId++;
}

Vec2 Graph::DefaultLayerPosition(int layerIndex) const {
    return Vec2{ 260.0f + static_cast<float>(layerIndex) * 220.0f, 120.0f };
}

NodeGroup* Graph::AddGroup(std::string title, Vec2 position, Vec2 size) {
    NodeGroup group;
    group.id = m_NextGroupId++;
    group.title = title;
    group.position = position;
    group.size = size;
    m_Groups.push_back(std::move(group));
    TouchStructure();
    return &m_Groups.back();
}

bool Graph::RemoveGroup(int groupId) {
    auto it = std::remove_if(m_Groups.begin(), m_Groups.end(), [groupId](const NodeGroup& g) {
        return g.id == groupId;
    });
    if (it != m_Groups.end()) {
        m_Groups.erase(it, m_Groups.end());
        TouchStructure();
        return true;
    }
    return false;
}

NodeGroup* Graph::FindGroup(int groupId) {
    auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [groupId](const NodeGroup& g) {
        return g.id == groupId;
    });
    return it != m_Groups.end() ? &(*it) : nullptr;
}

const NodeGroup* Graph::FindGroup(int groupId) const {
    auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [groupId](const NodeGroup& g) {
        return g.id == groupId;
    });
    return it != m_Groups.end() ? &(*it) : nullptr;
}

namespace {

ScenePathInfo MergeScenePathInfo(ScenePathInfo a, const ScenePathInfo& b) {
    a.sceneReferred = a.sceneReferred || b.sceneReferred;
    a.hasViewTransform = a.hasViewTransform || b.hasViewTransform;
    return a;
}

ScenePathInfo AnalyzeScenePathRecursive(
    const Graph& graph,
    int nodeId,
    std::unordered_set<int>& visiting) {
    if (!visiting.insert(nodeId).second) {
        return {};
    }

    ScenePathInfo state;
    const Node* node = graph.FindNode(nodeId);
    if (!node) {
        visiting.erase(nodeId);
        return state;
    }

    auto mergeInput = [&](const std::string& socketId) {
        if (const Link* input = graph.FindInputLink(nodeId, socketId)) {
            state = MergeScenePathInfo(
                state,
                AnalyzeScenePathRecursive(graph, input->fromNodeId, visiting));
        }
    };

    switch (node->kind) {
        case NodeKind::RawDecode:
        case NodeKind::RawDevelop:
            state.sceneReferred = true;
            mergeInput(kRawInputSocketId);
            break;
        case NodeKind::RawDetailFusion:
            state.sceneReferred = true;
            mergeInput(kImageInputSocketId);
            break;
        case NodeKind::HdrMerge:
            state.sceneReferred = true;
            mergeInput(kHdrMergeInput1SocketId);
            mergeInput(kHdrMergeInput2SocketId);
            mergeInput(kHdrMergeInput3SocketId);
            break;
        case NodeKind::Mfsr:
            state.sceneReferred = true;
            for (int inputIndex = 0; inputIndex < kMaxMfsrInputCount; ++inputIndex) {
                mergeInput(MfsrInputSocketId(inputIndex));
            }
            break;
        case NodeKind::RawDetailAutoMask:
            state.sceneReferred = true;
            mergeInput(kImageInputSocketId);
            break;
        case NodeKind::RawNeuralDenoise:
            mergeInput(kRawInputSocketId);
            break;
        case NodeKind::Layer:
            if (node->layerType == LayerType::ViewTransform) {
                state.hasViewTransform = true;
            }
            mergeInput(kImageInputSocketId);
            break;
        case NodeKind::Lut:
        case NodeKind::Output:
            if (graph.FindInputLink(node->id, kImageInputSocketId)) {
                mergeInput(kImageInputSocketId);
            } else {
                mergeInput("r");
                mergeInput("g");
                mergeInput("b");
                mergeInput("a");
            }
            break;
        case NodeKind::Mix:
            mergeInput(kMixInputASocketId);
            mergeInput(kMixInputBSocketId);
            break;
        case NodeKind::DataMath:
            for (int inputIndex = 0; inputIndex < kMaxDataMathInputCount; ++inputIndex) {
                mergeInput(DataMathInputSocketId(inputIndex));
            }
            mergeInput(kDataMathBaseInputSocketId);
            break;
        case NodeKind::ChannelSplit:
        case NodeKind::FrequencyFft:
        case NodeKind::FrequencyIfft:
        case NodeKind::SpectrumView:
        case NodeKind::SpectrumAnalyzer:
            mergeInput(kImageInputSocketId);
            break;
        case NodeKind::SpectrumMath:
            mergeInput(kMixInputASocketId);
            mergeInput(kMixInputBSocketId);
            mergeInput(kMaskInputSocketId);
            break;
        case NodeKind::MagnitudePhase:
            mergeInput(kImageInputSocketId);
            mergeInput("magnitude");
            mergeInput("phase");
            break;
        case NodeKind::ChannelCombine:
            mergeInput("r");
            mergeInput("g");
            mergeInput("b");
            mergeInput("a");
            break;
        case NodeKind::ImageToMask:
            mergeInput(kImageToMaskInputSocketId);
            break;
        case NodeKind::MaskCombine:
            mergeInput(kMaskCombineInputASocketId);
            mergeInput(kMaskCombineInputBSocketId);
            break;
        case NodeKind::MaskUtility:
            mergeInput(kMaskUtilityInputSocketId);
            break;
        case NodeKind::Image:
        case NodeKind::RawSource:
        case NodeKind::ImageGenerator:
        case NodeKind::MaskGenerator:
        case NodeKind::FrequencyMask:
        case NodeKind::CustomMask:
        case NodeKind::Composite:
        case NodeKind::Scope:
        case NodeKind::Preview:
            break;
    }

    visiting.erase(nodeId);
    return state;
}

} // namespace

ScenePathInfo AnalyzeScenePath(const Graph& graph, int nodeId) {
    std::unordered_set<int> visiting;
    return AnalyzeScenePathRecursive(graph, nodeId, visiting);
}

} // namespace EditorNodeGraph
