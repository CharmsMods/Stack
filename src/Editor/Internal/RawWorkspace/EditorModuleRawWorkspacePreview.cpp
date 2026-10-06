#include "Utils/DisplayRefreshRate.h"
#include "Renderer/GpuMemoryBudget.h"
#include "Editor/EditorModule.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/Internal/RawWorkspace/RawWorkspaceRecipeUiPolicy.h"

#include "App/AppPaths.h"
#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Library/LibraryManager.h"
#include "Raw/RawLoader.h"
#include "Raw/RawGpuMemoryBudget.h"
#include "Raw/RawViewportDetail.h"
#include "Restormer/RestormerClient.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/PixelBufferUtils.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

using Stack::Editor::RawWorkspaceInternal::BuildLocalRangeUiRecipe;

namespace {

constexpr double kRawWorkspaceAnalysisQuietSeconds = 0.25;
constexpr double kRawWorkspaceNativeRefinementQuietSeconds = 0.20;
constexpr float kRawLocalRangeMinDeltaEv = -4.0f;
constexpr float kRawLocalRangeMaxDeltaEv = 4.0f;

bool IsRawViewportTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("STACK_RAW_VIEWPORT_TRACE");
        return value && std::string_view(value) == "1";
    }();
    return enabled;
}

void TraceRawViewportState(const std::string& state) noexcept {
    static std::mutex mutex;
    static std::string lastState;
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (state == lastState) {
            return;
        }
        lastState = state;
        const std::filesystem::path logDirectory =
            AppPaths::GetLogsDirectory();
        std::error_code error;
        std::filesystem::create_directories(logDirectory, error);
        std::ofstream output(
            logDirectory / "raw_viewport_trace.log",
            std::ios::app);
        if (output) {
            output << state << '\n';
            output.flush();
        }
    } catch (...) {
        // Diagnostics must never interfere with rendering.
    }
}


} // namespace

void EditorModule::NoteRawWorkspaceRecipePreviewEdit(bool interactionActive) {
    CancelRawViewportCalibration();
    UpdateRawViewportEditTiming(RawViewportRecipe());
    CancelRawWorkspaceCachePrewarm(true);
    m_RawWorkspacePreviewSourceKey =
        GetActiveRawWorkspacePreviewIdentity();
    const bool releasedGesture = Stack::EditorRenderScheduling::
        ShouldRequestRawRefinementOnRelease(m_RawWorkspaceAdaptiveGestureActive,
            interactionActive, HasRawWorkspaceLivePreviewForSource(
                m_RawWorkspacePreviewSourceKey));
    m_RawWorkspaceExplicitFullQualityRenderRequested = false;
    m_RawWorkspaceFullResolutionPreviewPending = true;
    m_RawWorkspaceFullResolutionPreviewRequested = releasedGesture;
    m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
    m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
    m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
    m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
    if (interactionActive &&
        !m_RawWorkspaceAdaptiveGestureActive) {
        m_RawWorkspacePreviewHealthyStreak = 0;
        m_RawWorkspacePreviewSlowSamples = 0;
        m_RawWorkspacePreviewScaleCooldown = 0;
        m_RawWorkspaceAdaptiveGestureActive = true;
        ++m_RawViewportGestureId;
        const int measuredEdge = GetCalibratedRawViewportEdge();
        if (measuredEdge > 0) {
            m_RawWorkspaceAdaptivePreviewScale = static_cast<float>(measuredEdge) /
                std::max(1, m_RawWorkspacePhysicalViewportMaxDimension);
        }
        m_RawWorkspaceAdaptiveLastAcceptedCommandTime = {};
        m_RawWorkspaceAdaptiveLastAdoptionTime = {};
        m_RawViewportEditTimingWindow.Reset();
        m_RawWorkspacePreviewSupersededStreak = 0;
    } else if (!interactionActive) {
        m_RawWorkspaceAdaptiveGestureActive = false;
        m_RawWorkspaceAdaptiveLastAcceptedCommandTime = {};
        m_RawWorkspaceAdaptiveLastAdoptionTime = {};
        m_RawWorkspacePreviewSupersededStreak = 0;
    }
    if (ImGui::GetCurrentContext()) {
        const double now = ImGui::GetTime();
        m_RawWorkspaceFastPreviewUntilTime = (interactionActive || releasedGesture)
            ? -1.0
            : now + kRawWorkspaceNativeRefinementQuietSeconds;
        m_RawWorkspaceAnalysisQuietUntilTime =
            now + kRawWorkspaceAnalysisQuietSeconds;
        m_RawWorkspaceAnalysisPending = true;
        m_RawWorkspaceAnalysisRequested = false;
        m_RawWorkspaceAnalysisScopeRetryCount = 0;
    } else {
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
        m_RawWorkspaceAnalysisQuietUntilTime = -1.0;
        m_RawWorkspaceAnalysisPending = false;
        m_RawWorkspaceAnalysisRequested = false;
    }
    if (m_RawWorkspacePhysicalViewportMaxDimension > 0) {
        if (interactionActive) {
            const float adaptiveScale =
                Stack::EditorRenderScheduling::
                    ClampRawInteractivePreviewScale(
                        m_RawWorkspaceAdaptivePreviewScale,
                        static_cast<float>(m_RawWorkspaceInteractivePreviewMaximumEdge) /
                            std::max(1, m_RawWorkspacePhysicalViewportMaxDimension));
            m_RawWorkspaceInteractivePreviewMaxDimension = std::max(
                Raw::kMinimumInteractiveViewportEdge,
                static_cast<int>(std::ceil(
                    static_cast<float>(
                        m_RawWorkspacePhysicalViewportMaxDimension) *
                    adaptiveScale / 64.0f)) * 64);
            m_RawWorkspaceInteractivePreviewMaxDimension = std::min(
                m_RawWorkspaceInteractivePreviewMaxDimension,
                m_RawWorkspaceInteractivePreviewMaximumEdge);
        } else if (!releasedGesture) {
            // The native command ignores this edge, but retain a truthful
            // physical fit target for fallback when memory policy refuses a
            // monolithic native raster.
            m_RawWorkspaceInteractivePreviewMaxDimension =
                m_RawWorkspacePhysicalViewportMaxDimension;
        }
    }
}

void EditorModule::NoteRawWorkspaceProjectOpenPreview() {
    CancelRawWorkspaceCachePrewarm(true);
    m_RawWorkspacePreviewSourceKey =
        GetActiveRawWorkspacePreviewIdentity();
    m_RawWorkspaceExplicitFullQualityRenderRequested = false;
    if (!ImGui::GetCurrentContext()) {
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
        m_RawWorkspaceFullResolutionPreviewPending = true;
        m_RawWorkspaceFullResolutionPreviewRequested = false;
        m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
        m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
        m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
        m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
        m_RawWorkspaceAnalysisQuietUntilTime = -1.0;
        m_RawWorkspaceAnalysisPending = false;
        m_RawWorkspaceAnalysisRequested = false;
        return;
    }
    // Opening always publishes a display-sized frame first. Native work is
    // deliberately held until that texture is adopted so a cold full-raster
    // render can never leave the newly opened project with an empty viewport.
    m_RawWorkspaceFastPreviewUntilTime = -1.0;
    m_RawWorkspaceFullResolutionPreviewPending = true;
    m_RawWorkspaceFullResolutionPreviewRequested = false;
    m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
    m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
    m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
    m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
    m_RawWorkspaceAnalysisQuietUntilTime =
        ImGui::GetTime() + kRawWorkspaceAnalysisQuietSeconds;
    m_RawWorkspaceAnalysisPending = true;
    m_RawWorkspaceAnalysisRequested = false;
    m_RawWorkspaceAnalysisScopeRetryCount = 0;
}

std::string EditorModule::GetActiveRawWorkspacePreviewIdentity() const {
    if (IsMultiFrameRawProjectActive() &&
        m_Project->snapshot &&
        !m_Project->snapshot->activeSourceSetId.empty()) {
        const Stack::Project::MultiFrameSourceSet* sourceSet =
            Stack::Project::FindSourceSet(
                *m_Project->snapshot,
                m_Project->snapshot->activeSourceSetId);
        const bool hdr = sourceSet && sourceSet->operationIntent ==
            Stack::Project::MultiFrameOperationIntent::RawBurstHdr;
        return std::string(hdr ? "hdr://" : "mfd://") +
            m_Project->snapshot->projectId + "/" +
            m_Project->snapshot->activeSourceSetId;
    }
    return m_Project->rawSourceKey;
}

bool EditorModule::HasActiveRawWorkspacePresentationForValidation() const {
    return HasRawWorkspaceLivePreviewForSource(
        GetActiveRawWorkspacePreviewIdentity());
}

bool EditorModule::TryGetActiveRawWorkspacePresentationTexture(
    unsigned int& outTexture,
    int& outWidth,
    int& outHeight) const {
    outTexture = 0;
    outWidth = 0;
    outHeight = 0;
    const std::string activeIdentity = GetActiveRawWorkspacePreviewIdentity();
    if (activeIdentity.empty() ||
        m_RawWorkspacePreviewOutputKind != RawWorkspacePreviewOutputKind::SingleTexture ||
        !HasRawWorkspaceLivePreviewForSource(activeIdentity)) {
        return false;
    }
    outTexture = m_RawWorkspacePresentationTexture.texture;
    outWidth = m_RawWorkspacePresentationTexture.width;
    outHeight = m_RawWorkspacePresentationTexture.height;
    return true;
}

