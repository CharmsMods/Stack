#include "PipelineTab.h"
#include "Editor/EditorModule.h"
#include <imgui.h>

void PipelineTab::Initialize() {}

void PipelineTab::Render(EditorModule* editor) {
    auto& layers = editor->GetLayers();
    int selectedIdx = editor->GetSelectedLayerIndex();

    ImGui::Text("Rendering Stack (Sequential)");
    ImGui::TextDisabled("Drag and drop to reorder processing flow");
    ImGui::Separator();
    ImGui::Spacing();

    if (layers.empty()) {
        ImGui::TextDisabled("No layers in pipeline.");
        ImGui::TextDisabled("Add one from the 'Layers' tab.");
        return;
    }

    for (int n = 0; n < (int)layers.size(); n++) {
        auto& layer = layers[n];
        ImGui::PushID(n);

        // Visibility Toggle
        bool visible = layer->IsVisible();
        if (ImGui::Checkbox("##vis", &visible)) {
            layer->SetVisible(visible);
        }
        ImGui::SameLine();

        // Layer Selectable with Drag and Drop Support
        bool isSelected = (n == selectedIdx);
        char label[128];
        snprintf(label, sizeof(label), "%d. %s", n + 1, layer->GetName());

        if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_AllowOverlap)) {
            editor->SetSelectedLayerIndex(n);
        }

        // Drag Source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("DND_LAYER_INDEX", &n, sizeof(int));
            ImGui::Text("Moving %s", layer->GetName());
            ImGui::EndDragDropSource();
        }

        // Drop Target
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_LAYER_INDEX")) {
                int fromIndex = *(const int*)payload->Data;
                editor->MoveLayer(fromIndex, n);
            }
            ImGui::EndDragDropTarget();
        }

        // Delete Button (Small and on the right)
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
        if (ImGui::Button("x", ImVec2(20, 20))) {
            editor->RemoveLayer(n);
        }

        ImGui::PopID();
    }
}
