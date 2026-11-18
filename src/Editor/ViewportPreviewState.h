#pragma once
#include "Editor/EditorRenderWorker.h"
#include "Editor/ViewportFramePacer.h"
#include "Renderer/ViewportPreviewPolicy.h"
#include "Renderer/ViewportWorkloadTiming.h"
#include "Editor/ViewportQualityPreview.h"
#include "Editor/ViewportCompletionPolicy.h"
#include "Renderer/ViewportSectionStore.h"
#include <string>

namespace Stack::EditorRendering {
// Camera demand, resident presentation, and measurements belong to one view
// of one editor session. RAW and Graph retain independent cameras.
struct ViewportPreviewState {
    Renderer::ViewportPreviewRequest request;
    Renderer::ViewportPreviewRequest submittedRequest;
    Renderer::ViewportPreviewGeometry geometry;
    Renderer::ViewportWorkloadTiming timings;
    Renderer::ViewportPreviewSelection selection;
    // Keep diagnostics paired with the final planned raster, after probing,
    // stabilization and native refinement override the initial selection.
    int plannedEdge = 0;
    Renderer::ViewportPreviewTimingEstimate plannedTiming;
    ViewportQualityModel qualityModel;
    mutable ViewportQualityBar qualityBar;
    mutable double qualityBarUpdatedAt = -1;
    mutable std::uint64_t qualityBarRevision = 0;
    mutable int qualityBarRequestedFps = 0, qualityBarDisplayFps = 0;
    mutable int qualityBarMinimumEdge = 0;
    mutable bool qualityBarPreserveDetail = false;
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
    double navigationChangedAt = -1;
    double coverageRetryAt = -1;
    double planningRetryAt = -1;
    Renderer::ViewportRefinementStep refinementStep = Renderer::ViewportRefinementStep::None;
    ViewportFramePacer submissionPacer;
    ViewportCompletionPolicy completion;
    struct Section { EditorRenderWorker::SharedTextureResult native,preview; std::uint64_t lastUse=0; };
    std::shared_ptr<Renderer::ViewportSectionStore> sections;
    std::vector<Section> sectionTextures;
    int nextSection = -1;
    bool sectionNativeDemand = false, sectionsComplete = false, sectionsCoverView = false;
    bool sectionBudgetLimited = false;
    std::uint64_t sectionRevision = 0, sectionCheckedRevision = 0, sectionCheckedView = 0;
    std::uint64_t sectionUseSerial = 0, sectionCheckedBudget = 0;
    std::uint64_t sectionGpuBudgetBytes = 128ull * 1024 * 1024;
    bool sectionCheckedWhole = false;
    bool navigationActive = false, acceptedInteractive = false, overviewSettled = false;
    bool overviewCoversDisplay = false;
    double refinementRetryAt = -1;
    int refinementFailures = 0;
    std::string refinementError;
    bool mainEmpty = false, refinement = false, refinementBlocked = false;
    bool refinementMemoryDeferred = false;
};
}