std::string EditorModule::GetActiveRawWorkspacePresentationDiagnosticForValidation() const {
    const std::string activeIdentity = GetActiveRawWorkspacePreviewIdentity();
    std::ostringstream diagnostic;
    diagnostic
        << "activeIdentity="
        << (activeIdentity.empty() ? "<empty>" : activeIdentity)
        << ", presentedIdentity="
        << (m_ViewportOutputRawWorkspaceSourceKey.empty()
                ? "<empty>"
                : m_ViewportOutputRawWorkspaceSourceKey)
        << ", outputKind="
        << static_cast<int>(m_RawWorkspacePreviewOutputKind)
        << ", texture=" << m_RawWorkspacePresentationTexture.texture
        << ", size=" << m_RawWorkspacePresentationTexture.width
        << 'x' << m_RawWorkspacePresentationTexture.height
        << ", tiles=" << m_ViewportOutputTiles.tiles.size()
        << ", renderDirty=" << (m_RenderDirty ? "true" : "false")
        << ", renderPending=" << (m_RenderPending ? "true" : "false")
        << ", renderRevision=" << m_RenderRevision
        << ", submittedRevision=" << m_LastSubmittedRenderRevision;
    diagnostic << ", fade=" << m_RawViewportFadeDecision << ", fadeSeconds=" << m_RawViewportFadeDuration
        << ", cadenceMs=" << m_RawViewportCadenceMs << ", chosenEdge=" << m_RawWorkspaceInteractivePreviewMaxDimension;
    if (!m_RawWorkspaceStaleRenderStatusText.empty()) {
        diagnostic << ", renderError=" << m_RawWorkspaceStaleRenderStatusText;
    }
    return diagnostic.str();
}

std::uint64_t EditorModule::GetActiveRawWorkspacePreviewSourceHash() const {
    if(m_Bracketing&&m_Bracketing->HasInteractivePreview()&&IsBracketingActive())
        return m_Bracketing->interactiveRaw->contentIdentityHash;
    if (!IsMultiFrameRawProjectActive() ||
        !m_Project->snapshot ||
        m_Project->snapshot->activeSourceSetId.empty()) {
        return 0;
    }
    const Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(
            *m_Project->snapshot,
            m_Project->snapshot->activeSourceSetId);
    if (!sourceSet) {
        return 0;
    }
    if (sourceSet->operationIntent ==
        Stack::Project::MultiFrameOperationIntent::RawBurstHdr) {
        return m_HdrAdoptedRawResult &&
                m_HdrAdoptedRawResult->projectId ==
                    m_Project->snapshot->projectId &&
                m_HdrAdoptedRawResult->sourceSetId == sourceSet->sourceSetId &&
                (m_HdrAdoptedRawResult->inputRevision ==
                    m_Project->snapshot->hdrInputRevision || sourceSet->settings.contains("bracketing"))
            ? m_HdrAdoptedRawResult->contentHash
            : 0;
    }
    return m_MfdAdoptedRawResult &&
            m_MfdAdoptedRawResult->projectId ==
                m_Project->snapshot->projectId &&
            m_MfdAdoptedRawResult->sourceSetId == sourceSet->sourceSetId &&
            m_MfdAdoptedRawResult->inputRevision ==
                m_Project->snapshot->mfdInputRevision
        ? m_MfdAdoptedRawResult->contentHash
        : 0;
}

bool EditorModule::IsRawWorkspaceFastPreviewRenderActive(double now) const {
    const std::string currentIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    return m_RawWorkspaceFullResolutionPreviewPending &&
        !m_RawWorkspacePreviewSourceKey.empty() &&
        m_RawWorkspacePreviewSourceKey == currentIdentity &&
        m_RawWorkspaceFastPreviewUntilTime > 0.0 &&
        now < m_RawWorkspaceFastPreviewUntilTime;
}

void EditorModule::RefreshRawWorkspaceGpuMemoryBudget() {
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kBudgetRefreshInterval = std::chrono::seconds(2);
    if (m_RawWorkspaceVramWorkingBudgetBytes > 0u &&
        m_RawWorkspaceGpuBudgetLastRefresh.time_since_epoch().count() != 0 &&
        now - m_RawWorkspaceGpuBudgetLastRefresh < kBudgetRefreshInterval) {
        return;
    }
    if (m_RawWorkspaceGlMaxTextureSize <= 0) {
        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        m_RawWorkspaceGlMaxTextureSize = std::max(768, maxTextureSize);
    }
    const Raw::RawGpuMemoryBudgetDecision memoryDecision =
        Raw::ResolveRawGpuMemoryBudget(
            Stack::Renderer::QueryGpuMemoryBudget());
    m_RawWorkspaceVramWorkingBudgetBytes =
        memoryDecision.workingBudgetBytes;
    m_RawWorkspaceVramAvailableBytes =
        memoryDecision.availableBytes;
    m_RawWorkspaceMinimumMemoryTiling =
        memoryDecision.forceMinimumMemoryTiling;
    m_GraphPerformanceStats.rawVramWorkingBudgetBytes =
        memoryDecision.workingBudgetBytes;
    m_GraphPerformanceStats.rawVramAvailableBytes =
        memoryDecision.availableBytes;
    m_GraphPerformanceStats.rawMinimumMemoryTiling =
        memoryDecision.forceMinimumMemoryTiling;
    m_RawWorkspaceGpuBudgetLastRefresh = now;
}

void EditorModule::UpdateRawWorkspaceInteractivePreviewDimension(
    const ImVec2& imageBounds) {
    constexpr double kInteractiveBytesPerPixel = 16.0;
    constexpr double kProtectedWorkingSurfaceCount = 7.0;
    const auto* viewport=ImGui::GetWindowViewport();
    const int refresh=DisplayRefreshRate::ForWindow(viewport ? static_cast<GLFWwindow*>(viewport->PlatformHandle) : nullptr);
    m_RawViewportDisplayRefreshRate=refresh>0 ? refresh : 60;
    SyncRawViewportPreferences();
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const float previewPixelScale = std::max(
        1.0f,
        std::max(framebufferScale.x, framebufferScale.y));
    m_RawViewportPhysicalWidth=std::max(1,int(std::ceil(imageBounds.x*previewPixelScale)));
    m_RawViewportPhysicalHeight=std::max(1,int(std::ceil(imageBounds.y*previewPixelScale)));
    const float physicalLongestSide =
        std::max(imageBounds.x, imageBounds.y) * previewPixelScale;
    const float previewScale = (IsRawWorkspaceUiInteractionActive() ||
        m_RawWorkspaceFullResolutionPreviewPending)
        ? Stack::EditorRenderScheduling::
              ClampRawInteractivePreviewScale(
                  m_RawWorkspaceAdaptivePreviewScale,
                  static_cast<float>(m_RawWorkspaceInteractivePreviewMaximumEdge) /
                      std::max(1, m_RawWorkspacePhysicalViewportMaxDimension))
        : 1.0f;
    RefreshRawWorkspaceGpuMemoryBudget();
    const int memoryLimitedEdge = static_cast<int>(std::floor(std::sqrt(
        static_cast<double>(m_RawWorkspaceVramWorkingBudgetBytes) /
        (kInteractiveBytesPerPixel * kProtectedWorkingSurfaceCount))));
    const int maximumEdge = m_RawWorkspaceMinimumMemoryTiling
        ? std::min(m_RawWorkspaceGlMaxTextureSize, 1024)
        : std::max(
              768,
              std::min(m_RawWorkspaceGlMaxTextureSize, memoryLimitedEdge));
    m_RawWorkspacePhysicalViewportMaxDimension = std::clamp(
        static_cast<int>(std::ceil(physicalLongestSide / 64.0f)) * 64,
        Raw::kMinimumInteractiveViewportEdge,
        maximumEdge);
    const int nativeEdge = std::max(m_RawRenderSessionFullFrameWidth,
        m_RawRenderSessionFullFrameHeight);
    m_RawWorkspaceInteractivePreviewMaximumEdge = std::max(Raw::kMinimumInteractiveViewportEdge,
        std::min(maximumEdge, nativeEdge > 0 ? nativeEdge : maximumEdge));
    m_RawWorkspaceInteractivePreviewMaxDimension = std::clamp(
        static_cast<int>(std::ceil(
            m_RawWorkspacePhysicalViewportMaxDimension * previewScale / 64.0f)) * 64,
        Raw::kMinimumInteractiveViewportEdge,
        m_RawWorkspaceInteractivePreviewMaximumEdge);
}

