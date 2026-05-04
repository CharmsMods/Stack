#pragma once

#include "EditorNodeGraph.h"
#include <imgui.h>
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

    struct SocketHit {
        int nodeId = -1;
        std::string socketId;
        bool IsValid() const { return nodeId > 0 && !socketId.empty(); }
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
    void RenderValidationStatus(const EditorNodeGraph::Graph& graph);
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
    void DrawClippedText(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color) const;

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
    bool m_NodeControlCapture = false;
    bool m_BoxSelecting = false;
    EditorNodeGraph::Vec2 m_BoxSelectStart;
    EditorNodeGraph::Vec2 m_BoxSelectCurrent;
    EditorNodeGraph::Vec2 m_CanvasMin;
    EditorNodeGraph::Vec2 m_CanvasMax;
    std::string m_StatusMessage;
    char m_SearchBuffer[128] = {};
    std::map<int, unsigned int> m_ImagePreviewTextures;
    std::map<int, size_t> m_ImagePreviewFingerprints;
    std::map<int, unsigned int> m_GraphPreviewTextures;
    std::map<int, double> m_GraphPreviewRefreshTimes;
};
