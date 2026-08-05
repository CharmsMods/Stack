#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace EditorNodeGraph {

struct CompoundExpansionResult {
    bool success = false;
    std::string error;
    std::vector<int> expandedNodeIds;
    std::vector<int> authoredCompoundNodeIds;
};

struct GraphLookupCache;

class Graph {
public:
    void Clear();
    void ResetFromLayers(int layerCount, bool hasActiveImage);
    void SyncLayerNodes(int layerCount);

    Node* AddImageNode(ImagePayload payload, Vec2 position);
    Node* AddRawSourceNode(RawSourcePayload payload, Vec2 position);
    Node* AddRawDevelopmentNode(RawDevelopmentPayload payload, Vec2 position);
    Node* AddRawNeuralDenoiseNode(RawNeuralDenoisePayload payload, Vec2 position);
    Node* AddRawDecodeNode(RawDecodePayload payload, Vec2 position);
    Node* AddRawDevelopNode(RawDevelopPayload payload, Vec2 position);
    Node* AddRawDetailAutoMaskNode(RawDetailAutoMaskPayload payload, Vec2 position);
    Node* AddRawDetailFusionNode(RawDetailFusionPayload payload, Vec2 position);
    Node* AddHdrMergeNode(HdrMergePayload payload, Vec2 position);
    Node* AddMfsrNode(MfsrPayload payload, Vec2 position);
    Node* AddRawProjectFrameNode(RawProjectFramePayload payload, Vec2 position);
    Node* AddMultiFrameDenoiseNode(MultiFrameDenoisePayload payload, Vec2 position);
    Node* AddRawProjectSourceSetNode(RawProjectSourceSetPayload payload, Vec2 position);
    Node* AddLutNode(LutPayload payload, Vec2 position);
    Node* AddLayerNode(LayerType type, int layerIndex, Vec2 position);
    Node* AddScopeNode(ScopeKind scopeKind, Vec2 position);
    Node* AddMaskGeneratorNode(MaskGeneratorKind maskKind, Vec2 position);
    Node* AddMaskCombineNode(MaskCombineMode combineMode, Vec2 position);
    Node* AddMaskUtilityNode(MaskUtilityKind utilityKind, Vec2 position);
    Node* AddCustomMaskNode(CustomMaskPayload payload, Vec2 position);
    Node* AddImageToMaskNode(ImageToMaskKind converterKind, Vec2 position);
    Node* AddImageGeneratorNode(ImageGeneratorKind generatorKind, Vec2 position);
    Node* AddMixNode(Vec2 position);
    Node* AddDataMathNode(DataMathMode mode, Vec2 position);
    Node* AddValueNode(Stack::NodeMath::FirstClassValue value, Vec2 position);
    Node* AddFieldMeanNode(Vec2 position);
    Node* AddReformatNode(Vec2 position);
    Node* AddTechnicalImageNode(Stack::NodeMath::TechnicalImageOperation operation, Vec2 position);
    Node* AddCompoundNode(const Stack::NodeMath::DefinitionReference& definition, Vec2 position);
    Node* AddFrequencyFilterNode(FrequencyFilterMode mode, Vec2 position);
    Node* AddFrequencyResponseNode(Vec2 position);
    Node* AddFrequencyFftNode(Vec2 position);
    Node* AddFrequencyIfftNode(Vec2 position);
    Node* AddSpectrumViewNode(Vec2 position);
    Node* AddApplyFrequencyResponseNode(Vec2 position);
    Node* AddCombineSpectraNode(Vec2 position);
    Node* AddSpectrumSeparateNode(Vec2 position);
    Node* AddSpectrumRecombineNode(Vec2 position);
    Node* AddFrequencyMaskNode(FrequencyMaskShape shape, Vec2 position);
    Node* AddSpectrumMathNode(SpectrumMathMode mode, Vec2 position);
    Node* AddMagnitudePhaseNode(MagnitudePhaseMode mode, Vec2 position);
    Node* AddSpectrumAnalyzerNode(SpectrumAnalyzerMode mode, Vec2 position);
    Node* AddPreviewNode(Vec2 position);
    Node* AddChannelSplitNode(Vec2 position);
    Node* AddChannelCombineNode(Vec2 position);
    Node* AddConstantChannelNode(Vec2 position);
    Node* AddOutputNode(Vec2 position, bool makePrimary = false);
    Node* AddCompositeNode(Vec2 position);
    Node* EnsureOutputNode();
    void RemoveLayerNode(int layerIndex);