void EditorModule::UpdateRawWorkspaceSettledPreviewRender(double now) {
    if (IsRawViewportTraceEnabled()) {
        std::ostringstream trace;
        trace << "rootTab="
              << (m_RawWorkspaceRootTabActive ? "true" : "false")
              << ", interaction="
              << (IsRawWorkspaceUiInteractionActive() ? "true" : "false")
              << ", draft="
              << (m_Project->rawInteractionDraft.active ? "true" : "false")
              << ", localRangeDrag="
              << (m_RawWorkspaceLocalRangeTargetDragging ? "true" : "false")
              << ", previewPan="
              << (m_RawWorkspaceLabUi.previewPanning ? "true" : "false")
              << ", colorWarpInteraction="
              << (m_RawWorkspaceLabUi.colorWarpInteractionActive
                      ? "true"
                      : "false")
              << ", sessionNative="
              << m_RawRenderSessionFullFrameWidth << 'x'
              << m_RawRenderSessionFullFrameHeight
              << ", lastPurpose="
              << (m_GraphPerformanceStats.lastRawRenderPurpose.empty()
                      ? "<none>"
                      : m_GraphPerformanceStats.lastRawRenderPurpose)
              << ", queueMs="
              << m_GraphPerformanceStats.lastRawQueueWaitMs
              << ", workerMs="
              << m_GraphPerformanceStats.lastRawWorkerTotalMs
              << ", mainRenderMs="
              << m_GraphPerformanceStats.lastMainRenderMs
              << ", uiAdoptionMs="
              << m_GraphPerformanceStats.lastRawUiAdoptionMs
              << ", rawStageHits="
              << m_GraphPerformanceStats.lastMainGraphStats.rawStageCacheHits
              << ", rawStageMisses="
              << m_GraphPerformanceStats.lastMainGraphStats.rawStageCacheMisses
              << ", preprocessDispatches="
              << m_GraphPerformanceStats.lastMainGraphStats
                     .rawGpuPreprocessDispatches
              << ", preprocessCacheHits="
              << m_GraphPerformanceStats.lastMainGraphStats
                     .rawPreprocessCacheHits
              << ", sensorUploadMs="
              << m_GraphPerformanceStats.lastMainGraphStats.rawSensorUploadMs
              << ", transientEvictions="
              << m_GraphPerformanceStats.lastMainGraphStats
                     .transientTargetEvictions
              << ", persistentEvictions="
              << m_GraphPerformanceStats.lastMainGraphStats
                     .persistentCacheEvictions
              << ", " << GetFullQualityRenderDiagnostic();
        TraceRawViewportState(trace.str());
    }
    if (!m_RawWorkspaceFullResolutionPreviewPending &&
        !m_RawWorkspaceAnalysisPending) {
        return;
    }
    const std::string currentIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    if (m_RawWorkspacePreviewSourceKey.empty() ||
        m_RawWorkspacePreviewSourceKey != currentIdentity) {
        m_RawWorkspaceFullResolutionPreviewPending = false;
        m_RawWorkspaceFullResolutionPreviewRequested = false;
        m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
        m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
        m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
        m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
        m_RawWorkspaceAnalysisPending = false;
        m_RawWorkspaceAnalysisRequested = false;
        m_RawWorkspaceAnalysisQuietUntilTime = -1.0;
        m_RawWorkspaceExplicitFullQualityRenderRequested = false;
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
        return;
    }
    const bool interactiveGestureHeld =
        IsRawWorkspaceUiInteractionActive();
    if (interactiveGestureHeld) {
        m_RawWorkspaceAnalysisQuietUntilTime =
            now + kRawWorkspaceAnalysisQuietSeconds;
        return;
    }
    // Viewport handles can end their document gesture outside the tool panel.
    // Finish the preview gesture too, once, before requesting settled work.
    if (m_RawWorkspaceAdaptiveGestureActive) {
        NoteRawWorkspaceRecipePreviewEdit(false);
        MarkRenderRefreshDirty();
    }
    if (m_RawWorkspaceFullResolutionPreviewDeferredByBudget) {
        RefreshRawWorkspaceGpuMemoryBudget();
        if (Stack::EditorRenderScheduling::
                ShouldResumeRawNativeRefinementAfterBudgetChange(
                    m_RawWorkspaceFullResolutionPreviewDeferredByBudget,
                    m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes,
                    m_RawWorkspaceVramWorkingBudgetBytes,
                    m_RawWorkspaceMinimumMemoryTiling)) {
            m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
            m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
        }
    }
    if (Stack::EditorRenderScheduling::
            ShouldRearmUnsubmittedRawNativeRefinement(
                m_RawWorkspaceFullResolutionPreviewPending,
                m_RawWorkspaceFullResolutionPreviewRequested,
                m_RawWorkspaceFullResolutionPreviewRequestGeneration,
                m_RenderDirty,
                m_RenderPending,
                IsAnyRenderBackendBusy())) {
        m_RawWorkspaceFullResolutionPreviewRequested = false;
    }
    if (HasRawWorkspaceFullResolutionPreviewForSource(currentIdentity)) {
        m_RawWorkspaceFullResolutionPreviewPending = false;
        m_RawWorkspaceFullResolutionPreviewRequested = false;
        m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
        m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
        m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
        m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
        if (m_RawWorkspaceExplicitFullQualityRenderRequested) {
            m_RawWorkspaceExplicitFullQualityRenderRequested = false;
        }
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
    }
    if (Stack::EditorRenderScheduling::ShouldStartRawNativeRefinement(
            m_RawWorkspaceFullResolutionPreviewPending &&
                !m_RawWorkspaceFullResolutionPreviewDeferredByBudget,
            m_RawWorkspaceFullResolutionPreviewRequested,
            HasRawWorkspaceLivePreviewForSource(currentIdentity),
            interactiveGestureHeld,
            m_RawWorkspaceFastPreviewUntilTime <= 0.0 ||
                now >= m_RawWorkspaceFastPreviewUntilTime,
            !IsAnyRenderBackendBusy())) {
        m_RawWorkspaceFullResolutionPreviewRequested = true;
        m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
        MarkRenderRefreshDirty();
        return;
    }    if (Stack::EditorRenderScheduling::ShouldStartRawAnalysisAfterPreviewSettles(
            m_RawWorkspaceAnalysisPending &&
                (!m_RawWorkspaceFullResolutionPreviewPending || m_RawWorkspaceFullResolutionPreviewDeferredByBudget),
            m_RawWorkspaceAnalysisRequested,
            HasRawWorkspaceLivePreviewForSource(currentIdentity),
            interactiveGestureHeld,
            m_RawWorkspaceAnalysisQuietUntilTime <= 0.0 ||
                now >= m_RawWorkspaceAnalysisQuietUntilTime)) {
        m_RawWorkspaceAnalysisRequested = true;
        m_RawWorkspaceAnalysisQuietUntilTime = -1.0;
        MarkRenderRefreshDirty();
        return;
    }

}

bool EditorModule::HasRawWorkspaceLivePreviewForSource(const std::string& sourceKey) const {
    if (sourceKey.empty() || m_ViewportOutputRawWorkspaceSourceKey != sourceKey) {
        return false;
    }
    if (m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::Tiled) {
        return HasViewportOutputTiles();
    }
    if (m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::SingleTexture) {
        return IsViewportTextureSafeForDrawing(
                m_RawWorkspacePresentationTexture.texture) &&
            m_RawWorkspacePresentationTexture.width > 0 &&
            m_RawWorkspacePresentationTexture.height > 0;
    }
    return false;
}

bool EditorModule::HasRawWorkspaceCurrentPresentationForSource(const std::string& sourceKey) const {
    const std::string currentIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    if (IsMultiFrameRawProjectActive() &&
        m_Project->snapshot &&
        sourceKey == currentIdentity) {
        const Stack::Project::MultiFrameSourceSet* activeSet =
            Stack::Project::FindSourceSet(
                *m_Project->snapshot,
                m_Project->snapshot->activeSourceSetId);
        const bool activeHdr = activeSet && activeSet->operationIntent ==
            Stack::Project::MultiFrameOperationIntent::RawBurstHdr;
        const bool adoptedCurrent = activeHdr
            ? (m_HdrAdoptedRawResult &&
               m_HdrAdoptedRawResult->rawData &&
               m_HdrAdoptedRawResult->projectId ==
                   m_Project->snapshot->projectId &&
               m_HdrAdoptedRawResult->sourceSetId ==
                   m_Project->snapshot->activeSourceSetId &&
               m_HdrAdoptedRawResult->inputRevision ==
                   m_Project->snapshot->hdrInputRevision)
            : (m_MfdAdoptedRawResult &&
               m_MfdAdoptedRawResult->rawData &&
               m_MfdAdoptedRawResult->projectId ==
                   m_Project->snapshot->projectId &&
               m_MfdAdoptedRawResult->sourceSetId ==
                   m_Project->snapshot->activeSourceSetId &&
               m_MfdAdoptedRawResult->inputRevision ==
                   m_Project->snapshot->mfdInputRevision);
        if (!adoptedCurrent) return false;
    }
    return HasRawWorkspaceLivePreviewForSource(sourceKey) &&
        m_ViewportOutputRenderGeneration != 0 &&
        m_ViewportOutputRenderGeneration == m_LatestRawPresentationGeneration && !m_RenderDirty;
}

bool EditorModule::HasRawWorkspaceFullResolutionPreviewForSource(const std::string& sourceKey) const {
    if (!HasRawWorkspaceCurrentPresentationForSource(sourceKey)) return false;
    if (m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::Tiled ||
        !m_RawViewportRequest.visible.Valid()) {
        return m_ViewportOutputNativeExtentVerified && m_ViewportOutputPreviewMaxDimension == 0;
    }
    const auto& texture = m_RawWorkspacePresentationTexture;
    const auto& visible = m_RawViewportRequest.visible;
    if (Raw::ViewportDetailCoversNativeRegion(m_RawViewportPresentedRegion, texture.width, texture.height, visible)) return true;
    // The content fingerprint is independent of view and resolution, but includes
    // the RAW recipe and layer edits. A retained frame may cover more than the
    // currently accepted crop without belonging to an older edit.
    return !m_RawViewportLastPresentation.diagnostic && m_RawViewportDetailContent != 0 &&
        m_RawViewportDetailContent == m_RawViewportPresentationContent &&
        IsViewportTextureSafeForDrawing(m_RawViewportDetailTexture.texture) &&
        Raw::ViewportDetailCoversNativeRegion(m_RawViewportDetailRegion,
            m_RawViewportDetailTexture.width, m_RawViewportDetailTexture.height, visible);
}

