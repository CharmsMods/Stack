#include "Editor/EditorModule.h"

#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace {

constexpr float kGraphAutoFocusMinZoom = 0.16f;
constexpr float kGraphAutoFocusMaxZoom = 2.5f;
constexpr float kGraphAutoFocusPadding = 56.0f;
constexpr double kGraphAutoFocusDurationSeconds = 0.22;

float ComputeGraphAutoFocusZoom(float canvasWidth, float canvasHeight, const EditorNodeGraph::Vec2& nodeSize) {
    const float safeWidth = std::max(1.0f, nodeSize.x);
    const float safeHeight = std::max(1.0f, nodeSize.y);
    const float fitWidth = std::max(80.0f, canvasWidth - (kGraphAutoFocusPadding * 2.0f)) / safeWidth;
    const float fitHeight = std::max(80.0f, canvasHeight - (kGraphAutoFocusPadding * 2.0f)) / safeHeight;
    return std::clamp(std::min(fitWidth, fitHeight), kGraphAutoFocusMinZoom, kGraphAutoFocusMaxZoom);
}

void ComputeGraphAutoFocusPan(
    const float canvasWidth,
    const float canvasHeight,
    const EditorNodeGraph::Vec2& nodePosition,
    const EditorNodeGraph::Vec2& nodeSize,
    const float zoom,
    float& outPanX,
    float& outPanY) {
    const float centerX = nodePosition.x + (nodeSize.x * 0.5f);
    const float centerY = nodePosition.y + (nodeSize.y * 0.5f);
    outPanX = (canvasWidth * 0.5f) - (centerX * zoom);
    outPanY = (canvasHeight * 0.5f) - (centerY * zoom);
}

} // namespace

void EditorModule::SetGraphDropTargetRect(float minX, float minY, float maxX, float maxY) {
    m_GraphDropMinX = minX;
    m_GraphDropMinY = minY;
    m_GraphDropMaxX = maxX;
    m_GraphDropMaxY = maxY;
}

void EditorModule::SetGraphViewTransform(float originX, float originY, float panX, float panY, float zoom) {
    m_GraphViewOriginX = originX;
    m_GraphViewOriginY = originY;
    m_GraphViewPanX = panX;
    m_GraphViewPanY = panY;
    m_GraphViewZoom = std::max(0.01f, zoom);
}

void EditorModule::ApplyGraphAutoFocusFrame(float canvasWidth, float canvasHeight, float& panX, float& panY, float& zoom) {
    if (!m_GraphAutoFocus.trackingActive) {
        return;
    }

    const float safeCanvasWidth = std::max(1.0f, canvasWidth);
    const float safeCanvasHeight = std::max(1.0f, canvasHeight);
    const bool canvasChanged =
        std::abs(m_GraphAutoFocus.lastCanvasWidth - safeCanvasWidth) > 0.5f ||
        std::abs(m_GraphAutoFocus.lastCanvasHeight - safeCanvasHeight) > 0.5f;

    const bool needsInitialTarget =
        m_GraphAutoFocus.lastCanvasWidth <= 0.0f ||
        m_GraphAutoFocus.lastCanvasHeight <= 0.0f;

    if (needsInitialTarget || canvasChanged) {
        m_GraphAutoFocus.lastCanvasWidth = safeCanvasWidth;
        m_GraphAutoFocus.lastCanvasHeight = safeCanvasHeight;
        m_GraphAutoFocus.startPanX = panX;
        m_GraphAutoFocus.startPanY = panY;
        m_GraphAutoFocus.startZoom = zoom;
        m_GraphAutoFocus.targetZoom = ComputeGraphAutoFocusZoom(
            safeCanvasWidth,
            safeCanvasHeight,
            m_GraphAutoFocus.nodeSize);
        ComputeGraphAutoFocusPan(
            safeCanvasWidth,
            safeCanvasHeight,
            m_GraphAutoFocus.nodePosition,
            m_GraphAutoFocus.nodeSize,
            m_GraphAutoFocus.targetZoom,
            m_GraphAutoFocus.targetPanX,
            m_GraphAutoFocus.targetPanY);
        m_GraphAutoFocus.animationStartTime = ImGui::GetTime();
        m_GraphAutoFocus.animationActive = true;
    }

    if (!m_GraphAutoFocus.animationActive) {
        panX = m_GraphAutoFocus.targetPanX;
        panY = m_GraphAutoFocus.targetPanY;
        zoom = m_GraphAutoFocus.targetZoom;
        return;
    }

    const double elapsed = ImGui::GetTime() - m_GraphAutoFocus.animationStartTime;
    const float t = std::clamp(static_cast<float>(elapsed / kGraphAutoFocusDurationSeconds), 0.0f, 1.0f);
    const float eased = ImGuiExtras::EaseOutCubic(t);
    panX = m_GraphAutoFocus.startPanX + ((m_GraphAutoFocus.targetPanX - m_GraphAutoFocus.startPanX) * eased);
    panY = m_GraphAutoFocus.startPanY + ((m_GraphAutoFocus.targetPanY - m_GraphAutoFocus.startPanY) * eased);
    zoom = m_GraphAutoFocus.startZoom + ((m_GraphAutoFocus.targetZoom - m_GraphAutoFocus.startZoom) * eased);
    if (t >= 0.999f) {
        panX = m_GraphAutoFocus.targetPanX;
        panY = m_GraphAutoFocus.targetPanY;
        zoom = m_GraphAutoFocus.targetZoom;
        m_GraphAutoFocus.animationActive = false;
    }
}

