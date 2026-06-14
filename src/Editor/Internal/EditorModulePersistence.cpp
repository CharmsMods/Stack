#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Raw/RawLoader.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace {

namespace StackFormat = StackBinaryFormat;

struct DecodedImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    int originalChannels = 4;
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
    outImage.originalChannels = channels;
    outImage.pixels.assign(pixels, pixels + (width * height * 4));
    stbi_image_free(pixels);
    return true;
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> encoded;
    if (pixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return encoded;
    }

    auto writeCallback = [](void* context, void* data, int size) {
        auto* bytes = static_cast<std::vector<unsigned char>*>(context);
        const auto* src = static_cast<unsigned char*>(data);
        bytes->insert(bytes->end(), src, src + size);
    };

    stbi_write_png_to_func(writeCallback, &encoded, width, height, channels, pixels.data(), width * channels);
    return encoded;
}

std::vector<unsigned char> EncodePngBytesForImageStorage(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels) {
    if (bottomLeftPixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return {};
    }

    std::vector<unsigned char> topLeftPixels = bottomLeftPixels;
    LibraryManager::FlipImageRowsInPlace(topLeftPixels, width, height, std::max(1, channels));
    return EncodePngBytes(topLeftPixels, width, height, channels);
}

std::string FileNameFromPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path;
    }
}

std::string BuildTimestampString() {
    std::time_t now = std::time(nullptr);
    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) == 0) {
        return {};
    }
    return buffer;
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width) * static_cast<size_t>(height) * 4ull, 0u);
}

std::vector<unsigned char> BuildThumbnailBytes(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    if (pixels.empty() || width <= 0 || height <= 0) {
        return {};
    }

    constexpr int kThumbnailSize = 320;
    const float scale = std::min(
        static_cast<float>(kThumbnailSize) / static_cast<float>(std::max(1, width)),
        static_cast<float>(kThumbnailSize) / static_cast<float>(std::max(1, height)));
    const int thumbW = std::max(1, static_cast<int>(std::round(width * scale)));
    const int thumbH = std::max(1, static_cast<int>(std::round(height * scale)));
    std::vector<unsigned char> thumbPixels(static_cast<size_t>(thumbW) * static_cast<size_t>(thumbH) * 4ull, 0u);

    for (int y = 0; y < thumbH; ++y) {
        for (int x = 0; x < thumbW; ++x) {
            const int srcX = std::clamp(static_cast<int>(std::floor((static_cast<float>(x) / thumbW) * width)), 0, width - 1);
            const int srcY = std::clamp(static_cast<int>(std::floor((static_cast<float>(y) / thumbH) * height)), 0, height - 1);
            const size_t srcIndex = static_cast<size_t>((srcY * width + srcX) * 4);
            const size_t dstIndex = static_cast<size_t>((y * thumbW + x) * 4);
            std::copy_n(pixels.data() + srcIndex, 4, thumbPixels.data() + dstIndex);
        }
    }

    return EncodePngBytes(thumbPixels, thumbW, thumbH, 4);
}

} // namespace

nlohmann::json EditorModule::SerializePipeline() {
    json serialized = json::array();
    for (auto& layer : m_Layers) {
        serialized.push_back(layer->Serialize());
    }
    nlohmann::json payload = EditorNodeGraph::SerializeGraphPayload(serialized, m_NodeGraph);
    payload["editorComposite"] = SerializeCompositePersistence();
    return payload;
}

