#include "Editor/CompositePixels.h"
#include "Editor/EditorModule.h"

#include "Renderer/GLHelpers.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

using namespace Stack::EditorRendering;


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
    std::size_t chainFingerprint, bool pixelsPrepared) {
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
        if (pixelsPrepared) {
            if (!Stack::PixelBuffer::HasCompletePixelBuffer(pixels.size(), width, height, 4)) {
                ResetCompositeOutputRequestForRetry(outputNodeId);
                return false;
            }
            prepared = {std::move(pixels), width, height};
        } else if (!PrepareCompositePixels(
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
    // An intermediate result must not overwrite the identity of a newer
    // queued request, or the UI will submit that same request on every frame.
    if (renderRevision >= item->requestedRenderRevision) {
        item->requestedRenderRevision = renderRevision;
        item->requestedChainFingerprint = chainFingerprint;
        item->requestedRasterWidth = prepared.width;
        item->requestedRasterHeight = prepared.height;
    }
    completion->second = std::max(completion->second, renderRevision);
    if (priorTexture != 0) {
        glDeleteTextures(1, &priorTexture);
    }
    return true;
}
