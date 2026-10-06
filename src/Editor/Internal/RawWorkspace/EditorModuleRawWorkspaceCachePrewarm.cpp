#include "Editor/EditorModule.h"

#include "App/settings/AppearanceTheme.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"

#include <cmath>
#include <optional>

namespace {

using RawLabTool = Stack::EditorModuleTypes::RawLabTool;
using RawCacheStage = Stack::Renderer::RawDevelopmentCache::Stage;

constexpr double kRawCachePrewarmDwellSeconds = 0.20;
constexpr float kRawCachePrewarmPointerResetDistance = 4.0f;
constexpr std::uint64_t kRawCachePrewarmByteBudget =
    64ull * 1024ull * 1024ull;

bool FinishToneHasPendingSideEffects(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    return recipe.finishTone.layerJson.is_object() &&
        recipe.finishTone.layerJson.value("autoCalibratePending", false);
}

std::optional<RawCacheStage> ResolveSafePrewarmStage(
    RawLabTool tool,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    // RGB denoise may dispatch an external model and intentionally has its own
    // asynchronous completion lifecycle. Hovering must never start that work.
    if (Stack::RawRecipe::SanitizeRgbDenoiseRecipe(recipe.rgbDenoise).enabled) {
        return std::nullopt;
    }

    const bool localRangeEnabled =
        Stack::RawRecipe::IsLocalRangeEnabled(recipe);
    switch (tool) {
        case RawLabTool::Light:
        case RawLabTool::Calibration:
            return RawCacheStage::NeutralPlacement;
        case RawLabTool::Zones:
            return RawCacheStage::RawPlacement;
        case RawLabTool::Tone:
            // Target-zone Local Range can build selection resources and is
            // deliberately left to an explicit foreground render.
            if (localRangeEnabled) return std::nullopt;
            return RawCacheStage::RawPlacement;
        case RawLabTool::Detail:
        case RawLabTool::Color:
            if (localRangeEnabled || FinishToneHasPendingSideEffects(recipe)) {
                return std::nullopt;
            }
            return RawCacheStage::PostFinishTone;
        case RawLabTool::View:
            if (localRangeEnabled || FinishToneHasPendingSideEffects(recipe)) {
                return std::nullopt;
            }
            return (Stack::RawRecipe::IsColorWarpEnabled(recipe.colorWarp) || Stack::RawRecipe::IsDetailContrastActive(recipe.detailContrast))
                ? RawCacheStage::PostColorWarp
                : RawCacheStage::PostFinishTone;
        case RawLabTool::Denoise:
        case RawLabTool::RgbDenoise:
        case RawLabTool::MultiFrame:
        case RawLabTool::Inspect:
        case RawLabTool::Transform:
            return std::nullopt;
    }
    return std::nullopt;
}

template <typename State>
bool SamePrewarmSemantic(
    const State& state,
    RawCacheStage stage,
    const std::string& sourceKey,
    std::uint64_t sourceHash,
    std::size_t fingerprint,
    int targetEdge) {
    return state.stage == stage &&
        state.sourceKey == sourceKey &&
        state.sourceHash == sourceHash &&
        state.fingerprint == fingerprint &&
        state.targetEdge == targetEdge;
}

template <typename State>
bool SameCompletedPrewarmSemantic(
    const State& state,
    RawCacheStage stage,
    const std::string& sourceKey,
    std::uint64_t sourceHash,
    std::size_t fingerprint,
    int targetEdge) {
    return state.completed &&
        state.completedStage == stage &&
        state.completedSourceKey == sourceKey &&
        state.completedSourceHash == sourceHash &&
        state.completedFingerprint == fingerprint &&
        state.completedTargetEdge == targetEdge;
}

} // namespace

void EditorModule::CancelRawWorkspaceCachePrewarm(bool clearCompleted) {
    RawWorkspaceCachePrewarmState& state = m_RawWorkspaceCachePrewarm;
    if (state.submitted && m_RawRenderClientId != 0) {
        Stack::EditorRendering::RawRenderService::Get().CancelCachePrewarm(
            m_RawRenderClientId);
    }
    if (clearCompleted) {
        state = {};
        return;
    }
    state.hoverActive = false;
    state.submitted = false;
    state.dwellStartedAt = -1.0;
    state.pointerAnchor = ImVec2(-10000.0f, -10000.0f);
    state.generation = 0;
}

