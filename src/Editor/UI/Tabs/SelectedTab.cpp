#include "SelectedTab.h"
#include "Editor/EditorModule.h"
#include <imgui.h>

void SelectedTab::Initialize() {}

void SelectedTab::Render(EditorModule* editor) {
    auto& layers = editor->GetLayers();
    int idx = editor->GetSelectedLayerIndex();

    if (idx < 0 || idx >= (int)layers.size()) {
        ImGui::TextDisabled("Select a layer in the Pipeline tab to inspect its parameters.");
        return;
    }

    auto& layer = layers[idx];

    ImGui::Text("%s", layer->GetName());
    ImGui::TextDisabled("Category: %s", layer->GetCategory());
    ImGui::Separator();
    ImGui::Spacing();

    // Each layer draws its own controls via the virtual RenderUI()
    layer->RenderUI(editor);
}
