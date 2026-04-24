#include "EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Utils/ImGuiExtras.h"
#include "Layers/AdjustmentsLayer.h"
#include "Layers/AiryBloomLayer.h"
#include "Layers/AlphaHandlingLayer.h"
#include "Layers/AnalogVideoLayer.h"
#include "Layers/BackgroundPatcherLayer.h"
#include "Layers/BilateralFilterLayer.h"
#include "Layers/BlurLayer.h"
#include "Layers/CellShadingLayer.h"
#include "Layers/ChromaticAberrationLayer.h"
#include "Layers/ColorGradeLayer.h"
#include "Layers/CompressionLayer.h"
#include "Layers/CorruptionLayer.h"
#include "Layers/CropTransformLayer.h"
#include "Layers/DenoisingLayer.h"
#include "Layers/DitherLayer.h"
#include "Layers/EdgeEffectsLayer.h"
#include "Layers/ExpanderLayer.h"
#include "Layers/GlareRaysLayer.h"
#include "Layers/HalftoningLayer.h"
#include "Layers/HankelBlurLayer.h"
#include "Layers/HDRLayer.h"
#include "Layers/HeatwaveLayer.h"
#include "Layers/ImageBreaksLayer.h"
#include "Layers/LensDistortionLayer.h"
#include "Layers/NoiseLayer.h"
#include "Layers/PaletteReconstructorLayer.h"
#include "Layers/TextOverlayLayer.h"
#include "Layers/TiltShiftBlurLayer.h"
#include "Layers/VignetteLayer.h"
#include "Library/LibraryManager.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>

namespace {

struct DecodedImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
};

bool DecodeImageFromFile(const std::string& path, DecodedImageData& outImage) {
    outImage = {};

    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.channels = 4;
    outImage.pixels.assign(pixels, pixels + (width * height * 4));
    stbi_image_free(pixels);
    return true;
}

} // namespace

EditorModule::EditorModule() {}

EditorModule::~EditorModule() {}

void EditorModule::Initialize() {
    m_Pipeline.Initialize();
    m_Sidebar.Initialize();
    m_Viewport.Initialize();
    m_Scopes.Initialize();

    m_Layers.clear();
    m_SelectedLayerIndex = -1;
}

void EditorModule::AddLayer(LayerType type) {
    std::shared_ptr<LayerBase> newLayer = nullptr;

    switch (type) {
        case LayerType::Adjustments: newLayer = std::make_shared<AdjustmentsLayer>(); break;
        case LayerType::ColorGrade: newLayer = std::make_shared<ColorGradeLayer>(); break;
        case LayerType::HDR: newLayer = std::make_shared<HDRLayer>(); break;
        case LayerType::CropTransform: newLayer = std::make_shared<CropTransformLayer>(); break;
        case LayerType::Blur: newLayer = std::make_shared<BlurLayer>(); break;
        case LayerType::Noise: newLayer = std::make_shared<NoiseLayer>(); break;
        case LayerType::Vignette: newLayer = std::make_shared<VignetteLayer>(); break;
        case LayerType::ChromaticAberration: newLayer = std::make_shared<ChromaticAberrationLayer>(); break;
        case LayerType::LensDistortion: newLayer = std::make_shared<LensDistortionLayer>(); break;
        case LayerType::TiltShiftBlur: newLayer = std::make_shared<TiltShiftBlurLayer>(); break;
        case LayerType::Dither: newLayer = std::make_shared<DitherLayer>(); break;
        case LayerType::Compression: newLayer = std::make_shared<CompressionLayer>(); break;
        case LayerType::CellShading: newLayer = std::make_shared<CellShadingLayer>(); break;
        case LayerType::Heatwave: newLayer = std::make_shared<HeatwaveLayer>(); break;
        case LayerType::PaletteReconstructor: newLayer = std::make_shared<PaletteReconstructorLayer>(); break;
        case LayerType::EdgeEffects: newLayer = std::make_shared<EdgeEffectsLayer>(); break;
        case LayerType::AiryBloom: newLayer = std::make_shared<AiryBloomLayer>(); break;
        case LayerType::ImageBreaks: newLayer = std::make_shared<ImageBreaksLayer>(); break;
        case LayerType::AnalogVideo: newLayer = std::make_shared<AnalogVideoLayer>(); break;
        case LayerType::BilateralFilter: newLayer = std::make_shared<BilateralFilterLayer>(); break;
        case LayerType::Denoising: newLayer = std::make_shared<DenoisingLayer>(); break;
        case LayerType::Halftoning: newLayer = std::make_shared<HalftoningLayer>(); break;
        case LayerType::HankelBlur: newLayer = std::make_shared<HankelBlurLayer>(); break;
        case LayerType::GlareRays: newLayer = std::make_shared<GlareRaysLayer>(); break;
        case LayerType::Corruption: newLayer = std::make_shared<CorruptionLayer>(); break;
        case LayerType::AlphaHandling: newLayer = std::make_shared<AlphaHandlingLayer>(); break;
        case LayerType::BackgroundPatcher: newLayer = std::make_shared<BackgroundPatcherLayer>(); break;
        case LayerType::Expander: newLayer = std::make_shared<ExpanderLayer>(); break;
        case LayerType::TextOverlay: newLayer = std::make_shared<TextOverlayLayer>(); break;
    }

    if (newLayer) {
        newLayer->InitializeGL();

        const char* defaultName = newLayer->GetDefaultName();
        int count = 0;
        for (const auto& existing : m_Layers) {
            if (strcmp(existing->GetDefaultName(), defaultName) == 0) {
                count++;
            }
        }

        if (count > 0) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix), "%s (%d)", defaultName, count + 1);
            newLayer->SetInstanceName(suffix);
        }

        m_Layers.push_back(newLayer);
        m_SelectedLayerIndex = static_cast<int>(m_Layers.size()) - 1;
    }
}

