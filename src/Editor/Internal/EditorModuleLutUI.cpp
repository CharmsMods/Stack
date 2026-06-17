#include "Editor/EditorModule.h"

#include "Color/LutImporter.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <imgui.h>

namespace {

std::string LutDisplayLabelFromPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path(path).filename().string();
}

std::string LutTitleFromPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path(path).stem().string();
}

std::string LutSizeSummary(const ColorLut::LutPayload& payload) {
    if (ColorLut::HasShaper1D(payload) && ColorLut::HasLut3D(payload)) {
        return std::to_string(payload.shaper1D.size) + " + " + std::to_string(payload.lut3D.size) + "^3";
    }
    if (ColorLut::HasLut3D(payload)) {
        return std::to_string(payload.lut3D.size) + "^3";
    }
    if (ColorLut::HasLut1D(payload)) {
        return std::to_string(payload.lut1D.size);
    }
    return "None";
}

std::string LutDomainSummary(const std::array<float, 3>& domainMin, const std::array<float, 3>& domainMax) {
    char buffer[160];
    snprintf(
        buffer,
        sizeof(buffer),
        "(%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f)",
        domainMin[0],
        domainMin[1],
        domainMin[2],
        domainMax[0],
        domainMax[1],
        domainMax[2]);
    return std::string(buffer);
}

const std::array<float, 3>& PrimaryDomainMin(const ColorLut::LutPayload& payload) {
    if (ColorLut::HasShaper1D(payload)) {
        return payload.shaper1D.domainMin;
    }
    if (ColorLut::HasLut1D(payload)) {
        return payload.lut1D.domainMin;
    }
    return payload.lut3D.domainMin;
}

const std::array<float, 3>& PrimaryDomainMax(const ColorLut::LutPayload& payload) {
    if (ColorLut::HasShaper1D(payload)) {
        return payload.shaper1D.domainMax;
    }
    if (ColorLut::HasLut1D(payload)) {
        return payload.lut1D.domainMax;
    }
    return payload.lut3D.domainMax;
}

void PreserveLutViewSettings(const ColorLut::LutPayload& source, ColorLut::LutPayload& target) {
    target.useMode = source.useMode;
    target.inputTransform = source.inputTransform;
    target.outputTransform = source.outputTransform;
}

} // namespace

bool EditorModule::LoadLutNodeFromFile(int nodeId, const std::string& path, bool notifyOnFailure) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Lut) {
        return false;
    }

    const ColorLut::LutPayload previous = node->lut;
    ColorLut::LutImportResult import = ColorLut::ImportLutFile(path);
    PreserveLutViewSettings(previous, import.payload);

    if (!import.success) {
        ColorLut::ClearCanonicalLutData(import.payload);
        import.payload.sourcePath = path;
        if (import.payload.label.empty()) {
            import.payload.label = LutDisplayLabelFromPath(path);
        }
        if (import.payload.importedTitle.empty()) {
            import.payload.importedTitle = LutTitleFromPath(path);
        }
        import.payload.importError = import.message.empty() ? "Failed to import LUT." : import.message;
        node->lut = std::move(import.payload);
        MarkRenderDirty(nodeId);
        if (notifyOnFailure) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "LUT import failed: " + node->lut.importError,
                "lut-import-failed");
        }
        return false;
    }

    node->lut = std::move(import.payload);
    MarkRenderDirty(nodeId);
    return true;
}

bool EditorModule::ReloadLutNodeFromSourcePath(int nodeId, bool notifyOnFailure) {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Lut || node->lut.sourcePath.empty()) {
        if (notifyOnFailure) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "This LUT node does not have a source path to reload.",
                "lut-reload-missing-path");
        }
        return false;
    }
    return LoadLutNodeFromFile(nodeId, node->lut.sourcePath, notifyOnFailure);
}

bool EditorModule::ClearLutNodeData(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Lut) {
        return false;
    }

    ColorLut::LutPayload cleared = node->lut;
    cleared.sourcePath.clear();
    cleared.label.clear();
    cleared.importedTitle.clear();
    cleared.importError.clear();
    cleared.importFormat = ColorLut::LutImportFormat::Unknown;
    ColorLut::ClearCanonicalLutData(cleared);
    node->lut = std::move(cleared);
    MarkRenderDirty(nodeId);
    return true;
}

