#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include "Library/LibraryManager.h"
#include "Raw/LibRawRuntime.h"
#include "Raw/RawLoader.h"
#include "ThirdParty/stb_image.h"
#include "Utils/FileDialogs.h"
#include "Utils/PixelBufferUtils.h"
#include "Utils/PngEncodingUtils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void SetImportStatusNoThrow(
    std::string& target,
    std::string_view message) noexcept {
    try {
        target.assign(message.data(), message.size());
    } catch (...) {
        target.clear();
    }
}

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
    const bool copied = Stack::PixelBuffer::CopyInterleavedPixels(
        pixels, width, height, 4, outImage.pixels);
    stbi_image_free(pixels);
    if (!copied) {
        outImage = {};
    }
    return copied;
}

bool ReadImageInfoFromFile(const std::string& path, int& outWidth, int& outHeight, int& outChannels) {
    outWidth = 0;
    outHeight = 0;
    outChannels = 0;
    return stbi_info(path.c_str(), &outWidth, &outHeight, &outChannels) != 0 &&
        outWidth > 0 && outHeight > 0;
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    return Stack::PngEncoding::EncodeInterleaved(
        pixels, width, height, channels);
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
    return EditorNodeGraph::EncodeImagePayloadPngForStorage(
        bottomLeftPixels, width, height, channels);
}

std::vector<unsigned char> EncodePngBytesForImageStorageOwned(
    std::vector<unsigned char> bottomLeftPixels,
    int width,
    int height,
    int channels) {
    if (bottomLeftPixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return {};
    }

    // This buffer belongs only to the background embed task, so flipping it in
    // place avoids the second full-frame copy used by the non-owning helper.
    LibraryManager::FlipImageRowsInPlace(bottomLeftPixels, width, height, std::max(1, channels));
    return EncodePngBytes(bottomLeftPixels, width, height, channels);
}

double MillisecondsSince(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
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
    return Stack::PixelBuffer::RotateInterleavedQuarterTurnsClockwise(
        pixels,
        width,
        height,
        channels,
        quarterTurnsClockwise,
        outWidth,
        outHeight);
}

