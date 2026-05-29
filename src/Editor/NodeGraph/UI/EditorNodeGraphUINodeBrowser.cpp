#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <imgui.h>

namespace {

using NodeBrowserEntry = EditorNodeGraphDefinitions::NodeCatalogEntry;

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;

    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    };

    return lower(haystack).find(lower(needle)) != std::string::npos;
}

std::string EntryKey(const NodeBrowserEntry& entry) {
    return std::to_string(static_cast<int>(entry.kind)) + ":" + std::to_string(entry.value) + ":" + entry.label;
}

float AnimateToward(float current, float target, float rate, float dt) {
    const float t = 1.0f - std::exp(-std::max(0.0f, rate) * std::max(0.0f, dt));
    return current + (target - current) * std::clamp(t, 0.0f, 1.0f);
}

ImU32 ScaleAlpha(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

bool RenderBrowserRow(const NodeBrowserEntry& entry, float alpha, bool interactive) {
    if (alpha <= 0.01f) {
        return false;
    }

    ImGui::PushID(EntryKey(entry).c_str());
    const float width = ImGui::GetContentRegionAvail().x;
    const float rowHeight = 30.0f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, rowHeight);
    ImGui::InvisibleButton("row", size);
    const bool hovered = interactive && ImGui::IsItemHovered();
    const bool pressed = interactive && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 max = ImVec2(min.x + size.x, min.y + size.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 fill = hovered ? ScaleAlpha(IM_COL32(110, 150, 175, 54), alpha) : ScaleAlpha(IM_COL32(0, 0, 0, 0), alpha);
    const ImU32 text = interactive ? ScaleAlpha(IM_COL32(232, 238, 242, 255), alpha) : ScaleAlpha(IM_COL32(175, 184, 191, 255), alpha);
    if (fill != 0) {
        drawList->AddRectFilled(min, max, fill, 7.0f);
    }
    const ImVec2 textMin(min.x + 12.0f, min.y + 6.0f);
    drawList->PushClipRect(min, max, true);
    drawList->AddText(textMin, text, entry.label.c_str());
    drawList->PopClipRect();
    ImGui::PopID();
    return pressed;
}


bool PrototypeHasCompatibleInput(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    const NodeBrowserEntry& entry) {
    EditorNodeGraph::SocketDefinition fromSocket;
    if (!graph.FindSocket(fromNodeId, fromSocketId, &fromSocket)) {
        return false;
    }
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(prototype, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (fromSocket.type == EditorNodeGraph::SocketType::Image) {
            if (prototype.kind == EditorNodeGraph::NodeKind::Layer && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Mix &&
                (socket.id == EditorNodeGraph::kMixInputASocketId || socket.id == EditorNodeGraph::kMixInputBSocketId)) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::ImageToMask && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Output && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Preview && socket.id == EditorNodeGraph::kPreviewInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Scope && socket.id == EditorNodeGraph::kScopeInputSocketId) return true;
        } else if (fromSocket.type == EditorNodeGraph::SocketType::Mask) {
            if (prototype.kind == EditorNodeGraph::NodeKind::Layer && socket.id == EditorNodeGraph::kMaskInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Mix && socket.id == EditorNodeGraph::kMixFactorSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::MaskUtility && socket.id == EditorNodeGraph::kMaskInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Preview && socket.id == EditorNodeGraph::kPreviewInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Scope && socket.id == EditorNodeGraph::kScopeInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::ChannelCombine &&
                (socket.id == "r" || socket.id == "g" || socket.id == "b" || socket.id == "a")) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Output &&
                (socket.id == "r" || socket.id == "g" || socket.id == "b" || socket.id == "a")) return true;

            // Also allow mask outputs to connect to main image inputs (treating mask as grayscale image)
            if (prototype.kind == EditorNodeGraph::NodeKind::Layer && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Mix &&
                (socket.id == EditorNodeGraph::kMixInputASocketId || socket.id == EditorNodeGraph::kMixInputBSocketId)) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::ImageToMask && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Output && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::ChannelSplit && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
        } else if (fromSocket.type == EditorNodeGraph::SocketType::Raw) {
            if ((prototype.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
                 prototype.kind == EditorNodeGraph::NodeKind::RawDevelop) &&
                socket.id == EditorNodeGraph::kRawInputSocketId) return true;
        }
    }
    return false;
}

