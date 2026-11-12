#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Editor/NodeEditors/LightBandEditor.h"
#include "Editor/NodeEditors/PhotographicPanelEditors.h"
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
    const RawDevelopmentGraphScopeReadback emptyScope;
    const auto& inputScope=context.inputAnalysis ? *context.inputAnalysis :
        context.presentation==Stack::Editor::NodeEditorPresentation::Panel ? emptyScope : m_RawWorkspaceGraphScopeReadback;
    const auto inputRevision=context.inputAnalysis ? context.inputAnalysisRevision :
        context.presentation==Stack::Editor::NodeEditorPresentation::Panel ? 0 : m_RawWorkspaceGraphScopeReadbackRevision;
    auto& graph = m_RawWorkspaceLabUi.zonesCurveGraph;
    auto& history = m_RawWorkspaceLabUi.exposureHistory;
    const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),context.rawAdjustmentLayerId);
    const auto* exposureNode = context.nodeTarget ? ResolveNodeEditorTarget(*context.nodeTarget) :
        layer ? Stack::Project::FindRawOperation(*layer,
            Stack::RawRecipe::GraphOperationKind::LocalEv, context.rawOperationUuid) : nullptr;
    if(exposureNode && (exposureNode->kind!=EditorNodeGraph::NodeKind::RawOperation ||
        exposureNode->rawOperation.kind!=Stack::RawRecipe::GraphOperationKind::LocalEv))exposureNode=nullptr;
    const bool canAdjustExposure = exposureNode && exposureNode->rawOperation.enabled &&
        !context.IsParameterDriven("ev");
    const auto exposureUuid = exposureNode ? exposureNode->instanceUuid : context.rawOperationUuid;
    // The ordinary Local EV surface already owns this node's draft. Legacy
    // entry points without a target can still read their separate instance.
    float exposureEv = !context.nodeTarget && layer && exposureNode && exposureUuid != context.rawOperationUuid
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
                inputScope,
                inputScope.valid ? inputRevision : 0, localCurve.minEv,
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

    }
    {
        if (ShowsRawLabGraph(section)) {
            const float minimumEv = context.recipe.localRange.minEv;
            const float maximumEv = context.recipe.localRange.maxEv;
            const float middleGrey = std::max(0.000001f, context.recipe.localRange.middleGrey);


            constexpr float graphHorizontalInset = 8.0f;
            const float surfaceStartX = ImGui::GetCursorPosX();
            ImVec2 graphSize = ImGui::GetContentRegionAvail();
            graphSize.y = std::max(12.f,std::min(graphSize.y,graphSize.x*.75f));
            graphSize.x = std::max(1.0f, graphSize.x - graphHorizontalInset * 2.0f);
            ImGui::SetCursorPosX(surfaceStartX + graphHorizontalInset);
            Stack::Editor::LightBandViewState fallbackBandView;
            fallbackBandView.advanced=true;
            auto& bandView=context.nodeTarget && context.editorState ? context.State<Stack::Editor::LightBandViewState>() : fallbackBandView;
            graphSize.y=std::min(graphSize.y,180.f*ImGui::GetFontSize()/13.f);
            if(context.nodeTarget && context.editorState)ImGui::Checkbox("Advanced curve",&bandView.advanced);
            if(!bandView.advanced) {
                auto bandRange=inputScope.lightRange;
                if(bandRange.valid) {
                    const float shift=context.recipe.LocalEvInputExposureEv()-inputScope.inputExposureEv;
                    bandRange.lowEv+=shift;bandRange.highEv+=shift;
                }
                changed|=Stack::Editor::DrawLightBandGraph(context,bandRange,graphSize);
                changed|=Stack::Editor::DrawLightPanel(*this,context);
                return changed;
            }
            const auto& histogram = m_RawFloatingSurface.histogram.Zones(
                inputScope,
                inputScope.valid ? inputRevision : 0,
                minimumEv,
                maximumEv,
                middleGrey);
            ImGui::TextDisabled("Additive correction; Light sliders remain independent");
            bool curveChanged = false;
            if(context.nodeTarget && context.editorState) {
                auto& state=context.State<CurveCanvasState>();
                auto points=Stack::RawRecipe::RawLocalRangeBezierPoints(localRange);
                float normalization=1.f;
                curveChanged=DrawCurveCanvas("LocalEV",points,localRange.curveAmount,state,false,
                    histogram,graphSize,12,canAdjustExposure?&exposureEv:nullptr,{},false,
                    &normalization,false,true,
                    nullptr,inputRevision,{true,false,true,minimumEv,maximumEv});
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
    }
    ImGui::Spacing();
    if (ShowsRawLabSettings(section)) {
        constexpr float strengthHorizontalInset = 8.0f;
        if(context.presentation==Stack::Editor::NodeEditorPresentation::Panel) {
            ImGui::BeginDisabled(context.IsParameterDriven("strength"));
            changed|=Stack::Editor::PanelRoller("Strength","strength",localRange.strength,0,1,.005f,1,"%.2f",context.State<Stack::Editor::PanelControlState>());
            ImGui::EndDisabled();
            return changed;
        }
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
    if(context.presentation==Stack::Editor::NodeEditorPresentation::Panel)
        return Stack::Editor::DrawLightPanelSettings(*this,context);
    bool changed = context.nodeTarget && context.editorState ? Stack::Editor::DrawLightRangeSettings(context,m_RawWorkspaceGraphScopeReadback.lightRange,m_RawWorkspaceGraphScopeReadback.inputExposureEv) : false;
    ImGui::TextWrapped("Exposure follows Light with gentle highlight protection. Tonal ranges adapt automatically to the incoming image.");
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
