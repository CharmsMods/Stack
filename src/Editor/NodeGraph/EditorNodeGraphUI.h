#pragma once

#include "EditorNodeGraph.h"
#include "ThirdParty/json.hpp"
#include <imgui.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class EditorModule;

class EditorNodeGraphUI {
public:
    ~EditorNodeGraphUI();
    void Initialize();
    void Render(EditorModule* editor);

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

    EditorNodeGraph::Vec2 ScreenToGraph(const EditorNodeGraph::Vec2& screen) const;
    EditorNodeGraph::Vec2 GraphToScreen(const EditorNodeGraph::Vec2& graph) const;
    EditorNodeGraph::Vec2 NodeSize(const EditorNodeGraph::Node& node) const;
    EditorNodeGraph::Vec2 NodeScreenSize(const EditorNodeGraph::Node& node) const;
    void ZoomAtMouse(float wheel);
    void ClampPanToContent(const EditorNodeGraph::Graph& graph);

    void RenderContextMenu(EditorModule* editor);
    void RenderNode(EditorModule* editor, EditorNodeGraph::Node& node);
    void RenderLinks(const EditorNodeGraph::Graph& graph);
    void RenderInteraction(EditorModule* editor, const EditorNodeGraph::Graph& graph);
    void RenderNodeBrowser(EditorModule* editor);
    void RenderValidationStatus(const EditorNodeGraph::Graph& graph);
    void RenderInteractionDebugOverlay(const EditorNodeGraph::Graph& graph, int hoveredNodeId, GraphMouseOwner owner) const;
    bool IsGraphCanvasHovered() const;
    GraphMouseOwner ResolveMouseOwner(
        const EditorNodeGraph::Graph& graph,
        bool graphHovered,
        const SocketHit& hoveredInput,
        const SocketHit& hoveredOutput,
        int hoveredNodeId,
        const EditorNodeGraph::Link& hoveredLink) const;
    EditorNodeGraph::Vec2 InputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const;
    EditorNodeGraph::Vec2 OutputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const;
    SocketHit FindInputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const;
    SocketHit FindOutputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const;
    int FindNodeAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const;
    EditorNodeGraph::Link FindLinkAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const;
    bool IsPointNearLink(const EditorNodeGraph::Vec2& point, const EditorNodeGraph::Vec2& a, const EditorNodeGraph::Vec2& b) const;
    unsigned int GetImagePreviewTexture(const EditorNodeGraph::Node& node);
    unsigned int GetGraphPreviewTexture(EditorModule* editor, const EditorNodeGraph::Node& node);
    unsigned int UploadPreviewTexture(int nodeId, const std::vector<unsigned char>& pixels, int width, int height);
    NodeLayoutCache BuildNodeLayoutCache(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) const;
    void RefreshNodeLayoutCache(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node);
    const NodeLayoutCache* FindNodeLayoutCache(int nodeId) const;
    std::vector<int> GetNodeRenderOrder(const EditorNodeGraph::Graph& graph);
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
    bool m_NodeBrowserOpen = false;
    bool m_NodeBrowserFocusSearch = false;
    double m_NodeBrowserOpenedAt = 0.0;
    double m_NodeBrowserLastFrameTime = 0.0;
    NodeBrowserMode m_NodeBrowserMode = NodeBrowserMode::GeneralAdd;
    EditorNodeGraph::Vec2 m_NodeBrowserGraphPos {};
    EditorNodeGraph::Vec2 m_NodeBrowserScreenPos {};
    int m_NodeBrowserDragFromNodeId = -1;
    std::string m_NodeBrowserDragFromSocketId;
    int m_NodeBrowserDragToNodeId = -1;
    std::string m_NodeBrowserDragToSocketId;
    std::map<std::string, float> m_NodeBrowserEntryAlpha;
    std::map<int, unsigned int> m_ImagePreviewTextures;
    std::map<int, size_t> m_ImagePreviewFingerprints;
    std::map<int, ImVec2> m_ImagePreviewSizes;
    std::map<int, unsigned int> m_GraphPreviewTextures;
    std::map<int, std::uint64_t> m_GraphPreviewRevisions;
    std::map<int, ImVec2> m_GraphPreviewSizes;
    std::map<int, std::uint64_t> m_NodeFrontOrder;
    std::map<int, NodeLayoutCache> m_NodeLayoutCache;
    mutable std::map<int, float> m_NodeMeasuredBaseHeights;
    mutable std::map<int, bool> m_NodeContentOverflow;
    std::uint64_t m_NodeFrontOrderCounter = 1;
    EditorModule* m_ActiveEditor = nullptr;
    int m_PushedSourceNodeId = -1;
    float m_PushDistance = 0.0f;
    std::vector<int> m_PushedNodeIds;

    void CopySelectedNodes(EditorModule* editor);
    void PasteNodes(EditorModule* editor);
    void DuplicateSelectedNodes(EditorModule* editor);
    nlohmann::json m_Clipboard;
    int m_ClipboardPasteCount = 0;
};