    Node* FindNode(int nodeId);
    const Node* FindNode(int nodeId) const;
    Node* FindNodeByLayerIndex(int layerIndex);
    const Node* FindNodeByLayerIndex(int layerIndex) const;

    // Mutable node access is for payload/layout edits. Call EditNodes() before
    // changing the vector's membership or node IDs.
    std::vector<Node>& GetNodes() { return m_Nodes; }
    const std::vector<Node>& GetNodes() const { return m_Nodes; }
    std::vector<Node>& EditNodes() {
        TouchStructure();
        return m_Nodes;
    }
    const std::vector<Link>& GetLinks() const { return m_Links; }
    std::vector<Link>& EditLinks() {
        TouchStructure();
        return m_Links;
    }

    NodeGroup* AddGroup(std::string title, Vec2 position, Vec2 size);
    bool RemoveGroup(int groupId);
    NodeGroup* FindGroup(int groupId);
    const NodeGroup* FindGroup(int groupId) const;
    std::vector<NodeGroup>& GetGroups() { return m_Groups; }
    const std::vector<NodeGroup>& GetGroups() const { return m_Groups; }

    bool AddCompoundDefinition(Stack::NodeMath::CompoundDefinition definition, std::string* error = nullptr);
    const Stack::NodeMath::CompoundDefinition* FindCompoundDefinition(
        const Stack::NodeMath::DefinitionReference& reference) const;
    Stack::NodeMath::CompoundDefinition* FindCompoundDefinition(
        const Stack::NodeMath::DefinitionReference& reference);
    std::vector<Stack::NodeMath::CompoundDefinition>& GetCompoundDefinitions() { return m_CompoundDefinitions; }
    const std::vector<Stack::NodeMath::CompoundDefinition>& GetCompoundDefinitions() const { return m_CompoundDefinitions; }
    bool ResolveCompoundNode(int nodeId);
    bool MakeCompoundNodeUnique(int nodeId, std::string* error = nullptr);
    bool UpdateCompoundNodeDefinition(
        int nodeId,
        const Stack::NodeMath::DefinitionReference& definition,
        std::string* error = nullptr);
    bool UnpackCompoundNode(int nodeId, std::vector<int>* unpackedNodeIds = nullptr, std::string* error = nullptr);
    bool ExpandAllCompoundNodes(Graph& expanded, CompoundExpansionResult* result = nullptr) const;
    bool CreateCompoundFromSelection(
        const std::vector<int>& nodeIds,
        const std::string& label,
        int* compoundNodeId = nullptr,
        std::string* error = nullptr);

