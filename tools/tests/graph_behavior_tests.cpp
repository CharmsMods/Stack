#include "Raw/RawViewportCalibration.h"
void TestRawViewportTimingLearning();
void TestRawLayerStackContracts();
void TestClosetWorkspaceStorage();
void TestProjectStoreOwnership();
#include "Raw/RawViewportResolutionPolicy.h"
#include "Raw/RawViewportTransition.h"
#include "Color/LutCreator.h"
#include "Color/LutImporter.h"
#include "Async/TaskSystem.h"
#include "Editor/LayerRegistry.h"
#include "Editor/GraphCapture.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/Internal/RawLab/RawLabGalleryLayout.h"
#include "Editor/Internal/RawWorkspace/RawWorkspaceThumbnailScheduler.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include "Editor/RawWorkspaceAutoBaseState.h"
#include "Editor/RawAttributeTargetProject.h"
#include "Editor/MultiFrameResultCache.h"
#include "Editor/RawLocalRangeTargetInteraction.h"
#include "Editor/RawRenderPlanning.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Editor/RawVariablePrecisionValueGraph.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include "Editor/Timeline/TimelineFrameProducer.h"
#include "Editor/Timeline/TimelinePersistence.h"
#include "Editor/Timeline/TimelinePlayback.h"
#include "Library/LibraryManager.h"
#include "MFSR/MFSRTypes.h"
#include "NodeMath/ChannelImageSemantics.h"
#include "NodeMath/DescriptorSerialization.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/ProjectIndex.h"
#include "Persistence/ProjectSaveCoordinator.h"
#include "Persistence/ProjectSessionController.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/RawProjectAttributeTransfer.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/Denoise/RawDenoiseBandSchedule.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawColorWarpAnalysis.h"
#include "Raw/RawColorWarpMask.h"
#include "Raw/RawEditAttributes.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/RawGpuMemoryBudget.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RawLoader.h"
#include "Raw/RawRestormerAdapter.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawWorkspaceManagedGraph.h"
#include "Queue/RenderQueueModel.h"
#include "Renderer/RawPreviewProxy.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/RenderTiling.h"
#include "Editor/UI/EditorViewportPreviewState.h"
#include "Restormer/RestormerPackage.h"
#include "Restormer/RestormerProtocol.h"
#include "Restormer/RestormerTiling.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/PixelBufferUtils.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::shared_ptr<LayerBase> NullLayerFactory() {
    return nullptr;
}

const std::vector<LayerDescriptor>& TestLayerDescriptors() {
    static const std::vector<LayerDescriptor> descriptors = {
        { LayerType::Brightness, "Brightness", "Brightness", "Brightness", "Color", "Adjust brightness.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::ChannelSafe },
        { LayerType::Contrast, "Contrast", "Contrast", "Contrast", "Color", "Adjust contrast.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::ChannelSafe },
        { LayerType::Saturation, "Saturation", "Saturation", "Saturation", "Color", "Adjust saturation.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::FullImagePreferred },
        { LayerType::Warmth, "Warmth", "Warmth", "Warmth", "Color", "Adjust warmth.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::FullImagePreferred },
        { LayerType::Sharpen, "Sharpen", "Sharpen", "Sharpen", "Color", "Adjust sharpening.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::ChannelUsefulWithWarning },
        { LayerType::ToneCurve, "ToneCurve", "Tone Curve", "Tone Curve", "Color / Tone", "Manual scene-referred finish curve.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::FullImagePreferred },
        { LayerType::ViewTransform, "ViewTransform", "View Transform", "View Transform", "Color / Tone", "Display/output transform.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::FullImagePreferred },
    };
    return descriptors;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestVariablePrecisionValueGraph() {
    namespace Graph = Stack::Editor::VariablePrecisionValueGraph;
    constexpr Graph::Configuration configuration { -16.0f, 16.0f, 0.20f };
    Require(std::abs(Graph::Span(configuration, 0.0f) - 32.0f) < 0.0001f &&
            std::abs(Graph::Span(configuration, 1.0f) - 0.20f) < 0.0001f,
        "variable-precision exposure graph should expose the full range at the top and 0.20 EV at the bottom");
    Graph::State state = Graph::FromValue(configuration, -8.0f, 0.0f);
    const float valueBeforeVertical = state.value;
    Graph::ApplyVertical(configuration, state, 0.8f);
    Require(std::abs(state.value - valueBeforeVertical) < 0.000001f &&
            std::abs(state.position - 0.25f) < 0.000001f,
        "descending through the exposure graph should preserve value and horizontal position");
    Graph::ApplyVertical(configuration, state, -0.8f);
    Require(std::abs(state.value - valueBeforeVertical) < 0.000001f,
        "ascending through the exposure graph should preserve value");
    state = Graph::FromValue(configuration, 0.0f, 1.0f);
    Graph::ApplyHorizontal(configuration, state, 1.0f);
    const float edgeValue = state.value;
    Graph::ApplyHorizontal(configuration, state, 10.0f);
    Graph::ApplyHorizontal(configuration, state, -0.01f);
    Require(state.value < edgeValue && state.position < 1.0f,
        "exposure graph edges should discard overscroll and reverse immediately");
}

void TestViewportPreviewPresentationMatrix() {
    using EditorViewportPreview::Presentation;
    using EditorViewportPreview::ResolvePresentation;
    using EditorViewportPreview::StateInput;

    StateInput input;
    input.outputAvailable = true;
    input.sourceAvailable = true;
    input.commandKeysAllowed = true;

    Require(
        ResolvePresentation(input) == Presentation::FilteredEdited,
        "viewport preview should remain edited when the pointer is outside the image");

    input.imageHovered = true;
    Require(
        ResolvePresentation(input) == Presentation::HoverOriginal,
        "viewport preview should show the original on normal image hover");

    input.shiftHeld = true;
    Require(
        ResolvePresentation(input) == Presentation::CleanProcessed,
        "Shift should show clean processed pixels without values");

    input.valuesHeld = true;
    Require(
        ResolvePresentation(input) == Presentation::CleanProcessedCursorReadout,
        "Shift+V should use a cursor readout when labels do not fit");

    input.labelsFit = true;
    Require(
        ResolvePresentation(input) == Presentation::CleanProcessedInlineValues,
        "Shift+V should use inline values when labels fit");

    input.cameraLocked = true;
    Require(
        ResolvePresentation(input) == Presentation::CleanProcessedInlineValues,
        "camera lock should not disable processed-pixel inspection");

    input.shiftHeld = false;
    input.valuesHeld = true;
    input.labelsFit = false;
    Require(
        ResolvePresentation(input) == Presentation::HoverOriginal,
        "V alone should follow normal hover-original behavior");

    input.staticCompareActive = true;
    input.shiftHeld = true;
    input.valuesHeld = true;
    Require(
        ResolvePresentation(input) == Presentation::StaticCompare,
        "Y comparison should override Shift and V inspection");

    input.staticCompareActive = false;
    input.sourceAvailable = false;
    input.shiftHeld = false;
    input.valuesHeld = false;
    Require(
        ResolvePresentation(input) == Presentation::FilteredEdited,
        "hover should remain edited when the original source is unavailable");

    input.sourceAvailable = true;
    input.shiftHeld = true;
    input.wantTextInput = true;
    Require(
        ResolvePresentation(input) == Presentation::HoverOriginal,
        "text input should block Shift inspection without disabling normal hover comparison");

    input.wantTextInput = false;
    input.ctrlHeld = true;
    Require(
        ResolvePresentation(input) == Presentation::HoverOriginal,
        "Ctrl should block Shift inspection");

    input.ctrlHeld = false;
    input.commandKeysAllowed = false;
    Require(
        ResolvePresentation(input) == Presentation::HoverOriginal,
        "blocked editor command input should block Shift inspection");
}

void TestEditorRenderWorkerGenerationScheduling() {
    using namespace Stack::EditorRenderScheduling;

    Require(
        ShouldUseCompositeRenderPath(true, false, false) &&
            ShouldUseCompositeRenderPath(true, true, false) &&
            !ShouldUseCompositeRenderPath(true, true, true) &&
            !ShouldUseCompositeRenderPath(false, true, true),
        "the RAW root workspace must retain its dedicated presentation path even when its graph has multiple completed chains");

    Require(
        std::abs(ClampRawInteractivePreviewScale(1.4f) - 1.0f) < 0.0001f &&
            std::abs(ClampRawInteractivePreviewScale(0.01f) - kRawPreviewMinimumScale) < 0.0001f,
        "adaptive RAW interaction should start at full viewport resolution and retain a 0.25x scale floor");
    const double adaptiveEma =
        UpdateRawInteractiveFrameTimeEma(40.0, 20.0);
    Require(
        std::abs(adaptiveEma - 35.0) < 0.001,
        "adaptive RAW interaction should smooth measured accepted-presentation cadence");
    Require(
        ShouldIncludeRawInteractiveCadenceSample(16.0, 34.0) &&
            !ShouldIncludeRawInteractiveCadenceSample(0.0, 80.0) &&
            !ShouldIncludeRawInteractiveCadenceSample(180.0, 180.0),
        "pointer-down startup and pauses inside a held gesture must not contaminate the sustained interaction frame-time controller");
    Require(
        ReduceRawInteractivePreviewScaleForFrameTime(1.0f, 60.0) < 1.0f &&
            ReduceRawInteractivePreviewScaleForFrameTime(1.0f, 35.0) == 1.0f,
        "adaptive RAW interaction should reduce raster size below the responsiveness floor without reacting to harmless timing noise");
    Require(
        ReduceRawInteractivePreviewScaleForBacklog(1.0f) < 1.0f &&
            RaiseRawInteractivePreviewScale(0.75f) > 0.75f,
        "adaptive RAW interaction should respond in both directions to backlog and sustained healthy frames");
    Require(
        IsRawInteractiveFrameTimeHealthy(30.0) &&
            !IsRawInteractiveFrameTimeHealthy(50.0),
        "the adaptive controller should distinguish the 30 fps target from the 20 fps floor");
    Require(
        ShouldStartRawNativeRefinement(true, false, true, false, true, true) &&
            !ShouldStartRawNativeRefinement(true, false, true, false, false, true) &&
            !ShouldStartRawNativeRefinement(true, false, false, false, true, true) &&
            !ShouldStartRawNativeRefinement(true, false, true, true, true, true) &&
            !ShouldStartRawNativeRefinement(true, false, true, false, true, false),
        "native refinement should wait for quiet input, a first presentation, and an idle render backend");
    Require(
        ShouldStartRawAnalysisAfterPreviewSettles(
            true, false, true, false, true) &&
            !ShouldStartRawAnalysisAfterPreviewSettles(
                true, true, true, false, true) &&
            !ShouldStartRawAnalysisAfterPreviewSettles(
                true, false, false, false, true) &&
            !ShouldStartRawAnalysisAfterPreviewSettles(
                true, false, true, true, true) &&
            !ShouldStartRawAnalysisAfterPreviewSettles(
                true, false, true, false, false),
        "settled RAW analysis should start from the accepted preview before native refinement");
    Require(
        ClassifyRawNativeRefinementCompletion(
            RawRenderPurpose::ViewportRefinement,
            0,
            true,
            true,
            true,
            true,
            true,
            false) == RawNativeRefinementCompletion::NativePresentation,
        "only an adopted, dimension-verified target-0 viewport refinement should complete native presentation");
    Require(
        ClassifyRawNativeRefinementCompletion(
            RawRenderPurpose::ViewportRefinement,
            1280,
            true,
            true,
            false,
            true,
            false,
            false) == RawNativeRefinementCompletion::BudgetFallback,
        "a memory-denied proxy should be recorded as a distinct native-refinement fallback");
    Require(
        ClassifyRawNativeRefinementCompletion(
            RawRenderPurpose::ExplicitInspection,
            1280,
            true,
            true,
            false,
            true,
            false,
            false) == RawNativeRefinementCompletion::BudgetFallback,
        "an explicit full-quality inspection should enter the same retryable resource deferral when it can only publish a proxy");
    Require(
        ClassifyRawNativeRefinementCompletion(
            RawRenderPurpose::ViewportRefinement,
            1280,
            true,
            true,
            true,
            true,
            false,
            false) == RawNativeRefinementCompletion::None &&
            ClassifyRawNativeRefinementCompletion(
                RawRenderPurpose::ViewportRefinement,
                0,
                true,
                true,
                true,
                true,
                false,
                false) == RawNativeRefinementCompletion::ExtentMismatch &&
            ClassifyRawNativeRefinementCompletion(
                RawRenderPurpose::ViewportRefinement,
                0,
                true,
                true,
                true,
                false,
                true,
                false) == RawNativeRefinementCompletion::None &&
            ClassifyRawNativeRefinementCompletion(
                RawRenderPurpose::AnalysisScopes,
                0,
                true,
                true,
                true,
                true,
                true,
                false) == RawNativeRefinementCompletion::None,
        "proxy, dimension-mismatched, unadopted, and analysis results must never masquerade as native presentation completion");
    const RawPresentationExtent uncroppedNative =
        ResolveExpectedRawPresentationExtent(
            3648, 2736, 0, false,
            0.0f, 0.0f, 1.0f, 1.0f);
    const RawPresentationExtent rotatedCroppedNative =
        ResolveExpectedRawPresentationExtent(
            3648, 2736, 90, true,
            0.25f, 0.10f, 0.50f, 0.80f);
    Require(
        uncroppedNative.width == 3648 &&
            uncroppedNative.height == 2736 &&
            rotatedCroppedNative.width == 1368 &&
            rotatedCroppedNative.height == 2920 &&
            IsRawPresentationNativeExtent(3648, 2736, 3648, 2736) &&
            !IsRawPresentationNativeExtent(1280, 960, 3648, 2736) &&
            ResolveRawPresentationPreviewMaxDimension(
                0, 1280, 960, 3648, 2736) == 1280 &&
            ResolveRawPresentationPreviewMaxDimension(
                1280, 3648, 2736, 3648, 2736) == 0,
        "RAW presentation quality must be derived from actual and expected raster extents, including rotation and output crop");
    Require(
        RawRenderResultOwnsTrackedRequest(
            RawRenderPurpose::ViewportRefinement,
            RawRenderPurpose::ViewportRefinement,
            82,
            82) &&
            !RawRenderResultOwnsTrackedRequest(
                RawRenderPurpose::ViewportRefinement,
                RawRenderPurpose::ViewportRefinement,
                81,
                82) &&
            !RawRenderResultOwnsTrackedRequest(
                RawRenderPurpose::ViewportRefinement,
                RawRenderPurpose::InteractiveSample,
                82,
                82) &&
            !RawRenderResultOwnsTrackedRequest(
                RawRenderPurpose::ViewportRefinement,
                RawRenderPurpose::ViewportRefinement,
                82,
                0),
        "only the exact submitted native generation may complete or re-arm its tracked request");
    Require(
        IsRawNativePresentationCurrent(0, 82, 82, false) &&
            !IsRawNativePresentationCurrent(1280, 82, 82, false) &&
            !IsRawNativePresentationCurrent(0, 81, 82, false) &&
            !IsRawNativePresentationCurrent(0, 82, 82, true),
        "native presentation currency should follow the presentation generation lane and current edit state");
    Require(
        ShouldRearmRawNativeRefinementAfterAcceptedPresentation(
            1280,
            RawNativeRefinementCompletion::None,
            true) &&
            !ShouldRearmRawNativeRefinementAfterAcceptedPresentation(
                0,
                RawNativeRefinementCompletion::NativePresentation,
                true) &&
            !ShouldRearmRawNativeRefinementAfterAcceptedPresentation(
                1280,
                RawNativeRefinementCompletion::BudgetFallback,
                true),
        "every accepted proxy should restore native refinement unless the tracked request entered resource deferral");
    Require(
        ShouldRearmUnsubmittedRawNativeRefinement(
            true, true, 0, false, false, false) &&
            !ShouldRearmUnsubmittedRawNativeRefinement(
                true, true, 83, false, false, false) &&
            !ShouldRearmUnsubmittedRawNativeRefinement(
                true, true, 0, true, false, false),
        "a scheduler-only native request should recover when no submission or backend work can still own it");
    Require(
        ShouldRetryRawNativeRefinementAfterFailure(0, 2) &&
            ShouldRetryRawNativeRefinementAfterFailure(1, 2) &&
            !ShouldRetryRawNativeRefinementAfterFailure(2, 2),
        "native allocation and render failures should retry twice without creating an infinite loop");
    Require(
        ShouldResumeRawNativeRefinementAfterBudgetChange(
            true, 700, 900, false) &&
            !ShouldResumeRawNativeRefinementAfterBudgetChange(
                true, 900, 900, false) &&
            !ShouldResumeRawNativeRefinementAfterBudgetChange(
                true, 700, 900, true),
        "a budget-deferred native request should resume only after usable GPU headroom increases");
    Require(
        ShouldCountRawPreviewSupersession(true, true, true),
        "a worker-started interactive frame superseded by sustained backlog should lower the adaptive scale");
    Require(
        !ShouldCountRawPreviewSupersession(true, true, false) &&
            !ShouldCountRawPreviewSupersession(false, true, true) &&
            !ShouldCountRawPreviewSupersession(true, false, true),
        "UI-frame coalescing, native refinement, and ordinary stale results must not lower RAW viewport quality");

    {
        int healthy = 0, slow = 0, cooldown = 0;
        float scale = 1.0f;
        for (int frame = 0; frame < 40; ++frame) {
            scale = StabilizeRawPreviewScale(scale,
                frame % 2 ? 0.8f : 1.3f, healthy, slow, cooldown);
        }
        Require(scale == 1.0f, "alternating timing pressure must not oscillate resolution");
        healthy = slow = cooldown = 0;
        for (int frame = 0; frame < 2; ++frame)
            scale = StabilizeRawPreviewScale(scale, 0.6f, healthy, slow, cooldown);
        Require(std::abs(scale - 0.6f) < 0.001f && cooldown == 2,
            "sustained overload lowers resolution and starts a settling interval");
        for (int frame = 0; frame < 2; ++frame)
            scale = StabilizeRawPreviewScale(scale, 1.5f, healthy, slow, cooldown);
        Require(std::abs(scale - 0.6f) < 0.001f,
            "cache timing during settling must not immediately reverse a reduction");
        for (int frame = 0; frame < 4; ++frame)
            scale = StabilizeRawPreviewScale(scale, 1.5f, healthy, slow, cooldown);
        Require(scale > 0.8f && scale < 0.85f,
            "sustained headroom raises resolution gradually after settling");
        Require(ShouldRequestRawRefinementOnRelease(true, false, true) &&
            !ShouldRequestRawRefinementOnRelease(true, true, true) &&
            !ShouldRequestRawRefinementOnRelease(false, false, true) &&
            !ShouldRequestRawRefinementOnRelease(true, false, false),
            "explicit release bypasses the proxy handoff while initial loading keeps its first preview");
    }
    {
        int healthy = 0, slow = 0, cooldown = 0;
        float scale = 1.0f;
        for (int frame = 0; frame < 100; ++frame) {
            const float previous = scale;
            scale = StabilizeRawPreviewScale(scale, frame < 10 ? 0.5f : 2.0f,
                healthy, slow, cooldown, false);
            Require(scale <= previous,
                "cold gestures may reduce quality but must never probe upward mid-drag");
        }
        Require(healthy == 4, "healthy drag samples retain headroom evidence when increases are disabled");
        Require(RawPreviewScaleForCost(100.0, 4.0f, 15) >
                RawPreviewScaleForCost(100.0, 4.0f, 60) &&
            Raw::ClampViewportTargetFps(-5) == 5 &&
            Raw::ResolveViewportTargetFps(2000, 144) == 144 &&
            Raw::ResolveViewportTargetFps(90, 60) == 60 &&
            Raw::ResolveViewportTargetFps(90, 144) == 90 &&
            Raw::ResolveViewportTargetFps(90, 0) == 60,
            "lower FPS budgets permit higher resolution and FPS settings stay bounded");
    }
    {
        float scale = 0.75f;
        for (int fps : { 30, 10, 20, 5, 30, 10 }) {
            scale = RawPreviewScaleAfterTargetChange(scale, 15.0, 4.0f, fps);
            Require(scale == RawPreviewScaleForCost(15.0, 4.0f, fps),
                "FPS slider changes spend the selected frame budget using measured pixel cost");
        }
        Require(RawPreviewScaleAfterTargetChange(1.0f, 100.0, 4.0f, 60) < 1.0f &&
            RawPreviewScaleAfterTargetChange(0.75f, 0.0, 4.0f, 5) == 0.75f,
            "tighter budgets reduce quality while unknown costs never enlarge it");
        Require(!ShouldLearnRawSteadyRenderCost(true, true) &&
            !ShouldLearnRawSteadyRenderCost(false, false) &&
            ShouldLearnRawSteadyRenderCost(false, true),
            "steady cost learning excludes startup and cached-only presentations");
    }
    {
        Raw::ViewportCalibration calibration;
        Require(calibration.EdgeForFps(30) == 0 && calibration.NativeFps() == 0.0,
            "unmeasured pipelines must not claim calibrated quality or native FPS");
        calibration.samples = { { 1000, 40.0, 20.0, false },
            { 2000, 100.0, 80.0, false }, { 4000, 300.0, 200.0, true } };
        Require(calibration.EdgeForFps(5) == 4000 && calibration.EdgeForFps(50) == 1000 &&
            calibration.EdgeForFps(10) > 2000 && calibration.EdgeForFps(10) < 4000 &&
            calibration.NativeFps() == 5.0,
            "measured costs immediately select larger rasters for lower FPS budgets");
        Require(!RawRenderPurposeMayPublishPresentation(RawRenderPurpose::ViewportCalibration) &&
            RawRenderPurposePriority(RawRenderPurpose::ViewportCalibration) >
                RawRenderPurposePriority(RawRenderPurpose::InteractivePresentation),
            "calibration runs below editing priority and cannot publish viewport pixels");
    }
    {
        Require(IsRawGestureIntermediate(true, true, 7, 7) &&
            !IsRawGestureIntermediate(true, true, 6, 7) &&
            !IsRawGestureIntermediate(true, false, 7, 7) &&
            !IsRawGestureIntermediate(false, true, 7, 7),
            "Completed intermediate frames belong only to the current active gesture");
        auto recipe = Stack::RawRecipe::MakeDefaultRecipe("test", "test");
        const auto neutral = Raw::ViewportWorkloadKeys(recipe);
        recipe.preToneExposureEv = 0.5f;
        const auto active = Raw::ViewportWorkloadKeys(recipe);
        recipe.preToneExposureEv = 2.0f;
        Require(active == Raw::ViewportWorkloadKeys(recipe) && neutral[2] != active[2],
            "Uniform exposure changes retain cost history; activating a bypassed stage needs its own timing");
        const auto baseline = recipe;
        const auto probes = Raw::BuildViewportProbes(recipe, Raw::ViewportStage::PostLocalRange);
        Require(probes.size() > 1 && Stack::RawRecipe::SerializeRecipe(recipe) == Stack::RawRecipe::SerializeRecipe(baseline),
            "Viewport preparation creates offscreen copies without changing the project");
        Raw::ViewportTimingBank bank;
        Raw::ViewportCalibrationSample sample {1000, 100.0, 80.0, false};
        sample.stages = {50, 10, 5, 5, 3, 3, 3, 1};
        bank.Record(active, sample);
        bool known = false;
        const double cold = bank.Cost(active, 1000, Raw::ViewportStage::RawPlacement, true, &known);
        const double warm = bank.Cost(active, 1000, Raw::ViewportStage::RawPlacement, false);
        Require(known && cold > warm && bank.EdgeForFps(active, 20, 1000, Raw::ViewportStage::RawPlacement, false) == 1000,
            "Cached edit suffix and first-use pipeline costs remain distinct");
        Require(bank.Cost(active, 1000, Raw::ViewportStage::RawPlacement, false, nullptr, 0.25, 3) <= warm,
            "Visible area reduces local work while retaining global dependencies");
        const double retainedCost = bank.Cost(active,1000,Raw::ViewportStage::RawBase,true);
        for (const auto& module : Raw::kViewportModules) {
            const auto sectionProbes = Raw::BuildViewportProbes(baseline,module.stage);
            Require(!sectionProbes.empty() && Raw::ViewportWorkloadKeys(sectionProbes.front().recipe) == active &&
                bank.Cost(active,1000,Raw::ViewportStage::RawBase,true) == retainedCost,
                "All RAW section descriptors preserve the current processing profile");
        }
        recipe.rgbDenoise.diagnosticMode = Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
        recipe.finishTone.layerJson["activeGraphView"] = 2;
        Require(Raw::ViewportWorkloadKeys(recipe) == active,
            "Scope display state cannot invalidate processing measurements");
        auto expanded = active;
        recipe.localRange.targetZones.push_back({});
        expanded = Raw::ViewportWorkloadKeys(recipe);
        bank.Cost(expanded,1000,Raw::ViewportStage::RawPlacement,false,&known);
        Require(!known,"Additional selection work remains provisional until measured");
        bank.Cost(active,1200,Raw::ViewportStage::RawPlacement,false,&known);
        Require(!known,"Higher-resolution first use is not extrapolated as verified");
        auto cachedSample = sample;
        cachedSample.firstMeasuredStage = 2;
        cachedSample.renderMs = cachedSample.startupMs = 20;
        cachedSample.stages[0] = cachedSample.stages[1] = 0;
        bank.Record(active,cachedSample);
        Require(bank.Cost(active,1000,Raw::ViewportStage::RawBase,true) >= 80.0,
            "A cached suffix observation cannot replace the cost of rebuilding upstream inputs");
        Raw::ViewportTimingBank twoSizes;
        twoSizes.Record(active,sample);
        auto larger = sample;
        larger.edge = 2000; larger.renderMs *= 4; larger.startupMs *= 4;
        for (auto& stage : larger.stages) stage *= 4;
        twoSizes.Record(active,larger);
        const int smallEdge = twoSizes.EdgeForFps(active,40,2000,Raw::ViewportStage::RawBase,true);
        Require(smallEdge > 0 && smallEdge <= 500,
            "Measured fixed and variable work can choose a raster below the first calibration size");
        Raw::ViewportTimingBank prepared;
        Raw::ViewportCalibrationSample ready {1000,400,20,false};
        ready.stages = {10,2,2,1,2,1,1,1};
        prepared.Record(active,ready);
        ready.edge = 4000; ready.startupMs = 800; ready.renderMs = 80;
        for (auto& cost : ready.stages) cost *= 4;
        prepared.Record(active,ready);
        for (int fps : {5,10}) Require(prepared.EdgeForFps(active,fps,4000,Raw::ViewportStage::RawBase,false) == 4000,
            "A prepared native rebuild uses its repeated cost at 5 and 10 FPS, not startup latency");
        Require(prepared.EdgeForFps(active,10,4000,Raw::ViewportStage::RawBase,true) < 4000,
            "First-use cost stays available without limiting every later render");
        const double exposureCost = prepared.Cost(active,4000,Raw::ViewportStage::RawPlacement,false);
        auto curveEdit = ready;
        curveEdit.changingStage = 4;
        curveEdit.stages[4] += 12; curveEdit.renderMs += 12;
        prepared.Record(active,curveEdit);
        Require(std::abs(prepared.Cost(active,4000,Raw::ViewportStage::RawPlacement,false)-exposureCost) < 0.001 &&
            prepared.Cost(active,4000,Raw::ViewportStage::PostFinishTone,false) >= curveEdit.stages[4],
            "Curve LUT rebuild cost applies to curve edits while EV reuses the prepared LUT");
        Require(Raw::ViewportMeasurementKey(active,1000,2) != Raw::ViewportMeasurementKey(active,1000,4),
            "Prepared inputs and module reconfiguration have separate verification identities");
        const std::array<Raw::ViewportResolutionCandidate,3> cachedChoices {{{3500,45},{1024,4},{5500,4.1}}};
        Require(Raw::SelectViewportResolution(cachedChoices,400) == 5500,
            "An unreachable FPS target compares cached edit latency instead of accepting a slower rebuild");
        const std::array<Raw::ViewportResolutionCandidate,3> qualityChoices {{{4000,90},{1024,3},{5500,110}}};
        Require(Raw::SelectViewportResolution(qualityChoices,10) == 4000 &&
            Raw::SelectViewportResolution(qualityChoices,5) == 5500 &&
            Raw::SelectViewportResolution(qualityChoices,400) == 1024,
            "Resolution selection spends the 5 and 10 FPS budgets while favoring faster cached work at 400 FPS");
        Raw::ViewportTimingBank fixed;
        Raw::ViewportCalibrationSample overhead {1000,20,20,false}; overhead.stages[7] = 20;
        fixed.Record(active,overhead); overhead.edge = 4000; fixed.Record(active,overhead);
        Require(fixed.EdgeForFps(active,400,4000,Raw::ViewportStage::RawBase,false) == 4000 &&
            std::abs(RawViewportEquivalentRenderMs(20,0.25f,20)-20) < 0.001 &&
            RawPreviewScaleForCost(20,4,400,20) == 4,
            "An unattainable FPS target does not discard detail when measured cost is independent of resolution");
        const auto regionRequirements = Raw::ViewportRegionRequirements(baseline,1000,750,4000,3000);
        Require(regionRequirements[0].support >= 2 && regionRequirements[0].materializeFullInput,
            "Demosaic support retains the full CFA input until its regional execution path is available");
        Raw::ViewportPresentation old;
        old.source = "source"; old.gesture = 1; old.view = 2;
        old.stage = Raw::ViewportStage::RawPlacement; old.interactive = true;
        auto next = old;
        auto fade = [&](double gap, double work, int fps = 30) {
            return Raw::ViewportFadeDuration(old, next, gap, work, fps);
        };
        Require(std::abs(fade(100,100)-0.065) < 0.0001 &&
            std::abs(fade(200,200)-0.13) < 0.0001 && fade(1000,1000) == 0.20,
            "Raw fades follow completion timing with bounded visual lag");
        Require(fade(16,16) == 0 && fade(10000,10) == 0 && fade(20,100) == 0,
            "Fast results and idle gaps do not introduce fading");
        Require(fade(50,50,10) == 0 && fade(50,50,30) > 0 && fade(20,20,60) > 0,
            "The fade FPS threshold controls eligibility independently of rendering");
        next.gesture = 2; next.interactive = false;
        Require(fade(150,150) > 0, "Released controls and new adjustments can fade completed results");
        next.stage = Raw::ViewportStage::NeutralPlacement;
        Require(fade(150,150) > 0, "Slow denoise completions can fade");
        next.diagnostic = true;
        Require(fade(150,150) == 0, "Diagnostics remain immediate");
        next = old; next.stage = Raw::ViewportStage::PostOutputCrop;
        Require(fade(150,150) == 0, "Geometry edits remain immediate");
        next = old; next.source = "another";
        Require(fade(150,150) == 0, "Source changes never blend unrelated images");
        next = old; next.view++;
        Require(fade(150,150) == 0, "Navigation generations must match");
        next = old; next.region = {100,80,0,0,50,40};
        Require(fade(150,150) > 0, "A full-image preview can map into a cropped refinement");
        const auto cropMap = Raw::MapViewportFade({},next.region);
        Require(cropMap.valid && cropMap.x == 0 && cropMap.y == 0.5 && cropMap.width == 0.5 &&
            cropMap.height == 0.5 && !Raw::MapViewportFade(next.region,{}).valid,
            "Refinement maps the correct UV area and rejects missing coverage");
        old.region = {100,80,25,20,50,40};
        next = old; next.region = {400,320,100,80,200,160};
        Require(fade(150,150) > 0, "Native refinement can fade across pixel grids for the same view");
        old.region = {};
        next = old; next.encodedSrgb = true;
        Require(fade(150,150) == 0, "Color encodings must match");
        next = old;
        Require(Raw::ViewportFadeDuration({},next,150,150,30) == 0 &&
            fade(-1,100) == 0 && fade(100,std::numeric_limits<double>::infinity()) == 0,
            "First frames and invalid timing never fade");
        old.width = 1000; old.height = 750;
        next = old; next.width = 4000; next.height = 3000; next.interactive = false;
        Require(std::abs(fade(10,10,5)-2.0/60) < 0.0001 && std::abs(fade(150,150)-0.0975) < 0.0001 &&
            fade(1000,1000) == 0.200,
            "Resolution increases after release get a quick adaptive fade regardless of the FPS threshold");
        next.interactive = true;
        Require(fade(10,10,30) == 0, "Fast updates during a drag still follow the cadence threshold");
        next.interactive = false; next.diagnostic = true;
        Require(fade(10,10) == 0, "Quality refinement never blends diagnostic views");
        next.diagnostic = false; next.view++;
        Require(fade(10,10) == 0, "Quality refinement never blends across navigation changes");
        next = old; next.interactive = false;
        Require(fade(10,10) == 0, "Same-resolution completions do not force a quality fade");
        next.width = 500; next.height = 375;
        Require(fade(10,10) == 0, "Resolution decreases do not force a quality fade");
        old.region = {1000,750,250,150,500,450}; old.width = 500; old.height = 450;
        next = old; next.region = {4000,3000,1000,600,2000,1800};
        next.width = 2000; next.height = 1800; next.interactive = false;
        Require(std::abs(fade(10,10)-2.0/60) < 0.0001, "Regional proxy-to-native refinements use normalized image density");
        Require(Raw::ViewportFadeDuration(old,next,100,10,30,80) > fade(100,10) &&
            Raw::ViewportFadeDuration(old,next,10,10,30,0,1000.0/144) < fade(10,10),
            "Recent render cadence smooths alternating completion times and fast displays shorten refinement handoffs");
        Require(Raw::ClampViewportFadeBelowFps(360) == 360 &&
            Raw::ResolveViewportTargetFps(Raw::ClampViewportFadeBelowFps(360),240) == 240 &&
            Raw::ResolveViewportTargetFps(Raw::ClampViewportFadeBelowFps(360),360) == 360,
            "Fade thresholds share the viewport target's monitor limit without a 120 FPS ceiling");
        const Raw::ViewportRegion region {100, 80, 0, 40, 50, 40};
        const auto scaled = Raw::ScaleViewportRegion(region, 50, 40);
        Require(scaled.Valid() && scaled.x == 0 && scaled.y == 20 && scaled.width == 25 && scaled.height == 20,
            "Viewport rectangles preserve edge coverage at proxy resolution");
    }
    Require(std::abs(RawViewportEquivalentRenderMs(20.0, 0.5f) - 80.0) < 0.001 &&
        std::abs(RawPreviewScaleForCost(80.0) - std::sqrt((1000.0 / 30.0) / 80.0)) < 0.001 &&
        RawPreviewScaleForCost(10.0) == 1.0f &&
        RawPreviewScaleForCost(10.0, 2.0f) > 1.0f &&
        RawPreviewScaleForCost(1.0, 2.0f) == 2.0f &&
        RawPreviewScaleForCost(10000.0) == kRawPreviewMinimumScale,
        "adaptive raster follows measured pixel cost within resolution bounds");

    Require(
        !DeferNewInteractivePreviewWhileFrameIsRendering(
            true, true, true, true),
        "active RAW drags should supersede an older interactive frame after UI-frame coalescing");
    Require(
        !DeferNewInteractivePreviewWhileFrameIsRendering(
            true, false, true, true),
        "settled RAW renders should retain latest-wins replacement behavior");
    Require(
        !DeferNewInteractivePreviewWhileFrameIsRendering(
            true, true, false, true),
        "an idle RAW renderer should immediately accept the newest interactive frame");
    Require(
        !DeferNewInteractivePreviewWhileFrameIsRendering(
            false, true, true, true),
        "non-RAW graph rendering should retain its existing scheduling policy");
    Require(
        !ShouldRefreshRawProjectCover(false, false, false),
        "interactive RAW proxies must not trigger synchronous cover readback");
    Require(
        ShouldRefreshRawProjectCover(false, true, false),
        "a settled display preview should be allowed to refresh the project cover");
    Require(
        ShouldRefreshRawProjectCover(true, false, false),
        "an explicit full-quality render should be allowed to refresh the project cover");
    Require(
        !ShouldRefreshRawProjectCover(true, true, true),
        "a stale settled render must not refresh the project cover");

    Require(
        !ShouldAdvanceProjectLoadAfterFirstPresentation(
            true, 19, 19, 41, 41, true),
        "project loading must wait for a newly accepted presentation generation");
    Require(
        !ShouldAdvanceProjectLoadAfterFirstPresentation(
            true, 19, 19, 41, 42, false),
        "project loading must not accept a generation without its matching presentation");
    Require(
        ShouldAdvanceProjectLoadAfterFirstPresentation(
            true, 19, 19, 41, 42, true),
        "an accepted first presentation should finish project loading even while auxiliary work continues");
    Require(
        ShouldAdvanceProjectLoadAfterFirstPresentation(
            false, 0, 0, 41, 41, false),
        "a deliberately disconnected graph should not wait for a first presentation");
    Require(
        !ShouldBlockProjectReadyForOptionalThumbnails(8, 24),
        "optional node-browser thumbnails must not hold the project-loading modal open");

    Require(
        AcceptSubmission(12, false, 10, 12),
        "render worker should accept a same-generation follow-up request");
    Require(
        !AcceptSubmission(11, false, 10, 12),
        "render worker should reject an older request after a newer submission");
    Require(
        !AcceptSubmission(9, false, 10, 9),
        "render worker should reject an explicitly invalidated request");
    Require(
        !AcceptSubmission(13, true, 10, 12),
        "render worker should reject submissions after shutdown starts");

    Require(
        DiscardCompletedResult(11, false, 10, 12, false),
        "render worker should discard a result superseded while it rendered");
    Require(
        !DiscardCompletedResult(11, false, 10, 12, true),
        "render worker should preserve a stale RAW cancellation acknowledgment");
    Require(
        DiscardCompletedResult(12, true, 10, 12, true),
        "render worker should discard all results once shutdown starts");
    Require(
        !DiscardCompletedResult(12, false, 10, 12, false),
        "render worker should publish the latest completed generation");

    std::uint64_t invalidBeforeGeneration = 12;
    std::uint64_t latestSubmittedGeneration = 12;
    for (std::uint64_t generation = 13; generation <= 2048; ++generation) {
        Require(
            AcceptSubmission(
                generation,
                false,
                invalidBeforeGeneration,
                latestSubmittedGeneration),
            "render worker should accept every monotonically newer generation");
        latestSubmittedGeneration = generation;
        Require(
            !AcceptSubmission(
                generation - 1,
                false,
                invalidBeforeGeneration,
                latestSubmittedGeneration),
            "rapid render submission should never re-admit the previous generation");
        Require(
            DiscardCompletedResult(
                generation - 1,
                false,
                invalidBeforeGeneration,
                latestSubmittedGeneration,
                false),
            "rapid render completion should discard every superseded image result");
        Require(
            !DiscardCompletedResult(
                generation,
                false,
                invalidBeforeGeneration,
                latestSubmittedGeneration,
                false),
            "rapid render completion should retain only the latest image result");

        if ((generation % 31u) == 0u) {
            invalidBeforeGeneration = generation;
            Require(
                !AcceptSubmission(
                    generation - 1,
                    false,
                    invalidBeforeGeneration,
                    latestSubmittedGeneration),
                "explicit invalidation should reject an in-flight pre-transition generation");
        }
    }
}

void TestPixelBufferLayoutGuards() {
    std::size_t byteCount = 0;
    Require(
        Stack::PixelBuffer::TryComputePixelByteCount(8, 8, 4, byteCount) &&
            byteCount == 256,
        "pixel byte count should accept ordinary RGBA layouts");
    Require(
        !Stack::PixelBuffer::TryComputePixelByteCount(0, 8, 4, byteCount) &&
            byteCount == 0,
        "pixel byte count should reject zero dimensions");
    Require(
        !Stack::PixelBuffer::TryComputePixelByteCount(-1, 8, 4, byteCount) &&
            byteCount == 0,
        "pixel byte count should reject negative dimensions");
    Require(
        !Stack::PixelBuffer::HasCompletePixelBuffer(255, 8, 8, 4),
        "pixel buffer validation should reject truncated RGBA payloads");
    Require(
        Stack::PixelBuffer::HasCompletePixelBuffer(256, 8, 8, 4),
        "pixel buffer validation should accept complete RGBA payloads");
    Require(
        !Stack::PixelBuffer::HasCompletePixelBuffer(320, 8, 8, 5),
        "pixel buffer validation should reject unsupported channel counts");

    const std::vector<unsigned char> placeholder =
        Stack::PixelBuffer::BuildTransparentRgbaPixels(8, 8);
    Require(
        placeholder.size() == 256 &&
            std::all_of(
                placeholder.begin(),
                placeholder.end(),
                [](unsigned char value) { return value == 0; }),
        "transparent placeholder should contain a complete zeroed RGBA image");
    Require(
        Stack::PixelBuffer::BuildTransparentRgbaPixels(32768, 32768).empty(),
        "oversized transparent placeholder should fail without allocating");

    const std::vector<unsigned char> rotationSource { 0, 1, 2, 3, 4, 5 };
    int rotatedWidth = 0;
    int rotatedHeight = 0;
    const std::vector<unsigned char> rotated =
        Stack::PixelBuffer::RotateInterleavedQuarterTurnsClockwise(
            rotationSource,
            2,
            3,
            1,
            1,
            rotatedWidth,
            rotatedHeight);
    Require(
        rotatedWidth == 3 && rotatedHeight == 2 &&
            rotated == std::vector<unsigned char>({ 1, 3, 5, 0, 2, 4 }),
        "checked pixel rotation should preserve the authored clockwise mapping");

    const std::vector<unsigned char> rejectedRotation =
        Stack::PixelBuffer::RotateInterleavedQuarterTurnsClockwise(
            std::vector<unsigned char>({ 0, 1, 2 }),
            2,
            2,
            1,
            1,
            rotatedWidth,
            rotatedHeight);
    Require(
        rejectedRotation.empty() &&
            rotatedWidth == 0 && rotatedHeight == 0,
        "checked pixel rotation should reject truncated input");
}

class SerializedLayerFixture : public LayerBase {
public:
    explicit SerializedLayerFixture(nlohmann::json value)
        : m_Value(std::move(value)) {}

    nlohmann::json Serialize() const override { return m_Value; }
    void Deserialize(const nlohmann::json& value) override { m_Value = value; }
    const char* GetDefaultName() const override { return "Serialized Layer Fixture"; }
    const char* GetCategory() const override { return "Test"; }
    void InitializeGL() override {}
    void Execute(unsigned int, int, int, FullscreenQuad&) override {}
    void RenderUI() override {}

private:
    nlohmann::json m_Value;
};

} // namespace

void LibraryManager::FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels) {
    if (height <= 1 ||
        !Stack::PixelBuffer::HasCompletePixelBuffer(
            pixels.size(), width, height, channels)) {
        return;
    }
    std::size_t rowBytes = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            width, 1, channels, rowBytes)) {
        return;
    }
    std::vector<unsigned char> scratch(rowBytes);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = pixels.data() + static_cast<std::size_t>(y) * rowBytes;
        unsigned char* bottom =
            pixels.data() + static_cast<std::size_t>(height - 1 - y) * rowBytes;
        std::copy(top, top + rowBytes, scratch.begin());
        std::copy(bottom, bottom + rowBytes, top);
        std::copy(scratch.begin(), scratch.end(), bottom);
    }
}

namespace {

int NodeId(const EditorNodeGraph::Node* node) {
    Require(node != nullptr, "node allocation failed");
    return node->id;
}

void TestGraphSliderDragSensitivityIsZoomIndependent() {
    constexpr float valueMin = -1.0f;
    constexpr float valueMax = 1.0f;
    constexpr float logicalTrackWidth = 160.0f;

    const float zoomedOutStep = ImGuiExtras::GraphSliderDragStepPerPixel(
        valueMin, valueMax, logicalTrackWidth * 0.25f, 0.25f, 1.0f);
    const float normalStep = ImGuiExtras::GraphSliderDragStepPerPixel(
        valueMin, valueMax, logicalTrackWidth, 1.0f, 1.0f);
    const float zoomedInStep = ImGuiExtras::GraphSliderDragStepPerPixel(
        valueMin, valueMax, logicalTrackWidth * 2.5f, 2.5f, 1.0f);
    Require(
        std::abs(zoomedOutStep - normalStep) < 0.000001f &&
            std::abs(zoomedInStep - normalStep) < 0.000001f,
        "node slider drag value-per-pixel should not change with graph zoom");

    const float fineStep = ImGuiExtras::GraphSliderDragStepPerPixel(
        valueMin, valueMax, logicalTrackWidth, 1.0f, 0.05f);
    Require(
        std::abs(fineStep - (normalStep * 0.05f)) < 0.000001f,
        "node slider drag sensitivity should scale the zoom-independent value-per-pixel");
}

EditorNodeGraph::ImagePayload TestImagePayload() {
    EditorNodeGraph::ImagePayload payload;
    payload.label = "Test Image";
    payload.width = 4;
    payload.height = 4;
    payload.channels = 4;
    payload.originalChannels = 4;
    payload.pixels.assign(4 * 4 * 4, 255);
    return payload;
}

void TestImagePayloadPreviewIsBounded() {
    constexpr int sourceWidth = 1600;
    constexpr int sourceHeight = 800;
    constexpr int channels = 4;
    std::vector<unsigned char> sourcePixels(
        static_cast<std::size_t>(sourceWidth) * sourceHeight * channels,
        0);
    for (std::size_t index = 3; index < sourcePixels.size(); index += channels) {
        sourcePixels[index] = 173;
    }
    sourcePixels[0] = 19;

    std::vector<unsigned char> previewPixels;
    int previewWidth = 0;
    int previewHeight = 0;
    int previewChannels = 0;
    EditorNodeGraph::BuildImagePayloadPreview(
        sourcePixels,
        sourceWidth,
        sourceHeight,
        channels,
        previewPixels,
        previewWidth,
        previewHeight,
        previewChannels,
        400);

    Require(previewWidth == 400 && previewHeight == 200, "image preview should preserve aspect ratio within its bound");
    Require(previewChannels == channels, "image preview should preserve channel count");
    Require(previewPixels.size() == static_cast<std::size_t>(previewWidth) * previewHeight * channels,
        "image preview should allocate exactly its bounded dimensions");
    Require(previewPixels[3] == 173, "image preview should preserve sampled alpha");
    Require(sourcePixels[0] == 19, "image preview generation must not mutate render-source pixels");

    previewPixels.assign(1, 99);
    previewWidth = 12;
    previewHeight = 12;
    previewChannels = 4;
    EditorNodeGraph::BuildImagePayloadPreview(
        std::vector<unsigned char>(15, 0),
        4,
        4,
        4,
        previewPixels,
        previewWidth,
        previewHeight,
        previewChannels,
        2);
    Require(
        previewPixels.empty() && previewWidth == 0 &&
            previewHeight == 0 && previewChannels == 0,
        "image preview should reject a truncated source payload");

    EditorNodeGraph::BuildImagePayloadPreview(
        std::vector<unsigned char>(20, 0),
        2,
        2,
        5,
        previewPixels,
        previewWidth,
        previewHeight,
        previewChannels,
        1);
    Require(
        previewPixels.empty() && previewChannels == 0,
        "image preview should reject unsupported channel counts");
    Require(
        EditorNodeGraph::EncodeImagePayloadPngForStorage(
            std::vector<unsigned char>(15, 0),
            4,
            1,
            4).empty(),
        "PNG serialization should reject a truncated source payload");
}

void TestInteractiveTaskPriorityRunsBeforeBlockedBackgroundWork() {
    Async::TaskSystem& tasks = Async::TaskSystem::Get();
    std::atomic<int> backgroundStarted { 0 };
    std::atomic<int> backgroundCompleted { 0 };
    std::atomic<bool> highPriorityCompleted { false };
    std::mutex mutex;
    std::condition_variable condition;
    bool releaseBackground = false;

    constexpr int kBackgroundTaskCount = 8;
    for (int index = 0; index < kBackgroundTaskCount; ++index) {
        tasks.Submit([&]() {
            backgroundStarted.fetch_add(1, std::memory_order_relaxed);
            condition.notify_all();
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&]() { return releaseBackground; });
            backgroundCompleted.fetch_add(1, std::memory_order_relaxed);
            condition.notify_all();
        });
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(1), [&]() {
            return backgroundStarted.load(std::memory_order_relaxed) >= 2;
        });
    }

    tasks.SubmitHighPriority([&]() {
        highPriorityCompleted.store(true, std::memory_order_relaxed);
        condition.notify_all();
    });

    bool highPriorityRanBeforeRelease = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        highPriorityRanBeforeRelease = condition.wait_for(lock, std::chrono::seconds(1), [&]() {
            return highPriorityCompleted.load(std::memory_order_relaxed);
        });
        releaseBackground = true;
    }
    condition.notify_all();

    bool backgroundReleasedCleanly = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        backgroundReleasedCleanly = condition.wait_for(lock, std::chrono::seconds(2), [&]() {
            return backgroundCompleted.load(std::memory_order_relaxed) == kBackgroundTaskCount;
        });
    }

    Require(highPriorityRanBeforeRelease,
        "interactive task should run while ordinary background work occupies the normal worker pool");
    Require(backgroundReleasedCleanly, "blocked background tasks should drain after release");
}

void TestTaskSystemShutdownJoinsRunningWorkAndDiscardsCompletions() {
    Async::TaskSystem& tasks = Async::TaskSystem::Get();
    std::mutex mutex;
    std::condition_variable condition;
    bool taskStarted = false;
    bool releaseTask = false;
    bool taskFinished = false;
    bool mainCompletionRan = false;

    Require(tasks.SubmitHighPriority([&]() {
        {
            std::unique_lock<std::mutex> lock(mutex);
            taskStarted = true;
            condition.notify_all();
            condition.wait(lock, [&]() { return releaseTask; });
        }
        taskFinished = true;
        Async::TaskSystem::Get().PostToMain([&]() {
            mainCompletionRan = true;
        });
    }), "task-system shutdown test task should be accepted");

    {
        std::unique_lock<std::mutex> lock(mutex);
        Require(
            condition.wait_for(lock, std::chrono::seconds(1), [&]() { return taskStarted; }),
            "task-system shutdown test task should start");
    }
    Require(!tasks.IsDrainedForShutdown(),
        "running task must be visible to the shutdown drain check");

    std::thread releaser([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        {
            std::lock_guard<std::mutex> lock(mutex);
            releaseTask = true;
        }
        condition.notify_all();
    });
    tasks.RequestStopDiscardQueued();
    Require(!tasks.Submit([]() {}),
        "task submission should report rejection after shutdown begins");
    Require(!tasks.SubmitHighPriority({}) && !tasks.PostToMain({}),
        "empty task submissions should report rejection");
    tasks.Shutdown();
    releaser.join();

    Require(taskFinished, "task-system shutdown must join already-running work");
    tasks.PumpMainThreadTasks();
    Require(!mainCompletionRan,
        "task-system shutdown must discard completions from canceled module work");

    std::atomic<bool> restartedTaskRan { false };
    Require(tasks.SubmitHighPriority([&]() {
        restartedTaskRan.store(true, std::memory_order_release);
        condition.notify_all();
    }), "task system should accept work after restart");
    {
        std::unique_lock<std::mutex> lock(mutex);
        Require(
            condition.wait_for(lock, std::chrono::seconds(1), [&]() {
                return restartedTaskRan.load(std::memory_order_acquire);
            }),
            "task system should restart cleanly after shutdown");
    }
    tasks.Shutdown();
}

std::string WriteTempTextFile(const std::string& stem, const std::string& extension, const std::string& contents) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (stem + "_" + std::to_string(std::rand()) + extension);
    std::ofstream out(path, std::ios::binary);
    Require(out.good(), "failed to open temporary test file");
    out << contents;
    Require(out.good(), "failed to write temporary test file");
    out.close();
    return path.string();
}

std::filesystem::path MakeTempDirectory(const std::string& stem) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (stem + "_" + std::to_string(std::rand()));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    ec.clear();
    std::filesystem::create_directories(path, ec);
    Require(!ec, "failed to create temporary test directory");
    return path;
}

void WriteRawWorkspaceTestFile(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Require(!ec, "failed to create temporary RAW parent directory");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(out.good(), "failed to open temporary RAW test file");
    out << "raw-placeholder";
    Require(out.good(), "failed to write temporary RAW test file");
}

void WriteRawWorkspaceJsonFile(const std::filesystem::path& path, const nlohmann::json& json) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Require(!ec, "failed to create temporary JSON parent directory");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(out.good(), "failed to open temporary JSON file");
    out << json.dump(2);
    Require(out.good(), "failed to write temporary JSON file");
}

nlohmann::json ReadRawWorkspaceJsonFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    Require(in.good(), "failed to open temporary JSON file for reading");
    nlohmann::json json;
    in >> json;
    Require(!in.fail(), "failed to parse temporary JSON file");
    return json;
}

void WriteRawWorkspaceBinaryFile(const std::filesystem::path& path, const std::string& contents) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Require(!ec, "failed to create temporary binary parent directory");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(out.good(), "failed to open temporary binary file");
    out << contents;
    Require(out.good(), "failed to write temporary binary file");
}

void AppendRawWorkspacePngBytes(void* context, void* data, int size) {
    auto* bytes = static_cast<std::string*>(context);
    bytes->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

void WriteRawWorkspaceTestPng(
    const std::filesystem::path& path,
    int width = 8,
    int height = 6) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        255u);
    std::string pngBytes;
    const int encoded = stbi_write_png_to_func(
        AppendRawWorkspacePngBytes,
        &pngBytes,
        width,
        height,
        4,
        pixels.data(),
        width * 4);
    Require(encoded != 0 && !pngBytes.empty(),
        "failed to encode temporary RAW thumbnail PNG");
    WriteRawWorkspaceBinaryFile(path, pngBytes);
}

nlohmann::json BuildTestThumbnailSignatureJson(const Stack::RawWorkspace::ThumbnailSignature& signature) {
    nlohmann::json value = nlohmann::json::object();
    value["schema"] = "stack.rawWorkspace.thumbnailSignature";
    value["schemaVersion"] = signature.schemaVersion;
    value["sourceRelativePath"] = signature.sourceRelativePath;
    value["sourceFileSizeBytes"] = signature.sourceFileSizeBytes;
    value["sourceModifiedTimeTicks"] = signature.sourceModifiedTimeTicks;
    value["sourceFingerprint"] = signature.sourceFingerprint.empty() ? nlohmann::json() : nlohmann::json(signature.sourceFingerprint);
    value["rawLoaderAlgorithmVersion"] = signature.rawLoaderAlgorithmVersion;
    value["neutralPreviewSettingsVersion"] = signature.neutralPreviewSettingsVersion;
    value["thumbnailVersion"] = signature.thumbnailVersion;
    value["maxDimension"] = signature.maxDimension;
    value["thumbnailWidth"] = 8;
    value["thumbnailHeight"] = 6;
    return value;
}

void TestScalarMaskCanUseLayerMath() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 200.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 1, { 400.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 600.0f, 0.0f }));

    Require(graph.CanConnectSockets(maskId, kMaskOutputSocketId, brightnessId, kImageInputSocketId),
        "mask scalar output should be allowed into a layer image input");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, brightnessId, kImageInputSocketId),
        "mask scalar output should connect into layer image input");
    Require(graph.IsScalarSocketStream(brightnessId, kImageOutputSocketId),
        "layer image output should keep scalar lineage when its image input is scalar");

    Require(graph.CanConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "scalar layer output should feed another layer image input");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "scalar layer output should connect to another layer image input");
    Require(graph.IsScalarSocketStream(contrastId, kImageOutputSocketId),
        "chained layer image output should keep scalar lineage");

    Require(graph.CanConnectSockets(contrastId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar image output should be allowed into scalar factor inputs");
    Require(graph.TryConnectSockets(contrastId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar image output should connect to scalar factor inputs");
}

void TestChannelAverageStaysScalarThroughContrastMaskWorkflow() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int splitId = NodeId(graph.AddChannelSplitNode({ 200.0f, 0.0f }));
    const int averageId = NodeId(graph.AddDataMathNode(DataMathMode::Average, { 400.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 0, { 600.0f, 0.0f }));
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 1, { 800.0f, 0.0f }));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, splitId, kImageInputSocketId),
        "source image should feed Channel Split");
    const char* channels[] = { "r", "g", "b", "a" };
    for (int inputIndex = 0; inputIndex < 4; ++inputIndex) {
        Require(graph.TryConnectSockets(
                    splitId,
                    channels[inputIndex],
                    averageId,
                    DataMathInputSocketId(inputIndex)),
            "Channel Split outputs should feed scalar Average inputs");
    }
    Require(graph.IsScalarSocketStream(averageId, kImageOutputSocketId),
        "channel Average should output one scalar field");

    Require(graph.TryConnectSockets(averageId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "channel Average should feed Contrast as scalar image data, not as Contrast's mask");
    Require(graph.IsScalarSocketStream(contrastId, kImageOutputSocketId),
        "Contrast should preserve the averaged scalar field");

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, brightnessId, kImageInputSocketId),
        "Brightness should keep the original image on its image input");
    Require(graph.CanConnectSockets(contrastId, kImageOutputSocketId, brightnessId, kMaskInputSocketId),
        "scalar Contrast output should connect directly to Brightness's mask input");
    Require(!graph.CanInsertImageToScalarExtractor(
                contrastId,
                kImageOutputSocketId,
                brightnessId,
                kMaskInputSocketId),
        "scalar Contrast output must not require a luminance extractor");
    Require(graph.TryConnectSockets(contrastId, kImageOutputSocketId, brightnessId, kMaskInputSocketId),
        "scalar Contrast output should become the Brightness mask");
    Require(graph.HasLink(contrastId, kImageOutputSocketId, brightnessId, kMaskInputSocketId),
        "Brightness mask should link directly from Contrast with no Luminance Mask node");
}

void TestChannelRoleConnectsDirectlyToMaskInputs() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(
        graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int splitId = NodeId(
        graph.AddChannelSplitNode({ 200.0f, 120.0f }));
    const int contrastId = NodeId(
        graph.AddLayerNode(LayerType::Contrast, 0, { 400.0f, 0.0f }));
    const int outputId = NodeId(
        graph.AddOutputNode({ 620.0f, 0.0f }, true));

    std::string error;
    Require(
        graph.TryConnectSockets(
            imageId,
            kImageOutputSocketId,
            splitId,
            kImageInputSocketId,
            &error) &&
        graph.TryConnectSockets(
            imageId,
            kImageOutputSocketId,
            contrastId,
            kImageInputSocketId,
            &error),
        "the Channel-to-Mask regression fixture should connect its image paths");
    Require(
        graph.CanConnectSockets(
            splitId,
            "r",
            contrastId,
            kMaskInputSocketId,
            nullptr,
            &error),
        "a Channel Split red Channel should be accepted by Contrast's Mask input");
    Require(
        graph.TryConnectSockets(
            splitId,
            "r",
            contrastId,
            kMaskInputSocketId,
            &error) &&
        graph.TryConnectSockets(
            contrastId,
            kImageOutputSocketId,
            outputId,
            kImageInputSocketId,
            &error),
        error.empty()
            ? "the red Channel should author as Contrast's Mask"
            : error.c_str());

    const Link* maskLink = graph.FindInputLink(
        contrastId,
        kMaskInputSocketId);
    Require(
        maskLink &&
        maskLink->fromNodeId == splitId &&
        maskLink->fromSocketId == "r" &&
        graph.IsRenderLink(*maskLink),
        "the accepted Channel-role link should participate in renderer scheduling");
    Require(
        graph.IsOutputConnected() && graph.Validate().valid,
        "the authored Image -> Split R -> Contrast Mask graph should be valid and complete");

    const nlohmann::json saved = SerializeGraphPayload(
        nlohmann::json::array({
            {
                { "type", "Contrast" },
                { "contrast", 0.0f }
            }
        }),
        graph);
    Graph restored;
    DeserializeGraphPayload(saved, restored, 1, {}, 0, 0, 0);
    const Link* restoredMaskLink = restored.FindInputLink(
        contrastId,
        kMaskInputSocketId);
    Require(
        restoredMaskLink &&
        restoredMaskLink->fromNodeId == splitId &&
        restoredMaskLink->fromSocketId == "r" &&
        restored.IsRenderLink(*restoredMaskLink) &&
        restored.IsOutputConnected() &&
        restored.Validate().valid,
        "the Channel-to-Mask link should remain valid and renderable after save/reload");

    const int incompatibleSplitId = NodeId(
        restored.AddChannelSplitNode({ 820.0f, 120.0f }));
    error.clear();
    Require(
        !restored.CanConnectSockets(
            splitId,
            "r",
            incompatibleSplitId,
            kImageInputSocketId,
            nullptr,
            &error) &&
        error.find("Channel") != std::string::npos &&
        error.find("Frequency") == std::string::npos,
        "an ordinary invalid Channel link should report Channel guidance, not a frequency error");
}

void TestToneCurveInheritsInputScenePath() {
    using namespace EditorNodeGraph;

    Graph displayGraph;
    const int imageId = NodeId(displayGraph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int displayToneCurveId = NodeId(
        displayGraph.AddLayerNode(LayerType::ToneCurve, 0, { 220.0f, 0.0f }));
    Require(displayGraph.TryConnectSockets(
                imageId,
                kImageOutputSocketId,
                displayToneCurveId,
                kImageInputSocketId),
        "display-referred image should feed Tone Curve");
    const ScenePathInfo displayTonePath = AnalyzeScenePath(displayGraph, displayToneCurveId);
    Require(!displayTonePath.sceneReferred,
        "Tone Curve should preserve display-referred input instead of forcing a View Transform");
    Require(!displayTonePath.hasViewTransform,
        "display-referred Tone Curve path should not claim a View Transform is present");

    Graph rawGraph;
    const int rawSourceId = NodeId(rawGraph.AddRawSourceNode(RawSourcePayload{}, { 0.0f, 0.0f }));
    const int rawDecodeId = NodeId(rawGraph.AddRawDecodeNode(RawDecodePayload{}, { 220.0f, 0.0f }));
    const int rawToneCurveId = NodeId(
        rawGraph.AddLayerNode(LayerType::ToneCurve, 0, { 440.0f, 0.0f }));
    const int viewTransformId = NodeId(
        rawGraph.AddLayerNode(LayerType::ViewTransform, 1, { 660.0f, 0.0f }));
    Require(rawGraph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "RAW source should feed RAW Decode");
    Require(rawGraph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, rawToneCurveId, kImageInputSocketId),
        "RAW Decode should feed Tone Curve");
    const ScenePathInfo rawTonePath = AnalyzeScenePath(rawGraph, rawToneCurveId);
    Require(rawTonePath.sceneReferred,
        "Tone Curve should retain the scene-referred state of a RAW Decode input");
    Require(!rawTonePath.hasViewTransform,
        "RAW Tone Curve should still need a downstream View Transform");

    Require(rawGraph.TryConnectSockets(rawToneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "RAW Tone Curve should feed View Transform");
    const ScenePathInfo transformedRawPath = AnalyzeScenePath(rawGraph, viewTransformId);
    Require(transformedRawPath.sceneReferred && transformedRawPath.hasViewTransform,
        "View Transform should satisfy display mapping for a scene-referred Tone Curve path");

    RawDevelopmentPayload rawDevelopmentPayload;
    rawDevelopmentPayload.recipe =
        Stack::RawRecipe::MakeDefaultRecipe("scene-path-test.dng");
    Graph builtInViewGraph;
    const int builtInRawId = NodeId(
        builtInViewGraph.AddRawDevelopmentNode(
            rawDevelopmentPayload,
            { 0.0f, 0.0f }));
    const int builtInFlipId = NodeId(
        builtInViewGraph.AddLayerNode(
            LayerType::Flip,
            0,
            { 220.0f, 0.0f }));
    Require(builtInViewGraph.TryConnectSockets(
                builtInRawId,
                kImageOutputSocketId,
                builtInFlipId,
                kImageInputSocketId),
        "RAW Development should feed Flip with its built-in View Transform enabled");
    const ScenePathInfo builtInViewPath =
        AnalyzeScenePath(builtInViewGraph, builtInFlipId);
    Require(builtInViewPath.sceneReferred && builtInViewPath.hasViewTransform,
        "RAW Development built-in View Transform should remain satisfied after Flip");
    const int duplicateBuiltInViewId = NodeId(
        builtInViewGraph.AddLayerNode(
            LayerType::ViewTransform,
            1,
            { 440.0f, 0.0f }));
    std::string duplicateViewError;
    Require(!builtInViewGraph.TryConnectSockets(
                builtInFlipId,
                kImageOutputSocketId,
                duplicateBuiltInViewId,
                kImageInputSocketId,
                &duplicateViewError) &&
            duplicateViewError.find("already contains a View Transform") !=
                std::string::npos,
        "a graph View Transform should not be allowed after RAW Development's built-in transform");

    rawDevelopmentPayload.recipe.viewTransform.layerJson["enabled"] = false;
    Graph externalViewGraph;
    const int externalRawId = NodeId(
        externalViewGraph.AddRawDevelopmentNode(
            rawDevelopmentPayload,
            { 0.0f, 0.0f }));
    const int externalFlipId = NodeId(
        externalViewGraph.AddLayerNode(
            LayerType::Flip,
            0,
            { 220.0f, 0.0f }));
    Require(externalViewGraph.TryConnectSockets(
                externalRawId,
                kImageOutputSocketId,
                externalFlipId,
                kImageInputSocketId),
        "scene-linear RAW Development should feed Flip");
    const ScenePathInfo externalViewPath =
        AnalyzeScenePath(externalViewGraph, externalFlipId);
    Require(externalViewPath.sceneReferred && !externalViewPath.hasViewTransform,
        "disabling RAW Development View Transform should leave Flip scene-linear and require an external View Transform");
    const int externalViewTransformId = NodeId(
        externalViewGraph.AddLayerNode(
            LayerType::ViewTransform,
            1,
            { 440.0f, 0.0f }));
    Require(externalViewGraph.TryConnectSockets(
                externalFlipId,
                kImageOutputSocketId,
                externalViewTransformId,
                kImageInputSocketId),
        "a graph View Transform should be allowed after RAW Display Mapping is disabled");

    Graph multiFrameViewGraph;
    MultiFrameDenoisePayload denoisePayload;
    denoisePayload.internalViewTransformEnabled = true;
    const int denoiseId = NodeId(
        multiFrameViewGraph.AddMultiFrameDenoiseNode(
            denoisePayload,
            { 0.0f, 0.0f }));
    const int denoiseDuplicateViewId = NodeId(
        multiFrameViewGraph.AddLayerNode(
            LayerType::ViewTransform,
            0,
            { 220.0f, 0.0f }));
    Require(!multiFrameViewGraph.TryConnectSockets(
                denoiseId,
                kImageOutputSocketId,
                denoiseDuplicateViewId,
                kImageInputSocketId),
        "MFD should reject a second View Transform while its post-merge RAW View is active");

    Graph hdrViewGraph;
    MultiFrameHdrPayload hdrPayload;
    hdrPayload.internalViewTransformEnabled = true;
    const int hdrId = NodeId(
        hdrViewGraph.AddMultiFrameHdrNode(
            hdrPayload,
            { 0.0f, 0.0f }));
    const int hdrDuplicateViewId = NodeId(
        hdrViewGraph.AddLayerNode(
            LayerType::ViewTransform,
            0,
            { 220.0f, 0.0f }));
    Require(!hdrViewGraph.TryConnectSockets(
                hdrId,
                kImageOutputSocketId,
                hdrDuplicateViewId,
                kImageInputSocketId),
        "HDR should reject a second View Transform while its post-merge RAW View is active");
}

void TestFullImageStillCannotTargetScalarInput() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 220.0f, 0.0f }));

    std::string error;
    Require(!graph.CanConnectSockets(imageId, kImageOutputSocketId, mixId, kMixFactorSocketId, nullptr, &error),
        "full image output should not connect directly to scalar factor input");
    Require(!error.empty(), "rejected full-image-to-scalar connection should explain why");
    Require(graph.CanInsertImageToScalarExtractor(imageId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "full image output should report that an explicit scalar extractor can make the connection valid");
    const std::size_t nodeCountBeforeRejectedDrop = graph.GetNodes().size();
    Require(!graph.TryConnectSockets(imageId, kImageOutputSocketId, mixId, kMixFactorSocketId, &error),
        "full image to scalar factor should reject instead of inserting an extractor");
    Require(graph.GetNodes().size() == nodeCountBeforeRejectedDrop,
        "rejected full image to scalar drop must not create a node");
    Require(error.find("explicit Luminance Mask") != std::string::npos,
        "rejected full image to scalar drop should explain the explicit extraction path");

    const int luminanceId = NodeId(graph.AddImageToMaskNode(ImageToMaskKind::Luminance, { 110.0f, 90.0f }));
    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, luminanceId, kImageToMaskInputSocketId),
        "explicit Luminance Mask should accept the full image");
    Require(graph.TryConnectSockets(luminanceId, kMaskOutputSocketId, mixId, kMixFactorSocketId),
        "explicit Luminance Mask output should feed the scalar factor");
}

void TestOutputChannelNormalization() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int splitId = NodeId(graph.AddChannelSplitNode({ 220.0f, 0.0f }));
    const int maskId = NodeId(graph.AddMaskGeneratorNode(
        MaskGeneratorKind::Solid, { 220.0f, 160.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 440.0f, 0.0f }, true));
    const int lutId = NodeId(graph.AddLutNode({}, { 440.0f, 160.0f }));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, splitId, kImageInputSocketId),
        "image should connect to channel split input");
    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "test setup should connect a full image to output");

    std::string normalized;
    Require(graph.CanConnectSockets(splitId, "r", outputId, kImageInputSocketId, &normalized),
        "Channel should be accepted by the stable Output Result input");
    Require(normalized == kImageInputSocketId,
        "Output Channel connections must remain on the stable Result input");
    Require(graph.TryConnectSockets(splitId, "r", outputId, kImageInputSocketId),
        "Channel should replace the previous Image on Output Result");
    Require(graph.HasLink(
            splitId,
            "r",
            outputId,
            kImageInputSocketId),
        "Output Channel link should be stored directly on Result");
    Require(graph.IsOutputChannelInspection(outputId),
        "Output should classify the connected Result as Channel inspection");
    Require(!graph.CanConnectSockets(
            maskId,
            kMaskOutputSocketId,
            outputId,
            kImageInputSocketId),
        "Mask must not enter the exact Image-or-Channel Output union");
    SocketDefinition outputSocket;
    Require(graph.FindSocket(
            outputId,
            kImageInputSocketId,
            &outputSocket) &&
            outputSocket.type == SocketType::ImageOrChannel &&
            outputSocket.label == "Result · Image or Channel",
        "Output v2 exposes one stable, explicitly labelled union socket");

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, lutId, kImageInputSocketId),
        "test setup should connect a full image to LUT");
    Require(graph.TryConnectSockets(splitId, "g", lutId, kImageInputSocketId),
        "channel output should connect to a normalized LUT channel input");
    Require(graph.HasLink(splitId, "g", lutId, "g"),
        "normalized LUT channel link should be stored on the G socket");
    Require(graph.FindInputLink(lutId, kImageInputSocketId) == nullptr,
        "connecting a LUT channel should remove the mutually exclusive full-image input");
}

void TestOutputV2Persistence() {
    using namespace EditorNodeGraph;
    using Stack::NodeMath::OutputChannelViewMode;

    Graph graph;
    const int imageId = NodeId(
        graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int splitId = NodeId(
        graph.AddChannelSplitNode({ 220.0f, 0.0f }));
    const int outputId = NodeId(
        graph.AddOutputNode({ 440.0f, 0.0f }, true));
    Require(
        graph.TryConnectSockets(
            imageId,
            kImageOutputSocketId,
            splitId,
            kImageInputSocketId) &&
        graph.TryConnectSockets(
            splitId,
            "g",
            outputId,
            kImageInputSocketId),
        "Output v2 persistence fixture should connect");
    graph.FindNode(outputId)->outputSettings.channelViewMode =
        OutputChannelViewMode::Blue;

    nlohmann::json saved =
        SerializeGraphPayload(nlohmann::json::array(), graph);
    Require(
        saved["nodeGraph"].value("version", 0) == 9,
        "Output v2 persistence uses the current graph schema");
    Graph loaded;
    DeserializeGraphPayload(saved, loaded, 0, {}, 0, 0, 0);
    const Node* loadedOutput = loaded.FindNode(outputId);
    Require(
        loadedOutput &&
            loadedOutput->definitionResolved &&
            loadedOutput->definitionVersion == "2.0.0" &&
            loadedOutput->outputSettings.channelViewMode ==
                OutputChannelViewMode::Blue &&
            loaded.HasLink(
                splitId,
                "g",
                outputId,
                kImageInputSocketId),
        "Output v2 mode and stable Channel link survive save/load");

}

void TestPartialImageComponentPresenceSurvivesGraphRoundTrip() {
    using namespace EditorNodeGraph;
    using namespace Stack::NodeMath;

    Graph graph;
    const int imageId = NodeId(
        graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int splitId = NodeId(
        graph.AddChannelSplitNode({ 220.0f, 0.0f }));
    const int combineId = NodeId(
        graph.AddChannelCombineNode({ 440.0f, 0.0f }));
    const int outputId = NodeId(
        graph.AddOutputNode({ 660.0f, 0.0f }, true));

    Require(
        graph.TryConnectSockets(
            imageId,
            kImageOutputSocketId,
            splitId,
            kImageInputSocketId) &&
        graph.TryConnectSockets(splitId, "r", combineId, "r") &&
        graph.TryConnectSockets(splitId, "b", combineId, "b") &&
        graph.TryConnectSockets(
            combineId,
            kImageOutputSocketId,
            outputId,
            kImageInputSocketId),
        "partial R+B Image graph should connect exactly");

    const ImageComponentSet expected = MakeImageComponentSet({
        ImageComponent::Red,
        ImageComponent::Blue
    });
    const std::string expectedIdentity = DescriptorContentIdentity(
        MakePartialColorImageDescriptor(
            expected,
            "image.combine.v2"));

    const nlohmann::json saved =
        SerializeGraphPayload(nlohmann::json::array(), graph);
    Graph loaded;
    DeserializeGraphPayload(saved, loaded, 0, {}, 0, 0, 0);

    Require(
        loaded.HasLink(splitId, "r", combineId, "r") &&
        loaded.HasLink(splitId, "b", combineId, "b") &&
        loaded.FindInputLink(combineId, "g") == nullptr &&
        loaded.FindInputLink(combineId, "a") == nullptr,
        "graph round trip should preserve connected R+B and absent G+A");

    ImageComponentSet loadedComponents;
    for (const Link& link : loaded.GetLinks()) {
        if (link.toNodeId != combineId) {
            continue;
        }
        if (link.toSocketId == "r") {
            AddImageComponent(loadedComponents, ImageComponent::Red);
        } else if (link.toSocketId == "g") {
            AddImageComponent(loadedComponents, ImageComponent::Green);
        } else if (link.toSocketId == "b") {
            AddImageComponent(loadedComponents, ImageComponent::Blue);
        } else if (link.toSocketId == "a") {
            AddImageComponent(loadedComponents, ImageComponent::Alpha);
        }
    }
    const ValueDescriptor loadedDescriptor =
        MakePartialColorImageDescriptor(
            loadedComponents,
            "image.combine.v2");
    Require(
        loadedComponents == expected &&
        ValidateDescriptor(loadedDescriptor).empty() &&
        DescriptorContentIdentity(loadedDescriptor) == expectedIdentity,
        "loaded topology should reproduce the exact partial-Image descriptor identity");
}

void TestCompletedChainsSplitSharedUpstreamAcrossOutputs() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 220.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 1, { 440.0f, -80.0f }));
    const int saturationId = NodeId(graph.AddLayerNode(LayerType::Saturation, 2, { 440.0f, 80.0f }));
    const int outputAId = NodeId(graph.AddOutputNode({ 660.0f, -80.0f }, true));
    const int outputBId = NodeId(graph.AddOutputNode({ 660.0f, 80.0f }, true));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, brightnessId, kImageInputSocketId),
        "source should connect to shared upstream brightness node");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "shared upstream brightness node should feed first branch");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, saturationId, kImageInputSocketId),
        "shared upstream brightness node should feed second branch");
    Require(graph.TryConnectSockets(contrastId, kImageOutputSocketId, outputAId, kImageInputSocketId),
        "first branch should connect to first output");
    Require(graph.TryConnectSockets(saturationId, kImageOutputSocketId, outputBId, kImageInputSocketId),
        "second branch should connect to second output");

    const std::vector<CompletedChainInfo> chains = graph.GetCompletedChains();
    Require(chains.size() == 2,
        "split graph with two output nodes should produce two completed output chains");

    const CompletedChainInfo* outputAChain = nullptr;
    const CompletedChainInfo* outputBChain = nullptr;
    for (const CompletedChainInfo& chain : chains) {
        if (chain.outputNodeId == outputAId) {
            outputAChain = &chain;
        } else if (chain.outputNodeId == outputBId) {
            outputBChain = &chain;
        }
    }

    Require(outputAChain != nullptr,
        "completed chains should include a row anchored to the first output node");
    Require(outputBChain != nullptr,
        "completed chains should include a row anchored to the second output node");
    Require(outputAChain->sourceNodeId == imageId && outputBChain->sourceNodeId == imageId,
        "both output chains should keep the shared source node");
    Require(outputAChain->terminalNodeId == contrastId,
        "first output chain should terminate at the first branch node");
    Require(outputBChain->terminalNodeId == saturationId,
        "second output chain should terminate at the second branch node");
    Require(std::find(outputAChain->nodeIds.begin(), outputAChain->nodeIds.end(), brightnessId) != outputAChain->nodeIds.end(),
        "first output chain should include the shared upstream node");
    Require(std::find(outputBChain->nodeIds.begin(), outputBChain->nodeIds.end(), brightnessId) != outputBChain->nodeIds.end(),
        "second output chain should include the shared upstream node");
}

void TestSplitAdjustmentAnimatableRegistryCoverage() {
    using namespace EditorNodeGraph;
    using Stack::Timeline::AnimatableParameterDefinition;

    Graph graph;
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 0.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 1, { 0.0f, 0.0f }));
    const int saturationId = NodeId(graph.AddLayerNode(LayerType::Saturation, 2, { 0.0f, 0.0f }));
    const int warmthId = NodeId(graph.AddLayerNode(LayerType::Warmth, 3, { 0.0f, 0.0f }));
    const int sharpenId = NodeId(graph.AddLayerNode(LayerType::Sharpen, 4, { 0.0f, 0.0f }));

    auto requireSingleParameter = [&](int nodeId, const char* expectedParameterId, const char* expectedStorageKey) {
        const Node* node = graph.FindNode(nodeId);
        Require(node != nullptr, "animatable registry test node should exist");
        const std::vector<AnimatableParameterDefinition> parameters =
            Stack::Timeline::CollectAnimatableParametersForNode(*node);
        Require(parameters.size() == 1, "single-control split adjustment node should expose one animatable parameter");
        Require(parameters[0].target.nodeId == nodeId, "animatable parameter should target the source node id");
        Require(parameters[0].target.parameterId == expectedParameterId, "animatable parameter id should be stable");
        Require(parameters[0].storageKey == expectedStorageKey, "animatable parameter should map to the serialized layer key");
    };

    requireSingleParameter(brightnessId, "layer.brightness", "brightness");
    requireSingleParameter(contrastId, "layer.contrast", "contrast");
    requireSingleParameter(saturationId, "layer.saturation", "saturation");
    requireSingleParameter(warmthId, "layer.warmth", "warmth");

    const Node* sharpenNode = graph.FindNode(sharpenId);
    Require(sharpenNode != nullptr, "sharpen node should exist");
    const std::vector<AnimatableParameterDefinition> sharpenParameters =
        Stack::Timeline::CollectAnimatableParametersForNode(*sharpenNode);
    Require(sharpenParameters.size() == 2, "sharpen node should expose both adjustable values");
    Require(sharpenParameters[0].target.parameterId == "layer.sharpening" &&
            sharpenParameters[0].storageKey == "sharpening",
        "sharpen amount should use stable parameter and serialized storage ids");
    Require(sharpenParameters[1].target.parameterId == "layer.sharpenThreshold" &&
            sharpenParameters[1].storageKey == "sharpenThreshold",
        "sharpen threshold should use stable parameter and serialized storage ids");
}

void TestBlurFamilyAnimatableRegistryCoverage() {
    using namespace EditorNodeGraph;
    using Stack::Timeline::AnimatableParameterDefinition;

    Graph graph;
    const int boxBlurId = NodeId(graph.AddLayerNode(LayerType::BoxBlur, 0, { 0.0f, 0.0f }));
    const int gaussianBlurId = NodeId(graph.AddLayerNode(LayerType::GaussianBlur, 1, { 0.0f, 0.0f }));
    const int hankelBlurId = NodeId(graph.AddLayerNode(LayerType::HankelBlur, 2, { 0.0f, 0.0f }));
    const int tiltShiftBlurId = NodeId(graph.AddLayerNode(LayerType::TiltShiftBlur, 3, { 0.0f, 0.0f }));

    auto requireSingleBlurAmount = [&](int nodeId) {
        const Node* node = graph.FindNode(nodeId);
        Require(node != nullptr, "blur-family registry test node should exist");
        const std::vector<AnimatableParameterDefinition> parameters =
            Stack::Timeline::CollectAnimatableParametersForNode(*node);
        Require(parameters.size() == 1, "simple blur node should expose one animatable blur amount");
        Require(parameters[0].target.nodeId == nodeId, "blur amount parameter should target the source node id");
        Require(parameters[0].target.parameterId == "layer.blurAmount",
            "simple blur amount should use a stable parameter id");
        Require(parameters[0].storageKey == "amount",
            "simple blur amount should map to the serialized amount key");
        Require(std::abs(parameters[0].minValue - 0.5f) < 0.0001f &&
                std::abs(parameters[0].maxValue - 16.0f) < 0.0001f,
            "simple blur amount should preserve the node slider range");
    };

    requireSingleBlurAmount(boxBlurId);
    requireSingleBlurAmount(gaussianBlurId);

    const Node* hankelNode = graph.FindNode(hankelBlurId);
    Require(hankelNode != nullptr, "hankel blur node should exist");
    SerializedLayerFixture hankelLayer({
        { "type", "HankelBlur" },
        { "radius", 12.5f },
        { "quality", 10.0f },
        { "intensity", 0.40f }
    });
    const std::vector<AnimatableParameterDefinition> hankelParameters =
        Stack::Timeline::CollectAnimatableParametersForNode(*hankelNode, &hankelLayer);
    Require(hankelParameters.size() == 3,
        "hankel blur should expose radius, quality, and intensity sliders");
    Require(hankelParameters[0].target.parameterId == "layer.hankelRadius" &&
            hankelParameters[0].storageKey == "radius" &&
            hankelParameters[0].hasCurrentValue &&
            std::abs(hankelParameters[0].currentValue - 12.5f) < 0.0001f,
        "hankel radius should expose serialized current value readback");
    Require(hankelParameters[1].target.parameterId == "layer.hankelQuality" &&
            hankelParameters[1].storageKey == "quality" &&
            std::abs(hankelParameters[1].minValue - 2.0f) < 0.0001f &&
            std::abs(hankelParameters[1].maxValue - 16.0f) < 0.0001f,
        "hankel quality should expose the quality slider range");
    Require(hankelParameters[2].target.parameterId == "layer.hankelIntensity" &&
            hankelParameters[2].storageKey == "intensity" &&
            std::abs(hankelParameters[2].currentValue - 0.40f) < 0.0001f,
        "hankel intensity should expose serialized current value readback");

    const Node* tiltShiftNode = graph.FindNode(tiltShiftBlurId);
    Require(tiltShiftNode != nullptr, "tilt-shift blur node should exist");
    const std::vector<AnimatableParameterDefinition> tiltShiftParameters =
        Stack::Timeline::CollectAnimatableParametersForNode(*tiltShiftNode);
    Require(tiltShiftParameters.size() == 6,
        "tilt-shift blur should expose its discrete blur type and five float sliders");
    Require(tiltShiftParameters[0].target.parameterId == "layer.tiltShiftBlurType" &&
            tiltShiftParameters[0].storageKey == "blurType",
        "tilt-shift blur type should map to the serialized blurType key");
    Require(tiltShiftParameters[1].target.parameterId == "layer.tiltShiftBlurStrength" &&
            tiltShiftParameters[1].storageKey == "amount",
        "tilt-shift blur strength should map to the serialized amount key");
    Require(tiltShiftParameters[2].target.parameterId == "layer.tiltShiftFocusRadius" &&
            tiltShiftParameters[2].storageKey == "focusRadius",
        "tilt-shift focus radius should map to the serialized focus radius key");
    Require(tiltShiftParameters[3].target.parameterId == "layer.tiltShiftFocusFalloff" &&
            tiltShiftParameters[3].storageKey == "transition",
        "tilt-shift focus falloff should map to the serialized transition key");
    Require(tiltShiftParameters[4].target.parameterId == "layer.tiltShiftFocusX" &&
            tiltShiftParameters[4].storageKey == "centerX",
        "tilt-shift focus X should map to the serialized centerX key");
    Require(tiltShiftParameters[5].target.parameterId == "layer.tiltShiftFocusY" &&
            tiltShiftParameters[5].storageKey == "centerY",
        "tilt-shift focus Y should map to the serialized centerY key");
}

void TestNonRawNumericLayerAnimatableRegistryCoverage() {
    using namespace EditorNodeGraph;
    using Stack::Timeline::AnimatableParameterDefinition;
    using Stack::Timeline::AnimatableValueType;

    struct ExpectedParameter {
        const char* parameterId;
        const char* storageKey;
        AnimatableValueType valueType;
    };

    auto requireParameters = [](LayerType layerType, std::initializer_list<ExpectedParameter> expected) {
        Graph graph;
        const int nodeId = NodeId(graph.AddLayerNode(layerType, 0, { 0.0f, 0.0f }));
        const Node* node = graph.FindNode(nodeId);
        Require(node != nullptr, "numeric registry test node should exist");

        const std::vector<AnimatableParameterDefinition> parameters =
            Stack::Timeline::CollectAnimatableParametersForNode(*node);
        Require(parameters.size() == expected.size(),
            "numeric layer node should expose exactly the expected numeric parameters");

        std::size_t index = 0;
        for (const ExpectedParameter& item : expected) {
            Require(parameters[index].target.nodeId == nodeId,
                "numeric animatable parameter should target the source node id");
            Require(parameters[index].target.parameterId == item.parameterId,
                "numeric animatable parameter id should be stable");
            Require(parameters[index].storageKey == item.storageKey,
                "numeric animatable parameter should map to the serialized layer key");
            Require(parameters[index].valueType == item.valueType,
                "numeric animatable parameter should preserve float/integer metadata");
            ++index;
        }
    };

    auto requireCorruption = [&](LayerType layerType) {
        requireParameters(layerType, {
            { "layer.corruptionScale", "resScale", AnimatableValueType::Float }
        });
    };
    requireCorruption(LayerType::JpegBlocks);
    requireCorruption(LayerType::Pixelation);
    requireCorruption(LayerType::ColorBleed);

    auto requireCompression = [&](LayerType layerType) {
        requireParameters(layerType, {
            { "layer.compressionQuality", "quality", AnimatableValueType::Float },
            { "layer.compressionBlockSize", "blockSize", AnimatableValueType::Float },
            { "layer.compressionBlend", "blend", AnimatableValueType::Float },
            { "layer.compressionIterations", "iterations", AnimatableValueType::Integer }
        });
    };
    requireCompression(LayerType::DctCompression);
    requireCompression(LayerType::ChromaSubsampleCompression);
    requireCompression(LayerType::WaveletCompression);

    requireParameters(LayerType::NonLocalMeansDenoise, {
        { "layer.denoiseSearchRadius", "searchRadius", AnimatableValueType::Integer },
        { "layer.denoisePatchRadius", "patchRadius", AnimatableValueType::Integer },
        { "layer.denoiseFilterStrength", "h", AnimatableValueType::Float },
        { "layer.denoiseBlendStrength", "strength", AnimatableValueType::Float }
    });
    requireParameters(LayerType::MedianDenoise, {
        { "layer.denoiseSearchRadius", "searchRadius", AnimatableValueType::Integer },
        { "layer.denoiseBlendStrength", "strength", AnimatableValueType::Float }
    });
    requireParameters(LayerType::MeanDenoise, {
        { "layer.denoiseSearchRadius", "searchRadius", AnimatableValueType::Integer },
        { "layer.denoiseBlendStrength", "strength", AnimatableValueType::Float }
    });

    requireParameters(LayerType::EdgeOverlay, {
        { "layer.edgeBlend", "blend", AnimatableValueType::Float },
        { "layer.edgeStrength", "strength", AnimatableValueType::Float },
        { "layer.edgeTolerance", "tolerance", AnimatableValueType::Float }
    });
    requireParameters(LayerType::EdgeSaturationMask, {
        { "layer.edgeBlend", "blend", AnimatableValueType::Float },
        { "layer.edgeStrength", "strength", AnimatableValueType::Float },
        { "layer.edgeTolerance", "tolerance", AnimatableValueType::Float },
        { "layer.edgeForegroundSaturation", "foregroundSaturation", AnimatableValueType::Float },
        { "layer.edgeBackgroundSaturation", "backgroundSaturation", AnimatableValueType::Float },
        { "layer.edgeBloomSpread", "bloomSpread", AnimatableValueType::Float },
        { "layer.edgeBloomSmoothness", "bloomSmoothness", AnimatableValueType::Float }
    });

    requireParameters(LayerType::Crop, {
        { "layer.cropLeft", "cropLeft", AnimatableValueType::Float },
        { "layer.cropRight", "cropRight", AnimatableValueType::Float },
        { "layer.cropTop", "cropTop", AnimatableValueType::Float },
        { "layer.cropBottom", "cropBottom", AnimatableValueType::Float }
    });
    requireParameters(LayerType::Rotate, {
        { "layer.rotation", "rotation", AnimatableValueType::Float }
    });

    auto requireDistortion = [&](LayerType layerType) {
        requireParameters(layerType, {
            { "layer.distortionIntensity", "intensity", AnimatableValueType::Float },
            { "layer.distortionPhase", "phase", AnimatableValueType::Float },
            { "layer.distortionScale", "scale", AnimatableValueType::Float }
        });
    };
    requireDistortion(LayerType::HeatwaveDistortion);
    requireDistortion(LayerType::RippleDistortion);

    Graph graph;
    const int dctId = NodeId(graph.AddLayerNode(LayerType::DctCompression, 0, { 0.0f, 0.0f }));
    const Node* dctNode = graph.FindNode(dctId);
    Require(dctNode != nullptr, "compression readback test node should exist");
    SerializedLayerFixture dctLayer({
        { "type", "DctCompression" },
        { "quality", 42.0f },
        { "blockSize", 12.0f },
        { "blend", 64.0f },
        { "iterations", 7 }
    });
    const std::vector<AnimatableParameterDefinition> dctParameters =
        Stack::Timeline::CollectAnimatableParametersForNode(*dctNode, &dctLayer);
    Require(dctParameters.size() == 4, "compression readback should expose all numeric parameters");
    Require(dctParameters[0].hasCurrentValue && std::abs(dctParameters[0].currentValue - 42.0f) < 0.0001f,
        "compression quality should read serialized float current value");
    Require(dctParameters[3].hasCurrentValue && std::abs(dctParameters[3].currentValue - 7.0f) < 0.0001f,
        "compression iterations should read serialized integer current value");
}

void TestEffectsGenerateStylizeLayerAnimatableRegistryCoverage() {
    using namespace EditorNodeGraph;
    using Stack::Timeline::AnimatableParameterDefinition;
    using Stack::Timeline::AnimatableValueType;

    struct ExpectedParameter {
        const char* parameterId;
        const char* storageKey;
        AnimatableValueType valueType;
    };

    auto requireParameters = [](LayerType layerType, std::initializer_list<ExpectedParameter> expected) {
        Graph graph;
        const int nodeId = NodeId(graph.AddLayerNode(layerType, 0, { 0.0f, 0.0f }));
        const Node* node = graph.FindNode(nodeId);
        Require(node != nullptr, "effects registry test node should exist");

        const std::vector<AnimatableParameterDefinition> parameters =
            Stack::Timeline::CollectAnimatableParametersForNode(*node);
        Require(parameters.size() == expected.size(),
            "effects layer node should expose exactly the expected animatable parameters");

        std::size_t index = 0;
        for (const ExpectedParameter& item : expected) {
            Require(parameters[index].target.nodeId == nodeId,
                "effects parameter should target the source node id");
            Require(parameters[index].target.parameterId == item.parameterId,
                "effects parameter id should be stable");
            Require(parameters[index].storageKey == item.storageKey,
                "effects parameter should map to the serialized layer key");
            Require(parameters[index].valueType == item.valueType,
                "effects parameter should preserve value type metadata");
            ++index;
        }
    };

    requireParameters(LayerType::BilateralFilter, {
        { "layer.bilateralRadius", "radius", AnimatableValueType::Integer },
        { "layer.bilateralColorSigma", "sigmaCol", AnimatableValueType::Float },
        { "layer.bilateralSpatialSigma", "sigmaSpace", AnimatableValueType::Float },
        { "layer.bilateralKernel", "kernel", AnimatableValueType::Enum },
        { "layer.bilateralEdgeMode", "edgeMode", AnimatableValueType::Enum }
    });

    requireParameters(LayerType::Noise, {
        { "layer.noiseStrength", "strength", AnimatableValueType::Float },
        { "layer.noiseType", "noiseType", AnimatableValueType::Enum },
        { "layer.noiseBlendMode", "blendMode", AnimatableValueType::Enum },
        { "layer.noiseBlurriness", "blurriness", AnimatableValueType::Float },
        { "layer.noiseSaturationStrength", "satStrength", AnimatableValueType::Float },
        { "layer.noiseSaturationImpact", "satImpact", AnimatableValueType::Float },
        { "layer.noiseScale", "scale", AnimatableValueType::Float },
        { "layer.noiseOpacity", "opacity", AnimatableValueType::Float }
    });

    auto requireSplitDither = [&](LayerType layerType) {
        requireParameters(layerType, {
            { "layer.ditherBitDepth", "bitDepth", AnimatableValueType::Integer },
            { "layer.ditherPaletteSize", "paletteSize", AnimatableValueType::Integer },
            { "layer.ditherStrength", "strength", AnimatableValueType::Float },
            { "layer.ditherScale", "scale", AnimatableValueType::Float },
            { "layer.ditherGammaCorrect", "gammaCorrect", AnimatableValueType::Boolean },
            { "layer.ditherUsePaletteBank", "usePaletteBank", AnimatableValueType::Boolean }
        });
    };
    requireSplitDither(LayerType::OrderedDither8x8);
    requireSplitDither(LayerType::ErrorDiffusionDither);
    requireSplitDither(LayerType::WhiteNoiseDither);
    requireSplitDither(LayerType::OrderedDither4x4);
    requireSplitDither(LayerType::OrderedDither2x2);
    requireSplitDither(LayerType::InterleavedGradientDither);

    requireParameters(LayerType::HDR, {
        { "layer.hdrTolerance", "tolerance", AnimatableValueType::Float },
        { "layer.hdrAmount", "amount", AnimatableValueType::Float }
    });
    requireParameters(LayerType::ColorGrade, {
        { "layer.colorGradeStrength", "strength", AnimatableValueType::Float }
    });
    requireParameters(LayerType::Vignette, {
        { "layer.vignetteIntensity", "intensity", AnimatableValueType::Float },
        { "layer.vignetteRadius", "radius", AnimatableValueType::Float },
        { "layer.vignetteSoftness", "softness", AnimatableValueType::Float }
    });
    requireParameters(LayerType::ChromaticAberration, {
        { "layer.chromaticAmount", "amount", AnimatableValueType::Float },
        { "layer.chromaticEdgeBlur", "edgeBlur", AnimatableValueType::Float },
        { "layer.chromaticZoomBlur", "zoomBlur", AnimatableValueType::Float },
        { "layer.chromaticLinkFalloffToBlur", "linkFalloffToBlur", AnimatableValueType::Boolean },
        { "layer.chromaticRadius", "radius", AnimatableValueType::Float },
        { "layer.chromaticFalloff", "falloff", AnimatableValueType::Float },
        { "layer.chromaticCenterX", "center", AnimatableValueType::Float },
        { "layer.chromaticCenterY", "center", AnimatableValueType::Float }
    });
    requireParameters(LayerType::LensDistortion, {
        { "layer.lensDistortionAmount", "amount", AnimatableValueType::Float },
        { "layer.lensDistortionScale", "scale", AnimatableValueType::Float }
    });
    requireParameters(LayerType::GlareRays, {
        { "layer.glareRaysIntensity", "intensity", AnimatableValueType::Float },
        { "layer.glareRaysCount", "rays", AnimatableValueType::Float },
        { "layer.glareRaysLength", "length", AnimatableValueType::Float },
        { "layer.glareRaysSoftness", "softness", AnimatableValueType::Float }
    });
    requireParameters(LayerType::AiryBloom, {
        { "layer.airyBloomIntensity", "intensity", AnimatableValueType::Float },
        { "layer.airyBloomAperture", "aperture", AnimatableValueType::Float },
        { "layer.airyBloomThreshold", "threshold", AnimatableValueType::Float },
        { "layer.airyBloomThresholdFade", "thresholdFade", AnimatableValueType::Float },
        { "layer.airyBloomCutoff", "cutoff", AnimatableValueType::Float }
    });
    requireParameters(LayerType::Halftoning, {
        { "layer.halftoneSize", "size", AnimatableValueType::Float },
        { "layer.halftoneIntensity", "intensity", AnimatableValueType::Float },
        { "layer.halftoneSharpness", "sharpness", AnimatableValueType::Float },
        { "layer.halftonePattern", "pattern", AnimatableValueType::Enum },
        { "layer.halftoneColorMode", "colorMode", AnimatableValueType::Enum },
        { "layer.halftoneGray", "gray", AnimatableValueType::Boolean },
        { "layer.halftoneInvert", "invert", AnimatableValueType::Boolean }
    });
    requireParameters(LayerType::CellShading, {
        { "layer.cellShadingLevels", "levels", AnimatableValueType::Integer },
        { "layer.cellShadingBias", "bias", AnimatableValueType::Float },
        { "layer.cellShadingGamma", "gamma", AnimatableValueType::Float },
        { "layer.cellShadingQuantMode", "quantMode", AnimatableValueType::Enum },
        { "layer.cellShadingBandMap", "bandMap", AnimatableValueType::Enum },
        { "layer.cellShadingEdgeMethod", "edgeMethod", AnimatableValueType::Enum },
        { "layer.cellShadingEdgeStrength", "edgeStrength", AnimatableValueType::Float },
        { "layer.cellShadingEdgeThickness", "edgeThickness", AnimatableValueType::Float },
        { "layer.cellShadingColorPreserve", "colorPreserve", AnimatableValueType::Float },
        { "layer.cellShadingShowEdges", "showEdges", AnimatableValueType::Boolean }
    });
    requireParameters(LayerType::ImageBreaks, {
        { "layer.imageBreaksColumns", "columns", AnimatableValueType::Float },
        { "layer.imageBreaksRows", "rows", AnimatableValueType::Float },
        { "layer.imageBreaksShiftX", "shiftX", AnimatableValueType::Float },
        { "layer.imageBreaksShiftY", "shiftY", AnimatableValueType::Float },
        { "layer.imageBreaksShiftBlur", "shiftBlur", AnimatableValueType::Float },
        { "layer.imageBreaksSeed", "seed", AnimatableValueType::Float },
        { "layer.imageBreaksSquareDensity", "squareDensity", AnimatableValueType::Float },
        { "layer.imageBreaksGridSize", "gridSize", AnimatableValueType::Float },
        { "layer.imageBreaksSquareDistance", "squareDistance", AnimatableValueType::Float },
        { "layer.imageBreaksSquareBlur", "squareBlur", AnimatableValueType::Float }
    });
    requireParameters(LayerType::AnalogVideo, {
        { "layer.analogWobble", "wobble", AnimatableValueType::Float },
        { "layer.analogBleed", "bleed", AnimatableValueType::Float },
        { "layer.analogCurve", "curve", AnimatableValueType::Float },
        { "layer.analogNoise", "noise", AnimatableValueType::Float }
    });
    requireParameters(LayerType::Expander, {
        { "layer.expanderPadding", "padding", AnimatableValueType::Float }
    });
    requireParameters(LayerType::PaletteReconstructor, {
        { "layer.paletteBlend", "blend", AnimatableValueType::Float },
        { "layer.paletteSmoothing", "smoothing", AnimatableValueType::Float },
        { "layer.paletteSmoothingType", "smoothingType", AnimatableValueType::Enum }
    });
    requireParameters(LayerType::Flip, {
        { "layer.flipHorizontal", "flipH", AnimatableValueType::Boolean },
        { "layer.flipVertical", "flipV", AnimatableValueType::Boolean }
    });

    Graph graph;
    const int chromaticId = NodeId(graph.AddLayerNode(LayerType::ChromaticAberration, 0, { 0.0f, 0.0f }));
    const Node* chromaticNode = graph.FindNode(chromaticId);
    Require(chromaticNode != nullptr, "chromatic readback test node should exist");
    SerializedLayerFixture chromaticLayer({
        { "type", "ChromaticAberration" },
        { "amount", 12.0f },
        { "edgeBlur", 5.0f },
        { "zoomBlur", 6.0f },
        { "linkFalloffToBlur", true },
        { "radius", 25.0f },
        { "falloff", 35.0f },
        { "center", { 0.25f, 0.75f } }
    });
    const std::vector<AnimatableParameterDefinition> chromaticParameters =
        Stack::Timeline::CollectAnimatableParametersForNode(*chromaticNode, &chromaticLayer);
    Require(chromaticParameters.size() == 8,
        "chromatic readback should expose all scalar parameters");
    Require(chromaticParameters[3].hasCurrentValue &&
            std::abs(chromaticParameters[3].currentValue - 1.0f) < 0.0001f,
        "chromatic link falloff bool should read serialized current value");
    Require(chromaticParameters[6].hasCurrentValue &&
            std::abs(chromaticParameters[6].currentValue - 0.25f) < 0.0001f,
        "chromatic center X should read serialized array current value");
    Require(chromaticParameters[7].hasCurrentValue &&
            std::abs(chromaticParameters[7].currentValue - 0.75f) < 0.0001f,
        "chromatic center Y should read serialized array current value");

    const int noiseId = NodeId(graph.AddLayerNode(LayerType::Noise, 1, { 0.0f, 0.0f }));
    const Node* noiseNode = graph.FindNode(noiseId);
    Require(noiseNode != nullptr, "noise readback test node should exist");
    SerializedLayerFixture noiseLayer({
        { "type", "Noise" },
        { "strength", 50.0f },
        { "noiseType", 7 },
        { "blendMode", 3 },
        { "blurriness", 25.0f },
        { "satStrength", 1.0f },
        { "satImpact", 0.0f },
        { "scale", 5.0f },
        { "opacity", 0.5f }
    });
    const std::vector<AnimatableParameterDefinition> noiseParameters =
        Stack::Timeline::CollectAnimatableParametersForNode(*noiseNode, &noiseLayer);
    Require(noiseParameters.size() == 8,
        "noise readback should expose numeric and enum scalar parameters");
    Require(noiseParameters[1].hasCurrentValue &&
            std::abs(noiseParameters[1].currentValue - 7.0f) < 0.0001f,
        "noise type enum should read serialized current value");
    Require(noiseParameters[2].hasCurrentValue &&
            std::abs(noiseParameters[2].currentValue - 3.0f) < 0.0001f,
        "noise blend mode enum should read serialized current value");
}

void TestTimelineKeyframesTrackSharedUpstreamChains() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 220.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 1, { 440.0f, -80.0f }));
    const int saturationId = NodeId(graph.AddLayerNode(LayerType::Saturation, 2, { 440.0f, 80.0f }));
    const int outputAId = NodeId(graph.AddOutputNode({ 660.0f, -80.0f }, true));
    const int outputBId = NodeId(graph.AddOutputNode({ 660.0f, 80.0f }, true));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, brightnessId, kImageInputSocketId),
        "timeline keyframe test source should connect to shared brightness node");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "timeline keyframe test first branch should connect");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, saturationId, kImageInputSocketId),
        "timeline keyframe test second branch should connect");
    Require(graph.TryConnectSockets(contrastId, kImageOutputSocketId, outputAId, kImageInputSocketId),
        "timeline keyframe test first output should connect");
    Require(graph.TryConnectSockets(saturationId, kImageOutputSocketId, outputBId, kImageInputSocketId),
        "timeline keyframe test second output should connect");

    const std::vector<CompletedChainInfo> chains = graph.GetCompletedChains();
    const CompletedChainInfo* outputAChain = nullptr;
    const CompletedChainInfo* outputBChain = nullptr;
    for (const CompletedChainInfo& chain : chains) {
        if (chain.outputNodeId == outputAId) {
            outputAChain = &chain;
        } else if (chain.outputNodeId == outputBId) {
            outputBChain = &chain;
        }
    }
    Require(outputAChain != nullptr && outputBChain != nullptr,
        "timeline keyframe test should have both output chains");

    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget brightnessTarget {
        brightnessId,
        "layer.brightness"
    };
    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 24, 0.35f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 12, 0.15f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 12, 0.25f);

    const Stack::Timeline::TimelineTrack* brightnessTrack =
        Stack::Timeline::FindTimelineTrack(animation, brightnessTarget);
    Require(brightnessTrack != nullptr, "timeline keyframe track should be created for the target");
    Require(brightnessTrack->keyframes.size() == 2,
        "setting a keyframe on an existing frame should replace instead of duplicate");
    Require(brightnessTrack->keyframes[0].frame == 12 &&
            std::abs(brightnessTrack->keyframes[0].value - 0.25f) < 0.0001f &&
            brightnessTrack->keyframes[1].frame == 24,
        "keyframes should stay sorted and replacement should keep the latest value");
    Require(Stack::Timeline::TrackAffectsCompletedChain(*brightnessTrack, *outputAChain),
        "shared upstream brightness keyframe should affect first output row");
    Require(Stack::Timeline::TrackAffectsCompletedChain(*brightnessTrack, *outputBChain),
        "shared upstream brightness keyframe should affect second output row");

    const Stack::Timeline::AnimatableParameterTarget contrastTarget {
        contrastId,
        "layer.contrast"
    };
    Stack::Timeline::SetOrReplaceKeyframe(animation, contrastTarget, 18, -0.2f);
    const Stack::Timeline::TimelineTrack* contrastTrack =
        Stack::Timeline::FindTimelineTrack(animation, contrastTarget);
    Require(contrastTrack != nullptr, "branch-local keyframe track should be created");
    Require(Stack::Timeline::TrackAffectsCompletedChain(*contrastTrack, *outputAChain),
        "branch-local contrast keyframe should affect its output row");
    Require(!Stack::Timeline::TrackAffectsCompletedChain(*contrastTrack, *outputBChain),
        "branch-local contrast keyframe should not affect the other output row");
}

void TestTimelineFrameEvaluationSamplesLinearAndHold() {
    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget target {
        42,
        "layer.brightness"
    };

    Stack::Timeline::SetOrReplaceKeyframe(
        animation,
        target,
        10,
        0.20f,
        Stack::Timeline::TimelineInterpolation::Linear);
    Stack::Timeline::SetOrReplaceKeyframe(
        animation,
        target,
        20,
        0.60f,
        Stack::Timeline::TimelineInterpolation::Hold);
    Stack::Timeline::SetOrReplaceKeyframe(
        animation,
        target,
        30,
        -0.20f,
        Stack::Timeline::TimelineInterpolation::Linear);

    const Stack::Timeline::TimelineTrack* track = Stack::Timeline::FindTimelineTrack(animation, target);
    Require(track != nullptr, "frame evaluation test track should exist");

    float value = 0.0f;
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*track, 5, value) &&
            std::abs(value - 0.20f) < 0.0001f,
        "frames before first key should hold the first keyed value");
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*track, 15, value) &&
            std::abs(value - 0.40f) < 0.0001f,
        "linear keyframe segment should interpolate between surrounding keys");
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*track, 25, value) &&
            std::abs(value - 0.60f) < 0.0001f,
        "hold keyframe segment should keep the previous keyed value until the next key");
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*track, 35, value) &&
            std::abs(value - (-0.20f)) < 0.0001f,
        "frames after last key should hold the last keyed value");

    const Stack::Timeline::FrameEvaluationContext context =
        Stack::Timeline::BuildFrameEvaluationContext(animation, 15);
    Require(context.frame == 15, "frame evaluation context should keep the sampled frame");
    Require(context.values.size() == 1, "frame evaluation context should contain one sampled parameter");
    Require(Stack::Timeline::TryGetFrameParameterValue(context, target, value) &&
            std::abs(value - 0.40f) < 0.0001f,
        "frame evaluation context should expose sampled values by stable target");
}

void TestTimelineFrameEvaluationSamplesDiscreteValuesAsHold() {
    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget enumTarget {
        42,
        "layer.noiseType"
    };
    const Stack::Timeline::AnimatableParameterTarget boolTarget {
        43,
        "layer.flipHorizontal"
    };

    Stack::Timeline::SetOrReplaceKeyframe(animation, enumTarget, 0, 0.0f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, enumTarget, 10, 12.0f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, boolTarget, 0, 0.0f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, boolTarget, 10, 1.0f);

    const Stack::Timeline::TimelineTrack* enumTrack =
        Stack::Timeline::FindTimelineTrack(animation, enumTarget);
    const Stack::Timeline::TimelineTrack* boolTrack =
        Stack::Timeline::FindTimelineTrack(animation, boolTarget);
    Require(enumTrack != nullptr && boolTrack != nullptr,
        "discrete frame evaluation test tracks should exist");

    float value = 0.0f;
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*enumTrack, 5, value) &&
            std::abs(value - 0.0f) < 0.0001f,
        "enum targets should hold the previous keyed value between keyframes");
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*boolTrack, 5, value) &&
            std::abs(value - 0.0f) < 0.0001f,
        "boolean targets should hold the previous keyed value between keyframes");
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*enumTrack, 10, value) &&
            std::abs(value - 12.0f) < 0.0001f,
        "enum targets should switch at the next keyed frame");
    Require(Stack::Timeline::EvaluateTimelineTrackAtFrame(*boolTrack, 10, value) &&
            std::abs(value - 1.0f) < 0.0001f,
        "boolean targets should switch at the next keyed frame");
}

void TestTimelineFrameEvaluationCanRemoveLiveEditPreviewTarget() {
    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget brightnessTarget {
        101,
        "layer.brightness"
    };
    const Stack::Timeline::AnimatableParameterTarget contrastTarget {
        202,
        "layer.contrast"
    };

    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 10, 0.25f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, contrastTarget, 10, -0.35f);

    Stack::Timeline::FrameEvaluationContext context =
        Stack::Timeline::BuildFrameEvaluationContext(animation, 10);
    Require(context.values.size() == 2,
        "frame evaluation should include both animated targets before live-edit removal");

    Require(Stack::Timeline::RemoveFrameParameterValue(context, brightnessTarget),
        "live-edit removal should remove the matching target from frame evaluation");
    float value = 0.0f;
    Require(!Stack::Timeline::TryGetFrameParameterValue(context, brightnessTarget, value),
        "live-edit removal should prevent the edited target from overriding live layer state");
    Require(Stack::Timeline::TryGetFrameParameterValue(context, contrastTarget, value) &&
            std::abs(value - (-0.35f)) < 0.0001f,
        "live-edit removal should leave unrelated animated targets in frame evaluation");
    Require(!Stack::Timeline::RemoveFrameParameterValue(context, brightnessTarget),
        "live-edit removal should report false when the target is already absent");
}

void TestFrameEvaluationAppliesLayerJsonOverridesWithoutMutatingLiveJson() {
    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget brightnessTarget {
        101,
        "layer.brightness"
    };
    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 0, 0.0f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 10, 0.50f);

    const Stack::Timeline::FrameEvaluationContext context =
        Stack::Timeline::BuildFrameEvaluationContext(animation, 5);

    const nlohmann::json liveLayerJson = {
        { "type", "Brightness" },
        { "brightness", -0.80f }
    };
    nlohmann::json frameLayerJson = liveLayerJson;

    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            context,
            101,
            LayerType::Brightness,
            frameLayerJson),
        "frame evaluation should apply matching split adjustment override to layer JSON");
    Require(std::abs(frameLayerJson.value("brightness", 0.0f) - 0.25f) < 0.0001f,
        "frame evaluation should write the sampled value to the serialized storage key");
    Require(std::abs(liveLayerJson.value("brightness", 0.0f) - (-0.80f)) < 0.0001f,
        "frame evaluation should leave the original live layer JSON unchanged when applied to a copy");

    nlohmann::json wrongNodeJson = liveLayerJson;
    Require(!Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            context,
            202,
            LayerType::Brightness,
            wrongNodeJson),
        "frame evaluation should ignore overrides for other node ids");
    Require(std::abs(wrongNodeJson.value("brightness", 0.0f) - (-0.80f)) < 0.0001f,
        "non-matching node override should not change layer JSON");

    nlohmann::json wrongLayerJson = liveLayerJson;
    Require(!Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            context,
            101,
            LayerType::Contrast,
            wrongLayerJson),
        "frame evaluation should ignore parameter ids that do not belong to the layer type");
    Require(std::abs(wrongLayerJson.value("brightness", 0.0f) - (-0.80f)) < 0.0001f,
        "non-matching layer type should not change layer JSON");

    Stack::Timeline::TimelineAnimationState blurAnimation;
    const Stack::Timeline::AnimatableParameterTarget gaussianAmountTarget {
        303,
        "layer.blurAmount"
    };
    const Stack::Timeline::AnimatableParameterTarget tiltShiftFocusXTarget {
        404,
        "layer.tiltShiftFocusX"
    };
    Stack::Timeline::SetOrReplaceKeyframe(blurAnimation, gaussianAmountTarget, 5, 9.5f);
    Stack::Timeline::SetOrReplaceKeyframe(blurAnimation, tiltShiftFocusXTarget, 5, 0.75f);
    const Stack::Timeline::FrameEvaluationContext blurContext =
        Stack::Timeline::BuildFrameEvaluationContext(blurAnimation, 5);

    nlohmann::json gaussianLayerJson = {
        { "type", "GaussianBlur" },
        { "amount", 2.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            blurContext,
            303,
            LayerType::GaussianBlur,
            gaussianLayerJson),
        "frame evaluation should apply gaussian blur amount overrides");
    Require(std::abs(gaussianLayerJson.value("amount", 0.0f) - 9.5f) < 0.0001f,
        "gaussian blur amount override should write the serialized amount key");

    nlohmann::json tiltShiftLayerJson = {
        { "type", "TiltShiftBlur" },
        { "amount", 10.0f },
        { "focusRadius", 30.0f },
        { "transition", 30.0f },
        { "centerX", 0.5f },
        { "centerY", 0.5f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            blurContext,
            404,
            LayerType::TiltShiftBlur,
            tiltShiftLayerJson),
        "frame evaluation should apply tilt-shift focus overrides");
    Require(std::abs(tiltShiftLayerJson.value("centerX", 0.0f) - 0.75f) < 0.0001f,
        "tilt-shift focus override should write the serialized centerX key");
    Require(std::abs(tiltShiftLayerJson.value("centerY", 0.0f) - 0.5f) < 0.0001f,
        "tilt-shift focus override should leave unrelated focus values unchanged");

    Stack::Timeline::TimelineAnimationState numericAnimation;
    Stack::Timeline::SetOrReplaceKeyframe(
        numericAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 505, "layer.compressionIterations" },
        5,
        99.2f);
    Stack::Timeline::SetOrReplaceKeyframe(
        numericAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 606, "layer.denoiseSearchRadius" },
        5,
        6.6f);
    Stack::Timeline::SetOrReplaceKeyframe(
        numericAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 707, "layer.edgeStrength" },
        5,
        750.0f);
    Stack::Timeline::SetOrReplaceKeyframe(
        numericAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 808, "layer.cropLeft" },
        5,
        55.0f);
    Stack::Timeline::SetOrReplaceKeyframe(
        numericAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 909, "layer.distortionPhase" },
        5,
        120.0f);
    const Stack::Timeline::FrameEvaluationContext numericContext =
        Stack::Timeline::BuildFrameEvaluationContext(numericAnimation, 5);

    nlohmann::json compressionLayerJson = {
        { "type", "DctCompression" },
        { "quality", 50.0f },
        { "blockSize", 8.0f },
        { "blend", 100.0f },
        { "iterations", 1 }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            numericContext,
            505,
            LayerType::DctCompression,
            compressionLayerJson),
        "frame evaluation should apply compression integer overrides");
    Require(compressionLayerJson["iterations"].is_number_integer() &&
            compressionLayerJson.value("iterations", 0) == 20,
        "compression iterations should be rounded and clamped before JSON write");

    nlohmann::json denoiseLayerJson = {
        { "type", "NonLocalMeansDenoise" },
        { "searchRadius", 5 },
        { "patchRadius", 2 },
        { "h", 0.5f },
        { "strength", 100.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            numericContext,
            606,
            LayerType::NonLocalMeansDenoise,
            denoiseLayerJson),
        "frame evaluation should apply denoise integer overrides");
    Require(denoiseLayerJson["searchRadius"].is_number_integer() &&
            denoiseLayerJson.value("searchRadius", 0) == 7,
        "denoise search radius should round to the nearest integer before JSON write");

    nlohmann::json edgeLayerJson = {
        { "type", "EdgeOverlay" },
        { "blend", 100.0f },
        { "strength", 500.0f },
        { "tolerance", 10.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            numericContext,
            707,
            LayerType::EdgeOverlay,
            edgeLayerJson),
        "frame evaluation should apply edge-effect float overrides");
    Require(std::abs(edgeLayerJson.value("strength", 0.0f) - 750.0f) < 0.0001f,
        "edge strength override should write the serialized strength key");

    nlohmann::json cropLayerJson = {
        { "type", "Crop" },
        { "cropLeft", 0.0f },
        { "cropRight", 0.0f },
        { "cropTop", 0.0f },
        { "cropBottom", 0.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            numericContext,
            808,
            LayerType::Crop,
            cropLayerJson),
        "frame evaluation should apply transform float overrides");
    Require(std::abs(cropLayerJson.value("cropLeft", 0.0f) - 50.0f) < 0.0001f,
        "crop overrides should clamp to the registered slider range before JSON write");

    nlohmann::json distortionLayerJson = {
        { "type", "RippleDistortion" },
        { "intensity", 30.0f },
        { "phase", 50.0f },
        { "scale", 20.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            numericContext,
            909,
            LayerType::RippleDistortion,
            distortionLayerJson),
        "frame evaluation should apply heat/ripple distortion float overrides");
    Require(std::abs(distortionLayerJson.value("phase", 0.0f) - 120.0f) < 0.0001f,
        "distortion phase override should write the serialized phase key");

    Stack::Timeline::TimelineAnimationState effectsAnimation;
    Stack::Timeline::SetOrReplaceKeyframe(
        effectsAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1001, "layer.bilateralRadius" },
        5,
        100.0f);
    Stack::Timeline::SetOrReplaceKeyframe(
        effectsAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1002, "layer.ditherBitDepth" },
        5,
        0.2f);
    Stack::Timeline::SetOrReplaceKeyframe(
        effectsAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1003, "layer.chromaticCenterY" },
        5,
        0.82f);
    Stack::Timeline::SetOrReplaceKeyframe(
        effectsAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1004, "layer.imageBreaksGridSize" },
        5,
        88.0f);
    Stack::Timeline::SetOrReplaceKeyframe(
        effectsAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1005, "layer.airyBloomThreshold" },
        5,
        0.42f);
    const Stack::Timeline::FrameEvaluationContext effectsContext =
        Stack::Timeline::BuildFrameEvaluationContext(effectsAnimation, 5);

    nlohmann::json bilateralLayerJson = {
        { "type", "BilateralFilter" },
        { "radius", 3 },
        { "sigmaCol", 0.1f },
        { "sigmaSpace", 3.0f },
        { "kernel", 0 },
        { "edgeMode", 0 }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            effectsContext,
            1001,
            LayerType::BilateralFilter,
            bilateralLayerJson),
        "frame evaluation should apply bilateral integer overrides");
    Require(bilateralLayerJson["radius"].is_number_integer() &&
            bilateralLayerJson.value("radius", 0) == 30,
        "bilateral radius should be rounded and clamped before JSON write");

    nlohmann::json splitDitherLayerJson = {
        { "type", "OrderedDither8x8" },
        { "bitDepth", 4 },
        { "paletteSize", 8 },
        { "strength", 100.0f },
        { "scale", 1.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            effectsContext,
            1002,
            LayerType::OrderedDither8x8,
            splitDitherLayerJson),
        "frame evaluation should apply split dither integer overrides");
    Require(splitDitherLayerJson["bitDepth"].is_number_integer() &&
            splitDitherLayerJson.value("bitDepth", 0) == 1,
        "dither bit depth should be rounded and clamped before JSON write");

    nlohmann::json chromaticLayerJson = {
        { "type", "ChromaticAberration" },
        { "amount", 0.0f },
        { "edgeBlur", 0.0f },
        { "zoomBlur", 0.0f },
        { "center", { 0.25f, 0.75f } },
        { "radius", 50.0f },
        { "falloff", 50.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            effectsContext,
            1003,
            LayerType::ChromaticAberration,
            chromaticLayerJson),
        "frame evaluation should apply chromatic array-element overrides");
    Require(chromaticLayerJson["center"].is_array() &&
            std::abs(chromaticLayerJson["center"][0].get<float>() - 0.25f) < 0.0001f &&
            std::abs(chromaticLayerJson["center"][1].get<float>() - 0.82f) < 0.0001f,
        "chromatic center Y override should update only the serialized array element");

    nlohmann::json imageBreaksLayerJson = {
        { "type", "ImageBreaks" },
        { "columns", 10.0f },
        { "rows", 10.0f },
        { "shiftX", 0.2f },
        { "shiftY", 0.0f },
        { "shiftBlur", 0.0f },
        { "seed", 0.0f },
        { "squareDensity", 0.0f },
        { "gridSize", 20.0f },
        { "squareDistance", 0.1f },
        { "squareBlur", 0.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            effectsContext,
            1004,
            LayerType::ImageBreaks,
            imageBreaksLayerJson),
        "frame evaluation should apply image breaks numeric overrides");
    Require(std::abs(imageBreaksLayerJson.value("gridSize", 0.0f) - 88.0f) < 0.0001f,
        "image breaks grid size override should write the serialized grid size key");

    nlohmann::json airyBloomLayerJson = {
        { "type", "AiryBloom" },
        { "intensity", 0.5f },
        { "aperture", 8.0f },
        { "threshold", 0.7f },
        { "thresholdFade", 0.1f },
        { "cutoff", 0.1f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            effectsContext,
            1005,
            LayerType::AiryBloom,
            airyBloomLayerJson),
        "frame evaluation should apply airy bloom numeric overrides");
    Require(std::abs(airyBloomLayerJson.value("threshold", 0.0f) - 0.42f) < 0.0001f,
        "airy bloom threshold override should write the serialized threshold key");

    Stack::Timeline::TimelineAnimationState typedAnimation;
    Stack::Timeline::SetOrReplaceKeyframe(
        typedAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1101, "layer.noiseType" },
        5,
        99.0f);
    Stack::Timeline::SetOrReplaceKeyframe(
        typedAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1102, "layer.flipHorizontal" },
        5,
        0.8f);
    Stack::Timeline::SetOrReplaceKeyframe(
        typedAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1103, "layer.ditherGammaCorrect" },
        5,
        0.1f);
    Stack::Timeline::SetOrReplaceKeyframe(
        typedAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1104, "layer.cellShadingQuantMode" },
        5,
        1.6f);
    Stack::Timeline::SetOrReplaceKeyframe(
        typedAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1105, "layer.paletteSmoothingType" },
        5,
        0.7f);
    Stack::Timeline::SetOrReplaceKeyframe(
        typedAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1106, "layer.chromaticLinkFalloffToBlur" },
        5,
        1.0f);
    const Stack::Timeline::FrameEvaluationContext typedContext =
        Stack::Timeline::BuildFrameEvaluationContext(typedAnimation, 5);

    nlohmann::json noiseLayerJson = {
        { "type", "Noise" },
        { "strength", 50.0f },
        { "noiseType", 0 },
        { "blendMode", 0 },
        { "scale", 1.0f }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            typedContext,
            1101,
            LayerType::Noise,
            noiseLayerJson),
        "frame evaluation should apply noise enum overrides");
    Require(noiseLayerJson["noiseType"].is_number_integer() &&
            noiseLayerJson.value("noiseType", 0) == 12,
        "noise enum overrides should round and clamp before writing integer JSON");

    nlohmann::json flipLayerJson = {
        { "type", "Flip" },
        { "flipH", false },
        { "flipV", false }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            typedContext,
            1102,
            LayerType::Flip,
            flipLayerJson),
        "frame evaluation should apply flip boolean overrides");
    Require(flipLayerJson["flipH"].is_boolean() &&
            flipLayerJson.value("flipH", false),
        "flip boolean overrides should write boolean JSON");

    nlohmann::json ditherBoolLayerJson = {
        { "type", "OrderedDither8x8" },
        { "gammaCorrect", true },
        { "usePaletteBank", true }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            typedContext,
            1103,
            LayerType::OrderedDither8x8,
            ditherBoolLayerJson),
        "frame evaluation should apply dither boolean overrides");
    Require(ditherBoolLayerJson["gammaCorrect"].is_boolean() &&
            !ditherBoolLayerJson.value("gammaCorrect", true),
        "dither boolean overrides should threshold and write boolean JSON");

    nlohmann::json cellShadingLayerJson = {
        { "type", "CellShading" },
        { "quantMode", 0 },
        { "bandMap", 0 },
        { "edgeMethod", 0 },
        { "showEdges", true }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            typedContext,
            1104,
            LayerType::CellShading,
            cellShadingLayerJson),
        "frame evaluation should apply cell-shading enum overrides");
    Require(cellShadingLayerJson["quantMode"].is_number_integer() &&
            cellShadingLayerJson.value("quantMode", 0) == 2,
        "cell-shading enum overrides should round to the nearest option");

    nlohmann::json paletteLayerJson = {
        { "type", "PaletteReconstructor" },
        { "smoothingType", 0 }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            typedContext,
            1105,
            LayerType::PaletteReconstructor,
            paletteLayerJson),
        "frame evaluation should apply palette enum overrides");
    Require(paletteLayerJson["smoothingType"].is_number_integer() &&
            paletteLayerJson.value("smoothingType", 0) == 1,
        "palette enum overrides should write integer JSON");

    nlohmann::json chromaticBoolLayerJson = {
        { "type", "ChromaticAberration" },
        { "linkFalloffToBlur", false }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            typedContext,
            1106,
            LayerType::ChromaticAberration,
            chromaticBoolLayerJson),
        "frame evaluation should apply chromatic boolean overrides");
    Require(chromaticBoolLayerJson["linkFalloffToBlur"].is_boolean() &&
            chromaticBoolLayerJson.value("linkFalloffToBlur", false),
        "chromatic boolean overrides should write boolean JSON");
}

void TestBackgroundPatcherAnimatableRegistryCoverage() {
    using namespace EditorNodeGraph;
    using Stack::Timeline::AnimatableParameterDefinition;
    using Stack::Timeline::AnimatableValueType;

    struct ExpectedParameter {
        const char* parameterId;
        const char* storageKey;
        AnimatableValueType valueType;
    };

    const ExpectedParameter expected[] = {
        { "layer.backgroundPatcherTargetAlpha", "targetAlpha", AnimatableValueType::Float },
        { "layer.backgroundPatcherTolerance", "tolerance", AnimatableValueType::Float },
        { "layer.backgroundPatcherSmoothing", "smoothing", AnimatableValueType::Float },
        { "layer.backgroundPatcherEdgeShift", "edgeShift", AnimatableValueType::Float },
        { "layer.backgroundPatcherDefringe", "defringe", AnimatableValueType::Float },
        { "layer.backgroundPatcherKeepSelected", "keepSelected", AnimatableValueType::Boolean },
        { "layer.backgroundPatcherShowDebugOverlay", "showDebugOverlay", AnimatableValueType::Boolean }
    };
    const std::size_t expectedCount = sizeof(expected) / sizeof(expected[0]);

    Graph graph;
    const int nodeId = NodeId(graph.AddLayerNode(LayerType::BackgroundPatcher, 0, { 0.0f, 0.0f }));
    const Node* node = graph.FindNode(nodeId);
    Require(node != nullptr, "background patcher registry test node should exist");

    const std::vector<AnimatableParameterDefinition> parameters =
        Stack::Timeline::CollectAnimatableParametersForNode(*node);
    Require(parameters.size() == expectedCount,
        "background patcher should expose only simple serialized scalar animatable parameters");

    for (std::size_t index = 0; index < expectedCount; ++index) {
        Require(parameters[index].target.nodeId == nodeId,
            "background patcher parameter should target the source node id");
        Require(parameters[index].target.parameterId == expected[index].parameterId,
            "background patcher parameter id should be stable");
        Require(parameters[index].storageKey == expected[index].storageKey,
            "background patcher parameter should map to the serialized layer key");
        Require(parameters[index].valueType == expected[index].valueType,
            "background patcher parameter should preserve value type metadata");
    }

    SerializedLayerFixture patcherLayer({
        { "type", "BackgroundPatcher" },
        { "targetColor", { 0.1f, 0.2f, 0.3f } },
        { "targetAlpha", 0.35f },
        { "tolerance", 0.45f },
        { "smoothing", 0.25f },
        { "edgeShift", -3.0f },
        { "defringe", 0.15f },
        { "keepSelected", true },
        { "showDebugOverlay", false }
    });
    const std::vector<AnimatableParameterDefinition> parametersWithCurrent =
        Stack::Timeline::CollectAnimatableParametersForNode(*node, &patcherLayer);
    Require(parametersWithCurrent.size() == expectedCount,
        "background patcher readback should expose all registered scalar parameters");
    Require(parametersWithCurrent[0].hasCurrentValue &&
            std::abs(parametersWithCurrent[0].currentValue - 0.35f) < 0.0001f,
        "background patcher target alpha should read serialized float current value");
    Require(parametersWithCurrent[1].hasCurrentValue &&
            std::abs(parametersWithCurrent[1].currentValue - 0.45f) < 0.0001f,
        "background patcher tolerance should read serialized float current value");
    Require(parametersWithCurrent[3].hasCurrentValue &&
            std::abs(parametersWithCurrent[3].currentValue + 3.0f) < 0.0001f,
        "background patcher edge shift should read signed serialized float current value");
    Require(parametersWithCurrent[5].hasCurrentValue &&
            std::abs(parametersWithCurrent[5].currentValue - 1.0f) < 0.0001f,
        "background patcher keep-selected bool should read serialized current value");
    Require(parametersWithCurrent[6].hasCurrentValue &&
            std::abs(parametersWithCurrent[6].currentValue - 0.0f) < 0.0001f,
        "background patcher visualizer bool should read serialized current value");

    Stack::Timeline::TimelineAnimationState patcherAnimation;
    Stack::Timeline::SetOrReplaceKeyframe(
        patcherAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1201, "layer.backgroundPatcherTargetAlpha" },
        5,
        2.0f);
    Stack::Timeline::SetOrReplaceKeyframe(
        patcherAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1201, "layer.backgroundPatcherTolerance" },
        5,
        -0.25f);
    Stack::Timeline::SetOrReplaceKeyframe(
        patcherAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1201, "layer.backgroundPatcherEdgeShift" },
        5,
        15.0f);
    Stack::Timeline::SetOrReplaceKeyframe(
        patcherAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1201, "layer.backgroundPatcherKeepSelected" },
        5,
        0.8f);
    Stack::Timeline::SetOrReplaceKeyframe(
        patcherAnimation,
        Stack::Timeline::AnimatableParameterTarget{ 1201, "layer.backgroundPatcherShowDebugOverlay" },
        5,
        0.1f);
    const Stack::Timeline::FrameEvaluationContext patcherContext =
        Stack::Timeline::BuildFrameEvaluationContext(patcherAnimation, 5);

    nlohmann::json patcherJson = {
        { "type", "BackgroundPatcher" },
        { "targetColor", { 0.1f, 0.2f, 0.3f } },
        { "targetAlpha", 0.35f },
        { "tolerance", 0.45f },
        { "smoothing", 0.25f },
        { "edgeShift", -3.0f },
        { "defringe", 0.15f },
        { "keepSelected", false },
        { "showDebugOverlay", true }
    };
    Require(Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
            patcherContext,
            1201,
            LayerType::BackgroundPatcher,
            patcherJson),
        "frame evaluation should apply background patcher scalar overrides");
    Require(std::abs(patcherJson.value("targetAlpha", 0.0f) - 1.0f) < 0.0001f,
        "background patcher target alpha override should clamp to the registered range");
    Require(std::abs(patcherJson.value("tolerance", 1.0f) - 0.0f) < 0.0001f,
        "background patcher tolerance override should clamp to the registered range");
    Require(std::abs(patcherJson.value("edgeShift", 0.0f) - 10.0f) < 0.0001f,
        "background patcher edge shift override should clamp to the signed slider range");
    Require(patcherJson["keepSelected"].is_boolean() &&
            patcherJson.value("keepSelected", false),
        "background patcher keep-selected override should write boolean JSON");
    Require(patcherJson["showDebugOverlay"].is_boolean() &&
            !patcherJson.value("showDebugOverlay", true),
        "background patcher visualizer override should write boolean JSON");
    Require(patcherJson["targetColor"].is_array() &&
            std::abs(patcherJson["targetColor"][0].get<float>() - 0.1f) < 0.0001f,
        "background patcher frame overrides should leave target color out of scope");
}

void TestTimelinePlaybackAdvancesFramesAndStopsOrLoops() {
    double accumulator = 0.0;
    Stack::Timeline::TimelinePlaybackAdvanceResult result =
        Stack::Timeline::AdvanceTimelinePlayback(0, 5, 10, 0.05, false, accumulator);
    Require(result.frame == 0 && !result.frameChanged && !result.shouldStop,
        "timeline playback should accumulate partial frames without advancing early");
    Require(std::abs(accumulator - 0.5) < 0.0001,
        "timeline playback should retain fractional frame time");

    result = Stack::Timeline::AdvanceTimelinePlayback(0, 5, 10, 0.05, false, accumulator);
    Require(result.frame == 1 && result.frameChanged && !result.shouldStop,
        "timeline playback should advance after enough accumulated time");
    Require(std::abs(accumulator) < 0.0001,
        "timeline playback should consume whole-frame accumulator time");

    accumulator = 0.0;
    result = Stack::Timeline::AdvanceTimelinePlayback(3, 5, 10, 0.30, false, accumulator);
    Require(result.frame == 4 && result.frameChanged && result.shouldStop && !result.wrapped,
        "timeline playback should clamp and stop at the final frame when loop is disabled");

    accumulator = 0.0;
    result = Stack::Timeline::AdvanceTimelinePlayback(4, 5, 10, 0.20, true, accumulator);
    Require(result.frame == 1 && result.frameChanged && !result.shouldStop && result.wrapped,
        "timeline playback should wrap around when loop is enabled");

    accumulator = 0.25;
    result = Stack::Timeline::AdvanceTimelinePlayback(0, 1, 30, 0.50, true, accumulator);
    Require(result.frame == 0 && !result.frameChanged && result.shouldStop,
        "single-frame timelines should stop playback cleanly");
    Require(std::abs(accumulator) < 0.0001,
        "single-frame timelines should clear playback accumulation");
}

void TestTimelineStepFrameWrapsWithinRange() {
    Require(Stack::Timeline::ResolveTimelineStepFrame(0, -1, 120, 20) == 20,
        "timeline previous-frame step should wrap from the range start to the range end");
    Require(Stack::Timeline::ResolveTimelineStepFrame(20, 1, 120, 20) == 0,
        "timeline next-frame step should wrap from the range end to the range start");
    Require(Stack::Timeline::ResolveTimelineStepFrame(5, 3, 120, 20) == 8,
        "timeline frame stepping should preserve ordinary in-range forward steps");
    Require(Stack::Timeline::ResolveTimelineStepFrame(5, -7, 120, 20) == 19,
        "timeline frame stepping should support multi-frame backward wrapping");
    Require(Stack::Timeline::ResolveTimelineStepFrame(100, -1, 120, 20) == 20,
        "timeline frame stepping should re-enter the range from the end when current frame is beyond it and stepping backward");
    Require(Stack::Timeline::ResolveTimelineStepFrame(100, 1, 120, 20) == 0,
        "timeline frame stepping should re-enter the range from the start when current frame is beyond it and stepping forward");
    Require(Stack::Timeline::ResolveTimelineStepFrame(0, 1, 1, 0) == 0,
        "single-frame timeline step should remain on frame zero");
}

void TestTimelineAnimationReportsLastDistinctKeyframeFrames() {
    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget brightnessTarget {
        101,
        "layer.brightness"
    };
    const Stack::Timeline::AnimatableParameterTarget contrastTarget {
        202,
        "layer.contrast"
    };

    Require(Stack::Timeline::FindLastTimelineKeyframeFrame(animation) == -1,
        "empty timeline animation should not report a last keyframe frame");
    Require(Stack::Timeline::CountDistinctTimelineKeyframeFrames(animation) == 0,
        "empty timeline animation should not report distinct keyframe frames");

    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 12, 0.25f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, contrastTarget, 12, -0.10f);
    Require(Stack::Timeline::FindLastTimelineKeyframeFrame(animation) == 12,
        "last keyframe frame should include all timeline tracks");
    Require(Stack::Timeline::CountDistinctTimelineKeyframeFrames(animation) == 1,
        "distinct keyframe frame count should collapse multiple tracks on the same frame");

    Stack::Timeline::SetOrReplaceKeyframe(animation, contrastTarget, 48, 0.50f);
    Require(Stack::Timeline::FindLastTimelineKeyframeFrame(animation) == 48,
        "last keyframe frame should update when a later keyframe is added");
    Require(Stack::Timeline::CountDistinctTimelineKeyframeFrames(animation) == 2,
        "distinct keyframe frame count should include separate timeline frames");
}

void TestTimelineAnimationUpdatesOnlyExistingKeyframes() {
    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget brightnessTarget {
        101,
        "layer.brightness"
    };
    const Stack::Timeline::AnimatableParameterTarget contrastTarget {
        202,
        "layer.contrast"
    };

    Require(!Stack::Timeline::UpdateExistingKeyframeValue(animation, brightnessTarget, 12, 0.30f),
        "existing-keyframe update should not create a missing track");

    Stack::Timeline::SetOrReplaceKeyframe(animation, brightnessTarget, 12, 0.10f);
    Stack::Timeline::SetOrReplaceKeyframe(
        animation,
        brightnessTarget,
        24,
        0.80f,
        Stack::Timeline::TimelineInterpolation::Hold);
    Require(Stack::Timeline::UpdateExistingKeyframeValue(animation, brightnessTarget, 12, 0.45f),
        "existing-keyframe update should modify a matching keyframe");
    Require(!Stack::Timeline::UpdateExistingKeyframeValue(animation, brightnessTarget, 18, 0.60f),
        "existing-keyframe update should not create a missing frame on an existing track");
    Require(!Stack::Timeline::UpdateExistingKeyframeValue(animation, contrastTarget, 12, -0.20f),
        "existing-keyframe update should not create a missing target track");

    const Stack::Timeline::TimelineTrack* brightnessTrack =
        Stack::Timeline::FindTimelineTrack(animation, brightnessTarget);
    Require(brightnessTrack != nullptr && brightnessTrack->keyframes.size() == 2,
        "existing-keyframe update should preserve the existing track size");
    Require(brightnessTrack->keyframes[0].frame == 12 &&
            std::abs(brightnessTrack->keyframes[0].value - 0.45f) < 0.0001f,
        "existing-keyframe update should write the new value to the matching frame");
    Require(brightnessTrack->keyframes[1].interpolation == Stack::Timeline::TimelineInterpolation::Hold,
        "existing-keyframe update should preserve unrelated keyframe interpolation");
}

void TestTimelineFrameProducerNormalizesRequestsAndBuildsContext() {
    const Stack::Timeline::TimelineFrameRequest normalized =
        Stack::Timeline::NormalizeTimelineFrameRequest(999, 10, 300);
    Require(normalized.frame == 9,
        "timeline frame producer should clamp frame requests to the duration");
    Require(normalized.durationFrames == 10,
        "timeline frame producer should preserve valid duration");
    Require(normalized.framesPerSecond == 240,
        "timeline frame producer should clamp FPS to the supported range");

    const Stack::Timeline::TimelineFrameRequest minimum =
        Stack::Timeline::NormalizeTimelineFrameRequest(-20, -1, 0);
    Require(minimum.frame == 0 && minimum.durationFrames == 1 && minimum.framesPerSecond == 1,
        "timeline frame producer should normalize invalid requests to safe minimums");

    Stack::Timeline::TimelineAnimationState animation;
    const Stack::Timeline::AnimatableParameterTarget target {
        77,
        "layer.contrast"
    };
    Stack::Timeline::SetOrReplaceKeyframe(animation, target, 0, 0.0f);
    Stack::Timeline::SetOrReplaceKeyframe(animation, target, 9, 0.90f);

    const Stack::Timeline::TimelineFrameEvaluation evaluation =
        Stack::Timeline::BuildTimelineFrameEvaluation(animation, normalized);
    Require(evaluation.request.frame == 9 &&
            evaluation.request.durationFrames == 10 &&
            evaluation.request.framesPerSecond == 240,
        "timeline frame producer should keep the normalized request on the evaluation");
    Require(evaluation.HasAnimatedValues(),
        "timeline frame producer should build frame evaluation values for keyed tracks");

    float value = 0.0f;
    Require(Stack::Timeline::TryGetFrameParameterValue(evaluation.frameContext, target, value) &&
            std::abs(value - 0.90f) < 0.0001f,
        "timeline frame producer should sample animation values for the normalized frame");
}

void TestTimelinePersistenceRoundTripsDocument() {
    Stack::Timeline::TimelineDocumentState document;
    document.currentFrame = 12;
    document.durationFrames = 48;
    document.framesPerSecond = 24;

    const Stack::Timeline::AnimatableParameterTarget brightnessTarget {
        101,
        "layer.brightness"
    };
    const Stack::Timeline::AnimatableParameterTarget sharpenTarget {
        202,
        "layer.sharpenThreshold"
    };
    Stack::Timeline::SetOrReplaceKeyframe(document.animation, brightnessTarget, 0, -0.10f);
    Stack::Timeline::SetOrReplaceKeyframe(document.animation, brightnessTarget, 24, 0.40f);
    Stack::Timeline::SetOrReplaceKeyframe(
        document.animation,
        sharpenTarget,
        47,
        0.25f,
        Stack::Timeline::TimelineInterpolation::Hold);

    const nlohmann::json json = Stack::Timeline::SerializeTimelineDocument(document);
    Require(json.value("schemaVersion", 0) == 1,
        "timeline persistence should write schema version 1");
    Require(json.value("currentFrame", -1) == 12 &&
            json.value("durationFrames", -1) == 48 &&
            json.value("framesPerSecond", -1) == 24,
        "timeline persistence should write normalized timeline settings");

    const Stack::Timeline::TimelineDocumentState loaded =
        Stack::Timeline::DeserializeTimelineDocument(
            json,
            [](const Stack::Timeline::AnimatableParameterTarget&) {
                return true;
            });

    Require(loaded.currentFrame == 12 &&
            loaded.durationFrames == 48 &&
            loaded.framesPerSecond == 24,
        "timeline persistence should round-trip timeline settings");
    Require(loaded.animation.tracks.size() == 2,
        "timeline persistence should round-trip all valid tracks");

    const Stack::Timeline::TimelineTrack* brightnessTrack =
        Stack::Timeline::FindTimelineTrack(loaded.animation, brightnessTarget);
    Require(brightnessTrack != nullptr &&
            brightnessTrack->keyframes.size() == 2 &&
            brightnessTrack->keyframes[0].frame == 0 &&
            std::abs(brightnessTrack->keyframes[1].value - 0.40f) < 0.0001f,
        "timeline persistence should round-trip sorted float keyframes");

    const Stack::Timeline::TimelineTrack* sharpenTrack =
        Stack::Timeline::FindTimelineTrack(loaded.animation, sharpenTarget);
    Require(sharpenTrack != nullptr &&
            sharpenTrack->keyframes.size() == 1 &&
            sharpenTrack->keyframes[0].interpolation == Stack::Timeline::TimelineInterpolation::Hold,
        "timeline persistence should round-trip keyframe interpolation");
}

void TestTimelinePersistenceRepairsInvalidTargetsAndFrames() {
    nlohmann::json json = {
        { "schemaVersion", 1 },
        { "currentFrame", 999 },
        { "durationFrames", 10 },
        { "framesPerSecond", 999 },
        { "tracks", nlohmann::json::array({
            {
                { "target", {
                    { "nodeId", 10 },
                    { "parameterId", "layer.brightness" }
                } },
                { "keyframes", nlohmann::json::array({
                    {
                        { "frame", 5 },
                        { "value", 0.5f },
                        { "interpolation", "linear" }
                    },
                    {
                        { "frame", -4 },
                        { "value", 0.8f },
                        { "interpolation", "hold" }
                    },
                    {
                        { "frame", 50 },
                        { "value", 1.0f },
                        { "interpolation", "hold" }
                    }
                }) }
            },
            {
                { "target", {
                    { "nodeId", 20 },
                    { "parameterId", "layer.contrast" }
                } },
                { "keyframes", nlohmann::json::array({
                    {
                        { "frame", 2 },
                        { "value", 0.2f },
                        { "interpolation", "linear" }
                    }
                }) }
            },
            {
                { "target", {
                    { "nodeId", 30 },
                    { "parameterId", "" }
                } },
                { "keyframes", nlohmann::json::array({
                    {
                        { "frame", 2 },
                        { "value", 0.2f }
                    }
                }) }
            }
        }) }
    };

    const Stack::Timeline::TimelineDocumentState loaded =
        Stack::Timeline::DeserializeTimelineDocument(
            json,
            [](const Stack::Timeline::AnimatableParameterTarget& target) {
                return target.nodeId == 10 && target.parameterId == "layer.brightness";
            });

    Require(loaded.currentFrame == 9 &&
            loaded.durationFrames == 10 &&
            loaded.framesPerSecond == 240,
        "timeline persistence should normalize loaded timeline settings");
    Require(loaded.animation.tracks.size() == 1,
        "timeline persistence should drop missing or unsupported targets");

    const Stack::Timeline::AnimatableParameterTarget brightnessTarget {
        10,
        "layer.brightness"
    };
    const Stack::Timeline::TimelineTrack* track =
        Stack::Timeline::FindTimelineTrack(loaded.animation, brightnessTarget);
    Require(track != nullptr && track->keyframes.size() == 2,
        "timeline persistence should keep only valid keyframes for valid targets");
    Require(track->keyframes[0].frame == 5 &&
            track->keyframes[1].frame == 9 &&
            track->keyframes[1].interpolation == Stack::Timeline::TimelineInterpolation::Hold,
        "timeline persistence should ignore negative frames and clamp frames beyond duration");

    const Stack::Timeline::TimelineDocumentState futureSchema =
        Stack::Timeline::DeserializeTimelineDocument({ { "schemaVersion", 99 } });
    Require(futureSchema.animation.tracks.empty() &&
            futureSchema.currentFrame == 0 &&
            futureSchema.durationFrames == 120 &&
            futureSchema.framesPerSecond == 30,
        "timeline persistence should ignore unsupported future schemas safely");
}

void TestScalarCyclesAreRejected() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 220.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 1, { 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, brightnessId, kImageInputSocketId),
        "mask should connect to layer image input");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "layer should connect downstream");
    Require(!graph.CanConnectSockets(contrastId, kImageOutputSocketId, brightnessId, kMaskInputSocketId),
        "scalar/image feedback into an upstream layer mask should be rejected as a cycle");
}

void TestCustomMaskConnections() {
    using namespace EditorNodeGraph;

    Graph graph;
    CustomMaskPayload payload;
    payload.width = 4;
    payload.height = 4;
    payload.rasterLayer.assign(16, 0.5f);

    const int maskId = NodeId(graph.AddCustomMaskNode(payload, { 0.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 220.0f, 0.0f }));
    const int layerId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 440.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 660.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 880.0f, 0.0f }, true));

    Require(graph.IsScalarSocketStream(maskId, kMaskOutputSocketId),
        "custom mask output should be treated as a scalar stream");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, previewId, kPreviewInputSocketId),
        "custom mask should connect directly to preview nodes");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, layerId, kMaskInputSocketId),
        "custom mask should connect to layer mask inputs");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, mixId, kMixFactorSocketId),
        "custom mask should connect to mix factor inputs");
    Require(!graph.CanConnectSockets(
            maskId,
            kMaskOutputSocketId,
            outputId,
            kImageInputSocketId),
        "custom mask should not enter the exact Image-or-Channel Output union");
    Require(!graph.CanConnectSockets(maskId, kMaskOutputSocketId, maskId, kMaskOutputSocketId),
        "custom mask should reject self-connections");
}

void TestCustomMaskThroughMaskCombineExclude() {
    using namespace EditorNodeGraph;

    Graph graph;
    CustomMaskPayload payload;
    payload.width = 2;
    payload.height = 2;
    payload.rasterLayer.assign(4, 1.0f);

    const int customA = NodeId(graph.AddCustomMaskNode(payload, { 0.0f, 0.0f }));
    const int customB = NodeId(graph.AddCustomMaskNode(payload, { 0.0f, 120.0f }));
    const int combineId = NodeId(graph.AddMaskCombineNode(MaskCombineMode::Exclude, { 220.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(customA, kMaskOutputSocketId, combineId, kMaskCombineInputASocketId),
        "custom mask should connect to exclude combine A");
    Require(graph.TryConnectSockets(customB, kMaskOutputSocketId, combineId, kMaskCombineInputBSocketId),
        "custom mask should connect to exclude combine B");
    Require(graph.IsScalarSocketStream(combineId, kMaskOutputSocketId),
        "exclude mask combine should preserve scalar stream classification");
    Require(graph.TryConnectSockets(combineId, kMaskOutputSocketId, previewId, kPreviewInputSocketId),
        "exclude mask combine should preview as a scalar output");
}

void TestCustomMaskSparseRasterPersistence() {
    using namespace EditorNodeGraph;

    CustomMaskPayload sparse;
    sparse.width = kMaximumCustomMaskDimension;
    sparse.height = kMaximumCustomMaskDimension;
    const nlohmann::json encodedSparse = SerializeCustomMaskPayload(sparse);
    const CustomMaskPayload restoredSparse =
        DeserializeCustomMaskPayload(encodedSparse);
    Require(
        restoredSparse.width == kMaximumCustomMaskDimension &&
        restoredSparse.height == kMaximumCustomMaskDimension &&
        restoredSparse.rasterLayer.empty(),
        "an all-zero custom mask should remain a sparse zero raster instead of "
        "allocating a full maximum-size float canvas");

    Graph graph;
    Node* sparseNode = graph.AddCustomMaskNode(restoredSparse, { 0.0f, 0.0f });
    Require(
        sparseNode != nullptr && sparseNode->customMask.rasterLayer.empty(),
        "adding a sparse custom mask should preserve its lazy zero raster");

    CustomMaskPayload painted;
    painted.width = 2;
    painted.height = 2;
    painted.rasterLayer = { 0.0f, 0.25f, 0.5f, 1.0f };
    const CustomMaskPayload restoredPainted =
        DeserializeCustomMaskPayload(SerializeCustomMaskPayload(painted));
    Require(
        restoredPainted.rasterLayer.size() == painted.rasterLayer.size(),
        "a painted custom mask should retain its exact raster extent");
    for (std::size_t index = 0;
         index < restoredPainted.rasterLayer.size();
         ++index) {
        Require(
            std::abs(
                restoredPainted.rasterLayer[index] -
                painted.rasterLayer[index]) <=
                (1.0f / 65535.0f + 1.0e-7f),
            "custom mask U16 persistence should preserve painted values");
    }

    nlohmann::json truncated = encodedSparse;
    truncated["width"] = 4;
    truncated["height"] = 4;
    truncated["rasterLayer"] =
        nlohmann::json::binary(std::vector<unsigned char>{ 0, 0 });
    Require(
        DeserializeCustomMaskPayload(truncated).rasterLayer.empty(),
        "a truncated custom-mask raster should fail closed to sparse zero");
}

void TestManualRawBaselineChainShape() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawSourcePayload rawPayload;
    rawPayload.label = "RAW";
    rawPayload.sourcePath = "manual-baseline.dng";

    const int rawSourceId = NodeId(graph.AddRawSourceNode(rawPayload, { 0.0f, 0.0f }));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 280.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 560.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 840.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1120.0f, 0.0f }, true));

    std::string rawToImageError;
    Require(!graph.CanConnectSockets(rawSourceId, kRawOutputSocketId, toneCurveId, kImageInputSocketId, nullptr, &rawToImageError),
        "RAW source should not connect directly to an image layer input");
    Require(!rawToImageError.empty(),
        "rejected RAW-to-image connection should explain why");

    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "manual RAW baseline should connect RAW Source to RAW Decode");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "manual RAW baseline should connect RAW Decode to Tone Curve");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "manual RAW baseline should connect Tone Curve to View Transform");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "manual RAW baseline should connect View Transform to Output");
    Require(graph.IsOutputConnected(),
        "manual RAW baseline should complete an output chain");
}

void TestRawWorkspaceFolderCatalogFoundation() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_workspace_test");
    const std::filesystem::path dayOne = root / "Day 1";
    const std::filesystem::path nested = dayOne / "Nested";

    WriteRawWorkspaceTestFile(root / "root_image.ARW");
    WriteRawWorkspaceTestFile(dayOne / "image_0001.DNG");
    WriteRawWorkspaceTestFile(nested / "image_0002.raw");
    WriteRawWorkspaceTestFile(dayOne / "sidecar.xmp");

    const auto sourceTreeSnapshot = [&]() {
        std::vector<std::string> paths;
        std::error_code snapshotError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 snapshotError)) {
            if (snapshotError) break;
            paths.push_back(
                std::filesystem::relative(entry.path(), root, snapshotError)
                    .generic_string());
            if (snapshotError) break;
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    };
    const std::vector<std::string> sourceTreeBefore = sourceTreeSnapshot();

    std::string error;
    Require(RawWorkspace::EnsureManagedFolders(root, &error),
        "RAW Workspace should create managed folders");
    const RawWorkspace::ManagedLayout layout = RawWorkspace::BuildManagedLayout(root);
    Require(std::filesystem::exists(layout.thumbnailsDirectory),
        "RAW Workspace thumbnails folder should exist");
    Require(std::filesystem::exists(layout.projectsDirectory),
        "RAW Workspace projects folder should exist");
    Require(std::filesystem::exists(layout.catalogDirectory),
        "RAW Workspace catalog folder should exist");
    const auto isOutsideSourceFolder = [&](const std::filesystem::path& path) {
        const std::filesystem::path relative =
            path.lexically_normal().lexically_relative(root.lexically_normal());
        return relative.empty() ||
            (relative.begin() != relative.end() && *relative.begin() == "..");
    };
    Require(!isOutsideSourceFolder(layout.thumbnailsDirectory) &&
            !isOutsideSourceFolder(layout.projectsDirectory) &&
            !isOutsideSourceFolder(layout.catalogDirectory),
        "RAW Workspace managed data must live inside the photo folder Closet");

    RawWorkspace::ScanProgress lastProgress;
    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate,
        [&](const RawWorkspace::ScanProgress& progress) {
            lastProgress = progress;
        });
    Require(scan.success, "RAW Workspace scan should succeed");
    Require(scan.sources.size() == 3, "RAW Workspace scan should find only user RAW files");
    Require(lastProgress.discoveredRawCount == 3, "RAW Workspace scan progress should count discovered RAW files");
    Require(scan.progress.managedDirectoriesSkipped == 1,
        "RAW Workspace must exclude its Closet from RAW scanning");

    const auto hasSource = [&](const std::string& relativePath) {
        return std::any_of(scan.sources.begin(), scan.sources.end(), [&](const RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == relativePath;
        });
    };
    Require(hasSource("root_image.ARW"), "RAW Workspace scan should include root RAW files");
    Require(hasSource("Day 1/image_0001.DNG"), "RAW Workspace scan should include subfolder RAW files");
    Require(hasSource("Day 1/Nested/image_0002.raw"), "RAW Workspace scan should include nested RAW files");

    const auto dayOneIt = std::find_if(scan.sources.begin(), scan.sources.end(), [&](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(dayOneIt != scan.sources.end(), "RAW Workspace test source should exist");
    Require(dayOneIt->parentFolderKey == "Day 1",
        "RAW Workspace source records should preserve parent folder grouping");

    RawWorkspace::SourceSetProjectMembership cachedMembership;
    cachedMembership.projectId = "warm-project";
    cachedMembership.projectName = "Warm Project";
    cachedMembership.projectPath =
        scan.layout.projectsDirectory / "Warm Project";
    cachedMembership.sourceSetId = "set-1";
    cachedMembership.sourceSetName = "Capture set";
    cachedMembership.projectIsMultiFrame = true;
    cachedMembership.projectCoverThumbnailCachePath =
        scan.layout.projectCoversDirectory /
        "warm-project-1234.png";
    std::error_code managedProjectError;
    std::filesystem::create_directories(
        cachedMembership.projectPath, managedProjectError);
    Require(!managedProjectError,
        "RAW Workspace isolation test should create its managed project folder");
    dayOneIt->sourceSetProjectMemberships.push_back(cachedMembership);

    Require(RawWorkspace::WriteCatalogSkeleton(scan.layout, scan.sources, "Day 1/image_0001.DNG", &error),
        "RAW Workspace should write catalog skeleton");
    Require(std::filesystem::exists(scan.layout.catalogPath),
        "RAW Workspace catalog.json should exist");
    Require(std::filesystem::exists(scan.layout.ratingsPath),
        "RAW Workspace ratings.json skeleton should exist");
    const nlohmann::json catalog = ReadRawWorkspaceJsonFile(scan.layout.catalogPath);
    Require(catalog.value("schema", std::string()) == "stack.rawWorkspace.catalog",
        "RAW Workspace catalog should preserve schema");
    Require(catalog["sources"].is_array(),
        "RAW Workspace catalog should serialize source array");
    Require(catalog["sources"].size() == scan.sources.size(),
        "RAW Workspace catalog should serialize every source");
    for (std::size_t index = 0; index < scan.sources.size(); ++index) {
        const auto& saved = catalog["sources"][index];
        Require(saved.value("relativePath", std::string()) ==
                scan.sources[index].relativePathKey &&
                saved.value("fingerprint", std::string()) ==
                scan.sources[index].fingerprint,
            "RAW Workspace compact catalog should preserve source identity");
        Require(!saved.contains("fileName") && !saved.contains("stem") &&
                !saved.contains("extension") &&
                !saved.contains("parentFolder"),
            "RAW Workspace compact catalog should omit derivable path fields");
    }
    Require(catalog.value("sourceIdentityAlgorithmVersion", 0) ==
            RawWorkspace::kSourceIdentityAlgorithmVersion,
        "RAW Workspace catalog should version reusable source identities");

    std::vector<RawWorkspace::SourceRecord> cachedSources;
    std::vector<RawWorkspace::SourceSetProjectCatalogEntry> cachedProjects;
    std::string cachedSelection;
    Require(RawWorkspace::LoadCatalogSnapshot(
            scan.layout,
            cachedSources,
            &cachedSelection,
            &error,
            &cachedProjects),
        "RAW Workspace should load its saved catalog as a warm-start snapshot");
    Require(cachedSources.size() == scan.sources.size(),
        "RAW Workspace warm-start catalog should restore every source");
    for (std::size_t index = 0; index < cachedSources.size(); ++index) {
        Require(cachedSources[index].relativePathKey ==
                    scan.sources[index].relativePathKey &&
                cachedSources[index].fileName == scan.sources[index].fileName &&
                cachedSources[index].stem == scan.sources[index].stem &&
                cachedSources[index].extension ==
                    scan.sources[index].extension &&
                cachedSources[index].parentFolderKey ==
                    scan.sources[index].parentFolderKey &&
                !cachedSources[index].thumbnail.absolutePath.empty() &&
                cachedSources[index].sourceIdentityAlgorithmVersion ==
                    RawWorkspace::kSourceIdentityAlgorithmVersion,
            "RAW Workspace warm start should reconstruct omitted catalog fields");
    }
    Require(cachedSelection == "Day 1/image_0001.DNG",
        "RAW Workspace warm-start catalog should restore its saved selection key");
    const auto cachedDayOne = std::find_if(
        cachedSources.begin(), cachedSources.end(),
        [](const RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == "Day 1/image_0001.DNG";
        });
    Require(cachedDayOne != cachedSources.end(),
        "RAW Workspace warm-start catalog should restore nested source keys");
    Require(cachedDayOne->absolutePath ==
            (scan.layout.workspaceRoot / "Day 1" / "image_0001.DNG")
                .lexically_normal(),
        "RAW Workspace warm-start paths should be derived from the active folder");
    Require(cachedDayOne->project.sourceRelativePathKey ==
            cachedDayOne->relativePathKey,
        "RAW Workspace warm-start project identity should follow the restored source");
    Require(cachedDayOne->sourceSetProjectMemberships.size() == 1 &&
            cachedDayOne->sourceSetProjectMemberships.front().projectIsMultiFrame,
        "RAW Workspace warm-start catalog should restore project memberships");
    Require(cachedProjects.size() == 1 &&
            cachedProjects.front().projectId == "warm-project" &&
            cachedProjects.front().multiFrameProject,
        "RAW Workspace warm-start catalog should reconstruct Gallery project cards");

    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = scan.layout.workspaceRoot;
    state.sources = scan.sources;
    Require(RawWorkspace::SelectSourceByKey(state, "Day 1/image_0001.DNG"),
        "RAW Workspace should select a source record");
    Require(state.selectedSourceKey == "Day 1/image_0001.DNG",
        "RAW Workspace selection should be preview-only state");

    std::error_code ec;
    Require(std::filesystem::exists(root / "Closet" / "workspace.json"),
        "Workspace initialization must publish its ownership manifest");

    ec.clear();
    std::filesystem::remove_all(root, ec);
}

void TestRawWorkspaceProjectRootIsolation() {
    namespace RawWorkspace = Stack::RawWorkspace;
    namespace Project = Stack::Project;

    const std::filesystem::path workspaceA =
        MakeTempDirectory("stack_raw_workspace_projects_a");
    const std::filesystem::path workspaceB =
        MakeTempDirectory("stack_raw_workspace_projects_b");
    const RawWorkspace::ManagedLayout layoutA =
        RawWorkspace::BuildManagedLayout(workspaceA);
    const RawWorkspace::ManagedLayout layoutB =
        RawWorkspace::BuildManagedLayout(workspaceB);
    Require(layoutA.projectsDirectory != layoutB.projectsDirectory,
        "Different RAW workspaces should have different project roots");
    Require(layoutA.projectsDirectory.parent_path() ==
            layoutA.dataDirectory,
        "RAW workspace projects should live beside the workspace catalog");

    const auto createRawProject = [](
        const std::filesystem::path& path,
        const std::string& projectId,
        const std::string& projectName) {
        Project::RawProjectSnapshot snapshot;
        snapshot.projectId = projectId;
        snapshot.projectName = projectName;
        snapshot.projectKindHint = StackBinaryFormat::kRawProjectKind;
        const Project::ProjectStoreOpenResult created =
            Project::CreateProjectStore(
                path,
                Project::ProjectStorageKind::DirectoryBundle,
                snapshot);
        Require(static_cast<bool>(created),
            "RAW workspace isolation test should create a project bundle");
    };
    createRawProject(
        layoutA.projectsDirectory / "Workspace A Project",
        "workspace-a-project",
        "Workspace A Project");
    createRawProject(
        layoutB.projectsDirectory / "Workspace B Project",
        "workspace-b-project",
        "Workspace B Project");

    Project::ProjectIndex::Get().Rebuild({
        layoutA.projectsDirectory,
        layoutB.projectsDirectory
    });
    const std::vector<Project::ProjectRecord> workspaceARecords =
        Project::ProjectIndex::Get().SnapshotForRoot(
            layoutA.projectsDirectory);
    Require(workspaceARecords.size() == 1u &&
            workspaceARecords.front().projectId == "workspace-a-project",
        "RAW workspace project discovery should exclude another workspace");

    std::vector<RawWorkspace::SourceRecord> sources;
    std::vector<RawWorkspace::SourceSetProjectCatalogEntry> projects;
    Require(RawWorkspace::DiscoverSourceSetProjects(
                layoutA, sources, projects),
        "RAW workspace project discovery should succeed for an empty source folder");
    Require(projects.size() == 1u &&
            projects.front().projectId == "workspace-a-project",
        "RAW Gallery project cards should come only from the active workspace root");

    std::error_code error;
    std::filesystem::remove_all(layoutA.catalogDirectory, error);
    error.clear();
    std::filesystem::remove_all(layoutB.catalogDirectory, error);
    error.clear();
    std::filesystem::remove_all(workspaceA, error);
    error.clear();
    std::filesystem::remove_all(workspaceB, error);
}

void TestRawWorkspaceThumbnailPipelineFoundation() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_thumbnail_test");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0001.DNG");
    WriteRawWorkspaceTestFile(root / "Day 2" / "image_0002.ARW");
    WriteRawWorkspaceTestFile(root / "image_0003.raw");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW Workspace thumbnail test scan should succeed");
    Require(scan.sources.size() == 3, "RAW Workspace thumbnail test should discover three source records");

    const auto stackProjectSnapshot = [&]() {
        std::vector<std::filesystem::path> projects;
        std::error_code snapshotError;
        if (!std::filesystem::exists(
                scan.layout.projectsDirectory, snapshotError) || snapshotError) {
            return projects;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 scan.layout.projectsDirectory,
                 std::filesystem::directory_options::skip_permission_denied,
                 snapshotError)) {
            if (snapshotError) {
                snapshotError.clear();
                continue;
            }
            if (entry.is_regular_file(snapshotError) && !snapshotError &&
                entry.path().extension() == ".stack") {
                projects.push_back(entry.path().lexically_normal());
            }
            snapshotError.clear();
        }
        std::sort(projects.begin(), projects.end());
        return projects;
    };
    const std::vector<std::filesystem::path> projectsBefore =
        stackProjectSnapshot();

    auto sourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(sourceIt != scan.sources.end(), "thumbnail test source should exist");

    RawWorkspace::ThumbnailInfo info = RawWorkspace::BuildThumbnailInfo(scan.layout, *sourceIt);
    Require(info.relativePath.generic_string() == "Day 1/image_0001.thumb.png",
        "thumbnail path should mirror source subfolder");
    Require(info.signatureRelativePath.generic_string() == "Day 1/image_0001.thumb.json",
        "thumbnail signature path should mirror source subfolder");
    Require(
        RawWorkspace::kFastNeutralThumbnailMaxDimension <
            RawWorkspace::kNeutralThumbnailMaxDimension,
        "the immediate RAW preview should remain smaller than the persistent thumbnail");
    const RawWorkspace::ThumbnailInfo transientInfo =
        RawWorkspace::BuildTransientThumbnailInfo(scan.layout, *sourceIt);
    Require(
        transientInfo.absolutePath.parent_path() != info.absolutePath.parent_path() &&
            transientInfo.relativePath.generic_string() ==
                "Day 1/image_0001.preview.png",
        "quick RAW previews should use a separate disposable cache tree");
    WriteRawWorkspaceTestFile(transientInfo.absolutePath);
    Require(
        RawWorkspace::RemoveTransientThumbnailCache(scan.layout) &&
            !std::filesystem::exists(scan.layout.transientThumbnailsDirectory),
        "the disposable RAW preview cache should be removed after promotion");

    WriteRawWorkspaceTestPng(info.absolutePath);
    WriteRawWorkspaceJsonFile(
        info.signaturePath,
        BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt)));
    RawWorkspace::ThumbnailStatus validStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(validStatus == RawWorkspace::ThumbnailStatus::Valid,
        "matching thumbnail signature should classify as valid");
    Require(sourceIt->thumbnail.width == 8 && sourceIt->thumbnail.height == 6,
        "thumbnail dimensions should be loaded from the PNG");

    WriteRawWorkspaceBinaryFile(info.absolutePath, "not-a-real-png-but-present");
    RawWorkspace::ThumbnailStatus invalidPngStatus =
        RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(invalidPngStatus == RawWorkspace::ThumbnailStatus::Stale,
        "matching signature with an invalid PNG should classify as stale");
    WriteRawWorkspaceTestPng(info.absolutePath);

    nlohmann::json nullDimensionSignature =
        BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt));
    nullDimensionSignature["thumbnailWidth"] = nullptr;
    nullDimensionSignature["thumbnailHeight"] = nullptr;
    WriteRawWorkspaceJsonFile(info.signaturePath, nullDimensionSignature);
    RawWorkspace::ThumbnailStatus nullDimensionStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(nullDimensionStatus == RawWorkspace::ThumbnailStatus::Valid,
        "null optional thumbnail dimensions should not invalidate a matching signature");
    Require(sourceIt->thumbnail.width == 8 && sourceIt->thumbnail.height == 6,
        "null optional thumbnail dimensions should use the PNG dimensions safely");

    nlohmann::json staleSignature = BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt));
    staleSignature["sourceFileSizeBytes"] = sourceIt->fileSizeBytes + 10;
    WriteRawWorkspaceJsonFile(info.signaturePath, staleSignature);
    RawWorkspace::ThumbnailStatus staleStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(staleStatus == RawWorkspace::ThumbnailStatus::Stale,
        "changed source signature should classify existing thumbnail as stale");

    nlohmann::json nullRequiredSignature =
        BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt));
    nullRequiredSignature["sourceRelativePath"] = nullptr;
    WriteRawWorkspaceJsonFile(info.signaturePath, nullRequiredSignature);
    RawWorkspace::ThumbnailStatus nullRequiredStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(nullRequiredStatus == RawWorkspace::ThumbnailStatus::Stale,
        "null required thumbnail signature fields should classify as stale");

    WriteRawWorkspaceTestFile(info.signaturePath);
    RawWorkspace::ThumbnailStatus malformedSignatureStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(malformedSignatureStatus == RawWorkspace::ThumbnailStatus::Stale,
        "malformed thumbnail signature JSON should classify as stale");

    auto missingIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 2/image_0002.ARW";
    });
    Require(missingIt != scan.sources.end(), "missing thumbnail test source should exist");
    RawWorkspace::ThumbnailStatus missingStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *missingIt);
    Require(missingStatus == RawWorkspace::ThumbnailStatus::Missing,
        "absent thumbnail files should classify as missing");

    RawWorkspace::ClassifyThumbnails(scan.layout, scan.sources);
    RawWorkspace::ThumbnailProgress progress = RawWorkspace::BuildThumbnailProgress(scan.sources);
    Require(progress.total == 3, "thumbnail progress should count all sources");
    Require(progress.queued >= 2, "missing/stale thumbnails should be counted as queued work");

    RawWorkspace::SourceRecord invalidSource = *missingIt;
    invalidSource.absolutePath.clear();
    RawWorkspace::ThumbnailGenerationResult generated =
        RawWorkspace::GenerateNeutralThumbnail(scan.layout, invalidSource);
    Require(!generated.success, "invalid RAW source should fail neutral thumbnail generation");

    const std::vector<std::filesystem::path> projectsAfter =
        stackProjectSnapshot();
    Require(projectsAfter == projectsBefore,
        "RAW Workspace thumbnail generation should not create .stack projects");

    std::error_code ec;
    ec.clear();
    std::filesystem::remove_all(root, ec);
}

void TestRawWorkspaceLoadingCancellationModel() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_loading_cancel_test");
    WriteRawWorkspaceTestFile(root / "image_0001.ARW");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0002.DNG");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0003.raw");

    bool cancelScan = false;
    RawWorkspace::ScanResult canceledScan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate,
        [&](const RawWorkspace::ScanProgress& progress) {
            if (progress.filesVisited >= 1) {
                cancelScan = true;
            }
        },
        [&]() {
            return cancelScan;
        });
    Require(!canceledScan.success, "RAW Workspace scan cancellation should stop the scan");
    Require(canceledScan.errorMessage.find("canceled") != std::string::npos,
        "RAW Workspace scan cancellation should report a canceled status");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW Workspace cancellation baseline scan should succeed");
    Require(scan.sources.size() == 3, "RAW Workspace cancellation baseline should find all sources");

    bool classifyCanceled = RawWorkspace::ClassifyThumbnails(
        scan.layout,
        scan.sources,
        RawWorkspace::kNeutralThumbnailMaxDimension,
        []() {
            return true;
        });
    Require(!classifyCanceled, "RAW Workspace thumbnail classification should honor cancellation");

    bool discoverCanceled = RawWorkspace::DiscoverProjects(
        scan.layout,
        scan.sources,
        []() {
            return true;
        });
    Require(!discoverCanceled, "RAW Workspace project discovery should honor cancellation");

    RawWorkspace::ThumbnailGenerationResult thumbnailCanceled =
        RawWorkspace::GenerateNeutralThumbnail(
            scan.layout,
            scan.sources.front(),
            RawWorkspace::kNeutralThumbnailMaxDimension,
            []() {
                return true;
            });
    Require(!thumbnailCanceled.success, "RAW thumbnail generation should stop when canceled before decode");
    Require(thumbnailCanceled.thumbnail.status == RawWorkspace::ThumbnailStatus::Queued,
        "canceled RAW thumbnail generation should return the source to queued status");

    Raw::RawImageData canceledRaw;
    Require(!Raw::RawLoader::LoadFile(
            scan.sources.front().absolutePath.string(),
            canceledRaw,
            []() {
                return true;
            }),
        "RAW loader should honor cancellation before starting decode work");
    Require(canceledRaw.metadata.error.find("canceled") != std::string::npos,
        "canceled RAW loader should report a canceled status");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void TestRawLabGalleryVisibleRange() {
    using Stack::Editor::RawLabInternal::ComputeGalleryVisibleRange;
    using Stack::Editor::RawLabInternal::ComputeFilmstripHorizontalLayout;
    using Stack::Editor::RawLabInternal::ShouldUseSideFilmstripPerforations;

    const auto empty = ComputeGalleryVisibleRange(
        0, 100.0f, 10.0f, 0.0f, 0.0f, 300.0f, 240.0f);
    Require(empty.first == 0 && empty.lastExclusive == 0,
        "RAW Lab visible-range helper should handle an empty gallery");

    const auto viewport = ComputeGalleryVisibleRange(
        10, 100.0f, 10.0f, 0.0f, 220.0f, 320.0f, 0.0f);
    Require(viewport.first == 2 && viewport.lastExclusive == 4,
        "RAW Lab visible-range helper should include the viewport boundary tiles");

    const auto prefetched = ComputeGalleryVisibleRange(
        10, 100.0f, 10.0f, 0.0f, 220.0f, 320.0f, 110.0f);
    Require(prefetched.first == 1 && prefetched.lastExclusive == 5,
        "RAW Lab visible-range helper should expand by the prefetch margin");

    const auto clamped = ComputeGalleryVisibleRange(
        3, 100.0f, 10.0f, 0.0f, -500.0f, 5000.0f, 240.0f);
    Require(clamped.first == 0 && clamped.lastExclusive == 3,
        "RAW Lab visible-range helper should clamp to the item count");

    const auto bottomBoundary = ComputeGalleryVisibleRange(
        10, 100.0f, 10.0f, 0.0f, 930.0f, 1000.0f, 0.0f);
    Require(bottomBoundary.first == 8 && bottomBoundary.lastExclusive == 10,
        "RAW Lab visible-range helper should keep the bottom scroll boundary populated");

    const auto openingFolder = ComputeGalleryVisibleRange(
        10, 100.0f, 10.0f, 0.0f, 0.0f, 1.0f, 240.0f);
    Require(openingFolder.first == 0 && openingFolder.lastExclusive > 0,
        "RAW Lab folder animations should prefetch the opening rows");

    const auto collapsedFolder = ComputeGalleryVisibleRange(
        0, 100.0f, 10.0f, 0.0f, 0.0f, 0.0f, 240.0f);
    Require(collapsedFolder.first == 0 && collapsedFolder.lastExclusive == 0,
        "RAW Lab collapsed folders should reserve no thumbnail range");

    const auto centeredFilmstrip = ComputeFilmstripHorizontalLayout(
        3u, 124.0f, 10.0f, 800.0f);
    Require(
        centeredFilmstrip.centered &&
            std::abs(centeredFilmstrip.contentWidth - 392.0f) < 0.001f &&
            std::abs(centeredFilmstrip.leadingOffset - 204.0f) < 0.001f,
        "short RAW filmstrips should be centered within any available width");
    const auto overflowingFilmstrip = ComputeFilmstripHorizontalLayout(
        8u, 124.0f, 10.0f, 800.0f);
    Require(
        !overflowingFilmstrip.centered &&
            std::abs(overflowingFilmstrip.leadingOffset) < 0.001f,
        "overflowing RAW filmstrips should retain their scrolling origin");
    Require(
        ShouldUseSideFilmstripPerforations(1000, 1120) &&
            ShouldUseSideFilmstripPerforations(1000, 1500) &&
            !ShouldUseSideFilmstripPerforations(1000, 1119) &&
            !ShouldUseSideFilmstripPerforations(1500, 1000),
        "filmstrip perforations should switch sides only at the portrait threshold");
}

void TestRawWorkspaceThumbnailScheduler() {
    using Stack::Editor::RawWorkspaceInternal::RawWorkspaceThumbnailScheduler;
    using Stack::Editor::RawWorkspaceInternal::RawWorkspaceThumbnailWorkItem;

    std::vector<RawWorkspaceThumbnailWorkItem> items;
    for (int index = 0; index < 4; ++index) {
        RawWorkspaceThumbnailWorkItem item;
        item.sourceIndex = static_cast<std::size_t>(index);
        item.sourceKey = "source-" + std::to_string(index);
        items.push_back(std::move(item));
    }

    RawWorkspaceThumbnailScheduler scheduler;
    scheduler.Reset(std::move(items));
    scheduler.Promote("source-2");

    RawWorkspaceThumbnailWorkItem next;
    Require(scheduler.TryTakeNext(next) && next.sourceKey == "source-2",
        "visible RAW thumbnail sources should be promoted ahead of FIFO work");

    std::vector<std::string> completed;
    while (scheduler.TryTakeNext(next)) {
        completed.push_back(next.sourceKey);
    }
    Require(completed.size() == 3 && completed[0] == "source-0" &&
            completed[1] == "source-1" && completed[2] == "source-3",
        "promoting visible RAW thumbnails should not starve the remaining queue");

    RawWorkspaceThumbnailWorkItem streamedA;
    streamedA.sourceKey = "streamed-a";
    RawWorkspaceThumbnailWorkItem streamedB;
    streamedB.sourceKey = "streamed-b";
    Require(scheduler.PushBack(streamedA) && scheduler.PushBack(streamedB) &&
            !scheduler.PushBack(streamedA),
        "live scan intake should append each source exactly once");
    scheduler.Promote("streamed-b");
    Require(scheduler.TryTakeNext(next) && next.sourceKey == "streamed-b" &&
            scheduler.TryTakeNext(next) && next.sourceKey == "streamed-a",
        "newly streamed work should retain priority promotion");

    RawWorkspaceThumbnailWorkItem canceledItem;
    canceledItem.sourceKey = "stale-folder/source";
    scheduler.Reset({ std::move(canceledItem) });
    scheduler.Clear();
    Require(!scheduler.TryTakeNext(next) && scheduler.Empty(),
        "canceling RAW thumbnail work should prevent stale queued results");

    items.clear();
    for (int index = 0; index < 10000; ++index) {
        RawWorkspaceThumbnailWorkItem item;
        item.sourceIndex = static_cast<std::size_t>(index);
        item.sourceKey = "large-" + std::to_string(index);
        items.push_back(std::move(item));
    }
    scheduler.Reset(std::move(items));
    scheduler.Promote("missing-source");
    scheduler.Promote({ "large-9999", "large-7000", "large-7000" });
    RawWorkspaceThumbnailWorkItem repair;
    repair.sourceKey = "repair";
    Require(scheduler.PushFront(repair) && !scheduler.PushFront(repair),
        "thumbnail repair should enqueue a source only once");
    Require(scheduler.Size() == 10001 &&
            scheduler.TryTakeNext(next) && next.sourceKey == "repair",
        "new repairs should precede promoted catalog entries");
    Require(scheduler.TryTakeNext(next) && next.sourceKey == "large-7000" &&
            scheduler.TryTakeNext(next) && next.sourceKey == "large-9999",
        "batch promotion should retain catalog order regardless of promotion order");
    for (int index = 0; index < 9999; ++index) {
        if (index == 7000) continue;
        Require(scheduler.TryTakeNext(next) &&
                next.sourceIndex == static_cast<std::size_t>(index),
            "large thumbnail queues should drain in order without losing entries");
    }
    Require(scheduler.Empty() && !scheduler.TryTakeNext(next),
        "repeated promotions should not leave stale work after draining");
    Require(scheduler.PushFront(repair),
        "a completed source should be eligible for another repair");
    scheduler.Reset({});
    Require(scheduler.Empty() && scheduler.PushFront(repair) &&
            scheduler.TryTakeNext(next) && next.sourceKey == "repair",
        "reset should discard both priority and source membership state");
}

void TestRawWorkspaceAppStateUsesCurrentSchema() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_workspace_json_test");
    const std::filesystem::path statePath = root / "RawWorkspaceState.json";
    const std::filesystem::path recentRoot = root / "Recent Workspace";

    nlohmann::json state = nlohmann::json::object();
    state["schema"] = "stack.rawWorkspace.appState";
    state["schemaVersion"] = 1;
    WriteRawWorkspaceJsonFile(statePath, state);

    RawWorkspace::AppState loaded;
    std::string error;
    Require(!RawWorkspace::LoadAppState(statePath, loaded, &error),
        "RAW Workspace should reject an obsolete app-state schema");

    state["schemaVersion"] = 2;
    state["lastWorkspaceRoot"] = nullptr;
    state["rawLabToolRailWidth"] = "not-a-number";
    state["rawLabLowerShelfHeight"] = nullptr;
    state["rawLabLowerShelfOpen"] = 1;
    state["rawViewportTargetFps"] = "bad-value";
    state["rawViewportFadeBelowFps"] = "bad-value";
    state["rawLabFilmstripHeight"] = "not-a-number";
    state["rawLabGalleryThumbnailScale"] = "not-a-number";
    state["rawLabActiveTool"] = nullptr;
    state["rawLabActivePointCurve"] = "not-an-index";
    state["rawLabColorWarpLiveCloud"] = "not-a-boolean";
    state["rawLabLastGalleryHost"] = nullptr;
    state["rawLabGalleryDisplayMode"] = nullptr;
    state["rawLabGalleryContentMode"] = nullptr;
    state["recentWorkspaces"] = nlohmann::json::array({
        nullptr,
        7,
        recentRoot.string()
    });
    WriteRawWorkspaceJsonFile(statePath, state);

    Require(RawWorkspace::LoadAppState(statePath, loaded, &error),
        "current RAW Workspace app state should tolerate malformed optional values");
    Require(loaded.rawViewportTargetFps == 30,
        "malformed or missing viewport target retains the 30 FPS default");
    Require(loaded.rawViewportFadeBelowFps == 30 && loaded.smoothRawViewportUpdates,
        "Malformed or missing fade settings retain enabled smoothing at 30 FPS");
    Require(loaded.lastWorkspaceRoot.empty(),
        "null last workspace root should load as empty");
    Require(loaded.recentWorkspaceRoots.size() == 1,
        "recent workspace loader should ignore null and non-string entries");
    Require(loaded.rawLabToolRailWidth == 0.0f &&
            loaded.rawLabLowerShelfHeight == 0.0f &&
            !loaded.rawLabLowerShelfOpen &&
            loaded.rawLabFilmstripHeight == 0.0f &&
            std::abs(loaded.rawLabGalleryThumbnailScale - 1.0f) < 0.001f &&
            loaded.rawLabActiveTool == 0 &&
            loaded.rawLabActivePointCurve == 0 &&
            !loaded.rawLabColorWarpLiveCloud &&
            loaded.rawLabLastGalleryHost == 1 &&
            loaded.rawLabGalleryDisplayMode == 0 &&
            loaded.rawLabGalleryContentMode == 0,
        "RAW Lab optional layout fields should retain safe defaults when missing or malformed");

    loaded.rawViewportTargetFps = 60;
    loaded.rawViewportFadeBelowFps = 360;
    loaded.smoothRawViewportUpdates = false;
    loaded.rawLabToolRailWidth = 364.0f;
    loaded.rawLabLowerShelfHeight = 176.0f;
    loaded.rawLabLowerShelfOpen = true;
    loaded.rawLabFilmstripHeight = 148.0f;
    loaded.rawLabGalleryThumbnailScale = 1.4f;
    loaded.rawLabActiveTool = 9;
    loaded.rawLabActivePointCurve = 3;
    loaded.rawLabColorWarpLiveCloud = true;
    loaded.rawLabLastGalleryHost = 3;
    loaded.rawLabGalleryDisplayMode = 1;
    loaded.rawLabGalleryContentMode = 1;
    Stack::RawWorkspace::RawGalleryManualGrouping manualGrouping;
    manualGrouping.stacks = {{ "a.dng", "b.dng" }};
    manualGrouping.detachedSourceKeys = { "c.dng" };
    manualGrouping.sortMode = Stack::RawWorkspace::
        RawGalleryFilmstripSortMode::Manual;
    manualGrouping.manualSourceOrder = {
        "c.dng", "a.dng", "b.dng" };
    loaded.rawLabManualGroupings.emplace(
        recentRoot.lexically_normal().generic_string(),
        manualGrouping);
    Require(RawWorkspace::SaveAppState(statePath, loaded, &error),
        "RAW Workspace should persist optional RAW Lab layout fields");
    RawWorkspace::AppState reloaded;
    Require(RawWorkspace::LoadAppState(statePath, reloaded, &error),
        "RAW Workspace should reload optional RAW Lab layout fields");
    Require(reloaded.rawViewportTargetFps == 30 && reloaded.rawViewportFadeBelowFps == 30 && reloaded.smoothRawViewportUpdates,
        "Workspace saves must no longer own application viewport preferences");
    Require(std::abs(reloaded.rawLabToolRailWidth - 364.0f) < 0.001f &&
            std::abs(reloaded.rawLabLowerShelfHeight - 176.0f) < 0.001f &&
            reloaded.rawLabLowerShelfOpen &&
            std::abs(reloaded.rawLabFilmstripHeight - 148.0f) < 0.001f &&
            std::abs(reloaded.rawLabGalleryThumbnailScale - 1.4f) < 0.001f &&
            reloaded.rawLabActiveTool == 9 &&
            reloaded.rawLabActivePointCurve == 3 &&
            reloaded.rawLabColorWarpLiveCloud &&
            reloaded.rawLabLastGalleryHost == 3 &&
            reloaded.rawLabGalleryDisplayMode == 1 &&
            reloaded.rawLabGalleryContentMode == 1 &&
            reloaded.rawLabManualGroupings.empty(),
        "current RAW Lab tool, Gallery, and layout state should round-trip");

    for (int pass = 0; pass < 16; ++pass) {
        for (int tool = 0; tool <= 7; ++tool) {
            loaded.rawLabActiveTool = tool;
            loaded.rawLabLowerShelfOpen = ((pass + tool) % 2) != 0;
            Require(RawWorkspace::SaveAppState(statePath, loaded, &error),
                "rapid RAW Lab tool switching should persist app state");
            RawWorkspace::AppState toolReloaded;
            Require(RawWorkspace::LoadAppState(statePath, toolReloaded, &error),
                "rapid RAW Lab tool switching should reload app state");
            Require(toolReloaded.rawLabActiveTool == tool &&
                    toolReloaded.rawLabLowerShelfOpen == loaded.rawLabLowerShelfOpen,
                "every RAW Lab tool and adjacent layout state should survive repeated switching");
        }
    }

    RawWorkspace::AppState latestState = loaded;
    latestState.rawLabActiveTool = 3;
    latestState.rawLabToolRailWidth = 397.0f;
    Require(RawWorkspace::SaveAppState(statePath, latestState, &error),
        "RAW Lab latest-wins app-state fixture should persist its accepted state");
    RawWorkspace::AppState supersededState = latestState;
    supersededState.rawLabActiveTool = 1;
    supersededState.rawLabToolRailWidth = 281.0f;
    bool staleCommitChecked = false;
    Require(RawWorkspace::SaveAppStateIfCurrent(
            statePath,
            supersededState,
            [&]() {
                staleCommitChecked = true;
                return false;
            },
            &error),
        "a superseded RAW Lab app-state write should cancel without reporting file corruption");
    RawWorkspace::AppState afterSupersededWrite;
    Require(staleCommitChecked &&
            RawWorkspace::LoadAppState(statePath, afterSupersededWrite, &error),
        "a superseded RAW Lab app-state write should leave a readable accepted state");
    Require(afterSupersededWrite.rawLabActiveTool == 3 &&
            std::abs(afterSupersededWrite.rawLabToolRailWidth - 397.0f) < 0.001f,
        "a stale RAW Lab app-state generation must not overwrite the latest accepted tool or layout");

    const std::filesystem::path malformedPath = root / "MalformedRawWorkspaceState.json";
    WriteRawWorkspaceTestFile(malformedPath);
    loaded = {};
    error.clear();
    Require(RawWorkspace::LoadAppState(malformedPath, loaded, &error),
        "malformed RAW Workspace app state should be ignored without aborting");
    Require(loaded.lastWorkspaceRoot.empty() && loaded.recentWorkspaceRoots.empty(),
        "malformed RAW Workspace app state should leave default state");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void TestRawWorkspaceGalleryPresentation() {
    namespace RawWorkspace = Stack::RawWorkspace;

    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = "C:/Workspace";

    RawWorkspace::SourceRecord rootSource;
    rootSource.relativePathKey = "root_image.ARW";
    rootSource.relativePath = "root_image.ARW";
    rootSource.fileName = "root_image.ARW";
    rootSource.fileSizeBytes = 2048;
    rootSource.thumbnail.status = RawWorkspace::ThumbnailStatus::Missing;

    RawWorkspace::SourceRecord dayOneSource;
    dayOneSource.relativePathKey = "Day 1/image_0001.DNG";
    dayOneSource.relativePath = "Day 1/image_0001.DNG";
    dayOneSource.fileName = "image_0001.DNG";
    dayOneSource.parentFolderKey = "Day 1";
    dayOneSource.fileSizeBytes = 4096;
    dayOneSource.thumbnail.status = RawWorkspace::ThumbnailStatus::Ready;
    dayOneSource.thumbnail.relativePath = "Day 1/image_0001.thumb.png";

    RawWorkspace::SourceRecord secondDayOneSource;
    secondDayOneSource.relativePathKey = "Day 1/image_0002.ARW";
    secondDayOneSource.relativePath = "Day 1/image_0002.ARW";
    secondDayOneSource.fileName = "image_0002.ARW";
    secondDayOneSource.parentFolderKey = "Day 1";
    secondDayOneSource.thumbnail.status = RawWorkspace::ThumbnailStatus::Failed;

    state.sources = { rootSource, dayOneSource, secondDayOneSource };
    state.selectedSourceKey = dayOneSource.relativePathKey;

    const RawWorkspace::GalleryPresentation presentation = RawWorkspace::BuildGalleryPresentation(state);
    Require(presentation.totalSources == 3,
        "RAW Workspace gallery presentation should count all sources");
    Require(presentation.groups.size() == 2,
        "RAW Workspace gallery presentation should preserve folder groups");
    Require(presentation.groups[0].label == "Workspace root",
        "RAW Workspace gallery presentation should label root sources");
    Require(presentation.groups[1].folderKey == "Day 1" && presentation.groups[1].sources.size() == 2,
        "RAW Workspace gallery presentation should group sibling folder sources");
    Require(presentation.hasSelection && presentation.selectedSourceKey == "Day 1/image_0001.DNG",
        "RAW Workspace gallery presentation should preserve preview-only selection");
    Require(presentation.groups[1].sources[0].selected,
        "RAW Workspace gallery source view should mark selected source");
    Require(presentation.groups[1].sources[0].projectStatus == RawWorkspace::ProjectStatus::Unknown,
        "Phase 3 project status should remain an unknown placeholder");
    Require(std::string(RawWorkspace::ProjectStatusLabel(presentation.groups[1].sources[0].projectStatus)) == "Unknown",
        "RAW Workspace project status label should expose the Phase 3 placeholder");
    Require(presentation.readyThumbnailCount == 1 &&
            presentation.queuedThumbnailCount == 1 &&
            presentation.failedThumbnailCount == 1,
        "RAW Workspace gallery presentation should summarize thumbnail states");

    RawWorkspace::WorkspaceState overlaidState = state;
    RawWorkspace::SourceSetProjectCatalogEntry savedSingle;
    savedSingle.projectId = "saved-single";
    savedSingle.projectName = "Image 0001 Edit";
    savedSingle.absolutePath =
        "C:/Stack Projects/Image 0001 Edit--saved/project.stack";
    savedSingle.coverThumbnailCachePath =
        "C:/Workspace/Stack RAW Catalog/project-covers/saved-single.png";
    savedSingle.totalFrameCount = 1u;
    savedSingle.multiFrameProject = false;
    overlaidState.sourceSetProjects.push_back(savedSingle);
    RawWorkspace::SourceSetProjectMembership savedMembership;
    savedMembership.projectId = savedSingle.projectId;
    savedMembership.projectName = savedSingle.projectName;
    savedMembership.projectPath = savedSingle.absolutePath;
    overlaidState.sources[1].sourceSetProjectMemberships.push_back(
        savedMembership);
    const RawWorkspace::GalleryPresentation overlaidPresentation =
        RawWorkspace::BuildGalleryPresentation(overlaidState);
    const RawWorkspace::GallerySourceView& overlaidSource =
        overlaidPresentation.groups[1].sources[0];
    Require(overlaidPresentation.totalSources == 3 &&
            overlaidPresentation.groups[1].sources.size() == 2u &&
            overlaidSource.representsProject &&
            !overlaidSource.projectIsMultiFrame &&
            overlaidSource.projectId == savedSingle.projectId &&
            overlaidSource.projectPath == savedSingle.absolutePath &&
            overlaidSource.projectCoverThumbnailCachePath ==
                savedSingle.coverThumbnailCachePath,
        "Gallery mode should keep every folder image in place while overlaying its saved single-RAW project and edited cover");

    RawWorkspace::WorkspaceState projectOnlyState;
    RawWorkspace::SourceSetProjectCatalogEntry retainedProject;
    retainedProject.projectId = "retained-project";
    retainedProject.projectName = "Retained HDR";
    retainedProject.absolutePath = "C:/Stack Projects/Retained HDR";
    retainedProject.totalFrameCount = 3u;
    projectOnlyState.sourceSetProjects.push_back(retainedProject);
    const RawWorkspace::GalleryPresentation projectOnlyPresentation =
        RawWorkspace::BuildGalleryPresentation(projectOnlyState);
    Require(projectOnlyPresentation.totalSources == 0 &&
            projectOnlyPresentation.projects.size() == 1u &&
            projectOnlyPresentation.projects.front().projectName ==
                "Retained HDR",
        "Gallery project cards should remain discoverable when their browsed originals were removed");
    Require(RawWorkspace::ResolveExclusiveGalleryPlacement(RawWorkspace::GalleryPlacementMode::RightGallery) ==
            RawWorkspace::GalleryPlacementMode::RightGallery,
        "RAW Workspace right gallery placement should be valid");
    Require(RawWorkspace::ResolveExclusiveGalleryPlacement(RawWorkspace::GalleryPlacementMode::BottomFilmstrip) ==
            RawWorkspace::GalleryPlacementMode::BottomFilmstrip,
        "RAW Workspace bottom filmstrip placement should be valid");
}

void TestRawWorkspacePanelStateModel() {
    namespace RawWorkspace = Stack::RawWorkspace;
    namespace RawRecipe = Stack::RawRecipe;

    Require(!RawWorkspace::BuildRawPanelState(nullptr).recipeControlsEditable,
        "RAW panel without a selection should not expose recipe controls");

    RawWorkspace::SourceRecord source;
    source.relativePathKey = "Day 1/image_0001.DNG";
    source.fileName = "image_0001.DNG";
    source.project.status = RawWorkspace::ProjectStatus::NoProject;
    source.project.mode = RawWorkspace::RawProjectMode::UnifiedLayers;

    RawWorkspace::RawPanelState previewState = RawWorkspace::BuildRawPanelState(&source);
    Require(previewState.recipeControlsEditable,
        "Preview-only RAW selections should allow a first recipe edit");
    Require(previewState.editCreatesProject,
        "Preview-only RAW controls should report that the edit creates a project");
    Require(!previewState.openGraphEnabled,
        "Preview-only RAW selections should not open a graph project");
    Require(previewState.graphTooltip == "Make an edit to create this RAW project first.",
        "Preview-only Open In Graph tooltip should explain the first-edit requirement");

    source.project.status = RawWorkspace::ProjectStatus::Existing;
    source.project.mode = RawWorkspace::RawProjectMode::UnifiedLayers;
    RawWorkspace::RawPanelState editedState = RawWorkspace::BuildRawPanelState(&source);
    Require(editedState.recipeControlsEditable && editedState.openGraphEnabled,
        "Recipe-backed RAW projects should be editable and openable in the graph");

    source.project.mode = RawWorkspace::RawProjectMode::CustomGraph;
    source.project.readOnlyReason.clear();
    RawWorkspace::RawPanelState customState = RawWorkspace::BuildRawPanelState(&source);
    Require(!customState.recipeControlsEditable && customState.openGraphEnabled,
        "Custom Graph Mode should keep graph access but block RAW recipe controls");
    Require(customState.readOnlyMessage.find("read-only") != std::string::npos,
        "Custom Graph Mode should expose a RAW tab read-only message");

    source.project.mode = RawWorkspace::RawProjectMode::Unknown;
    source.project.errorMessage = "Unsupported RAW Workspace mode";
    RawWorkspace::RawPanelState unknownModeState = RawWorkspace::BuildRawPanelState(&source);
    Require(!unknownModeState.recipeControlsEditable && unknownModeState.openGraphEnabled,
        "Unsupported RAW project modes should keep graph access but block RAW recipe controls");
    Require(unknownModeState.readOnlyMessage.find("Unsupported") != std::string::npos,
        "Unsupported RAW project modes should expose a read-only explanation");

    source.project.status = RawWorkspace::ProjectStatus::Invalid;
    RawWorkspace::RawPanelState invalidState = RawWorkspace::BuildRawPanelState(&source);
    Require(!invalidState.recipeControlsEditable && !invalidState.openGraphEnabled,
        "Invalid RAW projects should block recipe controls and graph opening");

    RawRecipe::RawDevelopmentRecipe recipe = RawRecipe::MakeDefaultRecipe("image_0001.DNG", "image_0001.DNG");
    const RawRecipe::WhiteBalanceMode modes[] = {
        RawRecipe::WhiteBalanceMode::AsShot,
        RawRecipe::WhiteBalanceMode::CustomMultipliers
    };
    for (RawRecipe::WhiteBalanceMode mode : modes) {
        recipe.whiteBalance.mode = mode;
        recipe.whiteBalance.hasMultipliers =
            mode == RawRecipe::WhiteBalanceMode::CustomMultipliers;
        const RawRecipe::RawDevelopmentRecipe roundTrip =
            RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(recipe));
        Require(roundTrip.whiteBalance.mode == mode,
            "RAW recipe should preserve each current white-balance panel mode");
    }
}

void AttachManagedRawAssetToTestProject(
    const std::filesystem::path& projectPath,
    const Stack::RawWorkspace::SourceRecord& source) {
    Stack::Project::ProjectStoreOpenResult opened =
        Stack::Project::OpenProjectStore(projectPath);
    Require(static_cast<bool>(opened),
        "RAW project test should reopen its unified project store");
    Stack::Project::RawProjectSnapshot snapshot = opened.snapshot;
    const Stack::Project::ProjectStoreTransaction transaction =
        opened.store->BeginTransaction(snapshot.persistedStorageRevision);
    Require(static_cast<bool>(transaction),
        "RAW project test should begin managed-asset staging");
    Stack::Project::EmbeddedAssetRecord asset;
    std::string error;
    Require(opened.store->StageAssetFile(
            transaction,
            source.absolutePath,
            Stack::Project::MultiFrameInputFamily::Raw,
            nlohmann::json::object(),
            asset,
            &error),
        "RAW project test should stage its managed original");
    if (!Stack::Project::FindEmbeddedAsset(snapshot, asset.assetId)) {
        snapshot.embeddedAssets.push_back(asset);
    }
    snapshot.rawWorkspaceData["managedAssetId"] = asset.assetId;
    snapshot.lifecycle.initialAssetIds.push_back(asset.assetId);
    snapshot.dirtyRevision = std::max<std::uint64_t>(snapshot.dirtyRevision, 1u);
    const Stack::Project::ProjectStoreCommitResult committed =
        opened.store->Commit(transaction, snapshot);
    Require(static_cast<bool>(committed),
        "RAW project test should commit its managed original");
}

void BindDocumentSourceAssetForInitialRawCommit(
    StackBinaryFormat::ProjectDocument& document) {
    const std::vector<std::uint8_t> sourceBytes(
        document.sourceImageBytes.begin(), document.sourceImageBytes.end());
    const Stack::RawEvidence::SourceIdentity identity =
        Stack::RawEvidence::ComputeSourceIdentity(sourceBytes);
    Require(identity.valid,
        "RAW project test source payload should have a stable identity");
    document.rawWorkspaceData["managedAssetId"] =
        Stack::Project::MakeAssetId(identity.sha256, identity.byteSize);
}

void TestRawWorkspaceProjectLifecycleModel() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_project_lifecycle_test");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0001.DNG");
    WriteRawWorkspaceTestFile(root / "Day 2" / "image_0002.ARW");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(root, RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW Workspace project lifecycle scan should succeed");
    const std::filesystem::path projectRoot =
        MakeTempDirectory("stack_raw_project_lifecycle_store_test");
    scan.layout.projectsDirectory = projectRoot;
    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    Require(scan.sources.size() == 2, "RAW Workspace project lifecycle test should find two RAW files");

    auto sourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(sourceIt != scan.sources.end(), "RAW project lifecycle source should exist");
    Require(sourceIt->project.status == RawWorkspace::ProjectStatus::NoProject,
        "RAW source should start preview-only with no project");

    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = scan.layout.workspaceRoot;
    state.sources = scan.sources;
    Require(RawWorkspace::SelectSourceByKey(state, "Day 1/image_0001.DNG"),
        "RAW project lifecycle selection should succeed");
    Require(!std::filesystem::exists(
            scan.layout.projectsDirectory / "image_0001" / "project.stack"),
        "Preview selection should not create a RAW project");

    RawWorkspace::SourceRecord source = *sourceIt;
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fileSizeBytes = static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.preToneExposureEv = 1.0f;

    EditorNodeGraph::Graph graph;
    EditorNodeGraph::RawDevelopmentPayload rawPayload;
    rawPayload.recipe = recipe;
    rawPayload.projectStatus = "Edited";
    rawPayload.edited = true;
    const int rawDevelopmentId = NodeId(graph.AddRawDevelopmentNode(rawPayload, { 0.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 260.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(rawDevelopmentId, EditorNodeGraph::kImageOutputSocketId, outputId, EditorNodeGraph::kImageInputSocketId),
        "RAW project lifecycle graph should connect compact RAW Development to output");
    const nlohmann::json downstreamGraph = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), graph);

    StackBinaryFormat::ProjectDocument document;
    document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    document.metadata.projectName = "image_0001";
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    document.thumbnailBytes = { 1, 2, 3 };
    document.sourceImageBytes = { 4, 5, 6 };
    document.pipelineData = downstreamGraph;
    Require(RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(source, recipe, downstreamGraph, document),
        "RAW project lifecycle should apply RAW metadata to a project document");
    Require(document.rawWorkspaceData.value("rawWorkspaceMode", std::string()) == "recipe-backed",
        "New RAW edit projects should be recipe-backed");
    Require(document.rawWorkspaceData["rawSourceRef"].value("linked", false),
        "New RAW edit projects should link RAW files by default");
    Require(document.rawWorkspaceData.contains("managedRawSection") &&
            document.rawWorkspaceData.contains("customRawSection"),
        "RAW project data should reserve managed/custom mode fields");
    document.rawWorkspaceData["readOnlyReason"] = "current read-only metadata";
    document.rawWorkspaceData["futureRawWorkspaceKey"] = { { "sentinel", true } };
    Stack::RawRecipe::RawDevelopmentRecipe editedRecipe = recipe;
    editedRecipe.preToneExposureEv = 1.5f;
    Require(RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(source, editedRecipe, downstreamGraph, document),
        "RAW project lifecycle should update owned recipe metadata in place");
    Require(document.rawWorkspaceData["managedRawSection"].is_null() &&
            document.rawWorkspaceData["customRawSection"].is_null(),
        "RAW project lifecycle saves should preserve current empty ownership sections");
    Require(document.rawWorkspaceData.value("readOnlyReason", std::string()) == "current read-only metadata",
        "RAW project lifecycle saves should preserve read-only metadata");
    Require(!document.rawWorkspaceData.contains("futureRawWorkspaceKey"),
        "RAW project lifecycle saves should drop fields outside the current RAW workspace schema");
    recipe = editedRecipe;

    const std::filesystem::path projectRelative = RawWorkspace::BuildProjectRelativePathForSource(source);
    Require(projectRelative.generic_string() == "image_0001",
        "RAW project path should use one top-level working project folder");
    const std::filesystem::path projectPath = scan.layout.projectsDirectory / projectRelative;
    std::filesystem::create_directories(projectPath.parent_path());
    BindDocumentSourceAssetForInitialRawCommit(document);
    Require(StackBinaryFormat::WriteProjectFile(projectPath, document),
        "RAW project lifecycle should write a .stack project");
    AttachManagedRawAssetToTestProject(projectPath, source);

    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    sourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& candidate) {
        return candidate.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(sourceIt != scan.sources.end(), "RAW project lifecycle source should still exist after discovery");
    Require(sourceIt->project.status == RawWorkspace::ProjectStatus::Embedded,
        "RAW project discovery should attach existing projects");
    Require(sourceIt->project.relativePath.generic_string() == "image_0001",
        "RAW project discovery should report the relative project path");

    StackBinaryFormat::ProjectDocument loadedDocument;
    Require(StackBinaryFormat::ReadProjectFile(projectPath, loadedDocument),
        "RAW project lifecycle should reload the project file");
    RawWorkspace::ProjectInfo loadedInfo;
    Stack::RawRecipe::RawDevelopmentRecipe loadedRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedDocument, loadedInfo, &loadedRecipe),
        "RAW project lifecycle should parse project RAW metadata");
    Require(!loadedInfo.linkedRaw && loadedInfo.embeddedRaw,
        "Reloaded RAW project should use its managed original");
    Require(loadedInfo.mode == RawWorkspace::RawProjectMode::UnifiedLayers,
        "Reloaded RAW project should preserve recipe-backed mode");
    Require(std::abs(loadedRecipe.preToneExposureEv - 1.5f) < 0.001f,
        "Reloaded RAW project should preserve recipe edits");
    Require(loadedDocument.metadata.projectKind == StackBinaryFormat::kRawProjectKind,
        "RAW project lifecycle should identify the document as a RAW project");
    EditorNodeGraph::Graph loadedGraph;
    EditorNodeGraph::DeserializeGraphPayload(
        loadedDocument.pipelineData,
        loadedGraph,
        2,
        {},
        0,
        0,
        0);
    Require(loadedGraph.IsOutputConnected(),
        "RAW project lifecycle should store a usable canonical graph in pipelineData");
    Require(!loadedDocument.rawWorkspaceData.contains("downstreamGraph"),
        "RAW project lifecycle should not duplicate the canonical graph in RAW metadata");

    StackBinaryFormat::ProjectDocument missingModeDocument = loadedDocument;
    missingModeDocument.rawWorkspaceData.erase("rawWorkspaceMode");
    RawWorkspace::ProjectInfo missingModeInfo;
    Require(RawWorkspace::ReadProjectInfoFromDocument(missingModeDocument, missingModeInfo, nullptr),
        "RAW project lifecycle should parse malformed metadata enough to report invalid status");
    Require(missingModeInfo.status == RawWorkspace::ProjectStatus::Invalid &&
            missingModeInfo.mode == RawWorkspace::RawProjectMode::Unknown,
        "Missing RAW project mode should be invalid instead of recipe-backed");

    StackBinaryFormat::ProjectDocument futureModeDocument = loadedDocument;
    futureModeDocument.rawWorkspaceData["rawWorkspaceMode"] = "future-managed-mode";
    RawWorkspace::ProjectInfo futureModeInfo;
    Require(RawWorkspace::ReadProjectInfoFromDocument(futureModeDocument, futureModeInfo, nullptr),
        "RAW project lifecycle should parse future-mode metadata enough to report invalid status");
    Require(futureModeInfo.status == RawWorkspace::ProjectStatus::Invalid &&
            futureModeInfo.mode == RawWorkspace::RawProjectMode::Unknown,
        "Unsupported RAW project mode should be invalid instead of recipe-backed");

    auto secondSourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& candidate) {
        return candidate.relativePathKey == "Day 2/image_0002.ARW";
    });
    Require(secondSourceIt != scan.sources.end(), "RAW project lifecycle second source should exist");
    Stack::RawRecipe::RawDevelopmentRecipe futureModeRecipe =
        Stack::RawRecipe::MakeDefaultRecipe(secondSourceIt->absolutePath.string(), secondSourceIt->fileName);
    futureModeRecipe.source.relativePathKey = secondSourceIt->relativePathKey;
    futureModeRecipe.source.fileSizeBytes = static_cast<std::uint64_t>(secondSourceIt->fileSizeBytes);
    futureModeRecipe.source.modifiedTimeTicks = secondSourceIt->modifiedTimeTicks;
    StackBinaryFormat::ProjectDocument futureModeProject;
    futureModeProject.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    futureModeProject.metadata.projectName = "image_0002";
    futureModeProject.metadata.sourceWidth = 1;
    futureModeProject.metadata.sourceHeight = 1;
    futureModeProject.thumbnailBytes = { 1, 2, 3 };
    futureModeProject.sourceImageBytes = { 4, 5, 6 };
    futureModeProject.pipelineData = downstreamGraph;
    Require(RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
            *secondSourceIt,
            futureModeRecipe,
            downstreamGraph,
            futureModeProject),
        "RAW project lifecycle should create a second project document");
    futureModeProject.rawWorkspaceData["rawWorkspaceMode"] = "future-managed-mode";
    const std::filesystem::path futureModeProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(*secondSourceIt);
    std::filesystem::create_directories(futureModeProjectPath.parent_path());
    BindDocumentSourceAssetForInitialRawCommit(futureModeProject);
    Require(StackBinaryFormat::WriteProjectFile(futureModeProjectPath, futureModeProject),
        "RAW project lifecycle should write a future-mode test project");
    AttachManagedRawAssetToTestProject(futureModeProjectPath, *secondSourceIt);
    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    secondSourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& candidate) {
        return candidate.relativePathKey == "Day 2/image_0002.ARW";
    });
    Require(secondSourceIt != scan.sources.end(), "RAW project lifecycle second source should remain discoverable");
    Require(secondSourceIt->project.status == RawWorkspace::ProjectStatus::Invalid &&
            secondSourceIt->project.mode == RawWorkspace::RawProjectMode::Unknown,
        "Discovery should keep unsupported project modes invalid and read-only");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ec.clear();
    std::filesystem::remove_all(projectRoot, ec);
}

nlohmann::json RawLabUntouchedRecipeFields(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    nlohmann::json state = Stack::RawRecipe::SerializeRecipe(recipe);
    if (state.contains("sourceRef") && state["sourceRef"].is_object()) {
        state["sourceRef"].erase("sourcePath");
    }
    state.erase("exposureEv");
    state.erase("finishTone");
    state.erase("viewTransform");
    if (state.contains("localRange") && state["localRange"].is_object()) {
        state["localRange"].erase("enabled");
        state["localRange"].erase("points");
        state["localRange"].erase("targetZones");
    }
    return state;
}

void TestRawLabToolSequencePreservesHiddenFieldsThroughProjectReload() {
    namespace RawRecipe = Stack::RawRecipe;
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root =
        MakeTempDirectory("stack_raw_lab_hidden_field_roundtrip_test");
    const std::filesystem::path sourcePath = root / "Lab Sequence.DNG";
    WriteRawWorkspaceTestFile(sourcePath);

    RawWorkspace::SourceRecord source;
    source.absolutePath = sourcePath;
    source.relativePath = "Lab Sequence.DNG";
    source.relativePathKey = "Lab Sequence.DNG";
    source.fileName = "Lab Sequence.DNG";
    source.stem = "Lab Sequence";
    source.fingerprint = "raw-lab-sequence-fingerprint";
    source.fileSizeBytes = static_cast<std::uintmax_t>(
        std::filesystem::file_size(sourcePath));
    source.modifiedTimeTicks = 8675309;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe(sourcePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes =
        static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.whiteBalance.mode = RawRecipe::WhiteBalanceMode::CustomMultipliers;
    recipe.whiteBalance.hasMultipliers = true;
    recipe.whiteBalance.multipliers = { 1.91f, 1.0f, 1.37f };
    recipe.technical.applyBaselineExposure = false;
    recipe.technical.mosaicDenoise.enabled = true;
    recipe.technical.mosaicDenoise.lumaStrength = 0.41f;
    recipe.rgbDenoise.enabled = true;
    recipe.rgbDenoise.colorNoise = 0.52f;
    recipe.rgbDenoise.chromaMap.baseMultiplier = 0.52f;
    recipe.cropRotation.cropEnabled = true;
    recipe.cropRotation.cropX = 0.11f;
    recipe.cropRotation.cropY = 0.07f;
    recipe.cropRotation.cropWidth = 0.78f;
    recipe.cropRotation.cropHeight = 0.84f;
    recipe.cropRotation.rotationDegrees = 270;
    recipe.localRange.regionMaskEnabled = true;
    recipe.localRange.regionMaskMode = "radial-gradient";
    recipe.localRange.regionMaskCenterX = 0.34f;
    recipe.localRange.regionMaskCenterY = 0.62f;
    recipe.localRange.colorMaskEnabled = true;
    recipe.localRange.colorMaskTargetR = 0.18f;
    recipe.localRange.colorMaskTargetG = 0.63f;
    recipe.localRange.colorMaskTargetB = 0.29f;
    const nlohmann::json untouchedBaseline =
        RawLabUntouchedRecipeFields(recipe);

    StackBinaryFormat::ProjectDocument document;
    document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    document.metadata.projectName = source.stem;
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    document.thumbnailBytes = { 1, 2, 3, 4 };
    document.sourceImageBytes = { 5, 6, 7, 8 };
    EditorNodeGraph::Graph downstreamGraphModel;
    const nlohmann::json downstreamGraph =
        EditorNodeGraph::SerializeGraphPayload(
            nlohmann::json::array(), downstreamGraphModel);
    document.pipelineData = downstreamGraph;
    document.rawWorkspaceData = nlohmann::json::object();
    BindDocumentSourceAssetForInitialRawCommit(document);
    const std::filesystem::path projectPath = root / "Lab Sequence Project";

    for (int pass = 0; pass < 24; ++pass) {
        // Exposure -> Zones -> Curve -> View mirrors the four Lab surfaces.
        recipe.preToneExposureEv = -1.20f + 0.10f * static_cast<float>(pass);
        recipe.localRange.enabled = true;
        recipe.localRange.points = {
            { -8.0f, 0.04f * static_cast<float>(pass) },
            { 0.0f, -0.015f * static_cast<float>(pass) },
            { 6.0f, -0.02f * static_cast<float>(pass) }
        };
        RawRecipe::RawLocalRangeTargetZone zone;
        zone.id = "raw-lab-zone-1";
        zone.name = "Repeated target";
        zone.centerEv = -2.0f + 0.03f * static_cast<float>(pass);
        zone.deltaEv = 0.25f + 0.02f * static_cast<float>(pass);
        zone.seeds = { { 0.31f, 0.57f } };
        recipe.localRange.targetZones = { zone };

        recipe.finishTone.layerJson["points"] = nlohmann::json::array({
            { { "x", 0.0f }, { "y", 0.0f } },
            { { "x", 0.5f }, { "y", 0.42f + 0.01f * static_cast<float>(pass) } },
            { { "x", 1.0f }, { "y", 1.0f } }
        });
        recipe.finishTone.layerJson["preparedPoints"] =
            recipe.finishTone.layerJson["points"];
        recipe.viewTransform.layerJson["contrast"] =
            0.80f + 0.025f * static_cast<float>(pass);
        recipe.viewTransform.layerJson["saturation"] =
            1.15f - 0.01f * static_cast<float>(pass);

        Require(RawLabUntouchedRecipeFields(recipe) == untouchedBaseline,
            "RAW Lab tool changes must not mutate hidden recipe fields before save");
        Require(RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
                source,
                recipe,
                downstreamGraph,
                document),
            "RAW Lab tool sequence should update the shared project document");
        Require(!document.rawWorkspaceData.contains("futureRawWorkspaceKey"),
            "RAW Lab project updates should emit only current workspace metadata");
        Require(StackBinaryFormat::WriteProjectFile(projectPath, document),
            "RAW Lab tool sequence should save after every repeated switch cycle");

        StackBinaryFormat::ProjectDocument reloadedDocument;
        Require(StackBinaryFormat::ReadProjectFile(projectPath, reloadedDocument),
            "RAW Lab tool sequence should reload after every repeated switch cycle");
        const nlohmann::json& reloadedRecipeFileSize =
            reloadedDocument.rawWorkspaceData["rawRecipe"]["sourceRef"]["fileSizeBytes"];
        Require(reloadedRecipeFileSize.is_number_unsigned() &&
                reloadedRecipeFileSize.get<std::uint64_t>() ==
                    static_cast<std::uint64_t>(source.fileSizeBytes),
            "project binary JSON should preserve unsigned RAW source identity values without changing their type");
        RawWorkspace::ProjectInfo projectInfo;
        RawRecipe::RawDevelopmentRecipe reloadedRecipe;
        Require(RawWorkspace::ReadProjectInfoFromDocument(
                reloadedDocument,
                projectInfo,
                &reloadedRecipe),
            "RAW Lab tool sequence should recover its recipe from the saved project");
        const nlohmann::json reloadedUntouched =
            RawLabUntouchedRecipeFields(reloadedRecipe);
        if (reloadedUntouched != untouchedBaseline) {
            std::cerr << "RAW Lab untouched baseline: "
                      << untouchedBaseline.dump() << "\n";
            std::cerr << "RAW Lab untouched reloaded: "
                      << reloadedUntouched.dump() << "\n";
        }
        Require(reloadedUntouched == untouchedBaseline,
            "RAW Lab save/reload must preserve current technical, WB, denoise, crop, and output fields");
        Require(std::abs(reloadedRecipe.preToneExposureEv - recipe.preToneExposureEv) < 0.001f &&
                reloadedRecipe.localRange.targetZones.size() == 1 &&
                std::abs(reloadedRecipe.localRange.targetZones[0].deltaEv - zone.deltaEv) < 0.001f &&
                std::abs(reloadedRecipe.finishTone.layerJson["points"][1].value(
                    "y", 0.0f) - (0.42f + 0.01f * static_cast<float>(pass))) < 0.001f &&
                std::abs(reloadedRecipe.viewTransform.layerJson.value(
                    "contrast", 0.0f) - (0.80f + 0.025f * static_cast<float>(pass))) < 0.001f,
            "RAW Lab visible Exposure, Zones, Curve, and View edits should survive save/reload together");
        Require(!reloadedDocument.rawWorkspaceData.contains("futureRawWorkspaceKey"),
            "RAW Lab repeated saves must not retain unknown project-level RAW metadata");

        recipe = std::move(reloadedRecipe);
        document = std::move(reloadedDocument);
    }

    const std::filesystem::path replacementTemporaryPath =
        projectPath / "project.stack.tmp";
    Require(!std::filesystem::exists(replacementTemporaryPath),
        "repeated atomic project replacement should not leave its same-directory temporary file behind");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

Stack::RawRecipe::RawDevelopmentRecipe BuildRawWorkspaceReloadTestRecipe(
    const Stack::RawWorkspace::SourceRecord& source) {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes = static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    return recipe;
}

const Stack::RawWorkspace::SourceRecord& FindReloadTestSource(
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources,
    const std::string& relativePathKey) {
    const auto it = std::find_if(
        sources.begin(),
        sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == relativePathKey;
        });
    Require(it != sources.end(), "RAW reload ownership test source should exist");
    return *it;
}

struct ManagedRawReloadTestGraph {
    EditorNodeGraph::Graph graph;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::ManagedRawSection section;
    int rawDecodeNodeId = 0;
    int toneCurveNodeId = 0;
};

ManagedRawReloadTestGraph BuildManagedRawReloadTestGraph(
    const Stack::RawWorkspace::SourceRecord& source,
    const char* sectionSuffix) {
    using namespace EditorNodeGraph;

    ManagedRawReloadTestGraph result;
    result.recipe = BuildRawWorkspaceReloadTestRecipe(source);

    RawSourcePayload sourcePayload;
    sourcePayload.label = source.fileName;
    sourcePayload.sourcePath = source.absolutePath.string();
    sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
    const int rawSourceId = NodeId(result.graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));

    RawDecodePayload decodePayload;
    decodePayload.settings.exposureStops = 0.75f;
    decodePayload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::Manual;
    decodePayload.settings.manualWhiteBalance = { 1.75f, 1.0f, 1.25f };
    decodePayload.settings.rotationDegrees = 180;
    result.rawDecodeNodeId = NodeId(result.graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
    result.toneCurveNodeId = NodeId(result.graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(result.graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(result.graph.AddOutputNode({ 1040.0f, 0.0f }, true));

    Require(result.graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, result.rawDecodeNodeId, kRawInputSocketId),
        "RAW reload ownership managed source should connect");
    Require(result.graph.TryConnectSockets(result.rawDecodeNodeId, kImageOutputSocketId, result.toneCurveNodeId, kImageInputSocketId),
        "RAW reload ownership managed decode should connect");
    Require(result.graph.TryConnectSockets(result.toneCurveNodeId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "RAW reload ownership managed tone should connect");
    Require(result.graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "RAW reload ownership managed view transform should connect");

    result.section = Stack::RawWorkspace::BuildManagedRawSection(
        std::string("managed-raw:reload-") + sectionSuffix,
        source.relativePathKey,
        source.relativePathKey,
        source.fingerprint,
        -1,
        rawSourceId,
        result.rawDecodeNodeId,
        result.toneCurveNodeId,
        viewTransformId);
    return result;
}

void WriteRawWorkspaceReloadTestProject(
    const Stack::RawWorkspace::ManagedLayout& layout,
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& graphPayload,
    Stack::RawWorkspace::RawProjectMode mode,
    const nlohmann::json& managedRawSection,
    const nlohmann::json& customRawSection,
    const std::string& readOnlyReason = {}) {
    StackBinaryFormat::ProjectDocument document;
    document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    document.metadata.projectName = source.stem.empty() ? source.fileName : source.stem;
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    document.thumbnailBytes = { 1, 2, 3 };
    document.sourceImageBytes = { 4, 5, 6, 7 };
    document.pipelineData = graphPayload;
    Require(Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
            source,
            recipe,
            graphPayload,
            document,
            mode),
        "RAW reload ownership test should apply RAW Workspace metadata");
    document.rawWorkspaceData["managedRawSection"] = managedRawSection;
    document.rawWorkspaceData["customRawSection"] = customRawSection;
    document.rawWorkspaceData["readOnlyReason"] =
        readOnlyReason.empty() ? nlohmann::json() : nlohmann::json(readOnlyReason);

    const std::filesystem::path projectPath =
        layout.projectsDirectory / Stack::RawWorkspace::BuildProjectRelativePathForSource(source);
    std::error_code ec;
    std::filesystem::create_directories(projectPath.parent_path(), ec);
    Require(!ec, "RAW reload ownership test should create project parent directory");
    BindDocumentSourceAssetForInitialRawCommit(document);
    Require(StackBinaryFormat::WriteProjectFile(projectPath, document),
        "RAW reload ownership test should write project file");
    AttachManagedRawAssetToTestProject(projectPath, source);
}

void TestRawWorkspaceProjectReloadPreservesOwnershipModes() {
    namespace RawWorkspace = Stack::RawWorkspace;
    namespace RawRecipe = Stack::RawRecipe;
    using namespace EditorNodeGraph;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_project_reload_modes_test");
    WriteRawWorkspaceTestFile(root / "recipe_image.DNG");
    WriteRawWorkspaceTestFile(root / "managed_image.DNG");
    WriteRawWorkspaceTestFile(root / "custom_image.DNG");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(root, RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW reload ownership scan should succeed");
    const std::filesystem::path projectRoot =
        MakeTempDirectory("stack_raw_project_reload_modes_store_test");
    scan.layout.projectsDirectory = projectRoot;
    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);

    const RawWorkspace::SourceRecord& recipeSource =
        FindReloadTestSource(scan.sources, "recipe_image.DNG");
    const RawWorkspace::SourceRecord& managedSource =
        FindReloadTestSource(scan.sources, "managed_image.DNG");
    const RawWorkspace::SourceRecord& customSource =
        FindReloadTestSource(scan.sources, "custom_image.DNG");

    RawRecipe::RawDevelopmentRecipe recipeBackedRecipe =
        BuildRawWorkspaceReloadTestRecipe(recipeSource);
    recipeBackedRecipe.preToneExposureEv = 0.35f;
    Graph recipeGraph;
    RawDevelopmentPayload recipePayload;
    recipePayload.recipe = recipeBackedRecipe;
    recipePayload.projectStatus = "Edited";
    recipePayload.edited = true;
    const int rawDevelopmentId = NodeId(recipeGraph.AddRawDevelopmentNode(recipePayload, { 0.0f, 0.0f }));
    const int recipeOutputId = NodeId(recipeGraph.AddOutputNode({ 260.0f, 0.0f }, true));
    Require(recipeGraph.TryConnectSockets(rawDevelopmentId, kImageOutputSocketId, recipeOutputId, kImageInputSocketId),
        "RAW reload ownership recipe-backed graph should connect");
    const nlohmann::json recipeGraphPayload =
        SerializeGraphPayload(nlohmann::json::array(), recipeGraph);
    WriteRawWorkspaceReloadTestProject(
        scan.layout,
        recipeSource,
        recipeBackedRecipe,
        recipeGraphPayload,
        RawWorkspace::RawProjectMode::UnifiedLayers,
        nullptr,
        nullptr);

    ManagedRawReloadTestGraph managed = BuildManagedRawReloadTestGraph(managedSource, "managed");
    const nlohmann::json managedGraphPayload =
        SerializeGraphPayload(nlohmann::json::array(), managed.graph);
    WriteRawWorkspaceReloadTestProject(
        scan.layout,
        managedSource,
        managed.recipe,
        managedGraphPayload,
        RawWorkspace::RawProjectMode::ManagedDecomposed,
        RawWorkspace::SerializeManagedRawSection(managed.section),
        nullptr);

    ManagedRawReloadTestGraph custom = BuildManagedRawReloadTestGraph(customSource, "custom");
    custom.graph.RemoveLink(custom.rawDecodeNodeId, kImageOutputSocketId, custom.toneCurveNodeId, kImageInputSocketId);
    const int mixId = NodeId(custom.graph.AddMixNode({ 390.0f, 90.0f }));
    Require(custom.graph.TryConnectSockets(custom.rawDecodeNodeId, kImageOutputSocketId, mixId, kMixInputASocketId),
        "RAW reload ownership custom graph should connect custom node after decode");
    Require(custom.graph.TryConnectSockets(mixId, kImageOutputSocketId, custom.toneCurveNodeId, kImageInputSocketId),
        "RAW reload ownership custom graph should reconnect to managed tone node");
    const nlohmann::json customRawSection = {
        { "schema", "stack.rawWorkspace.customRawSection" },
        { "schemaVersion", 1 },
        { "modeState", "custom-graph" },
        { "previousManagedSectionId", custom.section.sectionId },
        { "reason", RawWorkspace::kCustomGraphReadOnlyReason }
    };
    const nlohmann::json customGraphPayload =
        SerializeGraphPayload(nlohmann::json::array(), custom.graph);
    WriteRawWorkspaceReloadTestProject(
        scan.layout,
        customSource,
        custom.recipe,
        customGraphPayload,
        RawWorkspace::RawProjectMode::CustomGraph,
        RawWorkspace::SerializeManagedRawSection(custom.section),
        customRawSection,
        RawWorkspace::kCustomGraphReadOnlyReason);

    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    Require(FindReloadTestSource(scan.sources, "recipe_image.DNG").project.mode ==
            RawWorkspace::RawProjectMode::UnifiedLayers,
        "RAW project discovery should preserve recipe-backed ownership mode");
    Require(FindReloadTestSource(scan.sources, "managed_image.DNG").project.mode ==
            RawWorkspace::RawProjectMode::ManagedDecomposed,
        "RAW project discovery should preserve managed-decomposed ownership mode");
    Require(FindReloadTestSource(scan.sources, "custom_image.DNG").project.mode ==
            RawWorkspace::RawProjectMode::CustomGraph,
        "RAW project discovery should preserve custom graph ownership mode");

    const std::filesystem::path recipeProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(recipeSource);
    StackBinaryFormat::ProjectDocument loadedRecipeDocument;
    Require(StackBinaryFormat::ReadProjectFile(recipeProjectPath, loadedRecipeDocument),
        "RAW reload ownership recipe-backed project should reload from disk");
    RawWorkspace::ProjectInfo loadedRecipeInfo;
    RawRecipe::RawDevelopmentRecipe loadedRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedRecipeDocument, loadedRecipeInfo, &loadedRecipe),
        "RAW reload ownership recipe-backed metadata should parse");
    Require(loadedRecipeInfo.mode == RawWorkspace::RawProjectMode::UnifiedLayers,
        "RAW reload ownership recipe-backed mode should survive file round-trip");
    Require(loadedRecipeDocument.rawWorkspaceData["managedRawSection"].is_null() &&
            loadedRecipeDocument.rawWorkspaceData["customRawSection"].is_null(),
        "RAW reload ownership recipe-backed project should not grow managed/custom payloads");
    Require(std::abs(loadedRecipe.preToneExposureEv - 0.35f) < 0.001f,
        "RAW reload ownership recipe-backed edits should survive file round-trip");

    const std::filesystem::path managedProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(managedSource);
    StackBinaryFormat::ProjectDocument loadedManagedDocument;
    Require(StackBinaryFormat::ReadProjectFile(managedProjectPath, loadedManagedDocument),
        "RAW reload ownership managed project should reload from disk");
    RawWorkspace::ProjectInfo loadedManagedInfo;
    RawRecipe::RawDevelopmentRecipe loadedManagedRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedManagedDocument, loadedManagedInfo, &loadedManagedRecipe),
        "RAW reload ownership managed metadata should parse");
    Require(loadedManagedInfo.mode == RawWorkspace::RawProjectMode::ManagedDecomposed,
        "RAW reload ownership managed mode should survive file round-trip");
    const RawWorkspace::ManagedRawSection loadedManagedSection =
        RawWorkspace::DeserializeManagedRawSection(
            loadedManagedDocument.rawWorkspaceData.value("managedRawSection", nlohmann::json::object()));
    Graph loadedManagedGraph;
    DeserializeGraphPayload(loadedManagedDocument.pipelineData, loadedManagedGraph, 2, {}, 0, 0, 0);
    const RawWorkspace::ManagedRawValidationResult managedValidation =
        RawWorkspace::ValidateManagedRawSection(loadedManagedGraph, loadedManagedSection, loadedManagedRecipe);
    if (!managedValidation.valid) {
        std::cerr << "Managed reload validation failed: " << managedValidation.message << "\n";
        std::cerr << "Managed section ids: source=" << loadedManagedSection.rawSourceNodeId
                  << " decode=" << loadedManagedSection.rawDecodeNodeId
                  << " tone=" << loadedManagedSection.toneCurveNodeId
                  << " view=" << loadedManagedSection.viewTransformNodeId << "\n";
        std::cerr << "Reloaded graph node ids:";
        for (const EditorNodeGraph::Node& node : loadedManagedGraph.GetNodes()) {
            std::cerr << " " << node.id;
        }
        std::cerr << "\n";
    }
    Require(managedValidation.valid,
        "RAW reload ownership managed graph should still validate after file round-trip");
    Require(std::abs(managedValidation.recipe.preToneExposureEv - 0.75f) < 0.001f,
        "RAW reload ownership managed decode exposure should still sync after reload");
    Require(managedValidation.recipe.whiteBalance.mode == RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            managedValidation.recipe.whiteBalance.hasMultipliers,
        "RAW reload ownership managed white balance should still sync after reload");
    Require(managedValidation.recipe.cropRotation.rotationDegrees == 180,
        "RAW reload ownership managed rotation should still sync after reload");

    const std::filesystem::path customProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(customSource);
    StackBinaryFormat::ProjectDocument loadedCustomDocument;
    Require(StackBinaryFormat::ReadProjectFile(customProjectPath, loadedCustomDocument),
        "RAW reload ownership custom project should reload from disk");
    RawWorkspace::ProjectInfo loadedCustomInfo;
    RawRecipe::RawDevelopmentRecipe loadedCustomRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedCustomDocument, loadedCustomInfo, &loadedCustomRecipe),
        "RAW reload ownership custom metadata should parse");
    Require(loadedCustomInfo.mode == RawWorkspace::RawProjectMode::CustomGraph,
        "RAW reload ownership custom graph mode should survive file round-trip");
    Require(loadedCustomInfo.readOnlyReason.find("read-only") != std::string::npos,
        "RAW reload ownership custom graph read-only reason should survive file round-trip");
    Require(loadedCustomDocument.rawWorkspaceData["customRawSection"].value("modeState", std::string()) == "custom-graph",
        "RAW reload ownership custom section payload should survive file round-trip");
    const RawWorkspace::ManagedRawSection loadedPreviousManagedSection =
        RawWorkspace::DeserializeManagedRawSection(
            loadedCustomDocument.rawWorkspaceData.value("managedRawSection", nlohmann::json::object()));
    Graph loadedCustomGraph;
    DeserializeGraphPayload(loadedCustomDocument.pipelineData, loadedCustomGraph, 2, {}, 0, 0, 0);
    Require(!RawWorkspace::ValidateManagedRawSection(
                loadedCustomGraph,
                loadedPreviousManagedSection,
                loadedCustomRecipe).valid,
        "RAW reload ownership custom graph should remain outside managed editability after reload");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ec.clear();
    std::filesystem::remove_all(projectRoot, ec);
}

void TestScalarThroughDataMathToPreviewAndScalarTargets() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskId = NodeId(graph.AddCustomMaskNode({}, { 0.0f, 0.0f }));
    const int clampId = NodeId(graph.AddDataMathNode(DataMathMode::Clamp, { 220.0f, 0.0f }));
    const int addId = NodeId(graph.AddDataMathNode(DataMathMode::Add, { 440.0f, 0.0f }));
    const int averageId = NodeId(graph.AddDataMathNode(DataMathMode::Average, { 660.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 880.0f, 0.0f }));
    const int layerId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 880.0f, 140.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 880.0f, 280.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 880.0f, 420.0f }, true));

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, clampId, kMixInputASocketId),
        "custom mask should feed Data Math clamp input A");
    Require(graph.IsScalarSocketStream(clampId, kImageOutputSocketId),
        "Data Math with only scalar inputs should output a scalar stream");
    Require(graph.TryConnectSockets(clampId, kImageOutputSocketId, previewId, kPreviewInputSocketId),
        "scalar Data Math output should connect to preview");

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, addId, kMixInputASocketId),
        "custom mask should feed Data Math add input A");
    Require(graph.TryConnectSockets(clampId, kImageOutputSocketId, addId, kMixInputBSocketId),
        "scalar Data Math output should feed Data Math input B");
    Require(graph.IsScalarSocketStream(addId, kImageOutputSocketId),
        "Data Math with two scalar inputs should preserve scalar output classification");
    Require(graph.TryConnectSockets(addId, kImageOutputSocketId, layerId, kMaskInputSocketId),
        "scalar Data Math output should connect to a layer mask");

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, averageId, kMixInputASocketId),
        "custom mask should feed Data Math average input A");
    Require(graph.TryConnectSockets(addId, kImageOutputSocketId, averageId, kMixInputBSocketId),
        "scalar Data Math add output should feed Data Math average input B");
    Require(graph.TryConnectSockets(averageId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar Data Math output should connect to mix factor");
    Require(!graph.CanConnectSockets(
            averageId,
            kImageOutputSocketId,
            outputId,
            kImageInputSocketId),
        "generic scalar Data Math output should not masquerade as a Channel at Output");
}

void TestImageThroughDataMathToOutput() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int clampId = NodeId(graph.AddDataMathNode(DataMathMode::Clamp, { 220.0f, 0.0f }));
    const int averageId = NodeId(graph.AddDataMathNode(DataMathMode::ImageAverage, { 440.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 660.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, clampId, kMixInputASocketId),
        "full image should feed Data Math clamp input A");
    Require(!graph.IsScalarSocketStream(clampId, kImageOutputSocketId),
        "Data Math with a full image input should output an image stream");
    Require(graph.TryConnectSockets(clampId, kImageOutputSocketId, averageId, kMixInputASocketId),
        "full image Data Math output should feed another Data Math image input");
    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, averageId, kMixInputBSocketId),
        "full image should feed Data Math input B");
    Require(graph.TryConnectSockets(averageId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "image Data Math output should connect to output image input");
}

void TestFrequencyNodeShellSocketsAndConnections() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int splitId = NodeId(graph.AddChannelSplitNode({ 180.0f, 0.0f }));
    const int filterId = NodeId(graph.AddFrequencyFilterNode(
        FrequencyFilterMode::LowPass, { 380.0f, 0.0f }));
    const int responseId = NodeId(graph.AddFrequencyResponseNode({ 380.0f, 220.0f }));
    const int fftId = NodeId(graph.AddFrequencyFftNode({ 600.0f, 0.0f }));
    const int applyId = NodeId(graph.AddApplyFrequencyResponseNode({ 820.0f, 0.0f }));
    const int combineId = NodeId(graph.AddCombineSpectraNode({ 1040.0f, 0.0f }));
    const int separateId = NodeId(graph.AddSpectrumSeparateNode({ 1260.0f, 0.0f }));
    const int recombineId = NodeId(graph.AddSpectrumRecombineNode({ 1480.0f, 0.0f }));
    const int viewId = NodeId(graph.AddSpectrumViewNode({ 1480.0f, 220.0f }));
    const int ifftId = NodeId(graph.AddFrequencyIfftNode({ 1700.0f, 0.0f }));
    const int analyzerId = NodeId(graph.AddSpectrumAnalyzerNode(
        SpectrumAnalyzerMode::RadialEnergy, { 1260.0f, 420.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1920.0f, 0.0f }, true));

    const Node* fftNode = graph.FindNode(fftId);
    Require(fftNode && fftNode->kind == NodeKind::FrequencyFft &&
            fftNode->title == "Fourier Transform",
        "Fourier Transform should use the approachable revised title");
    const Node* ifftNode = graph.FindNode(ifftId);
    Require(ifftNode && ifftNode->kind == NodeKind::FrequencyIfft &&
            ifftNode->title == "Inverse Fourier Transform",
        "Inverse Fourier Transform should use the revised title");

    SocketDefinition channelInput;
    SocketDefinition responseInput;
    SocketDefinition channelOutput;
    SocketDefinition spectrumOutput;
    SocketDefinition responseOutput;
    SocketDefinition magnitudeOutput;
    SocketDefinition phaseOutput;
    SocketDefinition radialPowerOutput;
    SocketDefinition bandPowerOutput;
    Require(graph.FindSocket(filterId, kChannelInputSocketId, &channelInput) &&
            channelInput.type == SocketType::Channel &&
            graph.FindSocket(filterId, kFrequencyResponseInputSocketId, &responseInput) &&
            responseInput.type == SocketType::FrequencyResponse &&
            graph.FindSocket(filterId, kChannelOutputSocketId, &channelOutput) &&
            channelOutput.type == SocketType::Channel,
        "Frequency Filter should expose Channel, optional Response, and Channel sockets");
    Require(graph.FindSocket(fftId, kChannelInputSocketId, &channelInput) &&
            channelInput.type == SocketType::Channel &&
            graph.FindSocket(fftId, kSpectrumOutputSocketId, &spectrumOutput) &&
            spectrumOutput.type == SocketType::Spectrum,
        "Fourier Transform should expose exact Channel-to-Spectrum sockets");
    Require(graph.FindSocket(
                responseId, kFrequencyResponseOutputSocketId, &responseOutput) &&
            responseOutput.type == SocketType::FrequencyResponse,
        "Frequency Response should expose a distinct Response socket");
    Require(graph.FindSocket(
                separateId, kSpectrumMagnitudeOutputSocketId, &magnitudeOutput) &&
            magnitudeOutput.type == SocketType::SpectrumMagnitude &&
            graph.FindSocket(
                separateId, kSpectrumPhaseOutputSocketId, &phaseOutput) &&
            phaseOutput.type == SocketType::SpectrumPhase,
        "Separate Spectrum should expose raw Magnitude and Phase types");
    Require(graph.FindSocket(
                analyzerId, kRadialPowerOutputSocketId, &radialPowerOutput) &&
            radialPowerOutput.type == SocketType::Analysis &&
            graph.FindSocket(
                analyzerId, kBandPowerOutputSocketId, &bandPowerOutput) &&
            bandPowerOutput.type == SocketType::Scalar,
        "Spectrum Analyzer should expose Data plus independent scalar measurements");

    std::string error;
    Require(!graph.TryConnectSockets(
            imageId, kImageOutputSocketId,
            fftId, kChannelInputSocketId, &error) &&
            error.find("Channel") != std::string::npos,
        "ordinary Image output must be rejected by an exact Channel input");
    Require(graph.TryConnectSockets(
            imageId, kImageOutputSocketId,
            splitId, kImageInputSocketId),
        "image should connect to Channel Split");
    Require(graph.TryConnectSockets(
            splitId, "r", filterId, kChannelInputSocketId),
        "Channel should connect to the approachable Frequency Filter");
    Require(graph.TryConnectSockets(
            responseId, kFrequencyResponseOutputSocketId,
            filterId, kFrequencyResponseInputSocketId),
        "Response should connect only to the Frequency Filter Response input");
    Require(graph.TryConnectSockets(
            filterId, kChannelOutputSocketId,
            fftId, kChannelInputSocketId),
        "filtered Channel should connect to Fourier Transform");
    Require(graph.TryConnectSockets(
            fftId, kSpectrumOutputSocketId,
            applyId, kSpectrumInputSocketId),
        "Spectrum should connect to Apply Frequency Response");
    Require(graph.TryConnectSockets(
            responseId, kFrequencyResponseOutputSocketId,
            applyId, kFrequencyResponseInputSocketId),
        "Response should connect to Apply Frequency Response");
    Require(graph.TryConnectSockets(
            fftId, kSpectrumOutputSocketId,
            combineId, kSpectrumInputASocketId) &&
            graph.TryConnectSockets(
                applyId, kSpectrumOutputSocketId,
                combineId, kSpectrumInputBSocketId),
        "Combine Spectra should accept two exact Spectrum inputs");
    Require(graph.TryConnectSockets(
            combineId, kSpectrumOutputSocketId,
            separateId, kSpectrumInputSocketId),
        "combined Spectrum should connect to Separate Spectrum");
    Require(graph.TryConnectSockets(
            separateId, kSpectrumMagnitudeOutputSocketId,
            recombineId, kSpectrumMagnitudeInputSocketId) &&
            graph.TryConnectSockets(
                separateId, kSpectrumPhaseOutputSocketId,
                recombineId, kSpectrumPhaseInputSocketId),
        "raw Magnitude and Phase should reconnect only to matching typed inputs");
    Require(!graph.TryConnectSockets(
            separateId, kSpectrumMagnitudeOutputSocketId,
            recombineId, kSpectrumPhaseInputSocketId),
        "Magnitude must not accidentally connect to Phase");
    Require(graph.TryConnectSockets(
            recombineId, kSpectrumOutputSocketId,
            viewId, kSpectrumInputSocketId),
        "recombined Spectrum should connect to Spectrum View");
    Require(graph.TryConnectSockets(viewId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "Spectrum View output should connect to graph output");
    Require(
        graph.IsOutputConnected(),
        "the advanced typed frequency chain should qualify as a completed "
        "viewport output chain");
    Require(graph.TryConnectSockets(
            recombineId, kSpectrumOutputSocketId,
            ifftId, kSpectrumInputSocketId),
        "Spectrum should connect to Inverse Fourier Transform");
    Require(graph.TryConnectSockets(
            recombineId, kSpectrumOutputSocketId,
            analyzerId, kSpectrumInputSocketId),
        "Spectrum Analyzer should accept exact Spectrum only");
    const int analyzedExposureId = NodeId(
        graph.AddTechnicalImageNode(
            Stack::NodeMath::TechnicalImageOperation::Exposure,
            { 1700.0f, 420.0f }));
    Require(
        !graph.TryConnectSockets(
            analyzerId,
            kRadialPowerOutputSocketId,
            analyzedExposureId,
            kExposureValueInputSocketId),
        "Spectrum Analyzer radial Data must not masquerade as a scalar Value");
    Require(
        graph.TryConnectSockets(
            viewId,
            kImageOutputSocketId,
            analyzedExposureId,
            kImageInputSocketId) &&
        graph.TryConnectSockets(
            analyzerId,
            kBandPowerOutputSocketId,
            analyzedExposureId,
            kExposureValueInputSocketId) &&
        graph.TryConnectSockets(
            analyzedExposureId,
            kImageOutputSocketId,
            outputId,
            kImageInputSocketId),
        "Spectrum Analyzer scalar measurements should drive implemented "
        "runtime Value inputs");
    const std::vector<CompletedChainInfo> analyzedChains =
        graph.GetCompletedChains();
    Require(
        analyzedChains.size() == 1 &&
        std::find(
            analyzedChains.front().nodeIds.begin(),
            analyzedChains.front().nodeIds.end(),
            analyzerId) != analyzedChains.front().nodeIds.end() &&
        std::find(
            analyzedChains.front().nodeIds.begin(),
            analyzedChains.front().nodeIds.end(),
            analyzedExposureId) != analyzedChains.front().nodeIds.end(),
        "completed-chain lowering should retain the analyzer reduction branch "
        "that controls Exposure");

    Graph basicGraph;
    const int basicImageId = NodeId(
        basicGraph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int basicSplitId = NodeId(
        basicGraph.AddChannelSplitNode({ 180.0f, 0.0f }));
    const int basicFilterId = NodeId(
        basicGraph.AddFrequencyFilterNode(
            FrequencyFilterMode::LowPass,
            { 380.0f, 0.0f }));
    const int basicCombineId = NodeId(
        basicGraph.AddChannelCombineNode({ 580.0f, 0.0f }));
    const int basicOutputId = NodeId(
        basicGraph.AddOutputNode({ 780.0f, 0.0f }, true));
    Require(
        basicGraph.TryConnectSockets(
            basicImageId,
            kImageOutputSocketId,
            basicSplitId,
            kImageInputSocketId) &&
        basicGraph.TryConnectSockets(
            basicSplitId,
            "r",
            basicFilterId,
            kChannelInputSocketId) &&
        basicGraph.TryConnectSockets(
            basicFilterId,
            kChannelOutputSocketId,
            basicCombineId,
            "r") &&
        basicGraph.TryConnectSockets(
            basicSplitId,
            "g",
            basicCombineId,
            "g") &&
        basicGraph.TryConnectSockets(
            basicSplitId,
            "b",
            basicCombineId,
            "b") &&
        basicGraph.TryConnectSockets(
            basicSplitId,
            "a",
            basicCombineId,
            "a") &&
        basicGraph.TryConnectSockets(
            basicCombineId,
            kImageOutputSocketId,
            basicOutputId,
            kImageInputSocketId),
        "the basic low-pass regression graph should connect exactly");
    const std::vector<CompletedChainInfo> basicChains =
        basicGraph.GetCompletedChains();
    Require(
        basicGraph.IsOutputConnected() &&
        basicChains.size() == 1 &&
        basicChains.front().outputNodeId == basicOutputId &&
        std::find(
            basicChains.front().nodeIds.begin(),
            basicChains.front().nodeIds.end(),
            basicFilterId) != basicChains.front().nodeIds.end(),
        "Image -> Split -> Frequency Filter -> Combine -> Output should "
        "remain a completed viewport chain");

    Require(graph.SetParameterExposed(
            filterId, kStrengthParameterId, true) &&
            graph.FindSocket(
                filterId,
                ParameterInputSocketId(kStrengthParameterId),
                &bandPowerOutput) &&
            bandPowerOutput.type == SocketType::Scalar,
        "exposed Strength should gain a stable parameter-derived Value socket");

    Node* responseNode = graph.FindNode(responseId);
    responseNode->frequencyResponseSettings.mode =
        FrequencyFilterMode::NotchReject;
    responseNode->frequencyResponseSettings.profile =
        FrequencyTransitionProfile::Butterworth;
    responseNode->frequencyResponseSettings.notches = {
        { "notch-a", 0.1875f, 31.0f, 0.018f },
        { "notch-b", 0.3125f, -67.0f, 0.027f }
    };
    Require(graph.SetParameterExposed(
            responseId,
            FrequencyNotchParameterId("notch-b", "direction"),
            true) &&
            graph.FindSocket(
                responseId,
                ParameterInputSocketId(
                    FrequencyNotchParameterId(
                        "notch-b", "direction")),
                &bandPowerOutput) &&
            bandPowerOutput.type == SocketType::Scalar,
        "notch parameters should support stable graph exposure");
    const nlohmann::json serialized =
        SerializeGraphPayload(nlohmann::json::array(), graph);
    Require(serialized["nodeGraph"].value("version", 0) == 9,
        "Output v2 uses the current channel-first graph schema");
    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);
    const Node* loadedResponse = loaded.FindNode(responseId);
    Require(loadedResponse != nullptr &&
            loadedResponse->frequencyResponseSettings.notches.size() == 2 &&
            loadedResponse->frequencyResponseSettings.notches[1].id == "notch-b" &&
            std::find(
                loadedResponse->exposedParameterIds.begin(),
                loadedResponse->exposedParameterIds.end(),
                FrequencyNotchParameterId(
                    "notch-b", "direction")) !=
                loadedResponse->exposedParameterIds.end(),
        "response notches and exposed parameter IDs should persist exactly");

    nlohmann::json outOfDomainDocument = serialized;
    for (nlohmann::json& item :
         outOfDomainDocument["nodeGraph"]["nodes"]) {
        if (item.value("id", -1) == responseId) {
            item["frequencyResponseSettings"]["butterworthOrder"] = 99.0f;
            item["frequencyResponseSettings"]["notches"][0]["width"] = 8.0f;
        } else if (item.value("id", -1) == analyzerId) {
            item["spectrumAnalyzerSettings"]["innerRadius"] = 9.0f;
            item["spectrumAnalyzerSettings"]["outerRadius"] = 10.0f;
        }
    }
    Graph bounded;
    DeserializeGraphPayload(
        outOfDomainDocument, bounded, 0, {}, 0, 0, 0);
    const Node* boundedResponse = bounded.FindNode(responseId);
    const Node* boundedAnalyzer = bounded.FindNode(analyzerId);
    Require(
        boundedResponse != nullptr &&
        boundedResponse->frequencyResponseSettings.butterworthOrder == 12.0f &&
        boundedResponse->frequencyResponseSettings.notches.size() == 2 &&
        boundedResponse->frequencyResponseSettings.notches[0].width == 0.25f &&
        boundedAnalyzer != nullptr &&
        std::abs(
            boundedAnalyzer->spectrumAnalyzerSettings.innerRadius -
            0.70710678f) < 1.0e-7f &&
        std::abs(
            boundedAnalyzer->spectrumAnalyzerSettings.outerRadius -
            0.70710678f) < 1.0e-7f,
        "frequency response and analyzer persistence should canonicalize "
        "out-of-domain values to the same limits as live execution");

    const std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry> catalog =
        EditorNodeGraphDefinitions::BuildNodeCatalogEntries();
    const int mainFrequencyCount = static_cast<int>(std::count_if(
        catalog.begin(), catalog.end(), [](const auto& entry) {
            return entry.category == "Frequency";
        }));
    const int advancedFrequencyCount = static_cast<int>(std::count_if(
        catalog.begin(), catalog.end(), [](const auto& entry) {
            return entry.category == "Advanced Frequency";
        }));
    Require(mainFrequencyCount == 6 && advancedFrequencyCount == 9,
        "browser should expose six friendly presets and nine advanced frequency nodes");
    Require(std::none_of(
            catalog.begin(), catalog.end(), [](const auto& entry) {
                return entry.kind == NodeKind::FrequencyMask ||
                    entry.kind == NodeKind::SpectrumMath ||
                    entry.kind == NodeKind::MagnitudePhase;
            }),
        "legacy frequency shells should not remain in the node browser");

    Graph oldGraph;
    oldGraph.AddFrequencyFftNode({ 0.0f, 0.0f });
    nlohmann::json oldDocument =
        SerializeGraphPayload(nlohmann::json::array(), oldGraph);
    oldDocument["nodeGraph"]["version"] = 6;
    Graph rejectedGraph;
    DeserializeGraphPayload(oldDocument, rejectedGraph, 0, {}, 0, 0, 0);
    Require(rejectedGraph.GetNodes().empty(),
        "non-current graph schemas should be rejected without conversion");
}

void TestAverageNodeInputRules() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageAId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int imageBId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 160.0f }));
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 320.0f }));
    const int splitId = NodeId(graph.AddChannelSplitNode({ 220.0f, 0.0f }));
    const int scalarAverageId = NodeId(graph.AddDataMathNode(DataMathMode::Average, { 440.0f, 0.0f }));
    const int imageAverageId = NodeId(graph.AddDataMathNode(DataMathMode::ImageAverage, { 440.0f, 180.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 660.0f, 180.0f }, true));

    Require(!graph.FindSocket(scalarAverageId, kDataMathBaseInputSocketId),
        "scalar Average should not expose a masked Base input");
    Require(!graph.FindSocket(scalarAverageId, kMaskInputSocketId),
        "scalar Average should not expose a blend Mask input");
    Require(!graph.CanConnectSockets(imageAId, kImageOutputSocketId, scalarAverageId, kMixInputASocketId),
        "scalar Average should reject full image inputs");
    Require(graph.CanInsertImageToScalarExtractor(imageAId, kImageOutputSocketId, scalarAverageId, kMixInputASocketId),
        "full images should be convertible into scalar Average inputs through an extractor");
    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, splitId, kImageInputSocketId),
        "full image should feed channel split");
    Require(graph.TryConnectSockets(splitId, "r", scalarAverageId, kMixInputASocketId),
        "scalar Average should accept split channel inputs");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, scalarAverageId, kMixInputBSocketId),
        "scalar Average should accept mask inputs");
    Require(graph.IsScalarSocketStream(scalarAverageId, kImageOutputSocketId),
        "scalar Average should always output a scalar stream");

    Require(!graph.CanConnectSockets(maskId, kMaskOutputSocketId, imageAverageId, kMixInputASocketId),
        "Average Images should reject mask inputs");
    Require(!graph.CanConnectSockets(scalarAverageId, kImageOutputSocketId, imageAverageId, kMixInputASocketId),
        "Average Images should reject scalar math outputs");
    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, imageAverageId, kMixInputASocketId),
        "Average Images should accept full image input A");
    Require(graph.TryConnectSockets(imageBId, kImageOutputSocketId, imageAverageId, kMixInputBSocketId),
        "Average Images should accept full image input B");
    Require(!graph.IsScalarSocketStream(imageAverageId, kImageOutputSocketId),
        "Average Images should output a full image stream");
    Require(graph.TryConnectSockets(imageAverageId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "Average Images should feed image outputs");
}

void TestSemanticNodeMutationsInvalidateExecutionState() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageAId = NodeId(graph.AddImageGeneratorNode(
        ImageGeneratorKind::SolidColor, { 0.0f, 0.0f }));
    const int imageBId = NodeId(graph.AddImageGeneratorNode(
        ImageGeneratorKind::SolidColor, { 0.0f, 160.0f }));
    const int dataMathId = NodeId(graph.AddDataMathNode(
        DataMathMode::Clamp, { 260.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode(
        { 520.0f, 0.0f }, true));
    Require(
        graph.TryConnectSockets(
            imageAId,
            kImageOutputSocketId,
            dataMathId,
            DataMathInputSocketId(0)) &&
        graph.TryConnectSockets(
            dataMathId,
            kImageOutputSocketId,
            outputId,
            kImageInputSocketId),
        "semantic-mutation fixture should connect");

    const Node* dataMath = graph.FindNode(dataMathId);
    const std::string clampDefinitionId =
        dataMath ? dataMath->definitionId : std::string();
    Require(
        graph.GetCompletedChains().size() == 1,
        "Clamp with one image input should initially complete");

    const std::uint64_t clampRevision = graph.GetStructureRevision();
    Require(
        graph.SetDataMathMode(dataMathId, DataMathMode::ImageAverage) &&
        graph.GetStructureRevision() > clampRevision,
        "changing a Data Math mode should advance semantic topology");
    dataMath = graph.FindNode(dataMathId);
    Require(
        dataMath &&
        dataMath->definitionResolved &&
        dataMath->definitionId != clampDefinitionId,
        "changing a Data Math variant should pin its new exact definition");
    Require(
        graph.GetCompletedChains().empty(),
        "Average Images should invalidate the cached one-input completed chain");
    Require(
        graph.TryConnectSockets(
            imageBId,
            kImageOutputSocketId,
            dataMathId,
            DataMathInputSocketId(1)) &&
        graph.GetCompletedChains().size() == 1,
        "Average Images should complete again after receiving two images");

    Require(
        graph.SetDataMathMode(dataMathId, DataMathMode::Average),
        "switching from image Average to scalar Average should succeed");
    Require(
        graph.FindInputLink(dataMathId, DataMathInputSocketId(0)) == nullptr &&
        graph.FindInputLink(dataMathId, DataMathInputSocketId(1)) == nullptr,
        "a semantic mode change should remove links that no longer match its socket contract");
    Require(
        graph.GetCompletedChains().empty(),
        "scalar Average should not retain an image-mode completed chain");

    Graph maskGraph;
    const int combineId = NodeId(maskGraph.AddMaskCombineNode(
        MaskCombineMode::Add, { 0.0f, 0.0f }));
    const Node* combine = maskGraph.FindNode(combineId);
    const std::string addDefinitionId =
        combine ? combine->definitionId : std::string();
    Require(
        maskGraph.SetMaskCombineMode(
            combineId, MaskCombineMode::Exclude),
        "Mask Combine mode mutation should succeed");
    combine = maskGraph.FindNode(combineId);
    Require(
        combine &&
        combine->definitionResolved &&
        combine->definitionId != addDefinitionId,
        "Mask Combine mode mutation should update exact definition identity");

    Graph layerGraph;
    layerGraph.ResetFromLayers(1, true);
    const Node* layer = layerGraph.FindNodeByLayerIndex(0);
    Require(
        layer &&
        layer->definitionResolved &&
        !layer->instanceUuid.empty() &&
        layerGraph.IsOutputConnected(),
        "legacy layer-list graph construction should create executable, identified layer nodes");
    const int layerId = layer->id;
    const std::string brightnessDefinitionId = layer->definitionId;
    Require(
        layerGraph.SetLayerNodeType(layerId, LayerType::Contrast),
        "layer metadata synchronization should accept a concrete type");
    layer = layerGraph.FindNode(layerId);
    Require(
        layer &&
        layer->definitionResolved &&
        layer->typeId == "Contrast" &&
        layer->definitionId != brightnessDefinitionId &&
        layerGraph.IsOutputConnected(),
        "layer type changes should update identity without losing the completed chain");
    layerGraph.EditNodes();
    Node* unresolvedLayer = layerGraph.FindNode(layerId);
    unresolvedLayer->definitionResolved = false;
    unresolvedLayer->definitionResolutionError =
        "Injected saved layer definition mismatch.";
    const std::string unresolvedLayerDefinitionId =
        unresolvedLayer->definitionId;
    layerGraph.SetLayerNodeType(layerId, LayerType::Contrast);
    layer = layerGraph.FindNode(layerId);
    Require(
        layer &&
        !layer->definitionResolved &&
        layer->definitionId == unresolvedLayerDefinitionId,
        "layer metadata refresh must not silently replace an unresolved saved definition");

    Graph unresolvedGraph;
    const int unresolvedSourceId = NodeId(
        unresolvedGraph.AddImageGeneratorNode(
            ImageGeneratorKind::SolidColor, { 0.0f, 0.0f }));
    const int unresolvedOutputId = NodeId(
        unresolvedGraph.AddOutputNode({ 260.0f, 0.0f }, true));
    Require(
        unresolvedGraph.TryConnectSockets(
            unresolvedSourceId,
            kImageOutputSocketId,
            unresolvedOutputId,
            kImageInputSocketId) &&
        unresolvedGraph.IsOutputConnected(),
        "unresolved-definition fixture should initially execute");
    unresolvedGraph.EditNodes();
    Node* unresolvedSource =
        unresolvedGraph.FindNode(unresolvedSourceId);
    unresolvedSource->definitionResolved = false;
    unresolvedSource->definitionResolutionError =
        "Injected exact-definition mismatch.";
    Require(
        !unresolvedGraph.IsOutputConnected() &&
        unresolvedGraph.GetOutputConnectionDiagnostic().find(
            "Injected exact-definition mismatch") != std::string::npos,
        "completed-chain analysis should fail closed on any unresolved exact definition");

    Graph unresolvedOptionalGraph;
    const int optionalImageAId = NodeId(
        unresolvedOptionalGraph.AddImageNode(
            TestImagePayload(), { 0.0f, 0.0f }));
    const int optionalImageBId = NodeId(
        unresolvedOptionalGraph.AddImageNode(
            TestImagePayload(), { 0.0f, 160.0f }));
    const int optionalMaskId = NodeId(
        unresolvedOptionalGraph.AddMaskGeneratorNode(
            MaskGeneratorKind::Solid, { 220.0f, 280.0f }));
    const int optionalMixId = NodeId(
        unresolvedOptionalGraph.AddMixNode(
            { 220.0f, 80.0f }));
    const int optionalOutputId = NodeId(
        unresolvedOptionalGraph.AddOutputNode(
            { 440.0f, 80.0f }, true));
    Require(
        unresolvedOptionalGraph.TryConnectSockets(
            optionalImageAId,
            kImageOutputSocketId,
            optionalMixId,
            kMixInputASocketId) &&
            unresolvedOptionalGraph.TryConnectSockets(
                optionalImageBId,
                kImageOutputSocketId,
                optionalMixId,
                kMixInputBSocketId) &&
            unresolvedOptionalGraph.TryConnectSockets(
                optionalMaskId,
                kMaskOutputSocketId,
                optionalMixId,
                kMixFactorSocketId) &&
            unresolvedOptionalGraph.TryConnectSockets(
                optionalMixId,
                kImageOutputSocketId,
                optionalOutputId,
                kImageInputSocketId) &&
            unresolvedOptionalGraph.IsOutputConnected(),
        "optional render-dependency fixture should initially execute");
    unresolvedOptionalGraph.EditNodes();
    Node* unresolvedOptionalMask =
        unresolvedOptionalGraph.FindNode(optionalMaskId);
    unresolvedOptionalMask->definitionResolved = false;
    unresolvedOptionalMask->definitionResolutionError =
        "Injected optional-mask definition mismatch.";
    Require(
        !unresolvedOptionalGraph.IsOutputConnected() &&
            unresolvedOptionalGraph.GetOutputConnectionDiagnostic().find(
                "Injected optional-mask definition mismatch") !=
                std::string::npos,
        "an unresolved authored mask must fail closed instead of "
        "disappearing into an unmasked render snapshot");

    Graph legacyFrequencyGraph;
    const int legacyMaskId = NodeId(
        legacyFrequencyGraph.AddFrequencyMaskNode(
            FrequencyMaskShape::LowPass, { 0.0f, 0.0f }));
    const int legacyOutputId = NodeId(
        legacyFrequencyGraph.AddOutputNode({ 260.0f, 0.0f }, true));
    Require(
        !legacyFrequencyGraph.TryConnectSockets(
            legacyMaskId,
            kMaskOutputSocketId,
            legacyOutputId,
            kImageInputSocketId),
        "Output v2 should reject a legacy frequency Mask instead of "
        "misrepresenting it as a Channel");
    Require(
        !legacyFrequencyGraph.IsOutputConnected(),
        "a rejected legacy frequency Mask must never advertise a completed output");
}

void TestImageAndScalarThroughDataMathStaysImage() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 120.0f }));
    const int multiplyId = NodeId(graph.AddDataMathNode(DataMathMode::Multiply, { 220.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 440.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, multiplyId, kMixInputASocketId),
        "full image should feed Data Math multiply input A");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, multiplyId, kMixInputBSocketId),
        "scalar stream should broadcast into Data Math input B");
    Require(!graph.IsScalarSocketStream(multiplyId, kImageOutputSocketId),
        "Data Math with image plus scalar inputs should remain a full image stream");
    Require(graph.TryConnectSockets(multiplyId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "image plus scalar Data Math output should connect to output image input");
}

void TestFullImageDataMathRejectedByScalarOnlyInputs() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int clampId = NodeId(graph.AddDataMathNode(DataMathMode::Clamp, { 220.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, clampId, kMixInputASocketId),
        "full image should feed Data Math clamp input A");
    Require(!graph.CanConnectSockets(clampId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "full-image Data Math output should not connect to scalar-only factor inputs");
}

void TestLegacyMaskCombineRemainsScalar() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskAId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int maskBId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::RadialGradient, { 0.0f, 120.0f }));
    const int combineId = NodeId(graph.AddMaskCombineNode(MaskCombineMode::Intersect, { 220.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(maskAId, kMaskOutputSocketId, combineId, kMaskCombineInputASocketId),
        "legacy MaskCombine input A should still accept scalar streams");
    Require(graph.TryConnectSockets(maskBId, kMaskOutputSocketId, combineId, kMaskCombineInputBSocketId),
        "legacy MaskCombine input B should still accept scalar streams");
    Require(graph.IsScalarSocketStream(combineId, kMaskOutputSocketId),
        "legacy MaskCombine should still output a scalar stream");
    Require(graph.TryConnectSockets(combineId, kMaskOutputSocketId, previewId, kPreviewInputSocketId),
        "legacy MaskCombine saved-node behavior should still preview as scalar");
}

void TestLutNodeConnectionsAndScalarPropagation() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 120.0f }));
    const int lutId = NodeId(graph.AddLutNode({}, { 220.0f, 0.0f }));
    const int scalarLutId = NodeId(graph.AddLutNode({}, { 220.0f, 160.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 440.0f, 0.0f }, true));
    const int mixId = NodeId(graph.AddMixNode({ 440.0f, 180.0f }));

    Require(graph.CanConnectSockets(imageId, kImageOutputSocketId, lutId, kImageInputSocketId),
        "full image output should connect to LUT image input");
    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, lutId, kImageInputSocketId),
        "full image output should wire into LUT image input");
    Require(graph.CanConnectSockets(maskId, kMaskOutputSocketId, lutId, kMaskInputSocketId),
        "mask output should connect to LUT mask input");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, lutId, kMaskInputSocketId),
        "mask output should wire into LUT mask input");
    Require(graph.TryConnectSockets(lutId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "LUT image output should connect to output image input");

    Require(graph.CanConnectSockets(maskId, kMaskOutputSocketId, scalarLutId, kImageInputSocketId),
        "scalar mask output should be allowed into LUT image input");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, scalarLutId, kImageInputSocketId),
        "scalar mask output should connect into LUT image input");
    Require(graph.IsScalarSocketStream(scalarLutId, kImageOutputSocketId),
        "LUT image output should preserve scalar lineage when fed from a scalar image stream");
    Require(graph.TryConnectSockets(scalarLutId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar LUT output should connect to scalar-only downstream inputs");
}

void TestViewportTilePlannerCoverage() {
    ViewportTilingSettings settings;
    settings.mode = ViewportTilingMode::Always;
    settings.tileSize = 512;
    settings.haloPixels = 16;

    const int width = 1300;
    const int height = 777;
    const std::vector<RenderTileRect> tiles = RenderTiling::PlanTiles(width, height, settings);
    Require(tiles.size() == 6, "odd-sized image should be split into the expected tile count");

    std::vector<unsigned char> coverage(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (const RenderTileRect& tile : tiles) {
        Require(tile.width > 0 && tile.height > 0, "tile content rect should be non-empty");
        Require(tile.x >= 0 && tile.y >= 0, "tile content rect should start inside the canvas");
        Require(tile.x + tile.width <= width && tile.y + tile.height <= height,
            "tile content rect should be clamped to the canvas");
        Require(tile.haloX >= 0 && tile.haloY >= 0, "tile halo should start inside the canvas");
        Require(tile.haloX + tile.haloWidth <= width && tile.haloY + tile.haloHeight <= height,
            "tile halo should be clamped to the canvas");
        Require(tile.haloX <= tile.x && tile.haloY <= tile.y,
            "tile halo should include the content origin");
        Require(tile.haloX + tile.haloWidth >= tile.x + tile.width &&
                tile.haloY + tile.haloHeight >= tile.y + tile.height,
            "tile halo should include the full content rect");

        for (int y = tile.y; y < tile.y + tile.height; ++y) {
            for (int x = tile.x; x < tile.x + tile.width; ++x) {
                unsigned char& count = coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                ++count;
            }
        }
    }

    for (unsigned char count : coverage) {
        Require(count == 1, "tile content coverage should have no gaps or duplicate pixels");
    }
}

void TestViewportTilingModeDecisions() {
    ViewportTilingSettings settings;
    settings.tileSize = 1024;
    settings.autoPixelThresholdMegapixels = 4;

    settings.mode = ViewportTilingMode::Off;
    Require(!RenderTiling::ShouldUseTiling(settings, 4096, 4096),
        "Off mode should never select tiled rendering");

    settings.mode = ViewportTilingMode::Always;
    Require(!RenderTiling::ShouldUseTiling(settings, 512, 512),
        "Always mode should skip tiling when the image fits in one tile");
    Require(RenderTiling::ShouldUseTiling(settings, 2048, 512),
        "Always mode should tile images larger than the selected tile size");

    settings.mode = ViewportTilingMode::Auto;
    settings.tileSize = 4096;
    Require(!RenderTiling::ShouldUseTiling(settings, 1000, 1000),
        "Auto mode should leave small images on the full-canvas path");
    Require(RenderTiling::ShouldUseTiling(settings, 2500, 2000),
        "Auto mode should tile images beyond the megapixel threshold");
}

void TestViewportTileSafeGraphClassification() {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 3;

    RenderGraphNode image;
    image.nodeId = 1;
    image.kind = RenderGraphNodeKind::Image;
    image.image.width = 1024;
    image.image.height = 768;
    image.image.channels = 4;
    image.image.pixels = MakeSharedPixelBufferOwned(std::vector<unsigned char>(1024u * 768u * 4u, 255u));

    RenderGraphNode layer;
    layer.nodeId = 2;
    layer.kind = RenderGraphNodeKind::Layer;
    layer.layerJson["type"] = "Brightness";

    RenderGraphNode output;
    output.nodeId = 3;
    output.kind = RenderGraphNodeKind::Output;

    graph.nodes = { image, layer, output };
    graph.links = {
        { 1, "imageOut", 2, "imageIn" },
        { 2, "imageOut", 3, "imageIn" },
    };

    std::string reason;
    Require(RenderTiling::IsGraphTileSafe(graph, 1024, 768, &reason),
        "simple image plus basic adjustment graph should be tile-safe");

    graph.nodes[1].layerJson["type"] = "Blur";
    Require(!RenderTiling::IsGraphTileSafe(graph, 1024, 768, &reason),
        "unsupported layer types should require full-canvas fallback");
    RenderGraphRegionPlan fallbackPlan = RenderTiling::PlanGraphRegions(graph, 1024, 768);
    Require(fallbackPlan.valid && !fallbackPlan.tileable && fallbackPlan.requiresFullFrame &&
            fallbackPlan.reason.find("no trusted region mapping") != std::string::npos,
        "a legal unsupported layer should produce an explained full-frame fallback, not an invalid graph");

    graph.nodes[1].kind = RenderGraphNodeKind::RawDevelop;
    Require(!RenderTiling::IsGraphTileSafe(graph, 1024, 768, &reason),
        "Raw Develop should remain on full-canvas fallback until it is tile-aware");
}

void TestRegionAwareTilePlanningAndCancellation() {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 4;

    RenderGraphNode image;
    image.nodeId = 1;
    image.kind = RenderGraphNodeKind::Image;
    image.definitionId = "stack:graph/image";

    RenderGraphNode gaussian;
    gaussian.nodeId = 2;
    gaussian.kind = RenderGraphNodeKind::Layer;
    gaussian.definitionId = "stack:layer/gaussianblur";
    gaussian.layerJson = { { "type", "GaussianBlur" }, { "amount", 3.9 } };

    RenderGraphNode technical;
    technical.nodeId = 3;
    technical.kind = RenderGraphNodeKind::TechnicalImage;
    technical.definitionId = "stack:technical/exposure";

    RenderGraphNode output;
    output.nodeId = 4;
    output.kind = RenderGraphNodeKind::Output;
    output.definitionId = "stack:graph/output";

    graph.nodes = { image, gaussian, technical, output };
    graph.links = {
        { 1, "imageOut", 2, "imageIn" },
        { 2, "imageOut", 3, "imageIn" },
        { 3, "imageOut", 4, "imageIn" },
    };

    RenderGraphRegionPlan plan = RenderTiling::PlanGraphRegions(graph, 640, 480);
    Require(plan.valid && plan.tileable && !plan.requiresFullFrame,
        "Gaussian plus pointwise graph should have a trusted region plan");
    Require(plan.requiredHaloX == 3 && plan.requiredHaloY == 3,
        "Gaussian graph should derive its exact integer support without a user halo");
    Require(plan.outputSpatial.fullWindow == Stack::NodeMath::Rect{ 0, 0, 640, 480 } &&
            plan.outputSpatial.dataWindow == plan.outputSpatial.fullWindow &&
            plan.outputSpatial.rasterOrigin == Stack::NodeMath::RasterOrigin::BottomLeft,
        "live region plan should declare the full/data windows and graph raster origin");
    Require(std::any_of(plan.stages.begin(), plan.stages.end(), [](const RenderGraphRegionStage& stage) {
        return stage.nodeId == 2 &&
            stage.capability == Stack::NodeMath::CapabilityClass::Neighborhood &&
            stage.border == Stack::NodeMath::BorderPolicy::Clamp &&
            stage.support == Stack::NodeMath::NeighborhoodSupport{ 3, 3, 3, 3 };
    }), "region plan should expose the Gaussian neighborhood and clamp border stage");

    RenderGraphNode box = gaussian;
    box.nodeId = 5;
    box.definitionId = "stack:layer/boxblur";
    box.layerJson = { { "type", "BoxBlur" }, { "amount", 2.0 } };
    graph.nodes.insert(graph.nodes.end() - 1, box);
    graph.links = {
        { 1, "imageOut", 2, "imageIn" },
        { 2, "imageOut", 5, "imageIn" },
        { 5, "imageOut", 3, "imageIn" },
        { 3, "imageOut", 4, "imageIn" },
    };
    plan = RenderTiling::PlanGraphRegions(graph, 640, 480);
    Require(plan.valid && plan.requiredHaloX == 5 && plan.requiredHaloY == 5,
        "neighborhood support should accumulate in authored dependency order");

    ViewportTilingSettings settings;
    settings.mode = ViewportTilingMode::Always;
    settings.tileSize = 256;
    settings.haloPixels = 1;
    const std::vector<RenderTileRect> tiles = RenderTiling::PlanTiles(
        640, 480, settings, plan.requiredHaloX, plan.requiredHaloY);
    Require(tiles.size() > 1, "region-aware plan should produce multiple tiles for the fixture");
    const auto interior = std::find_if(tiles.begin(), tiles.end(), [](const RenderTileRect& tile) {
        return tile.x > 0 && tile.y > 0;
    });
    Require(interior != tiles.end() &&
            interior->x - interior->haloX == 5 &&
            interior->y - interior->haloY == 5,
        "planner-derived halo should override a smaller extra-halo preference");

    std::size_t callbackCount = 0;
    const TileIterationResult canceled = RenderTiling::IterateTiles(
        tiles,
        [&]() { return callbackCount == 2; },
        [&](const RenderTileRect&, std::size_t) {
            ++callbackCount;
            return true;
        });
    Require(canceled.status == TileIterationStatus::Canceled &&
            canceled.completedTiles == 2 && callbackCount == 2,
        "tile iteration should stop before publishing work after cancellation");

    callbackCount = 0;
    const TileIterationResult failed = RenderTiling::IterateTiles(
        tiles,
        {},
        [&](const RenderTileRect&, std::size_t index) {
            ++callbackCount;
            return index != 1;
        });
    Require(failed.status == TileIterationStatus::Failed &&
            failed.completedTiles == 1 && callbackCount == 2,
        "tile iteration should distinguish stage failure from cancellation");
}

void TestLutImporterCubeVariants() {
    const std::string lut1dPath = WriteTempTextFile(
        "stack_lut_1d",
        ".cube",
        "TITLE \"OneD\"\n"
        "LUT_1D_SIZE 2\n"
        "DOMAIN_MIN 0 0 0\n"
        "DOMAIN_MAX 1 1 1\n"
        "0 0 0\n"
        "1 1 1\n");
    const ColorLut::LutImportResult lut1d = ColorLut::ImportLutFile(lut1dPath);
    Require(lut1d.success, ".cube 1D LUT should import successfully");
    Require(ColorLut::HasLut1D(lut1d.payload), ".cube 1D LUT should populate canonical 1D data");
    Require(!ColorLut::HasLut3D(lut1d.payload), ".cube 1D LUT should not populate 3D data");

    const std::string lut3dPath = WriteTempTextFile(
        "stack_lut_3d",
        ".cube",
        "TITLE \"ThreeD\"\n"
        "LUT_3D_SIZE 2\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "1 1 0\n"
        "0 0 1\n"
        "1 0 1\n"
        "0 1 1\n"
        "1 1 1\n");
    const ColorLut::LutImportResult lut3d = ColorLut::ImportLutFile(lut3dPath);
    Require(lut3d.success, ".cube 3D LUT should import successfully");
    Require(ColorLut::HasLut3D(lut3d.payload), ".cube 3D LUT should populate canonical 3D data");
    Require(!ColorLut::HasLut1D(lut3d.payload), ".cube 3D LUT should not populate standalone 1D data");

    const std::string combinedPath = WriteTempTextFile(
        "stack_lut_combined",
        ".cube",
        "TITLE \"Combined\"\n"
        "LUT_1D_SIZE 2\n"
        "LUT_3D_SIZE 2\n"
        "0 0 0\n"
        "1 1 1\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "1 1 0\n"
        "0 0 1\n"
        "1 0 1\n"
        "0 1 1\n"
        "1 1 1\n");
    const ColorLut::LutImportResult combined = ColorLut::ImportLutFile(combinedPath);
    Require(combined.success, "combined .cube LUT should import successfully");
    Require(ColorLut::HasShaper1D(combined.payload), "combined .cube LUT should populate canonical shaper 1D data");
    Require(ColorLut::HasLut3D(combined.payload), "combined .cube LUT should populate canonical 3D data");

    const std::string invalidPath = WriteTempTextFile(
        "stack_lut_invalid",
        ".cube",
        "TITLE \"Broken\"\n"
        "0 0 0\n");
    const ColorLut::LutImportResult invalid = ColorLut::ImportLutFile(invalidPath);
    Require(!invalid.success, "malformed .cube LUT should fail import");

    const std::string extraSamplePath = WriteTempTextFile(
        "stack_lut_extra_sample",
        ".cube",
        "LUT_1D_SIZE 2\n"
        "0 0 0\n"
        "1 1 1\n"
        "0.5 0.5 0.5\n");
    Require(
        !ColorLut::ImportLutFile(extraSamplePath).success,
        ".cube import should reject samples beyond its declared dimensions");

    const std::string invalidDomainPath = WriteTempTextFile(
        "stack_lut_invalid_domain",
        ".cube",
        "LUT_1D_SIZE 2\n"
        "DOMAIN_MIN 1 0 0\n"
        "DOMAIN_MAX 0 1 1\n"
        "0 0 0\n"
        "1 1 1\n");
    Require(
        !ColorLut::ImportLutFile(invalidDomainPath).success,
        "LUT import should reject reversed channel domains");

    const std::string spi3dPath = WriteTempTextFile(
        "stack_lut_spi3d",
        ".spi3d",
        "SPILUT 1.0\n"
        "2 2 2\n"
        "0 0 0 0 0 0\n"
        "0 0 1 0 0 1\n"
        "0 1 0 0 1 0\n"
        "0 1 1 0 1 1\n"
        "1 0 0 1 0 0\n"
        "1 0 1 1 0 1\n"
        "1 1 0 1 1 0\n"
        "1 1 1 1 1 1\n");
    Require(
        ColorLut::ImportLutFile(spi3dPath).success,
        "complete .spi3d grids should import successfully");

    const std::string sparseSpi3dPath = WriteTempTextFile(
        "stack_lut_sparse_spi3d",
        ".spi3d",
        "SPILUT 1.0\n"
        "100000 100000 100000\n"
        "0 0 0 0 0 0\n");
    Require(
        !ColorLut::ImportLutFile(sparseSpi3dPath).success,
        "sparse huge .spi3d declarations should fail without allocating their declared cube");

    const std::string duplicateSpi3dPath = WriteTempTextFile(
        "stack_lut_duplicate_spi3d",
        ".spi3d",
        "SPILUT 1.0\n"
        "2 2 2\n"
        "0 0 0 0 0 0\n"
        "0 0 0 0 0 1\n"
        "0 1 0 0 1 0\n"
        "0 1 1 0 1 1\n"
        "1 0 0 1 0 0\n"
        "1 0 1 1 0 1\n"
        "1 1 0 1 1 0\n"
        "1 1 1 1 1 1\n");
    Require(
        !ColorLut::ImportLutFile(duplicateSpi3dPath).success,
        ".spi3d import should reject duplicate coordinates that hide a missing sample");

    ColorLut::LutPayload overflowPayload;
    overflowPayload.lut3D.size = std::numeric_limits<int>::max();
    Require(
        !ColorLut::HasLut3D(overflowPayload),
        "LUT payload validation should reject overflowing 3D dimensions");
}

void TestLutCreatorRoundTripSidecar() {
    ColorLut::LutCreatorImage source;
    ColorLut::LutCreatorImage target;
    source.sourcePath = "source.png";
    target.sourcePath = "target.png";
    source.width = 4;
    source.height = 4;
    target.width = 4;
    target.height = 4;
    source.channels = 4;
    source.originalChannels = 4;
    target.channels = 4;
    target.originalChannels = 4;
    source.pixels.resize(4u * 4u * 4u, 255u);
    target.pixels.resize(4u * 4u * 4u, 255u);

    ColorLut::LutCreatorImage truncated = source;
    truncated.pixels.resize(3);
    ColorLut::LutCreatorSettings invalidSettings;
    Require(
        !ColorLut::CreateLutFromImages(
            truncated, target, invalidSettings).success,
        "LUT creator should reject truncated source pixel buffers");
    ColorLut::LutCreatorSettings overflowingSettings;
    overflowingSettings.lutSize =
        std::numeric_limits<int>::max();
    Require(
        !ColorLut::CreateLutFromImages(
            source, target, overflowingSettings).success,
        "LUT creator should reject overflowing cubic dimensions before allocation");

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::size_t base = (static_cast<std::size_t>(y) * 4u + static_cast<std::size_t>(x)) * 4u;
            const float r = static_cast<float>(x) / 3.0f;
            const float g = static_cast<float>(y) / 3.0f;
            const float b = static_cast<float>(x + y) / 6.0f;
            source.pixels[base] = static_cast<unsigned char>(std::round(r * 255.0f));
            source.pixels[base + 1u] = static_cast<unsigned char>(std::round(g * 255.0f));
            source.pixels[base + 2u] = static_cast<unsigned char>(std::round(b * 255.0f));

            const float mappedR = std::clamp(r * 0.80f + 0.10f, 0.0f, 1.0f);
            const float mappedG = std::clamp(g * 0.65f + 0.18f, 0.0f, 1.0f);
            const float mappedB = std::clamp(b * 0.75f + 0.12f, 0.0f, 1.0f);
            target.pixels[base] = static_cast<unsigned char>(std::round(mappedR * 255.0f));
            target.pixels[base + 1u] = static_cast<unsigned char>(std::round(mappedG * 255.0f));
            target.pixels[base + 2u] = static_cast<unsigned char>(std::round(mappedB * 255.0f));
        }
    }

    ColorLut::LutCreatorSettings settings;
    settings.lutSize = 17;
    settings.maxSamples = 128;
    settings.manualStride = 1;
    settings.smoothPasses = 1;
    settings.smoothStrength = 0.25f;
    settings.identityBias = 0.08f;
    settings.observationThreshold = 1.0f;
    settings.label = "RoundTrip Label";
    settings.importedTitle = "RoundTrip Title";
    settings.useMode = ColorLut::LutUseMode::PreViewTransform;
    settings.inputTransform = ColorLut::LutTransferFunction::SrgbEncode;
    settings.outputTransform = ColorLut::LutTransferFunction::SrgbDecode;

    const ColorLut::LutCreatorResult created =
        ColorLut::CreateLutFromImages(source, target, settings);
    Require(created.success, "LUT creator should succeed for matching source and target images");
    Require(ColorLut::HasLut3D(created.payload), "LUT creator should populate 3D LUT data");
    Require(created.stats.meanAbsoluteError < 0.08f, "LUT creator should fit the test mapping with a low mean absolute error");

    const std::string path = WriteTempTextFile("stack_generated_lut", ".cube", "");
    std::string saveMessage;
    Require(
        ColorLut::SaveCubeLutWithSidecar(
            path,
            created.payload,
            settings,
            created.stats,
            source.sourcePath,
            target.sourcePath,
            &saveMessage),
        "generated LUT should save as .cube plus sidecar");

    const ColorLut::LutImportResult imported = ColorLut::ImportLutFile(path);
    Require(imported.success, "saved generated LUT should import successfully");
    Require(imported.payload.importedTitle == "RoundTrip Title", "generated LUT sidecar should restore the saved imported title");
    Require(imported.payload.label == "RoundTrip Label", "generated LUT sidecar should restore the saved label");
    Require(imported.payload.useMode == ColorLut::LutUseMode::PreViewTransform, "generated LUT sidecar should restore the saved use mode");
    Require(imported.payload.inputTransform == ColorLut::LutTransferFunction::SrgbEncode, "generated LUT sidecar should restore the saved input transform");
    Require(imported.payload.outputTransform == ColorLut::LutTransferFunction::SrgbDecode, "generated LUT sidecar should restore the saved output transform");
}

Stack::Mfsr::MfsrFramePacketSummary TestMfsrFrame(
    Stack::Mfsr::MfsrFrameClass frameClass,
    bool reference,
    int width = 4000,
    int height = 3000) {
    Stack::Mfsr::MfsrFramePacketSummary frame;
    frame.frameClass = frameClass;
    frame.isReference = reference;
    frame.width = width;
    frame.height = height;
    frame.bitDepth = frameClass == Stack::Mfsr::MfsrFrameClass::RasterLinear ? 16 : 14;
    frame.source.sourcePath = reference ? "reference" : "support";
    frame.source.sourceFingerprint = static_cast<std::size_t>(width * 31 + height);
    if (frameClass == Stack::Mfsr::MfsrFrameClass::RawMosaic ||
        frameClass == Stack::Mfsr::MfsrFrameClass::RawLinear) {
        frame.raw.present = true;
        frame.raw.visibleWidth = width;
        frame.raw.visibleHeight = height;
        frame.raw.rawWidth = width;
        frame.raw.rawHeight = height;
        frame.raw.bitDepth = frame.bitDepth;
        frame.raw.cfaPattern = Raw::CfaPattern::RGGB;
        frame.raw.pixelLayout = frameClass == Stack::Mfsr::MfsrFrameClass::RawMosaic
            ? Raw::RawPixelLayout::MosaicBayer
            : Raw::RawPixelLayout::LinearRgb;
        frame.raw.sampleFormat = Raw::RawSampleFormat::UInt16;
        frame.raw.mosaiced = frameClass == Stack::Mfsr::MfsrFrameClass::RawMosaic;
        frame.raw.blackLevel = 512.0f;
        frame.raw.whiteLevel = 16383.0f;
    } else if (frameClass == Stack::Mfsr::MfsrFrameClass::RasterLinear) {
        frame.raster.present = true;
        frame.raster.width = width;
        frame.raster.height = height;
        frame.raster.channels = 4;
        frame.raster.bitDepth = frame.bitDepth;
        frame.raster.linearLight = true;
        frame.raster.colorSpaceKnown = true;
        frame.raster.colorSpaceName = "linear-test";
    }
    return frame;
}

void TestMfsrValidationRejectsEmptyAndMissingReference() {
    using namespace Stack::Mfsr;

    const MfsrValidationResult empty = ValidateMfsrFrameSet({});
    Require(!empty.valid, "MFSR validation should reject zero inputs");
    Require(empty.HasError(MfsrValidationCode::EmptyInputSet), "MFSR zero-input rejection should be explicit");

    const std::vector<MfsrFramePacketSummary> noReference = {
        TestMfsrFrame(MfsrFrameClass::RawMosaic, false)
    };
    const MfsrValidationResult missingReference = ValidateMfsrFrameSet(noReference);
    Require(!missingReference.valid, "MFSR validation should require a reference input");
    Require(missingReference.HasError(MfsrValidationCode::MissingReferenceInput),
        "MFSR missing-reference rejection should be explicit");
}

void TestMfsrValidationRejectsMixedAndUnknownInputs() {
    using namespace Stack::Mfsr;

    const std::vector<MfsrFramePacketSummary> mixed = {
        TestMfsrFrame(MfsrFrameClass::RawMosaic, true),
        TestMfsrFrame(MfsrFrameClass::RasterLinear, false)
    };
    const MfsrValidationResult mixedResult = ValidateMfsrFrameSet(mixed);
    Require(!mixedResult.valid, "MFSR validation should reject mixed RAW/raster bursts");
    Require(mixedResult.inputFamily == MfsrInputFamily::MixedUnsupported,
        "MFSR mixed bursts should report the mixed unsupported family");
    Require(mixedResult.HasError(MfsrValidationCode::MixedInputFamilies),
        "MFSR mixed-family rejection should be explicit");

    std::vector<MfsrFramePacketSummary> unknown = {
        TestMfsrFrame(MfsrFrameClass::Unknown, true)
    };
    const MfsrValidationResult unknownResult = ValidateMfsrFrameSet(unknown);
    Require(!unknownResult.valid, "MFSR validation should reject unknown frame kinds");
    Require(unknownResult.HasError(MfsrValidationCode::UnsupportedInputKind),
        "MFSR unknown-kind rejection should be explicit");
}

void TestMfsrValidationAllowsSingleFamilyWithReference() {
    using namespace Stack::Mfsr;

    std::vector<MfsrFramePacketSummary> rawBurst = {
        TestMfsrFrame(MfsrFrameClass::RawMosaic, true),
        TestMfsrFrame(MfsrFrameClass::RawMosaic, false)
    };
    const MfsrValidationResult rawResult = ValidateMfsrFrameSet(rawBurst);
    Require(rawResult.valid, "MFSR validation should allow a simple RAW burst with one reference");
    Require(rawResult.inputFamily == MfsrInputFamily::RawBurst,
        "MFSR RAW burst validation should report RAW family");
    Require(rawResult.referenceFrameIndex == 0, "MFSR validation should remember the reference frame index");

    rawBurst[1].width = 3996;
    const MfsrValidationResult dimensionWarning = ValidateMfsrFrameSet(rawBurst);
    Require(dimensionWarning.valid, "MFSR Phase 1 should warn, not fail, for dimension mismatch");
    Require(dimensionWarning.HasWarning(MfsrValidationCode::IncompatibleDimensions),
        "MFSR dimension mismatch should produce a warning for later phases");

    rawBurst[1].raw.cfaPattern = Raw::CfaPattern::BGGR;
    const MfsrValidationResult metadataWarning = ValidateMfsrFrameSet(rawBurst);
    Require(metadataWarning.valid, "MFSR Phase 1 should warn, not fail, for RAW metadata mismatch");
    Require(metadataWarning.HasWarning(MfsrValidationCode::IncompatibleRawMetadata),
        "MFSR RAW metadata mismatch should produce a warning for later phases");
}

void TestMfsrCacheKeyFingerprintsReactToInputsAndSettings() {
    using namespace Stack::Mfsr;

    MfsrSettings settingsA;
    MfsrSettings settingsB = settingsA;
    settingsB.scalePreset = MfsrScalePreset::Scale150;

    const std::size_t settingsFingerprintA = BuildMfsrSettingsFingerprint(settingsA);
    const std::size_t settingsFingerprintB = BuildMfsrSettingsFingerprint(settingsB);
    Require(settingsFingerprintA != settingsFingerprintB,
        "MFSR settings fingerprint should change when settings change");

    MfsrCacheKey key;
    key.algorithmVersion = settingsA.algorithmVersion;
    key.inputFamily = MfsrInputFamily::RawBurst;
    key.inputSetFingerprint = BuildMfsrFrameSourceFingerprint(TestMfsrFrame(MfsrFrameClass::RawMosaic, true).source);
    key.settingsFingerprint = settingsFingerprintA;

    const std::size_t keyFingerprintA = BuildMfsrCacheKeyFingerprint(key);
    key.settingsFingerprint = settingsFingerprintB;
    const std::size_t keyFingerprintB = BuildMfsrCacheKeyFingerprint(key);
    Require(keyFingerprintA != keyFingerprintB,
        "MFSR cache-key fingerprint should include settings fingerprint");

    key.settingsFingerprint = settingsFingerprintA;
    key.inputSetFingerprint += 1;
    const std::size_t keyFingerprintC = BuildMfsrCacheKeyFingerprint(key);
    Require(keyFingerprintA != keyFingerprintC,
        "MFSR cache-key fingerprint should include input-set fingerprint");
}

void TestMfsrNodeShellSocketsAndConnections() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageAId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int imageBId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 180.0f }));
    const int mfsrId = NodeId(graph.AddMfsrNode(MfsrPayload{}, { 260.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 520.0f, 0.0f }, true));

    const Node* mfsr = graph.FindNode(mfsrId);
    Require(mfsr && mfsr->kind == NodeKind::Mfsr, "MFSR node shell should be created");
    Require(mfsr->title == "MFSR", "MFSR node shell should have a stable visible title");
    Require(graph.DefaultInputSocket(*mfsr) == kMfsrReferenceInputSocketId,
        "MFSR default input should be the reference socket");
    Require(graph.DefaultOutputSocket(*mfsr) == kImageOutputSocketId,
        "MFSR default output should be the image socket");
    Require(graph.FindSocket(mfsrId, kMfsrReferenceInputSocketId),
        "MFSR should expose a reference input");
    Require(graph.FindSocket(mfsrId, MfsrInputSocketId(1)),
        "MFSR should expose a first support frame input");
    Require(graph.FindSocket(mfsrId, kImageOutputSocketId),
        "MFSR should expose an image output");

    std::vector<SocketDefinition> visibleSockets = graph.GetSockets(*mfsr, true);
    const auto visibleSocketCount = [&](const std::string& socketId) {
        return std::count_if(visibleSockets.begin(), visibleSockets.end(), [&](const SocketDefinition& socket) {
            return socket.id == socketId;
        });
    };
    Require(visibleSocketCount(kMfsrReferenceInputSocketId) == 1,
        "MFSR visible sockets should include Reference");
    Require(visibleSocketCount(MfsrInputSocketId(1)) == 1,
        "MFSR visible sockets should include Frame 2");
    Require(visibleSocketCount(MfsrInputSocketId(2)) == 0,
        "MFSR should not reveal Frame 3 before a support frame is connected");

    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, mfsrId, kMfsrReferenceInputSocketId),
        "image should connect to MFSR reference input");
    Require(graph.TryConnectSockets(imageBId, kImageOutputSocketId, mfsrId, MfsrInputSocketId(1)),
        "image should connect to MFSR support input");
    visibleSockets = graph.GetSockets(*graph.FindNode(mfsrId), true);
    Require(std::any_of(visibleSockets.begin(), visibleSockets.end(), [&](const SocketDefinition& socket) {
        return socket.id == MfsrInputSocketId(2);
    }), "MFSR should reveal the next support input after Frame 2 is connected");

    Require(graph.TryConnectSockets(mfsrId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "MFSR image output should connect downstream");
    Require(graph.IsOutputConnected(), "MFSR with reference and support should complete an output chain");
}

void TestMfsrNodeShellRejectsScalarAndMixedFamilies() {
    using namespace EditorNodeGraph;

    Graph scalarGraph;
    const int maskId = NodeId(scalarGraph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int layerId = NodeId(scalarGraph.AddLayerNode(LayerType::Brightness, 0, { 220.0f, 0.0f }));
    const int mfsrScalarId = NodeId(scalarGraph.AddMfsrNode(MfsrPayload{}, { 440.0f, 0.0f }));
    Require(scalarGraph.TryConnectSockets(maskId, kMaskOutputSocketId, layerId, kImageInputSocketId),
        "test setup should connect scalar mask through a layer");
    std::string scalarError;
    Require(!scalarGraph.CanConnectSockets(layerId, kImageOutputSocketId, mfsrScalarId, kMfsrReferenceInputSocketId, nullptr, &scalarError),
        "MFSR should reject scalar image streams");
    Require(scalarError.find("full image") != std::string::npos,
        "MFSR scalar rejection should explain the full-image requirement");

    Graph mixedGraph;
    RawSourcePayload rawPayload;
    rawPayload.label = "RAW";
    rawPayload.sourcePath = "burst-a.dng";
    const int rawSourceId = NodeId(mixedGraph.AddRawSourceNode(rawPayload, { 0.0f, 0.0f }));
    const int rawDevelopId = NodeId(mixedGraph.AddRawDevelopNode(RawDevelopPayload{}, { 220.0f, 0.0f }));
    const int rasterId = NodeId(mixedGraph.AddImageNode(TestImagePayload(), { 0.0f, 220.0f }));
    const int mfsrId = NodeId(mixedGraph.AddMfsrNode(MfsrPayload{}, { 440.0f, 0.0f }));

    Require(mixedGraph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDevelopId, kRawInputSocketId),
        "test setup should connect RAW source to develop");
    Require(mixedGraph.TryConnectSockets(rawDevelopId, kImageOutputSocketId, mfsrId, kMfsrReferenceInputSocketId),
        "MFSR should accept a RAW-derived reference image");

    std::string mixedError;
    Require(!mixedGraph.CanConnectSockets(rasterId, kImageOutputSocketId, mfsrId, MfsrInputSocketId(1), nullptr, &mixedError),
        "MFSR should reject mixed RAW-derived and raster-derived inputs");
    Require(mixedError.find("RAW-derived") != std::string::npos &&
            mixedError.find("raster-derived") != std::string::npos,
        "MFSR mixed-family rejection should name RAW-derived and raster-derived inputs");
}

void TestMfsrNodeShellSerializesRoundTrip() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageAId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int imageBId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 180.0f }));
    MfsrPayload payload;
    payload.settings.scalePreset = Stack::Mfsr::MfsrScalePreset::Scale150;
    payload.settings.qualityPreset = Stack::Mfsr::MfsrQualityPreset::Conservative;
    payload.placeholderStatus = "Phase 2 placeholder";
    const int mfsrId = NodeId(graph.AddMfsrNode(payload, { 260.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 520.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, mfsrId, kMfsrReferenceInputSocketId),
        "test setup should connect MFSR reference before serialization");
    Require(graph.TryConnectSockets(imageBId, kImageOutputSocketId, mfsrId, MfsrInputSocketId(1)),
        "test setup should connect MFSR support before serialization");
    Require(graph.TryConnectSockets(mfsrId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "test setup should connect MFSR output before serialization");

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);
    const Node* loadedMfsr = nullptr;
    for (const Node& node : loaded.GetNodes()) {
        if (node.kind == NodeKind::Mfsr) {
            loadedMfsr = &node;
            break;
        }
    }
    Require(loadedMfsr != nullptr, "MFSR node should survive graph serialization");
    Require(loadedMfsr->title == "MFSR", "MFSR title should survive graph serialization");
    Require(loadedMfsr->mfsr.settings.scalePreset == Stack::Mfsr::MfsrScalePreset::Scale150,
        "MFSR scale setting should survive graph serialization");
    Require(loadedMfsr->mfsr.settings.qualityPreset == Stack::Mfsr::MfsrQualityPreset::Conservative,
        "MFSR quality setting should survive graph serialization");
    Require(loadedMfsr->mfsr.placeholderStatus == "Phase 2 placeholder",
        "MFSR placeholder status should survive graph serialization");
    Require(loaded.IsOutputConnected(), "loaded MFSR graph should keep its completed output chain");
}

void TestRetiredNeuralDenoiseIsRejected() {
    using namespace EditorNodeGraph;

    const std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry> catalog =
        EditorNodeGraphDefinitions::BuildNodeCatalogEntries();
    Require(std::none_of(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.kind == NodeKind::RawNeuralDenoise;
    }), "retired RAW neural denoise should not be offered for new graphs");
    const EditorNodeGraphDefinitions::LiveNodeDefinition* retiredDefinition =
        EditorNodeGraphDefinitions::FindLiveNodeDefinition(
            NodeKind::RawNeuralDenoise, 0);
    Require(retiredDefinition == nullptr,
        "retired RAW neural denoise should not retain a compatibility definition");

    Graph graph;
    RawNeuralDenoisePayload payload;
    payload.settings.enabled = true;
    payload.settings.selectedModelId = "legacy-test-model";
    payload.settings.strength = 0.42f;
    payload.settings.runtimePreference = NeuralDenoise::RuntimePreference::Cuda;
    payload.settings.runRequestRevision = 7;
    payload.settings.tilePlan.tileSize = 768;
    payload.settings.tilePlan.overlap = 96;
    graph.AddRawNeuralDenoiseNode(std::move(payload), { 120.0f, 80.0f });

    const nlohmann::json serialized =
        SerializeGraphPayload(nlohmann::json::array(), graph);
    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);

    Require(!IsCurrentGraphPayload(serialized) && loaded.GetNodes().empty(),
        "graphs containing retired RAW neural denoise nodes should be rejected");
}

void TestTechnicalImageAndSourceMetadataSerializeRoundTrip() {
    using namespace EditorNodeGraph;

    ImagePayload image = TestImagePayload();
    image.sourceColorMetadata = Stack::NodeMath::InspectSourceColorMetadata(
        { 0xff, 0xd8, 0xff, 0xd9 }, image.width, image.height, image.originalChannels,
        Stack::NodeMath::LogicalPrecision::UInt8, "round-trip-untagged.jpg");

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(std::move(image), { 0.0f, 0.0f }));
    const int technicalId = NodeId(graph.AddTechnicalImageNode(
        Stack::NodeMath::TechnicalImageOperation::Exposure, { 220.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 440.0f, 0.0f }, true));
    Node* technical = graph.FindNode(technicalId);
    Require(technical != nullptr, "Technical Image node should be created");
    technical->technicalImageSettings.exposureValue = 1.75f;
    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId,
            technicalId, kImageInputSocketId),
        "test setup should connect source to Technical Image node");
    Require(graph.TryConnectSockets(technicalId, kImageOutputSocketId,
            outputId, kImageInputSocketId),
        "test setup should connect Technical Image node to output");

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);
    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);

    const Node* loadedImage = loaded.FindNode(imageId);
    const Node* loadedTechnical = loaded.FindNode(technicalId);
    Require(loadedImage != nullptr && loadedTechnical != nullptr,
        "source and Technical Image nodes should survive graph serialization");
    Require(loadedTechnical->kind == NodeKind::TechnicalImage &&
            loadedTechnical->technicalImageSettings.operation ==
                Stack::NodeMath::TechnicalImageOperation::Exposure &&
            std::abs(loadedTechnical->technicalImageSettings.exposureValue - 1.75f) < 0.001f,
        "Technical Image operation and parameter should survive graph serialization");
    Require(loadedImage->image.sourceColorMetadata.descriptor.color.state ==
                Stack::NodeMath::KnowledgeState::Unknown &&
            loadedImage->image.sourceColorMetadata.dependencyIdentity ==
                graph.FindNode(imageId)->image.sourceColorMetadata.dependencyIdentity,
        "descriptive Unknown source state and its dependency identity should survive graph serialization");
    Require(loaded.IsOutputConnected(),
        "loaded Technical Image graph should keep its completed output chain");
}

void TestCompositeNodeSerializesRoundTrip() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int compositeId = NodeId(graph.AddCompositeNode({ 120.0f, 80.0f }));
    if (Node* compositeNode = graph.FindNode(compositeId)) {
        compositeNode->title = "Composite Test";
        compositeNode->expanded = true;
    }

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);

    const Node* compositeNode = loaded.FindNode(compositeId);
    Require(compositeNode != nullptr, "Composite node should survive graph serialization");
    Require(compositeNode->kind == NodeKind::Composite, "Composite node should deserialize with the Composite kind");
    Require(compositeNode->title == "Composite Test", "Composite node title should survive graph serialization");
    Require(compositeNode->expanded, "Composite node expanded state should survive graph serialization");
}

void TestGraphInfoNoLayoutPayloadPreservesGraphOrderAndState() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int sourceAId = NodeId(graph.AddImageGeneratorNode(
        ImageGeneratorKind::SolidColor, { 720.0f, 410.0f }));
    const int sourceBId = NodeId(graph.AddImageGeneratorNode(
        ImageGeneratorKind::ColorGradient, { -180.0f, 90.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 360.0f, -240.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1080.0f, 640.0f }, true));

    Node* mix = graph.FindNode(mixId);
    Require(mix != nullptr, "no-layout graph fixture should contain its Mix node");
    mix->mixFactor = 0.37f;

    Require(graph.TryConnectSockets(
            sourceBId, kImageOutputSocketId, mixId, kMixInputBSocketId),
        "no-layout graph fixture should connect its B branch");
    Require(graph.TryConnectSockets(
            sourceAId, kImageOutputSocketId, mixId, kMixInputASocketId),
        "no-layout graph fixture should connect its A branch");
    Require(graph.TryConnectSockets(
            mixId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "no-layout graph fixture should connect to its output");
    Require(graph.AddGroup(
            "Canvas Organization", { 120.0f, -320.0f }, { 840.0f, 760.0f }) != nullptr,
        "no-layout graph fixture should contain a canvas group");

    nlohmann::json payload =
        SerializeGraphPayload(nlohmann::json::array(), graph);
    RemoveGraphLayoutFromPayload(payload);

    const nlohmann::json& graphJson = payload["nodeGraph"];
    Require(!graphJson.contains("groups") &&
            !graphJson.contains("nextGroupId") &&
            !graphJson.contains("selectedNodeId"),
        "no-layout graph info should omit canvas groups and selection layout state");

    const nlohmann::json& nodesJson = graphJson["nodes"];
    const std::vector<int> expectedNodeOrder = {
        sourceAId, sourceBId, mixId, outputId
    };
    Require(nodesJson.size() == expectedNodeOrder.size(),
        "no-layout graph info should preserve every serialized node");
    for (std::size_t index = 0; index < expectedNodeOrder.size(); ++index) {
        Require(nodesJson[index].value("id", -1) == expectedNodeOrder[index],
            "no-layout graph info should preserve node order");
        Require(!nodesJson[index].contains("x") && !nodesJson[index].contains("y"),
            "no-layout graph info should omit every node position");
    }
    Require(std::abs(nodesJson[2].value("mixFactor", 0.0f) - 0.37f) < 1.0e-6f,
        "no-layout graph info should retain functional node state");

    const nlohmann::json& linksJson = graphJson["links"];
    Require(linksJson.size() == 3,
        "no-layout graph info should preserve every connection");
    Require(
        linksJson[0].value("fromNodeId", -1) == sourceBId &&
        linksJson[0].value("toNodeId", -1) == mixId &&
        linksJson[0].value("toSocket", std::string()) == kMixInputBSocketId &&
        linksJson[1].value("fromNodeId", -1) == sourceAId &&
        linksJson[1].value("toNodeId", -1) == mixId &&
        linksJson[1].value("toSocket", std::string()) == kMixInputASocketId &&
        linksJson[2].value("fromNodeId", -1) == mixId &&
        linksJson[2].value("toNodeId", -1) == outputId,
        "no-layout graph info should preserve authored connection order");
}

void TestHdrMergeDeghostModeMediumRoundTrip() {
    using namespace EditorNodeGraph;

    Graph graph;
    HdrMergePayload payload;
    payload.settings.deghostMode = Raw::HdrMergeDeghostMode::Medium;
    const int hdrMergeId = NodeId(graph.AddHdrMergeNode(std::move(payload), { 180.0f, 110.0f }));

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);

    const Node* hdrMergeNode = loaded.FindNode(hdrMergeId);
    Require(hdrMergeNode != nullptr, "HDR merge node should survive graph serialization");
    Require(hdrMergeNode->kind == NodeKind::HdrMerge, "HDR merge node kind should survive graph serialization");
    Require(hdrMergeNode->hdrMerge.settings.deghostMode == Raw::HdrMergeDeghostMode::Medium,
        "HDR merge deghost mode Medium should survive graph serialization");
}

void TestRawDevelopmentRecipeDefaultsAndRoundTrip() {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0001.dng", "IMG_0001.dng");
    Require(recipe.technical.processingVersion == Raw::RawProcessingVersion::TruthfulV2 &&
            recipe.technical.demosaicMethod == Raw::DemosaicMethod::MalvarHeCutler &&
            recipe.technical.workingSpace == Raw::RawWorkingSpace::LinearRec2020D65 &&
            recipe.technical.applyBaselineExposure &&
            recipe.technical.encodeSrgbOutput &&
            recipe.rgbDenoise.enabled &&
            recipe.rgbDenoise.method ==
                Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1 &&
            recipe.rgbDenoise.mapping ==
                Stack::RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1 &&
            std::abs(recipe.rgbDenoise.colorNoise - 0.35f) < 0.001f &&
            std::abs(recipe.rgbDenoise.luminanceNoise - 0.20f) < 0.001f &&
            std::abs(recipe.rgbDenoise.detailProtection - 0.75f) < 0.001f &&
            std::abs(recipe.rgbDenoise.lumaMap.baseMultiplier) < 0.001f &&
            std::abs(recipe.rgbDenoise.chromaMap.baseMultiplier) < 0.001f &&
            !Stack::RawRecipe::IsRgbDenoiseActive(recipe.rgbDenoise),
        "new RAW recipes should opt into the explicit Truthful V2 processing contract");
    recipe.technical.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    recipe.source.relativePathKey = "card/IMG_0001.dng";
    recipe.source.fingerprint = "sample-fingerprint";
    recipe.source.fileSizeBytes = 1234567;
    recipe.source.modifiedTimeTicks = 42;
    recipe.preToneExposureEv = 0.75f;
    recipe.technical.mosaicDenoise.enabled = true;
    recipe.technical.mosaicDenoise.mode =
        Raw::RawMosaicDenoiseMode::DngNoiseProfile;
    recipe.technical.mosaicDenoise.hotPixelSuppression = true;
    recipe.technical.mosaicDenoise.hotPixelThreshold = 0.18f;
    recipe.technical.mosaicDenoise.lumaStrength = 0.42f;
    recipe.technical.mosaicDenoise.chromaStrength = 0.67f;
    recipe.technical.mosaicDenoise.radius = 3;
    recipe.technical.mosaicDenoise.edgeProtection = 0.71f;
    recipe.technical.mosaicDenoise.iterations = 2;
    recipe.rgbDenoise.enabled = true;
    recipe.rgbDenoise.method =
        Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    recipe.rgbDenoise.colorNoise = 0.48f;
    recipe.rgbDenoise.luminanceNoise = 0.27f;
    recipe.rgbDenoise.detailProtection = 0.83f;
    recipe.rgbDenoise.edgeSensitivity = 0.68f;
    recipe.rgbDenoise.maximumStructureSize = 256.0f;
    recipe.rgbDenoise.lumaMap.baseMultiplier = 0.62f;
    recipe.rgbDenoise.lumaMap.points = {
        { 9, 17, true, 0.78f, -3.0f, 0.14f, 1.25f, 0.85f }
    };
    recipe.rgbDenoise.chromaMap.baseMultiplier = 1.0f;
    recipe.rgbDenoise.chromaMap.points = {
        { 10, 17, true, 0.78f, -3.0f, 0.14f, 1.25f, 0.30f }
    };
    recipe.rgbDenoise.nextControlPointId = 11;
    recipe.whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
    recipe.whiteBalance.hasMultipliers = true;
    recipe.whiteBalance.multipliers = { 2.0f, 1.0f, 1.5f };
    recipe.finishTone.layerJson = Stack::RawRecipe::DefaultFinishToneJson();
    Stack::RawRecipe::RawPointCurveComponent finishToneComposite;
    finishToneComposite.points = {
        { 0.0f, 0.0f, 1 },
        { 0.45f, 0.50f, 1 },
        { 1.0f, 1.0f, 1 }
    };
    Stack::RawRecipe::StorePointCurveComponentInFinishToneJson(
        recipe.finishTone.layerJson,
        Stack::RawRecipe::RawPointCurveChannel::Composite,
        finishToneComposite);
    recipe.viewTransform.layerJson = Stack::RawRecipe::DefaultViewTransformJson();
    recipe.viewTransform.layerJson["contrast"] = 1.18f;
    recipe.viewTransform.layerJson["saturation"] = 0.92f;
    recipe.viewTransform.layerJson["enabled"] = false;
    recipe.localRange.enabled = true;
    recipe.localRange.strength = 0.80f;
    recipe.localRange.middleGrey = 0.20f;
    recipe.localRange.minEv = -9.0f;
    recipe.localRange.maxEv = 5.0f;
    recipe.localRange.points = {
        { -9.0f, 1.25f },
        { -1.0f, 0.20f },
        { 4.0f, -0.75f }
    };
    recipe.localRange.smoothness = 0.70f;
    recipe.localRange.edgeProtection = 0.82f;
    recipe.localRange.detailProtection = 0.61f;
    recipe.localRange.highlightProtection = 0.55f;
    recipe.localRange.maskPreviewMode = "delta-map";
    recipe.localRange.regionMaskEnabled = true;
    recipe.localRange.regionMaskMode = "radial-gradient";
    recipe.localRange.regionMaskInvert = true;
    recipe.localRange.regionMaskCenterX = 0.35f;
    recipe.localRange.regionMaskCenterY = 0.62f;
    recipe.localRange.regionMaskAngleDegrees = 23.0f;
    recipe.localRange.regionMaskSize = 0.42f;
    recipe.localRange.regionMaskFeather = 0.27f;
    recipe.localRange.regionMaskLowEv = -5.0f;
    recipe.localRange.regionMaskHighEv = 2.5f;
    recipe.localRange.colorMaskEnabled = true;
    recipe.localRange.colorMaskTargetR = 0.12f;
    recipe.localRange.colorMaskTargetG = 0.74f;
    recipe.localRange.colorMaskTargetB = 0.18f;
    recipe.localRange.colorMaskHueWidth = 0.24f;
    recipe.localRange.colorMaskFeather = 0.31f;
    recipe.localRange.colorMaskMinChroma = 0.10f;
    recipe.localRange.targetZoneCombineMode =
        Stack::RawRecipe::RawLocalRangeZoneCombineMode::Blend;
    Stack::RawRecipe::RawLocalRangeTargetZone targetZone;
    targetZone.id = "zone-test-1";
    targetZone.name = "Court";
    targetZone.centerEv = -2.25f;
    targetZone.coreHalfWidthEv = 0.40f;
    targetZone.featherEv = 0.80f;
    targetZone.deltaEv = 1.15f;
    targetZone.scope = Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
    targetZone.colorEnabled = true;
    const std::array<float, 3> targetUv =
        Stack::RawRecipe::SceneLinearRgbToUvChroma(
            0.12f,
            0.74f,
            0.18f,
            Raw::RawWorkingSpace::LinearRec2020D65);
    targetZone.targetUPrime = targetUv[0];
    targetZone.targetVPrime = targetUv[1];
    targetZone.targetChroma = targetUv[2];
    targetZone.seeds = { { 0.25f, 0.60f }, { 0.72f, 0.44f } };
    recipe.localRange.targetZones.push_back(targetZone);
    recipe.cropRotation.rotationDegrees = 90;
    recipe.cropRotation.flipHorizontally = true;
    recipe.cropRotation.flipVertically = true;

    const Raw::RawDevelopSettings liveSettings =
        Stack::RawRecipe::ToRawDevelopSettings(recipe);
    Require(
        liveSettings.processingVersion == Raw::RawProcessingVersion::TruthfulV2 &&
            liveSettings.demosaicMethod == Raw::DemosaicMethod::MalvarHeCutler &&
            liveSettings.whiteBalanceMode == Raw::WhiteBalanceMode::Manual &&
            liveSettings.manualWhiteBalance ==
                std::array<float, 3>{ 2.0f, 1.0f, 1.5f } &&
            std::abs(liveSettings.exposureStops - recipe.preToneExposureEv) < 0.0001f,
        "RAW Lab technical recipe values should map to the live Truthful V2 RAW settings");
    const std::vector<std::string>& defaultOrder = Stack::RawRecipe::DefaultStageOrder();
    Require(defaultOrder.size() >= 7, "RAW recipe should define a stable default stage order");
    Require(defaultOrder[0] == "source", "RAW recipe stage order should begin with source");
    Require(std::find(defaultOrder.begin(), defaultOrder.end(), "white-balance") != defaultOrder.end(),
        "RAW recipe stage order should include white balance");
    Require(std::find(defaultOrder.begin(), defaultOrder.end(), "pre-tone-exposure") != defaultOrder.end(),
        "RAW recipe stage order should include pre-tone exposure");
    const auto rgbDenoiseStage = std::find(defaultOrder.begin(), defaultOrder.end(), "rgb-denoise");
    const auto exposureStage = std::find(defaultOrder.begin(), defaultOrder.end(), "pre-tone-exposure");
    const auto whiteBalanceStage = std::find(defaultOrder.begin(), defaultOrder.end(), "white-balance");
    Require(
        whiteBalanceStage != defaultOrder.end() &&
            rgbDenoiseStage != defaultOrder.end() &&
            exposureStage != defaultOrder.end() &&
            whiteBalanceStage < rgbDenoiseStage &&
            rgbDenoiseStage < exposureStage,
        "RAW recipe stage order should place RGB denoise after demosaic/WB and before authored exposure");
    const auto localRangeStage = std::find(defaultOrder.begin(), defaultOrder.end(), "local-range");
    const auto toneCurveStage = std::find(defaultOrder.begin(), defaultOrder.end(), "tone-curve");
    Require(localRangeStage != defaultOrder.end() && toneCurveStage != defaultOrder.end() && localRangeStage < toneCurveStage,
        "RAW recipe stage order should place local range before tone curve");
    Require(std::find(defaultOrder.begin(), defaultOrder.end(), "view-transform") != defaultOrder.end(),
        "RAW recipe stage order should include view transform");

    const nlohmann::json serialized = Stack::RawRecipe::SerializeRecipe(recipe);
    Require(serialized.value("rawRecipeVersion", 0) == Stack::RawRecipe::kRawDevelopmentRecipeVersion,
        "RAW recipe should serialize the current compact recipe version");
    Require(serialized["processing"].value("version", std::string()) == "truthful-v2" &&
            serialized["processing"].value("demosaic", std::string()) == "malvar-he-cutler-5x5" &&
            serialized["processing"].value("workingSpace", std::string()) == "linear-rec2020-d65" &&
            serialized["processing"].value("outputTransfer", std::string()) == "srgb",
        "RAW recipe should serialize the complete processing contract");
    Require(
        serialized["processing"]["mosaicDenoise"].value("enabled", false) &&
            serialized["processing"]["mosaicDenoise"].value(
                "mode",
                std::string()) == "dng-noise-profile-v1" &&
            std::abs(serialized["processing"]["mosaicDenoise"].value(
                "greenPlaneStrength",
                0.0f) - 0.42f) < 0.001f,
        "RAW recipe should serialize the versioned pre-demosaic denoise contract");
    Require(
        serialized["rgbDenoise"].value("version", 0) == 5 &&
            serialized["rgbDenoise"].value("enabled", false) &&
            serialized["rgbDenoise"].value("method", std::string()) ==
                "classical-multiscale-v1" &&
            serialized["rgbDenoise"].value("mapping", std::string()) ==
                "scene-linear-safe-v1" &&
            serialized["rgbDenoise"].value("packageId", std::string()) ==
                Stack::RawRecipe::kRestormerDenoisePackageId &&
            serialized["rgbDenoise"].value("adapterVersion", std::string()) ==
                Stack::RawRecipe::kRestormerDenoiseAdapterVersion &&
            std::abs(serialized["rgbDenoise"].value("colorNoise", 0.0f) - 0.48f) <
                0.001f &&
            serialized["rgbDenoise"]["lumaMap"]["points"].size() == 1 &&
            serialized["rgbDenoise"]["chromaMap"]["points"].size() == 1,
        "RAW recipe should serialize the versioned post-demosaic RGB denoise contract");
    Require(serialized.contains("sourceRef"), "RAW recipe should serialize sourceRef");
    Require(serialized.contains("exposureEv"), "RAW recipe should serialize exposureEv");
    Require(!serialized.contains("localExposure") &&
            !serialized.contains("toneCurve"),
        "RAW recipe should not serialize retired tone paths");
    Require(serialized.contains("localRange"), "RAW recipe should serialize localRange");
    Require(serialized.contains("cropRotate"), "RAW recipe should serialize cropRotate");
    Require(
        serialized["cropRotate"].value("flipHorizontally", false) &&
            serialized["cropRotate"].value("flipVertically", false),
        "RAW recipe should serialize horizontal and vertical orientation flips");
    Require(serialized.contains("finishTone"), "RAW recipe should serialize finish tone layer state");
    Require(serialized.contains("viewTransform") &&
            !serialized["viewTransform"].value("enabled", true),
        "RAW recipe should serialize the built-in view transform enable state");
    const Stack::RawRecipe::RawDevelopmentRecipe loaded =
        Stack::RawRecipe::DeserializeRecipe(serialized);
    Require(loaded.rawRecipeVersion == Stack::RawRecipe::kRawDevelopmentRecipeVersion,
        "RAW recipe version should survive serialization");
    Require(loaded.technical.processingVersion == Raw::RawProcessingVersion::TruthfulV2 &&
            loaded.technical.demosaicMethod == Raw::DemosaicMethod::MalvarHeCutler &&
            loaded.viewTransform.layerJson.value("encodeSrgbOutput", false) &&
            !Stack::RawRecipe::IsViewTransformEnabled(loaded),
        "the canonical RAW processing contract and output transfer should survive serialization");
    Require(
        loaded.technical.mosaicDenoise.enabled &&
            loaded.technical.mosaicDenoise.mode ==
                Raw::RawMosaicDenoiseMode::DngNoiseProfile &&
            loaded.technical.mosaicDenoise.hotPixelSuppression &&
            std::abs(loaded.technical.mosaicDenoise.hotPixelThreshold - 0.18f) <
                0.001f &&
            std::abs(loaded.technical.mosaicDenoise.lumaStrength - 0.42f) <
                0.001f &&
            std::abs(loaded.technical.mosaicDenoise.chromaStrength - 0.67f) <
                0.001f &&
            loaded.technical.mosaicDenoise.radius == 3 &&
            std::abs(loaded.technical.mosaicDenoise.edgeProtection - 0.71f) <
                0.001f &&
            loaded.technical.mosaicDenoise.iterations == 2,
        "RAW recipe pre-demosaic denoise settings should survive serialization");
    Require(
        loaded.rgbDenoise.enabled &&
            loaded.rgbDenoise.method ==
                Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1 &&
            std::abs(loaded.rgbDenoise.colorNoise - 0.48f) < 0.001f &&
            std::abs(loaded.rgbDenoise.luminanceNoise - 0.27f) < 0.001f &&
            std::abs(loaded.rgbDenoise.detailProtection - 0.83f) < 0.001f &&
            std::abs(loaded.rgbDenoise.edgeSensitivity - 0.68f) < 0.001f &&
            std::abs(loaded.rgbDenoise.maximumStructureSize - 256.0f) < 0.001f &&
            loaded.rgbDenoise.lumaMap.points.size() == 1 &&
            loaded.rgbDenoise.chromaMap.points.size() == 1 &&
            loaded.rgbDenoise.lumaMap.points[0].linkGroup == 17 &&
            loaded.rgbDenoise.chromaMap.points[0].linkGroup == 17 &&
            loaded.rgbDenoise.nextControlPointId == 11,
        "RAW recipe post-demosaic RGB denoise settings should survive serialization");
    nlohmann::json bypassedRgbDenoiseRecipe = serialized;
    bypassedRgbDenoiseRecipe["rgbDenoise"]["enabled"] = false;
    const Stack::RawRecipe::RawDevelopmentRecipe bypassedRgbDenoise =
        Stack::RawRecipe::DeserializeRecipe(bypassedRgbDenoiseRecipe);
    Require(
        !bypassedRgbDenoise.rgbDenoise.enabled &&
            Stack::RawRecipe::HasRgbDenoiseEffect(
                bypassedRgbDenoise.rgbDenoise) &&
            !Stack::RawRecipe::IsRgbDenoiseActive(
                bypassedRgbDenoise.rgbDenoise) &&
            bypassedRgbDenoise.rgbDenoise.lumaMap.points.size() == 1 &&
            bypassedRgbDenoise.rgbDenoise.chromaMap.points.size() == 1 &&
            !Stack::RawRecipe::SerializeRecipe(bypassedRgbDenoise)
                ["rgbDenoise"].value("enabled", true),
        "RGB denoise bypass should round-trip independently without discarding authored settings");
    Require(
        Stack::RawRecipe::EvaluateRawDenoiseControlMap(
            loaded.rgbDenoise.lumaMap, 0.78f, -3.0f) > 1.46f &&
        std::abs(Stack::RawRecipe::EvaluateRawDenoiseControlMap(
            loaded.rgbDenoise.lumaMap, 0.20f, 3.0f) - 0.62f) < 0.001f,
        "RAW denoise points should have compact local influence in frequency and EV");
    Stack::RawRecipe::RawDenoiseControlMap neutralControlMap;
    neutralControlMap.points.push_back({});
    Require(
        !Stack::RawRecipe::HasRawDenoiseControlMapEffect(neutralControlMap),
        "a zero-base RAW denoise map with neutral points should skip rendering");
    neutralControlMap.points.front().multiplierDelta = 0.5f;
    Require(
        Stack::RawRecipe::HasRawDenoiseControlMapEffect(neutralControlMap),
        "a positive RAW denoise point should activate rendering");
    Stack::RawRecipe::RawDenoiseControlMap borderControlMap;
    borderControlMap.baseMultiplier = 0.5f;
    Stack::RawRecipe::RawDenoiseControlPoint frequencyOnlyPoint;
    frequencyOnlyPoint.id = 1;
    frequencyOnlyPoint.frequency = 0.4f;
    frequencyOnlyPoint.sceneEv = borderControlMap.minimumEv;
    frequencyOnlyPoint.frequencyRadius = 0.2f;
    frequencyOnlyPoint.multiplierDelta = 0.5f;
    borderControlMap.points = {frequencyOnlyPoint};
    Require(
        Stack::RawRecipe::EvaluateRawDenoiseControlMap(
            borderControlMap, 0.4f, borderControlMap.maximumEv) > 0.99f &&
        std::abs(Stack::RawRecipe::EvaluateRawDenoiseControlMap(
            borderControlMap, 0.9f, 0.0f) - 0.5f) < 0.001f,
        "RAW denoise points on an EV border should control frequency across all luminances");
    Stack::RawRecipe::RawDenoiseControlPoint luminanceOnlyPoint;
    luminanceOnlyPoint.id = 2;
    luminanceOnlyPoint.frequency = 0.0f;
    luminanceOnlyPoint.sceneEv = -2.0f;
    luminanceOnlyPoint.luminanceRadiusEv = 1.0f;
    luminanceOnlyPoint.multiplierDelta = 0.5f;
    borderControlMap.points = {luminanceOnlyPoint};
    Require(
        Stack::RawRecipe::EvaluateRawDenoiseControlMap(
            borderControlMap, 1.0f, -2.0f) > 0.99f &&
        std::abs(Stack::RawRecipe::EvaluateRawDenoiseControlMap(
            borderControlMap, 0.5f, 2.0f) - 0.5f) < 0.001f,
        "RAW denoise points on a frequency border should control luminance across all frequencies");
    luminanceOnlyPoint.sceneEv = borderControlMap.minimumEv;
    borderControlMap.points = {luminanceOnlyPoint};
    Require(
        Stack::RawRecipe::EvaluateRawDenoiseControlMap(
            borderControlMap, 1.0f, borderControlMap.maximumEv) > 0.99f,
        "RAW denoise points in a graph corner should control the whole map");
    const std::vector<Stack::RawRecipe::RawDenoiseBand> previewBands =
        Stack::RawRecipe::BuildRawDenoiseBandSchedule(
            1500, 1000, 6000, 4000, 128.0f);
    Require(
        previewBands.size() == 4 &&
            std::abs(previewBands.front().sourceSupportPixels - 16.0f) < 0.001f &&
            std::abs(previewBands.back().sourceSupportPixels - 128.0f) < 0.001f &&
            previewBands.front().normalizedFrequency >
                previewBands.back().normalizedFrequency,
        "RAW denoise bands should stay in source-pixel coordinates across preview scaling");

    nlohmann::json offsetRgbDenoiseRecipe = serialized;
    offsetRgbDenoiseRecipe["rgbDenoise"]["version"] = 3;
    offsetRgbDenoiseRecipe["rgbDenoise"]["enabled"] = true;
    offsetRgbDenoiseRecipe["rgbDenoise"]["lumaMap"]["baseMultiplier"] = 1.0f;
    offsetRgbDenoiseRecipe["rgbDenoise"]["chromaMap"]["baseMultiplier"] = 1.0f;
    offsetRgbDenoiseRecipe["rgbDenoise"]["lumaMap"]["points"][0]["multiplierDelta"] = -4.0f;
    offsetRgbDenoiseRecipe["rgbDenoise"]["chromaMap"]["points"][0]["multiplierDelta"] = 0.7f;
    const Stack::RawRecipe::RawDevelopmentRecipe migratedOffsetDenoise =
        Stack::RawRecipe::DeserializeRecipe(offsetRgbDenoiseRecipe);
    Require(
        std::abs(migratedOffsetDenoise.rgbDenoise.lumaMap.baseMultiplier) < 0.001f &&
            std::abs(migratedOffsetDenoise.rgbDenoise.chromaMap.baseMultiplier) < 0.001f &&
            std::abs(migratedOffsetDenoise.rgbDenoise.lumaMap.points[0].multiplierDelta) < 0.001f &&
            std::abs(migratedOffsetDenoise.rgbDenoise.chromaMap.points[0].multiplierDelta - 0.7f) < 0.001f,
        "version 3 denoise maps should drop the automatic baseline and subtractive points");

    nlohmann::json legacyRgbDenoiseRecipe = serialized;
    legacyRgbDenoiseRecipe["rgbDenoise"].erase("lumaMap");
    legacyRgbDenoiseRecipe["rgbDenoise"].erase("chromaMap");
    legacyRgbDenoiseRecipe["rgbDenoise"]["version"] = 2;
    const Stack::RawRecipe::RawDevelopmentRecipe migratedLegacyDenoise =
        Stack::RawRecipe::DeserializeRecipe(legacyRgbDenoiseRecipe);
    Require(
        std::abs(migratedLegacyDenoise.rgbDenoise.lumaMap.baseMultiplier - 0.27f) < 0.001f &&
            std::abs(migratedLegacyDenoise.rgbDenoise.chromaMap.baseMultiplier - 0.48f) < 0.001f,
        "version 2 RGB denoise sliders should migrate to additive version 5 control maps");

    nlohmann::json neutralVersion4Recipe =
        Stack::RawRecipe::SerializeRecipe(
            Stack::RawRecipe::MakeDefaultRecipe(
                "D:/shoot/card/IMG_0004.dng",
                "IMG_0004.dng"));
    neutralVersion4Recipe["rgbDenoise"]["version"] = 4;
    neutralVersion4Recipe["rgbDenoise"]["enabled"] = false;
    const Stack::RawRecipe::RawDevelopmentRecipe migratedNeutralVersion4 =
        Stack::RawRecipe::DeserializeRecipe(neutralVersion4Recipe);
    Require(
        migratedNeutralVersion4.rgbDenoise.enabled &&
            !Stack::RawRecipe::IsRgbDenoiseActive(
                migratedNeutralVersion4.rgbDenoise),
        "version 4's derived disabled value should migrate to an armed but neutral denoise stage, not a user bypass");

    nlohmann::json restormerRecipe = serialized;
    restormerRecipe["rgbDenoise"]["method"] = "restormer-real-v1";
    restormerRecipe["rgbDenoise"]["mapping"] = "processed-rgb-match-v1";
    restormerRecipe["rgbDenoise"]["packageVersion"] = "1.0.0-dev";
    restormerRecipe["rgbDenoise"]["modelSha256"] =
        std::string(64, 'a');
    const Stack::RawRecipe::RawDevelopmentRecipe loadedRestormer =
        Stack::RawRecipe::DeserializeRecipe(restormerRecipe);
    Require(
        loadedRestormer.rgbDenoise.method ==
            Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1 &&
            loadedRestormer.rgbDenoise.mapping ==
                Stack::RawRecipe::RawRgbDenoiseMapping::ProcessedRgbMatchV1 &&
            loadedRestormer.rgbDenoise.packageVersion == "1.0.0-dev" &&
            loadedRestormer.rgbDenoise.modelSha256 == std::string(64, 'a'),
        "RAW recipes should preserve the exact Restormer model and adapter contract");
    const Raw::RawDevelopSettings truthfulSettings = Stack::RawRecipe::ToRawDevelopSettings(loaded);
    Require(truthfulSettings.processingVersion == Raw::RawProcessingVersion::TruthfulV2 &&
            truthfulSettings.demosaicMethod == Raw::DemosaicMethod::MalvarHeCutler &&
            truthfulSettings.falseColorSuppression == 0.0f &&
            truthfulSettings.defringeStrength == 0.0f &&
            truthfulSettings.highlightEdgeCleanup == 0.0f &&
            truthfulSettings.mosaicDenoise.enabled &&
            truthfulSettings.mosaicDenoise.mode ==
                Raw::RawMosaicDenoiseMode::DngNoiseProfile,
        "the canonical RAW contract should map authored denoise while leaving reconstructive cleanup off");
    Require(loaded.source.sourcePath == recipe.source.sourcePath,
        "RAW recipe source path should survive serialization");
    Require(loaded.source.relativePathKey == recipe.source.relativePathKey,
        "RAW recipe relative source key should survive serialization");
    Require(loaded.source.fileSizeBytes == recipe.source.fileSizeBytes,
        "RAW recipe source file size should survive serialization");
    Require(loaded.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers,
        "RAW recipe white balance mode should survive serialization");
    Require(std::abs(loaded.whiteBalance.multipliers[0] - 2.0f) < 0.001f,
        "RAW recipe white balance multipliers should survive serialization");
    Require(loaded.finishTone.layerJson.value("type", std::string()) == "ToneCurve",
        "RAW recipe finish tone should deserialize as ToneCurve layer state");
    Require(loaded.finishTone.layerJson.value("domain", -1) == 1,
        "RAW recipe finish tone should preserve its current log-EV domain");
    const Stack::RawRecipe::RawPointCurveComponent loadedFinishTone =
        Stack::RawRecipe::PointCurveComponentFromFinishToneJson(
            loaded.finishTone.layerJson,
            Stack::RawRecipe::RawPointCurveChannel::Composite);
    Require(loadedFinishTone.points.size() == 3,
        "RAW recipe finish tone points should survive serialization");
    Require(std::abs(loadedFinishTone.points[1].x - 0.45f) < 0.001f &&
            std::abs(loadedFinishTone.points[1].y - 0.50f) < 0.001f,
        "RAW recipe finish tone point coordinates should survive serialization");
    Require(loaded.viewTransform.layerJson.value("type", std::string()) == "ViewTransform",
        "RAW recipe view transform should deserialize as ViewTransform layer state");
    Require(std::abs(loaded.viewTransform.layerJson.value("contrast", 0.0f) - 1.18f) < 0.001f &&
            std::abs(loaded.viewTransform.layerJson.value("saturation", 0.0f) - 0.92f) < 0.001f,
        "RAW recipe view transform values should survive serialization");
    Require(Stack::RawRecipe::IsLocalRangeEnabled(loaded),
        "RAW recipe local range enabled state should survive serialization");
    Require(std::abs(loaded.localRange.strength - 0.80f) < 0.001f &&
            std::abs(loaded.localRange.middleGrey - 0.20f) < 0.001f,
        "RAW recipe local range scalar settings should survive serialization");
    Require(
        loaded.localRange.targetZoneCombineMode ==
                Stack::RawRecipe::RawLocalRangeZoneCombineMode::Blend &&
            loaded.localRange.targetZones.size() == 1 &&
            loaded.localRange.targetZones[0].id == "zone-test-1" &&
            loaded.localRange.targetZones[0].name == "Court" &&
            loaded.localRange.targetZones[0].seeds.size() == 2 &&
            std::abs(loaded.localRange.targetZones[0].centerEv - -2.25f) < 0.001f &&
            std::abs(loaded.localRange.targetZones[0].deltaEv - 1.15f) < 0.001f,
        "RAW recipe independent target zones should survive serialization");
    Require(loaded.localRange.points.size() == 3 &&
            std::abs(loaded.localRange.points[0].ev - -9.0f) < 0.001f &&
            std::abs(loaded.localRange.points[0].deltaEv - 1.25f) < 0.001f &&
            std::abs(loaded.localRange.points[2].deltaEv - -0.75f) < 0.001f,
        "RAW recipe local range points should survive serialization");
    Require(loaded.localRange.maskPreviewMode == "delta-map",
        "RAW recipe local range mask preview mode should survive serialization");
    Require(loaded.localRange.regionMaskEnabled &&
            loaded.localRange.regionMaskMode == "radial-gradient" &&
            loaded.localRange.regionMaskInvert,
        "RAW recipe local range region mask mode should survive serialization");
    Require(std::abs(loaded.localRange.regionMaskCenterX - 0.35f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskCenterY - 0.62f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskSize - 0.42f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskFeather - 0.27f) < 0.001f,
        "RAW recipe local range region mask geometry should survive serialization");
    Require(std::abs(loaded.localRange.regionMaskLowEv - -5.0f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskHighEv - 2.5f) < 0.001f,
        "RAW recipe local range luminance region mask EV range should survive serialization");
    Require(loaded.localRange.colorMaskEnabled &&
            std::abs(loaded.localRange.colorMaskTargetR - 0.12f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskTargetG - 0.74f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskTargetB - 0.18f) < 0.001f,
        "RAW recipe local range color qualification target should survive serialization");
    Require(std::abs(loaded.localRange.colorMaskHueWidth - 0.24f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskFeather - 0.31f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskMinChroma - 0.10f) < 0.001f,
        "RAW recipe local range color qualification width and guards should survive serialization");
    const Raw::RawDevelopSettings rawSettings = Stack::RawRecipe::ToRawDevelopSettings(loaded);
    Require(std::abs(rawSettings.exposureStops - 0.75f) < 0.001f,
        "RAW recipe pre-tone exposure should map to RAW develop settings");
    Require(rawSettings.whiteBalanceMode == Raw::WhiteBalanceMode::Manual,
        "RAW recipe custom multipliers should map to manual RAW white balance");
    Require(rawSettings.toneCurvePoints.empty(),
        "RAW recipe finish tone should be applied after RAW develop settings");
    Require(rawSettings.rotationDegrees == 90,
        "RAW recipe rotation placeholder should map to RAW develop settings");
    Require(rawSettings.flipHorizontally && rawSettings.flipVertically,
        "RAW recipe orientation flips should map to RAW develop settings");
    Stack::RawRecipe::RawDevelopmentRecipe defaultRecipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0003.dng", "IMG_0003.dng");
    Require(defaultRecipe.finishTone.layerJson.value("type", std::string()) == "ToneCurve" &&
            defaultRecipe.finishTone.layerJson.value("mode", -1) == 1 &&
            defaultRecipe.finishTone.layerJson.value("domain", -1) == 1,
        "RAW recipe defaults should use RGB Log Scene finish tone");
    Require(defaultRecipe.viewTransform.layerJson.value("type", std::string()) == "ViewTransform",
        "RAW recipe defaults should include view transform layer state");
    const Raw::RawDevelopSettings defaultSettings =
        Stack::RawRecipe::ToRawDevelopSettings(defaultRecipe);
    Require(defaultSettings.toneCurvePoints.empty(),
        "RAW recipe default tone curve should remain identity in RAW develop settings");
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(defaultRecipe),
        "RAW recipe local range should be disabled by default");
    Require(!Stack::RawRecipe::IsRgbDenoiseActive(defaultRecipe.rgbDenoise),
        "RAW recipe post-demosaic RGB denoise should be a skipped neutral stage by default");
    Require(defaultRecipe.localRange.points.size() == 3 &&
            std::abs(defaultRecipe.localRange.points.front().ev - -8.0f) < 0.001f &&
            std::abs(defaultRecipe.localRange.points.back().ev - 6.0f) < 0.001f,
        "RAW recipe local range should use identity EV anchor points by default");
    nlohmann::json clampedRgbDenoiseRecipe = serialized;
    clampedRgbDenoiseRecipe["rgbDenoise"]["colorNoise"] = 4.0f;
    clampedRgbDenoiseRecipe["rgbDenoise"]["luminanceNoise"] = -2.0f;
    clampedRgbDenoiseRecipe["rgbDenoise"]["detailProtection"] = 7.0f;
    const Stack::RawRecipe::RawDevelopmentRecipe clampedRgbDenoise =
        Stack::RawRecipe::DeserializeRecipe(clampedRgbDenoiseRecipe);
    Require(
        std::abs(clampedRgbDenoise.rgbDenoise.colorNoise - 1.0f) < 0.001f &&
            std::abs(clampedRgbDenoise.rgbDenoise.luminanceNoise) < 0.001f &&
            std::abs(clampedRgbDenoise.rgbDenoise.detailProtection - 1.0f) < 0.001f,
        "RAW RGB denoise strengths should sanitize to the supported unit interval");
    defaultRecipe.localRange.enabled = true;
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(defaultRecipe),
        "RAW recipe local range should stay neutral when enabled with identity points");
    defaultRecipe.localRange.points[1].deltaEv = 1.0f;
    Require(Stack::RawRecipe::IsLocalRangeEnabled(defaultRecipe),
        "RAW recipe local range should enable when an EV point delta is present");
    Stack::RawRecipe::RawLocalRangeTargetZone directZone;
    directZone.id = "direct-zone";
    directZone.centerEv = -2.0f;
    directZone.coreHalfWidthEv = 0.40f;
    directZone.featherEv = 0.60f;
    directZone.deltaEv = 1.50f;
    directZone.scope = Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches;
    Require(
        Stack::RawRecipe::EvaluateLocalRangeTargetZoneTonalWeight(directZone, -2.0f) > 0.999f &&
            Stack::RawRecipe::EvaluateLocalRangeTargetZoneTonalWeight(directZone, -0.8f) < 0.01f,
        "target-zone tonal qualification should use a full core and smooth EV feather");
    const float directDelta =
        Stack::RawRecipe::EvaluateLocalRangeTargetZoneDeltaEv(
            directZone,
            -2.0f,
            0.12f,
            0.74f,
            0.18f,
            Raw::RawWorkingSpace::LinearRec2020D65);
    Require(
        std::abs(directDelta - 1.50f) < 0.001f,
        "brightness-first target zones should apply their direct EV correction at full qualification");
    directZone.colorEnabled = true;
    directZone.targetUPrime = targetUv[0];
    directZone.targetVPrime = targetUv[1];
    directZone.targetChroma = targetUv[2];
    Require(
        Stack::RawRecipe::EvaluateLocalRangeTargetZoneColorWeight(
            directZone,
            0.12f,
            0.74f,
            0.18f,
            Raw::RawWorkingSpace::LinearRec2020D65) > 0.99f &&
        Stack::RawRecipe::EvaluateLocalRangeTargetZoneColorWeight(
            directZone,
            0.70f,
            0.10f,
            0.12f,
            Raw::RawWorkingSpace::LinearRec2020D65) < 0.10f,
        "target-zone u-prime/v-prime color qualification should accept the sample and reject a distant hue");
    const std::vector<float> overlapDeltas = { 1.0f, -0.50f };
    const std::vector<float> overlapWeights = { 1.0f, 1.0f };
    Require(
        std::abs(Stack::RawRecipe::CombineLocalRangeTargetZoneDeltaEv(
                     Stack::RawRecipe::RawLocalRangeZoneCombineMode::Add,
                     overlapDeltas,
                     overlapWeights) -
                 0.50f) <
                0.001f &&
            std::abs(Stack::RawRecipe::CombineLocalRangeTargetZoneDeltaEv(
                         Stack::RawRecipe::RawLocalRangeZoneCombineMode::Strongest,
                         overlapDeltas,
                         overlapWeights) -
                     1.0f) <
                0.001f &&
            std::abs(Stack::RawRecipe::CombineLocalRangeTargetZoneDeltaEv(
                         Stack::RawRecipe::RawLocalRangeZoneCombineMode::Blend,
                         overlapDeltas,
                         overlapWeights) -
                     0.25f) <
                0.001f,
        "target-zone overlap modes should implement additive, strongest, and weighted blend semantics");
    Require(
        std::abs(Stack::RawLocalRangeTargetInteraction::DragOffsetEv(
                     100.0f,
                     92.0f)) <
                0.0001f &&
            std::abs(Stack::RawLocalRangeTargetInteraction::DragOffsetEv(
                         100.0f,
                         -28.0f) -
                     1.0f) <
                0.0001f &&
            std::abs(Stack::RawLocalRangeTargetInteraction::DragOffsetEv(
                         100.0f,
                         228.0f) +
                     1.0f) <
                0.0001f,
        "target drag mapping should preserve an eight-pixel dead zone and use 120 pixels per EV");
    Require(
        Stack::RawLocalRangeTargetInteraction::ShouldCreateZone(false, false) &&
            !Stack::RawLocalRangeTargetInteraction::ShouldCreateZone(true, false) &&
            Stack::RawLocalRangeTargetInteraction::ShouldCreateZone(true, true),
        "target creation should be implicit only for the first zone and require Ctrl afterward");
    Require(
        Stack::RawLocalRangeTargetInteraction::ShouldPreserveBasePresentation(
            true,
            true,
            true,
            false) &&
            Stack::RawLocalRangeTargetInteraction::
                ShouldPreserveBasePresentation(
                    false,
                    true,
                    true,
                    false) &&
            !Stack::RawLocalRangeTargetInteraction::
                ShouldPreserveBasePresentation(
                    false,
                    true,
                    true,
                    true),
        "target hover work should preserve the photograph, while an active recipe edit must publish its adjusted photograph");
    const std::vector<Stack::RawLocalRangeTargetInteraction::Candidate>
        targetCandidates = {
            { "active", 0, 0.51f, true },
            { "strongest", 1, 0.55f, false }
        };
    Require(
        Stack::RawLocalRangeTargetInteraction::ChooseCandidate(targetCandidates) == 0,
        "target candidate selection should retain the active zone within the hysteresis band");
    const std::vector<Stack::RawLocalRangeTargetInteraction::Candidate>
        strongerTargetCandidates = {
            { "active", 0, 0.30f, true },
            { "strongest", 1, 0.80f, false }
        };
    Require(
        Stack::RawLocalRangeTargetInteraction::ChooseCandidate(
            strongerTargetCandidates) == 1,
        "target candidate selection should switch when another authored mask is clearly stronger");
    Require(
        Stack::RawLocalRangeTargetInteraction::OverlayMatchesPresentation(
            true,
            12,
            10,
            44,
            44) &&
            !Stack::RawLocalRangeTargetInteraction::OverlayMatchesPresentation(
                true,
                12,
                10,
                45,
                44) &&
            Stack::RawLocalRangeTargetInteraction::OverlayMatchesPresentation(
                true,
                11,
                10,
                43,
                44) &&
            Stack::RawLocalRangeTargetInteraction::OverlayMatchesPresentation(
                false,
                10,
                10,
                0,
                0) &&
            !Stack::RawLocalRangeTargetInteraction::OverlayMatchesPresentation(
                false,
                12,
                10,
                0,
                0),
        "target outlines should retain the last accepted transient generation while newer hover work is pending, whereas ordinary overlays follow the base viewport generation");
    const Stack::RawRecipe::RawLocalRangeRecipe openShadowsPreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            Stack::RawRecipe::DefaultLocalRangeRecipe(),
            Stack::RawRecipe::RawLocalRangePreset::OpenShadows);
    Require(Stack::RawRecipe::IsLocalRangeEnabled(openShadowsPreset) &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(openShadowsPreset, -5.0f) > 1.0f &&
            std::abs(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(openShadowsPreset, 0.0f)) < 0.05f,
        "RAW recipe Open Shadows preset should lift shadow EV zones while leaving midtones near identity");
    const Stack::RawRecipe::RawLocalRangeRecipe holdHighlightsPreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            Stack::RawRecipe::DefaultLocalRangeRecipe(),
            Stack::RawRecipe::RawLocalRangePreset::HoldHighlights);
    Require(Stack::RawRecipe::IsLocalRangeEnabled(holdHighlightsPreset) &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(holdHighlightsPreset, 4.0f) < -0.80f &&
            std::abs(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(holdHighlightsPreset, -4.0f)) < 0.05f,
        "RAW recipe Hold Highlights preset should compress highlight EV zones while leaving shadows near identity");
    const Stack::RawRecipe::RawLocalRangeRecipe compressRangePreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            Stack::RawRecipe::DefaultLocalRangeRecipe(),
            Stack::RawRecipe::RawLocalRangePreset::CompressRange);
    Require(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(compressRangePreset, -3.0f) > 0.50f &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(compressRangePreset, 4.0f) < -0.65f,
        "RAW recipe Compress Range preset should lift dark zones and compress bright zones");
    const Stack::RawRecipe::RawLocalRangeRecipe resetPreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            compressRangePreset,
            Stack::RawRecipe::RawLocalRangePreset::Reset);
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(resetPreset),
        "RAW recipe Reset preset should return Local Range to disabled identity");
    Stack::RawRecipe::RawLocalRangeRecipe gradientLocalRange =
        Stack::RawRecipe::DefaultLocalRangeRecipe();
    gradientLocalRange.enabled = true;
    gradientLocalRange.strength = 1.0f;
    gradientLocalRange.points = {
        { -8.0f, 0.0f },
        { -3.0f, 1.0f },
        { 0.0f, 0.0f },
        { 4.0f, -1.0f },
        { 6.0f, 0.0f }
    };
    const float middleGrey = gradientLocalRange.middleGrey;
    const float shadowLuma = middleGrey * std::pow(2.0f, -3.0f);
    const float midtoneLuma = middleGrey;
    const float highlightLuma = middleGrey * std::pow(2.0f, 4.0f);
    Require(std::abs(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(gradientLocalRange, -3.0f) - 1.0f) < 0.001f,
        "RAW recipe local range evaluator should return the requested shadow-zone EV delta");
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, shadowLuma) - 2.0f) < 0.02f,
        "RAW recipe local range should brighten a matching shadow zone by roughly one stop");
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, midtoneLuma) - 1.0f) < 0.02f,
        "RAW recipe local range should keep identity midtones near identity when the curve says zero");
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, highlightLuma) - 0.5f) < 0.02f,
        "RAW recipe local range should compress a matching highlight zone by roughly one stop");
    gradientLocalRange.strength = 0.40f;
    Require(
        std::abs(
            Stack::RawRecipe::EvaluateLocalRangeControlDeltaEv(
                gradientLocalRange,
                -3.0f) -
            1.0f) <
            0.001f &&
            std::abs(
                Stack::RawRecipe::EvaluateLocalRangeDeltaEv(
                    gradientLocalRange,
                    -3.0f) -
                0.40f) <
                0.001f,
        "RAW recipe local range control evaluation should preserve the graph value independently of Strength");
    const float resumedTargetDeltaEv = std::clamp(
        Stack::RawRecipe::EvaluateLocalRangeControlDeltaEv(
            gradientLocalRange,
            -3.0f) +
            0.50f,
        -4.0f,
        4.0f);
    Require(
        std::abs(resumedTargetDeltaEv - 1.50f) < 0.001f,
        "RAW recipe target dragging should be able to continue from the existing graph adjustment");
    gradientLocalRange.strength = 1.0f;
    gradientLocalRange.enabled = false;
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, shadowLuma) - 1.0f) < 0.001f,
        "RAW recipe disabled local range should be indistinguishable from no local range");

    Stack::RawRecipe::RawLocalRangeRecipe edgeAwareLocalRange =
        Stack::RawRecipe::DefaultLocalRangeRecipe();
    edgeAwareLocalRange.enabled = true;
    edgeAwareLocalRange.strength = 1.0f;
    edgeAwareLocalRange.smoothness = 1.0f;
    edgeAwareLocalRange.edgeProtection = 0.92f;
    edgeAwareLocalRange.detailProtection = 0.80f;
    edgeAwareLocalRange.highlightProtection = 0.50f;
    edgeAwareLocalRange.points = {
        { -8.0f, 0.0f },
        { -3.0f, 1.0f },
        { 0.0f, 0.0f },
        { 4.0f, -1.0f },
        { 6.0f, 0.0f }
    };
    const float protectedDarkDelta = Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
        edgeAwareLocalRange,
        -3.0f,
        { -3.2f, -2.9f, -3.1f, 4.0f, 4.2f, 3.8f });
    Stack::RawRecipe::RawLocalRangeRecipe unprotectedEdgeRange = edgeAwareLocalRange;
    unprotectedEdgeRange.edgeProtection = 0.0f;
    const float bleedingDarkDelta = Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
        unprotectedEdgeRange,
        -3.0f,
        { -3.2f, -2.9f, -3.1f, 4.0f, 4.2f, 3.8f });
    Require(protectedDarkDelta > 0.70f && protectedDarkDelta > bleedingDarkDelta + 0.45f,
        "RAW recipe edge-aware local range should let dark regions lift while rejecting bright cross-edge samples");

    Stack::RawRecipe::RawLocalRangeRecipe detailAwareLocalRange =
        Stack::RawRecipe::DefaultLocalRangeRecipe();
    detailAwareLocalRange.enabled = true;
    detailAwareLocalRange.strength = 1.0f;
    detailAwareLocalRange.smoothness = 1.0f;
    detailAwareLocalRange.edgeProtection = 0.70f;
    detailAwareLocalRange.detailProtection = 1.0f;
    detailAwareLocalRange.highlightProtection = 0.50f;
    detailAwareLocalRange.points = {
        { -8.0f, 0.0f },
        { -4.0f, 2.0f },
        { -3.0f, 0.0f },
        { 6.0f, 0.0f }
    };
    const float directTextureDeltaSpread = std::abs(
        Stack::RawRecipe::EvaluateLocalRangeDeltaEv(detailAwareLocalRange, -4.20f) -
        Stack::RawRecipe::EvaluateLocalRangeDeltaEv(detailAwareLocalRange, -3.80f));
    const float edgeAwareTextureDeltaSpread = std::abs(
        Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
            detailAwareLocalRange,
            -4.20f,
            { -4.05f, -3.95f, -4.10f, -3.90f }) -
        Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
            detailAwareLocalRange,
            -3.80f,
            { -4.05f, -3.95f, -4.10f, -3.90f }));
    Require(edgeAwareTextureDeltaSpread < directTextureDeltaSpread * 0.70f,
        "RAW recipe detail-aware local range should reduce per-pixel texture-driven EV variation");

    Stack::RawRecipe::RawLocalRangeRecipe linearMaskRange = gradientLocalRange;
    linearMaskRange.enabled = true;
    linearMaskRange.regionMaskEnabled = true;
    linearMaskRange.regionMaskMode = "linear-gradient";
    linearMaskRange.regionMaskInvert = false;
    linearMaskRange.regionMaskCenterX = 0.5f;
    linearMaskRange.regionMaskCenterY = 0.5f;
    linearMaskRange.regionMaskAngleDegrees = 0.0f;
    linearMaskRange.regionMaskSize = 0.25f;
    linearMaskRange.regionMaskFeather = 0.5f;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(linearMaskRange, 0.10f, 0.5f, -3.0f) < 0.05f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(linearMaskRange, 0.90f, 0.5f, -3.0f) > 0.95f,
        "RAW recipe linear region mask should gate one side of the image with a soft transition");

    Stack::RawRecipe::RawLocalRangeRecipe radialMaskRange = gradientLocalRange;
    radialMaskRange.enabled = true;
    radialMaskRange.regionMaskEnabled = true;
    radialMaskRange.regionMaskMode = "radial-gradient";
    radialMaskRange.regionMaskCenterX = 0.5f;
    radialMaskRange.regionMaskCenterY = 0.5f;
    radialMaskRange.regionMaskSize = 0.25f;
    radialMaskRange.regionMaskFeather = 0.20f;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(radialMaskRange, 0.50f, 0.5f, -3.0f) > 0.95f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(radialMaskRange, 0.95f, 0.95f, -3.0f) < 0.05f,
        "RAW recipe radial region mask should keep the center selected and fade out beyond the radius");

    Stack::RawRecipe::RawLocalRangeRecipe luminanceMaskRange = gradientLocalRange;
    luminanceMaskRange.enabled = true;
    luminanceMaskRange.regionMaskEnabled = true;
    luminanceMaskRange.regionMaskMode = "luminance-range";
    luminanceMaskRange.regionMaskLowEv = -4.0f;
    luminanceMaskRange.regionMaskHighEv = 2.0f;
    luminanceMaskRange.regionMaskFeather = 0.25f;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, -3.0f) > 0.95f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, 5.0f) < 0.05f,
        "RAW recipe luminance range mask should gate by scene EV independently of image position");
    luminanceMaskRange.regionMaskInvert = true;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, -3.0f) < 0.05f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, 5.0f) > 0.95f,
        "RAW recipe local range region mask invert should flip the gate");

    Stack::RawRecipe::RawLocalRangeRecipe colorMaskRange = gradientLocalRange;
    colorMaskRange.enabled = true;
    colorMaskRange.colorMaskEnabled = true;
    colorMaskRange.colorMaskTargetR = 0.12f;
    colorMaskRange.colorMaskTargetG = 0.74f;
    colorMaskRange.colorMaskTargetB = 0.18f;
    colorMaskRange.colorMaskHueWidth = 0.24f;
    colorMaskRange.colorMaskFeather = 0.35f;
    colorMaskRange.colorMaskMinChroma = 0.10f;
    Require(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.10f, 0.72f, 0.16f) > 0.90f,
        "RAW recipe local range color qualifier should accept scene colors near the sampled target");
    Require(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.12f, 0.22f, 0.82f) < 0.20f,
        "RAW recipe local range color qualifier should reject a different saturated hue at similar brightness");
    Require(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.80f, 0.82f, 0.78f) < 0.20f,
        "RAW recipe local range color qualifier should reject low-chroma white or grey regions for colored targets");
    colorMaskRange.colorMaskEnabled = false;
    Require(std::abs(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.12f, 0.22f, 0.82f) - 1.0f) < 0.001f,
        "RAW recipe disabled local range color qualifier should be neutral");

    nlohmann::json clampedLocalRangeRecipe = serialized;
    clampedLocalRangeRecipe["localRange"]["strength"] = 2.0f;
    clampedLocalRangeRecipe["localRange"]["middleGrey"] = 5.0f;
    clampedLocalRangeRecipe["localRange"]["minEv"] = -40.0f;
    clampedLocalRangeRecipe["localRange"]["maxEv"] = 40.0f;
    clampedLocalRangeRecipe["localRange"]["points"] = nlohmann::json::array({
        { { "ev", -99.0f }, { "deltaEv", 9.0f } },
        { { "ev", 99.0f }, { "deltaEv", -9.0f } }
    });
    clampedLocalRangeRecipe["localRange"]["maskPreviewMode"] = "not-a-mode";
    clampedLocalRangeRecipe["localRange"]["regionMaskMode"] = "not-a-mask";
    clampedLocalRangeRecipe["localRange"]["regionMaskCenterX"] = -3.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskCenterY"] = 4.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskAngleDegrees"] = 420.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskSize"] = -1.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskFeather"] = 9.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskLowEv"] = 14.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskHighEv"] = 12.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskTargetR"] = -4.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskTargetG"] = 44.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskTargetB"] = 128.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskHueWidth"] = 9.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskFeather"] = -3.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskMinChroma"] = 2.0f;
    const Stack::RawRecipe::RawDevelopmentRecipe clampedLocalRange =
        Stack::RawRecipe::DeserializeRecipe(clampedLocalRangeRecipe);
    Require(std::abs(clampedLocalRange.localRange.strength - 1.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.middleGrey - 1.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.minEv - -16.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.maxEv - 16.0f) < 0.001f,
        "RAW recipe local range scalar fields should clamp to supported ranges");
    Require(clampedLocalRange.localRange.points.size() == 2 &&
            std::abs(clampedLocalRange.localRange.points[0].ev - -16.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.points[0].deltaEv - 4.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.points[1].ev - 16.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.points[1].deltaEv - -4.0f) < 0.001f,
        "RAW recipe local range points should clamp and sort by scene EV");
    Require(clampedLocalRange.localRange.maskPreviewMode == "none",
        "RAW recipe local range should reject unknown mask preview modes");
    Require(clampedLocalRange.localRange.regionMaskMode == "linear-gradient" &&
            std::abs(clampedLocalRange.localRange.regionMaskCenterX - 0.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.regionMaskCenterY - 1.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.regionMaskAngleDegrees - 180.0f) < 0.001f,
        "RAW recipe local range region mask geometry should clamp to supported ranges");
    Require(std::abs(clampedLocalRange.localRange.regionMaskSize - 0.02f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.regionMaskFeather - 1.0f) < 0.001f &&
            clampedLocalRange.localRange.regionMaskHighEv > clampedLocalRange.localRange.regionMaskLowEv,
        "RAW recipe local range region mask feather, size, and EV range should sanitize");
    Require(std::abs(clampedLocalRange.localRange.colorMaskTargetR - 0.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskTargetG - 32.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskTargetB - 32.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskHueWidth - 1.20f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskFeather - 0.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskMinChroma - 1.0f) < 0.001f,
        "RAW recipe local range color qualification values should clamp to supported ranges");

    Stack::RawRecipe::RawDevelopmentRecipe finishChanged = loaded;
    finishChanged.finishTone.layerJson["domain"] = 0;
    Require(!Stack::RawRecipe::FinishStateEquals(loaded, finishChanged),
        "RAW recipe finish-state comparison should detect finish tone changes");
    Require(Stack::RawRecipe::FinishStateHash(loaded) != Stack::RawRecipe::FinishStateHash(finishChanged),
        "RAW recipe finish-state hash should detect finish tone changes");
    Stack::RawRecipe::RawDevelopmentRecipe viewChanged = loaded;
    viewChanged.viewTransform.layerJson["contrast"] = 1.40f;
    Require(!Stack::RawRecipe::FinishStateEquals(loaded, viewChanged),
        "RAW recipe finish-state comparison should detect view transform changes");
    Stack::RawRecipe::RawDevelopmentRecipe localRangeChanged = loaded;
    localRangeChanged.localRange.points[1].deltaEv = 1.50f;
    Require(!Stack::RawRecipe::LocalRangeStateEquals(loaded, localRangeChanged),
        "RAW recipe local-range comparison should detect point changes");
    Require(Stack::RawRecipe::LocalRangeStateHash(loaded) != Stack::RawRecipe::LocalRangeStateHash(localRangeChanged),
        "RAW recipe local-range hash should detect point changes");
    Require(Stack::RawRecipe::SerializeRecipe(loaded).dump() != Stack::RawRecipe::SerializeRecipe(localRangeChanged).dump(),
        "RAW Development render identity should change when enabled local range points change");
    Stack::RawRecipe::RawDevelopmentRecipe localRangeMaskChanged = loaded;
    localRangeMaskChanged.localRange.regionMaskCenterX = 0.75f;
    Require(!Stack::RawRecipe::LocalRangeStateEquals(loaded, localRangeMaskChanged),
        "RAW recipe local-range comparison should detect region mask changes");
    Require(Stack::RawRecipe::LocalRangeStateHash(loaded) != Stack::RawRecipe::LocalRangeStateHash(localRangeMaskChanged),
        "RAW recipe local-range hash should detect region mask changes");
    Require(Stack::RawRecipe::SerializeRecipe(loaded).dump() != Stack::RawRecipe::SerializeRecipe(localRangeMaskChanged).dump(),
        "RAW Development render identity should change when local range region mask changes");
    Stack::RawRecipe::RawDevelopmentRecipe localRangeColorChanged = loaded;
    localRangeColorChanged.localRange.colorMaskTargetG = 0.52f;
    Require(!Stack::RawRecipe::LocalRangeStateEquals(loaded, localRangeColorChanged),
        "RAW recipe local-range comparison should detect color qualification changes");
    Require(Stack::RawRecipe::LocalRangeStateHash(loaded) != Stack::RawRecipe::LocalRangeStateHash(localRangeColorChanged),
        "RAW recipe local-range hash should detect color qualification changes");
    Require(Stack::RawRecipe::SerializeRecipe(loaded).dump() != Stack::RawRecipe::SerializeRecipe(localRangeColorChanged).dump(),
        "RAW Development render identity should change when local range color qualification changes");
    Stack::RawRecipe::RawDevelopmentRecipe localRangeTargetZoneChanged = loaded;
    localRangeTargetZoneChanged.localRange.targetZones[0].deltaEv += 0.25f;
    Require(
        !Stack::RawRecipe::LocalRangeStateEquals(loaded, localRangeTargetZoneChanged) &&
            Stack::RawRecipe::LocalRangeStateHash(loaded) !=
                Stack::RawRecipe::LocalRangeStateHash(localRangeTargetZoneChanged),
        "RAW recipe local-range identity should include independent target-zone edits");

    nlohmann::json clampedDenoiseRecipe = serialized;
    clampedDenoiseRecipe["processing"]["mosaicDenoise"]["hotPixelThreshold"] =
        4.0f;
    clampedDenoiseRecipe["processing"]["mosaicDenoise"]["greenPlaneStrength"] =
        -2.0f;
    clampedDenoiseRecipe["processing"]["mosaicDenoise"]["redBluePlaneStrength"] =
        8.0f;
    clampedDenoiseRecipe["processing"]["mosaicDenoise"]["radius"] = 99;
    clampedDenoiseRecipe["processing"]["mosaicDenoise"]["edgeProtection"] =
        -3.0f;
    clampedDenoiseRecipe["processing"]["mosaicDenoise"]["iterations"] = 8;
    const Stack::RawRecipe::RawDevelopmentRecipe clampedDenoiseLoaded =
        Stack::RawRecipe::DeserializeRecipe(clampedDenoiseRecipe);
    Require(
        std::abs(
            clampedDenoiseLoaded.technical.mosaicDenoise.hotPixelThreshold -
            1.0f) < 0.001f &&
            std::abs(
                clampedDenoiseLoaded.technical.mosaicDenoise.lumaStrength) <
                0.001f &&
            std::abs(
                clampedDenoiseLoaded.technical.mosaicDenoise.chromaStrength -
                1.0f) < 0.001f &&
            clampedDenoiseLoaded.technical.mosaicDenoise.radius == 4 &&
            std::abs(
                clampedDenoiseLoaded.technical.mosaicDenoise.edgeProtection) <
                0.001f &&
            clampedDenoiseLoaded.technical.mosaicDenoise.iterations == 2,
        "RAW recipe denoise values should clamp to the supported processing range");

}

void TestRawGpuMemoryBudgetPolicy() {
    constexpr std::uint64_t mib = 1024ull * 1024ull;
    constexpr std::uint64_t gib = 1024ull * mib;
    const Raw::RawGpuMemoryBudgetDecision fallback =
        Raw::ResolveRawGpuMemoryBudget({});
    Require(fallback.usedFallback &&
            fallback.workingBudgetBytes ==
                std::numeric_limits<std::uint64_t>::max(),
        "an unavailable VRAM query should permit a native attempt instead of imposing an arbitrary fallback cap");

    Raw::RawGpuMemoryBudgetInput normalInput;
    normalInput.queryAvailable = true;
    normalInput.localBudgetBytes = 8ull * gib;
    normalInput.localUsageBytes = 2ull * gib;
    const Raw::RawGpuMemoryBudgetDecision normal =
        Raw::ResolveRawGpuMemoryBudget(normalInput);
    Require(normal.reserveBytes == 512ull * mib &&
            normal.workingBudgetBytes == 6ull * gib - 512ull * mib &&
            !normal.forceMinimumMemoryTiling,
        "RAW GPU budgeting should expose current local headroom after a modest reserve without an arbitrary percentage cap");

    Raw::RawGpuMemoryBudgetInput largeDeviceInput;
    largeDeviceInput.queryAvailable = true;
    largeDeviceInput.localBudgetBytes = 12ull * gib;
    largeDeviceInput.localUsageBytes = 2ull * gib;
    const Raw::RawGpuMemoryBudgetDecision largeDevice =
        Raw::ResolveRawGpuMemoryBudget(largeDeviceInput);
    Require(
        largeDevice.reserveBytes == 12ull * gib / 20ull &&
            largeDevice.workingBudgetBytes ==
                10ull * gib - largeDevice.reserveBytes &&
            largeDevice.workingBudgetBytes > 3ull * gib,
        "a high-headroom GPU should not be constrained by the former hard 3 GiB ceiling");

    Raw::RawGpuMemoryBudgetInput pressuredInput;
    pressuredInput.queryAvailable = true;
    pressuredInput.localBudgetBytes = 4ull * gib;
    pressuredInput.localUsageBytes = 3900ull * mib;
    const Raw::RawGpuMemoryBudgetDecision pressured =
        Raw::ResolveRawGpuMemoryBudget(pressuredInput);
    Require(pressured.availableBytes < 256ull * mib &&
            pressured.forceMinimumMemoryTiling &&
            pressured.workingBudgetBytes == 0u,
        "RAW GPU budgeting should force minimum-memory tiling below 256 MiB of local headroom");

    const Raw::RawFullFramePreviewDecision sonyFullFrame =
        Raw::ResolveRawFullFramePreviewDecision(
            6000,
            4000,
            16384,
            normal.workingBudgetBytes,
            false);
    Require(sonyFullFrame.allowed &&
            sonyFullFrame.estimatedWorkingSetBytes ==
                6000ull * 4000ull * 8ull * 7ull,
        "a 24 MP full-frame RAW preview should use the monolithic settled path when its protected RGBA16F working set fits");

    const Raw::RawFullFramePreviewDecision sixtyOneMegapixel =
        Raw::ResolveRawFullFramePreviewDecision(
            9568,
            6376,
            16384,
            largeDevice.workingBudgetBytes,
            false);
    Require(sixtyOneMegapixel.allowed &&
            sixtyOneMegapixel.estimatedWorkingSetBytes > 3ull * gib,
        "a roughly 61 MP native preview should be allowed when actual device headroom can hold it");

    const Raw::RawFullFramePreviewDecision fallbackSony =
        Raw::ResolveRawFullFramePreviewDecision(
            6000,
            4000,
            16384,
            fallback.workingBudgetBytes,
            false);
    Require(fallbackSony.allowed &&
            fallbackSony.fitsTextureLimits &&
            fallbackSony.fitsWorkingBudget,
        "a settled RAW preview should attempt native resolution when the device budget cannot be queried");

    const Raw::RawFullFramePreviewDecision oversizedTexture =
        Raw::ResolveRawFullFramePreviewDecision(
            18000,
            12000,
            16384,
            3ull * gib,
            false);
    Require(!oversizedTexture.allowed &&
            !oversizedTexture.fitsTextureLimits,
        "a RAW full-frame refinement must not exceed the GL texture limit while native tiling remains deferred");

    const Raw::RawFullFramePreviewDecision minimumMemory =
        Raw::ResolveRawFullFramePreviewDecision(
            4000,
            3000,
            16384,
            3ull * gib,
            true);
    Require(!minimumMemory.allowed,
        "minimum-memory mode should not attempt a monolithic settled RAW refinement");
}

void TestRawRenderPlanningContract() {
    using namespace Stack::EditorRendering;
    constexpr std::uint64_t mib = 1024ull * 1024ull;
    constexpr std::uint64_t gib = 1024ull * mib;

    RawRenderPlanInput interactiveInput;
    interactiveInput.interactiveTargetEdge = 1280;
    interactiveInput.fullFrameWidth = 6000;
    interactiveInput.fullFrameHeight = 4000;
    interactiveInput.maximumTextureSize = 16384;
    interactiveInput.workingBudgetBytes = 768ull * mib;
    const RawRenderPlan interactive =
        BuildRawRenderPlan(interactiveInput);
    Require(
        interactive.purpose ==
            RawRenderPurpose::InteractivePresentation &&
        interactive.targetEdge == 1280,
        "ordinary RAW edits should produce one bounded interactive presentation plan");

    RawRenderPlanInput deniedInspectionInput = interactiveInput;
    deniedInspectionInput.explicitInspectionRequested = true;
    const RawRenderPlan deniedInspection =
        BuildRawRenderPlan(deniedInspectionInput);
    Require(
        deniedInspection.purpose ==
            RawRenderPurpose::ExplicitInspection &&
        !deniedInspection.nativePresentationAllowed &&
        deniedInspection.targetEdge == 1280,
        "inspection should retain the bounded proxy when native presentation exceeds the shared VRAM budget");

    RawRenderPlanInput diagnosticInput = interactiveInput;
    diagnosticInput.targetAuxiliary = true;
    diagnosticInput.fullResolutionDiagnosticRequested = true;
    const RawRenderPlan diagnosticPlan =
        BuildRawRenderPlan(diagnosticInput);
    Require(
        diagnosticPlan.purpose == RawRenderPurpose::ExplicitInspection &&
        !diagnosticPlan.nativePresentationAllowed &&
        diagnosticPlan.targetEdge == 0 &&
        diagnosticPlan.disableViewportTiling,
        "denoise diagnostics should attempt a native untiled render instead of falling back to a proxy");

    RawRenderPlanInput proxyDiagnosticInput = interactiveInput;
    proxyDiagnosticInput.targetAuxiliary = true;
    proxyDiagnosticInput.fullResolutionPreviewRequested = true;
    proxyDiagnosticInput.proxyResolutionDiagnosticRequested = true;
    const RawRenderPlan proxyDiagnosticPlan =
        BuildRawRenderPlan(proxyDiagnosticInput);
    Require(
        proxyDiagnosticPlan.purpose ==
            RawRenderPurpose::InteractivePresentation &&
        proxyDiagnosticPlan.targetEdge == interactiveInput.interactiveTargetEdge &&
        !proxyDiagnosticPlan.fullFrameRequested &&
        !proxyDiagnosticPlan.disableViewportTiling,
        "preview-quality denoise diagnostics should use the image proxy's exact render dimensions");

    RawRenderPlanInput allowedRefinementInput = interactiveInput;
    allowedRefinementInput.fullResolutionPreviewRequested = true;
    allowedRefinementInput.workingBudgetBytes = 3ull * gib;
    const RawRenderPlan allowedRefinement =
        BuildRawRenderPlan(allowedRefinementInput);
    Require(
        allowedRefinement.purpose ==
            RawRenderPurpose::ViewportRefinement &&
        allowedRefinement.nativePresentationAllowed &&
        allowedRefinement.targetEdge == 0 &&
        allowedRefinement.disableViewportTiling,
        "a native settled preview should be planned only after one shared texture and memory decision allows it");
    Require(
        allowedRefinement.cacheBudgetBytes >
            6000ull * 4000ull * 8ull,
        "native refinement should retain headroom for at least one native RAW stage boundary after protecting its active working set");
    Require(
        allowedRefinement.minimumRawStageCacheBytes ==
            6000ull * 4000ull * 8ull,
        "native refinement should explicitly reserve one RGBA16F RAW boundary instead of relying on a percentage cache partition");

    Stack::RawRecipe::RawDevelopmentRecipe cfaRecipe =
        Stack::RawRecipe::MakeDefaultRecipe("cache.arw", "Cache");
    cfaRecipe.technical.mosaicDenoise.enabled = true;
    cfaRecipe.technical.mosaicDenoise.radius = 4;
    cfaRecipe.technical.mosaicDenoise.iterations = 2;
    const auto beforeDownstreamEdit =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(
            cfaRecipe, 0);
    cfaRecipe.localRange.enabled = true;
    cfaRecipe.localRange.points[1].deltaEv = 1.0f;
    const auto afterDownstreamEdit =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(
            cfaRecipe, 0);
    Require(
        beforeDownstreamEdit.rawBase == afterDownstreamEdit.rawBase &&
            beforeDownstreamEdit.postLocalRange !=
                afterDownstreamEdit.postLocalRange,
        "a downstream Local Range edit must preserve the native CFA-denoised RawBase fingerprint while invalidating its own boundary");

    RawRenderPlanInput exportInput = interactiveInput;
    exportInput.exportRequested = true;
    exportInput.explicitInspectionRequested = true;
    exportInput.analysisRequested = true;
    const RawRenderPlan exportPlan = BuildRawRenderPlan(exportInput);
    Require(
        exportPlan.purpose == RawRenderPurpose::ExplicitExport &&
        exportPlan.targetEdge == 0,
        "authoritative export should retain purpose precedence and its separate tiled native path");

    constexpr std::size_t revision40 = 400;
    constexpr std::size_t revision41 = 410;
    constexpr std::size_t revision43 = 430;
    std::uint64_t displayedGestureGeneration = 39;
    for (std::uint64_t completedGeneration = 40;
         completedGeneration < 43;
         ++completedGeneration) {
        Require(
            ShouldAdoptRawPresentation(
                completedGeneration,
                static_cast<std::size_t>(completedGeneration * 10),
                43,
                revision43,
                displayedGestureGeneration,
                true),
            "each newly completed gesture frame should advance the visible RAW presentation monotonically");
        displayedGestureGeneration = completedGeneration;
    }
    Require(
        !ShouldAdoptRawPresentation(
            40, revision40, 41, revision41, 39) &&
        ShouldAdoptRawPresentation(
            40, revision40, 41, revision41, 39, true) &&
        !ShouldAdoptRawPresentation(
            40, revision40, 41, revision41, 40, true) &&
        !ShouldAdoptRawPresentation(
            41, revision41, 41, revision41, 41) &&
        ShouldAdoptRawPresentation(
            41, revision41, 41, revision41, 40) &&
        !ShouldAdoptRawPresentation(
            41, revision40, 41, revision41, 40) &&
        ShouldAdoptRawPresentation(
            41, revision41, 41, revision41, 41, false, true),
        "RAW presentation adoption should show monotonic completed gesture frames while requiring the exact latest revision when settled");
}

void TestRawRenderGraphOverlayContract() {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("overlay.dng", "Overlay");
    recipe.preToneExposureEv = 1.25f;
    recipe.finishTone.layerJson =
        Stack::RawRecipe::DefaultFinishToneJson();
    recipe.finishTone.layerJson["testMarker"] = "tone-overlay";
    recipe.viewTransform.layerJson =
        Stack::RawRecipe::DefaultViewTransformJson();
    recipe.viewTransform.layerJson["testMarker"] = "view-overlay";

    RenderGraphSnapshot graph;
    RenderGraphNode compact;
    compact.nodeId = 10;
    compact.kind = RenderGraphNodeKind::RawDevelopment;
    graph.nodes.push_back(compact);
    RenderGraphNode decode;
    decode.nodeId = 11;
    decode.kind = RenderGraphNodeKind::RawDecode;
    graph.nodes.push_back(decode);
    RenderGraphNode tone;
    tone.nodeId = 12;
    tone.kind = RenderGraphNodeKind::Layer;
    graph.nodes.push_back(tone);
    RenderGraphNode view;
    view.nodeId = 13;
    view.kind = RenderGraphNodeKind::Layer;
    graph.nodes.push_back(view);
    RenderGraphNode unrelated;
    unrelated.nodeId = 14;
    unrelated.kind = RenderGraphNodeKind::Layer;
    unrelated.layerJson = { { "testMarker", "unchanged" } };
    graph.nodes.push_back(unrelated);

    Stack::EditorRendering::ApplyRawRecipeOverlayToRenderGraph(
        graph,
        recipe,
        11,
        12,
        13);
    Require(
        graph.nodes[0].rawDevelopment.recipe.preToneExposureEv == 1.25f &&
        graph.nodes[1].rawDecode.settings.exposureStops == 1.25f &&
        graph.nodes[2].layerJson.value("testMarker", std::string()) ==
            "tone-overlay" &&
        graph.nodes[3].layerJson.value("testMarker", std::string()) ==
            "view-overlay" &&
        graph.nodes[4].layerJson.value("testMarker", std::string()) ==
            "unchanged",
        "compact and managed RAW graphs should receive one transient render overlay without mutating unrelated nodes");
}

void TestRawNeutralComparisonRecipeIsTransientAndGeometryStable() {
    namespace RawRecipe = Stack::RawRecipe;
    RawRecipe::RawDevelopmentRecipe current =
        RawRecipe::MakeDefaultRecipe("neutral-source.dng", "Neutral Source");
    current.technical.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    current.technical.workingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
    current.technical.mosaicDenoise.enabled = true;
    current.whiteBalance.mode = RawRecipe::WhiteBalanceMode::CustomMultipliers;
    current.whiteBalance.hasMultipliers = true;
    current.whiteBalance.multipliers = { 1.8f, 1.0f, 1.35f };
    current.rgbDenoise.enabled = true;
    current.rgbDenoise.lumaMap.baseMultiplier = 0.8f;
    current.preToneExposureEv = 1.25f;
    current.localRange.enabled = true;
    current.colorWarp.enabled = true;
    current.colorWarp.pins.push_back({});
    current.cropRotation.cropEnabled = true;
    current.cropRotation.cropX = 0.12f;
    current.cropRotation.cropY = 0.08f;
    current.cropRotation.cropWidth = 0.73f;
    current.cropRotation.cropHeight = 0.81f;
    current.cropRotation.rotationDegrees = 270;
    current.cropRotation.flipHorizontally = true;
    current.viewTransform.layerJson["enabled"] = false;
    current.viewTransform.layerJson["creative-sentinel"] = 17;
    const nlohmann::json authoredBefore =
        RawRecipe::SerializeRecipe(current);
    const RawRecipe::RawDevelopmentRecipe neutral =
        RawRecipe::BuildNeutralComparisonRecipe(current);
    Require(RawRecipe::SerializeRecipe(current) == authoredBefore,
        "building Neutral Compare must not mutate the authored RAW recipe");
    Require(neutral.source.sourcePath == current.source.sourcePath &&
            neutral.cropRotation.cropEnabled == current.cropRotation.cropEnabled &&
            neutral.cropRotation.cropX == current.cropRotation.cropX &&
            neutral.cropRotation.cropY == current.cropRotation.cropY &&
            neutral.cropRotation.cropWidth == current.cropRotation.cropWidth &&
            neutral.cropRotation.cropHeight == current.cropRotation.cropHeight &&
            neutral.cropRotation.rotationDegrees ==
                current.cropRotation.rotationDegrees &&
            neutral.cropRotation.flipHorizontally,
        "Neutral Compare should retain the current source, crop, rotation, and flips");
    Require(neutral.technical.processingVersion ==
                current.technical.processingVersion &&
            neutral.technical.workingSpace == current.technical.workingSpace &&
            !neutral.technical.mosaicDenoise.enabled &&
            neutral.whiteBalance.mode == RawRecipe::WhiteBalanceMode::AsShot &&
            neutral.rgbDenoise.enabled &&
            !RawRecipe::IsRgbDenoiseActive(neutral.rgbDenoise) &&
            neutral.preToneExposureEv == 0.0f &&
            !neutral.localRange.enabled &&
            !neutral.colorWarp.enabled,
        "Neutral Compare should retain the canonical processing contract while resetting authored development controls");
    Require(!neutral.viewTransform.layerJson.value("enabled", true) &&
            !neutral.viewTransform.layerJson.contains("creative-sentinel"),
        "Neutral Compare should retain View placement without retaining authored View settings");
}

void TestRawFinishTonePointCurveSetContract() {
    namespace RawRecipe = Stack::RawRecipe;

    const RawRecipe::RawDevelopmentRecipe blankRecipe;
    const nlohmann::json blankSerialized =
        RawRecipe::SerializeRecipe(blankRecipe);
    const RawRecipe::RawDevelopmentRecipe blankRoundTrip =
        RawRecipe::DeserializeRecipe(blankSerialized);
    Require(blankSerialized["viewTransform"].is_object() &&
            blankSerialized["viewTransform"].value("enabled", false) &&
            RawRecipe::IsViewTransformEnabled(blankRoundTrip) &&
            !blankRoundTrip.stageOrder.empty(),
        "a default-constructed RAW recipe should serialize safely into a usable canonical recipe");

    const nlohmann::json defaults = RawRecipe::DefaultFinishToneJson();
    Require(defaults.value("pointCurveSetVersion", 0) == 2 &&
            defaults.contains("pointCurves") &&
            defaults["pointCurves"].is_object(),
        "finish tone defaults should own the Bézier point-curve set");
    for (const char* key : { "composite", "red", "green", "blue" }) {
        Require(defaults["pointCurves"][key].value(
                    "interpolation", std::string()) == "bezier-segments-v1" &&
                defaults["pointCurves"][key]["points"].empty(),
            "canonical identity curves should serialize with no authored points");
    }
    const RawRecipe::RawPointCurveSet defaultSet =
        RawRecipe::PointCurveSetFromFinishToneJson(defaults);
    for (const RawRecipe::RawPointCurveComponent& component : defaultSet.curves) {
        Require(component.points.size() == 2 &&
                RawRecipe::IsIdentityRawPointCurveComponent(component),
            "all four schema-13 point curves should default to two-point identity");
    }

    RawRecipe::RawPointCurveComponent shaped;
    shaped.points = {
        { 0.0f, 0.0f, 1 },
        { 0.45f, 0.80f, 1 },
        { 1.0f, 1.0f, 1 }
    };
    Require(std::abs(
                RawRecipe::EvaluateRawPointCurveComponent(shaped, 0.225f) -
                0.40f) < 0.0002f,
        "new curve segments should begin with straight interpolation");
    shaped.points[0].outgoing.strength = 1.0f;
    shaped.points[1].incoming.strength = 1.0f;
    Require(std::abs(
                RawRecipe::EvaluateRawPointCurveComponent(shaped, 0.225f) -
                0.40f) > 0.001f,
        "100-percent side strength should use the automatically smoothed handles");

    std::vector<RawRecipe::RawBezierCurvePoint> tangentPoints =
        RawRecipe::RawPointCurveBezierPoints(shaped);
    tangentPoints[1].outgoing.strength = 1.5f;
    const RawRecipe::RawBezierSegment tangentSegment =
        RawRecipe::BuildRawBezierSegment(tangentPoints, 1u);
    const float handleDx =
        tangentSegment.leftHandleX - tangentSegment.left.x;
    const float handleDy =
        tangentSegment.leftHandleY - tangentSegment.left.y;
    const float neighborDx = tangentPoints[1].x - tangentPoints[0].x;
    const float neighborDy = tangentPoints[1].y - tangentPoints[0].y;
    Require(std::abs(handleDx * neighborDy - handleDy * neighborDx) < 0.0002f,
        "150-percent left strength should exactly align to the opposite neighboring tangent");

    tangentPoints[1].outgoing.manual = true;
    tangentPoints[1].outgoing.strength = 1.0f;
    tangentPoints[1].outgoing.offsetX = 0.48f;
    tangentPoints[1].outgoing.offsetY = -0.25f;
    tangentPoints[2].incoming.manual = true;
    tangentPoints[2].incoming.strength = 1.0f;
    tangentPoints[2].incoming.offsetX = -0.48f;
    tangentPoints[2].incoming.offsetY = 0.15f;
    const RawRecipe::RawBezierSegment constrainedSegment =
        RawRecipe::BuildRawBezierSegment(tangentPoints, 1u);
    Require(constrainedSegment.left.x <= constrainedSegment.leftHandleX &&
            constrainedSegment.leftHandleX <= constrainedSegment.rightHandleX &&
            constrainedSegment.rightHandleX <= constrainedSegment.right.x,
        "manual Bézier controls must preserve forward input progression");

    RawRecipe::RawDevelopmentRecipe localHandleRecipe =
        RawRecipe::MakeDefaultRecipe("curve-handles.dng", "curve-handles.dng");
    localHandleRecipe.localRange.points[0].outgoing.strength = 1.2f;
    localHandleRecipe.localRange.points[0].outgoing.manual = true;
    localHandleRecipe.localRange.points[0].outgoing.offsetX = 0.18f;
    localHandleRecipe.localRange.points[0].outgoing.offsetY = 0.11f;
    const RawRecipe::RawDevelopmentRecipe localHandleRoundTrip =
        RawRecipe::DeserializeRecipe(
            RawRecipe::SerializeRecipe(localHandleRecipe));
    const RawRecipe::RawBezierHandleState& storedHandle =
        localHandleRoundTrip.localRange.points[0].outgoing;
    Require(storedHandle.manual &&
            std::abs(storedHandle.strength - 1.2f) < 0.0001f &&
            std::abs(storedHandle.offsetX - 0.18f) < 0.0001f &&
            std::abs(storedHandle.offsetY - 0.11f) < 0.0001f,
        "Local Range manual handles and strengths should round-trip through schema 18");

    nlohmann::json malformed = defaults;
    malformed["unknownFinishField"] = nlohmann::json{{ "keep", 73 }};
    malformed["pointCurves"]["red"] = {
        { "interpolation", "not-supported" },
        { "points", nlohmann::json::array() }
    };
    for (int index = 0; index < 20; ++index) {
        malformed["pointCurves"]["red"]["points"].push_back({
            { "x", index == 4 ? nlohmann::json(nullptr) : nlohmann::json(0.05f * index) },
            { "y", index == 7 ? nlohmann::json("bad") : nlohmann::json(1.0f - 0.03f * index) },
            { "shape", index == 8 ? 99 : 1 }
        });
    }
    malformed["pointCurves"]["red"]["points"].push_back({
        { "x", 0.25f }, { "y", 0.75f }, { "shape", 1 }
    });
    malformed["pointCurves"].erase("green");
    malformed["pointCurves"]["blue"] = "not-a-component";
    const nlohmann::json sanitized =
        RawRecipe::SanitizeFinishTonePointCurveJson(malformed);
    const RawRecipe::RawPointCurveSet sanitizedSet =
        RawRecipe::PointCurveSetFromFinishToneJson(sanitized);
    const RawRecipe::RawPointCurveComponent& sanitizedRed =
        sanitizedSet.curves[static_cast<std::size_t>(RawRecipe::RawPointCurveChannel::Red)];
    Require(sanitized["unknownFinishField"]["keep"].get<int>() == 73,
        "point-curve sanitation should preserve unknown finish-tone fields");
    Require(sanitizedRed.interpolation == "bezier-segments-v1" &&
            sanitizedRed.points.size() <= RawRecipe::kMaxRawPointCurvePoints &&
            std::abs(sanitizedRed.points.front().x) < 0.0001f &&
            std::abs(sanitizedRed.points.back().x - 1.0f) < 0.0001f,
        "malformed curves should repair interpolation, endpoints, and point limits");
    for (std::size_t index = 1; index < sanitizedRed.points.size(); ++index) {
        Require(sanitizedRed.points[index].x > sanitizedRed.points[index - 1u].x,
            "duplicate point inputs should sanitize to a strictly ordered curve");
    }
    Require(RawRecipe::IsIdentityRawPointCurveComponent(
                sanitizedSet.curves[static_cast<std::size_t>(
                    RawRecipe::RawPointCurveChannel::Green)]) &&
            RawRecipe::IsIdentityRawPointCurveComponent(
                sanitizedSet.curves[static_cast<std::size_t>(
                    RawRecipe::RawPointCurveChannel::Blue)]),
        "missing or malformed curve components should sanitize to identity");

    RawRecipe::RawPointCurveComponent alternating;
    alternating.points = {
        { 0.0f, 0.0f, 1 },
        { 0.25f, 0.82f, 1 },
        { 0.55f, 0.18f, 1 },
        { 0.78f, 0.72f, 1 },
        { 1.0f, 1.0f, 1 }
    };
    for (std::size_t segment = 0; segment + 1u < alternating.points.size(); ++segment) {
        const auto& a = alternating.points[segment];
        const auto& b = alternating.points[segment + 1u];
        const float minimum = std::min(a.y, b.y) - 0.0002f;
        const float maximum = std::max(a.y, b.y) + 0.0002f;
        for (int step = 0; step <= 64; ++step) {
            const float x = a.x + (b.x - a.x) *
                (static_cast<float>(step) / 64.0f);
            const float value = RawRecipe::EvaluateRawPointCurveComponent(alternating, x);
            Require(std::isfinite(value) && value >= minimum && value <= maximum,
                "monotone cubic segments should remain finite and never overshoot their endpoints");
        }
    }
    RawRecipe::RawPointCurveComponent inverted;
    inverted.points = { { 0.0f, 1.0f, 1 }, { 1.0f, 0.0f, 1 } };
    Require(std::abs(RawRecipe::EvaluateRawPointCurveComponent(inverted, 0.2f) - 0.8f) < 0.0001f &&
            std::abs(RawRecipe::EvaluateRawPointCurveComponent(inverted, 0.8f) - 0.2f) < 0.0001f,
        "point curves should allow finite descending and inverted responses");

    std::uint32_t randomState = 0x6d2b79f5u;
    auto nextUnit = [&]() {
        randomState = randomState * 1664525u + 1013904223u;
        return static_cast<float>((randomState >> 8u) & 0x00ffffffu) /
            static_cast<float>(0x00ffffffu);
    };
    for (int curveIndex = 0; curveIndex < 24; ++curveIndex) {
        RawRecipe::RawPointCurveComponent randomCurve;
        randomCurve.points.push_back({ 0.0f, nextUnit(), 1 });
        for (int pointIndex = 1; pointIndex < 7; ++pointIndex) {
            randomCurve.points.push_back({
                static_cast<float>(pointIndex) / 7.0f,
                nextUnit(),
                1
            });
        }
        randomCurve.points.push_back({ 1.0f, nextUnit(), 1 });
        std::array<float, 4096> lut {};
        for (std::size_t sample = 0; sample < lut.size(); ++sample) {
            lut[sample] = RawRecipe::EvaluateRawPointCurveComponent(
                randomCurve,
                static_cast<float>(sample) / static_cast<float>(lut.size() - 1u));
        }
        for (int sample = 0; sample < 512; ++sample) {
            const float x = nextUnit();
            const float lutPosition = x * static_cast<float>(lut.size() - 1u);
            const std::size_t left = static_cast<std::size_t>(std::floor(lutPosition));
            const std::size_t right = std::min(left + 1u, lut.size() - 1u);
            const float fraction = lutPosition - static_cast<float>(left);
            const float sampled = lut[left] + (lut[right] - lut[left]) * fraction;
            const float reference =
                RawRecipe::EvaluateRawPointCurveComponent(randomCurve, x);
            Require(std::abs(sampled - reference) <= 2.0e-4f,
                "4096-sample GL-linear LUT interpolation should match the CPU curve reference");
        }
    }

    nlohmann::json sceneCurves = defaults;
    sceneCurves["domain"] = 0;
    RawRecipe::RawPointCurveComponent red;
    red.points = { { 0.0f, 0.0f, 1 }, { 0.5f, 0.75f, 1 }, { 1.0f, 1.0f, 1 } };
    RawRecipe::StorePointCurveComponentInFinishToneJson(
        sceneCurves, RawRecipe::RawPointCurveChannel::Red, red);
    const std::array<float, 3> redOnly =
        RawRecipe::EvaluateFinishTonePointCurveRgb(sceneCurves, { 0.5f, 0.4f, 0.3f });
    Require(std::abs(redOnly[0] - 0.75f) < 0.0001f &&
            std::abs(redOnly[1] - 0.4f) < 0.0001f &&
            std::abs(redOnly[2] - 0.3f) < 0.0001f,
        "an R-only curve should leave green and blue unchanged");

    RawRecipe::RawPointCurveComponent composite;
    composite.points = { { 0.0f, 0.0f, 1 }, { 0.5f, 0.6f, 1 }, { 1.0f, 1.0f, 1 } };
    red.points = { { 0.0f, 0.0f, 1 }, { 0.6f, 0.8f, 1 }, { 1.0f, 1.0f, 1 } };
    RawRecipe::StorePointCurveComponentInFinishToneJson(
        sceneCurves, RawRecipe::RawPointCurveChannel::Composite, composite);
    RawRecipe::StorePointCurveComponentInFinishToneJson(
        sceneCurves, RawRecipe::RawPointCurveChannel::Red, red);
    const std::array<float, 3> ordered =
        RawRecipe::EvaluateFinishTonePointCurveRgb(sceneCurves, { 0.5f, 0.5f, 0.5f });
    Require(std::abs(ordered[0] - 0.8f) < 0.0001f &&
            std::abs(ordered[1] - 0.6f) < 0.0001f &&
            std::abs(ordered[2] - 0.6f) < 0.0001f,
        "finish tone should apply composite first and the matching channel curve second");

    nlohmann::json logCurves = defaults;
    RawRecipe::RawPointCurveComponent logComposite;
    logComposite.points = {
        { 0.0f, 0.0f, 1 },
        { 0.625f, 0.75f, 1 },
        { 1.0f, 1.0f, 1 }
    };
    RawRecipe::StorePointCurveComponentInFinishToneJson(
        logCurves, RawRecipe::RawPointCurveChannel::Composite, logComposite);
    const std::array<float, 3> logMapped =
        RawRecipe::EvaluateFinishTonePointCurveRgb(logCurves, { 0.18f, 0.18f, 0.18f });
    Require(std::abs(logMapped[0] - 0.72f) < 0.0002f &&
            std::abs(logMapped[1] - 0.72f) < 0.0002f &&
            std::abs(logMapped[2] - 0.72f) < 0.0002f,
        "Log-domain point coordinates should round-trip through shared scene EV bounds");

}

void TestRawColorWarpContract() {
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/color-warp.dng", "color-warp.dng");
    const nlohmann::json serialized = RawRecipe::SerializeRecipe(recipe);
    Require(serialized.value("rawRecipeVersion", 0) == RawRecipe::kRawDevelopmentRecipeVersion &&
            serialized.contains("colorWarp") &&
            serialized["colorWarp"].value("enabled", false) &&
            serialized["colorWarp"].value("version", 0) == 2 &&
            serialized["colorWarp"].value("coordinateModel", std::string()) ==
                "oklab-ab-d65-v2",
        "current RAW recipes should own an enabled-by-default Color Warp section");

    const auto curveStage =
        std::find(recipe.stageOrder.begin(), recipe.stageOrder.end(), "tone-curve");
    const auto colorStage =
        std::find(recipe.stageOrder.begin(), recipe.stageOrder.end(), "color-warp");
    const auto viewStage =
        std::find(recipe.stageOrder.begin(), recipe.stageOrder.end(), "view-transform");
    Require(curveStage != recipe.stageOrder.end() &&
            colorStage != recipe.stageOrder.end() &&
            viewStage != recipe.stageOrder.end() &&
            curveStage < colorStage && colorStage < viewStage,
        "Color Warp should remain between Finish Tone and View Transform");

    RawRecipe::RawColorWarpRecipe bypassedColorWarp = recipe.colorWarp;
    bypassedColorWarp.enabled = false;
    for (const std::array<float, 3>& rgb : {
             std::array<float, 3>{ -0.08f, 0.25f, 1.4f },
             std::array<float, 3>{ 0.03f, 0.18f, 0.75f },
             std::array<float, 3>{ 2.5f, 1.25f, 0.4f } }) {
        const std::array<float, 3> unchanged = RawRecipe::ApplyColorWarp(
            bypassedColorWarp, rgb, recipe.technical.workingSpace);
        Require(unchanged == rgb,
            "a disabled Color Warp must be a bit-exact scene-linear identity");
    }

    for (const Raw::RawWorkingSpace workingSpace : {
             Raw::RawWorkingSpace::LinearSrgbD65,
             Raw::RawWorkingSpace::LinearRec2020D65 }) {
        const std::array<float, 3> rgb { 0.62f, 0.21f, 0.08f };
        const RawRecipe::RawColorWarpCoordinate coordinate =
            RawRecipe::WorkingRgbToColorWarpCoordinate(rgb, workingSpace);
        const std::array<float, 3> roundTrip =
            RawRecipe::ColorWarpCoordinateToWorkingRgb(coordinate, workingSpace);
        for (int channel = 0; channel < 3; ++channel) {
            Require(std::isfinite(roundTrip[static_cast<std::size_t>(channel)]) &&
                    std::abs(roundTrip[static_cast<std::size_t>(channel)] -
                        rgb[static_cast<std::size_t>(channel)]) < 0.00005f,
                "Color Warp coordinates should round-trip through either D65 working space");
        }
    }

    const std::array<float, 3> neutralLow { 0.09f, 0.09f, 0.09f };
    const std::array<float, 3> neutralHigh { 0.72f, 0.72f, 0.72f };
    const auto neutralLowCoordinate =
        RawRecipe::WorkingRgbToColorWarpCoordinate(
            neutralLow,
            Raw::RawWorkingSpace::LinearSrgbD65);
    const auto neutralHighCoordinate =
        RawRecipe::WorkingRgbToColorWarpCoordinate(
            neutralHigh,
            Raw::RawWorkingSpace::LinearSrgbD65);
    Require(std::abs(neutralLowCoordinate.a) < 0.0001f &&
            std::abs(neutralLowCoordinate.b) < 0.0001f &&
            std::abs(neutralHighCoordinate.a) < 0.0001f &&
            std::abs(neutralHighCoordinate.b) < 0.0001f,
        "neutral pixels at different lightnesses should remain at the OKLab disc center");

    const std::array<float, 3> chromaticLow { 0.24f, 0.06f, 0.015f };
    const std::array<float, 3> chromaticHigh { 1.92f, 0.48f, 0.12f };
    const auto chromaticLowCoordinate =
        RawRecipe::WorkingRgbToColorWarpCoordinate(
            chromaticLow,
            Raw::RawWorkingSpace::LinearSrgbD65);
    const auto chromaticHighCoordinate =
        RawRecipe::WorkingRgbToColorWarpCoordinate(
            chromaticHigh,
            Raw::RawWorkingSpace::LinearSrgbD65);
    Require(std::abs(chromaticHighCoordinate.a -
                2.0f * chromaticLowCoordinate.a) < 0.0002f &&
            std::abs(chromaticHighCoordinate.b -
                2.0f * chromaticLowCoordinate.b) < 0.0002f,
        "the current Color Warp plane should use literal OKLab a/b rather than exposure-normalized chroma");

    const std::array<float, 3> sourceRgb { 0.65f, 0.22f, 0.07f };
    const RawRecipe::RawColorWarpCoordinate sourceCoordinate =
        RawRecipe::WorkingRgbToColorWarpCoordinate(
            sourceRgb, Raw::RawWorkingSpace::LinearRec2020D65);
    RawRecipe::RawColorWarpPin pin;
    pin.id = "warm-family";
    pin.name = "Warm family";
    pin.sourceA = sourceCoordinate.a;
    pin.sourceB = sourceCoordinate.b;
    pin.targetA = sourceCoordinate.a - 0.09f;
    pin.targetB = sourceCoordinate.b + 0.06f;
    pin.radius = 0.2f;
    pin.softness = 0.5f;
    Require(pin.qualifierDirectionality == 0.0f &&
            std::abs(RawRecipe::EvaluateColorWarpPinShapeDistance(
                pin, pin.sourceA + pin.radius * 0.5f, pin.sourceB) - 0.5f) <
                0.00001f,
        "default Color Warp pins should retain the exact circular qualifier");

    RawRecipe::RawColorWarpPin cone = pin;
    cone.qualifierDirectionality = 1.0f;
    cone.qualifierOrientationRadians = 0.0f;
    cone.qualifierAperture = 0.20f;
    Require(RawRecipe::EvaluateColorWarpPinShapeWeight(
                cone, cone.sourceA + cone.radius * 0.8f, cone.sourceB) > 0.0f &&
            RawRecipe::EvaluateColorWarpPinShapeWeight(
                cone, cone.sourceA - cone.radius * 0.8f, cone.sourceB) == 0.0f,
        "a directional qualifier should keep forward colors while rejecting the same-radius rear colors");
    RawRecipe::RawColorWarpPin lobe = cone;
    lobe.qualifierDirectionality = 0.5f;
    const float rearCircleDistance =
        RawRecipe::EvaluateColorWarpPinShapeDistance(
            pin, pin.sourceA - pin.radius * 0.5f, pin.sourceB);
    const float rearLobeDistance =
        RawRecipe::EvaluateColorWarpPinShapeDistance(
            lobe, lobe.sourceA - lobe.radius * 0.5f, lobe.sourceB);
    const float rearConeDistance =
        RawRecipe::EvaluateColorWarpPinShapeDistance(
            cone, cone.sourceA - cone.radius * 0.5f, cone.sourceB);
    Require(rearLobeDistance > rearCircleDistance &&
            rearLobeDistance < rearConeDistance,
        "directionality should provide a continuous circle-to-cone transition");

    pin.qualifierDirectionality = 0.72f;
    pin.qualifierOrientationRadians = -0.83f;
    pin.qualifierAperture = 0.38f;
    pin.lightnessDeltaEv = 0.35f;
    recipe.colorWarp.enabled = true;
    recipe.colorWarp.pins = { pin };

    const std::array<float, 3> warped = RawRecipe::ApplyColorWarp(
        recipe.colorWarp, sourceRgb, Raw::RawWorkingSpace::LinearRec2020D65);
    const RawRecipe::RawColorWarpCoordinate warpedCoordinate =
        RawRecipe::WorkingRgbToColorWarpCoordinate(
            warped, Raw::RawWorkingSpace::LinearRec2020D65);
    Require(std::abs(warpedCoordinate.a - sourceCoordinate.a) > 0.03f &&
            std::abs(warpedCoordinate.b - sourceCoordinate.b) > 0.02f &&
            warpedCoordinate.sceneEv > sourceCoordinate.sceneEv + 0.2f,
        "an active Color Warp pin should move its selected color family and lightness");

    RawRecipe::RawColorWarpRecipe lightnessLimited = recipe.colorWarp;
    lightnessLimited.pins.front().evCurve =
        RawRecipe::MakeColorWarpEvCurveHump(
            sourceCoordinate.sceneEv + 2.5f, 0.5f, 0.0f);
    Require(RawRecipe::ApplyColorWarp(
                lightnessLimited,
                sourceRgb,
                Raw::RawWorkingSpace::LinearRec2020D65) == sourceRgb,
        "a Color Warp pin should leave colors outside its lightness range untouched");

    RawRecipe::RawColorWarpRecipe protectedWarp = recipe.colorWarp;
    RawRecipe::RawColorWarpPin protector = pin;
    protector.id = "protect-warm-family";
    protector.protectColor = true;
    protectedWarp.pins.push_back(protector);
    const std::array<float, 3> protectedResult = RawRecipe::ApplyColorWarp(
        protectedWarp, sourceRgb, Raw::RawWorkingSpace::LinearRec2020D65);
    const RawRecipe::RawColorWarpCoordinate protectedCoordinate =
        RawRecipe::WorkingRgbToColorWarpCoordinate(
            protectedResult, Raw::RawWorkingSpace::LinearRec2020D65);
    Require(std::abs(protectedCoordinate.a - sourceCoordinate.a) <
                std::abs(warpedCoordinate.a - sourceCoordinate.a) &&
            std::abs(protectedCoordinate.b - sourceCoordinate.b) <
                std::abs(warpedCoordinate.b - sourceCoordinate.b),
        "protect-color pins should damp overlapping color-family movement");

    const RawRecipe::RawDevelopmentRecipe roundTrip = RawRecipe::DeserializeRecipe(
        RawRecipe::SerializeRecipe(recipe));
    Require(roundTrip.colorWarp.enabled && roundTrip.colorWarp.pins.size() == 1u &&
            roundTrip.colorWarp.version == 2 &&
            std::abs(roundTrip.colorWarp.pins.front().qualifierDirectionality -
                0.72f) < 0.0001f &&
            std::abs(roundTrip.colorWarp.pins.front().qualifierOrientationRadians +
                0.83f) < 0.0001f &&
            std::abs(roundTrip.colorWarp.pins.front().qualifierAperture -
                0.38f) < 0.0001f,
        "Color Warp should save and reopen through the current schema");

    RawRecipe::RawColorWarpRecipe invalidShape = recipe.colorWarp;
    invalidShape.pins.front().qualifierDirectionality = 7.0f;
    invalidShape.pins.front().qualifierOrientationRadians = -20.0f;
    invalidShape.pins.front().qualifierAperture = -2.0f;
    invalidShape = RawRecipe::SanitizeColorWarpRecipe(std::move(invalidShape));
    Require(invalidShape.pins.front().qualifierDirectionality == 1.0f &&
            invalidShape.pins.front().qualifierOrientationRadians >=
                -3.141593f &&
            invalidShape.pins.front().qualifierAperture == 0.10f,
        "directional qualifier values should be bounded during load and edit sanitization");

    RawRecipe::RawColorWarpRecipe oversized = recipe.colorWarp;
    oversized.pins.assign(12u, pin);
    const RawRecipe::RawColorWarpRecipe bounded =
        RawRecipe::SanitizeColorWarpRecipe(std::move(oversized));
    Require(bounded.pins.size() == RawRecipe::kMaxRawColorWarpPins &&
            RawRecipe::kMaxRawColorWarpPins == 8u,
        "Color Warp should expose and persist no more than eight retained pins");

    RawRecipe::RawColorWarpPin multiPeakPin = pin;
    const auto allEv = RawRecipe::MakeUniformColorWarpEvCurve(1.0f);
    const auto noEv = RawRecipe::MakeUniformColorWarpEvCurve(0.0f);
    Require(RawRecipe::EvaluateColorWarpEvCurve(allEv, -40.0f) == 1.0f &&
            RawRecipe::EvaluateColorWarpEvCurve(allEv, 40.0f) == 1.0f &&
            RawRecipe::EvaluateColorWarpEvCurve(noEv, 0.0f) == 0.0f,
        "the EV curve All and None states should remain exact across and beyond the graph domain");
    multiPeakPin.evCurve = RawRecipe::MakeUniformColorWarpEvCurve(0.0f);
    const auto lowPeak = RawRecipe::MakeColorWarpEvCurveHump(-3.5f, 0.5f, 0.0f);
    const auto highPeak = RawRecipe::MakeColorWarpEvCurveHump(1.5f, 0.5f, 0.0f);
    for (std::size_t index = 0; index < multiPeakPin.evCurve.samples.size(); ++index) {
        multiPeakPin.evCurve.samples[index] = std::max(
            lowPeak.samples[index], highPeak.samples[index]);
    }
    RawRecipe::RawColorWarpRecipe multiPeakRecipe;
    multiPeakRecipe.pins = { multiPeakPin };
    multiPeakRecipe = RawRecipe::SanitizeColorWarpRecipe(std::move(multiPeakRecipe));
    Require(RawRecipe::EvaluateColorWarpPinLightnessWeight(
                multiPeakRecipe.pins.front(), -3.5f) == 1.0f &&
            RawRecipe::EvaluateColorWarpPinLightnessWeight(
                multiPeakRecipe.pins.front(), 1.5f) == 1.0f &&
            RawRecipe::EvaluateColorWarpPinLightnessWeight(
                multiPeakRecipe.pins.front(), -1.0f) == 0.0f,
        "one continuous EV curve should retain separate peaks and an unsupported valley");
    const nlohmann::json multiPeakJson =
        RawRecipe::SerializeColorWarpRecipe(multiPeakRecipe);
    Require(multiPeakJson["pins"][0].contains("evCurve") &&
            !multiPeakJson["pins"][0].contains("evBands") &&
            multiPeakJson["pins"][0]["evCurve"]["samples"].size() ==
                RawRecipe::kRawColorWarpEvCurveSampleCount,
        "current Color Warp recipes should store only the continuous sampled EV curve");
    const auto multiPeakRoundTrip =
        RawRecipe::DeserializeColorWarpRecipe(multiPeakJson);
    Require(RawRecipe::EvaluateColorWarpPinLightnessWeight(
                multiPeakRoundTrip.pins.front(), -3.5f) == 1.0f &&
            RawRecipe::EvaluateColorWarpPinLightnessWeight(
                multiPeakRoundTrip.pins.front(), -1.0f) == 0.0f &&
            RawRecipe::EvaluateColorWarpPinLightnessWeight(
                multiPeakRoundTrip.pins.front(), 1.5f) == 1.0f,
        "saving and reopening should preserve disconnected EV peaks on one selection");

    RawRecipe::RawColorWarpRecipe sampledRecipe = recipe.colorWarp;
    RawRecipe::RawColorWarpRegion sampledRegion;
    sampledRegion.id = "fabric-region";
    sampledRegion.spatialMode = RawRecipe::RawColorWarpSpatialMode::Cohesive;
    sampledRegion.featherPixels = 12.0f;
    sampledRegion.circles.push_back({
        "fabric-circle", 0.32f, 0.64f, 0.04f, 0.06f,
        RawRecipe::RawColorWarpInterpretationMode::GuidedFamily,
        RawRecipe::RawColorWarpSamplePolarity::Include
    });
    sampledRecipe.regions = { sampledRegion };
    sampledRecipe.pins.front().regionId = sampledRegion.id;
    sampledRecipe.linkGroups.push_back({
        "fabric-group", "Fabric", { sampledRecipe.pins.front().id }
    });
    const auto sampledRoundTrip = RawRecipe::DeserializeColorWarpRecipe(
        RawRecipe::SerializeColorWarpRecipe(sampledRecipe));
    Require(sampledRoundTrip.regions.size() == 1u &&
            sampledRoundTrip.regions.front().circles.size() == 1u &&
            sampledRoundTrip.pins.front().regionId == "fabric-region" &&
            sampledRoundTrip.linkGroups.size() == 1u,
        "sample circles, spatial settings, region links, and link groups should persist");

    nlohmann::json extendedFinish = RawRecipe::DefaultFinishToneJson();
    const std::array<float, 3> extendedInput { -0.12f, 0.4f, 2.4f };
    const std::array<float, 3> extendedOutput =
        RawRecipe::EvaluateFinishTonePointCurveRgb(
            extendedFinish,
            extendedInput,
            Raw::RawWorkingSpace::LinearRec2020D65);
    for (int channel = 0; channel < 3; ++channel) {
        Require(std::abs(extendedOutput[static_cast<std::size_t>(channel)] -
                    extendedInput[static_cast<std::size_t>(channel)]) < 0.0001f,
            "current identity Finish Tone should preserve negative and above-range scene-linear values");
    }
}

void TestRawColorWarpAreaAndMaskContract() {
    namespace RawRecipe = Stack::RawRecipe;

    std::vector<RawRecipe::RawColorWarpAnalysisSample> samples;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            RawRecipe::RawColorWarpAnalysisSample sample;
            sample.sourceU = (x + 0.5f) / 10.0f;
            sample.sourceV = (y + 0.5f) / 10.0f;
            const bool greenPopulation = x >= 8;
            sample.color.a = greenPopulation ? -0.18f : 0.22f;
            sample.color.b = greenPopulation ? 0.16f : 0.09f;
            sample.color.sceneEv = y < 5 ? -3.0f : 1.0f;
            samples.push_back(sample);
        }
    }
    RawRecipe::RawColorWarpSampleCircle circle;
    circle.centerU = 0.5f;
    circle.centerV = 0.5f;
    circle.radiusU = 0.75f;
    circle.radiusV = 0.75f;
    const auto guided = RawRecipe::AnalyzeRawColorWarpArea(samples, circle, 4u);
    Require(guided.proposals.size() == 1u &&
            guided.proposals.front().sourceA > 0.15f &&
            RawRecipe::EvaluateColorWarpEvCurve(
                guided.proposals.front().evCurve, -3.0f) > 0.95f &&
            RawRecipe::EvaluateColorWarpEvCurve(
                guided.proposals.front().evCurve, 1.0f) > 0.95f &&
            RawRecipe::EvaluateColorWarpEvCurve(
                guided.proposals.front().evCurve, -1.0f) < 0.10f,
        "Guided Family should reject an unrelated color population and seed separate brightness peaks on one curve");

    circle.interpretation = RawRecipe::RawColorWarpInterpretationMode::MultipleColors;
    const auto multiple = RawRecipe::AnalyzeRawColorWarpArea(samples, circle, 1u);
    Require(multiple.proposals.size() == 1u && multiple.capacityLimited,
        "Multiple Colors should report when the remaining pin slots truncate its proposals");
    const auto allMultiple = RawRecipe::AnalyzeRawColorWarpArea(samples, circle, 4u);
    Require(allMultiple.proposals.size() >= 2u,
        "Multiple Colors should produce distinct linked-pin candidates for separated color families");

    RawRecipe::RawColorWarpMaskGuide guide;
    guide.width = 9;
    guide.height = 5;
    guide.sourceWidth = 9;
    guide.sourceHeight = 5;
    guide.pixels.resize(45u);
    for (int y = 0; y < guide.height; ++y) {
        for (int x = 0; x < guide.width; ++x) {
            auto& pixel = guide.pixels[static_cast<std::size_t>(y) * guide.width + x];
            pixel.a = (x >= 1 && x <= 3) || x == 7 ? 0.22f : -0.22f;
            pixel.b = 0.09f;
            pixel.sceneEv = 0.0f;
        }
    }
    RawRecipe::RawColorWarpPin maskPin;
    maskPin.sourceA = 0.22f;
    maskPin.sourceB = 0.09f;
    maskPin.radius = 0.08f;
    maskPin.softness = 0.5f;
    maskPin.evCurve = RawRecipe::MakeColorWarpEvCurveHump(0.0f, 1.0f, 0.0f);
    RawRecipe::RawColorWarpRegion connectedRegion;
    connectedRegion.id = "connected";
    connectedRegion.spatialMode = RawRecipe::RawColorWarpSpatialMode::Connected;
    connectedRegion.circles.push_back({
        "include", 2.5f / 9.0f, 2.5f / 5.0f, 1.2f / 9.0f, 1.2f / 5.0f,
        RawRecipe::RawColorWarpInterpretationMode::GuidedFamily,
        RawRecipe::RawColorWarpSamplePolarity::Include
    });
    const auto connected = RawRecipe::BuildRawColorWarpMaskFields(
        guide, maskPin, &connectedRegion);
    Require(connected.gate[2u * 9u + 2u] == 255u &&
            connected.gate[2u * 9u + 7u] == 0u,
        "Connected should keep the seeded component and reject a remote matching speck");

    RawRecipe::RawColorWarpRegion featheredRegion = connectedRegion;
    featheredRegion.spatialMode = RawRecipe::RawColorWarpSpatialMode::Cohesive;
    featheredRegion.featherPixels = 2.0f;
    featheredRegion.featherDirection =
        RawRecipe::RawColorWarpFeatherDirection::Centered;
    const auto feathered = RawRecipe::BuildRawColorWarpMaskFields(
        guide, maskPin, &featheredRegion);
    Require(feathered.boundary[2u * 9u + 1u] < 255u &&
            feathered.boundary[2u * 9u + 1u] > 0u,
        "centered boundary feather should attenuate pixels near the signed region boundary");
    Require(std::any_of(
                feathered.edge.begin(),
                feathered.edge.end(),
                [](std::uint8_t value) { return value != 0u; }),
        "visible image edges near a qualified region should receive a separate edge-fade diagnostic");
}

void TestRawEditAttributeBundleContract() {
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe source =
        RawRecipe::MakeDefaultRecipe(
            "D:/source/private/source-a.dng", "source-a.dng");
    source.source.relativePathKey = "private/source-a.dng";
    source.source.fingerprint = "source-a-fingerprint";
    source.source.fileSizeBytes = 123456u;
    source.technical.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    source.technical.demosaicMethod = Raw::DemosaicMethod::Bilinear;
    source.technical.workingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
    source.technical.applyBaselineExposure = false;
    source.technical.mosaicDenoise.enabled = true;
    source.technical.mosaicDenoise.hotPixelThreshold = 0.41f;
    source.whiteBalance.mode = RawRecipe::WhiteBalanceMode::CustomMultipliers;
    source.whiteBalance.hasMultipliers = true;
    source.whiteBalance.multipliers = { 1.8f, 1.0f, 1.25f };
    source.rgbDenoise.enabled = true;
    source.rgbDenoise.colorNoise = 0.61f;
    source.rgbDenoise.chromaMap.baseMultiplier = 0.61f;
    source.preToneExposureEv = 1.35f;
    source.localRange.enabled = true;
    source.localRange.strength = 0.74f;
    source.localRange.points = {
        { -8.0f, 0.2f }, { 0.0f, -0.15f }, { 6.0f, -0.4f }
    };
    source.localRange.regionMaskEnabled = true;
    source.localRange.regionMaskCenterX = 0.22f;
    RawRecipe::RawLocalRangeTargetZone zone;
    zone.id = "copied-zone";
    zone.deltaEv = 0.8f;
    zone.seeds.push_back({ 0.17f, 0.73f });
    source.localRange.targetZones.push_back(zone);
    source.finishTone.layerJson["domain"] = 0;
    source.finishTone.layerJson["activeGraphView"] = 3;
    RawRecipe::RawPointCurveComponent redCurve;
    redCurve.points = {
        { 0.0f, 0.0f, 1 }, { 0.5f, 0.72f, 1 }, { 1.0f, 1.0f, 1 }
    };
    RawRecipe::StorePointCurveComponentInFinishToneJson(
        source.finishTone.layerJson,
        RawRecipe::RawPointCurveChannel::Red,
        redCurve);
    source.viewTransform.layerJson["contrast"] = 1.42f;
    source.viewTransform.layerJson["blackEv"] = -7.25f;
    source.viewTransform.layerJson["saturation"] = 1.18f;
    source.viewTransform.layerJson["enabled"] = false;
    source.cropRotation.cropEnabled = true;
    source.cropRotation.cropX = 0.14f;
    source.cropRotation.rotationDegrees = 90;
    source.cropRotation.flipHorizontally = true;
    // Canonicalize the deliberately mixed source state before capture.
    source = RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(source));

    const RawRecipe::RawEditAttributeBundle bundle =
        RawRecipe::CaptureRawEditAttributeBundle(
            source, "Source A", "post-cfa-merge", "graph");
    const nlohmann::json serialized =
        RawRecipe::SerializeRawEditAttributeBundle(bundle);
    Require(serialized.value("schema", std::string()) ==
                RawRecipe::kRawEditAttributeBundleSchema &&
            serialized["recipe"]["sourceRef"].value(
                "sourcePath", std::string("not-empty")).empty() &&
            serialized["recipe"]["sourceRef"].value(
                "fingerprint", std::string("not-empty")).empty(),
        "RAW attribute clipboard should be versioned and must not contain source identity");

    RawRecipe::RawEditAttributeBundle loadedBundle;
    std::string bundleError;
    Require(RawRecipe::DeserializeRawEditAttributeBundle(
                serialized, loadedBundle, &bundleError) &&
            loadedBundle.sourceKind == "post-cfa-merge" &&
            loadedBundle.viewTransformPlacement == "graph",
        "RAW attribute clipboard should round-trip source kind and view placement");
    nlohmann::json futureBundle = serialized;
    futureBundle["version"] = 99;
    Require(!RawRecipe::DeserializeRawEditAttributeBundle(
                futureBundle, loadedBundle, &bundleError) &&
            !bundleError.empty(),
        "RAW attribute clipboard should reject unknown future bundle versions");

    RawRecipe::RawDevelopmentRecipe target =
        RawRecipe::MakeDefaultRecipe(
            "D:/target/target-b.dng", "target-b.dng");
    target.source.relativePathKey = "target-b.dng";
    target.source.fingerprint = "target-b-fingerprint";
    target.source.fileSizeBytes = 654321u;
    target.localRange.regionMaskEnabled = false;
    target.localRange.targetZones.clear();
    target.cropRotation = {};
    target.finishTone.layerJson["activeGraphView"] = 1;
    const RawRecipe::RawSourceReference targetIdentity = target.source;
    const RawRecipe::RawTechnicalRecipe targetTechnical = target.technical;
    std::string targetPlacement = "internal";
    const RawRecipe::RawEditAttributeApplyResult defaultApplied =
        RawRecipe::ApplyRawEditAttributeBundle(
            bundle,
            RawRecipe::DefaultRawEditAttributeSelection(),
            target,
            &targetPlacement);
    Require(defaultApplied.success && defaultApplied.changed &&
            target.source.sourcePath == targetIdentity.sourcePath &&
            target.source.fingerprint == targetIdentity.fingerprint &&
            target.technical.processingVersion == targetTechnical.processingVersion &&
             target.technical.mosaicDenoise.enabled &&
             std::abs(target.preToneExposureEv - source.preToneExposureEv) < 0.0001f &&
             std::abs(target.rgbDenoise.colorNoise - source.rgbDenoise.colorNoise) < 0.0001f &&
             target.localRange.enabled &&
             std::abs(target.localRange.strength - source.localRange.strength) < 0.0001f,
         "default RAW attribute paste should copy normal edits while preserving source and technical interpretation");
    Require(target.localRange.targetZones.empty(),
        "default RAW attribute paste should not copy targeted areas");
    Require(!target.localRange.regionMaskEnabled,
        "default RAW attribute paste should not copy range masks");
    Require(!target.cropRotation.cropEnabled &&
            target.cropRotation.rotationDegrees == 0,
        "default RAW attribute paste should not copy crop or orientation");
    Require(targetPlacement == "internal",
        "default RAW attribute paste should preserve structural view placement");
    Require(target.finishTone.layerJson.value("activeGraphView", -1) == 1,
        "default RAW attribute paste should preserve target-only curve UI state");

    RawRecipe::RawDevelopmentRecipe redOnly =
        RawRecipe::MakeDefaultRecipe("target-c.dng", "target-c.dng");
    const auto redApplied = RawRecipe::ApplyRawEditAttributeBundle(
        bundle, { "curve.red" }, redOnly, nullptr);
    Require(redApplied.success && redApplied.changed,
        "a granular RAW attribute paste should report the requested red curve change");
    const RawRecipe::RawPointCurveComponent pastedRed =
        RawRecipe::PointCurveComponentFromFinishToneJson(
            redOnly.finishTone.layerJson,
            RawRecipe::RawPointCurveChannel::Red);
    const RawRecipe::RawPointCurveComponent sourceRed =
        RawRecipe::PointCurveComponentFromFinishToneJson(
            source.finishTone.layerJson,
            RawRecipe::RawPointCurveChannel::Red);
    Require(
        std::abs(RawRecipe::EvaluateRawPointCurveComponent(pastedRed, 0.5f) -
                 RawRecipe::EvaluateRawPointCurveComponent(sourceRed, 0.5f)) < 0.0001f,
        "a granular RAW attribute paste should copy the requested red curve");
    Require(RawRecipe::IsIdentityRawPointCurveComponent(
                RawRecipe::PointCurveComponentFromFinishToneJson(
                    redOnly.finishTone.layerJson,
                    RawRecipe::RawPointCurveChannel::Green)),
        "a granular RAW attribute paste should leave the green curve unchanged");

    RawRecipe::RawDevelopmentRecipe contrastOnly =
        RawRecipe::MakeDefaultRecipe("target-view.dng", "target-view.dng");
    contrastOnly.viewTransform.layerJson["blackEv"] = -5.5f;
    contrastOnly.viewTransform.layerJson["saturation"] = 0.82f;
    contrastOnly.viewTransform.layerJson["enabled"] = true;
    const auto contrastApplied = RawRecipe::ApplyRawEditAttributeBundle(
        bundle, { "view.contrast" }, contrastOnly, nullptr);
    Require(contrastApplied.success && contrastApplied.changed &&
            std::abs(contrastOnly.viewTransform.layerJson.value(
                "contrast", 0.0f) - 1.42f) < 0.0001f &&
            std::abs(contrastOnly.viewTransform.layerJson.value(
                "blackEv", 0.0f) + 5.5f) < 0.0001f &&
            std::abs(contrastOnly.viewTransform.layerJson.value(
                "saturation", 0.0f) - 0.82f) < 0.0001f &&
            contrastOnly.viewTransform.layerJson.value("enabled", false),
        "a granular View paste should copy only its selected subsection and preserve transform enablement");

    RawRecipe::RawDevelopmentRecipe fullTarget =
        RawRecipe::MakeDefaultRecipe("target-d.dng", "target-d.dng");
    fullTarget.source.relativePathKey = "target-d.dng";
    fullTarget.source.fingerprint = "target-d-fingerprint";
    const RawRecipe::RawSourceReference fullTargetIdentity = fullTarget.source;
    std::string fullPlacement = "internal";
    const auto fullApplied = RawRecipe::ApplyRawEditAttributeBundle(
        bundle,
        RawRecipe::AllRawEditAttributeKeys(),
        fullTarget,
        &fullPlacement);
    Require(fullApplied.success && fullApplied.changed &&
            fullTarget.source.fingerprint == fullTargetIdentity.fingerprint &&
            fullTarget.technical.processingVersion == source.technical.processingVersion &&
            fullTarget.localRange.targetZones.size() == 1u &&
            fullTarget.localRange.regionMaskEnabled &&
            fullTarget.cropRotation.cropEnabled &&
            fullTarget.cropRotation.rotationDegrees == 90 &&
            fullPlacement == "graph" &&
            fullApplied.warnings.size() >= 3u,
        "full RAW attribute paste should include opt-in technical and spatial edits without relinking the target source");

    RawRecipe::RawDevelopmentRecipe rejectedTarget = fullTarget;
    const auto rejected = RawRecipe::ApplyRawEditAttributeBundle(
        bundle, { "not.a.real.attribute" }, rejectedTarget, nullptr);
    Require(!rejected.success && !rejected.errorMessage.empty() &&
            RawRecipe::SerializeRecipe(rejectedTarget) ==
                RawRecipe::SerializeRecipe(fullTarget),
        "unknown RAW attribute keys should fail without partially changing a target recipe");

    RawRecipe::RawDevelopmentRecipe placementOnly =
        RawRecipe::MakeDefaultRecipe("placement.dng", "placement.dng");
    std::string placementOnlyTarget = "internal";
    const auto placementOnlyApplied = RawRecipe::ApplyRawEditAttributeBundle(
        bundle,
        { "view.placement" },
        placementOnly,
        &placementOnlyTarget);
    Require(placementOnlyApplied.success && placementOnlyApplied.changed &&
            placementOnlyTarget == "graph",
        "RAW attribute paste should report a placement-only change even when recipe values are unchanged");

    RawRecipe::RawEditAttributeTargetCompatibility hdrCompatibility;
    hdrCompatibility.postCfaMerge = true;
    hdrCompatibility.hdrMerge = true;
    const auto hdrPlan = RawRecipe::PlanRawEditAttributeSelectionForTarget(
        bundle,
        { "technical.interpretation", "technical.cfa-denoise",
          "exposure.raw", "view.contrast" },
        hdrCompatibility);
    Require(hdrPlan.applicableKeys ==
                std::vector<std::string>({ "exposure.raw", "view.contrast" }) &&
            hdrPlan.skippedKeys.size() == 2u &&
            hdrPlan.warnings.size() == 2u,
        "finished HDR targets should skip only impossible pre-CFA attributes while keeping normal RAW edits applicable");

    RawRecipe::RawEditAttributeTargetCompatibility graphCompatibility;
    graphCompatibility.graphViewTransform = true;
    const auto graphPlan = RawRecipe::PlanRawEditAttributeSelectionForTarget(
        bundle,
        { "view.contrast", "view.color", "view.placement", "curve.composite" },
        graphCompatibility);
    Require(graphPlan.applicableKeys ==
                std::vector<std::string>({ "view.placement", "curve.composite" }) &&
            graphPlan.skippedKeys.size() == 2u &&
            graphPlan.warnings.size() == 1u,
        "offline graph-view targets should keep compatible edits while skipping unsafe graph-node mutations");
}

void TestRawProjectEditPipelineContract() {
    namespace Project = Stack::Project;
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe singleRecipe =
        RawRecipe::MakeDefaultRecipe("asset-a.dng", "Asset A");
    singleRecipe.source.relativePathKey = "asset-a";
    Project::RawProjectSnapshot single;
    single.projectId = "single-project";
    single.projectName = "Single Project";
    single.rawWorkspaceData = {
        { "rawRecipe", RawRecipe::SerializeRecipe(singleRecipe) }
    };
    single.pipelineData = {
        { "graph", {
            { "nodes", nlohmann::json::array({
                {
                    { "kind", "RawDevelopment" },
                    { "rawRecipe", RawRecipe::SerializeRecipe(singleRecipe) },
                    { "rawEdited", false },
                    { "rawAutosaved", true }
                },
                { { "kind", "Output" } }
            }) }
        } }
    };
    Project::MultiFrameGraphNode staleGraphNode;
    staleGraphNode.nodeId = "stale-node-from-older-schema";
    single.multiFrameGraph.nodes.push_back(staleGraphNode);
    Require(!Project::IsMultiFrameProjectDocument(single),
        "a single-RAW document must not become a burst merely because an older graph field survived round-trip");
    Project::RawProjectEditRecipeBinding singleBinding;
    std::string error;
    Require(Project::ResolveRawProjectEditRecipe(
                single, {}, singleBinding, &error) &&
            !singleBinding.multiFrameResult &&
            singleBinding.sourceKind == "single-raw",
        "single-image project snapshots should resolve through the shared RAW edit pipeline");
    singleBinding.recipe.preToneExposureEv = 0.9f;
    const auto singleUpdate = Project::StoreRawProjectEditRecipe(
        single, singleBinding, singleBinding.recipe);
    Require(singleUpdate.success && singleUpdate.changed &&
            single.postRecipeRevision == 1u &&
            std::abs(single.rawWorkspaceData["rawRecipe"].value(
                "exposureEv", 0.0f) - 0.9f) < 0.0001f &&
            std::abs(single.pipelineData["graph"]["nodes"][0]["rawRecipe"].value(
                "exposureEv", 0.0f) - 0.9f) < 0.0001f &&
            single.pipelineData["graph"]["nodes"][0].value(
                "rawEdited", false),
        "single-image project edits should update metadata and the compact graph owner together");

    Project::RawProjectSnapshot mixedSingle = single;
    mixedSingle.activeSourceSetId = "capture-set";
    Project::MultiFrameSourceSet captureSet;
    captureSet.sourceSetId = "capture-set";
    captureSet.name = "Unmerged capture set";
    captureSet.operationIntent =
        Project::MultiFrameOperationIntent::RawCaptureSet;
    captureSet.settings = nlohmann::json::object();
    mixedSingle.sourceSets.push_back(captureSet);
    Require(Project::IsMultiFrameProjectDocument(mixedSingle),
        "a typed capture set should route to the Multiframe workspace even while it is still a one-frame draft");
    Project::RawProjectEditRecipeBinding mixedBinding;
    Require(Project::ResolveRawProjectEditRecipe(
                mixedSingle, {}, mixedBinding, &error) &&
            !mixedBinding.multiFrameResult &&
            mixedBinding.sourceKind == "single-raw",
        "a unified project may retain unrelated capture sets while its single-image RAW recipe remains editable");
    Project::RawProjectEditRecipeBinding unsupportedBinding;
    Require(!Project::ResolveRawProjectEditRecipe(
                mixedSingle,
                captureSet.sourceSetId,
                unsupportedBinding,
                &error) &&
            error.find("finished CFA merge") != std::string::npos,
        "an explicitly selected unmerged capture set should not masquerade as an editable HDR or denoise result");

    Project::RawProjectSnapshot graphHdr = mixedSingle;
    graphHdr.postRecipeRevision = 0u;
    graphHdr.rawWorkspaceData = nlohmann::json::object();
    graphHdr.pipelineData = nlohmann::json::object();
    graphHdr.sourceSets.front().settings["sharedPostHdrRecipe"] =
        RawRecipe::SerializeRecipe(
            RawRecipe::MakeDefaultRecipe(
                "hdr://capture-set", "Graph HDR result"));
    Project::MultiFrameGraphNode captureNode;
    captureNode.nodeId = "capture-node";
    captureNode.kind = Project::MultiFrameGraphNodeKind::CaptureSubset;
    captureNode.sourceSetId = captureSet.sourceSetId;
    captureNode.frameIds = { "frame-a", "frame-b" };
    Project::MultiFrameGraphNode hdrNode;
    hdrNode.nodeId = "hdr-node";
    hdrNode.kind = Project::MultiFrameGraphNodeKind::HdrMerge;
    Project::MultiFrameGraphNode outputNode;
    outputNode.nodeId = "output-node";
    outputNode.kind = Project::MultiFrameGraphNodeKind::Output;
    graphHdr.multiFrameGraph.nodes = {
        captureNode, hdrNode, outputNode
    };
    Project::MultiFrameGraphLink captureToHdr;
    captureToHdr.linkId = "capture-to-hdr";
    captureToHdr.fromNodeId = captureNode.nodeId;
    captureToHdr.toNodeId = hdrNode.nodeId;
    Project::MultiFrameGraphLink hdrToOutput;
    hdrToOutput.linkId = "hdr-to-output";
    hdrToOutput.fromNodeId = hdrNode.nodeId;
    hdrToOutput.toNodeId = outputNode.nodeId;
    graphHdr.multiFrameGraph.links = {
        captureToHdr, hdrToOutput
    };
    graphHdr.multiFrameGraph.outputNodeId = outputNode.nodeId;
    Project::RawProjectEditRecipeBinding graphHdrBinding;
    Require(Project::ResolveRawProjectEditRecipe(
                graphHdr,
                captureSet.sourceSetId,
                graphHdrBinding,
                &error) &&
            graphHdrBinding.multiFrameResult &&
            graphHdrBinding.hdrResult &&
            graphHdrBinding.sourceSetId == captureSet.sourceSetId,
        "a neutral capture set connected through the persisted MultiFrame HDR Output should expose its saved post-HDR RAW recipe");
    graphHdrBinding.recipe.preToneExposureEv = 0.75f;
    const auto graphHdrUpdate = Project::StoreRawProjectEditRecipe(
        graphHdr, graphHdrBinding, graphHdrBinding.recipe);
    Require(graphHdrUpdate.success && graphHdrUpdate.changed &&
            graphHdr.postRecipeRevision == 1u &&
            std::abs(graphHdr.sourceSets.front().settings[
                "sharedPostHdrRecipe"].value("exposureEv", 0.0f) -
                0.75f) < 0.0001f,
        "graph-owned HDR capture sets should store pasted RAW edits without rewriting their neutral capture-set intent");

    Project::RawProjectSnapshot graphBurst = graphHdr;
    graphBurst.postRecipeRevision = 0u;
    graphBurst.sourceSets.front().settings.erase("sharedPostHdrRecipe");
    graphBurst.sourceSets.front().settings["sharedPostMfdRecipe"] =
        RawRecipe::SerializeRecipe(
            RawRecipe::MakeDefaultRecipe(
                "mfd://capture-set", "Graph Burst result"));
    graphBurst.multiFrameGraph.nodes[1].kind =
        Project::MultiFrameGraphNodeKind::BurstDenoise;
    Project::RawProjectEditRecipeBinding graphBurstBinding;
    Require(Project::ResolveRawProjectEditRecipe(
                graphBurst,
                captureSet.sourceSetId,
                graphBurstBinding,
                &error) &&
            graphBurstBinding.multiFrameResult &&
            !graphBurstBinding.hdrResult,
        "a neutral capture set connected through the persisted MultiFrame Burst Output should expose its saved post-Burst RAW recipe");

    Project::RawProjectSnapshot mfd;
    mfd.projectId = "mfd-project";
    mfd.projectName = "MFD Project";
    mfd.activeSourceSetId = "set-a";
    Project::MultiFrameSourceSet mfdSet;
    mfdSet.sourceSetId = "set-a";
    mfdSet.name = "Burst A";
    mfdSet.operationIntent =
        Project::MultiFrameOperationIntent::RawBurstDenoise;
    mfdSet.settings = Project::MakeDefaultMfdOperationSettings();
    RawRecipe::RawDevelopmentRecipe pre =
        RawRecipe::MakeDefaultRecipe("mfd-pre://set-a", "Pre");
    pre.technical.mosaicDenoise.enabled = true;
    pre.technical.mosaicDenoise.hotPixelThreshold = 0.29f;
    pre.cropRotation.rotationDegrees = 90;
    RawRecipe::RawDevelopmentRecipe post =
        RawRecipe::MakeDefaultRecipe("mfd://set-a", "Post");
    post.preToneExposureEv = 0.4f;
    mfdSet.settings["sharedPreMfdRecipe"] =
        RawRecipe::SerializeRecipe(pre);
    mfdSet.settings["sharedPostMfdRecipe"] =
        RawRecipe::SerializeRecipe(post);
    mfd.sourceSets.push_back(mfdSet);

    Project::RawProjectEditRecipeBinding mfdBinding;
    Require(Project::ResolveRawProjectEditRecipe(
                mfd, {}, mfdBinding, &error) &&
            mfdBinding.multiFrameResult && !mfdBinding.hdrResult &&
            mfdBinding.sourceKind == "post-cfa-merge" &&
            mfdBinding.recipe.technical.mosaicDenoise.enabled &&
            mfdBinding.recipe.cropRotation.rotationDegrees == 90 &&
            std::abs(mfdBinding.recipe.preToneExposureEv - 0.4f) < 0.0001f,
        "MFD projects should resolve their pre-merge and post-merge pieces as one editable RAW recipe");
    mfdBinding.recipe.technical.mosaicDenoise.hotPixelThreshold = 0.63f;
    mfdBinding.recipe.preToneExposureEv = 1.1f;
    const auto mfdUpdate = Project::StoreRawProjectEditRecipe(
        mfd, mfdBinding, mfdBinding.recipe);
    Require(mfdUpdate.success && mfdUpdate.changed &&
            mfdUpdate.preMergeChanged && mfdUpdate.postMergeChanged &&
            mfd.mfdInputRevision == 1u && mfd.postRecipeRevision == 1u,
        "MFD recipe storage should split edits back across the correct processing boundaries and revisions");
    Project::RawProjectEditRecipeBinding mfdReloaded;
    Require(Project::ResolveRawProjectEditRecipe(
                mfd, "set-a", mfdReloaded, &error) &&
            std::abs(mfdReloaded.recipe.technical.mosaicDenoise.hotPixelThreshold - 0.63f) < 0.0001f &&
            std::abs(mfdReloaded.recipe.preToneExposureEv - 1.1f) < 0.0001f,
        "MFD split storage should reconstruct the exact edited recipe on reload");

    Project::RawProjectSnapshot hdr;
    hdr.projectId = "hdr-project";
    hdr.projectName = "HDR Project";
    hdr.activeSourceSetId = "set-hdr";
    Project::MultiFrameSourceSet hdrSet;
    hdrSet.sourceSetId = "set-hdr";
    hdrSet.name = "Bracket";
    hdrSet.operationIntent = Project::MultiFrameOperationIntent::RawBurstHdr;
    hdrSet.settings = Project::MakeDefaultHdrOperationSettings();
    RawRecipe::RawDevelopmentRecipe hdrPost =
        RawRecipe::MakeDefaultRecipe("hdr://set-hdr", "HDR Post");
    hdrPost.viewTransform.layerJson["contrast"] = 1.3f;
    hdrSet.settings["sharedPostHdrRecipe"] =
        RawRecipe::SerializeRecipe(hdrPost);
    hdr.sourceSets.push_back(hdrSet);
    Project::RawProjectEditRecipeBinding hdrBinding;
    Require(Project::ResolveRawProjectEditRecipe(
                hdr, {}, hdrBinding, &error) &&
            hdrBinding.multiFrameResult && hdrBinding.hdrResult &&
            std::abs(hdrBinding.recipe.viewTransform.layerJson.value(
                "contrast", 0.0f) - 1.3f) < 0.0001f,
        "HDR projects should expose their post-CFA recipe through the same edit-pipeline contract");
}

void TestMultiFrameBurstResultCacheContract() {
    const std::filesystem::path root =
        MakeTempDirectory("stack_multiframe_result_cache_test");
    const std::filesystem::path cachePath = root / "result.mfg-cache";

    Raw::RawImageData source;
    source.metadata.rawWidth = 3;
    source.metadata.rawHeight = 2;
    source.metadata.visibleWidth = 3;
    source.metadata.visibleHeight = 2;
    source.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    source.metadata.mosaiced = true;
    source.metadata.sourceContentSha256 = std::string(64u, 'a');
    source.normalizedMosaicInputContract =
        Raw::NormalizedMosaicInputContract::MfdReferencePreGain;
    source.normalizedMosaicContentHash = 987654321u;
    source.normalizedMosaicBuffer =
        std::make_shared<const std::vector<float>>(
            std::vector<float> { 0.05f, 0.12f, 0.23f, 0.34f, 0.45f, 0.56f });

    std::string error;
    Require(
        Stack::EditorMultiFrameCache::WriteBurstResult(
            cachePath, std::string(64u, 'b'), 17u, source, &error),
        "MultiFrame Burst result cache should write a verified final RAW output");
    std::shared_ptr<Raw::RawImageData> restored;
    Require(
        Stack::EditorMultiFrameCache::ReadBurstResult(
            cachePath, std::string(64u, 'b'), 17u, restored, &error) &&
            restored && restored->normalizedMosaicBuffer &&
            *restored->normalizedMosaicBuffer ==
                *source.normalizedMosaicBuffer &&
            restored->normalizedMosaicContentHash ==
                source.normalizedMosaicContentHash &&
            restored->normalizedMosaicInputContract ==
                Raw::NormalizedMosaicInputContract::MfdReferencePreGain,
        "MultiFrame Burst result cache should restore the exact verified Virtual Bayer output");
    Require(
        !Stack::EditorMultiFrameCache::ReadBurstResult(
            cachePath, std::string(64u, 'c'), 17u, restored, &error),
        "MultiFrame Burst result cache should reject a different graph identity");

    {
        std::ofstream corrupt(
            cachePath, std::ios::binary | std::ios::app);
        corrupt.put('\0');
    }
    Require(
        !Stack::EditorMultiFrameCache::ReadBurstResult(
            cachePath, std::string(64u, 'b'), 17u, restored, &error),
        "MultiFrame Burst result cache should reject content that no longer matches its checksum");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void TestRawProjectAttributeTransferTransaction() {
    namespace Project = Stack::Project;
    namespace RawRecipe = Stack::RawRecipe;

    const auto unique = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("stack-raw-attribute-transfer-" + std::to_string(unique));
    const std::filesystem::path singleRoot = root / "single";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    RawRecipe::RawDevelopmentRecipe targetRecipe =
        RawRecipe::MakeDefaultRecipe("assets/target.dng", "Target");
    targetRecipe.source.relativePathKey = "target.dng";
    targetRecipe.source.fingerprint = "target-fingerprint";
    Project::RawProjectSnapshot snapshot;
    snapshot.projectId = "attribute-transfer-project";
    snapshot.projectName = "Attribute Transfer Target";
    snapshot.rawWorkspaceData = {
        { "rawRecipe", RawRecipe::SerializeRecipe(targetRecipe) }
    };
    snapshot.pipelineData = {
        { "graph", {
            { "nodes", nlohmann::json::array({
                {
                    { "kind", "RawDevelopment" },
                    { "rawRecipe", RawRecipe::SerializeRecipe(targetRecipe) }
                },
                { { "kind", "Output" } }
            }) }
        } }
    };
    Project::ProjectStoreOpenResult created = Project::CreateProjectStore(
        singleRoot,
        Project::ProjectStorageKind::DirectoryBundle,
        snapshot);
    Require(static_cast<bool>(created),
        "RAW attribute transaction fixture should create a directory project");
    std::uint64_t checkedRevision = 0;
    std::string checkError;
    const auto manifestPath = Project::WorkingProjectDocumentPath(singleRoot);
    const auto originalWriteTime = std::filesystem::last_write_time(manifestPath);
    Require(created.store->ReadStorageRevision(checkedRevision, checkError) &&
            checkedRevision == created.store->StorageRevision() &&
            std::filesystem::last_write_time(manifestPath) == originalWriteTime,
        "Clean save checks must read storage without rewriting the manifest");

    RawRecipe::RawDevelopmentRecipe sourceRecipe =
        RawRecipe::MakeDefaultRecipe("source.dng", "Source");
    sourceRecipe.preToneExposureEv = 1.7f;
    sourceRecipe.viewTransform.layerJson["contrast"] = 1.22f;
    const RawRecipe::RawEditAttributeBundle bundle =
        RawRecipe::CaptureRawEditAttributeBundle(
            sourceRecipe, "Source", "single-raw", "internal");
    const Project::RawProjectAttributeTransferResult pasted =
        Project::PasteRawEditAttributesIntoProject(
            singleRoot, {}, bundle, { "exposure.raw", "view.contrast" });
    Require(pasted.success && pasted.changed &&
            pasted.committedStorageRevision > 0u,
        "RAW attribute transfer should commit a changed target project transaction");
    Require(created.store->ReadStorageRevision(checkedRevision, checkError) &&
            checkedRevision != created.store->StorageRevision(),
        "Save checks must detect external writes instead of returning the cached generation");

    const Project::ProjectStoreOpenResult reopened =
        Project::OpenProjectStore(singleRoot);
    Project::RawProjectEditRecipeBinding binding;
    std::string error;
    Require(reopened && Project::ResolveRawProjectEditRecipe(
                reopened.snapshot, {}, binding, &error) &&
            std::abs(binding.recipe.preToneExposureEv - 1.7f) < 0.0001f &&
            std::abs(binding.recipe.viewTransform.layerJson.value(
                "contrast", 0.0f) - 1.22f) < 0.0001f &&
            binding.recipe.source.fingerprint == "target-fingerprint",
        "committed RAW attributes should reload while preserving the target source identity");
    Require(
        std::abs(reopened.snapshot.pipelineData["graph"]["nodes"][0]["rawRecipe"].value(
            "exposureEv", 0.0f) - 1.7f) < 0.0001f,
        "transactional RAW attribute transfer should keep compact graph state synchronized");

    const Project::RawProjectAttributeTransferResult repeated =
        Project::PasteRawEditAttributesIntoProject(
            singleRoot, {}, bundle, { "exposure.raw", "view.contrast" });
    Require(repeated.success && !repeated.changed &&
            repeated.committedStorageRevision ==
                reopened.snapshot.persistedStorageRevision,
        "repeating identical RAW attributes should avoid a redundant project generation");

    RawRecipe::RawDevelopmentRecipe transferSource = sourceRecipe;
    transferSource.technical.mosaicDenoise.enabled = true;
    transferSource.technical.mosaicDenoise.hotPixelThreshold = 0.47f;
    transferSource.preToneExposureEv = 0.85f;
    transferSource.viewTransform.layerJson["contrast"] = 1.36f;
    const RawRecipe::RawEditAttributeBundle multiFrameBundle =
        RawRecipe::CaptureRawEditAttributeBundle(
            transferSource, "Multi-frame transfer source");

    Project::RawProjectSnapshot mfdSnapshot;
    mfdSnapshot.projectId = "attribute-transfer-mfd";
    mfdSnapshot.projectName = "Attribute Transfer MFD";
    mfdSnapshot.projectKindHint = StackBinaryFormat::kRawProjectKind;
    mfdSnapshot.activeSourceSetId = "mfd-set";
    Project::MultiFrameSourceSet mfdSet;
    mfdSet.sourceSetId = mfdSnapshot.activeSourceSetId;
    mfdSet.name = "Finished Denoise";
    mfdSet.operationIntent =
        Project::MultiFrameOperationIntent::RawBurstDenoise;
    mfdSet.operationSchemaVersion = Project::kMfdOperationSchemaVersion;
    mfdSet.settings = Project::MakeDefaultMfdOperationSettings();
    RawRecipe::RawDevelopmentRecipe mfdPre =
        RawRecipe::MakeDefaultRecipe("mfd-pre://fixture", "MFD pre");
    RawRecipe::RawDevelopmentRecipe mfdPost =
        RawRecipe::MakeDefaultRecipe("mfd://fixture", "MFD post");
    mfdSet.settings["sharedPreMfdRecipe"] =
        RawRecipe::SerializeRecipe(mfdPre);
    mfdSet.settings["sharedPostMfdRecipe"] =
        RawRecipe::SerializeRecipe(mfdPost);
    mfdSnapshot.sourceSets.push_back(std::move(mfdSet));
    mfdSnapshot.multiFrameGraph =
        Project::BuildOperationMultiFrameGraph(mfdSnapshot);
    const std::filesystem::path mfdRoot = root / "mfd";
    const Project::ProjectStoreOpenResult mfdCreated =
        Project::CreateProjectStore(
            mfdRoot,
            Project::ProjectStorageKind::DirectoryBundle,
            mfdSnapshot);
    Require(static_cast<bool>(mfdCreated),
        "MFD attribute transaction fixture should create a directory project");
    const Project::RawProjectAttributeTransferResult mfdPasted =
        Project::PasteRawEditAttributesIntoProject(
            mfdRoot,
            mfdSnapshot.activeSourceSetId,
            multiFrameBundle,
            { "technical.cfa-denoise", "exposure.raw", "view.contrast" });
    const Project::ProjectStoreOpenResult mfdReopened =
        Project::OpenProjectStore(mfdRoot);
    Project::RawProjectEditRecipeBinding mfdBinding;
    Require(mfdPasted.success && mfdPasted.changed && mfdReopened &&
            Project::ResolveRawProjectEditRecipe(
                mfdReopened.snapshot,
                mfdSnapshot.activeSourceSetId,
                mfdBinding,
                &error) &&
            mfdBinding.recipe.technical.mosaicDenoise.enabled &&
            std::abs(mfdBinding.recipe.technical.mosaicDenoise.hotPixelThreshold -
                0.47f) < 0.0001f &&
            std::abs(mfdBinding.recipe.preToneExposureEv - 0.85f) < 0.0001f &&
            std::abs(mfdBinding.recipe.viewTransform.layerJson.value(
                "contrast", 0.0f) - 1.36f) < 0.0001f &&
            mfdReopened.snapshot.mfdInputRevision == 1u &&
            mfdReopened.snapshot.postRecipeRevision == 1u,
        "MFD attribute paste should transactionally persist pre-merge and post-merge edits with both revisions");

    Project::RawProjectSnapshot hdrSnapshot;
    hdrSnapshot.projectId = "attribute-transfer-hdr";
    hdrSnapshot.projectName = "Attribute Transfer HDR";
    hdrSnapshot.projectKindHint = StackBinaryFormat::kRawProjectKind;
    hdrSnapshot.activeSourceSetId = "hdr-set";
    Project::MultiFrameSourceSet hdrSet;
    hdrSet.sourceSetId = hdrSnapshot.activeSourceSetId;
    hdrSet.name = "Finished HDR";
    hdrSet.operationIntent =
        Project::MultiFrameOperationIntent::RawBurstHdr;
    hdrSet.operationSchemaVersion = Project::kHdrOperationSchemaVersion;
    hdrSet.settings = Project::MakeDefaultHdrOperationSettings();
    RawRecipe::RawDevelopmentRecipe hdrPost =
        RawRecipe::MakeDefaultRecipe("hdr://fixture", "HDR post");
    hdrPost.technical.applyBaselineExposure = false;
    hdrSet.settings["sharedPostHdrRecipe"] =
        RawRecipe::SerializeRecipe(hdrPost);
    hdrSet.settings["viewTransformPlacement"] = "graph";
    hdrSet.settings["graphViewTransformNodeUuid"] = "hdr-view-node";
    hdrSnapshot.sourceSets.push_back(std::move(hdrSet));
    hdrSnapshot.multiFrameGraph =
        Project::BuildOperationMultiFrameGraph(hdrSnapshot);
    hdrSnapshot.pipelineData = {
        { "layers", nlohmann::json::array({
            { { "contrast", 1.0f } }
        }) },
        { "nodeGraph", {
            { "nodes", nlohmann::json::array({
                {
                    { "instanceUuid", "hdr-view-node" },
                    { "layerIndex", 0 }
                }
            }) }
        } }
    };
    const std::filesystem::path hdrRoot = root / "hdr";
    const Project::ProjectStoreOpenResult hdrCreated =
        Project::CreateProjectStore(
            hdrRoot,
            Project::ProjectStorageKind::DirectoryBundle,
            hdrSnapshot);
    Require(static_cast<bool>(hdrCreated),
        "HDR attribute transaction fixture should create a directory project");
    const Project::RawProjectAttributeTransferResult hdrPasted =
        Project::PasteRawEditAttributesIntoProject(
            hdrRoot,
            hdrSnapshot.activeSourceSetId,
            multiFrameBundle,
            { "technical.cfa-denoise", "exposure.raw", "view.contrast" });
    const Project::ProjectStoreOpenResult hdrReopened =
        Project::OpenProjectStore(hdrRoot);
    Project::RawProjectEditRecipeBinding hdrBinding;
    Require(hdrPasted.success && hdrPasted.changed &&
            hdrPasted.warnings.size() == 1u && hdrReopened &&
            Project::ResolveRawProjectEditRecipe(
                hdrReopened.snapshot,
                hdrSnapshot.activeSourceSetId,
                hdrBinding,
                &error) &&
            !hdrBinding.recipe.technical.mosaicDenoise.enabled &&
            std::abs(hdrBinding.recipe.preToneExposureEv - 0.85f) < 0.0001f &&
            std::abs(hdrBinding.recipe.viewTransform.layerJson.value(
                "contrast", 0.0f) - 1.36f) < 0.0001f &&
            std::abs(hdrReopened.snapshot.pipelineData["layers"][0].value(
                "contrast", 0.0f) - 1.36f) < 0.0001f &&
            hdrReopened.snapshot.hdrInputRevision == 0u &&
            hdrReopened.snapshot.postRecipeRevision == 1u,
        "HDR attribute paste should skip impossible CFA denoise while transactionally saving compatible post-merge and graph-view edits");

    std::filesystem::remove_all(root, cleanupError);
}

void TestRawAttributePasteCreatesIndependentProjects() {
    namespace RawRecipe = Stack::RawRecipe;

    const auto unique = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path workspace =
        std::filesystem::temp_directory_path() /
        ("stack-raw-attribute-create-" + std::to_string(unique));
    const std::filesystem::path original = workspace / "capture-a.dng";
    std::error_code error;
    std::filesystem::create_directories(workspace, error);
    {
        std::ofstream stream(original, std::ios::binary);
        stream << "independent-raw-asset-fixture";
    }

    Stack::RawWorkspace::SourceRecord source;
    source.absolutePath = original;
    source.relativePath = "capture-a.dng";
    source.relativePathKey = "capture-a.dng";
    source.fileName = "capture-a.dng";
    source.stem = "capture-a";
    source.extension = ".dng";
    source.fileSizeBytes = std::filesystem::file_size(original, error);
    source.fingerprint = "catalog-fingerprint";

    RawRecipe::RawDevelopmentRecipe copied =
        RawRecipe::MakeDefaultRecipe("source.dng", "Copied Source");
    copied.preToneExposureEv = 1.25f;
    copied.cropRotation.cropEnabled = true;
    copied.cropRotation.cropX = 0.1f;
    const RawRecipe::RawEditAttributeBundle bundle =
        RawRecipe::CaptureRawEditAttributeBundle(copied, "Copied Source");
    const auto created =
        Stack::EditorRawAttributes::CreateProjectAndPasteAttributes(
            workspace,
            source,
            bundle,
            { "exposure.raw", "geometry.crop" },
            workspace / "test-projects");
    Require(created.transfer.success && created.transfer.changed &&
            !created.projectPath.empty() &&
            std::filesystem::exists(created.projectPath) &&
            std::filesystem::exists(original),
        "pasting to an unedited Gallery image should create its own project without moving or deleting the original");

    error.clear();
    Require(std::filesystem::remove(original, error) && !error &&
            !std::filesystem::exists(original),
        "the independent-project fixture should allow its original source to be removed");

    const Stack::Project::ProjectStoreOpenResult opened =
        Stack::Project::OpenProjectStore(created.projectPath);
    Stack::Project::RawProjectEditRecipeBinding binding;
    std::string resolveError;
    Require(opened && Stack::Project::ResolveRawProjectEditRecipe(
                opened.snapshot, {}, binding, &resolveError) &&
            std::abs(binding.recipe.preToneExposureEv - 1.25f) < 0.0001f &&
            binding.recipe.cropRotation.cropEnabled &&
            binding.recipe.source.relativePathKey == "capture-a.dng" &&
            binding.recipe.source.sourcePath.find("assets") != std::string::npos &&
            opened.snapshot.embeddedAssets.size() == 1u &&
            !Stack::Project::IsMultiFrameProjectDocument(opened.snapshot),
        "a batch-created target should persist the pasted recipe and its own managed RAW asset");
    std::string assetError;
    const auto assetStream = opened.store->OpenAssetStream(
        opened.snapshot.embeddedAssets.front().assetId,
        &assetError);
    Require(static_cast<bool>(assetStream) &&
            assetStream.byteLength == source.fileSizeBytes,
        "a batch-created target should remain independently readable after its original source is deleted");

    RawRecipe::RawDevelopmentRecipe replacement =
        RawRecipe::MakeDefaultRecipe("replacement.dng", "Replacement");
    replacement.preToneExposureEv = 2.0f;
    const auto replacementBundle =
        RawRecipe::CaptureRawEditAttributeBundle(
            replacement, "Replacement");
    const auto duplicated =
        Stack::EditorRawAttributes::DuplicateProjectAndPasteAttributes(
            created.projectPath,
            {},
            replacementBundle,
            { "exposure.raw" });
    const Stack::Project::ProjectStoreOpenResult originalReopened =
        Stack::Project::OpenProjectStore(created.projectPath);
    const Stack::Project::ProjectStoreOpenResult duplicateOpened =
        Stack::Project::OpenProjectStore(duplicated.projectPath);
    Stack::Project::RawProjectEditRecipeBinding originalBinding;
    Stack::Project::RawProjectEditRecipeBinding duplicateBinding;
    const bool originalResolved = originalReopened &&
        Stack::Project::ResolveRawProjectEditRecipe(
            originalReopened.snapshot, {}, originalBinding,
            &resolveError);
    const bool duplicateResolved = duplicateOpened &&
        Stack::Project::ResolveRawProjectEditRecipe(
            duplicateOpened.snapshot, {}, duplicateBinding,
            &resolveError);
    const std::filesystem::path duplicateManagedSource =
        duplicateResolved
            ? std::filesystem::path(
                duplicateBinding.recipe.source.sourcePath).lexically_normal()
            : std::filesystem::path();
    const std::filesystem::path duplicateManagedRelative =
        duplicateManagedSource.lexically_relative(
            duplicated.projectPath.lexically_normal());
    const bool duplicateSourceIsInsideCopy =
        !duplicateManagedRelative.empty() &&
        !duplicateManagedRelative.is_absolute() &&
        *duplicateManagedRelative.begin() != std::filesystem::path("..");
    Require(duplicated.transfer.success && duplicated.transfer.changed &&
            duplicated.projectPath != created.projectPath &&
            originalReopened && duplicateOpened &&
            originalReopened.snapshot.projectId !=
                duplicateOpened.snapshot.projectId &&
            originalResolved && duplicateResolved &&
            std::abs(originalBinding.recipe.preToneExposureEv - 1.25f) <
                0.0001f &&
            std::abs(duplicateBinding.recipe.preToneExposureEv - 2.0f) <
                0.0001f &&
            duplicateOpened.snapshot.embeddedAssets.size() == 1u &&
            !Stack::Project::IsMultiFrameProjectDocument(
                duplicateOpened.snapshot) &&
            duplicateSourceIsInsideCopy,
        "pasting into an existing edited target should preserve the original project and apply edits only to an independent complete copy");

    std::filesystem::remove_all(workspace, error);
}

void TestRawDevelopmentStageCachePolicy() {
    namespace Cache = Stack::Renderer::RawDevelopmentCache;
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe(
            "D:/shoot/card/IMG_0100.dng",
            "IMG_0100.dng");
    recipe.source.relativePathKey = "card/IMG_0100.dng";
    recipe.source.fingerprint = "content-identity-a";
    recipe.source.fileSizeBytes = 42'000'000;
    recipe.source.modifiedTimeTicks = 1234;
    recipe.whiteBalance.mode = RawRecipe::WhiteBalanceMode::CustomMultipliers;
    recipe.whiteBalance.hasMultipliers = true;
    recipe.whiteBalance.multipliers = { 2.0f, 1.0f, 1.4f };
    recipe.preToneExposureEv = 0.65f;
    recipe.localRange.enabled = true;
    recipe.localRange.strength = 1.0f;
    recipe.localRange.points = {
        { -8.0f, 0.0f },
        { -3.0f, 0.8f },
        { 0.0f, 0.0f },
        { 6.0f, 0.0f }
    };
    RawRecipe::RawLocalRangeTargetZone zone;
    zone.id = "zone-cache-contract";
    zone.centerEv = -2.5f;
    zone.deltaEv = 0.7f;
    zone.scope = RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
    zone.seeds = { { 0.35f, 0.55f } };
    recipe.localRange.targetZones = { zone };
    recipe.finishTone.layerJson = RawRecipe::DefaultFinishToneJson();
    recipe.finishTone.layerJson["points"] = nlohmann::json::array({
        { { "x", 0.0f }, { "y", 0.0f } },
        { { "x", 0.5f }, { "y", 0.54f } },
        { { "x", 1.0f }, { "y", 1.0f } }
    });
    recipe.viewTransform.layerJson = RawRecipe::DefaultViewTransformJson();
    recipe.viewTransform.layerJson["contrast"] = 1.1f;

    const auto fingerprint = [&](const RawRecipe::RawDevelopmentRecipe& value,
                                 Cache::Stage stage,
                                 int preview = 1280) {
        return Cache::BuildStageFingerprint(value, preview, stage);
    };
    const std::array<Cache::Stage, 8> stages = {
        Cache::Stage::RawBase,
        Cache::Stage::NeutralPlacement,
        Cache::Stage::RawPlacement,
        Cache::Stage::PostLocalRange,
        Cache::Stage::PostFinishTone,
        Cache::Stage::PostColorWarp,
        Cache::Stage::PostViewTransform,
        Cache::Stage::PostOutputCrop
    };

    RawRecipe::RawDevelopmentRecipe presentationOnly = recipe;
    presentationOnly.source.relativePathKey = "renamed/IMG_0100.dng";
    presentationOnly.source.displayName = "Renamed source";
    for (Cache::Stage stage : stages) {
        Require(
            fingerprint(presentationOnly, stage) == fingerprint(recipe, stage),
            "RAW stage caches should ignore source/UI labels that do not change pixels");
    }

    RawRecipe::RawDevelopmentRecipe changedView = recipe;
    changedView.viewTransform.layerJson["contrast"] = 1.35f;
    for (Cache::Stage stage : stages) {
        const bool downstreamOfView =
            stage == Cache::Stage::PostViewTransform ||
            stage == Cache::Stage::PostOutputCrop;
        Require(
            (fingerprint(changedView, stage) != fingerprint(recipe, stage)) ==
                downstreamOfView,
            "a View edit should invalidate only View and final output pixels");
    }

    RawRecipe::RawDevelopmentRecipe changedCrop = recipe;
    changedCrop.cropRotation.cropEnabled = true;
    changedCrop.cropRotation.cropX = 0.1f;
    changedCrop.cropRotation.cropY = 0.08f;
    changedCrop.cropRotation.cropWidth = 0.75f;
    changedCrop.cropRotation.cropHeight = 0.8f;
    for (Cache::Stage stage : stages) {
        Require(
            (fingerprint(changedCrop, stage) != fingerprint(recipe, stage)) ==
                (stage == Cache::Stage::PostOutputCrop),
            "an output crop edit should reuse every upstream RAW Lab stage");
    }

    RawRecipe::RawDevelopmentRecipe changedFinish = recipe;
    RawRecipe::RawPointCurveComponent cacheCurve;
    cacheCurve.points = {
        { 0.0f, 0.0f, 1 },
        { 0.5f, 0.62f, 1 },
        { 1.0f, 1.0f, 1 }
    };
    RawRecipe::StorePointCurveComponentInFinishToneJson(
        changedFinish.finishTone.layerJson,
        RawRecipe::RawPointCurveChannel::Composite,
        cacheCurve);
    Require(
        fingerprint(changedFinish, Cache::Stage::PostLocalRange) ==
            fingerprint(recipe, Cache::Stage::PostLocalRange) &&
        fingerprint(changedFinish, Cache::Stage::PostFinishTone) !=
            fingerprint(recipe, Cache::Stage::PostFinishTone),
        "a Curve edit should invalidate Finish Tone without replaying Local Range");

    RawRecipe::RawDevelopmentRecipe changedColor = recipe;
    changedColor.colorWarp.enabled = true;
    RawRecipe::RawColorWarpPin colorPin;
    colorPin.id = "cache-color";
    colorPin.sourceA = 0.12f;
    colorPin.sourceB = 0.08f;
    colorPin.targetA = 0.04f;
    colorPin.targetB = 0.18f;
    changedColor.colorWarp.pins = { colorPin };
    Require(
        fingerprint(changedColor, Cache::Stage::PostFinishTone) ==
            fingerprint(recipe, Cache::Stage::PostFinishTone) &&
        fingerprint(changedColor, Cache::Stage::PostColorWarp) !=
            fingerprint(recipe, Cache::Stage::PostColorWarp),
        "a Color edit should reuse Finish Tone pixels and invalidate only Color Warp output");

    RawRecipe::RawDevelopmentRecipe changedZone = recipe;
    changedZone.localRange.targetZones.front().deltaEv = 1.1f;
    Require(
        fingerprint(changedZone, Cache::Stage::RawPlacement) ==
            fingerprint(recipe, Cache::Stage::RawPlacement) &&
        fingerprint(changedZone, Cache::Stage::PostLocalRange) !=
            fingerprint(recipe, Cache::Stage::PostLocalRange) &&
        fingerprint(changedZone, Cache::Stage::PostFinishTone) !=
            fingerprint(recipe, Cache::Stage::PostFinishTone),
        "a Zones edit should reuse pre-Local pixels and invalidate downstream stages");

    RawRecipe::RawDevelopmentRecipe changedExposure = recipe;
    changedExposure.preToneExposureEv += 0.5f;
    Require(
        fingerprint(changedExposure, Cache::Stage::RawBase) ==
            fingerprint(recipe, Cache::Stage::RawBase) &&
        fingerprint(changedExposure, Cache::Stage::NeutralPlacement) ==
            fingerprint(recipe, Cache::Stage::NeutralPlacement) &&
        fingerprint(changedExposure, Cache::Stage::RawPlacement) !=
            fingerprint(recipe, Cache::Stage::RawPlacement) &&
        fingerprint(changedExposure, Cache::Stage::PostLocalRange) !=
            fingerprint(recipe, Cache::Stage::PostLocalRange),
        "RAW Exposure should reuse the neutral base and invalidate every exposed stage");

    RawRecipe::RawDevelopmentRecipe changedDenoise = recipe;
    changedDenoise.rgbDenoise.enabled = true;
    changedDenoise.rgbDenoise.lumaMap.baseMultiplier = 0.65f;
    Require(
        fingerprint(changedDenoise, Cache::Stage::RawBase) ==
            fingerprint(recipe, Cache::Stage::RawBase) &&
        fingerprint(changedDenoise, Cache::Stage::NeutralPlacement) !=
            fingerprint(recipe, Cache::Stage::NeutralPlacement),
        "an RGB denoise edit should reuse demosaic and CFA denoise while invalidating the post-demosaic boundary");
    changedDenoise.rgbDenoise.enabled = false;
    Require(
        fingerprint(changedDenoise, Cache::Stage::RawBase) ==
            fingerprint(recipe, Cache::Stage::RawBase) &&
        fingerprint(changedDenoise, Cache::Stage::NeutralPlacement) ==
            fingerprint(recipe, Cache::Stage::NeutralPlacement),
        "bypassing RGB denoise should return to the neutral placement cache path without replaying sensor-domain work");

    RawRecipe::RawDevelopmentRecipe changedWhiteBalance = recipe;
    changedWhiteBalance.whiteBalance.multipliers[0] += 0.2f;
    for (Cache::Stage stage : stages) {
        Require(
            fingerprint(changedWhiteBalance, stage) != fingerprint(recipe, stage),
            "White Balance should invalidate every cached RAW pixel stage");
    }

    RawRecipe::RawDevelopmentRecipe changedDemosaic = recipe;
    changedDemosaic.technical.demosaicMethod = Raw::DemosaicMethod::Bilinear;
    for (Cache::Stage stage : stages) {
        Require(
            fingerprint(changedDemosaic, stage) != fingerprint(recipe, stage),
            "Demosaic method should invalidate every cached RAW pixel stage");
    }

    RawRecipe::RawDevelopmentRecipe changedOrientation = recipe;
    changedOrientation.cropRotation.flipHorizontally = true;
    for (Cache::Stage stage : stages) {
        Require(
            fingerprint(changedOrientation, stage) != fingerprint(recipe, stage),
            "an orientation edit should invalidate every cached RAW pixel stage");
    }
    for (Cache::Stage stage : stages) {
        Require(
            fingerprint(recipe, stage, 1024) != fingerprint(recipe, stage, 1280),
            "proxy dimensions should participate in every RAW stage cache key");
    }

    RawRecipe::RawDevelopmentRecipe changedSourceTime = recipe;
    ++changedSourceTime.source.modifiedTimeTicks;
    Require(
        Cache::BuildSourceDataIdentity(changedSourceTime.source) !=
            Cache::BuildSourceDataIdentity(recipe.source),
        "decoded RAW caches should reload when the source modification identity changes");
    for (Cache::Stage stage : stages) {
        Require(
            fingerprint(changedSourceTime, stage) != fingerprint(recipe, stage),
            "source modification identity should invalidate every RAW pixel stage");
    }

    const std::size_t preLocalFingerprint =
        fingerprint(recipe, Cache::Stage::RawPlacement);
    const std::size_t selectionFingerprint =
        Cache::BuildLocalRangeSelectionFingerprint(
            recipe.localRange,
            recipe.technical.workingSpace,
            preLocalFingerprint,
            1536);
    Require(
        Cache::BuildLocalRangeSelectionFingerprint(
            changedZone.localRange,
            changedZone.technical.workingSpace,
            preLocalFingerprint,
            1536) == selectionFingerprint,
        "delta-only target edits should reuse the exact connected-area mask");

    RawRecipe::RawDevelopmentRecipe changedReach = recipe;
    changedReach.localRange.targetZones.front().coreHalfWidthEv += 0.2f;
    Require(
        Cache::BuildLocalRangeSelectionFingerprint(
            changedReach.localRange,
            changedReach.technical.workingSpace,
            preLocalFingerprint,
            1536) != selectionFingerprint,
        "target reach edits should rebuild connected-area membership");
    Require(
        Cache::BuildLocalRangeSelectionFingerprint(
            recipe.localRange,
            recipe.technical.workingSpace,
            preLocalFingerprint + 1,
            1536) != selectionFingerprint,
        "connected-area membership must invalidate when upstream pixels change even if a GL texture name is reused");
    Require(
        Cache::BuildLocalRangeSelectionFingerprint(
            recipe.localRange,
            recipe.technical.workingSpace,
            preLocalFingerprint,
            768) != selectionFingerprint,
        "connected-area membership cache keys should include their bounded resolution");
}

void TestRestormerRgbAdapterContract() {
    constexpr int width = 4;
    constexpr int height = 4;
    std::vector<float> source(
        static_cast<std::size_t>(width * height * 4),
        0.0f);
    for (int pixel = 0; pixel < width * height; ++pixel) {
        source[static_cast<std::size_t>(pixel) * 4U + 0U] =
            pixel == 0 ? -0.08f : 0.12f + 0.01f * static_cast<float>(pixel);
        source[static_cast<std::size_t>(pixel) * 4U + 1U] =
            pixel == 1 ? 3.5f : 0.18f + 0.005f * static_cast<float>(pixel);
        source[static_cast<std::size_t>(pixel) * 4U + 2U] = 0.09f;
        source[static_cast<std::size_t>(pixel) * 4U + 3U] =
            0.25f + 0.01f * static_cast<float>(pixel);
    }

    std::vector<float> proxy;
    const Stack::RawRestormer::AdapterResult proxyResult =
        Stack::RawRestormer::BuildInputProxy(
            source,
            width,
            height,
            Raw::RawWorkingSpace::LinearRec2020D65,
            proxy);
    Require(
        proxyResult.ok && proxy.size() ==
            static_cast<std::size_t>(width * height * 3) &&
            std::all_of(proxy.begin(), proxy.end(), [](float value) {
                return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
            }),
        "Restormer input mapping should produce a finite bounded sRGB proxy");

    Stack::RawRecipe::RawRgbDenoiseRecipe settings;
    settings.enabled = true;
    settings.method =
        Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1;
    settings.mapping =
        Stack::RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1;
    settings.colorNoise = 1.0f;
    settings.luminanceNoise = 1.0f;
    settings.detailProtection = 0.0f;
    std::vector<float> identity;
    const Stack::RawRestormer::AdapterResult identityResult =
        Stack::RawRestormer::ApplyOutput(
            source,
            proxy,
            proxy,
            width,
            height,
            Raw::RawWorkingSpace::LinearRec2020D65,
            settings,
            identity);
    Require(
        identityResult.ok && identity == source,
        "Restormer mapping should be exact identity for a neutral model residual");

    std::vector<float> darkSource = source;
    for (int pixel = 0; pixel < width * height; ++pixel) {
        for (int channel = 0; channel < 3; ++channel) {
            darkSource[static_cast<std::size_t>(pixel) * 4U + channel] *=
                0.01f;
        }
    }
    std::vector<float> darkProxy;
    const Stack::RawRestormer::AdapterResult darkProxyResult =
        Stack::RawRestormer::BuildInputProxy(
            darkSource,
            width,
            height,
            Raw::RawWorkingSpace::LinearRec2020D65,
            darkProxy);
    Require(
        darkProxyResult.ok && darkProxyResult.inputExposureGain > 5.0f,
        "Restormer V2 should expose a dark scene only inside the model proxy");
    std::vector<float> darkIdentity;
    const Stack::RawRestormer::AdapterResult darkIdentityResult =
        Stack::RawRestormer::ApplyOutput(
            darkSource,
            darkProxy,
            darkProxy,
            width,
            height,
            Raw::RawWorkingSpace::LinearRec2020D65,
            settings,
            darkIdentity);
    Require(
        darkIdentityResult.ok && darkIdentity == darkSource,
        "Restormer V2 proxy exposure must divide out to exact scene identity");

    std::vector<float> modelOutput = proxy;
    for (std::size_t index = 0; index < modelOutput.size(); index += 3U) {
        modelOutput[index + 0U] =
            std::clamp(modelOutput[index + 0U] + 0.01f, 0.0f, 1.0f);
        modelOutput[index + 2U] =
            std::clamp(modelOutput[index + 2U] - 0.01f, 0.0f, 1.0f);
    }
    std::vector<float> adjusted;
    const Stack::RawRestormer::AdapterResult adjustedResult =
        Stack::RawRestormer::ApplyOutput(
            source,
            proxy,
            modelOutput,
            width,
            height,
            Raw::RawWorkingSpace::LinearRec2020D65,
            settings,
            adjusted);
    Require(
        adjustedResult.ok &&
            std::all_of(adjusted.begin(), adjusted.end(), [](float value) {
                return std::isfinite(value);
            }),
        "Restormer scene-linear mapping should remain finite");
    for (int pixel = 0; pixel < width * height; ++pixel) {
        Require(
            adjusted[static_cast<std::size_t>(pixel) * 4U + 3U] ==
                source[static_cast<std::size_t>(pixel) * 4U + 3U],
            "Restormer mapping must preserve alpha exactly");
    }

    settings.colorNoise = 0.0f;
    settings.luminanceNoise = 0.0f;
    std::vector<float> zeroStrength;
    const Stack::RawRestormer::AdapterResult zeroResult =
        Stack::RawRestormer::ApplyOutput(
            source,
            proxy,
            modelOutput,
            width,
            height,
            Raw::RawWorkingSpace::LinearRec2020D65,
            settings,
            zeroStrength);
    Require(
        zeroResult.ok && zeroStrength == source,
        "Restormer mapping should preserve exact identity at zero strength");
}

void TestRestormerProtocolAndTilingContract() {
    Stack::Restormer::ProtocolRequest request;
    request.requestId = "request-1";
    request.operation = Stack::Restormer::Operation::Denoise;
    request.generation = 42;
    request.modelKind = "real-photo";
    request.quality = Stack::Restormer::Quality::InteractivePreview;
    request.width = 17;
    request.height = 19;
    request.channels = 3;
    request.byteSize =
        static_cast<std::size_t>(request.width * request.height * 3) *
        sizeof(float);
    request.inputMappingName = "Local\\StackRestormer-test-input";
    request.outputMappingName = "Local\\StackRestormer-test-output";
    Stack::Restormer::ProtocolRequest parsed;
    std::string protocolError;
    Require(
        Stack::Restormer::ParseProtocolRequest(
            Stack::Restormer::SerializeProtocolRequest(request),
            parsed,
            protocolError) &&
            parsed.generation == request.generation &&
            parsed.byteSize == request.byteSize,
        "Restormer control protocol should round-trip validated shared-memory metadata");

    nlohmann::json unsafe =
        Stack::Restormer::SerializeProtocolRequest(request);
    unsafe["inputMappingName"] = "Global\\unowned";
    Require(
        !Stack::Restormer::ParseProtocolRequest(
            unsafe, parsed, protocolError),
        "Restormer control protocol should reject unowned shared-memory names");
    nlohmann::json unknownField =
        Stack::Restormer::SerializeProtocolRequest(request);
    unknownField["arbitraryPluginPath"] = "not-allowed";
    Require(
        !Stack::Restormer::ParseProtocolRequest(
            unknownField, parsed, protocolError),
        "Restormer control protocol should reject unknown request fields");
    nlohmann::json wrongType =
        Stack::Restormer::SerializeProtocolRequest(request);
    wrongType["width"] = "not-a-number";
    Require(
        !Stack::Restormer::ParseProtocolRequest(
            wrongType, parsed, protocolError),
        "Restormer control protocol should reject invalid JSON field types");

    const Stack::Restormer::TilePolicy policy { 16, 4 };
    const std::vector<Stack::Restormer::Tile> tiles =
        Stack::Restormer::BuildTiles(31, 23, policy);
    Require(
        tiles.size() > 1 &&
            tiles.front().x == 0 &&
            tiles.front().y == 0 &&
            tiles.back().x + tiles.back().width == 31 &&
            tiles.back().y + tiles.back().height == 23,
        "Restormer odd-sized tile planning should cover every image edge");
    Require(
        Stack::Restormer::ReflectIndex(-1, 5) == 1 &&
            Stack::Restormer::ReflectIndex(5, 5) == 3,
        "Restormer tile padding should use reflection");

    std::vector<float> input(31U * 23U * 3U);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] =
            static_cast<float>((index * 17U) % 101U) / 100.0f;
    }
    std::vector<float> output;
    const Stack::Restormer::TiledInferenceResult tiled =
        Stack::Restormer::RunTiledInference(
            input,
            31,
            23,
            policy,
            [](const float* tileInput,
               int tileWidth,
               int tileHeight,
               float* tileOutput,
               std::string&) {
                const std::size_t count =
                    static_cast<std::size_t>(tileWidth) *
                    static_cast<std::size_t>(tileHeight) * 3U;
                std::copy(tileInput, tileInput + count, tileOutput);
                return true;
            },
            []() { return false; },
            output);
    Require(
        tiled.ok && output.size() == input.size(),
        "Restormer tiled identity inference should complete");
    for (std::size_t index = 0; index < input.size(); ++index) {
        Require(
            std::abs(input[index] - output[index]) < 1.0e-5f,
            "Restormer raised-cosine overlap should not introduce identity seams");
    }
}

void TestRestormerDevelopmentPackageTrustContract() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "stack-restormer-package-contract-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "models", ec);
    std::filesystem::create_directories(root / "licenses", ec);
    {
        std::ofstream(root / "StackModelService.exe", std::ios::binary)
            << "service";
        std::ofstream(root / "runtime.dll", std::ios::binary)
            << "runtime";
        std::ofstream(root / "models" / "real.onnx", std::ios::binary)
            << "model";
        std::ofstream(root / "models" / "gaussian.onnx", std::ios::binary)
            << "gaussian-model";
        std::ofstream(root / "licenses" / "notice.txt", std::ios::binary)
            << "notice";
    }
    auto hash = [](const std::filesystem::path& path) {
        return Stack::RawEvidence::ComputeSourceIdentity(path).sha256;
    };
    const nlohmann::json manifest = {
        { "schemaVersion", 1 },
        { "packageId", "stack-restormer-denoise-v1" },
        { "packageVersion", "1.0.0-dev" },
        { "protocolVersion", 1 },
        { "adapterVersion", Stack::RawRecipe::kRestormerDenoiseAdapterVersion },
        { "developmentPackage", true },
        { "authorizationStatus", "development-only" },
        { "service", {
            { "path", "StackModelService.exe" },
            { "sha256", hash(root / "StackModelService.exe") }
        } },
        { "runtimeArtifacts", nlohmann::json::array({
            {
                { "path", "runtime.dll" },
                { "sha256", hash(root / "runtime.dll") }
            }
        }) },
        { "legalArtifacts", nlohmann::json::array({
            {
                { "path", "licenses/notice.txt" },
                { "sha256", hash(root / "licenses" / "notice.txt") }
            }
        }) },
        { "models", nlohmann::json::array({
            {
                { "kind", "real-photo" },
                { "path", "models/real.onnx" },
                { "sha256", hash(root / "models" / "real.onnx") },
                { "inputName", "input" },
                { "outputName", "output" }
            },
            {
                { "kind", "gaussian-blind" },
                { "path", "models/gaussian.onnx" },
                { "sha256", hash(root / "models" / "gaussian.onnx") },
                { "inputName", "input" },
                { "outputName", "output" }
            }
        }) }
    };
    {
        std::ofstream output(root / "manifest.json", std::ios::binary);
        output << manifest.dump(2);
    }

    Stack::Restormer::ValidationRequest validationRequest;
    validationRequest.method =
        Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1;
    Stack::Restormer::TrustPolicy releasePolicy;
    Require(
        !Stack::Restormer::ValidatePackage(
            root, validationRequest, releasePolicy).ok,
        "Release trust should reject a local Restormer development package");
    Stack::Restormer::TrustPolicy developmentPolicy;
    developmentPolicy.allowDevelopmentPackage = true;
    const Stack::Restormer::ValidationResult accepted =
        Stack::Restormer::ValidatePackage(
            root, validationRequest, developmentPolicy);
    Require(
        accepted.ok &&
            accepted.selectedModel.kind ==
                Stack::Restormer::ModelKind::RealPhoto,
        "Explicit development trust should accept a complete hashed local package");
    validationRequest.method =
        Stack::RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1;
    const Stack::Restormer::ValidationResult acceptedGaussian =
        Stack::Restormer::ValidatePackage(
            root, validationRequest, developmentPolicy);
    Require(
        acceptedGaussian.ok &&
            acceptedGaussian.selectedModel.kind ==
                Stack::Restormer::ModelKind::GaussianBlind,
        "One approved package should expose the frozen Gaussian Blind model");
    validationRequest.method =
        Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1;
    {
        std::ofstream(root / "models" / "gaussian.onnx", std::ios::binary)
            << "tampered-model";
    }
    Require(
        !Stack::Restormer::ValidatePackage(
            root, validationRequest, developmentPolicy).ok,
        "Restormer package validation should reject a tampered secondary model");
    {
        std::ofstream(root / "models" / "gaussian.onnx", std::ios::binary)
            << "gaussian-model";
    }

    nlohmann::json unsafeManifest = manifest;
    unsafeManifest["models"][0]["path"] = "../outside.onnx";
    {
        std::ofstream output(root / "manifest.json", std::ios::binary);
        output << unsafeManifest.dump(2);
    }
    Require(
        !Stack::Restormer::ValidatePackage(
            root, validationRequest, developmentPolicy).ok,
        "Restormer package validation should reject path traversal");
    std::filesystem::remove_all(root, ec);

    const char* externalPackage =
        std::getenv("STACK_TEST_RESTORMER_PACKAGE");
    if (externalPackage != nullptr && externalPackage[0] != '\0') {
        const std::filesystem::path packageRoot(externalPackage);
        validationRequest = {};
        validationRequest.method =
            Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1;
        const Stack::Restormer::ValidationResult real =
            Stack::Restormer::ValidatePackage(
                packageRoot, validationRequest, developmentPolicy);
        Require(
            real.ok &&
                real.selectedModel.kind ==
                    Stack::Restormer::ModelKind::RealPhoto,
            "The external Restormer development package should validate its Real Photo model");
        validationRequest.method =
            Stack::RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1;
        const Stack::Restormer::ValidationResult gaussian =
            Stack::Restormer::ValidatePackage(
                packageRoot, validationRequest, developmentPolicy);
        Require(
            gaussian.ok &&
                gaussian.selectedModel.kind ==
                    Stack::Restormer::ModelKind::GaussianBlind,
            "The external Restormer development package should validate its Gaussian Blind model");
    }
}

void TestRawLabViewTransformReferenceMatchesShaderFormula() {
    auto shaderFormula = [](
                             double input,
                             double exposure,
                             double blackEv,
                             double whiteEv,
                             double middleGrey,
                             double shoulder,
                             double toe,
                             double contrast,
                             double contrastPivotEv) {
        const double black = middleGrey * std::exp2(blackEv);
        const double white = middleGrey * std::exp2(whiteEv);
        double x = std::max(0.0, input * std::exp2(exposure) - black);
        double normalized = x / std::max(0.000001, white - black);
        const double safeContrast = std::max(0.05, contrast);
        const double pivotInput = middleGrey * std::exp2(contrastPivotEv);
        const double pivotNormalized = std::max(
            0.000001,
            (pivotInput - black) / std::max(0.000001, white - black));
        normalized = pivotNormalized * std::pow(
            std::max(0.0, normalized) / pivotNormalized,
            safeContrast);
        const double clampedToe = std::clamp(toe, 0.0, 1.0);
        normalized =
            normalized * (1.0 - clampedToe) +
            ((normalized + clampedToe * normalized / (normalized + 0.18)) /
             (1.0 + clampedToe)) *
                clampedToe;
        const double safeShoulder = std::max(0.001, shoulder);
        const double mapped = normalized / (normalized + safeShoulder);
        const double whiteMapped = 1.0 / (1.0 + safeShoulder);
        return std::clamp(mapped / std::max(0.0001, whiteMapped), 0.0, 1.0);
    };

    struct Sample {
        float input;
        float exposure;
        float blackEv;
        float whiteEv;
        float middleGrey;
        float shoulder;
        float toe;
        float contrast;
        float contrastPivotEv;
    };
    const Sample samples[] = {
        { 0.0f, 0.0f, -8.0f, 4.0f, 0.18f, 0.45f, 0.18f, 1.0f, 0.0f },
        { 0.18f, 0.0f, -8.0f, 4.0f, 0.18f, 0.45f, 0.18f, 1.0f, 0.0f },
        { 2.88f, 0.0f, -8.0f, 4.0f, 0.18f, 0.45f, 0.18f, 1.0f, 0.0f },
        { 0.04f, 1.25f, -10.0f, 6.0f, 0.20f, 0.72f, 0.35f, 1.4f, -0.5f },
        { 8.0f, -0.75f, -6.0f, 3.0f, 0.16f, 0.12f, 0.0f, 0.72f, 1.0f }
    };
    constexpr double tolerance = 2.0e-6;
    for (const Sample& sample : samples) {
        const float actual = Stack::RawRecipe::EvaluateViewTransformDisplayLuma(
            sample.input,
            sample.exposure,
            sample.blackEv,
            sample.whiteEv,
            sample.middleGrey,
            sample.shoulder,
            sample.toe,
            sample.contrast,
            sample.contrastPivotEv);
        const double expected = shaderFormula(
            sample.input,
            sample.exposure,
            sample.blackEv,
            sample.whiteEv,
            sample.middleGrey,
            sample.shoulder,
            sample.toe,
            sample.contrast,
            sample.contrastPivotEv);
        Require(
            std::abs(static_cast<double>(actual) - expected) <= tolerance,
            "RAW Lab View graph CPU evaluator should match the shader contrast model within 2e-6");
    }

    const nlohmann::json defaultView =
        Stack::RawRecipe::DefaultViewTransformJson();
    Require(
        defaultView.value("contrastModel", std::string()) ==
                Stack::RawRecipe::kViewContrastModelPivotedLogV2 &&
            std::abs(defaultView.value("contrastPivotEv", 99.0f)) < 0.0001f,
        "new View Transforms should use a middle-grey pivoted contrast model");

    const float pivotLowContrast =
        Stack::RawRecipe::EvaluateViewTransformDisplayLuma(
            0.18f, 0.0f, -8.0f, 4.0f, 0.18f, 0.45f, 0.18f,
            0.6f, 0.0f);
    const float pivotHighContrast =
        Stack::RawRecipe::EvaluateViewTransformDisplayLuma(
            0.18f, 0.0f, -8.0f, 4.0f, 0.18f, 0.45f, 0.18f,
            1.8f, 0.0f);
    Require(
        std::abs(pivotLowContrast - pivotHighContrast) <= 2.0e-6f,
        "pivoted View contrast should keep the authored pivot luminance fixed");
}

void TestRawImageAnalysisPercentilesAndFallbackGuards() {
    using Stack::RawAnalysis::AnalysisStageStatus;

    Stack::RawAnalysis::PercentileStats stats =
        Stack::RawAnalysis::BuildPercentileStatsFromLumas(
            { 0.01f, 0.02f, 0.18f, 1.0f, 4.0f },
            87.5f,
            AnalysisStageStatus::Complete,
            "test stats");
    Require(stats.valid, "RAW image analysis percentile stats should be valid for non-empty luma samples");
    Require(stats.status == AnalysisStageStatus::Complete, "RAW image analysis should preserve stage status");
    Require(std::abs(stats.p50Luma - 0.18f) < 0.0001f, "RAW image analysis median luma should be stable");
    Require(std::abs(stats.p50Ev - Stack::RawAnalysis::SafeLog2Luma(0.18f)) < 0.0001f,
        "RAW image analysis median EV should use safe log2 luma");
    Require(stats.dynamicRangeEv > 8.0f, "RAW image analysis should report broad percentile dynamic range");
    Require(std::abs(stats.validPixelPercent - 87.5f) < 0.0001f,
        "RAW image analysis should preserve valid pixel percentage");

    Stack::RawAnalysis::CurrentFrameInputStats currentFrameStats;
    currentFrameStats.valid = true;
    currentFrameStats.p001Luma = 0.005f;
    currentFrameStats.p01Luma = 0.01f;
    currentFrameStats.p05Luma = 0.03f;
    currentFrameStats.p50Luma = 0.18f;
    currentFrameStats.p95Luma = 1.2f;
    currentFrameStats.p99Luma = 2.0f;
    currentFrameStats.p999Luma = 3.0f;
    currentFrameStats.logAverageLuma = 0.16f;
    currentFrameStats.dynamicRangeEv = 7.64f;
    currentFrameStats.validPixelPercent = 100.0f;
    currentFrameStats.hdrPixelPercent = 2.5f;
    currentFrameStats.displayClipPercent = 0.25f;

    const Stack::RawAnalysis::RawImageAnalysis analysis =
        Stack::RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(currentFrameStats, "raw/test.dng");
    Require(analysis.valid, "RAW current-frame analysis should be valid when texture stats are valid");
    Require(analysis.currentFrameStats.valid, "RAW current-frame stats should be populated");
    Require(analysis.currentFrameStats.status == AnalysisStageStatus::Complete,
        "RAW current-frame analysis should be marked complete for rendered stats");
    Require(analysis.technicalStats.status == AnalysisStageStatus::Unavailable,
        "RAW technical analysis should remain unavailable for rendered fallback stats");
    Require(analysis.highlight.sensorStatus == AnalysisStageStatus::Unavailable,
        "RAW sensor clipping should remain unavailable without sensor-domain analysis");
    Require(analysis.highlight.displayStatus == AnalysisStageStatus::Complete,
        "RAW display clipping can be complete for rendered texture stats");
    Require(analysis.highlight.blocksPositiveRawExposure,
        "RAW fallback analysis should block positive RAW exposure recommendations");
    Require(std::abs(analysis.currentFrameStats.p99Luma - currentFrameStats.p99Luma) < 0.0001f,
        "RAW current-frame analysis should preserve render percentile luma");
}

Stack::RawAnalysis::RawImageAnalysis BuildAutoBaseTestAnalysis(
    float p01Ev,
    float p05Ev,
    float p50Ev,
    float p99Ev,
    float p999Ev,
    float dynamicRangeEv,
    float hdrPercent = 0.0f,
    float displayClipPercent = 0.0f,
    float anySensorClipPercent = 0.0f,
    float allSensorClipPercent = 0.0f,
    bool partialClipColorRisk = false,
    bool blocksPositiveRawExposure = false) {
    Stack::RawAnalysis::RawImageAnalysis analysis;
    analysis.valid = true;
    analysis.sourceKey = "raw/auto-base-test.dng";
    analysis.currentFrameStats.valid = true;
    analysis.currentFrameStats.status = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.currentFrameStats.p01Ev = p01Ev;
    analysis.currentFrameStats.p05Ev = p05Ev;
    analysis.currentFrameStats.p50Ev = p50Ev;
    analysis.currentFrameStats.p99Ev = p99Ev;
    analysis.currentFrameStats.p999Ev = p999Ev;
    analysis.currentFrameStats.p01Luma = std::exp2(p01Ev);
    analysis.currentFrameStats.p05Luma = std::exp2(p05Ev);
    analysis.currentFrameStats.p50Luma = std::exp2(p50Ev);
    analysis.currentFrameStats.p99Luma = std::exp2(p99Ev);
    analysis.currentFrameStats.p999Luma = std::exp2(p999Ev);
    analysis.currentFrameStats.dynamicRangeEv = dynamicRangeEv;
    analysis.currentFrameStats.validPixelPercent = 100.0f;
    analysis.technicalStats = analysis.currentFrameStats;
    analysis.technicalStats.status = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.valid = true;
    analysis.highlight.sensorStatus = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.displayStatus = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.hdrPixelPercent = hdrPercent;
    analysis.highlight.displayClipPercent = displayClipPercent;
    analysis.highlight.anyChannelClipPercent = anySensorClipPercent;
    analysis.highlight.allChannelClipPercent = allSensorClipPercent;
    analysis.highlight.partialClipColorRisk = partialClipColorRisk;
    analysis.highlight.severeSensorClip = allSensorClipPercent > 0.005f;
    analysis.highlight.blocksPositiveRawExposure = blocksPositiveRawExposure;
    return analysis;
}

Stack::RawAutoBase::WhiteBalanceCandidateEvidence BuildStrongGrayWorldWhiteBalanceEvidence() {
    namespace RawAutoBase = Stack::RawAutoBase;

    Stack::RawAnalysis::PercentileStats wbStats;
    wbStats.valid = true;
    wbStats.p05Luma = 0.05f;
    wbStats.p95Luma = 0.95f;
    std::vector<RawAutoBase::WhiteBalanceSample> warmNeutralSamples(64);
    for (RawAutoBase::WhiteBalanceSample& sample : warmNeutralSamples) {
        sample.r = 1.4f;
        sample.g = 1.0f;
        sample.b = 0.7f;
        sample.luma = 0.45f;
    }
    return RawAutoBase::BuildWhiteBalanceCandidateEvidence(
        warmNeutralSamples,
        wbStats,
        RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld);
}

Stack::RawAutoBase::LocalSuggestionAnalysisImage BuildStrongWhiteBalanceLocalImage() {
    namespace RawAutoBase = Stack::RawAutoBase;

    RawAutoBase::LocalSuggestionAnalysisImage image;
    image.valid = true;
    image.sceneLinearBeforeLocalRange = true;
    image.width = 8;
    image.height = 8;
    image.pixels.resize(64);
    for (RawAutoBase::LocalSuggestionPixel& pixel : image.pixels) {
        pixel.valid = true;
        pixel.r = 1.4f;
        pixel.g = 1.0f;
        pixel.b = 0.7f;
    }
    return image;
}

const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine* FindStartPointViewLine(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView& view,
    const std::string& label) {
    for (const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine& line : view.lines) {
        if (line.label == label) {
            return &line;
        }
    }
    return nullptr;
}

bool StartPointViewHasLineValue(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView& view,
    const std::string& label,
    const std::string& value) {
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine* line =
        FindStartPointViewLine(view, label);
    return line != nullptr && line->value == value;
}

bool StartPointViewLineValueContains(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView& view,
    const std::string& label,
    const std::string& fragment) {
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine* line =
        FindStartPointViewLine(view, label);
    return line != nullptr && line->value.find(fragment) != std::string::npos;
}

bool StartPointViewLineDetailContains(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView& view,
    const std::string& label,
    const std::string& fragment) {
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine* line =
        FindStartPointViewLine(view, label);
    return line != nullptr && line->detail.find(fragment) != std::string::npos;
}

bool StartPointViewContainsDetail(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView& view,
    const std::string& fragment) {
    for (const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine& line : view.lines) {
        if (line.detail.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool StartPointViewHasWarningDetail(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticsView& view,
    const std::string& fragment) {
    for (const Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticLine& line : view.lines) {
        if (line.severity == Stack::RawAutoStartPoint::RawAutoStartPointDiagnosticSeverity::Warning &&
            line.detail.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const Stack::RawAutoStartPoint::RawAutoStartPointCandidate* FindStartPointCandidate(
    const Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics& diagnostics,
    Stack::RawAutoStartPoint::RawAutoStartPointCandidateKind kind) {
    const auto it = std::find_if(
        diagnostics.candidates.begin(),
        diagnostics.candidates.end(),
        [kind](const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate) {
            return candidate.kind == kind;
        });
    return it == diagnostics.candidates.end() ? nullptr : &*it;
}

const Stack::RawAutoStartPoint::RawAutoStartPointStageDiagnostics* FindStartPointStage(
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate,
    Stack::RawAutoStartPoint::RawAutoStartPointStage stage) {
    const auto it = std::find_if(
        candidate.stageDiagnostics.begin(),
        candidate.stageDiagnostics.end(),
        [stage](const Stack::RawAutoStartPoint::RawAutoStartPointStageDiagnostics& item) {
            return item.stage == stage;
        });
    return it == candidate.stageDiagnostics.end() ? nullptr : &*it;
}

const Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest*
FindStartPointRenderRequest(
    const Stack::RawAutoStartPoint::RawAutoStartPointCandidate& candidate,
    Stack::RawAutoStartPoint::RawAutoStartPointStage stage) {
    const auto it = std::find_if(
        candidate.renderRequests.begin(),
        candidate.renderRequests.end(),
        [stage](const Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest& item) {
            return item.stage == stage;
        });
    return it == candidate.renderRequests.end() ? nullptr : &*it;
}

void TestRawAutoBaseViewTransformFit() {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0004.dng", "IMG_0004.dng");
    recipe.preToneExposureEv = 0.75f;

    const Stack::RawAnalysis::RawImageAnalysis normal =
        BuildAutoBaseTestAnalysis(-4.0f, -2.5f, -1.0f, 2.5f, 3.0f, 7.0f);
    const Stack::RawAutoBase::ViewFitDecision normalDecision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(normal, recipe);
    Require(normalDecision.canApply, "RAW Auto Base should apply when current-frame stats are valid");
    Require(std::abs(normalDecision.fit.middleGrey - 0.5f) < 0.001f,
        "RAW Auto Base should anchor middle grey near p50 luma");
    Require(normalDecision.fit.whiteEv >= 2.5f && normalDecision.fit.whiteEv <= 10.0f,
        "RAW Auto Base white EV should stay within the researched clamp");
    Require(normalDecision.fit.blackEv <= -4.0f && normalDecision.fit.blackEv >= -14.0f,
        "RAW Auto Base black EV should store a negative offset within the researched clamp");

    const Stack::RawAnalysis::RawImageAnalysis darkSky =
        BuildAutoBaseTestAnalysis(-10.0f, -8.0f, -5.0f, 2.0f, 4.0f, 14.0f, 2.0f, 1.0f);
    const Stack::RawAutoBase::ViewTransformFit darkSkyFit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(darkSky);
    Require(darkSkyFit.valid, "RAW Auto Base should fit dark foreground / bright sky stats");
    Require(darkSkyFit.shoulder > normalDecision.fit.shoulder,
        "RAW Auto Base should increase shoulder when highlight risk is higher");
    Require(std::abs(recipe.preToneExposureEv - 0.75f) < 0.001f,
        "RAW Auto Base fit computation should not mutate RAW exposure");

    const Stack::RawAnalysis::RawImageAnalysis lowRange =
        BuildAutoBaseTestAnalysis(-1.0f, -0.8f, -0.5f, 0.0f, 0.2f, 1.2f);
    const Stack::RawAutoBase::ViewTransformFit lowRangeFit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(lowRange);
    Require(lowRangeFit.valid, "RAW Auto Base should fit low dynamic range stats");
    Require(std::abs(lowRangeFit.blackEv + 4.0f) < 0.001f,
        "RAW Auto Base should avoid extreme black EV for low dynamic range images");
    Require(std::abs(lowRangeFit.whiteEv - 2.5f) < 0.001f,
        "RAW Auto Base should avoid extreme white EV for low dynamic range images");

    const Stack::RawAnalysis::RawImageAnalysis clipped =
        BuildAutoBaseTestAnalysis(-5.0f, -3.0f, -1.0f, 4.0f, 5.0f, 11.0f, 6.0f, 4.0f, 0.10f);
    const Stack::RawAutoBase::ViewTransformFit clippedFit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(clipped);
    Require(clippedFit.valid, "RAW Auto Base should fit clipped highlight stats");
    Require(clippedFit.whiteMarginEv > 0.35f && clippedFit.shoulder >= 0.55f,
        "RAW Auto Base should widen white margin and shoulder for highlight risk");

    Stack::RawAnalysis::RawImageAnalysis invalid;
    const Stack::RawAutoBase::ViewFitDecision invalidDecision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(invalid, recipe);
    Require(!invalidDecision.canApply, "RAW Auto Base should not apply without valid stats");

    Stack::RawAutoBase::ApplyViewTransformFitToRecipe(recipe, normalDecision.fit);
    Require(std::abs(recipe.preToneExposureEv - 0.75f) < 0.001f,
        "RAW Auto Base application should leave RAW Exposure unchanged");
    Require(std::abs(recipe.viewTransform.layerJson.value("middleGrey", 0.0f) - normalDecision.fit.middleGrey) < 0.001f,
        "RAW Auto Base application should write the fitted View Transform middle grey");
}

void TestRawAutoBaseRecommendations() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAnalysis = Stack::RawAnalysis;
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");

    const RawAnalysis::RawImageAnalysis darkSafe =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -1.0f, -0.8f, 7.2f);
    const RawAutoBase::RawExposureRecommendation darkExposure =
        RawAutoBase::BuildRawExposureRecommendation(darkSafe, recipe);
    Require(darkExposure.valid && darkExposure.deltaEv > 0.0f,
        "RAW exposure recommendations should suggest a positive lift for dark safe images");

    const RawAnalysis::RawImageAnalysis clippedDark =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.5f, -0.2f, 8.0f, 0.0f, 0.0f, 0.08f, 0.0f, false, true);
    const RawAutoBase::RawExposureRecommendation clippedExposure =
        RawAutoBase::BuildRawExposureRecommendation(clippedDark, recipe);
    Require(clippedExposure.valid && clippedExposure.deltaEv > 0.0f,
        "RAW exposure recommendations should still report a blocked lift when dark content has highlight risk");
    Require(clippedExposure.blockedByHighlightRisk && !clippedExposure.autoApplyAllowed,
        "RAW exposure recommendations should not auto-apply positive exposure when highlight risk blocks it");

    const RawAnalysis::RawImageAnalysis hdrScene =
        BuildAutoBaseTestAnalysis(-10.0f, -8.0f, -6.0f, 3.5f, 4.0f, 14.0f, 6.0f, 3.0f, 0.0f, 0.0f, true, false);
    const RawAutoBase::RawExposureRecommendation hdrExposure =
        RawAutoBase::BuildRawExposureRecommendation(hdrScene, recipe);
    Require(hdrExposure.valid && hdrExposure.confidence < 0.85f && !hdrExposure.autoApplyAllowed,
        "Low-confidence HDR RAW exposure recommendations should remain suggestion-only");

    const RawAnalysis::RawImageAnalysis smallDelta =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -2.9f, -0.2f, 0.0f, 5.0f);
    const RawAutoBase::RawExposureRecommendation smallExposure =
        RawAutoBase::BuildRawExposureRecommendation(smallDelta, recipe);
    Require(smallExposure.valid && smallExposure.autoApplyAllowed,
        "Small high-confidence RAW exposure deltas should be marked safe for future opt-in automation");
    Require(std::abs(recipe.preToneExposureEv) < 0.001f,
        "Building RAW exposure recommendations should not mutate the visible recipe");

    RawAnalysis::PercentileStats wbStats;
    wbStats.valid = true;
    wbStats.p05Luma = 0.05f;
    wbStats.p95Luma = 0.95f;
    std::vector<RawAutoBase::WhiteBalanceSample> warmNeutralSamples(64);
    for (RawAutoBase::WhiteBalanceSample& sample : warmNeutralSamples) {
        sample.r = 1.4f;
        sample.g = 1.0f;
        sample.b = 0.7f;
        sample.luma = 0.45f;
    }
    const RawAutoBase::WhiteBalanceCandidateEvidence grayWorld =
        RawAutoBase::BuildWhiteBalanceCandidateEvidence(
            warmNeutralSamples,
            wbStats,
            RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld);
    Require(grayWorld.valid && grayWorld.neutralResidualAfter < grayWorld.neutralResidualBefore,
        "Gray World WB evidence should reduce neutral residual for a consistent neutral cast");

    RawAnalysis::RawImageAnalysis cameraWbAnalysis = smallDelta;
    cameraWbAnalysis.metadata.hasCameraWhiteBalance = true;
    cameraWbAnalysis.metadata.cameraWbR = 2.0f;
    cameraWbAnalysis.metadata.cameraWbG = 1.0f;
    cameraWbAnalysis.metadata.cameraWbB = 1.4f;
    const RawAutoBase::WhiteBalanceRecommendation cameraWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(cameraWbAnalysis, recipe, &grayWorld);
    Require(cameraWb.valid && cameraWb.alternateCandidateAvailable && !cameraWb.autoApplyAllowed,
        "Alternate WB should remain suggestion-only when camera/as-shot WB exists");

    RawAnalysis::RawImageAnalysis noCameraWbAnalysis = smallDelta;
    const RawAutoBase::WhiteBalanceRecommendation noCameraWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(noCameraWbAnalysis, recipe, &grayWorld);
    Require(noCameraWb.valid && noCameraWb.autoApplyAllowed,
        "Alternate WB can be marked auto-apply safe when camera WB is absent and neutral evidence is strong");

    const RawAutoBase::LocalSuggestionAnalysisImage wbLocalImage =
        BuildStrongWhiteBalanceLocalImage();
    const RawAutoBase::AutoBaseRecommendations localImageWbRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(
            noCameraWbAnalysis,
            recipe,
            nullptr,
            &wbLocalImage);
    Require(
        localImageWbRecommendations.whiteBalance.valid &&
            localImageWbRecommendations.whiteBalance.alternateCandidateAvailable &&
            localImageWbRecommendations.whiteBalance.autoApplyAllowed,
        "Auto Base recommendations should derive strong Suggested WB evidence from the pre-Local-Range RGB readback when camera WB is absent");

    std::vector<RawAutoBase::WhiteBalanceSample> fewEligibleSamples(100);
    for (std::size_t index = 0; index < fewEligibleSamples.size(); ++index) {
        fewEligibleSamples[index].r = index == 0 ? 1.2f : 1.0f;
        fewEligibleSamples[index].g = index == 0 ? 1.0f : 0.1f;
        fewEligibleSamples[index].b = index == 0 ? 0.8f : 0.1f;
        fewEligibleSamples[index].luma = 0.45f;
    }
    const RawAutoBase::WhiteBalanceCandidateEvidence fewEligible =
        RawAutoBase::BuildWhiteBalanceCandidateEvidence(
            fewEligibleSamples,
            wbStats,
            RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld);
    const RawAutoBase::WhiteBalanceRecommendation fewEligibleWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(noCameraWbAnalysis, recipe, &fewEligible);
    Require(fewEligibleWb.confidence < 0.85f && !fewEligibleWb.autoApplyAllowed,
        "Few eligible neutral pixels should lower alternate WB confidence");

    std::vector<RawAutoBase::WhiteBalanceSample> extremeGainSamples(64);
    for (RawAutoBase::WhiteBalanceSample& sample : extremeGainSamples) {
        sample.r = 0.4f;
        sample.g = 1.0f;
        sample.b = 1.0f;
        sample.luma = 0.55f;
    }
    const RawAutoBase::WhiteBalanceCandidateEvidence extremeEvidence =
        RawAutoBase::BuildWhiteBalanceCandidateEvidence(
            extremeGainSamples,
            wbStats,
            RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld);
    const RawAutoBase::WhiteBalanceRecommendation extremeWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(noCameraWbAnalysis, recipe, &extremeEvidence);
    Require(extremeEvidence.candidateGainsAreExtreme && extremeWb.confidence < 0.85f,
        "Extreme alternate WB gains should lower confidence");

    const RawAnalysis::RawImageAnalysis partialClip =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -1.0f, 2.0f, 3.0f, 8.0f, 0.0f, 0.0f, 0.12f, 0.01f, true, true);
    const RawAutoBase::HighlightRecommendation partialHighlight =
        RawAutoBase::BuildHighlightRecommendation(partialClip);
    Require(partialHighlight.recommendReconstruction && partialHighlight.recommendAchromaticClip,
        "Partial channel clipping should recommend reconstruction and achromatic highlight handling");

    const RawAnalysis::RawImageAnalysis allChannelClip =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -1.0f, 2.0f, 3.0f, 8.0f, 0.0f, 0.0f, 0.12f, 0.08f, false, true);
    const RawAutoBase::HighlightRecommendation allChannelHighlight =
        RawAutoBase::BuildHighlightRecommendation(allChannelClip);
    Require(allChannelClip.highlight.severeSensorClip && allChannelHighlight.recommendReconstruction,
        "All-channel clipping should be treated as severe sensor clipping");

    const RawAnalysis::RawImageAnalysis displayClipOnly =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -1.0f, 2.0f, 3.0f, 8.0f, 3.0f, 2.5f);
    const RawAutoBase::HighlightRecommendation displayHighlight =
        RawAutoBase::BuildHighlightRecommendation(displayClipOnly);
    Require(displayHighlight.recommendProtectiveViewShoulder && !displayHighlight.recommendReconstruction,
        "Display clipping should recommend view protection without claiming sensor reconstruction is needed");

    const RawAutoBase::RawExposureRecommendation blockedExposure =
        RawAutoBase::BuildRawExposureRecommendation(partialClip, recipe);
    Require(blockedExposure.blockedByHighlightRisk && !blockedExposure.autoApplyAllowed,
        "Highlight risk should block positive RAW exposure automation");
}

void TestRawAutoStartPointDiagnosticsViews() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAutoStartPoint = Stack::RawAutoStartPoint;
    namespace RawRecipe = Stack::RawRecipe;

    const std::string sourceKey = "Day 1/IMG_1234.ARW";
    const RawAutoStartPoint::RawAutoStartPointDiagnostics unavailable =
        RawAutoStartPoint::MakeUnavailableDiagnostics(
            sourceKey,
            "No RAW workspace source is active.");
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView unavailableView =
        RawAutoStartPoint::BuildDiagnosticsView(unavailable);
    Require(unavailableView.title == "Build Starting Point Diagnostics",
        "Unavailable Starting Point diagnostics should keep the default diagnostics title");
    Require(StartPointViewHasLineValue(unavailableView, "Source", sourceKey),
        "Unavailable Starting Point diagnostics should include source attribution");
    Require(StartPointViewHasLineValue(unavailableView, "Build Starting Point", "Unavailable"),
        "Unavailable Starting Point diagnostics should expose the unavailable state");
    const nlohmann::json unavailableJson = RawAutoStartPoint::SerializeDiagnostics(unavailable);
    Require(unavailableJson.value("sourceKey", std::string()) == sourceKey,
        "Serialized unavailable Starting Point diagnostics should preserve sourceKey");
    Require(unavailableJson["uiView"]["lines"].is_array() &&
            !unavailableJson["uiView"]["lines"].empty(),
        "Serialized unavailable Starting Point diagnostics should include UI-readable lines");

    RawAutoStartPoint::RawAutoStartPointDiagnostics fallback;
    fallback.valid = true;
    fallback.dryRunOnly = false;
    fallback.appliedRecipeValues = false;
    fallback.requestedIntent = RawAutoStartPoint::RawAutoStartPointIntent::Balanced;
    fallback.hasSelectedCandidate = true;
    fallback.selectedCandidateKind = RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    fallback.sourceKey = sourceKey;
    fallback.statusMessage = "Fallback diagnostics view is synthesized for tests.";
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView fallbackView =
        RawAutoStartPoint::BuildDiagnosticsView(fallback);
    Require(StartPointViewHasLineValue(fallbackView, "State", "Ready"),
        "Synthesized Starting Point diagnostics should report ready state when diagnostics are valid");
    Require(StartPointViewHasLineValue(fallbackView, "Mode", "Balanced"),
        "Synthesized Starting Point diagnostics should render the requested intent label");
    Require(StartPointViewHasLineValue(fallbackView, "Selected", "Base"),
        "Synthesized Starting Point diagnostics should render selected candidate labels");
    Require(StartPointViewLineDetailContains(
                fallbackView,
                "Recipe writes",
                "Explicit starting-point actions write visible recipe controls only."),
        "Synthesized Starting Point diagnostics should explain visible-control recipe writes");
    Require(!StartPointViewContainsDetail(fallbackView, "future-only"),
        "Synthesized Starting Point diagnostics should not use stale future-only wording");

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_7777.DNG", "IMG_7777.DNG");
    recipe.preToneExposureEv = -0.25f;
    Stack::RawAnalysis::RawImageAnalysis analysis =
        BuildAutoBaseTestAnalysis(-6.0f, -4.5f, -2.5f, 0.2f, 0.5f, 5.5f);
    analysis.sourceKey = "Day 2/IMG_7777.DNG";
    const RawAutoBase::AutoBaseRecommendations recommendations =
        RawAutoBase::BuildAutoBaseRecommendations(analysis, recipe);

    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRun =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            RawAutoStartPoint::RawAutoStartPointDiagnostics(),
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView dryRunView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRun);
    Require(dryRun.valid && dryRun.dryRunOnly && !dryRun.appliedRecipeValues,
        "Dry-run Starting Point diagnostics should remain diagnostics-only");
    Require(dryRunView.title == "Build Starting Point Dry Run",
        "Dry-run Starting Point diagnostics should expose the dry-run title");
    Require(StartPointViewHasLineValue(dryRunView, "Source", analysis.sourceKey),
        "Dry-run Starting Point diagnostics should include source attribution");
    Require(StartPointViewHasLineValue(dryRunView, "Dry run", "Yes"),
        "Dry-run Starting Point diagnostics should explicitly label the dry-run state");
    Require(StartPointViewHasLineValue(dryRunView, "Recipe writes", "None"),
        "Dry-run Starting Point diagnostics should report no recipe writes");
    Require(StartPointViewHasLineValue(dryRunView, "Stage evidence", "0"),
        "Dry-run Starting Point diagnostics should expose missing stage-evidence count");
    Require(StartPointViewHasLineValue(dryRunView, "Build Starting Point action", "Ready"),
        "Dry-run Starting Point diagnostics should summarize Build Starting Point action readiness");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Build Starting Point action",
                "Suggested WB"),
        "Dry-run Starting Point readiness should name optional Suggested WB in the visible controls it can write");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Build Starting Point action",
                "Dry-run diagnostics do not apply recipe values"),
        "Dry-run Starting Point readiness should remain non-mutating");
    Require(StartPointViewHasLineValue(dryRunView, "Add Local Range action", "No candidate"),
        "Dry-run Starting Point diagnostics should report unavailable Balanced Local action readiness");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Add Local Range action",
                "visible Local Range candidate"),
        "Dry-run Starting Point Local Range readiness should explain the visible-candidate gate");
    Require(StartPointViewHasLineValue(dryRunView, "Add Mild Tone action", "No candidate"),
        "Dry-run Starting Point diagnostics should report unavailable Mild Tone action readiness");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Add Mild Tone action",
                "visible Finish Tone candidate"),
        "Dry-run Starting Point Mild Tone readiness should explain the visible-candidate gate");
    Require(StartPointViewLineValueContains(dryRunView, "CurrentFit visible controls", "Display Fit"),
        "Dry-run Starting Point diagnostics should summarize CurrentFit visible controls");
    Require(StartPointViewLineValueContains(dryRunView, "Base visible controls", "Display Fit"),
        "Dry-run Starting Point diagnostics should summarize Base visible controls");
    RawAutoBase::AutoBaseRecommendations wbDryRunRecommendations = recommendations;
    const RawAutoBase::WhiteBalanceCandidateEvidence dryRunWbEvidence =
        BuildStrongGrayWorldWhiteBalanceEvidence();
    wbDryRunRecommendations.whiteBalance =
        RawAutoBase::BuildWhiteBalanceRecommendation(analysis, recipe, &dryRunWbEvidence);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics wbDryRun =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            RawAutoStartPoint::RawAutoStartPointDiagnostics(),
            recipe,
            analysis,
            wbDryRunRecommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView wbDryRunView =
        RawAutoStartPoint::BuildDiagnosticsView(wbDryRun);
    Require(StartPointViewLineValueContains(wbDryRunView, "Base visible controls", "White Balance"),
        "Dry-run Base diagnostics should list Suggested WB as a visible control when policy allows it");
    const RawAutoStartPoint::RawAutoStartPointCandidate* wbBaseCandidate =
        FindStartPointCandidate(
            wbDryRun,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* wbDisplayRequest =
        wbBaseCandidate == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                *wbBaseCandidate,
                RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(wbDisplayRequest != nullptr &&
            wbDisplayRequest->hasRecipe &&
            wbDisplayRequest->recipe.whiteBalance.mode == RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            std::find(
                wbDisplayRequest->expectedControls.begin(),
                wbDisplayRequest->expectedControls.end(),
                RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) !=
                wbDisplayRequest->expectedControls.end(),
        "Dry-run Base Suggested WB should queue a Display Candidate render using the visible WB-updated recipe");
    RawAutoStartPoint::RawAutoStartPointStageDiagnostics staleWbDisplayStage;
    staleWbDisplayStage.stage = RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate;
    staleWbDisplayStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    staleWbDisplayStage.confidence01 = 1.0f;
    staleWbDisplayStage.statusMessage =
        "Rendered Base candidate evidence for Display Candidate from a stale WB recipe.";
    staleWbDisplayStage.display.valid = true;
    staleWbDisplayStage.display.status =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    staleWbDisplayStage.display.displayP05 = 0.10f;
    staleWbDisplayStage.display.displayP50 = 0.45f;
    staleWbDisplayStage.display.displayP95 = 0.82f;
    staleWbDisplayStage.display.displaySpread = 0.72f;
    RawAutoStartPoint::RawAutoStartPointCandidate staleWbRenderCandidate;
    staleWbRenderCandidate.valid = true;
    staleWbRenderCandidate.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
    staleWbRenderCandidate.stageDiagnostics.push_back(staleWbDisplayStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics staleWbRenderDiagnostics;
    staleWbRenderDiagnostics.valid = true;
    staleWbRenderDiagnostics.sourceKey = analysis.sourceKey;
    staleWbRenderDiagnostics.candidates.push_back(staleWbRenderCandidate);
    RawAutoStartPoint::RawAutoStartPointCandidateRenderResult staleWbRenderResult;
    staleWbRenderResult.request = *wbDisplayRequest;
    staleWbRenderResult.request.recipe.whiteBalance.multipliers[0] += 0.25f;
    staleWbRenderResult.attempted = true;
    staleWbRenderResult.success = true;
    staleWbRenderResult.diagnostics = staleWbRenderDiagnostics;
    const RawAutoStartPoint::RawAutoStartPointDiagnostics staleWbMerged =
        RawAutoStartPoint::MergeCandidateRenderResults(
            wbDryRun,
            { staleWbRenderResult },
            recipe,
            analysis,
            wbDryRunRecommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* staleWbMergedBase =
        FindStartPointCandidate(
            staleWbMerged,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* staleMergedDisplay =
        staleWbMergedBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *staleWbMergedBase,
                  RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(staleMergedDisplay == nullptr ||
            staleMergedDisplay->status !=
                RawAutoStartPoint::RawAutoStartPointStageStatus::Complete ||
            staleMergedDisplay->statusMessage.find("Rendered ") == std::string::npos,
        "Rendered Base candidate evidence should not merge when the queued WB recipe no longer matches");

    RawAutoBase::SuggestedLocalAdjustment wbStrictLocal;
    wbStrictLocal.valid = true;
    wbStrictLocal.kind = RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows;
    wbStrictLocal.targetEv = -4.0f;
    wbStrictLocal.deltaEv = 0.45f;
    wbStrictLocal.widthEv = 1.2f;
    wbStrictLocal.feather = 0.6f;
    wbStrictLocal.confidence = 0.90f;
    wbStrictLocal.affectedAreaPercent = 12.0f;
    wbStrictLocal.label = "Open shadows";
    RawAutoBase::AutoBaseRecommendations wbLocalRequestRecommendations =
        wbDryRunRecommendations;
    wbLocalRequestRecommendations.localAdjustments = { wbStrictLocal };
    const RawAutoStartPoint::RawAutoStartPointDiagnostics wbDryRunWithLocalRequest =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            RawAutoStartPoint::RawAutoStartPointDiagnostics(),
            recipe,
            analysis,
            wbLocalRequestRecommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* wbLocalRequestBase =
        FindStartPointCandidate(
            wbDryRunWithLocalRequest,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* wbLocalCandidateRequest =
        wbLocalRequestBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *wbLocalRequestBase,
                  RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate);
    Require(wbLocalCandidateRequest != nullptr &&
            wbLocalCandidateRequest->valid &&
            wbLocalCandidateRequest->hasRecipe &&
            wbLocalCandidateRequest->recipe.whiteBalance.mode ==
                RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            wbLocalCandidateRequest->recipe.localRange.enabled,
        "Dry-run Base Suggested WB should queue Local Candidate evidence with the WB-updated visible Local Range recipe");
    Require(wbLocalCandidateRequest != nullptr &&
            std::any_of(
                wbLocalCandidateRequest->expectedControls.begin(),
                wbLocalCandidateRequest->expectedControls.end(),
                [](RawAutoStartPoint::RawAutoStartPointControl control) {
                    return control == RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance;
                }) &&
            std::any_of(
                wbLocalCandidateRequest->expectedControls.begin(),
                wbLocalCandidateRequest->expectedControls.end(),
                [](RawAutoStartPoint::RawAutoStartPointControl control) {
                    return control == RawAutoStartPoint::RawAutoStartPointControl::LocalRange;
                }),
        "Post-WB Local Candidate render requests should report both White Balance and Local Range as expected visible controls");
    Require(StartPointViewHasLineValue(dryRunView, "Balanced Local/Tone visible controls", "None"),
        "Dry-run Starting Point diagnostics should report no Balanced controls when no proposal is ready");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Base visible controls",
                "dry-run diagnostics do not apply recipe values"),
        "Dry-run Starting Point diagnostics should keep visible-control ownership reporting non-mutating");
    Require(StartPointViewLineValueContains(dryRunView, "Base score components", "Scene"),
        "Dry-run Starting Point diagnostics should summarize Base score components");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Base score components",
                "Scene Placement"),
        "Dry-run Starting Point score component diagnostics should name staged score inputs");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Base score components",
                "Hidden Compensation"),
        "Dry-run Starting Point score component diagnostics should include penalty visibility");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Base score components",
                "does not change candidate scoring or apply recipe values"),
        "Dry-run Starting Point score component diagnostics should remain non-mutating");
    Require(StartPointViewHasLineValue(dryRunView, "CurrentFit warnings", "0"),
        "Dry-run Starting Point diagnostics should report zero candidate warnings when none are present");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "CurrentFit warnings",
                "No candidate-specific warnings"),
        "Dry-run Starting Point diagnostics should explain empty candidate warnings");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Base warnings",
                "Raw Placement readback is unavailable"),
        "Dry-run Starting Point diagnostics should summarize Base candidate warnings");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Base warnings",
                "Dry-run diagnostics do not apply recipe values"),
        "Dry-run Starting Point candidate warning summaries should stay diagnostics-only");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Balanced Local/Tone warnings",
                "Local Candidate readback is unavailable"),
        "Dry-run Starting Point diagnostics should summarize Balanced candidate warnings");
    Require(StartPointViewHasWarningDetail(dryRunView, "Raw Placement readback is not captured yet"),
        "Dry-run Starting Point diagnostics should surface Raw Placement fallback evidence");
    Require(StartPointViewHasWarningDetail(dryRunView, "Neutral Scene readback is not captured yet"),
        "Dry-run Starting Point diagnostics should surface Neutral Scene fallback evidence");
    Require(dryRun.candidates.size() == 3,
        "Dry-run Starting Point diagnostics should include CurrentFit, Base, and Balanced candidate reports");
    Require(dryRun.hasSelectedCandidate &&
            dryRun.selectedCandidateIndex >= 0 &&
            dryRun.selectedCandidateIndex < static_cast<int>(dryRun.candidates.size()),
        "Dry-run Starting Point diagnostics should select an informational candidate");
    const RawAutoStartPoint::RawAutoStartPointCandidate& selectedCandidate =
        dryRun.candidates[static_cast<std::size_t>(dryRun.selectedCandidateIndex)];
    Require(StartPointViewLineValueContains(
                dryRunView,
                "Candidate score order",
                selectedCandidate.label),
        "Dry-run Starting Point diagnostics should expose score order with the selected candidate label");
    Require(StartPointViewLineValueContains(
                dryRunView,
                "Candidate score order",
                ">"),
        "Dry-run Starting Point score order should compare the scored candidates at a glance");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Candidate score order",
                "does not change candidate scoring"),
        "Dry-run Starting Point score order should remain diagnostic-only");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Candidate score order",
                "action readiness"),
        "Dry-run Starting Point score order should not imply action readiness changes");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Diagnostic selection",
                "Selected score"),
        "Dry-run Starting Point diagnostics should include selected-candidate score evidence");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Diagnostic selection",
                selectedCandidate.score.summary),
        "Dry-run Starting Point diagnostics should include selected-candidate score rationale");
    std::string selectedControls;
    for (RawAutoStartPoint::RawAutoStartPointControl control :
         selectedCandidate.visibleEdits.touchedControls) {
        if (!selectedControls.empty()) {
            selectedControls += ", ";
        }
        selectedControls += RawAutoStartPoint::ControlLabel(control);
    }
    if (selectedControls.empty()) {
        selectedControls = "None";
    }
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Diagnostic selection",
                "Visible controls: " + selectedControls),
        "Dry-run Starting Point diagnostics should connect selected candidates to visible controls");
    Require(StartPointViewLineDetailContains(
                dryRunView,
                "Diagnostic selection",
                "appliedRecipeValues remains false"),
        "Dry-run Starting Point selected-candidate diagnostics should remain non-mutating");
    Require(std::abs(recipe.preToneExposureEv - -0.25f) < 0.001f,
        "Building dry-run Starting Point diagnostics should not mutate the current recipe");

    const nlohmann::json dryRunJson = RawAutoStartPoint::SerializeDiagnostics(dryRun);
    Require(dryRunJson.value("dryRunOnly", false) && !dryRunJson.value("appliedRecipeValues", true),
        "Serialized dry-run Starting Point diagnostics should preserve diagnostics-only flags");
    Require(dryRunJson["uiView"].value("title", std::string()) == "Build Starting Point Dry Run",
        "Serialized dry-run Starting Point diagnostics should preserve the UI view title");
    Require(dryRunJson["uiView"]["lines"].is_array() &&
            dryRunJson["uiView"]["lines"].size() >= dryRunView.lines.size(),
        "Serialized dry-run Starting Point diagnostics should include UI-readable lines");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics rawPlacementStage;
    rawPlacementStage.stage = RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
    rawPlacementStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    rawPlacementStage.confidence01 = 1.0f;
    rawPlacementStage.scene.valid = true;
    rawPlacementStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
    rawPlacementStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    rawPlacementStage.scene.evPercentiles.valid = true;
    rawPlacementStage.scene.evPercentiles.p50 = -2.70f;
    rawPlacementStage.scene.evPercentiles.p99 = -0.20f;
    rawPlacementStage.scene.evPercentiles.p999 = 0.0f;
    rawPlacementStage.statusMessage =
        "Current RAW Exposure/WB texture before local and tone stages.";
    RawAutoStartPoint::RawAutoStartPointCandidate rawPlacementReadbackCandidate;
    rawPlacementReadbackCandidate.valid = true;
    rawPlacementReadbackCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
    rawPlacementReadbackCandidate.stageDiagnostics.push_back(rawPlacementStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics rawPlacementDiagnostics;
    rawPlacementDiagnostics.valid = true;
    rawPlacementDiagnostics.sourceKey = analysis.sourceKey;
    rawPlacementDiagnostics.candidates.push_back(rawPlacementReadbackCandidate);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRunWithRawPlacement =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            rawPlacementDiagnostics,
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView rawPlacementView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRunWithRawPlacement);
    Require(StartPointViewHasLineValue(rawPlacementView, "Stage evidence", "1"),
        "Dry-run Starting Point diagnostics should count named Raw Placement evidence");
    Require(!StartPointViewContainsDetail(rawPlacementView, "Raw Placement readback is not captured yet"),
        "Dry-run Starting Point diagnostics should not report Raw Placement missing when the stage is complete");
    Require(!StartPointViewLineDetailContains(
                rawPlacementView,
                "Base warnings",
                "Raw Placement readback is unavailable"),
        "Dry-run Starting Point Base warnings should not call complete Raw Placement evidence unavailable");
    Require(StartPointViewLineDetailContains(
                rawPlacementView,
                "Base score components",
                "Base projected Raw Placement"),
        "Dry-run Starting Point Base scoring should name projected Raw Placement evidence when RAW Exposure changes");
    const RawAutoStartPoint::RawAutoStartPointCandidate* projectedBase =
        FindStartPointCandidate(
            dryRunWithRawPlacement,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    Require(projectedBase != nullptr,
        "Dry-run Starting Point should include a Base candidate for projected Raw Placement coverage");
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* projectedRawPlacement =
        projectedBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *projectedBase,
                  RawAutoStartPoint::RawAutoStartPointStage::RawPlacement);
    Require(projectedRawPlacement != nullptr &&
            projectedRawPlacement->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Projected &&
            projectedRawPlacement->scene.status == RawAutoStartPoint::RawAutoStartPointStageStatus::Projected,
        "Base candidate should carry candidate-specific projected Raw Placement evidence");
    Require(projectedRawPlacement != nullptr &&
            projectedRawPlacement->statusMessage.find("Projected Base Raw Placement") != std::string::npos,
        "Projected Raw Placement evidence should explain that it is projected, not rendered");
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* rawPlacementRenderRequest =
        projectedBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *projectedBase,
                  RawAutoStartPoint::RawAutoStartPointStage::RawPlacement);
    Require(rawPlacementRenderRequest != nullptr &&
            rawPlacementRenderRequest->valid &&
            rawPlacementRenderRequest->replacesStatus ==
                RawAutoStartPoint::RawAutoStartPointStageStatus::Projected,
        "Base candidate should request a real Raw Placement render to replace projected evidence");
    Require(projectedBase != nullptr &&
            rawPlacementRenderRequest != nullptr &&
            rawPlacementRenderRequest->hasRecipe &&
            std::abs(
                rawPlacementRenderRequest->recipe.preToneExposureEv -
                projectedBase->visibleEdits.preToneExposureEv) < 0.001f,
        "Base Raw Placement render request should carry the proposed visible RAW Exposure recipe");
    Require(StartPointViewLineValueContains(
                rawPlacementView,
                "Base render requests",
                "Raw Placement"),
        "Dry-run Starting Point diagnostics should surface Base Raw Placement render requests");
    Require(StartPointViewLineDetailContains(
                rawPlacementView,
                "Base render requests",
                "do not render hidden output"),
        "Dry-run Starting Point render-request diagnostics should reject hidden output semantics");
    const nlohmann::json dryRunWithRawPlacementJson =
        RawAutoStartPoint::SerializeDiagnostics(dryRunWithRawPlacement);
    bool serializedProjectedRawPlacement = false;
    bool serializedProjectedRawPlacementRenderRequest = false;
    for (const nlohmann::json& candidateJson :
         dryRunWithRawPlacementJson.value("candidates", nlohmann::json::array())) {
        if (candidateJson.value("kind", std::string()) != "base") {
            continue;
        }
        for (const nlohmann::json& stageJson :
             candidateJson.value("stageDiagnostics", nlohmann::json::array())) {
            if (stageJson.value("stage", std::string()) == "raw-placement" &&
                stageJson.value("status", std::string()) == "projected") {
                serializedProjectedRawPlacement = true;
            }
        }
        for (const nlohmann::json& requestJson :
             candidateJson.value("renderRequests", nlohmann::json::array())) {
            if (requestJson.value("stage", std::string()) == "raw-placement" &&
                requestJson.value("replacesStatus", std::string()) == "projected" &&
                requestJson.value("hasRecipe", false) &&
                requestJson.value("reason", std::string()).find("proposed visible RAW Exposure") !=
                    std::string::npos) {
                serializedProjectedRawPlacementRenderRequest = true;
            }
        }
    }
    Require(serializedProjectedRawPlacement,
        "Serialized Base candidate should expose projected Raw Placement status");
    Require(serializedProjectedRawPlacementRenderRequest,
        "Serialized Base candidate should expose the Raw Placement render request that replaces projected evidence");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics finishToneStage;
    finishToneStage.stage = RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate;
    finishToneStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    finishToneStage.confidence01 = 1.0f;
    finishToneStage.scene.valid = true;
    finishToneStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate;
    finishToneStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    finishToneStage.scene.evPercentiles.valid = true;
    finishToneStage.scene.evPercentiles.p25 = -0.55f;
    finishToneStage.scene.evPercentiles.p50 = -0.05f;
    finishToneStage.scene.evPercentiles.p75 = 0.55f;
    finishToneStage.scene.evPercentiles.p95 = 1.40f;
    finishToneStage.scene.midSpreadEv = 1.10f;
    finishToneStage.scene.wideSpreadEv = 2.40f;
    finishToneStage.statusMessage =
        "Current pre-display tone texture before View Transform.";
    RawAutoStartPoint::RawAutoStartPointStageDiagnostics displayStage;
    displayStage.stage = RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate;
    displayStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    displayStage.confidence01 = 1.0f;
    displayStage.display.valid = true;
    displayStage.display.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    displayStage.display.metricsAreLinearDisplay = true;
    displayStage.display.displayP05 = 0.10f;
    displayStage.display.displayP50 = 0.45f;
    displayStage.display.displayP95 = 0.82f;
    displayStage.display.displaySpread = 0.72f;
    displayStage.statusMessage =
        "Current post-View-Transform display texture.";
    RawAutoStartPoint::RawAutoStartPointCandidate downstreamCurrentCandidate;
    downstreamCurrentCandidate.valid = true;
    downstreamCurrentCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
    downstreamCurrentCandidate.stageDiagnostics.push_back(rawPlacementStage);
    downstreamCurrentCandidate.stageDiagnostics.push_back(finishToneStage);
    downstreamCurrentCandidate.stageDiagnostics.push_back(displayStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics downstreamDiagnostics;
    downstreamDiagnostics.valid = true;
    downstreamDiagnostics.sourceKey = analysis.sourceKey;
    downstreamDiagnostics.candidates.push_back(downstreamCurrentCandidate);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRunWithDownstream =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            downstreamDiagnostics,
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView downstreamView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRunWithDownstream);
    const RawAutoStartPoint::RawAutoStartPointCandidate* downstreamBase =
        FindStartPointCandidate(
            dryRunWithDownstream,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    Require(downstreamBase != nullptr,
        "Dry-run Starting Point should include a Base candidate for downstream projection coverage");
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* projectedFinishTone =
        downstreamBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *downstreamBase,
                  RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate);
    Require(projectedFinishTone != nullptr &&
            projectedFinishTone->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Projected &&
            projectedFinishTone->scene.status == RawAutoStartPoint::RawAutoStartPointStageStatus::Projected,
        "Base candidate should project pre-display tone evidence after a RAW Exposure change");
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* pendingDisplay =
        downstreamBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *downstreamBase,
                  RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(pendingDisplay != nullptr &&
            pendingDisplay->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Pending &&
            !pendingDisplay->display.valid,
        "Base candidate should mark display evidence pending after a RAW Exposure change");
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* finishToneRenderRequest =
        downstreamBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *downstreamBase,
                  RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate);
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* downstreamRawPlacementRenderRequest =
        downstreamBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *downstreamBase,
                  RawAutoStartPoint::RawAutoStartPointStage::RawPlacement);
    Require(downstreamRawPlacementRenderRequest != nullptr &&
            downstreamRawPlacementRenderRequest->valid,
        "Base downstream candidate should preserve the Raw Placement render request for merge coverage");
    Require(finishToneRenderRequest != nullptr &&
            finishToneRenderRequest->valid &&
            finishToneRenderRequest->replacesStatus ==
                RawAutoStartPoint::RawAutoStartPointStageStatus::Projected,
        "Base candidate should request a real Finish Tone Candidate render to replace projected tone evidence");
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* displayRenderRequest =
        downstreamBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *downstreamBase,
                  RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(displayRenderRequest != nullptr &&
            displayRenderRequest->valid &&
            displayRenderRequest->replacesStatus ==
                RawAutoStartPoint::RawAutoStartPointStageStatus::Pending,
        "Base candidate should request a real Display Candidate render to replace pending display evidence");
    Require(downstreamBase != nullptr &&
            displayRenderRequest != nullptr &&
            displayRenderRequest->hasRecipe &&
            std::abs(
                displayRenderRequest->recipe.preToneExposureEv -
                downstreamBase->visibleEdits.preToneExposureEv) < 0.001f,
        "Base Display Candidate render request should carry the proposed visible RAW Exposure recipe");
    Require(StartPointViewLineDetailContains(
                downstreamView,
                "Base score components",
                "projected Base Finish Tone Candidate"),
        "Base score components should name projected pre-display tone evidence after a RAW Exposure change");
    Require(StartPointViewLineDetailContains(
                downstreamView,
                "Base score components",
                "Base Display Candidate pending"),
        "Base score components should not reuse current display evidence after a RAW Exposure change");
    Require(StartPointViewLineDetailContains(
                downstreamView,
                "Base Display Fit",
                "after the proposed upstream Starting Point values render"),
        "Base Display Fit summary should explain the post-edit refit when RAW Exposure changes");
    Require(StartPointViewLineValueContains(
                downstreamView,
                "Base render requests",
                "Finish Tone Candidate"),
        "Dry-run Starting Point diagnostics should surface Base Finish Tone render requests");
    Require(StartPointViewLineValueContains(
                downstreamView,
                "Base render requests",
                "Display Candidate"),
        "Dry-run Starting Point diagnostics should surface Base Display Candidate render requests");
    Require(StartPointViewLineDetailContains(
                downstreamView,
                "Base render requests",
                "fit View Transform from that post-edit analysis"),
        "Display Candidate render-request diagnostics should explain the post-edit Display Fit bridge");
    const std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest> collectedRequests =
        RawAutoStartPoint::CollectCandidateRenderRequests(dryRunWithDownstream);
    Require(collectedRequests.size() == 3,
        "Starting Point render-request collection should hand off the three Base candidate evidence requests");
    Require(std::any_of(
            collectedRequests.begin(),
            collectedRequests.end(),
            [](const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest& request) {
                return request.stage == RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate &&
                    request.valid &&
                    request.hasRecipe &&
                    request.replacesStatus == RawAutoStartPoint::RawAutoStartPointStageStatus::Pending;
            }),
        "Starting Point render-request collection should preserve the pending Display Candidate request");

    RawAutoBase::SuggestedLocalAdjustment strictDryRunLocal;
    strictDryRunLocal.valid = true;
    strictDryRunLocal.kind = RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows;
    strictDryRunLocal.targetEv = -4.0f;
    strictDryRunLocal.deltaEv = 0.45f;
    strictDryRunLocal.widthEv = 1.2f;
    strictDryRunLocal.feather = 0.6f;
    strictDryRunLocal.confidence = 0.90f;
    strictDryRunLocal.affectedAreaPercent = 12.0f;
    strictDryRunLocal.label = "Open shadows";
    RawAutoBase::AutoBaseRecommendations localRequestRecommendations = recommendations;
    localRequestRecommendations.localAdjustments = { strictDryRunLocal };
    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRunWithLocalRequest =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            downstreamDiagnostics,
            recipe,
            analysis,
            localRequestRecommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* localRequestBase =
        FindStartPointCandidate(
            dryRunWithLocalRequest,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* pendingLocalCandidate =
        localRequestBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *localRequestBase,
                  RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate);
    Require(pendingLocalCandidate != nullptr &&
            pendingLocalCandidate->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Pending,
        "Base candidate should mark strict Local Candidate evidence pending after a RAW Exposure change");
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* localCandidateRenderRequest =
        localRequestBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *localRequestBase,
                  RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate);
    Require(localCandidateRenderRequest != nullptr &&
            localCandidateRenderRequest->valid &&
            localCandidateRenderRequest->replacesStatus ==
                RawAutoStartPoint::RawAutoStartPointStageStatus::Pending &&
            localCandidateRenderRequest->hasRecipe &&
            localCandidateRenderRequest->recipe.localRange.enabled,
        "Base candidate should request a rendered Local Candidate with the strict visible Local Range graph authored in the request recipe");
    Require(localCandidateRenderRequest != nullptr &&
            std::any_of(
                localCandidateRenderRequest->expectedControls.begin(),
                localCandidateRenderRequest->expectedControls.end(),
                [](RawAutoStartPoint::RawAutoStartPointControl control) {
                    return control == RawAutoStartPoint::RawAutoStartPointControl::LocalRange;
                }),
        "Base Local Candidate render request should report Local Range as an expected visible control");
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* postLocalToneRenderRequest =
        localRequestBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *localRequestBase,
                  RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate);
    Require(postLocalToneRenderRequest != nullptr &&
            postLocalToneRenderRequest->hasRecipe &&
            postLocalToneRenderRequest->recipe.localRange.enabled,
        "Base Finish Tone Candidate request should render after the safe Local Range graph recipe");
    Require(postLocalToneRenderRequest != nullptr &&
            std::any_of(
                postLocalToneRenderRequest->expectedControls.begin(),
                postLocalToneRenderRequest->expectedControls.end(),
                [](RawAutoStartPoint::RawAutoStartPointControl control) {
                    return control == RawAutoStartPoint::RawAutoStartPointControl::LocalRange;
                }) &&
            postLocalToneRenderRequest->reason.find("Local Range graph points") != std::string::npos,
        "Base Finish Tone Candidate request should name Local Range in its visible-control scope");
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView dryRunWithLocalRequestView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRunWithLocalRequest);
    Require(StartPointViewLineValueContains(
                dryRunWithLocalRequestView,
                "Base render requests",
                "Local Candidate"),
        "Dry-run Starting Point diagnostics should surface Base Local Candidate render requests");
    Require(StartPointViewLineDetailContains(
                dryRunWithLocalRequestView,
                "Base render requests",
                "safe visible Local Range points"),
        "Local Candidate render-request diagnostics should explain the visible graph evidence bridge");

    auto makeRenderedStageResult = [&](const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest& request,
                                       RawAutoStartPoint::RawAutoStartPointStageDiagnostics stage) {
        RawAutoStartPoint::RawAutoStartPointCandidate renderCandidate;
        renderCandidate.valid = true;
        renderCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
        renderCandidate.stageDiagnostics.push_back(std::move(stage));
        RawAutoStartPoint::RawAutoStartPointDiagnostics renderDiagnostics;
        renderDiagnostics.valid = true;
        renderDiagnostics.sourceKey = analysis.sourceKey;
        renderDiagnostics.candidates.push_back(std::move(renderCandidate));
        RawAutoStartPoint::RawAutoStartPointCandidateRenderResult result;
        result.request = request;
        result.attempted = true;
        result.success = true;
        result.diagnostics = std::move(renderDiagnostics);
        result.renderMs = 1.0f;
        if (request.hasRecipe) {
            result.hasRenderedRecipe = true;
            result.renderedRecipe = request.recipe;
        }
        return result;
    };
    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedLocalCandidateStage;
    renderedLocalCandidateStage.stage =
        RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    renderedLocalCandidateStage.status =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedLocalCandidateStage.confidence01 = 1.0f;
    renderedLocalCandidateStage.statusMessage =
        "Rendered Base candidate evidence for Local Candidate from the queued visible recipe.";
    renderedLocalCandidateStage.scene.valid = true;
    renderedLocalCandidateStage.scene.stage =
        RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    renderedLocalCandidateStage.scene.status =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedLocalCandidateStage.scene.evPercentiles.valid = true;
    renderedLocalCandidateStage.scene.evPercentiles.p25 = -4.2f;
    renderedLocalCandidateStage.scene.evPercentiles.p50 = -3.8f;
    renderedLocalCandidateStage.scene.evPercentiles.p75 = -2.9f;
    renderedLocalCandidateStage.scene.midSpreadEv = 1.3f;
    renderedLocalCandidateStage.scene.wideSpreadEv = 3.2f;
    renderedLocalCandidateStage.scene.statusMessage =
        renderedLocalCandidateStage.statusMessage;
    const RawAutoStartPoint::RawAutoStartPointDiagnostics mergedLocalCandidate =
        RawAutoStartPoint::MergeCandidateRenderResults(
            dryRunWithLocalRequest,
            { makeRenderedStageResult(
                *localCandidateRenderRequest,
                renderedLocalCandidateStage) },
            recipe,
            analysis,
            localRequestRecommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* mergedLocalBase =
        FindStartPointCandidate(
            mergedLocalCandidate,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest*
        chainedLocalDisplayRequest =
            mergedLocalBase == nullptr
                ? nullptr
                : FindStartPointRenderRequest(
                    *mergedLocalBase,
                    RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(mergedLocalBase != nullptr &&
            mergedLocalBase->hasRecipe &&
            mergedLocalBase->recipe.localRange.enabled,
        "Merged Local Candidate evidence should retain the rendered Local Range recipe for exact downstream matching");
    Require(chainedLocalDisplayRequest != nullptr &&
            chainedLocalDisplayRequest->hasRecipe &&
            chainedLocalDisplayRequest->recipe.localRange.enabled,
        "Rendered Local Candidate evidence should queue a full-recipe Display Candidate request with Local Range authored");
    Require(chainedLocalDisplayRequest != nullptr &&
            std::find(
                chainedLocalDisplayRequest->expectedControls.begin(),
                chainedLocalDisplayRequest->expectedControls.end(),
                RawAutoStartPoint::RawAutoStartPointControl::LocalRange) !=
                chainedLocalDisplayRequest->expectedControls.end(),
        "Chained full-recipe Display Candidate request should include Local Range in the visible-control scope");
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* chainedDisplayStage =
        mergedLocalBase == nullptr
            ? nullptr
            : FindStartPointStage(
                *mergedLocalBase,
                RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(chainedDisplayStage != nullptr &&
            chainedDisplayStage->status ==
                RawAutoStartPoint::RawAutoStartPointStageStatus::Pending &&
            chainedDisplayStage->statusMessage.find("full visible Starting Point recipe") !=
                std::string::npos,
        "Chained Local Range display evidence should stay pending until the full visible recipe is rendered");
    RawAutoStartPoint::RawAutoStartPointCandidateRenderResult renderedDisplayResult =
        makeRenderedStageResult(*displayRenderRequest, displayStage);
    renderedDisplayResult.hasRenderedRecipe = true;
    renderedDisplayResult.renderedRecipe = displayRenderRequest->recipe;
    renderedDisplayResult.renderedRecipe.viewTransform.layerJson =
        Stack::RawRecipe::DefaultViewTransformJson();
    renderedDisplayResult.renderedRecipe.viewTransform.layerJson["middleGrey"] = 0.333f;
    renderedDisplayResult.renderedRecipe.viewTransform.layerJson["blackEv"] = -6.0f;
    renderedDisplayResult.renderedRecipe.viewTransform.layerJson["whiteEv"] = 5.5f;
    RawAutoStartPoint::RawAutoStartPointCandidateRenderResult mismatchedControlScopeResult =
        renderedDisplayResult;
    mismatchedControlScopeResult.request.expectedControls = {
        RawAutoStartPoint::RawAutoStartPointControl::RawExposure
    };
    RawAutoStartPoint::RawAutoStartPointCandidateRenderResult mismatchedReplacedStatusResult =
        renderedDisplayResult;
    mismatchedReplacedStatusResult.request.replacesStatus =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Unavailable;
    const RawAutoStartPoint::RawAutoStartPointDiagnostics rejectedScopeMerge =
        RawAutoStartPoint::MergeCandidateRenderResults(
            dryRunWithDownstream,
            { mismatchedControlScopeResult, mismatchedReplacedStatusResult },
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* rejectedScopeBase =
        FindStartPointCandidate(
            rejectedScopeMerge,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* rejectedScopeDisplay =
        rejectedScopeBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *rejectedScopeBase,
                  RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(rejectedScopeDisplay != nullptr &&
            rejectedScopeDisplay->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Pending &&
            FindStartPointRenderRequest(
                *rejectedScopeBase,
                RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate) != nullptr,
        "Rendered candidate evidence should not merge when its expected visible-control scope or replaced-status scope changed");
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView rejectedScopeView =
        RawAutoStartPoint::BuildDiagnosticsView(rejectedScopeMerge);
    Require(StartPointViewLineDetailContains(
                rejectedScopeView,
                "Base warnings",
                "expected visible controls changed"),
        "Rejected rendered candidate evidence should explain visible-control scope mismatches in Diagnostics");
    Require(StartPointViewLineDetailContains(
                rejectedScopeView,
                "Base warnings",
                "replaced-stage status changed"),
        "Rejected rendered candidate evidence should explain replaced-stage status mismatches in Diagnostics");
    const std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> renderedResults = {
        makeRenderedStageResult(*downstreamRawPlacementRenderRequest, rawPlacementStage),
        makeRenderedStageResult(*finishToneRenderRequest, finishToneStage),
        renderedDisplayResult
    };
    const RawAutoStartPoint::RawAutoStartPointDiagnostics mergedDownstream =
        RawAutoStartPoint::MergeCandidateRenderResults(
            dryRunWithDownstream,
            renderedResults,
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* mergedBase =
        FindStartPointCandidate(
            mergedDownstream,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    Require(mergedBase != nullptr,
        "Merged Starting Point diagnostics should preserve the Base candidate");
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* mergedRawPlacement =
        mergedBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *mergedBase,
                  RawAutoStartPoint::RawAutoStartPointStage::RawPlacement);
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* mergedFinishTone =
        mergedBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *mergedBase,
                  RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate);
    const RawAutoStartPoint::RawAutoStartPointStageDiagnostics* mergedDisplay =
        mergedBase == nullptr
            ? nullptr
            : FindStartPointStage(
                  *mergedBase,
                  RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(mergedRawPlacement != nullptr &&
            mergedRawPlacement->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Complete &&
            mergedRawPlacement->statusMessage.find("Rendered Base candidate evidence") !=
                std::string::npos,
        "Merged Base diagnostics should replace projected Raw Placement with rendered evidence");
    Require(mergedFinishTone != nullptr &&
            mergedFinishTone->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Complete &&
            mergedFinishTone->statusMessage.find("Rendered Base candidate evidence") !=
                std::string::npos,
        "Merged Base diagnostics should replace projected Finish Tone Candidate with rendered evidence");
    Require(mergedDisplay != nullptr &&
            mergedDisplay->status == RawAutoStartPoint::RawAutoStartPointStageStatus::Complete &&
            mergedDisplay->statusMessage.find("Rendered Base candidate evidence") !=
                std::string::npos,
        "Merged Base diagnostics should replace pending Display Candidate with rendered evidence");
    Require(mergedBase != nullptr &&
            mergedBase->recipe.viewTransform.layerJson.value("middleGrey", 0.0f) > 0.332f &&
            mergedBase->recipe.viewTransform.layerJson.value("middleGrey", 0.0f) < 0.334f,
        "Merged Base diagnostics should keep the fitted View Transform recipe rendered for Display Candidate evidence");
    Require(mergedBase != nullptr &&
            mergedBase->visibleEdits.viewTransformSummary.find("rendered") != std::string::npos,
        "Merged Base Display Fit summary should explain that the fitted recipe came from rendered candidate evidence");
    Require(mergedBase != nullptr &&
            mergedBase->visibleEdits.viewTransformSummary.find("middle grey 0.333") != std::string::npos &&
            mergedBase->visibleEdits.viewTransformSummary.find("black -6.00 EV") != std::string::npos &&
            mergedBase->visibleEdits.viewTransformSummary.find("white +5.50 EV") != std::string::npos,
        "Merged Base Display Fit summary should report the rendered View Transform values");
    Require(FindStartPointRenderRequest(
                *mergedBase,
                RawAutoStartPoint::RawAutoStartPointStage::RawPlacement) == nullptr &&
            FindStartPointRenderRequest(
                *mergedBase,
                RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate) == nullptr &&
            FindStartPointRenderRequest(
                *mergedBase,
                RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate) == nullptr,
        "Merged Base diagnostics should drain satisfied candidate render requests");
    Require(RawAutoStartPoint::CollectCandidateRenderRequests(mergedDownstream).empty(),
        "Merged Starting Point diagnostics should not requeue completed candidate renders");
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView mergedDownstreamView =
        RawAutoStartPoint::BuildDiagnosticsView(mergedDownstream);
    Require(StartPointViewHasLineValue(
                mergedDownstreamView,
                "Base render requests",
                "None"),
        "Merged Starting Point UI should show no pending Base render requests");
    Require(StartPointViewLineDetailContains(
                mergedDownstreamView,
                "Base score components",
                "rendered Base Raw Placement"),
        "Merged Base score diagnostics should name rendered Raw Placement evidence");
    Require(StartPointViewLineDetailContains(
                mergedDownstreamView,
                "Base score components",
                "rendered Finish Tone Candidate"),
        "Merged Base score diagnostics should name rendered Finish Tone evidence");
    Require(StartPointViewLineDetailContains(
                mergedDownstreamView,
                "Base score components",
                "rendered Display Candidate"),
        "Merged Base score diagnostics should name rendered Display Candidate evidence");
    Require(!StartPointViewLineDetailContains(
                mergedDownstreamView,
                "Base score components",
                "projected Base Finish Tone Candidate"),
        "Merged Base score diagnostics should stop describing merged tone evidence as projected");
    Require(!StartPointViewLineDetailContains(
                mergedDownstreamView,
                "Base score components",
                "Base Display Candidate pending"),
        "Merged Base score diagnostics should stop describing merged display evidence as pending");
    Require(!StartPointViewLineDetailContains(
                mergedDownstreamView,
                "Base warnings",
                "true per-candidate render is still unavailable"),
        "Merged Base warnings should not claim rendered candidate evidence is unavailable");

    const nlohmann::json dryRunWithDownstreamJson =
        RawAutoStartPoint::SerializeDiagnostics(dryRunWithDownstream);
    bool serializedPendingDisplay = false;
    bool serializedPendingDisplayRenderRequest = false;
    for (const nlohmann::json& candidateJson :
         dryRunWithDownstreamJson.value("candidates", nlohmann::json::array())) {
        if (candidateJson.value("kind", std::string()) != "base") {
            continue;
        }
        for (const nlohmann::json& stageJson :
             candidateJson.value("stageDiagnostics", nlohmann::json::array())) {
            if (stageJson.value("stage", std::string()) == "display-candidate" &&
                stageJson.value("status", std::string()) == "pending") {
                serializedPendingDisplay = true;
            }
        }
        for (const nlohmann::json& requestJson :
             candidateJson.value("renderRequests", nlohmann::json::array())) {
            if (requestJson.value("stage", std::string()) == "display-candidate" &&
                requestJson.value("replacesStatus", std::string()) == "pending" &&
                requestJson.value("hasRecipe", false)) {
                serializedPendingDisplayRenderRequest = true;
            }
        }
    }
    Require(serializedPendingDisplay,
        "Serialized Base candidate should expose pending Display Candidate status after a RAW Exposure change");
    Require(serializedPendingDisplayRenderRequest,
        "Serialized Base candidate should expose the Display Candidate render request that replaces pending evidence");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics neutralSceneStage;
    neutralSceneStage.stage = RawAutoStartPoint::RawAutoStartPointStage::NeutralScene;
    neutralSceneStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    neutralSceneStage.confidence01 = 1.0f;
    neutralSceneStage.scene.valid = true;
    neutralSceneStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::NeutralScene;
    neutralSceneStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    neutralSceneStage.scene.evPercentiles.valid = true;
    neutralSceneStage.scene.evPercentiles.p50 = -3.10f;
    neutralSceneStage.scene.evPercentiles.p99 = -0.55f;
    neutralSceneStage.scene.evPercentiles.p999 = -0.10f;
    neutralSceneStage.statusMessage =
        "Neutral scene analysis render at 0 EV before local and tone stages.";
    RawAutoStartPoint::RawAutoStartPointCandidate neutralReadbackCandidate;
    neutralReadbackCandidate.valid = true;
    neutralReadbackCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
    neutralReadbackCandidate.stageDiagnostics.push_back(neutralSceneStage);
    neutralReadbackCandidate.stageDiagnostics.push_back(rawPlacementStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics neutralDiagnostics;
    neutralDiagnostics.valid = true;
    neutralDiagnostics.sourceKey = analysis.sourceKey;
    neutralDiagnostics.candidates.push_back(neutralReadbackCandidate);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRunWithNeutral =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            neutralDiagnostics,
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView neutralView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRunWithNeutral);
    Require(StartPointViewHasLineValue(neutralView, "Stage evidence", "2"),
        "Dry-run Starting Point diagnostics should count Neutral Scene and Raw Placement evidence");
    Require(!StartPointViewContainsDetail(neutralView, "Neutral Scene readback is not captured yet"),
        "Dry-run Starting Point diagnostics should not report Neutral Scene missing when the stage is complete");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics rawTechnicalStage;
    rawTechnicalStage.stage = RawAutoStartPoint::RawAutoStartPointStage::RawTechnical;
    rawTechnicalStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    rawTechnicalStage.confidence01 = 1.0f;
    rawTechnicalStage.rawSafety.valid = true;
    rawTechnicalStage.rawSafety.activeValidFraction = 1.0f;
    rawTechnicalStage.rawSafety.perChannelNearClippedFraction = { 0.0001f, 0.0002f, 0.0001f };
    rawTechnicalStage.rawSafety.fullClipFraction = 0.0f;
    rawTechnicalStage.rawSafety.highlightRecoverabilityScore = 0.95f;
    rawTechnicalStage.rawSafety.wbScaledHeadroomEv = 1.8f;
    rawTechnicalStage.rawSafety.statusMessage =
        "Synthetic raw safety ledger for diagnostic score coverage.";
    rawTechnicalStage.statusMessage =
        "Raw technical safety ledger from active RAW samples.";
    RawAutoStartPoint::RawAutoStartPointCandidate rawSafetyReadbackCandidate;
    rawSafetyReadbackCandidate.valid = true;
    rawSafetyReadbackCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
    rawSafetyReadbackCandidate.stageDiagnostics.push_back(rawTechnicalStage);
    rawSafetyReadbackCandidate.stageDiagnostics.push_back(neutralSceneStage);
    rawSafetyReadbackCandidate.stageDiagnostics.push_back(rawPlacementStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics rawSafetyDiagnostics;
    rawSafetyDiagnostics.valid = true;
    rawSafetyDiagnostics.sourceKey = analysis.sourceKey;
    rawSafetyDiagnostics.candidates.push_back(rawSafetyReadbackCandidate);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRunWithRawSafety =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            rawSafetyDiagnostics,
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView rawSafetyView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRunWithRawSafety);
    Require(StartPointViewHasLineValue(rawSafetyView, "Stage evidence", "3"),
        "Dry-run Starting Point diagnostics should count Raw Technical, Neutral Scene, and Raw Placement evidence");
    Require(StartPointViewLineDetailContains(
                rawSafetyView,
                "Base score components",
                "Raw Technical safety ledger"),
        "Dry-run Starting Point Raw Safety scoring should name the complete raw technical evidence");
    Require(!StartPointViewLineDetailContains(
                rawSafetyView,
                "Base score components",
                "dedicated raw safety ledger is not captured yet"),
        "Dry-run Starting Point Raw Safety scoring should not use the fallback once raw technical evidence exists");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics localFallbackStage;
    localFallbackStage.stage = RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    localFallbackStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Fallback;
    localFallbackStage.confidence01 = 0.65f;
    localFallbackStage.scene.valid = true;
    localFallbackStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    localFallbackStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Fallback;
    localFallbackStage.statusMessage =
        "Fallback local evidence from the current pre-Local-Range texture.";
    RawAutoStartPoint::RawAutoStartPointCandidate currentRenderCandidate;
    currentRenderCandidate.valid = true;
    currentRenderCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::CurrentFit;
    currentRenderCandidate.stageDiagnostics.push_back(localFallbackStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics localFallbackDiagnostics;
    localFallbackDiagnostics.valid = true;
    localFallbackDiagnostics.sourceKey = analysis.sourceKey;
    localFallbackDiagnostics.candidates.push_back(currentRenderCandidate);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRunWithLocalFallback =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            localFallbackDiagnostics,
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView localFallbackView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRunWithLocalFallback);
    Require(StartPointViewHasLineValue(localFallbackView, "Stage evidence", "1"),
        "Dry-run Starting Point diagnostics should count named fallback Local Candidate evidence");
    Require(StartPointViewLineDetailContains(
                localFallbackView,
                "Stage evidence",
                "Local Candidate"),
        "Dry-run Starting Point stage-evidence summary should name Local Candidate evidence");
    Require(StartPointViewLineDetailContains(
                localFallbackView,
                "Balanced Local/Tone warnings",
                "Local Candidate has pre-Local-Range fallback evidence"),
        "Dry-run Starting Point diagnostics should distinguish fallback Local Candidate evidence from missing evidence");
    Require(!StartPointViewContainsDetail(localFallbackView, "Local Candidate readback is unavailable"),
        "Dry-run Starting Point diagnostics should not call Local Candidate unavailable when fallback evidence exists");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics localCompleteStage = localFallbackStage;
    localCompleteStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    localCompleteStage.confidence01 = 1.0f;
    localCompleteStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    localCompleteStage.statusMessage =
        "Post-Local-Range texture immediately before Finish Tone and View Transform.";
    currentRenderCandidate.stageDiagnostics.clear();
    currentRenderCandidate.stageDiagnostics.push_back(localCompleteStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics localCompleteDiagnostics;
    localCompleteDiagnostics.valid = true;
    localCompleteDiagnostics.sourceKey = analysis.sourceKey;
    localCompleteDiagnostics.candidates.push_back(currentRenderCandidate);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics dryRunWithLocalComplete =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            localCompleteDiagnostics,
            recipe,
            analysis,
            recommendations);
    const RawAutoStartPoint::RawAutoStartPointDiagnosticsView localCompleteView =
        RawAutoStartPoint::BuildDiagnosticsView(dryRunWithLocalComplete);
    Require(StartPointViewHasLineValue(localCompleteView, "Stage evidence", "1"),
        "Dry-run Starting Point diagnostics should count named complete Local Candidate evidence");
    Require(!StartPointViewContainsDetail(localCompleteView, "Local Candidate readback is unavailable"),
        "Dry-run Starting Point diagnostics should not call complete Local Candidate evidence unavailable");
    Require(!StartPointViewContainsDetail(localCompleteView, "true post-Local-Range candidate render is still unavailable"),
        "Dry-run Starting Point diagnostics should not warn about missing post-local evidence when Local Candidate is complete");
}

bool StringListContainsFragment(const std::vector<std::string>& values, const std::string& fragment) {
    return std::any_of(values.begin(), values.end(), [&](const std::string& value) {
        return value.find(fragment) != std::string::npos;
    });
}

bool StartPointControlListContains(
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& controls,
    Stack::RawAutoStartPoint::RawAutoStartPointControl control) {
    return std::find(controls.begin(), controls.end(), control) != controls.end();
}

Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics BuildPlannerToneDiagnostics(
    float midSpreadEv,
    float wideSpreadEv) {
    namespace RawAutoStartPoint = Stack::RawAutoStartPoint;

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics stage;
    stage.stage = RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate;
    stage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    stage.scene.valid = true;
    stage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate;
    stage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    stage.scene.midSpreadEv = midSpreadEv;
    stage.scene.wideSpreadEv = wideSpreadEv;
    stage.scene.evPercentiles.valid = true;
    stage.scene.evPercentiles.p01 = -4.0f;
    stage.scene.evPercentiles.p05 = -0.5f * wideSpreadEv;
    stage.scene.evPercentiles.p25 = -0.5f * midSpreadEv;
    stage.scene.evPercentiles.p50 = 0.0f;
    stage.scene.evPercentiles.p75 = 0.5f * midSpreadEv;
    stage.scene.evPercentiles.p95 = 0.5f * wideSpreadEv;
    stage.scene.evPercentiles.p99 = 2.0f;

    RawAutoStartPoint::RawAutoStartPointCandidate candidate;
    candidate.valid = true;
    candidate.stageDiagnostics.push_back(stage);

    RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics;
    diagnostics.valid = true;
    diagnostics.candidates.push_back(candidate);
    return diagnostics;
}

Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics BuildPlannerExposureDiagnostics(
    float neutralP50Ev,
    float neutralP99Ev,
    float neutralP999Ev,
    float wbScaledHeadroomEv) {
    namespace RawAutoStartPoint = Stack::RawAutoStartPoint;

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics rawTechnicalStage;
    rawTechnicalStage.stage = RawAutoStartPoint::RawAutoStartPointStage::RawTechnical;
    rawTechnicalStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    rawTechnicalStage.confidence01 = 1.0f;
    rawTechnicalStage.rawSafety.valid = true;
    rawTechnicalStage.rawSafety.activeValidFraction = 1.0f;
    rawTechnicalStage.rawSafety.wbScaledHeadroomEv = wbScaledHeadroomEv;
    rawTechnicalStage.rawSafety.headroomEv = wbScaledHeadroomEv;
    rawTechnicalStage.rawSafety.perChannelNearClippedFraction = { 0.0001f, 0.0002f, 0.0001f };
    rawTechnicalStage.rawSafety.highlightRecoverabilityScore = 0.95f;
    rawTechnicalStage.rawSafety.statusMessage =
        "Synthetic Raw Technical safety ledger for planner coverage.";

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics neutralSceneStage;
    neutralSceneStage.stage = RawAutoStartPoint::RawAutoStartPointStage::NeutralScene;
    neutralSceneStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    neutralSceneStage.confidence01 = 1.0f;
    neutralSceneStage.scene.valid = true;
    neutralSceneStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::NeutralScene;
    neutralSceneStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    neutralSceneStage.scene.evPercentiles.valid = true;
    neutralSceneStage.scene.evPercentiles.p50 = neutralP50Ev;
    neutralSceneStage.scene.evPercentiles.p99 = neutralP99Ev;
    neutralSceneStage.scene.evPercentiles.p999 = neutralP999Ev;
    neutralSceneStage.statusMessage =
        "Synthetic Neutral Scene analysis render for planner coverage.";

    RawAutoStartPoint::RawAutoStartPointCandidate candidate;
    candidate.valid = true;
    candidate.stageDiagnostics.push_back(rawTechnicalStage);
    candidate.stageDiagnostics.push_back(neutralSceneStage);

    RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics;
    diagnostics.valid = true;
    diagnostics.candidates.push_back(candidate);
    return diagnostics;
}

void TestRawAutoStartPointConservativePlanner() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAutoStartPoint = Stack::RawAutoStartPoint;
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0100.dng", "IMG_0100.dng");

    const RawAutoStartPoint::RawAutoStartPointConservativePlan noAnalysisPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            Stack::RawAnalysis::RawImageAnalysis(),
            RawAutoBase::AutoBaseRecommendations(),
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(!noAnalysisPlan.valid && !noAnalysisPlan.hasAnalysis,
        "Conservative Starting Point planner should not apply without analysis");
    Require(StringListContainsFragment(noAnalysisPlan.withheldSummaries, "Preview analysis pending"),
        "No-analysis Starting Point plan should explain the analysis gate");

    const Stack::RawAnalysis::RawImageAnalysis safeAnalysis =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -2.9f, -0.2f, 0.0f, 5.0f);
    RawAutoBase::AutoBaseRecommendations safeRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(safeAnalysis, recipe);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan safePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            safeRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(safePlan.valid && safePlan.hasUpstreamRecipeChanges,
        "Safe high-confidence RAW exposure should produce an upstream recipe change");
    Require(safePlan.upstreamRecipe.preToneExposureEv > recipe.preToneExposureEv,
        "Safe Starting Point plan should lift RAW Exposure when confidence and highlight safety allow");
    Require(StringListContainsFragment(safePlan.appliedSummaries, "RAW Exposure"),
        "Safe Starting Point plan should list RAW Exposure in applied summaries");
    Require(StartPointControlListContains(
            safePlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
        "Safe Starting Point plan should include Display Fit in touched controls for applied-control readouts");
    Require(StringListContainsFragment(safePlan.appliedSummaries, "Display Fit refresh"),
        "Safe Starting Point plan should list Display Fit refresh in applied summaries");
    Require(StringListContainsFragment(safePlan.evidenceSummaries, "RAW Exposure from conservative exposure evidence"),
        "Safe Starting Point plan should report the RAW Exposure evidence source");
    Require(StringListContainsFragment(safePlan.evidenceSummaries, "Display Fit awaiting matching rendered Display Candidate evidence"),
        "Safe Starting Point plan should report that Display Fit still needs matching rendered evidence after upstream edits");

    const RawAutoBase::WhiteBalanceCandidateEvidence strongWbEvidence =
        BuildStrongGrayWorldWhiteBalanceEvidence();
    RawAutoBase::AutoBaseRecommendations wbRecommendations = safeRecommendations;
    wbRecommendations.whiteBalance =
        RawAutoBase::BuildWhiteBalanceRecommendation(safeAnalysis, recipe, &strongWbEvidence);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan wbPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(wbPlan.upstreamRecipe.whiteBalance.mode == RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            wbPlan.upstreamRecipe.whiteBalance.hasMultipliers,
        "Starting Point should apply visible Suggested WB multipliers when camera WB is absent and neutral evidence is strong");
    Require(StartPointControlListContains(
            wbPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance),
        "Applied Suggested WB should be reported as a visible touched control");
    Require(std::abs(wbPlan.upstreamRecipe.preToneExposureEv - recipe.preToneExposureEv) < 0.001f,
        "Starting Point should withhold RAW Exposure when Suggested WB changed before post-WB placement evidence");
    Require(StringListContainsFragment(wbPlan.withheldSummaries, "post-WB Raw Placement"),
        "Starting Point should explain that RAW Exposure is pending after a Suggested WB apply");
    Require(wbPlan.needsPostApplyDisplayFit && !wbPlan.usedRenderedDisplayFit,
        "Suggested WB apply should queue post-apply Display Fit instead of reusing stale display evidence");

    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedPostWbRawPlacementDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedPostWbRawPlacementStage;
    renderedPostWbRawPlacementStage.stage =
        RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
    renderedPostWbRawPlacementStage.status =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedPostWbRawPlacementStage.confidence01 = 1.0f;
    renderedPostWbRawPlacementStage.statusMessage =
        "Rendered Base candidate evidence for Raw Placement from the queued visible WB recipe.";
    renderedPostWbRawPlacementStage.scene.valid = true;
    renderedPostWbRawPlacementStage.scene.stage =
        RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
    renderedPostWbRawPlacementStage.scene.status =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedPostWbRawPlacementStage.scene.evPercentiles.valid = true;
    renderedPostWbRawPlacementStage.scene.evPercentiles.p50 = -3.60f;
    renderedPostWbRawPlacementStage.scene.evPercentiles.p99 = -0.12f;
    renderedPostWbRawPlacementStage.scene.evPercentiles.p999 = 0.0f;
    renderedPostWbRawPlacementStage.scene.statusMessage =
        renderedPostWbRawPlacementStage.statusMessage;
    RawAutoStartPoint::RawAutoStartPointCandidate renderedPostWbRawPlacementBase;
    renderedPostWbRawPlacementBase.valid = true;
    renderedPostWbRawPlacementBase.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    renderedPostWbRawPlacementBase.hasRecipe = true;
    renderedPostWbRawPlacementBase.recipe = recipe;
    RawAutoBase::ApplyWhiteBalanceRecommendationToRecipe(
        renderedPostWbRawPlacementBase.recipe,
        wbRecommendations.whiteBalance);
    renderedPostWbRawPlacementBase.stageDiagnostics.push_back(
        renderedPostWbRawPlacementStage);
    renderedPostWbRawPlacementDiagnostics.candidates.push_back(
        renderedPostWbRawPlacementBase);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan renderedPostWbRawPlacementPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            renderedPostWbRawPlacementDiagnostics);
    Require(renderedPostWbRawPlacementPlan.upstreamRecipe.whiteBalance.mode ==
                RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            renderedPostWbRawPlacementPlan.upstreamRecipe.preToneExposureEv >
                recipe.preToneExposureEv,
        "Rendered post-WB Raw Placement evidence should let Starting Point apply Suggested WB plus visible RAW Exposure");
    Require(StartPointControlListContains(
            renderedPostWbRawPlacementPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            StartPointControlListContains(
                renderedPostWbRawPlacementPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Rendered post-WB RAW Exposure apply should report WB and RAW Exposure as visible touched controls");
    Require(StringListContainsFragment(
            renderedPostWbRawPlacementPlan.appliedSummaries,
            "RAW Exposure"),
        "Rendered post-WB Raw Placement plan should list RAW Exposure in applied summaries");
    Require(!StringListContainsFragment(
            renderedPostWbRawPlacementPlan.withheldSummaries,
            "post-WB Raw Placement"),
        "Rendered post-WB Raw Placement plan should not still report RAW Exposure placement evidence pending");
    Require(renderedPostWbRawPlacementPlan.needsPostApplyDisplayFit &&
            !renderedPostWbRawPlacementPlan.usedRenderedDisplayFit,
        "Rendered post-WB RAW Exposure should keep Display Fit in the post-apply path");
    Require(StringListContainsFragment(
            renderedPostWbRawPlacementPlan.evidenceSummaries,
            "RAW Exposure from rendered post-WB Raw Placement"),
        "Rendered post-WB RAW Exposure plan should report the exact Raw Placement evidence source");

    const Stack::RawAnalysis::RawImageAnalysis lowConfidencePostWbAnalysis =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.2f, 0.0f, 13.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    RawAutoBase::AutoBaseRecommendations lowConfidencePostWbRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(lowConfidencePostWbAnalysis, recipe);
    lowConfidencePostWbRecommendations.whiteBalance =
        RawAutoBase::BuildWhiteBalanceRecommendation(
            lowConfidencePostWbAnalysis,
            recipe,
            &strongWbEvidence);
    Require(lowConfidencePostWbRecommendations.exposure.valid &&
            !lowConfidencePostWbRecommendations.exposure.autoApplyAllowed &&
            std::abs(lowConfidencePostWbRecommendations.exposure.confidence - 0.35f) < 0.001f,
        "Low-confidence post-WB RAW Exposure fixture should start below the old 70% one-click gate");
    RawAutoStartPoint::RawAutoStartPointDiagnostics lowConfidencePostWbDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    RawAutoStartPoint::RawAutoStartPointCandidate lowConfidencePostWbBase =
        renderedPostWbRawPlacementBase;
    lowConfidencePostWbBase.recipe = recipe;
    RawAutoBase::ApplyWhiteBalanceRecommendationToRecipe(
        lowConfidencePostWbBase.recipe,
        lowConfidencePostWbRecommendations.whiteBalance);
    lowConfidencePostWbDiagnostics.candidates.push_back(lowConfidencePostWbBase);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan lowConfidencePostWbPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            lowConfidencePostWbAnalysis,
            lowConfidencePostWbRecommendations,
            lowConfidencePostWbDiagnostics);
    Require(lowConfidencePostWbPlan.upstreamRecipe.whiteBalance.mode ==
                RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            std::abs(lowConfidencePostWbPlan.upstreamRecipe.preToneExposureEv - 0.15f) < 0.001f,
        "Rendered post-WB Raw Placement should now apply a tiny visible RAW Exposure nudge below the old full-confidence gate");
    Require(StartPointControlListContains(
                lowConfidencePostWbPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            StartPointControlListContains(
                lowConfidencePostWbPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Low-confidence rendered post-WB RAW Exposure nudge should report WB and RAW Exposure as visible touched controls");
    Require(StringListContainsFragment(
            lowConfidencePostWbPlan.withheldSummaries,
            "tiny positive post-WB one-click cap"),
        "Low-confidence rendered post-WB RAW Exposure nudge should report that additional lift remains manual");

    RawAutoStartPoint::RawAutoStartPointDiagnostics stalePostWbRawPlacementDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    RawAutoStartPoint::RawAutoStartPointCandidate stalePostWbRawPlacementBase =
        renderedPostWbRawPlacementBase;
    stalePostWbRawPlacementBase.recipe = recipe;
    stalePostWbRawPlacementDiagnostics.candidates.push_back(
        stalePostWbRawPlacementBase);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan stalePostWbRawPlacementPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            stalePostWbRawPlacementDiagnostics);
    Require(stalePostWbRawPlacementPlan.upstreamRecipe.whiteBalance.mode ==
                RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            std::abs(stalePostWbRawPlacementPlan.upstreamRecipe.preToneExposureEv -
                recipe.preToneExposureEv) < 0.001f,
        "Stale post-WB Raw Placement evidence should allow Suggested WB but not unlock RAW Exposure");
    Require(StartPointControlListContains(
            stalePostWbRawPlacementPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            !StartPointControlListContains(
                stalePostWbRawPlacementPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Stale post-WB Raw Placement evidence should not report RAW Exposure as a visible touched control");
    Require(StringListContainsFragment(
            stalePostWbRawPlacementPlan.withheldSummaries,
            "post-WB Raw Placement"),
        "Stale post-WB Raw Placement evidence should keep RAW Exposure evidence pending");
    Require(StringListContainsFragment(
            stalePostWbRawPlacementPlan.warnings,
            "visible recipe did not match"),
        "Stale post-WB Raw Placement evidence should report the recipe mismatch");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics stalePostWbRawPlacementDryRun =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            stalePostWbRawPlacementDiagnostics,
            recipe,
            safeAnalysis,
            wbRecommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* stalePostWbRawPlacementDryRunBase =
        FindStartPointCandidate(
            stalePostWbRawPlacementDryRun,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    Require(stalePostWbRawPlacementDryRunBase != nullptr &&
            StartPointControlListContains(
                stalePostWbRawPlacementDryRunBase->visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            !StartPointControlListContains(
                stalePostWbRawPlacementDryRunBase->visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Dry-run Base should not promote stale post-WB Raw Placement into RAW Exposure");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics postWbRawPlacementDryRun =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            renderedPostWbRawPlacementDiagnostics,
            recipe,
            safeAnalysis,
            wbRecommendations);
    const RawAutoStartPoint::RawAutoStartPointCandidate* postWbRawPlacementBase =
        FindStartPointCandidate(
            postWbRawPlacementDryRun,
            RawAutoStartPoint::RawAutoStartPointCandidateKind::Base);
    Require(postWbRawPlacementBase != nullptr &&
            StartPointControlListContains(
                postWbRawPlacementBase->visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            StartPointControlListContains(
                postWbRawPlacementBase->visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Dry-run Base should promote rendered post-WB Raw Placement into a WB-plus-RAW-Exposure visible recipe");
    const RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest* postWbDisplayRequest =
        postWbRawPlacementBase == nullptr
            ? nullptr
            : FindStartPointRenderRequest(
                  *postWbRawPlacementBase,
                  RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    Require(postWbDisplayRequest != nullptr &&
            postWbDisplayRequest->hasRecipe &&
            postWbDisplayRequest->recipe.whiteBalance.mode ==
                RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            std::abs(
                postWbDisplayRequest->recipe.preToneExposureEv -
                renderedPostWbRawPlacementPlan.upstreamRecipe.preToneExposureEv) < 0.001f,
        "Post-WB downstream Display Candidate request should carry the WB-plus-RAW-Exposure visible recipe");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics postWbDisplayStage;
    postWbDisplayStage.stage = RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate;
    postWbDisplayStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    postWbDisplayStage.confidence01 = 1.0f;
    postWbDisplayStage.statusMessage =
        "Rendered Base candidate evidence for Display Candidate from the queued visible recipe after fitting View Transform.";
    postWbDisplayStage.display.valid = true;
    postWbDisplayStage.display.status =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    postWbDisplayStage.display.displayP05 = 0.10f;
    postWbDisplayStage.display.displayP50 = 0.45f;
    postWbDisplayStage.display.displayP95 = 0.82f;
    postWbDisplayStage.display.displaySpread = 0.72f;
    postWbDisplayStage.display.statusMessage = postWbDisplayStage.statusMessage;

    RawAutoStartPoint::RawAutoStartPointDiagnostics stalePostWbDisplayDiagnostics =
        renderedPostWbRawPlacementDiagnostics;
    RawAutoStartPoint::RawAutoStartPointCandidate stalePostWbDisplayBase;
    stalePostWbDisplayBase.valid = true;
    stalePostWbDisplayBase.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    stalePostWbDisplayBase.hasRecipe = true;
    stalePostWbDisplayBase.recipe = renderedPostWbRawPlacementBase.recipe;
    stalePostWbDisplayBase.recipe.viewTransform.layerJson =
        RawRecipe::DefaultViewTransformJson();
    stalePostWbDisplayBase.recipe.viewTransform.layerJson["middleGrey"] = 0.222f;
    stalePostWbDisplayBase.stageDiagnostics.push_back(postWbDisplayStage);
    stalePostWbDisplayDiagnostics.candidates.push_back(stalePostWbDisplayBase);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan stalePostWbDisplayPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            stalePostWbDisplayDiagnostics);
    Require(stalePostWbDisplayPlan.needsPostApplyDisplayFit &&
            !stalePostWbDisplayPlan.usedRenderedDisplayFit,
        "WB-only rendered Display Candidate should not be reused after post-WB RAW Exposure is applied");

    RawAutoStartPoint::RawAutoStartPointDiagnostics exactPostWbDisplayDiagnostics =
        renderedPostWbRawPlacementDiagnostics;
    RawAutoStartPoint::RawAutoStartPointCandidate exactPostWbDisplayBase;
    exactPostWbDisplayBase.valid = true;
    exactPostWbDisplayBase.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    exactPostWbDisplayBase.hasRecipe = true;
    exactPostWbDisplayBase.recipe = renderedPostWbRawPlacementPlan.upstreamRecipe;
    exactPostWbDisplayBase.recipe.viewTransform.layerJson =
        RawRecipe::DefaultViewTransformJson();
    exactPostWbDisplayBase.recipe.viewTransform.layerJson["middleGrey"] = 0.377f;
    exactPostWbDisplayBase.recipe.viewTransform.layerJson["blackEv"] = -6.25f;
    exactPostWbDisplayBase.recipe.viewTransform.layerJson["whiteEv"] = 5.10f;
    exactPostWbDisplayBase.stageDiagnostics.push_back(postWbDisplayStage);
    exactPostWbDisplayDiagnostics.candidates.push_back(exactPostWbDisplayBase);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan exactPostWbDisplayPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            exactPostWbDisplayDiagnostics);
    Require(exactPostWbDisplayPlan.usedRenderedDisplayFit &&
            !exactPostWbDisplayPlan.needsPostApplyDisplayFit,
        "Exact WB-plus-RAW-Exposure Display Candidate should let Starting Point finish Display Fit in the same visible recipe edit");
    Require(std::abs(
            exactPostWbDisplayPlan.upstreamRecipe.viewTransform.layerJson.value("middleGrey", 0.0f) -
            0.377f) < 0.001f,
        "Exact post-WB Display Fit plan should carry the fitted View Transform recipe");
    Require(exactPostWbDisplayPlan.visibleEdits.viewTransformSummary.find("middle grey 0.377") !=
                std::string::npos &&
            exactPostWbDisplayPlan.visibleEdits.viewTransformSummary.find("black -6.25 EV") !=
                std::string::npos &&
            exactPostWbDisplayPlan.visibleEdits.viewTransformSummary.find("white +5.10 EV") !=
                std::string::npos,
        "Exact post-WB Display Fit plan should report the fitted View Transform values");

    RawAutoBase::SuggestedLocalAdjustment wbPlannerLocal;
    wbPlannerLocal.valid = true;
    wbPlannerLocal.kind = RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows;
    wbPlannerLocal.targetEv = -4.0f;
    wbPlannerLocal.deltaEv = 0.45f;
    wbPlannerLocal.widthEv = 1.2f;
    wbPlannerLocal.feather = 0.6f;
    wbPlannerLocal.confidence = 0.90f;
    wbPlannerLocal.affectedAreaPercent = 12.0f;
    wbPlannerLocal.label = "Open shadows";
    RawAutoBase::AutoBaseRecommendations wbLocalRecommendations = wbRecommendations;
    wbLocalRecommendations.exposure = RawAutoBase::RawExposureRecommendation();
    wbLocalRecommendations.localAdjustments = { wbPlannerLocal };
    const RawAutoStartPoint::RawAutoStartPointConservativePlan wbLocalPendingPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbLocalRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(!wbLocalPendingPlan.upstreamRecipe.localRange.enabled,
        "Post-WB Local Range should remain withheld before rendered Local Candidate evidence exists");
    Require(StringListContainsFragment(wbLocalPendingPlan.withheldSummaries, "post-WB Local Candidate"),
        "Post-WB Local Range withholding should name the missing rendered Local Candidate evidence");
    Require(StringListContainsFragment(wbLocalPendingPlan.warnings, "applied Suggested WB multipliers"),
        "Post-WB Local Range withholding should explain that the current local evidence is stale after WB");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedWbLocalStage;
    renderedWbLocalStage.stage = RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    renderedWbLocalStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedWbLocalStage.confidence01 = 1.0f;
    renderedWbLocalStage.statusMessage =
        "Rendered Base candidate evidence for Local Candidate from the queued visible WB recipe.";
    renderedWbLocalStage.scene.valid = true;
    renderedWbLocalStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    renderedWbLocalStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedWbLocalStage.scene.evPercentiles.valid = true;
    renderedWbLocalStage.scene.evPercentiles.p25 = -4.2f;
    renderedWbLocalStage.scene.evPercentiles.p50 = -3.8f;
    renderedWbLocalStage.scene.evPercentiles.p75 = -2.9f;
    renderedWbLocalStage.scene.midSpreadEv = 1.3f;
    renderedWbLocalStage.scene.wideSpreadEv = 3.2f;
    renderedWbLocalStage.scene.statusMessage = renderedWbLocalStage.statusMessage;
    RawAutoStartPoint::RawAutoStartPointCandidate renderedWbLocalBaseCandidate;
    renderedWbLocalBaseCandidate.valid = true;
    renderedWbLocalBaseCandidate.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    renderedWbLocalBaseCandidate.hasRecipe = true;
    renderedWbLocalBaseCandidate.recipe = wbLocalPendingPlan.upstreamRecipe;
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(
            wbPlannerLocal,
            renderedWbLocalBaseCandidate.recipe),
        "Rendered post-WB Local Candidate test fixture should carry the exact WB-plus-Local-Range recipe");
    renderedWbLocalBaseCandidate.stageDiagnostics.push_back(renderedWbLocalStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedWbLocalDiagnostics;
    renderedWbLocalDiagnostics.valid = true;
    renderedWbLocalDiagnostics.candidates.push_back(renderedWbLocalBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan renderedWbLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbLocalRecommendations,
            renderedWbLocalDiagnostics);
    Require(renderedWbLocalPlan.upstreamRecipe.whiteBalance.mode ==
                RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            renderedWbLocalPlan.upstreamRecipe.localRange.enabled &&
            renderedWbLocalPlan.visibleEdits.localRangeValid,
        "Rendered post-WB Local Candidate evidence should let Starting Point apply Suggested WB plus one visible Local Range point");
    Require(StartPointControlListContains(
            renderedWbLocalPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            StartPointControlListContains(
                renderedWbLocalPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::LocalRange),
        "Rendered post-WB Local Range apply should report both WB and Local Range as visible touched controls");
    Require(StringListContainsFragment(renderedWbLocalPlan.appliedSummaries, "Local Range"),
        "Rendered post-WB Local Range plan should list Local Range in applied summaries");
    Require(renderedWbLocalPlan.visibleEdits.localRangeSummary.find("width 1.20 EV") !=
                std::string::npos &&
            renderedWbLocalPlan.visibleEdits.localRangeSummary.find("feather 0.60") !=
                std::string::npos,
        "Rendered post-WB Local Range plan should keep visible point width and feather in the changed-value summary");
    Require(renderedWbLocalPlan.needsPostApplyDisplayFit &&
            !renderedWbLocalPlan.usedRenderedDisplayFit,
        "Rendered post-WB Local Range should keep Display Fit in the post-apply path");
    Require(!StringListContainsFragment(renderedWbLocalPlan.withheldSummaries, "post-WB Local Candidate"),
        "Rendered post-WB Local Range plan should not still report Local Candidate evidence pending");

    RawAutoBase::AutoBaseRecommendations wbExposureLocalRecommendations = wbRecommendations;
    wbExposureLocalRecommendations.localAdjustments = { wbPlannerLocal };
    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedWbExposureLocalDiagnostics =
        renderedPostWbRawPlacementDiagnostics;
    RawAutoStartPoint::RawAutoStartPointCandidate renderedWbExposureLocalBaseCandidate;
    renderedWbExposureLocalBaseCandidate.valid = true;
    renderedWbExposureLocalBaseCandidate.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    renderedWbExposureLocalBaseCandidate.hasRecipe = true;
    renderedWbExposureLocalBaseCandidate.recipe =
        renderedPostWbRawPlacementPlan.upstreamRecipe;
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(
            wbPlannerLocal,
            renderedWbExposureLocalBaseCandidate.recipe),
        "Rendered post-WB-plus-exposure Local Candidate test fixture should carry the exact WB-plus-RAW-Exposure-plus-Local-Range recipe");
    renderedWbExposureLocalBaseCandidate.stageDiagnostics.push_back(renderedWbLocalStage);
    renderedWbExposureLocalDiagnostics.candidates.push_back(
        renderedWbExposureLocalBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan renderedWbExposureLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbExposureLocalRecommendations,
            renderedWbExposureLocalDiagnostics);
    Require(renderedWbExposureLocalPlan.upstreamRecipe.whiteBalance.mode ==
                RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            renderedWbExposureLocalPlan.upstreamRecipe.preToneExposureEv >
                recipe.preToneExposureEv &&
            renderedWbExposureLocalPlan.upstreamRecipe.localRange.enabled &&
            renderedWbExposureLocalPlan.visibleEdits.localRangeValid,
        "Rendered post-WB-plus-RAW-Exposure Local Candidate evidence should let Starting Point apply WB, RAW Exposure, and one visible Local Range point");
    Require(StartPointControlListContains(
            renderedWbExposureLocalPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            StartPointControlListContains(
                renderedWbExposureLocalPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure) &&
            StartPointControlListContains(
                renderedWbExposureLocalPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::LocalRange),
        "Rendered post-WB-plus-exposure Local Range apply should report WB, RAW Exposure, and Local Range as visible touched controls");
    Require(renderedWbExposureLocalPlan.needsPostApplyDisplayFit &&
            !renderedWbExposureLocalPlan.usedRenderedDisplayFit,
        "Rendered post-WB-plus-exposure Local Range should keep Display Fit in the post-apply path");
    Require(StringListContainsFragment(
            renderedWbExposureLocalPlan.evidenceSummaries,
            "Local Range from exact rendered Local Candidate"),
        "Rendered post-WB-plus-exposure Local Range plan should report exact Local Candidate evidence");
    Require(!StringListContainsFragment(
            renderedWbExposureLocalPlan.withheldSummaries,
            "upstream RAW Exposure/WB changed"),
        "Rendered post-WB-plus-exposure Local Range should not still report combined upstream evidence pending");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics wbExposureToneStageSource =
        BuildPlannerToneDiagnostics(1.8f, 5.0f);
    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedWbExposureToneStage =
        wbExposureToneStageSource.candidates[0].stageDiagnostics[0];
    renderedWbExposureToneStage.statusMessage =
        "Rendered Base candidate evidence for Finish Tone Candidate after the queued visible WB plus RAW Exposure recipe.";
    renderedWbExposureToneStage.scene.statusMessage =
        renderedWbExposureToneStage.statusMessage;

    RawAutoStartPoint::RawAutoStartPointDiagnostics staleWbToneDiagnostics =
        renderedPostWbRawPlacementDiagnostics;
    RawAutoStartPoint::RawAutoStartPointCandidate staleWbToneBaseCandidate;
    staleWbToneBaseCandidate.valid = true;
    staleWbToneBaseCandidate.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    staleWbToneBaseCandidate.hasRecipe = true;
    staleWbToneBaseCandidate.recipe = renderedPostWbRawPlacementBase.recipe;
    staleWbToneBaseCandidate.stageDiagnostics.push_back(renderedWbExposureToneStage);
    staleWbToneDiagnostics.candidates.push_back(staleWbToneBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan staleWbTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            staleWbToneDiagnostics);
    Require(!staleWbTonePlan.visibleEdits.finishToneValid,
        "WB-only rendered Finish Tone Candidate should not be reused after post-WB RAW Exposure is applied");
    Require(StringListContainsFragment(staleWbTonePlan.withheldSummaries, "Finish Tone pending"),
        "Stale post-WB Finish Tone evidence should keep the post-edit tone evidence gate visible");

    RawAutoStartPoint::RawAutoStartPointDiagnostics exactWbExposureToneDiagnostics =
        renderedPostWbRawPlacementDiagnostics;
    RawAutoStartPoint::RawAutoStartPointCandidate exactWbExposureToneBaseCandidate;
    exactWbExposureToneBaseCandidate.valid = true;
    exactWbExposureToneBaseCandidate.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    exactWbExposureToneBaseCandidate.hasRecipe = true;
    exactWbExposureToneBaseCandidate.recipe =
        renderedPostWbRawPlacementPlan.upstreamRecipe;
    exactWbExposureToneBaseCandidate.stageDiagnostics.push_back(
        renderedWbExposureToneStage);
    exactWbExposureToneDiagnostics.candidates.push_back(
        exactWbExposureToneBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan exactWbExposureTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            exactWbExposureToneDiagnostics);
    Require(exactWbExposureTonePlan.visibleEdits.finishToneValid &&
            exactWbExposureTonePlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Exact post-WB-plus-RAW-Exposure Finish Tone Candidate evidence should let mild tone author visible points");
    Require(StartPointControlListContains(
            exactWbExposureTonePlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::WhiteBalance) &&
            StartPointControlListContains(
                exactWbExposureTonePlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure) &&
            StartPointControlListContains(
                exactWbExposureTonePlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::FinishTone),
        "Exact post-WB-plus-exposure Finish Tone apply should report WB, RAW Exposure, and Finish Tone as visible touched controls");
    Require(exactWbExposureTonePlan.needsPostApplyDisplayFit &&
            !exactWbExposureTonePlan.usedRenderedDisplayFit,
        "Exact post-WB-plus-exposure Finish Tone should keep Display Fit in the post-apply path");
    Require(exactWbExposureTonePlan.visibleEdits.finishToneSummary.find("5 visible points") !=
                std::string::npos &&
            exactWbExposureTonePlan.visibleEdits.finishToneSummary.find("shadow y") !=
                std::string::npos &&
            exactWbExposureTonePlan.visibleEdits.finishToneSummary.find("light y") !=
                std::string::npos,
        "Exact post-WB-plus-exposure Finish Tone summary should report the authored visible graph point movement");
    Require(StringListContainsFragment(
            exactWbExposureTonePlan.evidenceSummaries,
            "Finish Tone from exact rendered Finish Tone Candidate"),
        "Exact post-WB-plus-exposure Finish Tone plan should report exact Finish Tone Candidate evidence");
    Require(!StringListContainsFragment(
            exactWbExposureTonePlan.withheldSummaries,
            "Finish Tone pending"),
        "Exact post-WB-plus-exposure Finish Tone should not still report post-edit tone evidence pending");

    RawAutoStartPoint::RawAutoStartPointDiagnostics exactWbExposureToneDisplayDiagnostics =
        exactWbExposureToneDiagnostics;
    RawAutoStartPoint::RawAutoStartPointCandidate exactWbExposureToneDisplayBase;
    exactWbExposureToneDisplayBase.valid = true;
    exactWbExposureToneDisplayBase.kind =
        RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    exactWbExposureToneDisplayBase.hasRecipe = true;
    exactWbExposureToneDisplayBase.recipe =
        exactWbExposureTonePlan.upstreamRecipe;
    exactWbExposureToneDisplayBase.recipe.viewTransform.layerJson =
        RawRecipe::DefaultViewTransformJson();
    exactWbExposureToneDisplayBase.recipe.viewTransform.layerJson["middleGrey"] = 0.366f;
    exactWbExposureToneDisplayBase.recipe.viewTransform.layerJson["blackEv"] = -6.10f;
    exactWbExposureToneDisplayBase.recipe.viewTransform.layerJson["whiteEv"] = 5.20f;
    exactWbExposureToneDisplayBase.stageDiagnostics.push_back(postWbDisplayStage);
    exactWbExposureToneDisplayDiagnostics.candidates.push_back(
        exactWbExposureToneDisplayBase);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan exactWbExposureToneDisplayPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            wbRecommendations,
            exactWbExposureToneDisplayDiagnostics);
    Require(exactWbExposureToneDisplayPlan.visibleEdits.finishToneValid &&
            exactWbExposureToneDisplayPlan.usedRenderedDisplayFit &&
            !exactWbExposureToneDisplayPlan.needsPostApplyDisplayFit,
        "Exact post-tone Display Candidate evidence should let Starting Point apply Finish Tone and fitted Display Fit in one visible recipe edit");
    Require(std::abs(
            exactWbExposureToneDisplayPlan.upstreamRecipe.viewTransform.layerJson.value("middleGrey", 0.0f) -
            0.366f) < 0.001f,
        "Exact post-tone Display Fit plan should carry the fitted View Transform recipe");
    Require(exactWbExposureToneDisplayPlan.visibleEdits.viewTransformSummary.find("middle grey 0.366") !=
                std::string::npos &&
            exactWbExposureToneDisplayPlan.visibleEdits.viewTransformSummary.find("black -6.10 EV") !=
                std::string::npos &&
            exactWbExposureToneDisplayPlan.visibleEdits.viewTransformSummary.find("white +5.20 EV") !=
                std::string::npos,
        "Exact post-tone Display Fit plan should report the fitted View Transform values");
    Require(StringListContainsFragment(
            exactWbExposureToneDisplayPlan.evidenceSummaries,
            "Display Fit from exact rendered Display Candidate"),
        "Exact post-tone Display Fit plan should report exact Display Candidate evidence");

    const Stack::RawAnalysis::RawImageAnalysis largeLiftAnalysis =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.2f, 0.0f, 5.0f);
    const RawAutoBase::AutoBaseRecommendations largeLiftRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(largeLiftAnalysis, recipe);
    Require(largeLiftRecommendations.exposure.valid &&
            !largeLiftRecommendations.exposure.autoApplyAllowed &&
            largeLiftRecommendations.exposure.deltaEv > 0.50f,
        "Baseline RAW exposure recommendation should withhold large lifts before staged one-click capping");
    const RawAutoStartPoint::RawAutoStartPointConservativePlan cappedNoStageLiftPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(std::abs(cappedNoStageLiftPlan.upstreamRecipe.preToneExposureEv - 0.50f) < 0.001f,
        "Starting Point should move RAW Exposure by the visible cap when high-confidence exposure evidence is available before staged readbacks");
    Require(StartPointControlListContains(
            cappedNoStageLiftPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Pre-stage capped RAW Exposure should be reported as a visible applied control");
    Require(StringListContainsFragment(cappedNoStageLiftPlan.withheldSummaries, "one-click cap"),
        "Pre-stage capped RAW Exposure should report that additional lift remains withheld");
    Require(cappedNoStageLiftPlan.needsPostApplyDisplayFit && !cappedNoStageLiftPlan.usedRenderedDisplayFit,
        "Pre-stage capped RAW Exposure should still queue post-apply Display Fit");

    const Stack::RawAnalysis::RawImageAnalysis mediumConfidenceLiftAnalysis =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.2f, 0.0f, 13.0f);
    const RawAutoBase::AutoBaseRecommendations mediumConfidenceLiftRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(mediumConfidenceLiftAnalysis, recipe);
    Require(mediumConfidenceLiftRecommendations.exposure.valid &&
            !mediumConfidenceLiftRecommendations.exposure.autoApplyAllowed &&
            mediumConfidenceLiftRecommendations.exposure.confidence >= 0.50f &&
            mediumConfidenceLiftRecommendations.exposure.confidence < 0.70f,
        "Medium-confidence RAW exposure recommendation should be valid but below the full one-click gate");
    const RawAutoStartPoint::RawAutoStartPointConservativePlan cautiousNoStageLiftPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            mediumConfidenceLiftAnalysis,
            mediumConfidenceLiftRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(std::abs(cautiousNoStageLiftPlan.upstreamRecipe.preToneExposureEv - 0.25f) < 0.001f,
        "Starting Point should move RAW Exposure by the cautious visible nudge for medium-confidence exposure evidence");
    Require(StartPointControlListContains(
            cautiousNoStageLiftPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Medium-confidence cautious RAW Exposure nudge should be reported as a visible applied control");
    Require(StringListContainsFragment(cautiousNoStageLiftPlan.withheldSummaries, "cautious 0.25 EV cap"),
        "Medium-confidence cautious RAW Exposure nudge should report that the remaining lift stays manual");

    const Stack::RawAnalysis::RawImageAnalysis lowConfidenceLiftAnalysis =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.2f, 0.0f, 13.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    const RawAutoBase::AutoBaseRecommendations lowConfidenceLiftRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(lowConfidenceLiftAnalysis, recipe);
    Require(lowConfidenceLiftRecommendations.exposure.valid &&
            !lowConfidenceLiftRecommendations.exposure.autoApplyAllowed &&
            lowConfidenceLiftRecommendations.exposure.deltaEv > 0.50f &&
            std::abs(lowConfidenceLiftRecommendations.exposure.confidence - 0.35f) < 0.001f,
        "Low-confidence positive RAW exposure recommendation should be valid but below the cautious lift gate");
    const RawAutoStartPoint::RawAutoStartPointConservativePlan lowConfidenceLiftPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            lowConfidenceLiftAnalysis,
            lowConfidenceLiftRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(std::abs(lowConfidenceLiftPlan.upstreamRecipe.preToneExposureEv - 0.15f) < 0.001f,
        "Starting Point should visibly move RAW Exposure by a tiny positive nudge for lower-confidence safe lifts");
    Require(StartPointControlListContains(
            lowConfidenceLiftPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Lower-confidence tiny RAW Exposure lift should be reported as a visible applied control");
    Require(StringListContainsFragment(lowConfidenceLiftPlan.withheldSummaries, "tiny positive one-click cap"),
        "Lower-confidence tiny RAW Exposure lift should report that additional lift remains manual");

    const Stack::RawAnalysis::RawImageAnalysis lowConfidenceLoweringAnalysis =
        BuildAutoBaseTestAnalysis(-7.0f, -5.0f, 0.0f, 2.0f, 2.2f, 11.0f, 0.0f, 0.0f, 0.06f);
    const RawAutoBase::AutoBaseRecommendations lowConfidenceLoweringRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(lowConfidenceLoweringAnalysis, recipe);
    Require(lowConfidenceLoweringRecommendations.exposure.valid &&
            !lowConfidenceLoweringRecommendations.exposure.autoApplyAllowed &&
            lowConfidenceLoweringRecommendations.exposure.deltaEv < -0.25f &&
            lowConfidenceLoweringRecommendations.exposure.confidence >= 0.35f &&
            lowConfidenceLoweringRecommendations.exposure.confidence < 0.50f,
        "Low-confidence negative RAW exposure recommendation should be valid but below the cautious lift gate");
    const RawAutoStartPoint::RawAutoStartPointConservativePlan lowConfidenceLoweringPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            lowConfidenceLoweringAnalysis,
            lowConfidenceLoweringRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(std::abs(lowConfidenceLoweringPlan.upstreamRecipe.preToneExposureEv + 0.25f) < 0.001f,
        "Starting Point should visibly move RAW Exposure by a small downward nudge when the proposal lowers exposure");
    Require(StartPointControlListContains(
            lowConfidenceLoweringPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Lower-confidence downward RAW Exposure nudge should be reported as a visible applied control");
    Require(StringListContainsFragment(lowConfidenceLoweringPlan.withheldSummaries, "RAW Exposure lowering"),
        "Lower-confidence downward RAW Exposure nudge should report that additional lowering remains manual");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics stagedLiftDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan stagedLiftPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            stagedLiftDiagnostics);
    Require(std::abs(stagedLiftPlan.upstreamRecipe.preToneExposureEv - 0.50f) < 0.001f,
        "Staged Starting Point should cap a larger safe RAW Exposure lift to the visible one-click limit");
    Require(StartPointControlListContains(
            stagedLiftPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Staged capped RAW Exposure should be reported as a visible applied control");
    Require(StringListContainsFragment(stagedLiftPlan.withheldSummaries, "one-click cap"),
        "Staged capped RAW Exposure should report that additional lift remains withheld");
    Require(stagedLiftPlan.needsPostApplyDisplayFit && !stagedLiftPlan.usedRenderedDisplayFit,
        "Staged capped RAW Exposure without rendered Display Candidate should still queue post-apply Display Fit");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics partialBaseDryRun =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            stagedLiftDiagnostics,
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan partialBasePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            partialBaseDryRun);
    Require(StringListContainsFragment(
            partialBasePlan.evidenceSummaries,
            "Base candidate evidence partial:"),
        "Starting Point plan should summarize partial Base candidate evidence from the dry-run stages");
    Require(StringListContainsFragment(
            partialBasePlan.evidenceSummaries,
            "Raw Placement projected"),
        "Starting Point plan should name projected Raw Placement evidence when Base has no rendered placement");
    Require(StringListContainsFragment(
            partialBasePlan.evidenceSummaries,
            "Display Candidate pending"),
        "Starting Point plan should name pending Display Candidate evidence before the matching render finishes");
    Require(StringListContainsFragment(
            partialBasePlan.warnings,
            "partial Base candidate evidence"),
        "Starting Point plan should warn when an applied plan still carries partial Base evidence");

    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedDisplayFitDiagnostics =
        stagedLiftDiagnostics;
    RawAutoStartPoint::RawAutoStartPointCandidate renderedBaseCandidate;
    renderedBaseCandidate.valid = true;
    renderedBaseCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    renderedBaseCandidate.hasRecipe = true;
    renderedBaseCandidate.recipe = recipe;
    renderedBaseCandidate.recipe.preToneExposureEv = stagedLiftPlan.upstreamRecipe.preToneExposureEv;
    renderedBaseCandidate.recipe.viewTransform.layerJson =
        RawRecipe::DefaultViewTransformJson();
    renderedBaseCandidate.recipe.viewTransform.layerJson["middleGrey"] = 0.421f;
    renderedBaseCandidate.recipe.viewTransform.layerJson["blackEv"] = -6.5f;
    renderedBaseCandidate.recipe.viewTransform.layerJson["whiteEv"] = 5.25f;
    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedDisplayFitStage;
    renderedDisplayFitStage.stage = RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate;
    renderedDisplayFitStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedDisplayFitStage.confidence01 = 1.0f;
    renderedDisplayFitStage.statusMessage =
        "Rendered Base candidate evidence for Display Candidate from the queued visible recipe after fitting View Transform.";
    renderedDisplayFitStage.display.valid = true;
    renderedDisplayFitStage.display.status =
        RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedDisplayFitStage.display.displayP05 = 0.10f;
    renderedDisplayFitStage.display.displayP50 = 0.45f;
    renderedDisplayFitStage.display.displayP95 = 0.82f;
    renderedDisplayFitStage.display.displaySpread = 0.72f;
    renderedDisplayFitStage.display.statusMessage = renderedDisplayFitStage.statusMessage;
    renderedBaseCandidate.stageDiagnostics.push_back(renderedDisplayFitStage);
    renderedDisplayFitDiagnostics.candidates.push_back(renderedBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan renderedDisplayFitPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            renderedDisplayFitDiagnostics);
    Require(renderedDisplayFitPlan.hasUpstreamRecipeChanges &&
            !renderedDisplayFitPlan.needsPostApplyDisplayFit &&
            renderedDisplayFitPlan.usedRenderedDisplayFit,
        "Rendered Base Display Candidate evidence should let Starting Point apply RAW Exposure and fitted Display Fit in one visible recipe edit");
    Require(std::abs(
            renderedDisplayFitPlan.upstreamRecipe.viewTransform.layerJson.value("middleGrey", 0.0f) -
            0.421f) < 0.001f,
        "Rendered Display Fit plan should carry the fitted View Transform recipe into the applied upstream recipe");
    Require(
        renderedDisplayFitPlan.visibleEdits.viewTransformSummary.find("rendered") !=
            std::string::npos,
        "Rendered Display Fit plan should explain that Display Fit came from rendered candidate evidence");
    Require(
        renderedDisplayFitPlan.visibleEdits.viewTransformSummary.find("middle grey 0.421") !=
                std::string::npos &&
            renderedDisplayFitPlan.visibleEdits.viewTransformSummary.find("black -6.50 EV") !=
                std::string::npos &&
            renderedDisplayFitPlan.visibleEdits.viewTransformSummary.find("white +5.25 EV") !=
                std::string::npos,
        "Rendered Display Fit plan should report the fitted View Transform values");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics lowHeadroomDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 0.05f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan lowHeadroomPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            lowHeadroomDiagnostics);
    Require(std::abs(lowHeadroomPlan.upstreamRecipe.preToneExposureEv - recipe.preToneExposureEv) < 0.001f,
        "Staged Starting Point should not lift RAW Exposure when Raw Technical headroom is too low");
    Require(StringListContainsFragment(lowHeadroomPlan.withheldSummaries, "Raw Technical headroom"),
        "Low-headroom staged RAW Exposure plan should name the Raw Technical headroom gate");

    const Stack::RawAnalysis::RawImageAnalysis clippedDark =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.5f, -0.2f, 8.0f, 0.0f, 0.0f, 0.08f, 0.0f, false, true);
    const RawAutoBase::AutoBaseRecommendations clippedRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(clippedDark, recipe);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan clippedPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            clippedDark,
            clippedRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(std::abs(clippedPlan.upstreamRecipe.preToneExposureEv - recipe.preToneExposureEv) < 0.001f,
        "Highlight-blocked Starting Point plan should not lift RAW Exposure");
    Require(clippedPlan.hasUpstreamRecipeChanges &&
            StartPointControlListContains(
                clippedPlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::LocalRange),
        "Highlight-blocked dark Starting Point plan should recover shadows through a conservative visible Local Range point while RAW Exposure stays blocked");
    Require(StartPointControlListContains(
            clippedPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
        "Highlight-blocked dark Starting Point plan should still report Display Fit as the visible applied control");
    Require(!StartPointControlListContains(
            clippedPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Highlight-blocked dark Starting Point plan should not report RAW Exposure as applied");
    Require(StringListContainsFragment(clippedPlan.appliedSummaries, "Display Fit refresh"),
        "Highlight-blocked dark Starting Point plan should list Display Fit refresh in applied summaries");
    Require(StringListContainsFragment(clippedPlan.withheldSummaries, "highlight risk"),
        "Highlight-blocked Starting Point plan should explain the withheld RAW Exposure");
    Require(clippedPlan.summary.find("Display Fit refresh") != std::string::npos &&
            clippedPlan.summary.find("Withheld:") != std::string::npos,
        "Highlight-blocked dark Starting Point plan summary should expose both applied and withheld controls");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics clippedWithHeadroomDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan clippedWithHeadroomPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            clippedDark,
            clippedRecommendations,
            clippedWithHeadroomDiagnostics);
    Require(clippedWithHeadroomPlan.upstreamRecipe.preToneExposureEv > recipe.preToneExposureEv,
        "Dark Starting Point plan should lift RAW Exposure when highlight risk has agreeing Neutral Scene and Raw Technical headroom evidence");
    Require(StartPointControlListContains(
            clippedWithHeadroomPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Dark highlight-risk plan with staged headroom should report RAW Exposure as applied");
    Require(StringListContainsFragment(clippedWithHeadroomPlan.appliedSummaries, "RAW Exposure"),
        "Dark highlight-risk plan with staged headroom should list RAW Exposure in applied summaries");
    Require(!StringListContainsFragment(clippedWithHeadroomPlan.withheldSummaries, "highlight risk"),
        "Dark highlight-risk plan with staged headroom should not leave RAW Exposure blocked only by highlight risk");
    Require(StringListContainsFragment(clippedWithHeadroomPlan.evidenceSummaries, "Raw Technical safety ledger"),
        "Dark highlight-risk plan with staged headroom should name the Raw Technical evidence that made the lift safe");

    RawAutoBase::AutoBaseRecommendations localRecommendations = safeRecommendations;
    localRecommendations.exposure = RawAutoBase::RawExposureRecommendation();
    RawAutoBase::SuggestedLocalAdjustment local;
    local.valid = true;
    local.kind = RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows;
    local.targetEv = -4.0f;
    local.deltaEv = 0.45f;
    local.widthEv = 1.2f;
    local.feather = 0.6f;
    local.confidence = 0.90f;
    local.affectedAreaPercent = 12.0f;
    local.label = "Open shadows";
    localRecommendations.localAdjustments = { local };
    const RawAutoStartPoint::RawAutoStartPointConservativePlan localPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            localRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(localPlan.upstreamRecipe.localRange.enabled,
        "Strict high-confidence Local Range suggestion should be authored in the Starting Point plan");
    Require(StringListContainsFragment(localPlan.appliedSummaries, "Local Range"),
        "Starting Point plan should list applied Local Range when the strict gate passes");
    Require(localPlan.visibleEdits.localRangeSummary.find("width 1.20 EV") !=
                std::string::npos &&
            localPlan.visibleEdits.localRangeSummary.find("feather 0.60") !=
                std::string::npos &&
            localPlan.visibleEdits.localRangeSummary.find("area 12.0%") !=
                std::string::npos,
        "Starting Point Local Range summary should report the authored point width, feather, and affected area");

    RawAutoBase::AutoBaseRecommendations staleLocalRecommendations = largeLiftRecommendations;
    staleLocalRecommendations.localAdjustments = { local };
    const RawAutoStartPoint::RawAutoStartPointConservativePlan staleLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            staleLocalRecommendations,
            stagedLiftDiagnostics);
    Require(staleLocalPlan.upstreamRecipe.localRange.enabled &&
            staleLocalPlan.visibleEdits.localRangeValid,
        "Strict Local Range suggestion should author a shifted visible graph point when RAW Exposure changed first");
    Require(staleLocalPlan.visibleEdits.localRangeSummary.find("scene -3.50 EV") !=
                std::string::npos,
        "Shifted Local Range summary should move the target scene EV by the applied RAW Exposure delta");
    Require(StringListContainsFragment(
            staleLocalPlan.evidenceSummaries,
            "shifted by RAW Exposure"),
        "Shifted Local Range plan should explain the transformed evidence source");
    Require(!StringListContainsFragment(staleLocalPlan.withheldSummaries, "Local Range pending"),
        "Shifted Local Range plan should not leave the graph point pending");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedLocalStage;
    renderedLocalStage.stage = RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    renderedLocalStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedLocalStage.confidence01 = 1.0f;
    renderedLocalStage.statusMessage =
        "Rendered Base candidate evidence for Local Candidate from the queued visible recipe.";
    renderedLocalStage.scene.valid = true;
    renderedLocalStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate;
    renderedLocalStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    renderedLocalStage.scene.evPercentiles.valid = true;
    renderedLocalStage.scene.evPercentiles.p25 = -4.2f;
    renderedLocalStage.scene.evPercentiles.p50 = -3.8f;
    renderedLocalStage.scene.evPercentiles.p75 = -2.9f;
    renderedLocalStage.scene.midSpreadEv = 1.3f;
    renderedLocalStage.scene.wideSpreadEv = 3.2f;
    renderedLocalStage.scene.statusMessage = renderedLocalStage.statusMessage;
    RawAutoStartPoint::RawAutoStartPointCandidate renderedLocalBaseCandidate;
    renderedLocalBaseCandidate.valid = true;
    renderedLocalBaseCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    renderedLocalBaseCandidate.hasRecipe = true;
    renderedLocalBaseCandidate.recipe = stagedLiftPlan.upstreamRecipe;
    RawAutoBase::SuggestedLocalAdjustment shiftedLocal = local;
    shiftedLocal.targetEv += stagedLiftPlan.upstreamRecipe.preToneExposureEv - recipe.preToneExposureEv;
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(
            shiftedLocal,
            renderedLocalBaseCandidate.recipe),
        "Rendered post-exposure Local Candidate test fixture should carry the shifted RAW-Exposure-plus-Local-Range recipe");
    renderedLocalBaseCandidate.stageDiagnostics.push_back(renderedLocalStage);

    RawAutoStartPoint::RawAutoStartPointCandidate staleRenderedLocalBaseCandidate =
        renderedLocalBaseCandidate;
    staleRenderedLocalBaseCandidate.recipe = recipe;
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(
            local,
            staleRenderedLocalBaseCandidate.recipe),
        "Stale rendered Local Candidate test fixture should still carry a complete but mismatched Local Range recipe");
    RawAutoStartPoint::RawAutoStartPointDiagnostics staleRenderedLocalDiagnostics =
        stagedLiftDiagnostics;
    staleRenderedLocalDiagnostics.candidates.push_back(staleRenderedLocalBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan staleRenderedLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            staleLocalRecommendations,
            staleRenderedLocalDiagnostics);
    Require(staleRenderedLocalPlan.upstreamRecipe.localRange.enabled,
        "Mismatched rendered Local Candidate evidence should still allow shifted strict Local Range authoring from current evidence");
    Require(StringListContainsFragment(
            staleRenderedLocalPlan.evidenceSummaries,
            "shifted by RAW Exposure"),
        "Mismatched rendered Local Candidate path should name the shifted current-evidence fallback");

    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedLocalDiagnostics =
        stagedLiftDiagnostics;
    renderedLocalDiagnostics.candidates.push_back(renderedLocalBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan renderedLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            staleLocalRecommendations,
            renderedLocalDiagnostics);
    Require(renderedLocalPlan.upstreamRecipe.localRange.enabled &&
            renderedLocalPlan.visibleEdits.localRangeValid,
        "Rendered post-exposure Local Candidate evidence should let Starting Point author the strict visible Local Range graph");
    Require(StartPointControlListContains(
            renderedLocalPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::LocalRange),
        "Rendered Local Range apply should be reported as a visible touched control");
    Require(StringListContainsFragment(renderedLocalPlan.appliedSummaries, "Local Range"),
        "Rendered Local Range plan should list Local Range in applied summaries");
    Require(renderedLocalPlan.needsPostApplyDisplayFit && !renderedLocalPlan.usedRenderedDisplayFit,
        "Rendered Local Range after RAW Exposure should keep Display Fit in the post-apply path");
    Require(!StringListContainsFragment(renderedLocalPlan.withheldSummaries, "Local Range pending"),
        "Rendered Local Range plan should not call post-exposure Local Candidate evidence pending");
    Require(!StringListContainsFragment(renderedLocalPlan.withheldSummaries, "policy is still conservative"),
        "Rendered Local Range plan should no longer hit the placeholder conservative-policy withholding branch");

    localRecommendations.localAdjustments[0].confidence = 0.80f;
    const RawAutoStartPoint::RawAutoStartPointConservativePlan cautiousLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            localRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(cautiousLocalPlan.upstreamRecipe.localRange.enabled &&
            cautiousLocalPlan.visibleEdits.localRangeValid,
        "Cautious Local Range suggestion should author one visible graph point when confidence and delta are within the one-click nudge gate");
    Require(StartPointControlListContains(
            cautiousLocalPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::LocalRange),
        "Cautious Local Range apply should be reported as a visible touched control");
    Require(StringListContainsFragment(
            cautiousLocalPlan.evidenceSummaries,
            "cautious current Local Candidate evidence"),
        "Cautious Local Range plan should name the cautious evidence gate");

    RawAutoBase::AutoBaseRecommendations widerCautiousLocalRecommendations = localRecommendations;
    widerCautiousLocalRecommendations.localAdjustments[0].confidence = 0.72f;
    widerCautiousLocalRecommendations.localAdjustments[0].deltaEv = 0.58f;
    const RawAutoStartPoint::RawAutoStartPointConservativePlan widerCautiousLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            widerCautiousLocalRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(widerCautiousLocalPlan.upstreamRecipe.localRange.enabled &&
            widerCautiousLocalPlan.visibleEdits.localRangeValid,
        "Cautious Local Range suggestion should author a visible graph point up to the 0.60 EV one-click cap");
    Require(widerCautiousLocalPlan.visibleEdits.localRangeSummary.find("+0.58 EV") !=
                std::string::npos,
        "Wider cautious Local Range summary should show the applied editable graph-point value");
    Require(StartPointControlListContains(
            widerCautiousLocalPlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::LocalRange),
        "Wider cautious Local Range apply should be reported as a visible touched control");

    RawAutoBase::AutoBaseRecommendations cappedCautiousLocalRecommendations = localRecommendations;
    cappedCautiousLocalRecommendations.localAdjustments[0].confidence = 0.80f;
    cappedCautiousLocalRecommendations.localAdjustments[0].deltaEv = 0.95f;
    const RawAutoStartPoint::RawAutoStartPointConservativePlan cappedCautiousLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            cappedCautiousLocalRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(cappedCautiousLocalPlan.upstreamRecipe.localRange.enabled &&
            cappedCautiousLocalPlan.visibleEdits.localRangeValid,
        "An eligible Local Range proposal above the one-click limit should author a safely capped visible graph point instead of being dropped");
    Require(cappedCautiousLocalPlan.visibleEdits.localRangeSummary.find("+0.60 EV") !=
            std::string::npos,
        "Safely capped Local Range summary should show the visible 0.60 EV graph value");

    RawAutoBase::AutoBaseRecommendations contextualLocalRecommendations = localRecommendations;
    contextualLocalRecommendations.localAdjustments[0].confidence = 0.55f;
    contextualLocalRecommendations.localAdjustments[0].deltaEv = 0.90f;
    const RawAutoStartPoint::RawAutoStartPointConservativePlan contextualLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            contextualLocalRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(contextualLocalPlan.upstreamRecipe.localRange.enabled &&
            contextualLocalPlan.visibleEdits.localRangeValid,
        "A medium-confidence contextual shadow proposal should author a conservative visible graph point");
    Require(contextualLocalPlan.visibleEdits.localRangeSummary.find("+0.45 EV") !=
            std::string::npos,
        "Contextual Local Range should cap the visible graph point at 0.45 EV");

    RawAutoBase::AutoBaseRecommendations twoPointLocalRecommendations = localRecommendations;
    RawAutoBase::SuggestedLocalAdjustment secondLocal = local;
    secondLocal.kind = RawAutoBase::SuggestedLocalAdjustmentKind::ProtectSky;
    secondLocal.targetEv = -1.4f;
    secondLocal.deltaEv = -0.35f;
    secondLocal.confidence = 0.82f;
    secondLocal.affectedAreaPercent = 8.0f;
    secondLocal.label = "Protect sky";
    twoPointLocalRecommendations.localAdjustments = {
        localRecommendations.localAdjustments[0],
        secondLocal
    };
    const RawAutoStartPoint::RawAutoStartPointConservativePlan twoPointLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            twoPointLocalRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(twoPointLocalPlan.upstreamRecipe.localRange.enabled &&
            twoPointLocalPlan.visibleEdits.localRangeValid &&
            twoPointLocalPlan.visibleEdits.localRangePointCount == 2,
        "Build Starting Point should author two visible Local Range graph points when both pass the one-click gate");
    Require(twoPointLocalPlan.visibleEdits.localRangeSummary.find("2 authored adjustment points") !=
            std::string::npos &&
            twoPointLocalPlan.visibleEdits.localRangeSummary.find("Open shadows") !=
                std::string::npos &&
            twoPointLocalPlan.visibleEdits.localRangeSummary.find("Protect sky") !=
                std::string::npos,
        "Two-point Local Range summary should name both authored graph points");
    Require(StringListContainsFragment(twoPointLocalPlan.appliedSummaries, "Local Range 2 points"),
        "Two-point Local Range plan should list both visible graph points as applied");

    localRecommendations.localAdjustments[0].confidence = 0.45f;
    const RawAutoStartPoint::RawAutoStartPointConservativePlan withheldLocalPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            localRecommendations,
            RawAutoStartPoint::RawAutoStartPointDiagnostics());
    Require(!withheldLocalPlan.upstreamRecipe.localRange.enabled,
        "Below-cautious Local Range suggestion should be withheld from one-click Starting Point");
    Require(StringListContainsFragment(withheldLocalPlan.withheldSummaries, "50% contextual"),
        "Withheld Local Range plan should name the contextual and cautious one-click gates");

    RawAutoStartPoint::RawAutoStartPointDiagnostics darkRevealDiagnostics =
        BuildPlannerToneDiagnostics(4.34f, 8.0f);
    darkRevealDiagnostics.candidates[0].stageDiagnostics[0].scene.evPercentiles.p01 = -8.0f;
    darkRevealDiagnostics.candidates[0].stageDiagnostics[0].scene.evPercentiles.p05 = -7.0f;
    darkRevealDiagnostics.candidates[0].stageDiagnostics[0].scene.evPercentiles.p25 = -6.1f;
    darkRevealDiagnostics.candidates[0].stageDiagnostics[0].scene.evPercentiles.p50 = -5.0f;
    darkRevealDiagnostics.candidates[0].stageDiagnostics[0].scene.evPercentiles.p75 = -1.8f;
    darkRevealDiagnostics.candidates[0].stageDiagnostics[0].scene.evPercentiles.p95 = 1.0f;
    darkRevealDiagnostics.candidates[0].stageDiagnostics[0].scene.evPercentiles.p99 = 2.0f;
    const Stack::RawAnalysis::RawImageAnalysis darkRevealAnalysis =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.3f, -0.1f, 8.0f, 0.0f, 0.0f, 0.08f, 0.0f, false, true);
    RawAutoBase::SuggestedLocalAdjustment darkProtectSky = local;
    darkProtectSky.kind = RawAutoBase::SuggestedLocalAdjustmentKind::ProtectSky;
    darkProtectSky.targetEv = -0.8f;
    darkProtectSky.deltaEv = -0.45f;
    darkProtectSky.confidence = 0.92f;
    darkProtectSky.colorQualifierEnabled = true;
    darkProtectSky.label = "Protect sky";
    RawAutoBase::SuggestedLocalAdjustment darkOpenShadows = local;
    darkOpenShadows.targetEv = -6.0f;
    darkOpenShadows.deltaEv = 0.65f;
    darkOpenShadows.confidence = 0.55f;
    darkOpenShadows.affectedAreaPercent = 58.0f;
    darkOpenShadows.label = "Open shadows";
    RawAutoBase::AutoBaseRecommendations darkRevealRecommendations = safeRecommendations;
    darkRevealRecommendations.exposure = RawAutoBase::RawExposureRecommendation();
    darkRevealRecommendations.localAdjustments = { darkProtectSky, darkOpenShadows };
    const RawAutoStartPoint::RawAutoStartPointConservativePlan darkRevealPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            darkRevealAnalysis,
            darkRevealRecommendations,
            darkRevealDiagnostics);
    Require(darkRevealPlan.upstreamRecipe.localRange.enabled &&
            darkRevealPlan.visibleEdits.localRangeValid,
        "Dark reveal Starting Point should author a shadow-opening Local Range point even when a protective color suggestion appears first");
    Require(std::any_of(
            darkRevealPlan.upstreamRecipe.localRange.points.begin(),
            darkRevealPlan.upstreamRecipe.localRange.points.end(),
            [](const Stack::RawRecipe::RawLocalRangePoint& point) {
                return point.deltaEv > 0.70f;
            }),
        "Dark reveal Local Range should promote the editable shadow-lift graph point beyond the old 0.60 EV cap");
    Require(darkRevealPlan.visibleEdits.localRangeSummary.find("Open shadows") != std::string::npos,
        "Dark reveal Local Range summary should name the shadow-opening authored point");
    Require(darkRevealPlan.visibleEdits.localRangeSummary.find("Protect sky") == std::string::npos,
        "Dark reveal Local Range should not let a protective color target block the shadow-opening point");
    Require(darkRevealPlan.visibleEdits.finishToneValid &&
            darkRevealPlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Dark reveal Starting Point should author a shadow-reveal Finish Tone curve even when global spread is already wide");
    Require(StringListContainsFragment(darkRevealPlan.appliedSummaries, "Shadow-reveal Finish Tone"),
        "Dark reveal plan should list Shadow-reveal Finish Tone as an applied visible control");
    Require(StringListContainsFragment(darkRevealPlan.evidenceSummaries, "dark-reveal"),
        "Dark reveal plan should name the dark-reveal evidence gate");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics toneDiagnostics =
        BuildPlannerToneDiagnostics(1.8f, 3.5f);
    RawAutoBase::AutoBaseRecommendations toneOnlyRecommendations = safeRecommendations;
    toneOnlyRecommendations.exposure = RawAutoBase::RawExposureRecommendation();
    Stack::RawAnalysis::RawImageAnalysis toneSafeAnalysis = safeAnalysis;
    toneSafeAnalysis.highlight = Stack::RawAnalysis::HighlightRiskReport();
    const RawAutoStartPoint::RawAutoStartPointConservativePlan tonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            toneSafeAnalysis,
            toneOnlyRecommendations,
            toneDiagnostics);
    Require(tonePlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Mild low-risk Finish Tone proposal should author visible tone points");
    Require(StringListContainsFragment(tonePlan.appliedSummaries, "Mild Finish Tone"),
        "Starting Point plan should list Mild Finish Tone when it is applied");
    Require(tonePlan.visibleEdits.finishToneSummary.find("5 visible points") !=
                std::string::npos &&
            tonePlan.visibleEdits.finishToneSummary.find("shadow y") !=
                std::string::npos &&
            tonePlan.visibleEdits.finishToneSummary.find("light y") !=
                std::string::npos,
        "Starting Point Finish Tone summary should expose the authored graph point movement");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics dynamicRangeToneDiagnostics =
        BuildPlannerToneDiagnostics(2.80f, 4.20f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan dynamicRangeTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            toneOnlyRecommendations,
            dynamicRangeToneDiagnostics);
    Require(dynamicRangeTonePlan.visibleEdits.finishToneValid &&
            dynamicRangeTonePlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Wide dynamic range with sufficient existing contrast should still author a mild visible balancing curve");
    Require(StringListContainsFragment(
            dynamicRangeTonePlan.appliedSummaries,
            "Dynamic-range Finish Tone"),
        "Wide dynamic-range plan should identify the visible Finish Tone lane it applied");
    Require(dynamicRangeTonePlan.visibleEdits.localRangeValid &&
            dynamicRangeTonePlan.visibleEdits.localRangeSummary.find("Balance deep shadows") !=
                std::string::npos,
        "Wide dynamic-range staged evidence should author a conservative visible Local Range fallback when semantic suggestions are absent");
    Require(dynamicRangeTonePlan.visibleEdits.localRangeSummary.rfind("Local Range:", 0) == 0,
        "Applied Local Range value summaries should carry the control label so the graph readout cannot fall back to stale unchanged text");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics nearBoundaryRangeDiagnostics =
        BuildPlannerToneDiagnostics(2.76f, 3.99f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan nearBoundaryRangePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            toneSafeAnalysis,
            toneOnlyRecommendations,
            nearBoundaryRangeDiagnostics);
    Require(nearBoundaryRangePlan.visibleEdits.localRangeValid &&
            nearBoundaryRangePlan.visibleEdits.localRangeSummary.find("Balance deep shadows") !=
                std::string::npos,
        "A staged 3.99 EV spread should not miss the visible Local Range fallback because of a brittle 4.00 EV boundary");

    const Stack::RawAnalysis::RawImageAnalysis currentFrameWideRangeAnalysis =
        BuildAutoBaseTestAnalysis(-7.0f, -5.0f, -2.0f, 2.0f, 3.0f, 10.0f);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics compressedStageToneDiagnostics =
        BuildPlannerToneDiagnostics(2.60f, 2.76f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan currentFrameWideRangePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            currentFrameWideRangeAnalysis,
            toneOnlyRecommendations,
            compressedStageToneDiagnostics);
    Require(currentFrameWideRangePlan.visibleEdits.localRangeValid &&
            currentFrameWideRangePlan.visibleEdits.localRangeSummary.find("Balance deep shadows") !=
                std::string::npos,
        "Broad current-frame RAW range should author the conservative Local Range fallback when the staged spread alone is compressed");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedPostLocalToneStage =
        toneDiagnostics.candidates[0].stageDiagnostics[0];
    renderedPostLocalToneStage.statusMessage =
        "Rendered Base candidate evidence for Finish Tone Candidate after the queued visible Local Range recipe.";
    renderedPostLocalToneStage.scene.statusMessage = renderedPostLocalToneStage.statusMessage;
    RawAutoStartPoint::RawAutoStartPointCandidate renderedPostLocalToneCandidate;
    renderedPostLocalToneCandidate.valid = true;
    renderedPostLocalToneCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    renderedPostLocalToneCandidate.hasRecipe = true;
    renderedPostLocalToneCandidate.recipe = twoPointLocalPlan.upstreamRecipe;
    renderedPostLocalToneCandidate.stageDiagnostics.push_back(renderedPostLocalToneStage);
    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedPostLocalToneDiagnostics;
    renderedPostLocalToneDiagnostics.candidates.push_back(renderedPostLocalToneCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan postLocalTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            safeAnalysis,
            twoPointLocalRecommendations,
            renderedPostLocalToneDiagnostics);
    Require(postLocalTonePlan.visibleEdits.localRangeValid &&
            postLocalTonePlan.visibleEdits.localRangePointCount == 2 &&
            postLocalTonePlan.visibleEdits.finishToneValid,
        "Exact post-local Finish Tone Candidate evidence should let Build Starting Point author Local Range and Finish Tone visibly");
    Require(StartPointControlListContains(
                postLocalTonePlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::LocalRange) &&
            StartPointControlListContains(
                postLocalTonePlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::FinishTone),
        "Post-local tone plan should report both Local Range and Finish Tone as visible touched controls");
    Require(!StringListContainsFragment(postLocalTonePlan.withheldSummaries, "Finish Tone pending"),
        "Exact post-local tone evidence should not leave Finish Tone pending after Local Range authoring");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics cautiousToneDiagnostics =
        BuildPlannerToneDiagnostics(1.5f, 3.5f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan cautiousTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            toneSafeAnalysis,
            toneOnlyRecommendations,
            cautiousToneDiagnostics);
    Require(cautiousTonePlan.visibleEdits.finishToneValid &&
            cautiousTonePlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Cautious Finish Tone proposal should author visible curve points when strength is within the one-click cap");
    Require(StartPointControlListContains(
            cautiousTonePlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::FinishTone),
        "Cautious Finish Tone apply should be reported as a visible touched control");
    Require(StringListContainsFragment(
            cautiousTonePlan.evidenceSummaries,
            "cautious current pre-display tone evidence"),
        "Cautious Finish Tone plan should name the cautious evidence gate");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics widerCautiousToneDiagnostics =
        BuildPlannerToneDiagnostics(1.42f, 3.5f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan widerCautiousTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            toneSafeAnalysis,
            toneOnlyRecommendations,
            widerCautiousToneDiagnostics);
    Require(widerCautiousTonePlan.visibleEdits.finishToneValid &&
            widerCautiousTonePlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Wider cautious Finish Tone proposal should author visible curve points up to the 0.20 one-click cap");
    Require(StartPointControlListContains(
            widerCautiousTonePlan.visibleEdits.touchedControls,
            RawAutoStartPoint::RawAutoStartPointControl::FinishTone),
        "Wider cautious Finish Tone apply should be reported as a visible touched control");
    Require(widerCautiousTonePlan.visibleEdits.finishToneSummary.find("strength 0.20") !=
                std::string::npos,
        "Wider cautious Finish Tone summary should show the applied visible curve strength");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics strongToneDiagnostics =
        BuildPlannerToneDiagnostics(1.1f, 3.5f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan strongTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            toneSafeAnalysis,
            toneOnlyRecommendations,
            strongToneDiagnostics);
    Require(!strongTonePlan.visibleEdits.finishToneValid &&
            !StartPointControlListContains(
                strongTonePlan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::FinishTone),
        "Stronger Finish Tone proposal should stay withheld from one-click Starting Point");
    Require(StringListContainsFragment(strongTonePlan.withheldSummaries, "0.20 one-click cautious curve cap"),
        "Withheld strong Finish Tone plan should name the cautious one-click strength cap");

    RawAutoStartPoint::RawAutoStartPointDiagnostics staleToneDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    if (!staleToneDiagnostics.candidates.empty() &&
        !toneDiagnostics.candidates.empty() &&
        !toneDiagnostics.candidates[0].stageDiagnostics.empty()) {
        staleToneDiagnostics.candidates[0].stageDiagnostics.push_back(
            toneDiagnostics.candidates[0].stageDiagnostics[0]);
    }
    const RawAutoStartPoint::RawAutoStartPointConservativePlan staleTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            staleToneDiagnostics);
    Require(!staleTonePlan.visibleEdits.finishToneValid,
        "Mild Finish Tone should be withheld when upstream Starting Point edits changed first");
    Require(StringListContainsFragment(staleTonePlan.withheldSummaries, "Finish Tone pending"),
        "Stale Finish Tone plan should report that post-edit Finish Tone Candidate evidence is pending");
    Require(StringListContainsFragment(staleTonePlan.warnings, "measured before the applied upstream"),
        "Stale Finish Tone plan should explain why the mild proposal was withheld");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics projectedToneDryRun =
        RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            staleToneDiagnostics,
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan projectedTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            projectedToneDryRun);
    Require(projectedTonePlan.visibleEdits.finishToneValid,
        "Projected post-exposure Finish Tone Candidate evidence should let mild tone author visible points on the app dry-run path");
    Require(projectedTonePlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Projected Finish Tone plan should carry visible graph points into the applied recipe");
    Require(StringListContainsFragment(
            projectedTonePlan.evidenceSummaries,
            "projected post-exposure Finish Tone Candidate"),
        "Projected Finish Tone plan should name the projected post-exposure evidence source");
    Require(!StringListContainsFragment(projectedTonePlan.withheldSummaries, "Finish Tone pending"),
        "Projected Finish Tone plan should not leave the safe visible curve pending");

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics renderedToneStage =
        toneDiagnostics.candidates[0].stageDiagnostics[0];
    renderedToneStage.statusMessage =
        "Rendered Base candidate evidence for Finish Tone Candidate after the queued visible RAW Exposure recipe.";
    renderedToneStage.scene.statusMessage = renderedToneStage.statusMessage;
    RawAutoStartPoint::RawAutoStartPointCandidate renderedToneBaseCandidate;
    renderedToneBaseCandidate.valid = true;
    renderedToneBaseCandidate.kind = RawAutoStartPoint::RawAutoStartPointCandidateKind::Base;
    renderedToneBaseCandidate.hasRecipe = true;
    renderedToneBaseCandidate.recipe = recipe;
    renderedToneBaseCandidate.recipe.preToneExposureEv =
        stagedLiftPlan.upstreamRecipe.preToneExposureEv;
    renderedToneBaseCandidate.stageDiagnostics.push_back(renderedToneStage);

    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedToneDiagnostics =
        stagedLiftDiagnostics;
    renderedToneDiagnostics.candidates.push_back(renderedToneBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan renderedTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            renderedToneDiagnostics);
    Require(renderedTonePlan.visibleEdits.finishToneValid,
        "Rendered post-exposure Finish Tone Candidate evidence should let mild tone author visible points when Display Fit will still run after apply");
    Require(renderedTonePlan.upstreamRecipe.finishTone.layerJson.contains("points"),
        "Rendered Finish Tone plan should carry visible graph points into the applied recipe");
    Require(renderedTonePlan.needsPostApplyDisplayFit && !renderedTonePlan.usedRenderedDisplayFit,
        "Rendered Finish Tone with no rendered Display Candidate should keep the post-apply Display Fit queue");
    Require(StringListContainsFragment(renderedTonePlan.appliedSummaries, "Mild Finish Tone"),
        "Rendered Finish Tone plan should report Mild Finish Tone as an applied visible control");
    Require(!StringListContainsFragment(renderedTonePlan.withheldSummaries, "Finish Tone pending"),
        "Rendered Finish Tone plan should not call post-exposure tone evidence pending");

    RawAutoStartPoint::RawAutoStartPointDiagnostics renderedToneWithDisplayFitDiagnostics =
        renderedDisplayFitDiagnostics;
    renderedToneWithDisplayFitDiagnostics.candidates.push_back(renderedToneBaseCandidate);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan renderedToneWithDisplayFitPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            largeLiftAnalysis,
            largeLiftRecommendations,
            renderedToneWithDisplayFitDiagnostics);
    Require(!renderedToneWithDisplayFitPlan.visibleEdits.finishToneValid,
        "Rendered Finish Tone should be withheld when applying it would stale the rendered Display Fit recipe");
    Require(renderedToneWithDisplayFitPlan.usedRenderedDisplayFit &&
            !renderedToneWithDisplayFitPlan.needsPostApplyDisplayFit,
        "Withheld rendered Finish Tone should preserve the one-edit RAW Exposure plus rendered Display Fit path");
    Require(StringListContainsFragment(renderedToneWithDisplayFitPlan.withheldSummaries, "post-tone Display Candidate"),
        "Rendered Finish Tone withheld with rendered Display Fit should name the missing post-tone Display Candidate render");
    Require(StringListContainsFragment(renderedToneWithDisplayFitPlan.warnings, "stale the rendered Display Fit"),
        "Rendered Finish Tone withheld with rendered Display Fit should explain the stale-Display-Fit risk");

    RawRecipe::RawDevelopmentRecipe manualTone = recipe;
    manualTone.finishTone.layerJson = RawRecipe::DefaultFinishToneJson();
    manualTone.finishTone.layerJson["points"] = nlohmann::json::array({
        { { "x", 0.0f }, { "y", 0.0f }, { "shape", 1 } },
        { { "x", 0.5f }, { "y", 0.65f }, { "shape", 1 } },
        { { "x", 1.0f }, { "y", 1.0f }, { "shape", 1 } }
    });
    const RawAutoStartPoint::RawAutoStartPointConservativePlan manualTonePlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            manualTone,
            safeAnalysis,
            toneOnlyRecommendations,
            toneDiagnostics);
    Require(StringListContainsFragment(manualTonePlan.withheldSummaries, "already has non-neutral points"),
        "Starting Point planner should protect existing manual Finish Tone points");
    Require(manualTonePlan.summary.find("Build Starting Point plan:") != std::string::npos,
        "Starting Point plan summary should identify the applied/withheld control plan");
}

void TestRawWorkspaceStartingPointEditorStateModel() {
    namespace EditorTypes = Stack::EditorModuleTypes;
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAutoStartPoint = Stack::RawAutoStartPoint;
    namespace RawRecipe = Stack::RawRecipe;

    const std::string mixedControlStatus =
        EditorTypes::BuildRawStartingPointControlStatusSummary(
            "Build Starting Point plan: Display Fit pending: rendering once more before final fit.",
            "None",
            "RAW Exposure suggestion kept manual: confidence 42% below cautious gate, "
            "Local Range unchanged: no proposal, "
            "Finish Tone pending: render Finish Tone Candidate evidence, "
            "Display Fit pending: render Display Candidate evidence.",
            "None");
    Require(mixedControlStatus.find("RAW Exposure: blocked by policy") != std::string::npos,
        "Starting Point control status should distinguish blocked RAW Exposure from unchanged controls");
    Require(mixedControlStatus.find("Local Range: not proposed") != std::string::npos,
        "Starting Point control status should label no-proposal Local Range as not proposed");
    Require(mixedControlStatus.find("Finish Tone: pending") != std::string::npos,
        "Starting Point control status should label pending Finish Tone evidence");
    Require(mixedControlStatus.find("Display Fit / View Transform: pending") != std::string::npos,
        "Starting Point control status should label pending Display Fit evidence");

    EditorTypes::RawWorkspaceStartingPointPendingAction pending;
    EditorTypes::RawStartingPointContinuationContext context;
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(pending, context) ==
            EditorTypes::RawStartingPointContinuationDecision::Inactive,
        "Inactive Starting Point pending state should not continue or cancel");

    pending.active = true;
    pending.phase = EditorTypes::RawStartingPointPendingPhase::WaitingForInitialAnalysis;
    pending.sourceKey = "raw/source.dng";
    pending.sourceHash = 101;
    pending.recipeFingerprint = "recipe-a";

    context.sourceExists = true;
    context.activeSourceKey = pending.sourceKey;
    context.selectedSourceKey = pending.sourceKey;
    context.sourceHash = pending.sourceHash;
    context.analysisSourceKey = pending.sourceKey;
    context.analysisValid = false;
    context.recipeFingerprint = pending.recipeFingerprint;
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(pending, context) ==
            EditorTypes::RawStartingPointContinuationDecision::WaitForAnalysis,
        "Queued Starting Point should wait until analysis is valid for the pending source");

    context.analysisValid = true;
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(pending, context) ==
            EditorTypes::RawStartingPointContinuationDecision::ContinueInitialAnalysis,
        "Queued initial Starting Point should continue once source, recipe, and analysis match");

    pending.phase = EditorTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(pending, context) ==
            EditorTypes::RawStartingPointContinuationDecision::ContinuePostApplyAnalysis,
        "Queued post-apply Starting Point should continue to Display Fit once analysis matches");

    EditorTypes::RawStartingPointContinuationContext sourceMissing = context;
    sourceMissing.sourceExists = false;
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(pending, sourceMissing) ==
            EditorTypes::RawStartingPointContinuationDecision::CancelSourceMissing,
        "Queued Starting Point should cancel if its source disappears");
    Require(
        std::string(EditorTypes::RawStartingPointContinuationCancellationReason(
            EditorTypes::RawStartingPointContinuationDecision::CancelSourceMissing))
                .find("source disappeared") != std::string::npos,
        "Source-missing cancellation should have a user-readable reason");

    EditorTypes::RawStartingPointContinuationContext sourceChanged = context;
    sourceChanged.selectedSourceKey = "raw/other.dng";
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(pending, sourceChanged) ==
            EditorTypes::RawStartingPointContinuationDecision::CancelSourceChanged,
        "Queued Starting Point should cancel when the selected RAW changes");

    EditorTypes::RawStartingPointContinuationContext recipeChanged = context;
    recipeChanged.recipeFingerprint = "recipe-b";
    const EditorTypes::RawStartingPointContinuationDecision recipeDecision =
        EditorTypes::EvaluateRawStartingPointContinuation(pending, recipeChanged);
    Require(
        recipeDecision == EditorTypes::RawStartingPointContinuationDecision::CancelRecipeChanged,
        "Queued Starting Point should cancel when the recipe changes before continuation");
    Require(
        EditorTypes::RawStartingPointContinuationCancels(recipeDecision),
        "Recipe-changed continuation decision should be classified as a cancellation");

    pending.phase = EditorTypes::RawStartingPointPendingPhase::None;
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(pending, context) ==
            EditorTypes::RawStartingPointContinuationDecision::CancelUnknownPhase,
        "Active pending Starting Point with no phase should cancel instead of continuing");

    EditorTypes::RawWorkspaceAutoBaseUiState cancelUi;
    cancelUi.hasRevertSnapshot = true;
    cancelUi.startingPointDisplayFitPending = true;
    cancelUi.pendingStartingPoint = pending;
    const bool canceled =
        EditorTypes::CancelRawStartingPointPendingAction(
            cancelUi,
            "source changed before the queued Starting Point action completed.");
    Require(canceled, "Pending Starting Point cancellation helper should report cancellation");
    Require(!cancelUi.pendingStartingPoint.active,
        "Pending Starting Point cancellation should clear the pending action");
    Require(!cancelUi.startingPointDisplayFitPending,
        "Pending Starting Point cancellation should clear Display Fit pending state");
    Require(cancelUi.hasRevertSnapshot,
        "Pending Starting Point cancellation should not erase an existing undo snapshot");
    Require(cancelUi.summary.find("Build Starting Point canceled") != std::string::npos,
        "Pending Starting Point cancellation should update the visible action summary");
    Require(cancelUi.startingPointAppliedControlsSummary == "None",
        "Pending Starting Point cancellation should not claim applied controls");
    Require(
        cancelUi.startingPointWithheldControlsSummary.find("source changed") != std::string::npos,
        "Pending Starting Point cancellation should surface the cancellation reason");
    Require(!EditorTypes::CancelRawStartingPointPendingAction(cancelUi, ""),
        "Canceling with no active pending Starting Point should be a no-op");

    RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest queueRequest;
    queueRequest.valid = true;
    queueRequest.id = "graph-state-source-hash";
    queueRequest.stage = RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate;
    EditorTypes::RawWorkspaceStartingPointCandidateRenderQueueState sourceHashQueue;
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        sourceHashQueue,
        "raw/source.dng",
        { queueRequest },
        77,
        501);
    Require(sourceHashQueue.sourceHash == 501 &&
            EditorTypes::RawStartingPointCandidateRenderQueueMatchesSource(
                sourceHashQueue,
                "raw/source.dng",
                501),
        "Candidate render queue should match the active source hash");
    RawAutoStartPoint::RawAutoStartPointCandidateRenderResult queueResult;
    queueResult.request = queueRequest;
    queueResult.attempted = true;
    queueResult.success = true;
    const std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> queueResults = {
        queueResult
    };
    Require(EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            sourceHashQueue,
            queueResults,
            "raw/source.dng",
            501),
        "Candidate render results should be mergeable for the matching active source hash");
    Require(!EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            sourceHashQueue,
            queueResults,
            "raw/source.dng",
            502),
        "Candidate render results should not be mergeable after a same-key source hash change");
    Require(!EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            sourceHashQueue,
            {},
            "raw/source.dng",
            501),
        "Empty candidate render results should not be mergeable");
    Require(!EditorTypes::RawStartingPointCandidateRenderQueueMatchesSource(
            sourceHashQueue,
            "raw/source.dng",
            502),
        "Candidate render queue should reject the same source key with a different hash");
    Require(!EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
            sourceHashQueue,
            "raw/source.dng",
            "raw/source.dng",
            502,
            501),
        "Stale same-key rejected results should not clear the current matching-hash candidate queue");
    Require(EditorTypes::ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
            sourceHashQueue,
            "raw/source.dng",
            502),
        "Candidate render queue should clear when the active source hash changes");
    Require(sourceHashQueue.sourceKey.empty() &&
            sourceHashQueue.sourceHash == 0 &&
            sourceHashQueue.requests.empty(),
        "Candidate render queue hash clear should reset source identity and requests");
    Require(!EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            sourceHashQueue,
            queueResults,
            "raw/source.dng",
            502),
        "Candidate render results should not be mergeable after the source queue is cleared");
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        sourceHashQueue,
        "raw/source.dng",
        {},
        78,
        501);
    Require(sourceHashQueue.sourceKey.empty() &&
            sourceHashQueue.sourceHash == 0 &&
            sourceHashQueue.generation == 0 &&
            sourceHashQueue.requests.empty(),
        "Candidate render queue should clear instead of storing empty request sets");

    RawAutoStartPoint::RawAutoStartPointDiagnostics resetDiagnostics;
    resetDiagnostics.valid = true;
    resetDiagnostics.sourceKey = "raw/source.dng";
    resetDiagnostics.candidates.push_back(RawAutoStartPoint::RawAutoStartPointCandidate());
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        sourceHashQueue,
        "raw/source.dng",
        { queueRequest },
        79,
        501);
    std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> resetResults =
        queueResults;
    EditorTypes::ClearRawStartingPointCandidateEvidenceCache(
        resetDiagnostics,
        sourceHashQueue,
        resetResults);
    Require(!resetDiagnostics.valid &&
            resetDiagnostics.sourceKey.empty() &&
            resetDiagnostics.candidates.empty(),
        "Candidate evidence cache clear should reset diagnostics to the default empty state");
    Require(sourceHashQueue.sourceKey.empty() &&
            sourceHashQueue.requests.empty() &&
            resetResults.empty(),
        "Candidate evidence cache clear should reset queue identity and cached render results");

    RawRecipe::RawDevelopmentRecipe stagedRecipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0200.dng", "IMG_0200.dng");
    const Stack::RawAnalysis::RawImageAnalysis stagedAnalysis =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.2f, 0.0f, 5.0f);
    const RawAutoBase::AutoBaseRecommendations stagedRecommendations =
        RawAutoBase::BuildAutoBaseRecommendations(stagedAnalysis, stagedRecipe);
    const RawAutoStartPoint::RawAutoStartPointDiagnostics stagedDiagnostics =
        BuildPlannerExposureDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan stagedPlan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            stagedRecipe,
            stagedAnalysis,
            stagedRecommendations,
            stagedDiagnostics);
    Require(stagedPlan.hasUpstreamRecipeChanges,
        "Staged capped RAW Exposure plan should enter the editor post-fit handoff path");

    EditorTypes::RawWorkspaceStartingPointPendingAction stagedPending;
    stagedPending.active = true;
    stagedPending.phase = EditorTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
    stagedPending.upstreamApplyPassCount = 1;
    stagedPending.sourceKey = "raw/staged-cap.dng";
    stagedPending.sourceHash = 202;
    stagedPending.recipeFingerprint = "staged-cap-recipe";
    stagedPending.originalRecipe = stagedRecipe;
    stagedPending.plannedUpstreamRecipe = stagedPlan.upstreamRecipe;
    stagedPending.appliedControls =
        EditorTypes::RawStartingPointControlsWithoutDisplayFit(
            stagedPlan.visibleEdits.touchedControls);
    stagedPending.planSummary = stagedPlan.summary;
    stagedPending.appliedControlsSummary = "RAW Exposure";
    stagedPending.appliedValuesSummary = "RAW Exposure +0.50 EV";
    stagedPending.withheldControlsSummary = "Additional RAW Exposure lift withheld: one-click cap";
    stagedPending.evidenceSummary =
        EditorTypes::JoinRawStartingPointSummaryParts(stagedPlan.evidenceSummaries);
    stagedPending.warningSummary =
        "Build Starting Point carried partial Base candidate evidence.";
    Require(
        EditorTypes::RawStartingPointShouldContinueUpstreamApply(stagedPending, true),
        "A settled one-click action should continue when another safe upstream plan is available");
    Require(
        !EditorTypes::RawStartingPointShouldContinueUpstreamApply(stagedPending, false),
        "A settled one-click action should proceed to Display Fit when no upstream plan remains");
    EditorTypes::RawWorkspaceStartingPointPendingAction boundedPending = stagedPending;
    boundedPending.upstreamApplyPassCount =
        EditorTypes::kRawStartingPointMaxUpstreamApplyPasses;
    Require(
        !EditorTypes::RawStartingPointShouldContinueUpstreamApply(boundedPending, true) &&
            EditorTypes::RawStartingPointReachedUpstreamApplyLimit(boundedPending, true),
        "One-click upstream continuation should stop at the bounded apply-pass limit");

    EditorTypes::RawWorkspaceAutoBaseUiState stagedUi;
    stagedUi.hasAppliedViewFit = true;
    stagedUi.hasRevertSnapshot = true;
    EditorTypes::MarkRawStartingPointUpstreamApplied(
        stagedUi,
        std::move(stagedPending));
    Require(stagedUi.pendingStartingPoint.active,
        "Staged capped handoff should keep the post-fit pending action active");
    Require(!stagedUi.hasAppliedViewFit && stagedUi.startingPointDisplayFitPending,
        "Staged capped handoff should mark Display Fit pending after upstream visible controls apply");
    Require(std::abs(
            stagedUi.pendingStartingPoint.plannedUpstreamRecipe.preToneExposureEv -
            (stagedRecipe.preToneExposureEv + 0.50f)) < 0.001f,
        "Staged capped handoff should preserve the capped visible RAW Exposure value");
    Require(!StartPointControlListContains(
            stagedUi.pendingStartingPoint.appliedControls,
            RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
        "Staged capped handoff should not claim Display Fit as already applied");
    Require(StartPointControlListContains(
            stagedUi.pendingStartingPoint.appliedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Staged capped handoff should keep RAW Exposure in the applied upstream controls");
    Require(stagedUi.startingPointAppliedControlsSummary == "RAW Exposure",
        "Staged capped handoff should show the user the visible upstream control that changed");
    Require(stagedUi.startingPointAppliedValuesSummary.find("+0.50 EV") != std::string::npos,
        "Staged capped handoff should show the user the visible upstream value that changed");
    Require(stagedUi.startingPointWithheldControlsSummary.find("one-click cap") != std::string::npos,
        "Staged capped handoff should keep the additional lift visible as withheld");
    Require(stagedUi.startingPointControlStatusSummary.find("RAW Exposure: changed") !=
            std::string::npos &&
            stagedUi.startingPointControlStatusSummary.find("Display Fit / View Transform: pending") !=
                std::string::npos &&
            stagedUi.startingPointControlStatusSummary.find("Local Range: not proposed") !=
                std::string::npos,
        "Staged capped handoff should show a per-control changed/pending/not-proposed status");
    Require(stagedUi.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos &&
            stagedUi.startingPointEvidenceSummary.find("Display Fit awaiting matching rendered Display Candidate evidence") !=
                std::string::npos,
        "Staged capped handoff should keep the evidence summary visible while Display Fit is pending");
    Require(stagedUi.startingPointWarningSummary.find("partial Base candidate evidence") !=
            std::string::npos,
        "Staged capped handoff should keep plan warnings visible while Display Fit is pending");
    Require(stagedUi.summary.find("Display Fit pending") != std::string::npos,
        "Staged capped handoff should explain the remaining Display Fit phase");

    EditorTypes::RawWorkspaceAutoBaseUiState canceledPostApplyUi = stagedUi;
    const bool canceledPostApply =
        EditorTypes::CancelRawStartingPointPendingAction(
            canceledPostApplyUi,
            "recipe changed before the queued Starting Point action completed.");
    Require(canceledPostApply,
        "Post-apply Starting Point cancellation helper should report cancellation");
    Require(!canceledPostApplyUi.pendingStartingPoint.active,
        "Post-apply Starting Point cancellation should clear the pending action");
    Require(canceledPostApplyUi.startingPointDisplayFitPending,
        "Post-apply Starting Point cancellation should keep Refit Display visible as pending");
    Require(canceledPostApplyUi.hasRevertSnapshot,
        "Post-apply Starting Point cancellation should preserve undo availability");
    Require(
        canceledPostApplyUi.summary.find("canceled after applying upstream controls") !=
            std::string::npos,
        "Post-apply Starting Point cancellation should explain that upstream controls already changed");
    Require(canceledPostApplyUi.startingPointAppliedControlsSummary == "RAW Exposure",
        "Post-apply Starting Point cancellation should keep applied upstream controls visible");
    Require(canceledPostApplyUi.startingPointAppliedValuesSummary == "RAW Exposure +0.50 EV",
        "Post-apply Starting Point cancellation should keep applied upstream values visible");
    Require(
        canceledPostApplyUi.startingPointWithheldControlsSummary.find("one-click cap") !=
            std::string::npos &&
        canceledPostApplyUi.startingPointWithheldControlsSummary.find("Display Fit canceled") !=
            std::string::npos,
        "Post-apply Starting Point cancellation should keep prior withheld controls and Display Fit cancellation visible");
    Require(
        canceledPostApplyUi.startingPointControlStatusSummary.find("RAW Exposure: changed") !=
            std::string::npos &&
        canceledPostApplyUi.startingPointControlStatusSummary.find("Display Fit / View Transform: failed") !=
            std::string::npos,
        "Post-apply Starting Point cancellation should keep changed upstream controls and failed Display Fit distinct");
    Require(canceledPostApplyUi.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Post-apply Starting Point cancellation should preserve the evidence summary");
    Require(canceledPostApplyUi.startingPointWarningSummary.find("partial Base candidate evidence") !=
            std::string::npos,
        "Post-apply Starting Point cancellation should preserve the warning summary");

    EditorTypes::RawStartingPointContinuationContext stagedContext;
    stagedContext.sourceExists = true;
    stagedContext.activeSourceKey = stagedUi.pendingStartingPoint.sourceKey;
    stagedContext.selectedSourceKey = stagedUi.pendingStartingPoint.sourceKey;
    stagedContext.sourceHash = stagedUi.pendingStartingPoint.sourceHash;
    stagedContext.analysisSourceKey = stagedUi.pendingStartingPoint.sourceKey;
    stagedContext.analysisValid = true;
    stagedContext.recipeFingerprint = stagedUi.pendingStartingPoint.recipeFingerprint;
    Require(
        EditorTypes::EvaluateRawStartingPointContinuation(
            stagedUi.pendingStartingPoint,
            stagedContext) ==
            EditorTypes::RawStartingPointContinuationDecision::ContinuePostApplyAnalysis,
        "Staged capped handoff should continue to Display Fit once matching post-edit analysis arrives");

    const std::vector<RawAutoStartPoint::RawAutoStartPointControl> stagedCompletedControls =
        EditorTypes::MarkRawStartingPointPostFitApplied(
            stagedUi,
            stagedUi.pendingStartingPoint,
            303,
            "Display Fit refreshed: middle grey 0.250, black -6.00 EV, white +5.00 EV, shoulder 0.50, toe 0.20");
    Require(!stagedUi.pendingStartingPoint.active,
        "Completed staged capped handoff should clear the pending action");
    Require(stagedUi.hasAppliedViewFit && !stagedUi.startingPointDisplayFitPending,
        "Completed staged capped handoff should mark Display Fit applied and no longer pending");
    Require(stagedUi.viewTransformOwner == EditorTypes::RawAutoValueOwner::AutoBase,
        "Completed staged capped handoff should keep Display Fit auto-owned");
    Require(stagedUi.appliedAnalysisHash == 303,
        "Completed staged capped handoff should store the post-edit analysis hash");
    Require(stagedUi.hasRevertSnapshot,
        "Completed staged capped handoff should preserve undo availability");
    Require(StartPointControlListContains(
            stagedCompletedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Completed staged capped handoff should still report RAW Exposure as applied");
    Require(StartPointControlListContains(
            stagedCompletedControls,
            RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
        "Completed staged capped handoff should report Display Fit as applied");
    Require(stagedUi.startingPointAppliedControlsSummary.find("RAW Exposure") != std::string::npos &&
            stagedUi.startingPointAppliedControlsSummary.find("Display Fit") != std::string::npos,
        "Completed staged capped handoff should show RAW Exposure and Display Fit as changed controls");
    Require(stagedUi.startingPointAppliedValuesSummary.find("+0.50 EV") != std::string::npos &&
            stagedUi.startingPointAppliedValuesSummary.find("middle grey 0.250") != std::string::npos &&
            stagedUi.startingPointAppliedValuesSummary.find("black -6.00 EV") != std::string::npos &&
            stagedUi.startingPointAppliedValuesSummary.find("white +5.00 EV") != std::string::npos,
        "Completed staged capped handoff should preserve upstream values and add the final Display Fit value summary");
    const std::string displayFitControlValueText =
        EditorTypes::RawStartingPointRelevantControlValueText(
            stagedUi.startingPointAppliedValuesSummary,
            "Display Fit",
            "View Transform");
    Require(displayFitControlValueText.find("Display Fit refreshed") != std::string::npos &&
            displayFitControlValueText.find("middle grey 0.250") != std::string::npos &&
            displayFitControlValueText.find("black -6.00 EV") != std::string::npos &&
            displayFitControlValueText.find("white +5.00 EV") != std::string::npos &&
            displayFitControlValueText.find("shoulder 0.50") != std::string::npos &&
            displayFitControlValueText.find("toe 0.20") != std::string::npos,
        "Display Fit manual-control readout should preserve the full comma-separated View Transform value block");
    Require(displayFitControlValueText.find("RAW Exposure +0.50 EV") == std::string::npos,
        "Display Fit manual-control readout should not absorb the prior RAW Exposure value");
    Require(stagedUi.startingPointWithheldControlsSummary.find("one-click cap") != std::string::npos,
        "Completed staged capped handoff should keep the additional lift visible as withheld");
    Require(stagedUi.startingPointControlStatusSummary.find("RAW Exposure: changed") !=
            std::string::npos &&
            stagedUi.startingPointControlStatusSummary.find("Display Fit / View Transform: changed") !=
                std::string::npos,
        "Completed staged capped handoff should show RAW Exposure and Display Fit as changed per-control statuses");
    Require(stagedUi.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Completed staged capped handoff should preserve the evidence summary");
    Require(stagedUi.startingPointWarningSummary.find("partial Base candidate evidence") !=
            std::string::npos,
        "Completed staged capped handoff should preserve the warning summary");
    Require(stagedUi.summary.find("Display Fit refreshed") != std::string::npos,
        "Completed staged capped handoff should explain that post-edit Display Fit finished");

    EditorTypes::RawWorkspaceAutoBaseUiState postFitUi;
    postFitUi.hasRevertSnapshot = true;
    postFitUi.pendingStartingPoint.active = true;
    postFitUi.pendingStartingPoint.phase =
        EditorTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
    postFitUi.pendingStartingPoint.appliedControls = {
        RawAutoStartPoint::RawAutoStartPointControl::RawExposure
    };
    postFitUi.pendingStartingPoint.appliedControlsSummary = "RAW Exposure";
    postFitUi.pendingStartingPoint.appliedValuesSummary = "RAW Exposure +0.50 EV";
    postFitUi.pendingStartingPoint.withheldControlsSummary = "Local Range unchanged";
    postFitUi.pendingStartingPoint.evidenceSummary =
        "RAW Exposure from conservative exposure evidence";
    postFitUi.pendingStartingPoint.warningSummary =
        "Build Starting Point carried partial Base candidate evidence.";
    const std::string withheldSummary =
        EditorTypes::MarkRawStartingPointPostFitFailure(
            postFitUi,
            "display stats unavailable.");
    Require(!postFitUi.pendingStartingPoint.active,
        "Failed post-fit Starting Point should clear the pending action");
    Require(postFitUi.startingPointDisplayFitPending,
        "Failed post-fit Starting Point should keep Display Fit pending");
    Require(postFitUi.hasRevertSnapshot,
        "Failed post-fit Starting Point should preserve undo availability for upstream edits");
    Require(postFitUi.summary.find("Display Fit is still pending") != std::string::npos,
        "Failed post-fit Starting Point should explain the remaining Display Fit work");
    Require(postFitUi.startingPointResultSummary == postFitUi.summary,
        "Failed post-fit Starting Point should persist the visible Build result summary");
    Require(postFitUi.startingPointAppliedControlsSummary == "RAW Exposure",
        "Failed post-fit Starting Point should keep upstream applied controls visible");
    Require(postFitUi.startingPointAppliedValuesSummary == "RAW Exposure +0.50 EV",
        "Failed post-fit Starting Point should keep upstream applied values visible");
    Require(withheldSummary.find("Local Range unchanged") != std::string::npos &&
            withheldSummary.find("Display Fit pending") != std::string::npos,
        "Failed post-fit Starting Point should append the Display Fit pending reason");
    Require(postFitUi.startingPointWithheldControlsSummary == withheldSummary,
        "Failed post-fit Starting Point should persist the full withheld summary");
    Require(postFitUi.startingPointControlStatusSummary.find("RAW Exposure: changed") !=
            std::string::npos &&
            postFitUi.startingPointControlStatusSummary.find("Local Range: not proposed") !=
                std::string::npos &&
            postFitUi.startingPointControlStatusSummary.find("Display Fit / View Transform: pending") !=
                std::string::npos,
        "Failed post-fit Starting Point should keep changed, not-proposed, and pending controls distinct");
    Require(postFitUi.startingPointEvidenceSummary == "RAW Exposure from conservative exposure evidence",
        "Failed post-fit Starting Point should persist the evidence summary");
    Require(postFitUi.startingPointWarningSummary.find("partial Base candidate evidence") !=
            std::string::npos,
        "Failed post-fit Starting Point should persist the warning summary");

    EditorTypes::RawWorkspaceAutoBaseUiState postFitNoWithheldUi;
    postFitNoWithheldUi.pendingStartingPoint.active = true;
    postFitNoWithheldUi.pendingStartingPoint.phase =
        EditorTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
    postFitNoWithheldUi.pendingStartingPoint.withheldControlsSummary = "None";
    const std::string defaultReason =
        EditorTypes::MarkRawStartingPointPostFitFailure(postFitNoWithheldUi, "");
    Require(postFitNoWithheldUi.startingPointAppliedControlsSummary == "None",
        "Post-fit failure with no upstream applied summary should normalize applied controls to None");
    Require(defaultReason.find("post-edit analysis could not produce a Display Fit") !=
            std::string::npos,
        "Post-fit failure with no reason should use the default Display Fit pending reason");

    EditorTypes::RawWorkspaceAutoBaseUiState initialRenderFailureUi;
    initialRenderFailureUi.pendingStartingPoint.active = true;
    initialRenderFailureUi.pendingStartingPoint.phase =
        EditorTypes::RawStartingPointPendingPhase::WaitingForInitialAnalysis;
    Require(
        EditorTypes::MarkRawStartingPointRenderFailure(
            initialRenderFailureUi,
            "worker produced no RAW output."),
        "Initial-analysis render failure should update the pending Starting Point state");
    Require(!initialRenderFailureUi.pendingStartingPoint.active,
        "Initial-analysis render failure should clear the pending action");
    Require(!initialRenderFailureUi.startingPointDisplayFitPending,
        "Initial-analysis render failure should not leave Display Fit pending");
    Require(initialRenderFailureUi.startingPointAppliedControlsSummary == "None",
        "Initial-analysis render failure should not claim visible controls were applied");
    Require(initialRenderFailureUi.startingPointWithheldControlsSummary.find("worker produced no RAW output") !=
            std::string::npos,
        "Initial-analysis render failure should surface the render failure reason");

    EditorTypes::RawWorkspaceAutoBaseUiState postApplyRenderFailureUi;
    postApplyRenderFailureUi.hasRevertSnapshot = true;
    postApplyRenderFailureUi.pendingStartingPoint.active = true;
    postApplyRenderFailureUi.pendingStartingPoint.phase =
        EditorTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
    postApplyRenderFailureUi.pendingStartingPoint.appliedControls = {
        RawAutoStartPoint::RawAutoStartPointControl::RawExposure
    };
    postApplyRenderFailureUi.pendingStartingPoint.appliedControlsSummary = "RAW Exposure";
    postApplyRenderFailureUi.pendingStartingPoint.appliedValuesSummary = "RAW Exposure +0.50 EV";
    postApplyRenderFailureUi.pendingStartingPoint.withheldControlsSummary =
        "Additional RAW Exposure lift withheld: one-click cap";
    postApplyRenderFailureUi.pendingStartingPoint.evidenceSummary =
        "RAW Exposure from conservative exposure evidence";
    Require(
        EditorTypes::MarkRawStartingPointRenderFailure(
            postApplyRenderFailureUi,
            "worker produced no post-edit output."),
        "Post-apply render failure should update the pending Starting Point state");
    Require(!postApplyRenderFailureUi.pendingStartingPoint.active,
        "Post-apply render failure should clear the pending action");
    Require(postApplyRenderFailureUi.startingPointDisplayFitPending,
        "Post-apply render failure should keep Display Fit pending");
    Require(postApplyRenderFailureUi.hasRevertSnapshot,
        "Post-apply render failure should preserve undo availability");
    Require(postApplyRenderFailureUi.startingPointAppliedControlsSummary == "RAW Exposure",
        "Post-apply render failure should keep applied upstream controls visible");
    Require(postApplyRenderFailureUi.startingPointAppliedValuesSummary == "RAW Exposure +0.50 EV",
        "Post-apply render failure should keep applied upstream values visible");
    Require(postApplyRenderFailureUi.startingPointWithheldControlsSummary.find("one-click cap") !=
            std::string::npos &&
            postApplyRenderFailureUi.startingPointWithheldControlsSummary.find("worker produced no post-edit output") !=
            std::string::npos,
        "Post-apply render failure should preserve prior withheld controls and append the render failure reason");
    Require(postApplyRenderFailureUi.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Post-apply render failure should preserve the evidence summary");
}

void TestRawAutoBaseNoiseDetailRecommendations() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAnalysis = Stack::RawAnalysis;
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0006.dng", "IMG_0006.dng");
    RawAnalysis::RawImageAnalysis baseAnalysis =
        BuildAutoBaseTestAnalysis(-7.0f, -5.0f, -2.5f, 1.0f, 2.0f, 8.0f);

    RawAnalysis::RawImageAnalysis lowIsoAnalysis = baseAnalysis;
    lowIsoAnalysis.metadata.iso = 100.0f;
    const RawAutoBase::NoiseDetailRecommendation lowIso =
        RawAutoBase::BuildNoiseDetailRecommendation(lowIsoAnalysis, recipe);
    Require(lowIso.valid && !lowIso.suggestChromaDenoise && !lowIso.suggestLumaDenoise,
        "Low ISO RAW noise/detail recommendations should not suggest denoise");
    Require(std::abs(lowIso.sharpeningScale - 1.0f) < 0.001f,
        "Low ISO RAW noise/detail recommendations should preserve sharpening scale");

    RawAnalysis::RawImageAnalysis mediumIsoAnalysis = baseAnalysis;
    mediumIsoAnalysis.metadata.iso = 1600.0f;
    const RawAutoBase::NoiseDetailRecommendation mediumIso =
        RawAutoBase::BuildNoiseDetailRecommendation(mediumIsoAnalysis, recipe);
    Require(mediumIso.effectiveNoiseScore > lowIso.effectiveNoiseScore,
        "Effective noise score should increase with ISO");
    Require(mediumIso.suggestChromaDenoise && !mediumIso.suggestLumaDenoise,
        "Moderate ISO RAW noise/detail recommendations should suggest mild chroma denoise only");

    RawRecipe::RawDevelopmentRecipe shadowLiftRecipe = recipe;
    shadowLiftRecipe.localRange.enabled = true;
    shadowLiftRecipe.localRange.strength = 1.0f;
    shadowLiftRecipe.localRange.points = {
        { -8.0f, 0.0f },
        { -4.0f, 1.0f },
        { 0.0f, 0.0f },
        { 6.0f, 0.0f }
    };
    const RawAutoBase::NoiseDetailRecommendation mediumIsoLifted =
        RawAutoBase::BuildNoiseDetailRecommendation(mediumIsoAnalysis, shadowLiftRecipe);
    Require(mediumIsoLifted.shadowLiftEv >= 0.9f &&
            mediumIsoLifted.effectiveNoiseScore > mediumIso.effectiveNoiseScore,
        "Effective noise score should increase when shadow lift is applied");
    Require(mediumIsoLifted.sharpeningScale < mediumIso.sharpeningScale,
        "High enough ISO plus shadow lift should reduce sharpening scale before increasing denoise");

    RawAnalysis::RawImageAnalysis highIsoAnalysis = baseAnalysis;
    highIsoAnalysis.metadata.iso = 6400.0f;
    const RawAutoBase::NoiseDetailRecommendation highIso =
        RawAutoBase::BuildNoiseDetailRecommendation(highIsoAnalysis, recipe);
    Require(highIso.suggestChromaDenoise && highIso.suggestLumaDenoise,
        "High ISO RAW noise/detail recommendations should suggest chroma and light luma denoise");
    Require(highIso.suggestReduceSharpening && highIso.sharpeningScale <= 0.75f,
        "High ISO RAW noise/detail recommendations should reduce sharpening scale");
    Require(!highIso.autoApplyMinimalChromaDenoise,
        "Noise/detail recommendations should not auto-apply when RAW workspace controls are not visible");

    const RawAutoBase::NoiseDetailRecommendation highIsoControlsVisible =
        RawAutoBase::BuildNoiseDetailRecommendation(highIsoAnalysis, recipe, nullptr, true);
    Require(highIsoControlsVisible.autoApplyMinimalChromaDenoise,
        "Minimal chroma denoise may only be marked auto-apply safe when visible controls exist");

    RawAnalysis::RawImageAnalysis missingIsoAnalysis = baseAnalysis;
    missingIsoAnalysis.metadata.iso = 0.0f;
    const RawAutoBase::NoiseDetailRecommendation missingIso =
        RawAutoBase::BuildNoiseDetailRecommendation(missingIsoAnalysis, recipe);
    Require(!missingIso.valid && missingIso.confidence < 0.25f,
        "Missing ISO should lower confidence instead of inventing a noise/detail recommendation");
    Require(!missingIso.suggestChromaDenoise && !missingIso.suggestLumaDenoise &&
            !missingIso.autoApplyMinimalChromaDenoise,
        "Missing ISO should avoid denoise suggestions and auto-apply");

    RawAutoBase::SuggestedLocalAdjustment shadowSuggestion;
    shadowSuggestion.valid = true;
    shadowSuggestion.kind = RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows;
    shadowSuggestion.deltaEv = 1.25f;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> suggestions = { shadowSuggestion };
    Require(RawAutoBase::EstimateShadowLiftEvForNoiseDetail(recipe, &suggestions) >= 1.20f,
        "Noise/detail shadow lift estimation should include suggested Local Range shadow lifts");
}

Stack::RawAutoBase::LocalSuggestionAnalysisImage MakeLocalSuggestionImage(
    int width,
    int height,
    float r,
    float g,
    float b) {
    Stack::RawAutoBase::LocalSuggestionAnalysisImage image;
    image.valid = true;
    image.sceneLinearBeforeLocalRange = true;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (Stack::RawAutoBase::LocalSuggestionPixel& pixel : image.pixels) {
        pixel.valid = true;
        pixel.r = r;
        pixel.g = g;
        pixel.b = b;
    }
    return image;
}

void FillLocalSuggestionRect(
    Stack::RawAutoBase::LocalSuggestionAnalysisImage& image,
    int x0,
    int y0,
    int x1,
    int y1,
    float r,
    float g,
    float b,
    bool textured = false) {
    x0 = std::clamp(x0, 0, image.width);
    y0 = std::clamp(y0, 0, image.height);
    x1 = std::clamp(x1, 0, image.width);
    y1 = std::clamp(y1, 0, image.height);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float texture = textured
                ? (((x + y) & 1) == 0 ? 0.68f : 1.32f)
                : 1.0f;
            Stack::RawAutoBase::LocalSuggestionPixel& pixel =
                image.pixels[static_cast<std::size_t>(y * image.width + x)];
            pixel.valid = true;
            pixel.r = r * texture;
            pixel.g = g * texture;
            pixel.b = b * texture;
        }
    }
}

bool HasLocalSuggestionKind(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions,
    Stack::RawAutoBase::SuggestedLocalAdjustmentKind kind) {
    return std::any_of(
        suggestions.begin(),
        suggestions.end(),
        [kind](const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
            return suggestion.kind == kind;
        });
}

const Stack::RawAutoBase::SuggestedLocalAdjustment* FindLocalSuggestionKind(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions,
    Stack::RawAutoBase::SuggestedLocalAdjustmentKind kind) {
    const auto it = std::find_if(
        suggestions.begin(),
        suggestions.end(),
        [kind](const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
            return suggestion.kind == kind;
        });
    return it == suggestions.end() ? nullptr : &*it;
}

void TestRawAutoBaseLocalRangeSuggestions() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawRecipe = Stack::RawRecipe;

    const Stack::RawAnalysis::RawImageAnalysis baseAnalysis =
        BuildAutoBaseTestAnalysis(-7.0f, -5.0f, -2.5f, 1.0f, 2.5f, 9.0f);

    RawAutoBase::LocalSuggestionAnalysisImage skyImage =
        MakeLocalSuggestionImage(80, 60, 0.06f, 0.06f, 0.06f);
    FillLocalSuggestionRect(skyImage, 0, 0, 80, 22, 0.22f, 0.55f, 1.15f);
    RawAutoBase::LocalSuggestionComponentReport skyReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> skySuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, skyImage, &skyReport);
    Require(skyReport.valid && skyReport.skyAreaPercent > 8.0f,
        "Local suggestions should classify a connected bright blue top region as sky");
    Require(HasLocalSuggestionKind(skySuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::ProtectSky),
        "Local suggestions should include Protect sky for bright sky over dark foreground");

    RawAutoBase::LocalSuggestionAnalysisImage lowerBlueImage =
        MakeLocalSuggestionImage(80, 60, 0.18f, 0.18f, 0.18f);
    FillLocalSuggestionRect(lowerBlueImage, 8, 36, 72, 56, 0.12f, 0.35f, 1.10f);
    RawAutoBase::LocalSuggestionComponentReport lowerBlueReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> lowerBlueSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, lowerBlueImage, &lowerBlueReport);
    Require(lowerBlueReport.valid && lowerBlueReport.skyAreaPercent < 3.0f,
        "Local suggestions should not classify a lower-frame blue object as sky");
    Require(!HasLocalSuggestionKind(lowerBlueSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::ProtectSky),
        "Local suggestions should not offer Protect sky for a lower-frame blue object");

    RawAutoBase::LocalSuggestionAnalysisImage foliageImage =
        MakeLocalSuggestionImage(80, 60, 0.18f, 0.18f, 0.18f);
    FillLocalSuggestionRect(foliageImage, 0, 24, 80, 60, 0.10f, 0.42f, 0.08f, true);
    RawAutoBase::LocalSuggestionComponentReport foliageReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> foliageSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, foliageImage, &foliageReport);
    const RawAutoBase::SuggestedLocalAdjustment* foliageSuggestion =
        FindLocalSuggestionKind(foliageSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::BrightenFoliage);
    Require(foliageReport.valid && foliageReport.foliageAreaPercent > 3.0f,
        "Local suggestions should classify textured green/yellow-green regions as foliage");
    Require(foliageSuggestion != nullptr && foliageSuggestion->colorQualifierEnabled,
        "Brighten foliage must use a color qualifier");

    RawAutoBase::LocalSuggestionAnalysisImage flatGreenImage =
        MakeLocalSuggestionImage(80, 60, 0.10f, 0.42f, 0.08f);
    RawAutoBase::LocalSuggestionComponentReport flatGreenReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> flatGreenSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, flatGreenImage, &flatGreenReport);
    Require(flatGreenReport.valid && flatGreenReport.foliageAreaPercent < 3.0f,
        "Local suggestions should reject flat green walls as foliage");
    Require(!HasLocalSuggestionKind(flatGreenSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::BrightenFoliage),
        "Local suggestions should not offer Brighten foliage for flat green walls");

    RawAutoBase::LocalSuggestionAnalysisImage backlitImage =
        MakeLocalSuggestionImage(80, 60, 0.08f, 0.08f, 0.08f);
    FillLocalSuggestionRect(backlitImage, 0, 0, 80, 26, 0.25f, 0.62f, 1.25f);
    FillLocalSuggestionRect(backlitImage, 26, 28, 54, 54, 0.035f, 0.035f, 0.035f);
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> backlitSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, backlitImage, nullptr);
    Require(HasLocalSuggestionKind(backlitSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::OpenBacklitSubject),
        "Local suggestions should include Open backlit subject for dark center under a bright sky");

    RawAutoBase::LocalSuggestionAnalysisImage invertedBacklitImage =
        MakeLocalSuggestionImage(80, 60, 0.08f, 0.08f, 0.08f);
    FillLocalSuggestionRect(invertedBacklitImage, 0, 42, 80, 60, 0.30f, 0.75f, 1.50f);
    FillLocalSuggestionRect(invertedBacklitImage, 24, 8, 56, 36, 0.025f, 0.025f, 0.025f);
    RawAutoBase::LocalSuggestionComponentReport invertedBacklitReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> invertedBacklitSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(
            baseAnalysis,
            invertedBacklitImage,
            &invertedBacklitReport);
    Require(invertedBacklitReport.brightBorderAreaPercent > 8.0f,
        "Local suggestions should record a bright connected border region independent of image orientation");
    Require(HasLocalSuggestionKind(
            invertedBacklitSuggestions,
            RawAutoBase::SuggestedLocalAdjustmentKind::OpenBacklitSubject),
        "Local suggestions should include Open backlit subject when the bright background is at the bottom or side of the frame");

    RawAutoBase::LocalSuggestionAnalysisImage shadowImage =
        MakeLocalSuggestionImage(80, 60, 0.40f, 0.40f, 0.40f);
    FillLocalSuggestionRect(shadowImage, 0, 0, 32, 60, 0.020f, 0.020f, 0.020f);
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> shadowSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, shadowImage, nullptr);
    const RawAutoBase::SuggestedLocalAdjustment* shadowSuggestion =
        FindLocalSuggestionKind(shadowSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows);
    Require(shadowSuggestion != nullptr && !shadowSuggestion->colorQualifierEnabled,
        "Local suggestions should include a luminance-only Open shadows suggestion for large shadow masses");

    RawRecipe::RawDevelopmentRecipe foliageRecipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");
    Require(foliageSuggestion != nullptr, "foliage suggestion should exist before recipe apply test");
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(*foliageSuggestion, foliageRecipe),
        "Applying a foliage suggestion should update the recipe");
    Require(foliageRecipe.localRange.enabled && foliageRecipe.localRange.colorMaskEnabled,
        "Applying a foliage suggestion should enable Local Range and color qualification");

    RawRecipe::RawDevelopmentRecipe shadowRecipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");
    shadowRecipe.localRange.colorMaskEnabled = true;
    shadowRecipe.localRange.colorMaskTargetR = 0.8f;
    shadowRecipe.localRange.colorMaskTargetG = 0.4f;
    shadowRecipe.localRange.colorMaskTargetB = 0.2f;
    Require(shadowSuggestion != nullptr, "shadow suggestion should exist before recipe apply test");
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(*shadowSuggestion, shadowRecipe),
        "Applying a shadow suggestion should update the recipe");
    Require(shadowRecipe.localRange.colorMaskEnabled &&
            std::abs(shadowRecipe.localRange.colorMaskTargetR - 0.8f) < 0.001f,
        "Applying a non-color local suggestion should preserve an existing user color mask");

    RawRecipe::RawDevelopmentRecipe overlapRecipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");
    overlapRecipe.localRange.points.push_back({ shadowSuggestion->targetEv + 0.10f, 0.25f });
    const std::size_t beforePointCount = overlapRecipe.localRange.points.size();
    Require(!RawAutoBase::ApplySuggestedLocalAdjustment(*shadowSuggestion, overlapRecipe),
        "Applying a local suggestion should refuse to overwrite a nearby user point");
    Require(overlapRecipe.localRange.points.size() == beforePointCount,
        "Failed local suggestion application should leave Local Range points unchanged");
}

Raw::RawImageData BuildMosaicRawProxyFixture(int width, int height) {
    Raw::RawImageData raw;
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = width;
    raw.metadata.visibleHeight = height;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.metadata.bitDepth = 16;
    raw.metadata.whiteLevel = 65535.0f;
    raw.rawBuffer.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < raw.rawBuffer.size(); ++i) {
        raw.rawBuffer[i] = static_cast<std::uint16_t>(i % 65535u);
    }
    return raw;
}

Raw::RawImageData BuildLinearRawProxyFixture(int width, int height, int channels) {
    Raw::RawImageData raw;
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = width;
    raw.metadata.visibleHeight = height;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::LinearRgb;
    raw.metadata.linearChannels = channels;
    raw.metadata.linearSampleFormat = Raw::RawSampleFormat::Float32;
    raw.metadata.whiteLevel = 1.0f;
    raw.linearFloatBuffer.resize(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        static_cast<std::size_t>(channels));
    for (std::size_t i = 0; i < raw.linearFloatBuffer.size(); ++i) {
        raw.linearFloatBuffer[i] = static_cast<float>((i % 1024u) / 1023.0f);
    }
    return raw;
}

void TestRawPreviewProxyUsesCappedRawData() {
    const Raw::RawImageData mosaic = BuildMosaicRawProxyFixture(800, 600);
    Raw::RawImageData mosaicPreview;
    Require(Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(mosaic, 200, mosaicPreview),
        "capped mosaic RAW preview should build a proxy buffer");
    const Stack::Renderer::RawPreviewProxy::Summary mosaicSummary =
        Stack::Renderer::RawPreviewProxy::Summarize(mosaicPreview, true);
    Require(mosaicSummary.usedProxy, "mosaic preview summary should report a proxy");
    Require(mosaicSummary.rawWidth == 200 && mosaicSummary.rawHeight == 150,
        "mosaic proxy should respect the requested preview cap");
    Require((mosaicSummary.rawWidth % 2) == 0 && (mosaicSummary.rawHeight % 2) == 0,
        "mosaic proxy dimensions should preserve Bayer parity");
    Require(mosaicSummary.rawSampleCount < mosaic.rawBuffer.size(),
        "mosaic proxy should use fewer RAW samples than the source");

    Raw::RawImageData cfaAverageFixture = BuildMosaicRawProxyFixture(8, 8);
    cfaAverageFixture.metadata.hasDngNoiseProfile = true;
    cfaAverageFixture.metadata.dngNoiseProfile = {
        Raw::DngNoiseProfilePlane { 0.016, 0.008 }
    };
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            cfaAverageFixture.rawBuffer[static_cast<std::size_t>(y * 8 + x)] =
                static_cast<std::uint16_t>(y * 10 + x);
        }
    }
    Raw::RawImageData cfaAveragePreview;
    Require(Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(cfaAverageFixture, 2, cfaAveragePreview) &&
            cfaAveragePreview.rawBuffer == std::vector<std::uint16_t>({ 33, 34, 43, 44 }),
        "mosaic proxy should area-average each CFA sublattice instead of selecting one nearest sensor sample");
    Require(
        cfaAveragePreview.metadata.dngNoiseProfile.size() == 1 &&
            std::abs(
                cfaAveragePreview.metadata.dngNoiseProfile[0].shotScale -
                0.001) <
                1.0e-9 &&
            std::abs(
                cfaAveragePreview.metadata.dngNoiseProfile[0].readNoiseVariance -
                0.0005) <
                1.0e-9,
        "mosaic proxy should scale DNG variance coefficients for its same-CFA area averaging");

    Raw::RawImageData activeAreaFixture = BuildMosaicRawProxyFixture(10, 8);
    std::fill(activeAreaFixture.rawBuffer.begin(), activeAreaFixture.rawBuffer.end(), 999);
    activeAreaFixture.metadata.hasDngActiveArea = true;
    activeAreaFixture.metadata.dngActiveArea = { 1, 2, 7, 8 };
    for (int y = 1; y < 7; ++y) {
        for (int x = 2; x < 8; ++x) {
            activeAreaFixture.rawBuffer[static_cast<std::size_t>(y * 10 + x)] = 100;
        }
    }
    Raw::RawImageData activeAreaPreview;
    Require(
        Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(
            activeAreaFixture,
            2,
            activeAreaPreview) &&
        activeAreaPreview.metadata.rawWidth == 2 &&
        activeAreaPreview.metadata.rawHeight == 2 &&
        activeAreaPreview.rawBuffer == std::vector<std::uint16_t>({ 100, 100, 100, 100 }),
        "mosaic proxy should crop and scale the declared DNG ActiveArea without averaging optical-black borders");

    Raw::RawImageData uncappedPreview;
    Require(!Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(mosaic, 0, uncappedPreview),
        "uncapped RAW preview should not build or reuse a capped proxy");

    const std::string cap200Key =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey("source", mosaic, 200);
    const std::string cap160Key =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey("source", mosaic, 160);
    const std::string uncappedKey =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey("source", mosaic, 0);
    Require(cap200Key != cap160Key,
        "RAW preview cache key should include the preview cap");
    Require(cap200Key != uncappedKey,
        "capped RAW preview cache key should not satisfy an uncapped render");

    Raw::RawImageData smallerMosaicPreview;
    Require(Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(mosaic, 160, smallerMosaicPreview),
        "alternate capped mosaic RAW preview should build a proxy buffer");
    Require(smallerMosaicPreview.metadata.rawWidth == 160 &&
            smallerMosaicPreview.metadata.rawHeight == 120,
        "alternate preview cap should produce distinct proxy dimensions");

    Raw::RawImageData gainMapMosaic = mosaic;
    gainMapMosaic.metadata.dngGainMapCount = 1;
    gainMapMosaic.metadata.dngGainMaps.push_back(Raw::DngGainMapOpcode {});
    Raw::RawImageData gainMapPreview;
    Require(!Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(gainMapMosaic, 200, gainMapPreview),
        "DNG gain-map RAW preview should not build a proxy that strips gain-map correction");
    Raw::RawImageData correctedFixture = BuildMosaicRawProxyFixture(8, 8);
    correctedFixture.contentIdentity = "sha256:fixture#decoder:v1";
    correctedFixture.contentIdentityHash = 42;
    correctedFixture.metadata.blackLevel = 100.0f;
    correctedFixture.metadata.whiteLevel = 1100.0f;
    std::fill(correctedFixture.rawBuffer.begin(), correctedFixture.rawBuffer.end(), 500);
    Raw::DngGainMapOpcode gainMap;
    gainMap.bottom = 8;
    gainMap.right = 8;
    gainMap.mapPointsV = 1;
    gainMap.mapPointsH = 1;
    gainMap.mapPlanes = 1;
    gainMap.mapSpacingV = 1.0;
    gainMap.mapSpacingH = 1.0;
    gainMap.gains = { 2.0f };
    correctedFixture.metadata.dngGainMaps = { gainMap };
    correctedFixture.metadata.dngGainMapCount = 1;
    Raw::RawImageData correctedPreview;
    Require(
        Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(
            correctedFixture,
            2,
            correctedPreview) &&
            correctedPreview.rawBuffer.empty() &&
            correctedPreview.normalizedMosaicBuffer &&
            correctedPreview.normalizedMosaicBuffer->size() == 4u &&
            std::all_of(
                correctedPreview.normalizedMosaicBuffer->begin(),
                correctedPreview.normalizedMosaicBuffer->end(),
                [](float value) { return std::abs(value - 0.8f) < 0.0001f; }) &&
            correctedPreview.metadata.dngGainMaps.empty(),
        "Truthful V2 should build metadata-correct proxies after black/white normalization and gain maps");
    Raw::RawImageData nonlinearMosaic = mosaic;
    nonlinearMosaic.metadata.dngLinearizationTable = { 0, 1, 4, 9 };
    Raw::RawImageData nonlinearPreview;
    Require(!Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(nonlinearMosaic, 200, nonlinearPreview),
        "mosaic proxy should decline nonlinear stored samples rather than average them in the wrong domain");
    Raw::RawImageData repeatedBlackMosaic = mosaic;
    repeatedBlackMosaic.metadata.dngBlackLevelRepeatDim = { 4, 4 };
    Raw::RawImageData repeatedBlackPreview;
    Require(
        !Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(
            repeatedBlackMosaic,
            200,
            repeatedBlackPreview),
        "mosaic proxy should decline black-level patterns larger than the preserved 2x2 CFA sublattices");

    const Raw::RawImageData linear = BuildLinearRawProxyFixture(800, 600, 3);
    Raw::RawImageData linearPreview;
    Require(Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(linear, 200, linearPreview),
        "capped linear RAW preview should build a proxy buffer");
    const Stack::Renderer::RawPreviewProxy::Summary linearSummary =
        Stack::Renderer::RawPreviewProxy::Summarize(linearPreview, true);
    Require(linearSummary.rawWidth == 200 && linearSummary.rawHeight == 150,
        "linear proxy should respect the requested preview cap");
    Require(linearSummary.linearFloatSampleCount < linear.linearFloatBuffer.size(),
        "linear proxy should use fewer linear samples than the source");
    Require(linearSummary.linearFloatSampleCount ==
            static_cast<std::size_t>(200) * static_cast<std::size_t>(150) * static_cast<std::size_t>(3),
        "linear proxy sample count should match dimensions and channel count");
}

void TestCompactRawDevelopmentNodeGraphContract() {
    using namespace EditorNodeGraph;

    RawDevelopmentPayload payload;
    payload.recipe = Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0002.dng", "IMG_0002.dng");
    payload.recipe.source.relativePathKey = "card/IMG_0002.dng";
    payload.recipe.preToneExposureEv = -0.25f;
    payload.projectStatus = "Unknown";

    Graph graph;
    const int rawDevelopmentId = NodeId(graph.AddRawDevelopmentNode(payload, { 0.0f, 0.0f }));
    const int layerId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 260.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 520.0f, 0.0f }, true));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 260.0f, 180.0f }));

    const Node* rawDevelopmentNode = graph.FindNode(rawDevelopmentId);
    Require(rawDevelopmentNode != nullptr, "RAW Development node should be created");
    Require(rawDevelopmentNode->kind == NodeKind::RawDevelopment,
        "RAW Development node should use the compact node kind");
    Require(EditorNodeGraphDefinitions::DefaultInputSocket(*rawDevelopmentNode).empty(),
        "RAW Development node should not expose RAW internals as an input by default");
    Require(EditorNodeGraphDefinitions::DefaultOutputSocket(*rawDevelopmentNode) == kImageOutputSocketId,
        "RAW Development node should expose an image output");

    Require(graph.TryConnectSockets(rawDevelopmentId, kImageOutputSocketId, layerId, kImageInputSocketId),
        "RAW Development image output should connect to downstream graph editing");
    Require(graph.TryConnectSockets(layerId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "RAW Development downstream edit should connect to output");
    Require(graph.IsOutputConnected(),
        "RAW Development node should complete a normal image output chain");
    Require(!graph.CanConnectSockets(rawDevelopmentId, kImageOutputSocketId, rawDecodeId, kRawInputSocketId),
        "RAW Development node should not feed legacy RAW sockets");

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);
    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 1, {}, 0, 0, 0);
    const Node* loadedRawDevelopment = loaded.FindNode(rawDevelopmentId);
    Require(loadedRawDevelopment != nullptr,
        "RAW Development node should survive graph serialization");
    Require(loadedRawDevelopment->kind == NodeKind::RawDevelopment,
        "RAW Development node kind should survive graph serialization");
    Require(loadedRawDevelopment->rawDevelopment.recipe.source.relativePathKey == "card/IMG_0002.dng",
        "RAW Development recipe should survive graph serialization");
    Require(std::abs(loadedRawDevelopment->rawDevelopment.recipe.preToneExposureEv - -0.25f) < 0.001f,
        "RAW Development recipe exposure should survive graph serialization");
    Require(loadedRawDevelopment->rawDevelopment.projectStatus == "Unknown",
        "RAW Development neutral project status should survive graph serialization");
}

void TestRawPreviewProxyPreservesMosaicUntilDemosaic() {
    const Raw::RawImageData mosaic = BuildMosaicRawProxyFixture(800, 600);
    Raw::RawImageData mosaicPreview;
    Require(
        !Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(
            mosaic,
            200,
            mosaicPreview),
        "mosaic RAW must remain at sensor resolution until after demosaic");

    const Raw::RawImageData linear = BuildLinearRawProxyFixture(800, 600, 3);
    Raw::RawImageData linearPreview;
    Require(
        Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(
            linear,
            200,
            linearPreview) &&
            linearPreview.metadata.rawWidth == 200 &&
            linearPreview.metadata.rawHeight == 150,
        "already-linear RAW may still use a display-sized source proxy");
}

void TestRawDevelopNodeSerializesCurrentFields() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawDevelopPayload payload;
    payload.settings.exposureStops = 1.25f;
    payload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::Auto;
    payload.settings.mosaicDenoise.enabled = true;
    payload.settings.mosaicDenoise.mode =
        Raw::RawMosaicDenoiseMode::DngNoiseProfile;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    const int rawDevelopId = NodeId(graph.AddRawDevelopNode(payload, { 180.0f, 120.0f }));

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);
    const Node* rawDevelopNode = loaded.FindNode(rawDevelopId);
    Require(rawDevelopNode != nullptr, "RawDevelop node should survive current graph serialization");
    Require(rawDevelopNode->kind == NodeKind::RawDevelop,
        "RawDevelop node should retain its current node kind");
    Require(std::abs(rawDevelopNode->rawDevelop.settings.exposureStops - 1.25f) < 0.001f,
        "RawDevelop exposure should survive graph serialization");
    Require(rawDevelopNode->rawDevelop.settings.whiteBalanceMode == Raw::WhiteBalanceMode::Auto,
        "RawDevelop white balance should survive graph serialization");
    Require(
        rawDevelopNode->rawDevelop.settings.mosaicDenoise.enabled &&
            rawDevelopNode->rawDevelop.settings.mosaicDenoise.mode ==
                Raw::RawMosaicDenoiseMode::DngNoiseProfile,
        "RAW mosaic denoise mode should survive graph serialization");

}

void TestManagedRawSectionValidationAndSync() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawSourcePayload sourcePayload;
    sourcePayload.sourcePath = "D:/shoot/card/IMG_0100.dng";
    sourcePayload.label = "IMG_0100.dng";
    sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));

    RawDecodePayload decodePayload;
    decodePayload.settings.exposureStops = 1.25f;
    decodePayload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::Manual;
    decodePayload.settings.manualWhiteBalance = { 2.0f, 1.0f, 1.5f };
    decodePayload.settings.rotationDegrees = 90;
    decodePayload.settings.flipHorizontally = true;
    decodePayload.settings.flipVertically = true;
    decodePayload.settings.mosaicDenoise.enabled = true;
    decodePayload.settings.mosaicDenoise.mode =
        Raw::RawMosaicDenoiseMode::DngNoiseProfile;
    decodePayload.settings.mosaicDenoise.lumaStrength = 0.44f;
    decodePayload.settings.mosaicDenoise.chromaStrength = 0.66f;
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "managed RAW source should connect to RAW Decode");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "managed RAW Decode should connect to Tone Curve");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "managed Tone Curve should connect to View Transform");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "managed View Transform should connect downstream");

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(sourcePayload.sourcePath, sourcePayload.label);
    recipe.source.relativePathKey = "Day 1/IMG_0100.dng";
    recipe.source.fingerprint = "fingerprint-0100";
    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);

    Stack::RawWorkspace::ManagedRawValidationResult validation =
        Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe);
    Require(validation.valid, "baseline managed RAW section should validate");
    Require(std::abs(validation.recipe.preToneExposureEv - 1.25f) < 0.001f,
        "managed RAW Decode exposure should sync back to recipe");
    Require(validation.recipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers,
        "manual RAW Decode white balance should sync back to custom multiplier recipe mode");
    Require(validation.recipe.whiteBalance.hasMultipliers,
        "manual RAW Decode white balance should preserve multipliers in recipe");
    Require(validation.recipe.cropRotation.rotationDegrees == 90,
        "managed RAW Decode rotation should sync back to recipe");
    Require(
        validation.recipe.cropRotation.flipHorizontally &&
            validation.recipe.cropRotation.flipVertically,
        "managed RAW Decode orientation flips should sync back to recipe");
    Require(
        validation.recipe.technical.mosaicDenoise.enabled &&
            validation.recipe.technical.mosaicDenoise.mode ==
                Raw::RawMosaicDenoiseMode::DngNoiseProfile &&
            std::abs(
                validation.recipe.technical.mosaicDenoise.lumaStrength -
                0.44f) < 0.001f &&
            std::abs(
                validation.recipe.technical.mosaicDenoise.chromaStrength -
                0.66f) < 0.001f,
        "managed RAW Decode denoise should sync back to the shared RAW recipe");

    const Stack::RawWorkspace::ManagedRawSection loadedSection =
        Stack::RawWorkspace::DeserializeManagedRawSection(
            Stack::RawWorkspace::SerializeManagedRawSection(section));
    validation = Stack::RawWorkspace::ValidateManagedRawSection(graph, loadedSection, recipe);
    Require(validation.valid, "serialized managed RAW section metadata should validate");

    const std::string originalSourcePath = graph.FindNode(rawSourceId)->rawSource.sourcePath;
    graph.FindNode(rawSourceId)->rawSource.sourcePath = "D:/shoot/card/IMG_9999.dng";
    validation = Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe);
    Require(!validation.valid,
        "managed RAW section should reject a raw source path that drifts away from the recipe");
    graph.FindNode(rawSourceId)->rawSource.sourcePath = originalSourcePath;

    Stack::RawWorkspace::ManagedRawSection adoptedSection;
    Stack::RawRecipe::RawDevelopmentRecipe adoptedRecipe;
    std::string reason;
    Require(Stack::RawWorkspace::TryBuildManagedRawSectionFromGraph(
                graph,
                recipe,
                adoptedSection,
                adoptedRecipe,
                &reason),
        "valid graph-first RAW chain should be adoptable as managed RAW");
    Require(adoptedSection.rawDecodeNodeId == rawDecodeId,
        "graph-first adoption should capture the RAW Decode node id");
}

void TestManagedRawSectionRejectsCustomGraphChanges() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(RawSourcePayload{}, { 0.0f, 0.0f }));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "managed RAW source should connect");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "managed RAW Decode should connect");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "managed tone should connect");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "managed view should connect");

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0101.dng", "IMG_0101.dng");
    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-invalid",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    Require(Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "baseline managed RAW graph should validate before mutation");

    graph.RemoveLink(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId);
    const int mixId = NodeId(graph.AddMixNode({ 390.0f, 80.0f }));
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, mixId, kMixInputASocketId),
        "custom inserted mix should connect after RAW Decode");
    Require(graph.TryConnectSockets(mixId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "custom inserted mix should feed Tone Curve");
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "custom node insertion inside managed RAW section should fail validation");
}

void TestManagedRawSectionRepairsMissingRequiredLinksOnly() {
    using namespace EditorNodeGraph;

    auto buildManagedGraph = [](
        Graph& graph,
        Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        Stack::RawWorkspace::ManagedRawSection& section,
        int& rawSourceId,
        int& rawDecodeId,
        int& toneCurveId,
        int& viewTransformId) {
        RawSourcePayload sourcePayload;
        sourcePayload.sourcePath = "D:/shoot/card/IMG_0200.dng";
        sourcePayload.label = "IMG_0200.dng";
        sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
        rawSourceId = NodeId(graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));

        RawDecodePayload decodePayload;
        decodePayload.settings.exposureStops = 0.5f;
        rawDecodeId = NodeId(graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
        toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
        viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
        const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));

        Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
            "repair test managed RAW source should connect");
        Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
            "repair test managed RAW decode should connect");
        Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
            "repair test managed RAW tone should connect");
        Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
            "repair test managed RAW view should connect downstream");

        recipe = Stack::RawRecipe::MakeDefaultRecipe(sourcePayload.sourcePath, sourcePayload.label);
        recipe.source.relativePathKey = "Day 1/IMG_0200.dng";
        recipe.source.fingerprint = "fingerprint-0200";
        section = Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-repair",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    };

    Graph brokenLinkGraph;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::ManagedRawSection section;
    int rawSourceId = 0;
    int rawDecodeId = 0;
    int toneCurveId = 0;
    int viewTransformId = 0;
    buildManagedGraph(brokenLinkGraph, recipe, section, rawSourceId, rawDecodeId, toneCurveId, viewTransformId);
    Require(Stack::RawWorkspace::ValidateManagedRawSection(brokenLinkGraph, section, recipe).valid,
        "repair test baseline managed RAW graph should validate");

    Require(brokenLinkGraph.RemoveLink(
            rawDecodeId,
            kImageOutputSocketId,
            toneCurveId,
            kImageInputSocketId),
        "repair test should remove the managed decode-to-tone link");
    const Stack::RawWorkspace::ManagedRawValidationResult brokenValidation =
        Stack::RawWorkspace::ValidateManagedRawSection(brokenLinkGraph, section, recipe);
    Require(!brokenValidation.valid && brokenValidation.repairable,
        "missing required managed link should be marked repairable");
    const Stack::RawWorkspace::ManagedRawRepairResult repaired =
        Stack::RawWorkspace::RepairManagedRawSectionGraph(brokenLinkGraph, section, recipe);
    Require(repaired.repaired && repaired.changed,
        "repair should reconnect a missing required managed RAW link");
    Require(Stack::RawWorkspace::ValidateManagedRawSection(brokenLinkGraph, section, recipe).valid,
        "repaired managed RAW graph should validate");
    Require(brokenLinkGraph.HasLink(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "repair should restore the required decode-to-tone link");

    Graph customGraph;
    Stack::RawRecipe::RawDevelopmentRecipe customRecipe;
    Stack::RawWorkspace::ManagedRawSection customSection;
    int customRawSourceId = 0;
    int customRawDecodeId = 0;
    int customToneCurveId = 0;
    int customViewTransformId = 0;
    buildManagedGraph(
        customGraph,
        customRecipe,
        customSection,
        customRawSourceId,
        customRawDecodeId,
        customToneCurveId,
        customViewTransformId);
    Require(customGraph.RemoveLink(
            customRawDecodeId,
            kImageOutputSocketId,
            customToneCurveId,
            kImageInputSocketId),
        "repair refusal test should remove the managed decode-to-tone link");
    const int mixId = NodeId(customGraph.AddMixNode({ 390.0f, 80.0f }));
    Require(customGraph.TryConnectSockets(customRawDecodeId, kImageOutputSocketId, mixId, kMixInputASocketId),
        "repair refusal test custom mix should connect after RAW Decode");
    Require(customGraph.TryConnectSockets(mixId, kImageOutputSocketId, customToneCurveId, kImageInputSocketId),
        "repair refusal test custom mix should feed Tone Curve");
    const Stack::RawWorkspace::ManagedRawRepairResult refused =
        Stack::RawWorkspace::RepairManagedRawSectionGraph(customGraph, customSection, customRecipe);
    Require(!refused.repaired && !refused.changed,
        "repair should refuse custom internal managed RAW graph changes");
    Require(customGraph.HasLink(customRawDecodeId, kImageOutputSocketId, mixId, kMixInputASocketId) &&
            customGraph.HasLink(mixId, kImageOutputSocketId, customToneCurveId, kImageInputSocketId),
        "repair should not remove or bypass custom internal graph edits");
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(customGraph, customSection, customRecipe).valid,
        "custom internal graph should remain invalid after refused repair");
}

void TestManagedRawSectionMutationWarnings() {
    using namespace EditorNodeGraph;

    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-warnings",
            "project-local-test",
            "Day 1/IMG_0201.dng",
            "fingerprint-0201",
            -1,
            1,
            2,
            3,
            4);

    Require(!Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                1,
                kRawOutputSocketId,
                2,
                kRawInputSocketId).requiresConfirmation,
        "required managed RAW source-to-decode connection should not warn");
    Require(Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                2,
                kImageOutputSocketId,
                99,
                kImageInputSocketId).requiresConfirmation,
        "branching from an internal managed RAW stage should warn before mutation");
    Require(Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                99,
                kImageOutputSocketId,
                3,
                kImageInputSocketId).requiresConfirmation,
        "replacing an internal managed RAW stage input should warn before mutation");
    Require(!Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                4,
                kImageOutputSocketId,
                99,
                kImageInputSocketId).requiresConfirmation,
        "connecting downstream from managed View Transform output should remain allowed without warning");

    Require(Stack::RawWorkspace::BuildManagedRawGraphLinkRemovalWarning(
                section,
                2,
                kImageOutputSocketId,
                3,
                kImageInputSocketId).requiresConfirmation,
        "removing a required managed RAW link should warn before mutation");
    Require(!Stack::RawWorkspace::BuildManagedRawGraphLinkRemovalWarning(
                section,
                4,
                kImageOutputSocketId,
                99,
                kImageInputSocketId).requiresConfirmation,
        "removing a downstream link after the managed View Transform should not warn");

    Require(Stack::RawWorkspace::BuildManagedRawGraphNodeRemovalWarning(section, 2).requiresConfirmation,
        "removing a managed RAW chain node should warn before mutation");
    Require(!Stack::RawWorkspace::BuildManagedRawGraphNodeRemovalWarning(section, 99).requiresConfirmation,
        "removing a non-managed node should not warn");
}

void TestManagedRawSectionRejectsFlexibleReorderingInV1() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawSourcePayload sourcePayload;
    sourcePayload.sourcePath = "D:/shoot/card/IMG_0202.dng";
    sourcePayload.label = "IMG_0202.dng";
    sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "V1 reordering test managed RAW source should connect");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "V1 reordering test managed RAW decode should connect");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "V1 reordering test managed RAW tone should connect");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "V1 reordering test managed RAW view should connect downstream");

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(sourcePayload.sourcePath, sourcePayload.label);
    recipe.source.relativePathKey = "Day 1/IMG_0202.dng";
    recipe.source.fingerprint = "fingerprint-0202";
    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-reorder",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    Require(Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "V1 reordering test baseline managed RAW graph should validate");

    Stack::RawWorkspace::ManagedRawSection reorderedMetadata = section;
    reorderedMetadata.orderedNodeIds = {
        rawSourceId,
        rawDecodeId,
        viewTransformId,
        toneCurveId
    };
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(graph, reorderedMetadata, recipe).valid,
        "V1 should reject flexible-stage metadata reordering until a round-trip contract exists");

    Graph reorderedGraph = graph;
    Require(reorderedGraph.RemoveLink(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "V1 reordering test should remove decode-to-tone");
    Require(reorderedGraph.RemoveLink(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "V1 reordering test should remove tone-to-view");
    Require(reorderedGraph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "V1 reordering test should connect decode directly to View Transform");
    Require(reorderedGraph.TryConnectSockets(viewTransformId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "V1 reordering test should connect View Transform into Tone Curve");
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(reorderedGraph, section, recipe).valid,
        "V1 should reject graph stage reordering instead of treating it as RAW-tab editable");
}

void TestManagedRawSectionBlocksUnsupportedRecipeAndDecodeFields() {
    using namespace EditorNodeGraph;

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0102.dng", "IMG_0102.dng");
    std::string reason;
    Require(Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(recipe, &reason),
        "default RAW recipe should be representable as managed graph");

    Stack::RawRecipe::RawDevelopmentRecipe customTone = recipe;
    customTone.finishTone.layerJson = Stack::RawRecipe::DefaultFinishToneJson();
    customTone.finishTone.layerJson["points"] = nlohmann::json::array({
        { { "x", 0.0f }, { "y", 0.0f }, { "shape", 1 } },
        { { "x", 0.5f }, { "y", 0.65f }, { "shape", 1 } },
        { { "x", 1.0f }, { "y", 1.0f }, { "shape", 1 } }
    });
    customTone.viewTransform.layerJson = Stack::RawRecipe::DefaultViewTransformJson();
    customTone.viewTransform.layerJson["contrast"] = 1.20f;
    Require(Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(customTone, &reason),
        "custom finish tone and view transform should decompose into managed graph layers");

    Stack::RawRecipe::RawDevelopmentRecipe denoised = recipe;
    denoised.technical.mosaicDenoise.enabled = true;
    denoised.technical.mosaicDenoise.mode =
        Raw::RawMosaicDenoiseMode::DngNoiseProfile;
    Require(
        Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(
            denoised,
            &reason),
        "authored pre-demosaic denoise should remain representable in the managed RAW graph");

    Stack::RawRecipe::RawDevelopmentRecipe rgbDenoised = recipe;
    rgbDenoised.rgbDenoise.enabled = true;
    rgbDenoised.rgbDenoise.lumaMap.baseMultiplier = 1.0f;
    Require(
        !Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(
            rgbDenoised,
            &reason),
        "post-demosaic RGB denoise should block managed decomposition until a dedicated graph stage exists");

    Stack::RawRecipe::RawDevelopmentRecipe cropped = recipe;
    cropped.cropRotation.cropEnabled = true;
    Require(!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(cropped, &reason),
        "crop should block managed decomposition until a crop node mapping exists");

    Stack::RawRecipe::RawDevelopmentRecipe localRange = recipe;
    localRange.localRange.enabled = true;
    localRange.localRange.points[1].deltaEv = 1.0f;
    Require(!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(localRange, &reason),
        "local range should block managed decomposition until a managed stage mapping exists");

    Graph graph;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(RawSourcePayload{}, { 0.0f, 0.0f }));
    RawDecodePayload decodePayload;
    decodePayload.settings.highlightMode = Raw::HighlightReconstructionMode::Luminance;
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "managed RAW source should connect for unsupported settings test");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "managed RAW Decode should connect for unsupported settings test");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "managed tone should connect for unsupported settings test");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "managed view should connect for unsupported settings test");

    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-unsupported-settings",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "unsupported RAW Decode settings should fail managed validation");
}

void TestGraphCaptureLinkedResolution() {
    namespace Capture = Stack::EditorGraphCapture;

    Capture::Settings landscape;
    Capture::InitializeEightKLongEdge(landscape, 16.0f / 9.0f);
    Require(landscape.width == 7680 && landscape.height == 4320,
        "graph capture should default a landscape canvas to an 8K long edge");
    Require(landscape.resolutionDriver == Capture::ResolutionDriver::Width,
        "landscape graph capture should use width as its default driving dimension");

    Capture::Settings portrait;
    Capture::InitializeEightKLongEdge(portrait, 0.5f);
    Require(portrait.width == 3840 && portrait.height == 7680,
        "graph capture should default a portrait canvas to an 8K long edge");
    Require(portrait.resolutionDriver == Capture::ResolutionDriver::Height,
        "portrait graph capture should use height as its default driving dimension");

    landscape.width = 6001;
    landscape.resolutionDriver = Capture::ResolutionDriver::Width;
    Capture::ResolveLinkedResolution(landscape, 2.0f);
    Require(landscape.width == 6001 && landscape.height == 3001,
        "width-driven graph capture should round the linked height to the nearest pixel");

    portrait.height = 3001;
    portrait.resolutionDriver = Capture::ResolutionDriver::Height;
    Capture::ResolveLinkedResolution(portrait, 0.75f);
    Require(portrait.width == 2251 && portrait.height == 3001,
        "height-driven graph capture should round the linked width to the nearest pixel");
}

void TestGraphCaptureResolutionValidation() {
    namespace Capture = Stack::EditorGraphCapture;
    const Capture::ResolutionLimits limits;
    std::string error;
    Require(Capture::ValidateResolution(7680, 4320, limits, &error) && error.empty(),
        "8K graph capture dimensions should pass validation");
    Require(!Capture::ValidateResolution(16385, 1000, limits, &error),
        "graph capture should reject dimensions beyond the edge ceiling");
    Require(!Capture::ValidateResolution(12000, 9000, limits, &error),
        "graph capture should reject dimensions beyond the 100-megapixel budget");
    Require(!Capture::ValidateResolution(7680, 63, limits, &error),
        "graph capture should reject an unusably small linked edge");
    Require(!Capture::ValidateResolution(0, 4320, limits, &error),
        "graph capture should reject non-positive dimensions");
}

void TestGraphCaptureBoundsFittingAndPadding() {
    namespace Capture = Stack::EditorGraphCapture;
    Capture::FloatBounds bounds;
    bounds.minX = 0.0f;
    bounds.minY = 0.0f;
    bounds.maxX = 100.0f;
    bounds.maxY = 50.0f;
    bounds.valid = true;

    const Capture::CameraTransform noPadding =
        Capture::FitBoundsToCanvas(bounds, 1000.0f, 500.0f, 0.0f, 0.01f, 100.0f);
    Require(std::abs(noPadding.zoom - 10.0f) < 0.001f &&
            std::abs(noPadding.panX) < 0.001f && std::abs(noPadding.panY) < 0.001f,
        "zero-padding graph capture should fit matching-aspect bounds edge to edge");

    const Capture::CameraTransform fivePercent =
        Capture::FitBoundsToCanvas(bounds, 1000.0f, 500.0f, 5.0f, 0.01f, 100.0f);
    Require(std::abs(fivePercent.zoom - 9.0f) < 0.001f &&
            std::abs(fivePercent.panX - 50.0f) < 0.001f &&
            std::abs(fivePercent.panY - 25.0f) < 0.001f,
        "five-percent graph capture padding should be based on the shorter canvas edge");

    const Capture::CameraTransform twentyFivePercent =
        Capture::FitBoundsToCanvas(bounds, 1000.0f, 500.0f, 25.0f, 0.01f, 100.0f);
    Require(std::abs(twentyFivePercent.zoom - 5.0f) < 0.001f &&
            std::abs(twentyFivePercent.panX - 250.0f) < 0.001f &&
            std::abs(twentyFivePercent.panY - 125.0f) < 0.001f,
        "twenty-five-percent graph capture padding should preserve centered bounds");

    Capture::FloatBounds emptyBounds;
    const Capture::CameraTransform empty =
        Capture::FitBoundsToCanvas(emptyBounds, 1000.0f, 500.0f, 5.0f);
    Require(empty.zoom == 1.0f && empty.panX == 0.0f && empty.panY == 0.0f,
        "empty graph bounds should leave the capture camera at its neutral transform");
}

void TestGraphCaptureNodeStatePresetDoesNotMutateSource() {
    namespace Capture = Stack::EditorGraphCapture;
    EditorNodeGraph::Graph source;
    const EditorNodeGraph::Node* first =
        source.AddOutputNode({ 0.0f, 0.0f }, true);
    const int firstId = first ? first->id : -1;
    const EditorNodeGraph::Node* second =
        source.AddPreviewNode({ 240.0f, 0.0f });
    const int secondId = second ? second->id : -1;
    Require(firstId > 0 && secondId > 0,
        "graph capture node-state test should create fixture nodes");
    source.FindNode(firstId)->expanded = true;
    source.FindNode(secondId)->expanded = false;

    EditorNodeGraph::Graph asShown = source;
    Capture::ApplyNodeStatePreset(asShown, Capture::NodeState::AsShown);
    Require(asShown.GetNodes()[0].expanded && !asShown.GetNodes()[1].expanded,
        "as-shown graph capture should preserve each copied node state");

    EditorNodeGraph::Graph expanded = source;
    Capture::ApplyNodeStatePreset(expanded, Capture::NodeState::ExpandAll);
    Require(expanded.GetNodes()[0].expanded && expanded.GetNodes()[1].expanded,
        "expand-all graph capture should expand every copied node");

    EditorNodeGraph::Graph collapsed = source;
    Capture::ApplyNodeStatePreset(collapsed, Capture::NodeState::CollapseAll);
    Require(!collapsed.GetNodes()[0].expanded && !collapsed.GetNodes()[1].expanded,
        "collapse-all graph capture should collapse every copied node");
    Require(source.GetNodes()[0].expanded && !source.GetNodes()[1].expanded,
        "graph capture node-state presets must never mutate the source graph");
}

void TestGraphCaptureReadbackAndEncoding() {
    namespace Capture = Stack::EditorGraphCapture;
    std::vector<unsigned char> pixels = {
        50, 25, 0, 128,
        10, 20, 30, 255,
    };
    Require(Capture::NormalizeReadbackRgba(pixels, 1, 2, true),
        "graph capture should normalize a complete OpenGL RGBA readback");
    Require(pixels[0] == 10 && pixels[1] == 20 && pixels[2] == 30 && pixels[3] == 255,
        "graph capture should flip OpenGL rows into top-left image order");
    Require(pixels[4] == 100 && pixels[5] == 50 && pixels[6] == 0 && pixels[7] == 128,
        "transparent graph capture should un-premultiply partially transparent RGB");

    const std::vector<unsigned char> rgba = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    std::vector<unsigned char> png;
    Require(Capture::EncodePng(rgba, 2, 2, png),
        "graph capture should encode PNG bytes");
    Require(png.size() > 24 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G',
        "graph capture PNG should have the expected signature");
    Require(png[19] == 2 && png[23] == 2,
        "graph capture PNG should preserve the requested dimensions");

    std::vector<unsigned char> bmp;
    Require(Capture::EncodeBmp(rgba, 2, 2, bmp),
        "graph capture should encode BMP bytes");
    Require(bmp.size() > 26 && bmp[0] == 'B' && bmp[1] == 'M',
        "graph capture BMP should have the expected signature");
    Require(bmp[18] == 2 && bmp[22] == 2,
        "graph capture BMP should preserve the requested dimensions");
}

void TestPhase3TypedValuesAndUnifiedDefinitions() {
    std::vector<std::string> registryErrors;
    Require(EditorNodeGraphDefinitions::ValidateUnifiedNodeDefinitionRegistry(&registryErrors),
        registryErrors.empty() ? "unified node definition registry should validate" : registryErrors.front().c_str());

    const auto* brightnessDefinition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(
        EditorNodeGraph::NodeKind::Layer,
        static_cast<int>(LayerType::Brightness));
    Require(brightnessDefinition && !brightnessDefinition->identity.contentHash.empty(),
        "layer-backed nodes should resolve through the unified exact definition registry");
    Require(brightnessDefinition && !brightnessDefinition->parameters.empty(),
        "layer-backed definitions should share the declarative timeline parameter catalog");

    EditorNodeGraph::Node split;
    split.kind = EditorNodeGraph::NodeKind::ChannelSplit;
    const auto splitSockets = EditorNodeGraphDefinitions::BuildSockets(split, false);
    const auto red = std::find_if(splitSockets.begin(), splitSockets.end(), [](const auto& socket) {
        return socket.id == "r";
    });
    Require(red != splitSockets.end() && red->type == EditorNodeGraph::SocketType::Channel,
        "Channel Split should expose exact Channel sockets rather than masks");

    EditorNodeGraph::Graph graph;
    const int scalarId = graph.AddValueNode(Stack::NodeMath::MakeUniformScalar(2.0), { 0.0f, 0.0f })->id;
    const int unknownId = graph.AddValueNode(
        Stack::NodeMath::MakeUnknownValue(
            Stack::NodeMath::LogicalValueType::Scalar,
            Stack::NodeMath::ValueStorageClass::Uniform),
        { 0.0f, 120.0f })->id;
    const int missingId = graph.AddValueNode(
        Stack::NodeMath::MakeMissingValue(
            Stack::NodeMath::LogicalValueType::Scalar,
            Stack::NodeMath::ValueStorageClass::Uniform,
            "required measurement is unavailable"),
        { 0.0f, 240.0f })->id;
    const int vectorId = graph.AddValueNode(
        Stack::NodeMath::FirstClassValue{
            Stack::NodeMath::kFirstClassValueSchemaVersion,
            Stack::NodeMath::LogicalValueType::Vector2,
            Stack::NodeMath::ValueStorageClass::Uniform,
            Stack::NodeMath::ValueAvailability::Known,
            {}, std::array<double, 2>{ 1.0, 2.0 }, {} },
        { 0.0f, 360.0f })->id;
    const int exposureId = graph.AddTechnicalImageNode(
        Stack::NodeMath::TechnicalImageOperation::Exposure, { 280.0f, 0.0f })->id;

    Require(graph.TryConnectSockets(
        scalarId, EditorNodeGraph::kValueOutputSocketId,
        exposureId, EditorNodeGraph::kExposureValueInputSocketId),
        "a uniform Scalar Value should connect to Exposure EV");
    double resolvedExposure = 0.0;
    Require(graph.TryResolveUniformScalarInput(
            exposureId, EditorNodeGraph::kExposureValueInputSocketId, resolvedExposure) &&
            std::abs(resolvedExposure - 2.0) < 1e-12,
        "the renderer-facing binding should resolve the connected uniform Scalar exactly");
    Require(!graph.CanConnectSockets(
        vectorId, EditorNodeGraph::kValueOutputSocketId,
        exposureId, EditorNodeGraph::kExposureValueInputSocketId),
        "typed value sockets should reject implicit vector-to-scalar conversion");

    EditorNodeGraph::Graph nonFiniteGraph;
    const int nonFiniteSourceId = nonFiniteGraph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor,
        { 0.0f, 0.0f })->id;
    const int nonFiniteValueId = nonFiniteGraph.AddValueNode(
        Stack::NodeMath::MakeUniformScalar(
            std::numeric_limits<double>::infinity()),
        { 0.0f, 120.0f })->id;
    const int nonFiniteExposureId =
        nonFiniteGraph.AddTechnicalImageNode(
            Stack::NodeMath::TechnicalImageOperation::Exposure,
            { 260.0f, 0.0f })->id;
    const int nonFiniteOutputId =
        nonFiniteGraph.AddOutputNode({ 520.0f, 0.0f }, true)->id;
    std::string nonFiniteError;
    Require(!nonFiniteGraph.TryConnectSockets(
            nonFiniteValueId,
            EditorNodeGraph::kValueOutputSocketId,
            nonFiniteExposureId,
            EditorNodeGraph::kExposureValueInputSocketId,
            &nonFiniteError) &&
            nonFiniteError.find("finite") != std::string::npos,
        "authoring should reject a non-finite uniform parameter before it enters the render graph");
    nonFiniteGraph.EditLinks().push_back({
        nonFiniteSourceId,
        EditorNodeGraph::kImageOutputSocketId,
        nonFiniteExposureId,
        EditorNodeGraph::kImageInputSocketId
    });
    nonFiniteGraph.EditLinks().push_back({
        nonFiniteValueId,
        EditorNodeGraph::kValueOutputSocketId,
        nonFiniteExposureId,
        EditorNodeGraph::kExposureValueInputSocketId
    });
    nonFiniteGraph.EditLinks().push_back({
        nonFiniteExposureId,
        EditorNodeGraph::kImageOutputSocketId,
        nonFiniteOutputId,
        EditorNodeGraph::kImageInputSocketId
    });
    double nonFiniteResolved = 0.0;
    Require(!nonFiniteGraph.TryResolveUniformScalarInput(
                nonFiniteExposureId,
                EditorNodeGraph::kExposureValueInputSocketId,
                nonFiniteResolved) &&
            !nonFiniteGraph.IsOutputConnected() &&
            !nonFiniteGraph.Validate().valid,
        "loaded or programmatically injected non-finite Values should fail closed in lookup, completion, and validation");

    const nlohmann::json serialized = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), graph);
    Require(serialized["nodeGraph"].value("version", 0) == 9,
        "graphs should use the channel-first Output inspection schema version");
    EditorNodeGraph::Graph loaded;
    EditorNodeGraph::DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* loadedScalar = loaded.FindNode(scalarId);
    const EditorNodeGraph::Node* loadedUnknown = loaded.FindNode(unknownId);
    const EditorNodeGraph::Node* loadedMissing = loaded.FindNode(missingId);
    Require(loadedScalar && loadedScalar->definitionResolved &&
            loadedScalar->value.value == Stack::NodeMath::MakeUniformScalar(2.0),
        "known typed values and exact definition identities should round-trip");
    Require(loadedUnknown && loadedUnknown->value.value.availability == Stack::NodeMath::ValueAvailability::Unknown,
        "unknown values should remain distinct after save/load");
    Require(loadedMissing && loadedMissing->value.value.availability == Stack::NodeMath::ValueAvailability::Missing,
        "missing values should remain distinct after save/load");

    nlohmann::json invalidValueDocument = serialized;
    for (auto& item : invalidValueDocument["nodeGraph"]["nodes"]) {
        if (item.value("id", 0) == scalarId) item["value"]["payload"] = "not-a-number";
    }
    EditorNodeGraph::Graph invalidValueGraph;
    EditorNodeGraph::DeserializeGraphPayload(invalidValueDocument, invalidValueGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* invalidValue = invalidValueGraph.FindNode(scalarId);
    Require(invalidValue &&
            invalidValue->value.value.logicalType == Stack::NodeMath::LogicalValueType::Scalar &&
            invalidValue->value.value.availability == Stack::NodeMath::ValueAvailability::Missing,
        "an invalid saved payload should become a typed Missing value rather than a guessed default");

    nlohmann::json mismatchedDefinitionDocument = serialized;
    for (auto& item : mismatchedDefinitionDocument["nodeGraph"]["nodes"]) {
        if (item.value("id", 0) == scalarId) item["definition"]["contentHash"] = std::string(64, '0');
    }
    EditorNodeGraph::Graph mismatchedDefinitionGraph;
    EditorNodeGraph::DeserializeGraphPayload(mismatchedDefinitionDocument, mismatchedDefinitionGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* mismatchedDefinition = mismatchedDefinitionGraph.FindNode(scalarId);
    Require(mismatchedDefinition && !mismatchedDefinition->definitionResolved &&
            !mismatchedDefinition->definitionResolutionError.empty(),
        "a saved definition hash mismatch should remain explicitly unresolved without fallback");
    Require(!mismatchedDefinitionGraph.Validate().valid,
        "an unresolved exact definition should make graph validation fail explicitly");
}

void TestConstantChannelFoundation() {
    using namespace EditorNodeGraph;

    const auto* constantDefinition =
        EditorNodeGraphDefinitions::FindLiveNodeDefinition(
            NodeKind::ConstantChannel,
            0);
    const auto* combineDefinition =
        EditorNodeGraphDefinitions::FindLiveNodeDefinition(
            NodeKind::ChannelCombine,
            0);
    Require(
        constantDefinition &&
            constantDefinition->identity.version ==
                Stack::NodeMath::SemanticVersion{ 1, 0, 0 } &&
            combineDefinition &&
            combineDefinition->identity.version ==
                Stack::NodeMath::SemanticVersion{ 2, 0, 0 },
        "Constant Channel v1 and Image Combine v2 should have exact registered identities");

    Node prototype;
    prototype.kind = NodeKind::ConstantChannel;
    const std::vector<SocketDefinition> sockets =
        EditorNodeGraphDefinitions::BuildSockets(
            prototype,
            false);
    const auto extentSocket = std::find_if(
        sockets.begin(),
        sockets.end(),
        [](const SocketDefinition& socket) {
            return socket.id == kMatchExtentInputSocketId;
        });
    const auto outputSocket = std::find_if(
        sockets.begin(),
        sockets.end(),
        [](const SocketDefinition& socket) {
            return socket.id == kChannelOutputSocketId;
        });
    Require(
        extentSocket != sockets.end() &&
            extentSocket->direction == SocketDirection::Input &&
            extentSocket->type == SocketType::Channel &&
            !extentSocket->optional &&
            extentSocket->visibilityTier ==
                SocketVisibilityTier::Advanced &&
            outputSocket != sockets.end() &&
            outputSocket->direction == SocketDirection::Output &&
            outputSocket->type == SocketType::Channel,
        "Constant Channel should expose required advanced Match Extent and branchable Channel pins");

    Graph graph;
    graph.Clear();
    const int sourceId = graph.AddImageGeneratorNode(
        ImageGeneratorKind::SolidColor,
        { 0.0f, 0.0f })->id;
    const int splitId =
        graph.AddChannelSplitNode({ 220.0f, 0.0f })->id;
    const int constantId =
        graph.AddConstantChannelNode({ 440.0f, 160.0f })->id;
    const int combineId =
        graph.AddChannelCombineNode({ 660.0f, 0.0f })->id;
    const int outputId =
        graph.AddOutputNode({ 880.0f, 0.0f }, true)->id;
    Node* constant = graph.FindNode(constantId);
    Node* combine = graph.FindNode(combineId);
    Require(
        constant && combine,
        "Constant Channel persistence fixture should create its nodes");
    constant->constantChannelSettings.value = 0.375f;
    constant->constantChannelSettings.generatedOpaqueAlpha = true;
    combine->imageCombineSettings.autoAlphaSuppressed = true;

    Require(
        graph.TryConnectSockets(
            sourceId,
            kImageOutputSocketId,
            splitId,
            kImageInputSocketId) &&
            graph.TryConnectSockets(
                splitId,
                "r",
                constantId,
                kMatchExtentInputSocketId) &&
            graph.TryConnectSockets(
                splitId,
                "r",
                combineId,
                "r") &&
            graph.TryConnectSockets(
                splitId,
                "g",
                combineId,
                "g") &&
            graph.TryConnectSockets(
                splitId,
                "b",
                combineId,
                "b") &&
            graph.TryConnectSockets(
                constantId,
                kChannelOutputSocketId,
                combineId,
                "a") &&
            graph.TryConnectSockets(
                combineId,
                kImageOutputSocketId,
                outputId,
                kImageInputSocketId),
        "Constant Channel should author through exact Channel connections");
    Require(
        graph.IsScalarSocketStream(
            constantId,
            kChannelOutputSocketId) &&
            graph.ResolveSocketChannel(
                constantId,
                kChannelOutputSocketId) == "a" &&
            graph.IsOutputConnected() &&
            graph.Validate().valid,
        "generated opaque Constant Channel should retain Channel/Alpha identity in a valid output chain");

    const nlohmann::json saved =
        SerializeGraphPayload(nlohmann::json::array(), graph);
    Graph restored;
    DeserializeGraphPayload(saved, restored, 0, {}, 0, 0, 0);
    const Node* restoredConstant =
        restored.FindNode(constantId);
    const Node* restoredCombine =
        restored.FindNode(combineId);
    Require(
        restoredConstant &&
            restoredConstant->kind == NodeKind::ConstantChannel &&
            restoredConstant->constantChannelSettings.value ==
                0.375f &&
            restoredConstant->constantChannelSettings
                .generatedOpaqueAlpha &&
            restoredConstant->definitionResolved &&
            restoredCombine &&
            restoredCombine->imageCombineSettings
                .autoAlphaSuppressed &&
            restoredCombine->definitionResolved &&
            restored.IsOutputConnected(),
        "Constant value/purpose, Image Combine suppression, exact identities, and links should survive graph round-trip");

    nlohmann::json malformed = saved;
    for (auto& item : malformed["nodeGraph"]["nodes"]) {
        if (item.value("id", 0) == constantId) {
            item["constantChannelSettings"]["value"] =
                nullptr;
        }
    }
    Graph repaired;
    DeserializeGraphPayload(
        malformed,
        repaired,
        0,
        {},
        0,
        0,
        0);
    const Node* repairedConstant =
        repaired.FindNode(constantId);
    Require(
        repairedConstant &&
            repairedConstant->constantChannelSettings.value ==
                1.0f,
        "invalid saved Constant Channel values should repair to the finite opaque default");

}

void TestPhase6BFieldMeanGraphContract() {
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(
        EditorNodeGraph::NodeKind::FieldMean, 0);
    Require(definition && definition->identity.id == "stack:analysis/field-mean" &&
            definition->identity.version == Stack::NodeMath::SemanticVersion{ 1, 0, 0 },
        "Field Mean should resolve its exact public reduction definition");

    EditorNodeGraph::Graph graph;
    const int imageId = graph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor, { 0.0f, 0.0f })->id;
    const int splitId = graph.AddChannelSplitNode({ 260.0f, 0.0f })->id;
    const int meanId = graph.AddFieldMeanNode({ 520.0f, 120.0f })->id;
    const int scalarAverageId = graph.AddDataMathNode(
        EditorNodeGraph::DataMathMode::Average, { 520.0f, 260.0f })->id;
    const int scalarReformatId = graph.AddReformatNode({ 700.0f, 260.0f })->id;
    const int exposureId = graph.AddTechnicalImageNode(
        Stack::NodeMath::TechnicalImageOperation::Exposure, { 780.0f, 0.0f })->id;
    const int outputId = graph.AddOutputNode({ 1040.0f, 0.0f }, true)->id;

    const EditorNodeGraph::Node* meanNode = graph.FindNode(meanId);
    Require(meanNode && meanNode->definitionResolved &&
            meanNode->definitionId == "stack:analysis/field-mean",
        "a new Field Mean node should pin its exact installed definition");
    EditorNodeGraph::SocketDefinition inputSocket;
    EditorNodeGraph::SocketDefinition outputSocket;
    Require(graph.FindSocket(meanId, EditorNodeGraph::kReductionFieldInputSocketId, &inputSocket) &&
            inputSocket.direction == EditorNodeGraph::SocketDirection::Input &&
            inputSocket.type == EditorNodeGraph::SocketType::ScalarField &&
            inputSocket.logicalType == Stack::NodeMath::LogicalValueType::ScalarField &&
            inputSocket.semanticRoleKey == "field" &&
            inputSocket.visibilityTier == EditorNodeGraph::SocketVisibilityTier::Required,
        "Field Mean should declare one required per-pixel ScalarField input");
    Require(graph.FindSocket(meanId, EditorNodeGraph::kValueOutputSocketId, &outputSocket) &&
            outputSocket.direction == EditorNodeGraph::SocketDirection::Output &&
            outputSocket.type == EditorNodeGraph::SocketType::Scalar &&
            outputSocket.logicalType == Stack::NodeMath::LogicalValueType::Scalar &&
            outputSocket.semanticRoleKey == "mean",
        "Field Mean should declare one uniform Scalar output");

    std::string error;
    Require(!graph.TryConnectSockets(
            imageId, EditorNodeGraph::kImageOutputSocketId,
            meanId, EditorNodeGraph::kReductionFieldInputSocketId, &error) &&
            error.find("explicit") != std::string::npos,
        "Field Mean must reject a full image and request an explicit channel or luminance extractor");
    error.clear();
    Require(graph.TryConnectSockets(
                imageId, EditorNodeGraph::kImageOutputSocketId,
                splitId, EditorNodeGraph::kImageInputSocketId, &error) &&
            graph.TryConnectSockets(
                splitId, "r",
                meanId, EditorNodeGraph::kReductionFieldInputSocketId, &error) &&
            graph.TryConnectSockets(
                meanId, EditorNodeGraph::kValueOutputSocketId,
                exposureId, EditorNodeGraph::kExposureValueInputSocketId, &error) &&
            graph.TryConnectSockets(
                imageId, EditorNodeGraph::kImageOutputSocketId,
                exposureId, EditorNodeGraph::kImageInputSocketId, &error) &&
            graph.TryConnectSockets(
                exposureId, EditorNodeGraph::kImageOutputSocketId,
                outputId, EditorNodeGraph::kImageInputSocketId, &error),
        error.empty()
            ? "the authored Field Mean to Exposure graph should connect"
            : error.c_str());
    Require(graph.IsOutputConnected() && graph.Validate().valid,
        "the authored Field Mean side dependency should leave a valid connected output");
    const std::vector<EditorNodeGraph::CompletedChainInfo>&
        fieldMeanChains = graph.GetCompletedChains();
    Require(
        !fieldMeanChains.empty() &&
            std::find(
                fieldMeanChains.front().nodeIds.begin(),
                fieldMeanChains.front().nodeIds.end(),
                meanId) != fieldMeanChains.front().nodeIds.end(),
        "completed-chain scheduling must retain a connected Field Mean "
        "side dependency");
    Require(graph.TryConnectSockets(
                splitId, "g",
                scalarAverageId, EditorNodeGraph::kMixInputASocketId, &error) &&
            graph.TryConnectSockets(
                scalarAverageId, EditorNodeGraph::kImageOutputSocketId,
                scalarReformatId, EditorNodeGraph::kImageInputSocketId, &error) &&
            graph.TryConnectSockets(
                scalarReformatId, EditorNodeGraph::kImageOutputSocketId,
                meanId, EditorNodeGraph::kReductionFieldInputSocketId, &error),
        "an Image-typed scalar stream should pass through explicit Reformat into a ScalarField input");
    Require(graph.IsScalarSocketStream(
                scalarReformatId, EditorNodeGraph::kImageOutputSocketId),
        "Reformat should preserve scalar lineage for masks, channels, and scalar fields");
    const EditorNodeGraph::Link* averagedFieldInput = graph.FindInputLink(
        meanId, EditorNodeGraph::kReductionFieldInputSocketId);
    Require(averagedFieldInput &&
            averagedFieldInput->fromNodeId == scalarReformatId &&
            graph.IsRenderLink(*averagedFieldInput),
        "accepted scalar-image-to-ScalarField links must participate in render scheduling");
    Require(graph.Validate().valid,
        "the accepted scalar Average -> Field Mean -> Exposure graph should validate");

    const nlohmann::json saved = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    EditorNodeGraph::Graph loaded;
    EditorNodeGraph::DeserializeGraphPayload(saved, loaded, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* loadedMean = loaded.FindNode(meanId);
    Require(loadedMean && loadedMean->kind == EditorNodeGraph::NodeKind::FieldMean,
        "Field Mean node kind should round-trip");
    Require(loadedMean && loadedMean->definitionResolved,
        "Field Mean exact definition identity should resolve after reload");
    Require(loaded.IsOutputConnected(),
        "Field Mean side dependency should preserve a connected output after reload");
    Require(loaded.HasLink(
            meanId, EditorNodeGraph::kValueOutputSocketId,
            exposureId, EditorNodeGraph::kExposureValueInputSocketId),
        "Field Mean scalar connection should round-trip");

    RenderGraphSnapshot renderGraph;
    renderGraph.outputNodeId = outputId;
    auto renderNode = [](int nodeId, std::string definitionId, RenderGraphNodeKind kind) {
        RenderGraphNode node;
        node.nodeId = nodeId;
        node.definitionId = std::move(definitionId);
        node.kind = kind;
        return node;
    };
    renderGraph.nodes = {
        renderNode(imageId, "stack:graph/image-generator-solid-color", RenderGraphNodeKind::Image),
        renderNode(splitId, "stack:graph/channel-split", RenderGraphNodeKind::ChannelSplit),
        renderNode(meanId, "stack:analysis/field-mean", RenderGraphNodeKind::FieldMean),
        renderNode(exposureId, "stack:graph/technical-image-exposure", RenderGraphNodeKind::TechnicalImage),
        renderNode(outputId, "stack:graph/output", RenderGraphNodeKind::Output)
    };
    renderGraph.links = {
        { imageId, EditorNodeGraph::kImageOutputSocketId, splitId, EditorNodeGraph::kImageInputSocketId },
        { splitId, "r", meanId, EditorNodeGraph::kReductionFieldInputSocketId },
        { meanId, EditorNodeGraph::kValueOutputSocketId, exposureId, EditorNodeGraph::kExposureValueInputSocketId },
        { imageId, EditorNodeGraph::kImageOutputSocketId, exposureId, EditorNodeGraph::kImageInputSocketId },
        { exposureId, EditorNodeGraph::kImageOutputSocketId, outputId, EditorNodeGraph::kImageInputSocketId }
    };
    const RenderGraphRegionPlan plan = RenderTiling::PlanGraphRegions(renderGraph, 64, 32);
    Require(plan.valid && plan.requiresFullFrame && !plan.tileable &&
            std::any_of(plan.stages.begin(), plan.stages.end(), [](const RenderGraphRegionStage& stage) {
                return stage.capability == Stack::NodeMath::CapabilityClass::Reduction &&
                    stage.definitionId == "stack:analysis/field-mean";
            }),
        "the region planner should classify Field Mean as an explicit full-frame Reduction stage");
}

void TestPhase6GeometryAndSpecializedGraphContract() {
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(
        EditorNodeGraph::NodeKind::Reformat, 0);
    Require(definition && definition->identity.id == "stack:geometry/reformat" &&
            definition->identity.version == Stack::NodeMath::SemanticVersion{ 1, 0, 0 },
        "Reformat should resolve its exact public geometry definition");

    EditorNodeGraph::Graph graph;
    const int imageId = graph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor, { 0.0f, 0.0f })->id;
    EditorNodeGraph::Node* reformat = graph.AddReformatNode({ 260.0f, 0.0f });
    const int reformatId = reformat ? reformat->id : -1;
    Require(reformat && reformat->definitionResolved &&
            reformat->definitionId == "stack:geometry/reformat",
        "a new Reformat node should pin its exact installed definition");
    reformat->reformatSettings.width = 13;
    reformat->reformatSettings.height = 9;
    reformat->reformatSettings.filter = Stack::NodeMath::ReconstructionFilter::Nearest;
    const int outputId = graph.AddOutputNode({ 520.0f, 0.0f }, true)->id;

    EditorNodeGraph::SocketDefinition inputSocket;
    EditorNodeGraph::SocketDefinition outputSocket;
    Require(graph.FindSocket(reformatId, EditorNodeGraph::kImageInputSocketId, &inputSocket) &&
            inputSocket.direction == EditorNodeGraph::SocketDirection::Input &&
            inputSocket.type == EditorNodeGraph::SocketType::Image &&
            inputSocket.logicalType == Stack::NodeMath::LogicalValueType::ColorImage &&
            inputSocket.semanticRoleKey == "image",
        "Reformat should declare a required typed image input");
    Require(graph.FindSocket(reformatId, EditorNodeGraph::kImageOutputSocketId, &outputSocket) &&
            outputSocket.direction == EditorNodeGraph::SocketDirection::Output &&
            outputSocket.type == EditorNodeGraph::SocketType::Image &&
            outputSocket.semanticRoleKey == "image",
        "Reformat should declare a typed image output");

    std::string error;
    Require(graph.TryConnectSockets(
                imageId, EditorNodeGraph::kImageOutputSocketId,
                reformatId, EditorNodeGraph::kImageInputSocketId, &error) &&
            graph.TryConnectSockets(
                reformatId, EditorNodeGraph::kImageOutputSocketId,
                outputId, EditorNodeGraph::kImageInputSocketId, &error),
        error.empty() ? "the authored Reformat graph should connect" : error.c_str());
    Require(graph.IsOutputConnected() && graph.Validate().valid,
        "Reformat should participate in completed output traversal before rendering");

    const nlohmann::json saved = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    EditorNodeGraph::Graph loaded;
    EditorNodeGraph::DeserializeGraphPayload(saved, loaded, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* loadedReformat = loaded.FindNode(reformatId);
    Require(loadedReformat && loadedReformat->kind == EditorNodeGraph::NodeKind::Reformat &&
            loadedReformat->definitionResolved &&
            loadedReformat->reformatSettings.width == 13 &&
            loadedReformat->reformatSettings.height == 9 &&
            loadedReformat->reformatSettings.filter ==
                Stack::NodeMath::ReconstructionFilter::Nearest &&
            loaded.IsOutputConnected(),
        "Reformat definition, settings, and completed traversal should survive save/load");

    auto makeRenderNode = [](int nodeId, RenderGraphNodeKind kind, const char* definitionId) {
        RenderGraphNode node;
        node.nodeId = nodeId;
        node.kind = kind;
        node.definitionId = definitionId;
        return node;
    };
    RenderGraphSnapshot renderGraph;
    renderGraph.outputNodeId = outputId;
    RenderGraphNode image = makeRenderNode(imageId, RenderGraphNodeKind::Image, "stack:graph/image-generator-solid-color");
    image.image.width = 40;
    image.image.height = 20;
    RenderGraphNode geometry = makeRenderNode(reformatId, RenderGraphNodeKind::Reformat, "stack:geometry/reformat");
    geometry.reformatSettings = loadedReformat->reformatSettings;
    renderGraph.nodes = {
        image,
        geometry,
        makeRenderNode(outputId, RenderGraphNodeKind::Output, "stack:graph/output")
    };
    renderGraph.links = {
        { imageId, EditorNodeGraph::kImageOutputSocketId, reformatId, EditorNodeGraph::kImageInputSocketId },
        { reformatId, EditorNodeGraph::kImageOutputSocketId, outputId, EditorNodeGraph::kImageInputSocketId }
    };
    const RenderGraphRegionPlan plan = RenderTiling::PlanGraphRegions(renderGraph, 40, 20);
    Require(plan.valid && plan.requiresFullFrame && !plan.tileable &&
            plan.outputSpatial.fullWindow.width == 13 &&
            plan.outputSpatial.fullWindow.height == 9 &&
            std::any_of(plan.stages.begin(), plan.stages.end(), [](const auto& stage) {
                return stage.capability == Stack::NodeMath::CapabilityClass::SampleResample &&
                    stage.definitionId == "stack:geometry/reformat" &&
                    stage.border == Stack::NodeMath::BorderPolicy::Clamp;
            }),
        "Reformat should propagate its extent through a typed full-frame Sample/Resample stage");

    RenderGraphSnapshot mismatched;
    mismatched.outputNodeId = 104;
    RenderGraphNode left = makeRenderNode(101, RenderGraphNodeKind::Image, "test:image-left");
    left.image.width = 40;
    left.image.height = 20;
    RenderGraphNode right = makeRenderNode(102, RenderGraphNodeKind::Image, "test:image-right");
    right.image.width = 20;
    right.image.height = 10;
    mismatched.nodes = {
        left,
        right,
        makeRenderNode(103, RenderGraphNodeKind::Mix, "stack:graph/blend-images"),
        makeRenderNode(104, RenderGraphNodeKind::Output, "stack:graph/output")
    };
    mismatched.links = {
        { 101, EditorNodeGraph::kImageOutputSocketId, 103, EditorNodeGraph::kMixInputASocketId },
        { 102, EditorNodeGraph::kImageOutputSocketId, 103, EditorNodeGraph::kMixInputBSocketId },
        { 103, EditorNodeGraph::kImageOutputSocketId, 104, EditorNodeGraph::kImageInputSocketId }
    };
    const RenderGraphRegionPlan mismatchPlan =
        RenderTiling::PlanGraphRegions(mismatched, 40, 20);
    Require(!mismatchPlan.valid && mismatchPlan.reason.find("explicit Reformat") != std::string::npos,
        "mismatched image extents should fail planning with explicit Reformat guidance");

    RenderGraphSnapshot rawGraph;
    rawGraph.outputNodeId = 203;
    rawGraph.nodes = {
        makeRenderNode(201, RenderGraphNodeKind::RawSource, "stack:graph/raw-source"),
        makeRenderNode(202, RenderGraphNodeKind::RawDevelop, "stack:graph/raw-develop"),
        makeRenderNode(203, RenderGraphNodeKind::Output, "stack:graph/output")
    };
    rawGraph.links = {
        { 201, EditorNodeGraph::kRawOutputSocketId, 202, EditorNodeGraph::kRawInputSocketId },
        { 202, EditorNodeGraph::kImageOutputSocketId, 203, EditorNodeGraph::kImageInputSocketId }
    };
    const RenderGraphRegionPlan rawPlan = RenderTiling::PlanGraphRegions(rawGraph, 64, 32);
    Require(rawPlan.valid && rawPlan.requiresFullFrame &&
            std::any_of(rawPlan.stages.begin(), rawPlan.stages.end(), [](const auto& stage) {
                return stage.specializedKind == Stack::NodeMath::SpecializedStageKind::RawDecode;
            }) &&
            std::any_of(rawPlan.stages.begin(), rawPlan.stages.end(), [](const auto& stage) {
                return stage.specializedKind == Stack::NodeMath::SpecializedStageKind::RawDevelopment;
            }),
        "RAW decode and development should remain distinct typed opaque full-frame stages");
}

void TestPhase5ExecutableCompoundLifecycle() {
    using Stack::NodeMath::CompoundDefinition;
    using Stack::NodeMath::CompoundDefinitionClass;
    using Stack::NodeMath::CompoundResolutionStatus;

    const auto& templates = EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    Require(templates.size() >= 2,
        "Phase 5 should ship transparent and optimized-equivalent reference compounds");
    Require(templates[0].definitionClass == CompoundDefinitionClass::TransparentGraph &&
            templates[1].definitionClass == CompoundDefinitionClass::GraphDefinedOptimizedEquivalent,
        "shipped Phase 5 references should cover both graph-defined compound classes");
    Require(Stack::NodeMath::ValidateCompoundCatalog(templates).empty(),
        "shipped compound definitions should form a valid nonrecursive exact catalog");

    {
        EditorNodeGraph::Graph authoredChain;
        std::string authoredError;
        Require(authoredChain.AddCompoundDefinition(templates[0], &authoredError) &&
                authoredChain.AddCompoundDefinition(templates[1], &authoredError),
            "the real authored-chain fixture should embed both shipped compounds");
        const int authoredSourceId = authoredChain.AddImageGeneratorNode(
            EditorNodeGraph::ImageGeneratorKind::SolidColor, { 0.0f, 0.0f })->id;
        const int addMultiplyId = authoredChain.AddCompoundNode(
            templates[0].identity, { 280.0f, 0.0f })->id;
        const int exposurePremultiplyId = authoredChain.AddCompoundNode(
            templates[1].identity, { 560.0f, 0.0f })->id;
        const int authoredOutputId = authoredChain.AddOutputNode(
            { 840.0f, 0.0f }, true)->id;
        Require(authoredChain.TryConnectSockets(
                    authoredSourceId, EditorNodeGraph::kImageOutputSocketId,
                    addMultiplyId, "image-in", &authoredError) &&
                authoredChain.TryConnectSockets(
                    addMultiplyId, "image-out",
                    exposurePremultiplyId, "image-in", &authoredError) &&
                authoredChain.TryConnectSockets(
                    exposurePremultiplyId, "image-out",
                    authoredOutputId, EditorNodeGraph::kImageInputSocketId, &authoredError),
            "Image -> Add, Then Multiply -> Exposure, Then Premultiply -> Output should author normally");
        Require(authoredChain.IsOutputConnected(),
            "the exact shipped-compound chain should report a connected output before Unpack");
        Require(authoredChain.ResolveReferenceSourceNodeIdForOutput(authoredOutputId) == authoredSourceId,
            "compound-aware reference-canvas traversal should find the authored image source");

        const nlohmann::json authoredSaved = EditorNodeGraph::SerializeGraphPayload(
            nlohmann::json::array(), authoredChain);
        EditorNodeGraph::Graph authoredReloaded;
        EditorNodeGraph::DeserializeGraphPayload(
            authoredSaved, authoredReloaded, 0, {}, 0, 0, 0);
        Require(authoredReloaded.IsOutputConnected() &&
                authoredReloaded.ResolveReferenceSourceNodeIdForOutput(authoredOutputId) == authoredSourceId,
            "the authored shipped-compound chain should remain executable after save/load");

        EditorNodeGraph::Graph authoredExpanded;
        EditorNodeGraph::CompoundExpansionResult authoredExpansion;
        Require(authoredReloaded.ExpandAllCompoundNodes(
                    authoredExpanded, &authoredExpansion) &&
                authoredExpansion.success && authoredExpanded.IsOutputConnected(),
            "the authored chain and its canonical unpacked execution graph should both be connected");
    }

    for (EditorNodeGraph::NodeKind kind : {
            EditorNodeGraph::NodeKind::RawSource,
            EditorNodeGraph::NodeKind::RawDevelopment,
            EditorNodeGraph::NodeKind::RawDecode,
            EditorNodeGraph::NodeKind::RawDevelop,
            EditorNodeGraph::NodeKind::RawDetailAutoMask,
            EditorNodeGraph::NodeKind::RawDetailFusion }) {
        const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(kind, 0);
        Require(definition && definition->inspectability ==
                Stack::NodeMath::Inspectability::OpaqueSpecialized,
            "RAW live definitions should remain opaque specialized operations");
    }

    EditorNodeGraph::Graph graph;
    std::string error;

    {
        CompoundDefinition duplicateInputBinding = templates[0];
        const auto publicInput = std::find_if(
            duplicateInputBinding.ports.begin(),
            duplicateInputBinding.ports.end(),
            [](const auto& port) {
                return port.direction == Stack::NodeMath::PortDirection::Input;
            });
        Require(publicInput != duplicateInputBinding.ports.end(),
            "compound contract fixture should expose a public input");
        auto aliasInput = *publicInput;
        aliasInput.id += "-alias";
        duplicateInputBinding.ports.push_back(std::move(aliasInput));
        Stack::NodeMath::RefreshCompoundDefinitionContentHash(duplicateInputBinding);
        EditorNodeGraph::Graph contractGraph;
        error.clear();
        Require(!contractGraph.AddCompoundDefinition(duplicateInputBinding, &error) &&
                error.find("same canonical input socket") != std::string::npos,
            "compound definitions should reject ambiguous duplicate public-input bindings");

        CompoundDefinition connectedInputBinding = templates[0];
        EditorNodeGraph::Graph canonical;
        EditorNodeGraph::DeserializeGraphPayload(
            connectedInputBinding.canonicalGraph, canonical, 0, {}, 0, 0, 0);
        Require(!canonical.GetLinks().empty(),
            "compound contract fixture should contain an internal connection");
        const EditorNodeGraph::Link& internalLink = canonical.GetLinks().front();
        const EditorNodeGraph::Node* internalTarget =
            canonical.FindNode(internalLink.toNodeId);
        Require(internalTarget != nullptr,
            "compound contract fixture internal connection should have a target");
        auto connectedPublicInput = std::find_if(
            connectedInputBinding.ports.begin(),
            connectedInputBinding.ports.end(),
            [](const auto& port) {
                return port.direction == Stack::NodeMath::PortDirection::Input;
            });
        connectedPublicInput->internalInstanceUuid = internalTarget->instanceUuid;
        connectedPublicInput->internalSocketId = internalLink.toSocketId;
        Stack::NodeMath::RefreshCompoundDefinitionContentHash(connectedInputBinding);
        error.clear();
        Require(!contractGraph.AddCompoundDefinition(connectedInputBinding, &error) &&
                error.find("unconnected canonical input socket") != std::string::npos,
            "compound definitions should not expose an input that silently replaces canonical wiring");

        CompoundDefinition duplicateParameterBinding = templates[0];
        Require(!duplicateParameterBinding.parameters.empty(),
            "compound contract fixture should expose a promoted parameter");
        auto aliasParameter = duplicateParameterBinding.parameters.front();
        aliasParameter.id += "-alias";
        duplicateParameterBinding.parameters.push_back(std::move(aliasParameter));
        Stack::NodeMath::RefreshCompoundDefinitionContentHash(duplicateParameterBinding);
        error.clear();
        Require(!contractGraph.AddCompoundDefinition(duplicateParameterBinding, &error) &&
                error.find("same canonical parameter") != std::string::npos,
            "compound definitions should reject ambiguous duplicate promoted-parameter bindings");

        CompoundDefinition duplicateCanonicalUuid = templates[0];
        EditorNodeGraph::Graph duplicateUuidCanonical;
        EditorNodeGraph::DeserializeGraphPayload(
            duplicateCanonicalUuid.canonicalGraph,
            duplicateUuidCanonical,
            0,
            {},
            0,
            0,
            0);
        Require(duplicateUuidCanonical.GetNodes().size() >= 2,
            "compound contract fixture should contain multiple internal nodes");
        auto& duplicateUuidNodes = duplicateUuidCanonical.EditNodes();
        duplicateUuidNodes[1].instanceUuid = duplicateUuidNodes[0].instanceUuid;
        duplicateCanonicalUuid.canonicalGraph =
            EditorNodeGraph::SerializeGraphPayload(
                nlohmann::json::array(),
                duplicateUuidCanonical);
        Stack::NodeMath::RefreshCompoundDefinitionContentHash(
            duplicateCanonicalUuid);
        error.clear();
        Require(!contractGraph.AddCompoundDefinition(duplicateCanonicalUuid, &error) &&
                error.find("Duplicate or invalid node instance UUID") !=
                    std::string::npos,
            "compound definitions should reject ambiguous canonical node identities");

        EditorNodeGraph::Graph authoringRollbackGraph;
        const int rollbackAuthoringSource =
            authoringRollbackGraph.AddImageGeneratorNode(
                EditorNodeGraph::ImageGeneratorKind::SolidColor,
                { 0.0f, 0.0f })->id;
        const int rollbackAuthoringMath =
            authoringRollbackGraph.AddDataMathNode(
                EditorNodeGraph::DataMathMode::Add,
                { 300.0f, 0.0f })->id;
        const int rollbackAuthoringTarget =
            authoringRollbackGraph.AddOutputNode(
                { 600.0f, 0.0f },
                true)->id;
        Require(authoringRollbackGraph.TryConnectSockets(
                    rollbackAuthoringSource,
                    EditorNodeGraph::kImageOutputSocketId,
                    rollbackAuthoringMath,
                    EditorNodeGraph::DataMathInputSocketId(0),
                    &error) &&
                authoringRollbackGraph.TryConnectSockets(
                    rollbackAuthoringMath,
                    EditorNodeGraph::kImageOutputSocketId,
                    rollbackAuthoringTarget,
                    EditorNodeGraph::kImageInputSocketId,
                    &error),
            "compound authoring rollback fixture should start connected");
        auto& rollbackAuthoringNodes =
            authoringRollbackGraph.EditNodes();
        const auto invalidAuthoringTarget = std::find_if(
            rollbackAuthoringNodes.begin(),
            rollbackAuthoringNodes.end(),
            [rollbackAuthoringTarget](const auto& node) {
                return node.id == rollbackAuthoringTarget;
            });
        Require(invalidAuthoringTarget != rollbackAuthoringNodes.end(),
            "compound authoring rollback fixture should retain its target");
        invalidAuthoringTarget->kind =
            EditorNodeGraph::NodeKind::Value;
        const std::size_t authoringNodeCountBefore =
            authoringRollbackGraph.GetNodes().size();
        const std::size_t authoringLinkCountBefore =
            authoringRollbackGraph.GetLinks().size();
        const std::size_t authoringDefinitionCountBefore =
            authoringRollbackGraph.GetCompoundDefinitions().size();
        const int authoringNextIdBefore =
            authoringRollbackGraph.GetNextNodeId();
        int failedCompoundId = -77;
        error.clear();
        Require(!authoringRollbackGraph.CreateCompoundFromSelection(
                    { rollbackAuthoringMath },
                    "Must Roll Back",
                    &failedCompoundId,
                    &error) &&
                authoringRollbackGraph.GetNodes().size() ==
                    authoringNodeCountBefore &&
                authoringRollbackGraph.GetLinks().size() ==
                    authoringLinkCountBefore &&
                authoringRollbackGraph.GetCompoundDefinitions().size() ==
                    authoringDefinitionCountBefore &&
                authoringRollbackGraph.GetNextNodeId() ==
                    authoringNextIdBefore &&
                authoringRollbackGraph.FindNode(
                    rollbackAuthoringMath) != nullptr &&
                failedCompoundId == -77,
            "failed compound boundary validation should preserve authored topology, catalog, IDs, and caller output");
    }

    Require(graph.AddCompoundDefinition(templates[0], &error),
        "graph should embed the exact shipped transparent definition");
    const int sourceId = graph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor, { 0.0f, 0.0f })->id;
    const int compoundId = graph.AddCompoundNode(templates[0].identity, { 300.0f, 0.0f })->id;
    const int outputId = graph.AddOutputNode({ 600.0f, 0.0f }, true)->id;
    const bool inputConnected = graph.TryConnectSockets(
        sourceId, EditorNodeGraph::kImageOutputSocketId,
        compoundId, "image-in", &error);
    Require(inputConnected,
        error.empty() ? "compound input port should connect like an ordinary graph port" : error.c_str());
    error.clear();
    const bool outputConnected = graph.TryConnectSockets(
        compoundId, "image-out",
        outputId, EditorNodeGraph::kImageInputSocketId, &error);
    Require(outputConnected,
        error.empty() ? "compound output port should connect like an ordinary graph port" : error.c_str());
    EditorNodeGraph::Node* compoundNode = graph.FindNode(compoundId);
    Require(compoundNode && compoundNode->definitionResolved &&
            compoundNode->compound.instance.resolution == CompoundResolutionStatus::Exact,
        "new compound instance should resolve its exact embedded definition");
    Stack::NodeMath::SetCompoundParameterOverride(
        compoundNode->compound.instance, "add", 0.25);
    Stack::NodeMath::SetCompoundParameterOverride(
        compoundNode->compound.instance, "multiply", 2.0);

    const nlohmann::json saved = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    Require(saved["nodeGraph"]["compoundDefinitions"].size() == 1,
        "project save should embed the exact compound definition");
    EditorNodeGraph::Graph loaded;
    EditorNodeGraph::DeserializeGraphPayload(saved, loaded, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* loadedCompound = loaded.FindNode(compoundId);
    Require(loadedCompound && loadedCompound->definitionResolved &&
            loadedCompound->compound.instance.interfaceSnapshot.size() == 2 &&
            Stack::NodeMath::ResolveCompoundParameterValue(
                loadedCompound->compound.instance, templates[0].parameters[0]) ==
                Stack::NodeMath::ParameterValue(0.25),
        "save/load should preserve exact identity, interface snapshot, and promoted overrides");

    EditorNodeGraph::Graph copied = loaded;
    const EditorNodeGraph::Node* copiedCompound = copied.FindNode(compoundId);
    Require(copiedCompound && copiedCompound->definitionResolved &&
            copied.FindCompoundDefinition(copiedCompound->compound.instance.definition),
        "copying a graph payload should carry the exact embedded compound definition");

    EditorNodeGraph::Graph expanded;
    EditorNodeGraph::CompoundExpansionResult expansion;
    Require(loaded.ExpandAllCompoundNodes(expanded, &expansion) && expansion.success,
        "transparent compound should lower to its canonical graph for execution");
    Require(std::none_of(expanded.GetNodes().begin(), expanded.GetNodes().end(), [](const auto& node) {
            return node.kind == EditorNodeGraph::NodeKind::Compound;
        }),
        "execution graph should contain no graph-defined compound shell");
    const auto add = std::find_if(expanded.GetNodes().begin(), expanded.GetNodes().end(), [](const auto& node) {
        return node.kind == EditorNodeGraph::NodeKind::DataMath &&
            node.dataMathMode == EditorNodeGraph::DataMathMode::Add;
    });
    const auto multiply = std::find_if(expanded.GetNodes().begin(), expanded.GetNodes().end(), [](const auto& node) {
        return node.kind == EditorNodeGraph::NodeKind::DataMath &&
            node.dataMathMode == EditorNodeGraph::DataMathMode::Multiply;
    });
    Require(add != expanded.GetNodes().end() && multiply != expanded.GetNodes().end() &&
            std::abs(add->dataMathSettings.constantB - 0.25f) < 1.0e-6f &&
            std::abs(multiply->dataMathSettings.constantB - 2.0f) < 1.0e-6f &&
            expanded.HasLink(add->id, multiply->id),
        "canonical expansion should preserve promoted values and authored Add-then-Multiply order");

    EditorNodeGraph::NodeGroup* group = graph.AddGroup(
        "Organization Only", { 240.0f, -80.0f }, { 360.0f, 220.0f });
    Require(group != nullptr, "compound fixture should create an organizational group");
    const int groupId = group->id;
    int nestedId = -1;
    Require(graph.CreateCompoundFromSelection(
            { compoundId }, "Nested Wrapper", &nestedId, &error),
        "a graph-defined compound should be nestable in another compound");
    Require(graph.IsOutputConnected(),
        "a custom nested compound should render through its authored public output before Unpack");
    Require(graph.FindGroup(groupId) && graph.FindGroup(groupId)->title == "Organization Only",
        "compound creation should leave organizational groups unchanged");
    const EditorNodeGraph::Node* nested = graph.FindNode(nestedId);
    const CompoundDefinition* nestedDefinition = nested
        ? graph.FindCompoundDefinition(nested->compound.instance.definition) : nullptr;
    Require(nested && nestedDefinition && nestedDefinition->dependencies.size() == 1 &&
            Stack::NodeMath::SameDefinitionReference(
                nestedDefinition->dependencies.front(), templates[0].identity),
        "nested definition should pin and embed its exact transitive dependency");
    EditorNodeGraph::Graph nestedExpanded;
    Require(graph.ExpandAllCompoundNodes(nestedExpanded, &expansion) && expansion.success &&
            std::none_of(nestedExpanded.GetNodes().begin(), nestedExpanded.GetNodes().end(), [](const auto& node) {
                return node.kind == EditorNodeGraph::NodeKind::Compound;
            }),
        "nonrecursive nested compounds should expand completely");

    const nlohmann::json nestedSaved = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    EditorNodeGraph::Graph nestedLoaded;
    EditorNodeGraph::DeserializeGraphPayload(nestedSaved, nestedLoaded, 0, {}, 0, 0, 0);
    Require(nestedLoaded.FindNode(nestedId) && nestedLoaded.FindNode(nestedId)->definitionResolved,
        "nested compound should survive project save/load with its closure");
    Require(nestedLoaded.IsOutputConnected(),
        "a saved and reloaded nested compound should remain executable before Unpack");

    const std::string originalNestedId =
        nestedLoaded.FindNode(nestedId)->compound.instance.definition.id;
    Require(nestedLoaded.MakeCompoundNodeUnique(nestedId, &error),
        "Make Unique should detach a compound instance into a new embedded definition");
    Require(nestedLoaded.IsOutputConnected(),
        "a unique compound copy should remain executable before Unpack");
    Require(nestedLoaded.FindNode(nestedId)->compound.instance.definition.id != originalNestedId,
        "Make Unique should assign a new stable definition family ID");

    const CompoundDefinition* uniqueDefinition = nestedLoaded.FindCompoundDefinition(
        nestedLoaded.FindNode(nestedId)->compound.instance.definition);
    Require(uniqueDefinition != nullptr, "unique definition should remain embedded exactly");
    CompoundDefinition incompatible = *uniqueDefinition;
    incompatible.identity.version = { 1, 0, 1 };
    incompatible.ports.front().logicalType = Stack::NodeMath::LogicalValueType::Scalar;
    Stack::NodeMath::RefreshCompoundDefinitionContentHash(incompatible);
    Require(!nestedLoaded.AddCompoundDefinition(incompatible, &error) &&
            nestedLoaded.FindNode(nestedId)->compound.instance.definition.version ==
                Stack::NodeMath::SemanticVersion{ 1, 0, 0 },
        "a definition whose stable port type disagrees with its canonical binding should be rejected");
    CompoundDefinition updated;
    Require(Stack::NodeMath::CreateEditedCompoundDefinitionVersion(
            *uniqueDefinition, { 1, 1, 0 }, uniqueDefinition->canonicalGraph, updated, &error) &&
            nestedLoaded.AddCompoundDefinition(updated, &error),
        "definition editing should create a deliberate newer embedded version");
    Require(nestedLoaded.FindNode(nestedId)->compound.instance.definition.version ==
            Stack::NodeMath::SemanticVersion{ 1, 0, 0 },
        "adding a newer definition should not automatically move an instance");
    Require(nestedLoaded.UpdateCompoundNodeDefinition(nestedId, updated.identity, &error) &&
            nestedLoaded.FindNode(nestedId)->compound.instance.definition.version ==
                Stack::NodeMath::SemanticVersion{ 1, 1, 0 },
        "explicit update should move only the chosen instance to the chosen exact version");
    Require(nestedLoaded.IsOutputConnected(),
        "a deliberately updated compound definition should remain executable before Unpack");

    {
        EditorNodeGraph::Graph rollbackGraph;
        Require(rollbackGraph.AddCompoundDefinition(templates[0], &error),
            "Unpack rollback fixture should embed its compound definition");
        const int rollbackSource = rollbackGraph.AddImageGeneratorNode(
            EditorNodeGraph::ImageGeneratorKind::SolidColor,
            { 0.0f, 0.0f })->id;
        const int rollbackCompound = rollbackGraph.AddCompoundNode(
            templates[0].identity,
            { 300.0f, 0.0f })->id;
        const int rollbackTarget = rollbackGraph.AddOutputNode(
            { 600.0f, 0.0f },
            true)->id;
        Require(rollbackGraph.TryConnectSockets(
                    rollbackSource,
                    EditorNodeGraph::kImageOutputSocketId,
                    rollbackCompound,
                    "image-in",
                    &error) &&
                rollbackGraph.TryConnectSockets(
                    rollbackCompound,
                    "image-out",
                    rollbackTarget,
                    EditorNodeGraph::kImageInputSocketId,
                    &error),
            "Unpack rollback fixture should start with valid external wiring");
        auto& rollbackNodes = rollbackGraph.EditNodes();
        const auto rollbackTargetNode = std::find_if(
            rollbackNodes.begin(),
            rollbackNodes.end(),
            [rollbackTarget](const auto& node) {
                return node.id == rollbackTarget;
            });
        Require(rollbackTargetNode != rollbackNodes.end(),
            "Unpack rollback fixture should retain its target node");
        rollbackTargetNode->kind = EditorNodeGraph::NodeKind::Value;
        const std::size_t nodeCountBeforeFailedUnpack =
            rollbackGraph.GetNodes().size();
        const std::size_t linkCountBeforeFailedUnpack =
            rollbackGraph.GetLinks().size();
        const int nextNodeIdBeforeFailedUnpack =
            rollbackGraph.GetNextNodeId();
        std::vector<int> failedUnpackIds{ -77 };
        error.clear();
        Require(!rollbackGraph.UnpackCompoundNode(
                    rollbackCompound,
                    &failedUnpackIds,
                    &error) &&
                rollbackGraph.GetNodes().size() ==
                    nodeCountBeforeFailedUnpack &&
                rollbackGraph.GetLinks().size() ==
                    linkCountBeforeFailedUnpack &&
                rollbackGraph.GetNextNodeId() ==
                    nextNodeIdBeforeFailedUnpack &&
                rollbackGraph.FindNode(rollbackCompound) != nullptr &&
                failedUnpackIds == std::vector<int>{ -77 },
            "failed Unpack validation should roll back transient nodes, links, IDs, and caller output");
    }

    std::vector<int> unpackedIds;
    Require(nestedLoaded.UnpackCompoundNode(nestedId, &unpackedIds, &error) &&
            !unpackedIds.empty() && nestedLoaded.FindGroup(groupId),
        "Unpack should restore the canonical internal graph without changing groups");
    Require(nestedLoaded.IsOutputConnected(),
        "unpacking one compound level should preserve the executable output chain");
    Require(std::any_of(unpackedIds.begin(), unpackedIds.end(), [&](int id) {
            const EditorNodeGraph::Node* node = nestedLoaded.FindNode(id);
            return node && node->kind == EditorNodeGraph::NodeKind::Compound;
        }),
        "unpacking one nested level should expose its authored child compound");

    nlohmann::json missingDocument = nestedSaved;
    missingDocument["nodeGraph"]["compoundDefinitions"] = nlohmann::json::array();
    EditorNodeGraph::Graph missingGraph;
    EditorNodeGraph::DeserializeGraphPayload(missingDocument, missingGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* missingNode = missingGraph.FindNode(nestedId);
    Require(missingNode && !missingNode->definitionResolved &&
            missingNode->compound.instance.interfaceSnapshot.size() ==
                nested->compound.instance.interfaceSnapshot.size() &&
            missingGraph.HasLink(sourceId, nestedId) && missingGraph.HasLink(nestedId, outputId),
        "missing definitions should preserve the unresolved node shell, ports, and connections");
    EditorNodeGraph::Graph failedExpansion;
    Require(!missingGraph.ExpandAllCompoundNodes(failedExpansion, &expansion) &&
            !expansion.error.empty(),
        "missing compound dependency should block execution rather than substitute behavior");
    Require(!missingGraph.IsOutputConnected() &&
            !missingGraph.GetOutputConnectionDiagnostic().empty(),
        "an unresolved compound should report why its authored output cannot execute");

    nlohmann::json invalidInstanceDocument = saved;
    for (nlohmann::json& item : invalidInstanceDocument["nodeGraph"]["nodes"]) {
        if (item.value("id", 0) == compoundId) {
            item["compoundInstance"]["instanceUuid"] = "not-a-uuid";
        }
    }
    EditorNodeGraph::Graph invalidInstanceGraph;
    EditorNodeGraph::DeserializeGraphPayload(
        invalidInstanceDocument, invalidInstanceGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* invalidInstance = invalidInstanceGraph.FindNode(compoundId);
    Require(invalidInstance && !invalidInstance->definitionResolved &&
            invalidInstance->compound.instance.resolution == CompoundResolutionStatus::InvalidDefinition,
        "malformed compound instance should remain unresolved even when its referenced definition exists");

    EditorNodeGraph::Graph optimizedGraph;
    Require(optimizedGraph.AddCompoundDefinition(templates[1], &error),
        "optimized-equivalent reference definition should embed exactly");
    const int optimizedId = optimizedGraph.AddCompoundNode(
        templates[1].identity, { 0.0f, 0.0f })->id;
    Stack::NodeMath::SetCompoundParameterOverride(
        optimizedGraph.FindNode(optimizedId)->compound.instance, "exposure-ev", 1.0);
    EditorNodeGraph::Graph optimizedExpanded;
    Require(optimizedGraph.ExpandAllCompoundNodes(optimizedExpanded, &expansion),
        "optimized-equivalent compound should always retain an executable canonical expansion");
    const auto exposure = std::find_if(
        optimizedExpanded.GetNodes().begin(), optimizedExpanded.GetNodes().end(), [](const auto& node) {
            return node.kind == EditorNodeGraph::NodeKind::TechnicalImage &&
                node.technicalImageSettings.operation == Stack::NodeMath::TechnicalImageOperation::Exposure;
        });
    const auto premultiply = std::find_if(
        optimizedExpanded.GetNodes().begin(), optimizedExpanded.GetNodes().end(), [](const auto& node) {
            return node.kind == EditorNodeGraph::NodeKind::TechnicalImage &&
                node.technicalImageSettings.operation == Stack::NodeMath::TechnicalImageOperation::Premultiply;
        });
    Require(exposure != optimizedExpanded.GetNodes().end() &&
            premultiply != optimizedExpanded.GetNodes().end() &&
            std::abs(exposure->technicalImageSettings.exposureValue - 1.0f) < 1.0e-6f &&
            optimizedExpanded.HasLink(exposure->id, premultiply->id),
        "optimized-equivalent canonical path should preserve Exposure-then-Premultiply order and parameters");

    EditorNodeGraph::Graph typedGraph;
    const int typedSourceId = typedGraph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor, { 0.0f, 0.0f })->id;
    const int typedValueId = typedGraph.AddValueNode(
        Stack::NodeMath::MakeUniformScalar(1.25), { 0.0f, 160.0f })->id;
    const int typedExposureId = typedGraph.AddTechnicalImageNode(
        Stack::NodeMath::TechnicalImageOperation::Exposure, { 300.0f, 0.0f })->id;
    const int typedOutputId = typedGraph.AddOutputNode({ 600.0f, 0.0f }, true)->id;
    Require(typedGraph.TryConnectSockets(
            typedSourceId, EditorNodeGraph::kImageOutputSocketId,
            typedExposureId, EditorNodeGraph::kImageInputSocketId, &error) &&
            typedGraph.TryConnectSockets(
                typedValueId, EditorNodeGraph::kValueOutputSocketId,
                typedExposureId, EditorNodeGraph::kExposureValueInputSocketId, &error) &&
            typedGraph.TryConnectSockets(
                typedExposureId, EditorNodeGraph::kImageOutputSocketId,
                typedOutputId, EditorNodeGraph::kImageInputSocketId, &error),
        "typed compound fixture should connect before authoring");
    int typedCompoundId = -1;
    Require(typedGraph.CreateCompoundFromSelection(
            { typedExposureId, typedExposureId }, "Typed Exposure", &typedCompoundId, &error),
        "compound authoring should de-duplicate repeated selection IDs while preserving typed boundary ports");
    const EditorNodeGraph::Node* typedCompound = typedGraph.FindNode(typedCompoundId);
    const CompoundDefinition* typedDefinition = typedCompound
        ? typedGraph.FindCompoundDefinition(typedCompound->compound.instance.definition) : nullptr;
    Require(typedDefinition && std::any_of(
            typedDefinition->ports.begin(), typedDefinition->ports.end(), [](const auto& port) {
                return port.direction == Stack::NodeMath::PortDirection::Input &&
                    port.logicalType == Stack::NodeMath::LogicalValueType::Scalar;
            }),
        "authored compound should expose its uniform Scalar boundary with an exact type");
    EditorNodeGraph::Graph typedExpanded;
    Require(typedGraph.ExpandAllCompoundNodes(typedExpanded, &expansion),
        "typed compound should expand its exact canonical graph");
    const auto expandedExposure = std::find_if(
        typedExpanded.GetNodes().begin(), typedExpanded.GetNodes().end(), [](const auto& node) {
            return node.kind == EditorNodeGraph::NodeKind::TechnicalImage &&
                node.technicalImageSettings.operation == Stack::NodeMath::TechnicalImageOperation::Exposure;
        });
    double resolvedTypedExposure = 0.0;
    Require(expandedExposure != typedExpanded.GetNodes().end() &&
            typedExpanded.TryResolveUniformScalarInput(
                expandedExposure->id,
                EditorNodeGraph::kExposureValueInputSocketId,
                resolvedTypedExposure,
                &error) &&
            std::abs(resolvedTypedExposure - 1.25) < 1.0e-12,
        "typed compound expansion should restore the external Scalar connection exactly");

    EditorNodeGraph::Graph pixelGraph;
    Require(pixelGraph.AddCompoundDefinition(templates[0], &error),
        "pixel-sharing fixture should embed its exact compound definition");
    EditorNodeGraph::ImagePayload imagePayload;
    imagePayload.label = "Expansion Pixels";
    imagePayload.width = 2;
    imagePayload.height = 1;
    imagePayload.channels = 4;
    imagePayload.pixels = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const int pixelSourceId = pixelGraph.AddImageNode(std::move(imagePayload), { 0.0f, 0.0f })->id;
    const int pixelCompoundId = pixelGraph.AddCompoundNode(
        templates[0].identity, { 300.0f, 0.0f })->id;
    Require(pixelGraph.TryConnectSockets(
            pixelSourceId, EditorNodeGraph::kImageOutputSocketId,
            pixelCompoundId, "image-in", &error),
        "pixel-sharing fixture should connect to the compound");
    EditorNodeGraph::Graph pixelExpanded;
    Require(pixelGraph.ExpandAllCompoundNodes(pixelExpanded, &expansion),
        "image-backed compound graph should expand");
    const EditorNodeGraph::Node* originalPixels = pixelGraph.FindNode(pixelSourceId);
    const EditorNodeGraph::Node* expansionPixels = pixelExpanded.FindNode(pixelSourceId);
    Require(originalPixels && expansionPixels && originalPixels->image.pixels.size() == 8 &&
            expansionPixels->image.pixels.empty() && expansionPixels->image.sharedPixels &&
            expansionPixels->image.sharedPixels->size() == 8,
        "execution expansion should share source pixels instead of duplicating the full image buffer");
}

void TestProjectSaveCoordinatorRevisionQueue() {
    using Stack::Project::ProjectSaveCoordinator;
    using Stack::Project::ProjectSaveReason;
    using Stack::Project::ProjectSaveResult;
    using Stack::Project::ProjectSaveStatus;

    Require(!Stack::Project::IsProjectAutosaveDue(
            true, true, false, 9.9, 0.0, -1.0),
        "autosave should not interrupt the ten-second editing quiet window");
    Require(Stack::Project::IsProjectAutosaveDue(
            true, true, false, 10.0, 0.0, -1.0),
        "a first autosave should become due after a genuine editing pause");
    Require(!Stack::Project::IsProjectAutosaveDue(
            true, true, false, 75.0, 70.0, 20.0),
        "periodic autosave should respect both the quiet window and cadence");
    Require(!Stack::Project::IsProjectAutosaveDue(
            true, true, false, 79.0, 65.0, 20.0),
        "periodic autosave should not run more than once per minute");
    Require(Stack::Project::IsProjectAutosaveDue(
            true, true, false, 80.0, 65.0, 20.0),
        "periodic autosave should run after both idle and cadence gates pass");

    ProjectSaveCoordinator coordinator;
    std::deque<ProjectSaveCoordinator::Completion> activeWrites;
    const auto starter = [&](ProjectSaveCoordinator::Completion completion) {
        activeWrites.push_back(std::move(completion));
    };

    ProjectSaveCoordinator::Request first;
    first.projectId = "project-a";
    first.editRevision = 1;
    first.reason = ProjectSaveReason::Autosave;
    first.start = starter;
    Require(coordinator.Enqueue(std::move(first)) && activeWrites.size() == 1,
        "first save revision should start immediately");

    ProjectSaveCoordinator::Request newer;
    newer.projectId = "project-a";
    newer.editRevision = 2;
    newer.reason = ProjectSaveReason::Autosave;
    newer.start = starter;
    Require(coordinator.Enqueue(std::move(newer)) && coordinator.HasPendingSave(),
        "an edit during a save should queue the newer revision");

    bool explicitCompleted = false;
    ProjectSaveCoordinator::Request explicitFlush;
    explicitFlush.projectId = "project-a";
    explicitFlush.editRevision = 2;
    explicitFlush.reason = ProjectSaveReason::Explicit;
    explicitFlush.start = starter;
    explicitFlush.completion = [&](ProjectSaveResult result) {
        explicitCompleted = static_cast<bool>(result) &&
            result.persistedEditRevision >= 2;
    };
    Require(coordinator.Enqueue(std::move(explicitFlush)),
        "Ctrl+S should join an active autosave queue");

    ProjectSaveResult revisionOne;
    revisionOne.status = ProjectSaveStatus::Saved;
    revisionOne.projectId = "project-a";
    revisionOne.persistedEditRevision = 1;
    auto completeRevisionOne = std::move(activeWrites.front());
    activeWrites.pop_front();
    completeRevisionOne(revisionOne);
    Require(!explicitCompleted && activeWrites.size() == 1,
        "Ctrl+S must wait until its requested revision is durable");

    ProjectSaveResult revisionTwo;
    revisionTwo.status = ProjectSaveStatus::Saved;
    revisionTwo.projectId = "project-a";
    revisionTwo.persistedEditRevision = 2;
    auto completeRevisionTwo = std::move(activeWrites.front());
    activeWrites.pop_front();
    completeRevisionTwo(revisionTwo);
    Require(explicitCompleted && !coordinator.IsBusy(),
        "the explicit waiter should finish after the newer commit");

    bool cleanSaveCompleted = false;
    ProjectSaveCoordinator::Request clean;
    clean.projectId = "project-a";
    clean.editRevision = 2;
    clean.reason = ProjectSaveReason::Explicit;
    clean.start = starter;
    clean.completion = [&](ProjectSaveResult result) {
        cleanSaveCompleted = static_cast<bool>(result);
    };
    Require(coordinator.Enqueue(std::move(clean)) && !cleanSaveCompleted &&
            activeWrites.size() == 1,
        "a clean Ctrl+S should verify the current save target");
    ProjectSaveResult cleanResult;
    cleanResult.status = ProjectSaveStatus::Saved;
    cleanResult.projectId = "project-a";
    cleanResult.persistedEditRevision = 2;
    auto completeCleanSave = std::move(activeWrites.front());
    activeWrites.pop_front();
    completeCleanSave(cleanResult);
    Require(cleanSaveCompleted && activeWrites.empty(),
        "a clean Ctrl+S should complete after verifying the save target");

    coordinator.Reset("project-b", 0);
    bool failedDelivered = false;
    ProjectSaveCoordinator::Request failing;
    failing.projectId = "project-b";
    failing.editRevision = 1;
    failing.reason = ProjectSaveReason::Explicit;
    failing.start = starter;
    failing.completion = [&](ProjectSaveResult result) {
        failedDelivered = result.status == ProjectSaveStatus::Failed;
    };
    Require(coordinator.Enqueue(std::move(failing)),
        "a retryable save should start normally");
    ProjectSaveResult failure;
    failure.status = ProjectSaveStatus::Failed;
    failure.projectId = "project-b";
    auto completeFailure = std::move(activeWrites.front());
    activeWrites.pop_front();
    completeFailure(failure);
    Require(failedDelivered && coordinator.PersistedEditRevision() == 0,
        "a failed save must not advance the persisted revision fence");

    coordinator.Reset("project-old", 0);
    ProjectSaveCoordinator::Request oldProject;
    oldProject.projectId = "project-old";
    oldProject.editRevision = 1;
    oldProject.reason = ProjectSaveReason::Autosave;
    oldProject.start = starter;
    Require(coordinator.Enqueue(std::move(oldProject)) && activeWrites.size() == 1,
        "old project save should start before the project switch");
    ProjectSaveCoordinator::Completion staleCompletion =
        std::move(activeWrites.front());
    activeWrites.pop_front();

    coordinator.Reset("project-new", 0);
    bool newProjectCompleted = false;
    ProjectSaveCoordinator::Request newProject;
    newProject.projectId = "project-new";
    newProject.editRevision = 1;
    newProject.reason = ProjectSaveReason::Explicit;
    newProject.start = starter;
    newProject.completion = [&](ProjectSaveResult result) {
        newProjectCompleted = result.status == ProjectSaveStatus::Saved;
    };
    Require(coordinator.Enqueue(std::move(newProject)) && activeWrites.size() == 1,
        "new project save should start after the switch");

    ProjectSaveResult staleResult;
    staleResult.status = ProjectSaveStatus::Saved;
    staleResult.projectId = "project-old";
    staleResult.persistedEditRevision = 1;
    staleCompletion(staleResult);
    Require(coordinator.IsBusy() &&
            coordinator.ProjectId() == "project-new" &&
            coordinator.PersistedEditRevision() == 0 &&
            !newProjectCompleted,
        "a stale old-project completion must not complete the new project save");

    ProjectSaveCoordinator::Completion newCompletion =
        std::move(activeWrites.front());
    activeWrites.pop_front();
    ProjectSaveResult newResult;
    newResult.status = ProjectSaveStatus::Saved;
    newResult.projectId = "project-new";
    newResult.persistedEditRevision = 1;
    newCompletion(newResult);
    Require(newProjectCompleted && !coordinator.IsBusy(),
        "the current project completion should remain authoritative");

    coordinator.Reset("project-throw", 0);
    bool thrownStarterFailed = false;
    ProjectSaveCoordinator::Request throwing;
    throwing.projectId = "project-throw";
    throwing.editRevision = 1;
    throwing.reason = ProjectSaveReason::Explicit;
    throwing.start = [](ProjectSaveCoordinator::Completion) {
        throw std::runtime_error("starter failure");
    };
    throwing.completion = [&](ProjectSaveResult result) {
        thrownStarterFailed =
            result.status == ProjectSaveStatus::Failed;
    };
    Require(coordinator.Enqueue(std::move(throwing)) &&
            thrownStarterFailed && !coordinator.IsBusy(),
        "an exception before background save startup must release the save coordinator");
}

void TestRenderQueueModelSelectionAndDeduplication() {
    using Stack::Queue::ItemState;
    Stack::Queue::RenderQueueModel queue;
    const std::filesystem::path first =
        std::filesystem::path("queue-tests") / "first.dng";
    const std::filesystem::path second =
        std::filesystem::path("queue-tests") / "second.dng";
    Require(queue.AddSources({ first, second, first }) == 2u,
        "queue should add each source path once");
    std::vector<Stack::Queue::Item> items = queue.Snapshot();
    Require(items.size() == 2u && items[0].selected && items[1].selected,
        "new queue items should be selected by default");

    queue.SetSelected(items[0].id, false);
    Require(queue.HasSelectedWaitingItems(),
        "another selected waiting item should keep export enabled");
    queue.SetAllSelected(false);
    Require(!queue.HasSelectedWaitingItems(),
        "select-none should disable export eligibility");
    queue.SetAllSelected(true);
    queue.UpdateState(
        items[0].id, ItemState::Complete, 1.0f, "Complete");
    queue.ClearCompleted();
    items = queue.Snapshot();
    Require(items.size() == 1u && items[0].path.filename() == "second.dng",
        "clear-completed should retain unfinished queue work");
    queue.UpdateState(
        items[0].id, ItemState::Failed, 0.35f, "Injected failure");
    items = queue.Snapshot();
    Require(items[0].state == ItemState::Failed &&
            std::abs(items[0].progress - 0.35f) < 0.0001f &&
            queue.HasSelectedWaitingItems(),
        "failed queue work should retain progress and remain retryable");

    const std::filesystem::path thumbnailRoot =
        MakeTempDirectory("stack_queue_source_thumbnail");
    const std::filesystem::path thumbnailPath =
        thumbnailRoot / "first-thumbnail.png";
    WriteRawWorkspaceTestFile(thumbnailPath);
    Stack::RawGalleryQueue::SourceRequest sourceRequest;
    sourceRequest.sourcePath = thumbnailRoot / "third.dng";
    sourceRequest.thumbnailPath = thumbnailPath;
    Require(queue.AddSourceRequests({ sourceRequest }) == 1u,
        "queue should accept a Gallery source request with its thumbnail");
    items = queue.Snapshot();
    const auto thumbnailItem = std::find_if(
        items.begin(), items.end(), [&](const Stack::Queue::Item& item) {
            return item.path.filename() == "third.dng";
        });
    Require(thumbnailItem != items.end() &&
            !thumbnailItem->thumbnailBytes.empty(),
        "an unedited queued source should retain its Gallery thumbnail bytes");
    std::error_code cleanupError;
    std::filesystem::remove_all(thumbnailRoot, cleanupError);
}

void TestRawGalleryTrashAndRevertActions() {
    namespace FileActions = Stack::RawGalleryFileActions;
    namespace Project = Stack::Project;
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root =
        MakeTempDirectory("stack_raw_gallery_file_actions");
    const std::filesystem::path source = root / "Dance" / "frame.DNG";
    const std::filesystem::path outside =
        MakeTempDirectory("stack_raw_gallery_file_actions_outside") /
        "outside.DNG";
    WriteRawWorkspaceTestFile(source);
    WriteRawWorkspaceTestFile(outside);

    const FileActions::Result rejected =
        FileActions::DeleteProjectsToTrash(root, { source, outside });
    Require(rejected.moved == 0u && rejected.errors.size() == 2u &&
            std::filesystem::exists(source) && std::filesystem::exists(outside),
        "Project deletion must reject original source files");

    const std::filesystem::path original = root / "Dance" / "original.DNG";
    WriteRawWorkspaceTestFile(original);
    Project::RawProjectSnapshot snapshot;
    snapshot.projectId = "gallery-revert-project";
    snapshot.projectName = "Gallery Revert Project";
    const std::filesystem::path projectsRoot = RawWorkspace::BuildManagedLayout(root).projectsDirectory;
    const std::filesystem::path projectRoot = projectsRoot /
        "Gallery Revert Project--revert01";
    const Project::ProjectStoreOpenResult created =
        Project::CreateProjectStore(
            projectRoot,
            Project::ProjectStorageKind::DirectoryBundle,
            snapshot);
    Require(static_cast<bool>(created),
        "Gallery Revert fixture should create a complete managed project bundle");

    const FileActions::Result reverted =
        FileActions::RevertProjectsToTrash({ projectRoot }, root);
    Require(reverted.moved == 1u && reverted.errors.empty() &&
            !std::filesystem::exists(projectRoot) &&
            reverted.destinations.size() == 1u &&
            Project::IsDirectoryProjectBundle(reverted.destinations.front()),
        "Gallery Revert should move the complete project bundle into recoverable project Trash");
    Require(std::filesystem::exists(original),
        "Gallery Revert should retain the original RAW source");

    const std::filesystem::path deleteOriginal =
        root / "Dance" / "delete-original.DNG";
    WriteRawWorkspaceTestFile(deleteOriginal);
    snapshot.projectId = "gallery-delete-project";
    snapshot.projectName = "Gallery Delete Project";
    const std::filesystem::path deleteProjectRoot = projectsRoot /
        "Gallery Delete Project--delete01";
    const Project::ProjectStoreOpenResult deleteCreated =
        Project::CreateProjectStore(
            deleteProjectRoot,
            Project::ProjectStorageKind::DirectoryBundle,
            snapshot);
    Require(static_cast<bool>(deleteCreated),
        "Gallery Delete fixture should create a complete managed project bundle");

    const FileActions::Result projectDeleted =
        FileActions::DeleteProjectsToTrash(root, { deleteProjectRoot });
    Require(projectDeleted.moved == 1u && projectDeleted.errors.empty() &&
            !std::filesystem::exists(deleteProjectRoot) &&
            std::filesystem::exists(deleteOriginal) && std::filesystem::exists(outside),
        "Project deletion must leave original RAW files untouched");
    Require(projectDeleted.destinations.size() == 1u &&
            Project::IsDirectoryProjectBundle(projectDeleted.destinations.front()),
        "Project deletion must retain the complete bundle in project Trash");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::remove_all(outside.parent_path(), cleanupError);
}

void TestProjectAutosaveQuietWindow() {
    using Stack::Project::IsProjectAutosaveDue;
    using Stack::Project::ShouldDeferProjectAutosaveForForegroundInput;
    Require(!IsProjectAutosaveDue(true, true, false, 100.0, 100.0, -1.0),
        "the first edit after a long idle period should not autosave immediately");
    Require(!IsProjectAutosaveDue(true, true, false, 109.999, 100.0, -1.0),
        "autosave should wait for the complete ten-second quiet window");
    Require(IsProjectAutosaveDue(true, true, false, 110.0, 100.0, -1.0),
        "autosave should become due after ten seconds without another edit");
    Require(!IsProjectAutosaveDue(true, true, true, 120.0, 100.0, -1.0),
        "continued editing should not chain another autosave while a write is active");
    Require(!IsProjectAutosaveDue(true, true, false, 110.0, 100.0, 102.0),
        "one edit revision should enqueue at most one autosave");
    Require(!IsProjectAutosaveDue(true, true, false, 110.5, 110.0, 102.0),
        "a newer edit should restart the quiet window even after an older save");
    Require(
        ShouldDeferProjectAutosaveForForegroundInput(
            false, false, true, 0.0f, 0.0f),
        "the first pointer-down frame after an idle period must defer autosave");
    Require(
        ShouldDeferProjectAutosaveForForegroundInput(
            false, false, false, 1.0f, 0.0f),
        "wheel-based adjustments must defer autosave before an item becomes active");
    Require(
        !ShouldDeferProjectAutosaveForForegroundInput(
            false, false, false, 0.0f, 0.0f),
        "an input-idle frame should not extend the document quiet window");

    double continuousEditTime = 200.0;
    for (int edit = 0; edit < 60; ++edit) {
        continuousEditTime += 0.05;
        Require(
            !IsProjectAutosaveDue(
                true,
                true,
                false,
                continuousEditTime,
                continuousEditTime,
                -1.0),
            "continuous adjustments must not enqueue a save per edit");
    }
    Require(
        !IsProjectAutosaveDue(
            true, true, true,
            continuousEditTime + 10.0,
            continuousEditTime,
            -1.0),
        "autosave should wait while the interaction or live render is busy");
    Require(
        IsProjectAutosaveDue(
            true, true, false,
            continuousEditTime + 10.0,
            continuousEditTime,
            -1.0),
        "one autosave should become due after editing and rendering are idle");

    Stack::Project::ProjectSessionController session;
    const Stack::Project::ProjectReplacementToken replacement =
        session.BeginReplacement();
    Require(
        session.CompleteReplacement(replacement, "edit-during-save", 4, 9),
        "the edit-during-save fixture should activate a project");
    session.NoteEdit();
    const Stack::Project::ProjectSaveToken save = session.BeginSave();
    Require(static_cast<bool>(save),
        "a dirty project should begin its asynchronous save");
    const std::uint64_t newerRevision = session.NoteEdit();
    Require(
        newerRevision == save.snapshotDirtyRevision + 1,
        "editing during a save should advance beyond the writer snapshot");
    Require(
        session.CompleteSave(save, true, 10) && session.IsDirty() &&
            session.Phase() ==
                Stack::Project::ProjectLifecyclePhase::ReadyDirty,
        "an older save completion must preserve the newer editable revision");
}

void TestManagedImageSerializationReusesImmutableAsset() {
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::ImagePayload image = TestImagePayload();
    image.managedAssetId = "sha256-test-managed-image-8";
    image.projectAssetPath = "assets/images/source.png";
    EditorNodeGraph::Node* node =
        graph.AddImageNode(std::move(image), { 0.0f, 0.0f });
    Require(node != nullptr, "managed-image fixture should create an image node");

    const auto findSerializedNode = [&](const nlohmann::json& pipeline)
        -> const nlohmann::json* {
        if (!pipeline.contains("nodeGraph") ||
            !pipeline["nodeGraph"].contains("nodes")) {
            return nullptr;
        }
        for (const nlohmann::json& value : pipeline["nodeGraph"]["nodes"]) {
            if (value.value("id", 0) == node->id) return &value;
        }
        return nullptr;
    };

    nlohmann::json pipeline = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    const nlohmann::json* savedNode = findSerializedNode(pipeline);
    Require(savedNode &&
            savedNode->value("managedAssetId", std::string()) ==
                "sha256-test-managed-image-8" &&
            savedNode->value("projectAssetPath", std::string()) ==
                "assets/images/source.png" &&
            !savedNode->contains("pngBytes"),
        "ordinary serialization should reuse an immutable managed image without copying its PNG payload");

    node->image.managedAssetId.clear();
    node->image.projectAssetPath.clear();
    pipeline = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    savedNode = findSerializedNode(pipeline);
    Require(savedNode && savedNode->contains("pngBytes"),
        "a pixel-changing edit that clears the managed identity should publish new image bytes");
}

} // namespace

namespace LayerRegistry {

std::shared_ptr<LayerBase> CreateLayer(LayerType type) {
    (void)type;
    return nullptr;
}

std::shared_ptr<LayerBase> CreateLayerFromTypeId(const std::string& typeId) {
    (void)typeId;
    return nullptr;
}

const LayerDescriptor* GetDescriptor(LayerType type) {
    for (const LayerDescriptor& descriptor : TestLayerDescriptors()) {
        if (descriptor.type == type) {
            return &descriptor;
        }
    }
    return nullptr;
}

const LayerDescriptor* FindDescriptorByTypeId(const std::string& typeId) {
    for (const LayerDescriptor& descriptor : TestLayerDescriptors()) {
        if (typeId == descriptor.typeId) {
            return &descriptor;
        }
    }
    return nullptr;
}

const std::vector<LayerDescriptor>& GetAllDescriptors() {
    return TestLayerDescriptors();
}

std::map<std::string, std::vector<const LayerDescriptor*>> GetDescriptorsByCategory() {
    std::map<std::string, std::vector<const LayerDescriptor*>> byCategory;
    for (const LayerDescriptor& descriptor : TestLayerDescriptors()) {
        byCategory[descriptor.categoryName].push_back(&descriptor);
    }
    return byCategory;
}

std::string GetDisplayNameFromTypeId(const std::string& typeId) {
    const LayerDescriptor* descriptor = FindDescriptorByTypeId(typeId);
    return descriptor ? descriptor->displayName : std::string();
}

std::string GetLibraryDisplayNameFromTypeId(const std::string& typeId) {
    const LayerDescriptor* descriptor = FindDescriptorByTypeId(typeId);
    return descriptor ? descriptor->libraryDisplayName : std::string();
}

const char* LifecycleStatusLabel(LayerLifecycleStatus status) {
    switch (status) {
        case LayerLifecycleStatus::Stable: return "Stable";
        case LayerLifecycleStatus::NeedsFix: return "Needs Fix";
        case LayerLifecycleStatus::Experimental: return "Experimental";
        case LayerLifecycleStatus::Deprecated: return "Deprecated";
        case LayerLifecycleStatus::Hidden: return "Hidden";
    }
    return "Unknown";
}

const char* ChannelPolicyLabel(LayerChannelPolicy policy) {
    switch (policy) {
        case LayerChannelPolicy::ChannelSafe: return "Channel Safe";
        case LayerChannelPolicy::ChannelUsefulWithWarning: return "Channel Useful With Warning";
        case LayerChannelPolicy::FullImagePreferred: return "Full Image Preferred";
        case LayerChannelPolicy::FullImageOnly: return "Full Image Only";
        case LayerChannelPolicy::ReworkBeforeExpose: return "Rework Before Expose";
    }
    return "Unknown";
}

bool ShouldShowInNodeBrowser(const LayerDescriptor& descriptor) {
    return descriptor.visibleInNodeBrowser &&
        descriptor.lifecycleStatus != LayerLifecycleStatus::Hidden &&
        descriptor.lifecycleStatus != LayerLifecycleStatus::Deprecated;
}

bool ValidateRegistry(std::vector<std::string>* errors) {
    if (errors) {
        errors->clear();
    }
    return true;
}

} // namespace LayerRegistry

void RunGraphScaleTests();
void RunNodeLayoutTests();

void TestGraphRenderPolicyAndNavigation();

void RunGraphOutputSemanticsTests();

void TestProjectGalleryDiscovery();

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--raw-layers-only") {
        try {
            TestRawLayerStackContracts();
        } catch (const std::exception& error) {
            std::cerr << "RAW layer checks failed: " << error.what() << '\n';
            return 1;
        }
        return 0;
    }
    if (argc>1 && std::string(argv[1])=="--raw-viewport-only") {
        TestRawViewportTimingLearning();
        TestGraphRenderPolicyAndNavigation();
        TestRawWorkspaceAppStateUsesCurrentSchema();
        std::cout << "RAW viewport focused checks passed.\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--project-gallery") {
        try {
            TestProjectGalleryDiscovery();
            TestRawWorkspaceProjectLifecycleModel();
        } catch (const std::exception& error) {
            std::cerr << "Project Gallery validation failed: " << error.what() << '\n';
            return 1;
        }
        std::cout << "Saved RAW project lifecycle discovery passed.\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--project-ownership") {
        try {
            TestProjectStoreOwnership();
        } catch (const std::exception& error) {
            std::cerr << "Project store ownership validation failed: " << error.what() << '\n';
            return 1;
        }
        TestProjectSaveCoordinatorRevisionQueue();
        TestProjectAutosaveQuietWindow();
        TestRawGalleryTrashAndRevertActions();
        TestRawWorkspaceProjectRootIsolation();
        std::cout << "Project ownership and source preservation checks passed.\n";
        return 0;
    }
    RunGraphOutputSemanticsTests();
    TestGraphRenderPolicyAndNavigation();
    TestVariablePrecisionValueGraph();
    RunGraphScaleTests();
    RunNodeLayoutTests();
    TestViewportPreviewPresentationMatrix();
    TestEditorRenderWorkerGenerationScheduling();
    TestPixelBufferLayoutGuards();
    TestPhase3TypedValuesAndUnifiedDefinitions();
    TestConstantChannelFoundation();
    TestPhase6BFieldMeanGraphContract();
    TestPhase6GeometryAndSpecializedGraphContract();
    TestPhase5ExecutableCompoundLifecycle();
    TestProjectSaveCoordinatorRevisionQueue();
    TestRenderQueueModelSelectionAndDeduplication();
    TestRawGalleryTrashAndRevertActions();
    TestProjectAutosaveQuietWindow();
    TestManagedImageSerializationReusesImmutableAsset();
    TestGraphSliderDragSensitivityIsZoomIndependent();
    TestGraphCaptureLinkedResolution();
    TestGraphCaptureResolutionValidation();
    TestGraphCaptureBoundsFittingAndPadding();
    TestGraphCaptureNodeStatePresetDoesNotMutateSource();
    TestGraphCaptureReadbackAndEncoding();
    TestImagePayloadPreviewIsBounded();
    TestInteractiveTaskPriorityRunsBeforeBlockedBackgroundWork();
    TestTaskSystemShutdownJoinsRunningWorkAndDiscardsCompletions();
    TestScalarMaskCanUseLayerMath();
    TestChannelAverageStaysScalarThroughContrastMaskWorkflow();
    TestChannelRoleConnectsDirectlyToMaskInputs();
    TestToneCurveInheritsInputScenePath();
    TestFullImageStillCannotTargetScalarInput();
    TestOutputChannelNormalization();
    TestOutputV2Persistence();
    TestPartialImageComponentPresenceSurvivesGraphRoundTrip();
    TestCompletedChainsSplitSharedUpstreamAcrossOutputs();
    TestSplitAdjustmentAnimatableRegistryCoverage();
    TestBlurFamilyAnimatableRegistryCoverage();
    TestNonRawNumericLayerAnimatableRegistryCoverage();
    TestEffectsGenerateStylizeLayerAnimatableRegistryCoverage();
    TestBackgroundPatcherAnimatableRegistryCoverage();
    TestTimelineKeyframesTrackSharedUpstreamChains();
    TestTimelineFrameEvaluationSamplesLinearAndHold();
    TestTimelineFrameEvaluationSamplesDiscreteValuesAsHold();
    TestTimelineFrameEvaluationCanRemoveLiveEditPreviewTarget();
    TestFrameEvaluationAppliesLayerJsonOverridesWithoutMutatingLiveJson();
    TestTimelinePlaybackAdvancesFramesAndStopsOrLoops();
    TestTimelineStepFrameWrapsWithinRange();
    TestTimelineAnimationReportsLastDistinctKeyframeFrames();
    TestTimelineAnimationUpdatesOnlyExistingKeyframes();
    TestTimelineFrameProducerNormalizesRequestsAndBuildsContext();
    TestTimelinePersistenceRoundTripsDocument();
    TestTimelinePersistenceRepairsInvalidTargetsAndFrames();
    TestScalarCyclesAreRejected();
    TestCustomMaskConnections();
    TestCustomMaskThroughMaskCombineExclude();
    TestCustomMaskSparseRasterPersistence();
    TestManualRawBaselineChainShape();
    ::TestClosetWorkspaceStorage();
    TestRawWorkspaceFolderCatalogFoundation();
    TestRawWorkspaceProjectRootIsolation();
    TestRawWorkspaceThumbnailPipelineFoundation();
    TestRawWorkspaceLoadingCancellationModel();
    TestRawLabGalleryVisibleRange();
    TestRawWorkspaceThumbnailScheduler();
    TestRawWorkspaceAppStateUsesCurrentSchema();
    TestRawWorkspaceGalleryPresentation();
    TestRawWorkspacePanelStateModel();
    TestRawWorkspaceProjectLifecycleModel();
    TestRawLabToolSequencePreservesHiddenFieldsThroughProjectReload();
    TestRawWorkspaceProjectReloadPreservesOwnershipModes();
    TestRawDevelopmentRecipeDefaultsAndRoundTrip();
    TestRawGpuMemoryBudgetPolicy();
    TestRawRenderPlanningContract();
    TestRawRenderGraphOverlayContract();
    TestRawNeutralComparisonRecipeIsTransientAndGeometryStable();
    TestRawFinishTonePointCurveSetContract();
    TestRawColorWarpContract();
    TestRawColorWarpAreaAndMaskContract();
    TestRawEditAttributeBundleContract();
    TestRawProjectEditPipelineContract();
    TestMultiFrameBurstResultCacheContract();
    TestRawProjectAttributeTransferTransaction();
    TestRawAttributePasteCreatesIndependentProjects();
    TestRawDevelopmentStageCachePolicy();
    TestRestormerRgbAdapterContract();
    TestRestormerProtocolAndTilingContract();
    TestRestormerDevelopmentPackageTrustContract();
    TestRawLabViewTransformReferenceMatchesShaderFormula();
    TestRawImageAnalysisPercentilesAndFallbackGuards();
    TestRawAutoBaseViewTransformFit();
    TestRawAutoBaseRecommendations();
    TestRawAutoStartPointDiagnosticsViews();
    TestRawAutoStartPointConservativePlanner();
    TestRawWorkspaceStartingPointEditorStateModel();
    TestRawAutoBaseNoiseDetailRecommendations();
    TestRawAutoBaseLocalRangeSuggestions();
    TestRawPreviewProxyPreservesMosaicUntilDemosaic();
    TestCompactRawDevelopmentNodeGraphContract();
    TestRawDevelopNodeSerializesCurrentFields();
    TestManagedRawSectionValidationAndSync();
    TestManagedRawSectionRejectsCustomGraphChanges();
    TestManagedRawSectionRepairsMissingRequiredLinksOnly();
    TestManagedRawSectionMutationWarnings();
    TestManagedRawSectionRejectsFlexibleReorderingInV1();
    TestManagedRawSectionBlocksUnsupportedRecipeAndDecodeFields();
    TestScalarThroughDataMathToPreviewAndScalarTargets();
    TestImageThroughDataMathToOutput();
    TestFrequencyNodeShellSocketsAndConnections();
    TestSemanticNodeMutationsInvalidateExecutionState();
    TestAverageNodeInputRules();
    TestImageAndScalarThroughDataMathStaysImage();
    TestFullImageDataMathRejectedByScalarOnlyInputs();
    TestLegacyMaskCombineRemainsScalar();
    TestLutNodeConnectionsAndScalarPropagation();
    TestViewportTilePlannerCoverage();
    TestViewportTilingModeDecisions();
    TestViewportTileSafeGraphClassification();
    TestRegionAwareTilePlanningAndCancellation();
    TestLutImporterCubeVariants();
    TestLutCreatorRoundTripSidecar();
    TestMfsrValidationRejectsEmptyAndMissingReference();
    TestMfsrValidationRejectsMixedAndUnknownInputs();
    TestMfsrValidationAllowsSingleFamilyWithReference();
    TestMfsrCacheKeyFingerprintsReactToInputsAndSettings();
    TestMfsrNodeShellSocketsAndConnections();
    TestMfsrNodeShellRejectsScalarAndMixedFamilies();
    TestMfsrNodeShellSerializesRoundTrip();
    TestRetiredNeuralDenoiseIsRejected();
    TestTechnicalImageAndSourceMetadataSerializeRoundTrip();
    TestCompositeNodeSerializesRoundTrip();
    TestGraphInfoNoLayoutPayloadPreservesGraphOrderAndState();
    TestHdrMergeDeghostModeMediumRoundTrip();

    std::cout << "Stack graph behavior tests passed.\n";
    TestRawViewportTimingLearning();
    return 0;
}
