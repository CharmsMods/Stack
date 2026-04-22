#include "CanvasTab.h"

#include "Editor/EditorModule.h"
#include "Utils/FileDialogs.h"
#include <imgui.h>

void CanvasTab::Initialize() {}

void CanvasTab::Render(EditorModule* editor) {
    auto& pipeline = editor->GetPipeline();
    const bool sourceLoadBusy = editor->IsSourceLoadBusy();
    const bool exportBusy = editor->IsExportBusy();
    const bool anyCanvasTaskBusy = sourceLoadBusy || exportBusy;

    ImGui::Text("Canvas Settings");
    ImGui::Separator();

    if (pipeline.HasSourceImage()) {
        ImGui::Text("Dimensions: %d x %d", pipeline.GetCanvasWidth(), pipeline.GetCanvasHeight());
        ImGui::Spacing();

        ImGui::BeginDisabled(anyCanvasTaskBusy);
        if (ImGui::Button("Replace Image...")) {
            std::string path = FileDialogs::OpenImageFileDialog("Load Source Image");
            if (!path.empty()) {
                editor->RequestLoadSourceImage(path);
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Export Rendered Image...")) {
            std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
            if (!path.empty()) {
                editor->RequestExportImage(path);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("(PNG, JPG, BMP, TGA)");
    } else {
        ImGui::TextWrapped("No source image loaded. Load an image to begin editing.");
        ImGui::Spacing();

        ImGui::BeginDisabled(anyCanvasTaskBusy);
        if (ImGui::Button("Load Image...")) {
            std::string path = FileDialogs::OpenImageFileDialog("Load Source Image");
            if (!path.empty()) {
                editor->RequestLoadSourceImage(path);
            }
        }
        ImGui::EndDisabled();
    }

    if (!editor->GetSourceLoadStatusText().empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", editor->GetSourceLoadStatusText().c_str());
    }
    if (!editor->GetExportStatusText().empty()) {
        ImGui::TextDisabled("%s", editor->GetExportStatusText().c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Global Rendering Options");
    bool onlyUpToActive = editor->IsRenderOnlyUpToActive();
    if (ImGui::Checkbox("Only Render Up To Active Layer", &onlyUpToActive)) {
        editor->SetRenderOnlyUpToActive(onlyUpToActive);
    }
    ImGui::TextDisabled("When enabled, the pipeline stops at the currently\nselected layer in the Pipeline tab.");
}
