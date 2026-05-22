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

bool ConnectOutputToBestInput(EditorModule* editor, int fromNodeId, const std::string& fromSocketId, int toNodeId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!to) {
        return false;
    }
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        std::string error;
        if (editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
            return true;
        }
    }
    return false;
}

bool ConnectBestOutputToInput(EditorModule* editor, int fromNodeId, int toNodeId, const std::string& toSocketId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    if (!from) {
        return false;
    }
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        std::string error;
        if (editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
            return true;
        }
    }
    return false;
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
        } else if (fromSocket.type == EditorNodeGraph::SocketType::Mask && fromSocketId == EditorNodeGraph::kMaskOutputSocketId) {
            if (prototype.kind == EditorNodeGraph::NodeKind::Layer && socket.id == EditorNodeGraph::kMaskInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Mix && socket.id == EditorNodeGraph::kMixFactorSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::MaskUtility && socket.id == EditorNodeGraph::kMaskInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Preview && socket.id == EditorNodeGraph::kPreviewInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Scope && socket.id == EditorNodeGraph::kScopeInputSocketId) return true;
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
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::ExportBoundsSettings:
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
    if (!m_NodeBrowserOpen || !editor) {
        return;
    }

    const double now = ImGui::GetTime();
    const float dt = static_cast<float>(std::clamp(now - m_NodeBrowserLastFrameTime, 0.0, 0.05));
    m_NodeBrowserLastFrameTime = now;
    const float alpha = std::clamp(static_cast<float>((now - m_NodeBrowserOpenedAt) / 0.18), 0.0f, 1.0f);
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
        const bool visible = matchesSearch && compatibleOutput && compatibleInput;
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

    const ImVec2 windowSize(620.0f, 430.0f);
    const float margin = 18.0f;
    ImVec2 windowPos = ImVec2(m_NodeBrowserScreenPos.x + 10.0f, m_NodeBrowserScreenPos.y + 10.0f);
    windowPos.x = std::clamp(windowPos.x, m_CanvasMin.x + margin, m_CanvasMax.x - windowSize.x - margin);
    windowPos.y = std::clamp(windowPos.y, m_CanvasMin.y + margin, m_CanvasMax.y - windowSize.y - margin);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(24, 31, 35, 244));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(74, 97, 108, 125));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(34, 43, 48, 235));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(44, 57, 63, 235));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(232, 238, 242, 255));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, IM_COL32(149, 163, 171, 255));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;
    bool closeBrowser = false;
    if (ImGui::Begin("##NodeBrowserPalette", nullptr, flags)) {
        const bool browserHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && !browserHovered) {
            closeBrowser = true;
        }
        if (m_NodeBrowserFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_NodeBrowserFocusSearch = false;
        }
        const char* prompt = (m_NodeBrowserMode == NodeBrowserMode::GeneralAdd)
            ? "Search nodes"
            : "Search compatible nodes";
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputTextWithHint("##NodeBrowserSearch", prompt, m_NodeBrowserSearchBuffer, sizeof(m_NodeBrowserSearchBuffer));
        ImGui::PopItemWidth();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::BeginChild("NodeBrowserResults", ImVec2(0.0f, 0.0f), false);
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
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::min(alpha, categoryAlpha[currentCategory]));
                ImGui::TextDisabled("%s", currentCategory.c_str());
                ImGui::PopStyleVar();
            }
            if (RenderBrowserRow(*entry, entryAlpha, matchesVisible)) {
                activatedEntry = entry;
            }
        }
        if (filtered.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextDisabled("No matching nodes");
        }
        ImGui::EndChild();

        if (!activatedEntry &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
            !filtered.empty()) {
            activatedEntry = filtered.front();
        }

        if (activatedEntry) {
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
                // Clear the push state to commit the positions of all downstream nodes
                m_PushedSourceNodeId = -1;
                m_PushDistance = 0.0f;
                m_PushedNodeIds.clear();

                if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromOutput) {
                    ConnectOutputToBestInput(editor, m_NodeBrowserDragFromNodeId, m_NodeBrowserDragFromSocketId, newNodeId);
                } else if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromInput) {
                    ConnectBestOutputToInput(editor, newNodeId, m_NodeBrowserDragToNodeId, m_NodeBrowserDragToSocketId);
                } else if (previousSelectedNodeId > 0) {
                    bool inserted = false;
                    if (selectedNodeIds.size() == 1 && hasDownstreamLink && downstreamLinkCopy.fromNodeId == previousSelectedNodeId) {
                        if (editor->RemoveGraphLink(
                                downstreamLinkCopy.fromNodeId,
                                downstreamLinkCopy.fromSocketId,
                                downstreamLinkCopy.toNodeId,
                                downstreamLinkCopy.toSocketId)) {
                            const bool connectedFirst = ConnectOutputToBestInput(editor, previousSelectedNodeId, previousOutputSocket, newNodeId);
                            const bool connectedSecond = connectedFirst
                                ? ConnectBestOutputToInput(editor, newNodeId, downstreamLinkCopy.toNodeId, downstreamLinkCopy.toSocketId)
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
                        ConnectOutputToBestInput(editor, previousSelectedNodeId, previousOutputSocket, newNodeId);
                    }
                }
            }
            CloseNodeBrowser();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeBrowser = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(7);

    if (closeBrowser) {
        CloseNodeBrowser();
    }
}
