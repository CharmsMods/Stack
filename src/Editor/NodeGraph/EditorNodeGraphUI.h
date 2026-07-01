#pragma once

#include "EditorNodeGraph.h"
#include "ThirdParty/json.hpp"
#include <imgui.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EditorModule;
struct PresetEntry;

class EditorNodeGraphUI {
public:
    ~EditorNodeGraphUI();
    void Initialize();
    void Render(EditorModule* editor);
    bool IsNodeBrowserOpen() const { return m_DrawerMode == DrawerMode::NodeBrowser; }
    bool HasDrawerOpen() const { return m_DrawerMode != DrawerMode::None; }
    bool IsGraphMiddlePanActive() const { return m_MiddlePanCaptureActive; }
    void CloseTransientDrawers();
    void RenderNodesPanelDrawer(
        EditorModule* editor,
        float panelWidth,
        float targetPanelWidth,
        float paneHeight,
        const ImVec2& workspacePos);
    void RenderPresetsPanel(EditorModule* editor, float availableWidth);
    void RenderPresetPreviewPane(EditorModule* editor, const ImVec2& availableSize);
    void SetPresetPreviewHoverTarget(const std::shared_ptr<PresetEntry>& preset);
    bool ApplyPresetPayload(EditorModule* editor, const nlohmann::json& graphPayload, std::string* outSummary);

    static bool ConnectOutputToBestInput(EditorModule* editor, int fromNodeId, const std::string& fromSocketId, int toNodeId);
    static bool ConnectBestOutputToInput(EditorModule* editor, int fromNodeId, int toNodeId, const std::string& toSocketId);
    static std::string GetUpstreamChannel(const EditorNodeGraph::Graph& graph, int nodeId, const std::string& socketId, std::unordered_set<int>& visited);


private:
    enum class ContextTarget {
        Canvas,
        Node,
        Link
    };

    enum class NodeBrowserMode {
        GeneralAdd,
        ConnectFromOutput,
        ConnectFromInput
    };

    enum class DrawerMode {
        None,
        NodeBrowser
    };

    enum class GraphMouseOwner {
        None,
        Canvas,
        NodeFrame,
        NodeHeader,
        NodeContent,
        InputPin,
        OutputPin,
        Link,
        Popup
    };

    struct SocketHit {
        int nodeId = -1;
        std::string socketId;
        bool IsValid() const { return nodeId > 0 && !socketId.empty(); }
    };

    struct CachedRect {
        ImVec2 min;
        ImVec2 max;

        bool IsValid() const { return max.x > min.x && max.y > min.y; }
        bool Contains(const ImVec2& point) const {
            return point.x >= min.x && point.x <= max.x &&
                   point.y >= min.y && point.y <= max.y;
        }
    };

    struct SocketAnchor {
        std::string socketId;
        EditorNodeGraph::SocketDirection direction = EditorNodeGraph::SocketDirection::Input;
        ImVec2 screenPos;
    };

    struct NodeLayoutCache {
        CachedRect frameRect;
        CachedRect headerRect;
        CachedRect contentRect;
        CachedRect contentUsedRect;
        std::vector<SocketAnchor> socketAnchors;
    };

    struct GraphRenderOptions {
        bool interactive = true;
        bool syncEditorViewTransform = true;
        bool allowDropTarget = true;
        bool showValidation = true;
        bool showContextMenu = true;
        bool showNodeBrowser = true;
        bool showZoomDial = true;
    };

    struct PreviewGraphCacheEntry {
        std::string revisionToken;
        EditorNodeGraph::Graph graph;
        std::vector<std::shared_ptr<LayerBase>> layers;
        std::string error;
        bool loadAttempted = false;
    };