void EditorModule::DeserializePipeline(const nlohmann::json& serialized) {
    ResetRenderSubmissionState();
    ClearAutoGainMaskPreview();
    m_CompositeSelectedOutputNodeId = -1;
    m_Pipeline.Clear();
    m_Pipeline.ClearOutput();
    m_CompositePreviewPipeline.Clear();
    m_CompositePreviewPipeline.ClearOutput();
    m_Layers.clear();
    ClearCompositeRuntimeState();
    m_NodeDirtyGenerations.clear();
    m_DevelopAutoSolveTriggerHashes.clear();
    m_DevelopAutoRawSolveTriggerHashes.clear();
    m_DevelopAutoRawCalibrationHashes.clear();
    m_DevelopAutoGuidanceDrafts.clear();
    m_RawDevelopExposureDrafts.clear();
    m_LastRawDevelopInteractionTime = -1.0;
    m_RawDevelopInteractionSerialCounter = 1;
    m_RawDevelopInteractionTimes.clear();
    m_RawDevelopInteractionSerials.clear();
    m_DeferredDevelopCandidateFeedbackTimes.clear();
    m_PreviewDisplayedRevisions.clear();
    m_PreviewPixelCache.clear();
    m_PreviewRequestedGenerations.clear();
    m_PreviewCompletedGenerations.clear();
    m_ScopeDisplayedRevisions.clear();
    const nlohmann::json layers = EditorNodeGraph::ExtractLayerArray(serialized);
    if (!layers.is_array()) return;

    for (const auto& layerData : layers) {
        std::string type = layerData.value("type", "");
        std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayerFromTypeId(type);

        if (newLayer) {
            newLayer->InitializeGL();
            newLayer->Deserialize(layerData);
            m_Layers.push_back(newLayer);
        }
    }

    m_SelectedLayerIndex = m_Layers.empty() ? -1 : 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    const std::vector<unsigned char>& sourcePixels = m_Pipeline.GetSourcePixelsRaw();
    if (!sourcePixels.empty()) {
        sourceWidth = m_Pipeline.GetCanvasWidth();
        sourceHeight = m_Pipeline.GetCanvasHeight();
    }

    EditorNodeGraph::DeserializeGraphPayload(
        serialized,
        m_NodeGraph,
        static_cast<int>(m_Layers.size()),
        sourcePixels,
        sourceWidth,
        sourceHeight,
        m_Pipeline.GetSourceChannels());

    DeserializeCompositePersistence(serialized);
    RefreshGraphLayerMetadata();

    const int activeImageNodeId = m_NodeGraph.GetActiveImageNodeId();
    if (activeImageNodeId > 0) {
        if (EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(activeImageNodeId)) {
            if (imageNode->kind == EditorNodeGraph::NodeKind::Image && !imageNode->image.pixels.empty()) {
                LoadSourceFromPixels(imageNode->image.pixels.data(), imageNode->image.width, imageNode->image.height, imageNode->image.channels);
            } else if (imageNode->kind == EditorNodeGraph::NodeKind::RawSource) {
                const int width = Raw::DisplayWidth(imageNode->rawSource.metadata);
                const int height = Raw::DisplayHeight(imageNode->rawSource.metadata);
                std::vector<unsigned char> transparent = BuildTransparentPixels(width, height);
                LoadSourceFromPixels(transparent.data(), width, height, 4);
            }
        }
        ApplyGraphLayerOrder();
    } else {
        m_Pipeline.ClearOutput();
    }
    MarkRenderDirty();
}

void EditorModule::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    m_Pipeline.LoadSourceFromPixels(data, w, h, ch);
    m_CompositePreviewPipeline.LoadSourceFromPixels(data, w, h, ch);
    ClearCompositeSceneTextures();
    MarkRenderDirty();
}

bool EditorModule::ApplyLoadedProject(const LoadedProjectData& projectData) {
    if (projectData.sourcePixels.empty() || projectData.width <= 0 || projectData.height <= 0) {
        return false;
    }

    ResetRenderSubmissionState();
    ClearAutoGainMaskPreview();
    m_Pipeline.ClearOutput();
    m_CompositePreviewPipeline.ClearOutput();
    LoadSourceFromPixels(projectData.sourcePixels.data(), projectData.width, projectData.height, projectData.channels);
    DeserializePipeline(projectData.pipelineData);
    SetCurrentProjectName(projectData.projectName);
    SetCurrentProjectFileName(projectData.projectFileName);
    ClearDirty();
    m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_LastAutoSaveTime = -1.0;
    return true;
}

