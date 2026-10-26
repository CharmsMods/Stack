#pragma once
#include "Editor/EditorRenderWorker.h"
#include "Renderer/ViewportPreviewPolicy.h"
#include <string>

namespace Stack::EditorRendering {
// Camera demand, resident presentation, and measurements belong to one view
// of one editor session. RAW and Graph retain independent cameras.
struct ViewportPreviewState {
    Renderer::ViewportPreviewRequest request;
    Renderer::ViewportPreviewGeometry geometry;
    Renderer::ViewportPreviewTimingCurve timings;
    Renderer::ViewportPreviewSelection selection;
    EditorRenderWorker::SharedTextureResult overview;
    GraphRendering::RequestTag acceptedTag;
    GraphRendering::RequestTag submittedTag;
    RawRenderPurpose submittedPurpose = RawRenderPurpose::InteractivePresentation;
    std::size_t contentIdentity = 0, acceptedContentIdentity = 0, overviewContentIdentity = 0;
    std::size_t executionIdentity = 0;
    std::uint64_t acceptedGeneration = 0;
    std::uint64_t submittedViewGeneration = 0;
    std::uint64_t refinementGeneration = 0;
    std::uint64_t analysisGeneration = 0;
    std::uint64_t blockedBudget = 0;
    int physicalWidth = 1, physicalHeight = 1, interactiveEdge = 0;
    int mainDemandFrame = -10;
    Renderer::ImageRegion backdropVisible;
    int backdropPhysicalWidth = 1, backdropPhysicalHeight = 1, backdropDemandFrame = -10;
    double contentChangedAt = -1;
    double lastSubmissionAt = -1;
    bool navigationActive = false, acceptedInteractive = false;
    double refinementRetryAt = -1;
    int refinementFailures = 0;
    std::string refinementError;
    bool mainEmpty = false, refinement = false, refinementBlocked = false;
};
}