void EditorModule::ClearRawWorkspaceLocalRangeTargetState(bool keepMode) {
    const bool targetMode = m_RawWorkspaceLocalRangeTargetMode;
    m_RawWorkspaceLocalRangeTargetMode = keepMode ? targetMode : false;
    m_RawWorkspaceLocalRangeTargetDragging = false;
    m_RawWorkspaceLocalRangeTargetSamplePending = false;
    m_RawWorkspaceLocalRangeTargetSampleValid = false;
    m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
    m_RawWorkspaceLocalRangeTargetSourceKey.clear();
    m_RawWorkspaceLocalRangeTargetU = 0.0f;
    m_RawWorkspaceLocalRangeTargetV = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneEv = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneLuma = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneR = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneG = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneB = 0.0f;
    m_RawWorkspaceLocalRangeTargetStartMouseY = 0.0f;
    m_RawWorkspaceLocalRangeTargetStartDeltaEv = 0.0f;
    m_RawWorkspaceLocalRangeTargetDragOffsetEv = 0.0f;
    m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
    m_RawWorkspaceLocalRangeTargetPointIndex = -1;
    m_RawWorkspaceLocalRangeTargetZoneId.clear();
    m_RawWorkspaceLocalRangeTargetCreateZone = false;
    m_RawWorkspaceLocalRangeTargetTransientZone = false;
    m_RawWorkspaceLocalRangeTargetHoverSample = false;
    m_RawWorkspaceLocalRangeTargetContextMenuRequested = false;
    m_RawWorkspaceLocalRangeTargetInteractionState =
        Stack::RawLocalRangeTargetInteraction::State::Hover;
    m_RawWorkspaceLocalRangeTargetCreateIntent = false;
    m_RawWorkspaceLocalRangeTargetEditStarted = false;
    m_RawWorkspaceLocalRangeTargetAuthoredZoneHitBits = 0;
    m_RawWorkspaceLocalRangeTargetStrongestAuthoredZoneWeight = 0.0f;
    m_RawWorkspaceLocalRangeTargetHoverZoneIndex = -1;
    m_RawWorkspaceLocalRangeTargetHoverZoneName.clear();
    m_RawWorkspaceLocalRangeTargetPreview = {};
    ++m_RawWorkspaceLocalRangeTargetPreviewGeneration;
    m_RawWorkspaceLocalRangeTargetLastPointerMotionTime = -1.0;
    m_RawWorkspaceLocalRangeTargetPreviewRefined = false;
    m_RawWorkspaceLocalRangeTargetPreviewRefinementPending = false;
    m_RawWorkspaceLocalRangeTargetLastRefinementPollTime = -1.0;
    m_RawWorkspaceLocalRangeTargetLastHoverRequestTime = -1.0;
    m_RawWorkspaceLocalRangeTargetLastHoverMouse = ImVec2(-10000.0f, -10000.0f);
}

bool EditorModule::HasRawWorkspaceLocalRangeTargetSampleForSource(const std::string& sourceKey) const {
    return !sourceKey.empty() &&
        m_RawWorkspaceLocalRangeTargetSampleValid &&
        m_RawWorkspaceLocalRangeTargetSourceKey == sourceKey &&
        std::isfinite(m_RawWorkspaceLocalRangeTargetSceneEv);
}

void EditorModule::AdoptRawWorkspaceLocalRangeTargetSampleFromResult(
    const EditorRenderWorker::Result& result) {
    if (!m_RawWorkspaceLocalRangeTargetSamplePending) {
        return;
    }
    if (result.rawWorkspace.sourceKey.empty() ||
        result.rawWorkspace.sourceKey != m_RawWorkspaceLocalRangeTargetSourceKey ||
        result.rawWorkspace.sourceKey !=
            GetActiveRawWorkspacePreviewIdentity()) {
        return;
    }

    const bool sameSamplePoint =
        std::abs(result.rawWorkspace.localRangeTargetSample.u - m_RawWorkspaceLocalRangeTargetU) < 0.001f &&
        std::abs(result.rawWorkspace.localRangeTargetSample.v - m_RawWorkspaceLocalRangeTargetV) < 0.001f;
    m_RawWorkspaceLocalRangeTargetSamplePending = false;
    if (!result.rawWorkspace.localRangeTargetSample.valid || !sameSamplePoint) {
        m_RawWorkspaceLocalRangeTargetSampleValid = false;
        m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
        return;
    }

    m_RawWorkspaceLocalRangeTargetSampleValid = true;
    m_RawWorkspaceLocalRangeTargetSceneEv = result.rawWorkspace.localRangeTargetSample.sceneEv;
    m_RawWorkspaceLocalRangeTargetSceneLuma = result.rawWorkspace.localRangeTargetSample.sceneLuma;
    m_RawWorkspaceLocalRangeTargetSceneR = result.rawWorkspace.localRangeTargetSample.sceneR;
    m_RawWorkspaceLocalRangeTargetSceneG = result.rawWorkspace.localRangeTargetSample.sceneG;
    m_RawWorkspaceLocalRangeTargetSceneB = result.rawWorkspace.localRangeTargetSample.sceneB;
    m_RawWorkspaceLocalRangeTargetU = result.rawWorkspace.localRangeTargetSample.u;
    m_RawWorkspaceLocalRangeTargetV = result.rawWorkspace.localRangeTargetSample.v;
    m_RawWorkspaceLocalRangeTargetAuthoredZoneHitBits =
        result.rawWorkspace.localRangeTargetSample.authoredZoneHitBits;
    m_RawWorkspaceLocalRangeTargetStrongestAuthoredZoneWeight =
        result.rawWorkspace.localRangeTargetSample.strongestAuthoredZoneWeight;

    const Stack::RawRecipe::RawLocalRangeRecipe currentRange =
        BuildLocalRangeUiRecipe(m_Project->rawRecipe.localRange);
    const Stack::RawRecipe::RawLocalRangeTargetZone* matchedZone = nullptr;
    int matchedZoneIndex = -1;
    std::vector<Stack::RawLocalRangeTargetInteraction::Candidate> candidates;
    candidates.reserve(currentRange.targetZones.size());
    for (std::size_t zoneIndex = 0;
         zoneIndex < currentRange.targetZones.size() && zoneIndex < 32u;
         ++zoneIndex) {
        const Stack::RawRecipe::RawLocalRangeTargetZone& zone =
            currentRange.targetZones[zoneIndex];
        if (!zone.enabled) {
            continue;
        }
        const std::uint32_t zoneBit = std::uint32_t(1u) << zoneIndex;
        if ((m_RawWorkspaceLocalRangeTargetAuthoredZoneHitBits & zoneBit) == 0u) {
            continue;
        }
        const float tonalWeight =
            Stack::RawRecipe::EvaluateLocalRangeTargetZoneTonalWeight(
                zone,
                m_RawWorkspaceLocalRangeTargetSceneEv);
        const float colorWeight =
            Stack::RawRecipe::EvaluateLocalRangeTargetZoneColorWeight(
                zone,
                m_RawWorkspaceLocalRangeTargetSceneR,
                m_RawWorkspaceLocalRangeTargetSceneG,
                m_RawWorkspaceLocalRangeTargetSceneB,
                m_Project->rawRecipe.technical.workingSpace);
        candidates.push_back({
            zone.id,
            static_cast<int>(zoneIndex),
            std::max(
                Stack::RawLocalRangeTargetInteraction::kCandidateMinimumWeight,
                tonalWeight * colorWeight),
            zone.id == m_RawWorkspaceLocalRangeTargetZoneId
        });
    }
    const int candidateIndex =
        Stack::RawLocalRangeTargetInteraction::ChooseCandidate(candidates);
    if (candidateIndex >= 0) {
        matchedZoneIndex =
            candidates[static_cast<std::size_t>(candidateIndex)].zoneIndex;
        if (matchedZoneIndex >= 0 &&
            matchedZoneIndex < static_cast<int>(currentRange.targetZones.size())) {
            matchedZone =
                &currentRange.targetZones[static_cast<std::size_t>(matchedZoneIndex)];
        }
    }

    m_RawWorkspaceLocalRangeTargetHoverZoneIndex = matchedZoneIndex;
    m_RawWorkspaceLocalRangeTargetHoverZoneName =
        matchedZone != nullptr ? matchedZone->name : std::string();
    m_RawWorkspaceLocalRangeTargetPreview.enabled =
        m_RawWorkspaceLocalRangeTargetMode;
    m_RawWorkspaceLocalRangeTargetPreview.generation =
        ++m_RawWorkspaceLocalRangeTargetPreviewGeneration;
    m_RawWorkspaceLocalRangeTargetPreviewRefined = false;
    m_RawWorkspaceLocalRangeTargetPreviewRefinementPending = false;
    m_RawWorkspaceLocalRangeTargetPreview.sourceU =
        m_RawWorkspaceLocalRangeTargetU;
    m_RawWorkspaceLocalRangeTargetPreview.sourceV =
        m_RawWorkspaceLocalRangeTargetV;
    m_RawWorkspaceLocalRangeTargetPreview.existingZoneIndex =
        matchedZoneIndex;
    m_RawWorkspaceLocalRangeTargetPreview.provisional = true;
    m_RawWorkspaceLocalRangeTargetPreview.requestConnectedRefinement = false;
    if (matchedZone != nullptr) {
        m_RawWorkspaceLocalRangeTargetPreview.prospectiveZone = *matchedZone;
    } else {
        Stack::RawRecipe::RawLocalRangeTargetZone prospectiveZone;
        prospectiveZone.id = "__target-preview__";
        prospectiveZone.name = "Prospective Zone";
        prospectiveZone.centerEv = std::clamp(
            m_RawWorkspaceLocalRangeTargetSceneEv,
            currentRange.minEv + 0.001f,
            currentRange.maxEv - 0.001f);
        prospectiveZone.scope =
            Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
        prospectiveZone.seeds.push_back({
            m_RawWorkspaceLocalRangeTargetU,
            m_RawWorkspaceLocalRangeTargetV
        });
        m_RawWorkspaceLocalRangeTargetPreview.prospectiveZone =
            std::move(prospectiveZone);
    }
    if (m_RawWorkspaceLocalRangeTargetHoverSample) {
        m_RawWorkspaceLocalRangeTargetHoverSample = false;
        MarkRenderRefreshDirty();
        return;
    }

    if (m_RawWorkspaceLocalRangeTargetCreateIntent) {
        m_RawWorkspaceLocalRangeTargetZoneId.clear();
        m_RawWorkspaceLocalRangeTargetTransientZone = true;
    } else if (m_RawWorkspaceLocalRangeTargetCreateZone) {
        if (matchedZone != nullptr) {
            m_RawWorkspaceLocalRangeTargetZoneId = matchedZone->id;
            m_RawWorkspaceLocalRangeTargetTransientZone = false;
        } else {
            m_RawWorkspaceLocalRangeTargetZoneId.clear();
            m_RawWorkspaceLocalRangeTargetTransientZone = true;
        }
    } else if (matchedZone != nullptr) {
        m_RawWorkspaceLocalRangeTargetZoneId = matchedZone->id;
        m_RawWorkspaceLocalRangeTargetTransientZone = false;
    } else {
        m_RawWorkspaceLocalRangeTargetZoneId.clear();
        m_RawWorkspaceLocalRangeTargetTransientZone = false;
    }
    m_RawWorkspaceLocalRangeTargetCreateZone = false;
    m_RawWorkspaceLocalRangeTargetStartDeltaEv = std::clamp(
        !m_RawWorkspaceLocalRangeTargetCreateIntent && matchedZone != nullptr
            ? matchedZone->deltaEv
            : 0.0f,
        kRawLocalRangeMinDeltaEv,
        kRawLocalRangeMaxDeltaEv);
    const bool sampledInteractionCanEdit =
        m_RawWorkspaceLocalRangeTargetCreateIntent || matchedZone != nullptr;
    if (sampledInteractionCanEdit) {
        m_RawWorkspaceLocalRangeTargetDeltaEv = std::clamp(
            m_RawWorkspaceLocalRangeTargetStartDeltaEv +
                m_RawWorkspaceLocalRangeTargetDragOffsetEv,
            kRawLocalRangeMinDeltaEv,
            kRawLocalRangeMaxDeltaEv);
    } else {
        // A normal drag outside every authored mask is deliberately a no-op.
        // Do not leave its physical mouse distance in the HUD as though an EV
        // edit had been accepted.
        m_RawWorkspaceLocalRangeTargetDragOffsetEv = 0.0f;
        m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
    }

    if (m_RawWorkspaceLocalRangeTargetApplyWhenSampled &&
        sampledInteractionCanEdit &&
        std::abs(m_RawWorkspaceLocalRangeTargetDragOffsetEv) > 0.005f) {
        if (ApplyRawWorkspaceLocalRangeTargetDelta(
                m_RawWorkspaceLocalRangeTargetDragging)) {
            m_RawWorkspaceLocalRangeTargetEditStarted = true;
        }
    }
    m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
    if (!m_RawWorkspaceLocalRangeTargetDragging) {
        m_RawWorkspaceLocalRangeTargetCreateIntent = false;
    }
}

