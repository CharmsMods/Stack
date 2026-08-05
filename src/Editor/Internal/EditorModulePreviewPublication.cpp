#include "Editor/EditorModule.h"

#include <new>
#include <stdexcept>
#include <utility>

void EditorModule::ResetPreviewRequestForRetry(
    int previewNodeId,
    std::uint64_t requestGeneration) {
    const auto requested =
        m_PreviewRequestedGenerations.find(previewNodeId);
    if (requested != m_PreviewRequestedGenerations.end() &&
        requested->second <= requestGeneration) {
        m_PreviewRequestedGenerations.erase(requested);
    }
}

void EditorModule::ResetIncompletePreviewRequestsForRetry() {
    for (auto requested = m_PreviewRequestedGenerations.begin();
         requested != m_PreviewRequestedGenerations.end();) {
        const auto completed =
            m_PreviewCompletedGenerations.find(requested->first);
        if (completed != m_PreviewCompletedGenerations.end() &&
            completed->second >= requested->second) {
            ++requested;
        } else {
            requested = m_PreviewRequestedGenerations.erase(requested);
        }
    }
}

bool EditorModule::PublishPreviewResultPixels(
    EditorRenderWorker::PreviewResult& previewResult) {
    GraphPreviewPixels cached;
    cached.pixels = std::move(previewResult.pixels);
    cached.width = previewResult.width;
    cached.height = previewResult.height;
    cached.revision = previewResult.dirtyGeneration;

    bool insertedPixels = false;
    bool insertedCompleted = false;
    bool insertedDisplayed = false;
    const auto rollback = [&]() {
        if (insertedPixels) {
            m_PreviewPixelCache.erase(previewResult.previewNodeId);
        }
        if (insertedCompleted) {
            m_PreviewCompletedGenerations.erase(
                previewResult.previewNodeId);
        }
        if (insertedDisplayed) {
            m_PreviewDisplayedRevisions.erase(
                previewResult.previewNodeId);
        }
        ResetPreviewRequestForRetry(
            previewResult.previewNodeId,
            previewResult.dirtyGeneration);
    };

    try {
        const auto pixelEntry =
            m_PreviewPixelCache.try_emplace(
                previewResult.previewNodeId);
        insertedPixels = pixelEntry.second;
        const auto completedEntry =
            m_PreviewCompletedGenerations.try_emplace(
                previewResult.previewNodeId,
                0);
        insertedCompleted = completedEntry.second;
        const auto displayedEntry =
            m_PreviewDisplayedRevisions.try_emplace(
                previewResult.previewNodeId,
                0);
        insertedDisplayed = displayedEntry.second;

        pixelEntry.first->second = std::move(cached);
        completedEntry.first->second =
            previewResult.dirtyGeneration;
        displayedEntry.first->second =
            previewResult.dirtyGeneration;
        return true;
    } catch (const std::bad_alloc&) {
        rollback();
        return false;
    } catch (const std::length_error&) {
        rollback();
        return false;
    }
}
