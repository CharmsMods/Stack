#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Raw/LibRawRuntime.h"
#include "Raw/RawLoader.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/FileDialogs.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> pngBytes;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return pngBytes;
    }

    const int safeChannels = std::max(1, channels);
    stbi_write_png_to_func(PngWriteCallback, &pngBytes, width, height, safeChannels, pixels.data(), width * safeChannels);
    return pngBytes;
}

std::string FileNameFromPath(const std::string& path) {
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path.empty() ? std::string("Image") : path;
    }
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

int NormalizeQuarterTurnsClockwise(int quarterTurnsClockwise) {
    int normalized = quarterTurnsClockwise % 4;
    if (normalized < 0) {
        normalized += 4;
    }
    return normalized;
}

std::vector<unsigned char> RotateBottomLeftImagePixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels,
    int quarterTurnsClockwise,
    int& outWidth,
    int& outHeight) {
    outWidth = width;
    outHeight = height;
    const int safeChannels = std::max(1, channels);
    const int normalizedTurns = NormalizeQuarterTurnsClockwise(quarterTurnsClockwise);
    if (pixels.empty() || width <= 0 || height <= 0 || normalizedTurns == 0) {
        return pixels;
    }

    if (normalizedTurns == 2) {
        std::vector<unsigned char> rotated(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(safeChannels));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int srcX = width - 1 - x;
                const int srcY = height - 1 - y;
                const std::size_t dstIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * static_cast<std::size_t>(safeChannels);
                const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(srcX)) * static_cast<std::size_t>(safeChannels);
                std::copy_n(pixels.data() + srcIndex, safeChannels, rotated.data() + dstIndex);
            }
        }
        return rotated;
    }

    outWidth = height;
    outHeight = width;
    std::vector<unsigned char> rotated(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * static_cast<std::size_t>(safeChannels));
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            int srcX = 0;
            int srcY = 0;
            if (normalizedTurns == 1) {
                srcX = width - 1 - y;
                srcY = x;
            } else {
                srcX = y;
                srcY = height - 1 - x;
            }
            const std::size_t dstIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth) + static_cast<std::size_t>(x)) * static_cast<std::size_t>(safeChannels);
            const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(srcX)) * static_cast<std::size_t>(safeChannels);
            std::copy_n(pixels.data() + srcIndex, safeChannels, rotated.data() + dstIndex);
        }
    }

    return rotated;
}

EditorNodeGraph::ImagePayload BuildImagePayloadFromDecoded(
    const std::string& path,
    DecodedImageData decoded) {
    EditorNodeGraph::ImagePayload payload;
    payload.label = FileNameFromPath(path);
    payload.sourcePath = path;
    payload.width = decoded.width;
    payload.height = decoded.height;
    payload.channels = decoded.channels;
    payload.originalChannels = decoded.originalChannels;
    payload.pngBytes = EncodePngBytesForImageStorage(decoded.pixels, decoded.width, decoded.height, decoded.channels);
    payload.pixels = std::move(decoded.pixels);
    return payload;
}

EditorNodeGraph::RawSourcePayload BuildRawPayloadFromMetadata(
    const std::string& path,
    Raw::RawMetadata metadata) {
    EditorNodeGraph::RawSourcePayload payload;
    payload.label = FileNameFromPath(path).empty() ? "RAW" : FileNameFromPath(path);
    payload.sourcePath = path;
    payload.metadata = std::move(metadata);
    payload.metadata.sourcePath = path;
    return payload;
}

} // namespace

void EditorModule::PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const std::string path = FileDialogs::OpenImageFileDialog("Add Image Node");
    if (!path.empty()) {
        AddImageNodeFromFile(path, graphPosition);
    }
}

void EditorModule::RequestPromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    m_PendingAddImageNodePrompt = true;
    m_PendingAddImageNodeGraphPosition = graphPosition;
}

bool EditorModule::AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
    if (Raw::RawLoader::IsRawPath(path)) {
        const Raw::LibRawRuntimeStatus& runtimeStatus = Raw::GetLibRawRuntimeStatus();
        if (!runtimeStatus.runtimeAvailable) {
            QueueUiNotification(UiNotificationSeverity::Error, runtimeStatus.message, "editor-raw-runtime");
            return false;
        }
        return AddGraphRawChainFromFile(path, graphPosition);
    }
    DecodedImageData decoded;
    if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
        return false;
    }

    return AddImageNodeFromPayload(BuildImagePayloadFromDecoded(path, std::move(decoded)), graphPosition);
}

