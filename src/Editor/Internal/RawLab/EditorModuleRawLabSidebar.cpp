#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include <cstdint>

using namespace Stack::Editor::RawLabInternal;
using RawCurveGraphUiState = Stack::EditorModuleTypes::RawCurveGraphUiState;

namespace {
int RawLabToolIndex(Stack::EditorModuleTypes::RawLabTool tool) {
    using namespace Stack::EditorModuleTypes;
    if (tool == RawLabTool::MultiFrame) return kRawLabBracketingIndex;
    tool = RawLabDrawerTool(tool);
    for (int i = 0; i < kRawLabBracketingIndex; ++i)
        if (kRawLabDrawerTools[i] == tool) return i;
    return -1;
}
}

bool EditorModule::IsRawBracketModeActive() const {
    return m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame;
}

void EditorModule::RequestRawLabTool(RawLabTool tool) {
    if (RawLabToolIndex(tool) >= 0) m_RequestedRawLabTool = tool;
}

void EditorModule::RequestRawLabToolIndex(int index) {
    using namespace Stack::EditorModuleTypes;
    if (index < 0 || index > kRawLabBracketingIndex) return;
    if (index == kRawLabBracketingIndex) {
        RequestRawLabTool(RawLabTool::MultiFrame);
        return;
    }
    RawLabTool tool = kRawLabDrawerTools[index];
    const RawLabTool current = IsRawBracketModeActive()
        ? m_RawLabLastEditTool : m_RawWorkspaceLabUi.activeTool;
    if (tool == RawLabTool::Zones) {
        tool = RawLabDrawerTool(current) == RawLabTool::Zones
            ? current : m_RawWorkspaceLabUi.lastCurvesTool;
    } else if (tool == RawLabTool::Color) {
        tool = RawLabDrawerTool(current) == RawLabTool::Color
            ? current : m_RawWorkspaceLabUi.lastColorTool;
    } else if (tool == RawLabTool::Denoise && current == RawLabTool::RgbDenoise) {
        tool = RawLabTool::RgbDenoise;
    }
    RequestRawLabTool(tool);
}

void EditorModule::RequestRawBracketMode(bool bracket) {
    if (Stack::Workspace::IsPreview() || !FinishWorkspaceInteraction()) return;
    CloseRawWorkspaceGalleryWorkspace();
    if (bracket == IsRawBracketModeActive()) return;
    const int editIndex = RawLabToolIndex(m_RawLabLastEditTool);
    RequestRawLabTool(bracket ? RawLabTool::MultiFrame :
        (editIndex >= 0 && editIndex < Stack::EditorModuleTypes::kRawLabBracketingIndex
            ? m_RawLabLastEditTool : RawLabTool::Light));
}