void EditorModule::RequestGraphNodeAutoFocus(
    int nodeId,
    const EditorNodeGraph::Vec2& nodePosition,
    const EditorNodeGraph::Vec2& nodeSize,
    float currentPanX,
    float currentPanY,
    float currentZoom) {
    if (nodeId <= 0 || nodeSize.x <= 0.0f || nodeSize.y <= 0.0f) {
        return;
    }

    m_GraphAutoFocus.trackingActive = true;
    m_GraphAutoFocus.animationActive = false;
    m_GraphAutoFocus.nodeId = nodeId;
    m_GraphAutoFocus.nodePosition = nodePosition;
    m_GraphAutoFocus.nodeSize = nodeSize;
    m_GraphAutoFocus.startPanX = currentPanX;
    m_GraphAutoFocus.startPanY = currentPanY;
    m_GraphAutoFocus.startZoom = currentZoom;
    m_GraphAutoFocus.targetPanX = currentPanX;
    m_GraphAutoFocus.targetPanY = currentPanY;
    m_GraphAutoFocus.targetZoom = currentZoom;
    m_GraphAutoFocus.lastCanvasWidth = 0.0f;
    m_GraphAutoFocus.lastCanvasHeight = 0.0f;
    m_GraphAutoFocus.animationStartTime = ImGui::GetTime();
}

void EditorModule::CancelGraphAutoFocusTracking() {
    m_GraphAutoFocus.trackingActive = false;
    m_GraphAutoFocus.animationActive = false;
    m_GraphAutoFocus.nodeId = -1;
}

void EditorModule::ClearGraphAutoFocusIfTrackedNode(int nodeId) {
    if (m_GraphAutoFocus.trackingActive && m_GraphAutoFocus.nodeId == nodeId) {
        CancelGraphAutoFocusTracking();
    }
}

bool EditorModule::IsScreenPointOverGraph(float x, float y) const {
    return x >= m_GraphDropMinX && x <= m_GraphDropMaxX && y >= m_GraphDropMinY && y <= m_GraphDropMaxY;
}

bool EditorModule::HandleGraphFileDrop(const std::string& path, float screenX, float screenY) {
    return HandleGraphFileDrop(std::vector<std::string>{ path }, screenX, screenY);
}

bool EditorModule::HandleGraphFileDrop(const std::vector<std::string>& paths, float screenX, float screenY) {
    float anchorX = screenX;
    float anchorY = screenY;
    if (!IsScreenPointOverGraph(screenX, screenY)) {
        const bool hasGraphRect = m_GraphDropMaxX > m_GraphDropMinX && m_GraphDropMaxY > m_GraphDropMinY;
        if (!hasGraphRect) {
            return false;
        }
        anchorX = (m_GraphDropMinX + m_GraphDropMaxX) * 0.5f;
        anchorY = (m_GraphDropMinY + m_GraphDropMaxY) * 0.5f;
    }

    const float safeZoom = std::max(0.01f, m_GraphViewZoom);
    const EditorNodeGraph::Vec2 graphPosition{
        (anchorX - m_GraphViewOriginX - m_GraphViewPanX) / safeZoom - 40.0f,
        (anchorY - m_GraphViewOriginY - m_GraphViewPanY) / safeZoom - 40.0f
    };
    return RequestGraphImageChainImports(paths, graphPosition);
}

void EditorModule::TogglePartialSplitTargets(
    float workspaceWidth,
    float minLeftWidth,
    float maxLeftWidth,
    bool compositeViewportMode) {
    const float leftTarget = minLeftWidth;
    const float rightTarget = compositeViewportMode ? maxLeftWidth : maxLeftWidth;
    const float midpoint = workspaceWidth * 0.5f;
    m_SplitAutoAnimFrom = m_LeftPaneWidth;

    if (compositeViewportMode && m_LeftPaneWidth <= 2.0f) {
        m_SplitAutoAnimTo = leftTarget;
    } else if (compositeViewportMode && m_LeftPaneWidth >= workspaceWidth - 2.0f) {
        m_SplitAutoAnimTo = rightTarget;
    } else {
        m_SplitAutoAnimTo = (m_LeftPaneWidth < midpoint) ? rightTarget : leftTarget;
    }

    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimStartTime = ImGui::GetTime();
    m_SplitAutoAnimating = true;
}