bool EditorModule::AddRawSourceNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
    if (!Raw::IsLibRawRuntimeAvailable()) {
        return false;
    }

    Raw::RawImageData rawData;
    const bool loaded = Raw::RawLoader::LoadFile(path, rawData);
    if (!loaded && rawData.metadata.sourcePath.empty()) {
        rawData.metadata.sourcePath = path;
    }
    if (!loaded && rawData.metadata.error.empty()) {
        rawData.metadata.error = "Failed to load RAW file.";
    }

    EditorNodeGraph::RawSourcePayload payload = BuildRawPayloadFromMetadata(path, std::move(rawData.metadata));
    return AddRawSourceNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddImageNodeFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddImageNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
    }
    return node != nullptr;
}

bool EditorModule::AddRawSourceNodeFromPayload(EditorNodeGraph::RawSourcePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    if (payload.metadata.sourcePath.empty()) {
        payload.metadata.sourcePath = payload.sourcePath;
    }
    if (!Raw::IsLibRawRuntimeAvailable() &&
        !payload.metadata.sourcePath.empty() &&
        payload.metadata.error.empty()) {
        payload.metadata.error = Raw::GetLibRawRuntimeStatus().message;
    }

    EditorNodeGraph::Node* node = m_NodeGraph.AddRawSourceNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::RequestGraphImageChainImports(
    const std::vector<std::string>& paths,
    EditorNodeGraph::Vec2 sourcePosition) {
    std::vector<std::string> validPaths;
    validPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        if (!path.empty()) {
            validPaths.push_back(path);
        }
    }
    if (validPaths.empty()) {
        return false;
    }
    if (Async::IsBusy(m_GraphDropImportTaskState)) {
        m_PendingGraphDropImports.push_back(PendingGraphDropImportRequest{ std::move(validPaths), sourcePosition });
        return true;
    }

    return StartGraphImageChainImport(std::move(validPaths), sourcePosition);
}

bool EditorModule::StartGraphImageChainImport(
    std::vector<std::string> validPaths,
    EditorNodeGraph::Vec2 sourcePosition) {
    if (validPaths.empty()) {
        return false;
    }

    ++m_GraphDropImportGeneration;
    const std::uint64_t generation = m_GraphDropImportGeneration;
    m_GraphDropImportTaskState = Async::TaskState::Queued;
    m_GraphDropImportStatusText = validPaths.size() > 1
        ? "Loading dropped images into the graph..."
        : "Loading dropped image into the graph...";

    Async::TaskSystem::Get().Submit([this, generation, validPaths = std::move(validPaths), sourcePosition]() mutable {
        struct DecodedDropImage {
            std::string path;
            DecodedImageData decoded;
        };

        std::vector<DecodedDropImage> decodedImages;
        decodedImages.reserve(validPaths.size());
        std::vector<std::string> rawPaths;
        for (const std::string& path : validPaths) {
            if (Raw::RawLoader::IsRawPath(path)) {
                rawPaths.push_back(path);
                continue;
            }
            DecodedImageData decoded;
            if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
                continue;
            }
            decodedImages.push_back(DecodedDropImage{ path, std::move(decoded) });
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            sourcePosition,
            requestedCount = validPaths.size(),
            decodedImages = std::move(decodedImages),
            rawPaths = std::move(rawPaths)
        ]() mutable {
            if (generation != m_GraphDropImportGeneration) {
                return;
            }

            const Raw::LibRawRuntimeStatus& rawRuntimeStatus = Raw::GetLibRawRuntimeStatus();
            const bool rawRuntimeUnavailable = !rawPaths.empty() && !rawRuntimeStatus.runtimeAvailable;

            if (decodedImages.empty() && rawPaths.empty()) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                m_GraphDropImportStatusText = "Failed to import the dropped images.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to import the dropped images.", "editor-graph-drop-import");
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
                }
                return;
            }

            m_GraphDropImportTaskState = Async::TaskState::Applying;
            m_GraphDropImportStatusText = "Creating image nodes...";

            constexpr float kGraphDropRowSpacing = 190.0f;
            const std::size_t totalNodes = decodedImages.size() + rawPaths.size();
            const float startY = sourcePosition.y - (static_cast<float>(totalNodes - 1) * kGraphDropRowSpacing * 0.5f);
            int importedCount = 0;
            size_t outputIndex = 0;
            for (size_t index = 0; index < decodedImages.size(); ++index, ++outputIndex) {
                EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                nodePosition.y = startY + static_cast<float>(outputIndex) * kGraphDropRowSpacing;
                if (AddGraphImageChainFromPayload(
                    BuildImagePayloadFromDecoded(decodedImages[index].path, std::move(decodedImages[index].decoded)),
                    nodePosition)) {
                    ++importedCount;
                }
            }
            if (rawRuntimeUnavailable) {
                QueueUiNotification(UiNotificationSeverity::Error, rawRuntimeStatus.message, "editor-raw-runtime");
                outputIndex += rawPaths.size();
            } else {
                for (const std::string& path : rawPaths) {
                    EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                    nodePosition.y = startY + static_cast<float>(outputIndex++) * kGraphDropRowSpacing;
                    if (AddGraphRawChainFromFile(path, nodePosition)) {
                        ++importedCount;
                    }
                }
            }

            if (importedCount <= 0) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                if (rawRuntimeUnavailable) {
                    m_GraphDropImportStatusText = rawRuntimeStatus.message;
                } else {
                    m_GraphDropImportStatusText = "Failed to create graph nodes for the dropped images.";
                    QueueUiNotification(UiNotificationSeverity::Error, "Failed to create graph nodes for the dropped images.", "editor-graph-drop-import");
                }
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
                }
                return;
            }

            m_GraphDropImportTaskState = Async::TaskState::Idle;
            if (importedCount == static_cast<int>(requestedCount)) {
                m_GraphDropImportStatusText = importedCount == 1
                    ? "Imported 1 image into the graph."
                    : "Imported " + std::to_string(importedCount) + " images into the graph.";
            } else {
                m_GraphDropImportStatusText =
                    "Imported " + std::to_string(importedCount) + " of " + std::to_string(requestedCount) + " dropped images.";
            }
            QueueUiNotification(UiNotificationSeverity::Success, m_GraphDropImportStatusText, "editor-graph-drop-import");

            if (!m_PendingGraphDropImports.empty()) {
                PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
            }
        });
    });

    return true;
}

