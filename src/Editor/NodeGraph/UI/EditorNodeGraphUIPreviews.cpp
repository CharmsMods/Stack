#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cstring>

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
    }
    return hash;
}

} // namespace

EditorNodeGraphUI::~EditorNodeGraphUI() {
    for (auto& item : m_ImagePreviewTextures) {
        unsigned int texture = item.second;
        if (texture) {
            glDeleteTextures(1, &texture);
        }
    }
    for (auto& item : m_GraphPreviewTextures) {
        unsigned int texture = item.second;
        if (texture) {
            glDeleteTextures(1, &texture);
        }
    }
}

unsigned int EditorNodeGraphUI::GetImagePreviewTexture(const EditorNodeGraph::Node& node) {
    if (node.kind != EditorNodeGraph::NodeKind::Image || node.image.pixels.empty() || node.image.width <= 0 || node.image.height <= 0) {
        return 0;
    }
    const size_t fingerprint = HashImagePayload(node);
    auto it = m_ImagePreviewTextures.find(node.id);
    auto fingerprintIt = m_ImagePreviewFingerprints.find(node.id);
    if (it != m_ImagePreviewTextures.end() &&
        fingerprintIt != m_ImagePreviewFingerprints.end() &&
        fingerprintIt->second == fingerprint) {
        return it->second;
    }
    if (it != m_ImagePreviewTextures.end() && it->second != 0) {
        unsigned int oldTexture = it->second;
        glDeleteTextures(1, &oldTexture);
        m_ImagePreviewTextures.erase(it);
    }
    m_ImagePreviewSizes.erase(node.id);
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(node.image.pixels.data(), node.image.width, node.image.height, node.image.channels);
    m_ImagePreviewTextures[node.id] = texture;
    m_ImagePreviewFingerprints[node.id] = fingerprint;
    m_ImagePreviewSizes[node.id] = ImVec2(static_cast<float>(node.image.width), static_cast<float>(node.image.height));
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
