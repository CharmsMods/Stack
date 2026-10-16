#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include "Restormer/RestormerClient.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/RawGallerySelectionVisuals.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Stack::Editor::RawLabInternal;
using RawCurveGraphUiState = Stack::EditorModuleTypes::RawCurveGraphUiState;
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"

bool EditorModule::RenderRawWorkspaceLabZonesSurface(RawWorkspaceEditContext& context, RawLabControlSection section) {
    bool changed = false;
    auto& graph = m_RawWorkspaceLabUi.zonesCurveGraph;
    auto& history = m_RawWorkspaceLabUi.exposureHistory;
    const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),context.rawAdjustmentLayerId);
    const auto* exposureNode = layer ? Stack::Project::FindRawOperation(*layer,
        Stack::RawRecipe::GraphOperationKind::LocalEv, context.rawOperationUuid) : nullptr;
    const bool canAdjustExposure = exposureNode && exposureNode->rawOperation.enabled &&
        !context.IsParameterDriven("ev");
    const auto exposureUuid = exposureNode ? exposureNode->instanceUuid : context.rawOperationUuid;
    // The ordinary Local EV surface already owns this node's draft. Legacy
    // entry points without a target can still read their separate instance.
    float exposureEv = exposureNode && exposureUuid != context.rawOperationUuid
        ? Stack::Project::ReadRawLayerOperation(*layer,exposureUuid).preToneExposureEv
        : context.recipe.preToneExposureEv;
    const float exposureBefore = exposureEv;
    const std::string exposureSource = m_Project->storePath.generic_string() +
        "|" + GetActiveRawWorkspacePreviewIdentity() + ":" + context.rawAdjustmentLayerId +
        ":" + context.rawOperationUuid + ":" + exposureUuid;
    if (history.SetSource(exposureSource, exposureEv)) {
        if (graph.draggingExposure) ImGui::ClearActiveID();
        graph = {};
        m_RawWorkspaceLabUi.selectedZonePoint = -1;
    }
    history.Observe(exposureEv, graph.draggingExposure);
    context.recipe.localRange =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(std::move(context.recipe.localRange));
    Stack::RawRecipe::RawLocalRangeRecipe& localRange = context.recipe.localRange;

    if (ShowsRawLabSettings(section) && context.rawAdjustmentLayerId.empty()) {
        ImGui::TextUnformatted(m_RawWorkspaceLabUi.selectedEvGradient >= 0
            ? "Local EV mask" : "Local EV");
        ContinueRawLabControlRow("Entire image");
        if (ImGui::SmallButton("Entire image##EvGradientGlobal")) {
            m_RawWorkspaceLabUi.selectedEvGradient = -1;
            m_RawWorkspaceLabUi.gradientDrawShape = -1;
            m_RawWorkspaceLabUi.zonesCurveGraph = {};
            m_RawWorkspaceLabUi.selectedZonePoint = -1;
            m_RawWorkspaceLabUi.zonesTargetedView = false;
        }
        if (ImGui::SmallButton(m_RawWorkspaceLabUi.gradientOverlayVisible
                ? "Hide overlay##EvGradientOverlay" : "Show overlay##EvGradientOverlay"))
            m_RawWorkspaceLabUi.gradientOverlayVisible =
                !m_RawWorkspaceLabUi.gradientOverlayVisible;
        ContinueRawLabControlRow(m_RawWorkspaceLabUi.gradientGuidesVisible ? "Hide guides" : "Show guides");
        if (ImGui::SmallButton(m_RawWorkspaceLabUi.gradientGuidesVisible
                ? "Hide guides##EvGradientGuides" : "Show guides##EvGradientGuides"))
            m_RawWorkspaceLabUi.gradientGuidesVisible =
                !m_RawWorkspaceLabUi.gradientGuidesVisible;
    }
    if (m_RawWorkspaceLabUi.selectedEvGradient >= 0 &&
        m_RawWorkspaceLabUi.selectedEvGradient < static_cast<int>(context.recipe.evGradients.size())) {
        auto& localCurve = context.recipe.evGradients[
            static_cast<std::size_t>(m_RawWorkspaceLabUi.selectedEvGradient)].curve;
        if (ShowsRawLabGraph(section)) {
            const auto& histogram = m_RawFloatingSurface.histogram.Zones(
                m_RawWorkspaceGraphScopeReadback,
                m_RawWorkspaceGraphScopeReadback.valid ? m_RawWorkspaceGraphScopeReadbackRevision : 0, localCurve.minEv,
                localCurve.maxEv, std::max(0.000001f, localCurve.middleGrey));
            ImVec2 graphSize = ImGui::GetContentRegionAvail();
            graphSize.y = std::max(12.f,std::min(graphSize.y,graphSize.x*.75f));
            graphSize.x = std::max(1.0f, graphSize.x - 16.0f);
            changed |= DrawFramelessLocalRangeBezier(
                "##RawLabGradientEvGraph", localCurve,
                m_RawWorkspaceLabUi.zonesCurveGraph,
                m_RawWorkspaceLabUi.selectedZonePoint,
                m_RawWorkspaceLocalRangeTargetZoneId, histogram,
                false, graphSize, false, nullptr, nullptr, true);
        }
        return changed;
    }

    auto stopTargeting = [&]() {
        if (!m_RawWorkspaceLocalRangeTargetMode) {
            return;
        }
        const std::string selectedZoneId = m_RawWorkspaceLocalRangeTargetZoneId;
        m_RawWorkspaceLocalRangeOverlayMode =
            m_RawWorkspaceLocalRangeTargetPreviousOverlayMode;
        ClearRawWorkspaceLocalRangeOverlayState();
        ClearRawWorkspaceLocalRangeTargetState(false);
        m_RawWorkspaceLocalRangeTargetZoneId = selectedZoneId;
        MarkRenderRefreshDirty();
    };
    if (ShowsRawLabSettings(section)) {
        if (BypassEyeButton(
                "##RawLabEvBypass",
                localRange.enabled,
                "EV adjustments are enabled. Click to bypass them without losing any settings.",
                "EV adjustments are bypassed. Click to apply the preserved settings again.")) {
            localRange.enabled = !localRange.enabled;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("EV adjustments");
        ImGui::Spacing();

        const bool targetedView = m_RawWorkspaceLabUi.zonesTargetedView;
        if (BareToolIslandButton("Overall Tones", !targetedView) && targetedView) {
            stopTargeting();
            m_RawWorkspaceLabUi.zonesTargetedView = false;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (BareToolIslandButton("Areas", targetedView) && !targetedView) {
            m_RawWorkspaceLabUi.zonesTargetedView = true;
            m_RawWorkspaceLabUi.selectedZonePoint = -1;
            if (!localRange.areas.empty()) {
                m_RawWorkspaceAnalysisPending = true;
                m_RawWorkspaceAnalysisRequested = false;
                m_RawWorkspaceAnalysisQuietUntilTime = ImGui::GetTime() + .1;
            }
        }
        ImGui::Spacing();

    }
    if (!m_RawWorkspaceLabUi.zonesTargetedView) {
        if (ShowsRawLabGraph(section)) {
            const float minimumEv = context.recipe.localRange.minEv;
            const float maximumEv = context.recipe.localRange.maxEv;
            const float middleGrey = std::max(0.000001f, context.recipe.localRange.middleGrey);
            const auto& histogram = m_RawFloatingSurface.histogram.Zones(
                m_RawWorkspaceGraphScopeReadback,
                m_RawWorkspaceGraphScopeReadback.valid ? m_RawWorkspaceGraphScopeReadbackRevision : 0,
                minimumEv,
                maximumEv,
                middleGrey);

            constexpr float graphHorizontalInset = 8.0f;
            const float surfaceStartX = ImGui::GetCursorPosX();
            ImVec2 graphSize = ImGui::GetContentRegionAvail();
            graphSize.y = std::max(12.f,std::min(graphSize.y,graphSize.x*.75f));
            graphSize.x = std::max(1.0f, graphSize.x - graphHorizontalInset * 2.0f);
            ImGui::SetCursorPosX(surfaceStartX + graphHorizontalInset);
            bool curveChanged = false;
            if(context.nodeTarget && context.editorState) {
                auto& state=context.State<CurveCanvasState>();
                auto points=Stack::RawRecipe::RawLocalRangeBezierPoints(localRange);
                const auto tonalDomain=ExposureTonalSliderDomain(minimumEv,maximumEv);
                const bool normalizationDriven=context.IsParameterDriven("normalization");
                float normalization=localRange.curveNormalization;
                const auto normalizationFeedback=context.uniformParameters.find("normalization");
                const bool normalizationAvailable=normalizationFeedback!=context.uniformParameters.end() &&
                    normalizationFeedback->second.available && std::isfinite(normalizationFeedback->second.value);
                if(normalizationDriven && normalizationAvailable)
                    normalization=std::clamp(float(normalizationFeedback->second.value),0.f,1.f);
                curveChanged=DrawCurveCanvas("LocalEV",points,localRange.curveAmount,state,false,
                    histogram,graphSize,12,canAdjustExposure?&exposureEv:nullptr,{},false,
                    &normalization,normalizationDriven,normalizationAvailable,
                    &tonalDomain,m_RawWorkspaceGraphScopeReadbackRevision);
                if(!normalizationDriven)localRange.curveNormalization=normalization;
                if(curveChanged && !Stack::RawRecipe::SameCurvePoints(points,
                    Stack::RawRecipe::RawLocalRangeBezierPoints(localRange))) {
                    localRange.points.clear();
                    for(const auto& p:points) localRange.points.push_back({minimumEv+p.x*(maximumEv-minimumEv),-4+8*p.y,p.incoming,p.outgoing,p.tonal});
                    localRange.enabled=true;
                }
                graph.draggingExposure=state.exposureGesture;
            } else {
            DrawFramelessLocalRangeBezier(
                    "##RawLabZonesOverallGraph",
                    context.recipe.localRange,
                    m_RawWorkspaceLabUi.zonesCurveGraph,
                    m_RawWorkspaceLabUi.selectedZonePoint,
                    m_RawWorkspaceLocalRangeTargetZoneId,
                    histogram,
                    false,
                    graphSize,
                    false,
                    canAdjustExposure ? &exposureEv : nullptr,
                    &curveChanged,
                    true);
            }
            changed |= curveChanged;
            if (exposureEv != exposureBefore) {
                context.recipe.preToneExposureEv = exposureEv;
                changed = true;
            }
            m_RawWorkspaceLabUi.globalExposureInteractionActive =
                m_RawWorkspaceLabUi.zonesCurveGraph.draggingExposure;
            history.Observe(exposureEv, graph.draggingExposure);

        }
    } else {
        changed |= RenderRawWorkspaceLabAreas(context, section);
        if (ShowsRawLabSettings(section)) {
            if (!localRange.targetZones.empty()) {
                ImGui::Spacing();
                const bool legacyOpen = ImGui::CollapsingHeader("Existing targets");
                m_RawWorkspaceLabUi.zoneAreas.legacy = legacyOpen;
                if (legacyOpen) changed |= RenderRawWorkspaceLabLegacyZones(context);
                else stopTargeting();
            } else {
                m_RawWorkspaceLabUi.zoneAreas.legacy = false;
            }
    }

    }
    ImGui::Spacing();
    if (ShowsRawLabSettings(section)) {
        constexpr float strengthHorizontalInset = 8.0f;
        const float strengthStartX = ImGui::GetCursorPosX();
        const float strengthWidth = std::max(
            1.0f,
            ImGui::GetContentRegionAvail().x - strengthHorizontalInset * 2.0f);
        ImGui::SetCursorPosX(strengthStartX + strengthHorizontalInset);
        ImGui::TextUnformatted("Strength");
        ImGui::SameLine();
        const ImVec2 strengthLabelSize = ImGui::CalcTextSize("Strength");
        const ImVec2 strengthValueSize = ImGui::CalcTextSize("1.00");
        ImGui::SetCursorPosX(strengthStartX + strengthHorizontalInset +
            std::max(
                strengthLabelSize.x + 12.0f,
                strengthWidth - strengthValueSize.x));
        ImGui::TextDisabled("%.2f", localRange.strength);
        ImGui::SetCursorPosX(strengthStartX + strengthHorizontalInset);
    ImGui::BeginDisabled(context.IsParameterDriven("strength"));
        if (DrawRawLabRingDial(
                "##RawLabZonesStrength",
                localRange.strength,
                strengthWidth,
                "EV strength. Drag horizontally to move the dial rings. Double-click restores 1.00.")) {
            changed = true;
        }
        ImGui::EndDisabled();
    }
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabZonesSecondaryControls(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    ImGui::TextWrapped("Local EV measures this operation's input. Use Exposure for a uniform brightness adjustment.");
    ImGui::Spacing();
    float smoothness = context.recipe.localRange.smoothness;
    if (BareSliderFloat(
            "Smoothness",
            "RawLabZoneSmoothness",
            &smoothness,
            0.0f,
            1.0f,
            "%.2f")) {
        context.recipe.localRange.smoothness = smoothness;
        changed = true;
    }
    LabTooltip("Sets the blend with an edge-aware, multiscale brightness guide. Its largest scale is measured in original-image pixels.");
    ImGui::TextDisabled("Guide scale: %.0f source px", 16.f + 240.f * smoothness);
    float edgeProtection = context.recipe.localRange.edgeProtection;
    if (BareSliderFloat(
            "Edge Protection",
            "RawLabZoneEdge",
            &edgeProtection,
            0.0f,
            1.0f,
            "%.2f")) {
        context.recipe.localRange.edgeProtection = edgeProtection;
        changed = true;
    }
    float detailProtection = context.recipe.localRange.detailProtection;
    if (BareSliderFloat(
            "Detail Protection",
            "RawLabZoneDetail",
            &detailProtection,
            0.0f,
            1.0f,
            "%.2f")) {
        context.recipe.localRange.detailProtection = detailProtection;
        changed = true;
    }
    bool colorTarget = context.recipe.localRange.colorMaskEnabled;
    if (ImGui::Checkbox("Use Color Target", &colorTarget)) {
        context.recipe.localRange.colorMaskEnabled = colorTarget;
        changed = true;
    }
    bool regionMask = context.recipe.localRange.regionMaskEnabled;
    if (ImGui::Checkbox("Use Region Mask", &regionMask)) {
        context.recipe.localRange.regionMaskEnabled = regionMask;
        changed = true;
    }
    return changed;
}