bool EditorModule::ConnectGraphImageNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return false;
    }
    bool sourceChanged = false;
    if (node->kind == EditorNodeGraph::NodeKind::Image) {
        if (node->image.pixels.empty()) {
            return false;
        }
        LoadSourceFromPixels(node->image.pixels.data(), node->image.width, node->image.height, node->image.channels);
        m_NodeGraph.SetActiveImageNodeId(nodeId);
        sourceChanged = true;
    } else if (node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    m_NodeGraph.ConnectImageToOutput(nodeId);
    if (sourceChanged) {
        MarkNodeBrowserThumbnailSourceChanged();
    }
    SelectGraphNode(nodeId);
    MarkRenderDirty();
    return true;
}

bool EditorModule::RotateImageNode(int nodeId, int quarterTurnsClockwise) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Image) {
        return false;
    }

    const int normalizedTurns = NormalizeQuarterTurnsClockwise(quarterTurnsClockwise);
    if (normalizedTurns == 0 || node->image.pixels.empty() || node->image.width <= 0 || node->image.height <= 0) {
        return normalizedTurns == 0;
    }

    int rotatedWidth = node->image.width;
    int rotatedHeight = node->image.height;
    std::vector<unsigned char> rotatedPixels = RotateBottomLeftImagePixels(
        node->image.pixels,
        node->image.width,
        node->image.height,
        node->image.channels,
        normalizedTurns,
        rotatedWidth,
        rotatedHeight);
    if (rotatedPixels.empty()) {
        return false;
    }

    node->image.width = rotatedWidth;
    node->image.height = rotatedHeight;
    node->image.pixels = std::move(rotatedPixels);
    node->image.pngBytes = EncodePngBytesForImageStorage(
        node->image.pixels,
        node->image.width,
        node->image.height,
        node->image.channels);
    EditorNodeGraph::InvalidateImagePayloadRuntime(node->image);

    if (m_NodeGraph.GetActiveImageNodeId() == nodeId) {
        LoadSourceFromPixels(
            node->image.pixels.data(),
            node->image.width,
            node->image.height,
            node->image.channels);
        MarkNodeBrowserThumbnailSourceChanged();
    }

    MarkRenderDirty(nodeId);
    return true;
}
