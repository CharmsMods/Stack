#include "Tools/ToolsModule.h"

#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

namespace {

const char* kUseModeLabels[] = {
    "Post View Transform",
    "Pre View Transform"
};

const char* kTransferLabels[] = {
    "None",
    "sRGB Encode",
    "Gamma 2.2 Encode",
    "sRGB Decode",
    "Gamma 2.2 Decode"
};

int UseModeToIndex(ColorLut::LutUseMode mode) {
    return mode == ColorLut::LutUseMode::PreViewTransform ? 1 : 0;
}

ColorLut::LutUseMode UseModeFromIndex(int index) {
    return index == 1
        ? ColorLut::LutUseMode::PreViewTransform
        : ColorLut::LutUseMode::PostViewTransform;
}

int TransferToIndex(ColorLut::LutTransferFunction transform) {
    switch (transform) {
        case ColorLut::LutTransferFunction::SrgbEncode: return 1;
        case ColorLut::LutTransferFunction::Gamma22Encode: return 2;
        case ColorLut::LutTransferFunction::SrgbDecode: return 3;
        case ColorLut::LutTransferFunction::Gamma22Decode: return 4;
        case ColorLut::LutTransferFunction::None:
        default:
            return 0;
    }
}

ColorLut::LutTransferFunction TransferFromIndex(int index) {
    switch (index) {
        case 1: return ColorLut::LutTransferFunction::SrgbEncode;
        case 2: return ColorLut::LutTransferFunction::Gamma22Encode;
        case 3: return ColorLut::LutTransferFunction::SrgbDecode;
        case 4: return ColorLut::LutTransferFunction::Gamma22Decode;
        case 0:
        default:
            return ColorLut::LutTransferFunction::None;
    }
}

std::string FileStem(const std::string& path) {
    try {
        return std::filesystem::path(path).stem().string();
    } catch (...) {
        return {};
    }
}

std::string FileName(const std::string& path) {
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path;
    }
}

std::string FormatPercent(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f%%", value * 100.0f);
    return std::string(buffer);
}

std::string FormatInteger(int value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return std::string(buffer);
}

std::string FormatImageDimensions(const ToolsModule::PreviewImageState& state) {
    if (state.image.width <= 0 || state.image.height <= 0) {
        return "Not loaded";
    }

    char buffer[64];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%d x %d  (%d ch)",
        state.image.width,
        state.image.height,
        std::max(1, state.image.originalChannels));
    return std::string(buffer);
}

void BeginPanel(const char* id, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
}

void EndPanel() {
    ImGui::EndChild();
}

} // namespace

ToolsModule::ToolsModule() = default;

ToolsModule::~ToolsModule() {
    ReleasePreviewImage(m_SourceImage);
    ReleasePreviewImage(m_TargetImage);
}

void ToolsModule::Initialize() {
}

void ToolsModule::RenderUI(std::function<void(ColorLut::LutPayload)> onOpenGeneratedLut) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 22.0f));
    ImGui::BeginChild("ToolsWorkspace", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    RenderShellHeader();
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    RenderToolTabs();
    ImGui::Dummy(ImVec2(0.0f, 14.0f));

    if (m_SelectedPage == ToolPage::Overview) {
        RenderOverview();
    } else {
        RenderLutCreator(onOpenGeneratedLut);
    }

    ImGui::EndChild();
}

void ToolsModule::ReleasePreviewImage(PreviewImageState& state) {
    if (state.texture != 0) {
        glDeleteTextures(1, &state.texture);
        state.texture = 0;
    }
    state.image = {};
}

