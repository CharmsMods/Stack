#include "RawLabGalleryDetails.h"

#include "Raw/RawImageData.h"
#include "Raw/RawWorkspace.h"

#include <imgui.h>

#include <cstdio>
#include <string>

namespace Stack::Editor::RawLabInternal {

void GalleryDetails::Render(const RawWorkspace::SourceRecord* source, const Raw::RawMetadata* metadata) {
    if (source == nullptr) {
        ImGui::TextWrapped("Hover or select an image to see its properties.");
        return;
    }

    ImGui::TextWrapped("%s", source->fileName.c_str());
    ImGui::Spacing();
    const float unit = ImGui::GetFontSize() / 13.0f;
    const auto beginRows = [unit](const char* id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            return false;
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 78.0f * unit);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    };
    const auto row = [](const char* label, const std::string& value) {
        if (value.empty()) return;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", value.c_str());
    };

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 4.0f * unit));
    if (metadata) {
        const std::string camera = metadata->cameraMake.empty()
            ? metadata->cameraModel
            : metadata->cameraModel.rfind(metadata->cameraMake, 0) == 0
                ? metadata->cameraModel
                : metadata->cameraMake + " " + metadata->cameraModel;
        if (beginRows("CameraProperties")) {
            row("Camera", camera);
            row("Lens", metadata->lensModel);
            const int width = Raw::DisplayWidth(*metadata);
            const int height = Raw::DisplayHeight(*metadata);
            if (width > 0 && height > 0)
                row("Resolution", std::to_string(width) + " x " + std::to_string(height));
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (beginRows("ExposureProperties")) {
            char value[96] {};
            if (metadata->hasIsoSpeed) {
                std::snprintf(value, sizeof(value), "%.0f", metadata->isoSpeed);
                row("ISO", value);
            }
            if (metadata->hasExposureTime && metadata->exposureTimeSeconds > 0.0f) {
                if (metadata->exposureTimeSeconds < 1.0f) {
                    std::snprintf(value, sizeof(value), "1/%.0f s", 1.0f / metadata->exposureTimeSeconds);
                } else {
                    std::snprintf(value, sizeof(value), "%.2f s", metadata->exposureTimeSeconds);
                }
                row("Shutter", value);
            }
            if (metadata->hasApertureFNumber) {
                std::snprintf(value, sizeof(value), "f/%.1f", metadata->apertureFNumber);
                row("Aperture", value);
            }
            if (metadata->hasFocalLength) {
                std::snprintf(value, sizeof(value), "%.1f mm", metadata->focalLengthMm);
                row("Focal length", value);
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Camera metadata unavailable.");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    char fileSize[64] {};
    std::snprintf(fileSize, sizeof(fileSize), "%.2f MB",
        static_cast<double>(source->fileSizeBytes) / (1024.0 * 1024.0));
    if (beginRows("FileProperties")) {
        row("Size", fileSize);
        row("Type", source->extension);
        row("Folder", source->parentFolderKey);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

} // namespace Stack::Editor::RawLabInternal
