#include "EditorAdvancedNodeEditor.h"

#include "Editor/EditorModule.h"
#include "Editor/Layers/LayerBase.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <imgui.h>

EditorAdvancedNodeEditor::EditorAdvancedNodeEditor() {}
EditorAdvancedNodeEditor::~EditorAdvancedNodeEditor() {}

void EditorAdvancedNodeEditor::Initialize() {}

void EditorAdvancedNodeEditor::Render(EditorModule* editor, const ImVec2& workspacePos, const ImVec2& workspaceSize) {
    if (!editor) {
        return;
    }

    const bool editorOpen = editor->IsAdvancedEditorOpen();
    if (!editorOpen && m_VisibleAlpha <= 0.001f) {
        m_CloseRequested = false;
        return;
    }

    if (editorOpen && !m_CloseRequested) {
        m_VisibleAlpha = ImGuiExtras::AnimateTowards(m_VisibleAlpha, 1.0f, ImGui::GetIO().DeltaTime, 16.0f);
    } else {
        m_VisibleAlpha = ImGuiExtras::AnimateTowards(m_VisibleAlpha, 0.0f, ImGui::GetIO().DeltaTime, 18.0f);
        if (m_CloseRequested && m_VisibleAlpha <= 0.01f) {
            editor->CloseAdvancedEditor();
            m_CloseRequested = false;
            m_VisibleAlpha = 0.0f;
            return;
        }
        if (!editorOpen) {
            return;
        }
    }

    LayerBase* layer = editor->GetActiveAdvancedEditorLayer();
    const EditorNodeGraph::Node* node = editor->GetActiveAdvancedEditorNode();
    if (!layer || !node) {
        editor->CloseAdvancedEditor();
        m_CloseRequested = false;
        m_VisibleAlpha = 0.0f;
        return;
    }

    const ImVec2 overlayPos = workspacePos;
    const ImVec2 overlaySize = workspaceSize;
    const float easedAlpha = ImGuiExtras::EaseOutCubic(std::clamp(m_VisibleAlpha, 0.0f, 1.0f));
    const float inset = 20.0f;
    const ImVec2 panelPos(workspacePos.x + inset, workspacePos.y + inset);
    const ImVec2 panelSize(
        std::max(240.0f, workspaceSize.x - inset * 2.0f),
        std::max(220.0f, workspaceSize.y - inset * 2.0f));

    ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(overlaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::Begin(
            "##EditorAdvancedNodeEditorBlocker",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            overlayPos,
            ImVec2(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y),
            IM_COL32(5, 8, 11, static_cast<int>(120.0f * easedAlpha)));
        ImGui::SetCursorScreenPos(overlayPos);
        ImGui::InvisibleButton("##AdvancedEditorCapture", overlaySize);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, easedAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30.0f, 26.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(22, 30, 35, 246));
    bool closeRequested = false;
    if (ImGui::Begin(
            "##EditorAdvancedNodeEditorPanel",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoCollapse)) {
        const float buttonSize = 30.0f;
        ImGui::TextUnformatted(layer->GetAdvancedEditorTitle());
        ImGui::SameLine();
        ImGui::TextDisabled("Advanced Editor");
        ImGui::SameLine(std::max(0.0f, ImGui::GetContentRegionAvail().x - buttonSize));
        if (ImGui::Button("X", ImVec2(buttonSize, buttonSize))) {
            closeRequested = true;
        }
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        const nlohmann::json before = layer->Serialize();
        layer->RenderAdvancedEditor(editor);
        const nlohmann::json after = layer->Serialize();
        if (before != after) {
            editor->MarkRenderDirty(node->id);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        closeRequested = true;
    }

    if (closeRequested) {
        editor->SetPickingColor(false);
        m_CloseRequested = true;
    }
}