    void SelectNode(int nodeId, bool additive = false);
    int GetSelectedNodeId() const { return m_SelectedNodeId; }
    bool IsNodeSelected(int nodeId) const;
    void ClearSelection();
    void SetSocketPreviewIntent(int nodeId, SocketPreviewIntent intent) {
        m_SocketPreviewNodeId = nodeId;
        m_SocketPreviewIntent = intent;
    }
    SocketPreviewIntent GetSocketPreviewIntent(int nodeId) const {
        return m_SocketPreviewNodeId == nodeId ? m_SocketPreviewIntent : SocketPreviewIntent::None;
    }
    void SelectNodesInRect(Vec2 min, Vec2 max, bool additive = false);
    void SelectNodesInRect(
        Vec2 min,
        Vec2 max,
        const std::function<Vec2(const Node&)>& sizeResolver,
        bool additive = false);
    void SelectNodesInBounds(
        Vec2 min,
        Vec2 max,
        const std::function<GraphRect(const Node&)>& boundsResolver,
        bool additive = false);
    const std::vector<int>& GetSelectedNodeIds() const { return m_SelectedNodeIds; }
    void SelectLink(int fromNodeId, int toNodeId);
    void SelectLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId);
    void ClearSelectedLink();
    const Link* GetSelectedLink() const;
    bool HasSelectedLink() const;

    void ConnectImageToOutput(int nodeId);
    void DisconnectOutput();
    bool TryConnect(int fromNodeId, int toNodeId, std::string* errorMessage = nullptr);
    bool CanConnectSockets(
        int fromNodeId,
        const std::string& fromSocketId,
        int toNodeId,
        const std::string& toSocketId,
        std::string* normalizedToSocketId = nullptr,
        std::string* errorMessage = nullptr) const;
    bool IsScalarTargetSocket(int nodeId, const std::string& socketId) const;
    bool CanInsertImageToScalarExtractor(
        int fromNodeId,
        const std::string& fromSocketId,
        int toNodeId,
        const std::string& toSocketId,
        std::string* errorMessage = nullptr) const;
    bool TryConnectSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage = nullptr);
    bool SetOutputNodeEnabled(int nodeId, bool enabled);
    bool SetParameterExposed(int nodeId, const std::string& parameterId, bool exposed);
    bool SetDataMathMode(int nodeId, DataMathMode mode);
    bool SetMaskCombineMode(int nodeId, MaskCombineMode mode);
    bool SetImageToMaskKind(int nodeId, ImageToMaskKind kind);
    bool SetLayerNodeType(int nodeId, LayerType type);
    bool RemoveNode(int nodeId);
    bool RemoveLink(int fromNodeId, int toNodeId);
    bool RemoveLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId);
    bool RemoveSelectedLink();
    bool HasLink(int fromNodeId, int toNodeId) const;
    bool HasLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const;
    int GetActiveImageNodeId() const { return m_ActiveImageNodeId; }
    void SetActiveImageNodeId(int nodeId) { m_ActiveImageNodeId = nodeId; }
    bool IsOutputConnected() const;
    bool IsOutputChannelInspection(int outputNodeId) const;
    std::string GetOutputConnectionDiagnostic() const;
    std::vector<int> GetOutputNodeIds() const;
    std::vector<int> GetConnectedOutputNodeIds() const;
    const std::vector<CompletedChainInfo>& GetCompletedChains() const;
    std::vector<int> GetDownstreamRenderNodeIds(int nodeId) const;
    std::vector<int> GetDownstreamOutputNodeIds(int nodeId) const;
    int FindAdjacentMainChainNodeId(int nodeId, int direction) const;
    int ResolvePreviewOutputNodeId() const;
    std::uint64_t GetStructureRevision() const { return m_StructureRevision; }

    int GetOutputNodeId() const { return m_OutputNodeId; }
    void SetOutputNodeId(int nodeId) { m_OutputNodeId = nodeId; }
    int GetNextNodeId() const { return m_NextNodeId; }
    void SetNextNodeId(int nextNodeId) { m_NextNodeId = nextNodeId; }
    int GetNextGroupId() const { return m_NextGroupId; }
    void SetNextGroupId(int nextGroupId) { m_NextGroupId = nextGroupId; }
    void SetAllowNoOutput(bool allow) { m_AllowNoOutput = allow; }
    bool AllowsNoOutput() const { return m_AllowNoOutput; }
    void RebuildLinks();
    void AutoLayout();
    ValidationResult Validate() const;
    std::vector<int> GetRenderLayerNodePath() const;
    std::vector<int> GetRenderLayerNodePath(int outputNodeId) const;
    std::vector<int> GetRenderLayerIndexPath() const;
    std::vector<int> GetRenderLayerIndexPath(int outputNodeId) const;
    LinkRole GetLinkRole(const Link& link) const;
    bool IsRenderChainNode(const Node& node) const;
    std::vector<SocketDefinition> GetSockets(const Node& node, bool visibleOnly = false) const;
    bool FindSocket(int nodeId, const std::string& socketId, SocketDefinition* outSocket = nullptr) const;
    std::string DefaultInputSocket(const Node& node) const;
    std::string DefaultOutputSocket(const Node& node) const;
    std::string ResolveSocketChannel(int nodeId, const std::string& socketId) const;
    bool IsScalarSocketStream(int nodeId, const std::string& socketId) const;
    bool TryResolveUniformScalarInput(
        int nodeId,
        const std::string& socketId,
        double& value,
        std::string* errorMessage = nullptr) const;
    bool ResolveCompoundOutputInputDependencies(
        int nodeId,
        const std::string& outputSocketId,
        std::vector<std::string>& inputSocketIds,
        std::string* errorMessage = nullptr) const;
    int ResolveReferenceSourceNodeId(int nodeId, const std::string& socketId) const;
    int ResolveReferenceSourceNodeIdForOutput(int outputNodeId) const;
    const Link* FindInputLink(int nodeId, const std::string& socketId = kImageInputSocketId) const;
    const Link* FindAnyInputLink(int nodeId, const std::string& socketId) const;
    const Link* FindOutputLink(int nodeId, const std::string& socketId = kImageOutputSocketId) const;
    const Link* FindScopeInputLink(int nodeId) const;
    void ForEachIncomingLink(
        int nodeId,
        const std::function<void(const Link&)>& visitor) const;
    void ForEachOutgoingLink(
        int nodeId,
        const std::function<void(const Link&)>& visitor) const;
    void ForEachIncomingRenderLink(
        int nodeId,
        const std::function<void(const Link&)>& visitor) const;
    void ForEachOutgoingRenderLink(
        int nodeId,
        const std::function<void(const Link&)>& visitor) const;
    bool IsRenderLink(const Link& link) const;