void EditorModule::RenderLutControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    (void)advanced;
    if (node.kind != EditorNodeGraph::NodeKind::Lut) {
        return;
    }

    auto openLutPicker = [&]() {
        const std::string path = FileDialogs::OpenLutFileDialog("Load LUT");
        if (!path.empty()) {
            LoadLutNodeFromFile(node.id, path, true);
        }
    };

    const bool hasLutData = ColorLut::HasAnyLutData(node.lut);
    const bool hasSourcePath = !node.lut.sourcePath.empty();
    bool changed = false;

    ImGuiExtras::RichSectionLabel("FILE", 4.0f);
    if (ImGuiExtras::RichFullWidthButton("Load LUT", controlWidth, 28.0f)) {
        openLutPicker();
    }
    if (hasLutData && ImGuiExtras::RichFullWidthButton("Replace LUT", controlWidth, 28.0f)) {
        openLutPicker();
    }
    ImGui::BeginDisabled(!hasSourcePath);
    if (ImGuiExtras::RichFullWidthButton("Reload From Disk", controlWidth, 28.0f)) {
        ReloadLutNodeFromSourcePath(node.id, true);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!hasLutData && !hasSourcePath && node.lut.importError.empty());
    if (ImGuiExtras::RichFullWidthButton("Clear LUT", controlWidth, 28.0f)) {
        ClearLutNodeData(node.id);
    }
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextDisabled("Path");
    if (!node.lut.sourcePath.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + controlWidth);
        ImGui::TextUnformatted(node.lut.sourcePath.c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextDisabled("Not loaded");
    }

    ImGui::TextDisabled("Imported Name");
    ImGui::TextUnformatted((node.lut.importedTitle.empty() ? "Untitled LUT" : node.lut.importedTitle).c_str());

    ImGui::TextDisabled("Format");
    ImGui::TextUnformatted(ColorLut::LutImportFormatLabel(node.lut.importFormat));

    ImGui::TextDisabled("Type");
    ImGui::TextUnformatted(ColorLut::LutTypeSummary(node.lut));

    ImGui::TextDisabled("Size");
    ImGui::TextUnformatted(LutSizeSummary(node.lut).c_str());

    if (hasLutData) {
        ImGui::TextDisabled("Domain");
        ImGui::TextUnformatted(LutDomainSummary(PrimaryDomainMin(node.lut), PrimaryDomainMax(node.lut)).c_str());
    }

    if (!node.lut.importError.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "Import Error");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + controlWidth);
        ImGui::TextUnformatted(node.lut.importError.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGuiExtras::RichSectionLabel("LOOKUP MODE", 4.0f);
    int useMode = static_cast<int>(node.lut.useMode);
    const char* modeOptions[] = { "Post View Transform", "Pre View Transform" };
    if (ImGuiExtras::NodeCombo("Mode", "##LutUseMode", &useMode, modeOptions, IM_ARRAYSIZE(modeOptions), controlWidth)) {
        const auto newMode = static_cast<ColorLut::LutUseMode>(std::clamp(useMode, 0, 1));
        ColorLut::ApplyModeDefaultsIfUnmodified(node.lut, newMode);
        changed = true;
    }

    int inputTransform = static_cast<int>(node.lut.inputTransform);
    const char* inputOptions[] = { "None", "sRGB Encode", "Gamma 2.2 Encode" };
    if (ImGuiExtras::NodeCombo("Input Transform", "##LutInputTransform", &inputTransform, inputOptions, IM_ARRAYSIZE(inputOptions), controlWidth)) {
        node.lut.inputTransform = static_cast<ColorLut::LutTransferFunction>(std::clamp(inputTransform, 0, 2));
        changed = true;
    }

    int outputTransform = 0;
    switch (node.lut.outputTransform) {
        case ColorLut::LutTransferFunction::SrgbDecode: outputTransform = 1; break;
        case ColorLut::LutTransferFunction::Gamma22Decode: outputTransform = 2; break;
        case ColorLut::LutTransferFunction::None:
        case ColorLut::LutTransferFunction::SrgbEncode:
        case ColorLut::LutTransferFunction::Gamma22Encode:
        default:
            outputTransform = 0;
            break;
    }
    const char* outputOptions[] = { "None", "sRGB Decode", "Gamma 2.2 Decode" };
    if (ImGuiExtras::NodeCombo("Output Transform", "##LutOutputTransform", &outputTransform, outputOptions, IM_ARRAYSIZE(outputOptions), controlWidth)) {
        node.lut.outputTransform = outputTransform == 1
            ? ColorLut::LutTransferFunction::SrgbDecode
            : (outputTransform == 2
                ? ColorLut::LutTransferFunction::Gamma22Decode
                : ColorLut::LutTransferFunction::None);
        changed = true;
    }

    if (changed) {
        MarkRenderDirty(node.id);
    }
}