void EditorModule::RemoveLayer(int index) {
    if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
        m_Layers.erase(m_Layers.begin() + index);
        if (m_SelectedLayerIndex >= static_cast<int>(m_Layers.size())) {
            m_SelectedLayerIndex = static_cast<int>(m_Layers.size()) - 1;
        }
    }
}

void EditorModule::MoveLayer(int from, int to) {
    if (from == to) return;
    if (from < 0 || from >= static_cast<int>(m_Layers.size())) return;
    if (to < 0 || to >= static_cast<int>(m_Layers.size())) return;

    if (from < to) {
        std::rotate(m_Layers.begin() + from, m_Layers.begin() + from + 1, m_Layers.begin() + to + 1);
    } else {
        std::rotate(m_Layers.begin() + to, m_Layers.begin() + from, m_Layers.begin() + from + 1);
    }

    if (m_SelectedLayerIndex == from) {
        m_SelectedLayerIndex = to;
    } else if (from < m_SelectedLayerIndex && to >= m_SelectedLayerIndex) {
        m_SelectedLayerIndex--;
    } else if (from > m_SelectedLayerIndex && to <= m_SelectedLayerIndex) {
        m_SelectedLayerIndex++;
    }
}

void EditorModule::RenderUI() {
    if (m_Pipeline.HasSourceImage()) {
        if (m_RenderOnlyUpToActive && m_SelectedLayerIndex >= 0) {
            std::vector<std::shared_ptr<LayerBase>> slicedLayers;
            for (int i = 0; i <= m_SelectedLayerIndex && i < static_cast<int>(m_Layers.size()); ++i) {
                slicedLayers.push_back(m_Layers[i]);
            }
            m_Pipeline.Execute(slicedLayers);
        } else {
            m_Pipeline.Execute(m_Layers);
        }
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("StackEditorWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleVar();

    ImGuiID editorDockId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(editorDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    static bool first = true;
    if (first) {
        first = false;
        ImGui::DockBuilderRemoveNode(editorDockId);
        ImGui::DockBuilderAddNode(editorDockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(editorDockId, ImGui::GetWindowSize());

        ImGuiID left = 0;
        ImGuiID bottom = 0;
        ImGuiID main = editorDockId;
        left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.25f, nullptr, &main);
        bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.35f, nullptr, &main);

        ImGui::DockBuilderDockWindow("Inspector Panel Sidebar", left);
        ImGui::DockBuilderDockWindow("Canvas Viewport", main);
        ImGui::DockBuilderDockWindow("Scopes Panel", bottom);
        ImGui::DockBuilderFinish(editorDockId);
    }

    m_Sidebar.Render(this);
    m_Viewport.Render(this);
    m_Scopes.Render(this);

    if (IsSourceLoadBusy()) {
        ImGuiExtras::RenderBusyOverlay("Loading source image...");
    } else if (IsExportBusy()) {
        ImGuiExtras::RenderBusyOverlay("Exporting image...");
    } else if (Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
    }

    ImGui::EndChild();
}

nlohmann::json EditorModule::SerializePipeline() {
    json serialized = json::array();
    for (auto& layer : m_Layers) {
        serialized.push_back(layer->Serialize());
    }
    return serialized;
}

void EditorModule::DeserializePipeline(const nlohmann::json& serialized) {
    m_Layers.clear();
    if (!serialized.is_array()) return;

    for (const auto& layerData : serialized) {
        std::string type = layerData.value("type", "");
        std::shared_ptr<LayerBase> newLayer = nullptr;

        if (type == "Adjustments") newLayer = std::make_shared<AdjustmentsLayer>();
        else if (type == "ColorGrade") newLayer = std::make_shared<ColorGradeLayer>();
        else if (type == "HDR") newLayer = std::make_shared<HDRLayer>();
        else if (type == "Blur") newLayer = std::make_shared<BlurLayer>();
        else if (type == "Noise") newLayer = std::make_shared<NoiseLayer>();
        else if (type == "Vignette") newLayer = std::make_shared<VignetteLayer>();
        else if (type == "CropTransform") newLayer = std::make_shared<CropTransformLayer>();
        else if (type == "ChromaticAberration") newLayer = std::make_shared<ChromaticAberrationLayer>();
        else if (type == "LensDistortion") newLayer = std::make_shared<LensDistortionLayer>();
        else if (type == "TiltShiftBlur") newLayer = std::make_shared<TiltShiftBlurLayer>();
        else if (type == "Dither" || type == "Dithering") newLayer = std::make_shared<DitherLayer>();
        else if (type == "Compression") newLayer = std::make_shared<CompressionLayer>();
        else if (type == "CellShading" || type == "Cell") newLayer = std::make_shared<CellShadingLayer>();
        else if (type == "Heatwave") newLayer = std::make_shared<HeatwaveLayer>();
        else if (type == "Palette" || type == "PaletteReconstructor") newLayer = std::make_shared<PaletteReconstructorLayer>();
        else if (type == "Edge" || type == "EdgeEffects") newLayer = std::make_shared<EdgeEffectsLayer>();
        else if (type == "AiryBloom" || type == "AiryDiskBloom") newLayer = std::make_shared<AiryBloomLayer>();
        else if (type == "ImageBreaks") newLayer = std::make_shared<ImageBreaksLayer>();
        else if (type == "AnalogVideo") newLayer = std::make_shared<AnalogVideoLayer>();
        else if (type == "BilateralFilter") newLayer = std::make_shared<BilateralFilterLayer>();
        else if (type == "Denoising") newLayer = std::make_shared<DenoisingLayer>();
        else if (type == "Halftoning") newLayer = std::make_shared<HalftoningLayer>();
        else if (type == "HankelBlur") newLayer = std::make_shared<HankelBlurLayer>();
        else if (type == "GlareRays") newLayer = std::make_shared<GlareRaysLayer>();
        else if (type == "Corruption") newLayer = std::make_shared<CorruptionLayer>();
        else if (type == "AlphaHandling") newLayer = std::make_shared<AlphaHandlingLayer>();
        else if (type == "BackgroundPatcher") newLayer = std::make_shared<BackgroundPatcherLayer>();
        else if (type == "Expander") newLayer = std::make_shared<ExpanderLayer>();
        else if (type == "TextOverlay") newLayer = std::make_shared<TextOverlayLayer>();

        if (newLayer) {
            newLayer->InitializeGL();
            newLayer->Deserialize(layerData);
            m_Layers.push_back(newLayer);
        }
    }

    m_SelectedLayerIndex = m_Layers.empty() ? -1 : 0;
}

void EditorModule::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    m_Pipeline.LoadSourceFromPixels(data, w, h, ch);
}

bool EditorModule::ApplyLoadedProject(const LoadedProjectData& projectData) {
    if (projectData.sourcePixels.empty() || projectData.width <= 0 || projectData.height <= 0) {
        return false;
    }

    LoadSourceFromPixels(projectData.sourcePixels.data(), projectData.width, projectData.height, projectData.channels);
    DeserializePipeline(projectData.pipelineData.is_array() ? projectData.pipelineData : nlohmann::json::array());
    SetCurrentProjectName(projectData.projectName);
    SetCurrentProjectFileName(projectData.projectFileName);
    return true;
}

void EditorModule::RequestLoadSourceImage(const std::string& path) {
    if (path.empty()) {
        return;
    }

    ++m_SourceLoadGeneration;
    const std::uint64_t generation = m_SourceLoadGeneration;
    m_SourceLoadTaskState = Async::TaskState::Queued;
    m_SourceLoadStatusText = "Loading source image in the background...";

    Async::TaskSystem::Get().Submit([this, generation, path]() {
        DecodedImageData decoded;
        const bool success = DecodeImageFromFile(path, decoded);

        Async::TaskSystem::Get().PostToMain([this, generation, path, decoded = std::move(decoded), success]() mutable {
            if (generation != m_SourceLoadGeneration) {
                return;
            }

            if (!success || decoded.pixels.empty()) {
                m_SourceLoadTaskState = Async::TaskState::Failed;
                m_SourceLoadStatusText = "Failed to load the selected source image.";
                return;
            }

            m_SourceLoadTaskState = Async::TaskState::Applying;
            m_SourceLoadStatusText = "Applying source image to the editor...";

            LoadSourceFromPixels(decoded.pixels.data(), decoded.width, decoded.height, decoded.channels);
            SetCurrentProjectName("");
            SetCurrentProjectFileName("");

            m_SourceLoadTaskState = Async::TaskState::Idle;
            m_SourceLoadStatusText = "Source image loaded.";
        });
    });
}

bool EditorModule::ExportImage(const std::string& path) {
    return RequestExportImage(path);
}

bool EditorModule::RequestExportImage(const std::string& path) {
    if (path.empty() || !m_Pipeline.HasSourceImage()) {
        return false;
    }

    if (Async::IsBusy(m_ExportTaskState)) {
        return false;
    }

    m_ExportTaskState = Async::TaskState::Applying;
    m_ExportStatusText = "Capturing the rendered image...";

    m_Pipeline.Execute(m_Layers);

    int width = 0;
    int height = 0;
    auto pixels = m_Pipeline.GetOutputPixels(width, height);
    if (pixels.empty()) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to capture the rendered image.";
        return false;
    }

    if (!m_CurrentProjectName.empty()) {
        LibraryManager::Get().RequestSaveProject(m_CurrentProjectName, this, m_CurrentProjectFileName);
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Writing PNG export in the background...";

    Async::TaskSystem::Get().Submit([this, generation, path, width, height, pixels = std::move(pixels)]() mutable {
        bool success = false;

        try {
            const std::filesystem::path destination(path);
            if (destination.has_parent_path()) {
                std::filesystem::create_directories(destination.parent_path());
            }

            success = stbi_write_png(
                destination.string().c_str(),
                width,
                height,
                4,
                pixels.data(),
                width * 4) != 0;
        } catch (...) {
            success = false;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, success]() {
            if (generation != m_ExportGeneration) {
                return;
            }

            if (success) {
                m_ExportTaskState = Async::TaskState::Idle;
                m_ExportStatusText = "Rendered image exported.";
            } else {
                m_ExportTaskState = Async::TaskState::Failed;
                m_ExportStatusText = "Failed to write the exported PNG.";
            }
        });
    });

    return true;
}