private:
    bool EnsureLookupCache() const;
    int AllocateNodeId();
    void TouchStructure();
    Vec2 DefaultLayerPosition(int layerIndex) const;
    bool WouldCreateCycle(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const;
    void RemoveRenderLinksForNodeInput(int nodeId, const std::string& socketId);
    void RemoveRenderLinksForNodeOutput(int nodeId, const std::string& socketId);
    void RemoveScopeLinksForNodeInput(int nodeId, const std::string& socketId);
    void RemoveLinksForNodeInput(int nodeId, const std::string& socketId);
    void ActivateImageNode(int nodeId);
    void RefreshPrimaryOutputNode();

    std::vector<Node> m_Nodes;
    std::vector<Link> m_Links;
    std::vector<NodeGroup> m_Groups;
    std::vector<Stack::NodeMath::CompoundDefinition> m_CompoundDefinitions;
    int m_NextNodeId = 1;
    int m_NextGroupId = 1;
    int m_SelectedNodeId = -1;
    std::vector<int> m_SelectedNodeIds;
    Link m_SelectedLink;
    bool m_HasSelectedLink = false;
    int m_ActiveImageNodeId = -1;
    int m_OutputNodeId = -1;
    bool m_AllowNoOutput = false;
    int m_SocketPreviewNodeId = -1;
    SocketPreviewIntent m_SocketPreviewIntent = SocketPreviewIntent::None;
    mutable std::uint64_t m_StructureRevision = 1;
    mutable std::uint64_t m_CompletedChainsCacheRevision = 0;
    mutable std::vector<CompletedChainInfo> m_CompletedChainsCache;
    mutable std::string m_OutputConnectionDiagnosticCache;
    mutable std::shared_ptr<const GraphLookupCache> m_LookupCache;
};

struct ScenePathInfo {
    bool sceneReferred = false;
    bool hasViewTransform = false;
};

// Determines whether the upstream path carries scene-linear/HDR image data and
// whether it has already been mapped through a display transform. Image-editing
// nodes such as Tone Curve inherit this state from their image input.
ScenePathInfo AnalyzeScenePath(const Graph& graph, int nodeId);

} // namespace EditorNodeGraph