void EditorModule::RequestLoadSourceImage(const std::string& path) {
    if (path.empty()) {
        return;
    }

    if (Raw::RawLoader::IsRawPath(path)) {
        if (AddGraphRawChainFromFile(path, EditorNodeGraph::Vec2{ 20.0f, 120.0f })) {
            m_SourceLoadTaskState = Async::TaskState::Idle;
            m_SourceLoadStatusText = "RAW source node loaded.";
            QueueUiNotification(UiNotificationSeverity::Success, "RAW source node loaded.", "editor-source-load");
        } else {
            m_SourceLoadTaskState = Async::TaskState::Failed;
            m_SourceLoadStatusText = "Failed to load RAW source node.";
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to load RAW source node.", "editor-source-load");
        }
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
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected source image.", "editor-source-load");
                return;
            }

            m_SourceLoadTaskState = Async::TaskState::Applying;
            m_SourceLoadStatusText = "Applying source image to the editor...";

            LoadSourceFromPixels(decoded.pixels.data(), decoded.width, decoded.height, decoded.channels);
            EditorNodeGraph::ImagePayload payload;
            payload.label = FileNameFromPath(path);
            payload.sourcePath = path;
            payload.width = decoded.width;
            payload.height = decoded.height;
            payload.channels = decoded.channels;
            payload.pngBytes = EncodePngBytesForImageStorage(decoded.pixels, decoded.width, decoded.height, decoded.channels);
            payload.pixels = std::move(decoded.pixels);

            EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
            if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image) {
                m_NodeGraph.ResetFromLayers(static_cast<int>(m_Layers.size()), true);
                imageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
            } else if (!m_NodeGraph.IsOutputConnected()) {
                m_NodeGraph.RebuildLinks();
            }

            if (imageNode) {
                imageNode->title = payload.label.empty() ? "Image" : payload.label;
                imageNode->image = std::move(payload);
                m_NodeGraph.SetActiveImageNodeId(imageNode->id);
            }
            SetCurrentProjectName("");
            SetCurrentProjectFileName("");

            m_SourceLoadTaskState = Async::TaskState::Idle;
            m_SourceLoadStatusText = "Source image loaded.";
            QueueUiNotification(UiNotificationSeverity::Success, "Source image loaded.", "editor-source-load");
        });
    });
}

bool EditorModule::ExportImage(const std::string& path) {
    return RequestExportImage(path);
}

bool EditorModule::RequestExportImage(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    if (Async::IsBusy(m_ExportTaskState)) {
        return false;
    }

    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    m_ExportTaskState = Async::TaskState::Applying;
    m_ExportStatusText = IsCompositeViewportMode()
        ? "Capturing the composite export..."
        : "Capturing the rendered image...";

    if (IsCompositeViewportMode()) {
        if (!BuildCompositeExportRaster(pixels, width, height)) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText = "Failed to capture the composite export.";
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the composite export.", "editor-export-image");
            return false;
        }
    } else {
        if (!BuildSingleOutputExportRaster(pixels, width, height)) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText = "Failed to capture the rendered export.";
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the rendered export.", "editor-export-image");
            return false;
        }
    }

    if (pixels.empty()) {
        std::cerr << "[EditorModule] RequestExportImage: Failed to capture pixels (empty output). Width=" 
                  << width << ", Height=" << height << std::endl;
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to capture the rendered image.";
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the rendered image.", "editor-export-image");
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
        std::string errorMsg;

        try {
            const std::filesystem::path destination = std::filesystem::u8path(path);
            if (destination.has_parent_path()) {
                std::filesystem::create_directories(destination.parent_path());
            }

            int stbiResult = stbi_write_png(
                destination.string().c_str(),
                width,
                height,
                4,
                pixels.data(),
                width * 4);
                
            success = (stbiResult != 0);
            
            if (success) {
                // Verify the file was actually written
                if (!std::filesystem::exists(destination) || std::filesystem::file_size(destination) == 0) {
                    success = false;
                    errorMsg = "File does not exist or is empty after stbi_write_png returned success.";
                }
            }
            
            if (!success) {
                errorMsg = "stbi_write_png or verification failed.";
                // Try fallback path with sanitized name
                std::string fallbackPath = (destination.parent_path() / "fallback_export.png").string();
                std::cerr << "[EditorModule] stbi_write_png failed for " << path << ". Trying fallback " << fallbackPath << std::endl;
                success = stbi_write_png(
                    fallbackPath.c_str(),
                    width,
                    height,
                    4,
                    pixels.data(),
                    width * 4) != 0;
            }
        } catch (const std::exception& e) {
            success = false;
            errorMsg = std::string("Exception: ") + e.what();
        } catch (...) {
            success = false;
            errorMsg = "Unknown exception during export.";
        }

        if (!success) {
            std::cerr << "[EditorModule] Export failed for path: " << path << ". Reason: " << errorMsg << std::endl;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, success]() {
            if (generation != m_ExportGeneration) {
                return;
            }

            if (success) {
                m_ExportTaskState = Async::TaskState::Idle;
                m_ExportStatusText = "Rendered image exported.";
                QueueUiNotification(UiNotificationSeverity::Success, "Rendered image exported.", "editor-export-image");
            } else {
                m_ExportTaskState = Async::TaskState::Failed;
                m_ExportStatusText = "Failed to write the exported PNG.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to write the exported PNG.", "editor-export-image");
            }
        });
    });

    return true;
}

