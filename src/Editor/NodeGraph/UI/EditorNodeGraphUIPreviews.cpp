#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Async/TaskSystem.h"
#include <atomic>
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace {

size_t HashImagePayload(const EditorNodeGraph::Node& node) {
    size_t hash = 1469598103934665603ull;
    auto mix = [&](size_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(static_cast<size_t>(node.image.width));
    mix(static_cast<size_t>(node.image.height));
    mix(static_cast<size_t>(std::max(1, node.image.channels)));
    mix(static_cast<size_t>(node.image.previewWidth));
    mix(static_cast<size_t>(node.image.previewHeight));
    mix(node.image.previewPixels.size());
    if (!node.image.previewPixels.empty()) {
        mix(node.image.previewPixels.front());
        mix(node.image.previewPixels[node.image.previewPixels.size() / 2]);
        mix(node.image.previewPixels.back());
    }
    mix(node.image.pixels.size());
    if (!node.image.pixels.empty()) {
        mix(node.image.pixels.front());
        mix(node.image.pixels[node.image.pixels.size() / 2]);
        mix(node.image.pixels.back());
    } else {
        mix(node.image.pngBytes.size());
        if (!node.image.pngBytes.empty()) {
            mix(node.image.pngBytes.front());
            mix(node.image.pngBytes[node.image.pngBytes.size() / 2]);
            mix(node.image.pngBytes.back());
        }
    }
    return hash;
}

bool DecodeImagePreviewPixelsFromPng(
    const std::vector<unsigned char>& pngBytes,
    std::vector<unsigned char>& outPixels,
    int& outWidth,
    int& outHeight,
    int& outChannels) {
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;
    outChannels = 4;
    if (pngBytes.empty() || pngBytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    stbi_set_flip_vertically_on_load_thread(1);
    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        pngBytes.data(),
        static_cast<int>(pngBytes.size()),
        &decodedWidth,
        &decodedHeight,
        &decodedChannels,
        4);
    if (!pixels || decodedWidth <= 0 || decodedHeight <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const bool copied = Stack::PixelBuffer::CopyInterleavedPixels(
        pixels, decodedWidth, decodedHeight, 4, outPixels);
    stbi_image_free(pixels);
    if (!copied) {
        outPixels.clear();
        return false;
    }
    outWidth = decodedWidth;
    outHeight = decodedHeight;
    outChannels = 4;
    return true;
}

} // namespace

// Worker-owned input/result only: no UI, graph or editor pointer crosses the
// thread boundary. Removing a cache entry makes a late result unreachable.
struct EditorNodeGraphUI::ImagePreviewDecode {
    size_t fingerprint = 0;
    std::atomic<bool> ready{false};
    std::vector<unsigned char> pixels;
    int width = 0, height = 0, channels = 4;
};

EditorNodeGraphUI::~EditorNodeGraphUI() {
    Shutdown();
}

void EditorNodeGraphUI::Shutdown() {
    m_DragLayout.reset();
    m_LayoutGroupPresentation.clear();
    m_LayoutRestingNodes.clear();m_LayoutRestingGroups.clear();
    ClearLayoutMotion();
    m_NodeOpenAppearance = {};
    m_PresetPreviewCurrentRenderer.reset();
    m_PresetPreviewPreviousRenderer.reset();
    m_SocketIconTexture.Reset();
    ResetPerGraphVisualCaches();
}

void EditorNodeGraphUI::ResetPerGraphVisualCaches() {
    m_NodeNotes.Reset();
    m_WireRoutes.reset();m_PreparedWireWorldRevision=0;++m_WireWorldRevision;m_WireGraph=nullptr;
    m_NodeLayoutCache.clear();
    m_NodeOrderCacheGraph = nullptr;
    m_NodeOrderCacheNodes = nullptr;
    m_LinkHitTestFrame = -1;
    m_LinkLabelHitRects.clear();
    m_LinkLabelRevealAlpha = 0.0f;
    for (auto& item : m_ImagePreviewTextures) {
        unsigned int texture = item.second;
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }
    for (auto& item : m_GraphPreviewTextures) {
        unsigned int texture = item.second;
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }
    for (auto& item : m_NodeBrowserThumbnailTextures) {
        unsigned int texture = item.second;
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }

    m_ImagePreviewTextures.clear();
    m_ImagePreviewDecodes.clear();
    m_ImagePreviewFingerprints.clear();
    m_ImagePreviewSizes.clear();
    m_GraphPreviewTextures.clear();
    m_GraphPreviewRevisions.clear();
    m_GraphPreviewSizes.clear();
    m_NodeBrowserThumbnailTextures.clear();
    m_NodeBrowserThumbnailRevisions.clear();
    m_NodeBrowserThumbnailSizes.clear();
    m_NodeMeasuredBaseHeights.clear();
    m_NodeMeasuredContentRevisions.clear();
    m_NodeContentOverflow.clear();
    m_NodeFrontOrder.clear();
    m_NodeFrontOrderCounter = 1;
    ResetDetailCardHover();
}

void EditorNodeGraphUI::SyncPerGraphVisualCaches(const EditorNodeGraph::Graph& graph) {
    std::unordered_set<int> liveNodeIds;
    liveNodeIds.reserve(graph.GetNodes().size());
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        liveNodeIds.insert(node.id);
    }

    auto pruneTextureMap = [&](auto& textureMap) {
        for (auto it = textureMap.begin(); it != textureMap.end(); ) {
            if (liveNodeIds.find(it->first) != liveNodeIds.end()) {
                ++it;
                continue;
            }
            unsigned int texture = it->second;
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
            it = textureMap.erase(it);
        }
    };

    auto pruneKeyedMap = [&](auto& map) {
        for (auto it = map.begin(); it != map.end(); ) {
            if (liveNodeIds.find(it->first) != liveNodeIds.end()) {
                ++it;
            } else {
                it = map.erase(it);
            }
        }
    };

    pruneTextureMap(m_ImagePreviewTextures);
    pruneTextureMap(m_GraphPreviewTextures);
    pruneKeyedMap(m_ImagePreviewFingerprints);
    pruneKeyedMap(m_ImagePreviewDecodes);
    pruneKeyedMap(m_ImagePreviewSizes);
    pruneKeyedMap(m_GraphPreviewRevisions);
    pruneKeyedMap(m_GraphPreviewSizes);
    pruneKeyedMap(m_NodeMeasuredBaseHeights);
    pruneKeyedMap(m_NodeMeasuredContentRevisions);
    pruneKeyedMap(m_NodeContentOverflow);
    pruneKeyedMap(m_NodeFrontOrder);
    pruneKeyedMap(m_NodeLayoutCache);
    for (auto it = m_RevealedAdvancedInputNodes.begin(); it != m_RevealedAdvancedInputNodes.end();) {
        if (liveNodeIds.count(*it)) ++it;
        else it = m_RevealedAdvancedInputNodes.erase(it);
    }
    for (auto it = m_RevealedAdvancedOutputNodes.begin(); it != m_RevealedAdvancedOutputNodes.end();) {
        if (liveNodeIds.count(*it)) ++it;
        else it = m_RevealedAdvancedOutputNodes.erase(it);
    }
}

unsigned int EditorNodeGraphUI::GetImagePreviewTexture(const EditorNodeGraph::Node& node) {
    if (node.kind != EditorNodeGraph::NodeKind::Image || node.image.width <= 0 || node.image.height <= 0) {
        return 0;
    }

    const size_t fingerprint = HashImagePayload(node);
    auto it = m_ImagePreviewTextures.find(node.id);
    auto fingerprintIt = m_ImagePreviewFingerprints.find(node.id);
    if (it != m_ImagePreviewTextures.end() &&
        fingerprintIt != m_ImagePreviewFingerprints.end() &&
        fingerprintIt->second == fingerprint &&
        it->second != 0) {
        return it->second;
    }

    const unsigned int previous = it != m_ImagePreviewTextures.end() ? it->second : 0;
    const std::vector<unsigned char>* decodedPixels = nullptr;
    int decodedWidth = 0, decodedHeight = 0, decodedChannels = 4;
    if (node.image.previewPixels.empty() && node.image.pixels.empty()) {
        auto& pending = m_ImagePreviewDecodes[node.id];
        if (!pending || pending->fingerprint != fingerprint) {
            // Admit at most one cold image per frame, including failed PNGs.
            if (m_ImagePreviewDecodeFrame == ImGui::GetFrameCount()) return previous;
            m_ImagePreviewDecodeFrame = ImGui::GetFrameCount();
            pending = std::make_shared<ImagePreviewDecode>();
            pending->fingerprint = fingerprint;
            const auto result = pending;
            if (!Async::TaskSystem::Get().Submit("Preparing image node preview",
                [target = std::weak_ptr<ImagePreviewDecode>(result), bytes = node.image.pngBytes] {
                    const auto result = target.lock();
                    if (!result) return;
                    try {
                        std::vector<unsigned char> pixels;
                        int width = 0, height = 0, channels = 4;
                        if (DecodeImagePreviewPixelsFromPng(bytes, pixels, width, height, channels)) {
                            EditorNodeGraph::BuildImagePayloadPreview(pixels, width, height, channels,
                                result->pixels, result->width, result->height, result->channels);
                            if (result->pixels.empty() && width <= 768 && height <= 768) {
                                result->pixels = std::move(pixels);
                                result->width = width; result->height = height; result->channels = channels;
                            }
                        }
                    } catch (...) { result->pixels.clear(); }
                    result->ready.store(true, std::memory_order_release);
                })) result->ready.store(true, std::memory_order_release);
            return previous;
        }
        if (!pending->ready.load(std::memory_order_acquire) || pending->pixels.empty()) return previous;
        decodedPixels = &pending->pixels;
        decodedWidth = pending->width; decodedHeight = pending->height; decodedChannels = pending->channels;
    } else m_ImagePreviewDecodes.erase(node.id);

    const unsigned char* uploadPixels = nullptr;
    int uploadWidth = node.image.width;
    int uploadHeight = node.image.height;
    int uploadChannels = std::max(1, node.image.channels);
    std::vector<unsigned char> generatedPreviewPixels;
    const std::vector<unsigned char>* previewSourcePixels = nullptr;
    if (!node.image.previewPixels.empty() &&
        node.image.previewWidth > 0 &&
        node.image.previewHeight > 0) {
        uploadPixels = node.image.previewPixels.data();
        uploadWidth = node.image.previewWidth;
        uploadHeight = node.image.previewHeight;
        uploadChannels = std::max(1, node.image.previewChannels);
        previewSourcePixels = &node.image.previewPixels;
    } else if (!node.image.pixels.empty()) {
        uploadPixels = node.image.pixels.data();
        previewSourcePixels = &node.image.pixels;
    } else if (decodedPixels && !decodedPixels->empty()) {
        uploadWidth = decodedWidth; uploadHeight = decodedHeight; uploadChannels = decodedChannels;
        uploadPixels = decodedPixels->data();
        previewSourcePixels = decodedPixels;
    } else {
        return previous;
    }
    if (!previewSourcePixels || !Stack::PixelBuffer::HasCompletePixelBuffer(
            previewSourcePixels->size(), uploadWidth, uploadHeight, uploadChannels))
        return previous;

    // Non-import image payloads may not carry a runtime thumbnail. Downsample
    // them before any GL upload so a tiny node never allocates a full-resolution
    // graph-preview texture.
    if (previewSourcePixels && (uploadWidth > 768 || uploadHeight > 768)) {
        int previewWidth = 0;
        int previewHeight = 0;
        int previewChannels = uploadChannels;
        EditorNodeGraph::BuildImagePayloadPreview(
            *previewSourcePixels,
            uploadWidth,
            uploadHeight,
            uploadChannels,
            generatedPreviewPixels,
            previewWidth,
            previewHeight,
            previewChannels);
        if (!generatedPreviewPixels.empty()) {
            uploadPixels = generatedPreviewPixels.data();
            uploadWidth = previewWidth;
            uploadHeight = previewHeight;
            uploadChannels = previewChannels;
        }
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(
        uploadPixels,
        uploadWidth,
        uploadHeight,
        uploadChannels);
    if (texture == 0) {
        return previous;
    }

    if (previous != 0) glDeleteTextures(1, &previous);
    m_ImagePreviewTextures[node.id] = texture;
    m_ImagePreviewFingerprints[node.id] = fingerprint;
    m_ImagePreviewSizes[node.id] = ImVec2(static_cast<float>(uploadWidth), static_cast<float>(uploadHeight));
    m_ImagePreviewDecodes.erase(node.id);
    return texture;
}

unsigned int EditorNodeGraphUI::UploadPreviewTexture(int nodeId, const std::vector<unsigned char>& pixels, int width, int height) {
    if (!Stack::PixelBuffer::HasCompletePixelBuffer(pixels.size(), width, height, 4)) {
        return 0;
    }

    std::vector<unsigned char> flippedPixels = pixels;
    const std::size_t rowStride = static_cast<std::size_t>(width) * 4u;
    std::vector<unsigned char> tempRow(rowStride);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = flippedPixels.data() + static_cast<std::size_t>(y) * rowStride;
        unsigned char* bottom = flippedPixels.data() + static_cast<std::size_t>(height - 1 - y) * rowStride;
        std::memcpy(tempRow.data(), top, rowStride);
        std::memcpy(top, bottom, rowStride);
        std::memcpy(bottom, tempRow.data(), rowStride);
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(flippedPixels.data(), width, height, 4);
    if (texture != 0) {
        const auto existing = m_GraphPreviewTextures.find(nodeId);
        if (existing != m_GraphPreviewTextures.end() && existing->second != 0)
            glDeleteTextures(1, &existing->second);
        m_GraphPreviewTextures[nodeId] = texture;
        m_GraphPreviewSizes[nodeId] = ImVec2(static_cast<float>(width), static_cast<float>(height));
    }
    return texture;
}

unsigned int EditorNodeGraphUI::GetGraphPreviewTexture(EditorModule* editor, const EditorNodeGraph::Node& node) {
    if (!editor ||
        (node.kind != EditorNodeGraph::NodeKind::Preview &&
         node.kind != EditorNodeGraph::NodeKind::RawDetailAutoMask)) {
        return 0;
    }

    const std::uint64_t previewRevision = editor->GetPreviewNodeRevision(node.id);
    auto textureIt = m_GraphPreviewTextures.find(node.id);
    auto revisionIt = m_GraphPreviewRevisions.find(node.id);
    if (previewRevision == 0) {
        if (textureIt != m_GraphPreviewTextures.end() && textureIt->second != 0) {
            unsigned int oldTexture = textureIt->second;
            glDeleteTextures(1, &oldTexture);
            m_GraphPreviewTextures.erase(textureIt);
        }
        m_GraphPreviewSizes.erase(node.id);
        m_GraphPreviewRevisions[node.id] = 0;
        return 0;
    }
    if (textureIt != m_GraphPreviewTextures.end() &&
        revisionIt != m_GraphPreviewRevisions.end() &&
        revisionIt->second == previewRevision) {
        return textureIt->second;
    }
    if (m_MiddlePanCaptureActive) {
        return textureIt != m_GraphPreviewTextures.end() ? textureIt->second : 0;
    }

    if (const EditorModule::GraphPreviewPixels* cached = editor->GetCachedPreviewPixelsForNode(node.id)) {
        if (cached->revision >= previewRevision && !cached->pixels.empty() && cached->width > 0 && cached->height > 0) {
            const unsigned int texture = UploadPreviewTexture(node.id, cached->pixels, cached->width, cached->height);
            if (texture != 0) {
                m_GraphPreviewRevisions[node.id] = cached->revision;
                return texture;
            }
        }
    }

    return textureIt != m_GraphPreviewTextures.end() ? textureIt->second : 0;
}

unsigned int EditorNodeGraphUI::GetNodeBrowserThumbnailTexture(
    EditorModule* editor,
    const std::string& previewKey,
    ImVec2* outSize,
    bool* outPending,
    bool* outFallback) {
    if (outSize) {
        *outSize = ImVec2(0.0f, 0.0f);
    }
    if (outPending) {
        *outPending = false;
    }
    if (outFallback) {
        *outFallback = false;
    }
    if (!editor || previewKey.empty()) {
        return 0;
    }

    EditorModule::NodeBrowserThumbnailView view;
    if (!editor->GetNodeBrowserThumbnailView(previewKey, view)) {
        const auto existing = m_NodeBrowserThumbnailTextures.find(previewKey);
        if (existing != m_NodeBrowserThumbnailTextures.end() && existing->second != 0) {
            unsigned int oldTexture = existing->second;
            glDeleteTextures(1, &oldTexture);
            m_NodeBrowserThumbnailTextures.erase(existing);
        }
        m_NodeBrowserThumbnailRevisions.erase(previewKey);
        m_NodeBrowserThumbnailSizes.erase(previewKey);
        return 0;
    }

    if (outPending) {
        *outPending = view.pending;
    }
    if (outFallback) {
        *outFallback = view.fallback;
    }
    if (outSize && view.width > 0 && view.height > 0) {
        *outSize = ImVec2(static_cast<float>(view.width), static_cast<float>(view.height));
    }
    if (outSize) {
        const auto sizeIt = m_NodeBrowserThumbnailSizes.find(previewKey);
        if (sizeIt != m_NodeBrowserThumbnailSizes.end()) {
            *outSize = sizeIt->second;
        }
    }
    if (!view.pngBytes || view.pngBytes->empty()) {
        return 0;
    }

    const auto textureIt = m_NodeBrowserThumbnailTextures.find(previewKey);
    const auto revisionIt = m_NodeBrowserThumbnailRevisions.find(previewKey);
    if (textureIt != m_NodeBrowserThumbnailTextures.end() &&
        revisionIt != m_NodeBrowserThumbnailRevisions.end() &&
        revisionIt->second == view.revision &&
        textureIt->second != 0) {
        if (outSize) {
            const auto sizeIt = m_NodeBrowserThumbnailSizes.find(previewKey);
            if (sizeIt != m_NodeBrowserThumbnailSizes.end()) {
                *outSize = sizeIt->second;
            }
        }
        return textureIt->second;
    }

    const int frameCount = ImGui::GetFrameCount();
    if (m_NodeBrowserTextureUploadBudgetFrame != frameCount) {
        m_NodeBrowserTextureUploadBudgetFrame = frameCount;
        m_NodeBrowserTextureUploadsThisFrame = 0;
    }
    constexpr int kMaxTextureUploadsPerFrame = 4;
    const unsigned int existingTexture =
        (textureIt != m_NodeBrowserThumbnailTextures.end()) ? textureIt->second : 0u;
    if (m_NodeBrowserTextureUploadsThisFrame >= kMaxTextureUploadsPerFrame) {
        return existingTexture;
    }

    const std::vector<unsigned char>* pixels = view.decodedPixels;
    int decodedWidth = view.width;
    int decodedHeight = view.height;
    int decodedChannels = view.channels;
    if (!pixels || !Stack::PixelBuffer::HasCompletePixelBuffer(
            pixels->size(), decodedWidth, decodedHeight, decodedChannels)) {
        // GetNodeBrowserThumbnailView schedules the project-owned decoder.
        // Keep the last card (or its placeholder) while those pixels arrive.
        return existingTexture;
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(
        pixels->data(),
        decodedWidth,
        decodedHeight,
        decodedChannels);
    if (texture == 0) {
        return existingTexture;
    }

    ++m_NodeBrowserTextureUploadsThisFrame;

    if (existingTexture != 0 && existingTexture != texture) {
        unsigned int oldTexture = existingTexture;
        glDeleteTextures(1, &oldTexture);
    }

    m_NodeBrowserThumbnailTextures[previewKey] = texture;
    m_NodeBrowserThumbnailRevisions[previewKey] = view.revision;
    m_NodeBrowserThumbnailSizes[previewKey] = ImVec2(
        static_cast<float>(decodedWidth),
        static_cast<float>(decodedHeight));
    if (outSize) {
        *outSize = m_NodeBrowserThumbnailSizes[previewKey];
    }
    return texture;
}