    EditorNodeGraph::Vec2 ScreenToGraph(const EditorNodeGraph::Vec2& screen) const;
    EditorNodeGraph::Vec2 GraphToScreen(const EditorNodeGraph::Vec2& graph) const;
    EditorNodeGraph::Vec2 NodeSize(const EditorNodeGraph::Node& node) const;
    EditorNodeGraph::Vec2 NodeScreenSize(const EditorNodeGraph::Node& node) const;
    EditorNodeGraph::Vec2 NodeViewportSizePx(const EditorNodeGraph::Node& node) const;
    EditorNodeGraph::Vec2 NodeGraphFootprintSize(const EditorNodeGraph::Node& node) const;
    bool UsesFixedNodeViewport() const;
    float NodeContentScale() const;
    float NodePinRadius() const;
    void ZoomAtMouse(float wheel);
    void ClampPanToContent(const EditorNodeGraph::Graph& graph);
    void StopMiddlePanCapture();
    bool UpdateMiddlePanCapture(EditorModule* editor, bool graphHovered, bool allowStart);

    void RenderContextMenu(EditorModule* editor);
    void RenderNode(EditorModule* editor, EditorNodeGraph::Node& node);
    void RenderGroups(EditorModule* editor, EditorNodeGraph::Graph& graph);
    void RenderLinks(const EditorNodeGraph::Graph& graph);
    void RenderPendingOutputLinkDrag(EditorModule* editor, const EditorNodeGraph::Graph& graph, const SocketHit& hoveredInput);
    void RenderPendingInputLinkDrag(EditorModule* editor, const EditorNodeGraph::Graph& graph, const SocketHit& hoveredOutput);
    void RenderInteraction(EditorModule* editor, const EditorNodeGraph::Graph& graph);
    void RenderNodeBrowser(EditorModule* editor);
    void RenderValidationStatus(const EditorNodeGraph::Graph& graph);
    void RenderGraphZoomDial(
        EditorModule* editor,
        ImDrawList* drawList,
        const ImVec2& canvasMin,
        const ImVec2& canvasMax,
        const ImVec2& canvasSize) const;
    void RenderInteractionDebugOverlay(const EditorNodeGraph::Graph& graph, int hoveredNodeId, GraphMouseOwner owner);
    void RenderChannelSplitConfirmPrompt(EditorModule* editor);
    void RenderGraphCanvas(
        EditorModule* editor,
        EditorNodeGraph::Graph& graph,
        const ImVec2& canvasMin,
        const ImVec2& canvasMax,
        const GraphRenderOptions& options);
    void RenderStaticGraphPreview(
        EditorModule* editor,
        EditorNodeGraph::Graph& graph,
        std::vector<std::shared_ptr<LayerBase>>* layers,
        const ImVec2& canvasMin,
        const ImVec2& canvasMax,
        float opacity);
public:
    EditorNodeGraph::Graph& GetActiveGraph(EditorModule* editor) const;
    std::vector<std::shared_ptr<LayerBase>>* GetActiveLayers(EditorModule* editor) const;
    const std::vector<std::shared_ptr<LayerBase>>* GetActiveLayers(const EditorModule* editor) const;
    NodeSurfaceSpec ResolveLayerSurfaceSpec(const EditorModule* editor, int layerIndex) const;
    bool ResolveNodeUsesSidebarOnlyComplexEditor(const EditorModule* editor, const EditorNodeGraph::Node& node) const;
    bool ResolveNodeHasDedicatedComplexEditor(const EditorModule* editor, const EditorNodeGraph::Node& node) const;
    bool ResolveLayerUsesRichNodeSurface(const EditorModule* editor, int layerIndex) const;
    void FitGraphPreviewToCanvas(EditorModule* editor, const EditorNodeGraph::Graph& graph, const ImVec2& canvasSize);
    PreviewGraphCacheEntry* GetPresetPreviewGraphCacheEntry(const std::string& presetId);
    const PreviewGraphCacheEntry* GetPresetPreviewGraphCacheEntry(const std::string& presetId) const;
    bool EnsurePresetPreviewGraphLoaded(const PresetEntry& preset);
private:
    bool IsGraphCanvasHovered() const;
    bool CanOpenChannelSplitConfirm(const EditorNodeGraph::Graph& graph, int nodeId) const;
    void CancelChannelSplitConfirm();
    GraphMouseOwner ResolveMouseOwner(
        const EditorNodeGraph::Graph& graph,
        bool graphHovered,
        const SocketHit& hoveredInput,
        const SocketHit& hoveredOutput,
        int hoveredNodeId,
        const EditorNodeGraph::Link& hoveredLink) const;
    EditorNodeGraph::Vec2 InputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const;
    EditorNodeGraph::Vec2 OutputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const;
    SocketHit FindInputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos);
    SocketHit FindOutputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos);
    int FindNodeAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos);
    EditorNodeGraph::Link FindLinkAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos);
    bool IsPointNearLink(const EditorNodeGraph::Vec2& point, const EditorNodeGraph::Vec2& a, const EditorNodeGraph::Vec2& b) const;
    unsigned int GetImagePreviewTexture(const EditorNodeGraph::Node& node);
    unsigned int GetGraphPreviewTexture(EditorModule* editor, const EditorNodeGraph::Node& node);
    unsigned int GetNodeBrowserThumbnailTexture(
        EditorModule* editor,
        const std::string& previewKey,
        ImVec2* outSize,
        bool* outPending,
        bool* outFallback);
    unsigned int UploadPreviewTexture(int nodeId, const std::vector<unsigned char>& pixels, int width, int height);
    void ResetPerGraphVisualCaches();
    void SyncPerGraphVisualCaches(const EditorNodeGraph::Graph& graph);
    NodeLayoutCache BuildNodeLayoutCache(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) const;
    void RefreshNodeLayoutCache(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node);
    const NodeLayoutCache* FindNodeLayoutCache(int nodeId) const;
    void RefreshNodeLookupCache(const EditorNodeGraph::Graph& graph, bool force = false);
    EditorNodeGraph::Node* FindCachedNode(EditorNodeGraph::Graph& graph, int nodeId);
    const EditorNodeGraph::Node* FindCachedNode(const EditorNodeGraph::Graph& graph, int nodeId);
    void RefreshNodeOrderCache(const EditorNodeGraph::Graph& graph);
    const std::vector<int>& GetNodeRenderOrder(const EditorNodeGraph::Graph& graph);
    const std::vector<int>& GetNodeHitTestOrder(const EditorNodeGraph::Graph& graph);
    void TouchNodeFront(int nodeId);
    const SocketAnchor* FindSocketAnchor(
        const NodeLayoutCache& cache,
        const std::string& socketId,
        EditorNodeGraph::SocketDirection direction) const;
    bool IsPointInNodeHeader(int nodeId, const ImVec2& point) const;
    bool IsPointInNodeDraggableRegion(int nodeId, const ImVec2& point) const;
    void DrawClippedText(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color) const;
    void OpenNodeBrowser(NodeBrowserMode mode, const EditorNodeGraph::Vec2& graphPos);
    void CloseNodeBrowser();

    EditorNodeGraph::Vec2 m_CanvasOrigin;
    EditorNodeGraph::Vec2 m_Pan = { 40.0f, 40.0f };
    float m_Zoom = 1.0f;
    float m_ZoomTarget = 1.0f;
    bool m_SmoothZoomActive = false;
    EditorNodeGraph::Vec2 m_SmoothZoomFocusScreen {};
    EditorNodeGraph::Vec2 m_SmoothZoomFocusGraph {};
    bool m_MiddlePanCaptureActive = false;
    int m_MiddlePanSuppressDeltaFrames = 0;
    int m_MiddlePanLastUpdateFrame = -1;
    ImVec2 m_MiddlePanAnchorScreenPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_MiddlePanRestoreScreenPos = ImVec2(0.0f, 0.0f);
    EditorNodeGraph::Vec2 m_ContextGraphPos;
    ContextTarget m_ContextTarget = ContextTarget::Canvas;
    int m_ContextNodeId = -1;
    EditorNodeGraph::Link m_ContextLink;
    int m_DragOutputNodeId = -1;
    std::string m_DragOutputSocketId;
    int m_DragInputNodeId = -1;
    std::string m_DragInputSocketId;
    int m_DragNodeId = -1;
    int m_HoveredInputNodeId = -1;
    std::string m_HoveredInputSocketId;
    int m_HoveredOutputNodeId = -1;
    std::string m_HoveredOutputSocketId;
    bool m_NodeContentActive = false;
    bool m_NodeContentHovered = false;
    bool m_BoxSelecting = false;
    bool m_DebugInteractionOverlay = false;
    bool m_GraphInteractionBlocked = false;
    GraphMouseOwner m_MouseOwner = GraphMouseOwner::None;
    ImGuiID m_LastNodeControlId = 0;
    EditorNodeGraph::Vec2 m_BoxSelectStart;
    EditorNodeGraph::Vec2 m_BoxSelectCurrent;
    EditorNodeGraph::Vec2 m_CanvasMin;
    EditorNodeGraph::Vec2 m_CanvasMax;
    std::string m_StatusMessage;
    char m_SearchBuffer[128] = {};
    char m_NodeBrowserSearchBuffer[128] = {};
    DrawerMode m_DrawerMode = DrawerMode::None;
    bool m_NodeBrowserFocusSearch = false;
    bool m_NodeBrowserThumbnailCatalogEnsured = false;
    bool m_NodeBrowserFilterCacheValid = false;
    double m_NodeBrowserOpenedAt = 0.0;
    double m_NodeBrowserLastFrameTime = 0.0;
    float m_NodeBrowserDrawerAlpha = 0.0f;
    float m_NodeBrowserStablePanelWidth = 0.0f;
    NodeBrowserMode m_NodeBrowserMode = NodeBrowserMode::GeneralAdd;
    NodeBrowserMode m_NodeBrowserFilterCacheMode = NodeBrowserMode::GeneralAdd;
    EditorNodeGraph::Vec2 m_NodeBrowserGraphPos {};
    EditorNodeGraph::Vec2 m_NodeBrowserScreenPos {};
    int m_NodeBrowserDragFromNodeId = -1;
    std::string m_NodeBrowserDragFromSocketId;
    int m_NodeBrowserDragToNodeId = -1;
    std::string m_NodeBrowserDragToSocketId;
    int m_NodeBrowserFilterCacheDragFromNodeId = -1;
    std::string m_NodeBrowserFilterCacheDragFromSocketId;
    int m_NodeBrowserFilterCacheDragToNodeId = -1;
    std::string m_NodeBrowserFilterCacheDragToSocketId;
    int m_NodeBrowserTextureUploadBudgetFrame = -1;
    int m_NodeBrowserTextureUploadsThisFrame = 0;
    std::uint64_t m_NodeBrowserFilterCacheGraphRevision = 0;
    std::string m_NodeBrowserFilterCacheSearch;
    std::vector<std::size_t> m_NodeBrowserFilteredEntryIndices;
    std::map<std::string, float> m_NodeBrowserEntryAlpha;
    std::map<int, unsigned int> m_ImagePreviewTextures;
    std::map<int, size_t> m_ImagePreviewFingerprints;
    std::map<int, ImVec2> m_ImagePreviewSizes;
    std::map<int, unsigned int> m_GraphPreviewTextures;
    std::map<int, std::uint64_t> m_GraphPreviewRevisions;
    std::map<int, ImVec2> m_GraphPreviewSizes;
    std::map<std::string, unsigned int> m_NodeBrowserThumbnailTextures;
    std::map<std::string, std::uint64_t> m_NodeBrowserThumbnailRevisions;
    std::map<std::string, ImVec2> m_NodeBrowserThumbnailSizes;
    std::map<std::string, float> m_NodeBrowserCardHoverAnim;
    std::unordered_map<int, float> m_NodeSelectionAnim;
    std::unordered_map<int, float> m_NodeHoverAnim;
    std::unordered_map<std::string, float> m_LinkEmphasisAnim;
    std::unordered_map<int, float> m_GroupEmphasisAnim;
    std::map<int, std::uint64_t> m_NodeFrontOrder;
    std::map<int, NodeLayoutCache> m_NodeLayoutCache;
    mutable std::map<int, float> m_NodeMeasuredBaseHeights;
    mutable std::map<int, bool> m_NodeContentOverflow;
    std::uint64_t m_NodeFrontOrderCounter = 1;
    int m_NodeOrderCacheFrame = -1;
    const EditorNodeGraph::Graph* m_NodeOrderCacheGraph = nullptr;
    std::uint64_t m_NodeOrderCacheGraphRevision = 0;
    std::size_t m_NodeOrderCacheNodeCount = 0;
    std::uint64_t m_NodeOrderCacheFrontCounter = 0;
    std::vector<int> m_NodeRenderOrderCache;
    std::vector<int> m_NodeHitTestOrderCache;
    const EditorNodeGraph::Graph* m_NodeLookupCacheGraph = nullptr;
    std::uint64_t m_NodeLookupCacheGraphRevision = 0;
    std::size_t m_NodeLookupCacheNodeCount = 0;
    std::unordered_map<int, std::size_t> m_NodeLookupCache;
    EditorNodeGraph::Graph* m_RenderGraphOverride = nullptr;
    std::vector<std::shared_ptr<LayerBase>>* m_RenderLayersOverride = nullptr;
    bool m_RenderPreviewOnly = false;
    EditorNodeGraph::Vec2 m_LastGraphMousePos {};
    bool m_HasLastGraphMousePos = false;
    EditorModule* m_ActiveEditor = nullptr;
    int m_PushedSourceNodeId = -1;
    float m_PushDistance = 0.0f;
    std::vector<int> m_PushedNodeIds;
    int m_EditingGroupId = -1;
    char m_GroupRenameBuffer[128] = {};
    int m_DragGroupId = -1;
    int m_ResizingGroupId = -1;
    int m_HoveredGroupId = -1;
    int m_ChannelSplitConfirmNodeId = -1;
    double m_ChannelSplitConfirmStartTime = 0.0;
    CachedRect m_ChannelSplitConfirmRect {};
    double m_ContextMenuOpenedAt = 0.0;
    bool m_ContextMenuFadeActive = false;
    std::uint64_t m_LastGraphStructureRevision = 0;
    std::unordered_map<std::string, PreviewGraphCacheEntry> m_PresetPreviewGraphCache;
    std::unique_ptr<EditorNodeGraphUI> m_PresetPreviewCurrentRenderer;
    std::unique_ptr<EditorNodeGraphUI> m_PresetPreviewPreviousRenderer;
    std::string m_DisplayedPresetPreviewId;
    std::string m_PreviousPresetPreviewId;
    double m_PresetPreviewFadeStartedAt = 0.0;

    void CopySelectedNodes(EditorModule* editor, bool writeSystemClipboard = false);
    void PasteNodes(EditorModule* editor, bool preferSystemClipboard = false);
    void CopyGraphInfo(EditorModule* editor, bool wholeGraph, bool includeState);
    void PasteGraphInfo(EditorModule* editor);
    bool PasteClipboardPayload(EditorModule* editor, const nlohmann::json& clipboardPayload, std::string* outSummary);
    void DuplicateSelectedNodes(EditorModule* editor);
    nlohmann::json m_Clipboard;
    int m_ClipboardPasteCount = 0;
    bool m_OpenRenameProjectPopup = false;
    char m_RenameProjectBuffer[256] = {};
    bool m_OpenSavePresetPopup = false;
    char m_SavePresetNameBuffer[128] = {};
};