void EditorModule::UpdateRawWorkspaceCachePrewarmHover(
    const RawWorkspaceEditContext& context,
    RawLabTool tool,
    bool hovered) {
    if (!hovered ||
        !context.canEdit ||
        context.multiFrameResult ||
        IsMultiFrameRawProjectActive() ||
        !m_RawWorkspaceRootTabActive ||
        IsRawWorkspaceUiInteractionActive() ||
        m_RawWorkspaceLocalRangeTargetMode ||
        m_RawWorkspaceLocalRangeTargetSamplePending ||
        m_RawWorkspaceExportRenderRequested ||
        ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
        ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
        ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        CancelRawWorkspaceCachePrewarm(false);
        return;
    }

    const std::optional<RawCacheStage> resolvedStage =
        ResolveSafePrewarmStage(tool, context.recipe);
    const std::string sourceKey = GetActiveRawWorkspacePreviewIdentity();
    const Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
    if (!resolvedStage.has_value() || sourceKey.empty() || source == nullptr) {
        CancelRawWorkspaceCachePrewarm(false);
        return;
    }

    const int targetEdge = std::max(
        384,
        m_RawWorkspaceInteractivePreviewMaxDimension);
    const std::uint64_t sourceHash =
        BuildRawWorkspaceAutoBaseSourceHash(*source);
    const std::size_t fingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            context.recipe,
            targetEdge,
            *resolvedStage);
    if (fingerprint == 0) {
        CancelRawWorkspaceCachePrewarm(false);
        return;
    }

    RawWorkspaceCachePrewarmState& state = m_RawWorkspaceCachePrewarm;
    const bool repeatedAttempt = state.attempted &&
        SamePrewarmSemantic(
            state,
            *resolvedStage,
            sourceKey,
            sourceHash,
            fingerprint,
            targetEdge);
    const bool alreadyCompleted = SameCompletedPrewarmSemantic(
        state,
        *resolvedStage,
        sourceKey,
        sourceHash,
        fingerprint,
        targetEdge);
    const bool sameActiveHover = state.hoverActive &&
        SamePrewarmSemantic(
            state,
            *resolvedStage,
            sourceKey,
            sourceHash,
            fingerprint,
            targetEdge);
    const ImVec2 pointer = ImGui::GetMousePos();

    if (!sameActiveHover) {
        CancelRawWorkspaceCachePrewarm(false);
        state.hoverActive = true;
        state.stage = *resolvedStage;
        state.sourceKey = sourceKey;
        state.sourceHash = sourceHash;
        state.fingerprint = fingerprint;
        state.targetEdge = targetEdge;
        state.dwellStartedAt = ImGui::GetTime();
        state.pointerAnchor = pointer;
        state.attempted = repeatedAttempt || alreadyCompleted;
        return;
    }

    const float pointerDx = pointer.x - state.pointerAnchor.x;
    const float pointerDy = pointer.y - state.pointerAnchor.y;
    const float resetDistanceSquared =
        kRawCachePrewarmPointerResetDistance *
        kRawCachePrewarmPointerResetDistance;
    if (pointerDx * pointerDx + pointerDy * pointerDy >
        resetDistanceSquared) {
        if (state.submitted && m_RawRenderClientId != 0) {
            Stack::EditorRendering::RawRenderService::Get().CancelCachePrewarm(
                m_RawRenderClientId);
            state.submitted = false;
            state.generation = 0;
            // Movement supersedes speculative GPU work. Do not relaunch the
            // same semantic request during this hover session.
            state.attempted = true;
        }
        state.dwellStartedAt = ImGui::GetTime();
        state.pointerAnchor = pointer;
    }
}