bool EditorModule::BuildProjectDocumentForSave(
    const std::string& displayName,
    StackFormat::ProjectDocument& outDocument) {
    const std::string trimmedName = displayName.empty() ? "Untitled Project" : displayName;
    const StackFormat::json pipeline = SerializePipeline();

    int renderedW = 0;
    int renderedH = 0;
    std::vector<unsigned char> renderedPixels;
    const bool compositeProject = GetViewportMode() == ViewportMode::CompositeCanvas;
    if (compositeProject) {
        BuildCompositeExportRaster(renderedPixels, renderedW, renderedH);
    } else {
        renderedPixels = m_Pipeline.GetOutputPixels(renderedW, renderedH);
        if ((renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) && m_NodeGraph.IsOutputConnected()) {
            BuildSingleOutputExportRaster(renderedPixels, renderedW, renderedH);
        }
    }
    if (renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) {
        return false;
    }

    int sourceW = 0;
    int sourceH = 0;
    std::vector<unsigned char> sourcePixels;
    std::vector<unsigned char> sourcePngBytes;
    if (compositeProject) {
        sourceW = renderedW;
        sourceH = renderedH;
        sourcePixels = BuildTransparentPixels(sourceW, sourceH);
    } else {
        if (const EditorNodeGraph::Node* activeImageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
            activeImageNode &&
            activeImageNode->kind == EditorNodeGraph::NodeKind::Image &&
            !activeImageNode->image.pngBytes.empty() &&
            activeImageNode->image.width > 0 &&
            activeImageNode->image.height > 0) {
            sourceW = activeImageNode->image.width;
            sourceH = activeImageNode->image.height;
            sourcePngBytes = activeImageNode->image.pngBytes;
        }
        if (sourcePngBytes.empty()) {
            sourcePixels = m_Pipeline.GetSourcePixels(sourceW, sourceH);
        }
        if (sourcePixels.empty() && sourcePngBytes.empty()) {
            sourceW = renderedW;
            sourceH = renderedH;
            sourcePixels = BuildTransparentPixels(sourceW, sourceH);
        }
    }

    outDocument = {};
    outDocument.metadata.projectKind = StackFormat::kEditorProjectKind;
    outDocument.metadata.projectName = trimmedName;
    outDocument.metadata.timestamp = BuildTimestampString();
    outDocument.metadata.sourceWidth = sourceW;
    outDocument.metadata.sourceHeight = sourceH;
    outDocument.thumbnailBytes = BuildThumbnailBytes(renderedPixels, renderedW, renderedH);
    outDocument.sourceImageBytes = !sourcePngBytes.empty()
        ? std::move(sourcePngBytes)
        : EncodePngBytes(sourcePixels, sourceW, sourceH, 4);
    outDocument.pipelineData = pipeline;
    return !outDocument.sourceImageBytes.empty();
}

bool EditorModule::RequestExportProject(const std::string& path) {
    if (path.empty() || Async::IsBusy(m_ExportTaskState)) {
        return false;
    }

    m_ExportTaskState = Async::TaskState::Applying;
    m_ExportStatusText = "Packaging project file...";

    const std::string exportName = m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName;
    StackFormat::ProjectDocument document;
    if (!BuildProjectDocumentForSave(exportName, document)) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to package the current project.";
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to package the current project.", "editor-export-project");
        return false;
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Writing project file...";

    Async::TaskSystem::Get().Submit([this, generation, path, document = std::move(document)]() mutable {
        bool success = false;
        try {
            const std::filesystem::path destination(path);
            if (destination.has_parent_path()) {
                std::filesystem::create_directories(destination.parent_path());
            }
            success = StackFormat::WriteProjectFile(destination, document);
        } catch (...) {
            success = false;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, path, success]() {
            if (generation != m_ExportGeneration) {
                return;
            }

            if (success) {
                m_ExportTaskState = Async::TaskState::Idle;
                m_ExportStatusText = "Project exported.";
                QueueUiNotification(UiNotificationSeverity::Success, "Project exported.", "editor-export-project");
                if (m_CurrentProjectName.empty()) {
                    const std::string stem = std::filesystem::path(path).stem().string();
                    m_CurrentProjectName = stem.empty() ? std::string("Untitled Project") : stem;
                }
            } else {
                m_ExportTaskState = Async::TaskState::Failed;
                m_ExportStatusText = "Failed to write the project file.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to write the project file.", "editor-export-project");
            }
        });
    });

    return true;
}
