#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace EditorNodeGraph {

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
    Node* AddPreviewNode(Vec2 position);
    Node* AddChannelSplitNode(Vec2 position);
    Node* AddChannelCombineNode(Vec2 position);
    Node* AddOutputNode(Vec2 position, bool makePrimary = false);
    Node* AddCompositeNode(Vec2 position);
    Node* EnsureOutputNode();
    void RemoveLayerNode(int layerIndex);

    Node* FindNode(int nodeId);
    const Node* FindNode(int nodeId) const;
    Node* FindNodeByLayerIndex(int layerIndex);
    const Node* FindNodeByLayerIndex(int layerIndex) const;

    std::vector<Node>& GetNodes() { return m_Nodes; }
    const std::vector<Node>& GetNodes() const { return m_Nodes; }
    const std::vector<Link>& GetLinks() const { return m_Links; }
    std::vector<Link>& GetLinks() { return m_Links; }

    NodeGroup* AddGroup(std::string title, Vec2 position, Vec2 size);
    bool RemoveGroup(int groupId);
    NodeGroup* FindGroup(int groupId);
    const NodeGroup* FindGroup(int groupId) const;
    std::vector<NodeGroup>& GetGroups() { return m_Groups; }
    const std::vector<NodeGroup>& GetGroups() const { return m_Groups; }

    void SelectNode(int nodeId, bool additive = false);
    int GetSelectedNodeId() const { return m_SelectedNodeId; }
    bool IsNodeSelected(int nodeId) const;
    void ClearSelection();
    void SetForceOutputFourPins(bool force) { m_ForceOutputFourPins = force; }
    bool IsOutputFourPinsForced() const { return m_ForceOutputFourPins; }
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
    bool CanConnectSocketsOrInsertExtractor(
        int fromNodeId,
        const std::string& fromSocketId,
        int toNodeId,
        const std::string& toSocketId) const;
    bool TryConnectSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage = nullptr);
    bool SetOutputNodeEnabled(int nodeId, bool enabled);
    bool RemoveNode(int nodeId);
    bool RemoveLink(int fromNodeId, int toNodeId);
    bool RemoveLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId);
    bool RemoveSelectedLink();
    bool HasLink(int fromNodeId, int toNodeId) const;
    bool HasLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const;
    int GetActiveImageNodeId() const { return m_ActiveImageNodeId; }
    void SetActiveImageNodeId(int nodeId) { m_ActiveImageNodeId = nodeId; }
    bool IsOutputConnected() const;
    std::vector<int> GetOutputNodeIds() const;
    std::vector<int> GetConnectedOutputNodeIds() const;
    std::vector<CompletedChainInfo> GetCompletedChains() const;
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
    int ResolveReferenceSourceNodeId(int nodeId, const std::string& socketId) const;
    int ResolveReferenceSourceNodeIdForOutput(int outputNodeId) const;
    const Link* FindInputLink(int nodeId, const std::string& socketId = kImageInputSocketId) const;
    const Link* FindAnyInputLink(int nodeId, const std::string& socketId) const;
    const Link* FindOutputLink(int nodeId, const std::string& socketId = kImageOutputSocketId) const;
    const Link* FindScopeInputLink(int nodeId) const;
    bool IsRenderLink(const Link& link) const;

private:
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
    int m_NextNodeId = 1;
    int m_NextGroupId = 1;
    int m_SelectedNodeId = -1;
    std::vector<int> m_SelectedNodeIds;
    Link m_SelectedLink;
    bool m_HasSelectedLink = false;
    int m_ActiveImageNodeId = -1;
    int m_OutputNodeId = -1;
    bool m_ForceOutputFourPins = false;
    int m_SocketPreviewNodeId = -1;
    SocketPreviewIntent m_SocketPreviewIntent = SocketPreviewIntent::None;
    mutable std::uint64_t m_StructureRevision = 1;
    mutable std::uint64_t m_CompletedChainsCacheRevision = 0;
    mutable std::vector<CompletedChainInfo> m_CompletedChainsCache;
};

} // namespace EditorNodeGraph