bool EditorModule::TrySubmitRawWorkspaceCachePrewarm(double now) {
    RawWorkspaceCachePrewarmState& state = m_RawWorkspaceCachePrewarm;
    if (!state.hoverActive || state.submitted || state.attempted ||
        state.dwellStartedAt < 0.0 ||
        now - state.dwellStartedAt < kRawCachePrewarmDwellSeconds ||
        m_RenderDirty || m_RenderPending || IsAnyRenderBackendBusy() ||
        ShouldDeferPreviewLikeWork(now) ||
        IsRawWorkspaceUiInteractionActive() ||
        m_RawWorkspaceExportRenderRequested ||
        m_RawWorkspaceExplicitFullQualityRenderRequested ||
        m_RawWorkspaceFullResolutionPreviewRequested ||
        m_RawWorkspaceAnalysisRequested ||
        m_RawWorkspaceLocalRangeTargetSamplePending ||
        m_RawRenderClientId == 0 ||
        m_RawRenderSessionSourceIdentity != state.sourceKey ||
        m_RawRenderSessionSourceHash != state.sourceHash ||
        m_RawRenderSessionGraphStructureRevision !=
            m_Project->graph.GetStructureRevision() ||
        m_RawRenderSessionContractRevision !=
            kRawRenderProcessingContractRevision) {
        return false;
    }

    const Stack::RawRecipe::RawDevelopmentRecipe recipe =
        m_Project->rawInteractionDraft.active &&
            m_Project->rawInteractionDraft.sourceKey == m_Project->rawSourceKey
        ? m_Project->rawInteractionDraft.recipe
        : m_Project->rawRecipe;
    const std::size_t currentFingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe,
            state.targetEdge,
            state.stage);
    if (currentFingerprint != state.fingerprint ||
        GetActiveRawWorkspacePreviewIdentity() != state.sourceKey) {
        CancelRawWorkspaceCachePrewarm(true);
        return false;
    }

    Stack::EditorRendering::RawRenderCommand command;
    command.generation =
        Stack::EditorRenderScheduling::NextGlobalGeneration();
    command.lastAcceptedGeneration = m_LastCompletedRenderGeneration;
    command.cancellationToken = command.generation;
    command.purpose = RawRenderPurpose::CachePrewarm;
    command.priority = RawRenderPurposePriority(command.purpose);
    command.targetEdge = state.targetEdge;
    command.telemetry.snapshotReadyAt =
        std::chrono::steady_clock::now();
    command.telemetry.interactionActive = false;
    command.rawWorkspace.sourceKey = state.sourceKey;
    command.rawWorkspace.sourceHash = state.sourceHash;
    command.rawWorkspace.hasRecipe = true;
    command.rawWorkspace.recipe = recipe;
    command.rawWorkspace.analysisRequested = false;
    command.rawWorkspace.graphScopeStage =
        RawDevelopmentGraphScopeStage::None;
    command.rawWorkspace.gradingScopeSource =
        RawDevelopmentGradingScopeSource::None;
    command.rawWorkspace.cachePrewarmStage = state.stage;
    command.rawWorkspace.cachePrewarmFingerprint = state.fingerprint;
    command.rawWorkspace.cachePrewarmByteBudget =
        kRawCachePrewarmByteBudget;
    if (m_Appearance) {
        command.viewportTiling = m_Appearance->GetViewportTilingSettings();
    }

    const std::uint64_t generation = command.generation;
    const bool submitted =
        Stack::EditorRendering::RawRenderService::Get().SubmitCommand(
            m_RawRenderClientId,
            std::move(command));
    Stack::EditorRenderScheduling::CommitRawSubmissionGeneration(
        submitted,
        false,
        generation,
        m_LatestRawPresentationGeneration,
        m_LatestRawAuxiliaryGeneration);
    if (!submitted) {
        return false;
    }

    state.submitted = true;
    state.attempted = true;
    state.generation = generation;
    m_RenderPending = true;
    return true;
}

void EditorModule::AdoptRawWorkspaceCachePrewarmResult(
    const EditorRenderWorker::Result& result) {
    if (result.rawRenderPurpose != RawRenderPurpose::CachePrewarm) {
        return;
    }

    RawWorkspaceCachePrewarmState& state = m_RawWorkspaceCachePrewarm;
    if (state.generation == 0 || result.generation != state.generation) {
        return;
    }
    state.submitted = false;
    state.generation = 0;
    if (!result.success ||
        !result.rawWorkspace.cachePrewarmCompleted ||
        !result.rawWorkspace.cachePrewarmStage.has_value() ||
        *result.rawWorkspace.cachePrewarmStage != state.stage ||
        result.rawWorkspace.sourceKey != state.sourceKey ||
        result.rawWorkspace.sourceHash != state.sourceHash ||
        result.rawWorkspace.cachePrewarmFingerprint != state.fingerprint) {
        return;
    }

    state.completed = true;
    state.completedStage = state.stage;
    state.completedSourceKey = state.sourceKey;
    state.completedSourceHash = state.sourceHash;
    state.completedFingerprint = state.fingerprint;
    state.completedTargetEdge = state.targetEdge;
}