bool EditorModule::ApplyRawWorkspaceLocalRangeTargetDelta(bool interactionActive) {
    if (!HasRawWorkspaceLocalRangeTargetSampleForSource(m_Project->rawSourceKey)) {
        return false;
    }
    if (m_Project->rawSourceKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe editedRecipe = m_Project->rawRecipe;
    Stack::RawRecipe::RawLocalRangeRecipe localRange =
        BuildLocalRangeUiRecipe(editedRecipe.localRange);
    const float targetEv = std::clamp(
        m_RawWorkspaceLocalRangeTargetSceneEv,
        localRange.minEv + 0.001f,
        localRange.maxEv - 0.001f);
    const float targetDeltaEv = std::clamp(
        m_RawWorkspaceLocalRangeTargetDeltaEv,
        kRawLocalRangeMinDeltaEv,
        kRawLocalRangeMaxDeltaEv);
    auto zoneIt = std::find_if(
        localRange.targetZones.begin(),
        localRange.targetZones.end(),
        [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
            return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
        });
    if (zoneIt == localRange.targetZones.end()) {
        if (localRange.targetZones.size() >=
            Stack::RawRecipe::kMaxRawLocalRangeTargetZones) {
            return false;
        }
        Stack::RawRecipe::RawLocalRangeTargetZone zone;
        std::ostringstream id;
        id << "zone-" << std::hex << m_RenderGeneration << "-"
           << ++m_RawWorkspaceLocalRangeTargetZoneSerial;
        zone.id = id.str();
        zone.name = "Zone " + std::to_string(localRange.targetZones.size() + 1);
        zone.centerEv = targetEv;
        zone.deltaEv = targetDeltaEv;
        zone.scope = Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
        zone.seeds.push_back({
            std::clamp(m_RawWorkspaceLocalRangeTargetU, 0.0f, 1.0f),
            std::clamp(m_RawWorkspaceLocalRangeTargetV, 0.0f, 1.0f)
        });
        const std::array<float, 3> uvChroma =
            Stack::RawRecipe::SceneLinearRgbToUvChroma(
                m_RawWorkspaceLocalRangeTargetSceneR,
                m_RawWorkspaceLocalRangeTargetSceneG,
                m_RawWorkspaceLocalRangeTargetSceneB,
                m_Project->rawRecipe.technical.workingSpace);
        zone.targetUPrime = uvChroma[0];
        zone.targetVPrime = uvChroma[1];
        zone.targetChroma = uvChroma[2];
        localRange.targetZones.push_back(std::move(zone));
        zoneIt = std::prev(localRange.targetZones.end());
        m_RawWorkspaceLocalRangeTargetZoneId = zoneIt->id;
    } else {
        zoneIt->deltaEv = targetDeltaEv;
    }
    localRange.enabled = true;
    editedRecipe.localRange = BuildLocalRangeUiRecipe(localRange);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(editedRecipe, interactionActive)) {
        return false;
    }

    m_RawWorkspaceLocalRangeTargetPreview.interactionEditing = true;
    m_RawWorkspaceLocalRangeTargetTransientZone = false;
    return true;
}