void EditorModule::HandleSpacebarPress(
    float workspaceWidth,
    float paneHeight,
    float minLeftWidth,
    float maxLeftWidth,
    float splitGap) {
    
    m_SplitAutoAnimFrom = m_LeftPaneWidth;
    
    if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
        const bool compositeViewportMode = IsCompositeViewportMode();
        
        if (compositeViewportMode) {
            // --- 1. COMPOSITE CANVAS MODE: ORIGINAL MIDPOINT TOGGLE ---
            const float midpoint = workspaceWidth * 0.5f;
            const float leftTarget = minLeftWidth;
            const float rightTarget = maxLeftWidth;
            
            if (m_LeftPaneWidth <= 2.0f) {
                m_SplitAutoAnimTo = leftTarget;
            } else if (m_LeftPaneWidth >= workspaceWidth - 2.0f) {
                m_SplitAutoAnimTo = rightTarget;
            } else {
                m_SplitAutoAnimTo = (m_LeftPaneWidth < midpoint) ? rightTarget : leftTarget;
            }
        } else {
            // --- 2. SINGLE IMAGE VIEWPORT MODE: ASPECT-RATIO MAXIMUM SIZING ---
            int imgW = m_Pipeline.GetCanvasWidth();
            int imgH = m_Pipeline.GetCanvasHeight();
            
            // If no image is active/loaded, fall back to default toggle behavior
            if (imgW <= 0 || imgH <= 0) {
                const float midpoint = workspaceWidth * 0.5f;
                m_SplitAutoAnimTo = (m_LeftPaneWidth < midpoint) ? maxLeftWidth : minLeftWidth;
            } else {
                // Viewport padding parameters matching EditorViewport
                const float paddingY = 32.0f;
                const float paddingX = 36.0f;
                const float availY = std::max(100.0f, paneHeight - paddingY);
                
                // Maximum height-limited display width (no 0.84f constraint so it reaches absolute max height limit)
                const float dispW = availY * (static_cast<float>(imgW) / static_cast<float>(imgH));
                const float optimalRightWidth = dispW + paddingX;
                
                // Constrain optimalRightWidth to fit within currently enforced limits
                const float minRightWidth = 420.0f;
                const float maxRightWidth = workspaceWidth - minLeftWidth - splitGap;
                const float constrainedRightWidth = std::clamp(optimalRightWidth, minRightWidth, maxRightWidth);
                
                const float targetLeftPaneWidth = workspaceWidth - splitGap - constrainedRightWidth;
                
                // Toggle logic: if we are already close to the optimal expanded state, toggle back to maxLeftWidth to see mostly graph
                if (std::abs(m_LeftPaneWidth - targetLeftPaneWidth) < 5.0f) {
                    m_SplitAutoAnimTo = maxLeftWidth;
                } else {
                    m_SplitAutoAnimTo = targetLeftPaneWidth;
                }
            }
        }
    } else {
        // --- 3. SIDEBAR MENU WINDOW OPTIMAL SIZING ---
        float optimalWidth = 360.0f;
        if (m_ActiveSubWindow == EditorSubWindow::ComplexNode) {
            optimalWidth = 470.0f;
        }
        
        const float targetLeftPaneWidth = std::clamp(optimalWidth, minLeftWidth, maxLeftWidth);
        
        // Toggle logic: if already at the optimal width, contract to minLeftWidth, otherwise expand to optimal
        if (std::abs(m_LeftPaneWidth - targetLeftPaneWidth) < 5.0f) {
            m_SplitAutoAnimTo = minLeftWidth;
        } else {
            m_SplitAutoAnimTo = targetLeftPaneWidth;
        }
    }
    
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimStartTime = ImGui::GetTime();
    m_SplitAutoAnimating = true;
}

void EditorModule::HandleSpacebarLongPress(
    float workspaceWidth,
    float paneHeight,
    float minLeftWidth,
    float maxLeftWidth,
    float splitGap) {
    
    m_SplitAutoAnimFrom = m_LeftPaneWidth;
    
    const float midpoint = workspaceWidth * 0.5f;
    if (m_LeftPaneWidth <= 5.0f) {
        m_SplitAutoAnimTo = workspaceWidth;
        m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::GraphOnly;
    } else if (m_LeftPaneWidth >= workspaceWidth - 5.0f) {
        m_SplitAutoAnimTo = 0.0f;
        m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::ViewportOnly;
    } else {
        if (m_LeftPaneWidth < midpoint) {
            m_SplitAutoAnimTo = workspaceWidth;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::GraphOnly;
        } else {
            m_SplitAutoAnimTo = 0.0f;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::ViewportOnly;
        }
    }
    
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimStartTime = ImGui::GetTime();
    m_SplitAutoAnimating = true;
}