EditorNodeGraph::ImagePayload BuildImagePayloadFromDecoded(
    const std::string& path,
    DecodedImageData decoded,
    bool includeStoragePng = true) {
    EditorNodeGraph::ImagePayload payload;
    payload.label = FileNameFromPath(path);
    payload.sourcePath = path;
    payload.width = decoded.width;
    payload.height = decoded.height;
    payload.channels = decoded.channels;
    payload.originalChannels = decoded.originalChannels;
    payload.sourceColorMetadata = EditorNodeGraph::InspectImageFileColorMetadata(
        path, decoded.width, decoded.height, decoded.originalChannels);
    EditorNodeGraph::BuildImagePayloadPreview(
        decoded.pixels,
        decoded.width,
        decoded.height,
        decoded.channels,
        payload.previewPixels,
        payload.previewWidth,
        payload.previewHeight,
        payload.previewChannels);
    if (includeStoragePng) {
        payload.pngBytes = EncodePngBytesForImageStorage(decoded.pixels, decoded.width, decoded.height, decoded.channels);
    }
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
    const std::string path = FileDialogs::OpenImageFileDialog("Import Slice");
    if (!path.empty()) {
        StartAsyncGraphImageNodeImport(path, graphPosition);
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

bool EditorModule::StartAsyncGraphImageNodeImport(
    const std::string& path,
    EditorNodeGraph::Vec2 graphPosition) {
    if (path.empty()) {
        return false;
    }
    if (Raw::RawLoader::IsRawPath(path)) {
        return AddImageNodeFromFile(path, graphPosition);
    }

    const std::vector<int> selectionBefore =
        m_NodeGraph.GetSelectedNodeIds();
    const std::uint64_t requestId = m_NextGraphImageImportRequestId++;
    EditorNodeGraph::ImagePayload pendingPayload;
    pendingPayload.label = FileNameFromPath(path);
    pendingPayload.sourcePath = path;
    int sourceChannels = 0;
    ReadImageInfoFromFile(path, pendingPayload.width, pendingPayload.height, sourceChannels);
    pendingPayload.originalChannels = sourceChannels > 0 ? sourceChannels : pendingPayload.originalChannels;
    pendingPayload.isLoading = true;
    pendingPayload.importRequestId = requestId;

    EditorNodeGraph::Node* pendingNode = m_NodeGraph.AddImageNode(std::move(pendingPayload), graphPosition);
    if (!pendingNode) {
        return false;
    }
    const int nodeId = pendingNode->id;
    SelectGraphNode(nodeId);
    MarkDirty();

    const auto queuedAt = std::chrono::steady_clock::now();
    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().SubmitHighPriority([this, path, nodeId, requestId, queuedAt]() {
        auto postImportFailure = [this, nodeId, requestId]() {
            return Async::TaskSystem::Get().PostToMain([this, nodeId, requestId]() {
                EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
                if (node && node->kind == EditorNodeGraph::NodeKind::Image &&
                    node->image.isLoading && node->image.importRequestId == requestId) {
                    m_NodeGraph.RemoveNode(nodeId);
                    MarkDirty();
                }
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    "Failed to load the selected slice.",
                    "editor-graph-image-import");
            });
        };
        auto postEmbeddingFailure = [this, nodeId, requestId]() {
            return Async::TaskSystem::Get().PostToMain([this, nodeId, requestId]() {
                EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
                if (!node || node->kind != EditorNodeGraph::NodeKind::Image ||
                    !node->image.isEmbedding ||
                    node->image.embeddingRequestId != requestId) {
                    return;
                }
                node->image.isEmbedding = false;
                node->image.embeddingRequestId = 0;
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    "The slice loaded, but Stack could not embed it for project storage.",
                    "editor-graph-image-embed");
                MarkDirty();
            });
        };
        bool payloadQueued = false;
        try {
        const double queueMs = MillisecondsSince(queuedAt);
        const auto decodeBegin = std::chrono::steady_clock::now();
        DecodedImageData decoded;
        if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
            postImportFailure();
            return;
        }
        const double decodeMs = MillisecondsSince(decodeBegin);

        const auto previewBegin = std::chrono::steady_clock::now();
        EditorNodeGraph::ImagePayload payload =
            BuildImagePayloadFromDecoded(path, std::move(decoded), false);
        const double previewMs = MillisecondsSince(previewBegin);
        payload.isEmbedding = true;
        payload.importRequestId = requestId;
        payload.embeddingRequestId = requestId;

        // The storage encoder needs its own source buffer because renderable
        // pixels are handed to the main thread immediately. This moves the old
        // flip-and-encode work off the interaction path.
        const auto storageCopyBegin = std::chrono::steady_clock::now();
        std::vector<unsigned char> storagePixels = payload.pixels;
        const double storageCopyMs = MillisecondsSince(storageCopyBegin);
        const int width = payload.width;
        const int height = payload.height;
        const int channels = payload.channels;
        const std::size_t pixelBytes = storagePixels.size();

        payloadQueued = Async::TaskSystem::Get().PostToMain([
            this,
            nodeId,
            requestId,
            queueMs,
            decodeMs,
            previewMs,
            storageCopyMs,
            pixelBytes,
            payload = std::move(payload)
        ]() mutable {
            EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
            if (!node || node->kind != EditorNodeGraph::NodeKind::Image ||
                !node->image.isLoading || node->image.importRequestId != requestId) {
                return;
            }

            node->title = payload.label.empty() ? "Slice" : payload.label;
            node->image = std::move(payload);
            m_GraphPerformanceStats.lastSliceImportQueueMs = queueMs;
            m_GraphPerformanceStats.lastSliceImportDecodeMs = decodeMs;
            m_GraphPerformanceStats.lastSliceImportPreviewMs = previewMs;
            m_GraphPerformanceStats.lastSliceImportStorageCopyMs = storageCopyMs;
            m_GraphPerformanceStats.lastSliceImportWidth = node->image.width;
            m_GraphPerformanceStats.lastSliceImportHeight = node->image.height;
            m_GraphPerformanceStats.lastSliceImportPixelBytes = pixelBytes;
            MarkDirty();
        });
        if (!payloadQueued) {
            return;
        }

        const auto embedBegin = std::chrono::steady_clock::now();
        std::vector<unsigned char> pngBytes =
            EncodePngBytesForImageStorageOwned(std::move(storagePixels), width, height, channels);
        const double embedMs = MillisecondsSince(embedBegin);
        Async::TaskSystem::Get().PostToMain([
            this,
            nodeId,
            requestId,
            embedMs,
            pngBytes = std::move(pngBytes)
        ]() mutable {
            m_GraphPerformanceStats.lastSliceImportEmbedMs = embedMs;
            m_GraphPerformanceStats.lastSliceImportEmbeddedBytes = pngBytes.size();
            EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
            if (!node || node->kind != EditorNodeGraph::NodeKind::Image ||
                !node->image.isEmbedding || node->image.embeddingRequestId != requestId) {
                return;
            }

            if (pngBytes.empty()) {
                node->image.isEmbedding = false;
                node->image.embeddingRequestId = 0;
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    "The slice loaded, but Stack could not embed it for project storage.",
                    "editor-graph-image-embed");
                MarkDirty();
                return;
            }

            node->image.pngBytes = std::move(pngBytes);
            node->image.isEmbedding = false;
            node->image.embeddingRequestId = 0;
            MarkDirty();
        });
        } catch (...) {
            if (payloadQueued) {
                postEmbeddingFailure();
            } else {
                postImportFailure();
            }
        }
        });
    } catch (const std::bad_alloc&) {
        submitted = false;
    } catch (const std::length_error&) {
        submitted = false;
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        m_NodeGraph.RemoveNode(nodeId);
        m_NodeGraph.ClearSelection();
        for (const int selectedNodeId : selectionBefore) {
            m_NodeGraph.SelectNode(selectedNodeId, true);
        }
        MarkDirty();
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "The image import could not be queued.",
            "editor-graph-image-import");
        return false;
    }

    return true;
}