void EditorModule::HandleRawWorkspaceLocalRangeTargetInteraction(
    const Stack::RawWorkspace::SourceRecord& selectedSource,
    const ImVec2& imageMin,
    const ImVec2& imageMax,
    bool selectedProjectActive,
    bool currentRawPreview) {
    if (!m_RawWorkspaceLocalRangeTargetMode ||
        !selectedProjectActive ||
        !currentRawPreview ||
        imageMax.x <= imageMin.x ||
        imageMax.y <= imageMin.y) {
        if (m_RawWorkspaceLocalRangeTargetDragging &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_RawWorkspaceLocalRangeTargetDragging = false;
        }
        return;
    }

    const ImRect imageRect(imageMin, imageMax);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered =
        imageRect.Contains(mouse) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (hovered || m_RawWorkspaceLocalRangeTargetDragging) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
    if (hovered) {
        m_RawWorkspaceLocalRangeTargetPreview.hitRadiusU =
            12.0f / std::max(1.0f, imageRect.GetWidth());
        m_RawWorkspaceLocalRangeTargetPreview.hitRadiusV =
            12.0f / std::max(1.0f, imageRect.GetHeight());
    } else if (!m_RawWorkspaceLocalRangeTargetDragging &&
        m_RawWorkspaceLocalRangeTargetPreview.enabled) {
        m_RawWorkspaceLocalRangeTargetPreview = {};
        m_RawWorkspaceLocalRangeTargetHoverZoneIndex = -1;
        m_RawWorkspaceLocalRangeTargetHoverZoneName.clear();
        ClearRawWorkspaceLocalRangeOverlayState();
    }

    auto editActiveZone = [&](const std::function<bool(
                                  Stack::RawRecipe::RawLocalRangeRecipe&,
                                  Stack::RawRecipe::RawLocalRangeTargetZone&)>& edit,
                              bool interactionActive) {
        if (m_RawWorkspaceLocalRangeTargetZoneId.empty()) {
            return false;
        }
        Stack::RawRecipe::RawDevelopmentRecipe editedRecipe =
            m_Project->rawRecipe;
        Stack::RawRecipe::RawLocalRangeRecipe localRange =
            BuildLocalRangeUiRecipe(editedRecipe.localRange);
        const auto zoneIt = std::find_if(
            localRange.targetZones.begin(),
            localRange.targetZones.end(),
            [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
            });
        if (zoneIt == localRange.targetZones.end() ||
            !edit(localRange, *zoneIt)) {
            return false;
        }
        localRange.enabled = true;
        editedRecipe.localRange = BuildLocalRangeUiRecipe(localRange);
        const bool changed = ApplyRawWorkspaceRecipeEditForSelectedSource(
            editedRecipe,
            interactionActive);
        if (changed) {
            m_RawWorkspaceLocalRangeTargetPreview.interactionEditing = true;
            m_RawWorkspaceLocalRangeTargetTransientZone = false;
        }
        return changed;
    };

    const bool shiftHeld = ImGui::GetIO().KeyShift;
    const bool ctrlHeld = ImGui::GetIO().KeyCtrl;
    if (hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        shiftHeld &&
        !m_RawWorkspaceLocalRangeTargetZoneId.empty()) {
        editActiveZone(
            [&](Stack::RawRecipe::RawLocalRangeRecipe&,
                Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                const bool alreadyPresent = std::any_of(
                    zone.seeds.begin(),
                    zone.seeds.end(),
                    [&](const Stack::RawRecipe::RawLocalRangeTargetSeed& seed) {
                        const float du = seed.sourceU -
                            std::clamp(
                                (mouse.x - imageRect.Min.x) /
                                    std::max(1.0f, imageRect.GetWidth()),
                                0.0f,
                                1.0f);
                        const float dv = seed.sourceV -
                            std::clamp(
                                (mouse.y - imageRect.Min.y) /
                                    std::max(1.0f, imageRect.GetHeight()),
                                0.0f,
                                1.0f);
                        return du * du + dv * dv < 0.000025f;
                    });
                if (alreadyPresent ||
                    zone.seeds.size() >=
                        Stack::RawRecipe::kMaxRawLocalRangeTargetSeeds) {
                    return false;
                }
                zone.seeds.push_back({
                    std::clamp(
                        (mouse.x - imageRect.Min.x) /
                            std::max(1.0f, imageRect.GetWidth()),
                        0.0f,
                        1.0f),
                    std::clamp(
                        (mouse.y - imageRect.Min.y) /
                            std::max(1.0f, imageRect.GetHeight()),
                        0.0f,
                        1.0f)
                });
                return true;
            },
            false);
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const bool hasAnyZones = std::any_of(
            m_Project->rawRecipe.localRange.targetZones.begin(),
            m_Project->rawRecipe.localRange.targetZones.end(),
            [](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                return zone.enabled;
            });
        m_RawWorkspaceLocalRangeTargetDragging = true;
        m_RawWorkspaceLocalRangeTargetInteractionState =
            Stack::RawLocalRangeTargetInteraction::State::Armed;
        m_RawWorkspaceLocalRangeTargetCreateIntent =
            Stack::RawLocalRangeTargetInteraction::ShouldCreateZone(
                hasAnyZones,
                ctrlHeld) ||
            m_RawWorkspaceLocalRangeTargetCreateZone;
        m_RawWorkspaceLocalRangeTargetEditStarted = false;
        m_RawWorkspaceLocalRangeTargetSamplePending = true;
        m_RawWorkspaceLocalRangeTargetSampleValid = false;
        m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
        m_RawWorkspaceLocalRangeTargetHoverSample = false;
        m_RawWorkspaceLocalRangeTargetCreateZone = false;
        m_RawWorkspaceLocalRangeTargetTransientZone = false;
        m_RawWorkspaceLocalRangeTargetSourceKey = selectedSource.relativePathKey;
        m_RawWorkspaceLocalRangeTargetU =
            std::clamp((mouse.x - imageRect.Min.x) / std::max(1.0f, imageRect.GetWidth()), 0.0f, 1.0f);
        m_RawWorkspaceLocalRangeTargetV =
            std::clamp((mouse.y - imageRect.Min.y) / std::max(1.0f, imageRect.GetHeight()), 0.0f, 1.0f);
        m_RawWorkspaceLocalRangeTargetSceneEv = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneLuma = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneR = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneG = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneB = 0.0f;
        m_RawWorkspaceLocalRangeTargetStartMouseY = mouse.y;
        m_RawWorkspaceLocalRangeTargetStartDeltaEv = 0.0f;
        m_RawWorkspaceLocalRangeTargetDragOffsetEv = 0.0f;
        m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
        m_RawWorkspaceLocalRangeTargetPointIndex = -1;
        m_RawWorkspaceLocalRangeTargetLastPointerMotionTime = ImGui::GetTime();
        // The settled outline is useful for choosing an area, but it must not
        // cover the pixels while exposure is being adjusted. Pointer/cursor
        // feedback remains visible; a fresh connected outline is requested
        // after release and dwell.
        m_RawWorkspaceLocalRangeTargetPreview.provisional = true;
        m_RawWorkspaceLocalRangeTargetPreview.requestConnectedRefinement =
            false;
        m_RawWorkspaceLocalRangeTargetPreview.generation =
            ++m_RawWorkspaceLocalRangeTargetPreviewGeneration;
        m_RawWorkspaceLocalRangeTargetPreviewRefined = false;
        m_RawWorkspaceLocalRangeTargetPreviewRefinementPending = false;
        ClearRawWorkspaceLocalRangeOverlayState();
        MarkRenderRefreshDirty();
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_RawWorkspaceLocalRangeTargetSamplePending = true;
        m_RawWorkspaceLocalRangeTargetSampleValid = false;
        m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
        m_RawWorkspaceLocalRangeTargetHoverSample = false;
        m_RawWorkspaceLocalRangeTargetCreateZone = true;
        m_RawWorkspaceLocalRangeTargetContextMenuRequested = true;
        m_RawWorkspaceLocalRangeTargetSourceKey = selectedSource.relativePathKey;
        m_RawWorkspaceLocalRangeTargetU =
            std::clamp((mouse.x - imageRect.Min.x) / std::max(1.0f, imageRect.GetWidth()), 0.0f, 1.0f);
        m_RawWorkspaceLocalRangeTargetV =
            std::clamp((mouse.y - imageRect.Min.y) / std::max(1.0f, imageRect.GetHeight()), 0.0f, 1.0f);
        MarkRenderRefreshDirty();
    } else if (hovered &&
        !m_RawWorkspaceLocalRangeTargetDragging &&
        !m_RawWorkspaceLocalRangeTargetSamplePending) {
        const double now = ImGui::GetTime();
        const float dx = mouse.x - m_RawWorkspaceLocalRangeTargetLastHoverMouse.x;
        const float dy = mouse.y - m_RawWorkspaceLocalRangeTargetLastHoverMouse.y;
        if ((dx * dx + dy * dy) >= 9.0f &&
            (m_RawWorkspaceLocalRangeTargetLastHoverRequestTime < 0.0 ||
                now - m_RawWorkspaceLocalRangeTargetLastHoverRequestTime >= 0.05)) {
            m_RawWorkspaceLocalRangeTargetLastHoverRequestTime = now;
            m_RawWorkspaceLocalRangeTargetLastHoverMouse = mouse;
            m_RawWorkspaceLocalRangeTargetLastPointerMotionTime = now;
            m_RawWorkspaceLocalRangeTargetPreview.provisional = true;
            m_RawWorkspaceLocalRangeTargetPreview.requestConnectedRefinement = false;
            m_RawWorkspaceLocalRangeTargetPreviewRefined = false;
            m_RawWorkspaceLocalRangeTargetPreviewRefinementPending = false;
            m_RawWorkspaceLocalRangeTargetSamplePending = true;
            m_RawWorkspaceLocalRangeTargetHoverSample = true;
            m_RawWorkspaceLocalRangeTargetSourceKey = selectedSource.relativePathKey;
            m_RawWorkspaceLocalRangeTargetU =
                std::clamp((mouse.x - imageRect.Min.x) / std::max(1.0f, imageRect.GetWidth()), 0.0f, 1.0f);
            m_RawWorkspaceLocalRangeTargetV =
                std::clamp((mouse.y - imageRect.Min.y) / std::max(1.0f, imageRect.GetHeight()), 0.0f, 1.0f);
            // Hover sampling is observational. It needs an auxiliary proxy
            // render for the scene value, but must not enter edit-preview
            // mode, schedule a settled render, or replace the visible image.
            MarkRenderRefreshDirty();
        }
    }

    if (hovered &&
        !m_RawWorkspaceLocalRangeTargetDragging &&
        m_RawWorkspaceLocalRangeTargetPreview.enabled &&
        m_RawWorkspaceLocalRangeTargetPreview.provisional &&
        m_RawWorkspaceLocalRangeTargetLastPointerMotionTime >= 0.0 &&
        ImGui::GetTime() - m_RawWorkspaceLocalRangeTargetLastPointerMotionTime >= 0.12) {
        m_RawWorkspaceLocalRangeTargetPreview.provisional = false;
        m_RawWorkspaceLocalRangeTargetPreview.requestConnectedRefinement = true;
        m_RawWorkspaceLocalRangeTargetPreview.generation =
            ++m_RawWorkspaceLocalRangeTargetPreviewGeneration;
        m_RawWorkspaceLocalRangeTargetPreviewRefined = false;
        m_RawWorkspaceLocalRangeTargetPreviewRefinementPending = true;
        m_RawWorkspaceLocalRangeTargetLastRefinementPollTime =
            ImGui::GetTime();
        MarkRenderRefreshDirty();
    }
    if (hovered &&
        !m_RawWorkspaceLocalRangeTargetDragging &&
        m_RawWorkspaceLocalRangeTargetPreview.enabled &&
        m_RawWorkspaceLocalRangeTargetPreview.requestConnectedRefinement &&
        m_RawWorkspaceLocalRangeTargetPreviewRefinementPending &&
        !m_RawWorkspaceLocalRangeTargetPreviewRefined) {
        const double now = ImGui::GetTime();
        if (m_RawWorkspaceLocalRangeTargetLastRefinementPollTime < 0.0 ||
            now - m_RawWorkspaceLocalRangeTargetLastRefinementPollTime >=
                (1.0 / 60.0)) {
            m_RawWorkspaceLocalRangeTargetLastRefinementPollTime = now;
            MarkRenderRefreshDirty();
        }
    }

    if (hovered && !m_LibraryWindowHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0001f) {
        const float wheel = ImGui::GetIO().MouseWheel;
        editActiveZone(
            [&](Stack::RawRecipe::RawLocalRangeRecipe&,
                Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                if (ctrlHeld && zone.colorEnabled) {
                    zone.colorRadius = std::clamp(
                        zone.colorRadius + wheel * 0.004f,
                        0.002f,
                        0.25f);
                } else if (shiftHeld) {
                    zone.featherEv = std::clamp(
                        zone.featherEv + wheel * 0.10f,
                        0.02f,
                        4.0f);
                } else {
                    zone.coreHalfWidthEv = std::clamp(
                        zone.coreHalfWidthEv + wheel * 0.10f,
                        0.05f,
                        4.0f);
                }
                return true;
            },
            true);
    }

    if (m_RawWorkspaceLocalRangeTargetDragging &&
        m_RawWorkspaceLocalRangeTargetSourceKey == selectedSource.relativePathKey &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float dragOffsetEv = std::clamp(
            Stack::RawLocalRangeTargetInteraction::DragOffsetEv(
                m_RawWorkspaceLocalRangeTargetStartMouseY,
                mouse.y),
            kRawLocalRangeMinDeltaEv - kRawLocalRangeMaxDeltaEv,
            kRawLocalRangeMaxDeltaEv - kRawLocalRangeMinDeltaEv);
        const float targetDeltaEv = std::clamp(
            m_RawWorkspaceLocalRangeTargetStartDeltaEv + dragOffsetEv,
            kRawLocalRangeMinDeltaEv,
            kRawLocalRangeMaxDeltaEv);
        if (std::abs(dragOffsetEv - m_RawWorkspaceLocalRangeTargetDragOffsetEv) >= 0.01f ||
            std::abs(targetDeltaEv - m_RawWorkspaceLocalRangeTargetDeltaEv) >= 0.01f) {
            m_RawWorkspaceLocalRangeTargetDragOffsetEv = dragOffsetEv;
            const bool canEdit =
                m_RawWorkspaceLocalRangeTargetCreateIntent ||
                !m_RawWorkspaceLocalRangeTargetZoneId.empty();
            if (m_RawWorkspaceLocalRangeTargetSamplePending) {
                // Preserve a fast drag until the click sample arrives. The
                // sampled mask decides whether it refines, creates, or remains
                // a true no-op.
                m_RawWorkspaceLocalRangeTargetApplyWhenSampled = true;
            } else if (m_RawWorkspaceLocalRangeTargetSampleValid && canEdit) {
                m_RawWorkspaceLocalRangeTargetDeltaEv = targetDeltaEv;
                if (ApplyRawWorkspaceLocalRangeTargetDelta(true)) {
                    m_RawWorkspaceLocalRangeTargetEditStarted = true;
                    m_RawWorkspaceLocalRangeTargetInteractionState =
                        m_RawWorkspaceLocalRangeTargetCreateIntent
                            ? Stack::RawLocalRangeTargetInteraction::State::Creating
                            : Stack::RawLocalRangeTargetInteraction::State::Refining;
                }
            } else {
                m_RawWorkspaceLocalRangeTargetDragOffsetEv = 0.0f;
                m_RawWorkspaceLocalRangeTargetDeltaEv =
                    m_RawWorkspaceLocalRangeTargetStartDeltaEv;
            }
        }
    } else if (m_RawWorkspaceLocalRangeTargetDragging &&
        m_RawWorkspaceLocalRangeTargetSourceKey == selectedSource.relativePathKey) {
        const bool shouldApply =
            std::abs(m_RawWorkspaceLocalRangeTargetDragOffsetEv) > 0.005f;
        const bool canEdit =
            m_RawWorkspaceLocalRangeTargetCreateIntent ||
            !m_RawWorkspaceLocalRangeTargetZoneId.empty();
        if (shouldApply &&
            canEdit &&
            m_RawWorkspaceLocalRangeTargetSampleValid) {
            ApplyRawWorkspaceLocalRangeTargetDelta(false);
        } else if (shouldApply &&
            m_RawWorkspaceLocalRangeTargetSamplePending) {
            m_RawWorkspaceLocalRangeTargetApplyWhenSampled = true;
        } else if (!m_RawWorkspaceLocalRangeTargetEditStarted) {
            m_RawWorkspaceLocalRangeTargetDragOffsetEv = 0.0f;
            m_RawWorkspaceLocalRangeTargetDeltaEv =
                m_RawWorkspaceLocalRangeTargetStartDeltaEv;
        }
        m_RawWorkspaceLocalRangeTargetDragging = false;
        m_RawWorkspaceLocalRangeTargetInteractionState =
            Stack::RawLocalRangeTargetInteraction::State::Hover;
        if (!m_RawWorkspaceLocalRangeTargetApplyWhenSampled) {
            m_RawWorkspaceLocalRangeTargetCreateIntent = false;
        }
    }

    if (m_RawWorkspaceLocalRangeTargetContextMenuRequested &&
        m_RawWorkspaceLocalRangeTargetSampleValid &&
        !m_RawWorkspaceLocalRangeTargetSamplePending) {
        if (m_RawWorkspaceLocalRangeTargetZoneId.empty()) {
            m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
            if (ApplyRawWorkspaceLocalRangeTargetDelta(false)) {
                // A right-click needs a concrete zone while its menu is open,
                // but opening and dismissing the menu must not leave behind a
                // zero-EV recipe edit.
                m_RawWorkspaceLocalRangeTargetTransientZone = true;
            }
        }
        ImGui::OpenPopup("RawLocalRangeTargetZoneMenu");
        m_RawWorkspaceLocalRangeTargetContextMenuRequested = false;
    }

    const bool targetZoneMenuOpen =
        ImGui::BeginPopup("RawLocalRangeTargetZoneMenu");
    if (targetZoneMenuOpen) {
        Stack::RawRecipe::RawLocalRangeTargetZone activeZone;
        bool hasActiveZone = false;
        for (const Stack::RawRecipe::RawLocalRangeTargetZone& zone :
             m_Project->rawRecipe.localRange.targetZones) {
            if (zone.id == m_RawWorkspaceLocalRangeTargetZoneId) {
                activeZone = zone;
                hasActiveZone = true;
                break;
            }
        }
        if (hasActiveZone) {
            ImGui::TextUnformatted(
                activeZone.name.empty() ? "Target Zone" : activeZone.name.c_str());
            ImGui::TextDisabled(
                "%+.2f EV  |  %.2f EV core  |  %.2f EV feather",
                activeZone.deltaEv,
                activeZone.coreHalfWidthEv,
                activeZone.featherEv);
            if (ImGui::MenuItem(
                    "Selected areas",
                    nullptr,
                    activeZone.scope ==
                        Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas)) {
                editActiveZone(
                    [](Stack::RawRecipe::RawLocalRangeRecipe&,
                       Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                        zone.scope =
                            Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
                        return true;
                    },
                    false);
            }
            if (ImGui::MenuItem(
                    "All matches",
                    nullptr,
                    activeZone.scope ==
                        Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches)) {
                editActiveZone(
                    [](Stack::RawRecipe::RawLocalRangeRecipe&,
                       Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                        zone.scope =
                            Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches;
                        return true;
                    },
                    false);
            }
            if (ImGui::MenuItem(
                    "Color match",
                    nullptr,
                    activeZone.colorEnabled)) {
                const std::array<float, 3> sampledColor =
                    Stack::RawRecipe::SceneLinearRgbToUvChroma(
                        m_RawWorkspaceLocalRangeTargetSceneR,
                        m_RawWorkspaceLocalRangeTargetSceneG,
                        m_RawWorkspaceLocalRangeTargetSceneB,
                        m_Project->rawRecipe.technical.workingSpace);
                editActiveZone(
                    [&](Stack::RawRecipe::RawLocalRangeRecipe&,
                        Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                        zone.colorEnabled = !zone.colorEnabled;
                        if (zone.colorEnabled) {
                            zone.targetUPrime = sampledColor[0];
                            zone.targetVPrime = sampledColor[1];
                            zone.targetChroma = sampledColor[2];
                        }
                        return true;
                    },
                    false);
            }
            if (activeZone.colorEnabled &&
                ImGui::MenuItem("Resample color here")) {
                const std::array<float, 3> sampledColor =
                    Stack::RawRecipe::SceneLinearRgbToUvChroma(
                        m_RawWorkspaceLocalRangeTargetSceneR,
                        m_RawWorkspaceLocalRangeTargetSceneG,
                        m_RawWorkspaceLocalRangeTargetSceneB,
                        m_Project->rawRecipe.technical.workingSpace);
                editActiveZone(
                    [&](Stack::RawRecipe::RawLocalRangeRecipe&,
                        Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                        zone.targetUPrime = sampledColor[0];
                        zone.targetVPrime = sampledColor[1];
                        zone.targetChroma = sampledColor[2];
                        return true;
                    },
                    false);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Combine target zones");
            const auto setCombineMode =
                [&](const char* label,
                    Stack::RawRecipe::RawLocalRangeZoneCombineMode mode) {
                    if (ImGui::MenuItem(
                            label,
                            nullptr,
                            m_Project->rawRecipe.localRange
                                    .targetZoneCombineMode == mode)) {
                        Stack::RawRecipe::RawDevelopmentRecipe edited =
                            m_Project->rawRecipe;
                        edited.localRange.targetZoneCombineMode = mode;
                        ApplyRawWorkspaceRecipeEditForSelectedSource(edited, false);
                    }
                };
            setCombineMode(
                "Add",
                Stack::RawRecipe::RawLocalRangeZoneCombineMode::Add);
            setCombineMode(
                "Strongest",
                Stack::RawRecipe::RawLocalRangeZoneCombineMode::Strongest);
            setCombineMode(
                "Blend",
                Stack::RawRecipe::RawLocalRangeZoneCombineMode::Blend);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset zone exposure")) {
                editActiveZone(
                    [](Stack::RawRecipe::RawLocalRangeRecipe&,
                       Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                        zone.deltaEv = 0.0f;
                        return true;
                    },
                    false);
            }
            if (ImGui::MenuItem("Delete zone")) {
                Stack::RawRecipe::RawDevelopmentRecipe edited =
                    m_Project->rawRecipe;
                auto& zones = edited.localRange.targetZones;
                zones.erase(
                    std::remove_if(
                        zones.begin(),
                        zones.end(),
                        [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                            return zone.id ==
                                m_RawWorkspaceLocalRangeTargetZoneId;
                        }),
                    zones.end());
                ApplyRawWorkspaceRecipeEditForSelectedSource(edited, false);
                m_RawWorkspaceLocalRangeTargetZoneId.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (!targetZoneMenuOpen &&
        !m_RawWorkspaceLocalRangeTargetContextMenuRequested &&
        m_RawWorkspaceLocalRangeTargetTransientZone &&
        !m_RawWorkspaceLocalRangeTargetZoneId.empty()) {
        Stack::RawRecipe::RawDevelopmentRecipe edited =
            m_Project->rawRecipe;
        auto& zones = edited.localRange.targetZones;
        const std::size_t previousCount = zones.size();
        zones.erase(
            std::remove_if(
                zones.begin(),
                zones.end(),
                [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                    return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
                }),
            zones.end());
        if (zones.size() != previousCount) {
            ApplyRawWorkspaceRecipeEditForSelectedSource(edited, false);
        }
        m_RawWorkspaceLocalRangeTargetZoneId.clear();
        m_RawWorkspaceLocalRangeTargetTransientZone = false;
    }

    if (hovered || m_RawWorkspaceLocalRangeTargetDragging) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
        const ImU32 shadow = IM_COL32(0, 0, 0, 190);
        const float radius = 17.0f;
        drawList->AddCircle(mouse, radius + 1.5f, shadow, 32, 3.0f);
        drawList->AddCircle(mouse, radius, accent, 32, 1.8f);
        drawList->AddLine(
            ImVec2(mouse.x - 7.0f, mouse.y),
            ImVec2(mouse.x + 7.0f, mouse.y),
            accent,
            1.5f);
        drawList->AddLine(
            ImVec2(mouse.x, mouse.y - 7.0f),
            ImVec2(mouse.x, mouse.y + 7.0f),
            accent,
            1.5f);

        std::string actionText;
        if (m_RawWorkspaceLocalRangeTargetDragging) {
            if (m_RawWorkspaceLocalRangeTargetCreateIntent) {
                actionText = "New zone";
            } else if (!m_RawWorkspaceLocalRangeTargetZoneId.empty()) {
                actionText = "Refine " +
                    (m_RawWorkspaceLocalRangeTargetHoverZoneName.empty()
                            ? std::string("target")
                            : m_RawWorkspaceLocalRangeTargetHoverZoneName);
            } else {
                actionText = "No zone here - Ctrl-drag to create";
            }
        } else if (ctrlHeld) {
            actionText = "Ctrl-drag: new zone";
        } else if (m_RawWorkspaceLocalRangeTargetHoverZoneIndex >= 0) {
            actionText = "Refine " +
                (m_RawWorkspaceLocalRangeTargetHoverZoneName.empty()
                        ? std::string("target")
                        : m_RawWorkspaceLocalRangeTargetHoverZoneName);
        } else if (!m_Project->rawRecipe.localRange.targetZones.empty()) {
            actionText = "Ctrl-drag: new zone";
        } else {
            actionText = "Drag: first zone";
        }
        if (m_RawWorkspaceLocalRangeTargetPreview.enabled &&
            m_RawWorkspaceLocalRangeTargetPreview.provisional &&
            !m_RawWorkspaceLocalRangeTargetDragging) {
            actionText += " | refining outline...";
        }

        char readout[192];
        if (HasRawWorkspaceLocalRangeTargetSampleForSource(
                selectedSource.relativePathKey)) {
            const bool hasEditableTarget =
                m_RawWorkspaceLocalRangeTargetCreateIntent ||
                !m_RawWorkspaceLocalRangeTargetZoneId.empty();
            if (hasEditableTarget) {
                std::snprintf(
                    readout,
                    sizeof(readout),
                    "%s  |  %+.2f scene EV  |  %+.2f EV",
                    actionText.c_str(),
                    m_RawWorkspaceLocalRangeTargetSceneEv,
                    m_RawWorkspaceLocalRangeTargetDeltaEv);
            } else {
                std::snprintf(
                    readout,
                    sizeof(readout),
                    "%s  |  %+.2f scene EV",
                    actionText.c_str(),
                    m_RawWorkspaceLocalRangeTargetSceneEv);
            }
        } else {
            std::snprintf(
                readout,
                sizeof(readout),
                "%s  |  sampling scene EV...",
                actionText.c_str());
        }
        const ImVec2 readoutSize = ImGui::CalcTextSize(readout);
        const ImVec2 textMin(
            std::clamp(
                mouse.x + 22.0f,
                imageRect.Min.x + 4.0f,
                imageRect.Max.x - readoutSize.x - 10.0f),
            std::clamp(
                mouse.y + 16.0f,
                imageRect.Min.y + 4.0f,
                imageRect.Max.y - readoutSize.y - 8.0f));
        drawList->AddRectFilled(
            ImVec2(textMin.x - 5.0f, textMin.y - 3.0f),
            ImVec2(
                textMin.x + readoutSize.x + 5.0f,
                textMin.y + readoutSize.y + 3.0f),
            IM_COL32(0, 0, 0, 155),
            4.0f);
        drawList->AddText(textMin, IM_COL32(235, 245, 245, 240), readout);
    }
}

void EditorModule::ClearRawWorkspaceLivePreviewState() {
    ClearRawWorkspaceLabGradingScope();
    ClearRawRenderSession();
    m_RawViewportRequest = {};
    m_RawViewportPresentedRegion = {};
    m_RawViewportPresentedGeneration = 0;
    ClearViewportOutputTiles();
    if (m_RawWorkspaceExportRenderRequested) {
        m_RawWorkspaceExportRenderRequested = false;
        m_RawWorkspaceExportRenderGeneration = 0;
        m_RawWorkspaceExportPath.clear();
        m_RawWorkspaceExportColorChunks = {};
        ++m_ExportGeneration;
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText =
            "RAW export canceled because the source or project changed.";
    }
    if (m_RawWorkspaceLabUi.colorWarpCloudVertexBuffer != 0) {
        glDeleteBuffers(
            1,
            &m_RawWorkspaceLabUi.colorWarpCloudVertexBuffer);
    }
    if (m_RawWorkspaceLabUi.colorWarpCloudVertexArray != 0) {
        glDeleteVertexArrays(
            1,
            &m_RawWorkspaceLabUi.colorWarpCloudVertexArray);
    }
    if (m_RawWorkspaceLabUi.colorWarpCloudProgram != 0) {
        glDeleteProgram(m_RawWorkspaceLabUi.colorWarpCloudProgram);
    }
    m_RawWorkspaceLabUi.colorWarpCloudProgram = 0;
    m_RawWorkspaceLabUi.colorWarpCloudVertexArray = 0;
    m_RawWorkspaceLabUi.colorWarpCloudVertexBuffer = 0;
    m_RawWorkspaceLabUi.colorWarpCloudGpuFingerprint = 0;
    m_RawWorkspaceLabUi.colorWarpCloudGpuPointCount = 0;
    if (m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture != 0) {
        glDeleteTextures(
            1,
            &m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture);
    }
    m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture = 0;
    m_RawWorkspaceLabUi.colorWarpAffectedOverlayWidth = 0;
    m_RawWorkspaceLabUi.colorWarpAffectedOverlayHeight = 0;
    m_RawWorkspaceLabUi.colorWarpAffectedOverlayFingerprint = 0;
    m_RawWorkspaceLabUi.colorWarpAffectedOverlaySourceKey.clear();
    int outputWidth = 0;
    int outputHeight = 0;
    EditorRenderWorker::SharedTextureResult outputTexture;
    outputTexture.texture = m_Pipeline.TakeExternalOutputTexture(outputWidth, outputHeight);
    outputTexture.width = outputWidth;
    outputTexture.height = outputHeight;
    QueueViewportOutputTextureRelease(outputTexture);
    m_RawWorkspacePreviewSourceKey.clear();
    m_RawWorkspaceFastPreviewUntilTime = -1.0;
    m_RawWorkspaceFullResolutionPreviewPending = false;
    m_RawWorkspaceFullResolutionPreviewRequested = false;
    m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
    m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
    m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
    m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
    m_RawWorkspaceExplicitFullQualityRenderRequested = false;
    m_RawWorkspacePreviewSlowSamples = 0;
    m_RawWorkspacePreviewScaleCooldown = 0;
    m_RawWorkspaceAdaptivePreviewScale = 1.0f;
    m_RawWorkspaceAdaptiveFrameTimeMs = 0.0;
    m_RawWorkspaceAdaptiveGestureActive = false;
    m_RawWorkspaceAdaptiveLastAcceptedCommandTime = {};
    m_RawWorkspaceAdaptiveLastAdoptionTime = {};
    m_RawWorkspacePreviewSupersededStreak = 0;
    m_RawWorkspacePreviewHealthyStreak = 0;
    m_RawWorkspaceGpuBudgetLastRefresh = {};
    m_RawWorkspaceViewTransformInputStats = {};
    m_RawWorkspaceFinalDisplayStats = {};
    m_RawWorkspaceStageStatsReadbacks.clear();
    m_RawWorkspaceGraphScopeReadback = {};
    ClearRawWorkspaceGraphScopeReadbackCaches();
    m_RawWorkspaceStartPointDiagnostics =
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics();
    m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
    ClearRawWorkspaceLocalRangeTargetState(true);
}

void EditorModule::ReleaseRawWorkspacePreviewForTabChange() {
    const bool hasRawPreviewState =
        !m_ViewportOutputRawWorkspaceSourceKey.empty() ||
        !m_RawWorkspacePreviewSourceKey.empty() ||
        m_RawWorkspaceFullResolutionPreviewPending ||
        m_RawWorkspacePreviewOutputKind != RawWorkspacePreviewOutputKind::None;
    if (!hasRawPreviewState) {
        return;
    }

    ClearRawWorkspaceLivePreviewState();
    if (IsRawWorkspaceProjectActive()) {
        MarkRenderRefreshDirty();
    }
}
