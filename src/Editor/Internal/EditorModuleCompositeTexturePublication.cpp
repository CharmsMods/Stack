#include "Editor/EditorModule.h"

#include "Renderer/GLHelpers.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace {

struct PreparedCompositePixels {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

bool PrepareCompositePixels(
    std::vector<unsigned char> source,
    int width,
    int height,
    int padding,
    bool keepFullFrame,
    PreparedCompositePixels& output) {
    std::size_t sourceBytes = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            width, height, 4, sourceBytes) ||
        source.size() != sourceBytes) {
        return false;
    }

    if (keepFullFrame) {
        output.pixels = std::move(source);
        output.width = width;
        output.height = height;
        return true;
    }

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < height; ++y) {
        const std::size_t row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) * 4u;
        for (int x = 0; x < width; ++x) {
            const std::size_t alpha =
                row + static_cast<std::size_t>(x) * 4u + 3u;
            if (source[alpha] == 0) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        output.pixels = std::move(source);
        output.width = width;
        output.height = height;
        return true;
    }

    const int safePadding = std::max(0, padding);
    minX = std::max(0, minX - safePadding);
    minY = std::max(0, minY - safePadding);
    maxX = std::min(width - 1, maxX + safePadding);
    maxY = std::min(height - 1, maxY + safePadding);
    const int croppedWidth = maxX - minX + 1;
    const int croppedHeight = maxY - minY + 1;
    if (croppedWidth == width && croppedHeight == height) {
        output.pixels = std::move(source);
        output.width = width;
        output.height = height;
        return true;
    }

    std::size_t croppedBytes = 0;
    std::size_t croppedRowBytes = 0;
    std::size_t sourceRowBytes = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            croppedWidth, croppedHeight, 4, croppedBytes) ||
        !Stack::PixelBuffer::TryComputePixelByteCount(
            croppedWidth, 1, 4, croppedRowBytes) ||
        !Stack::PixelBuffer::TryComputePixelByteCount(
            width, 1, 4, sourceRowBytes)) {
        return false;
    }

    std::vector<unsigned char> cropped;
    try {
        cropped.resize(croppedBytes);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    for (int y = 0; y < croppedHeight; ++y) {
        const std::size_t sourceOffset =
            static_cast<std::size_t>(minY + y) * sourceRowBytes +
            static_cast<std::size_t>(minX) * 4u;
        const std::size_t destinationOffset =
            static_cast<std::size_t>(y) * croppedRowBytes;
        std::memcpy(
            cropped.data() + destinationOffset,
            source.data() + sourceOffset,
            croppedRowBytes);
    }

    output.pixels = std::move(cropped);
    output.width = croppedWidth;
    output.height = croppedHeight;
    return true;
}

} // namespace

void EditorModule::ResetCompositeOutputRequestForRetry(int outputNodeId) {
    m_CompositeOutputRequestedGenerations.erase(outputNodeId);
    CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId);
    if (item == nullptr) {
        return;
    }
    item->requestedRenderRevision = item->cachedRenderRevision;
    item->requestedChainFingerprint = item->cachedChainFingerprint;
    item->requestedRasterWidth = item->textureWidth;
    item->requestedRasterHeight = item->textureHeight;
}

void EditorModule::ResetIncompleteCompositeOutputRequestsForRetry() {
    for (auto request = m_CompositeOutputRequestedGenerations.begin();
         request != m_CompositeOutputRequestedGenerations.end();) {
        const auto completed =
            m_CompositeOutputCompletedGenerations.find(request->first);
        const std::uint64_t completedGeneration =
            completed == m_CompositeOutputCompletedGenerations.end()
                ? 0
                : completed->second;
        if (completedGeneration >= request->second) {
            ++request;
            continue;
        }
        const int outputNodeId = request->first;
        request = m_CompositeOutputRequestedGenerations.erase(request);
        CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId);
        if (item != nullptr) {
            item->requestedRenderRevision = item->cachedRenderRevision;
            item->requestedChainFingerprint =
                item->cachedChainFingerprint;
            item->requestedRasterWidth = item->textureWidth;
            item->requestedRasterHeight = item->textureHeight;
        }
    }
}

bool EditorModule::PublishCompositeOutputPixels(
    int outputNodeId,
    std::vector<unsigned char> pixels,
    int width,
    int height,
    std::uint64_t renderRevision,
    std::size_t chainFingerprint) {
    CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId);
    if (item == nullptr) {
        ResetCompositeOutputRequestForRetry(outputNodeId);
        return false;
    }

    const bool scalableGenerator =
        CompletedChainSourceUsesScalableGenerator(outputNodeId);
    const bool keepFullFrame =
        scalableGenerator &&
        CompletedChainSourceKeepsFullRasterFrame(outputNodeId);
    PreparedCompositePixels prepared;
    try {
        if (!PrepareCompositePixels(
                std::move(pixels),
                width,
                height,
                scalableGenerator ? 2 : 0,
                keepFullFrame,
                prepared)) {
            ResetCompositeOutputRequestForRetry(outputNodeId);
            return false;
        }
    } catch (const std::bad_alloc&) {
        ResetCompositeOutputRequestForRetry(outputNodeId);
        return false;
    } catch (const std::length_error&) {
        ResetCompositeOutputRequestForRetry(outputNodeId);
        return false;
    }

    decltype(m_CompositeOutputCompletedGenerations)::iterator completion;
    try {
        completion =
            m_CompositeOutputCompletedGenerations
                .try_emplace(outputNodeId, 0)
                .first;
    } catch (const std::bad_alloc&) {
        ResetCompositeOutputRequestForRetry(outputNodeId);
        return false;
    } catch (const std::length_error&) {
        ResetCompositeOutputRequestForRetry(outputNodeId);
        return false;
    }

    const unsigned int replacement =
        GLHelpers::CreateTextureFromPixels(
            prepared.pixels.data(),
            prepared.width,
            prepared.height,
            4);
    if (replacement == 0) {
        ResetCompositeOutputRequestForRetry(outputNodeId);
        return false;
    }

    const unsigned int priorTexture = item->texture;
    item->texture = replacement;
    item->textureWidth = prepared.width;
    item->textureHeight = prepared.height;
    item->keepFullRasterFrame = keepFullFrame;
    item->rgbaPixels = std::move(prepared.pixels);
    item->cachedRenderRevision = renderRevision;
    item->cachedChainFingerprint = chainFingerprint;
    item->requestedRenderRevision = renderRevision;
    item->requestedChainFingerprint = chainFingerprint;
    item->requestedRasterWidth = prepared.width;
    item->requestedRasterHeight = prepared.height;
    completion->second = renderRevision;
    if (priorTexture != 0) {
        glDeleteTextures(1, &priorTexture);
    }
    return true;
}