bool ToolsModule::LoadPreviewImage(
    const std::string& path,
    PreviewImageState& state,
    const char* imageRole) {
    ColorLut::LutCreatorImage loadedImage;
    std::string message;
    if (!ColorLut::LoadRasterImageForLutCreator(path, loadedImage, &message)) {
        SetStatus(std::string(imageRole) + ": " + message, true);
        return false;
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(
        loadedImage.pixels.data(),
        loadedImage.width,
        loadedImage.height,
        loadedImage.channels);
    if (texture == 0) {
        SetStatus(std::string(imageRole) + ": preview texture creation failed.", true);
        return false;
    }

    ReleasePreviewImage(state);
    state.image = std::move(loadedImage);
    state.texture = texture;
    InvalidateGeneratedLut();
    SetStatus(std::string("Loaded ") + imageRole + " image.", false);
    return true;
}

void ToolsModule::InvalidateGeneratedLut() {
    m_HasGeneratedPayload = false;
    m_GeneratedPayload = {};
    m_LastStats = {};
}

void ToolsModule::ApplyRuntimeMetadataToGeneratedPayload() {
    m_LutSettings.label = m_LabelBuffer[0] != '\0' ? std::string(m_LabelBuffer) : std::string();
    m_LutSettings.importedTitle = m_TitleBuffer[0] != '\0' ? std::string(m_TitleBuffer) : std::string();

    if (!m_HasGeneratedPayload) {
        return;
    }

    const std::string title = m_LutSettings.importedTitle.empty()
        ? BuildCurrentGeneratedTitle()
        : m_LutSettings.importedTitle;
    const std::string label = m_LutSettings.label.empty()
        ? title
        : m_LutSettings.label;

    m_GeneratedPayload.label = label;
    m_GeneratedPayload.importedTitle = title;
    m_GeneratedPayload.useMode = m_LutSettings.useMode;
    m_GeneratedPayload.inputTransform = m_LutSettings.inputTransform;
    m_GeneratedPayload.outputTransform = m_LutSettings.outputTransform;
}

bool ToolsModule::GenerateCurrentLut() {
    ApplyRuntimeMetadataToGeneratedPayload();
    ColorLut::LutCreatorResult result = ColorLut::CreateLutFromImages(
        m_SourceImage.image,
        m_TargetImage.image,
        m_LutSettings);
    if (!result.success) {
        InvalidateGeneratedLut();
        SetStatus(result.message.empty() ? "LUT generation failed." : result.message, true);
        return false;
    }

    m_HasGeneratedPayload = true;
    m_GeneratedPayload = std::move(result.payload);
    m_LastStats = result.stats;
    ApplyRuntimeMetadataToGeneratedPayload();

    char message[256];
    std::snprintf(
        message,
        sizeof(message),
        "Generated %d^3 LUT. MAE %s, coverage %s, sampled %d pixels.",
        m_GeneratedPayload.lut3D.size,
        FormatPercent(m_LastStats.meanAbsoluteError).c_str(),
        FormatPercent(m_LastStats.voxelCoverage).c_str(),
        m_LastStats.sampledPixelCount);
    SetStatus(message, false);
    return true;
}

bool ToolsModule::SaveCurrentLut(
    bool openInEditor,
    const std::function<void(ColorLut::LutPayload)>& onOpenGeneratedLut) {
    if (!m_HasGeneratedPayload) {
        SetStatus("Generate a LUT before saving it.", true);
        return false;
    }

    ApplyRuntimeMetadataToGeneratedPayload();
    const std::string cubePath = FileDialogs::SaveLutFileDialog(
        openInEditor ? "Save LUT And Open In Editor" : "Save LUT",
        BuildDefaultCubeFileName().c_str());
    if (cubePath.empty()) {
        return false;
    }

    ColorLut::LutPayload payload = m_GeneratedPayload;
    payload.sourcePath = cubePath;
    if (payload.importedTitle.empty()) {
        payload.importedTitle = BuildCurrentGeneratedTitle();
    }
    if (payload.label.empty()) {
        payload.label = payload.importedTitle;
    }

    std::string message;
    if (!ColorLut::SaveCubeLutWithSidecar(
            cubePath,
            payload,
            m_LutSettings,
            m_LastStats,
            m_SourceImage.image.sourcePath,
            m_TargetImage.image.sourcePath,
            &message)) {
        SetStatus(message.empty() ? "Saving the LUT failed." : message, true);
        return false;
    }

    m_LastSavedCubePath = cubePath;
    m_GeneratedPayload = payload;
    SetStatus(message, false);

    if (openInEditor && onOpenGeneratedLut) {
        onOpenGeneratedLut(payload);
    }
    return true;
}

void ToolsModule::RenderShellHeader() {
    BeginPanel("ToolsShellHeader", ImVec2(0.0f, 114.0f));

    if (ImGui::BeginTable("ToolsShellHeaderTable", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Main", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("Stats", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Tools");
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled(
            "A utility workspace for creating reusable assets, checking solve quality, and sending the result back into the editor.");
        ImGui::PopTextWrapPos();

        ImGui::TableNextColumn();
        if (ImGui::BeginTable("ToolsShellStats", 3, ImGuiTableFlags_SizingStretchSame)) {
            const char* labels[3] = { "Live Tools", "Output", "Editor Handoff" };
            const char* values[3] = { "1", ".cube + sidecar", "Ready" };
            for (int i = 0; i < 3; ++i) {
                ImGui::TableNextColumn();
                BeginPanel((std::string("ToolsShellStat") + std::to_string(i)).c_str(), ImVec2(0.0f, 68.0f));
                ImGui::TextUnformatted(values[i]);
                ImGui::TextDisabled("%s", labels[i]);
                EndPanel();
            }
            ImGui::EndTable();
        }
        ImGui::EndTable();
    }
    EndPanel();
}

void ToolsModule::RenderToolTabs() {
    const ImVec4 activeButton = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    const ImVec4 headerColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    const ImVec4 inactiveButton = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    const ImVec4 hoveredButton = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);

    const struct {
        ToolPage page;
        const char* label;
        const char* blurb;
    } tabs[] = {
        { ToolPage::Overview, "Overview", "See the active tools shelf and future expansion slots." },
        { ToolPage::LutCreator, "LUT Creator", "Build a .cube LUT from a source and target image pair." }
    };

    if (ImGui::BeginTable("ToolsSubTabs", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (const auto& tab : tabs) {
            ImGui::TableNextColumn();
            const bool selected = m_SelectedPage == tab.page;
            ImGui::PushID(tab.label);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? headerColor : inactiveButton);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? activeButton : hoveredButton);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeButton);
            if (ImGui::Button(tab.label, ImVec2(ImGui::GetContentRegionAvail().x, 34.0f))) {
                m_SelectedPage = tab.page;
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGui::TextDisabled("%s", tab.blurb);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void ToolsModule::RenderOverview() {
    if (ImGui::BeginTable("ToolsOverviewLayout", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        BeginPanel("OverviewActiveTool");
        ImGuiExtras::RichSectionLabel("ACTIVE TOOL", 6.0f);
        ImGui::TextUnformatted("LUT Creator");
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled(
            "Generate a 3D LUT that approximates the transformation from one image to another, save it as a portable .cube, and reopen it directly as a LUT node.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::BulletText("Exact-match image dimensions for now");
        ImGui::BulletText("Advanced solve controls and runtime metadata");
        ImGui::BulletText("Save + open handoff back into the editor");
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (ImGuiExtras::RichFullWidthButton("Open LUT Creator", ImGui::GetContentRegionAvail().x, 34.0f)) {
            m_SelectedPage = ToolPage::LutCreator;
        }
        EndPanel();

        ImGui::TableNextColumn();
        BeginPanel("OverviewFutureSlot");
        ImGuiExtras::RichSectionLabel("FUTURE SLOTS", 6.0f);
        ImGui::TextUnformatted("More tools can live here next.");
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled(
            "The Tools root tab now has its own sub-tab shell, so future utilities can land beside LUT Creator instead of competing for one long page.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::BulletText("Batch asset prep");
        ImGui::BulletText("Format conversion");
        ImGui::BulletText("Reference analysis");
        EndPanel();
        ImGui::EndTable();
    }
}

void ToolsModule::RenderLutCreator(const std::function<void(ColorLut::LutPayload)>& onOpenGeneratedLut) {
    RenderStatusBanner();

    if (ImGui::BeginTable("LutCreatorLayout", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Main", ImGuiTableColumnFlags_WidthStretch, 1.65f);
        ImGui::TableSetupColumn("Side", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextColumn();
        RenderLutCreatorInputs();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        RenderLutCreatorMetadataEditor();
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        RenderLutCreatorSolveControls();

        ImGui::TableNextColumn();
        RenderLutCreatorActionPanel(onOpenGeneratedLut);
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        RenderLutCreatorReadoutPanel();

        ImGui::EndTable();
    }
}

void ToolsModule::RenderStatusBanner() {
    if (m_StatusMessage.empty()) {
        return;
    }

    const ImVec4 bannerColor = m_StatusIsError
        ? ImVec4(0.34f, 0.10f, 0.10f, 0.84f)
        : ImVec4(0.09f, 0.18f, 0.13f, 0.84f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bannerColor);
    BeginPanel("LutCreatorStatusBanner", ImVec2(0.0f, 62.0f));
    ImGui::TextUnformatted(m_StatusIsError ? "Attention" : "Latest Action");
    ImGui::PushTextWrapPos();
    ImGui::TextWrapped("%s", m_StatusMessage.c_str());
    ImGui::PopTextWrapPos();
    EndPanel();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
}

void ToolsModule::RenderLutCreatorInputs() {
    BeginPanel("LutCreatorInputsPanel");
    ImGuiExtras::RichSectionLabel("REFERENCE PAIR", 6.0f);
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled(
        "Choose the source look and the target look. The generator samples how colors change between them and fits that into a reusable 3D LUT.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::BeginTable("LutCreatorInputCards", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextColumn();
        RenderImageCard("Source Image", m_SourceImage, "Choose Source Image", "Load Source Image");
        ImGui::TableNextColumn();
        RenderImageCard("Target Image", m_TargetImage, "Choose Target Image", "Load Target Image");
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::SeparatorText("Pair Status");
    ImGui::TextUnformatted(BuildPairStatusLabel().c_str());
    if (!IsReadyToGenerate()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("Generation stays locked until both images are loaded and the source and target dimensions match exactly.");
        ImGui::PopTextWrapPos();
    }
    EndPanel();
}

void ToolsModule::RenderLutCreatorMetadataEditor() {
    BeginPanel("LutCreatorMetadataPanel");
    ImGuiExtras::RichSectionLabel("RUNTIME METADATA", 6.0f);
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("These values travel with the generated LUT so the node reads clearly once it lands back in the editor.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    bool runtimeChanged = false;
    if (ImGui::BeginTable("LutCreatorMetadataTable", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Imported Title");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        runtimeChanged |= ImGui::InputTextWithHint(
            "##LutCreatorTitle",
            BuildCurrentGeneratedTitle().c_str(),
            m_TitleBuffer,
            sizeof(m_TitleBuffer));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Node Label");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        runtimeChanged |= ImGui::InputTextWithHint(
            "##LutCreatorLabel",
            BuildCurrentGeneratedTitle().c_str(),
            m_LabelBuffer,
            sizeof(m_LabelBuffer));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Use Mode");
        ImGui::TableNextColumn();
        int useModeIndex = UseModeToIndex(m_LutSettings.useMode);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##LutCreatorUseMode", &useModeIndex, kUseModeLabels, IM_ARRAYSIZE(kUseModeLabels))) {
            m_LutSettings.useMode = UseModeFromIndex(useModeIndex);
            runtimeChanged = true;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Input Transform");
        ImGui::TableNextColumn();
        int inputTransformIndex = TransferToIndex(m_LutSettings.inputTransform);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##LutCreatorInputTransform", &inputTransformIndex, kTransferLabels, IM_ARRAYSIZE(kTransferLabels))) {
            m_LutSettings.inputTransform = TransferFromIndex(inputTransformIndex);
            runtimeChanged = true;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Output Transform");
        ImGui::TableNextColumn();
        int outputTransformIndex = TransferToIndex(m_LutSettings.outputTransform);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##LutCreatorOutputTransform", &outputTransformIndex, kTransferLabels, IM_ARRAYSIZE(kTransferLabels))) {
            m_LutSettings.outputTransform = TransferFromIndex(outputTransformIndex);
            runtimeChanged = true;
        }

        ImGui::EndTable();
    }

    if (runtimeChanged) {
        ApplyRuntimeMetadataToGeneratedPayload();
    }
    EndPanel();
}

void ToolsModule::RenderLutCreatorSolveControls() {
    BeginPanel("LutCreatorSolvePanel");
    ImGuiExtras::RichSectionLabel("SOLVE CONTROLS", 6.0f);
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("Balance LUT density, sample count, and smoothing here. Changes invalidate the current solve so the next generate run reflects the new setup.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    bool solveChanged = false;
    int lutSizeIndex = 1;
    if (m_LutSettings.lutSize <= 17) {
        lutSizeIndex = 0;
    } else if (m_LutSettings.lutSize >= 65) {
        lutSizeIndex = 2;
    }
    const char* lutSizeLabels[] = { "17", "33", "65" };
    ImGui::TextDisabled("LUT Edge");
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("##LutCreatorEdge", &lutSizeIndex, lutSizeLabels, IM_ARRAYSIZE(lutSizeLabels))) {
        m_LutSettings.lutSize = lutSizeIndex == 0 ? 17 : (lutSizeIndex == 2 ? 65 : 33);
        solveChanged = true;
    }

    solveChanged |= ImGui::SliderInt("Max Samples", &m_LutSettings.maxSamples, 4000, 1000000, "%d");
    solveChanged |= ImGui::SliderInt("Manual Stride", &m_LutSettings.manualStride, 0, 24, "Auto = %d");
    solveChanged |= ImGui::SliderInt("Smooth Passes", &m_LutSettings.smoothPasses, 0, 8);
    solveChanged |= ImGui::SliderFloat("Smooth Strength", &m_LutSettings.smoothStrength, 0.0f, 1.0f, "%.2f");
    solveChanged |= ImGui::SliderFloat("Identity Bias", &m_LutSettings.identityBias, 0.0f, 1.0f, "%.2f");
    solveChanged |= ImGui::SliderFloat("Observation Threshold", &m_LutSettings.observationThreshold, 0.25f, 4.0f, "%.2f");

    if (solveChanged) {
        InvalidateGeneratedLut();
    }
    EndPanel();
}

void ToolsModule::RenderLutCreatorActionPanel(const std::function<void(ColorLut::LutPayload)>& onOpenGeneratedLut) {
    BeginPanel("LutCreatorActionPanel");
    ImGuiExtras::RichSectionLabel("ACTION CENTER", 6.0f);
    ImGui::Text("Pair Status: %s", BuildPairStatusLabel().c_str());
    ImGui::Text("Output: %d^3 .cube", m_LutSettings.lutSize);
    ImGui::Text("Sidecar: .stacklut.json");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    const float controlWidth = ImGui::GetContentRegionAvail().x;
    ImGui::BeginDisabled(!IsReadyToGenerate());
    if (ImGuiExtras::RichFullWidthButton("Generate LUT", controlWidth, 34.0f)) {
        GenerateCurrentLut();
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!m_HasGeneratedPayload);
    if (ImGuiExtras::RichFullWidthButton("Save .cube + Sidecar", controlWidth, 30.0f)) {
        SaveCurrentLut(false, onOpenGeneratedLut);
    }
    if (ImGuiExtras::RichFullWidthButton("Save + Open In Editor", controlWidth, 30.0f)) {
        SaveCurrentLut(true, onOpenGeneratedLut);
    }
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::SeparatorText("What This Saves");
    ImGui::BulletText("Portable .cube LUT for reuse outside Stack");
    ImGui::BulletText("Stack sidecar with label, title, and runtime transform metadata");
    ImGui::BulletText("Optional handoff into the editor as a ready-made LUT node");

    if (!m_LastSavedCubePath.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::SeparatorText("Last Saved");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(m_LastSavedCubePath.c_str());
        ImGui::PopTextWrapPos();
    }
    EndPanel();
}

void ToolsModule::RenderLutCreatorReadoutPanel() {
    BeginPanel("LutCreatorReadoutPanel");
    ImGuiExtras::RichSectionLabel("READOUT", 6.0f);

    auto renderMetricCard = [](const char* id, const char* label, const std::string& value) {
        BeginPanel(id, ImVec2(0.0f, 68.0f));
        ImGui::TextUnformatted(value.c_str());
        ImGui::TextDisabled("%s", label);
        EndPanel();
    };

    if (ImGui::BeginTable("LutCreatorReadoutCards", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        renderMetricCard("ReadoutPairStatus", "Pair", BuildPairStatusLabel());
        ImGui::TableNextColumn();
        renderMetricCard("ReadoutEdge", "LUT Edge", FormatInteger(m_LutSettings.lutSize));

        if (m_HasGeneratedPayload) {
            ImGui::TableNextColumn();
            renderMetricCard("ReadoutCoverage", "Coverage", FormatPercent(m_LastStats.voxelCoverage));
            ImGui::TableNextColumn();
            renderMetricCard("ReadoutMae", "Mean Abs Error", FormatPercent(m_LastStats.meanAbsoluteError));

            ImGui::TableNextColumn();
            renderMetricCard("ReadoutRmse", "RMSE", FormatPercent(m_LastStats.rootMeanSquareError));
            ImGui::TableNextColumn();
            renderMetricCard("ReadoutMax", "Max Error", FormatPercent(m_LastStats.maxAbsoluteError));

            ImGui::TableNextColumn();
            renderMetricCard("ReadoutSamples", "Sampled Pixels", FormatInteger(m_LastStats.sampledPixelCount));
            ImGui::TableNextColumn();
            renderMetricCard("ReadoutStride", "Effective Stride", FormatInteger(m_LastStats.effectiveStride));
        }
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::SeparatorText("Notes");
    if (!m_HasGeneratedPayload) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("No solve has been run yet. Once you generate a LUT, this panel becomes the quick QA surface for coverage, fit quality, and save readiness.");
        ImGui::PopTextWrapPos();
    } else if (m_LastStats.meanAbsoluteError > 0.035f) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(
            ImVec4(0.95f, 0.72f, 0.42f, 1.0f),
            "This pair is only partly LUT-friendly. Repeated source colors that need different target colors will be averaged into one compromise.");
        ImGui::PopTextWrapPos();
    } else {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("This solve is reading as reasonably consistent for a LUT-style transform. Save it when the numbers and previews look right.");
        ImGui::PopTextWrapPos();
    }
    EndPanel();
}

void ToolsModule::RenderImageCard(
    const char* label,
    PreviewImageState& state,
    const char* dialogTitle,
    const char* loadButtonLabel) {
    BeginPanel((std::string("ImageCard_") + label).c_str(), ImVec2(0.0f, 338.0f));
    ImGui::TextUnformatted(label);
    const std::string fileName = FileName(state.image.sourcePath);
    if (!fileName.empty()) {
        ImGui::TextDisabled("%s", fileName.c_str());
    } else {
        ImGui::TextDisabled("No image selected");
    }
    ImGui::TextDisabled("%s", FormatImageDimensions(state).c_str());
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float maxPreviewHeight = 184.0f;
    if (state.texture != 0 && state.image.width > 0 && state.image.height > 0) {
        float previewWidth = availableWidth;
        float previewHeight = previewWidth * (static_cast<float>(state.image.height) / static_cast<float>(state.image.width));
        if (previewHeight > maxPreviewHeight) {
            previewHeight = maxPreviewHeight;
            previewWidth = previewHeight * (static_cast<float>(state.image.width) / static_cast<float>(state.image.height));
        }
        const float xOffset = std::max(0.0f, (availableWidth - previewWidth) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset);
        ImGui::Image((ImTextureID)(intptr_t)state.texture, ImVec2(previewWidth, previewHeight));
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::BeginChild((std::string("EmptyPreview_") + label).c_str(), ImVec2(availableWidth, maxPreviewHeight), true);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 60.0f);
        ImGui::TextDisabled("Preview will appear here");
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    if (ImGui::Button(loadButtonLabel, ImVec2(availableWidth - 90.0f, 30.0f))) {
        const std::string path = FileDialogs::OpenRasterImageFileDialog(dialogTitle);
        if (!path.empty()) {
            LoadPreviewImage(path, state, label);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.texture == 0);
    if (ImGui::Button("Clear", ImVec2(80.0f, 30.0f))) {
        ReleasePreviewImage(state);
        InvalidateGeneratedLut();
        SetStatus(std::string("Cleared ") + label + ".", false);
    }
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextDisabled("Path");
    ImGui::PushTextWrapPos();
    if (!state.image.sourcePath.empty()) {
        ImGui::TextUnformatted(state.image.sourcePath.c_str());
    } else {
        ImGui::TextDisabled("Not loaded");
    }
    ImGui::PopTextWrapPos();
    EndPanel();
}

std::string ToolsModule::BuildCurrentGeneratedTitle() const {
    if (m_TitleBuffer[0] != '\0') {
        return std::string(m_TitleBuffer);
    }

    const std::string sourceStem = FileStem(m_SourceImage.image.sourcePath);
    const std::string targetStem = FileStem(m_TargetImage.image.sourcePath);
    if (!sourceStem.empty() && !targetStem.empty()) {
        return sourceStem + " to " + targetStem;
    }
    if (!targetStem.empty()) {
        return targetStem;
    }
    if (!sourceStem.empty()) {
        return sourceStem + " LUT";
    }
    return "Generated LUT";
}

std::string ToolsModule::BuildDefaultCubeFileName() const {
    std::string stem = BuildCurrentGeneratedTitle();
    std::replace(stem.begin(), stem.end(), ' ', '_');
    if (stem.empty()) {
        stem = "generated_lut";
    }
    return stem + ".cube";
}

std::string ToolsModule::BuildPairStatusLabel() const {
    const bool hasSource = m_SourceImage.image.width > 0 && m_SourceImage.image.height > 0;
    const bool hasTarget = m_TargetImage.image.width > 0 && m_TargetImage.image.height > 0;
    if (!hasSource && !hasTarget) {
        return "Waiting for source and target";
    }
    if (!hasSource) {
        return "Waiting for source image";
    }
    if (!hasTarget) {
        return "Waiting for target image";
    }
    if (m_SourceImage.image.width != m_TargetImage.image.width ||
        m_SourceImage.image.height != m_TargetImage.image.height) {
        return "Dimension mismatch";
    }
    return "Ready to generate";
}

bool ToolsModule::IsReadyToGenerate() const {
    const bool hasSource = m_SourceImage.image.width > 0 && m_SourceImage.image.height > 0;
    const bool hasTarget = m_TargetImage.image.width > 0 && m_TargetImage.image.height > 0;
    return hasSource &&
        hasTarget &&
        m_SourceImage.image.width == m_TargetImage.image.width &&
        m_SourceImage.image.height == m_TargetImage.image.height;
}

void ToolsModule::SetStatus(std::string message, bool isError) {
    m_StatusMessage = std::move(message);
    m_StatusIsError = isError;
}