bool EditorModule::HasPendingGraphImageImports() const {
    return std::any_of(
        m_NodeGraph.GetNodes().begin(),
        m_NodeGraph.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) {
            return node.kind == EditorNodeGraph::NodeKind::Image &&
                (node.image.isLoading || node.image.isEmbedding);
        });
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
        ? "Loading dropped slices into the graph..."
        : "Loading dropped slice into the graph...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().SubmitHighPriority([this, generation, validPaths = std::move(validPaths), sourcePosition]() mutable {
        try {
        std::vector<EditorNodeGraph::ImagePayload> importedImages;
        importedImages.reserve(validPaths.size());
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
            // Keep PNG embedding on the worker. The former main-thread payload
            // build made a successful drop appear to freeze after decoding.
            importedImages.push_back(BuildImagePayloadFromDecoded(path, std::move(decoded)));
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            sourcePosition,
            requestedCount = validPaths.size(),
            importedImages = std::move(importedImages),
            rawPaths = std::move(rawPaths)
        ]() mutable {
            if (generation != m_GraphDropImportGeneration) {
                return;
            }

            const Raw::LibRawRuntimeStatus& rawRuntimeStatus = Raw::GetLibRawRuntimeStatus();
            const bool rawRuntimeUnavailable = !rawPaths.empty() && !rawRuntimeStatus.runtimeAvailable;

            if (importedImages.empty() && rawPaths.empty()) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                m_GraphDropImportStatusText = "Failed to import the dropped slices.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to import the dropped slices.", "editor-graph-drop-import");
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
                }
                return;
            }

            m_GraphDropImportTaskState = Async::TaskState::Applying;
            m_GraphDropImportStatusText = "Creating slice nodes...";

            constexpr float kGraphDropRowSpacing = 190.0f;
            const std::size_t totalNodes = importedImages.size() + rawPaths.size();
            const float startY = sourcePosition.y - (static_cast<float>(totalNodes - 1) * kGraphDropRowSpacing * 0.5f);
            int importedCount = 0;
            size_t outputIndex = 0;
            for (size_t index = 0; index < importedImages.size(); ++index, ++outputIndex) {
                EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                nodePosition.y = startY + static_cast<float>(outputIndex) * kGraphDropRowSpacing;
                if (AddGraphImageChainFromPayload(
                    std::move(importedImages[index]),
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
                    m_GraphDropImportStatusText = "Failed to create graph nodes for the dropped slices.";
                    QueueUiNotification(UiNotificationSeverity::Error, "Failed to create graph nodes for the dropped slices.", "editor-graph-drop-import");
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
                    ? "Imported 1 slice into the graph."
                    : "Imported " + std::to_string(importedCount) + " slices into the graph.";
            } else {
                m_GraphDropImportStatusText =
                    "Imported " + std::to_string(importedCount) + " of " + std::to_string(requestedCount) + " dropped slices.";
            }
            QueueUiNotification(UiNotificationSeverity::Success, m_GraphDropImportStatusText, "editor-graph-drop-import");

            if (!m_PendingGraphDropImports.empty()) {
                PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
            }
        });
        } catch (...) {
            Async::TaskSystem::Get().PostToMain([this, generation]() {
                if (generation != m_GraphDropImportGeneration) {
                    return;
                }
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                SetImportStatusNoThrow(
                    m_GraphDropImportStatusText,
                    "The dropped slices could not be decoded.");
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    "The dropped slices could not be decoded.",
                    "editor-graph-drop-import");
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest =
                        std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(
                        std::move(nextRequest.paths),
                        nextRequest.sourcePosition);
                }
            });
        }
        });
    } catch (const std::bad_alloc&) {
        submitted = false;
    } catch (const std::length_error&) {
        submitted = false;
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        m_GraphDropImportTaskState = Async::TaskState::Failed;
        SetImportStatusNoThrow(
            m_GraphDropImportStatusText,
            "The dropped slices could not be queued for import.");
        return false;
    }

    return true;
}

