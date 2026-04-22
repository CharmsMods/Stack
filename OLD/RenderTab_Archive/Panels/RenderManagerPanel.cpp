#include "RenderManagerPanel.h"

#include <imgui.h>
#include <cstdio>

namespace RenderManagerPanel {

RenderManagerAction Render(const RenderManagerPanelModel& model) {
    RenderManagerAction action = RenderManagerAction::None;

    if (ImGui::Button("Resume Viewport")) {
        action = RenderManagerAction::StartPreview;
    }

    ImGui::SameLine();
    if (ImGui::Button("Pause Viewport")) {
        action = RenderManagerAction::CancelPreview;
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset Viewport")) {
        action = RenderManagerAction::ResetAccumulation;
    }

    ImGui::SameLine();
    if (ImGui::Button("Save Project")) {
        action = RenderManagerAction::SaveProjectToLibrary;
    }

    ImGui::SameLine();
    if (ImGui::Button("Save As")) {
        action = RenderManagerAction::SaveProjectAsToLibrary;
    }

    ImGui::SameLine();
    if (model.job.IsFinalRequested() || model.job.IsFinalRunning()) {
        if (ImGui::Button("Cancel Final")) {
            action = RenderManagerAction::CancelFinalRender;
        }
    } else if (ImGui::Button("Render Final")) {
        action = RenderManagerAction::StartFinalRender;
    }

    ImGui::Spacing();
    ImGui::Text("Project: %s%s", model.projectName.empty() ? "Untitled Render Scene" : model.projectName.c_str(), model.hasUnsavedChanges ? " *" : "");
    ImGui::Text("Viewport State: %s", model.job.GetStateLabel());
    ImGui::TextWrapped("Status: %s", model.job.GetStatusText().c_str());
    ImGui::TextWrapped("Last Reset: %s", model.job.GetLastResetReason().c_str());
    ImGui::Text("Final State: %s", model.job.GetFinalStateLabel());
    ImGui::TextWrapped("Final Status: %s", model.job.GetFinalStatusText().c_str());
    if (!model.job.GetLatestFinalAssetFileName().empty()) {
        ImGui::TextWrapped("Latest Asset: %s", model.job.GetLatestFinalAssetFileName().c_str());
    }
    ImGui::Separator();

    int finalResolutionX = model.settings.GetFinalRenderResolutionX();
    int finalResolutionY = model.settings.GetFinalRenderResolutionY();
    if (ImGui::InputInt("Final Width", &finalResolutionX)) {
        model.settings.SetFinalRenderResolution(finalResolutionX, model.settings.GetFinalRenderResolutionY());
    }
    if (ImGui::InputInt("Final Height", &finalResolutionY)) {
        model.settings.SetFinalRenderResolution(model.settings.GetFinalRenderResolutionX(), finalResolutionY);
    }

    int finalSampleTarget = model.settings.GetFinalRenderSampleTarget();
    if (ImGui::SliderInt("Final Samples", &finalSampleTarget, 1, 2048)) {
        model.settings.SetFinalRenderSampleTarget(finalSampleTarget);
    }

    int finalBounceCount = model.settings.GetFinalRenderMaxBounceCount();
    if (ImGui::SliderInt("Final Max Bounces", &finalBounceCount, 1, 16)) {
        model.settings.SetFinalRenderMaxBounceCount(finalBounceCount);
    }

    char outputNameBuffer[160] = {};
    std::snprintf(outputNameBuffer, sizeof(outputNameBuffer), "%s", model.settings.GetFinalRenderOutputName().c_str());
    if (ImGui::InputText("Final Output Name", outputNameBuffer, sizeof(outputNameBuffer))) {
        model.settings.SetFinalRenderOutputName(outputNameBuffer);
    }

    ImGui::Spacing();

    if (ImGui::BeginTable("RenderManagerState", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Viewport Live");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(model.job.IsPreviewRequested() ? "Yes" : "No");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Sample Count");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%u | goal %d", model.buffers.GetSampleCount(), model.settings.GetPreviewSampleTarget());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Output Resolution");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d x %d", model.settings.GetResolutionX(), model.settings.GetResolutionY());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Final Resolution");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d x %d", model.settings.GetFinalRenderResolutionX(), model.settings.GetFinalRenderResolutionY());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Accumulation");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(model.settings.IsAccumulationEnabled() ? "Enabled" : "Single Sample");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Scene Lights");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", model.scene.GetLightCount());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Final Progress");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%u / %u", model.job.GetFinalSampleCount(), model.job.GetFinalSampleTarget());

        ImGui::EndTable();
    }

    return action;
}

} // namespace RenderManagerPanel
