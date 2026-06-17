#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include <algorithm>
#include <cstring>
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
    if (pngBytes.empty()) {
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

    outPixels.assign(pixels, pixels + (decodedWidth * decodedHeight * 4));
    outWidth = decodedWidth;
    outHeight = decodedHeight;
    outChannels = 4;
    stbi_image_free(pixels);
    return true;
}

} // namespace

EditorNodeGraphUI::~EditorNodeGraphUI() {
    ResetPerGraphVisualCaches();
}

void EditorNodeGraphUI::ResetPerGraphVisualCaches() {
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
    m_ImagePreviewFingerprints.clear();
    m_ImagePreviewSizes.clear();
    m_GraphPreviewTextures.clear();
    m_GraphPreviewRevisions.clear();
    m_GraphPreviewSizes.clear();
    m_NodeBrowserThumbnailTextures.clear();
    m_NodeBrowserThumbnailRevisions.clear();
    m_NodeBrowserThumbnailSizes.clear();
    m_NodeMeasuredBaseHeights.clear();
    m_NodeContentOverflow.clear();
    m_NodeFrontOrder.clear();
    m_NodeFrontOrderCounter = 1;
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
    pruneKeyedMap(m_ImagePreviewSizes);
    pruneKeyedMap(m_GraphPreviewRevisions);
    pruneKeyedMap(m_GraphPreviewSizes);
    pruneKeyedMap(m_NodeMeasuredBaseHeights);
    pruneKeyedMap(m_NodeContentOverflow);
    pruneKeyedMap(m_NodeFrontOrder);
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

    if (it != m_ImagePreviewTextures.end() && it->second != 0) {
        unsigned int oldTexture = it->second;
        glDeleteTextures(1, &oldTexture);
    }
    m_ImagePreviewTextures.erase(node.id);
    m_ImagePreviewFingerprints.erase(node.id);
    m_ImagePreviewSizes.erase(node.id);

    const unsigned char* uploadPixels = nullptr;
    int uploadWidth = node.image.width;
    int uploadHeight = node.image.height;
    int uploadChannels = std::max(1, node.image.channels);
    std::vector<unsigned char> decodedPixels;
    if (!node.image.pixels.empty()) {
        uploadPixels = node.image.pixels.data();
    } else if (DecodeImagePreviewPixelsFromPng(node.image.pngBytes, decodedPixels, uploadWidth, uploadHeight, uploadChannels)) {
        uploadPixels = decodedPixels.data();
    } else {
        return 0;
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(
        uploadPixels,
        uploadWidth,
        uploadHeight,
        uploadChannels);
    if (texture == 0) {
        return 0;
    }

    m_ImagePreviewTextures[node.id] = texture;
    m_ImagePreviewFingerprints[node.id] = fingerprint;
    m_ImagePreviewSizes[node.id] = ImVec2(static_cast<float>(uploadWidth), static_cast<float>(uploadHeight));
    return texture;
}

unsigned int EditorNodeGraphUI::UploadPreviewTexture(int nodeId, const std::vector<unsigned char>& pixels, int width, int height) {
    if (pixels.empty() || width <= 0 || height <= 0) {
        return 0;
    }

    auto existing = m_GraphPreviewTextures.find(nodeId);
    if (existing != m_GraphPreviewTextures.end() && existing->second != 0) {
        unsigned int oldTexture = existing->second;
        glDeleteTextures(1, &oldTexture);
        m_GraphPreviewTextures.erase(existing);
    }
    m_GraphPreviewSizes.erase(nodeId);

    std::vector<unsigned char> flippedPixels = pixels;
    const int rowStride = width * 4;
    std::vector<unsigned char> tempRow(static_cast<size_t>(rowStride));
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = flippedPixels.data() + static_cast<size_t>(y * rowStride);
        unsigned char* bottom = flippedPixels.data() + static_cast<size_t>((height - 1 - y) * rowStride);
        std::memcpy(tempRow.data(), top, static_cast<size_t>(rowStride));
        std::memcpy(top, bottom, static_cast<size_t>(rowStride));
        std::memcpy(bottom, tempRow.data(), static_cast<size_t>(rowStride));
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(flippedPixels.data(), width, height, 4);
    if (texture != 0) {
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

    if (const EditorModule::GraphPreviewPixels* cached = editor->GetCachedPreviewPixelsForNode(node.id)) {
        if (cached->revision >= previewRevision && !cached->pixels.empty() && cached->width > 0 && cached->height > 0) {
            const unsigned int texture = UploadPreviewTexture(node.id, cached->pixels, cached->width, cached->height);
            m_GraphPreviewRevisions[node.id] = cached->revision;
            return texture;
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

    if (textureIt != m_NodeBrowserThumbnailTextures.end() && textureIt->second != 0) {
        unsigned int oldTexture = textureIt->second;
        glDeleteTextures(1, &oldTexture);
    }
    m_NodeBrowserThumbnailTextures.erase(previewKey);
    m_NodeBrowserThumbnailRevisions.erase(previewKey);
    m_NodeBrowserThumbnailSizes.erase(previewKey);

    const std::vector<unsigned char>* pixels = view.decodedPixels;
    std::vector<unsigned char> decodedPixels;
    int decodedWidth = view.width;
    int decodedHeight = view.height;
    int decodedChannels = view.channels;
    if (!pixels || pixels->empty() || decodedWidth <= 0 || decodedHeight <= 0) {
        if (!DecodeImagePreviewPixelsFromPng(*view.pngBytes, decodedPixels, decodedWidth, decodedHeight, decodedChannels)) {
            return 0;
        }
        pixels = &decodedPixels;
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(
        pixels->data(),
        decodedWidth,
        decodedHeight,
        decodedChannels);
    if (texture == 0) {
        return 0;
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