bool PrototypeHasCompatibleOutput(
    const EditorNodeGraph::Graph& graph,
    const NodeBrowserEntry& entry,
    int toNodeId,
    const std::string& toSocketId) {
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!to) {
        return false;
    }
    EditorNodeGraph::SocketDefinition toSocket;
    if (!graph.FindSocket(toNodeId, toSocketId, &toSocket)) {
        return false;
    }
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(prototype, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (toSocket.type == EditorNodeGraph::SocketType::Image && socket.type == EditorNodeGraph::SocketType::Image) {
            return true;
        }
        if (toSocket.type == EditorNodeGraph::SocketType::Mask && socket.type == EditorNodeGraph::SocketType::Mask) {
            return true;
        }
        if (toSocket.type == EditorNodeGraph::SocketType::Raw &&
            socket.type == EditorNodeGraph::SocketType::Raw &&
            (prototype.kind == EditorNodeGraph::NodeKind::RawSource ||
             prototype.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise)) {
            return true;
        }
    }
    return false;
}

int AddNodeFromBrowserEntry(EditorModule* editor, const NodeBrowserEntry& entry, const EditorNodeGraph::Vec2& graphPos) {
    if (!editor) {
        return -1;
    }
    switch (entry.kind) {
        case EditorNodeGraph::NodeKind::Output:
            editor->AddOutputNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Layer:
            editor->AddLayerNodeAt(static_cast<LayerType>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Scope:
            editor->AddScopeNodeAt(static_cast<EditorNodeGraph::ScopeKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Preview:
            editor->AddPreviewNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            editor->AddMaskNodeAt(static_cast<EditorNodeGraph::MaskGeneratorKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            editor->AddMaskUtilityNodeAt(static_cast<EditorNodeGraph::MaskUtilityKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            editor->AddImageToMaskNodeAt(static_cast<EditorNodeGraph::ImageToMaskKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            editor->AddImageGeneratorNodeAt(static_cast<EditorNodeGraph::ImageGeneratorKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Mix:
            editor->AddMixNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            editor->AddChannelSplitNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            editor->AddChannelCombineNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawDevelop:
            editor->AddRawDevelopNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            editor->AddRawNeuralDenoiseNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::Composite:
            break;
    }
    const std::vector<int>& selected = editor->GetNodeGraph().GetSelectedNodeIds();
    return selected.empty() ? -1 : selected.back();
}

} // namespace

void EditorNodeGraphUI::OpenNodeBrowser(NodeBrowserMode mode, const EditorNodeGraph::Vec2& graphPos) {
    m_NodeBrowserMode = mode;
    m_NodeBrowserGraphPos = graphPos;
    m_NodeBrowserScreenPos = GraphToScreen(graphPos);
    m_NodeBrowserOpen = true;
    m_NodeBrowserFocusSearch = true;
    m_NodeBrowserOpenedAt = ImGui::GetTime();
    m_NodeBrowserLastFrameTime = m_NodeBrowserOpenedAt;
    m_NodeBrowserSearchBuffer[0] = '\0';
    m_NodeBrowserEntryAlpha.clear();
}

void EditorNodeGraphUI::CloseNodeBrowser() {
    if (m_NodeBrowserOpen && m_PushedSourceNodeId > 0) {
        if (m_ActiveEditor) {
            EditorNodeGraph::Graph& graph = m_ActiveEditor->GetNodeGraph();
            for (int id : m_PushedNodeIds) {
                if (EditorNodeGraph::Node* dsNode = graph.FindNode(id)) {
                    dsNode->position.x -= m_PushDistance;
                    RefreshNodeLayoutCache(graph, *dsNode);
                }
            }
        }
    }
    m_PushedSourceNodeId = -1;
    m_PushDistance = 0.0f;
    m_PushedNodeIds.clear();

    m_NodeBrowserOpen = false;
    m_NodeBrowserFocusSearch = false;
    m_NodeBrowserDragFromNodeId = -1;
    m_NodeBrowserDragFromSocketId.clear();
    m_NodeBrowserDragToNodeId = -1;
    m_NodeBrowserDragToSocketId.clear();
    m_NodeBrowserEntryAlpha.clear();
}

void EditorNodeGraphUI::RenderNodeBrowser(EditorModule* editor) {
    // Old popup window rendering removed in favor of RenderNodesPanelDrawer
}

void EditorNodeGraphUI::RenderNodesPanelDrawer(
    EditorModule* editor,
    float panelWidth,
    float paneHeight,
    const ImVec2& workspacePos) {
    
    if (!editor) {
        return;
    }

    const bool isOpen = m_NodeBrowserOpen;

    const double now = ImGui::GetTime();
    const float dt = static_cast<float>(std::clamp(now - m_NodeBrowserLastFrameTime, 0.0, 0.05));
    m_NodeBrowserLastFrameTime = now;
    
    // Overall fade alpha based on open/closed state
    const float targetAlpha = isOpen ? 1.0f : 0.0f;
    static float currentAlpha = 0.0f;
    if (!isOpen && currentAlpha < 0.01f) {
        currentAlpha = 0.0f;
    } else {
        currentAlpha += (targetAlpha - currentAlpha) * dt * 15.0f;
    }
    const float alpha = std::clamp(currentAlpha, 0.0f, 1.0f);

    const EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    std::vector<NodeBrowserEntry> entries = EditorNodeGraphDefinitions::BuildNodeCatalogEntries();
    const std::string search = m_NodeBrowserSearchBuffer;
    std::vector<const NodeBrowserEntry*> filtered;
    filtered.reserve(entries.size());
    std::map<std::string, float> categoryAlpha;
    
    for (const NodeBrowserEntry& entry : entries) {
        const bool matchesSearch =
            ContainsCaseInsensitive(entry.label, search) ||
            ContainsCaseInsensitive(entry.category, search);
        const bool compatibleOutput =
            m_NodeBrowserMode != NodeBrowserMode::ConnectFromOutput ||
            PrototypeHasCompatibleInput(graph, m_NodeBrowserDragFromNodeId, m_NodeBrowserDragFromSocketId, entry);
        const bool compatibleInput =
            m_NodeBrowserMode != NodeBrowserMode::ConnectFromInput ||
            PrototypeHasCompatibleOutput(graph, entry, m_NodeBrowserDragToNodeId, m_NodeBrowserDragToSocketId);
        const bool visible = isOpen && matchesSearch && compatibleOutput && compatibleInput;
        const std::string key = EntryKey(entry);
        float& entryAlpha = m_NodeBrowserEntryAlpha[key];
        entryAlpha = AnimateToward(entryAlpha, visible ? 1.0f : 0.0f, visible ? 18.0f : 14.0f, dt);
        if (visible) {
            filtered.push_back(&entry);
        }
        if (entryAlpha > 0.02f) {
            categoryAlpha[entry.category] = std::max(categoryAlpha[entry.category], entryAlpha);
        }
    }

    for (auto it = m_NodeBrowserEntryAlpha.begin(); it != m_NodeBrowserEntryAlpha.end();) {
        if (it->second <= 0.01f) {
            it = m_NodeBrowserEntryAlpha.erase(it);
        } else {
            ++it;
        }
    }

    // 1. Sliding panel background & content drawing
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 panelMin = workspacePos;
    
    float gradientWidth = std::min(60.0f, panelWidth);
    float solidWidth = panelWidth - gradientWidth;
    
    ImVec4 workspaceColor = editor->GetWorkspaceBaseColor();
    const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
    const bool isLightBg = luminance >= 0.5f;

    ImVec4 colBgOpaqueVec = workspaceColor;
    colBgOpaqueVec.w = (isLightBg ? 0.94f : 0.92f) * alpha;
    const ImU32 colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);

    ImVec4 colBgTransVec = workspaceColor;
    colBgTransVec.w = 0.0f;
    const ImU32 colBgTrans = ImGui::ColorConvertFloat4ToU32(colBgTransVec);

    const ImU32 colTitleText = ScaleAlpha(isLightBg ? IM_COL32(18, 24, 30, 255) : IM_COL32(255, 255, 255, 255), alpha);
    const ImU32 colPassiveText = ScaleAlpha(isLightBg ? IM_COL32(80, 95, 105, 220) : IM_COL32(140, 160, 170, 200), alpha);
    const ImU32 colActiveText = ScaleAlpha(isLightBg ? IM_COL32(16, 110, 190, 255) : IM_COL32(92, 178, 255, 255), alpha);
    const ImU32 colNormalText = ScaleAlpha(isLightBg ? IM_COL32(40, 50, 60, 220) : IM_COL32(200, 210, 220, 200), alpha);
    const ImU32 colHoveredHeader = ScaleAlpha(isLightBg ? IM_COL32(16, 110, 190, 28) : IM_COL32(92, 178, 255, 30), alpha);
    const ImU32 colActiveHeader = ScaleAlpha(isLightBg ? IM_COL32(16, 110, 190, 48) : IM_COL32(92, 178, 255, 50), alpha);

    // Solid part
    if (solidWidth > 0.0f) {
        drawList->AddRectFilled(panelMin, ImVec2(workspacePos.x + solidWidth, workspacePos.y + paneHeight), colBgOpaque);
    }
    // Gradient feathered blend part
    drawList->AddRectFilledMultiColor(
        ImVec2(workspacePos.x + solidWidth, workspacePos.y),
        ImVec2(workspacePos.x + panelWidth, workspacePos.y + paneHeight),
        colBgOpaque, colBgTrans, colBgTrans, colBgOpaque
    );

    bool closeBrowser = false;
    if (isOpen) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (mousePos.x < workspacePos.x || mousePos.x > workspacePos.x + panelWidth ||
                mousePos.y < workspacePos.y || mousePos.y > workspacePos.y + paneHeight) {
                closeBrowser = true;
            }
        }
    }

    float contentWidth = panelWidth - 40.0f; // breathing room on right for the feathered edge
    if (contentWidth > 1.0f) {
        ImGui::SetCursorScreenPos(ImVec2(workspacePos.x + 16.0f, workspacePos.y + 28.0f));
        // Added ImGuiWindowFlags_NoNav to prevent blue outline selection focus on child window
        ImGui::BeginChild("NodesPanelDrawerChild", ImVec2(contentWidth, paneHeight - 56.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);

        ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
        ImGui::TextUnformatted(m_NodeBrowserMode == NodeBrowserMode::GeneralAdd ? "ADD NODE" : "COMPATIBLE NODES");
        ImGui::PopStyleColor();
        
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        if (isOpen && m_NodeBrowserFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_NodeBrowserFocusSearch = false;
        }

        const char* prompt = (m_NodeBrowserMode == NodeBrowserMode::GeneralAdd)
            ? "Search nodes"
            : "Search compatible nodes";

        // Compact custom input box theme
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(34, 43, 48, 235));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(44, 57, 63, 235));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(44, 57, 63, 235));
        ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, colPassiveText);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        
        ImGui::PushItemWidth(-1.0f);
        ImGuiInputTextFlags searchFlags = isOpen ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        ImGui::InputTextWithHint("##NodesPanelSearch", prompt, m_NodeBrowserSearchBuffer, sizeof(m_NodeBrowserSearchBuffer), searchFlags);
        ImGui::PopItemWidth();
        
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
        
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        // Added ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav to prevent default snappy scrolling and blue nav outlines
        ImGui::BeginChild("NodesPanelScrollingResults", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);

        // Premium smooth scrolling implementation
        static float targetScrollY = 0.0f;
        static bool isScrollInit = false;
        static float lastAppliedScrollY = -1.0f;
        
        float currentScrollY = ImGui::GetScrollY();
        float maxScrollY = ImGui::GetScrollMaxY();
        
        if (!isScrollInit || !isOpen) {
            targetScrollY = currentScrollY;
            lastAppliedScrollY = currentScrollY;
            isScrollInit = true;
        }
        
        targetScrollY = std::clamp(targetScrollY, 0.0f, maxScrollY);
        
        // Detect if the scroll position changed externally (e.g. scrollbar drag or click)
        bool externalScroll = false;
        if (lastAppliedScrollY >= 0.0f && std::abs(currentScrollY - lastAppliedScrollY) > 0.01f) {
            externalScroll = true;
            targetScrollY = currentScrollY;
            lastAppliedScrollY = currentScrollY;
        }
        
        if (isOpen && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().MouseWheel != 0.0f) {
            targetScrollY -= ImGui::GetIO().MouseWheel * 280.0f; // 280px scroll speed per wheel notch is responsive and matching standard OS speeds
            targetScrollY = std::clamp(targetScrollY, 0.0f, maxScrollY);
            externalScroll = false;
        }
        
        if (!externalScroll) {
            if (std::abs(currentScrollY - targetScrollY) > 0.5f) {
                float newScrollY = currentScrollY + (targetScrollY - currentScrollY) * dt * 20.0f;
                ImGui::SetScrollY(newScrollY);
                lastAppliedScrollY = newScrollY;
            } else {
                ImGui::SetScrollY(targetScrollY);
                lastAppliedScrollY = targetScrollY;
            }
        }

        std::string currentCategory;
        const NodeBrowserEntry* activatedEntry = nullptr;

        for (const NodeBrowserEntry& entryRef : entries) {
            const NodeBrowserEntry* entry = &entryRef;
            if (!entry) {
                continue;
            }
            const std::string key = EntryKey(*entry);
            const auto alphaIt = m_NodeBrowserEntryAlpha.find(key);
            const float entryAlpha = alphaIt != m_NodeBrowserEntryAlpha.end() ? alphaIt->second : 0.0f;
            if (entryAlpha <= 0.02f) {
                continue;
            }
            const bool matchesVisible = std::find(filtered.begin(), filtered.end(), entry) != filtered.end();
            
            if (entry->category != currentCategory && categoryAlpha[entry->category] > 0.02f) {
                currentCategory = entry->category;
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::min(alpha, categoryAlpha[currentCategory]));
                ImGui::TextDisabled("%s", currentCategory.c_str());
                ImGui::PopStyleVar();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
            }

            ImGui::PushID(key.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, entryAlpha * alpha);
            
            ImGui::PushStyleColor(ImGuiCol_Header, colActiveHeader);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colHoveredHeader);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, colActiveHeader);
            ImGui::PushStyleColor(ImGuiCol_Text, colNormalText);
            
            ImGuiSelectableFlags sFlags = isOpen ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled;
            bool pressed = ImGui::Selectable(entry->label.c_str(), false, sFlags, ImVec2(0, 22.0f));
            
            if (isOpen && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const NodeBrowserEntry* dragEntry = entry;
                ImGui::SetDragDropPayload("ADD_NODE_DRAG_PAYLOAD", &dragEntry, sizeof(NodeBrowserEntry*));
                ImGui::Text("Add %s", entry->label.c_str());
                ImGui::EndDragDropSource();
            }
            
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
            ImGui::PopID();
            
            if (pressed && matchesVisible && isOpen) {
                activatedEntry = entry;
            }
        }

        if (filtered.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, colPassiveText);
            ImGui::TextUnformatted("No matching nodes");
            ImGui::PopStyleColor();
        }

        ImGui::EndChild(); // end NodesPanelScrollingResults

        if (isOpen && !activatedEntry &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
            !filtered.empty()) {
            activatedEntry = filtered.front();
        }

        if (isOpen && activatedEntry) {
            const std::vector<int> selectedNodeIds = graph.GetSelectedNodeIds();
            const int previousSelectedNodeId = selectedNodeIds.size() == 1 ? selectedNodeIds.front() : -1;
            const EditorNodeGraph::Node* previousSelectedNode = previousSelectedNodeId > 0 ? graph.FindNode(previousSelectedNodeId) : nullptr;
            const std::string previousOutputSocket = previousSelectedNode ? graph.DefaultOutputSocket(*previousSelectedNode) : std::string();
            EditorNodeGraph::Link downstreamLinkCopy {};
            bool hasDownstreamLink = false;
            if (previousSelectedNode && !previousOutputSocket.empty()) {
                if (const EditorNodeGraph::Link* downstreamLink = graph.FindOutputLink(previousSelectedNodeId, previousOutputSocket)) {
                    downstreamLinkCopy = *downstreamLink;
                    hasDownstreamLink = true;
                }
            }

            const int newNodeId = AddNodeFromBrowserEntry(editor, *activatedEntry, m_NodeBrowserGraphPos);
            if (newNodeId > 0) {
                m_PushedSourceNodeId = -1;
                m_PushDistance = 0.0f;
                m_PushedNodeIds.clear();

                if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromOutput) {
                    EditorNodeGraphUI::ConnectOutputToBestInput(editor, m_NodeBrowserDragFromNodeId, m_NodeBrowserDragFromSocketId, newNodeId);
                } else if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromInput) {
                    EditorNodeGraphUI::ConnectBestOutputToInput(editor, newNodeId, m_NodeBrowserDragToNodeId, m_NodeBrowserDragToSocketId);
                } else if (previousSelectedNodeId > 0) {
                    bool inserted = false;
                    if (selectedNodeIds.size() == 1 && hasDownstreamLink && downstreamLinkCopy.fromNodeId == previousSelectedNodeId) {
                        if (editor->RemoveGraphLink(
                                downstreamLinkCopy.fromNodeId,
                                downstreamLinkCopy.fromSocketId,
                                downstreamLinkCopy.toNodeId,
                                downstreamLinkCopy.toSocketId)) {
                            const bool connectedFirst = EditorNodeGraphUI::ConnectOutputToBestInput(editor, previousSelectedNodeId, previousOutputSocket, newNodeId);
                            const bool connectedSecond = connectedFirst
                                ? EditorNodeGraphUI::ConnectBestOutputToInput(editor, newNodeId, downstreamLinkCopy.toNodeId, downstreamLinkCopy.toSocketId)
                                : false;
                            if (!connectedFirst || !connectedSecond) {
                                editor->RemoveGraphNode(newNodeId);
                                editor->ConnectGraphSockets(
                                    downstreamLinkCopy.fromNodeId,
                                    downstreamLinkCopy.fromSocketId,
                                    downstreamLinkCopy.toNodeId,
                                    downstreamLinkCopy.toSocketId,
                                    nullptr);
                            } else {
                                inserted = true;
                            }
                        }
                    }
                    if (!inserted && selectedNodeIds.size() == 1) {
                        EditorNodeGraphUI::ConnectOutputToBestInput(editor, previousSelectedNodeId, previousOutputSocket, newNodeId);
                    }
                }
            }
            CloseNodeBrowser();
        }

        if (isOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeBrowser = true;
        }

        ImGui::EndChild(); // end NodesPanelDrawerChild
    }

    if (closeBrowser && isOpen) {
        CloseNodeBrowser();
    }
}
