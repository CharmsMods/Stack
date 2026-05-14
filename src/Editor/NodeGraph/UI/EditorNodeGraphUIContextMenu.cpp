#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Library/LibraryManager.h"
#include "Utils/FileDialogs.h"
#include <cctype>

namespace {

std::string DefaultProjectExportFileName(const EditorModule* editor) {
    if (!editor) {
        return "project.stack";
    }
    if (!editor->GetCurrentProjectFileName().empty()) {
        return editor->GetCurrentProjectFileName();
    }
    if (!editor->GetCurrentProjectName().empty()) {
        std::string stem;
        for (unsigned char ch : editor->GetCurrentProjectName()) {
            if (std::isalnum(ch) || ch == '_' || ch == '-') {
                stem.push_back(static_cast<char>(ch));
            } else if (std::isspace(ch)) {
                stem.push_back('_');
            }
        }
        while (!stem.empty() && stem.back() == '_') {
            stem.pop_back();
        }
        if (!stem.empty()) {
            return stem + ".stack";
        }
    }
    return "project.stack";
}

} // namespace

void EditorNodeGraphUI::RenderContextMenu(EditorModule* editor) {
    if (!ImGui::BeginPopup("EditorNodeGraphContextMenu")) {
        return;
    }

    if (m_ContextTarget == ContextTarget::Node) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(m_ContextNodeId);
        if (node) {
            ImGui::TextDisabled("%s", node->title.c_str());
            const bool canDeleteNode =
                node->kind != EditorNodeGraph::NodeKind::Output ||
                editor->GetNodeGraph().GetOutputNodeIds().size() > 1;
            if (canDeleteNode && ImGui::MenuItem("Delete Node")) {
                editor->RemoveGraphNode(node->id);
            }
            if (node->kind == EditorNodeGraph::NodeKind::Layer && editor->LayerSupportsAdvancedEditor(node->layerIndex)) {
                if (ImGui::MenuItem("Open Editor")) {
                    editor->OpenAdvancedEditorForNode(node->id);
                }
            } else if (ImGui::MenuItem(node->expanded ? "Collapse" : "Expand")) {
                node->expanded = !node->expanded;
                if (!node->expanded) {
                    editor->ClearGraphAutoFocusIfTrackedNode(node->id);
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::Output) {
                ImGui::BeginDisabled(!editor->GetNodeGraph().IsOutputConnected() || editor->IsExportBusy());
                if (ImGui::MenuItem("Export")) {
                    const std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
                    if (!path.empty()) {
                        editor->RequestExportImage(path);
                    }
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndPopup();
        return;
    }

    if (m_ContextTarget == ContextTarget::Link) {
        if (ImGui::MenuItem("Delete Link")) {
            editor->RemoveGraphLink(m_ContextLink.fromNodeId, m_ContextLink.fromSocketId, m_ContextLink.toNodeId, m_ContextLink.toSocketId);
        }
        ImGui::EndPopup();
        return;
    }

    const bool canSaveProject = editor && editor->GetPipeline().HasSourceImage();
    const bool saveBusy = Async::IsBusy(LibraryManager::Get().GetSaveTaskState());
    const bool exportBusy = editor && editor->IsExportBusy();
    if (ImGui::BeginMenu("Save...")) {
        ImGui::BeginDisabled(!canSaveProject || saveBusy);
        if (ImGui::MenuItem("To Library")) {
            const std::string projectName = editor->GetCurrentProjectName().empty()
                ? "New Project"
                : editor->GetCurrentProjectName();
            LibraryManager::Get().RequestSaveProject(projectName, editor, editor->GetCurrentProjectFileName());
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!canSaveProject || exportBusy);
        if (ImGui::MenuItem("Export File")) {
            const std::string path = FileDialogs::SaveProjectFileDialog("Export Project File", DefaultProjectExportFileName(editor).c_str());
            if (!path.empty()) {
                editor->RequestExportProject(path);
            }
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Add Image")) {
            editor->PromptAddImageNodeAt(m_ContextGraphPos);
        }
        if (ImGui::MenuItem("Add Output")) {
            editor->AddOutputNodeAt(m_ContextGraphPos);
        }

        if (ImGui::MenuItem("Add Node...")) {
            OpenNodeBrowser(NodeBrowserMode::GeneralAdd, m_ContextGraphPos);
        }
        if (ImGui::BeginMenu("Scopes")) {
            if (ImGui::MenuItem("Histogram")) {
                editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::Histogram, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Vectorscope")) {
                editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::Vectorscope, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("RGB Parade")) {
                editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::RGBParade, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Analysis")) {
            if (ImGui::MenuItem("Preview")) {
                editor->AddPreviewNodeAt(m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mask")) {
            if (ImGui::MenuItem("Solid Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::Solid, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Linear Gradient Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::LinearGradient, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Radial Gradient Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::RadialGradient, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Noise Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::Noise, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mask Utility")) {
            if (ImGui::MenuItem("Invert Mask")) {
                editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Invert, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Levels Mask")) {
                editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Levels, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Threshold Mask")) {
                editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Threshold, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Generator")) {
            if (ImGui::MenuItem("Luminance Mask")) {
                editor->AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind::Luminance, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Solid Color Image")) {
                editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::SolidColor, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Color Gradient Image")) {
                editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::ColorGradient, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Square")) {
                editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::Square, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Circle")) {
                editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::Circle, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Merge")) {
            if (ImGui::MenuItem("Mix")) {
                editor->AddMixNodeAt(m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Auto Layout")) {
            editor->AutoLayoutGraph();
        }
        ImGui::EndMenu();
    }

    ImGui::EndPopup();
}