void EditorModule::ApplyRequestedRawLabTool(RawWorkspaceEditContext& context) {
    if (!m_RequestedRawLabTool) return;
    const RawLabTool tool = *m_RequestedRawLabTool;
    m_RequestedRawLabTool.reset();
    if(!Stack::Workspace::IsPreview()&&tool!=m_RawWorkspaceLabUi.activeTool&&
        RequestAutoBracketForeground("change editing tools",[this,tool]{RequestRawLabTool(tool);}))return;
    const bool multiFrameProject = IsMultiFrameRawProjectActive() &&
        m_Project->snapshot;
    const Stack::Project::MultiFrameSourceSet* activeSourceSet =
        multiFrameProject
        ? Stack::Project::FindSourceSet(
            *m_Project->snapshot,
            m_Project->snapshot->activeSourceSetId)
        : nullptr;
    const bool activeHdr = activeSourceSet && activeSourceSet->operationIntent ==
        Stack::Project::MultiFrameOperationIntent::RawBurstHdr;
    const bool currentMultiFrameResult = multiFrameProject && (activeHdr
        ? (m_HdrAdoptedRawResult && m_HdrAdoptedRawResult->rawData &&
            m_HdrAdoptedRawResult->projectId == m_Project->snapshot->projectId &&
            m_HdrAdoptedRawResult->sourceSetId == m_Project->snapshot->activeSourceSetId &&
            (IsBracketingActive() || m_HdrAdoptedRawResult->inputRevision == m_Project->snapshot->hdrInputRevision))
        : (m_MfdAdoptedRawResult && m_MfdAdoptedRawResult->rawData &&
            m_MfdAdoptedRawResult->projectId == m_Project->snapshot->projectId &&
            m_MfdAdoptedRawResult->sourceSetId == m_Project->snapshot->activeSourceSetId &&
            m_MfdAdoptedRawResult->inputRevision == m_Project->snapshot->mfdInputRevision));
    const auto activateTool = [&](RawLabTool tool) {
        if (m_RawWorkspaceLabUi.activeTool == tool) {
            return;
        }
        const RawLabTool previousTool = m_RawWorkspaceLabUi.activeTool;
        m_Project->rawLayers.EndGesture();
        if (m_RawWorkspaceAdaptiveGestureActive) NoteRawWorkspaceRecipePreviewEdit(false);
        const auto rememberGroupedTool = [&](RawLabTool selected) {
            if (Stack::EditorModuleTypes::RawLabDrawerTool(selected) == RawLabTool::Zones)
                m_RawWorkspaceLabUi.lastCurvesTool = selected;
            else if (Stack::EditorModuleTypes::RawLabDrawerTool(selected) == RawLabTool::Color)
                m_RawWorkspaceLabUi.lastColorTool = selected;
        };
        rememberGroupedTool(previousTool);
        m_RawWorkspaceLabUi.calibrationGestureActive = false;
        if (previousTool != RawLabTool::MultiFrame)
            m_RawLabLastEditTool = previousTool;
        m_RawWorkspaceLabUi.zonesCurveGraph.draggingExposure = false;
        m_RawWorkspaceLabUi.sceneToneDraggingPoint = -1;
        if (previousTool == RawLabTool::Zones) {
            if (m_RawWorkspaceLabUi.zoneAreas.active) {
                ResolveRawWorkspaceInteractionDraft(false);
                m_RawWorkspaceLabUi.zoneAreas.active = false;
            }
            m_RawWorkspaceLabUi.zoneAreas.history.Observe(context.recipe.localRange.areas, false);
            for (auto& entry : m_RawWorkspaceLabUi.zoneAreas.graphs) entry.second.interaction = {};
            ClearCurveSegmentSelection(
                m_RawWorkspaceLabUi.zonesCurveGraph,
                m_RawWorkspaceLabUi.selectedZonePoint);
        } else if (previousTool == RawLabTool::Tone) {
            for (RawCurveGraphUiState& graphState :
                 m_RawWorkspaceLabUi.toneCurveGraphs) {
                ClearCurveSegmentSelection(
                    graphState,
                    m_RawWorkspaceLabUi.selectedTonePoint);
            }
        }
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
            m_RawWorkspaceLocalRangeTargetMode) {
            m_RawWorkspaceLocalRangeOverlayMode =
                m_RawWorkspaceLocalRangeTargetPreviousOverlayMode;
            ClearRawWorkspaceLocalRangeOverlayState();
            ClearRawWorkspaceLocalRangeTargetState(false);
            if (!m_RawWorkspaceLocalRangeOverlayMode.empty() &&
                m_RawWorkspaceLocalRangeOverlayMode != "none") {
                // Restoring a non-final overlay still requires graph
                // execution even when the new tool's scope input is
                // available from the settled readback cache.
                MarkRenderRefreshDirty();
            }
        }
        if (previousTool == RawLabTool::Color) {
            m_RawWorkspaceLabUi.colorWarpProvisionalPinValid = false;
            m_RawWorkspaceLabUi.colorWarpProvisionalSceneEvValid = false;
            m_RawWorkspaceLabUi.colorWarpDraggingProvisional = false;
            m_RawWorkspaceLabUi.colorWarpPhotoHoverValid = false;
        }
        m_RawWorkspaceLabUi.activeTool = tool;
        rememberGroupedTool(tool);
        if (tool != RawLabTool::MultiFrame)
            m_RawLabLastEditTool = tool;
        m_RawWorkspaceLabUi.gradientDrawShape = -1;
        m_RawWorkspaceLabUi.gradientDragHandle = -1;
        if(tool==RawLabTool::MultiFrame)OpenBracketingTool();
        if (tool == RawLabTool::Zones ||
            tool == RawLabTool::Tone ||
            tool == RawLabTool::Color) {
            if (!RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool()) {
                // The image presentation is already usable. Let the matching
                // scope packet start after the tool switch settles so opening
                // a surface never launches auxiliary GPU/readback work in the
                // same frame as the UI transition.
                m_RawWorkspaceAnalysisPending = true;
                m_RawWorkspaceAnalysisRequested = false;
                m_RawWorkspaceAnalysisQuietUntilTime =
                    ImGui::GetTime() + 0.25;
            }
        } else if (previousTool == RawLabTool::Zones ||
                   previousTool == RawLabTool::Tone ||
                   previousTool == RawLabTool::Color) {
            m_RawWorkspaceGraphScopeReadback = {};
        }
        SaveRawWorkspaceAppState();
    };

    const bool denoise = tool == RawLabTool::Denoise || tool == RawLabTool::RgbDenoise;
    if (tool == RawLabTool::MultiFrame || tool == RawLabTool::Transform || !multiFrameProject || currentMultiFrameResult || (denoise && !activeHdr))
        activateTool(tool);
}

int EditorModule::GetRawLabToolIndex() const {
    return RawLabToolIndex(m_RawWorkspaceLabUi.activeTool);
}