bool EditorModule::UseGraphImageNodeAsActiveSource(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Image || node->image.pixels.empty()) {
        return false;
    }

    LoadSourceFromImagePayload(node->image, true, false);
    m_NodeGraph.SetActiveImageNodeId(nodeId);
    MarkNodeBrowserThumbnailSourceChanged();
    SelectGraphNode(nodeId);
    MarkRenderDirty(nodeId);
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
        LoadSourceFromImagePayload(node->image, true, false);
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
    // PNG compression can be expensive for large slices; rebuild storage bytes lazily on save/export.
    node->image.pngBytes.clear();
    node->image.isEmbedding = false;
    node->image.embeddingRequestId = 0;
    EditorNodeGraph::InvalidateImagePayloadRuntime(node->image);
    EditorNodeGraph::BuildImagePayloadPreview(
        node->image.pixels,
        node->image.width,
        node->image.height,
        node->image.channels,
        node->image.previewPixels,
        node->image.previewWidth,
        node->image.previewHeight,
        node->image.previewChannels);

    if (m_NodeGraph.GetActiveImageNodeId() == nodeId) {
        LoadSourceFromImagePayload(node->image, true, false);
        MarkNodeBrowserThumbnailSourceChanged();
    }

    MarkRenderDirty(nodeId);
    return true;
}
